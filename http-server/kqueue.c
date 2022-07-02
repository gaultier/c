#include <assert.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <sys/time.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"
#include "vendor/picohttpparser/picohttpparser.h"

#define IP_ADDR_STR_LEN 17
#define CONN_BUF_LEN 2048
#define CONN_BUF_LEN_MAX 10 * 1024 * 1024
#define LISTEN_BACKLOG 512

typedef struct {
    u64 upper_bound_milliseconds_excl;
    u64 count;
} latency_histogram_bucket;

typedef struct {
    latency_histogram_bucket buckets[10];
} latency_histogram;

typedef enum {
    HM_GET,
    HM_POST,
    HM_PUT,
    HM_PATCH,
    HM_DELETE,
} http_method;

typedef struct {
    const char* path;
    struct phr_header headers[50];
    http_method method;
    u16 path_len;
    u8 num_headers;
} http_req;

typedef struct server server;

typedef struct {
    server* s;
    struct timeval start;
    int fd;
    gbArray(char) req_buf;
    char res_buf[CONN_BUF_LEN];
    char ip[IP_ADDR_STR_LEN];
    http_req req;
} conn_handle;

struct server {
    int fd;
    int queue;
    gbAllocator allocator;
    gbArray(conn_handle) conn_handles;
    struct kevent event_list[LISTEN_BACKLOG];
    latency_histogram hist;
    u64 requests_in_flight;
};

static bool verbose = false;

#define LOG(fmt, ...)                                     \
    do {                                                  \
        if (verbose) fprintf(stderr, fmt, ##__VA_ARGS__); \
    } while (0)

static int http_request_parse(http_req* req, gbArray(char) buf,
                              u64 prev_buf_len) {
    assert(req != NULL);

    const char* method = NULL;
    const char* path = NULL;
    usize method_len = 0;
    usize path_len = 0;
    int minor_version = 0;
    struct phr_header headers[100] = {0};
    usize num_headers = sizeof(headers) / sizeof(headers[0]);

    int res = phr_parse_request(buf, gb_array_count(buf), &method, &method_len,
                                &path, &path_len, &minor_version, headers,
                                &num_headers, prev_buf_len);

    LOG("phr_parse_request: res=%d\n", res);
    if (res == -1) {
        LOG("Failed to phr_parse_request:\n");
        return res;
    }
    if (res == -2) {
        LOG("Partial http parse, need more data\n");
        return res;
    }
    if (method_len >= sizeof("DELETE") - 1) {  // Longest method
        return EINVAL;
    }
    if (method_len == 3 && memcmp(method, "GET", 3) == 0)
        req->method = HM_GET;
    else if (method_len == 4 && memcmp(method, "POST", 4) == 0)
        req->method = HM_POST;
    else if (method_len == 3 && memcmp(method, "PUT", 3) == 0)
        req->method = HM_PUT;
    else if (method_len == 5 && memcmp(method, "PATCH", 5) == 0)
        req->method = HM_PATCH;
    else if (method_len == 6 && memcmp(method, "DELETE", 6) == 0)
        req->method = HM_DELETE;
    else
        return EINVAL;

    if (path_len >= 4096) {
        return EINVAL;
    }
    req->path = path;
    req->path_len = path_len;

    if (num_headers >= UINT8_MAX ||
        num_headers >= sizeof(headers) / sizeof(headers[0]))
        return EINVAL;
    req->num_headers = num_headers;
    /* memcpy(req->headers, headers, num_headers * sizeof(headers[0])); */

    LOG("method=%d path=%.*s\n", req->method, req->path_len, req->path);
    return res;
}

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
    assert(val > 0);
    assert(res != NULL);

    uint8_t a = val >> 24, b = val >> 16, c = val >> 8, d = val & 0xff;
    snprintf(res, 16, "%hhu.%hhu.%hhu.%hhu", d, c, b, a);
}

static void conn_handle_init(conn_handle* ch, gbAllocator allocator,
                             struct sockaddr_in client_addr) {
    ip(client_addr.sin_addr.s_addr, ch->ip);

    gettimeofday(&ch->start, NULL);

    gb_array_init_reserve(ch->req_buf, allocator, CONN_BUF_LEN);
    ch->req.num_headers = sizeof(ch->req.headers) / sizeof(ch->req.headers[0]);
}

static int conn_handle_read_request(conn_handle* ch) {
    assert(ch != NULL);

    const u64 prev_len = gb_array_count(ch->req_buf);
    const u64 new_capacity = gb_array_count(ch->req_buf) + CONN_BUF_LEN;
    if (new_capacity >= CONN_BUF_LEN_MAX) {
        return EINVAL;
    }
    gb_array_grow(ch->req_buf, new_capacity);
    const ssize_t received = read(ch->fd, &ch->req_buf[prev_len], CONN_BUF_LEN);
    if (received == -1) {
        fprintf(stderr, "Failed to read(2): ip=%shu err=%s\n", ch->ip,
                strerror(errno));
        return errno;
    }
    if (received == 0) {  // Client closed connection
        return 0;
    }

    const u64 new_size = prev_len + received;
    gb_array_resize(ch->req_buf, new_size);
    LOG("[D009] Read: received=%zd `%.*s`\n", received,
        (int)gb_array_count(ch->req_buf), ch->req_buf);

    int res = http_request_parse(&ch->req, ch->req_buf, prev_len);
    return res;
}

static int server_init(server* s, gbAllocator allocator) {
    assert(s != NULL);

    s->allocator = allocator;

    LOG("[D001] sock_fd=%d\n", s->fd);

    gb_array_init_reserve(s->conn_handles, s->allocator, LISTEN_BACKLOG);

    s->queue = kqueue();
    if (s->queue == -1) {
        fprintf(stderr, "%s:%d:Failed to create queue with kqueue(): %s\n",
                __FILE__, __LINE__, strerror(errno));
        return errno;
    }

    s->hist =
        (latency_histogram){.buckets = {
                                {.upper_bound_milliseconds_excl = 5},
                                {.upper_bound_milliseconds_excl = 20},
                                {.upper_bound_milliseconds_excl = 100},
                                {.upper_bound_milliseconds_excl = 500},
                                {.upper_bound_milliseconds_excl = 1000},
                                {.upper_bound_milliseconds_excl = UINT64_MAX},
                            }};
    return 0;
}

static int server_add_event(server* s, int fd) {
    assert(s != NULL);

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
    assert(s != NULL);

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

static int server_accept_new_connection(server* s) {
    assert(s != NULL);

    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = sizeof(client_addr);
    const int conn_fd = accept(s->fd, (void*)&client_addr, &client_addr_len);
    if (conn_fd == -1) {
        fprintf(stderr, "Failed to accept(2): %s\n", strerror(errno));
        return errno;
    }
    LOG("\n\n---------------- Request start "
        "(requests_in_flight=%llu)\n\n[D002] New conn: %d\n",
        s->requests_in_flight, conn_fd);

    int res = 0;
    if ((res = fd_set_non_blocking(conn_fd)) != 0) return res;

    server_add_event(s, conn_fd);

    conn_handle ch = {.fd = conn_fd, .s = s};
    conn_handle_init(&ch, s->allocator, client_addr);
    assert(ch.fd == conn_fd);
    gb_array_append(s->conn_handles, ch);

    s->requests_in_flight++;

    return 0;
}

static conn_handle* server_find_conn_handle_by_fd(server* s, int fd) {
    assert(s != NULL);

    for (int i = 0; i < gb_array_count(s->conn_handles); i++) {
        conn_handle* ch = &s->conn_handles[i];
        LOG("Searching for: haystack[%d]=%d needle=%d\n", i, ch->fd, fd);
        if (ch->fd == fd) return ch;
    }
    assert(0 && "Unreachable");
    return NULL;
}

static char* http_content_type_for_file(const char* ext, int ext_len) {
    assert(ext != NULL);

    if (ext_len == 4 && memcmp(ext, "html", 4) == 0) return "text/html";
    if (ext_len == 2 && memcmp(ext, "js", 2) == 0)
        return "application/javascript";
    if (ext_len == 3 && memcmp(ext, "css", 2) == 0)
        return "text/css";
    else
        return "text/plain";
}

static int conn_handle_write(conn_handle* ch) {
    assert(ch != NULL);

    int written = 0;
    const int total = strlen(ch->res_buf);

    while (written < total) {
        const int nb = write(ch->fd, &ch->res_buf[written], total - written);
        if (nb == -1) {
            fprintf(stderr, "Failed to write(2): ip=%s err=%s\n", ch->ip,
                    strerror(errno));
            return errno;
        }
        written += nb;
    }
    return 0;
}

static void histogram_add_entry(latency_histogram* hist, float val) {
    assert(hist != NULL);

    latency_histogram_bucket* bucket = NULL;
    for (int i = 0; i < sizeof(hist->buckets) / sizeof(hist->buckets[0]); i++) {
        bucket = &hist->buckets[i];
        if (bucket->upper_bound_milliseconds_excl > val) {
            break;
        }
    }
    assert(bucket != NULL);
    bucket->count++;

    LOG("histogram_add_entry: %f\n", val);
}

static void histogram_print(latency_histogram* hist) {
    assert(hist != NULL);

    puts("");
    for (int i = 0; i < sizeof(hist->buckets) / sizeof(hist->buckets[0]); i++) {
        latency_histogram_bucket* bucket = &hist->buckets[i];
        printf("Latency < %llu: %llu\n", bucket->upper_bound_milliseconds_excl,
               bucket->count);
        if (bucket->upper_bound_milliseconds_excl == UINT64_MAX) break;
    }
}

static void server_remove_connection(server* s, conn_handle* ch) {
    assert(s != NULL);
    assert(ch != NULL);

    // Stats
    {
        struct timeval end = {0};
        gettimeofday(&end, NULL);
        const u64 secs = end.tv_sec - ch->start.tv_sec;
        const u64 usecs = end.tv_usec - ch->start.tv_usec;
        const float total_msecs = usecs / 1000.0 + 1000 * secs;

        histogram_add_entry(&s->hist, total_msecs);
        // TODO: add a timer to print it every X seconds?
        histogram_print(&s->hist);
        s->requests_in_flight--;

        LOG("Removing connection: fd=%d remaining=%td "
            "cap(conn_handles)=%td "
            "lifetime=%fms\n",
            ch->fd, gb_array_count(s->conn_handles),
            gb_array_capacity(s->conn_handles), total_msecs);
    }

    // Rm
    {
        server_remove_event(s, ch->fd);

        close(ch->fd);

        // Remove handle from array
        gb_array_free(ch->req_buf);
        assert(s->conn_handles <= ch &&
               ch <= &s->conn_handles[gb_array_count(s->conn_handles) - 1]);
        memcpy(ch, &s->conn_handles[gb_array_count(s->conn_handles) - 1],
               sizeof(conn_handle));
        s->conn_handles[gb_array_count(s->conn_handles) - 1] = (conn_handle){0};
        gb_array_pop(s->conn_handles);
    }

    LOG("\n\n---------------- Request end (requests_in_flight=%llu)\n\n",
        s->requests_in_flight);
}

static int conn_handle_respond_404(conn_handle* ch) {
    assert(ch != NULL);

    snprintf(ch->res_buf, CONN_BUF_LEN,
             "HTTP/1.1 404 Not Found\r\n"
             "Content-Length: 0\r\n"
             "\r\n");

    return conn_handle_write(ch);
}

static int conn_handle_serve_static_file(conn_handle* ch) {
    assert(ch != NULL);

    // TODO: security
    char path[PATH_MAX] = "";
    if (ch->req.path_len >= PATH_MAX) {
        conn_handle_respond_404(ch);
        return 0;
    }
    if (ch->req.path_len <= 1) {
        memcpy(path, "index.html", sizeof("index.html"));
    } else {
        memcpy(path, ch->req.path + 1 /* Skip leading slash */,
               ch->req.path_len - 1);
    }

    const char* const ext = gb_path_extension(path);
    LOG("Serving static file `%s` ext=`%s`\n", path, ext);

    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open(2): path=`%s` err=%s\n", path,
                strerror(errno));
        conn_handle_respond_404(ch);
        return 0;
    }

    struct stat st = {0};
    if (stat(path, &st) == -1) {
        fprintf(stderr, "Failed to stat(2): path=`%s` err=%s\n", path,
                strerror(errno));
        close(fd);
        conn_handle_respond_404(ch);
        return 0;
    }
    LOG("Serving static file `%s` size=%lld\n", path, st.st_size);

    snprintf(ch->res_buf, CONN_BUF_LEN,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s; charset=utf8\r\n"
             "Content-Length: %lld\r\n"
             "\r\n",
             http_content_type_for_file(ext, strlen(ext)), st.st_size);
    struct iovec header = {
        .iov_base = ch->res_buf,
        .iov_len = strlen(ch->res_buf),
    };
    struct sf_hdtr headers_trailers = {
        .headers = &header,
        .hdr_cnt = 1,
    };

    off_t len = 0;
    // TODO: partial writes
    int res = sendfile(fd, ch->fd, 0, &len, &headers_trailers, 0);
    if (res == -1) {
        fprintf(stderr, "Failed to sendfile(2): path=`%s` err=%s\n", path,
                strerror(errno));
        close(fd);
        return -1;
    }
    LOG("sendfile(2): res=%d len=%lld in_fd=%d out_fd=%d\n", res, len, fd,
        ch->fd);
    close(fd);
    return 0;
}

static bool str_ends_with(const char* haystack, int haystack_len,
                          const char* needle, int needle_len) {
    assert(haystack != NULL);
    assert(needle != NULL);

    if (haystack_len < needle_len) return false;

    return memcmp(haystack + haystack_len - needle_len, needle, needle_len) ==
           0;
}

static int conn_handle_send_response(conn_handle* ch) {
    assert(ch != NULL);

    if (ch->req.method != HM_GET) {
        conn_handle_respond_404(ch);
        return 0;
    }

    if (ch->req.path_len <= 1 ||
        str_ends_with(ch->req.path, ch->req.path_len, ".html", 5) ||
        str_ends_with(ch->req.path, ch->req.path_len, ".js", 3) ||
        str_ends_with(ch->req.path, ch->req.path_len, ".css", 4)) {
        conn_handle_serve_static_file(ch);
        return 0;
    }

    snprintf(ch->res_buf, CONN_BUF_LEN,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain; charset=utf8\r\n"
             "Content-Length: %d\r\n"
             "\r\n"
             "%.*s",
             (int)ch->req.path_len, (int)ch->req.path_len, ch->req.path);

    return conn_handle_write(ch);
}

static int server_listen_and_bind(server* s, u16 port) {
    assert(s != NULL);
    assert(port > 0);

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
    assert(s != NULL);
    assert(event_count != NULL);

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
    assert(s != NULL);

    for (int i = 0; i < event_count; i++) {
        const struct kevent* const e = &s->event_list[i];
        const int fd = e->ident;

        if (fd == s->fd) {  // New connection to accept
            server_accept_new_connection(s);
            continue;
        }

        LOG("[D008] Data to be read on: %d\n", fd);
        conn_handle* ch = server_find_conn_handle_by_fd(s, fd);
        assert(ch != NULL);
        assert(ch->fd == fd);

        // Connection gone
        if (e->flags & EV_EOF) {
            server_remove_connection(s, ch);
            continue;
        }

        int res = 0;
        if ((res = conn_handle_read_request(ch)) <= 0) {
            assert(ch->fd == fd);
            if (res == -2) {  // Need more data
                assert(ch->fd == fd);
                continue;
            }
            assert(ch->fd == fd);
            server_remove_connection(s, ch);
            continue;
        }

        assert(ch->fd == fd);
        conn_handle_send_response(ch);
        assert(ch->fd == fd);
        server_remove_connection(s, ch);
    }
}

static int server_run(server* s, u16 port) {
    assert(s != NULL);
    assert(port > 0);

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
    printf("sizeof(http_req)=%lu sizeof(conn_handle)=%lu\n", sizeof(http_req),
           sizeof(conn_handle));
    if (argc != 2) {
        print_usage(argc, argv);
        return 0;
    }

    const u64 port = gb_str_to_u64(argv[1], NULL, 10);
    if (port == 0 || port > UINT16_MAX) {
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
