#include "pg.h"

#include <_types/_uint64_t.h>

#include "vendor/greatest/greatest.h"

TEST test_pg_array_append(void) {
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
  pg_array_init(array, pg_heap_allocator());

  ASSERT_EQ(pg_array_capacity(array), 0);

  pg_array_set_capacity(array, 3);
  ASSERT_EQ(pg_array_capacity(array), 3);

  pg_array_free(array);
  ASSERT_EQ(array, NULL);

  PASS();
}

SUITE(pg_array) {
  RUN_TEST(test_pg_array_append);
  RUN_TEST(test_pg_array_capacity);
}

TEST test_pg_hashtable() {
  PG_HASHTABLE(uint64_t, my_hash);
  my_hash h = {0};
  pg_hashtable_init(h, 5, pg_heap_allocator());

  bool found = false;
  uint64_t index = -1;
  pg_string_t key = pg_string_make_length(pg_heap_allocator(), "does not exist",
                                          strlen("does not exist"));
  pg_hashtable_find(h, key, found, index);
  ASSERT_EQ(found, false);

  PASS();
}

SUITE(pg_hashtable) { RUN_TEST(test_pg_hashtable); }

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  /* Individual tests can be run directly in main, outside of suites. */
  /* RUN_TEST(x_should_equal_1); */

  /* Tests can also be gathered into test suites. */
  RUN_SUITE(pg_array);
  RUN_SUITE(pg_hashtable);

  GREATEST_MAIN_END(); /* display results */
}
