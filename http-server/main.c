#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void ip(uint32_t val, char* res) {
    uint8_t a = val >> 24, b = val >> 16, c = val >> 8, d = val & 0xff;
    snprintf(res, 16, "%hhu.%hhu.%hhu.%hhu", d, c, b, a);
}

int main() {
    int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
        return errno;
    }

    int err = 0;
    int val = 1;
    if ((err = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &val,
                          sizeof(val))) == -1) {
        fprintf(stderr, "Failed to setsockopt(2): %s\n", strerror(errno));
        return errno;
    }

    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(12345),
    };

    if ((err = bind(sock_fd, (const struct sockaddr*)&addr, sizeof(addr))) ==
        -1) {
        fprintf(stderr, "Failed to bind(2): %s\n", strerror(errno));
        return errno;
    }

    if ((err = listen(sock_fd, 1024)) == -1) {
        fprintf(stderr, "Failed to listen(2): %s\n", strerror(errno));
        return errno;
    }

    while (1) {
        struct sockaddr_in client_addr = {0};
        socklen_t client_addr_len = sizeof(client_addr);
        int conn_fd = accept(sock_fd, (void*)&client_addr, &client_addr_len);
        if (conn_fd == -1) {
            fprintf(stderr, "Failed to accept(2): %s\n", strerror(errno));
            return errno;
        }
        char ip_addr[17] = "";
        ip(client_addr.sin_addr.s_addr, ip_addr);
        printf("New connection: %s:%hu\n", ip_addr, client_addr.sin_port);

        int sent = send(conn_fd, "hello!", 6, 0);
        if (sent == -1) {
            fprintf(stderr, "Failed to send(2): addr=%s:%hu err=%s\n", ip_addr,
                    client_addr.sin_port, strerror(errno));
            return errno;
        }
        sleep(10);

        close(conn_fd);
    }
}
