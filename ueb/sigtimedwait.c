#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

void on_sigchld(int sig) { (void)sig; }

int main(int argc, char *argv[]) {
  (void)argc;
  signal(SIGCHLD, on_sigchld);

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

    sigset_t sigset = {0};
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);

    siginfo_t siginfo = {0};

    struct timespec timeout = {
        .tv_sec = wait_ms / 1000,
        .tv_nsec = (wait_ms % 1000) * 1000 * 1000,
    };

    int sig = sigtimedwait(&sigset, &siginfo, &timeout);
    if (-1 == sig && EAGAIN != errno) { // Error
      return errno;
    }
    if (-1 != sig) { // Child finished.
      if (WIFEXITED(siginfo.si_status) && 0 == WEXITSTATUS(siginfo.si_status)) {
        return 0;
      }
    }

    if (-1 == kill(child_pid, SIGKILL)) {
      return errno;
    }

    if (-1 == wait(NULL)) {
      return errno;
    }

    usleep(wait_ms * 1000);
    wait_ms *= 2;
  }
  return 1;
}
