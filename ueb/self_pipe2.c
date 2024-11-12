#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  (void)argc;

  uint32_t wait_ms = 128;

  for (int retry = 0; retry < 10; retry += 1) {
    int pipe_fd[2] = {0};
    if (-1 == pipe(pipe_fd)) {
      return errno;
    }

    int child_pid = fork();
    if (-1 == child_pid) {
      return errno;
    }

    if (0 == child_pid) { // Child
      // Close the read end of the pipe.
      close(pipe_fd[0]);

      argv += 1;
      if (-1 == execvp(argv[0], argv)) {
        return errno;
      }
      __builtin_unreachable();
    }

    // Close the write end of the pipe.
    close(pipe_fd[1]);

    struct pollfd poll_fd = {
        .fd = pipe_fd[0],
        .events = POLLHUP | POLLIN,
    };

    // Wait for the child to finish with a timeout.
    poll(&poll_fd, 1, (int)wait_ms);

    kill(child_pid, SIGKILL);
    int status = 0;
    wait(&status);
    if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
      return 0;
    }

    close(pipe_fd[0]);

    usleep(wait_ms * 1000);
    wait_ms *= 2;
  }
  return 1;
}
