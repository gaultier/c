#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"
#include "vendor/picohttpparser/picohttpparser.h"

static void print_usage(int argc, char* argv[]) {
    GB_ASSERT(argc > 0);
    printf("%s <port>\n", argv[0]);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        print_usage(argc, argv);
        return 0;
    }

    const u64 port = gb_str_to_u64(argv[1], NULL, 10);
    if (port > UINT16_MAX) {
        fprintf(stderr, "Invalid port number: %llu\n", port);
        return EINVAL;
    }

    int fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
        return errno;
    }
    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if (connect(fd, (void*)&addr, sizeof(addr)) == -1) {
        fprintf(stderr, "Failed to connect(2): %s\n", strerror(errno));
        return errno;
    }

    const char msg[] =
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost:12347\r\n"
        "\r\n";
    const u64 msg_len = sizeof(msg) - 1;
    u64 sent = 0;
    u64 total_sent = 0;

    while (total_sent < msg_len) {
        sent = send(fd, &msg[sent], msg_len - total_sent, 0);
        if (sent == -1) {
            fprintf(stderr, "Failed to send(2): %s\n", strerror(errno));
            return errno;
        }
        total_sent += sent;
    }

    u64 received = 0;
    u64 total_received = 0;
    char buf[4096] = "";
    while (total_received <= sizeof(buf)) {
        received = recv(fd, &buf[received], sizeof(buf) - total_received, 0);
        if (received == -1) {
            fprintf(stderr, "Failed to recv(2): %s\n", strerror(errno));
            return errno;
        }
        if (received == 0) {
            break;
        }
        total_received += received;
    }

    fprintf(stderr, "Received: `%.*s`\n", (int)total_received, buf);
    return 0;
}
