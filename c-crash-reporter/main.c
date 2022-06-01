#include <libunwind.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void show_backtrace(void) {
    unw_cursor_t cursor;
    unw_context_t uc;
    unw_word_t ip, sp;

    unw_getcontext(&uc);
    unw_init_local(&cursor, &uc);
    while (unw_step(&cursor) > 0) {
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        char fn_name[256] = {};
        unw_word_t offp = 0;
        unw_get_proc_name(&cursor, fn_name, sizeof(fn_name), &offp);
        printf("name=%s ip = %lx, sp = %lx\n", fn_name, (long)ip, (long)sp);
    }
}

void baz() {
    int* p = 0;
    *p += 1;
}

void bar() { baz(); }

void foo() {
    printf("[foo] %p\n", *(uint64_t*)__builtin_frame_address(1));
    bar();
}

void on_sigsegv(int sig) {
    printf("sig=%d\n", sig);

    show_backtrace();
    abort();
}

int main() {
    /* int res = fork(); */
    /* if (res < 0) { */
    /*     fprintf(stderr, "fork Failed"); */
    /*     return 1; */
    /* } */
    /* if (res == 0) {  // Child */
    signal(SIGSEGV, on_sigsegv);
    printf("MAIN=%p\n", main);
    foo();
    /* } else {  // Parent */
    /*     int stat_loc = 0; */
    /*     wait(&stat_loc);  // Wait for child's message */
    /*     printf("Child says: stat_loc=%d WTERMSIG=%d\n", stat_loc, */
    /*            WTERMSIG(stat_loc)); */
    /* } */
}
