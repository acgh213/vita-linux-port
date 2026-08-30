#ifndef CART_INPUT_H
#define CART_INPUT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <cart/input_actions.h>   /* shared action enum ordering NONE..QUIT */
#include <cart/input_normalize.h> /* struct cart_input_axis_range */

#define CART_INPUT_MAX_SOURCES 4
#define CART_INPUT_NODE_MAX 32
#define CART_INPUT_NAME_MAX 128

/* Canonical axis slots; order is part of the ABI. */
enum {
    CART_INPUT_AXIS_LX = 0,
    CART_INPUT_AXIS_LY,
    CART_INPUT_AXIS_RX,
    CART_INPUT_AXIS_RY,
    CART_INPUT_AXIS_L2T,
    CART_INPUT_AXIS_R2T,
    CART_INPUT_AXIS_HAT0X,
    CART_INPUT_AXIS_HAT0Y,
    CART_INPUT_AXIS_COUNT,
};

#define CART_INPUT_DEFAULT_DEADZONE_PCT 15

enum cart_input_state {
    CART_INPUT_ABSENT = 0,
    CART_INPUT_CONNECTED,
    CART_INPUT_DESYNC,
};

struct cart_input_source {
    int fd;
    int rank;                       /* 4 phys > 3 name > 2 gamepad > 1 keyboard */
    enum cart_input_state state;
    uint32_t generation;            /* bumps on every successful connect */
    char node[CART_INPUT_NODE_MAX]; /* dev_input basename, e.g. "event1" */
    char name[CART_INPUT_NAME_MAX];
    uint64_t held_keys;             /* bitmask over codes 0..63 */
    /* Calibration snapshot taken at connect (EVIOCGABS authoritative). */
    struct cart_input_axis_range axis[CART_INPUT_AXIS_COUNT];
    uint8_t axis_valid[CART_INPUT_AXIS_COUNT];
    int32_t last_raw[CART_INPUT_AXIS_COUNT];
};

struct cart_input_frame {
    int16_t axis[CART_INPUT_AXIS_COUNT];
    uint8_t axis_present[CART_INPUT_AXIS_COUNT];
    uint8_t axis_owner[CART_INPUT_AXIS_COUNT]; /* source index per axis */
    uint64_t held_keys;
    uint32_t connected_mask;
    uint32_t generation_total;
};

/*
 * Syscall seam for deterministic tests. Production installs
 * cart_input_default_ops; fd values are opaque tokens owned by whichever
 * ops table is active. Tests substitute scripted operations.
 */
struct cart_input_ops {
    int (*open)(const char *path, int flags);
    int (*close)(int fd);
    ssize_t (*read)(int fd, void *buf, size_t count);
    ssize_t (*ioctl_name)(int fd, char *out, size_t out_size);
    ssize_t (*ioctl_key_state)(int fd, uint8_t *buffer, size_t bytes);
    int (*ioctl_bit)(int fd, unsigned type, uint8_t *buffer, size_t bytes);
    int (*ioctl_abs)(int fd, unsigned axis, int32_t *value, int32_t *minimum,
                     int32_t *maximum, int32_t *fuzz, int32_t *flat,
                     int32_t *resolution);
};

extern const struct cart_input_ops cart_input_default_ops;

/* Test-visible ordering helper: stable sort by rank desc, number asc,
 * preserving insertion stability among equals. Underscore prefix marks it
 * semi-private; production code never calls it outside discovery. */
struct cart_input_candidate {
    int fd;
    int rank;
    unsigned long number;
    char node[CART_INPUT_NODE_MAX];
    struct cart_input_source prepared;
};

int _cart_input_plan(struct cart_input_candidate *candidates, size_t count);

struct cart_input {
    const struct cart_input_ops *ops;
    struct cart_input_source source[CART_INPUT_MAX_SOURCES];
    struct {
        uint8_t count;
        uint8_t item[2 * CART_INPUT_MAX_SOURCES];
    } pending;
    struct {
        uint8_t buf[256];
        size_t len;
    } partial[CART_INPUT_MAX_SOURCES];
};

/* Discover + open ranked sources. Returns how many opened (0 allowed:
 * absent input is nonfatal), or -1 on invalid arguments. */
int cart_input_init(struct cart_input *input, const char *sys_class_input,
                    const char *dev_input);

/* Same, with an injected syscall ops table (tests use this; production
 * init delegates here with cart_input_default_ops). Ops pointer must
 * outlive the cart_input instance. */
int cart_input_init_ex(struct cart_input *input,
                       const struct cart_input_ops *ops,
                       const char *sys_class_input, const char *dev_input);

/* Attempt to fill empty slots using the same pipeline. Caller-driven
 * cadence (~every 2 s from the main tick). Returns newly opened count. */
int cart_input_rescan(struct cart_input *input, const char *sys_class_input,
                      const char *dev_input);

/* Drain readable events; emits at most ONE queued action per call into
 * *action (CART_INPUT_NONE when quiet). Frame reflects merged state;
 * may be NULL. Returns -1 only on NULL input/action. */
int cart_input_poll(struct cart_input *input, enum cart_input_action *action,
                    struct cart_input_frame *frame);

void cart_input_shutdown(struct cart_input *input);

#endif
