#include "libc.c"

// TODO: fork/exec/sleep/wait/poll(?)
// TODO: build for all os/archs
[[noreturn]] void _start() {
  char msg[] = "Hello world!";
  write(1, msg, sizeof(msg) - 1);
  exit(0);
}
