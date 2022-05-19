#include "fs_watch.h"

int main(int argc, char* argv[]) {
    gbArena arena = {};
    const isize memory_size = gb_kilobytes(1000);
    void* memory = calloc(memory_size, sizeof(void*));
    GB_ASSERT(memory != NULL);
    gb_arena_init_from_memory(&arena, memory, memory_size);
    gbAllocator allocator = gb_arena_allocator(&arena);

    if (argc != 2) {
        printf("Usage: %s <file>\n", argv[0]);
        return 0;
    }

    gbString path = gb_string_make(allocator, argv[1]);
    error* err = fs_watch_file(allocator, &path);
    if (err != NULL) {
        error_print(err);
        return 1;
    }
}
