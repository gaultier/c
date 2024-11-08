#include <errno.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/procdesc.h>

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
