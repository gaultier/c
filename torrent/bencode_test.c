#include "bencode.h"

#include "../vendor/greatest/greatest.h"

TEST test_bc_parse_number(void) {
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
    ASSERT_EQ_FMT(0ULL, pg_array_len(parser.spans), "%llu");
    ASSERT_EQ_FMT(0ULL, pg_array_len(parser.lengths), "%llu");
    ASSERT_EQ_FMT(0ULL, pg_array_len(parser.kinds), "%llu");

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
    pg_span_t span = pg_span_make_c("i-0e");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_INVALID_NUMBER, err, bc_parse_error_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("i03e");
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
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.spans), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.lengths), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.kinds), "%llu");

    ASSERT_STRN_EQ("-123", parser.spans[0].data, parser.spans[0].len);
    ASSERT_EQ_FMT(4ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_INTEGER, parser.kinds[0], bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }

  PASS();
}

TEST test_bc_parse_string(void) {
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
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.spans), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.lengths), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.kinds), "%llu");

    ASSERT_STRN_EQ("abc", parser.spans[0].data, parser.spans[0].len);
    ASSERT_EQ_FMT(3ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_STRING, parser.kinds[0], bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }

  PASS();
}

TEST test_bc_parse_array(void) {
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
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.lengths), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.kinds), "%llu");

    ASSERT_EQ_FMT(0ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_ARRAY, parser.kinds[0], bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }
  {
    pg_span_t span = pg_span_make_c("li3e2:abli9eee");
    bc_parser_t parser = {0};
    bc_parser_init(pg_heap_allocator(), &parser, 1);
    bc_parse_error_t err = bc_parse(&parser, &span);

    ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
    ASSERT_EQ_FMT(5ULL, pg_array_len(parser.spans), "%llu");
    ASSERT_EQ_FMT(5ULL, pg_array_len(parser.lengths), "%llu");
    ASSERT_EQ_FMT(5ULL, pg_array_len(parser.kinds), "%llu");

    ASSERT_EQ_FMT(3ULL, parser.lengths[0], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_ARRAY, parser.kinds[0], bc_value_kind_to_string);

    ASSERT_EQ_FMT(1ULL, parser.lengths[1], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_INTEGER, parser.kinds[1], bc_value_kind_to_string);

    ASSERT_EQ_FMT(2ULL, parser.lengths[2], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_STRING, parser.kinds[2], bc_value_kind_to_string);

    ASSERT_EQ_FMT(1ULL, parser.lengths[3], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_ARRAY, parser.kinds[3], bc_value_kind_to_string);

    ASSERT_EQ_FMT(1ULL, parser.lengths[4], "%llu");
    ASSERT_ENUM_EQ(BC_KIND_INTEGER, parser.kinds[4], bc_value_kind_to_string);

    bc_parser_destroy(&parser);
  }

  PASS();
}

TEST test_bc_parse_dictionary(void) {
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
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.spans), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.lengths), "%llu");
    ASSERT_EQ_FMT(1ULL, pg_array_len(parser.kinds), "%llu");

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
    ASSERT_EQ_FMT(3ULL, pg_array_len(parser.spans), "%llu");
    ASSERT_EQ_FMT(3ULL, pg_array_len(parser.lengths), "%llu");
    ASSERT_EQ_FMT(3ULL, pg_array_len(parser.kinds), "%llu");

    ASSERT_EQ_FMT(9ULL, parser.spans[0].len, "%llu");
    ASSERT_STRN_EQ("d2:abi3ee", parser.spans[0].data, 9ULL);
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

TEST test_bc_parse_value_info_span(void) {
  pg_span_t span = pg_span_make_c("d8:announce3:foo4:infod3:foo4:trueee");
  bc_parser_t parser = {0};
  bc_parser_init(pg_heap_allocator(), &parser, 1);
  bc_parse_error_t err = bc_parse(&parser, &span);

  ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);
  ASSERT_EQ_FMT(7ULL, pg_array_len(parser.spans), "%llu");
  ASSERT_EQ_FMT(7ULL, pg_array_len(parser.lengths), "%llu");
  ASSERT_EQ_FMT(7ULL, pg_array_len(parser.kinds), "%llu");

  ASSERT_EQ_FMT(4ULL, parser.lengths[0], "%llu");
  ASSERT_ENUM_EQ(BC_KIND_DICTIONARY, parser.kinds[0], bc_value_kind_to_string);
  PASS();
}

TEST test_bc_metainfo(void) {
  pg_span_t span = pg_span_make_c(
      "d8:announce3:foo13:announce-"
      "listl27:http://example.com/"
      "announcee4:infod3:foo4:true6:lengthi20e12:piece "
      "lengthi20e6:pieces20:000000000000000000004:name5:helloe6:lengthi99ee");
  bc_parser_t parser = {0};
  bc_parser_init(pg_heap_allocator(), &parser, 1);
  bc_parse_error_t err = bc_parse(&parser, &span);

  ASSERT_ENUM_EQ(BC_PE_NONE, err, bc_parse_error_to_string);

  bc_metainfo_t metainfo = {0};
  pg_span_t info_span = {0};
  bc_metainfo_error_t err_metainfo =
      bc_parser_init_metainfo(&parser, &metainfo, &info_span);
  ASSERT_ENUM_EQ(BC_ME_NONE, err_metainfo, bc_metainfo_error_to_string);

  ASSERT_EQ_FMT(3ULL, metainfo.announce.len, "%llu");
  ASSERT_STRN_EQ("foo", metainfo.announce.data, 3);

  ASSERT_EQ_FMT(20ULL, metainfo.pieces.len, "%llu");
  ASSERT_STRN_EQ("00000000000000000000", metainfo.pieces.data, 20);

  ASSERT_EQ_FMT(5ULL, metainfo.name.len, "%llu");
  ASSERT_STRN_EQ("hello", metainfo.name.data, 5);

  ASSERT_EQ_FMT(20U, metainfo.piece_length, "%u");
  ASSERT_EQ_FMT(20ULL, metainfo.length, "%llu");

  ASSERT_STRN_EQ(
      "d3:foo4:true6:lengthi20e12:piece "
      "lengthi20e6:pieces20:000000000000000000004:name5:helloee",
      info_span.data, info_span.len);

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
  RUN_TEST(test_bc_metainfo);

  GREATEST_MAIN_END(); /* display results */
}
