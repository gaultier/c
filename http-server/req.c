#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "../pg/pg.h"

static void print_usage(int argc, char *argv[]) {
  assert(argc > 0);
  printf("%s <port> <delay_ms> <batch_size_max> <payload_length>\nGot:", argv[0]);
  for (int i = 0; i < argc; i++)
    printf("%s ", argv[i]);
  puts("");
}

static uint64_t clamp_u64(uint64_t val, uint64_t limit) {
  return val > limit ? limit : val;
}

static int request_send(int fd, uint32_t delay_ms, uint64_t batch_size_max,
                        uint64_t payload_len) {
  static char msg[1000000] = "POST /index.html HTTP/1.1\r\n"
                             "Host: localhost:12347\r\n"
                             "\r\n";
  assert(payload_len < sizeof(msg));
  uint64_t msg_len = strlen(msg);
  for (uint64_t i = 0; i < payload_len; i++)
    msg[msg_len++] = 'A';

  uint64_t total_sent = 0;

  struct timeval send_start = {0};
  gettimeofday(&send_start, NULL);
  while (total_sent < msg_len) {
    const uint64_t batch_size = clamp_u64(msg_len - total_sent, batch_size_max);
    fprintf(stderr, "[D001] batch_size=%llu\n", batch_size);
    const ssize_t sent = send(fd, &msg[total_sent], batch_size, 0);
    if (sent == -1) {
      fprintf(stderr, "Failed to send(2): %s\n", strerror(errno));
      return errno;
    } else if (sent == 0) {
      fprintf(stderr, "Failed to send(2): other side closed\n");
      return errno;
    }
    total_sent += (uint64_t)sent;

    fprintf(stderr, "Sent: sent=%llu total_sent=%llu\n", (uint64_t)sent,
            total_sent);
    if (delay_ms > 0)
      usleep(delay_ms * 1000);
  }
  struct timeval send_end = {0};
  gettimeofday(&send_end, NULL);
  const double send_duration_ms =
      ((double)send_end.tv_sec * 1000 + (double)send_end.tv_usec / 1000) -
      ((double)send_start.tv_sec * 1000 + (double)send_start.tv_usec / 1000);

  fprintf(stderr, "Request sent: %02fms\n", send_duration_ms);

  return 0;
}

static int response_receive(int fd, uint64_t batch_size_max) {
  uint64_t total_received = 0;
  char buf[4096] = "";
  while (total_received <= sizeof(buf)) {
    const ssize_t received = recv(fd, &buf[total_received], batch_size_max, 0);
    if (received == -1) {
      fprintf(stderr, "Failed to recv(2): %s\n", strerror(errno));
      return errno;
    }
    if (received == 0) {
      break;
    }
    printf("Partial received: %llu\n", (uint64_t)received);
    total_received += (uint64_t)received;
  }

  fprintf(stderr, "Received: total_received=%llu `%.*s`\n", total_received,
          (int)total_received, buf);
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 5) {
    print_usage(argc, argv);
    return 0;
  }

  bool valid = false;
  const uint64_t port =
      pg_span_parse_u64_decimal(pg_span_make_c(argv[1]), &valid);
  if (!valid || port > UINT16_MAX) {
    fprintf(stderr, "Invalid port number: %llu\n", port);
    return EINVAL;
  }

  const uint64_t delay_ms =
      pg_span_parse_u64_decimal(pg_span_make_c(argv[2]), &valid);
  if (!valid || delay_ms > UINT32_MAX) {
    fprintf(stderr, "Invalid delay: %llu\n", delay_ms);
    return EINVAL;
  }
  const uint64_t batch_size_max =
      pg_span_parse_u64_decimal(pg_span_make_c(argv[3]), &valid);
  if (!valid) {
    fprintf(stderr, "Invalid batch_size_max: %llu\n", delay_ms);
    return EINVAL;
  }
  const uint64_t payload_len =
      pg_span_parse_u64_decimal(pg_span_make_c(argv[4]), &valid);
  if (!valid) {
    fprintf(stderr, "Invalid payload_len: %llu\n", delay_ms);
    return EINVAL;
  }

  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
    return errno;
  }
  const struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
  };

  if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
    fprintf(stderr, "Failed to connect(2): %s\n", strerror(errno));
    return errno;
  }

  int res = 0;
  if ((res = request_send(fd, (uint32_t)delay_ms, batch_size_max,
                          payload_len)) != 0) {
    return res;
  }
  if ((res = response_receive(fd, batch_size_max)) != 0) {
    return res;
  }

  return 0;
}
