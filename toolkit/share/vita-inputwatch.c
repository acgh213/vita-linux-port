#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PATH_SIZE 512
#define TEXT_SIZE 256
#define DEFAULT_DURATION_MS 1000U

struct input_device {
    unsigned int number;
    char name[TEXT_SIZE];
    char phys[TEXT_SIZE];
    char path[PATH_SIZE];
};

struct counts {
    unsigned int events_seen;
    unsigned int key_presses;
    unsigned int key_releases;
    unsigned int key_repeats;
    unsigned int abs_events;
    unsigned int syn_reports;
};

static int path_join(char *out, size_t size, const char *root, const char *suffix) {
    int written = snprintf(out, size, "%s%s", root, suffix);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

static int read_text(const char *path, char *out, size_t size) {
    int fd;
    ssize_t length;
    size_t end;

    if (size == 0)
        return -1;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    length = read(fd, out, size - 1U);
    close(fd);
    if (length < 0)
        return -1;
    out[length] = '\0';
    end = (size_t)length;
    while (end > 0 && (out[end - 1U] == '\n' || out[end - 1U] == '\r'))
        out[--end] = '\0';
    return end > 0 ? 0 : -1;
}

static int parse_positive(const char *text, unsigned int *value) {
    char *end;
    unsigned long parsed;

    if (!text || !*text)
        return -1;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > UINT32_MAX)
        return -1;
    *value = (unsigned int)parsed;
    return 0;
}

static int find_device(const char *sysroot, const char *devroot,
                       const char *wanted_phys, struct input_device *device) {
    unsigned int number;

    for (number = 0; number < 128U; ++number) {
        char suffix[64];
        char sys_event[PATH_SIZE];
        char phys_path[PATH_SIZE];
        char name_path[PATH_SIZE];
        char phys[TEXT_SIZE];

        if (snprintf(suffix, sizeof(suffix), "/class/input/event%u", number) < 0 ||
            path_join(sys_event, sizeof(sys_event), sysroot, suffix) != 0)
            return -1;
        if (access(sys_event, F_OK) != 0)
            continue;
        if (path_join(phys_path, sizeof(phys_path), sys_event, "/device/phys") != 0 ||
            read_text(phys_path, phys, sizeof(phys)) != 0 ||
            strcmp(phys, wanted_phys) != 0)
            continue;
        if (path_join(name_path, sizeof(name_path), sys_event, "/device/name") != 0)
            return -1;
        memset(device, 0, sizeof(*device));
        device->number = number;
        snprintf(device->phys, sizeof(device->phys), "%s", phys);
        if (read_text(name_path, device->name, sizeof(device->name)) != 0)
            snprintf(device->name, sizeof(device->name), "UNKNOWN");
        if (snprintf(device->path, sizeof(device->path), "%s/input/event%u",
                     devroot, number) < 0)
            return -1;
        return 0;
    }
    return -1;
}

static int elapsed_ms(const struct timespec *start) {
    struct timespec now;
    long long seconds;
    long nanoseconds;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    seconds = (long long)now.tv_sec - (long long)start->tv_sec;
    nanoseconds = now.tv_nsec - start->tv_nsec;
    return (int)(seconds * 1000LL + nanoseconds / 1000000L);
}

static void print_event(unsigned int index, const struct input_event *event,
                        int machine) {
    if (machine) {
        printf("event_%u_type=%u\nevent_%u_code=%u\nevent_%u_value=%d\n",
               index, (unsigned int)event->type, index, (unsigned int)event->code,
               index, event->value);
    } else {
        printf("event %u: type=%u code=%u value=%d\n", index,
               (unsigned int)event->type, (unsigned int)event->code, event->value);
    }
    fflush(stdout);
}

static void count_event(const struct input_event *event, struct counts *counts) {
    counts->events_seen++;
    if (event->type == EV_KEY) {
        if (event->value == 1)
            counts->key_presses++;
        else if (event->value == 0)
            counts->key_releases++;
        else if (event->value == 2)
            counts->key_repeats++;
    } else if (event->type == EV_ABS) {
        counts->abs_events++;
    } else if (event->type == EV_SYN && event->code == SYN_REPORT) {
        counts->syn_reports++;
    }
}

static void usage(void) {
    printf("usage: vita-inputwatch [--machine] [--phys ID] "
           "[--duration-ms N] [--max-events N]\n");
}

int main(int argc, char **argv) {
    const char *sysroot = getenv("VITA_INPUTWATCH_SYSFS_ROOT");
    const char *devroot = getenv("VITA_INPUTWATCH_DEV_ROOT");
    const char *wanted_phys = "vita_syscon_buttons";
    struct input_device device;
    struct counts counts;
    struct timespec started;
    unsigned char pending[sizeof(struct input_event)];
    unsigned int duration_ms = DEFAULT_DURATION_MS;
    unsigned int max_events = 0;
    size_t pending_length = 0;
    int machine = 0;
    int descriptor;
    int status = 0;
    int index;

    if (!sysroot)
        sysroot = "/sys";
    if (!devroot)
        devroot = "/dev";
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--machine") == 0) {
            machine = 1;
        } else if (strcmp(argv[index], "--phys") == 0 && index + 1 < argc) {
            wanted_phys = argv[++index];
        } else if (strcmp(argv[index], "--duration-ms") == 0 && index + 1 < argc) {
            if (parse_positive(argv[++index], &duration_ms) != 0) {
                fprintf(stderr, "vita-inputwatch: invalid duration\n");
                return 2;
            }
        } else if (strcmp(argv[index], "--max-events") == 0 && index + 1 < argc) {
            if (parse_positive(argv[++index], &max_events) != 0) {
                fprintf(stderr, "vita-inputwatch: invalid max-events\n");
                return 2;
            }
        } else if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }
    if (find_device(sysroot, devroot, wanted_phys, &device) != 0) {
        fprintf(stderr, "vita-inputwatch: no input device with phys=%s\n", wanted_phys);
        return 1;
    }
    descriptor = open(device.path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) {
        fprintf(stderr, "vita-inputwatch: cannot open %s: %s\n",
                device.path, strerror(errno));
        return 1;
    }
    memset(&counts, 0, sizeof(counts));
    if (clock_gettime(CLOCK_MONOTONIC, &started) != 0)
        memset(&started, 0, sizeof(started));
    if (machine) {
        printf("schema=1\ninput_read_only=1\nselected_event=event%u\n"
               "selected_device=/dev/input/event%u\nselected_name=%s\n"
               "selected_phys=%s\nduration_ms=%u\nmax_events=%u\n",
               device.number, device.number, device.name, device.phys,
               duration_ms, max_events);
    } else {
        printf("watching event%u (%s, phys=%s)\n", device.number,
               device.name, device.phys);
    }
    fflush(stdout);
    for (;;) {
        struct pollfd poll_descriptor = { descriptor, POLLIN, 0 };
        int remaining = (int)duration_ms - elapsed_ms(&started);
        int poll_result;

        if (max_events > 0 && counts.events_seen >= max_events) {
            status = 0;
            break;
        }
        if (remaining <= 0) {
            status = 1;
            break;
        }
        poll_result = poll(&poll_descriptor, 1, remaining);
        if (poll_result < 0) {
            if (errno == EINTR)
                continue;
            status = 1;
            break;
        }
        if (poll_result == 0) {
            status = 1;
            break;
        }
        if (poll_descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (poll_descriptor.revents & POLLIN) {
                /* Read below so a final regular-file record is not lost. */
            } else {
                status = 1;
                break;
            }
        }
        {
            ssize_t received = read(descriptor, pending + pending_length,
                                     sizeof(pending) - pending_length);
            if (received == 0) {
                status = pending_length == 0 ? 0 : 1;
                break;
            }
            if (received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                status = 1;
                break;
            }
            pending_length += (size_t)received;
        }
        while (pending_length == sizeof(struct input_event)) {
            struct input_event event;
            memcpy(&event, pending, sizeof(event));
            pending_length = 0;
            count_event(&event, &counts);
            print_event(counts.events_seen, &event, machine);
            if (max_events > 0 && counts.events_seen >= max_events)
                break;
        }
    }
    close(descriptor);
    if (machine) {
        printf("status=%s\nevents_seen=%u\nkey_presses=%u\nkey_releases=%u\n"
               "key_repeats=%u\nabs_events=%u\nsyn_reports=%u\n",
               status == 0 ? "complete" : "timeout", counts.events_seen,
               counts.key_presses, counts.key_releases, counts.key_repeats,
               counts.abs_events, counts.syn_reports);
    } else {
        printf("status=%s events=%u key_presses=%u key_releases=%u "
               "key_repeats=%u abs_events=%u syn_reports=%u\n",
               status == 0 ? "complete" : "timeout", counts.events_seen,
               counts.key_presses, counts.key_releases, counts.key_repeats,
               counts.abs_events, counts.syn_reports);
    }
    return status == 0 ? 0 : 1;
}
