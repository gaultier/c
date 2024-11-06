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

static inline long syscall0(long n);
static inline long syscall1(long n, long a1);
static inline long syscall2(long n, long a1, long a2);
static inline long syscall3(long n, long a1, long a2, long a3);
static inline long syscall4(long n, long a1, long a2, long a3, long a4);
static inline long syscall5(long n, long a1, long a2, long a3, long a4,
                            long a5);
static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5,
                            long a6);
