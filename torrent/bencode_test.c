#include "bencode.h"

#include <_types/_uint8_t.h>

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
    pg_string_span_t span = {.data = "0", .len = 1};
    int64_t res = 0;
    bc_parse_error_t err = bc_parse_i64(&span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0LL, res, "%lld");
  }
  {
    pg_string_span_t span = {.data = "130foo", .len = 4};
    int64_t res = 0;
    bc_parse_error_t err = bc_parse_i64(&span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(130LL, res, "%lld");
    ASSERT_EQ_FMT(1ULL, span.len, "%llu");
    ASSERT_STR_EQ("foo", span.data);
  }
  {
    pg_string_span_t span = {.data = "-", .len = 1};
    int64_t res = 0;
    bc_parse_error_t err = bc_parse_i64(&span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0LL, res, "%lld");
  }
  {
    pg_string_span_t span = {.data = "", .len = 0};
    int64_t res = 0;
    bc_parse_error_t err = bc_parse_i64(&span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0LL, res, "%lld");
  }

  PASS();
}
TEST test_bc_parse_string() {
  const pg_string_t zero = {0};
  {
    pg_string_span_t span = {.data = "", .len = 0};
    pg_string_t res = {0};
    bc_parse_error_t err = bc_parse_string(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(pg_string_t));
  }
  {
    pg_string_span_t span = {.data = "x", .len = 1};
    pg_string_t res = {0};
    bc_parse_error_t err = bc_parse_string(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(pg_string_t));
  }
  {
    pg_string_span_t span = {.data = "2", .len = 1};
    pg_string_t res = {0};
    bc_parse_error_t err = bc_parse_string(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(pg_string_t));
  }
  {
    pg_string_span_t span = {.data = "2.", .len = 2};
    pg_string_t res = {0};
    bc_parse_error_t err = bc_parse_string(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(2ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(pg_string_t));
  }
  {
    pg_string_span_t span = {.data = "2:", .len = 2};
    pg_string_t res = {0};
    bc_parse_error_t err = bc_parse_string(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_STRING_LENGTH, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(2ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(pg_string_t));
  }
  {
    pg_string_span_t span = {.data = "-2:", .len = 3};
    pg_string_t res = {0};
    bc_parse_error_t err = bc_parse_string(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_STRING_LENGTH, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(3ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(pg_string_t));
  }
  {
    pg_string_span_t span = {.data = "2:ab", .len = 4};
    pg_string_t res = {0};
    bc_parse_error_t err = bc_parse_string(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_STR_EQ("ab", res);
  }

  PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  RUN_TEST(test_bc_parse_i64);
  RUN_TEST(test_bc_parse_string);

  GREATEST_MAIN_END(); /* display results */
}
