#pragma once

/* unsigned int usleep(unsigned int secs); */
#if defined(__x86_64__)
typedef unsigned long int uint64_t;
typedef uint64_t size_t;
typedef signed long int int64_t;
typedef int64_t ssize_t;
#else
#error "os/arch not implemented"
#endif

void exit(int);
ssize_t write(int fd, const void *buf, size_t count);

static inline long syscall1(long n, long a1);
static inline long syscall2(long n, long a1, long a2);
static inline long syscall3(long n, long a1, long a2, long a3);
static inline long syscall4(long n, long a1, long a2, long a3, long a4);
static inline long syscall5(long n, long a1, long a2, long a3, long a4,
                            long a5);
static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5,
                            long a6);
