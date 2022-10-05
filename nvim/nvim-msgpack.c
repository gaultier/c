#include <_types/_uint32_t.h>
#include <_types/_uint64_t.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include "vendor/msgpack-c/include/msgpack.h"
#include "vendor/msgpack-c/include/msgpack/pack.h"

static void handle_connection(int conn_fd) {
    static uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    uint16_t buf_len = 0;

    msgpack_unpacked msg;
    msgpack_unpacked_init(&msg);
    msgpack_object obj = {0};
    while (1) {
        int err = read(conn_fd, &buf[buf_len], sizeof(buf) - buf_len);
        if (err == -1) {
            fprintf(stderr, "Failed to read from socket: %d %s", errno,
                    strerror(errno));
            exit(errno);
        }
        if (err == 0) break;

        for (uint16_t i = buf_len; i < buf_len + err; i++) {
            printf("%#0x ", buf[i]);
        }
        buf_len += err;

        msgpack_unpack_return ret =
            msgpack_unpack_next(&msg, (const char*)buf, buf_len, NULL);
        printf("msgpack: ret=%d\n", ret);
        if (ret != MSGPACK_UNPACK_SUCCESS) {
            continue;  // Need more data
        }
        obj = msg.data;
        msgpack_object_print(stdout, obj);
        break;
    }
    printf("\nRead: len=%d\n", buf_len);

    if (obj.type != MSGPACK_OBJECT_ARRAY) {
        fprintf(stderr, "Malformed requests, expected array, got: %d\n",
                obj.type);
        exit(EINVAL);
    }
    if (obj.via.array.size != 4) {
        fprintf(stderr, "Malformed requests, expected 4 elements, got: %d\n",
                obj.via.array.size);
        exit(EINVAL);
    }

    msgpack_object msg_id = obj.via.array.ptr[1];
    if (msg_id.type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
        msg_id.via.u64 > UINT32_MAX) {
        fprintf(stderr, "Invalid msg id, expected uint32_t, got:");
        msgpack_object_print(stderr, msg_id);
        exit(EINVAL);
    }
    const uint32_t msg_id_u32 = msg_id.via.u64;

    // Response
    msgpack_sbuffer* buffer = msgpack_sbuffer_new();
    msgpack_packer* pk = msgpack_packer_new(buffer, msgpack_sbuffer_write);
    msgpack_pack_array(pk, 4);
    msgpack_pack_int(pk, 1);           // RESPONSE
    msgpack_pack_int(pk, msg_id_u32);  // MSG ID
    msgpack_pack_nil(pk);              // ERROR
    msgpack_pack_str_with_body(pk, "hello world",
                               sizeof("hello world") - 1);  // RESULT

    uint64_t sent = 0;
    printf("Sending: ");
    while (sent < buffer->size) {
        int ret = send(conn_fd, buffer->data + sent, buffer->size - sent, 0);
        if (ret == -1) {
            fprintf(stderr, "Failed to send(2): %d %s\n", errno,
                    strerror(errno));
            exit(errno);
        }
        if (ret == 0) {
            fprintf(stderr, "Other side closed\n");
            exit(0);
        }
        for (uint64_t i = sent; i < sent + ret; i++) {
            printf("%02x ", buffer->data[i]);
        }
        sent += ret;
    }

    msgpack_sbuffer_free(buffer);
    msgpack_packer_free(pk);
    msgpack_unpacked_destroy(&msg);

    sleep(99);  // FIXME
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
        return errno;
    }

    int val = 1;
    int err = 0;
    if ((err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))) ==
        -1) {
        fprintf(stderr, "Failed to setsockopt(2): %s\n", strerror(errno));
        return errno;
    }

    const uint8_t ip[4] = {127, 0, 0, 1};
    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = {.s_addr = *(uint32_t*)ip},
        .sin_port = htons(12345),
    };

    if ((err = bind(fd, (const struct sockaddr*)&addr, sizeof(addr))) == -1) {
        fprintf(stderr, "Failed to bind(2): %s\n", strerror(errno));
        return errno;
    }

    if ((err = listen(fd, 16 * 1024)) == -1) {
        fprintf(stderr, "Failed to listen(2): %s\n", strerror(errno));
        return errno;
    }
    while (1) {
        int conn_fd = accept(fd, NULL, 0);
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
}
