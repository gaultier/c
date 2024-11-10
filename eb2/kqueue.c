
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/event.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  (void)argc;

  uint32_t wait_ms = 128;
  int queue = kqueuex(KQUEUE_CLOEXEC);

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

    struct kevent change_list = {
        .ident = child_pid,
        .filter = EVFILT_PROC,
        .fflags = NOTE_EXIT,
        .flags = EV_ADD | EV_CLEAR,
    };

    struct kevent event_list = {0};

    struct timespec timeout = {
        .tv_sec = wait_ms / 1000,
        .tv_nsec = (wait_ms % 1000) * 1000 * 1000,
    };

    int ret = kevent(queue, &change_list, 1, &event_list, 1, &timeout);
    if (-1 == ret) { // Error
      return errno;
    }
    if (1 == ret) { // Child finished.
      int status = 0;
      if (-1 == wait(&status)) {
        return errno;
      }
      if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
        return 0;
      }
    }

    kill(child_pid, SIGKILL);
    wait(NULL);

    change_list = (struct kevent){
        .ident = child_pid,
        .filter = EVFILT_PROC,
        .fflags = NOTE_EXIT,
        .flags = EV_DELETE,
    };
    kevent(queue, &change_list, 1, NULL, 0, &timeout);

    usleep(wait_ms * 1000);
    wait_ms *= 2;
  }
  return 1;
}
