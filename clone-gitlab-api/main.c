#include <assert.h>
#include <getopt.h>
#include <stdio.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"

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

int main(int argc, char* argv[]) {
    gbAllocator allocator = gb_heap_allocator();
    options opts = {0};
    options_parse_from_cli(allocator, argc, argv, &opts);
}
