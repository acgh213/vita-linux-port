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

struct fake_tree {
    char root[CART_INPUT_PATH_MAX];
    char sys[CART_INPUT_PATH_MAX];
    char dev[CART_INPUT_PATH_MAX];
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

static void bitmap_with_bit(char *output, size_t size, unsigned int bit)
{
    static const char hex[] = "0123456789abcdef";
    size_t digits = bit / 4U + 1U;

    CHECK(digits + 2U <= size);
    memset(output, '0', digits);
    output[0] = hex[1U << (bit % 4U)];
    output[digits] = '\n';
    output[digits + 1U] = '\0';
}

static void fake_event_add(const struct fake_tree *tree, const char *event,
                           const char *name, const char *phys,
                           const char *capability_ev, unsigned int key_code)
{
    char event_dir[CART_INPUT_PATH_MAX];
    char device_dir[CART_INPUT_PATH_MAX];
    char path[CART_INPUT_PATH_MAX];
    char key_capability[256];

    join_path(event_dir, sizeof(event_dir), tree->sys, event);
    make_dir(event_dir);
    join_path(device_dir, sizeof(device_dir), event_dir, "device");
    make_dir(device_dir);
    join_path(path, sizeof(path), device_dir, "name");
    write_text(path, name);
    join_path(path, sizeof(path), device_dir, "phys");
    write_text(path, phys);
    join_path(path, sizeof(path), device_dir, "capabilities");
    make_dir(path);
    join_path(path, sizeof(path), device_dir, "capabilities/ev");
    write_text(path, capability_ev);
    bitmap_with_bit(key_capability, sizeof(key_capability), key_code);
    join_path(path, sizeof(path), device_dir, "capabilities/key");
    write_text(path, key_capability);
    join_path(path, sizeof(path), tree->dev, event);
    write_text(path, "");
}

static void fake_event_remove(const struct fake_tree *tree, const char *event)
{
    char event_dir[CART_INPUT_PATH_MAX];
    char device_dir[CART_INPUT_PATH_MAX];
    char path[CART_INPUT_PATH_MAX];

    join_path(path, sizeof(path), tree->dev, event);
    CHECK(unlink(path) == 0);
    join_path(event_dir, sizeof(event_dir), tree->sys, event);
    join_path(device_dir, sizeof(device_dir), event_dir, "device");
    join_path(path, sizeof(path), device_dir, "capabilities/ev");
    CHECK(unlink(path) == 0);
    join_path(path, sizeof(path), device_dir, "capabilities/key");
    CHECK(unlink(path) == 0);
    join_path(path, sizeof(path), device_dir, "capabilities");
    CHECK(rmdir(path) == 0);
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

static void test_discovers_vita_buttons_when_not_event0(void)
{
    const char *events[] = { "event0", "event1", "event7" };
    struct fake_tree tree;
    struct cart_input input;

    fake_tree_init(&tree);
    fake_event_add(&tree, "event0", "PlayStation Vita Touchscreen\n",
                   "vita_syscon_ts\n", "b\n", BTN_TOUCH);
    fake_event_add(&tree, "event7", "USB Keyboard\n", "usb-keys/input0\n",
                   "3\n", KEY_Q);
    fake_event_add(&tree, "event1", "PlayStation Vita Buttons (Syscon)\n",
                   "vita_syscon_buttons\n", "3\n", BTN_A);

    CHECK(cart_input_discover(&input, tree.sys, tree.dev) == 1);
    CHECK(strstr(input.path, "/event1") != NULL);
    CHECK(strcmp(input.name, "PlayStation Vita Buttons (Syscon)") == 0);
    cart_input_close(&input);
    fake_tree_destroy(&tree, events, sizeof(events) / sizeof(events[0]));
}

static void test_falls_back_to_lowest_key_capable_event(void)
{
    const char *events[] = { "event10", "event2", "event0", "eventx" };
    struct fake_tree tree;
    struct cart_input input;

    fake_tree_init(&tree);
    fake_event_add(&tree, "event10", "Second Keyboard\n", "usb-2\n", "3\n",
                   KEY_Q);
    fake_event_add(&tree, "event2", "First Keyboard\n", "usb-1\n", "3\n",
                   KEY_ESC);
    fake_event_add(&tree, "event0", "Touch Only\n", "touch\n", "b\n",
                   BTN_TOUCH);
    fake_event_add(&tree, "eventx", "Malformed Name\n", "bad\n", "3\n",
                   KEY_Q);

    CHECK(cart_input_discover(&input, tree.sys, tree.dev) == 1);
    CHECK(strstr(input.path, "/event2") != NULL);
    CHECK(strcmp(input.name, "First Keyboard") == 0);
    cart_input_close(&input);
    fake_tree_destroy(&tree, events, sizeof(events) / sizeof(events[0]));
}

static void test_broken_unrelated_event_does_not_poison_discovery(void)
{
    const char *events[] = { "event0", "event1" };
    struct fake_tree tree;
    struct cart_input input;
    char broken_capability[CART_INPUT_PATH_MAX];

    fake_tree_init(&tree);
    fake_event_add(&tree, "event0", "Broken Device\n", "broken\n", "3\n",
                   KEY_Q);
    fake_event_add(&tree, "event1", "PlayStation Vita Buttons (Syscon)\n",
                   "vita_syscon_buttons\n", "3\n", BTN_A);
    join_path(broken_capability, sizeof(broken_capability), tree.sys,
              "event0/device/capabilities/ev");
    CHECK(unlink(broken_capability) == 0);

    CHECK(cart_input_discover(&input, tree.sys, tree.dev) == 1);
    CHECK(strstr(input.path, "/event1") != NULL);
    cart_input_close(&input);

    write_text(broken_capability, "3\n");
    fake_tree_destroy(&tree, events, sizeof(events) / sizeof(events[0]));
}

static void test_no_input_is_nonfatal(void)
{
    struct fake_tree tree;
    struct cart_input input;

    fake_tree_init(&tree);
    CHECK(cart_input_discover(&input, tree.sys, tree.dev) == 0);
    CHECK(input.fd == -1);
    cart_input_close(&input);
    CHECK(rmdir(tree.sys) == 0);
    CHECK(rmdir(tree.dev) == 0);
    CHECK(rmdir(tree.root) == 0);
}

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
    int pipe_fds[2];
    struct cart_input input = { .fd = -1 };
    enum cart_input_action action;

    CHECK(pipe2(pipe_fds, O_NONBLOCK | O_CLOEXEC) == 0);
    input.fd = pipe_fds[0];

    write_event(pipe_fds[1], EV_KEY, BTN_A, 1);
    CHECK(cart_input_poll(&input, &action) == 0);
    CHECK(action == CART_INPUT_NEXT);

    write_event(pipe_fds[1], EV_KEY, BTN_A, 0);
    write_event(pipe_fds[1], EV_KEY, BTN_A, 2);
    write_event(pipe_fds[1], EV_ABS, ABS_X, 127);
    CHECK(cart_input_poll(&input, &action) == 0);
    CHECK(action == CART_INPUT_NONE);

    write_event(pipe_fds[1], EV_KEY, BTN_START, 1);
    write_event(pipe_fds[1], EV_KEY, KEY_Q, 1);
    CHECK(cart_input_poll(&input, &action) == 0);
    CHECK(action == CART_INPUT_QUIT);

    cart_input_close(&input);
    CHECK(close(pipe_fds[1]) == 0);
}

int main(void)
{
    test_discovers_vita_buttons_when_not_event0();
    test_falls_back_to_lowest_key_capable_event();
    test_broken_unrelated_event_does_not_poison_discovery();
    test_no_input_is_nonfatal();
    test_normalizes_key_presses();
    puts("all input discovery tests passed");
    return 0;
}
