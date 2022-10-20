# libclang-plugin

## detect-ub-pod-cpp.c

This plugin reports this form of undefined behavior in C++:

```cpp
#include <cstdio>

struct Person { int age; };

int main() {
  
  Person p;
  printf("%d\n", p.age); // Undefined behavior!

  return 0;
}
```

According to https://en.cppreference.com/w/cpp/language/default_initialization, in C++, using a local, non-static variable, whose type is a POD class (or struct), where the variable has been defined as such: `T object;` leads to undefined behavior, that is, the content of the variable is garbage and cannot be relied upon (technically it's using this variable which is undefined behavior, not merely defining it).

That's because no constructor has been called explicitly, so the default constructor is called, which then does nothing, so the members of the class are never initialized. 

That situation is problematic because it is not obvious unless we inspect the layout of the class to determine whether it is a POD type or not (and those rules are complex, and change given the C++ standard). And the symptoms might show up way later in the execution of the program.

This plugin using libclang attempts to detect this issue.

`clang-tidy` reports this issue when trying to pass such a variable as argument to a function, but that's all. We want to detect all problematic locations, even when the variable is not passed to a function.

`clang` does not report any issue even when called with `-Weverything`.

### Usage

```sh
# Requires libclang which comes with LLVM/Clang
$ make
$ ./detect-ub-pod-cpp test.cpp
test.cpp:8:3: Person p
```

Note: We use the C++11 (and above) definition of POD which is different from the C++98 one.

### BUGS

We use the function `clang_isPODType()` from libclang to determine whether the type is POD, but it seems there are false positives, when compared with `std::is_pod<T>()`.
