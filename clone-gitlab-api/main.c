#include <assert.h>
#include <curl/curl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"

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

typedef struct {
    gbString root_directory;
    gbString api_token;
    gbString gitlab_domain;
} options;
static bool verbose = false;

typedef struct {
    const int queue;
} watch_project_cloning_arg;

typedef struct {
    CURL* http_handle;
    gbString response_body;
    gbString url;
    gbArray(jsmntok_t) tokens;
    bool finished;
} api_t;

static gbAtomic64 projects_count = {0};

static void print_usage(int argc, char* argv[]) {
    printf(
        "Clone or update all git repositories from Gitlab.\n\n"
        "USAGE:\n"
        "\t%s [OPTIONS]\n\n"
        "OPTIONS:\n"
        "\t-d, --root-directory <DIRECTORY>    The root directory to "
        "clone/update all "
        "the projects\n"
        "\t-u, --url <GITLAB URL>\n"
        "\t-t, --api-token <API TOKEN>         The api token from gitlab to "
        "fetch "
        "private repositories\n"
        "\t-h, --help\n"
        "\t-v, --verbose\n\n"
        "The repositories are cloned with git over ssh with a depth of 1, "
        "without tags, in a flat manner.\n"
        "If some repositories already exist in the root directory, they are "
        "updated (with git pull) instead of cloned.\n"
        "If some repositories fail, this command does not stop and tries to "
        "clone or update the other repositories.\n\n"
        "EXAMPLES:\n\n"
        "\tclone-gitlab-api -u gitlab.com -t abcdef123 -d /tmp/git/\n\n"
        "Clone/update all repositories from gitlab.custom.com with the token "
        "'abcdef123' "
        "in "
        "the directory /tmp/git verbosely:\n\n"
        "\tclone-gitlab-api -u gitlab.custom.com -t abcdef123 -d /tmp/git/ "
        "-v\n\n",
        argv[0]);
}

static bool str_equal(const char* a, usize a_len, const char* b, usize b_len) {
    assert(a != NULL);
    assert(b != NULL);

    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static bool str_equal_c(const char* a, usize a_len, const char* b0) {
    assert(a != NULL);
    assert(b0 != NULL);

    return str_equal(a, a_len, b0, strlen(b0));
}

static bool is_directory(const char* path) {
    assert(path != NULL);

    struct stat s = {0};
    if (stat(path, &s) == -1) {
        return false;
    }
    return S_ISDIR(s.st_mode);
}

static u64 str_to_u64(const char* s, usize s_len) {
    assert(s != NULL);

    u64 res = 0;
    for (u64 i = 0; i < s_len; i++) {
        const char c = s[i];
        if (gb_char_is_space(c)) continue;
        if (gb_char_is_digit(c)) {
            const int v = c - '0';
            res *= 10;
            res += v;
        } else
            return 0;
    }
    return res;
}

static usize on_http_response_body_chunk(void* contents, usize size,
                                         usize nmemb, void* userp) {
    assert(contents != NULL);
    assert(userp != NULL);

    const usize real_size = size * nmemb;
    gbString* response_body = userp;
    *response_body =
        gb_string_append_length(*response_body, contents, real_size);

    return real_size;
}

static usize on_header(char* buffer, usize size, usize nitems, void* userdata) {
    assert(buffer != NULL);
    assert(userdata != NULL);
    api_t* const api = userdata;

    const usize real_size = nitems * size;
    const char* val = memchr(buffer, ':', real_size);
    if (val == NULL) return real_size;  // Could be HTTP/1.1 OK, skip

    assert(val > buffer);
    assert(val < buffer + real_size);
    const usize key_len = val - buffer;
    val++;  // Skip `:`
    usize val_len = buffer + real_size - val;

    if (str_equal_c(buffer, key_len, "Link")) {
        const char needle[] = ">; rel=\"next\"";
        const u64 needle_len = sizeof(needle) - 1;
        char* end = memmem(val, val_len, needle, needle_len);
        if (end == NULL) {
            // Finished - no more pages
            api->finished = true;
            return real_size;
        }

        char* start = end;
        while (start > val && *start != '<') start--;
        if (start == val) {
            fprintf(stderr, "Failed to parse HTTP header Link: %.*s\n",
                    (int)val_len, val);
            return 0;
        }

        start++;  // Skip `<`
        val_len = end - start;
        val = start;

        printf("[D003] Link: `%.*s`\n", (int)val_len, val);

        assert(api->url != NULL);
        gb_string_clear(api->url);
        api->url = gb_string_append_length(api->url, val, val_len);
    } else if (str_equal_c(buffer, key_len, "X-Total")) {
        const u64 total = str_to_u64(val, val_len);
        gb_atomic64_compare_exchange(&projects_count, 0, total);
    }

    return nitems * size;
}

static void api_init(gbAllocator allocator, api_t* api, options* opts) {
    assert(api != NULL);
    assert(opts != NULL);
    assert(opts->gitlab_domain != NULL);

    api->response_body = gb_string_make_reserve(allocator, 200 * 1024);

    gb_array_init_reserve(api->tokens, gb_heap_allocator(), 8 * 1000);

    api->http_handle = curl_easy_init();
    assert(api->http_handle != NULL);

    api->url = gb_string_make_reserve(allocator, MAX_URL_LEN);
    api->url = gb_string_append_fmt(
        api->url,
        "%s/api/v4/"
        "projects?statistics=false&top_level=&with_custom_"
        "attributes=false&simple=true&per_page=100&all_available="
        "true&order_by=id&sort=asc",
        opts->gitlab_domain);
    curl_easy_setopt(api->http_handle, CURLOPT_VERBOSE, verbose);
    curl_easy_setopt(api->http_handle, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(api->http_handle, CURLOPT_REDIR_PROTOCOLS, "http,https");
    curl_easy_setopt(api->http_handle, CURLOPT_WRITEFUNCTION,
                     on_http_response_body_chunk);
    curl_easy_setopt(api->http_handle, CURLOPT_WRITEDATA, &api->response_body);
    curl_easy_setopt(api->http_handle, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(api->http_handle, CURLOPT_HEADERDATA, api);

    if (opts->api_token != NULL) {
        struct curl_slist* list = NULL;
        gbString token_header = gb_string_make_reserve(
            allocator, 20 + gb_string_length(opts->api_token));
        token_header = gb_string_append_fmt(token_header, "PRIVATE-TOKEN: %s",
                                            opts->api_token);
        list = curl_slist_append(list, token_header);

        curl_easy_setopt(api->http_handle, CURLOPT_HTTPHEADER, list);
        // TODO: free token_header?
    }
}

static void api_destroy(api_t* api) {
    assert(api != NULL);

    gb_string_free(api->url);
    gb_string_free(api->response_body);
    gb_array_free(api->tokens);
}

static void options_parse_from_cli(gbAllocator allocator, int argc,
                                   char* argv[], options* opts) {
    assert(argv != NULL);
    assert(opts != NULL);

    struct option longopts[] = {
        {.name = "root-directory",
         .has_arg = required_argument,
         .flag = NULL,
         .val = 'd'},
        {.name = "api-token",
         .has_arg = required_argument,
         .flag = NULL,
         .val = 't'},
        {.name = "url", .has_arg = required_argument, .flag = NULL, .val = 'u'},
        {.name = "help", .has_arg = no_argument, .flag = NULL, .val = 'h'},
        {.name = "verbose", .has_arg = no_argument, .flag = NULL, .val = 'v'},
    };

    int ch = 0;
    while ((ch = getopt_long(argc, argv, "vhd:t:u:", longopts, NULL)) != -1) {
        switch (ch) {
            case 'd': {
                opts->root_directory = gb_string_make(allocator, optarg);
                if (strlen(optarg) > MAXPATHLEN) {
                    fprintf(stderr,
                            "Directory is too long: maximum %d characters\n",
                            MAXPATHLEN);
                    exit(EINVAL);
                }
                break;
            }
            case 't': {
                if (strlen(optarg) > 128) {
                    fprintf(stderr,
                            "Token is too long: maximum 128 characters\n");
                    exit(EINVAL);
                }
                opts->api_token = gb_string_make(allocator, optarg);
                break;
            }
            case 'u': {
                if (strlen(optarg) > MAX_URL_LEN) {
                    fprintf(stderr, "Url is too long: maximum %d characters\n",
                            MAX_URL_LEN);
                    exit(EINVAL);
                }

                if (!gb_str_has_prefix(optarg, "https://")) {
                    opts->gitlab_domain = gb_string_make_reserve(
                        allocator, strlen(optarg) + sizeof("https://"));
                    opts->gitlab_domain = gb_string_append_fmt(
                        opts->gitlab_domain, "https://%s", optarg);
                } else
                    opts->gitlab_domain = gb_string_make(allocator, optarg);

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
}

static int api_query_projects(gbAllocator allocator, api_t* api) {
    assert(api != NULL);
    assert(api->url != NULL);

    int res = 0;
    curl_easy_setopt(api->http_handle, CURLOPT_URL, api->url);

    if ((res = curl_easy_perform(api->http_handle)) != 0) {
        i32 error;
        curl_easy_getinfo(api->http_handle, CURLINFO_OS_ERRNO, &error);
        fprintf(stderr,
                "Failed to query api: url=%s response_body=%s res=%d err=%s "
                "errno=%d\n",
                api->url, api->response_body, res, curl_easy_strerror(res),
                error);
        return res;
    }

    return res;
}

static int upsert_project(gbString path, char* git_url, char* fs_path,
                          const options* opts, int queue);

static int api_parse_and_upsert_projects(api_t* api, const options* opts,
                                         int queue,
                                         uint64_t* projects_handled) {
    assert(api != NULL);

    jsmn_parser p;

    gb_array_clear(api->tokens);
    int res = 0;
    do {
        jsmn_init(&p);
        res = jsmn_parse(&p, api->response_body,
                         gb_string_length(api->response_body), api->tokens,
                         gb_array_capacity(api->tokens));
        if (res == JSMN_ERROR_NOMEM) {
            gb_array_reserve(api->tokens,
                             gb_array_capacity(api->tokens) * 2 + 8);
            continue;
        }
        if (res < 0 && res != JSMN_ERROR_NOMEM) {
            fprintf(stderr, "Failed to parse JSON: body=%s res=%d\n",
                    api->response_body, res);
            return res;
        }
        if (res == 0) {
            fprintf(stderr,
                    "Failed to parse JSON (is it empty?): body=%s res=%d\n",
                    api->response_body, res);
            return res;
        }
    } while (res == JSMN_ERROR_NOMEM);

    gb_array_resize(api->tokens, res);
    res = 0;

    const usize tokens_count = gb_array_count(api->tokens);
    assert(tokens_count > 0);
    if (api->tokens[0].type != JSMN_ARRAY) {
        fprintf(
            stderr,
            "Received unexpected JSON response: expected array, got: %.*s\n",
            (int)gb_string_length(api->response_body), api->response_body);
        return EINVAL;
    }

    const char key_path_with_namespace[] = "path_with_namespace";
    const usize key_path_with_namespace_len = sizeof("path_with_namespace") - 1;
    const char key_git_url[] = "ssh_url_to_repo";
    const usize key_git_url_len = sizeof("ssh_url_to_repo") - 1;

    char* fs_path = NULL;
    gbString path_with_namespace = NULL;
    char* git_url = NULL;
    u64 field_count = 0;

    for (int i = 1; i < gb_array_count(api->tokens); i++) {
        jsmntok_t* const cur = &api->tokens[i - 1];
        jsmntok_t* const next = &api->tokens[i];
        if (!(cur->type == JSMN_STRING && next->type == JSMN_STRING)) continue;

        char* const cur_s = &api->response_body[cur->start];
        const usize cur_s_len = cur->end - cur->start;
        char* const next_s = &api->response_body[next->start];
        const usize next_s_len = next->end - next->start;

        if (str_equal(cur_s, cur_s_len, key_path_with_namespace,
                      key_path_with_namespace_len)) {
            field_count++;
            path_with_namespace =
                gb_string_make_length(gb_heap_allocator(), next_s, next_s_len);

            // `execvp(2)` expects null terminated strings
            // This is safe to do because we override the terminating double
            // quote which no one cares about
            fs_path = next_s;
            fs_path[next_s_len] = 0;
            for (int j = 0; j < next_s_len; j++) {
                if (fs_path[j] == '/') fs_path[j] = '.';
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
                                          opts, queue)) != 0)
                    return res;
                git_url = NULL;

                *projects_handled += 1;
            }

            i++;
        }
    }

    return res;
}

static void* watch_workers(void* varg) {
    assert(varg != NULL);
    watch_project_cloning_arg* arg = varg;

    u64 finished = 0;
    struct kevent events[512] = {0};

    const bool is_tty = isatty(2);

    do {
        int event_count = kevent(arg->queue, NULL, 0, events, 512, 0);
        if (event_count == -1) {
            fprintf(stderr, "Failed to kevent(2) to query events: err=%s\n",
                    strerror(errno));
            return NULL;
        }

        for (int i = 0; i < event_count; i++) {
            const struct kevent* const event = &events[i];

            if ((event->filter == EVFILT_PROC) &&
                (event->fflags & NOTE_EXITSTATUS)) {
                const int exit_status = (event->data >> 8);
                char* const path_with_namespace = event->udata;

                finished += 1;
                const i64 count = gb_atomic64_load(&projects_count);
                char s[26] = "";
                if (count == 0) {
                    memcpy(s, "?", 1);
                } else {
                    gb_i64_to_str(count, s, 10);
                }

                if (exit_status == 0) {
                    printf(
                        "%s[%llu/%s] ✓ "
                        "%s%s\n",
                        pg_colors[is_tty][COL_GREEN], finished, s,
                        path_with_namespace, pg_colors[is_tty][COL_RESET]);
                } else {
                    printf(
                        "%s[%llu/%s] ❌ "
                        "%s (%d)%s\n",
                        pg_colors[is_tty][COL_RED], finished, s,
                        path_with_namespace, exit_status,
                        pg_colors[is_tty][COL_RESET]);
                }
                gb_string_free(path_with_namespace);
            }
        }
    } while (gb_atomic64_load(&projects_count) == 0 ||
             finished < gb_atomic64_load(&projects_count));

    struct timeval end = {0};
    gettimeofday(&end, NULL);
    printf("Finished in %lds\n", end.tv_sec - start.tv_sec);

    return NULL;
}

static int worker_update_project(char* fs_path, gbString git_url,
                                 const options* opts) {
    assert(fs_path != NULL);
    assert(git_url != NULL);
    assert(opts != NULL);

    if (chdir(fs_path) == -1) {
        fprintf(stderr, "Failed to chdir(2): fs_path=%s err=%s\n", fs_path,
                strerror(errno));
        exit(errno);
    }

    char* const argv[] = {"git", "pull",      "--quiet", "--depth",
                          "1",   "--no-tags", 0};

    if (execvp("git", argv) == -1) {
        fprintf(stderr, "Failed to pull: git_url=%s err=%s\n", git_url,
                strerror(errno));
        exit(errno);
    }
    assert(0 && "Unreachable");
    return 0;
}

static int worker_clone_project(char* fs_path, gbString git_url,
                                const options* opts) {
    assert(fs_path != NULL);
    assert(git_url != NULL);
    assert(opts != NULL);

    char* const argv[] = {"git",       "clone", "--quiet", "--depth", "1",
                          "--no-tags", git_url, fs_path,   0};

    if (execvp("git", argv) == -1) {
        fprintf(stderr, "Failed to clone: git_url=%s err=%s\n", git_url,
                strerror(errno));
        exit(errno);
    }
    assert(0 && "Unreachable");
    return 0;
}

static int change_directory(char* path) {
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

static int record_process_finished_event(int queue, pid_t pid,
                                         char* path_with_namespace) {
    assert(path_with_namespace != NULL);

    struct kevent event = {
        .filter = EVFILT_PROC,
        .ident = pid,
        .flags = EV_ADD | EV_ONESHOT,
        .fflags = NOTE_EXIT | NOTE_EXITSTATUS,
        .udata = path_with_namespace,
    };
    if (kevent(queue, &event, 1, NULL, 0, 0) == -1) {
        fprintf(stderr,
                "Failed to kevent(2) to watch for child process: err=%s\n",
                strerror(errno));
        return errno;
    }
    return 0;
}

static int upsert_project(gbString path, char* git_url, char* fs_path,
                          const options* opts, int queue) {
    assert(path != NULL);
    assert(git_url != NULL);
    assert(opts != NULL);

    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
        return errno;
    } else if (pid == 0) {
        if (is_directory(fs_path)) {
            worker_update_project(fs_path, git_url, opts);
        } else {
            worker_clone_project(fs_path, git_url, opts);
        }
        assert(0 && "Unreachable");
    } else {
        int res = 0;
        if ((res = record_process_finished_event(queue, pid, path)) != 0)
            return res;
    }
    return 0;
}

static int api_fetch_projects(gbAllocator allocator, api_t* api,
                              const options* opts, int queue,
                              uint64_t* projects_handled) {
    assert(api != NULL);
    assert(opts != NULL);

    int res = 0;
    gb_string_clear(api->response_body);

    if ((res = api_query_projects(allocator, api)) != 0) return res;

    if ((res = api_parse_and_upsert_projects(api, opts, queue,
                                             projects_handled)) != 0)
        return res;

    return 0;
}

int main(int argc, char* argv[]) {
    gettimeofday(&start, NULL);
    gbAllocator allocator = gb_heap_allocator();
    options opts = {0};
    options_parse_from_cli(allocator, argc, argv, &opts);

    int res = 0;
    // Do not require wait(2) on child processes
    {
        struct sigaction sa = {.sa_flags = SA_NOCLDWAIT};
        if ((res = sigaction(SIGCHLD, &sa, NULL)) == -1) {
            fprintf(stderr, "Failed to sigaction(2): err=%s\n",
                    strerror(errno));
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
    api_init(allocator, &api, &opts);

    static char cwd[MAXPATHLEN] = "";
    if (getcwd(cwd, MAXPATHLEN) == NULL) {
        fprintf(stderr, "Failed to getcwd(2): err=%s\n", strerror(errno));
        return errno;
    }

    if ((res = change_directory(opts.root_directory)) != 0) return res;

    printf("Changed directory to: %s\n", opts.root_directory);

    watch_project_cloning_arg arg = {
        .queue = queue,
    };

    // Start process exit watcher thread, only after we know from the first API
    // query how many items there are
    pthread_t process_exit_watcher = {0};
    {
        if (pthread_create(&process_exit_watcher, NULL, watch_workers, &arg) !=
            0) {
            fprintf(stderr, "Failed to watch projects cloning: err=%s\n",
                    strerror(errno));
            goto end;
        }
    }

    uint64_t projects_handled = 0;
    while (!api.finished &&
           (res = api_fetch_projects(allocator, &api, &opts, queue,
                                     &projects_handled)) == 0) {
    }
    gb_atomic64_compare_exchange(&projects_count, 0, projects_handled);

end:
    if ((res = change_directory(cwd)) != 0) return res;
    api_destroy(&api);

    pthread_join(process_exit_watcher, NULL);
}
