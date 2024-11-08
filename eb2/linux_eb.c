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
    if (child_pid < 0) {
      return errno;
    }
    if (0 == child_pid) {
      argv += 1;
      execvp(argv[0], argv);
    } else {
      int child_fd = (int)syscall(SYS_pidfd_open, child_pid, 0);
      if (child_fd < 0) {
        return 1;
      }

      struct pollfd poll_fd = {
          .fd = child_fd,
          .events = POLLHUP | POLLIN,
      };
      int err = poll(&poll_fd, 1, 2000);
      //   0 -> Timeout.
      //   1 -> Child finished.
      // < 0 -> Error.
      // > 1 -> Unreachable.
      if (err < 0) { // Error.
        return errno;
      }
      if (err == 0) { // Timeout fired.
        // Kill the child.
        syscall(SYS_pidfd_send_signal, child_pid, SIGKILL, NULL, 0);
      } else { // Child finished by itself.
        // Get exit status of child.
        siginfo_t siginfo = {0};
        // Reap zombie.
        if (-1 == waitid(P_PIDFD, (id_t)child_fd, &siginfo, WEXITED)) {
          return errno;
        }

        if (WIFEXITED(siginfo.si_status) &&
            0 == WEXITSTATUS(siginfo.si_status)) {
          return 0;
        }
      }

      sleep_ms *= 2;
      usleep(sleep_ms);

      close(child_fd);
    }
  }
}
