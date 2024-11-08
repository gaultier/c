#define _POSIX_C_SOURCE 1
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

/* #include "/home/pg/not-my-code/linux-syscall-support/linux_syscall_support.h"
 */

static int pipe_fd[2];

static void on_sigchld(int sig) {
  (void)sig;

  char c = 0;
  write(pipe_fd[1], &c, 1);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  pipe(pipe_fd);
  signal(SIGCHLD, on_sigchld);

  for (;;) {
    int pid = fork();
    if (pid < 0) {
      return pid;
    }
    if (0 == pid) {
      close(pipe_fd[0]);
      close(pipe_fd[1]);

      argv += 1;
      execve(argv[0], argv, 0);
    } else {
      struct pollfd poll_fd = {
          .fd = 0, // TODO
          .events = POLLIN,
      };
      sigset_t mask = {0};
      sigemptyset(&mask);
      sigaddset(&mask, SIGCHLD);
      struct timespec ts = {.tv_sec = 2};
      int err = ppoll(&poll_fd, 1, &ts, &mask);
      //   0 -> Timeout.
      //   1 -> Self-pipe read.
      // < 0 -> Error.
      // > 1 -> Unreachable.
      if (err < 0) { // Error.
        return errno;
      }
      if (err == 1) {
        // Child terminated normally.
        int status = 0;
        wait(&status); // Reap zombie.

        if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
          return 0;
        }
      }

      // Retry (timeout or child exit with error);
      kill(pid, SIGKILL);
      wait(0); // Reap zombie.
      sleep(1);
    }
  }
}
