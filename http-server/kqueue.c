#include <assert.h>
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

typedef struct {
    gbString req;
    gbString res;
    int fd;
    char buf[CONN_BUF_LEN];
    char ip[IP_ADDR_STR_LEN];
} conn_handle;

typedef struct {
    int fd;
    int queue;
    gbAllocator allocator;
    gbArray(conn_handle) conn_handles;
    gbArray(struct kevent) watch_list;
    gbArray(struct kevent) event_list;
} server;

static void ip(uint32_t val, char* res) {
    uint8_t a = val >> 24, b = val >> 16, c = val >> 8, d = val & 0xff;
    snprintf(res, 16, "%hhu.%hhu.%hhu.%hhu", d, c, b, a);
}

static void conn_handle_init(conn_handle* ch, gbAllocator allocator,
                             struct sockaddr_in client_addr) {
    ch->req = gb_string_make_reserve(allocator, 4096);
    ch->res = gb_string_make_reserve(allocator, 4096);
    ip(client_addr.sin_addr.s_addr, ch->ip);
}

static int conn_handle_read_request(conn_handle* ch) {
    const ssize_t received = read(ch->fd, ch->buf, CONN_BUF_LEN);
    if (received == -1) {
        fprintf(stderr, "Failed to read(2): ip=%shu err=%s\n", ch->ip,
                strerror(errno));
        return errno;
    }
    if (received == 0) {  // Client closed connection
        return 0;
    }
    printf("[D009] Read: %zd %.*s\n", received, (int)received, ch->buf);
    gb_string_append_length(ch->req, ch->buf, received);
    return received;
}

static void server_add_socket_to_watch_list(server* s, int fd) {
    gb_array_append(s->watch_list, ((struct kevent){0}));
    EV_SET(&s->watch_list[gb_array_count(s->watch_list) - 1], fd, EVFILT_READ,
           EV_ADD | EV_CLEAR, 0, 0, 0);
}

static void server_init(server* s, gbAllocator allocator) {
    s->allocator = allocator;

    gb_array_init_reserve(s->watch_list, s->allocator, LISTEN_BACKLOG);
    printf("[D001] sock_fd=%d\n", s->fd);

    gb_array_init_reserve(s->watch_list, s->allocator, LISTEN_BACKLOG);
    gb_array_init_reserve(s->event_list, s->allocator,
                          gb_array_capacity(s->watch_list));

    gb_array_init_reserve(s->conn_handles, s->allocator, LISTEN_BACKLOG);
}

static void print_usage(int argc, char* argv[]) {
    GB_ASSERT(argc > 0);
    printf("%s <port>\n", argv[0]);
}

/* static int on_url(http_parser* parser, const char* at, size_t length) { */
/*     parser->data = gb_string_append_length(parser->data, at, length); */
/*     return 0; */
/* } */

static int server_accept_new_connection(server* s) {
    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = sizeof(client_addr);
    const int conn_fd = accept(s->fd, (void*)&client_addr, &client_addr_len);
    if (conn_fd == -1) {
        fprintf(stderr, "Failed to accept(2): %s\n", strerror(errno));
        return errno;
    }
    printf("[D002] New conn: %d\n", conn_fd);

    server_add_socket_to_watch_list(s, conn_fd);

    conn_handle ch = {.fd = conn_fd};
    conn_handle_init(&ch, s->allocator, client_addr);
    gb_array_append(s->conn_handles, ch);
    return -1;
}

static conn_handle* server_find_conn_handle_by_fd(server* s, int fd) {
    for (int i = 0; i < gb_array_count(s->conn_handles); i++) {
        conn_handle* ch = &s->conn_handles[i];
        if (ch->fd == fd) return ch;
    }
    return NULL;
}

static void conn_handle_make_response(conn_handle* ch) {
    const char msg[] = "hello";
    bzero(ch->buf, CONN_BUF_LEN);
    snprintf(ch->buf, CONN_BUF_LEN,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain; charset=utf8\r\n"
             "Content-Length: %td\r\n"
             "\r\n"
             "%s",
             sizeof(msg) - 1, msg);
}

static int conn_handle_send_response(conn_handle* ch) {
    const int nb = write(ch->fd, ch->buf, strlen(ch->buf));
    if (nb == -1) {
        fprintf(stderr, "Failed to write(2): ip=%s err=%s\n", ch->ip,
                strerror(errno));
        return errno;
    }
    // TODO: check if all bytes were written

    return nb;
}

static void server_remove_connection(server* s, conn_handle* ch) {
    // Remove kevent
    struct kevent event = {0};
    EV_SET(&event, ch->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(s->queue, &event, 1, NULL, 0, NULL);

    // Close
    close(ch->fd);

    // Remove watch
    for (int i = 0; i < gb_array_count(s->watch_list); i++) {
        if (s->watch_list[i].ident == ch->fd) {
            s->watch_list[i] = s->watch_list[gb_array_count(s->watch_list) - 1];
            gb_array_pop(s->watch_list);
            break;
        }
    }

    // Remove handle
    for (int i = 0; i < gb_array_count(s->conn_handles); i++) {
        if (&s->conn_handles[i] == ch) {
            s->conn_handles[i] =
                s->conn_handles[gb_array_count(s->conn_handles) - 1];
            gb_array_pop(s->conn_handles);
            break;
        }
    }
}

static int server_listen_and_bind(server* s, u16 port) {
    int err = 0;

    s->fd = socket(PF_INET, SOCK_STREAM, 0);
    if (s->fd == -1) {
        fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
        return errno;
    }

    const int val = 1;
    if ((err = setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &val,
                          sizeof(val))) == -1) {
        fprintf(stderr, "Failed to setsockopt(2): %s\n", strerror(errno));
        return errno;
    }

    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if ((err = bind(s->fd, (const struct sockaddr*)&addr, sizeof(addr))) ==
        -1) {
        fprintf(stderr, "Failed to bind(2): %s\n", strerror(errno));
        return errno;
    }

    if ((err = listen(s->fd, 16 * 1024)) == -1) {
        fprintf(stderr, "Failed to listen(2): %s\n", strerror(errno));
        return errno;
    }
    server_add_socket_to_watch_list(s, s->fd);
    printf("Listening: :%d\n", port);
    return 0;
}

static int server_run(server* s, u16 port) {
    int err = 0;
    err = server_listen_and_bind(s, port);
    if (err != 0) return err;

    s->queue = kqueue();
    if (s->queue == -1) {
        fprintf(stderr, "%s:%d:Failed to create queue with kqueue(): %s\n",
                __FILE__, __LINE__, strerror(errno));
        return errno;
    }

    while (1) {
        printf("[D010] conn_handles=%td\n", gb_array_count(s->conn_handles));
        const int event_count =
            kevent(s->queue, s->watch_list, gb_array_count(s->watch_list),
                   s->event_list, gb_array_capacity(s->event_list), NULL);
        if (event_count == -1) {
            fprintf(stderr, "%s:%d:Failed to kevent(2): %s\n", __FILE__,
                    __LINE__, strerror(errno));
            return errno;
        }
        printf("[D006] Event count=%d\n", event_count);
        for (int i = 0; i < event_count; i++) {
            printf("[D007] ident=%lu \n", s->event_list[i].ident);
        }

        if (event_count == 0) continue;
        for (int i = 0; i < event_count; i++) {
            const struct kevent* const e = &s->event_list[i];
            const int fd = e->ident;

            if (fd == s->fd) {  // New connection to accept
                server_accept_new_connection(s);
                continue;
            }

            printf("[D008] Data to be read on: %d\n", fd);
            conn_handle* const ch = server_find_conn_handle_by_fd(s, fd);
            assert(ch != NULL);
            if ((err = conn_handle_read_request(ch)) <= 0) {
                server_remove_connection(s, ch);
                continue;
            }

            conn_handle_make_response(ch);
            conn_handle_send_response(ch);
            server_remove_connection(s, ch);
        }
    }
    return -1;
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
    gbAllocator allocator = gb_heap_allocator();
    server s = {0};
    server_init(&s, allocator);
    err = server_run(&s, port);
    if (err != 0) return err;
}
