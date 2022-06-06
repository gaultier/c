#include <stdio.h>
#include <stdlib.h>

/* #pragma once */
/* #include "macho.c" */

#define READ_RBP(rbp) __asm__ volatile("movq %%rbp, %0" : "=r"(rbp))

#define print_backtrace()                     \
    do {                                      \
        uintptr_t* rbp = 0;                   \
        READ_RBP(rbp);                        \
        while (1) {                           \
            if (rbp == 0 || *rbp == 0) break; \
            uintptr_t* prev_rip = (rbp + 1);  \
            printf("%#lx\n", *prev_rip);      \
            rbp = (uintptr_t*)*rbp;           \
        }                                     \
    } while (0)

int baz(int n) {
    print_backtrace();
    return n;
}

int bar(int n) {
    int a = 1;
    int b = 10;
    baz(n);
    return n;
}

int foo(int n) {
    // foo
    bar(n);
    return n;
}

int main(int argc, char* argv[]) {
    const int n = atoi(argv[1]);
    switch (n) {
        case 0:
            foo(bar(baz(n)));
            break;
        case 1:
            foo(baz(bar(n)));
            break;
        case 2:
            bar(foo(baz(n)));
            break;
        case 3:
            bar(baz(foo(n)));
            break;
        case 4:
            baz(foo(bar(n)));
            break;
        case 5:
            baz(bar(foo(n)));
            break;
    }
}
