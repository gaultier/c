#include <stdio.h>
#include <stdlib.h>

#include "macho.c"

char* exe_name = NULL;

int __attribute__((noinline)) baz(int n) {
    stacktrace_print(exe_name);
    return n;
}

int __attribute__((noinline)) bar(int n) {
    int a = 1;
    (void)a;
    int b = 10;
    (void)b;
    baz(n);
    return n;
}

int __attribute__((noinline)) foo(int n) {
    // foo
    bar(n);
    return n;
}

int main(int argc, char* argv[]) {
    assert(argc == 2);
    exe_name = argv[0];
    const int n = atoi(argv[1]);
    switch (n) {
        case 1:
            foo(bar(baz(n)));
            break;
        case 2:
            foo(baz(bar(n)));
            break;
        case 3:
            bar(foo(baz(n)));
            break;
        case 4:
            bar(baz(foo(n)));
            break;
        case 5:
            baz(foo(bar(n)));
            break;
        case 6:
            baz(bar(foo(n)));
            break;
        case 7:
            foo(n);
            break;
        case 8:
            bar(n);
            break;
        case 9:
            baz(n);
            break;
    }
}
