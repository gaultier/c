#include <assert.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include <../vendor/gb/gb.h>

#include "vendor/picohttpparser/picohttpparser.h"

#define CONN_BUF_LEN 2048
#define RESPONSE_BODY_CAP 2048
#define CONN_BUF_LEN_MAX 10 * 1024 * 1024  // 10 MiB
#define LISTEN_BACKLOG 512

typedef struct {
    u8 nprocs;
    u32 connection_max_duration_seconds;
    u16 port;
} options_t;
static bool verbose = false;

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
    http_method method;
    u16 path_len;
    u8 num_headers;
    struct phr_header headers[50];
} http_req;

typedef struct {
    gbString content_type;
    gbString body;
    gbString response;
    u16 status;
} http_res;

typedef enum {
    CHS_INIT,
    CHS_PARSED_REQ,
    CHS_PARTIALLY_SENT_RES,
    CHS_TOTALLY_SENT_RES,
} conn_handle_state;

typedef struct {
    struct timeval start;
    gbArray(char) req_buf;
    http_req req;
    int socket_fd;  // For recv(2)/send(2)
    // For sendfile(2)
    int sendfile_file_fd;
    u64 sendfile_total_file_bytes_sent;
    u64 sendfile_total_file_bytes_to_send;
    http_res res;
    char res_buf[CONN_BUF_LEN];
    conn_handle_state state;
    gbPool pool;
} conn_handle;

typedef struct {
    int fd;
    int queue;
    gbAllocator allocator;
    gbArray(conn_handle) conn_handles;
    struct kevent event_list[LISTEN_BACKLOG];
    latency_histogram hist;
    u64 requests_in_flight;
    options_t opts;
} server;

#define LOG(fmt, ...)                                     \
    do {                                                  \
        if (verbose) fprintf(stderr, fmt, ##__VA_ARGS__); \
    } while (0)

static bool str_eq(const char* a, u64 a_len, const char* b, u64 b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}
static bool str_eq0(const char* a, u64 a_len, const char* b0) {
    const u64 b_len = strlen(b0);
    return str_eq(a, a_len, b0, b_len);
}

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
    if (str_eq0(method, method_len, "GET"))
        req->method = HM_GET;
    else if (str_eq0(method, method_len, "POST"))
        req->method = HM_POST;
    else if (str_eq0(method, method_len, "PUT"))
        req->method = HM_PUT;
    else if (str_eq0(method, method_len, "PATCH"))
        req->method = HM_PATCH;
    else if (str_eq0(method, method_len, "DELETE"))
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

static void conn_handle_init(conn_handle* ch, gbAllocator allocator,
                             int socket_fd) {
    ch->socket_fd = socket_fd;
    gettimeofday(&ch->start, NULL);

    gb_array_init_reserve(ch->req_buf, allocator, CONN_BUF_LEN_MAX);
    ch->req.num_headers = sizeof(ch->req.headers) / sizeof(ch->req.headers[0]);

    gb_pool_init(&ch->pool, allocator, 50, 128);
    ch->res.response = gb_string_make_reserve(allocator, 256);
    ch->res.body = gb_string_make_reserve(allocator, 0);
}

static int conn_handle_read_request(conn_handle* ch, u64 nbytes_to_read) {
    assert(ch != NULL);

    const u64 prev_len = gb_array_count(ch->req_buf);
    u64 new_len = gb_array_count(ch->req_buf) + nbytes_to_read;
    if (new_len >= CONN_BUF_LEN_MAX) {
        return EINVAL;
    }
    const ssize_t received =
        read(ch->socket_fd, &ch->req_buf[prev_len], nbytes_to_read);
    if (received == -1) {
        fprintf(stderr, "Failed to read(2): err=%s\n", strerror(errno));
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
    ch->state = CHS_PARSED_REQ;
    return res;
}

static int server_init(server* s, gbAllocator allocator) {
    assert(s != NULL);

    s->allocator = allocator;

    LOG("[D001] sock_fd=%d\n", s->fd);

    gb_array_init_reserve(s->conn_handles, s->allocator, LISTEN_BACKLOG);

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

static int server_add_listen_event(server* s) {
    assert(s != NULL);

    struct kevent events[1] = {0};
    EV_SET(&events[0], s->fd, EVFILT_READ, EV_ADD, 0, 0, 0);
    if (kevent(s->queue, events, 1, NULL, 0, NULL) == -1) {
        fprintf(stderr, "%s:%d:Failed to kevent(2): %s\n", __FILE__, __LINE__,
                strerror(errno));
        return errno;
    }
    return 0;
}

static int server_add_connection_events(server* s, int fd) {
    assert(s != NULL);

    struct kevent events[3] = {0};
    EV_SET(&events[0], fd, EVFILT_READ, EV_ADD, 0, 0, 0);
    EV_SET(&events[1], fd, EVFILT_WRITE, EV_ADD, 0, 0, 0);
    u8 events_count = 2;
    if (s->opts.connection_max_duration_seconds > 0) {
        EV_SET(&events[2], fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_SECONDS,
               s->opts.connection_max_duration_seconds, 0);
        events_count += 1;
    }

    if (kevent(s->queue, events, events_count, NULL, 0, NULL) == -1) {
        fprintf(stderr, "%s:%d:Failed to kevent(2): %s\n", __FILE__, __LINE__,
                strerror(errno));
        return errno;
    }
    return 0;
}

static int server_remove_timer_event(server* s, int fd) {
    assert(s != NULL);

    struct kevent event = {0};
    EV_SET(&event, fd, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);

    if (kevent(s->queue, &event, 1, NULL, 0, NULL)) {
        fprintf(stderr, "%s:%d:Failed to kevent(2): %s\n", __FILE__, __LINE__,
                strerror(errno));
        return errno;
    }
    return 0;
}

static void print_usage(int argc, char* argv[]) {
    printf(
        "%s (-p|--port) <port> [(-j|--processes) <number of processes>] "
        "[(-d|--connection-max-duration-seconds) <seconds>] [-h|--help]\n",
        argv[0]);
}

static int server_accept_new_connection(server* s) {
    assert(s != NULL);

    const int conn_fd = accept(s->fd, NULL, 0);
    if (conn_fd == -1) {
        // If we get EAGAIN, it means another process has accepted the
        // connection before us
        if (errno != EAGAIN)
            fprintf(stderr, "Failed to accept(2): %s\n", strerror(errno));

        return errno;
    }
    LOG("\n\n---------------- Request start "
        "(requests_in_flight=%llu)\n\n[D002] New conn: %d\n",
        s->requests_in_flight, conn_fd);

    int res = 0;
    if ((res = fd_set_non_blocking(conn_fd)) != 0) return res;

    server_add_connection_events(s, conn_fd);

    conn_handle ch = {0};
    conn_handle_init(&ch, s->allocator, conn_fd);
    assert(ch.socket_fd == conn_fd);
    gb_array_append(s->conn_handles, ch);

    s->requests_in_flight++;

    return 0;
}

static conn_handle* server_find_conn_handle_by_fd(server* s, int fd) {
    assert(s != NULL);

    for (int i = 0; i < gb_array_count(s->conn_handles); i++) {
        conn_handle* ch = &s->conn_handles[i];
        if (ch->socket_fd == fd) return ch;
    }
    return NULL;
}

static char* http_content_type_for_file(const char* ext, int ext_len) {
    assert(ext != NULL);

    if (str_eq0(ext, ext_len, "html")) return "text/html";
    if (str_eq0(ext, ext_len, "js")) return "application/javascript";
    if (str_eq0(ext, ext_len, "css"))
        return "text/css";
    else
        return "text/plain";
}

static int conn_handle_send(conn_handle* ch) {
    assert(ch != NULL);

    int written = 0;
    const int total = gb_string_length(ch->res.response);
    LOG("Responding: `%s`\n", ch->res.response);

    // TODO: non blocking partial writes
    while (written < total) {
        const int nb =
            send(ch->socket_fd, &ch->res.response[written], total - written, 0);
        if (nb == -1) {
            fprintf(stderr, "Failed to write(2): err=%s\n", strerror(errno));
            return errno;
        }
        written += nb;
    }
    return 0;
}

static void histogram_add_entry(latency_histogram* hist, float val) {
    assert(hist != NULL);

    LOG("histogram_add_entry: %f\n", val);

    for (int i = 0; i < sizeof(hist->buckets) / sizeof(hist->buckets[0]); i++) {
        latency_histogram_bucket* const bucket = &hist->buckets[i];
        if (bucket->upper_bound_milliseconds_excl > val) {
            bucket->count++;
            return;
        }
    }
    assert(0 && "Unreachable");
}

static void histogram_print(latency_histogram* hist) {
    assert(hist != NULL);

    for (int i = 0; i < sizeof(hist->buckets) / sizeof(hist->buckets[0]); i++) {
        latency_histogram_bucket* bucket = &hist->buckets[i];
        printf("Latency < %llu ms: %llu\n",
               bucket->upper_bound_milliseconds_excl, bucket->count);
        if (bucket->upper_bound_milliseconds_excl == UINT64_MAX) break;
    }
}

static void server_print_stats(server* s) {
    assert(s != NULL);
    i64 sum_len_req_buf = 0;
    i64 sum_cap_req_buf = 0;
    for (int i = 0; i < gb_array_count(s->conn_handles); i++) {
        const conn_handle* const ch = &s->conn_handles[i];
        sum_len_req_buf += gb_array_count(ch->req_buf);
        sum_cap_req_buf += gb_array_capacity(ch->req_buf);
    }
    const u64 total_mem =
        gb_array_capacity(s->conn_handles) * sizeof(conn_handle) +
        sum_cap_req_buf;

    printf(
        "++++++++++\nStats:\n pid=%d len(conn_handles)=%td "
        "cap(conn_handles)=%td "
        "sum(len(req_buf))=%lld sum(cap(req_buf))=%lld total_mem=%llu\n",
        getpid(), gb_array_count(s->conn_handles),
        gb_array_capacity(s->conn_handles), sum_len_req_buf, sum_cap_req_buf,
        total_mem);
    histogram_print(&s->hist);
    puts("++++++++++\n");
}

static void conn_handles_rm_swap(gbArray(conn_handle) conn_handles,
                                 conn_handle* ch) {
    assert(conn_handles <= ch &&
           ch <= &conn_handles[gb_array_count(conn_handles) - 1]);
    memmove(ch, &conn_handles[gb_array_count(conn_handles) - 1],
            sizeof(conn_handle));
    conn_handles[gb_array_count(conn_handles) - 1] = (conn_handle){0};
    gb_array_pop(conn_handles);
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
        s->requests_in_flight--;

        LOG("Removing connection: fd=%d remaining=%td "
            "cap(conn_handles)=%td "
            "lifetime=%fms\n",
            ch->socket_fd, gb_array_count(s->conn_handles),
            gb_array_capacity(s->conn_handles), total_msecs);
    }

    // Rm
    {
        // Closing the file descriptor also automatically removes the kevents
        // associated with it i.e. `EVFILT_READ` and `EVFILT_WRITE` but not
        // `EVFILT_TIMER`
        close(ch->socket_fd);
        server_remove_timer_event(s, ch->socket_fd);

        // Remove handle from array
        gb_array_free(ch->req_buf);
        conn_handles_rm_swap(s->conn_handles, ch);
    }

    LOG("\n\n---------------- Request end (requests_in_flight=%llu)\n\n",
        s->requests_in_flight);
}

static void conn_handle_write_response_status(http_res* res) {
    assert(res != NULL);
    assert(res->status < 600);

    if (res->status == 0) res->status = 200;

    res->response = gb_string_appendc(res->response, "HTTP/1.1 ");

    switch (res->status) {
        case 200:
            res->response = gb_string_appendc(res->response, "200 OK");
            break;
        case 404:
            res->response = gb_string_appendc(res->response, "404 Not Found");
            break;
        default:
            assert(0 && "Unimplemented");
    }
    res->response = gb_string_appendc(res->response, "\r\n");
    LOG("conn_handle_write_response_status: response=`%s`\n", res->response);
}

static void conn_handle_write_response(conn_handle* ch) {
    assert(ch != NULL);

    conn_handle_write_response_status(&ch->res);

    if (gb_string_length(ch->res.body) == 0) return;

    ch->res.response =
        gb_string_append_fmt(ch->res.response, "Content-Length: %llu\r\n",
                             gb_string_length(ch->res.body));
    ch->res.response = gb_string_appendc(ch->res.response, "\r\n");
    ch->res.response = gb_string_append(ch->res.response, ch->res.body);
}

static int conn_handle_serve_static_file(conn_handle* ch) {
    assert(ch != NULL);
    int res = 0;
    off_t len = 0;

    // First call
    if (ch->sendfile_file_fd <= 0) {
        // TODO: security

        if (ch->req.path_len >= PATH_MAX) {
            ch->res.status = 404;
            conn_handle_write_response(ch);
            return conn_handle_send(ch);
        }
        char path[PATH_MAX] = "";
        if (ch->req.path_len <= 1) {
            memcpy(path, "index.html", sizeof("index.html"));
        } else {
            memcpy(path, ch->req.path + 1 /* Skip leading slash */,
                   ch->req.path_len - 1);
        }

        const char* const ext = gb_path_extension(path);
        LOG("Serving static file `%s` ext=`%s`\n", path, ext);

        ch->sendfile_file_fd = open(path, O_RDONLY);
        if (ch->sendfile_file_fd == -1) {
            fprintf(stderr, "Failed to open(2): path=`%s` err=%s\n", path,
                    strerror(errno));
            ch->res.status = 404;
            conn_handle_write_response(ch);
            return conn_handle_send(ch);
        }
        struct stat st = {0};
        if (stat(path, &st) == -1) {
            fprintf(stderr, "Failed to stat(2): path=`%s` err=%s\n", path,
                    strerror(errno));
            close(ch->sendfile_file_fd);
            ch->res.status = 404;
            conn_handle_write_response(ch);
            return conn_handle_send(ch);
        }
        LOG("Serving static file `%s` size=%lld\n", path, st.st_size);
        ch->sendfile_total_file_bytes_to_send = st.st_size;

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

        res = sendfile(ch->sendfile_file_fd, ch->socket_fd, 0, &len,
                       &headers_trailers, 0);
        if (res == -1 && errno != EAGAIN) {
            fprintf(stderr, "Failed to sendfile(2): path=`%s` err=%s\n", path,
                    strerror(errno));
            close(ch->sendfile_file_fd);
            return -1;
        }
        LOG("sendfile(2): res=%d header_len=%zu len=%llu size=%llu\n", res,
            header.iov_len, len, st.st_size);

        if (len < header.iov_len) {
            // Too hard and unlikely to handle that case
            return -1;
        }
        ch->sendfile_total_file_bytes_sent = len - header.iov_len;

        if (ch->sendfile_total_file_bytes_sent ==
            ch->sendfile_total_file_bytes_to_send) {
            close(ch->sendfile_file_fd);
            LOG("Static file totally sent: path=`%s`\n", path);
            ch->state = CHS_TOTALLY_SENT_RES;
            return 0;
        }

        // More to send, will happen later when EVFILT_WRITE triggers for this
        // socket
        LOG("Static file partially sent: path=`%s`\n", path);
        ch->state = CHS_PARTIALLY_SENT_RES;
        return EAGAIN;
    }

    // Subsequent calls
    assert(ch->state == CHS_PARTIALLY_SENT_RES);
    assert(ch->sendfile_file_fd > 0);
    assert(ch->sendfile_total_file_bytes_to_send > 0);
    assert(len == 0);
    res = sendfile(ch->sendfile_file_fd, ch->socket_fd,
                   ch->sendfile_total_file_bytes_sent, &len, NULL, 0);
    if (res == -1 && errno != EAGAIN) {
        fprintf(stderr, "Failed to sendfile(2): socket_fd=%d err=%s\n",
                ch->socket_fd, strerror(errno));
        close(ch->sendfile_file_fd);
        return -1;
    }
    ch->sendfile_total_file_bytes_sent += len;
    if (len > 0)
        LOG("sendfile(2): res=%d len=%llu %llu/%llu\n", res, len,
            ch->sendfile_total_file_bytes_sent,
            ch->sendfile_total_file_bytes_to_send);
    LOG("sendfile(2): res=%d ch->sendfile_total_file_bytes_sent=%lld in_fd=%d "
        "out_fd=%d\n",
        res, ch->sendfile_total_file_bytes_sent, ch->sendfile_file_fd,
        ch->socket_fd);
    if (ch->sendfile_total_file_bytes_sent ==
        ch->sendfile_total_file_bytes_to_send) {
        close(ch->sendfile_file_fd);
        LOG("Static file totally sent: socket_fd=%d\n", ch->socket_fd);
        ch->state = CHS_TOTALLY_SENT_RES;
        return 0;
    }

    // More to send, will happen later when EVFILT_WRITE triggers for this
    // socket
    LOG("Static file partially sent: socket_fd=%d\n", ch->socket_fd);
    ch->state = CHS_PARTIALLY_SENT_RES;
    return EAGAIN;
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

static int conn_handle_make_response(conn_handle* ch) {
    assert(ch != NULL);

    if (ch->req.method != HM_GET) {
        ch->res.status = 404;  // TODO: support POST, etc
    } else {
        if (ch->req.path_len <= 1 ||
            str_ends_with(ch->req.path, ch->req.path_len, ".txt", 4) ||
            str_ends_with(ch->req.path, ch->req.path_len, ".csv", 4) ||
            str_ends_with(ch->req.path, ch->req.path_len, ".iso", 4) ||
            str_ends_with(ch->req.path, ch->req.path_len, ".html", 5) ||
            str_ends_with(ch->req.path, ch->req.path_len, ".js", 3) ||
            str_ends_with(ch->req.path, ch->req.path_len, ".css", 4)) {
            return conn_handle_serve_static_file(ch);
        }
    }

    // Dummy body for now
    ch->res.body =
        gb_string_append_length(ch->res.body, ch->req.path, ch->req.path_len);

    conn_handle_write_response(ch);
    return conn_handle_send(ch);
}

static int server_listen_and_bind(server* s) {
    assert(s != NULL);

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

    const u8 ip[4] = {127, 0, 0, 1};
    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = {.s_addr = *(u32*)ip},
        .sin_port = htons(s->opts.port),
    };

    if ((res = bind(s->fd, (const struct sockaddr*)&addr, sizeof(addr))) ==
        -1) {
        fprintf(stderr, "Failed to bind(2): %s\n", strerror(errno));
        return errno;
    }

    if (s->opts.nprocs != 1) {
        u8 nprocs = s->opts.nprocs;
        if (nprocs == 0) {
            gbAffinity affinity = {0};
            gb_affinity_init(&affinity);
            nprocs = affinity.thread_count;
        }
        for (int i = 1; i < nprocs; i++) {
            const pid_t pid = fork();
            if (pid == -1) {
                fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
                exit(errno);
            }
            if (pid == 0) {  // Child
                break;
            }
        }
    }

    // The file descriptor returned by kqueue(2) is not inherited after a fork
    // by the child so it needs to be created after the fork.
    s->queue = kqueue();
    if (s->queue == -1) {
        fprintf(stderr, "%s:%d:Failed to create queue with kqueue(): %s\n",
                __FILE__, __LINE__, strerror(errno));
        return errno;
    }

    if ((res = listen(s->fd, LISTEN_BACKLOG)) == -1) {
        fprintf(stderr, "Failed to listen(2): %s\n", strerror(errno));
        return errno;
    }
    server_add_listen_event(s);
    printf("Listening: :%d\n", s->opts.port);
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
        int res = 0;

        if (e->filter == EVFILT_TIMER) {
            LOG("Timer expired for: fd=%d\n", fd);
            conn_handle* ch = server_find_conn_handle_by_fd(s, fd);
            if (ch != NULL) {  // Might have already completed
                server_remove_connection(s, ch);
            }
        } else if (e->filter == EVFILT_READ &&
                   fd == s->fd) {  // New connection to accept
            server_accept_new_connection(s);
        } else if (e->filter == EVFILT_READ) {
            LOG("[D008] Data to be read on: %d\n", fd);
            conn_handle* const ch = server_find_conn_handle_by_fd(s, fd);
            if (ch == NULL)
                continue;  // Connection might have been removed already, skip

            assert(ch->socket_fd == fd);

            // Connection gone
            if (e->flags & EV_EOF) {
                server_remove_connection(s, ch);
                continue;
            }

            if ((res = conn_handle_read_request(ch, e->data)) <= 0) {
                assert(ch->socket_fd == fd);
                if (res == -2) {  // Need more data
                    assert(ch->socket_fd == fd);
                    continue;
                }
                assert(ch->socket_fd == fd);
                server_remove_connection(s, ch);
                continue;
            }

            assert(ch->socket_fd == fd);
            if ((res = conn_handle_make_response(ch)) == EAGAIN)
                continue;  // Don't remove the connection yet

            assert(ch->socket_fd == fd);
            server_remove_connection(s, ch);

        } else if (e->filter == EVFILT_WRITE) {
            LOG("Data to be written on: fd=%d\n", fd);
            conn_handle* const ch = server_find_conn_handle_by_fd(s, fd);
            if (ch == NULL)
                continue;  // Connection might have been removed already, skip
                           //
            assert(ch->socket_fd == fd);
            LOG("state=%d\n", ch->state);
            if (ch->state != CHS_PARTIALLY_SENT_RES) continue;  // Ignore

            if ((res = conn_handle_make_response(ch)) == EAGAIN)
                continue;  // Don't remove the connection yet

            server_remove_connection(s, ch);
        }
    }
}

static int server_run(server* s) {
    assert(s != NULL);

    int res = 0;
    res = server_listen_and_bind(s);
    if (res != 0) return res;

    while (1) {
        int event_count = 0;
        server_poll_events(s, &event_count);
        server_handle_events(s, event_count);
    }
    return 0;
}

static void options_parse_from_cli(int argc, char* argv[], options_t* opts) {
    assert(argv != NULL);
    assert(opts != NULL);

    struct option longopts[] = {
        {.name = "processes",
         .has_arg = required_argument,
         .flag = NULL,
         .val = 'j'},
        {.name = "port",
         .has_arg = required_argument,
         .flag = NULL,
         .val = 'p'},
        {.name = "connection-max-duration-seconds",
         .has_arg = required_argument,
         .flag = NULL,
         .val = 'd'},
        {.name = "help", .has_arg = no_argument, .flag = NULL, .val = 'h'},
        {.name = "verbose", .has_arg = no_argument, .flag = NULL, .val = 'v'},
    };

    int ch = 0;
    while ((ch = getopt_long(argc, argv, "vhj:d:p:", longopts, NULL)) != -1) {
        switch (ch) {
            case 'j': {
                const u64 nprocs = gb_str_to_u64(optarg, NULL, 10);
                if (nprocs > UINT8_MAX) {
                    fprintf(stderr, "Invalid process count: %llu. Max: %d\n",
                            nprocs, UINT8_MAX);
                    exit(EINVAL);
                }
                opts->nprocs = nprocs;
                break;
            }
            case 'p': {
                const u64 port = gb_str_to_u64(optarg, NULL, 10);
                if (port == 0 || port > UINT16_MAX) {
                    fprintf(stderr, "Invalid port number: %llu\n", port);
                    exit(EINVAL);
                }
                opts->port = port;
                break;
            }
            case 'd': {
                const u64 seconds = gb_str_to_u64(optarg, NULL, 10);
                if (seconds > UINT16_MAX) {
                    fprintf(stderr,
                            "Invalid connection-max-duration-seconds: %llu. "
                            "Max: %d\n",
                            seconds, UINT16_MAX);
                    exit(EINVAL);
                }
                opts->connection_max_duration_seconds = seconds;
                break;
            }
            case 'v':
                verbose = true;
                break;
            default:
                print_usage(argc, argv);
                exit(0);
        }
    }

    if (opts->port == 0) {
        fprintf(stderr, "Port (-p, --port) is required\n");
        print_usage(argc, argv);
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    options_t opts = {0};
    options_parse_from_cli(argc, argv, &opts);

    int res = 0;
    gbAllocator allocator = gb_heap_allocator();
    server s = {.opts = opts};
    if ((res = server_init(&s, allocator)) != 0) return res;
    if ((res = server_run(&s)) != 0) return res;
}
