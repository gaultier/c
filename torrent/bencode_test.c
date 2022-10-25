#include "bencode.h"

#include "vendor/greatest/greatest.h"

TEST test_bc_parse_i64() {
  {
    pg_string_span_t span = {.data = "-23", .len = 3};
    int64_t res = 0;
    bc_parse_error_t err = bc_parse_i64(&span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(-23LL, res, "%lld");
  }
  {
    pg_string_span_t span = {.data = "0", .len = 3};
    int64_t res = 0;
    bc_parse_error_t err = bc_parse_i64(&span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0LL, res, "%lld");
  }
  {
    pg_string_span_t span = {.data = "130", .len = 3};
    int64_t res = 0;
    bc_parse_error_t err = bc_parse_i64(&span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(130LL, res, "%lld");
  }

  PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  RUN_TEST(test_bc_parse_i64);

  GREATEST_MAIN_END(); /* display results */
}
