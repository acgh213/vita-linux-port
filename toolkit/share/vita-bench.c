#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_WORKERS 4
#define DEFAULT_BYTES (8U * 1024U * 1024U)
#define QUICK_BYTES (1U * 1024U * 1024U)
#define DEFAULT_MEMORY_ROUNDS 8U
#define QUICK_MEMORY_ROUNDS 2U
#define DEFAULT_COMPUTE_MILLIONS 20U
#define QUICK_COMPUTE_MILLIONS 2U

struct start_gate {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int ready;
    int go;
};

struct worker_args {
    struct start_gate *gate;
    unsigned int kind;
    unsigned int rounds;
    size_t bytes;
    uint64_t checksum;
};

enum {
    WORK_COMPUTE = 0,
    WORK_MEMCPY = 1,
    WORK_MEMSET = 2
};

static double now_seconds(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return 0.0;
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
}

static const char *rooted_path(const char *root, const char *path,
                               char *buffer, size_t buffer_size) {
    int written = snprintf(buffer, buffer_size, "%s%s", root, path);
    if (written < 0 || (size_t)written >= buffer_size)
        return NULL;
    return buffer;
}

static void read_online_cpus(const char *root, char *buffer, size_t size) {
    char path[256];
    FILE *file;
    size_t length;

    if (!rooted_path(root, "/sys/devices/system/cpu/online", path,
                     sizeof(path))) {
        snprintf(buffer, size, "UNKNOWN");
        return;
    }
    file = fopen(path, "r");
    if (!file || !fgets(buffer, (int)size, file)) {
        if (file)
            fclose(file);
        snprintf(buffer, size, "UNKNOWN");
        return;
    }
    fclose(file);
    length = strlen(buffer);
    while (length > 0 && (buffer[length - 1] == '\n' ||
                          buffer[length - 1] == '\r'))
        buffer[--length] = '\0';
    if (length == 0)
        snprintf(buffer, size, "UNKNOWN");
}

static unsigned int online_worker_limit(const char *online) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    (void)online;
    if (count < 1)
        count = 1;
    if (count > MAX_WORKERS)
        count = MAX_WORKERS;
    return (unsigned int)count;
}

static void *worker_main(void *opaque) {
    struct worker_args *args = (struct worker_args *)opaque;
    uint64_t checksum = 0;
    unsigned int round;

    if (pthread_mutex_lock(&args->gate->mutex) != 0)
        return NULL;
    args->gate->ready++;
    pthread_cond_broadcast(&args->gate->condition);
    while (!args->gate->go)
        pthread_cond_wait(&args->gate->condition, &args->gate->mutex);
    pthread_mutex_unlock(&args->gate->mutex);

    if (args->kind == WORK_COMPUTE) {
        uint64_t value = 0x9e3779b97f4a7c15ULL ^ (uint64_t)(uintptr_t)args;
        uint64_t operations = (uint64_t)args->rounds * 1000000ULL;
        uint64_t index;
        for (index = 0; index < operations; ++index) {
            value ^= value >> 12;
            value ^= value << 25;
            value ^= value >> 27;
            value = value * 2685821657736338717ULL + index;
        }
        checksum = value;
    } else {
        unsigned char *source = (unsigned char *)malloc(args->bytes);
        unsigned char *destination = (unsigned char *)malloc(args->bytes);
        if (!source || !destination) {
            free(source);
            free(destination);
            args->checksum = UINT64_MAX;
            return NULL;
        }
        memset(source, (int)((uintptr_t)args & 0xffU), args->bytes);
        memset(destination, 0, args->bytes);
        for (round = 0; round < args->rounds; ++round) {
            if (args->kind == WORK_MEMCPY)
                memcpy(destination, source, args->bytes);
            else
                memset(destination, (int)(round & 0xffU), args->bytes);
            checksum ^= destination[(round * 4099U) % args->bytes];
            checksum += destination[args->bytes - 1U];
        }
        checksum ^= destination[0];
        free(source);
        free(destination);
    }
    args->checksum = checksum;
    return NULL;
}

static int run_memory(unsigned int kind, unsigned int workers,
                      unsigned int rounds, size_t bytes, double *seconds,
                      uint64_t *checksum) {
    struct start_gate gate;
    struct worker_args args[MAX_WORKERS];
    pthread_t threads[MAX_WORKERS];
    unsigned int created = 0;
    unsigned int index;
    double started;

    memset(&gate, 0, sizeof(gate));
    if (pthread_mutex_init(&gate.mutex, NULL) != 0 ||
        pthread_cond_init(&gate.condition, NULL) != 0)
        return -1;
    for (index = 0; index < workers; ++index) {
        args[index].gate = &gate;
        args[index].kind = kind;
        args[index].rounds = rounds;
        args[index].bytes = bytes;
        args[index].checksum = 0;
        if (pthread_create(&threads[index], NULL, worker_main, &args[index]) != 0)
            break;
        created++;
    }
    if (created != workers) {
        pthread_mutex_lock(&gate.mutex);
        gate.go = 1;
        pthread_cond_broadcast(&gate.condition);
        pthread_mutex_unlock(&gate.mutex);
        for (index = 0; index < created; ++index)
            pthread_join(threads[index], NULL);
        pthread_cond_destroy(&gate.condition);
        pthread_mutex_destroy(&gate.mutex);
        return -1;
    }
    pthread_mutex_lock(&gate.mutex);
    while (gate.ready != (int)workers)
        pthread_cond_wait(&gate.condition, &gate.mutex);
    started = now_seconds();
    gate.go = 1;
    pthread_cond_broadcast(&gate.condition);
    pthread_mutex_unlock(&gate.mutex);
    for (index = 0; index < workers; ++index)
        pthread_join(threads[index], NULL);
    *seconds = now_seconds() - started;
    *checksum = 0;
    for (index = 0; index < workers; ++index)
        *checksum ^= args[index].checksum;
    pthread_cond_destroy(&gate.condition);
    pthread_mutex_destroy(&gate.mutex);
    return *checksum == UINT64_MAX ? -1 : 0;
}

static int read_fb_metadata(const char *root, size_t *bytes) {
    char path[256];
    char geometry[64];
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    FILE *file;

    if (!rooted_path(root, "/sys/class/graphics/fb0/virtual_size", path,
                     sizeof(path)))
        return -1;
    file = fopen(path, "r");
    if (!file || fscanf(file, "%u,%u", &width, &height) != 2) {
        if (file)
            fclose(file);
        return -1;
    }
    fclose(file);
    if (!rooted_path(root, "/sys/class/graphics/fb0/stride", path,
                     sizeof(path)))
        return -1;
    file = fopen(path, "r");
    if (!file || fscanf(file, "%u", &stride) != 1) {
        if (file)
            fclose(file);
        return -1;
    }
    fclose(file);
    (void)geometry;
    *bytes = (size_t)height * stride;
    return *bytes > 0 ? 0 : -1;
}

static int run_framebuffer(const char *root, double *seconds, size_t *bytes) {
    char path[256];
    unsigned char *frame;
    size_t written = 0;
    int descriptor;
    double started;

    if (read_fb_metadata(root, bytes) != 0 ||
        !rooted_path(root, "/dev/fb0", path, sizeof(path)))
        return -1;
    frame = (unsigned char *)malloc(*bytes);
    if (!frame)
        return -1;
    memset(frame, 0, *bytes);
    descriptor = open(path, O_WRONLY);
    if (descriptor < 0) {
        free(frame);
        return -1;
    }
    started = now_seconds();
    while (written < *bytes) {
        ssize_t result = write(descriptor, frame + written, *bytes - written);
        if (result <= 0) {
            close(descriptor);
            free(frame);
            return -1;
        }
        written += (size_t)result;
    }
    close(descriptor);
    *seconds = now_seconds() - started;
    free(frame);
    return 0;
}

static void print_result(const char *name, unsigned int workers,
                         double seconds, size_t bytes, unsigned int rounds,
                         int machine) {
    double rate;
    if (seconds <= 0.0)
        seconds = 0.000001;
    rate = (double)workers * (double)bytes * (double)rounds /
           seconds / (1024.0 * 1024.0);
    if (machine)
        printf("%s_%u_workers_mib_s=%.3f\n", name, workers, rate);
    else
        printf("  %-7s %u workers: %.3f MiB/s\n", name, workers, rate);
}

int main(int argc, char **argv) {
    const char *root = getenv("VITA_BENCH_ROOT");
    char online[64];
    unsigned int requested = 0;
    unsigned int maximum;
    unsigned int rounds = DEFAULT_MEMORY_ROUNDS;
    unsigned int compute_millions = DEFAULT_COMPUTE_MILLIONS;
    size_t bytes = DEFAULT_BYTES;
    int machine = 0;
    int quick = 0;
    int framebuffer = 0;
    int index;

    if (!root)
        root = "/";
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--machine") == 0)
            machine = 1;
        else if (strcmp(argv[index], "--quick") == 0)
            quick = 1;
        else if (strcmp(argv[index], "--framebuffer") == 0)
            framebuffer = 1;
        else if (strcmp(argv[index], "--workers") == 0 && index + 1 < argc) {
            requested = (unsigned int)strtoul(argv[++index], NULL, 10);
            if (requested < 1 || requested > MAX_WORKERS) {
                fprintf(stderr, "vita-bench: workers must be 1..%u\n",
                        MAX_WORKERS);
                return 2;
            }
        } else if (strcmp(argv[index], "--help") == 0) {
            printf("usage: vita-bench [--machine] [--quick] [--workers N] "
                   "[--framebuffer]\n");
            return 0;
        } else {
            fprintf(stderr, "vita-bench: unknown option: %s\n", argv[index]);
            return 2;
        }
    }
    if (quick) {
        bytes = QUICK_BYTES;
        rounds = QUICK_MEMORY_ROUNDS;
        compute_millions = QUICK_COMPUTE_MILLIONS;
    }
    read_online_cpus(root, online, sizeof(online));
    maximum = requested ? requested : online_worker_limit(online);

    if (machine) {
        printf("schema=1\n");
        printf("online_cpus=%s\nworkers_requested=%u\n", online, maximum);
        printf("mode=%s\nbuffer_bytes=%zu\nmemory_rounds=%u\n",
               quick ? "quick" : "full", bytes, rounds);
    } else {
        printf("Vita Linux benchmark (%s, %u worker%s)\n",
               quick ? "quick" : "full", maximum == 1 ? 1 : maximum,
               maximum == 1 ? "" : "s");
        printf("  online CPUs: %s\n  buffer: %zu bytes\n", online, bytes);
    }

    {
        unsigned int first_worker = requested ? requested : 1;
        for (index = (int)first_worker; index <= (int)maximum; index *= 2) {
        double seconds;
        uint64_t checksum;
        if (run_memory(WORK_COMPUTE, (unsigned int)index, compute_millions,
                       1, &seconds, &checksum) != 0)
            return 1;
        if (machine)
            printf("compute_%d_workers_mops=%.3f\n", index,
                   ((double)index * (double)compute_millions) / seconds);
        else
            printf("  compute %d workers: %.3f Mops/s\n", index,
                   ((double)index * (double)compute_millions) / seconds);
        if (run_memory(WORK_MEMCPY, (unsigned int)index, rounds, bytes,
                       &seconds, &checksum) != 0)
            return 1;
        print_result("memcpy", (unsigned int)index, seconds, bytes, rounds,
                     machine);
        if (run_memory(WORK_MEMSET, (unsigned int)index, rounds, bytes,
                       &seconds, &checksum) != 0)
            return 1;
        print_result("memset", (unsigned int)index, seconds, bytes, rounds,
                     machine);
        if (requested || index == (int)maximum)
            break;
        }
    }

    if (framebuffer) {
        double seconds;
        size_t frame_bytes;
        if (run_framebuffer(root, &seconds, &frame_bytes) != 0) {
            fprintf(stderr, "vita-bench: framebuffer write failed\n");
            return 1;
        }
        if (seconds <= 0.0)
            seconds = 0.000001;
        if (machine)
            printf("framebuffer=enabled\nframebuffer_bytes=%zu\n"
                   "framebuffer_write_mib_s=%.3f\n", frame_bytes,
                   (double)frame_bytes / seconds / (1024.0 * 1024.0));
        else
            printf("  framebuffer write: %zu bytes at %.3f MiB/s\n",
                   frame_bytes,
                   (double)frame_bytes / seconds / (1024.0 * 1024.0));
    } else if (machine) {
        printf("framebuffer=skipped\n");
    } else {
        printf("  framebuffer write: skipped (use --framebuffer explicitly)\n");
    }
    return 0;
}
