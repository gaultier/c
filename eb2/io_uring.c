#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <liburing.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

void on_sigchld(int sig) { (void)sig; }

int main(int argc, char *argv[]) {
  (void)argc;
  signal(SIGCHLD, on_sigchld);

  uint32_t wait_ms = 128;

  struct io_uring_params p = {0};
  int ring_fd = io_uring_setup(1, &p);
  if (-1 == ring_fd) {
    return errno;
  }

  assert(p.features & IORING_FEAT_SINGLE_MMAP);

  int sring_sz = (int)(p.sq_off.array + p.sq_entries * (int)sizeof(unsigned));
  int cring_sz =
      (int)(p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe));
  if (cring_sz > sring_sz) {
    sring_sz = cring_sz;
  }
  cring_sz = sring_sz;
  void *sq_ptr = mmap(0, (size_t)sring_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
  if (sq_ptr == MAP_FAILED) {
    return errno;
  }
  void *cq_ptr = sq_ptr;
  unsigned *sring_tail = (uintptr_t)sq_ptr + p.sq_off.tail;
  unsigned *sring_mask = (uintptr_t)sq_ptr + p.sq_off.ring_mask;
  unsigned *sring_array = (uintptr_t)sq_ptr + p.sq_off.array;

  /* Map in the submission queue entries array */
  struct io_urigin_sqe *sqes = mmap(
      0, p.sq_entries * sizeof(struct io_uring_sqe), PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQES);
  if (sqes == MAP_FAILED) {

#if 0
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
#endif
    return 1;
  }
