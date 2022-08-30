#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
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
#include "../vendor/gb/gb.h"
#include "vendor/picohttpparser/picohttpparser.h"

#define IP_ADDR_STR_LEN 17
#define CONN_BUF_LEN 4096

static bool verbose = true;
static MDB_env* env = NULL;

#define LOG(fmt, ...)                                     \
    do {                                                  \
        if (verbose) fprintf(stderr, fmt, ##__VA_ARGS__); \
    } while (0)

static void ip(uint32_t val, char* res) {
    uint8_t a = val >> 24, b = val >> 16, c = val >> 8, d = val & 0xff;
    snprintf(res, 16, "%hhu.%hhu.%hhu.%hhu", d, c, b, a);
}

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
    u8 num_headers;
    struct phr_header headers[50];
} http_req_t;

static bool str_eq(const char* a, u64 a_len, const char* b, u64 b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static bool str_eq0(const char* a, u64 a_len, const char* b0) {
    const u64 b_len = strlen(b0);
    return str_eq(a, a_len, b0, b_len);
}

static int http_parse_request(http_req_t* req, gbString buf, u64 prev_buf_len) {
    assert(req != NULL);

    const char* method = NULL;
    const char* path = NULL;
    usize method_len = 0;
    usize path_len = 0;
    int minor_version = 0;
    struct phr_header headers[100] = {0};
    usize num_headers = sizeof(headers) / sizeof(headers[0]);

    int res = phr_parse_request(buf, gb_string_length(buf), &method,
                                &method_len, &path, &path_len, &minor_version,
                                headers, &num_headers, prev_buf_len);

    LOG("phr_parse_request: res=%d\n", res);
    if (res == -1) {
        LOG("Failed to phr_parse_request:\n");
        return res;
    }
    if (res == -2) {
        LOG("Partial http parse, need more data\n");
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

    if (num_headers >= UINT8_MAX ||
        num_headers >= sizeof(headers) / sizeof(headers[0])) {
        LOG("Invalid headers");
        return EINVAL;
    }
    req->num_headers = num_headers;
    /* memcpy(req->headers, headers, num_headers * sizeof(headers[0])); */

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

static int db_put(MDB_env* env, char* key, char* value) {
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

    MDB_val mdb_key = {.mv_data = key, .mv_size = strlen(key)},
            mdb_value = {.mv_data = value, .mv_size = strlen(value)};

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

static int db_get(MDB_env* env, char* key) {
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

    MDB_val mdb_key = {.mv_data = key, .mv_size = strlen(key)}, mdb_value = {0};
    if ((err = mdb_get(txn, dbi, &mdb_key, &mdb_value)) != 0) {
        fprintf(stderr, "Failed to mdb_get: err=%s\n", mdb_strerror(err));
        goto end;
    }
    printf("key=%.*s value=%.*s\n", (int)mdb_key.mv_size,
           (char*)mdb_key.mv_data, (int)mdb_value.mv_size,
           (char*)mdb_value.mv_data);

end:
    if (txn != NULL) mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);

    return err;
}

static gbString app_handle(const http_req_t* http_req) {
    gbString body = NULL;
    if (str_eq0(http_req->path, http_req->path_len, "/get-todos") &&
        http_req->method == HM_GET) {
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
        body = gb_string_make_reserve(gb_heap_allocator(), 512);

        for (u64 i = 0; i < (u64)gb_array_count(kvs); i++) {
            const kv_t* const kv = &kvs[i];

            body = gb_string_append_fmt(body, "%.*s: %.*s\n", kv->key.mv_size,
                                        kv->key.mv_data, kv->value.mv_size,
                                        kv->value.mv_data);
        }
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
                         gb_string_length(body), body);

    return res;
}

static void handle_connection(struct sockaddr_in client_addr, int conn_fd) {
    char ip_addr[IP_ADDR_STR_LEN] = "";
    ip(client_addr.sin_addr.s_addr, ip_addr);

    gbString req = gb_string_make_reserve(gb_heap_allocator(), 256);
    int err = 0;
    http_req_t http_req = {0};

    while (1) {
        if (gb_string_available_space(req) <= 256)
            req = gb_string_make_space_for(req, 256);
        ssize_t received = recv(conn_fd, &req[gb_string_length(req)],
                                gb_string_available_space(req), 0);
        if (received == -1) {
            fprintf(stderr, "Failed to recv(2): addr=%s:%hu err=%s\n", ip_addr,
                    client_addr.sin_port, strerror(errno));
            return;
        }
        if (received == 0) {  // Client closed connection
            return;
        }
        gb__set_string_length(req, gb_string_length(req) + received);
        const isize len = gb_string_length(req);

        // End of request ?
        // TODO: limit on received bytes total
        if (len >= 4 && req[len - 4] == '\r' && req[len - 3] == '\n' &&
            req[len - 2] == '\r' && req[len - 1] == '\n') {
            break;
        }
    }

    err = http_parse_request(&http_req, (char*)req, 0);

    if (err != 0) {
        fprintf(stderr,
                "Failed to parse http request: addr=%s:%hu res=%d "
                "received=%zd\n",
                ip_addr, client_addr.sin_port, err, gb_array_count(req));
        return;
    }

    gbString res = app_handle(&http_req);

    u64 written = 0;
    const u64 total = gb_string_length(res);
    while (written < total) {
        int sent = send(conn_fd, &res[written], total - written, 0);
        if (sent == -1) {
            fprintf(stderr, "Failed to send(2): addr=%s:%hu err=%s\n", ip_addr,
                    client_addr.sin_port, strerror(errno));
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
            close(conn_fd);
        } else if (pid == 0) {  // Child
            handle_connection(client_addr, conn_fd);
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
