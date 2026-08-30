#define _GNU_SOURCE
#include <cart/input.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VITA_BUTTONS_NAME "PlayStation Vita Buttons (Syscon)"
#define VITA_BUTTONS_PHYS "vita_syscon_buttons"

static int path_join(char *output, size_t output_size, const char *left,
                     const char *right)
{
    int written;

    written = snprintf(output, output_size, "%s/%s", left, right);
    if (written < 0 || (size_t)written >= output_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int read_line(const char *path, char *output, size_t output_size)
{
    FILE *file;
    size_t length;

    if (output_size < 2) {
        errno = EINVAL;
        return -1;
    }
    file = fopen(path, "rb");
    if (file == NULL)
        return -1;
    if (fgets(output, (int)output_size, file) == NULL) {
        int saved_errno = ferror(file) ? errno : EIO;

        fclose(file);
        errno = saved_errno;
        return -1;
    }
    length = strlen(output);
    if (length > 0 && output[length - 1] == '\n')
        output[--length] = '\0';
    if (length > 0 && output[length - 1] == '\r')
        output[--length] = '\0';
    if (!feof(file)) {
        int next = fgetc(file);

        if (next != EOF) {
            fclose(file);
            errno = EOVERFLOW;
            return -1;
        }
    }
    if (fclose(file) != 0)
        return -1;
    return 0;
}

static int event_number(const char *name, unsigned long *number)
{
    const char *digits;
    char *end;
    unsigned long parsed;

    if (strncmp(name, "event", 5) != 0)
        return 0;
    digits = name + 5;
    if (*digits == '\0')
        return 0;
    for (const char *cursor = digits; *cursor != '\0'; cursor++)
        if (!isdigit((unsigned char)*cursor))
            return 0;
    errno = 0;
    parsed = strtoul(digits, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > INT_MAX)
        return 0;
    *number = parsed;
    return 1;
}

static int capability_has_bit(const char *text, unsigned int bit)
{
    size_t length = strlen(text);
    unsigned int target_nibble = bit / 4U;
    unsigned int nibble = 0;

    while (length > 0) {
        unsigned char value;
        char digit = text[--length];

        if (isspace((unsigned char)digit))
            continue;
        if (digit >= '0' && digit <= '9')
            value = (unsigned char)(digit - '0');
        else if (digit >= 'a' && digit <= 'f')
            value = (unsigned char)(digit - 'a' + 10);
        else if (digit >= 'A' && digit <= 'F')
            value = (unsigned char)(digit - 'A' + 10);
        else
            return 0;
        if (nibble == target_nibble)
            return (value & (1U << (bit % 4U))) != 0;
        nibble++;
    }
    return 0;
}

static int candidate_rank(const char *name, const char *phys,
                          const char *key_capabilities)
{
    if (strcmp(phys, VITA_BUTTONS_PHYS) == 0)
        return 4;
    if (strcmp(name, VITA_BUTTONS_NAME) == 0)
        return 3;
    if (capability_has_bit(key_capabilities, BTN_A) ||
        capability_has_bit(key_capabilities, BTN_DPAD_UP) ||
        capability_has_bit(key_capabilities, BTN_START))
        return 2;
    if (capability_has_bit(key_capabilities, KEY_ESC) ||
        capability_has_bit(key_capabilities, KEY_Q))
        return 1;
    return 0;
}

int cart_input_discover(struct cart_input *input, const char *sys_class_input,
                        const char *dev_input)
{
    struct dirent *entry;
    DIR *directory;
    int best_fd = -1;
    int best_rank = 0;
    unsigned long best_number = ULONG_MAX;
    char best_path[CART_INPUT_PATH_MAX] = "";
    char best_name[CART_INPUT_NAME_MAX] = "";

    if (input == NULL || sys_class_input == NULL || dev_input == NULL) {
        errno = EINVAL;
        return -1;
    }
    input->fd = -1;
    input->path[0] = '\0';
    input->name[0] = '\0';

    directory = opendir(sys_class_input);
    if (directory == NULL)
        return errno == ENOENT ? 0 : -1;

    for (;;) {
        unsigned long number;
        char event_path[CART_INPUT_PATH_MAX];
        char device_path[CART_INPUT_PATH_MAX];
        char metadata_path[CART_INPUT_PATH_MAX];
        char name[CART_INPUT_NAME_MAX];
        char phys[CART_INPUT_NAME_MAX];
        char capabilities[256];
        char key_capabilities[256];
        int rank;
        int fd;

        errno = 0;
        entry = readdir(directory);
        if (entry == NULL)
            break;

        if (!event_number(entry->d_name, &number))
            continue;
        if (path_join(event_path, sizeof(event_path), sys_class_input,
                      entry->d_name) != 0 ||
            path_join(device_path, sizeof(device_path), event_path,
                      "device") != 0 ||
            path_join(metadata_path, sizeof(metadata_path), device_path,
                      "capabilities/ev") != 0 ||
            read_line(metadata_path, capabilities, sizeof(capabilities)) != 0 ||
            !capability_has_bit(capabilities, EV_KEY))
            continue;
        if (path_join(metadata_path, sizeof(metadata_path), device_path,
                      "name") != 0 ||
            read_line(metadata_path, name, sizeof(name)) != 0)
            continue;
        if (path_join(metadata_path, sizeof(metadata_path), device_path,
                      "phys") != 0 ||
            read_line(metadata_path, phys, sizeof(phys)) != 0)
            phys[0] = '\0';
        if (path_join(metadata_path, sizeof(metadata_path), device_path,
                      "capabilities/key") != 0 ||
            read_line(metadata_path, key_capabilities,
                      sizeof(key_capabilities)) != 0)
            continue;

        rank = candidate_rank(name, phys, key_capabilities);
        if (rank == 0)
            continue;
        if (rank < best_rank || (rank == best_rank && number >= best_number))
            continue;
        if (path_join(event_path, sizeof(event_path), dev_input,
                      entry->d_name) != 0)
            continue;
        fd = open(event_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (best_fd >= 0)
            close(best_fd);
        best_fd = fd;
        best_rank = rank;
        best_number = number;
        strcpy(best_path, event_path);
        strcpy(best_name, name);
    }
    {
        int scan_errno = errno;

        if (closedir(directory) != 0 && scan_errno == 0)
            scan_errno = errno;
        if (scan_errno != 0) {
            if (best_fd >= 0)
                close(best_fd);
            errno = scan_errno;
            return -1;
        }
    }

    if (best_fd < 0)
        return 0;
    input->fd = best_fd;
    strcpy(input->path, best_path);
    strcpy(input->name, best_name);
    return 1;
}

int cart_input_poll(struct cart_input *input, enum cart_input_action *action)
{
    struct input_event event;

    if (input == NULL || action == NULL) {
        errno = EINVAL;
        return -1;
    }
    *action = CART_INPUT_NONE;
    if (input->fd < 0)
        return 0;

    for (;;) {
        ssize_t bytes = read(input->fd, &event, sizeof(event));

        if (bytes == (ssize_t)sizeof(event)) {
            if (event.type != EV_KEY || event.value != 1)
                continue;
            if (event.code == KEY_ESC || event.code == KEY_Q) {
                *action = CART_INPUT_QUIT;
                return 0;
            }
            *action = CART_INPUT_NEXT;
            continue;
        }
        if (bytes == 0)
            return 0;
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        if (bytes >= 0)
            errno = EIO;
        return -1;
    }
}

void cart_input_close(struct cart_input *input)
{
    if (input == NULL)
        return;
    if (input->fd >= 0)
        close(input->fd);
    input->fd = -1;
    input->path[0] = '\0';
    input->name[0] = '\0';
}
