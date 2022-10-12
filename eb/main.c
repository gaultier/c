#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/wait.h>
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
        pid_t pid = {0};
        int ret = posix_spawnp(&pid, argv[1], NULL, NULL, argv + 2, NULL);
        if (ret < 0) {
            fprintf(stderr, "Failed to run command (malformed?): %s",
                    strerror(errno));
            return errno;
        }

        int status = 0;
        if (wait(&status) == -1) {
            fprintf(stderr, "Failed wait(2): %s", strerror(errno));
            return errno;
        }
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0)
                return 0;
            else {
                usleep(sleep_milliseconds);
                sleep_milliseconds *= 1.5;  // TODO: jitter, random
                continue;
            }
        }
        // TODO
    }
}
