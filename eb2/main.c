#include "libc.c"

// TODO: fork/exec/sleep/poll(?)
// TODO: build for all os/archs
int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  {
    char msg[] = "Hello world!\n";
    write(1, msg, sizeof(msg) - 1);
  }

  int pid = fork();
  if (pid < 0) {
    return pid;
  }
  if (0 == pid) {
    char msg[] = "Child\n";
    write(1, msg, sizeof(msg) - 1);
  } else {
    char msg[] = "Parent\n";
    write(1, msg, sizeof(msg) - 1);

    wait(0);
  }
  return 0;
}
