#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../vendor/lmdb/libraries/liblmdb/lmdb.h"

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../pg/pg.h"
#include "../vendor/gb/gb.h"
#include "vendor/picohttpparser/picohttpparser.h"

static const uint64_t KiB = 1024;
static const uint64_t max_payload_length = 16 * KiB;
static const uint64_t timeout_seconds = 15;
static bool verbose = true;
static MDB_env* env = NULL;

#define LOG(fmt, ...)                                     \
    do {                                                  \
        if (verbose) fprintf(stderr, fmt, ##__VA_ARGS__); \
    } while (0)

static void print_usage(int argc, char* argv[]) {
    GB_ASSERT(argc > 0);
    printf("%s <port>\n", argv[0]);
}

typedef enum {
    HM_GET,
    HM_HEAD,
    HM_POST,
    HM_PUT,
    HM_DELETE,
    HM_CONNECT,
    HM_OPTIONS,
    HM_TRACE,
    HM_PATCH,
} http_method;

typedef struct {
    const char* path;
    http_method method;
    u16 path_len;
    u16 content_len;
    u8 headers_len;
    struct phr_header headers[50];
} http_req_t;

static bool str_eq(const char* a, u64 a_len, const char* b, u64 b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static bool str_eq0(const char* a, u64 a_len, const char* b0) {
    const u64 b_len = strlen(b0);
    return str_eq(a, a_len, b0, b_len);
}

static bool str_starts_with0(const char* a, u64 a_len, const char* b0) {
    const u64 b_len = strlen(b0);
    return a_len >= b_len && str_eq(a, b_len, b0, b_len);
}

static u64 str_to_u64(const char* s, u64 s_len) {
    assert(s != NULL);

    u64 res = 0;
    for (u64 i = 0; i < s_len; i++) {
        const char c = s[i];
        if (pg_char_is_space(c)) continue;
        if (pg_char_is_digit(c)) {
            const int v = c - '0';
            res *= 10;
            res += v;
        } else
            return 0;
    }
    return res;
}

static int http_parse_request(http_req_t* req, gbString buf, u64 prev_buf_len) {
    assert(req != NULL);

    const char* method = NULL;
    const char* path = NULL;
    usize method_len = 0;
    usize path_len = 0;
    int minor_version = 0;
    usize headers_len = sizeof(req->headers) / sizeof(req->headers[0]);

    int res = phr_parse_request(buf, gb_string_length(buf), &method,
                                &method_len, &path, &path_len, &minor_version,
                                req->headers, &headers_len, prev_buf_len);

    LOG("phr_parse_request: res=%d\n", res);
    if (res == -1) {
        LOG("Failed to phr_parse_request:\n");
        return res;
    }
    if (res == -2) {
        // Partial http parse, need more data
        return res;
    }
    if (method_len >= sizeof("CONNECT") - 1) {  // Longest method
        LOG("Invalid method, too long: method_len=%zu method=%.*s", method_len,
            (int)method_len, method);
        return EINVAL;
    }
    if (str_eq0(method, method_len, "GET"))
        req->method = HM_GET;
    else if (str_eq0(method, method_len, "HEAD"))
        req->method = HM_HEAD;
    else if (str_eq0(method, method_len, "POST"))
        req->method = HM_POST;
    else if (str_eq0(method, method_len, "PUT"))
        req->method = HM_PUT;
    else if (str_eq0(method, method_len, "DELETE"))
        req->method = HM_DELETE;
    else if (str_eq0(method, method_len, "CONNECT"))
        req->method = HM_CONNECT;
    else if (str_eq0(method, method_len, "OPTIONS"))
        req->method = HM_OPTIONS;
    else if (str_eq0(method, method_len, "TRACE"))
        req->method = HM_TRACE;
    else if (str_eq0(method, method_len, "PATCH"))
        req->method = HM_PATCH;
    else {
        LOG("Unknown method: method=%.*s", (int)method_len, method);
        return EINVAL;
    }

    if (path_len >= 4096) {
        LOG("Invalid path, too long: path=%s", path);
        return EINVAL;
    }
    req->path = path;
    req->path_len = path_len;

    if (req->headers_len >= UINT8_MAX ||
        req->headers_len >= sizeof(req->headers) / sizeof(req->headers[0])) {
        LOG("Invalid headers");
        return EINVAL;
    }
    req->headers_len = headers_len;

    for (u64 i = 0; i < headers_len; i++) {
        const struct phr_header* const header = &req->headers[i];

        if (str_eq0(header->name, header->name_len, "Content-Length")) {
            const u64 content_len =
                str_to_u64(header->value, header->value_len);
            if (content_len > UINT16_MAX) return EINVAL;
            req->content_len = content_len;
        }
    }

    LOG("method=%d path=%.*s\n", req->method, req->path_len, req->path);
    return 0;
}

typedef struct {
    MDB_val key, value;
} kv_t;

static int db_scan(gbArray(kv_t) kvs) {
    int err = 0;
    MDB_txn* txn = NULL;
    MDB_dbi dbi = {0};
    MDB_cursor* cursor = NULL;

    if ((err = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn)) != 0) {
        fprintf(stderr, "Failed to mdb_txn_begin: err=%s\n", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_dbi_open(txn, NULL, 0, &dbi)) != 0) {
        fprintf(stderr, "Failed to mdb_dbi_open: err=%s", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_cursor_open(txn, dbi, &cursor)) != 0) {
        fprintf(stderr, "Failed to mdb_cursor_open: err=%s\n",
                mdb_strerror(err));
        goto end;
    }
    MDB_val key = {0}, value = {0};
    while ((err = mdb_cursor_get(cursor, &key, &value, MDB_NEXT)) == 0) {
        char* key_data = malloc(key.mv_size);
        memcpy(key_data, key.mv_data, key.mv_size);
        char* value_data = malloc(value.mv_size);
        memcpy(value_data, value.mv_data, value.mv_size);

        gb_array_append(
            kvs,
            ((kv_t){
                .key = (MDB_val){.mv_data = key_data, .mv_size = key.mv_size},
                .value =
                    (MDB_val){.mv_data = value_data, .mv_size = value.mv_size},
            }));
    }
    if (err == MDB_NOTFOUND) {
        err = 0;  // Not really an error
    } else {
        fprintf(stderr, "Failed to mdb_cursor_get: err=%s\n",
                mdb_strerror(err));
        goto end;
    }

end:
    if (cursor != NULL) mdb_cursor_close(cursor);
    if (txn != NULL) mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);

    return err;
}

static int db_put(MDB_env* env, char* key, uint64_t key_len, char* value,
                  uint64_t value_len) {
    int err = 0;
    MDB_txn* txn = NULL;
    MDB_dbi dbi = {0};
    if ((err = mdb_txn_begin(env, NULL, 0, &txn)) != 0) {
        fprintf(stderr, "Failed to mdb_txn_begin: err=%s", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_dbi_open(txn, NULL, 0, &dbi)) != 0) {
        fprintf(stderr, "Failed to mdb_dbi_open: err=%s", mdb_strerror(err));
        goto end;
    }

    MDB_val mdb_key = {.mv_data = key, .mv_size = key_len},
            mdb_value = {.mv_data = value, .mv_size = value_len};

    if ((err = mdb_put(txn, dbi, &mdb_key, &mdb_value, 0)) != 0) {
        fprintf(stderr, "Failed to mdb_put: err=%s", mdb_strerror(err));
        goto end;
    }
    if ((err = mdb_txn_commit(txn)) != 0) {
        fprintf(stderr, "Failed to mdb_txn_commit: err=%s", mdb_strerror(err));
        goto end;
    }

end:
    mdb_dbi_close(env, dbi);

    return err;
}

static int db_get(char* key, u64 key_len, MDB_val* value) {
    int err = 0;
    MDB_txn* txn = NULL;
    MDB_dbi dbi = {0};

    if ((err = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn)) != 0) {
        fprintf(stderr, "Failed to mdb_txn_begin: err=%s\n", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_dbi_open(txn, NULL, 0, &dbi)) != 0) {
        fprintf(stderr, "Failed to mdb_dbi_open: err=%s", mdb_strerror(err));
        goto end;
    }

    MDB_val mdb_key = {.mv_data = key, .mv_size = key_len};
    if ((err = mdb_get(txn, dbi, &mdb_key, value)) != 0) {
        fprintf(stderr, "Failed to mdb_get: err=%s\n", mdb_strerror(err));
        goto end;
    }

end:
    if (txn != NULL) mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);

    return err;
}

static gbString app_handle(const http_req_t* http_req, char* req_body,
                           u64 req_body_len) {
    if (req_body_len < 2 * sizeof(uint64_t))
        return gb_string_make(gb_heap_allocator(),
                              "HTTP/1.1 500 Internal Error\r\n"
                              "Content-Type: text/plain; charset=utf8\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n");

    gbString res_body = NULL;
    if ((str_eq0(http_req->path, http_req->path_len, "/list-todos") &&
         http_req->method == HM_GET) ||
        (str_eq0(http_req->path, http_req->path_len, "/list-todos/") &&
         http_req->method == HM_GET)) {
        gbArray(kv_t) kvs = {0};
        gb_array_init_reserve(kvs, gb_heap_allocator(), 10);
        int err = 0;
        if ((err = db_scan(kvs)) != 0) {
            return gb_string_make(gb_heap_allocator(),
                                  "HTTP/1.1 500 Internal Error\r\n"
                                  "Content-Type: text/plain; charset=utf8\r\n"
                                  "Content-Length: 0\r\n"
                                  "\r\n");
        }
        res_body = gb_string_make_reserve(gb_heap_allocator(), 512);

        for (u64 i = 0; i < (u64)gb_array_count(kvs); i++) {
            const kv_t* const kv = &kvs[i];

            res_body = gb_string_append_fmt(
                res_body, "%.*s: %.*s\n", kv->key.mv_size, kv->key.mv_data,
                kv->value.mv_size, kv->value.mv_data);
        }
    } else if (str_starts_with0(http_req->path, http_req->path_len,
                                "/get-todo?key=") &&
               http_req->method == HM_GET) {
        char* key_param = memmem(http_req->path, http_req->path_len, "=", 1);
        assert(key_param != NULL);
        u64 key_param_len = http_req->path + http_req->path_len - key_param;
        if (key_param_len == 1) {
            return gb_string_make(gb_heap_allocator(),
                                  "HTTP/1.1 404 Not Found\r\n"
                                  "Content-Type: text/plain; charset=utf8\r\n"
                                  "Content-Length: 0\r\n"
                                  "\r\n");
        }
        // Skip '='
        key_param++;
        key_param_len--;

        int err = 0;
        MDB_val value = {0};
        if ((err = db_get(key_param, key_param_len, &value)) != 0) {
            if (err == MDB_NOTFOUND) {
                return gb_string_make(
                    gb_heap_allocator(),
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: text/plain; charset=utf8\r\n"
                    "Content-Length: 0\r\n"
                    "\r\n");
            }
            return gb_string_make(gb_heap_allocator(),
                                  "HTTP/1.1 500 Internal Error\r\n"
                                  "Content-Type: text/plain; charset=utf8\r\n"
                                  "Content-Length: 0\r\n"
                                  "\r\n");
        }

        res_body = gb_string_make_length(gb_heap_allocator(), value.mv_data,
                                         value.mv_size);

    } else if (http_req->method == HM_POST &&
               str_starts_with0(http_req->path, http_req->path_len,
                                "/create-todo")) {
        char* data = req_body;
        MDB_val key = {0}, value = {0};
        key.mv_size = *(uint64_t*)data;
        data += sizeof(uint64_t);
        key.mv_data = data;

        data += key.mv_size;
        value.mv_size = *(uint64_t*)data;
        data += sizeof(uint64_t);
        value.mv_data = data;
        printf("%zu `%.*s` %zu `%.*s`\n", key.mv_size, (int)key.mv_size,
               (char*)key.mv_data, value.mv_size, (int)value.mv_size,
               (char*)value.mv_data);

        int err = 0;
        if ((err = db_put(env, key.mv_data, key.mv_size, value.mv_data,
                          value.mv_size)) != 0) {
            return gb_string_make(gb_heap_allocator(),
                                  "HTTP/1.1 500 Internal Error\r\n"
                                  "Content-Type: text/plain; charset=utf8\r\n"
                                  "Content-Length: 0\r\n"
                                  "\r\n");
        }

        return gb_string_make(gb_heap_allocator(),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/plain; charset=utf8\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n");

    } else {
        return gb_string_make(gb_heap_allocator(),
                              "HTTP/1.1 404 Not Found\r\n"
                              "Content-Type: text/plain; charset=utf8\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n");
    }

    gbString res = gb_string_make_reserve(gb_heap_allocator(), 256);
    gb_string_append_fmt(res,
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain; charset=utf8\r\n"
                         "Content-Length: %td\r\n"
                         "\r\n"
                         "%s",
                         gb_string_length(res_body), res_body);

    return res;
}

static void* timeout_background_worker_run(void* arg) {
    (void)arg;

    sleep(timeout_seconds);
    exit(0);
    return NULL;
}

static void handle_connection(int conn_fd) {
    pthread_t timeout_background_worker = {0};
    pthread_create(&timeout_background_worker, NULL,
                   timeout_background_worker_run, NULL);
    gbString req =
        gb_string_make_reserve(gb_heap_allocator(), max_payload_length);
    int err = 0;
    http_req_t http_req = {0};

    u64 prev_len = 0;
    while (1) {
        ssize_t received = recv(conn_fd, &req[gb_string_length(req)],
                                gb_string_available_space(req), 0);
        if (received == -1) {
            fprintf(stderr, "Failed to recv(2): err=%s\n", strerror(errno));
            return;
        }
        if (received == 0) {  // Client closed connection
            return;
        }
        prev_len = gb_string_length(req);
        gb__set_string_length(req, gb_string_length(req) + received);
        LOG("Request length: %td\n", gb_string_length(req));
        if (gb_string_available_space(req) == 0) {
            fprintf(stderr, "Request too big\n");
            return;
        }

        err = http_parse_request(&http_req, (char*)req, prev_len);

        if (err == -2) {
            continue;
        }
        if (err == -1) {
            fprintf(stderr,
                    "Failed to parse http request: res=%d "
                    "received=%zd\n",
                    err, gb_array_count(req));
            return;
        }
        break;
    }
    LOG("Content-Length: %d\n", http_req.content_len);
    // Perhaps we do not have the full body yet and we need to get the
    // Content-Length and read that amount
    char* req_body = memmem(req, gb_string_length(req), "\r\n\r\n", 4);
    u64 req_body_len = req + gb_string_length(req) - req_body;
    if (req_body != NULL) {
        req_body += 4;
        req_body_len -= 4;
        LOG("body=`%.*s`\n", (int)req_body_len, req_body);
    } else {
        req_body_len = 0;
    }

    gbString res = app_handle(&http_req, req_body, req_body_len);
    LOG("res=%s\n", res);

    u64 written = 0;
    const u64 total = gb_string_length(res);
    while (written < total) {
        int sent = send(conn_fd, &res[written], total - written, 0);
        if (sent == -1) {
            fprintf(stderr, "Failed to send(2): err=%s\n", strerror(errno));
            return;
        }
        written += total;
    }
    // Nothing to cleanup, since this process is going to exit right after
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
    struct sigaction sa = {.sa_flags = SA_NOCLDWAIT};
    if ((err = sigaction(SIGCHLD, &sa, NULL)) == -1) {
        fprintf(stderr, "Failed to sigaction(2): err=%s\n", strerror(errno));
        return errno;
    }

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

    const u8 ip[4] = {127, 0, 0, 1};
    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = {.s_addr = *(u32*)ip},
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

    if ((err = mdb_env_create(&env)) != 0) {
        fprintf(stderr, "Failed to mdb_env_create: err=%s", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_env_open(env, "./testdb", MDB_NOSUBDIR, 0664)) != 0) {
        fprintf(stderr, "Failed to mdb_env_open: err=%s", mdb_strerror(err));
        goto end;
    }

    while (1) {
        int conn_fd = accept(sock_fd, NULL, 0);
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
end:
    if (env != NULL) mdb_env_close(env);
}
