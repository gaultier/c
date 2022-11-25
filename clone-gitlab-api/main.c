#include <_types/_uint64_t.h>
#include <_types/_uint8_t.h>
#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "../pg/pg.h"
#define JSMN_STATIC
#include "../vendor/jsmn/jsmn.h"
#include "../vendor/sds/sds.c"

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
  sds path_with_namespace;
  int stderr_fd;
  pid_t pid;
  sds err;
  struct process_t *next; // TODO: improve
};
typedef struct process_t process_t;

typedef enum {
  GCM_SSH = 0,
  GCM_HTTPS = 1,
} git_clone_method_t;

typedef struct {
  sds root_directory;
  sds api_token;
  sds gitlab_domain;
  git_clone_method_t clone_method;
  PG_PAD(4);
} options_t;
static bool verbose = false;

typedef struct {
  const int queue;
} watch_project_cloning_arg_t;

typedef struct {
  CURL *http_handle;
  sds response_body;
  sds url;
  pg_array_t(jsmntok_t) tokens;
  struct curl_slist *curl_headers;
  bool finished;
  PG_PAD(7);
} api_t;

static /* _Atomic */ uint64_t projects_count = 0;

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

static uint64_t str_to_u64(const char *s, uint64_t s_len) {
  assert(s != NULL);

  uint64_t res = 0;
  for (uint64_t i = 0; i < s_len; i++) {
    const char c = s[i];
    if (pg_char_is_space(c))
      continue;
    if (pg_char_is_digit(c)) {
      const uint8_t v = (uint8_t)(c - '0');
      res *= 10;
      res += v;
    } else
      return 0;
  }
  return res;
}

static uint64_t on_http_response_body_chunk(void *contents, uint64_t size,
                                            uint64_t nmemb, void *userp) {
  assert(contents != NULL);
  assert(userp != NULL);

  const uint64_t real_size = size * nmemb;
  sds *response_body = userp;
  *response_body = sdscatlen(*response_body, contents, real_size);

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
    char *end = memmem(val, val_len, needle, needle_len);
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
    sdsclear(api->url);
    api->url = sdscatlen(api->url, val, val_len);
  } else if (str_iequal_c(buffer, key_len, "X-Total")) {
    uint64_t total = str_to_u64(val, val_len);
    uint64_t expected = 0;
    __atomic_compare_exchange(&projects_count, &expected, &total, false,
                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
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

  api->response_body = sdsempty();
  api->response_body = sdsMakeRoomFor(api->response_body, 4 * 1024);

  pg_array_init_reserve(api->tokens, 8 * 1000, pg_heap_allocator());

  api->http_handle = curl_easy_init();
  assert(api->http_handle != NULL);

  api->url = sdsdup(options->gitlab_domain);
  api->url = sdscat(api->url,
                    "/api/v4/"
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

static void api_destroy(api_t *api) {
  assert(api != NULL);

  sdsfree(api->url);
  sdsfree(api->response_body);
  pg_array_free(api->tokens);

  curl_slist_free_all(api->curl_headers);
  curl_easy_cleanup(api->http_handle);
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
      if (strlen(optarg) > PATH_MAX) {
        fprintf(stderr, "Directory is too long: maximum %d characters\n",
                PATH_MAX);
        exit(EINVAL);
      }
      options->root_directory = sdsnew(optarg);
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
      options->api_token = sdsnew(optarg);
      break;
    }
    case 'u': {
      const uint64_t optarg_len = strlen(optarg);
      if (optarg_len > MAX_URL_LEN) {
        fprintf(stderr, "Url is too long: maximum %d characters\n",
                MAX_URL_LEN);
        exit(EINVAL);
      }

      options->gitlab_domain = sdsempty();
      if (!pg_str_has_prefix(optarg, "https://")) {
        options->gitlab_domain = sdscat(options->gitlab_domain, "https://");
      }
      options->gitlab_domain = sdscat(options->gitlab_domain, optarg);

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

static int api_query_projects(api_t *api) {
  assert(api != NULL);
  assert(api->url != NULL);

  int res = 0;
  {
    assert(curl_easy_setopt(api->http_handle, CURLOPT_URL, api->url) ==
           CURLE_OK);
  }

  api->response_body = sdsMakeRoomFor(api->response_body, 4 * 1024);
  if ((res = curl_easy_perform(api->http_handle)) != 0) {
    int error;
    curl_easy_getinfo(api->http_handle, CURLINFO_OS_ERRNO, &error);
    fprintf(stderr,
            "Failed to query api: url=%s response_body=%s res=%d err=%s "
            "errno=%d\n",
            api->url, api->response_body, res, curl_easy_strerror(res), error);
    return res;
  }
  api->response_body = sdsRemoveFreeSpace(api->response_body);

  return 0;
}

static int upsert_project(sds path, char *git_url, char *fs_path,
                          const options_t *options, int queue);

static int api_parse_and_upsert_projects(api_t *api, const options_t *options,
                                         int queue,
                                         uint64_t *projects_handled) {
  assert(api != NULL);

  jsmn_parser p;

  pg_array_clear(api->tokens);
  int res = 0;

  do {
    jsmn_init(&p);
    res = jsmn_parse(&p, api->response_body, sdslen(api->response_body),
                     api->tokens, pg_array_capacity(api->tokens));
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
  sds path_with_namespace = NULL;
  char *git_url = NULL;
  uint64_t field_count = 0;

  for (uint64_t i = 1; i < pg_array_len(api->tokens); i++) {
    jsmntok_t *const cur = &api->tokens[i - 1];
    jsmntok_t *const next = &api->tokens[i];
    if (!(cur->type == JSMN_STRING && next->type == JSMN_STRING))
      continue;

    char *const cur_s = &api->response_body[cur->start];
    const uint64_t cur_s_len = cur->end - cur->start;
    char *const next_s = &api->response_body[next->start];
    const uint64_t next_s_len = next->end - next->start;

    if (str_equal(cur_s, cur_s_len, key_path_with_namespace,
                  key_path_with_namespace_len)) {
      field_count++;
      path_with_namespace = sdsnewlen(next_s, next_s_len);

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
                                  options, queue)) != 0)
          return res;
        git_url = NULL;

        *projects_handled += 1;
      }

      i++;
    }
  }

  return res;
}

static void *watch_workers(void *varg) {
  assert(varg != NULL);
  watch_project_cloning_arg_t *arg = varg;

  uint64_t finished = 0;
  struct kevent events[512] = {0};

  const bool is_tty = isatty(fileno(stdout));

  const uint32_t proc_fflags =
#if defined(__APPLE__)
      NOTE_EXITSTATUS;
#elif defined(__FreeBSD__)
      NOTE_EXIT;
#endif
  do {
    int event_count = kevent(arg->queue, NULL, 0, events, 512, 0);
    if (event_count == -1) {
      fprintf(stderr, "Failed to kevent(2) to query events: err=%s\n",
              strerror(errno));
      return NULL;
    }

    for (int i = 0; i < event_count; i++) {
      const struct kevent *const event = &events[i];

      if (event->filter == EVFILT_READ) {
        process_t *process = event->udata;
        assert(process != NULL);

        const uint64_t max_read = MIN(event->data, 128);
        process->err = sdsMakeRoomFor(process->err, max_read);
        int res = read(process->stderr_fd, process->err, max_read);
        if (res == -1) {
          fprintf(stderr, "Failed to read(2): err=%s\n", strerror(errno));
        }
        sdsIncrLen(process->err, res);
        close(process->stderr_fd);
      } else if ((event->filter == EVFILT_PROC) &&
                 (event->fflags & proc_fflags)) {
        const int exit_status = (event->data >> 8);
        process_t *process = event->udata;
        assert(process != NULL);

        finished += 1;

        const uint64_t count =
            __c11_atomic_load(&projects_count, __ATOMIC_SEQ_CST);
        if (exit_status == 0) {
          printf("%s[%" PRIu64 "/%" PRIu64 "] ✓ "
                 "%s%s\n",
                 pg_colors[is_tty][COL_GREEN], finished, count,
                 process->path_with_namespace, pg_colors[is_tty][COL_RESET]);
        } else {
          printf("%s[%" PRIu64 "/%" PRIu64 "] ❌ "
                 "%s (%d): %s%s\n",
                 pg_colors[is_tty][COL_RED], finished, count,
                 process->path_with_namespace, exit_status, process->err,
                 pg_colors[is_tty][COL_RESET]);
        }
        sdsfree(process->path_with_namespace);
        sdsfree(process->err);
        close(process->stderr_fd);
        free(process);
      }
    }
  } while (__c11_atomic_load(&projects_count, __ATOMIC_SEQ_CST) == 0 ||
           finished < __c11_atomic_load(&projects_count, __ATOMIC_SEQ_CST));

  struct timeval end = {0};
  gettimeofday(&end, NULL);
  printf("Finished in %lds\n", end.tv_sec - start.tv_sec);

  return NULL;
}

static int worker_update_project(char *fs_path, sds git_url,
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
    fprintf(stderr, "Failed to pull: git_url=%s err=%s\n", git_url,
            strerror(errno));
    exit(errno);
  }
  assert(0 && "Unreachable");
  return 0;
}

static int worker_clone_project(char *fs_path, sds git_url,
                                const options_t *options) {
  assert(fs_path != NULL);
  assert(git_url != NULL);
  assert(options != NULL);

  char *const argv[] = {"git",   "clone", "--quiet", "--no-tags",
                        git_url, fs_path, 0};

  if (execvp("git", argv) == -1) {
    fprintf(stderr, "Failed to clone: git_url=%s err=%s\n", git_url,
            strerror(errno));
    exit(errno);
  }
  assert(0 && "Unreachable");
  return 0;
}

static int change_directory(char *path) {
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

static int record_process_finished_event(int queue, process_t *process) {
  assert(process != NULL);

  uint32_t proc_fflags = NOTE_EXIT;
#if defined(__APPLE__)
  proc_fflags |= NOTE_EXITSTATUS;
#endif
  struct kevent events[2] = {
      {
          .filter = EVFILT_PROC,
          .ident = (uintptr_t)process->pid,
          .flags = EV_ADD | EV_ONESHOT,
          .fflags = proc_fflags,
          .udata = process,
      },
      {
          .filter = EVFILT_READ,
          .ident = (uintptr_t)process->stderr_fd,
          .flags = EV_ADD | EV_ONESHOT,
          .udata = process,
      },
  };
  if (kevent(queue, events, 2, NULL, 0, 0) == -1) {
    fprintf(stderr, "Failed to kevent(2) to watch for child process: err=%s\n",
            strerror(errno));
    return errno;
  }
  return 0;
}

static int upsert_project(sds path, char *git_url, char *fs_path,
                          const options_t *options, int queue) {
  assert(path != NULL);
  assert(git_url != NULL);
  assert(options != NULL);

  int fds[2] = {0};
  if (pipe(fds) != 0) {
    fprintf(stderr, "Failed to pipe(2): err=%s\n", strerror(errno));
    return errno;
  }

  pid_t pid = fork();
  if (pid == -1) {
    fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
    return errno;
  } else if (pid == 0) {
    close(fds[0]); // Child does not read
    close(0);      // Close stdin
    close(1);      // Close stdout
    if (dup2(fds[1], 2) ==
        -1) { // Direct stderr to the pipe for the parent to read
      fprintf(stderr, "Failed to dup2(2): err=%s\n", strerror(errno));
    }
    close(fds[1]); // Not needed anymore

    if (is_directory(fs_path)) {
      worker_update_project(fs_path, git_url, options);
    } else {
      worker_clone_project(fs_path, git_url, options);
    }
    assert(0 && "Unreachable");
  } else {
    close(fds[1]); // Parent does not write
    process_t *process = calloc(1, sizeof(process_t));
    process->pid = pid;
    process->stderr_fd = fds[0];
    process->path_with_namespace = path;
    process->err = sdsempty();
    int res = 0;
    if ((res = record_process_finished_event(queue, process)) != 0)
      return res;
  }
  return 0;
}

static int api_fetch_projects(api_t *api, const options_t *options, int queue,
                              uint64_t *projects_handled) {
  assert(api != NULL);
  assert(options != NULL);

  int res = 0;
  sdsclear(api->response_body);

  if ((res = api_query_projects(api)) != 0) {
    return res;
  }

  if ((res = api_parse_and_upsert_projects(api, options, queue,
                                           projects_handled)) != 0) {
    return res;
  }

  return 0;
}

int main(int argc, char *argv[]) {
  gettimeofday(&start, NULL);
  options_t options = {0};
  options_parse_from_cli(argc, argv, &options);

  int res = 0;
  // Do not require wait(2) on child processes
  {
    struct sigaction sa = {.sa_flags = SA_NOCLDWAIT};
    if ((res = sigaction(SIGCHLD, &sa, NULL)) == -1) {
      fprintf(stderr, "Failed to sigaction(2): err=%s\n", strerror(errno));
      return errno;
    }
  }

  // Queue to get notified that child processes finished
  int queue = kqueue();
  if (queue == -1) {
    fprintf(stderr, "Failed to kqueue(2): err=%s\n", strerror(errno));
    return errno;
  }

  api_t api = {0};
  api_init(&api, &options);

  static char cwd[PATH_MAX] = "";
  if (getcwd(cwd, PATH_MAX) == NULL) {
    fprintf(stderr, "Failed to getcwd(2): err=%s\n", strerror(errno));
    return errno;
  }

  if ((res = change_directory(options.root_directory)) != 0)
    return res;

  printf("Changed directory to: %s\n", options.root_directory);

  watch_project_cloning_arg_t arg = {
      .queue = queue,
  };

  // Start process exit watcher thread, only after we know from the first API
  // query how many items there are
  pthread_t process_exit_watcher = {0};
  {
    if (pthread_create(&process_exit_watcher, NULL, watch_workers, &arg) != 0) {
      fprintf(stderr, "Failed to watch projects cloning: err=%s\n",
              strerror(errno));
      goto end;
    }
  }

  uint64_t projects_handled = 0;
  while (!api.finished && (res = api_fetch_projects(&api, &options, queue,
                                                    &projects_handled)) == 0) {
  }
  uint64_t expected = 0;
  __c11_atomic_compare_exchange_strong(&projects_count, &expected,
                                       projects_handled, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST);

end:
  if ((res = change_directory(cwd)) != 0)
    return res;
  api_destroy(&api);

  pthread_join(process_exit_watcher, NULL);
}
