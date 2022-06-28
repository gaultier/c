#include <netinet/in.h>
#include <stdio.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"
#include "../vendor/http-parser/http_parser.h"

#define IP_ADDR_STR_LEN 17
#define CONN_BUF_LEN 4096
#define LISTEN_BACKLOG 16384

static void ip(uint32_t val, char* res) {
    uint8_t a = val >> 24, b = val >> 16, c = val >> 8, d = val & 0xff;
    snprintf(res, 16, "%hhu.%hhu.%hhu.%hhu", d, c, b, a);
}

static void print_usage(int argc, char* argv[]) {
    GB_ASSERT(argc > 0);
    printf("%s <port>\n", argv[0]);
}

static int on_url(http_parser* parser, const char* at, size_t length) {
    parser->data = gb_string_append_length(parser->data, at, length);
    return 0;
}

static int handle_connection(struct sockaddr_in client_addr, int conn_fd) {
    char ip_addr[IP_ADDR_STR_LEN] = "";
    ip(client_addr.sin_addr.s_addr, ip_addr);
    printf("New connection: %s:%hu\n", ip_addr, client_addr.sin_port);

    char conn_buf[CONN_BUF_LEN] = "";

    http_parser_settings settings = {.on_url = on_url};
    http_parser parser = {0};
    http_parser_init(&parser, HTTP_REQUEST);

    static u8 mem[10 * 1024 /* 10KiB */] = {};
    gbArena arena = {0};
    gb_arena_init_from_memory(&arena, mem, sizeof(mem));
    gbAllocator allocator = gb_arena_allocator(&arena);
    gbString url = gb_string_make_reserve(allocator, 100);
    gbString req = gb_string_make_reserve(allocator, 4096);
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
        req = gb_string_append_length(req, conn_buf, received);

        const isize len = gb_string_length(req);
        // End of request ?
        // TODO: limit on received bytes total
        if (len >= 4 && req[len - 4] == '\r' && req[len - 3] == '\n' &&
            req[len - 2] == '\r' && req[len - 1] == '\n') {
            break;
        }
    }

    int nparsed =
        http_parser_execute(&parser, &settings, req, gb_string_length(req));

    if (parser.upgrade) {
        GB_ASSERT_MSG(0, "Unimplemented");
    } else if (nparsed != gb_string_length(req)) {
        fprintf(stderr,
                "Failed to parse http request: addr=%s:%hu nparsed=%d "
                "received=%zd\n",
                ip_addr, client_addr.sin_port, nparsed, gb_string_length(req));
        goto end;
    }

    // Response
    bzero(conn_buf, CONN_BUF_LEN);
    snprintf(conn_buf, CONN_BUF_LEN,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain; charset=utf8\r\n"
             "Content-Length: %td\r\n"
             "\r\n"
             "%s",
             GB_STRING_HEADER(url)->length, url);

    // TODO: send in loop
    int sent = send(conn_fd, conn_buf, strlen(conn_buf), 0);
    if (sent == -1) {
        fprintf(stderr, "Failed to send(2): addr=%s:%hu err=%s\n", ip_addr,
                client_addr.sin_port, strerror(errno));
        err = errno;
        goto end;
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

static void socket_add_to_watch_list(gbArray(struct kevent) * watch_list,
                                     int sock) {
    gb_array_append(*watch_list, ((struct kevent){0}));
    EV_SET(&(*watch_list)[gb_array_count(*watch_list) - 1], sock, EVFILT_READ,
           EV_ADD | EV_CLEAR, 0, 0, 0);
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

    if ((err = listen(sock_fd, 16 * 1024)) == -1) {
        fprintf(stderr, "Failed to listen(2): %s\n", strerror(errno));
        return errno;
    }

    const int queue = kqueue();
    if (queue == -1) {
        fprintf(stderr, "%s:%d:Failed to create queue with kqueue(): %s\n",
                __FILE__, __LINE__, strerror(errno));
        return errno;
    }

    static u8 mem[LISTEN_BACKLOG * sizeof(struct kevent) * 4] = {};
    gbArena arena = {0};
    gb_arena_init_from_memory(&arena, mem, sizeof(mem));
    gbAllocator allocator = gb_arena_allocator(&arena);

    gbArray(struct kevent) watch_list = NULL;
    gb_array_init_reserve(watch_list, allocator, LISTEN_BACKLOG);
    socket_add_to_watch_list(&watch_list, sock_fd);

    gbArray(struct kevent) event_list = NULL;
    gb_array_init_reserve(event_list, allocator, gb_array_capacity(watch_list));

    while (1) {
        const int event_count =
            kevent(queue, watch_list, gb_array_count(watch_list), event_list,
                   gb_array_capacity(event_list), NULL);
        if (event_count == -1) {
            fprintf(stderr, "%s:%d:Failed to kevent(2): %s\n", __FILE__,
                    __LINE__, strerror(errno));
            return errno;
        }
        printf("[D006] Event count=%d\n", event_count);

        struct sockaddr_in client_addr = {0};
        socklen_t client_addr_len = sizeof(client_addr);
        int conn_fd = accept(sock_fd, (void*)&client_addr, &client_addr_len);
        if (conn_fd == -1) {
            fprintf(stderr, "Failed to accept(2): %s\n", strerror(errno));
            return errno;
        }

        socket_add_to_watch_list(&watch_list, conn_fd);
        // TODO: recv/send(?) in this loop instead of in handle_connection
        // Alloc struct conn_handle_t with req/res buffers
        // Call handle_connection multiple times

        handle_connection(client_addr, conn_fd);
        // Remove watch
        for (int i = 0; i < gb_array_count(watch_list); i++) {
            if (watch_list[i].ident == conn_fd) {
                watch_list[i] = watch_list[gb_array_count(watch_list) - 1];
                gb_array_pop(watch_list);
                break;
            }
        }
    }
}
