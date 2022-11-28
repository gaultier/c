#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../pg/pg.h"

#define MAX_URL_LEN 2048

static void open_url_in_browser(pg_string_t url) {
  pg_string_t cmd = pg_string_make_reserve(
      pg_heap_allocator(), sizeof("open ''") + pg_string_len(url));
  const uint64_t cmd_len =
      (uint64_t)snprintf(cmd, pg_string_cap(cmd), "open '%s'", url);
  pg__set_string_len(cmd, cmd_len);
  printf("Running: %s\n", cmd);
  FILE *cmd_handle = popen(cmd, "r");
  assert(cmd_handle != NULL);

  pg_string_free(cmd);
}

static void copy_to_clipboard(pg_string_t s) {
  pg_string_t cmd = pg_string_make_reserve(
      pg_heap_allocator(), sizeof("printf '' | pbcopy") + pg_string_len(s));
  const uint64_t cmd_len =
      (uint64_t)snprintf(cmd, pg_string_cap(cmd), "printf '%s' | pbcopy", s);
  pg__set_string_len(cmd, cmd_len);
  printf("Running: %s\n", cmd);
  FILE *cmd_handle = popen(cmd, "r");
  assert(cmd_handle != NULL);

  pg_string_free(cmd);
}

static pg_string_t get_path_from_git_root(void) {
  char *argv[] = {"git", "rev-parse", "--show-prefix", 0};
  pg_string_t cmd_stdio =
      pg_string_make_reserve(pg_heap_allocator(), MAX_URL_LEN);
  pg_string_t cmd_stderr = pg_string_make_reserve(pg_heap_allocator(), 0);
  int exit_status = 0;
  if (!pg_exec(argv, &cmd_stdio, &cmd_stderr, &exit_status)) {
    fprintf(stderr, "Failed to execute command: %d %s\n", errno,
            strerror(errno));
    exit(errno);
  }
  if (WIFEXITED(exit_status) && WEXITSTATUS(exit_status) != 0) {
    fprintf(stderr,
            "Command exited with non-zero status code: status=%d err=%s\n",
            WEXITSTATUS(exit_status), cmd_stderr);
    exit(errno);
  }

  cmd_stdio = pg_string_trim(cmd_stdio, "\n");
  pg_string_free(cmd_stderr);

  return cmd_stdio;
}

static pg_string_t get_current_git_commit(void) {
  char *argv[] = {"git", "rev-parse", "HEAD", 0};
  pg_string_t cmd_stdio =
      pg_string_make_reserve(pg_heap_allocator(), MAX_URL_LEN);
  pg_string_t cmd_stderr = pg_string_make_reserve(pg_heap_allocator(), 0);
  int exit_status = 0;
  if (!pg_exec(argv, &cmd_stdio, &cmd_stderr, &exit_status)) {
    fprintf(stderr, "Failed to execute command: %d %s\n", errno,
            strerror(errno));
    exit(errno);
  }
  if (WIFEXITED(exit_status) && WEXITSTATUS(exit_status) != 0) {
    fprintf(stderr,
            "Command exited with non-zero status code: status=%d err=%s\n",
            WEXITSTATUS(exit_status), cmd_stderr);
    exit(errno);
  }

  cmd_stdio = pg_string_trim(cmd_stdio, "\n");
  assert(pg_string_len(cmd_stdio) > 0);

  pg_string_free(cmd_stderr);

  return cmd_stdio;
}

static pg_string_t get_git_origin_remote_url(void) {
  const char *const cmd = "git remote get-url origin";
  printf("Running: %s\n", cmd);

  char *argv[] = {"git", "remote", "get-url", "origin", 0};
  pg_string_t cmd_stdio =
      pg_string_make_reserve(pg_heap_allocator(), MAX_URL_LEN);
  pg_string_t cmd_stderr = pg_string_make_reserve(pg_heap_allocator(), 0);
  int exit_status = 0;
  if (!pg_exec(argv, &cmd_stdio, &cmd_stderr, &exit_status)) {
    fprintf(stderr, "Failed to execute command: %d %s\n", errno,
            strerror(errno));
    exit(errno);
  }
  if (WIFEXITED(exit_status) && WEXITSTATUS(exit_status) != 0) {
    fprintf(stderr,
            "Command exited with non-zero status code: status=%d err=%s\n",
            WEXITSTATUS(exit_status), cmd_stderr);
    exit(errno);
  }

  cmd_stdio = pg_string_trim(cmd_stdio, "\n");
  assert(pg_string_len(cmd_stdio) > 0);

  pg_string_free(cmd_stderr);

  return cmd_stdio;
}

static pg_string_t path_get_directory(const pg_string_t path) {
  const char *sep = pg_char_last_occurence(path, '/');
  assert(sep != NULL);
  pg_string_t dir =
      pg_string_make_length(pg_heap_allocator(), path, (uint64_t)(sep - path));

  return dir;
}

static pg_string_t
get_project_path_from_remote_git_url(const pg_string_t git_repository_url) {
  const pg_span_t ssh_url_start = pg_span_make_c("ssh://git@gitlab.ppro.com/");
  const pg_span_t http_url_start =
      pg_span_make_c("https://git@gitlab.ppro.com:");
  pg_span_t s = pg_span_make_c(git_repository_url);
  if (pg_span_starts_with(s, ssh_url_start)) {
    pg_span_consume_left(&s, ssh_url_start.len);

  } else if (pg_span_starts_with(s, http_url_start)) {
    pg_span_consume_left(&s, http_url_start.len);
  } else
    assert(0);

  const pg_span_t git = pg_span_make_c(".git");
  if (pg_span_ends_with(s, git)) pg_span_consume_right(&s, git.len);

  return pg_string_make_length(pg_heap_allocator(), s.data, s.len);
}

static void print_usage(char *argv0) {
  printf("%s </path/to/file> <line start> [<line end>]\nExample:\n    "
         "%s /Users/pgaultier/code/c/nvim/gitlab-url-copy.c 10 "
         "12\n",
         argv0, argv0);
}

int main(int argc, char *argv[]) {
  if (argc < 3 || argc > 4) {
    print_usage(argv[0]);
    return 0;
  }

  const pg_string_t file_path = pg_string_make(pg_heap_allocator(), argv[1]);
  const pg_string_t dir = path_get_directory(file_path);

  int ret = 0;
  if ((ret = chdir(dir)) != 0) {
    fprintf(stderr, "Failed to chdir(2): file_path=%s errno=%d %s\n", dir,
            errno, strerror(errno));
    exit(errno);
  }
  printf("Changed directory to: %s\n", dir);

  const pg_string_t git_repository_url = get_git_origin_remote_url();
  printf("git_repository_url=%s\n", git_repository_url);

  const pg_string_t project_path =
      get_project_path_from_remote_git_url(git_repository_url);
  printf("project_path=%s\n", project_path);

  const pg_string_t path_from_git_root = get_path_from_git_root();
  const pg_string_t commit = get_current_git_commit();
  const uint64_t line_start = strtoull(argv[2], NULL, 10);

  const uint64_t line_end =
      (argc == 4) ? strtoull(argv[3], NULL, 10) : line_start;

  pg_string_t res_url =
      pg_string_make_reserve(pg_heap_allocator(), MAX_URL_LEN);
  const uint64_t new_len = (uint64_t)(snprintf(
      res_url, pg_string_cap(res_url),
      "https://gitlab.ppro.com/%s/-/blob/%s/%s%s#L%llu-L%llu", project_path,
      commit, path_from_git_root, pg_path_base_name(file_path), line_start,
      line_end));
  pg__set_string_len(res_url, new_len);
  printf("Url: %s\n", res_url);

  open_url_in_browser(res_url);

  copy_to_clipboard(res_url);
}
