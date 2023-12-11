#include <assert.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../pg/pg.h"

static FILE *log_fd;

#define MAX_URL_LEN 2048

static pg_string_t path_get_directory(const pg_string_t path) {
  const char *sep = pg_char_last_occurence(path, '/');
  assert(sep != NULL);
  pg_string_t dir =
      pg_string_make_length(pg_heap_allocator(), path, (uint64_t)(sep - path));

  return dir;
}

static pg_string_t get_path_from_git_root(void) {
  char *argv[] = {"git", "rev-parse", "--show-prefix", 0};
  pg_string_t cmd_stdio =
      pg_string_make_reserve(pg_heap_allocator(), MAX_URL_LEN);
  pg_string_t cmd_stderr = pg_string_make_reserve(pg_heap_allocator(), 0);
  int exit_status = 0;
  if (!pg_exec(argv, &cmd_stdio, &cmd_stderr, &exit_status)) {
    fprintf(stderr,
            "time=%ld err=failed to execute command errno=%d err_msg=%s\n",
            time(NULL), errno, strerror(errno));
    exit(errno);
  }
  if (WIFEXITED(exit_status) && WEXITSTATUS(exit_status) != 0) {
    fprintf(stderr,
            "time=%ld err='git rev-parse --show-prefix' exited with non-zero "
            "status code status=%d "
            "err=%s\n",
            time(NULL), WEXITSTATUS(exit_status), cmd_stderr);
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
    fprintf(stderr,
            "time=%ld err=failed to execute command errno=%d err_msg=%s\n",
            time(NULL), errno, strerror(errno));
    exit(errno);
  }
  if (WIFEXITED(exit_status) && WEXITSTATUS(exit_status) != 0) {
    fprintf(stderr,
            "time=%ld err='git rev-parse HEAD' exited with non-zero status "
            "code status=%d "
            "err=%s\n",
            time(NULL), WEXITSTATUS(exit_status), cmd_stderr);
    exit(errno);
  }

  cmd_stdio = pg_string_trim(cmd_stdio, "\n");
  assert(pg_string_len(cmd_stdio) > 0);

  pg_string_free(cmd_stderr);

  return cmd_stdio;
}

static pg_string_t get_git_origin_remote_url(void) {
  const char *const cmd = "git remote get-url origin";
  fprintf(log_fd, "time=%ld msg=running_cmd cmd=%s\n", time(NULL), cmd);

  char *argv[] = {"git", "remote", "get-url", "origin", 0};
  pg_string_t cmd_stdio =
      pg_string_make_reserve(pg_heap_allocator(), MAX_URL_LEN);
  pg_string_t cmd_stderr = pg_string_make_reserve(pg_heap_allocator(), 0);
  int exit_status = 0;
  if (!pg_exec(argv, &cmd_stdio, &cmd_stderr, &exit_status)) {
    fprintf(stderr,
            "time=%ld err=failed to execute command errno=%d err_msg=%s\n",
            time(NULL), errno, strerror(errno));
    exit(errno);
  }
  if (WIFEXITED(exit_status) && WEXITSTATUS(exit_status) != 0) {
    fprintf(stderr,
            "time=%ld err='git remote get-url origin' exited with non-zero "
            "status code status=%d "
            "err=%s\n",
            time(NULL), WEXITSTATUS(exit_status), cmd_stderr);
    exit(errno);
  }

  cmd_stdio = pg_string_trim(cmd_stdio, "\n");
  assert(pg_string_len(cmd_stdio) > 0);
  pg_string_free(cmd_stderr);

  return cmd_stdio;
}

int main(int argc, char *argv[]) {
  const char *const home_dir = getenv("HOME");

  pg_string_t const log_file_path =
      pg_string_make_reserve(pg_heap_allocator(), MAX_URL_LEN);
  pg_string_appendc(log_file_path, home_dir);
  pg_string_appendc(log_file_path, "/.ado-link.log");

  log_fd = fopen(log_file_path, "a");
  assert(log_fd != NULL);
  fprintf(log_fd, "\n");

  if (argc != 4) {
    fprintf(log_fd, "time=%ld err=wrong number of arguments argc=%d\n",
            time(NULL), argc);
    exit(1);
  }

  const char *const file = argv[1];
  const char *const line_start = argv[2];
  const char *const line_end = argv[3];

  const pg_string_t file_path = pg_string_make(pg_heap_allocator(), argv[1]);
  const pg_string_t dir = path_get_directory(file_path);

  fprintf(log_fd,
          "time=%ld argc=%d file=%s line_start=%s line_end=%s file_path=%s "
          "dir=%s\n",
          time(NULL), argc, file, line_start, line_end, file_path, dir);

  int ret = 0;
  if ((ret = chdir(dir)) != 0) {
    fprintf(
        stderr,
        "time=%ld err=failed to chdir(2) file_path=%s errno=%d err_msg=%s\n",
        time(NULL), dir, errno, strerror(errno));
    exit(errno);
  }
  fprintf(log_fd, "time=%ld msg=changed directory dir=%s\n", time(NULL), dir);

  const pg_string_t const path_from_git_root = get_path_from_git_root();
  pg_string_t const remote_url = get_git_origin_remote_url();
  const pg_string_t const commit = get_current_git_commit();

  pg_span_t const path_span =
      (pg_span_t){.data = remote_url, .len = pg_string_len(remote_url)};
  pg_span_t org_path = {0};
  pg_span_t dir_path = {0};
  pg_span_t project_path = {0};
  pg_span_t remaining = path_span;

  pg_span_t discard = {0};
  assert(pg_span_split_at_first(remaining, '/', &discard, &remaining));
  assert(remaining.len > 0);
  assert(remaining.data[0] == '/');
  pg_span_consume_left(&remaining, 1);

  assert(pg_span_split_at_first(remaining, '/', &org_path, &remaining));
  assert(remaining.len > 0);
  assert(remaining.data[0] == '/');
  pg_span_consume_left(&remaining, 1);

  assert(pg_span_split_at_first(remaining, '/', &dir_path, &remaining));
  assert(remaining.len > 0);
  assert(remaining.data[0] == '/');
  pg_span_consume_left(&remaining, 1);

  project_path = remaining;

  fprintf(
      log_fd,
      "time=%ld remote_url=%s org_path=%.*s dir_path=%.*s project_path=%.*s\n",
      time(NULL), remote_url, (int)org_path.len, org_path.data,
      (int)dir_path.len, dir_path.data, (int)project_path.len,
      project_path.data);

  pg_string_t res_url =
      pg_string_make_reserve(pg_heap_allocator(), MAX_URL_LEN);
  res_url = pg_string_appendc(res_url, "https://dev.azure.com/");
  res_url = pg_string_append_length(res_url, org_path.data, org_path.len);
  res_url = pg_string_appendc(res_url, "/");
  res_url = pg_string_append_length(res_url, dir_path.data, dir_path.len);
  res_url = pg_string_appendc(res_url, "/_git/");
  res_url =
      pg_string_append_length(res_url, project_path.data, project_path.len);

  res_url = pg_string_appendc(res_url, "?path=");
  res_url = pg_string_append(res_url, path_from_git_root);
  res_url = pg_string_appendc(res_url, pg_path_base_name(file_path));

  res_url = pg_string_appendc(res_url, "&version=GC");
  res_url = pg_string_appendc(res_url, commit);

  res_url = pg_string_appendc(res_url, "&line=");
  res_url = pg_string_appendc(res_url, line_start);

  res_url = pg_string_appendc(res_url, "&lineEnd=");
  res_url = pg_string_appendc(res_url, line_end);

  res_url = pg_string_appendc(res_url,
                              "&lineStartColumn=1&lineStyle=plain&_a=contents");

  fprintf(log_fd, "time=%ld res_url=%s\n", time(NULL), res_url);

  puts(res_url);
}
