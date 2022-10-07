#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define GB_IMPLEMENTATION
#include "../vendor/gb/gb.h"

#define MAX_URL_LEN 2048
#define GIT_COMMIT_LENGTH (2 * 20)
#define MEM_SIZE (8500)

static int read_all(int fd, gbString* in) {
    while (gb_string_available_space(*in) > 0) {
        int ret = read(fd, *in + gb_string_length(*in),
                       gb_string_available_space(*in));
        if (ret == -1) {
            fprintf(stderr, "Failed to read from socket: %d %s\n", errno,
                    strerror(errno));
            return errno;
        }
        if (ret == 0) return 0;

        gb__set_string_length(*in, gb_string_length(*in) + ret);
    }

    return 0;
}

static void open_url_in_browser(gbAllocator allocator, gbString url) {
    gbString cmd = gb_string_make_reserve(
        allocator, sizeof("open ''") + gb_string_length(url));
    cmd = gb_string_append_fmt(cmd, "open '%s'", url);
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    gb_string_free(cmd);
}

static void copy_to_clipboard(gbAllocator allocator, gbString s) {
    gbString cmd = gb_string_make_reserve(
        allocator, sizeof("printf '' | pbcopy") + gb_string_length(s));
    cmd = gb_string_append_fmt(cmd, "printf '%s' | pbcopy", s);
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    gb_string_free(cmd);
}

static gbString get_path_from_git_root(gbAllocator allocator) {
    const char* const cmd = "git rev-parse --show-prefix";
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    gbString output = gb_string_make_reserve(allocator, MAXPATHLEN);

    int ret = 0;
    if ((ret = read_all(fileno(cmd_handle), &output)) != 0) {
        fprintf(stderr, "Failed to read(2) output from command: %d %s\n", errno,
                strerror(errno));
        exit(errno);
    }

    output = gb_string_trim(output, "\n");

    return output;
}

static gbString get_current_git_commit(gbAllocator allocator) {
    const char* const cmd = "git rev-parse HEAD";
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    gbString output = gb_string_make_reserve(allocator, GIT_COMMIT_LENGTH);

    int ret = 0;
    if ((ret = read_all(fileno(cmd_handle), &output)) != 0) {
        fprintf(stderr, "Failed to read(2) output from command: %d %s\n", errno,
                strerror(errno));
        exit(errno);
    }

    return output;
}

static gbString get_git_origin_remote_url(gbAllocator allocator) {
    char* cmd = "git remote get-url origin";
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    gbString output = gb_string_make_reserve(allocator, MAX_URL_LEN);

    int ret = 0;
    if ((ret = read_all(fileno(cmd_handle), &output)) != 0) {
        fprintf(stderr, "Failed to read(2) output from command: %d %s\n", errno,
                strerror(errno));
        exit(errno);
    }

    output = gb_string_trim(output, "\n");

    return output;
}

static gbString path_get_directory(gbAllocator allocator, gbString path) {
    const char* sep = gb_char_last_occurence(path, '/');
    assert(sep != NULL);
    gbString dir = gb_string_make_length(allocator, path, sep - path);

    return dir;
}

static gbString get_project_path_from_remote_git_url(
    gbAllocator allocator, gbString git_repository_url) {
    const char* remote_path_start =
        gb_char_first_occurence(git_repository_url, ':');
    assert(remote_path_start != NULL);
    remote_path_start += 1;

    assert(gb_str_has_suffix(remote_path_start, ".git"));

    const uint64_t len = gb_string_length(git_repository_url) -
                         (sizeof(".git") - 1) -
                         (remote_path_start - git_repository_url);
    gbString project_path =
        gb_string_make_length(allocator, remote_path_start, len);
    return project_path;
}

int main(int argc, char* argv[]) {
    assert(argc == 3);

    static char mem[MEM_SIZE] = {};
    gbArena arena = {0};
    gb_arena_init_from_memory(&arena, mem, MEM_SIZE);
    gbAllocator allocator = gb_arena_allocator(&arena);

    gbString file_path = gb_string_make(allocator, argv[1]);
    gbString dir = path_get_directory(allocator, file_path);

    int ret = 0;
    if ((ret = chdir(dir)) != 0) {
        fprintf(stderr, "Failed to chdir(2): file_path=%s errno=%d %s\n", dir,
                errno, strerror(errno));
        exit(errno);
    }

    gbString git_repository_url = get_git_origin_remote_url(allocator);
    printf("git_repository_url=%s\n", git_repository_url);

    gbString project_path =
        get_project_path_from_remote_git_url(allocator, git_repository_url);

    gbString path_from_git_root = get_path_from_git_root(allocator);
    gbString commit = get_current_git_commit(allocator);
    const uint64_t line = strtoul(argv[2], NULL, 10);

    gbString res_url = gb_string_make_reserve(allocator, MAX_URL_LEN);
    res_url = gb_string_append_fmt(
        res_url, "https://gitlab.ppro.com/%s/-/blob/%s/%s%s#L%llu",
        project_path, commit, path_from_git_root, gb_path_base_name(file_path),
        line);
    printf("Url: %s\n", res_url);

    open_url_in_browser(allocator, res_url);

    copy_to_clipboard(allocator, res_url);
}
