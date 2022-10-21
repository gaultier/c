# libclang-plugin

## detect-no-zero-initialization

*Warning: Work in progress.*

This plugin attempts to report this form of undefined behavior in C++, where zero initialization ist not used, which might leave some fields uninitialized, and containing garbage data:

**tl;dr:** Never use the syntax `T object;` because it will leave some fields uninitialized and they will contain garbage, leading to hard to track bugs. The fix is to always use `T object{}`; which will zero initialize the object.

In some cases, but not all, the problematic syntax is actually fine (see [default initialization](https://en.cppreference.com/w/cpp/language/default_initialization)). But the rules are so complex that it's better not to live on the edge; just zero initialize.

This plugin using libclang attempts to detect this problematic syntax. 

### Usage

Given this code `simple.cpp`:

```cpp
#include <cstdio>
#include <string>
#include <vector>

struct Foo {
  int n;
  std::vector<std::string> foo;
};

int main() {
  Foo foo;
  printf("%d", foo.n); // Undefined behavior. Will print garbage, e.g.: `-1140067760`

  Foo foo_ok{};
  printf("%d", foo_ok.n); // Completely fine, will print 0.
}
```

This plugin will report all problematic locations:

```sh
$ ./detect-no-zero-initialization simple.cpp
./simple.cpp:11:3: Foo foo
```

To analyze a whole project and ignore system headers:

```sh
$ ls  /path/to/project/**/*.cpp | xargs -I {} ./detect-ub-pod-cpp {} | grep -v '/path/to/system/headers'
```


**Lengthy explanation:**

Default initialization occurs under certain circumstances when using the syntax `T object;`. A few problems appear:
- If `T` is a non class, non array type such as `int`, no initialization is performed at all. This is obvious undefined behavior.
- If `T` is a POD, no initialization is performed at all. This is akin to doing `int a;` and then using `a`. This is obvious undefined behavior.
- If `T` is not a POD, the default constructor is called, and is responsible for initializing all fields. It is easy to miss one, leading to undefined behavior.

Not even mentioning that the rules around what is a POD change with nearly every C++ standard version, it is apparent it is simpler to let the compiler zero initialize all the fields for us. 


### Why this plugin?

`clang-tidy` reports this issue when trying to pass such a variable as argument to a function, but that's all. We want to detect all problematic locations, even when the variable is not passed to a function.

`clang` does not report any issue even when called with `-Weverything`.

### Building

```sh
# Requires libclang which comes with LLVM/Clang
# Look for LLVM in /usr/local/opt/llvm/ by default, override with CFLAGS
$ make
```

Note: We analyze the code in C++ 11 mode. In a later version this might be configurable.
