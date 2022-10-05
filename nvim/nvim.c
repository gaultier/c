#include <_types/_uint16_t.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void handle_connection(int conn_fd) {
    static uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    uint16_t buf_len = 0;
    while (true) {
        int err = read(conn_fd, &buf[buf_len], sizeof(buf) - buf_len);
        if (err == -1) {
            fprintf(stderr, "Failed to read from socket: %d %s", errno,
                    strerror(errno));
            exit(errno);
        }
        if (err == 0) break;

        for (uint16_t i = buf_len; i < buf_len + err; i++) {
            printf("%#0x ", buf[i]);
        }
        buf_len += err;
    }

    printf("\nRead: len=%d\n", buf_len);
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
