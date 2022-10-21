#include <cassert>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

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

struct Bus {
  int age;
  virtual void f() {}
};

struct Names {
  int age;
  std::vector<std::string> names;
};

int main() {
  Person p;
  assert(std::is_pod<Person>());
  printf("UB: %d\n", p.age);  // Undefined behavior! Will print garbage.

  Animal a;
  assert(!std::is_pod<Animal>());
  printf("OK: %d\n", a.age);  // Totally fine!

  Car c;
  assert(!std::is_pod<Car>());
  printf("OK: %d\n", c.age);  // Totally fine!

  Person* pp = new Person;
  printf("OK: %d\n", pp->age);  // Totally fine!
  delete pp;

  Bus b;
  printf("OK: %d\n", b.age);  // Totally fine!

  Names n;
  printf("UB: %d\n", n.age);  // Undefined behavior! Will print garbage.

  return 0;
}
