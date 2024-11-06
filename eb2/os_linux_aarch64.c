#include "libc.h"

[[noreturn]]
void exit(int status) {
  for (;;)
    syscall1(93, status);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return syscall3(64, fd, (long)buf, (long)count);
}
