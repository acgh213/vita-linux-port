#define _GNU_SOURCE
#include <cart/input.h>

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

#define CANDIDATE_CAP 16

/* ------------------------------------------------------------------ *
 * Default syscall operations                                          *
 * ------------------------------------------------------------------ */

static const size_t BIT_BYTES_KEY = 96; /* covers codes 0..767 */
static const size_t BIT_BYTES_ABS = 8;  /* ABS_MAX < 64 */
static const size_t STATE_BYTES_KEY = 96;

static unsigned long evreq(unsigned nr, size_t bytes)
{
    return (2UL << 30) | ((bytes & 0x3FFF) << 16) | ('E' << 8) | nr;
}

static int default_open(const char *path, int flags)
{
    return open(path, flags);
}

static int default_close(int fd)
{
    return close(fd);
}

static ssize_t default_read(int fd, void *buffer, size_t count)
{
    return read(fd, buffer, count);
}

static ssize_t default_ioctl_name(int fd, char *out, size_t out_size)
{
    int written;

    if (out == NULL || out_size == 0)
        return -1;
    written = ioctl(fd, EVIOCGNAME(out_size - 1), out);
    if (written < 0)
        return -1;
    if ((size_t)written >= out_size)
        written = (int)out_size - 1;
    out[written] = '\0';
    return out[0] == '\0' ? 0 : (ssize_t)written;
}

static ssize_t default_ioctl_key_state(int fd, uint8_t *buffer, size_t bytes)
{
    memset(buffer, 0, bytes);
    if (ioctl(fd, evreq(0x18, bytes), buffer) < 0)
        return -1;
    return (ssize_t)bytes;
}

static int default_ioctl_bit(int fd, unsigned type, uint8_t *buffer,
                             size_t bytes)
{
    memset(buffer, 0, bytes);
    if (ioctl(fd, evreq(0x20 + type, bytes), buffer) < 0)
        return -1;
    return 0;
}

static int default_ioctl_abs(int fd, unsigned axis, int32_t *value,
                             int32_t *minimum, int32_t *maximum,
                             int32_t *fuzz, int32_t *flat,
                             int32_t *resolution)
{
    struct input_absinfo info;

    if (ioctl(fd, EVIOCGABS(axis), &info) < 0)
        return -1;
    *value = info.value;
    *minimum = info.minimum;
    *maximum = info.maximum;
    *fuzz = info.fuzz;
    *flat = info.flat;
    *resolution = info.resolution;
    return 0;
}

const struct cart_input_ops cart_input_default_ops = {
    .open = default_open,
    .close = default_close,
    .read = default_read,
    .ioctl_name = default_ioctl_name,
    .ioctl_key_state = default_ioctl_key_state,
    .ioctl_bit = default_ioctl_bit,
    .ioctl_abs = default_ioctl_abs,
};

/* ------------------------------------------------------------------ *
 * Helpers                                                             *
 * ------------------------------------------------------------------ */

static int bit_test(const uint8_t *buffer, unsigned bit)
{
    return (buffer[bit >> 3] >> (bit & 7)) & 1;
}

static void path_of(char *out, size_t out_size, const char *base,
                    const char *leaf)
{
    snprintf(out, out_size, "%s/%s", base, leaf);
}

static int read_trimmed_line(const char *path, char *out, size_t out_size)
{
    FILE *file = fopen(path, "r");
    size_t len;

    if (file == NULL || out_size == 0) {
        if (file != NULL)
            fclose(file);
        return -1;
    }
    len = fread(out, 1, out_size - 1, file);
    fclose(file);
    out[len] = '\0';
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
        out[--len] = '\0';
    return 0;
}

static int event_node_number(const char *name, unsigned long *number)
{
    static const char prefix[] = "event";
    const char *digits = name + sizeof(prefix) - 1;
    char *end;

    if (strncmp(name, prefix, sizeof(prefix) - 1) != 0 || *digits == '\0')
        return 0;
    for (const char *cursor = digits; *cursor != '\0'; cursor++)
        if (*cursor < '0' || *cursor > '9')
            return 0;
    errno = 0;
    *number = strtoul(digits, &end, 10);
    if (errno != 0 || *end != '\0' || *number > INT_MAX)
        return 0;
    return 1;
}

static const unsigned AXIS_CODES[CART_INPUT_AXIS_COUNT] = {
    ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y,
};

static int axis_slot(unsigned code)
{
    for (unsigned slot = 0; slot < CART_INPUT_AXIS_COUNT; slot++)
        if (AXIS_CODES[slot] == code)
            return (int)slot;
    return -1;
}

/* ------------------------------------------------------------------ *
 * Authoritative probing (on an open fd)                               *
 * ------------------------------------------------------------------ */

static int probe_is_gamepad(const struct cart_input_ops *ops, int fd)
{
    uint8_t key_bits[BIT_BYTES_KEY];
    uint8_t abs_bits[BIT_BYTES_ABS];
    static const uint16_t buttons[] = {
        BTN_SOUTH, BTN_EAST, BTN_C, BTN_NORTH, BTN_WEST,
        BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE,
    };
    static const uint16_t sticks[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY };
    unsigned hits = 0;
    unsigned stick_axes = 0;

    if (ops->ioctl_bit(fd, 1, key_bits, sizeof(key_bits)) != 0 ||
        ops->ioctl_bit(fd, 3, abs_bits, sizeof(abs_bits)) != 0)
        return 0;
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
        hits += (unsigned)bit_test(key_bits, buttons[i]);
    for (size_t i = 0; i < sizeof(sticks) / sizeof(sticks[0]); i++)
        stick_axes += (unsigned)bit_test(abs_bits, sticks[i]);
    return hits >= 4 && stick_axes >= 2;
}

static int probe_is_action_keyboard(const struct cart_input_ops *ops, int fd)
{
    uint8_t key_bits[BIT_BYTES_KEY];

    if (ops->ioctl_bit(fd, 1, key_bits, sizeof(key_bits)) != 0)
        return 0;
    return bit_test(key_bits, KEY_ESC) || bit_test(key_bits, KEY_Q);
}

/* Snapshot calibration from the opened fd. Never fails a probe on its
 * own: axes that cannot be read are marked invalid and skipped. */
static void probe_snapshot_axes(struct cart_input_source *source,
                                const struct cart_input_ops *ops, int fd)
{
    uint8_t abs_bits[BIT_BYTES_ABS];

    source->held_keys = 0;
    memset(source->last_raw, 0, sizeof(source->last_raw));
    if (ops->ioctl_bit(fd, 3, abs_bits, sizeof(abs_bits)) != 0) {
        memset(source->axis_valid, 0, sizeof(source->axis_valid));
        return;
    }
    for (unsigned slot = 0; slot < CART_INPUT_AXIS_COUNT; slot++) {
        int32_t value, minimum, maximum, fuzz, flat, resolution;

        if (!bit_test(abs_bits, AXIS_CODES[slot])) {
            source->axis_valid[slot] = 0;
            continue;
        }
        if (ops->ioctl_abs(fd, AXIS_CODES[slot], &value, &minimum,
                           &maximum, &fuzz, &flat, &resolution) != 0) {
            source->axis_valid[slot] = 0;
            continue;
        }
        {
            int32_t span = maximum - minimum;
            int32_t pct = CART_INPUT_DEFAULT_DEADZONE_PCT;

            if (span > 0 && flat > 0) {
                int32_t suggested = flat * 200 / span;

                if (suggested > pct && suggested <= 50)
                    pct = suggested;
            }
            source->axis[slot].min = minimum;
            source->axis[slot].max = maximum;
            source->axis[slot].deadzone_pct = pct;
            source->axis_valid[slot] = 1;
            source->last_raw[slot] = value;
        }
    }
}

/* Rank classification of an OPEN node. sysfs_rank carries the exact-string
 * metadata hint (4/3/0). Probe verdict is mandatory: no fd evidence means
 * no admission, whatever the strings claim. */
static int classify_source(struct cart_input_source *scratch,
                           const struct cart_input_ops *ops, int fd,
                           int sysfs_rank)
{
    char name[CART_INPUT_NAME_MAX];
    int probed;

    if (ops->ioctl_name(fd, name, sizeof(name)) <= 0)
        return 0;
    {
        size_t name_len = strlen(name);

        if (name_len >= sizeof(scratch->name))
            name_len = sizeof(scratch->name) - 1;
        memcpy(scratch->name, name, name_len);
        scratch->name[name_len] = '\0';
    }

    probed = 0;
    if (probe_is_gamepad(ops, fd))
        probed = 2;
    else if (probe_is_action_keyboard(ops, fd))
        probed = 1;
    if (probed == 0)
        return 0;
    /* Admitted: calibrate axes now while we own the fd. */
    probe_snapshot_axes(scratch, ops, fd);
    return sysfs_rank > probed ? sysfs_rank : probed;
}

/* ------------------------------------------------------------------ *
 * Deterministic ordering                                              *
 * ------------------------------------------------------------------ */

int _cart_input_plan(struct cart_input_candidate *candidates, size_t count)
{
    for (size_t i = 1; i < count; i++) {
        struct cart_input_candidate key = candidates[i];
        size_t j = i;

        while (j > 0 &&
               (candidates[j - 1].rank < key.rank ||
                (candidates[j - 1].rank == key.rank &&
                 candidates[j - 1].number > key.number))) {
            candidates[j] = candidates[j - 1];
            j--;
        }
        candidates[j] = key;
    }
    return (int)(count > CANDIDATE_CAP ? CANDIDATE_CAP : count);
}

/* ------------------------------------------------------------------ *
 * Discovery core                                                      *
 * ------------------------------------------------------------------ */

static void reset_source(struct cart_input_source *source)
{
    memset(source, 0, sizeof(*source));
    source->fd = -1;
    source->state = CART_INPUT_ABSENT;
}

struct discovery {
    struct cart_input *input;
    const struct cart_input_ops *ops;
};

static int slot_empty(const struct cart_input *input, size_t index)
{
    return input->source[index].fd < 0;
}

static int first_free_slot(struct cart_input *input)
{
    for (size_t i = 0; i < CART_INPUT_MAX_SOURCES; i++)
        if (slot_empty(input, i))
            return (int)i;
    return -1;
}

static int discover_pass(struct cart_input *input,
                         const struct cart_input_ops *ops,
                         const char *sys_class_input, const char *dev_input)
{
    DIR *directory = opendir(sys_class_input);
    struct cart_input_candidate candidates[CANDIDATE_CAP];
    size_t candidate_count = 0;
    struct dirent *entry;
    int claimed = 0;

    if (directory == NULL)
        return errno == ENOENT ? 0 : -1;

    while ((entry = readdir(directory)) != NULL &&
           candidate_count < CANDIDATE_CAP) {
        unsigned long number = 0;
        char sys_dir[512];
        char dev_path[640];
        char phys_line[128];
        char name_line[CART_INPUT_NAME_MAX];
        int pre_opened;
        int sysfs_rank = 0;

        if (!event_node_number(entry->d_name, &number))
            continue;
        if (strlen(entry->d_name) >= CART_INPUT_NODE_MAX)
            continue;         /* eventN fits far below 32 either way */
        path_of(sys_dir, sizeof(sys_dir), sys_class_input, entry->d_name);

        /* Exact-STRING metadata hints (never parsed hex). */
        phys_line[0] = '\0';
        {
            char meta_path[768];

            path_of(meta_path, sizeof(meta_path), sys_dir, "device/phys");
            (void)read_trimmed_line(meta_path, phys_line,
                                    sizeof(phys_line));
            if (strcmp(phys_line, VITA_BUTTONS_PHYS) == 0)
                sysfs_rank = 4;

            path_of(meta_path, sizeof(meta_path), sys_dir, "device/name");
            (void)read_trimmed_line(meta_path, name_line,
                                    sizeof(name_line));
            if (sysfs_rank == 0 &&
                strcmp(name_line, VITA_BUTTONS_NAME) == 0)
                sysfs_rank = 3;
        }

        path_of(dev_path, sizeof(dev_path), dev_input, entry->d_name);
        pre_opened = ops->open(dev_path,
                               O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (pre_opened < 0)
            continue;             /* vanished or unusable; not fatal */

        {
            struct cart_input_source scratch;
            int classified;

            memset(&scratch, 0, sizeof(scratch));
            scratch.fd = -1;
            {
                size_t node_len = strlen(entry->d_name);

                memcpy(scratch.node, entry->d_name, node_len + 1);
            }
            classified = classify_source(&scratch, ops, pre_opened,
                                         sysfs_rank);
            if (classified == 0) {
                ops->close(pre_opened);
                continue;         /* probed junk; skip quietly */
            }

            /* The probe verdict IS the rank for non-Syscon nodes; Syscon
             * keeps its fixed-function precedence from metadata. */
            candidates[candidate_count].fd = pre_opened;
            candidates[candidate_count].rank =
                sysfs_rank > classified ? sysfs_rank : classified;
            candidates[candidate_count].number = number;
            memcpy(candidates[candidate_count].node, entry->d_name,
                   strlen(entry->d_name) + 1);
            candidates[candidate_count].prepared = scratch;
            candidate_count++;
        }
    }
    closedir(directory);

    (void)_cart_input_plan(candidates, candidate_count);

    for (size_t i = 0; i < candidate_count; i++) {
        struct cart_input_candidate *candidate = &candidates[i];
        struct cart_input_source *target;
        int slot = first_free_slot(input);

        if (slot < 0) {
            ops->close(candidate->fd);
            candidate->fd = -1;
            continue;
        }
        target = &input->source[slot];
        reset_source(target);
        *target = candidate->prepared;
        target->fd = candidate->fd;
        target->state = CART_INPUT_CONNECTED;
        target->generation++;
        candidate->fd = -1;
        claimed++;
    }

    for (size_t i = 0; i < candidate_count; i++) {
        struct cart_input_candidate *candidate = &candidates[i];
        int in_use = 0;

        if (candidate->fd < 0)
            continue;
        for (size_t s = 0; s < CART_INPUT_MAX_SOURCES; s++)
            if (input->source[s].fd == candidate->fd)
                in_use = 1;
        if (!in_use)
            ops->close(candidate->fd);
        candidate->fd = -1;
    }

    return claimed;
}

/* ------------------------------------------------------------------ *
 * Public lifecycle                                                    *
 * ------------------------------------------------------------------ */

int cart_input_init_ex(struct cart_input *input,
                       const struct cart_input_ops *ops,
                       const char *sys_class_input, const char *dev_input)
{
    if (input == NULL || ops == NULL || sys_class_input == NULL ||
        dev_input == NULL || ops->open == NULL || ops->close == NULL ||
        ops->read == NULL || ops->ioctl_name == NULL ||
        ops->ioctl_key_state == NULL || ops->ioctl_bit == NULL ||
        ops->ioctl_abs == NULL)
        return -1;
    memset(input, 0, sizeof(*input));
    input->ops = ops;
    for (size_t i = 0; i < CART_INPUT_MAX_SOURCES; i++)
        reset_source(&input->source[i]);
    return discover_pass(input, input->ops, sys_class_input, dev_input);
}

int cart_input_init(struct cart_input *input, const char *sys_class_input,
                    const char *dev_input)
{
    if (input == NULL)
        return -1;
    return cart_input_init_ex(input, &cart_input_default_ops,
                              sys_class_input, dev_input);
}

int cart_input_rescan(struct cart_input *input, const char *sys_class_input,
                      const char *dev_input)
{
    if (input == NULL || sys_class_input == NULL || dev_input == NULL)
        return -1;
    if (input->ops == NULL)
        input->ops = &cart_input_default_ops;
    return discover_pass(input, input->ops, sys_class_input, dev_input);
}

void cart_input_shutdown(struct cart_input *input)
{
    if (input == NULL)
        return;
    for (size_t i = 0; i < CART_INPUT_MAX_SOURCES; i++) {
        struct cart_input_source *source = &input->source[i];

        if (source->fd >= 0)
            input->ops->close(source->fd);
        reset_source(source);
    }
    input->pending.count = 0;
    for (size_t i = 0; i < CART_INPUT_MAX_SOURCES; i++)
        input->partial[i].len = 0;
}

/* ------------------------------------------------------------------ *
 * Poll                                                                *
 * ------------------------------------------------------------------ */

static void enqueue_action(struct cart_input *input,
                           enum cart_input_action action)
{
    if (action <= CART_INPUT_NONE || action > CART_INPUT_QUIT)
        return;
    if (input->pending.count <
        sizeof(input->pending.item) / sizeof(input->pending.item[0]))
        input->pending.item[input->pending.count++] = (uint8_t)action;
}

static void handle_disconnect(struct cart_input *input, size_t index)
{
    struct cart_input_source *source = &input->source[index];

    if (source->fd >= 0)
        input->ops->close(source->fd);
    source->fd = -1;
    source->state = CART_INPUT_ABSENT;
    source->generation++;
    memset(source->last_raw, 0, sizeof(source->last_raw));
    source->held_keys = 0;
    input->partial[index].len = 0;
}

static void process_key_event(struct cart_input *input, size_t index,
                              uint16_t code, int32_t value)
{
    struct cart_input_source *source = &input->source[index];

    if (code < 64) {
        if (value != 0)
            source->held_keys |= (uint64_t)1 << code;
        else
            source->held_keys &= ~((uint64_t)1 << code);
    }
    if (cart_input_key_emits_action(value))
        enqueue_action(input, (enum cart_input_action)cart_input_map_key(code));
}

static void resync_after_desync(struct cart_input *input, size_t index)
{
    struct cart_input_source *source = &input->source[index];
    uint8_t state[STATE_BYTES_KEY];
    ssize_t got =
        input->ops->ioctl_key_state(source->fd, state, sizeof(state));

    if (got >= 0) {
        uint64_t merged = 0;

        for (unsigned code = 0; code < 64; code++)
            merged |= (uint64_t)bit_test(state, code) << code;
        source->held_keys = merged;
    }
    for (unsigned slot = 0; slot < CART_INPUT_AXIS_COUNT; slot++) {
        int32_t value, minimum, maximum, fuzz, flat, resolution;

        if (!source->axis_valid[slot])
            continue;
        if (input->ops->ioctl_abs(source->fd, AXIS_CODES[slot], &value,
                                  &minimum, &maximum, &fuzz, &flat,
                                  &resolution) == 0)
            source->last_raw[slot] = value;
    }
}

static void consume_event(struct cart_input *input, size_t index,
                          const struct input_event *event)
{
    struct cart_input_source *source = &input->source[index];

    switch (event->type) {
    case EV_SYN:
        if (event->code == SYN_DROPPED &&
            source->state == CART_INPUT_CONNECTED)
            source->state = CART_INPUT_DESYNC;
        else if (event->code == SYN_REPORT &&
                 source->state == CART_INPUT_DESYNC) {
            resync_after_desync(input, index);
            source->state = CART_INPUT_CONNECTED;
        }
        break;
    case EV_KEY:
        if (source->state == CART_INPUT_CONNECTED)
            process_key_event(input, index, event->code, event->value);
        break;
    case EV_ABS: {
        int slot = axis_slot(event->code);

        if (slot >= 0 && source->state == CART_INPUT_CONNECTED)
            source->last_raw[slot] = event->value;
        break;
    }
    default:
        break;
    }
}

static void pump_source(struct cart_input *input, size_t index)
{
    struct cart_input_source *source = &input->source[index];
    uint8_t *buffer = input->partial[index].buf;
    size_t *length = &input->partial[index].len;

    for (;;) {
        while (*length >= sizeof(struct input_event)) {
            struct input_event event;

            memcpy(&event, buffer, sizeof(event));
            memmove(buffer, buffer + sizeof(event),
                    *length - sizeof(event));
            *length -= sizeof(event);
            consume_event(input, index, &event);
        }

        {
            ssize_t got = input->ops->read(source->fd, buffer + *length,
                                           sizeof(input->partial[index].buf)
                                           - *length);

            if (got > 0) {
                *length += (size_t)got;
                continue;
            }
            if (got == 0) {
                handle_disconnect(input, index);
                return;
            }
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            handle_disconnect(input, index);   /* ENODEV / EIO / other */
            return;
        }
    }
}

static void build_frame(struct cart_input *input,
                        struct cart_input_frame *frame)
{
    memset(frame, 0, sizeof(*frame));

    for (unsigned slot = 0; slot < CART_INPUT_AXIS_COUNT; slot++)
        frame->axis_owner[slot] = CART_INPUT_MAX_SOURCES;

    for (size_t i = 0; i < CART_INPUT_MAX_SOURCES; i++) {
        const struct cart_input_source *source = &input->source[i];

        if (source->state == CART_INPUT_ABSENT)
            continue;
        frame->connected_mask |= 1u << i;
        frame->generation_total += source->generation;
        frame->held_keys |= source->held_keys;

        for (unsigned slot = 0; slot < CART_INPUT_AXIS_COUNT; slot++) {
            struct cart_input_axis_range range = source->axis[slot];
            int normalized;

            if (!source->axis_valid[slot])
                continue;
            normalized = cart_input_axis_normalize(source->last_raw[slot],
                                                   &range);
            if (normalized == CART_INPUT_AXIS_INVALID)
                continue;
            /* Lower source index == higher-or-equal rank, and earlier
             * claimants win ties by contract: keep the first writer. */
            if (frame->axis_present[slot])
                continue;
            frame->axis[slot] = (int16_t)normalized;
            frame->axis_present[slot] = 1;
            frame->axis_owner[slot] = (uint8_t)i;
        }
    }
}

int cart_input_poll(struct cart_input *input, enum cart_input_action *action,
                    struct cart_input_frame *frame)
{
    if (input == NULL || action == NULL)
        return -1;
    *action = CART_INPUT_NONE;

    for (size_t i = 0; i < CART_INPUT_MAX_SOURCES; i++) {
        if (input->source[i].fd < 0)
            continue;
        if (input->source[i].state == CART_INPUT_DESYNC)
            continue;              /* swallow until SYN_REPORT arrives */
        pump_source(input, i);
    }

    if (input->pending.count > 0) {
        *action = (enum cart_input_action)input->pending.item[0];
        memmove(input->pending.item, input->pending.item + 1,
                input->pending.count - 1);
        input->pending.count--;
    }

    if (frame != NULL)
        build_frame(input, frame);
    return 0;
}
