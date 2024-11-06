#if defined(__x86_64__)

#include "os_x86_64.c"

#if defined(__linux__)
#include "os_linux_x86_64.c"
#elif defined(__FreeBSD__)
#include "os_freebsd_x86_64.c"
#else
#error "os/arch not implemented"
#endif

#elif defined(__aarch64__)

#include "os_aarch64.c"

#if defined(__linux__)
#include "os_linux_aarch64.c"
#else
#error "os/arch not implemented"
#endif

#elif defined(__ARM_ARCH_5_) || defined(__ARM_ARCH_5E_) ||                     \
    defined(__ARM_ARCH_6_) || defined(__ARM_ARCH_6T2_) ||                      \
    defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) ||                     \
    defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) ||                    \
    defined(__ARM_ARCH_6KZ__) || defined(__ARM_ARCH_7_)
#include "os_arm.c"

#if defined(__linux__)
#include "os_linux_arm.c"
#else
#error "os/arch not implemented"
#endif

#else
#error "os/arch not implemented"
#endif

int main(int argc, char *argv[]);

__attribute((__visibility__("hidden"))) [[noreturn]] void _start_c(long *p) {
  int argc = (int)p[0];
  char **argv = (void *)(p + 1);
  exit(main(argc, argv));
}
