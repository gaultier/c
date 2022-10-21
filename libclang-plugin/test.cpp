#include <cassert>
#include <cstdio>
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
  std::vector<int> ages;
};

int main() {
  Person p;
  printf("UB: %d\n", p.age);

  Animal a;
  printf("UB: %d\n", a.age);

  Car c;
  printf("OK: %d\n", c.age);

  Person* pp = new Person;
  printf("OK: %d\n", pp->age);
  delete pp;

  Bus b;
  printf("UB: %d\n", b.age);

  Names n;
  printf("UB: %d\n", n.age);

  Names n_zero_initialized{};
  printf("OK: %d\n", n_ok.age);

  return 0;
}
