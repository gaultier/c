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
  PG_HASHTABLE(pg_string_t, uint64_t, my_hash);
  my_hash_t dict = {0};
  pg_hashtable_init(dict, 5, pg_heap_allocator());

  bool found = false;
  uint64_t index = -1;
  pg_string_t key = pg_string_make_length(pg_heap_allocator(), "does not exist",
                                          strlen("does not exist"));
  pg_hashtable_find(dict, key, found, index);
  ASSERT_EQ(found, false);

  pg_hashtable_upsert(dict, key, 42);
  pg_hashtable_find(dict, key, found, index);
  ASSERT_EQ(found, true);
  ASSERT_EQ(1ULL, pg_hashtable_count(dict));

  pg_string_t key2 = pg_string_make_length(pg_heap_allocator(), "some key",
                                           strlen("some key"));
  pg_hashtable_upsert(dict, key2, 42);
  pg_hashtable_find(dict, key2, found, index);
  ASSERT_EQ(found, true);
  ASSERT_EQ(2ULL, pg_hashtable_count(dict));

  pg_hashtable_find(dict, key, found, index);
  ASSERT_EQ(found, true);

  my_hash_iter_t it = {0};
  pg_hashtable_init_iter(dict, it);
  uint64_t count = 0;
  while (it.has_next) {
    count++;
    pg_hashtable_next(dict, it);
  }
  ASSERT_EQ_FMT(2ULL, count, "%llu");

  pg_hashtable_destroy(dict, pg_string_free_ptr, pg_hashtable_destroy_kv_noop);
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
