# Experimental crash reporter (for macOS x64)

Also known as symbolizer, stacktracer reporter, etc. Shows at any point in the program the full stacktrace including source code lines and symbols.

Only parses Mach-O files for now which are macOS specific.

It then extracts the DWARF debug information from it, parses that, and from there can map addresses to code locations.

The stack unwinding is only for now done for x86-64 (but I suppose could be easily done for ARM).

## Example

In the code:

```c
#include "macho.h"

void baz(int n) {
  stacktrace_print(); // <-- There's only one function in the API
  return n;
}
```

```sh
$ ./crash-reporter 4
0x1043d611c /Users/pgaultier/code/c/c-crash-reporter/main.c:baz:20:             stacktrace_print();
0x1043d6091 /Users/pgaultier/code/c/c-crash-reporter/main.c:bar:29:             baz(n);
0x1043d6063 /Users/pgaultier/code/c/c-crash-reporter/main.c:foo:35:             bar(n);
0x1043d5fb4 /Users/pgaultier/code/c/c-crash-reporter/main.c:main:56:            bar(baz(foo(n)));
0x1043d611c /Users/pgaultier/code/c/c-crash-reporter/main.c:baz:20:             stacktrace_print();
0x1043d5fbb /Users/pgaultier/code/c/c-crash-reporter/main.c:main:56:            bar(baz(foo(n)));
0x1043d611c /Users/pgaultier/code/c/c-crash-reporter/main.c:baz:20:             stacktrace_print();
0x1043d6091 /Users/pgaultier/code/c/c-crash-reporter/main.c:bar:29:             baz(n);
0x1043d5fc2 /Users/pgaultier/code/c/c-crash-reporter/main.c:main:56:            bar(baz(foo(n)));
```

## Build

```sh
$ make
```

## Limits

- Stack unwinding is a difficult topic in case of memory corruption. This code has not been tested in those conditions yet.
- Frame pointers are required (`-fno-omit-frame-pointer`)
- If your environment does manipulate the stack in weird ways, e.g. C++ exceptions, longjmp, etc, this most likely won't work
