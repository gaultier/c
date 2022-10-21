#include <cassert>
#include <cstdio>
#include <vector>

// POD
struct Person {
  int age;
};

int main() {
  Person p_default_initialized;
  printf("UB: %d\n", p_default_initialized.age);

  Person* p_new = new Person;
  printf("OK: %d\n", p_new->age);
  delete p_new;

  Person p_zero_initialized{};
  printf("OK: %d\n", p_zero_initialized.age);

  Person p_zero_initialized_a_la_c{0};
  printf("OK: %d\n", p_zero_initialized_a_la_c.age);

  Person p_assigned = Person();
  printf("OK: %d\n", p_assigned.age);

  return 0;
}
