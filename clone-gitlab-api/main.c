#include <assert.h>
#include <curl/curl.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"

#define MAX_URL_LEN 4096

typedef struct {
    gbString root_directory;
    gbString api_token;
    gbString url;
} options;
static bool verbose = false;

static void print_usage(int argc, char* argv[]) {
    printf(
        "%s\n"
        "\t[(-d|--root-directory) <directory>]\n"
        "\t[(-u|--url) <gitlab url>]\n"
        "\t[(-t|--api-token) <api token>] [-h|--help] [-v|--verbose]\n",
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

static int api_query_projects(gbAllocator allocator, options* opts) {
    CURL* http_handle = curl_easy_init();
    gbString url = gb_string_make_reserve(allocator, MAX_URL_LEN);
    url = gb_string_append_fmt(
        url,
        "%s/api/v4/"
        "projects?statistics=false&top_level=&with_custom_attributes=false",
        opts->url);
    curl_easy_setopt(http_handle, CURLOPT_URL, url);
    curl_easy_setopt(http_handle, CURLOPT_VERBOSE, verbose);
    curl_easy_setopt(http_handle, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(http_handle, CURLOPT_REDIR_PROTOCOLS, "http,https");
    gbString response_body =
        gb_string_make_reserve(allocator, 20 * 1024 * 1024);
    curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION,
                     on_http_response_body_chunk);
    curl_easy_setopt(http_handle, CURLOPT_WRITEDATA, &response_body);

    struct curl_slist* list = NULL;
    gbString token = gb_string_make_reserve(allocator, 512);
    token = gb_string_append_fmt(token, "PRIVATE-TOKEN: %s", opts->api_token);
    list = curl_slist_append(list, token);

    curl_easy_setopt(http_handle, CURLOPT_HTTPHEADER, list);

    CURLcode res = curl_easy_perform(http_handle);
    printf("res=%d\n", res);

    gb_string_free(url);
    gb_string_free(token);

    printf("[D001] response_body=%s\n", response_body);
    gb_string_free(response_body);
    return res;
}

int main(int argc, char* argv[]) {
    gbAllocator allocator = gb_heap_allocator();
    options opts = {0};
    options_parse_from_cli(allocator, argc, argv, &opts);

    api_query_projects(allocator, &opts);
}
