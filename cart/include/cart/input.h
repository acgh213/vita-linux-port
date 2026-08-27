#ifndef CART_INPUT_H
#define CART_INPUT_H

#include <stddef.h>

#define CART_INPUT_PATH_MAX 4096
#define CART_INPUT_NAME_MAX 128

enum cart_input_action {
    CART_INPUT_NONE = 0,
    CART_INPUT_NEXT,
    CART_INPUT_QUIT,
};

struct cart_input {
    int fd;
    char path[CART_INPUT_PATH_MAX];
    char name[CART_INPUT_NAME_MAX];
};

/* Returns 1 when an input source is opened, 0 when none is available, and -1
 * for invalid arguments or an unrecoverable discovery error. */
int cart_input_discover(struct cart_input *input, const char *sys_class_input,
                        const char *dev_input);
int cart_input_poll(struct cart_input *input, enum cart_input_action *action);
void cart_input_close(struct cart_input *input);

#endif
