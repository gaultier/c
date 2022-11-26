#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../pg/pg.h"
#define JSMN_STATIC
#include "vendor/jsmn/jsmn.h"

#define MAX_URL_LEN 4096

typedef enum {
  COL_RESET,
  COL_RED,
  COL_GREEN,
  COL_COUNT,
} pg_color;

static const char pg_colors[2][COL_COUNT][14] = {
    // is_tty == true
    [true] = {[COL_RESET] = "\x1b[0m",
              [COL_RED] = "\x1b[31m",
              [COL_GREEN] = "\x1b[32m"}};

static struct timeval start;

struct process_t {
  struct process_t *next;
  pg_string_t path_with_namespace;
  int stderr_fd;
  pid_t pid;
};
typedef struct process_t process_t;

typedef enum {
  GCM_SSH = 0,
  GCM_HTTPS = 1,
} git_clone_method_t;

typedef struct {
  pg_string_t root_directory;
  pg_string_t api_token;
  pg_string_t gitlab_domain;
  git_clone_method_t clone_method;
  PG_PAD(4);
} options_t;
static bool verbose = false;

typedef struct {
  CURL *http_handle;
  pg_string_t response_body;
  pg_string_t url;
  pg_array_t(jsmntok_t) tokens;
  struct curl_slist *curl_headers;
  bool finished;
  PG_PAD(7);
} api_t;

static bool atomic_child_spawner_finished = false;
static pthread_mutex_t child_processes_mtx = PTHREAD_MUTEX_INITIALIZER;
// Use child_processes_mtx to access
static pg_array_t(process_t) concurrent_child_processes = {0};
// Use child_processes_mtx to access
static uint64_t concurrent_children_spawned_count = 0;

static void print_usage(int argc, char *argv[]) {
  (void)argc;

  printf(
      "Clone or update all git repositories from Gitlab.\n\n"
      "USAGE:\n"
      "\t%s [OPTIONS]\n\n"
      "OPTIONS:\n"
      "\t-m, --clone-method=https|ssh        Clone over https or ssh. "
      "Defaults to ssh.\n"
      "\t-d, --root-directory <DIRECTORY>    The root directory to "
      "clone/update all "
      "the projects. Required.\n"
      "\t-u, --url <GITLAB URL>\n"
      "\t-t, --api-token <API TOKEN>         The api token from gitlab to "
      "fetch "
      "private repositories\n"
      "\t-h, --help\n"
      "\t-v, --verbose\n\n"
      "The repositories are cloned with git over https or ssh "
      "without tags, in a flat manner.\n"
      "If some repositories already exist in the root directory, they are "
      "updated (with git pull) instead of cloned.\n"
      "If some repositories fail, this command continues and tries to "
      "clone or update the other repositories.\n\n"
      "EXAMPLES:\n\n"
      "Clone/update all repositories from gitlab.com over https in the "
      "directory /tmp/git:\n\n"
      "\tclone-gitlab-api -u gitlab.com -d /tmp/git/ --clone-method=https\n\n"
      "Clone/update all repositories from gitlab.example.com (over ssh which "
      "is the default) with the token 'abcdef123' in the directory /tmp/git "
      "verbosely:\n\n"
      "\tclone-gitlab-api -u gitlab.example.com -t abcdef123 -d /tmp/git/ "
      "-v\n\n",
      argv[0]);
}

static bool str_equal(const char *a, uint64_t a_len, const char *b,
                      uint64_t b_len) {
  assert(a != NULL);
  assert(b != NULL);

  return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static bool str_iequal(const char *a, uint64_t a_len, const char *b,
                       uint64_t b_len) {
  assert(a != NULL);
  assert(b != NULL);

  if (a_len != b_len)
    return false;
  for (uint64_t i = 0; i < a_len; i++) {
    if (pg_char_to_lower(a[i]) != pg_char_to_lower(b[i]))
      return false;
  }
  return true;
}

static bool str_iequal_c(const char *a, uint64_t a_len, const char *b0) {
  assert(a != NULL);
  assert(b0 != NULL);

  return str_iequal(a, a_len, b0, strlen(b0));
}

static bool is_directory(const char *path) {
  assert(path != NULL);

  struct stat s = {0};
  if (stat(path, &s) == -1) {
    return false;
  }
  return S_ISDIR(s.st_mode);
}

static uint64_t on_http_response_body_chunk(void *contents, uint64_t size,
                                            uint64_t nmemb, void *userp) {
  assert(contents != NULL);
  assert(userp != NULL);

  const uint64_t real_size = size * nmemb;
  pg_string_t *response_body = userp;
  *response_body = pg_string_append_length(*response_body, contents, real_size);

  return real_size;
}

static uint64_t on_header(char *buffer, uint64_t size, uint64_t nitems,
                          void *userdata) {
  assert(buffer != NULL);
  assert(userdata != NULL);
  api_t *const api = userdata;

  const uint64_t real_size = nitems * size;
  const char *val = memchr(buffer, ':', real_size);
  if (val == NULL)
    return real_size; // Could be HTTP/1.1 OK, skip

  assert(val > buffer);
  assert(val < buffer + real_size);
  const uint64_t key_len = (uint64_t)(val - buffer);
  val++; // Skip `:`
  uint64_t val_len = (uint64_t)(buffer + real_size - val);

  if (str_iequal_c(buffer, key_len, "Link")) {
    const char needle[] = ">; rel=\"next\"";
    const uint64_t needle_len = sizeof(needle) - 1;
    char *end = pg_memmem(val, val_len, needle, needle_len);
    if (end == NULL) {
      // Finished - no more pages
      api->finished = true;
      return real_size;
    }

    char *start_header = end;
    while (start_header > val && *start_header != '<')
      start_header--;
    if (start_header == val) {
      fprintf(stderr, "Failed to parse HTTP header Link: %.*s\n", (int)val_len,
              val);
      return 0;
    }

    start_header++; // Skip `<`
    val_len = (uint64_t)(end - start_header);
    val = start_header;

    if (val_len > MAX_URL_LEN) {
      fprintf(stderr, "Failed to parse HTTP header Link, too long: %.*s\n",
              (int)val_len, val);
      return 0;
    }

    assert(api->url != NULL);
    pg_string_clear(api->url);
    api->url = pg_string_append_length(api->url, val, val_len);
  }
  return nitems * size;
}

static int on_curl_socktopt(void *clientp, curl_socket_t curlfd,
                            curlsocktype purpose) {
  (void)clientp;
  (void)purpose;
  if (fcntl(curlfd, F_SETFD, FD_CLOEXEC) == -1) {
    fprintf(stderr, "Failed to fcntl(2) with FD_CLOEXEC: err=%s\n",
            strerror(errno));
  }
  return CURL_SOCKOPT_OK;
}

static void api_init(api_t *api, options_t *options) {
  assert(api != NULL);
  assert(options != NULL);
  assert(options->gitlab_domain != NULL);

  api->response_body = pg_string_make_reserve(
      pg_heap_allocator(),
      /* Empirically observed response size is ~86KiB at most */ 100 * 1000);

  pg_array_init_reserve(api->tokens, 8 * 1000, pg_heap_allocator());

  api->http_handle = curl_easy_init();
  assert(api->http_handle != NULL);

  api->url = pg_string_duplicate(pg_heap_allocator(), options->gitlab_domain);
  api->url = pg_string_appendc(
      api->url, "/api/v4/"
                "projects?statistics=false&top_level=&with_custom_"
                "attributes=false&simple=true&per_page=100&all_available="
                "true&order_by=id&sort=asc");
  assert(curl_easy_setopt(api->http_handle, CURLOPT_SOCKOPTFUNCTION,
                          on_curl_socktopt) == CURLE_OK);
  assert(curl_easy_setopt(api->http_handle, CURLOPT_VERBOSE, verbose) ==
         CURLE_OK);
  assert(curl_easy_setopt(api->http_handle, CURLOPT_MAXREDIRS, 5) == CURLE_OK);
  assert(curl_easy_setopt(api->http_handle, CURLOPT_TIMEOUT,
                          60 /* seconds */) == CURLE_OK);
  assert(curl_easy_setopt(api->http_handle, CURLOPT_FOLLOWLOCATION, true) ==
         CURLE_OK);
  assert(curl_easy_setopt(api->http_handle, CURLOPT_REDIR_PROTOCOLS,
                          "http,https") == CURLE_OK);
  assert(curl_easy_setopt(api->http_handle, CURLOPT_WRITEFUNCTION,
                          on_http_response_body_chunk) == CURLE_OK);
  assert(curl_easy_setopt(api->http_handle, CURLOPT_WRITEDATA,
                          &api->response_body) == CURLE_OK);
  assert(curl_easy_setopt(api->http_handle, CURLOPT_HEADERFUNCTION,
                          on_header) == CURLE_OK);
  assert(curl_easy_setopt(api->http_handle, CURLOPT_HEADERDATA, api) ==
         CURLE_OK);

  if (options->api_token != NULL) {
    static char token_header[150] = "";
    snprintf(token_header, sizeof(token_header) - 1, "PRIVATE-TOKEN: %s",
             options->api_token);

    api->curl_headers = curl_slist_append(NULL, token_header);
    assert(api->curl_headers != NULL);
    assert(curl_easy_setopt(api->http_handle, CURLOPT_HTTPHEADER,
                            api->curl_headers) == CURLE_OK);
  }
}

static void options_parse_from_cli(int argc, char *argv[], options_t *options) {
  assert(argv != NULL);
  assert(options != NULL);

  struct option longopts[] = {
      {.name = "root-directory",
       .has_arg = required_argument,
       .flag = NULL,
       .val = 'd'},
      {.name = "api-token",
       .has_arg = required_argument,
       .flag = NULL,
       .val = 't'},
      {.name = "clone-method",
       .has_arg = required_argument,
       .flag = NULL,
       .val = 'm'},
      {.name = "url", .has_arg = required_argument, .flag = NULL, .val = 'u'},
      {.name = "help", .has_arg = no_argument, .flag = NULL, .val = 'h'},
      {.name = "verbose", .has_arg = no_argument, .flag = NULL, .val = 'v'},
  };

  int ch = 0;
  while ((ch = getopt_long(argc, argv, "m:vhd:t:u:", longopts, NULL)) != -1) {
    switch (ch) {
    case 'm': {
      if (strcmp(optarg, "https") == 0) {
        options->clone_method = GCM_HTTPS;
      } else if (strcmp(optarg, "ssh") == 0) {
        options->clone_method = GCM_SSH;
      } else {
        fprintf(stderr, "Invalid --clone-method argument: must be https or "
                        "ssh\n");
        exit(EINVAL);
      }
      break;
    }
    case 'd': {
      options->root_directory = pg_string_make(pg_heap_allocator(), optarg);
      break;
    }
    case 't': {
      const uint64_t optarg_len = strlen(optarg);
      if (optarg_len == 0) {
        fprintf(stderr, "Empty token\n");
        exit(EINVAL);
      }
      if (optarg_len > 128) {
        fprintf(stderr, "Token is too long: maximum 128 characters\n");
        exit(EINVAL);
      }
      options->api_token = pg_string_make(pg_heap_allocator(), optarg);
      break;
    }
    case 'u': {
      const uint64_t optarg_len = strlen(optarg);
      if (optarg_len > MAX_URL_LEN) {
        fprintf(stderr, "Url is too long: maximum %d characters\n",
                MAX_URL_LEN);
        exit(EINVAL);
      }

      options->gitlab_domain =
          pg_string_make_reserve(pg_heap_allocator(), MAX_URL_LEN);
      if (!pg_str_has_prefix(optarg, "https://")) {
        options->gitlab_domain =
            pg_string_appendc(options->gitlab_domain, "https://");
      }
      options->gitlab_domain =
          pg_string_appendc(options->gitlab_domain, optarg);

      break;
    }
    case 'v':
      verbose = true;
      break;
    default:
      print_usage(argc, argv);
      exit(0);
    }
  }

  if (options->root_directory == NULL) {
    fprintf(stderr, "Missing required --root-directory CLI argument.\n");
    exit(EINVAL);
  }
  if (options->gitlab_domain == NULL) {
    fprintf(stderr, "Missing required --url CLI argument.\n");
    exit(EINVAL);
  }
}

static int api_fetch_projects(api_t *api) {
  assert(api != NULL);
  assert(api->url != NULL);

  assert(curl_easy_setopt(api->http_handle, CURLOPT_URL, api->url) == CURLE_OK);

  CURLcode res = 0;
  if ((res = curl_easy_perform(api->http_handle)) != 0) {
    int error;
    curl_easy_getinfo(api->http_handle, CURLINFO_OS_ERRNO, &error);
    fprintf(stderr,
            "Failed to query api: url=%s response_body=%s res=%d err=%s "
            "errno=%d\n",
            api->url, api->response_body, res, curl_easy_strerror(res), error);
    return (int)res;
  }

  return 0;
}

static int upsert_project(pg_string_t path, char *git_url, char *fs_path,
                          const options_t *options);

static int api_parse_and_upsert_projects(api_t *api, const options_t *options) {
  assert(api != NULL);

  jsmn_parser p;

  pg_array_clear(api->tokens);
  int res = 0;

  do {
    jsmn_init(&p);
    res = jsmn_parse(&p, api->response_body, pg_string_len(api->response_body),
                     api->tokens, (uint32_t)pg_array_capacity(api->tokens));
    if (res == JSMN_ERROR_NOMEM) {
      pg_array_grow(api->tokens, pg_array_capacity(api->tokens) * 2 + 8);
      continue;
    }
    if (res < 0 && res != JSMN_ERROR_NOMEM) {
      fprintf(stderr, "Failed to parse JSON: body=%s res=%d\n",
              api->response_body, res);
      return res;
    }
    if (res == 0) {
      fprintf(stderr, "Failed to parse JSON (is it empty?): body=%s res=%d\n",
              api->response_body, res);
      return res;
    }
  } while (res == JSMN_ERROR_NOMEM);

  pg_array_resize(api->tokens, res);
  res = 0;

  const uint64_t tokens_count = pg_array_len(api->tokens);
  assert(tokens_count > 0);
  if (api->tokens[0].type != JSMN_ARRAY) {
    fprintf(stderr,
            "Received unexpected JSON response: expected array, got: %s\n",
            api->response_body);
    return EINVAL;
  }

  const char key_path_with_namespace[] = "path_with_namespace";
  const uint64_t key_path_with_namespace_len =
      sizeof("path_with_namespace") - 1;
  const char clone_method_fields[][20] = {
      [GCM_SSH] = "ssh_url_to_repo",
      [GCM_HTTPS] = "http_url_to_repo",
  };
  const char *key_git_url = clone_method_fields[options->clone_method];
  const uint64_t key_git_url_len = strlen(key_git_url);

  char *fs_path = NULL;
  pg_string_t path_with_namespace = NULL;
  char *git_url = NULL;
  uint64_t field_count = 0;

  for (uint64_t i = 1; i < pg_array_len(api->tokens); i++) {
    jsmntok_t *const cur = &api->tokens[i - 1];
    jsmntok_t *const next = &api->tokens[i];
    if (!(cur->type == JSMN_STRING && next->type == JSMN_STRING))
      continue;

    char *const cur_s = &api->response_body[cur->start];
    const uint64_t cur_s_len = (uint64_t)(cur->end - cur->start);
    char *const next_s = &api->response_body[next->start];
    const uint64_t next_s_len = (uint64_t)(next->end - next->start);

    if (str_equal(cur_s, cur_s_len, key_path_with_namespace,
                  key_path_with_namespace_len)) {
      field_count++;
      path_with_namespace =
          pg_string_make_length(pg_heap_allocator(), next_s, next_s_len);

      // `execvp(2)` expects null terminated strings
      // This is safe to do because we override the terminating double
      // quote which no one cares about
      fs_path = next_s;
      fs_path[next_s_len] = 0;
      for (uint64_t j = 0; j < next_s_len; j++) {
        if (fs_path[j] == '/')
          fs_path[j] = '.';
      }

      i++;
      continue;
    }
    if (str_equal(cur_s, cur_s_len, key_git_url, key_git_url_len)) {
      field_count++;
      git_url = next_s;
      // `execvp(2)` expects null terminated strings
      // This is safe to do because we override the terminating double
      // quote which no one cares about
      git_url[next_s_len] = 0;

      if (field_count > 0 && field_count % 2 == 0) {
        assert(fs_path != NULL);
        assert(path_with_namespace != NULL);

        if ((res = upsert_project(path_with_namespace, git_url, fs_path,
                                  options)) != 0)
          return res;
        git_url = NULL;
      }

      i++;
    }
  }

  return res;
}

static void *watch_workers(void *varg) {
  (void)varg;

  static char child_proc_stderr[256] = {0};

  const bool is_tty = isatty(STDOUT_FILENO);

  uint64_t children_waited_count = 0, children_spawned_count = 0;

  while (1) {
    int stat_loc = 0;
    pid_t child_pid = wait(&stat_loc);
    if (child_pid < 0) {
      if (errno == ECHILD) {
        bool children_spawner_finished = false;
        __atomic_load(&atomic_child_spawner_finished,
                      &children_spawner_finished, __ATOMIC_SEQ_CST);

        if (children_spawner_finished)
          break;
        else {
          // Might happen temporarily. Could use pthread_cond_wait instead but
          // that should be fine, only reporting would be delayed
          sleep(1);
          continue;
        }
      }

      fprintf(stderr, "Failed to wait(): %s\n", strerror(errno));
      exit(errno);
    }

    children_waited_count += 1;

    process_t process = {0};
    {
      pthread_mutex_lock(&child_processes_mtx);
      for (uint64_t i = 0; i < pg_array_len(concurrent_child_processes); i++) {
        if (concurrent_child_processes[i].pid == child_pid) {
          process = concurrent_child_processes[i];
          concurrent_child_processes[i] = concurrent_child_processes
              [pg_array_len(concurrent_child_processes) - 1];
          pg_array_resize(concurrent_child_processes,
                          pg_array_len(concurrent_child_processes) - 1);

          break;
        }
      }

      children_spawned_count = concurrent_children_spawned_count;
      pthread_mutex_unlock(&child_processes_mtx);
    }

    assert(process.pid == child_pid);

    const int exit_status = WEXITSTATUS(stat_loc);
    if (exit_status == 0) {
      printf("%s[%" PRIu64 "/%" PRIu64 "] ✓ "
             "%s%s\n",
             pg_colors[is_tty][COL_GREEN], children_waited_count,
             children_spawned_count, process.path_with_namespace,
             pg_colors[is_tty][COL_RESET]);
    } else {
      // Best effort to get the stderr from the child
      ssize_t stderr_read = 0;
      {
        stderr_read = read(process.stderr_fd, child_proc_stderr,
                           sizeof(child_proc_stderr) - 1);
        if (stderr_read > 0) {
          child_proc_stderr[stderr_read] = 0;
        }
      }

      printf("%s[%" PRIu64 "/%" PRIu64 "] ❌ "
             "%s (%d): %s%s\n",
             pg_colors[is_tty][COL_RED], children_waited_count,
             children_spawned_count, process.path_with_namespace, exit_status,
             stderr_read > 0 ? child_proc_stderr : "(unknown)",
             pg_colors[is_tty][COL_RESET]);
    }

    // Rm
    {
      pg_string_free(process.path_with_namespace);
      close(process.stderr_fd);
    }
  }

  struct timeval end = {0};
  gettimeofday(&end, NULL);
  printf("Finished in %lds\n", end.tv_sec - start.tv_sec);

  return NULL;
}

static int worker_update_project(const char *fs_path, const pg_string_t git_url,
                                 const options_t *options) {
  assert(fs_path != NULL);
  assert(git_url != NULL);
  assert(options != NULL);

  if (chdir(fs_path) == -1) {
    fprintf(stderr, "Failed to chdir(2): fs_path=%s err=%s\n", fs_path,
            strerror(errno));
    exit(errno);
  }

  char *const argv[] = {"git", "pull", "--quiet", "--no-tags", 0};

  if (execvp("git", argv) == -1) {
    fprintf(stderr,
            "Failed to spawn %s as child process to pull: git_url=%s err=%s\n",
            argv[0], git_url, strerror(errno));
    exit(errno);
  }
  __builtin_unreachable();
}

static int worker_clone_project(char *fs_path, const pg_string_t git_url,
                                const options_t *options) {
  assert(fs_path != NULL);
  assert(git_url != NULL);
  assert(options != NULL);

  char *const argv[] = {"git",   "clone", "--quiet", "--no-tags",
                        git_url, fs_path, 0};

  if (execvp("git", argv) == -1) {
    fprintf(stderr,
            "Failed to spawn %s as child process to clone: git_url=%s err=%s\n",
            argv[0], git_url, strerror(errno));
    exit(errno);
  }
  __builtin_unreachable();
}

static int change_directory(const char *path) {
  assert(path != NULL);

  if (chdir(path) == -1) {
    if (errno == ENOENT) {
      if (mkdir(path, S_IRWXU) == -1) {
        fprintf(stderr, "Failed to mkdir(2): path=%s err=%s\n", path,
                strerror(errno));
        return errno;
      }
      printf("Created directory: %s\n", path);
      if (chdir(path) == -1) {
        fprintf(stderr, "Failed to chdir(2): path=%s err=%s\n", path,
                strerror(errno));
        return errno;
      }
    } else {
      fprintf(stderr, "Failed to chdir(2): path=%s err=%s\n", path,
              strerror(errno));
      return errno;
    }
  }
  return 0;
}

static int upsert_project(pg_string_t path, char *git_url, char *fs_path,
                          const options_t *options) {
  assert(path != NULL);
  assert(git_url != NULL);
  assert(options != NULL);

  int stderr_pipe[2] = {0};
  if (pipe(stderr_pipe) != 0) {
    fprintf(stderr, "Failed to pipe(2): err=%s\n", strerror(errno));
    return errno;
  }

  const pid_t pid = fork();
  if (pid == -1) {
    fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
    return errno;
  } else if (pid == 0) {
    close(stderr_pipe[0]); // Child does not read
    close(0);              // Close stdin
    close(1);              // Close stdout
    if (dup2(stderr_pipe[1], 2) ==
        -1) { // Direct stderr to the pipe for the parent to read
      fprintf(stderr, "Failed to dup2(2): err=%s\n", strerror(errno));
    }
    close(stderr_pipe[1]); // Not needed anymore

    if (is_directory(fs_path)) {
      worker_update_project(fs_path, git_url, options);
    } else {
      worker_clone_project(fs_path, git_url, options);
    }
    __builtin_unreachable();
  } else {
    close(stderr_pipe[1]); // Parent does not write

    const process_t process = {
        .pid = pid,
        .stderr_fd = stderr_pipe[0],
        .path_with_namespace = path,
    };
    {
      pthread_mutex_lock(&child_processes_mtx);
      pg_array_append(concurrent_child_processes, process);
      concurrent_children_spawned_count += 1;
      pthread_mutex_unlock(&child_processes_mtx);
    }
  }
  return 0;
}

static int api_fetch_and_upsert_projects(api_t *api, const options_t *options) {
  assert(api != NULL);
  assert(options != NULL);

  pg_string_clear(api->response_body);

  int res = 0;
  if ((res = api_fetch_projects(api)) != 0)
    return res;

  return api_parse_and_upsert_projects(api, options);
}

int main(int argc, char *argv[]) {
  gettimeofday(&start, NULL);
  options_t options = {0};
  options_parse_from_cli(argc, argv, &options);

  pg_array_init_reserve(concurrent_child_processes, 200, pg_heap_allocator());

  api_t api = {0};
  api_init(&api, &options);

  const char *const cwd = getcwd(NULL, 0);
  if (cwd == NULL) {
    fprintf(stderr, "Failed to getcwd(2): err=%s\n", strerror(errno));
    return errno;
  }

  int res = 0;
  if ((res = change_directory(options.root_directory)) != 0)
    return res;

  printf("Changed directory to: %s\n", options.root_directory);

  pthread_t process_exit_watcher = {0};
  if (pthread_create(&process_exit_watcher, NULL, watch_workers, NULL) != 0) {
    fprintf(stderr, "Failed to watch projects cloning: err=%s\n",
            strerror(errno));
    goto end;
  }

  while (!api.finished &&
         (res = api_fetch_and_upsert_projects(&api, &options)) == 0) {
  }

  bool btrue = true;
  __atomic_store(&atomic_child_spawner_finished, &btrue, __ATOMIC_SEQ_CST);

end:
  if ((res = change_directory(cwd)) != 0)
    return res;

  pthread_join(process_exit_watcher, NULL);
}
