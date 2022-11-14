#include "pg.h"

#include "vendor/greatest/greatest.h"

static void foo(pg_array_t(int) * arr) { pg_array_append(*arr, 1); }

TEST test_pg_array_append() {
  {
    pg_array_t(int) array;
    pg_array_init_reserve(array, 10, pg_heap_allocator());

    pg_array_append(array, 1);
    pg_array_append(array, 2);

    ASSERT_EQ(pg_array_len(array), 2);
    ASSERT_EQ(array[0], 1);
    ASSERT_EQ(array[1], 2);

    pg_array_free(array);
    ASSERT_EQ(array, NULL);
  }
  {
    pg_array_t(int) array;
    pg_array_init(array, pg_heap_allocator());
    foo(&array);
    ASSERT_EQ(1, array[0]);
  }
  {
    pg_array_t(int) array;
    pg_array_init_reserve(array, 10, pg_heap_allocator());
    for (uint64_t i = 0; i < 16; i++) foo(&array);
    ASSERT_EQ(1, array[15]);
  }

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

TEST test_pg_span_split_left() {
  {
    pg_span_t span = {.data = "foo\nhello", .len = strlen("foo\nhello")};
    pg_span_t left = {0}, right = {0};

    ASSERT_EQ(true, pg_span_split_left(span, '\n', &left, &right));
    ASSERT_EQ_FMT(3ULL, left.len, "%llu");
    ASSERT_STRN_EQ("foo", left.data, left.len);
    ASSERT_EQ_FMT(6ULL, right.len, "%llu");
    ASSERT_STRN_EQ("\nhello", right.data, right.len);
  }
  {
    pg_span_t span = {.data = "", .len = strlen("")};
    pg_span_t left = {0}, right = {0};

    ASSERT_EQ(false, pg_span_split_left(span, '\n', &left, &right));
    ASSERT_EQ_FMT(span.len, left.len, "%llu");
    ASSERT_EQ_FMT(span.data, left.data, "%p");
    ASSERT_EQ_FMT(0ULL, right.len, "%llu");
    ASSERT_EQ_FMT(NULL, right.data, "%p");
  }
  {
    pg_span_t span = {.data = "z", .len = strlen("z")};
    pg_span_t left = {0}, right = {0};

    ASSERT_EQ(false, pg_span_split_left(span, '\n', &left, &right));
    ASSERT_EQ_FMT(1ULL, left.len, "%llu");
    ASSERT_STRN_EQ("z", left.data, left.len);
    ASSERT_EQ_FMT(0ULL, right.len, "%llu");
    ASSERT_EQ_FMT(NULL, right.data, "%p");
  }
  {
    pg_span_t span = {.data = "z\n", .len = strlen("z\n")};
    pg_span_t left = {0}, right = {0};

    ASSERT_EQ(true, pg_span_split_left(span, '\n', &left, &right));
    ASSERT_EQ_FMT(1ULL, left.len, "%llu");
    ASSERT_STRN_EQ("z", left.data, left.len);
    ASSERT_EQ_FMT(0ULL, right.len, "%llu");
    ASSERT_EQ_FMT(NULL, right.data, "%p");
  }

  PASS();
}

TEST test_pg_span_split_right() {
  {
    pg_span_t span = pg_span_make_c("foo\nhe\nllo");
    pg_span_t left = {0}, right = {0};

    ASSERT_EQ(true, pg_span_split_right(span, '\n', &left, &right));
    ASSERT_EQ_FMT(3ULL, left.len, "%llu");
    ASSERT_STRN_EQ("foo", left.data, left.len);
    ASSERT_EQ_FMT(6ULL, right.len, "%llu");
    ASSERT_STRN_EQ("\nhello", right.data, right.len);
  }
  {
    pg_span_t span = pg_span_make_c("");
    pg_span_t left = {0}, right = {0};

    ASSERT_EQ(false, pg_span_split_right(span, '\n', &left, &right));
    ASSERT_EQ_FMT(span.len, left.len, "%llu");
    ASSERT_EQ_FMT(span.data, left.data, "%p");
    ASSERT_EQ_FMT(0ULL, right.len, "%llu");
    ASSERT_EQ_FMT(NULL, right.data, "%p");
  }
  {
    pg_span_t span = pg_span_make_c("z");
    pg_span_t left = {0}, right = {0};

    ASSERT_EQ(false, pg_span_split_right(span, '\n', &left, &right));
    ASSERT_EQ_FMT(1ULL, left.len, "%llu");
    ASSERT_STRN_EQ("z", left.data, left.len);
    ASSERT_EQ_FMT(0ULL, right.len, "%llu");
    ASSERT_EQ_FMT(NULL, right.data, "%p");
  }
  {
    pg_span_t span = pg_span_make_c("z\n");
    pg_span_t left = {0}, right = {0};

    ASSERT_EQ(true, pg_span_split_right(span, '\n', &left, &right));
    ASSERT_EQ_FMT(1ULL, left.len, "%llu");
    ASSERT_STRN_EQ("z", left.data, left.len);
    ASSERT_EQ_FMT(0ULL, right.len, "%llu");
    ASSERT_EQ_FMT(NULL, right.data, "%p");
  }

  PASS();
}

TEST test_pg_string_url_encode() {
  pg_string_t src = pg_string_make(pg_heap_allocator(), "foo?_. ");

  pg_string_t res = pg_string_url_encode(pg_heap_allocator(), src);
  ASSERT_EQ_FMT(21ULL, pg_string_length(res), "%llu");
  ASSERT_STRN_EQ("%66%6F%6F%3F%5F%2E%20", res, pg_string_length(res));

  PASS();
}

TEST test_pg_ring() {
  pg_ring_t ring = {0};
  pg_ring_init(pg_heap_allocator(), &ring, 100);

  ASSERT_EQ_FMT(0ULL, pg_ring_len(&ring), "%llu");
  ASSERT_GT(ring.cap, 0);

  pg_ring_push_back(&ring, 1);
  ASSERT_EQ_FMT(1ULL, pg_ring_len(&ring), "%llu");
  ASSERT_GT(ring.cap, 0);
  ASSERT_EQ_FMT(1, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(1, pg_ring_back(&ring), "%d");

  pg_ring_push_back(&ring, 2);
  ASSERT_EQ_FMT(2ULL, pg_ring_len(&ring), "%llu");
  ASSERT_GT(ring.cap, 0);
  ASSERT_EQ_FMT(1, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(2, pg_ring_back(&ring), "%d");

  pg_ring_push_back(&ring, 3);
  ASSERT_EQ_FMT(3ULL, pg_ring_len(&ring), "%llu");
  ASSERT_GT(ring.cap, 0);
  ASSERT_EQ_FMT(1, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(3, pg_ring_back(&ring), "%d");

  pg_ring_push_back(&ring, 4);
  ASSERT_EQ_FMT(4ULL, pg_ring_len(&ring), "%llu");
  ASSERT_GT(ring.cap, 0);
  ASSERT_EQ_FMT(1, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(4, pg_ring_back(&ring), "%d");

  pg_ring_push_back(&ring, 5);
  ASSERT_EQ_FMT(5ULL, pg_ring_len(&ring), "%llu");
  ASSERT_GT(ring.cap, 0);
  ASSERT_EQ_FMT(1, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(5, pg_ring_back(&ring), "%d");

  pg_ring_push_back(&ring, 6);
  ASSERT_EQ_FMT(6ULL, pg_ring_len(&ring), "%llu");
  ASSERT_GT(ring.cap, 0);
  ASSERT_EQ_FMT(1, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(6, pg_ring_back(&ring), "%d");

  ASSERT_EQ_FMT(1, pg_ring_get(&ring, 0), "%d");
  ASSERT_EQ_FMT(2, pg_ring_get(&ring, 1), "%d");
  ASSERT_EQ_FMT(3, pg_ring_get(&ring, 2), "%d");
  ASSERT_EQ_FMT(4, pg_ring_get(&ring, 3), "%d");
  ASSERT_EQ_FMT(5, pg_ring_get(&ring, 4), "%d");
  ASSERT_EQ_FMT(6, pg_ring_get(&ring, 5), "%d");

  ASSERT_EQ_FMT(1, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(6, pg_ring_back(&ring), "%d");

  pg_ring_push_front(&ring, 90);
  ASSERT_EQ_FMT(7ULL, pg_ring_len(&ring), "%llu");
  ASSERT_GT(ring.cap, 0);
  ASSERT_EQ_FMT(90, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(6, pg_ring_back(&ring), "%d");

  pg_ring_push_front(&ring, 91);
  ASSERT_EQ_FMT(8ULL, pg_ring_len(&ring), "%llu");
  ASSERT_GT(ring.cap, 0);
  ASSERT_EQ_FMT(91, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(6, pg_ring_back(&ring), "%d");

  ASSERT_EQ_FMT(91, pg_ring_get(&ring, 0), "%d");
  ASSERT_EQ_FMT(90, pg_ring_get(&ring, 1), "%d");
  ASSERT_EQ_FMT(1, pg_ring_get(&ring, 2), "%d");

  ASSERT_EQ_FMT(8ULL, pg_ring_len(&ring), "%llu");
  ASSERT_EQ_FMT(6, pg_ring_get(&ring, 7), "%d");

  ASSERT_EQ_FMT(91, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(6, pg_ring_back(&ring), "%d");

  ASSERT_EQ_FMT(6, pg_ring_pop_back(&ring), "%d");
  ASSERT_EQ_FMT(5, pg_ring_back(&ring), "%d");
  ASSERT_EQ_FMT(91, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(7ULL, pg_ring_len(&ring), "%llu");

  ASSERT_EQ_FMT(5, pg_ring_pop_back(&ring), "%d");
  ASSERT_EQ_FMT(91, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(4, pg_ring_back(&ring), "%d");
  ASSERT_EQ_FMT(6ULL, pg_ring_len(&ring), "%llu");

  ASSERT_EQ_FMT(91, pg_ring_pop_front(&ring), "%d");
  ASSERT_EQ_FMT(90, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(4, pg_ring_back(&ring), "%d");
  ASSERT_EQ_FMT(5ULL, pg_ring_len(&ring), "%llu");

  ASSERT_EQ_FMT(90, pg_ring_pop_front(&ring), "%d");
  ASSERT_EQ_FMT(1, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(4, pg_ring_back(&ring), "%d");
  ASSERT_EQ_FMT(4ULL, pg_ring_len(&ring), "%llu");

  pg_ring_consume_back(&ring, 2);
  ASSERT_EQ_FMT(2ULL, pg_ring_len(&ring), "%llu");
  ASSERT_EQ_FMT(1, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(2, pg_ring_back(&ring), "%d");

  pg_ring_consume_front(&ring, 1);
  ASSERT_EQ_FMT(1ULL, pg_ring_len(&ring), "%llu");
  ASSERT_EQ_FMT(2, pg_ring_front(&ring), "%d");
  ASSERT_EQ_FMT(2, pg_ring_back(&ring), "%d");

  pg_ring_clear(&ring);
  ASSERT_EQ_FMT(0ULL, pg_ring_len(&ring), "%llu");

  const char buf[] = "hello world!";
  pg_ring_push_backv(&ring, (uint8_t *)buf, sizeof(buf) - 1);
  ASSERT_EQ_FMT((uint64_t)(sizeof(buf) - 1), pg_ring_len(&ring), "%llu");
  ASSERT_EQ_FMT('h', pg_ring_front(&ring), "%c");
  ASSERT_EQ_FMT('!', pg_ring_back(&ring), "%c");

  pg_ring_destroy(&ring);

  PASS();
}

TEST test_pg_bitarray() {
  pg_bitarray_t bitarr = {0};
  pg_bitarray_init(pg_heap_allocator(), &bitarr, 10);

  {
    uint8_t bits[] = {0, 1};
    pg_bitarray_setv(&bitarr, bits, 2);
  }
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 0));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 1));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 2));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 3));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 4));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 5));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 6));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 7));
  ASSERT_EQ(true, pg_bitarray_get(&bitarr, 8));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 9));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 10));

  pg_bitarray_unset_all(&bitarr);

  pg_bitarray_set(&bitarr, 5);
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 0));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 1));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 2));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 3));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 4));
  ASSERT_EQ(true, pg_bitarray_get(&bitarr, 5));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 6));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 7));

  pg_bitarray_set(&bitarr, 9);
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 8));
  ASSERT_EQ(true, pg_bitarray_get(&bitarr, 9));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 10));

  ASSERT_EQ_FMT(2ULL, pg_bitarray_count_set(&bitarr), "%llu");
  ASSERT_EQ_FMT(9ULL, pg_bitarray_count_unset(&bitarr), "%llu");

  pg_bitarray_unset(&bitarr, 9);
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 0));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 1));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 2));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 3));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 4));
  ASSERT_EQ(true, pg_bitarray_get(&bitarr, 5));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 6));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 7));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 8));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 9));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 10));

  pg_bitarray_unset(&bitarr, 9);
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 0));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 1));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 2));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 3));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 4));
  ASSERT_EQ(true, pg_bitarray_get(&bitarr, 5));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 6));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 7));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 8));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 9));
  ASSERT_EQ(false, pg_bitarray_get(&bitarr, 10));

  ASSERT_EQ_FMT(11ULL, pg_bitarray_len(&bitarr), "%llu");
  ASSERT_EQ_FMT(1ULL, pg_bitarray_count_set(&bitarr), "%llu");
  ASSERT_EQ_FMT(10ULL, pg_bitarray_count_unset(&bitarr), "%llu");

  // Iterate
  uint64_t i = 0;
  bool is_set = false;
  while (pg_bitarray_next(&bitarr, &i, &is_set)) {
    if (i - 1 == 5)
      ASSERT_EQ(true, is_set);
    else
      ASSERT_EQ(false, is_set);
  }
  ASSERT_EQ_FMT(11LL, i, "%lld");

  ASSERT_EQ(false, pg_bitarray_is_all_set(&bitarr));
  ASSERT_EQ(false, pg_bitarray_is_all_unset(&bitarr));

  {
    uint8_t bits[] = {0xff, 15};
    pg_bitarray_setv(&bitarr, bits, 2);
    ASSERT_EQ(true, pg_bitarray_is_all_set(&bitarr));
    ASSERT_EQ(false, pg_bitarray_is_all_unset(&bitarr));
    ASSERT_EQ_FMT(11ULL, pg_bitarray_count_set(&bitarr), "%llu");
    ASSERT_EQ_FMT(0ULL, pg_bitarray_count_unset(&bitarr), "%llu");
  }
  {
    uint8_t bits[] = {0, 0};
    pg_bitarray_setv(&bitarr, bits, 2);
    ASSERT_EQ(false, pg_bitarray_is_all_set(&bitarr));
    ASSERT_EQ(true, pg_bitarray_is_all_unset(&bitarr));
    ASSERT_EQ_FMT(0ULL, pg_bitarray_count_set(&bitarr), "%llu");
    ASSERT_EQ_FMT(11ULL, pg_bitarray_count_unset(&bitarr), "%llu");
  }

  {
    uint8_t bits[] = {0xff, 0};
    pg_bitarray_setv(&bitarr, bits, 2);
    ASSERT_EQ(true, pg_bitarray_get(&bitarr, 0));
    ASSERT_EQ(true, pg_bitarray_get(&bitarr, 1));
    ASSERT_EQ(true, pg_bitarray_get(&bitarr, 2));
    ASSERT_EQ(true, pg_bitarray_get(&bitarr, 3));
    ASSERT_EQ(true, pg_bitarray_get(&bitarr, 4));
    ASSERT_EQ(true, pg_bitarray_get(&bitarr, 5));
    ASSERT_EQ(true, pg_bitarray_get(&bitarr, 6));
    ASSERT_EQ(true, pg_bitarray_get(&bitarr, 7));
    ASSERT_EQ(false, pg_bitarray_get(&bitarr, 8));
    ASSERT_EQ(false, pg_bitarray_get(&bitarr, 9));
    ASSERT_EQ(false, pg_bitarray_get(&bitarr, 10));

    pg_bitarray_set(&bitarr, 8);
    ASSERT_EQ(true, pg_bitarray_get(&bitarr, 8));

    pg_bitarray_unset(&bitarr, 8);
    ASSERT_EQ(false, pg_bitarray_get(&bitarr, 8));
  }

  pg_bitarray_destroy(&bitarr);

  PASS();
}

TEST test_pg_pool() {
  pg_pool_t pool = {0};

  typedef struct {
    int a, b, c;
  } bar_t;
  pg_pool_init(&pool, sizeof(bar_t), 5);

  bar_t *b0 = pg_pool_alloc(&pool);
  ASSERT_EQ(0, b0->a);
  ASSERT(b0 != NULL);
  b0->a = 1;
  pg_pool_free(&pool, b0);

  bar_t *b1 = pg_pool_alloc(&pool);
  ASSERT(b1 != NULL);
  ASSERT_EQ(0, b1->a);

  bar_t *b2 = pg_pool_alloc(&pool);
  ASSERT(b2 != NULL);
  ASSERT_EQ(0, b2->a);

  bar_t *b3 = pg_pool_alloc(&pool);
  ASSERT(b3 != NULL);
  ASSERT_EQ(0, b3->a);

  bar_t *b4 = pg_pool_alloc(&pool);
  ASSERT(b4 != NULL);
  ASSERT_EQ(0, b4->a);

  bar_t *b5 = pg_pool_alloc(&pool);
  ASSERT(b5 != NULL);
  ASSERT_EQ(0, b5->a);

  ASSERT_EQ(NULL, pg_pool_alloc(&pool));

  pg_pool_free_all(&pool);
  bar_t *b10 = pg_pool_alloc(&pool);
  ASSERT(b10 != NULL);
  ASSERT_EQ(0, b10->a);

  pg_pool_destroy(&pool);

  PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  /* Individual tests can be run directly in main, outside of suites. */
  /* RUN_TEST(x_should_equal_1); */

  /* Tests can also be gathered into test suites. */
  RUN_SUITE(pg_array);
  RUN_TEST(test_pg_span_split_left);
  RUN_TEST(test_pg_span_split_right);
  RUN_TEST(test_pg_string_url_encode);
  RUN_TEST(test_pg_ring);
  RUN_TEST(test_pg_bitarray);
  RUN_TEST(test_pg_pool);

  GREATEST_MAIN_END(); /* display results */
}
