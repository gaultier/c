#include "libc.c"

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
      int err = poll(&poll_fd, 1, 2000);
      if (err < 0) { // Error.
        return -err;
      }
      if (err == 0) { // Timeout.
        goto retry;
      }

      // Child terminated normally.
      int status = 0;
      wait(&status); // Reap zombie.

      if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
        return 0;
      }

    retry:
      kill(pid, SIGKILL);
      wait(0); // Reap zombie.
      sleep(1);
    }
  }
}
