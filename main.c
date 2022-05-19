#include "fs_watch.h"

int main(int argc, char* argv[]) {
    gbArena arena = {};
    const isize memory_size = gb_kilobytes(10);
    void* memory = calloc(memory_size, sizeof(void*));
    GB_ASSERT(memory != NULL);
    gb_arena_init_from_memory(&arena, memory, memory_size);
    gbAllocator allocator = gb_arena_allocator(&arena);

    gbString path = gb_string_make(allocator, argv[1]);
    fs_watch_file(&path);
}
