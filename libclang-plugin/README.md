# libclang-plugin

## detect-no-zero-initialization

*Warning: Work in progress.*

This plugin attempts to report this form of undefined behavior in C++, where zero initialization ist not used, which might leave some fields uninitialized, and containing garbage data:

```cpp
```

tl;dr: Never use the syntax `T object;` because it will leave some fields uninitialized and they will contain garbage, leading to hard to track bugs. The fix is to always use `T object{}`; which will zero initialize the object.

This plugin using libclang attempts to detect this problematic syntax. 

Note that in some cases, but not all, the problematic syntax is actually fine (see [default initialization](https://en.cppreference.com/w/cpp/language/default_initialization)). But the rules are so complex that it's better no to live on the edge; just zero initialize.

`clang-tidy` reports this issue when trying to pass such a variable as argument to a function, but that's all. We want to detect all problematic locations, even when the variable is not passed to a function.

`clang` does not report any issue even when called with `-Weverything`.

### Usage

```sh
# Requires libclang which comes with LLVM/Clang
$ make
$ ./detect-no-zero-initialization test.cpp
test.cpp:8:3: Person p
```

Note: We analyze the code in C++ 11 mode. In a later version this might be configurable.
