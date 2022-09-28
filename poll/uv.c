#include <assert.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>
static uv_loop_t* loop;
static uv_timer_t timers[3];

static char* ips[] = {"87.148.1.40", "72.235.131.120", "127.0.0.1"};
static uint16_t ports[] = {6881, 6881, 8081};
static uv_tcp_t servers[3] = {0};
static uv_connect_t reqs[3] = {0};

static void on_timer_expired(uv_timer_t* handle) {
    const uint64_t i = (uint64_t)handle->data;

    printf("[%s:%d] Timer expired\n", ips[i], ports[i]);
    uv_read_stop((uv_stream_t*)&servers[i]);
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
        const uint64_t i = (uint64_t)stream->data;
        printf("[%s:%d] Read: %.*s\n", ips[i], ports[i], (int)nread, buf->base);
        uv_timer_start(&timers[i], on_timer_expired, 5000, 0);
    }
}

static void my_alloc_cb(uv_handle_t* handle, size_t suggested_size,
                        uv_buf_t* buf) {
    (void)handle;
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

static void on_close(uv_handle_t* handle) {
    const uint64_t i = (uint64_t)handle->data;
    printf("[%s:%d] Close\n", ips[i], ports[i]);
}

static void on_connect(uv_connect_t* handle, int status) {
    const uint64_t i = (uint64_t)handle->handle->data;
    printf("[%s:%d] on_connect: %d\n", ips[i], ports[i], status);
    if (status == 0) {
        uv_timer_init(loop, &timers[i]);
        timers[i].data = (void*)i;
        uv_timer_start(&timers[i], on_timer_expired, 5000, 0);
    } else {
        uv_close((uv_handle_t*)handle->handle, on_close);
    }
}

int main() {
    printf(
        "uv_loop_t=%lu\nuv_handle_t=%lu\n, "
        "uv_stream_t=%lu\nuv_tcp_t:%lu\nuv_connect_t:%lu\nuv_req_t=%lu\nuv_"
        "write_t=%lu\nsignal_t=%lu\n",
        sizeof(uv_loop_t), sizeof(uv_handle_t), sizeof(uv_stream_t),
        sizeof(uv_tcp_t), sizeof(uv_connect_t), sizeof(uv_req_t),
        sizeof(uv_write_t), sizeof(uv_signal_t));
    return 0;
    loop = uv_default_loop();

    for (uint64_t i = 0; i < sizeof(ips) / sizeof(ips[0]); i++) {
        int r = uv_tcp_init(uv_default_loop(), &servers[i]);
        assert(r == 0);
        struct sockaddr_in addr = {0};
        uv_ip4_addr(ips[i], ports[i], &addr);

        reqs[i].data = servers[i].data = (void*)i;
        printf("[%s:%d] Connecting\n", ips[i], ports[i]);
        r = uv_tcp_connect(&reqs[i], &servers[i], (void*)&addr, on_connect);
        assert(r == 0);

        r = uv_read_start((uv_stream_t*)&servers[i], my_alloc_cb, on_read);
        assert(r == 0);
    }

    uv_run(loop, UV_RUN_DEFAULT);
}
