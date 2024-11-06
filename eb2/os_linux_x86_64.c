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
