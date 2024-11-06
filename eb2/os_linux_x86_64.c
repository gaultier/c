#include "libc.h"

/* unsigned int usleep(unsigned int secs) {} */

[[noreturn]]
void exit(int status) {
  for (;;)
    syscall1(60, status);
}
