#include "libc.h"

__attribute((noreturn)) void exit(int status) {
  for (;;)
    syscall1(93, status);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return syscall3(64, fd, (long)buf, (long)count);
}

#define SIGCHLD 17

int fork() { return (int)syscall2(220, SIGCHLD, 0); }

int wait4(int pid, int *status, int options, void *rusage) {
  return (int)syscall4(260, pid, (long)status, (long)options, (long)rusage);
}

int wait(int *status) { return wait4(-1, status, 0, 0); }
