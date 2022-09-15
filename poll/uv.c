#include <assert.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>
static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
        printf("[%s] Read: %.*s\n", (char*)stream->data, (int)nread, buf->base);
    }
}

static void my_alloc_cb(uv_handle_t* handle, size_t suggested_size,
                        uv_buf_t* buf) {
    (void)handle;
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

static void on_connect(uv_connect_t* handle, int status) {
    printf("[%s] on_connect: %d\n", (char*)handle->handle->data, status);
}

int main() {
    uv_loop_t* loop = uv_default_loop();
    char* ips[] = {"87.148.1.40", "72.235.131.120", "127.0.0.1"};
    uint16_t ports[] = {6881, 6881, 8081};

    uv_tcp_t servers[3] = {0};
    uv_connect_t reqs[3] = {0};
    for (int i = 0; i < sizeof(ips) / sizeof(ips[0]); i++) {
        int r = uv_tcp_init(uv_default_loop(), &servers[i]);
        assert(r == 0);
        struct sockaddr_in addr = {0};
        uv_ip4_addr(ips[i], ports[i], &addr);

        reqs[i].data = servers[i].data = ips[i];
        printf("[%s:%d] Connecting\n", ips[i], ports[i]);
        r = uv_tcp_connect(&reqs[i], &servers[i], (void*)&addr, on_connect);
        assert(r == 0);

        r = uv_read_start((uv_stream_t*)&servers[i], my_alloc_cb, on_read);
        assert(r == 0);
    }

    uv_run(loop, UV_RUN_DEFAULT);
}
