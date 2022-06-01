#include <mach-o/loader.h>
#include <stdio.h>

#define READ_RBP(rbp) __asm__ volatile("movq %%rbp, %0" : "=r"(rbp))

const int CALL_INSTR_SIZE = 5;

#define print_backtrace()                                  \
    do {                                                   \
        uintptr_t* rbp = 0;                                \
        READ_RBP(rbp);                                     \
        while (1) {                                        \
            if (rbp == 0 || *rbp == 0) break;              \
            uintptr_t* prev_rip = (rbp + 1);               \
            printf("%#lx\n", *prev_rip - CALL_INSTR_SIZE); \
            rbp = (uintptr_t*)*rbp;                        \
        }                                                  \
    } while (0)

void baz() { print_backtrace(); }

void bar() {
    int n = 1;
    int a = 10;
    baz();
}

void foo() {
    // foo
    bar();
}

int main() { foo(); }
