#include "libc.h"

[[noreturn]]
void exit(int status) {
  for (;;)
    syscall1(1, status);
}
