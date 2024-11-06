#include "libc.h"

[[noreturn]]
void exit(int status) {
  for (;;)
    syscall1(60, status);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return syscall3(1, fd, (long)buf, (long)count);
}

int fork() { return (int)syscall0(57); }
