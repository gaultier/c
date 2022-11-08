#include "bencode.h"

#include "vendor/greatest/greatest.h"

TEST test_bc_parse_number() {
  {
    pg_span_t span = pg_span_make_c("-23");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("0");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("i");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("i3");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("i-3");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(0ULL, pg_array_count(parser.tokens), "%llu");
    ASSERT_EQ_FMT(0ULL, pg_array_count(parser.lengths), "%llu");
    ASSERT_EQ_FMT(0ULL, pg_array_count(parser.kinds), "%llu");

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("iae");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("i-e");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("ie");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("i1-e");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("i-123e");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.tokens), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.lengths), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.kinds), "%llu");

    ASSERT_STRN_EQ("-123", parser.tokens[0].data, parser.tokens[0].len);
    ASSERT_EQ_FMT(4ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_INTEGER, parser.kinds[0], bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }

  PASS();
}

TEST test_bc_parse_string() {
  {
    pg_span_t span = pg_span_make_c(":");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("0");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("0:abc");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("2:a");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_STRING, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("1a");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_STRING, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("-3:abc");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("3:abc");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.tokens), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.lengths), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.kinds), "%llu");

    ASSERT_STRN_EQ("abc", parser.tokens[0].data, parser.tokens[0].len);
    ASSERT_EQ_FMT(3ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_STRING, parser.kinds[0], bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }

  PASS();
}

TEST test_bc_parse_array() {
  {
    pg_span_t span = pg_span_make_c("l");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("li2e");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("le");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.tokens), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.lengths), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.kinds), "%llu");

    ASSERT_EQ_FMT(0ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_ARRAY, parser.kinds[0], bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("li3e2:abe");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(3ULL, pg_array_count(parser.tokens), "%llu");
    ASSERT_EQ_FMT(3ULL, pg_array_count(parser.lengths), "%llu");
    ASSERT_EQ_FMT(3ULL, pg_array_count(parser.kinds), "%llu");

    ASSERT_EQ_FMT(2ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_ARRAY, parser.kinds[0], bc_value_kind_to_string);

    ASSERT_EQ_FMT(1ULL, parser.lengths[1], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_INTEGER, parser.kinds[1], bc_value_kind_to_string);

    ASSERT_EQ_FMT(2ULL, parser.lengths[2], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_STRING, parser.kinds[2], bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }

  PASS();
}

TEST test_bc_parse_dictionary() {
  {
    pg_span_t span = pg_span_make_c("d");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("dle");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_UNEXPECTED_CHARACTER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("dlelee");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_DICT, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("de");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.tokens), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.lengths), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_count(parser.kinds), "%llu");

    ASSERT_EQ_FMT(0ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_DICTIONARY, parser.kinds[0],
                   bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("d2:abi3ee");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(3ULL, pg_array_count(parser.tokens), "%llu");
    ASSERT_EQ_FMT(3ULL, pg_array_count(parser.lengths), "%llu");
    ASSERT_EQ_FMT(3ULL, pg_array_count(parser.kinds), "%llu");

    ASSERT_EQ_FMT(9ULL, parser.tokens[0].len, "%llu");
    ASSERT_STRN_EQ("d2:abi3ee", parser.tokens[0].data, 9ULL);
    ASSERT_EQ_FMT(2ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_DICTIONARY, parser.kinds[0],
                   bc_value_kind_to_string);

    ASSERT_EQ_FMT(2ULL, parser.lengths[1], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_STRING, parser.kinds[1], bc_value_kind_to_string);

    ASSERT_EQ_FMT(1ULL, parser.lengths[2], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_INTEGER, parser.kinds[2], bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }

  PASS();
}

#if 0
TEST test_bc_dictionary_words() {
  // Stress-test the hashtable

  pg_array_t(uint8_t) buf = {0};
  int64_t ret = 0;
  if ((ret = pg_read_file(pg_heap_allocator(), "/usr/share/dict/words",
                          &buf)) != 0) {
    fprintf(stderr, "Failed to read file: %s\n", strerror(ret));
    FAIL();
  }

  pg_span_t span = {.data = (char*)buf, .len = pg_array_count(buf)};
  bc_dictionary_t dict = {0};
  pg_hashtable_init(&dict, 10, pg_heap_allocator());

  for (uint64_t i = 0; i < pg_array_count(buf); i++) {
    pg_span_t left = {0}, right = {0};

    if (!pg_span_split(span, '\n', &left, &right)) break;
    span = right;

    pg_string_t key =
        pg_string_make_length(pg_heap_allocator(), left.data, left.len);
    bc_value_t* val = pg_heap_allocator().realloc(sizeof(bc_value_t), NULL, 0);
    val->kind = BC_KIND_INTEGER;
    val->v.integer = i;

    pg_hashtable_upsert(&dict, key, val);
  }

  ASSERT_EQ_FMT(235886ULL, pg_hashtable_count(&dict), "%llu");
  ASSERT_LTE(235886ULL, pg_array_capacity(dict.keys));

  pg_hashtable_destroy(&dict);
  PASS();
}

TEST test_bc_parse_value_info_span() {
  {
    bc_value_t value = {0};
    char* s = "d8:announce3:foo4:infod3:foo4:trueee";
    pg_span_t span = {.data = s, .len = strlen(s)};
    pg_span_t info_span = {0};

    ASSERT_ENUM_EQ(
        BC_PE_NONE,
        bc_parse_value(pg_heap_allocator(), &span, &value, &info_span),
        bc_parse_error_to_string);

    ASSERT_EQ_FMT((uint64_t)strlen("d3:foo4:truee"), info_span.len, "%llu");
    ASSERT_STRN_EQ("d3:foo4:truee", info_span.data, info_span.len);
  }
  PASS();
}
#endif

GREATEST_MAIN_DEFS();

int main(int argc, char** argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  RUN_TEST(test_bc_parse_number);
  RUN_TEST(test_bc_parse_string);
  RUN_TEST(test_bc_parse_array);
  RUN_TEST(test_bc_parse_dictionary);
  //  RUN_TEST(test_bc_dictionary_words);
  //  RUN_TEST(test_bc_parse_value_info_span);

  GREATEST_MAIN_END(); /* display results */
}
