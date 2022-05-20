#include "fs_watch.h"
#include "vendor/gb.h"

void on_file(gbString absolute_path, usize _len, void* arg) {
    printf("[D003] %s\n", absolute_path);
}

int main(int argc, char* argv[]) {
    gbArena arena = {};
    const isize memory_size = gb_kilobytes(1000);
    void* memory = calloc(memory_size, sizeof(void*));
    GB_ASSERT(memory != NULL);
    gb_arena_init_from_memory(&arena, memory, memory_size);
    gbAllocator allocator = gb_arena_allocator(&arena);

    if (argc != 2) {
        printf("Usage: %s <directory>\n", argv[0]);
        return 0;
    }

    char* in_name = argv[1];
    if (!path_is_directory(in_name)) {
        fprintf(stderr, "%s is not a directory\n", in_name);
        printf("Usage: %s <directory>\n", argv[0]);
        return 0;
    }

    gbString path = gb_string_make(allocator, in_name);
    path_directory_walk(path, on_file, &allocator);
    /* error* err = fs_watch_file(allocator, &path); */
    /* if (err != NULL) { */
    /*     error_print(err); */
    /*     return 1; */
    /* } */
}
