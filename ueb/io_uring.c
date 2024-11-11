#define _DEFAULT_SOURCE
#include <liburing.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  (void)argc;

  struct io_uring ring = {0};
  if (io_uring_queue_init(2, &ring,
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

    io_uring_submit(&ring);

    struct __kernel_timespec ts = {
        .tv_sec = wait_ms / 1000,
        .tv_nsec = (wait_ms % 1000) * 1000 * 1000,
    };
    struct io_uring_cqe *cqe = NULL;

    int ret = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);

    printf("ret=%d res=%d user_data=%llu status=%d exited=%d exit_status=%d\n",
           ret, cqe ? cqe->res : 0, cqe ? cqe->user_data : 0, si.si_status,
           WIFEXITED(si.si_status), WEXITSTATUS(si.si_status));
    // If child exited successfully: the end.
    if (ret == 0 && cqe->res >= 0 && cqe->user_data == 1 &&
        WIFEXITED(si.si_status) && 0 == WEXITSTATUS(si.si_status)) {
      return 0;
    }
    if (ret == 0) {
      io_uring_cqe_seen(&ring, cqe);
    } else {
      kill(child_pid, SIGKILL);
      // Drain the CQE.
      ret = io_uring_wait_cqe(&ring, &cqe);
      io_uring_cqe_seen(&ring, cqe);
    }

    wait(NULL);

    wait_ms *= 2;
    usleep(wait_ms * 1000);
  }
  return 1;
}
