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
  const bc_value_t zero = {0};
  {
    pg_string_span_t span = {.data = "", .len = 0};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "x", .len = 1};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "2", .len = 1};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "2.", .len = 2};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(2ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "2:", .len = 2};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_STRING_LENGTH, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(2ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "-2:", .len = 3};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_STRING_LENGTH, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(3ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "2:ab", .len = 4};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_ENUM_EQ(BC_KIND_STRING, res.kind, bc_value_kind_to_string);
    ASSERT_STR_EQ("ab", res.v.string);
  }

  PASS();
}

TEST test_bc_parse_number() {
  const bc_value_t zero = {0};
  {
    pg_string_span_t span = {.data = "", .len = 0};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_null_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "x", .len = 1};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "i", .len = 1};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "i2", .len = 2};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(2ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "ie", .len = 2};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(2ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "i-", .len = 2};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(2ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "i-987e", .len = 6};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_ENUM_EQ(BC_KIND_INTEGER, res.kind, bc_value_kind_to_string);
    ASSERT_EQ_FMT(-987LL, res.v.integer, "%lld");
  }

  PASS();
}

TEST test_bc_parse_array() {
  const bc_value_t zero = {0};
  {
    pg_string_span_t span = {.data = "", .len = 0};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_null_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "l", .len = 1};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "le", .len = 2};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_ENUM_EQ(BC_KIND_ARRAY, res.kind, bc_value_kind_to_string);
    ASSERT_EQ_FMT(0ULL, pg_array_count(res.v.array), "%llu");
  }
  {
    pg_string_span_t span = {.data = "li3e", .len = 4};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(4ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "li3e_", .len = 5};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(5ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "li3e3:fooe", .len = 10};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_ENUM_EQ(BC_KIND_ARRAY, res.kind, bc_value_kind_to_string);
    ASSERT_EQ_FMT(2ULL, pg_array_count(res.v.array), "%llu");

    bc_value_t num = res.v.array[0];
    ASSERT_ENUM_EQ(BC_KIND_INTEGER, num.kind, bc_value_kind_to_string);
    ASSERT_EQ_FMT(3LL, num.v.integer, "%lld");

    bc_value_t str = res.v.array[1];
    ASSERT_ENUM_EQ(BC_KIND_STRING, str.kind, bc_value_kind_to_string);
    ASSERT_STR_EQ("foo", str.v.string);
  }
  PASS();
}

TEST test_bc_parse_dictionary() {
  const bc_value_t zero = {0};
  {
    pg_string_span_t span = {.data = "", .len = 0};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_null_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "d", .len = 1};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "de", .len = 2};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_ENUM_EQ(BC_KIND_DICTIONARY, res.kind, bc_value_kind_to_string);
    ASSERT_EQ_FMT(0ULL, pg_hashtable_count(&res.v.dictionary), "%llu");
  }
  {
    pg_string_span_t span = {.data = "d2:ab", .len = 5};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_EOF, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(5ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "d2:abi3e_", .len = 9};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(9ULL, span.len, "%llu");
    ASSERT_MEM_EQ(&zero, &res, sizeof(bc_value_t));
  }
  {
    pg_string_span_t span = {.data = "d3:abci3ee", .len = 10};
    bc_value_t res = {0};
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &res);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, span.len, "%llu");
    ASSERT_ENUM_EQ(BC_KIND_DICTIONARY, res.kind, bc_value_kind_to_string);
    ASSERT_EQ_FMT(1ULL, pg_hashtable_count(&res.v.dictionary), "%llu");

    pg_string_t key =
        pg_string_make_length(pg_heap_allocator(), "abc", strlen("abc"));
    uint64_t index = -1;
    bool found = pg_hashtable_find(&res.v.dictionary, key, &index);

    ASSERT_EQ(true, found);

    bc_value_t val = res.v.dictionary.values[index];
    ASSERT_ENUM_EQ(BC_KIND_INTEGER, val.kind, bc_value_kind_to_string);
    ASSERT_EQ_FMT(3LL, val.v.integer, "%lld");
  }
  PASS();
}

TEST test_bc_dictionary_words() {
  // Stress-test the hashtable

  pg_array_t(uint8_t) buf = {0};
  int64_t ret = 0;
  if ((ret = pg_read_file(pg_heap_allocator(), "/usr/share/dict/words",
                          &buf)) != 0) {
    fprintf(stderr, "Failed to read file: %s\n", strerror(ret));
    FAIL();
  }

  pg_string_span_t span = {.data = (char*)buf, .len = pg_array_count(buf)};
  for (uint64_t i = 0; i < pg_array_count(buf); i++) {
    char* newline = memchr(span.data, '\n', span.len);
  }

  PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  RUN_TEST(test_bc_parse_i64);
  RUN_TEST(test_bc_parse_string);
  RUN_TEST(test_bc_parse_number);
  RUN_TEST(test_bc_parse_array);
  RUN_TEST(test_bc_parse_dictionary);
  RUN_TEST(test_bc_dictionary_words);

  GREATEST_MAIN_END(); /* display results */
}
