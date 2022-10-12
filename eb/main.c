#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#include "../vendor/gb/gb.h"

int main(int argc, char* argv[]) {
    if (argc == 1) {
        return EINVAL;
    }

    gbString cmd = gb_string_make_reserve(gb_heap_allocator(), 100);
    for (int i = 1; i < argc; i++) {
        cmd = gb_string_append_length(cmd, argv[i], strlen(argv[i]));
        cmd = gb_string_append_rune(cmd, ' ');
    }

    uint64_t sleep_milliseconds = 100;
    while (1) {
        puts(cmd);
        int ret = system(cmd);
        if (ret == 127) {
            fprintf(stderr, "Failed to run command (malformed?): %s",
                    strerror(errno));
            return errno > 0 ? errno : EINVAL;
        }
        if (ret == 0) return 0;

        usleep(sleep_milliseconds);
        sleep_milliseconds *= 1.5;  // TODO: jitter, random
    }
}
