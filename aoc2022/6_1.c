#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  assert(argc == 2);
  const char *s = argv[1];

  while (*s != 0) {
    const char a = s[0];
    const char b = s[1];
    const char c = s[2];
    const char d = s[3];

    const uint64_t mask_a = 1 << (a - 'a');
    const uint64_t mask_b = 1 << (b - 'a');
    const uint64_t mask_c = 1 << (c - 'a');
    const uint64_t mask_d = 1 << (d - 'a');

    const uint64_t combined = mask_a ^ mask_b ^ mask_c ^ mask_d;
    if (__builtin_popcount(combined) != 4) {
      s += 4;
      continue;
    }

    printf("%ld %c %c %c %c", s - argv[1], a, b, c, d);
    break;
  }
}
