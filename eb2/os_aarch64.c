
#define __asm_syscall(...)                                                     \
  do {                                                                         \
    __asm__ __volatile__("svc 0" : "=r"(x0) : __VA_ARGS__ : "memory", "cc");   \
    return x0;                                                                 \
  } while (0)

static inline long syscall0(long n) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0");
  __asm_syscall("r"(x8));
}

static inline long syscall1(long n, long a) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  __asm_syscall("r"(x8), "0"(x0));
}

static inline long syscall2(long n, long a, long b) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  __asm_syscall("r"(x8), "0"(x0), "r"(x1));
}

static inline long syscall3(long n, long a, long b, long c) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  __asm_syscall("r"(x8), "0"(x0), "r"(x1), "r"(x2));
}

static inline long syscall4(long n, long a, long b, long c, long d) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  register long x3 __asm__("x3") = d;
  __asm_syscall("r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3));
}

static inline long syscall5(long n, long a, long b, long c, long d, long e) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  register long x3 __asm__("x3") = d;
  register long x4 __asm__("x4") = e;
  __asm_syscall("r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4));
}

static inline long syscall6(long n, long a, long b, long c, long d, long e,
                            long f) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  register long x3 __asm__("x3") = d;
  register long x4 __asm__("x4") = e;
  register long x5 __asm__("x5") = f;
  __asm_syscall("r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5));
}

__asm__(".text \n"
        ".global _start\n"
        ".type _start,%function\n"
        "_start:\n"
        "	mov x29, #0\n"
        "	mov x30, #0\n"
        "	mov x0, sp\n"
        "	and sp, x0, #-16\n"
        "	b _start_c\n");
