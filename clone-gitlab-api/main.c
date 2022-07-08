#include <assert.h>
#include <curl/curl.h>
#include <getopt.h>
#include <stdio.h>
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

static size_t on_http_response_body_chunk(void* contents, size_t size,
                                          size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    gbString* response_body = userp;
    *response_body =
        gb_string_append_length(*response_body, contents, realsize);

    return realsize;
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
    url =
        gb_string_append_fmt(url,
                             "%s/api/v4/"
                             "projects?statistics=false&top_level=&with_custom_"
                             "attributes=false&simple=true&per_page=100",
                             opts->url);
    curl_easy_setopt(http_handle, CURLOPT_URL, url);
    curl_easy_setopt(http_handle, CURLOPT_VERBOSE, verbose);
    curl_easy_setopt(http_handle, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(http_handle, CURLOPT_REDIR_PROTOCOLS, "http,https");
    curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION,
                     on_http_response_body_chunk);
    curl_easy_setopt(http_handle, CURLOPT_WRITEDATA, response_body);

    struct curl_slist* list = NULL;
    gbString token = gb_string_make_reserve(allocator, 512);
    token = gb_string_append_fmt(token, "PRIVATE-TOKEN: %s", opts->api_token);
    list = curl_slist_append(list, token);

    curl_easy_setopt(http_handle, CURLOPT_HTTPHEADER, list);

    CURLcode res = curl_easy_perform(http_handle);
    printf("res=%d\n", res);

    gb_string_free(url);
    gb_string_free(token);

    /* printf("[D001] response_body=%s\n", *response_body); */
    return res;
}

static bool str_equal(const char* a, usize a_len, const char* b, usize b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static int api_parse_projects(gbString body,
                              gbArray(gbString) * path_with_namespaces,
                              gbArray(gbString) * git_urls) {
    jsmn_parser p;
    gbArray(jsmntok_t) tokens;
    gb_array_init_reserve(tokens, gb_heap_allocator(), 50 * 1000);

    jsmn_init(&p);
    int res = jsmn_parse(&p, body, gb_string_length(body), tokens,
                         gb_array_capacity(tokens));
    printf("[D002] res=%d\n", res);
    if (res < 0) return res;
    if (res == 0) return EINVAL;

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

static int clone_projects(gbArray(gbString) path_with_namespaces,
                          gbArray(gbString) git_urls, const options* opts) {
    assert(opts != NULL);
    assert(gb_array_count(path_with_namespaces) == gb_array_count(git_urls));

    gbString cwd = gb_string_make_reserve(gb_heap_allocator(), MAXPATHLEN);
    if (getcwd(cwd, MAXPATHLEN) == NULL) {
        fprintf(stderr, "Failed to getcwd(2): err=%s\n", strerror(errno));
        return errno;
    }

    if (chdir(opts->root_directory) == -1) {
        fprintf(stderr, "Failed to chdir(2): path=%s err=%s\n",
                opts->root_directory, strerror(errno));
        return errno;
    }
    printf("Changed directory to: %s\n", opts->root_directory);

    for (int i = 0; i < gb_array_count(path_with_namespaces); i++) {
        pid_t pid = fork();
        if (pid == -1) {
            fprintf(stderr, "Failed to fork(2): err=%s\n", strerror(errno));
            return errno;
        }
        if (pid == 0) {
            gbString path = path_with_namespaces[i];
            gbString url = git_urls[i];

            char* const argv[] = {"git", "clone", url, path, 0};

            printf("%s %s %s %s\n", argv[0], argv[1], argv[2], argv[3]);
            if (opts->dry_run) exit(0);

            if (execvp("git", argv) == -1) {
                fprintf(stderr, "Failed to clone: url=%s err=%s\n", url,
                        strerror(errno));
                exit(errno);
            }
            assert(0 && "Unreachable");
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

    clone_projects(path_with_namespaces, git_urls, &opts);
    /* gb_string_free(response_body); */
}
