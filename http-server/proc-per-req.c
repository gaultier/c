#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../pg/pg.h"
#include "../vendor/picohttpparser/picohttpparser.h"

static const uint64_t KiB = 1024;
static const uint64_t max_payload_length = 16 * KiB;
static const uint64_t timeout_seconds = 15;
static bool verbose = true;

#define LOG(fmt, ...)                                                          \
  do {                                                                         \
    if (verbose)                                                               \
      fprintf(stderr, fmt, ##__VA_ARGS__);                                     \
  } while (0)

static void print_usage(int argc, char *argv[]) {
  assert(argc > 0);
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
  pg_span_t path;
  uint64_t content_len;
  uint64_t headers_len;
  struct phr_header headers[50];
  http_method method;
  PG_PAD(4);
} http_req_t;

static int http_parse_request(http_req_t *req, pg_string_t buf,
                              uint64_t prev_buf_len) {
  assert(req != NULL);

  pg_span_t method = {0};
  pg_span_t path = {0};
  int minor_version = 0;
  uint64_t headers_len = sizeof(req->headers) / sizeof(req->headers[0]);

  int res = phr_parse_request(
      buf, pg_string_len(buf), (const char **)&method.data,
      (size_t *)&method.len, (const char **)&path.data, (size_t *)&path.len,
      &minor_version, req->headers, (size_t *)&headers_len, prev_buf_len);

  LOG("phr_parse_request: res=%d\n", res);
  if (res == -1) {
    LOG("Failed to phr_parse_request:\n");
    return res;
  }
  if (res == -2) {
    // Partial http parse, need more data
    return res;
  }
  if (method.len >= sizeof("CONNECT") - 1) { // Longest method
    LOG("Invalid method, too long: method_len=%llu method=%.*s", method.len,
        (int)method.len, method.data);
    return EINVAL;
  }
  if (pg_span_eq(method, pg_span_make_c("GET")))
    req->method = HM_GET;
  else if (pg_span_eq(method, pg_span_make_c("HEAD")))
    req->method = HM_HEAD;
  else if (pg_span_eq(method, pg_span_make_c("POST")))
    req->method = HM_POST;
  else if (pg_span_eq(method, pg_span_make_c("PUT")))
    req->method = HM_PUT;
  else if (pg_span_eq(method, pg_span_make_c("DELETE")))
    req->method = HM_DELETE;
  else if (pg_span_eq(method, pg_span_make_c("CONNECT")))
    req->method = HM_CONNECT;
  else if (pg_span_eq(method, pg_span_make_c("OPTIONS")))
    req->method = HM_OPTIONS;
  else if (pg_span_eq(method, pg_span_make_c("TRACE")))
    req->method = HM_TRACE;
  else if (pg_span_eq(method, pg_span_make_c("PATCH")))
    req->method = HM_PATCH;
  else {
    LOG("Unknown method: method=%.*s", (int)method.len, method.data);
    return EINVAL;
  }

  if (path.len >= 4096) {
    LOG("Invalid path, too long: path=%.*s", (int)path.len, path.data);
    return EINVAL;
  }
  req->path = path;

  if (req->headers_len >= UINT8_MAX ||
      req->headers_len >= sizeof(req->headers) / sizeof(req->headers[0])) {
    LOG("Invalid headers");
    return EINVAL;
  }
  req->headers_len = headers_len;

  for (uint64_t i = 0; i < headers_len; i++) {
    const struct phr_header *const header = &req->headers[i];

    const pg_span_t header_name = {.data = (char *)header->name,
                                   .len = header->name_len};
    const pg_span_t header_value = {.data = (char *)header->value,
                                    .len = header->value_len};
    if (pg_span_eq(header_name, pg_span_make_c("Content-Length"))) {
      bool content_length_valid = false;
      const uint64_t content_len =
          pg_span_parse_u64_decimal(header_value, &content_length_valid);
      if (!content_length_valid || content_len > UINT16_MAX)
        return EINVAL;
      req->content_len = content_len;
    }
  }

  LOG("method=%d path=%.*s\n", req->method, (int)req->path.len, req->path.data);
  return 0;
}

static pg_string_t app_handle(const http_req_t *http_req, const char *req_body,
                              uint64_t req_body_len) {
  (void)http_req;
  (void)req_body;
  (void)req_body_len;

  return pg_string_make(pg_heap_allocator(),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain; charset=utf8\r\n"
                        "Content-Length: 0\r\n"
                        "\r\n");
}

static void *timeout_background_worker_run(void *arg) {
  (void)arg;

  sleep(timeout_seconds);
  exit(0);
  return NULL;
}

static void handle_connection(int conn_fd) {
  pthread_t timeout_background_worker = {0};
  pthread_create(&timeout_background_worker, NULL,
                 timeout_background_worker_run, NULL);
  pg_string_t req =
      pg_string_make_reserve(pg_heap_allocator(), max_payload_length);
  int err = 0;
  http_req_t http_req = {0};

  uint64_t prev_len = 0;
  while (1) {
    const ssize_t received = recv(conn_fd, &req[pg_string_len(req)],
                                  pg_string_available_space(req), 0);
    if (received == -1) {
      fprintf(stderr, "Failed to recv(2): err=%s\n", strerror(errno));
      return;
    }
    if (received == 0) { // Client closed connection
      return;
    }
    prev_len = pg_string_len(req);
    pg__set_string_len(req, pg_string_len(req) + (uint64_t)received);
    LOG("Request length: %llu\n", pg_string_len(req));
    if (pg_string_available_space(req) == 0) {
      fprintf(stderr, "Request too big\n");
      return;
    }

    err = http_parse_request(&http_req, (char *)req, prev_len);

    if (err == -2) {
      continue;
    }
    if (err == -1) {
      fprintf(stderr,
              "Failed to parse http request: res=%d "
              "received=%llu\n",
              err, pg_string_len(req));
      return;
    }
    break;
  }
  LOG("Content-Length: %llu\n", http_req.content_len);
  // Perhaps we do not have the full body yet and we need to get the
  // Content-Length and read that amount
  const char *req_body = pg_memmem(req, pg_string_len(req), "\r\n\r\n", 4);
  uint64_t req_body_len = (uint64_t)(req + pg_string_len(req) - req_body);
  if (req_body != NULL) {
    req_body += 4;
    req_body_len -= 4;
    LOG("body=`%.*s`\n", (int)req_body_len, req_body);
  } else {
    req_body_len = 0;
  }

  pg_string_t res = app_handle(&http_req, req_body, req_body_len);
  LOG("res=%s\n", res);

  uint64_t written = 0;
  const uint64_t total = pg_string_len(res);
  while (written < total) {
    const ssize_t sent = send(conn_fd, &res[written], total - written, 0);
    if (sent == -1) {
      fprintf(stderr, "Failed to send(2): err=%s\n", strerror(errno));
      return;
    }
    written += total;
  }
  // Nothing to cleanup, since this process is going to exit right after
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    print_usage(argc, argv);
    return 0;
  }

  bool port_valid = false;
  const uint64_t port =
      pg_span_parse_u64_decimal(pg_span_make_c(argv[1]), &port_valid);
  if (!port_valid || port > UINT16_MAX) {
    fprintf(stderr, "Invalid port number: %llu\n", port);
    return 1;
  }

  int err = 0;
  struct sigaction sa = {.sa_flags = SA_NOCLDWAIT};
  if ((err = sigaction(SIGCHLD, &sa, NULL)) == -1) {
    fprintf(stderr, "Failed to sigaction(2): err=%s\n", strerror(errno));
    return errno;
  }

  const int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
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

  const uint8_t ip[4] = {127, 0, 0, 1};
  const struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr = {.s_addr = *(const uint32_t *)(const void *)ip},
      .sin_port = htons(12345),
  };

  if ((err = bind(sock_fd, (const struct sockaddr *)&addr, sizeof(addr))) ==
      -1) {
    fprintf(stderr, "Failed to bind(2): %s\n", strerror(errno));
    return errno;
  }

  if ((err = listen(sock_fd, 16 * 1024)) == -1) {
    fprintf(stderr, "Failed to listen(2): %s\n", strerror(errno));
    return errno;
  }

  while (1) {
    const int conn_fd = accept(sock_fd, NULL, 0);
    if (conn_fd == -1) {
      fprintf(stderr, "Failed to accept(2): %s\n", strerror(errno));
      return errno;
    }

    const pid_t pid = fork();
    if (pid == -1) {
      fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
      close(conn_fd);
    } else if (pid == 0) { // Child
      handle_connection(conn_fd);
      exit(0);
    } else { // Parent
      // Fds are duplicated by fork(2) and need to be
      // closed by both parent & child
      close(conn_fd);
    }
  }
}
