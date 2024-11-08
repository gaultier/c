#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  (void)argc;

  uint32_t sleep_ms = 128 * 1000;

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

    // Parent.

    int child_fd = (int)syscall(SYS_pidfd_open, child_pid, 0);
    if (-1 == child_fd) {
      return errno;
    }

    struct pollfd poll_fd = {
        .fd = child_fd,
        .events = POLLHUP | POLLIN,
    };
    // Wait for the child to finish with a timeout.
    if (-1 == poll(&poll_fd, 1, 2000)) {
      return errno;
    }

    // Maybe kill the child (the child might have terminated by itself even if
    // poll(2) timed-out).
    if (-1 == syscall(SYS_pidfd_send_signal, child_fd, SIGKILL, NULL, 0)) {
      return errno;
    }

    siginfo_t siginfo = {0};
    // Get exit status of child & reap zombie.
    if (-1 == waitid(P_PIDFD, (id_t)child_fd, &siginfo, WEXITED)) {
      return errno;
    }

    if (WIFEXITED(siginfo.si_status) && 0 == WEXITSTATUS(siginfo.si_status)) {
      return 0;
    }

    sleep_ms *= 2;
    usleep(sleep_ms);

    close(child_fd);
  }
}
