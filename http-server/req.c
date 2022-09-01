#include <_types/_uint64_t.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"
#include "vendor/picohttpparser/picohttpparser.h"

static void print_usage(int argc, char* argv[]) {
    GB_ASSERT(argc > 0);
    printf("%s <port> <delay_ms> <batch_size_max>\nGot:", argv[0]);
    for (int i = 0; i < argc; i++) printf("%s ", argv[i]);
    puts("");
}

static u64 clamp_u64(u64 val, u64 limit) { return val > limit ? limit : val; }

static int request_send(int fd, u64 delay_ms, u64 batch_size_max,
                        u64 payload_len) {
    static char msg[1000000] =
        "POST /index.html HTTP/1.1\r\n"
        "Host: localhost:12347\r\n"
        "\r\n";
    assert(payload_len < sizeof(msg));
    u64 msg_len = strlen(msg);
    for (uint64_t i = 0; i < payload_len; i++) msg[msg_len++] = 'A';

    i64 sent = 0;
    u64 total_sent = 0;

    struct timeval send_start = {0};
    gettimeofday(&send_start, NULL);
    while (total_sent < msg_len) {
        const u64 batch_size = clamp_u64(msg_len - total_sent, batch_size_max);
        fprintf(stderr, "[D001] batch_size=%llu\n", batch_size);
        sent = send(fd, &msg[total_sent], batch_size, 0);
        if (sent == -1) {
            fprintf(stderr, "Failed to send(2): %s\n", strerror(errno));
            return errno;
        }
        total_sent += sent;

        fprintf(stderr, "Sent: sent=%llu total_sent=%llu\n", sent, total_sent);
        if (delay_ms > 0) usleep(delay_ms * 1000);
    }
    struct timeval send_end = {0};
    gettimeofday(&send_end, NULL);
    const float send_duration_ms =
        (send_end.tv_sec * 1000.0 + send_end.tv_usec / 1000.0) -
        (send_start.tv_sec * 1000.0 + send_start.tv_usec / 1000.0);

    fprintf(stderr, "Request sent: %02fms\n", send_duration_ms);

    return 0;
}

static int response_receive(int fd, u64 batch_size_max) {
    i64 received = 0;
    u64 total_received = 0;
    char buf[4096] = "";
    while (total_received <= sizeof(buf)) {
        received = recv(fd, &buf[total_received], batch_size_max, 0);
        if (received == -1) {
            fprintf(stderr, "Failed to recv(2): %s\n", strerror(errno));
            return errno;
        }
        if (received == 0) {
            break;
        }
        printf("Partial received: %llu\n", received);
        total_received += received;
    }

    fprintf(stderr, "Received: total_received=%llu `%.*s`\n", total_received,
            (int)total_received, buf);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        print_usage(argc, argv);
        return 0;
    }

    const u64 port = gb_str_to_u64(argv[1], NULL, 10);
    if (port > UINT16_MAX) {
        fprintf(stderr, "Invalid port number: %llu\n", port);
        return EINVAL;
    }

    const u64 delay_ms = gb_str_to_u64(argv[2], NULL, 10);
    const u64 batch_size_max = gb_str_to_u64(argv[3], NULL, 10);
    const u64 payload_len = gb_str_to_u64(argv[4], NULL, 10);

    int fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
        return errno;
    }
    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if (connect(fd, (void*)&addr, sizeof(addr)) == -1) {
        fprintf(stderr, "Failed to connect(2): %s\n", strerror(errno));
        return errno;
    }

    int res = 0;
    if ((res = request_send(fd, delay_ms, batch_size_max, payload_len)) != 0) {
        return res;
    }
    if ((res = response_receive(fd, batch_size_max)) != 0) {
        return res;
    }

    return 0;
}
