#include "libc.c"

// TODO: fork/exec/sleep/wait/poll(?)
[[noreturn]] void _start() {
  char msg[] = "Hello world!";
  write(1, msg, sizeof(msg) - 1);
  exit(0);
}
