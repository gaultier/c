#include "pg.h"

#include "vendor/greatest/greatest.h"

TEST test_pg_array_append() {
  pg_array_t(int) array;
  pg_array_init_reserve(array, 10, pg_heap_allocator());

  pg_array_append(array, 1);
  pg_array_append(array, 2);

  ASSERT_EQ(pg_array_count(array), 2);
  ASSERT_EQ(array[0], 1);
  ASSERT_EQ(array[1], 2);

  pg_array_free(array);
  ASSERT_EQ(array, NULL);

  PASS();
}

TEST test_pg_array_capacity() {
  pg_array_t(uint64_t) array;
  pg_array_init_reserve(array, 3, pg_heap_allocator());

  ASSERT_EQ(pg_array_capacity(array), 3);

  pg_array_free(array);
  ASSERT_EQ(array, NULL);

  PASS();
}

SUITE(pg_array) {
  RUN_TEST(test_pg_array_append);
  RUN_TEST(test_pg_array_capacity);
}

TEST test_pg_span_split() {
  {
    pg_string_span_t span = {.data = "foo\nhello", .len = strlen("foo\nhello")};
    pg_string_span_t left = {0}, right = {0};

    ASSERT_EQ(true, pg_span_split(span, '\n', &left, &right));
    ASSERT_EQ_FMT(3ULL, left.len, "%llu");
    ASSERT_STRN_EQ("foo", left.data, left.len);
    ASSERT_EQ_FMT(5ULL, right.len, "%llu");
    ASSERT_STRN_EQ("hello", right.data, right.len);
  }
  {
    pg_string_span_t span = {.data = "", .len = strlen("")};
    pg_string_span_t left = {0}, right = {0};

    ASSERT_EQ(false, pg_span_split(span, '\n', &left, &right));
    ASSERT_EQ_FMT(span.len, left.len, "%llu");
    ASSERT_EQ_FMT(span.data, left.data, "%p");
    ASSERT_EQ_FMT(0ULL, right.len, "%llu");
    ASSERT_EQ_FMT(NULL, right.data, "%p");
  }
  {
    pg_string_span_t span = {.data = "z", .len = strlen("z")};
    pg_string_span_t left = {0}, right = {0};

    ASSERT_EQ(false, pg_span_split(span, '\n', &left, &right));
    ASSERT_EQ_FMT(1ULL, left.len, "%llu");
    ASSERT_STRN_EQ("z", left.data, left.len);
    ASSERT_EQ_FMT(0ULL, right.len, "%llu");
    ASSERT_EQ_FMT(NULL, right.data, "%p");
  }
  {
    pg_string_span_t span = {.data = "z\n", .len = strlen("z\n")};
    pg_string_span_t left = {0}, right = {0};

    ASSERT_EQ(true, pg_span_split(span, '\n', &left, &right));
    ASSERT_EQ_FMT(1ULL, left.len, "%llu");
    ASSERT_STRN_EQ("z", left.data, left.len);
    ASSERT_EQ_FMT(0ULL, right.len, "%llu");
    ASSERT_EQ_FMT(NULL, right.data, "%p");
  }

  PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  /* Individual tests can be run directly in main, outside of suites. */
  /* RUN_TEST(x_should_equal_1); */

  /* Tests can also be gathered into test suites. */
  RUN_SUITE(pg_array);
  RUN_TEST(test_pg_span_split);

  GREATEST_MAIN_END(); /* display results */
}
