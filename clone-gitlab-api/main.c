#include <assert.h>
#include <curl/curl.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"
#include "vendor/jsmn/jsmn.h"

#define MAX_URL_LEN 4096

typedef struct {
    gbString root_directory;
    gbString api_token;
    gbString url;
    bool dry_run;
} options;
static bool verbose = false;

typedef struct {
    u64 current_page;
    u64 total_pages;
} api_pagination;

typedef struct {
    int queue;
    gbArray(gbString) path_with_namespaces;
} watch_project_cloning_arg;

static void print_usage(int argc, char* argv[]) {
    printf(
        "%s\n"
        "\t[(-d|--root-directory) <directory>]\n"
        "\t[(-u|--url) <gitlab url>]\n"
        "\t[(-t|--api-token) <api token>]\n"
        "\t[-h|--help]\n"
        "\t[-n|--dry-run]\n"
        "\t[-v|--verbose]\n",
        argv[0]);
}

static bool str_equal(const char* a, usize a_len, const char* b, usize b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static bool str_equal_c(const char* a, usize a_len, const char* b0) {
    return str_equal(a, a_len, b0, strlen(b0));
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

static size_t on_http_response_body_chunk(void* contents, size_t size,
                                          size_t nmemb, void* userp) {
    const size_t real_size = size * nmemb;
    gbString* response_body = userp;
    *response_body =
        gb_string_append_length(*response_body, contents, real_size);

    return real_size;
}

static size_t on_header(char* buffer, size_t size, size_t nitems,
                        void* userdata) {
    assert(buffer != NULL);
    assert(userdata != NULL);

    const size_t real_size = nitems * size;
    const char* val = memchr(buffer, ':', real_size);
    if (val == NULL) return real_size;  // Could be HTTP/1.1 OK, skip

    assert(val > buffer);
    assert(val < buffer + real_size);
    const usize key_len = val - buffer;
    val++;  // Skip `:`
    const usize val_len = buffer + real_size - val;

    api_pagination* const pagination = userdata;

    // We re-parse it every time but because it could have changed
    // since the last response
    if (str_equal_c(buffer, key_len, "X-Total-Pages")) {
        pagination->total_pages = str_to_u64(val, val_len);
    }

    return nitems * size;
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
        {.name = "dry-run",
         .has_arg = no_argument,
         .flag = NULL,
         .val = 'n' /* Like make */},
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
            case 'n':
                opts->dry_run = true;
                break;
            default:
                print_usage(argc, argv);
                exit(0);
        }
    }
}

static int api_query_projects(gbAllocator allocator, options* opts,
                              gbString* response_body) {
    CURL* http_handle = curl_easy_init();
    gbString url = gb_string_make_reserve(allocator, MAX_URL_LEN);
    curl_easy_setopt(http_handle, CURLOPT_VERBOSE, verbose);
    curl_easy_setopt(http_handle, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(http_handle, CURLOPT_REDIR_PROTOCOLS, "http,https");
    curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION,
                     on_http_response_body_chunk);
    curl_easy_setopt(http_handle, CURLOPT_WRITEDATA, response_body);
    curl_easy_setopt(http_handle, CURLOPT_HEADERFUNCTION, on_header);
    api_pagination pagination = {.current_page = 1};
    curl_easy_setopt(http_handle, CURLOPT_HEADERDATA, &pagination);

    struct curl_slist* list = NULL;
    gbString token = gb_string_make_reserve(allocator, 512);
    token = gb_string_append_fmt(token, "PRIVATE-TOKEN: %s", opts->api_token);
    list = curl_slist_append(list, token);

    curl_easy_setopt(http_handle, CURLOPT_HTTPHEADER, list);

    do {
        url = gb_string_append_fmt(
            url,
            "%s/api/v4/"
            "projects?statistics=false&top_level=&with_custom_"
            "attributes=false&simple=true&per_page=100&page=%llu&all_available="
            "true&order_by=id&sort=asc",
            opts->url, pagination.current_page);
        curl_easy_setopt(http_handle, CURLOPT_URL, url);

        CURLcode res = curl_easy_perform(http_handle);
        if (res != 0) {
            gb_string_free(url);
            gb_string_free(token);

            fprintf(stderr, "Failed to query api: response_body=%s\n",
                    *response_body);
            return res;
        }

        pagination.current_page += 1;
    } while (pagination.current_page <= pagination.total_pages);
    return 0;
}

static int api_parse_projects(gbString body,
                              gbArray(gbString) * path_with_namespaces,
                              gbArray(gbString) * git_urls) {
    jsmn_parser p;
    gbArray(jsmntok_t) tokens;
    gb_array_init_reserve(tokens, gb_heap_allocator(), 100 * 1000);

    int res = 0;
    do {
        jsmn_init(&p);
        res = jsmn_parse(&p, body, gb_string_length(body), tokens,
                         gb_array_capacity(tokens));
        if (res == JSMN_ERROR_NOMEM) {
            gb_array_reserve(tokens, gb_array_capacity(tokens) * 2);
            continue;
        }
        if (res < 0 && res != JSMN_ERROR_NOMEM) {
            fprintf(stderr, "Failed to parse JSON: body=%s res=%d\n", body,
                    res);
            return res;
        }
        if (res == 0) return EINVAL;
    } while (res == JSMN_ERROR_NOMEM);

    gb_array_resize(tokens, res);

    const char key_path_with_namespace[] = "path_with_namespace";
    const usize key_path_with_namespace_len = sizeof("path_with_namespace") - 1;
    const char key_git_url[] = "ssh_url_to_repo";
    const usize key_git_url_len = sizeof("ssh_url_to_repo") - 1;

    for (int i = 1; i < res; i++) {
        jsmntok_t* const cur = &tokens[i - 1];
        jsmntok_t* const next = &tokens[i];
        if (!(cur->type == JSMN_STRING && next->type == JSMN_STRING)) continue;

        const char* cur_s = &body[cur->start];
        const usize cur_s_len = cur->end - cur->start;
        const char* next_s = &body[next->start];
        const usize next_s_len = next->end - next->start;

        if (str_equal(cur_s, cur_s_len, key_path_with_namespace,
                      key_path_with_namespace_len)) {
            gbString s =
                gb_string_make_length(gb_heap_allocator(), next_s, next_s_len);
            gb_array_append(*path_with_namespaces, s);
            i++;
            continue;
        }
        if (str_equal(cur_s, cur_s_len, key_git_url, key_git_url_len)) {
            gbString s =
                gb_string_make_length(gb_heap_allocator(), next_s, next_s_len);
            gb_array_append(*git_urls, s);
            i++;
        }
    }

    assert(gb_array_count(*path_with_namespaces) == gb_array_count(*git_urls));
    return 0;
}

static void* watch_project_cloning(void* varg) {
    watch_project_cloning_arg* arg = varg;

    u64 finished = 0;
    const u64 project_count = gb_array_count(arg->path_with_namespaces);
    gbArray(struct kevent) events;
    gb_array_init_reserve(events, gb_heap_allocator(), project_count);

    while (true) {
        int event_count =
            kevent(arg->queue, NULL, 0, events, gb_array_capacity(events), 0);
        if (event_count == -1) {
            fprintf(stderr, "Failed to kevent(2) to query events: err=%s\n",
                    strerror(errno));
            return NULL;
        }

        for (int i = 0; i < event_count; i++) {
            const struct kevent* const event = &events[i];

            if ((event->filter == EVFILT_PROC) &&
                (event->fflags & NOTE_EXITSTATUS)) {
                const u8 exit_status = event->data;
                const int project_i = (int)(u64)event->udata;
                assert(project_i >= 0);
                assert(project_i < project_count);

                finished += 1;
                const char emoji[][5] = {
                    "✓",
                    "❌",
                };
                printf(
                    "[%llu/%llu] %s Project clone finished: "
                    "exit_status=%d "
                    "path_with_namespace=%s\n",
                    finished, project_count,
                    exit_status == 0 ? emoji[0] : emoji[1], exit_status,
                    arg->path_with_namespaces[project_i]);
            }
        }
        if (finished == project_count) return NULL;
    }

    return NULL;
}

static int clone_projects(gbArray(gbString) path_with_namespaces,
                          gbArray(gbString) git_urls, const options* opts,
                          int queue) {
    assert(opts != NULL);
    assert(gb_array_count(path_with_namespaces) == gb_array_count(git_urls));

    gbString cwd = gb_string_make_reserve(gb_heap_allocator(), MAXPATHLEN);
    if (getcwd(cwd, MAXPATHLEN) == NULL) {
        fprintf(stderr, "Failed to getcwd(2): err=%s\n", strerror(errno));
        return errno;
    }

    if (chdir(opts->root_directory) == -1) {
        if (errno == ENOENT) {
            if (mkdir(opts->root_directory, S_IRWXU) == -1) {
                fprintf(stderr, "Failed to mkdir(2): path=%s err=%s\n",
                        opts->root_directory, strerror(errno));
                return errno;
            }
            printf("Created directory: %s\n", opts->root_directory);
            if (chdir(opts->root_directory) == -1) {
                fprintf(stderr, "Failed to chdir(2): path=%s err=%s\n",
                        opts->root_directory, strerror(errno));
                return errno;
            }
        } else {
            fprintf(stderr, "Failed to chdir(2): path=%s err=%s\n",
                    opts->root_directory, strerror(errno));
            return errno;
        }
    }
    printf("Changed directory to: %s\n", opts->root_directory);

    for (int i = 0; i < gb_array_count(path_with_namespaces); i++) {
        pid_t pid = fork();
        if (pid == -1) {
            fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
            return errno;
        } else if (pid == 0) {
            gbString path = path_with_namespaces[i];
            gbString url = git_urls[i];
            printf("Cloning %s %s\n", url, path);
            if (opts->dry_run) exit(0);

            for (int j = 0; j < gb_string_length(path); j++) {
                if (path[j] == '/') path[j] = '.';
            }

            char* const argv[] = {"git", "clone", "--quiet", url, path, 0};

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
        } else {
            struct kevent event = {
                .filter = EVFILT_PROC,
                .ident = pid,
                .flags = EV_ADD | EV_ONESHOT,
                .fflags = NOTE_EXIT | NOTE_EXITSTATUS,
                .udata = (void*)(u64)i,
            };
            if (kevent(queue, &event, 1, NULL, 0, 0) == -1) {
                fprintf(
                    stderr,
                    "Failed to kevent(2) to watch for child process: err=%s\n",
                    strerror(errno));
                return errno;
            }
        }
    }

    if (chdir(cwd) == -1) {
        fprintf(stderr, "Failed to chdir(2): path=%s err=%s\n",
                opts->root_directory, strerror(errno));
        return errno;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    gbAllocator allocator = gb_heap_allocator();
    options opts = {0};
    options_parse_from_cli(allocator, argc, argv, &opts);

    gbString response_body =
        gb_string_make_reserve(allocator, 20 * 1024 * 1024);
    int res = api_query_projects(allocator, &opts, &response_body);
    if (res != 0) return res;

    gbArray(gbString) path_with_namespaces;
    gb_array_init_reserve(path_with_namespaces, allocator, 100 * 1000);
    gbArray(gbString) git_urls;
    gb_array_init_reserve(git_urls, allocator, 100 * 1000);

    res = api_parse_projects(response_body, &path_with_namespaces, &git_urls);
    if (res != 0) return res;

    int queue = kqueue();
    if (queue == -1) {
        fprintf(stderr, "Failed to kqueue(2): err=%s\n", strerror(errno));
        return errno;
    }

    pthread_t thread;
    watch_project_cloning_arg arg = {
        .queue = queue, .path_with_namespaces = path_with_namespaces};
    if (!opts.dry_run) {
        if (pthread_create(&thread, NULL, watch_project_cloning, &arg) != 0) {
            fprintf(stderr, "Failed to watch projects cloning: err=%s\n",
                    strerror(errno));
        }
    }

    res = clone_projects(path_with_namespaces, git_urls, &opts, queue);
    if (res != 0) return res;

    if (!opts.dry_run) pthread_join(thread, NULL);
}
