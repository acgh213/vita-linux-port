#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

static int output_fd;

static void record_signal(int signal_number)
{
    static const char message[] = "SIGUSR1\n";
    if (signal_number == SIGUSR1) {
        (void)write(output_fd, message, sizeof(message) - 1);
    }
}

int main(int argc, char **argv)
{
    struct sigaction action;

    if (argc != 2) {
        return 2;
    }
    output_fd = open(argv[1], O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (output_fd < 0) {
        return 1;
    }
    action.sa_handler = record_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGUSR1, &action, NULL) < 0) {
        close(output_fd);
        return 1;
    }
    for (;;) {
        pause();
    }
}
