#include "libc.c"

// TODO: sleep/poll(?)
int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  for (;;) {
    int pid = fork();
    if (pid < 0) {
      return pid;
    }
    if (0 == pid) {
      char *cmd_argv[] = {"ls", "-l", "/tmp/a/b", 0};
      execve("/bin/ls", cmd_argv, 0);
    } else {
      int status = 0;
      int err = wait(&status);
      if (err < 0) {
        return -err;
      }

      int exited = 0 == ((uint32_t)status & 0x7f);
      int exit_code = (((uint32_t)status) & 0xff00) >> 8;
      if (exited && 0 == exit_code) {
        return 0;
      }

      sleep(1);
    }
  }
}
