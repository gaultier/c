#include "uv.h"

#include <assert.h>
#include <netinet/in.h>
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

static char* ip1 = "87.148.1.40";
static char* ip2 = "72.235.131.120";
static char* ip3 = "0.0.0.0";
static uv_tcp_t server1;
static uv_tcp_t server2;
static uv_tcp_t server3;
static uv_connect_t req1;
static uv_connect_t req2;
static uv_connect_t req3;

int main() {
    uv_loop_t* loop = uv_default_loop();

    {
        int r = uv_tcp_init(uv_default_loop(), &server1);
        struct sockaddr_in addr = {0};
        uv_ip4_addr("87.148.1.40", 6881, &addr);

        req1.data = server1.data = ip1;
        r = uv_tcp_connect(&req1, &server1, (void*)&addr, on_connect);
        assert(r == 0);

        r = uv_read_start((uv_stream_t*)&server1, my_alloc_cb, on_read);
        assert(r == 0);
    }
    {
        int r = uv_tcp_init(uv_default_loop(), &server2);
        struct sockaddr_in addr = {0};
        uv_ip4_addr("72.235.131.120", 6881, &addr);

        req2.data = server2.data = ip2;
        r = uv_tcp_connect(&req2, &server2, (void*)&addr, on_connect);
        assert(r == 0);

        r = uv_read_start((uv_stream_t*)&server2, my_alloc_cb, on_read);
        assert(r == 0);
    }
    {
        int r = uv_tcp_init(uv_default_loop(), &server3);
        struct sockaddr_in addr = {0};
        uv_ip4_addr("0.0.0.0", 8081, &addr);

        req3.data = server3.data = ip3;
        r = uv_tcp_connect(&req3, &server3, (void*)&addr, on_connect);
        assert(r == 0);

        r = uv_read_start((uv_stream_t*)&server3, my_alloc_cb, on_read);
        assert(r == 0);
    }
    uv_run(loop, UV_RUN_DEFAULT);
}
