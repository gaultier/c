#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <sched.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

// High level os-independent API:
// - Create child process -> return (err, child_pid, child_fd) e.g. `fork() +
// pidfd_open()` (Linux) or `clone3(..., CLONE_PIDFD)` or  `pdfork()` (FreeBSD)
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
        syscall(SYS_pidfd_send_signal, child_pid, SIGKILL, NULL, 0);
      } else { // Child finished by itself.
        // Get exit status of child.
        int status = 0;
        siginfo_t siginfo = {0};
        waitid(P_PIDFD, (__id_t)child_fd, &siginfo, 0); // Reap zombie.

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
