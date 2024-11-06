#include "libc.h"

/* unsigned int usleep(unsigned int secs) {} */

[[noreturn]]
void exit(int status) {
  for (;;)
    syscall1(60, status);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return syscall3(1, fd, (long)buf, (long)count);
}
