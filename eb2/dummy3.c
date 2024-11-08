#include <errno.h>
#include <poll.h>
#include <sys/procdesc.h>
#include <sys/wait.h>
#include <unistd.h>

// High level os-independent API:
// - Create child process -> return (err, child_pid, child_fd) e.g. `fork() +
// pidfd_open()` (Linux) or  `pdfork()` (FreeBSD)
// - if child -> do your thing
// - if parent:
//   + wait with timeout on child_fd with `poll` or `select`
//   + if timed out:
//     * kill child with SIGKILL using child_fd e.g.
//     `pidfd_send_signal(child_fd)` (Linux) or `close(child_fd)` (FreeBSD).
//     * sleep a bit
//     * retry
//  + else: the child exited by itself
//    * get exit status using `wait4(child_pid, &status, ...)`
//    * if status == 0: exit(0)
//    * else:
//      - sleep a bit
//      - retry

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  for (;;) {
    int child_fd = 0;
    int child_pid = pdfork(&child_fd, PD_CLOEXEC);
    if (child_pid < 0) {
      return errno;
    }
    if (0 == child_pid) {
      argv += 1;
      // TODO: execvp.
      execve(argv[0], argv, 0);
    } else {
      struct pollfd poll_fd = {
          .fd = child_fd,
          .events = POLLHUP,
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
        close(child_fd);
      } else { // Child finished by itself.
        // Get exit status of child.
        int status = 0;
        wait4(child_pid, &status, 0, 0); // Reap zombie.

        if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
          return 0;
        }
      }

      sleep(1);
    }
  }
}
