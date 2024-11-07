#include "/home/pg/not-my-code/linux/tools/include/nolibc/nolibc.h"

struct k_sigaction {
  void (*handler)(int);
  unsigned long flags;
  void (*restorer)(void);
  unsigned mask[2];
};

void __restore_rt();

static int signal(int sig, void (*fn)(int)) {
  struct k_sigaction ksa = {
      .handler = fn,
      .flags = SA_RESTORER | SA_RESTART,
      .restorer = __restore_rt,
  };
  return (int)syscall(0xd, sig, (long)&ksa, 0, 8);
}
// rt_sigaction(SIGCHLD, {sa_handler=0x2012a0, sa_mask=[],
// sa_flags=SA_RESTORER|SA_RESTART, sa_restorer=0x20159f}, {sa_handler=SIG_DFL,
// sa_mask=[], sa_flags=0}, 8) = 0

static void my_handler(int sig) {}

int main() { signal(SIGCHLD, my_handler); }
