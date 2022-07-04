#include <assert.h>
#include <stdio.h>
#include <sys/resource.h>
#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"

int main() {
    int fd = open("/dev/random", O_RDONLY);
    assert(fd >= 0);

    while (1) {
        u64 buf_cap = 2048;
        char* buf = malloc(buf_cap);
        u64 buf_len = 0;
        /* gbArray(char) buf; */
        /* gbAllocator allocator = gb_heap_allocator(); */

        /* gb_array_init_reserve(buf, allocator, 2048); */

        puts("New buf");
        for (int i = 0; i < 20; i++) {
            /* const u64 prev_len = gb_array_count(buf); */
            const u64 prev_len = buf_len;
            const u64 new_len = prev_len + 2048;
            /* const u64 old_capacity = gb_array_capacity(buf); */
            const u64 old_capacity = buf_cap;
            const u64 new_capacity =
                old_capacity < new_len ? new_len : old_capacity;
            /* gb_array_reserve(buf, new_capacity); */
            if (new_capacity > old_capacity) {
                buf = realloc(buf, new_capacity);
            }
            const u64 nbytes = read(fd, &buf[prev_len], 2048);
            /* new_len = prev_len + nbytes; */
            /* GB_ARRAY_HEADER(buf)->count = new_len; */
            buf_len = prev_len + nbytes;
        }
        /* gb_array_free(buf); */
        free(buf);
    }
}
