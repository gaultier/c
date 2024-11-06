#if defined(__x86_64__) && defined(__linux__)
#include "os_linux_x86_64.c"
#elif defined(__x86_64__) && defined(__FreeBSD__)
#include "os_freebsd_x86_64.c"
#else
#error "os/arch not implemented"
#endif
