#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define FB_WIDTH 1280
#define FB_HEIGHT 720
#define FB_BPP 4
#define SCALE 4
#define IMAGE_WIDTH (FB_WIDTH / SCALE)
#define IMAGE_HEIGHT (FB_HEIGHT / SCALE)
#define FB_BYTES ((size_t)FB_WIDTH * FB_HEIGHT * FB_BPP)
#define BMP_ROW_BYTES ((size_t)IMAGE_WIDTH * 3)
#define BMP_BYTES (54 + BMP_ROW_BYTES * IMAGE_HEIGHT)
#define REQUEST_BYTES 4096

static unsigned char framebuffer[FB_BYTES];
static unsigned char bitmap[BMP_BYTES];

static void usage(const char *program)
{
    printf("usage: %s [--device PATH] [--bind ADDRESS] [--port PORT] "
           "[--machine] [--once]\n", program);
    printf("  --device PATH   framebuffer device (default /dev/fb0)\n");
    printf("  --bind ADDRESS  listen address (default 127.0.0.1)\n");
    printf("  --listen ADDR   alias for --bind\n");
    printf("  --port PORT     TCP port (default 8080)\n");
    printf("  --machine       print stable startup key=value lines\n");
    printf("  --once          serve one valid request, then exit\n");
}

static int parse_port(const char *text, uint16_t *port)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 || value > 65535) {
        return -1;
    }
    *port = (uint16_t)value;
    return 0;
}

static int send_all(int fd, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    size_t sent = 0;

    while (sent < length) {
        ssize_t result = send(fd, bytes + sent, length - sent, MSG_NOSIGNAL);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (result == 0) {
            return -1;
        }
        sent += (size_t)result;
    }
    return 0;
}

static void put_u16(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)(value & 0xffU);
    destination[1] = (unsigned char)(value >> 8);
}

static void put_u32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)(value & 0xffU);
    destination[1] = (unsigned char)((value >> 8) & 0xffU);
    destination[2] = (unsigned char)((value >> 16) & 0xffU);
    destination[3] = (unsigned char)(value >> 24);
}

static int read_frame(const char *device)
{
    int fd = open(device, O_RDONLY | O_CLOEXEC);
    size_t offset = 0;

    if (fd < 0) {
        return -1;
    }
    while (offset < FB_BYTES) {
        ssize_t result = read(fd, framebuffer + offset, FB_BYTES - offset);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        if (result == 0) {
            close(fd);
            return -1;
        }
        offset += (size_t)result;
    }
    close(fd);
    return 0;
}

static void build_bitmap(void)
{
    size_t y;

    memset(bitmap, 0, sizeof(bitmap));
    bitmap[0] = 'B';
    bitmap[1] = 'M';
    put_u32(bitmap + 2, (uint32_t)BMP_BYTES);
    put_u32(bitmap + 10, 54);
    put_u32(bitmap + 14, 40);
    put_u32(bitmap + 18, IMAGE_WIDTH);
    put_u32(bitmap + 22, IMAGE_HEIGHT);
    put_u16(bitmap + 26, 1);
    put_u16(bitmap + 28, 24);
    put_u32(bitmap + 34, (uint32_t)(BMP_ROW_BYTES * IMAGE_HEIGHT));
    put_u32(bitmap + 38, 2835);
    put_u32(bitmap + 42, 2835);

    for (y = 0; y < IMAGE_HEIGHT; y++) {
        size_t source_y = IMAGE_HEIGHT - 1 - y;
        size_t destination = 54 + y * BMP_ROW_BYTES;
        size_t x;
        for (x = 0; x < IMAGE_WIDTH; x++) {
            size_t source = (source_y * SCALE * FB_WIDTH + x * SCALE) * FB_BPP;
            bitmap[destination++] = framebuffer[source + 2];
            bitmap[destination++] = framebuffer[source + 1];
            bitmap[destination++] = framebuffer[source];
        }
    }
}

static int send_response(int client, int status, const char *reason,
                         const char *content_type, const void *body,
                         size_t body_length)
{
    char header[512];
    int header_length = snprintf(header, sizeof(header),
                                 "HTTP/1.0 %d %s\r\n"
                                 "Content-Type: %s\r\n"
                                 "Content-Length: %zu\r\n"
                                 "Connection: close\r\n\r\n",
                                 status, reason, content_type, body_length);

    if (header_length < 0 || (size_t)header_length >= sizeof(header)) {
        return -1;
    }
    if (send_all(client, header, (size_t)header_length) < 0) {
        return -1;
    }
    return send_all(client, body, body_length);
}

static int handle_request(int client, const char *device)
{
    char request[REQUEST_BYTES + 1];
    char method[16];
    char path[128];
    char version[16];
    size_t used = 0;
    int parsed;

    while (used < REQUEST_BYTES) {
        ssize_t result = recv(client, request + used, REQUEST_BYTES - used, 0);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (result == 0) {
            return 0;
        }
        used += (size_t)result;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL || strstr(request, "\n\n") != NULL) {
            break;
        }
    }
    if (used == REQUEST_BYTES) {
        static const char body[] = "request too large\n";
        (void)send_response(client, 413, "Payload Too Large", "text/plain; charset=utf-8",
                            body, sizeof(body) - 1);
        return 1;
    }

    parsed = sscanf(request, "%15s %127s %15s", method, path, version);
    if (parsed != 3) {
        static const char body[] = "bad request\n";
        (void)send_response(client, 400, "Bad Request", "text/plain; charset=utf-8",
                            body, sizeof(body) - 1);
        return 1;
    }
    if (strcmp(method, "GET") != 0) {
        static const char body[] = "GET only\n";
        (void)send_response(client, 405, "Method Not Allowed", "text/plain; charset=utf-8",
                            body, sizeof(body) - 1);
        return 1;
    }
    if (strcmp(path, "/health") == 0) {
        static const char body[] = "status=ok\nread_only=1\n";
        (void)send_response(client, 200, "OK", "text/plain; charset=utf-8",
                            body, sizeof(body) - 1);
        return 1;
    }
    if (strcmp(path, "/") != 0 && strcmp(path, "/frame.bmp") != 0) {
        static const char body[] = "not found\n";
        (void)send_response(client, 404, "Not Found", "text/plain; charset=utf-8",
                            body, sizeof(body) - 1);
        return 1;
    }
    if (read_frame(device) < 0) {
        static const char body[] = "framebuffer read failed\n";
        (void)send_response(client, 503, "Service Unavailable", "text/plain; charset=utf-8",
                            body, sizeof(body) - 1);
        return 1;
    }
    build_bitmap();
    (void)send_response(client, 200, "OK", "image/bmp", bitmap, sizeof(bitmap));
    return 1;
}

int main(int argc, char **argv)
{
    const char *device = "/dev/fb0";
    const char *bind_address = "127.0.0.1";
    uint16_t port = 8080;
    int machine = 0;
    int once = 0;
    int server;
    int opt;
    struct sockaddr_in address;

    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--device") == 0 || strcmp(argv[index], "--bind") == 0 ||
            strcmp(argv[index], "--listen") == 0 || strcmp(argv[index], "--port") == 0) {
            if (index + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            if (strcmp(argv[index], "--device") == 0) {
                device = argv[++index];
            } else if (strcmp(argv[index], "--port") == 0) {
                if (parse_port(argv[++index], &port) < 0) {
                    fprintf(stderr, "invalid port\n");
                    return 2;
                }
            } else {
                bind_address = argv[++index];
            }
        } else if (strcmp(argv[index], "--machine") == 0) {
            machine = 1;
        } else if (strcmp(argv[index], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[index]);
            usage(argv[0]);
            return 2;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        return 1;
    }
    opt = 1;
    if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server);
        return 1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) {
        fprintf(stderr, "bind address must be an IPv4 address: %s\n", bind_address);
        close(server);
        return 2;
    }
    if (bind(server, (const struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(server, 4) < 0) {
        perror("bind/listen");
        close(server);
        return 1;
    }
    if (machine) {
        printf("schema=1\n");
        printf("bind=%s\n", bind_address);
        printf("port=%u\n", (unsigned)port);
        printf("device=%s\n", device);
        printf("width=%u\nheight=%u\nscale=%u\nread_only=1\n",
               IMAGE_WIDTH, IMAGE_HEIGHT, SCALE);
        fflush(stdout);
    }

    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            close(server);
            return 1;
        }
        if (handle_request(client, device) && once) {
            close(client);
            break;
        }
        close(client);
    }
    close(server);
    return 0;
}
