/* #include "libc.h" */
// #include "/home/pg/not-my-code/linux-syscall-support/linux_syscall_support.h"
#include "/home/pg/not-my-code/linux/tools/include/nolibc/nolibc.h"

int main(int argc, char *argv[]) {
  (void)argc;

  uint32_t sleep_ms = 128 * 1000;
  for (int retry = 0; retry < 10; retry += 1) {
    int child_pid = fork();
    if (child_pid < 0) {
      return child_pid;
    }
    if (0 == child_pid) {
      argv += 1;
      // TODO: execvp.
      execve(argv[0], argv, 0);
    } else {
      // pidfd_open.
      int child_fd = (int)syscall(434, child_pid, 0);
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
        return -err;
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
      sleep_ms *= 2;
      usleep(sleep_ms);
    }
  }
  return 1;
}
