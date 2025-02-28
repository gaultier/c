#include <errno.h>
#include <libproc.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  (void)argc;

  uint32_t wait_ms = 128;

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

    // Wait for the child to finish with a timeout.
    int perr = 0;
    struct ps_prochandle prochandle = Pgrab(child_pid, PGRAB_NOSTOP, &perr);
    if (NULL == prochandle) {
      return perr;
    }
    Pwait(prochandle, wait_ms);

    kill(child_pid, SIGKILL);
    int status = 0;
    wait(&status);
    if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
      return 0;
    }
    Prelease(prochandle);

    usleep(wait_ms * 1000);
    wait_ms *= 2;
  }
  return 1;
}
