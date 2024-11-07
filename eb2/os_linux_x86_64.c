#include "libc.h"

__attribute((noreturn)) void exit(int status) {
  for (;;)
    syscall1(60, status);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return syscall3(1, fd, (long)buf, (long)count);
}

int fork() { return (int)syscall0(57); }

int wait4(int pid, int *status, int options, void *rusage) {
  return (int)syscall4(61, pid, (long)status, (long)options, (long)rusage);
}

int wait(int *status) { return wait4(-1, status, 0, 0); }

int execve(const char *path, char *const argv[], char *const envp[]) {
  return (int)syscall3(59, (long)path, (long)argv, (long)envp);
}

int nanosleep(struct timespec *req, struct timespec *rem) {
  return (int)syscall2(35, (long)req, (long)rem);
}

unsigned int sleep(unsigned int secs) {
  struct timespec tv = {.tv_sec = secs};
  return (unsigned int)nanosleep(&tv, &tv);
}

struct k_sigaction {
  void (*handler)(int);
  unsigned long flags;
  void (*restorer)(void);
  unsigned mask[2];
};

void __restore_rt();

#define SA_RESTORER 0x04000000
#define SA_RESTART 0x10000000

int signal(int sig, void (*fn)(int)) {
  struct k_sigaction ksa = {
      .handler = fn,
      .flags = SA_RESTORER | SA_RESTART,
      .restorer = __restore_rt,
  };
  // rt_sigaction.
  return (int)syscall4(0xd, sig, (long)&ksa, 0, 8);
}

int poll(struct pollfd *fds, int nfds, int timeout) {
  struct timespec ts = {
      .tv_sec = timeout / 1000,
      .tv_nsec = (timeout % 1000) * 1000000,
  };

  // ppoll.
  return (int)syscall5(271, (long)fds, nfds, (long)&ts, 0, 0);
}

__asm__(".section .text.nolibc_memmove_memcpy\n"
        ".weak memmove\n"
        ".weak memcpy\n"
        "memmove:\n"
        "memcpy:\n"
        "movq %rdx, %rcx\n\t"
        "movq %rdi, %rax\n\t"
        "movq %rdi, %rdx\n\t"
        "subq %rsi, %rdx\n\t"
        "cmpq %rcx, %rdx\n\t"
        "jb   1f\n\t"
        "rep movsb\n\t"
        "retq\n"
        "1:" /* backward copy */
        "leaq -1(%rdi, %rcx, 1), %rdi\n\t"
        "leaq -1(%rsi, %rcx, 1), %rsi\n\t"
        "std\n\t"
        "rep movsb\n\t"
        "cld\n\t"
        "retq\n"

        ".section .text.nolibc_memset\n"
        ".weak memset\n"
        "memset:\n"
        "xchgl %eax, %esi\n\t"
        "movq  %rdx, %rcx\n\t"
        "pushq %rdi\n\t"
        "rep stosb\n\t"
        "popq  %rax\n\t"
        "retq\n"

        ".global __restore_rt\n"
        ".hidden __restore_rt\n"
        ".type __restore_rt,@function\n"
        "__restore_rt:\n"
        "	mov $15, %rax\n"
        "	syscall\n"
        ".size __restore_rt,.-__restore_rt\n");
