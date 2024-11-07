#define _POSIX_C_SOURCE 200112L
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

static int pipe_fd[2];

static void sigchld_handler(int sig) {
  char c = 0;
  write(pipe_fd[1], &c, 1);
}

int main(int argc, char *argv[]) {
  signal(SIGCHLD, sigchld_handler);

  pipe(pipe_fd);

  int pid = fork();
  if (-1 == pid)
    return 1;

  if (pid == 0) {
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    execvp(argv[0], argv + 1);
  } else {
    struct pollfd poll_fd = {
        .fd = pipe_fd[0],
        .events = POLLIN,
    };
    int res = poll(&poll_fd, 1, 1000);
    printf("\n%d\n", res);
  }
}
