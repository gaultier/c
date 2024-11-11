#define _DEFAULT_SOURCE
#include <liburing.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  (void)argc;

  struct io_uring ring = {0};
  if (io_uring_queue_init(1, &ring,
                          IORING_SETUP_SINGLE_ISSUER |
                              IORING_SETUP_DEFER_TASKRUN) < 0) {
    return 1;
  }

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

    struct io_uring_sqe *sqe = NULL;

    // Queue `waitid`.
    sqe = io_uring_get_sqe(&ring);
    siginfo_t si = {0};
    io_uring_prep_waitid(sqe, P_PID, (id_t)child_pid, &si, WEXITED, 0);
    sqe->user_data = 1;

    struct __kernel_timespec ts = {
        .tv_sec = wait_ms / 1000,
        .tv_nsec = (wait_ms % 1000) * 1000 * 1000,
    };
    struct io_uring_cqe *cqe = NULL;
    io_uring_submit_and_wait_timeout(&ring, &cqe, 1, &ts, NULL);

    printf("res=%d user_data=%llu status=%d exited=%d exit_status=%d\n",
           cqe->res, cqe->user_data, si.si_status, WIFEXITED(si.si_status),
           WEXITSTATUS(si.si_status));
    // If child exited successfully: the end.
    if (cqe->res == 0 && cqe->user_data == 1 && WIFEXITED(si.si_status) &&
        0 == WEXITSTATUS(si.si_status)) {
      return 0;
    }

    io_uring_cqe_seen(&ring, cqe);

    kill(child_pid, SIGKILL);
    wait(NULL);

    // Cancel pending wait.
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_cancel64(sqe, 1, 0);
    io_uring_submit(&ring);

    wait_ms *= 2;
    usleep(wait_ms * 1000);
  }
}
