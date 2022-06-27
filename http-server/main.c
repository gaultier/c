#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"
#include "../vendor/http-parser/http_parser.h"

#define IP_ADDR_STR_LEN 17
#define CONN_BUF_LEN 4096

static void ip(uint32_t val, char* res) {
    uint8_t a = val >> 24, b = val >> 16, c = val >> 8, d = val & 0xff;
    snprintf(res, 16, "%hhu.%hhu.%hhu.%hhu", d, c, b, a);
}

static void print_usage(int argc, char* argv[]) {
    GB_ASSERT(argc > 0);
    printf("%s <port>\n", argv[0]);
}

static int on_url(http_parser* parser, const char* at, size_t length) {
    gb_string_append_length(parser->data, at, length);
    return 0;
}

static int handle_connection(struct sockaddr_in client_addr, int conn_fd) {
    char ip_addr[IP_ADDR_STR_LEN] = "";
    ip(client_addr.sin_addr.s_addr, ip_addr);
    /* printf("New connection: %s:%hu\n", ip_addr, client_addr.sin_port); */

    char conn_buf[CONN_BUF_LEN] = "";

    http_parser_settings settings = {.on_url = on_url};
    http_parser parser = {0};
    http_parser_init(&parser, HTTP_REQUEST);

    gbAllocator allocator = gb_heap_allocator();
    gbString url = gb_string_make_reserve(allocator, 100);
    parser.data = url;
    int err = 0;

    while (1) {
        ssize_t received = recv(conn_fd, conn_buf, CONN_BUF_LEN, 0);
        if (received == -1) {
            fprintf(stderr, "Failed to recv(2): addr=%s:%hu err=%s\n", ip_addr,
                    client_addr.sin_port, strerror(errno));
            err = errno;
            goto end;
        }
        if (received == 0) {  // Client closed connection
            goto end;
        }

        int nparsed =
            http_parser_execute(&parser, &settings, conn_buf, received);

        if (parser.upgrade) {
            /* handle new protocol */
            GB_ASSERT_MSG(0, "Unimplemented");
        } else if (nparsed != received) {
            /* Handle error. Usually just close the connection. */
            fprintf(stderr,
                    "Failed to parse http request: addr=%s:%hu nparsed=%d "
                    "received=%zd\n",
                    ip_addr, client_addr.sin_port, nparsed, received);
            goto end;
        }
        /* printf( */
        /*     "content_length=%llu method=%d type=%d http=%d.%d nparsed=%d " */
        /*     "url=`%s` length=%td\n", */
        /*     parser.content_length, parser.method, parser.type, */
        /*     parser.http_major, parser.http_minor, nparsed, url, */
        /*     GB_STRING_HEADER(url)->length); */

        bzero(conn_buf, CONN_BUF_LEN);
        snprintf(conn_buf, CONN_BUF_LEN,
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/plain; charset=utf8\r\n"
                 "Content-Length: %td\r\n"
                 "\r\n"
                 "%s",
                 GB_STRING_HEADER(url)->length, url);
        int sent = send(conn_fd, conn_buf, strlen(conn_buf), 0);
        if (sent == -1) {
            fprintf(stderr, "Failed to send(2): addr=%s:%hu err=%s\n", ip_addr,
                    client_addr.sin_port, strerror(errno));
            err = errno;
            goto end;
        }
    }

end:
    gb_string_free(url);
    if ((err = close(conn_fd)) != 0) {
        fprintf(stderr, "Failed to close socket for: err=%s\n",
                strerror(errno));
        err = errno;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        print_usage(argc, argv);
        return 0;
    }

    const u64 port = gb_str_to_u64(argv[1], NULL, 10);
    if (port > UINT16_MAX) {
        fprintf(stderr, "Invalid port number: %llu\n", port);
        return 1;
    }

    int err = 0;
    int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
        return errno;
    }

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

        pid_t pid = fork();
        if (pid == -1) {
            fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
            /* exit(errno); */
        } else if (pid == 0) {  // Child
            err = handle_connection(client_addr, conn_fd);
            exit(err);
        } else {  // Parent
            // Fds are duplicated by fork(2) and need to be
            // closed by both parent & child
            close(conn_fd);
        }
    }
}
