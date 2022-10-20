# libclang-plugin

## detect-ub-pod-cpp

*Warning: Work in progress.*

This plugin attempts to report this form of undefined behavior in C++, where default initialization does *not* occur:

```cpp
#include <cassert>
#include <cstdio>
#include <type_traits>

// POD
struct Person {
  int age;
};

// Not a POD, and the default constructor does *not* initialize the member
struct Animal {
  int age;
  Animal() {}
};

// Not a POD, and the default constructor initializes the member
struct Car {
  int age;
  Car() : age(1) {}
};

int main() {
  Person p;
  assert(std::is_pod<Person>());
  printf("%d\n", p.age);  // Undefined behavior! Will print garbage.

  Animal a;
  assert(!std::is_pod<Animal>());
  printf("%d\n", a.age);  // Undefined behavior! Will print garbage.

  Car c;
  assert(!std::is_pod<Car>());
  printf("%d\n", c.age);  // Totally fine!

  Person* pp = new Person;
  printf("%d\n", pp->age);  // Totally fine!
  delete pp;

  return 0;
}
```

The [rules](https://en.cppreference.com/w/cpp/language/default_initialization) around default initialization in C++ are complex and confusing and it's easy to write code which is not covered by them:

> Default initialization is performed in three situations:
>  1) when a variable with automatic, static, or thread-local storage duration is declared with no initializer;
>  2) when an object with dynamic storage duration is created by a new-expression with no initializer;
>  3) when a base class or a non-static data member is not mentioned in a constructor initializer list and that constructor is called.
>
>  The effects of default initialization are: 
>  [...]  no initialization is performed: the objects with automatic storage duration (and their subobjects) contain indeterminate values. 

That situation is problematic because it is not obvious unless we inspect the layout of the class to determine whether it is a POD type or not. And the symptoms might show up way later in the execution of the program.

Ideally, an object would always be initialized explicitly, but in reality it does not always happen.

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

Note: We use the C++11 (and above) definition of POD which is different from the C++98 one. In a later version this might be configurable.

### BUGS

We use the function `clang_isPODType()` from libclang to determine whether the type is POD, but it seems there are false positives, when compared with `std::is_pod<T>()`.
