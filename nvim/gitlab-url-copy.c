#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../pg/pg.h"

#define MAX_URL_LEN 2048
#define GIT_COMMIT_LENGTH (2 * 20)
#define MEM_SIZE (8500)

static void open_url_in_browser(pg_string_t url) {
    pg_string_t cmd = pg_string_make_reserve(
        allocator, sizeof("open ''") + pg_string_len(url));
    cmd = pg_string_append_fmt(cmd, "open '%s'", url);
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    pg_string_free(cmd);
}

static void copy_to_clipboard(gbAllocator allocator, pg_string_t s) {
    pg_string_t cmd = pg_string_make_reserve(
        allocator, sizeof("printf '' | pbcopy") + pg_string_len(s));
    cmd = pg_string_append_fmt(cmd, "printf '%s' | pbcopy", s);
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    pg_string_free(cmd);
}

static pg_string_t get_path_from_git_root(gbAllocator allocator) {
    const char* const cmd = "git rev-parse --show-prefix";
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    pg_string_t output = pg_string_make_reserve(allocator, 100);

    int ret = 0;
    if ((ret = pg_string_read_file_fd(pg_heap_allocator(), fileno(cmd_handle),
                                      &output)) != 0) {
        fprintf(stderr, "Failed to read(2) output from command: %d %s\n", errno,
                strerror(errno));
        exit(errno);
    }

    output = pg_string_trim(output, "\n");

    return output;
}

static pg_string_t get_current_git_commit(gbAllocator allocator) {
    const char* const cmd = "git rev-parse HEAD";
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    pg_string_t output = pg_string_make_reserve(allocator, GIT_COMMIT_LENGTH);

    int ret = 0;
    if ((ret = read_all(fileno(cmd_handle), &output)) != 0) {
        fprintf(stderr, "Failed to read(2) output from command: %d %s\n", errno,
                strerror(errno));
        exit(errno);
    }

    return output;
}

static pg_string_t get_git_origin_remote_url(gbAllocator allocator) {
    char* cmd = "git remote get-url origin";
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    pg_string_t output = pg_string_make_reserve(allocator, MAX_URL_LEN);

    int ret = 0;
    if ((ret = read_all(fileno(cmd_handle), &output)) != 0) {
        fprintf(stderr, "Failed to read(2) output from command: %d %s\n", errno,
                strerror(errno));
        exit(errno);
    }

    output = pg_string_trim(output, "\n");

    return output;
}

static pg_string_t path_get_directory(gbAllocator allocator, pg_string_t path) {
    const char* sep = pg_char_last_occurence(path, '/');
    assert(sep != NULL);
    pg_string_t dir = pg_string_make_length(allocator, path, sep - path);

    return dir;
}

static pg_string_t get_project_path_from_remote_git_url(
    gbAllocator allocator, pg_string_t git_repository_url) {
    const char* remote_path_start =
        pg_char_first_occurence(git_repository_url, ':');
    assert(remote_path_start != NULL);
    remote_path_start += 1;

    assert(pg_str_has_suffix(remote_path_start, ".git"));

    const uint64_t len = pg_string_len(git_repository_url) -
                         (sizeof(".git") - 1) -
                         (remote_path_start - git_repository_url);
    pg_string_t project_path =
        pg_string_make_length(allocator, remote_path_start, len);
    return project_path;
}

static void print_usage(char* argv0) {
    printf(
        "%s </path/to/file> <line start> [<line end>]\nExample:\n    "
        "%s /Users/pgaultier/code/c/nvim/gitlab-url-copy.c 10 "
        "12\n",
        argv0, argv0);
}

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        print_usage(argv[0]);
        return 0;
    }

    static char mem[MEM_SIZE] = {};
    gbArena arena = {0};
    pg_arena_init_from_memory(&arena, mem, MEM_SIZE);
    gbAllocator allocator = pg_arena_allocator(&arena);

    pg_string_t file_path = pg_string_make(allocator, argv[1]);
    pg_string_t dir = path_get_directory(allocator, file_path);

    int ret = 0;
    if ((ret = chdir(dir)) != 0) {
        fprintf(stderr, "Failed to chdir(2): file_path=%s errno=%d %s\n", dir,
                errno, strerror(errno));
        exit(errno);
    }

    pg_string_t git_repository_url = get_git_origin_remote_url(allocator);
    printf("git_repository_url=%s\n", git_repository_url);

    pg_string_t project_path =
        get_project_path_from_remote_git_url(allocator, git_repository_url);

    pg_string_t path_from_git_root = get_path_from_git_root(allocator);
    pg_string_t commit = get_current_git_commit(allocator);
    const uint64_t line_start = strtoull(argv[2], NULL, 10);

    const uint64_t line_end =
        (argc == 4) ? strtoull(argv[3], NULL, 10) : line_start;

    pg_string_t res_url = pg_string_make_reserve(allocator, MAX_URL_LEN);
    res_url = pg_string_append_fmt(
        res_url, "https://gitlab.ppro.com/%s/-/blob/%s/%s%s#L%llu-L%llu",
        project_path, commit, path_from_git_root, pg_path_base_name(file_path),
        line_start, line_end);
    printf("Url: %s\n", res_url);

    open_url_in_browser(allocator, res_url);

    copy_to_clipboard(allocator, res_url);
}
