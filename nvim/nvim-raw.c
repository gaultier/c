#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

static int read_all(int fd, uint8_t* buf, uint64_t cap, uint64_t* len) {
    memset(buf, 0, cap);
    *len = 0;

    while (*len < cap) {
        int ret = recv(fd, &buf[*len], cap - *len, 0);
        if (ret == -1) {
            fprintf(stderr, "Failed to read from socket: %d %s", errno,
                    strerror(errno));
            return errno;
        }
        if (ret == 0) return 0;

        for (uint16_t i = *len; i < *len + ret; i++) {
            printf("%#0x ", buf[i]);
        }
        *len += ret;
    }

    return 0;
}

static void handle_connection(int conn_fd) {
    static uint8_t buf[MAXPATHLEN];
    uint64_t len = 0;

    int ret = 0;
    if ((ret = read_all(conn_fd, buf, sizeof(buf), &len)) != 0) {
        fprintf(stderr, "Failed to recv(2): %d %s\n", errno, strerror(errno));
        exit(errno);
    }
    printf("\nRead: len=%llu\n", len);
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
        return errno;
    }

    int val = 1;
    int err = 0;
    if ((err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))) ==
        -1) {
        fprintf(stderr, "Failed to setsockopt(2): %s\n", strerror(errno));
        return errno;
    }

    const uint8_t ip[4] = {127, 0, 0, 1};
    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = {.s_addr = *(uint32_t*)ip},
        .sin_port = htons(12345),
    };

    if ((err = bind(fd, (const struct sockaddr*)&addr, sizeof(addr))) == -1) {
        fprintf(stderr, "Failed to bind(2): %s\n", strerror(errno));
        return errno;
    }

    if ((err = listen(fd, 16 * 1024)) == -1) {
        fprintf(stderr, "Failed to listen(2): %s\n", strerror(errno));
        return errno;
    }
    while (1) {
        int conn_fd = accept(fd, NULL, 0);
        if (conn_fd == -1) {
            fprintf(stderr, "Failed to accept(2): %s\n", strerror(errno));
            return errno;
        }

        pid_t pid = fork();
        if (pid == -1) {
            fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
            close(conn_fd);
        } else if (pid == 0) {  // Child
            handle_connection(conn_fd);
            exit(0);
        } else {  // Parent
            // Fds are duplicated by fork(2) and need to be
            // closed by both parent & child
            close(conn_fd);
        }
    }
}
