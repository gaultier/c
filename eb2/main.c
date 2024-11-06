#include "libc.c"

// TODO: sleep/poll(?)
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

    char *cmd_argv[] = {"ls", "-l", 0};
    execve("/bin/ls", cmd_argv, 0);
  } else {
    char msg[] = "Parent\n";
    write(1, msg, sizeof(msg) - 1);

    wait(0);
  }
  return 0;
}
