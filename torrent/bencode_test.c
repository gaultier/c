#include "bencode.h"

#include "vendor/greatest/greatest.h"

TEST test_bencode_parse() {
  bc_value_t val = {0};
  PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  RUN_TEST(test_bencode_parse);

  GREATEST_MAIN_END(); /* display results */
}
