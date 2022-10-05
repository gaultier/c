#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#include "../vendor/gb/gb.h"

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

static void open_url_in_browser(gbString url) {
    gbString cmd =
        gb_string_make_reserve(gb_heap_allocator(), 10 + gb_string_length(url));
    cmd = gb_string_append_fmt(cmd, "open '%s'", url);
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    gb_string_free(cmd);
}

static void copy_to_clipboard(gbString s) {
    gbString cmd =
        gb_string_make_reserve(gb_heap_allocator(), 30 + gb_string_length(s));
    cmd = gb_string_append_fmt(cmd, "printf '%s' | pbcopy", s);
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    gb_string_free(cmd);
}

static gbString get_path_from_git_root() {
    gbString cmd =
        gb_string_make(gb_heap_allocator(), "git rev-parse --show-prefix");
    printf("Running: %s\n", cmd);
    FILE* cmd_handle = popen(cmd, "r");
    assert(cmd_handle != NULL);

    gb_string_free(cmd);

    gbString output = gb_string_make_reserve(gb_heap_allocator(), MAXPATHLEN);

    int ret = 0;
    if ((ret = read_all(fileno(cmd_handle), &output)) != 0) {
        fprintf(stderr, "Failed to read(2) output from command: %d %s\n", errno,
                strerror(errno));
        exit(errno);
    }

    output = gb_string_trim(output, "\n");

    return output;
}

static void handle_connection(int conn_fd) {
    gbString in = gb_string_make_reserve(gb_heap_allocator(), MAXPATHLEN);

    int ret = 0;
    if ((ret = read_all(conn_fd, &in)) != 0) {
        fprintf(stderr, "Failed to recv(2): %d %s\n", errno, strerror(errno));
        exit(errno);
    }
    printf("Received: %s\n", in);

    char* branch = "master";
    char* in_file_path_end =
        memchr(in, 0xa /* newline */, gb_string_length(in));
    assert(in_file_path_end != NULL);
    gbString file_path = gb_string_make_reserve(gb_heap_allocator(), 20);
    {
        uint64_t in_file_path_len = in_file_path_end - (char*)in;
        file_path = gb_string_append_length(file_path, in, in_file_path_len);
    }
    gbString remote_path = gb_string_make_reserve(gb_heap_allocator(), 20);
    {
        const char* sep = gb_char_last_occurence(file_path, '/');
        assert(sep != NULL);
        gbString dir = gb_string_make_length(gb_heap_allocator(), file_path,
                                             sep - file_path);

        if ((ret = chdir(dir)) != 0) {
            fprintf(stderr, "Failed to chdir(2): file_path=%s errno=%d %s\n",
                    dir, errno, strerror(errno));
            exit(errno);
        }

        FILE* git_cmd_handle = popen("git remote get-url origin", "r");
        gbString git_repository_url =
            gb_string_make_reserve(gb_heap_allocator(), MAXPATHLEN);
        if ((ret =
                 read_all(fileno(git_cmd_handle), &git_repository_url) != 0)) {
            fprintf(stderr, "Failed to read output from command: %d %s\n",
                    errno, strerror(errno));
            exit(errno);
        }
        printf("git_repository_url=%s\n", git_repository_url);

        const char* remote_path_start =
            gb_char_first_occurence(git_repository_url, ':');
        assert(remote_path_start != NULL);
        remote_path_start += 1;

        assert(gb_str_has_suffix(remote_path_start, ".git\n"));

        remote_path = gb_string_append_length(
            remote_path, remote_path_start,
            gb_string_length(git_repository_url) - 5 -
                (remote_path_start - git_repository_url));
    }
    uint64_t line = strtoul(in_file_path_end + 1, NULL, 10);

    gbString path_from_git_root = get_path_from_git_root();

    gbString res_url = gb_string_make_reserve(gb_heap_allocator(), 100);
    res_url = gb_string_append_fmt(
        res_url, "https://gitlab.ppro.com/%s/-/tree/%s/%s%s#L%llu", remote_path,
        branch, path_from_git_root, gb_path_base_name(file_path), line);
    printf("Url: %s\n", res_url);

    open_url_in_browser(res_url);

    copy_to_clipboard(res_url);
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        fprintf(stderr, "Failed to socket(2): %s\n", strerror(errno));
        return errno;
    }

    int val = 1;
    int err = 0;
    if ((err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))) ==
        -1) {
        fprintf(stderr, "Failed to setsockopt(2): %s\n", strerror(errno));
        return errno;
    }

    const uint8_t ip[4] = {127, 0, 0, 1};
    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = {.s_addr = *(uint32_t*)ip},
        .sin_port = htons(12345),
    };

    if ((err = bind(fd, (const struct sockaddr*)&addr, sizeof(addr))) == -1) {
        fprintf(stderr, "Failed to bind(2): %s\n", strerror(errno));
        return errno;
    }

    if ((err = listen(fd, 16 * 1024)) == -1) {
        fprintf(stderr, "Failed to listen(2): %s\n", strerror(errno));
        return errno;
    }
    while (1) {
        int conn_fd = accept(fd, NULL, 0);
        if (conn_fd == -1) {
            fprintf(stderr, "Failed to accept(2): %s\n", strerror(errno));
            return errno;
        }

        pid_t pid = fork();
        if (pid == -1) {
            fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
            close(conn_fd);
        } else if (pid == 0) {  // Child
            handle_connection(conn_fd);
            exit(0);
        } else {  // Parent
            // Fds are duplicated by fork(2) and need to be
            // closed by both parent & child
            close(conn_fd);
        }
    }
}
