#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

/* #include "/home/pg/not-my-code/linux-syscall-support/linux_syscall_support.h"
 */

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  for (;;) {
    int child_pid = fork();
    if (child_pid < 0) {
      return child_pid;
    }
    if (0 == child_pid) {
      argv += 1;
      // TODO: execvp.
      execve(argv[0], argv, 0);
    } else {
      int child_fd = syscall(SYS_pidfd_open, child_pid, 0);
      struct pollfd poll_fd = {
          .fd = child_fd,
          .events = POLLIN,
      };
      int err = poll(&poll_fd, 1, 2000);
      //   0 -> Timeout.
      //   1 -> Child finished.
      // < 0 -> Error.
      // > 1 -> Unreachable.
      if (err < 0) { // Error.
        return errno;
      }
      if (err == 1) {
        // Child finished.
        int status = 0;
        wait(&status); // Reap zombie.

        if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
          return 0;
        }
      }

      // Retry (timeout or child exit with error);
      kill(child_pid, SIGKILL);
      wait(0); // Reap zombie.
      sleep(1);
    }
  }
}
