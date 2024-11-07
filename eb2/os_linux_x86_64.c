#include "libc.h"

__attribute((noreturn)) void exit(int status) {
  for (;;)
    syscall1(60, status);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return syscall3(1, fd, (long)buf, (long)count);
}

int fork() { return (int)syscall0(57); }

int wait4(int pid, int *status, int options, void *rusage) {
  return (int)syscall4(61, pid, (long)status, (long)options, (long)rusage);
}

int wait(int *status) { return wait4(-1, status, 0, 0); }

int execve(const char *path, char *const argv[], char *const envp[]) {
  return (int)syscall3(59, (long)path, (long)argv, (long)envp);
}

struct timespec {
  long tv_sec;
  long tv_nsec;
};

int nanosleep(struct timespec *req, struct timespec *rem) {
  return (int)syscall2(35, (long)req, (long)rem);
}

unsigned int sleep(unsigned int secs) {
  struct timespec tv = {.tv_sec = secs};
  return (unsigned int)nanosleep(&tv, &tv);
}

#define __SIGRTMAX 64
#define _NSIG (__SIGRTMAX + 1)

struct k_sigaction {
  void (*handler)(int);
  unsigned long flags;
  void (*restorer)(void);
  unsigned mask[2];
};
int sigaction(int sig, struct sigaction *act, struct sigaction *) {
  struct k_sigaction ksa = {
      .mask[0] = (uint32_t)act->sa_mask,
      .mask[1] = (uint32_t)act->sa_mask,
      .flags = (uint64_t)act->sa_flags,
      .handler = act->sa_handler,
      .restorer = act->sa_restorer,
  };
  return (int)syscall4(13, sig, (long)&ksa, 0, _NSIG / 8);
}

#define SA_RESTART 0x10000000
#define SIG_ERR (-1)

void *signal(int sig, void (*fn)(int)) {
  struct sigaction sa_old, sa = {.sa_handler = fn, .sa_flags = SA_RESTART};
  if (sigaction(sig, &sa, &sa_old) < 0)
    return (void *)SIG_ERR;
  return 0; // Difference.
}
