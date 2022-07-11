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
    gbString url;
} options;
static bool verbose = false;

typedef struct {
    u64 current_page;
    u64 total_pages;
    u64 total_items;
} pagination_t;

typedef struct {
    int queue;
    gbArray(gbString) path_with_namespaces;
    pthread_mutex_t project_mutex;
    const u64 project_count;
} watch_project_cloning_arg;

typedef struct {
    CURL* http_handle;
    gbString response_body;
    gbString url;
    pagination_t pagination;
    gbArray(jsmntok_t) tokens;
} api_t;

static void print_usage(int argc, char* argv[]) {
    printf(
        "%s\n"
        "\t[(-d|--root-directory) <directory>]\n"
        "\t[(-u|--url) <gitlab url>]\n"
        "\t[(-t|--api-token) <api token>]\n"
        "\t[-h|--help]\n"
        "\t[-v|--verbose]\n",
        argv[0]);
}

static bool str_equal(const char* a, usize a_len, const char* b, usize b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static bool str_equal_c(const char* a, usize a_len, const char* b0) {
    return str_equal(a, a_len, b0, strlen(b0));
}

static bool is_directory(const char* path) {
    struct stat s = {0};
    if (stat(path, &s) == -1) {
        return false;
    }
    return S_ISDIR(s.st_mode);
}

static u64 str_to_u64(const char* s, usize s_len) {
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
    const usize real_size = size * nmemb;
    gbString* response_body = userp;
    *response_body =
        gb_string_append_length(*response_body, contents, real_size);

    return real_size;
}

static usize on_header(char* buffer, usize size, usize nitems, void* userdata) {
    assert(buffer != NULL);
    assert(userdata != NULL);

    const usize real_size = nitems * size;
    const char* val = memchr(buffer, ':', real_size);
    if (val == NULL) return real_size;  // Could be HTTP/1.1 OK, skip

    assert(val > buffer);
    assert(val < buffer + real_size);
    const usize key_len = val - buffer;
    val++;  // Skip `:`
    const usize val_len = buffer + real_size - val;

    pagination_t* const pagination = userdata;

    // Only set once
    if (pagination->total_pages == 0 &&
        str_equal_c(buffer, key_len, "X-Total-Pages")) {
        pagination->total_pages = str_to_u64(val, val_len);
    }

    // Only set once
    if (pagination->total_items == 0 &&
        str_equal_c(buffer, key_len, "X-Total")) {
        pagination->total_items = str_to_u64(val, val_len);
    }

    return nitems * size;
}

static void api_init(gbAllocator allocator, api_t* api, options* opts) {
    assert(api != NULL);
    assert(opts != NULL);

    api->pagination = (pagination_t){.current_page = 1};
    api->response_body = gb_string_make_reserve(allocator, 20 * 1024 * 1024);

    gb_array_init_reserve(api->tokens, gb_heap_allocator(), 100 * 1000);

    api->http_handle = curl_easy_init();
    assert(api->http_handle != NULL);

    api->url = gb_string_make_reserve(allocator, MAX_URL_LEN);
    api->url = gb_string_append(api->url, opts->url);
    curl_easy_setopt(api->http_handle, CURLOPT_VERBOSE, verbose);
    curl_easy_setopt(api->http_handle, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(api->http_handle, CURLOPT_REDIR_PROTOCOLS, "http,https");
    curl_easy_setopt(api->http_handle, CURLOPT_WRITEFUNCTION,
                     on_http_response_body_chunk);
    curl_easy_setopt(api->http_handle, CURLOPT_WRITEDATA, &api->response_body);
    curl_easy_setopt(api->http_handle, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(api->http_handle, CURLOPT_HEADERDATA, &api->pagination);

    struct curl_slist* list = NULL;
    gbString token_header = gb_string_make_reserve(allocator, 512);
    token_header = gb_string_append_fmt(token_header, "PRIVATE-TOKEN: %s",
                                        opts->api_token);
    list = curl_slist_append(list, token_header);

    curl_easy_setopt(api->http_handle, CURLOPT_HTTPHEADER, list);
}

static void api_destroy(api_t* api) {
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
                break;
            }
            case 't': {
                opts->api_token = gb_string_make(allocator, optarg);
                break;
            }
            case 'u': {
                if (!gb_str_has_prefix(optarg, "https://")) {
                    opts->url = gb_string_make_reserve(
                        allocator, strlen(optarg) + sizeof("https://"));
                    opts->url =
                        gb_string_append_fmt(opts->url, "https://%s", optarg);
                } else
                    opts->url = gb_string_make(allocator, optarg);

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

static int api_query_projects(gbAllocator allocator, api_t* api,
                              gbString base_url) {
    int res = 0;
    gb_string_clear(api->url);
    api->url = gb_string_append_fmt(
        api->url,
        "%s/api/v4/"
        "projects?statistics=false&top_level=&with_custom_"
        "attributes=false&simple=true&per_page=100&page=%llu&all_available="
        "true&order_by=id&sort=asc",
        base_url, api->pagination.current_page);
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

    api->pagination.current_page += 1;

    return res;
}

static int api_parse_projects(api_t* api,
                              gbArray(gbString) * path_with_namespaces,
                              gbArray(gbString) * git_urls,
                              pthread_mutex_t* project_mutex) {
    jsmn_parser p;

    gb_array_clear(api->tokens);
    int res = 0;
    do {
        jsmn_init(&p);
        res = jsmn_parse(&p, api->response_body,
                         gb_string_length(api->response_body), api->tokens,
                         gb_array_capacity(api->tokens));
        if (res == JSMN_ERROR_NOMEM) {
            gb_array_reserve(api->tokens, gb_array_capacity(api->tokens) * 2);
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
            res = EINVAL;
            return res;
        }
    } while (res == JSMN_ERROR_NOMEM);

    gb_array_resize(api->tokens, res);
    gb_array_set_capacity(api->tokens, res);
    res = 0;

    const char key_path_with_namespace[] = "path_with_namespace";
    const usize key_path_with_namespace_len = sizeof("path_with_namespace") - 1;
    const char key_git_url[] = "ssh_url_to_repo";
    const usize key_git_url_len = sizeof("ssh_url_to_repo") - 1;

    for (int i = 1; i < gb_array_count(api->tokens); i++) {
        jsmntok_t* const cur = &api->tokens[i - 1];
        jsmntok_t* const next = &api->tokens[i];
        if (!(cur->type == JSMN_STRING && next->type == JSMN_STRING)) continue;

        const char* cur_s = &api->response_body[cur->start];
        const usize cur_s_len = cur->end - cur->start;
        const char* next_s = &api->response_body[next->start];
        const usize next_s_len = next->end - next->start;

        if (str_equal(cur_s, cur_s_len, key_path_with_namespace,
                      key_path_with_namespace_len)) {
            gbString s =
                gb_string_make_length(gb_heap_allocator(), next_s, next_s_len);
            pthread_mutex_lock(project_mutex);
            gb_array_append(*path_with_namespaces, s);
            pthread_mutex_unlock(project_mutex);
            i++;
            continue;
        }
        if (str_equal(cur_s, cur_s_len, key_git_url, key_git_url_len)) {
            gbString s =
                gb_string_make_length(gb_heap_allocator(), next_s, next_s_len);
            pthread_mutex_lock(project_mutex);
            gb_array_append(*git_urls, s);
            pthread_mutex_unlock(project_mutex);
            i++;
        }
    }

    assert(gb_array_count(*path_with_namespaces) == gb_array_count(*git_urls));
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
                const int project_i = (int)(u64)event->udata;
                assert(project_i >= 0);

                finished += 1;
                pthread_mutex_lock(&arg->project_mutex);
                if (exit_status == 0) {
                    printf(
                        "%s[%llu/%llu] ✓ "
                        "%s%s\n",
                        pg_colors[is_tty][COL_GREEN], finished,
                        arg->project_count,
                        arg->path_with_namespaces[project_i],
                        pg_colors[is_tty][COL_RESET]);
                } else {
                    printf(
                        "%s[%llu/%llu] ❌ "
                        "%s (%d)%s\n",
                        pg_colors[is_tty][COL_RED], finished,
                        arg->project_count,
                        arg->path_with_namespaces[project_i], exit_status,
                        pg_colors[is_tty][COL_RESET]);
                }
                pthread_mutex_unlock(&arg->project_mutex);
            }
        }
    } while (finished < arg->project_count);

    struct timeval end = {0};
    gettimeofday(&end, NULL);
    printf("Finished in %lds\n", end.tv_sec - start.tv_sec);

    return NULL;
}

static int worker_update_project(gbString path, gbString fs_path, gbString url,
                                 const options* opts) {
    if (chdir(fs_path) == -1) {
        fprintf(stderr, "Failed to chdir(2): path=%s err=%s\n", fs_path,
                strerror(errno));
        exit(errno);
    }

    printf("Updating %s in %s\n", path, fs_path);

    char* const argv[] = {"git", "pull",      "--quiet", "--depth",
                          "1",   "--no-tags", 0};

    if (freopen("/dev/null", "w", stdout) == NULL) {
        fprintf(stderr, "Failed to silence subprocess: err=%s\n",
                strerror(errno));
    }

    if (execvp("git", argv) == -1) {
        fprintf(stderr, "Failed to pull: url=%s err=%s\n", url,
                strerror(errno));
        exit(errno);
    }
    assert(0 && "Unreachable");
    return 0;
}

static int worker_clone_project(gbString path, gbString fs_path, gbString url,
                                const options* opts) {
    printf("Cloning %s %s to %s\n", url, path, fs_path);

    char* const argv[] = {"git",       "clone", "--quiet", "--depth", "1",
                          "--no-tags", url,     fs_path,   0};

    if (freopen("/dev/null", "w", stdout) == NULL) {
        fprintf(stderr, "Failed to silence subprocess: err=%s\n",
                strerror(errno));
    }

    if (execvp("git", argv) == -1) {
        fprintf(stderr, "Failed to clone: url=%s err=%s\n", url,
                strerror(errno));
        exit(errno);
    }
    assert(0 && "Unreachable");
    return 0;
}

static int change_directory(char* path) {
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

static int record_process_finished_event(int queue, pid_t pid, int i) {
    struct kevent event = {
        .filter = EVFILT_PROC,
        .ident = pid,
        .flags = EV_ADD | EV_ONESHOT,
        .fflags = NOTE_EXIT | NOTE_EXITSTATUS,
        .udata = (void*)(u64)i,
    };
    if (kevent(queue, &event, 1, NULL, 0, 0) == -1) {
        fprintf(stderr,
                "Failed to kevent(2) to watch for child process: err=%s\n",
                strerror(errno));
        return errno;
    }
    return 0;
}

static int clone_projects_at(gbArray(gbString) path_with_namespaces,
                             gbArray(gbString) git_urls, const options* opts,
                             int queue, u64 project_offset) {
    for (int i = project_offset; i < gb_array_count(path_with_namespaces);
         i++) {
        pid_t pid = fork();
        if (pid == -1) {
            fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
            return errno;
        } else if (pid == 0) {
            gbString path = path_with_namespaces[i];
            gbString fs_path = gb_string_duplicate(gb_heap_allocator(), path);
            for (int j = 0; j < gb_string_length(fs_path); j++) {
                if (fs_path[j] == '/') fs_path[j] = '.';
            }
            gbString url = git_urls[i];
            if (is_directory(fs_path)) {
                worker_update_project(path, fs_path, url, opts);
            } else {
                worker_clone_project(path, fs_path, url, opts);
            }
            assert(0 && "Unreachable");
        } else {
            int res = 0;
            if ((res = record_process_finished_event(queue, pid, i)) != 0)
                return res;
        }
    }
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

    gbArray(gbString) path_with_namespaces;
    gb_array_init_reserve(path_with_namespaces, allocator, 100 * 1000);

    api_t api = {0};
    api_init(allocator, &api, &opts);
    gbArray(gbString) git_urls;
    gb_array_init_reserve(git_urls, allocator, 100 * 1000);

    static char cwd[MAXPATHLEN] = "";
    if (getcwd(cwd, MAXPATHLEN) == NULL) {
        fprintf(stderr, "Failed to getcwd(2): err=%s\n", strerror(errno));
        return errno;
    }

    if ((res = change_directory(opts.root_directory)) != 0) return res;

    printf("Changed directory to: %s\n", opts.root_directory);

    if ((res = api_query_projects(allocator, &api, opts.url)) != 0) goto end;
    assert(api.pagination.total_pages > 0);
    assert(api.pagination.current_page == 2);

    // Start process exit watcher thread
    pthread_t process_exit_watcher = {0};
    watch_project_cloning_arg arg = {
        .queue = queue,
        .path_with_namespaces = path_with_namespaces,
        .project_count = api.pagination.total_items};
    pthread_mutex_init(&arg.project_mutex, NULL);
    {
        if (pthread_create(&process_exit_watcher, NULL, watch_workers, &arg) !=
            0) {
            fprintf(stderr, "Failed to watch projects cloning: err=%s\n",
                    strerror(errno));
        }
    }

    while (api.pagination.current_page <= api.pagination.total_pages) {
        const u64 last_projects_count = gb_array_count(path_with_namespaces);
        if ((res = api_query_projects(allocator, &api, opts.url)) != 0)
            goto end;

        if ((res = api_parse_projects(&api, &path_with_namespaces, &git_urls,
                                      &arg.project_mutex)) != 0)
            goto end;

        if ((res = clone_projects_at(path_with_namespaces, git_urls, &opts,
                                     queue, last_projects_count)) != 0)
            goto end;

        gb_string_clear(api.response_body);
    }

    // TODO: change directory back even on error
end:
    if ((res = change_directory(cwd)) != 0) return res;
    api_destroy(&api);

    pthread_join(process_exit_watcher, NULL);
}
