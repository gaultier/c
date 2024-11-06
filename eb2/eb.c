#include "libc.c"

// TODO: fork/exec/sleep/wait/poll(?)
// TODO: build for all os/archs
int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  char msg[] = "Hello world!";
  write(1, msg, sizeof(msg) - 1);
  return 0;
}
