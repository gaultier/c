#include "fs_watch.h"
#include "vendor/gb.h"
typedef struct {
    gbAllocator allocator;
    gbArray(file_info) files;
} on_file_arg;

void on_file(gbString absolute_path, usize file_kind, void* arg) {
    GB_ASSERT_NOT_NULL(arg);

    on_file_arg* fn_arg = arg;
    GB_ASSERT_NOT_NULL(fn_arg->files);
    gb_array_append(fn_arg->files, ((file_info){.absolute_path = absolute_path,
                                                .kind = file_kind}));
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
    on_file_arg arg = {.allocator = allocator};
    gb_array_init_reserve(arg.files, allocator, 100);
    path_directory_walk(path, on_file, &arg);

    fs_watch_file(allocator, arg.files);
}
