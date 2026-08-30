#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SMALL_BUFFER 256
#define PID_BUFFER 32

static void usage(const char *program)
{
    printf("usage: %s status|next [options]\n", program);
    printf("  --machine                 stable key=value output\n");
    printf("  --pidfile PATH            cart PID file\n");
    printf("  --expected-exe PATH       required cart executable\n");
    printf("  --state PATH              rate-limit state file\n");
    printf("  --min-interval-ms MS      minimum interval between next actions\n");
}

static void report(int machine, const char *key, const char *value)
{
    if (machine) {
        printf("%s=%s\n", key, value);
    } else {
        printf("%s: %s\n", key, value);
    }
}

static int parse_unsigned(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || *text == '\0') {
        return -1;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }
    *value = (uint64_t)parsed;
    return 0;
}

static int read_small_file(const char *path, char *buffer, size_t capacity)
{
    int fd;
    ssize_t count;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    count = read(fd, buffer, capacity - 1);
    close(fd);
    if (count <= 0) {
        return -1;
    }
    buffer[count] = '\0';
    while (count > 0 && (buffer[count - 1] == '\n' || buffer[count - 1] == '\r' ||
                         buffer[count - 1] == ' ' || buffer[count - 1] == '\t')) {
        buffer[--count] = '\0';
    }
    return 0;
}

static int load_target(const char *pidfile, const char *expected_exe,
                       pid_t *pid, char *actual_exe, size_t actual_capacity)
{
    char pid_text[PID_BUFFER];
    char proc_exe[SMALL_BUFFER];
    uint64_t parsed_pid;
    ssize_t length;

    if (read_small_file(pidfile, pid_text, sizeof(pid_text)) < 0 ||
        parse_unsigned(pid_text, &parsed_pid) < 0 || parsed_pid == 0 ||
        parsed_pid > (uint64_t)INT32_MAX) {
        return -2;
    }
    *pid = (pid_t)parsed_pid;
    if (kill(*pid, 0) < 0 && errno != EPERM) {
        return -3;
    }
    if (snprintf(proc_exe, sizeof(proc_exe), "/proc/%ld/exe", (long)*pid) < 0) {
        return -4;
    }
    length = readlink(proc_exe, actual_exe, actual_capacity - 1);
    if (length < 0) {
        return -4;
    }
    actual_exe[length] = '\0';
    if (strcmp(actual_exe, expected_exe) != 0) {
        return -5;
    }
    return 0;
}

static int monotonic_ns(uint64_t *now)
{
    struct timespec clock_value;

    if (clock_gettime(CLOCK_MONOTONIC, &clock_value) < 0) {
        return -1;
    }
    *now = (uint64_t)clock_value.tv_sec * 1000000000ULL +
           (uint64_t)clock_value.tv_nsec;
    return 0;
}

static int load_interval(const char *override, uint64_t *milliseconds)
{
    const char *environment = override;

    if (environment == NULL) {
        environment = getenv("VITA_CONTROL_MIN_INTERVAL_MS");
    }
    if (environment == NULL) {
        environment = "500";
    }
    if (parse_unsigned(environment, milliseconds) < 0 || *milliseconds > 3600000ULL) {
        return -1;
    }
    return 0;
}

static void report_target_error(int machine, int result, const char *actual_exe)
{
    switch (result) {
    case -2:
        report(machine, "status", "invalid_pidfile");
        break;
    case -3:
        report(machine, "status", "target_not_running");
        break;
    case -5:
        report(machine, "status", "target_executable_mismatch");
        report(machine, "actual_exe", actual_exe);
        break;
    default:
        report(machine, "status", "target_inspection_failed");
        break;
    }
}

static int update_rate_state(const char *state_path, uint64_t interval_ms,
                             uint64_t now_ns, int machine)
{
    char previous_text[PID_BUFFER];
    char now_text[PID_BUFFER];
    ssize_t previous_length;
    uint64_t previous_ns;
    uint64_t elapsed_ns;
    uint64_t retry_ms;
    int fd;
    int result = 0;
    int length;

    fd = open(state_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0 || flock(fd, LOCK_EX) < 0) {
        if (fd >= 0) {
            close(fd);
        }
        report(machine, "status", "rate_state_unavailable");
        return -1;
    }
    previous_length = read(fd, previous_text, sizeof(previous_text) - 1);
    if (previous_length > 0) {
        previous_text[previous_length] = '\0';
        if (parse_unsigned(previous_text, &previous_ns) == 0 && now_ns >= previous_ns) {
            elapsed_ns = now_ns - previous_ns;
            if (elapsed_ns < interval_ms * 1000000ULL) {
                retry_ms = (interval_ms * 1000000ULL - elapsed_ns + 999999ULL) / 1000000ULL;
                char retry_text[PID_BUFFER];
                snprintf(retry_text, sizeof(retry_text), "%llu",
                         (unsigned long long)retry_ms);
                report(machine, "status", "rate_limited");
                report(machine, "retry_after_ms", retry_text);
                result = -1;
                goto unlock;
            }
        }
    }
    length = snprintf(now_text, sizeof(now_text), "%llu", (unsigned long long)now_ns);
    if (length < 0 || (size_t)length >= sizeof(now_text) ||
        ftruncate(fd, 0) < 0 || lseek(fd, 0, SEEK_SET) < 0 ||
        write(fd, now_text, (size_t)length) != length) {
        report(machine, "status", "rate_state_write_failed");
        result = -1;
    }
unlock:
    flock(fd, LOCK_UN);
    close(fd);
    return result;
}

int main(int argc, char **argv)
{
    const char *action = NULL;
    const char *pidfile = getenv("VITA_CONTROL_PIDFILE");
    const char *expected_exe = getenv("VITA_CONTROL_EXPECTED_EXE");
    const char *state_path = getenv("VITA_CONTROL_STATE");
    const char *interval_override = NULL;
    int machine = 0;
    int index;
    pid_t pid;
    char actual_exe[SMALL_BUFFER] = {0};
    int target_result;

    pid = 0;
    if (pidfile == NULL) {
        pidfile = "/run/pstv-demo-cart.pid";
    }
    if (expected_exe == NULL) {
        expected_exe = "/usr/local/bin/pstv-demo-cart";
    }
    if (state_path == NULL) {
        state_path = "/run/vita-control.next";
    }
    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "status") == 0 || strcmp(argv[index], "next") == 0) {
            if (action != NULL) {
                usage(argv[0]);
                return 2;
            }
            action = argv[index];
        } else if (strcmp(argv[index], "--machine") == 0) {
            machine = 1;
        } else if (strcmp(argv[index], "--pidfile") == 0 ||
                   strcmp(argv[index], "--expected-exe") == 0 ||
                   strcmp(argv[index], "--state") == 0 ||
                   strcmp(argv[index], "--min-interval-ms") == 0) {
            if (index + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            if (strcmp(argv[index], "--pidfile") == 0) {
                pidfile = argv[++index];
            } else if (strcmp(argv[index], "--expected-exe") == 0) {
                expected_exe = argv[++index];
            } else if (strcmp(argv[index], "--state") == 0) {
                state_path = argv[++index];
            } else {
                interval_override = argv[++index];
            }
        } else if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (action == NULL) {
        usage(argv[0]);
        return 2;
    }

    target_result = load_target(pidfile, expected_exe, &pid, actual_exe, sizeof(actual_exe));
    if (strcmp(action, "status") == 0) {
        char pid_text[PID_BUFFER];
        char interval_text[PID_BUFFER];
        uint64_t interval_ms;

        if (load_interval(interval_override, &interval_ms) < 0) {
            report(machine, "status", "invalid_interval");
            return 1;
        }
        snprintf(pid_text, sizeof(pid_text), "%ld", (long)pid);
        snprintf(interval_text, sizeof(interval_text), "%llu",
                 (unsigned long long)interval_ms);
        report(machine, "schema", "1");
        report(machine, "action", "status");
        report(machine, "pid", pid_text);
        report(machine, "running", target_result == 0 ? "1" : "0");
        if (actual_exe[0] != '\0') {
            report(machine, "exe", actual_exe);
        }
        report(machine, "expected_exe", expected_exe);
        report(machine, "next_signal", "SIGUSR1");
        report(machine, "min_interval_ms", interval_text);
        if (target_result != 0) {
            report_target_error(machine, target_result, actual_exe);
            return 1;
        }
        report(machine, "target_executable_match", "1");
        return 0;
    }

    if (target_result != 0) {
        report_target_error(machine, target_result, actual_exe);
        return 1;
    }
    {
        uint64_t interval_ms;
        uint64_t now_ns;

        if (load_interval(interval_override, &interval_ms) < 0 || monotonic_ns(&now_ns) < 0) {
            report(machine, "status", "clock_or_interval_failed");
            return 1;
        }
        if (update_rate_state(state_path, interval_ms, now_ns, machine) < 0) {
            return 1;
        }
    }
    if (kill(pid, SIGUSR1) < 0) {
        report(machine, "status", "signal_failed");
        return 1;
    }
    {
        char pid_text[PID_BUFFER];
        snprintf(pid_text, sizeof(pid_text), "%ld", (long)pid);
        report(machine, "status", "triggered");
        report(machine, "pid", pid_text);
        report(machine, "signal", "SIGUSR1");
    }
    return 0;
}
