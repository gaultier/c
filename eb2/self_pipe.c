#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

static int pipe_fd[2] = {0};
void on_sigchld(int sig) {
  (void)sig;
  char dummy = 0;
  write(pipe_fd[1], &dummy, 1);
}

int main(int argc, char *argv[]) {
  (void)argc;

  if (-1 == pipe(pipe_fd)) {
    return errno;
  }

  signal(SIGCHLD, on_sigchld);

  uint32_t wait_ms = 128;

  for (int retry = 0; retry < 10; retry += 1) {
    int child_pid = fork();
    if (-1 == child_pid) {
      return errno;
    }

    if (0 == child_pid) { // Child
      argv += 1;
      if (-1 == execvp(argv[0], argv)) {
        return errno;
      }
      __builtin_unreachable();
    }

    struct pollfd poll_fd = {
        .fd = pipe_fd[0],
        .events = POLLIN,
    };
    sigset_t sigset = {0};
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);

    struct timespec timeout = {
        .tv_sec = wait_ms / 1000,
        .tv_nsec = (wait_ms % 1000) * 1000 * 1000,
    };
    // Wait for the child to finish with a timeout.
    int ret = ppoll(&poll_fd, 1, &timeout, &sigset);
    if (-1 == ret) {
      return errno;
    }

    kill(child_pid, SIGKILL);
    int status = 0;
    wait(&status);
    if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
      return 0;
    }

    char dummy = 0;
    read(pipe_fd[0], &dummy, 1);

    usleep(wait_ms * 1000);
    wait_ms *= 2;
  }
  return 1;
}
