#include "libc.h"

/* unsigned int usleep(unsigned int secs) {} */

static inline long syscall1(long n, long a1) {
  unsigned long ret;
  __asm__ __volatile__("syscall"
                       : "=a"(ret)
                       : "a"(n), "D"(a1)
                       : "rcx", "r11", "memory");
  return (long)ret;
}

[[noreturn]]
void exit(int status) {
  for (;;)
    syscall1(60, status);
}
