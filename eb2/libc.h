#pragma once

typedef unsigned int uint32_t;

#if defined(__x86_64__) || defined(__aarch64__)
typedef unsigned long int uint64_t;
typedef uint64_t size_t;
typedef signed long int int64_t;
typedef int64_t ssize_t;
#elif defined(__ARM_ARCH_5_) || defined(__ARM_ARCH_5E_) ||                     \
    defined(__ARM_ARCH_6_) || defined(__ARM_ARCH_6T2_) ||                      \
    defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) ||                     \
    defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) ||                    \
    defined(__ARM_ARCH_6KZ__) || defined(__ARM_ARCH_7_)
typedef unsigned int uint32_t;
typedef uint32_t size_t;
typedef signed int int32_t;
typedef int32_t ssize_t;
#else
#error "os/arch not implemented"
#endif

void exit(int);
ssize_t write(int fd, const void *buf, size_t count);
int fork();
int wait4(int pid, int *status, int options, void *rusage);
int wait(int *status);
int execve(const char *path, char *const argv[], char *const envp[]);
unsigned int sleep(unsigned int secs);
int signal(int sig, void (*fn)(int));

struct timespec {
  long tv_sec, tv_nsec;
};

#define SIGKILL 9
#define SIGCHLD 17 /* Child terminated or stopped.  */

int kill(int pid, int sig);
int pipe(int pipefd[2]);
int close(int fd);

#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WTERMSIG(s) ((s) & 0x7f)
#define WSTOPSIG(s) WEXITSTATUS(s)
#define WIFEXITED(s) (!WTERMSIG(s))
#define WIFSTOPPED(s) ((short)((((s) & 0xffff) * 0x10001U) >> 8) > 0x7f00)
#define WIFSIGNALED(s) (((s) & 0xffff) - 1U < 0xffu)

#define POLLIN 0x0001
#define POLLPRI 0x0002
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020

struct pollfd {
  int fd;
  short int events;
  short int revents;
};

int poll(struct pollfd *fds, int nfds, int timeout);

static inline long syscall0(long n);
static inline long syscall1(long n, long a1);
static inline long syscall2(long n, long a1, long a2);
static inline long syscall3(long n, long a1, long a2, long a3);
static inline long syscall4(long n, long a1, long a2, long a3, long a4);
static inline long syscall5(long n, long a1, long a2, long a3, long a4,
                            long a5);
static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5,
                            long a6);
