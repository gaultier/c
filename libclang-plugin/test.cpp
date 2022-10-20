#include <cstdio>

struct Person {
  int age;
};

int main() {
  Person p;
  printf("%d\n", p.age);  // Undefined behavior!

  return 0;
}
