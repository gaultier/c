#include "libc.h"

[[noreturn]]
void exit(int status) {
  for (;;)
    syscall1(1, status);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return syscall3(4, fd, (long)buf, (long)count);
}

int fork() { return (int)syscall0(2); }

int wait4(int pid, int *status, int options, void *rusage) {
  return (int)syscall4(7, pid, (long)status, (long)options, (long)rusage);
}

int wait(int *status) { return wait4(-1, status, 0, 0); }

int execve(const char *path, char *const argv[], char *const envp[]) {
  return (int)syscall3(59, (long)path, (long)argv, (long)envp);
}
