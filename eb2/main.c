#include "libc.c"

static void on_sigchld(int) {}

// TODO: signal/pipe/poll
int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  signal(SIGCHLD, on_sigchld);

  for (;;) {
    int pid = fork();
    if (pid < 0) {
      return pid;
    }
    if (0 == pid) {
      argv += 1;
      execve(argv[0], argv, 0);
    } else {
      /* struct timespec tv = {.tv_sec = 1}; */
      /* int err = poll(0, 0, 0, 0, &tv, &sigset); */
      /* (void)err; */

      /* if (EINTR == err) { */
      /*   // TODO: get the exit code. */
      /* } */
      /* if (err < 0) { */
      /*   return -err; */
      /* } */

      /* int exited = 0 == ((uint32_t)status & 0x7f); */
      /* int exit_code = (((uint32_t)status) & 0xff00) >> 8; */
      /* if (exited && 0 == exit_code) { */
      /*   return 0; */
      /* } */

      sleep(1);
    }
  }
}
