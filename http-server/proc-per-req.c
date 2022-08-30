#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"
#include "vendor/picohttpparser/picohttpparser.h"

#define IP_ADDR_STR_LEN 17
#define CONN_BUF_LEN 4096

static bool verbose = true;
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

static int handle_connection(struct sockaddr_in client_addr, int conn_fd) {
    char ip_addr[IP_ADDR_STR_LEN] = "";
    ip(client_addr.sin_addr.s_addr, ip_addr);
    /* printf("New connection: %s:%hu\n", ip_addr, client_addr.sin_port); */

    gbString req = gb_string_make_reserve(gb_heap_allocator(), 256);
    int err = 0;
    http_req_t http_req = {0};
    gbString res = NULL;

    while (1) {
        if (gb_string_available_space(req) <= 256)
            gb_string_make_space_for(req, 256);
        ssize_t received = recv(conn_fd, &req[gb_string_length(req)],
                                gb_string_available_space(req), 0);
        if (received == -1) {
            fprintf(stderr, "Failed to recv(2): addr=%s:%hu err=%s\n", ip_addr,
                    client_addr.sin_port, strerror(errno));
            err = errno;
            goto end;
        }
        if (received == 0) {  // Client closed connection
            goto end;
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
        goto end;
    }

    // Response
    res = gb_string_make_reserve(gb_heap_allocator(), 256);
    gb_string_append_fmt(res,
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain; charset=utf8\r\n"
                         "Content-Length: %td\r\n"
                         "\r\n"
                         "%.*s",
                         http_req.path_len, http_req.path_len, http_req.path);

    // TODO: send in loop
    int sent = send(conn_fd, res, gb_string_length(res), 0);
    if (sent == -1) {
        fprintf(stderr, "Failed to send(2): addr=%s:%hu err=%s\n", ip_addr,
                client_addr.sin_port, strerror(errno));
        err = errno;
        goto end;
    } else if (sent != gb_string_length(res)) {
        LOG("Partial send(2), FIXME");
    }

end:
    gb_string_free(req);
    if (res != NULL) gb_string_free(res);
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
    struct sigaction sa = {.sa_flags = SA_NOCLDWAIT};
    if ((err = sigaction(SIGCHLD, &sa, NULL)) == -1) {
        fprintf(stderr, "Failed to sigaction(2): err=%s\n", strerror(errno));
        exit(errno);
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
            err = handle_connection(client_addr, conn_fd);
            exit(err);
        } else {  // Parent
            // Fds are duplicated by fork(2) and need to be
            // closed by both parent & child
            close(conn_fd);
        }
    }
}
