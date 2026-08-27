#define _GNU_SOURCE
#include <cart/input.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#define TEST_PATH_MAX 512
#define MAX_TEST_DEVICES 8
#define KEY_BITS_BYTES 96
#define ABS_BITS_BYTES 8

/* ------------------------------------------------------------------ *
 * Scripted syscall seam                                               *
 *                                                                     *
 * Discovery reads device/name + device/phys from the real filesystem,
 * but every probe runs through the injected ops. Scripted fds are
 * opaque tokens (2000 + index); fake_read forwards to a real pipe when
 * the test wires one up (event-pump test), EAGAIN otherwise.
 * ------------------------------------------------------------------ */

struct test_axis {
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t fuzz;
    int32_t flat;
    int32_t resolution;
};

struct test_device {
    char node[CART_INPUT_NODE_MAX];
    const char *name;
    uint8_t key_bits[KEY_BITS_BYTES];
    uint8_t abs_bits[ABS_BITS_BYTES];
    struct test_axis axis[CART_INPUT_AXIS_COUNT];
    int synthetic_fd;   /* opaque fd handed out by fake_open */
    int real_fd;        /* underlying fd forwarded by fake_read (-1 = none) */
    int name_fails;     /* ioctl_name reports failure (broken device) */
};

static struct test_device test_devices[MAX_TEST_DEVICES];
static size_t test_device_count;
static int next_synthetic_fd = 2000;

static void test_devices_reset(void)
{
    test_device_count = 0;
    next_synthetic_fd = 2000;
}

static struct test_device *add_device(const char *node, const char *name)
{
    struct test_device *device = &test_devices[test_device_count];

    CHECK(test_device_count < MAX_TEST_DEVICES);
    memset(device, 0, sizeof(*device));
    snprintf(device->node, sizeof(device->node), "%s", node);
    device->name = name;
    device->synthetic_fd = -1;
    device->real_fd = -1;
    test_device_count++;
    return device;
}

static void set_bit(uint8_t *bits, unsigned code)
{
    bits[code >> 3] |= (uint8_t)(1u << (code & 7));
}

static void vita_buttons_init(struct test_device *device)
{
    device->name = "PlayStation Vita Buttons (Syscon)";
    set_bit(device->key_bits, BTN_SOUTH);
    set_bit(device->key_bits, BTN_EAST);
    set_bit(device->key_bits, BTN_NORTH);
    set_bit(device->key_bits, BTN_WEST);
    set_bit(device->key_bits, BTN_SELECT);
    set_bit(device->key_bits, BTN_START);
    set_bit(device->abs_bits, ABS_X);
    set_bit(device->abs_bits, ABS_Y);
    for (unsigned slot = 0; slot < CART_INPUT_AXIS_COUNT; slot++) {
        device->axis[slot].value = 128;
        device->axis[slot].minimum = 0;
        device->axis[slot].maximum = 255;
    }
}

static struct test_device *device_by_fd(int fd)
{
    for (size_t i = 0; i < test_device_count; i++)
        if (test_devices[i].synthetic_fd == fd)
            return &test_devices[i];
    return NULL;
}

static int fake_open(const char *path, int flags)
{
    (void)flags;
    for (size_t i = 0; i < test_device_count; i++) {
        size_t node_len = strlen(test_devices[i].node);
        size_t path_len = strlen(path);

        if (path_len >= node_len + 1 &&
            strcmp(path + path_len - node_len, test_devices[i].node) == 0 &&
            path[path_len - node_len - 1] == '/') {
            test_devices[i].synthetic_fd = next_synthetic_fd++;
            return test_devices[i].synthetic_fd;
        }
    }
    return -1;
}

static int fake_close(int fd)
{
    struct test_device *device = device_by_fd(fd);

    if (device != NULL) {
        if (device->real_fd >= 0) {
            close(device->real_fd);
            device->real_fd = -1;
        }
        device->synthetic_fd = -1;
    }
    return 0;
}

static ssize_t fake_read(int fd, void *buffer, size_t count)
{
    struct test_device *device = device_by_fd(fd);

    if (device == NULL || device->real_fd < 0) {
        errno = EAGAIN;
        return -1;
    }
    return read(device->real_fd, buffer, count);
}

static ssize_t fake_ioctl_name(int fd, char *out, size_t out_size)
{
    struct test_device *device = device_by_fd(fd);

    if (device == NULL || device->name_fails || out == NULL || out_size == 0)
        return -1;
    snprintf(out, out_size, "%s", device->name);
    return (ssize_t)strlen(out);
}

static ssize_t fake_ioctl_key_state(int fd, uint8_t *buffer, size_t bytes)
{
    struct test_device *device = device_by_fd(fd);

    if (device == NULL || buffer == NULL)
        return -1;
    memset(buffer, 0, bytes);
    return (ssize_t)bytes;
}

static int fake_ioctl_bit(int fd, unsigned type, uint8_t *buffer,
                          size_t bytes)
{
    struct test_device *device = device_by_fd(fd);

    if (device == NULL || buffer == NULL)
        return -1;
    memset(buffer, 0, bytes);
    if (type == EV_KEY && bytes >= sizeof(device->key_bits))
        memcpy(buffer, device->key_bits, sizeof(device->key_bits));
    else if (type == EV_ABS && bytes >= sizeof(device->abs_bits))
        memcpy(buffer, device->abs_bits, sizeof(device->abs_bits));
    return 0;
}

static int fake_ioctl_abs(int fd, unsigned axis, int32_t *value,
                          int32_t *minimum, int32_t *maximum,
                          int32_t *fuzz, int32_t *flat,
                          int32_t *resolution)
{
    static const unsigned axis_codes[CART_INPUT_AXIS_COUNT] = {
        ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y,
    };
    struct test_device *device = device_by_fd(fd);
    int slot = -1;

    if (device == NULL)
        return -1;
    for (unsigned i = 0; i < CART_INPUT_AXIS_COUNT; i++) {
        if (axis_codes[i] == axis) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0)
        return -1;
    *value = device->axis[slot].value;
    *minimum = device->axis[slot].minimum;
    *maximum = device->axis[slot].maximum;
    *fuzz = device->axis[slot].fuzz;
    *flat = device->axis[slot].flat;
    *resolution = device->axis[slot].resolution;
    return 0;
}

static const struct cart_input_ops fake_ops = {
    .open = fake_open,
    .close = fake_close,
    .read = fake_read,
    .ioctl_name = fake_ioctl_name,
    .ioctl_key_state = fake_ioctl_key_state,
    .ioctl_bit = fake_ioctl_bit,
    .ioctl_abs = fake_ioctl_abs,
};

/* ------------------------------------------------------------------ *
 * Fake sysfs/dev tree (real directories; name/phys read by discovery) *
 * ------------------------------------------------------------------ */

struct fake_tree {
    char root[TEST_PATH_MAX];
    char sys[TEST_PATH_MAX];
    char dev[TEST_PATH_MAX];
};

static void join_path(char *output, size_t size, const char *left,
                      const char *right)
{
    int written = snprintf(output, size, "%s/%s", left, right);

    CHECK(written > 0 && (size_t)written < size);
}

static void make_dir(const char *path)
{
    CHECK(mkdir(path, 0700) == 0);
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");

    CHECK(file != NULL);
    CHECK(fputs(text, file) >= 0);
    CHECK(fclose(file) == 0);
}

static void fake_tree_init(struct fake_tree *tree)
{
    char template[] = "/tmp/cart-input-test.XXXXXX";
    char *root = mkdtemp(template);

    CHECK(root != NULL);
    CHECK(strlen(root) < sizeof(tree->root));
    strcpy(tree->root, root);
    join_path(tree->sys, sizeof(tree->sys), tree->root, "sys");
    join_path(tree->dev, sizeof(tree->dev), tree->root, "dev");
    make_dir(tree->sys);
    make_dir(tree->dev);
}

static void fake_event_add(const struct fake_tree *tree, const char *event,
                           const char *name, const char *phys)
{
    char event_dir[TEST_PATH_MAX];
    char device_dir[TEST_PATH_MAX];
    char path[TEST_PATH_MAX];

    join_path(event_dir, sizeof(event_dir), tree->sys, event);
    make_dir(event_dir);
    join_path(device_dir, sizeof(device_dir), event_dir, "device");
    make_dir(device_dir);
    join_path(path, sizeof(path), device_dir, "name");
    write_text(path, name);
    join_path(path, sizeof(path), device_dir, "phys");
    write_text(path, phys);
    join_path(path, sizeof(path), tree->dev, event);
    write_text(path, "");
}

static void fake_event_remove(const struct fake_tree *tree, const char *event)
{
    char event_dir[TEST_PATH_MAX];
    char device_dir[TEST_PATH_MAX];
    char path[TEST_PATH_MAX];

    join_path(path, sizeof(path), tree->dev, event);
    CHECK(unlink(path) == 0);
    join_path(event_dir, sizeof(event_dir), tree->sys, event);
    join_path(device_dir, sizeof(device_dir), event_dir, "device");
    join_path(path, sizeof(path), device_dir, "phys");
    CHECK(unlink(path) == 0);
    join_path(path, sizeof(path), device_dir, "name");
    CHECK(unlink(path) == 0);
    CHECK(rmdir(device_dir) == 0);
    CHECK(rmdir(event_dir) == 0);
}

static void fake_tree_destroy(struct fake_tree *tree, const char **events,
                              size_t count)
{
    for (size_t index = 0; index < count; index++)
        fake_event_remove(tree, events[index]);
    CHECK(rmdir(tree->sys) == 0);
    CHECK(rmdir(tree->dev) == 0);
    CHECK(rmdir(tree->root) == 0);
}

/* ------------------------------------------------------------------ *
 * Discovery ranking tests                                             *
 * ------------------------------------------------------------------ */

static void test_discovers_vita_buttons_when_not_event0(void)
{
    const char *events[] = { "event0", "event1", "event7" };
    struct fake_tree tree;
    struct cart_input input;
    struct test_device *device;

    test_devices_reset();
    fake_tree_init(&tree);
    fake_event_add(&tree, "event0", "PlayStation Vita Touchscreen\n",
                   "vita_syscon_ts\n");
    fake_event_add(&tree, "event7", "USB Keyboard\n", "usb-keys/input0\n");
    fake_event_add(&tree, "event1", "PlayStation Vita Buttons (Syscon)\n",
                   "vita_syscon_buttons\n");

    device = add_device("event0", "PlayStation Vita Touchscreen");
    set_bit(device->key_bits, BTN_TOUCH);
    device = add_device("event7", "USB Keyboard");
    set_bit(device->key_bits, KEY_Q);
    device = add_device("event1", "PlayStation Vita Buttons (Syscon)");
    vita_buttons_init(device);

    CHECK(cart_input_init_ex(&input, &fake_ops, tree.sys, tree.dev) == 2);
    CHECK(strcmp(input.source[0].node, "event1") == 0);
    CHECK(strcmp(input.source[1].node, "event7") == 0);
    cart_input_shutdown(&input);
    fake_tree_destroy(&tree, events, sizeof(events) / sizeof(events[0]));
}

static void test_falls_back_to_lowest_key_capable_event(void)
{
    const char *events[] = { "event10", "event2", "event0", "eventx" };
    struct fake_tree tree;
    struct cart_input input;
    struct test_device *device;

    test_devices_reset();
    fake_tree_init(&tree);
    fake_event_add(&tree, "event10", "Second Keyboard\n", "usb-2\n");
    fake_event_add(&tree, "event2", "First Keyboard\n", "usb-1\n");
    fake_event_add(&tree, "event0", "Touch Only\n", "touch\n");
    fake_event_add(&tree, "eventx", "Malformed Name\n", "bad\n");

    device = add_device("event10", "Second Keyboard");
    set_bit(device->key_bits, KEY_Q);
    device = add_device("event2", "First Keyboard");
    set_bit(device->key_bits, KEY_ESC);
    device = add_device("event0", "Touch Only");
    set_bit(device->key_bits, BTN_TOUCH);
    device = add_device("eventx", "Malformed Name");
    set_bit(device->key_bits, KEY_Q);

    CHECK(cart_input_init_ex(&input, &fake_ops, tree.sys, tree.dev) == 2);
    CHECK(strcmp(input.source[0].node, "event2") == 0);
    CHECK(strcmp(input.source[1].node, "event10") == 0);
    cart_input_shutdown(&input);
    fake_tree_destroy(&tree, events, sizeof(events) / sizeof(events[0]));
}

static void test_broken_unrelated_event_does_not_poison_discovery(void)
{
    const char *events[] = { "event0", "event1" };
    struct fake_tree tree;
    struct cart_input input;
    struct test_device *device;

    test_devices_reset();
    fake_tree_init(&tree);
    fake_event_add(&tree, "event0", "Broken Device\n", "broken\n");
    fake_event_add(&tree, "event1", "PlayStation Vita Buttons (Syscon)\n",
                   "vita_syscon_buttons\n");

    device = add_device("event0", "Broken Device");
    device->name_fails = 1;
    device = add_device("event1", "PlayStation Vita Buttons (Syscon)");
    vita_buttons_init(device);

    CHECK(cart_input_init_ex(&input, &fake_ops, tree.sys, tree.dev) == 1);
    CHECK(strcmp(input.source[0].node, "event1") == 0);
    cart_input_shutdown(&input);
    fake_tree_destroy(&tree, events, sizeof(events) / sizeof(events[0]));
}

static void test_no_input_is_nonfatal(void)
{
    struct fake_tree tree;
    struct cart_input input;

    test_devices_reset();
    fake_tree_init(&tree);
    CHECK(cart_input_init_ex(&input, &fake_ops, tree.sys, tree.dev) == 0);
    CHECK(input.source[0].state == CART_INPUT_ABSENT);
    cart_input_shutdown(&input);
    CHECK(rmdir(tree.sys) == 0);
    CHECK(rmdir(tree.dev) == 0);
    CHECK(rmdir(tree.root) == 0);
}

/* ------------------------------------------------------------------ *
 * Event-pump test (real pipe through the injected read)               *
 * ------------------------------------------------------------------ */

static void write_event(int fd, unsigned short type, unsigned short code,
                        int value)
{
    struct input_event event = {
        .type = type,
        .code = code,
        .value = value,
    };

    CHECK(write(fd, &event, sizeof(event)) == (ssize_t)sizeof(event));
}

static void test_normalizes_key_presses(void)
{
    const char *events[] = { "event1" };
    struct fake_tree tree;
    struct cart_input input;
    struct cart_input_frame frame;
    enum cart_input_action action;
    struct test_device *device;
    int pipe_fds[2];

    test_devices_reset();
    fake_tree_init(&tree);
    fake_event_add(&tree, "event1", "PlayStation Vita Buttons (Syscon)\n",
                   "vita_syscon_buttons\n");

    CHECK(pipe2(pipe_fds, O_NONBLOCK | O_CLOEXEC) == 0);

    device = add_device("event1", "PlayStation Vita Buttons (Syscon)");
    vita_buttons_init(device);
    device->real_fd = pipe_fds[0];

    CHECK(cart_input_init_ex(&input, &fake_ops, tree.sys, tree.dev) == 1);

    /* Cross press -> SELECT (B5 action contract: BTN_SOUTH). */
    write_event(pipe_fds[1], EV_KEY, BTN_SOUTH, 1);
    write_event(pipe_fds[1], EV_SYN, SYN_REPORT, 0);
    CHECK(cart_input_poll(&input, &action, NULL) == 0);
    CHECK(action == CART_INPUT_SELECT);

    /* Release + repeat + dead-center stick: nothing queued; frame carries
     * the merged axis (127 within the 15% deadzone -> 0). */
    write_event(pipe_fds[1], EV_KEY, BTN_SOUTH, 0);
    write_event(pipe_fds[1], EV_KEY, BTN_SOUTH, 2);
    write_event(pipe_fds[1], EV_ABS, ABS_X, 127);
    write_event(pipe_fds[1], EV_SYN, SYN_REPORT, 0);
    CHECK(cart_input_poll(&input, &action, &frame) == 0);
    CHECK(action == CART_INPUT_NONE);
    CHECK(frame.axis_present[CART_INPUT_AXIS_LX] == 1);
    CHECK(frame.axis[CART_INPUT_AXIS_LX] == 0);
    CHECK(frame.connected_mask == 1u);

    /* Start -> MENU, then Q -> QUIT, in event order. */
    write_event(pipe_fds[1], EV_KEY, BTN_START, 1);
    write_event(pipe_fds[1], EV_KEY, KEY_Q, 1);
    write_event(pipe_fds[1], EV_SYN, SYN_REPORT, 0);
    CHECK(cart_input_poll(&input, &action, NULL) == 0);
    CHECK(action == CART_INPUT_MENU);
    CHECK(cart_input_poll(&input, &action, NULL) == 0);
    CHECK(action == CART_INPUT_QUIT);
    CHECK(cart_input_poll(&input, &action, NULL) == 0);
    CHECK(action == CART_INPUT_NONE);

    cart_input_shutdown(&input);   /* closes pipe_fds[0] via fake_close */
    CHECK(close(pipe_fds[1]) == 0);
    fake_tree_destroy(&tree, events, sizeof(events) / sizeof(events[0]));
}

int main(void)
{
    test_discovers_vita_buttons_when_not_event0();
    test_falls_back_to_lowest_key_capable_event();
    test_broken_unrelated_event_does_not_poison_discovery();
    test_no_input_is_nonfatal();
    test_normalizes_key_presses();
    puts("all input engine tests passed");
    return 0;
}
