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
