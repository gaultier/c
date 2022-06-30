#include <assert.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"
#include "../vendor/http-parser/http_parser.h"

#define IP_ADDR_STR_LEN 17
#define CONN_BUF_LEN 4096
#define LISTEN_BACKLOG 512

typedef struct {
    /* gbString req; */
    /* gbString res; */
    int fd;
    char buf[CONN_BUF_LEN];
    char ip[IP_ADDR_STR_LEN];
    struct timeval start;
} conn_handle;

typedef struct {
    int fd;
    int queue;
    gbAllocator allocator;
    gbArray(conn_handle) conn_handles;
    struct kevent event_list[LISTEN_BACKLOG];
} server;

static bool verbose = false;
static u64 req_count = 0;
static u64 max_req_lifetime_usecs = {0};

#define LOG(fmt, ...)                                   \
    do {                                                \
        if (verbose) fprintf(stderr, fmt, __VA_ARGS__); \
    } while (0)

static int fd_set_non_blocking(int fd) {
    int res = 0;
    do res = fcntl(fd, F_GETFL);
    while (res == -1 && errno == EINTR);

    if (res == -1) {
        fprintf(stderr, "Failed to fcntl(2): %s\n", strerror(errno));
        return errno;
    }

    /* Bail out now if already set/clear. */
    if ((res & O_NONBLOCK) == 0) {
        do res = fcntl(fd, F_SETFL, res | O_NONBLOCK);
        while (res == -1 && errno == EINTR);

        if (res == -1) {
            fprintf(stderr, "Failed to  fcntl(2): %s\n", strerror(errno));
            return errno;
        }
    }
    return 0;
}

static void ip(uint32_t val, char* res) {
    uint8_t a = val >> 24, b = val >> 16, c = val >> 8, d = val & 0xff;
    snprintf(res, 16, "%hhu.%hhu.%hhu.%hhu", d, c, b, a);
}

static void conn_handle_init(conn_handle* ch, gbAllocator allocator,
                             struct sockaddr_in client_addr) {
    /* ch->req = gb_string_make_reserve(allocator, 4096); */
    /* ch->res = gb_string_make_reserve(allocator, 4096); */
    ip(client_addr.sin_addr.s_addr, ch->ip);

    gettimeofday(&ch->start, NULL);
}

static void conn_handle_destroy(conn_handle* ch) {
    /* gb_string_free(ch->req); */
    /* gb_string_free(ch->res); */
}

static int conn_handle_read_request(conn_handle* ch) {
    int total_received = 0;
    const ssize_t received =
        read(ch->fd, &ch->buf[total_received], CONN_BUF_LEN);
    if (received == -1) {
        fprintf(stderr, "Failed to read(2): ip=%shu err=%s\n", ch->ip,
                strerror(errno));
        return errno;
    }
    if (received == 0) {  // Client closed connection
        return 0;
    }
    total_received += received;
    LOG("[D009] Read: received=%zd total_received=%d %.*s\n", received,
        total_received, (int)received, ch->buf);
    /* ch->req = gb_string_append_length(ch->req, ch->buf, received); */
    return received;
}

static int server_init(server* s, gbAllocator allocator) {
    s->allocator = allocator;

    LOG("[D001] sock_fd=%d\n", s->fd);

    gb_array_init_reserve(s->conn_handles, s->allocator, LISTEN_BACKLOG);

    s->queue = kqueue();
    if (s->queue == -1) {
        fprintf(stderr, "%s:%d:Failed to create queue with kqueue(): %s\n",
                __FILE__, __LINE__, strerror(errno));
        return errno;
    }
    return 0;
}

static int server_add_event(server* s, int fd) {
    struct kevent event = {0};
    EV_SET(&event, fd, EVFILT_READ, EV_ADD, 0, 0, 0);

    if (kevent(s->queue, &event, 1, NULL, 0, NULL)) {
        fprintf(stderr, "%s:%d:Failed to kevent(2): %s\n", __FILE__, __LINE__,
                strerror(errno));
        return errno;
    }
    return 0;
}

static int server_remove_event(server* s, int fd) {
    struct kevent event = {0};
    EV_SET(&event, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);

    if (kevent(s->queue, &event, 1, NULL, 0, NULL)) {
        fprintf(stderr, "%s:%d:Failed to kevent(2): %s\n", __FILE__, __LINE__,
                strerror(errno));
        return errno;
    }
    return 0;
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
    LOG("[D002] New conn: %d\n", conn_fd);

    int res = 0;
    if ((res = fd_set_non_blocking(conn_fd)) != 0) return res;

    server_add_event(s, conn_fd);

    conn_handle ch = {.fd = conn_fd};
    conn_handle_init(&ch, s->allocator, client_addr);
    gb_array_append(s->conn_handles, ch);

    req_count++;
    return 0;
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
    int written = 0;
    const int total = strlen(ch->buf);
    while (written < total) {
        const int nb = write(ch->fd, &ch->buf[written], total - written);
        if (nb == -1) {
            fprintf(stderr, "Failed to write(2): ip=%s err=%s\n", ch->ip,
                    strerror(errno));
            return errno;
        }
        written += nb;
    }

    return 0;
}

static void server_remove_connection(server* s, conn_handle* ch) {
    struct timeval end = {0};
    gettimeofday(&end, NULL);
    u64 secs = end.tv_sec - ch->start.tv_sec;
    u64 usecs = end.tv_usec - ch->start.tv_usec;
    u64 total_usecs = usecs + 1000 * 1000 * secs;
    if (total_usecs > max_req_lifetime_usecs)
        max_req_lifetime_usecs = total_usecs;

    LOG("Removing connection: fd=%d remaining=%td cap(conn_handles)=%td "
        "lifetime=%llus %lluus "
        "max=%lluus\n",
        ch->fd, gb_array_count(s->conn_handles),
        gb_array_capacity(s->conn_handles), secs, usecs,
        max_req_lifetime_usecs);
    server_remove_event(s, ch->fd);

    // Close
    close(ch->fd);

    conn_handle_destroy(ch);

    // Remove handle
    for (int i = 0; i < gb_array_count(s->conn_handles); i++) {
        if (&s->conn_handles[i] == ch) {
            memcpy(&s->conn_handles[i],
                   &s->conn_handles[gb_array_count(s->conn_handles) - 1],
                   sizeof(conn_handle));
            s->conn_handles[gb_array_count(s->conn_handles) - 1] =
                (conn_handle){0};
            gb_array_pop(s->conn_handles);
            break;
        }
    }
}

static int server_listen_and_bind(server* s, u16 port) {
    int res = 0;

    s->fd = socket(PF_INET, SOCK_STREAM, 0);
    if (s->fd == -1) {
        fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
        return errno;
    }

    const int val = 1;
    if ((res = setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &val,
                          sizeof(val))) == -1) {
        fprintf(stderr, "Failed to setsockopt(2): %s\n", strerror(errno));
        return errno;
    }

    if ((res = fd_set_non_blocking(s->fd)) != 0) {
        return res;
    }

    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if ((res = bind(s->fd, (const struct sockaddr*)&addr, sizeof(addr))) ==
        -1) {
        fprintf(stderr, "Failed to bind(2): %s\n", strerror(errno));
        return errno;
    }

    if ((res = listen(s->fd, LISTEN_BACKLOG)) == -1) {
        fprintf(stderr, "Failed to listen(2): %s\n", strerror(errno));
        return errno;
    }
    server_add_event(s, s->fd);
    printf("Listening: :%d\n", port);
    return 0;
}

static int server_poll_events(server* s, int* event_count) {
    LOG("[D012] req_count=%llu\n", req_count);
    *event_count =
        kevent(s->queue, NULL, 0, s->event_list, LISTEN_BACKLOG, NULL);
    if (*event_count == -1) {
        fprintf(stderr, "%s:%d:Failed to kevent(2): %s\n", __FILE__, __LINE__,
                strerror(errno));
        return errno;
    }
    LOG("[D006] Event count=%d\n", *event_count);

    return 0;
}

static void server_handle_events(server* s, int event_count) {
    for (int i = 0; i < event_count; i++) {
        const struct kevent* const e = &s->event_list[i];
        const int fd = e->ident;

        if (fd == s->fd) {  // New connection to accept
            server_accept_new_connection(s);
            continue;
        }

        LOG("[D008] Data to be read on: %d\n", fd);
        conn_handle* const ch = server_find_conn_handle_by_fd(s, fd);
        assert(ch != NULL);

        // Connection gone
        if (e->flags & EV_EOF) {
            server_remove_connection(s, ch);
            continue;
        }

        int res = 0;
        if ((res = conn_handle_read_request(ch)) <= 0) {
            server_remove_connection(s, ch);
            continue;
        }

        conn_handle_make_response(ch);
        conn_handle_send_response(ch);
        server_remove_connection(s, ch);
    }
}

static int server_run(server* s, u16 port) {
    int res = 0;
    res = server_listen_and_bind(s, port);
    if (res != 0) return res;

    while (1) {
        int event_count = 0;
        server_poll_events(s, &event_count);
        server_handle_events(s, event_count);
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
        return EINVAL;
    }

    verbose = getenv("VERBOSE") != NULL;

    int res = 0;
    gbAllocator allocator = gb_heap_allocator();
    server s = {0};
    if ((res = server_init(&s, allocator)) != 0) return res;
    if ((res = server_run(&s, port)) != 0) return res;
}
