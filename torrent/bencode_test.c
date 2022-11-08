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

TEST test_bc_parse_value_info_span() {
  pg_span_t span = pg_span_make_c("d8:announce3:foo4:infod3:foo4:trueee");
  bc_parser_t parser = {0};
  bc_parser_init(pg_heap_allocator(), &parser, 1);
  bc_parse_error_t err = bc_parse(&parser, &span);

  ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
  ASSERT_EQ_FMT(7ULL, pg_array_count(parser.tokens), "%llu");
  ASSERT_EQ_FMT(7ULL, pg_array_count(parser.lengths), "%llu");
  ASSERT_EQ_FMT(7ULL, pg_array_count(parser.kinds), "%llu");
  PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  RUN_TEST(test_bc_parse_number);
  RUN_TEST(test_bc_parse_string);
  RUN_TEST(test_bc_parse_array);
  RUN_TEST(test_bc_parse_dictionary);
  RUN_TEST(test_bc_parse_value_info_span);

  GREATEST_MAIN_END(); /* display results */
}
