#pragma once

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../pg/pg.h"

#define BC_BLOCK_LENGTH ((uint32_t)1 << 14)

typedef enum {
  BC_KIND_NONE,
  BC_KIND_INTEGER,
  BC_KIND_STRING,
  BC_KIND_ARRAY,
  BC_KIND_DICTIONARY,
} bc_kind_t;

__attribute__((unused)) static const char *bc_value_kind_to_string(int n) {
  switch (n) {
  case BC_KIND_NONE:
    return "BC_KIND_NONE";
  case BC_KIND_INTEGER:
    return "BC_KIND_INTEGER";
  case BC_KIND_STRING:
    return "BC_KIND_STRING";
  case BC_KIND_ARRAY:
    return "BC_KIND_ARRAY";
  case BC_KIND_DICTIONARY:
    return "BC_KIND_DICTIONARY";
  default:
    __builtin_unreachable();
  }
}

typedef struct {
  pg_array_t(pg_span_t) spans;
  pg_array_t(uint64_t) lengths;
  pg_array_t(bc_kind_t) kinds;
  uint64_t parent;
} bc_parser_t;

__attribute__((unused)) static void
bc_parser_init(pg_allocator_t allocator, bc_parser_t *parser,
               uint64_t estimate_items_count) {
  pg_array_init_reserve(parser->spans, estimate_items_count, allocator);
  pg_array_init_reserve(parser->lengths, estimate_items_count, allocator);
  pg_array_init_reserve(parser->kinds, estimate_items_count, allocator);
}

__attribute__((unused)) static void bc_parser_destroy(bc_parser_t *parser) {
  pg_array_free(parser->spans);
  pg_array_free(parser->lengths);
  pg_array_free(parser->kinds);
}

typedef enum {
  BC_PE_NONE,
  BC_PE_UNEXPECTED_CHARACTER,
  BC_PE_INVALID_NUMBER,
  BC_PE_INVALID_STRING,
  BC_PE_INVALID_DICT,
} bc_parse_error_t;

__attribute__((unused)) static const char *bc_parse_error_to_string(int e) {
  switch (e) {
  case BC_PE_NONE:
    return "BC_PE_NONE";
  case BC_PE_UNEXPECTED_CHARACTER:
    return "BC_PE_UNEXPECTED_CHARACTER";
  case BC_PE_INVALID_NUMBER:
    return "BC_PE_INVALID_NUMBER";
  case BC_PE_INVALID_STRING:
    return "BC_PE_INVALID_STRING";
  case BC_PE_INVALID_DICT:
    return "BC_PE_INVALID_DICT";
  default:
    assert(0);
  }
}

__attribute__((unused)) static bc_parse_error_t bc_parse(bc_parser_t *parser,
                                                         pg_span_t *input) {
  assert(pg_array_len(parser->spans) == pg_array_len(parser->lengths));
  assert(pg_array_len(parser->lengths) == pg_array_len(parser->kinds));

  const char c = pg_span_peek_left(*input, NULL);

  switch (c) {
  case 'i': {
    if (parser->parent != -1ULL)
      parser->lengths[parser->parent] += 1;

    pg_span_t left = {0}, right = {0};
    const bool found = pg_span_split_at_first(*input, 'e', &left, &right);
    if (!found)
      return BC_PE_INVALID_NUMBER;

    assert(left.len >= 1);

    if (left.len == 1)
      return BC_PE_INVALID_NUMBER;  // `ie`
    pg_span_consume_left(&left, 1); // Skip 'i'

    assert(left.len > 0);

    if (left.data[0] == '-' && left.len == 1)
      return BC_PE_INVALID_NUMBER; // `i-e`
    if (left.data[0] == '-' && left.len == 2 && left.data[1] == '0')
      return BC_PE_INVALID_NUMBER; // `i-0e`

    if (!(pg_char_is_digit(left.data[0]) ||
          left.data[0] == '-')) // `iae` or `i-e`
      return BC_PE_INVALID_NUMBER;

    pg_span_consume_left(&right, 1); // Skip 'e'

    for (uint64_t i = 1; i < left.len; i++) {
      if (!pg_char_is_digit(left.data[i]))
        return BC_PE_INVALID_NUMBER;
    }
    if (left.len > 1 && left.data[0] == '0')
      return BC_PE_INVALID_NUMBER; // `i03e`

    pg_array_append(parser->spans, left);
    pg_array_append(parser->lengths, left.len);
    pg_array_append(parser->kinds, BC_KIND_INTEGER);

    *input = right;
    break;
  }
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9': {
    if (parser->parent != -1ULL)
      parser->lengths[parser->parent] += 1;

    pg_span_t left = {0}, right = {0};
    const bool found = pg_span_split_at_first(*input, ':', &left, &right);
    if (!found)
      return BC_PE_INVALID_STRING;

    assert(left.len >= 1);

    pg_span_consume_left(&right, 1); // Skip ':'

    uint64_t len = 0;
    for (uint64_t i = 0; i < left.len; i++) {
      if (!pg_char_is_digit(left.data[i]))
        return BC_PE_INVALID_STRING;
      len *= 10;
      len += (uint64_t)(left.data[i] - '0');
    }
    assert(len > 0);
    if (right.len < len)
      return BC_PE_INVALID_STRING; // `5:a`

    pg_span_t string = {.data = right.data, .len = len};
    pg_array_append(parser->spans, string);
    pg_array_append(parser->lengths, len);
    pg_array_append(parser->kinds, BC_KIND_STRING);

    *input = right;
    pg_span_consume_left(input, len); // Skip over string content
    break;
  }
  case 'l': {
    if (parser->parent != -1ULL)
      parser->lengths[parser->parent] += 1;

    pg_span_consume_left(input, 1); // Skip 'l'

    pg_array_append(parser->spans, (pg_span_t){0}); // Does not matter
    pg_array_append(parser->lengths, 0); // Will be patched at the end
    pg_array_append(parser->kinds, BC_KIND_ARRAY);

    const uint64_t parent = parser->parent;
    parser->parent = pg_array_len(parser->kinds) - 1;

    bool more_chars = false;
    while (pg_span_peek_left(*input, &more_chars) != 'e' && more_chars) {
      bc_parse_error_t err = bc_parse(parser, input);
      if (err != BC_PE_NONE)
        return err;
    }
    if (pg_span_peek_left(*input, NULL) != 'e')
      return BC_PE_UNEXPECTED_CHARACTER;
    pg_span_consume_left(input, 1); // Skip 'e'

    parser->parent = parent;
    break;
  }
  case 'd': {
    if (parser->parent != -1ULL)
      parser->lengths[parser->parent] += 1;

    const pg_span_t original = *input;

    pg_span_consume_left(input, 1); // Skip 'l'

    pg_array_append(parser->spans, original); // Will be patched at the end
    pg_array_append(parser->lengths, 0);      // Will be patched at the end
    pg_array_append(parser->kinds, BC_KIND_DICTIONARY);

    const uint64_t parent = parser->parent;
    parser->parent = pg_array_len(parser->kinds) - 1;
    const uint64_t me = parser->parent;

    bool more_chars = false;
    while (pg_span_peek_left(*input, &more_chars) != 'e' && more_chars) {
      bc_parse_error_t err = bc_parse(parser, input);
      if (err != BC_PE_NONE)
        return err;
    }
    if (pg_span_peek_left(*input, NULL) != 'e')
      return BC_PE_UNEXPECTED_CHARACTER;
    pg_span_consume_left(input, 1); // Skip 'e'

    assert(me < pg_array_len(parser->kinds));
    const uint64_t kv_count = parser->lengths[me];
    if (kv_count % 2 != 0) {
      return BC_PE_INVALID_DICT;
    }

    uint64_t j = me + 1;
    for (uint64_t i = 0; i < kv_count; i += 2) {
      assert(j < pg_array_len(parser->kinds));

      if (parser->kinds[j] != BC_KIND_STRING) {
        return BC_PE_INVALID_DICT;
      }
      // Skip over nested children
      if (parser->kinds[j + 1] == BC_KIND_DICTIONARY ||
          parser->kinds[j + 1] == BC_KIND_ARRAY)
        j += parser->lengths[j + 1];
    }

    assert(me < pg_array_len(parser->spans));
    parser->spans[me].len -= input->len;

    parser->parent = parent;

    break;
  }
  case 0:
    return BC_PE_NONE;

  default:
    return BC_PE_UNEXPECTED_CHARACTER;
  }
  return BC_PE_NONE;
}

__attribute__((unused)) static void bc_dump_value_indent(FILE *f,
                                                         uint64_t indent) {
  for (uint64_t i = 0; i < indent; i++)
    fprintf(f, " ");
}

__attribute__((unused)) static uint64_t
bc_dump_value(bc_parser_t *parser, FILE *f, uint64_t indent, uint64_t index) {
  assert(index < pg_array_len(parser->kinds));

  const bc_kind_t kind = parser->kinds[index];
  const pg_span_t span = parser->spans[index];
  const uint64_t len = parser->lengths[index];

  switch (kind) {
  case BC_KIND_NONE:
    assert(0);
  case BC_KIND_INTEGER:
    fprintf(f, "%.*s", (int)span.len, span.data);
    return 1;
  case BC_KIND_STRING: {
    fprintf(f, "\"");
    for (uint64_t i = 0; i < span.len; i++) {
      char c = span.data[i];
      if (pg_char_is_alphanumeric(c) || c == ' ' || c == '-' || c == '.' ||
          c == '_' || c == '/')
        fprintf(f, "%c", c);
      else
        fprintf(f, "\\u%04x", c);
    }
    fprintf(f, "\"");

    return 1;
  }
  case BC_KIND_ARRAY: {
    fprintf(f, "[\n");
    uint64_t j = index + 1;
    for (uint64_t i = 0; i < len; i++) {
      bc_dump_value_indent(f, indent + 2);
      j += bc_dump_value(parser, f, indent + 2, j);

      if (i < len - 1)
        fprintf(f, ",");

      fprintf(f, "\n");
    }
    bc_dump_value_indent(f, indent);
    fprintf(f, "]");

    return j - index;
  }
  case BC_KIND_DICTIONARY: {
    fprintf(f, "{\n");
    uint64_t j = index + 1;
    for (uint64_t i = 0; i < len; i += 2) {
      bc_dump_value_indent(f, indent + 2);

      j += bc_dump_value(parser, f, indent + 2, j);

      fprintf(f, ": ");
      j += bc_dump_value(parser, f, indent + 2, j);
      if (i < len - 2)
        fprintf(f, ",");

      fprintf(f, "\n");
    }
    bc_dump_value_indent(f, indent);
    fprintf(f, "}");

    return j - index;
  }
  }
  __builtin_unreachable();
}

__attribute__((unused)) static void bc_dump_values(bc_parser_t *parser, FILE *f,
                                                   uint64_t indent) {
  assert(pg_array_len(parser->spans) == pg_array_len(parser->lengths));
  assert(pg_array_len(parser->lengths) == pg_array_len(parser->kinds));

  bc_dump_value(parser, f, indent, 0);
}

typedef struct {
  pg_span_t announce;
  uint64_t length;
  pg_span_t name;
  pg_span_t pieces;

  uint32_t piece_length;
  // Computed
  uint32_t pieces_count, blocks_count, blocks_per_piece, last_piece_length,
      last_piece_block_count;
} bc_metainfo_t;

typedef enum {
  BC_ME_NONE,
  BC_ME_METAINFO_NOT_DICTIONARY,
  BC_ME_ANNOUNCE_NOT_FOUND,
  BC_ME_INFO_NOT_FOUND,
  BC_ME_PIECE_LENGTH_NOT_FOUND,
  BC_ME_PIECE_LENGTH_INVALID_VALUE,
  BC_ME_NAME_NOT_FOUND,
  BC_ME_NAME_INVALID_VALUE,
  BC_ME_LENGTH_NOT_FOUND,
  BC_ME_LENGTH_INVALID_VALUE,
  BC_ME_PIECES_NOT_FOUND,
  BC_ME_PIECES_INVALID_VALUE,
} bc_metainfo_error_t;

__attribute__((unused)) static const char *
bc_metainfo_error_to_string(int err) {
  switch (err) {
  case BC_ME_NONE:
    return "BC_ME_NONE";
  case BC_ME_METAINFO_NOT_DICTIONARY:
    return "BC_ME_METAINFO_NOT_DICTIONARY";
  case BC_ME_ANNOUNCE_NOT_FOUND:
    return "BC_ME_ANNOUNCE_NOT_FOUND";
  case BC_ME_INFO_NOT_FOUND:
    return "BC_ME_INFO_NOT_FOUND";
  case BC_ME_PIECE_LENGTH_NOT_FOUND:
    return "BC_ME_PIECE_LENGTH_NOT_FOUND";
  case BC_ME_PIECE_LENGTH_INVALID_VALUE:
    return "BC_ME_PIECE_LENGTH_INVALID_VALUE";
  case BC_ME_NAME_NOT_FOUND:
    return "BC_ME_NAME_NOT_FOUND";
  case BC_ME_NAME_INVALID_VALUE:
    return "BC_ME_NAME_INVALID_VALUE";
  case BC_ME_LENGTH_NOT_FOUND:
    return "BC_ME_LENGTH_NOT_FOUND";
  case BC_ME_LENGTH_INVALID_VALUE:
    return "BC_ME_LENGTH_INVALID_VALUE";
  case BC_ME_PIECES_NOT_FOUND:
    return "BC_ME_PIECES_NOT_FOUND";
  case BC_ME_PIECES_INVALID_VALUE:
    return "BC_ME_PIECES_INVALID_VALUE";
  default:
    assert(0);
  }
}

__attribute__((unused)) static bc_metainfo_error_t
bc_parser_init_metainfo(bc_parser_t *parser, bc_metainfo_t *metainfo,
                        pg_span_t *info_span) {
  if (pg_array_len(parser->kinds) == 0)
    return BC_ME_METAINFO_NOT_DICTIONARY;
  if (parser->kinds[0] != BC_KIND_DICTIONARY)
    return BC_ME_METAINFO_NOT_DICTIONARY;

  const pg_span_t announce_key = pg_span_make_c("announce");
  const pg_span_t info_key = pg_span_make_c("info");
  const pg_span_t piece_length_key = pg_span_make_c("piece length");
  const pg_span_t name_key = pg_span_make_c("name");
  const pg_span_t length_key = pg_span_make_c("length");
  const pg_span_t pieces_key = pg_span_make_c("pieces");

  bool in_info = false;
  uint64_t info_end = 0;
  for (uint64_t i = 1; i < pg_array_len(parser->kinds) - 1; i++) {
    if (in_info && i > info_end) {
      in_info = false;
    }

    bc_kind_t key_kind = parser->kinds[i];
    pg_span_t key_span = parser->spans[i];
    bc_kind_t value_kind = parser->kinds[i + 1];
    pg_span_t value_span = parser->spans[i + 1];

    if (key_kind == BC_KIND_STRING && pg_span_eq(announce_key, key_span) &&
        value_kind == BC_KIND_STRING) {
      metainfo->announce = value_span;
    } else if (key_kind == BC_KIND_STRING && pg_span_eq(info_key, key_span) &&
               value_kind == BC_KIND_DICTIONARY) {
      in_info = true;
      info_end = i + parser->lengths[i + 1];
      *info_span = value_span;
    } else if (in_info && key_kind == BC_KIND_STRING &&
               pg_span_eq(piece_length_key, key_span) &&
               value_kind == BC_KIND_INTEGER) {
      bool value_valid = false;
      const int64_t piece_length =
          pg_span_parse_i64_decimal(value_span, &value_valid);
      if (!value_valid || piece_length <= 0 || piece_length > UINT32_MAX)
        return BC_ME_PIECE_LENGTH_INVALID_VALUE;
      metainfo->piece_length = (uint32_t)piece_length;
    } else if (in_info && key_kind == BC_KIND_STRING &&
               pg_span_eq(name_key, key_span) && value_kind == BC_KIND_STRING) {
      // TODO: more validation
      if (value_span.len == 0)
        return BC_ME_NAME_INVALID_VALUE;

      metainfo->name = value_span;
    } else if (in_info && key_kind == BC_KIND_STRING &&
               pg_span_eq(length_key, key_span) &&
               value_kind == BC_KIND_INTEGER) {
      bool value_valid = false;
      const int64_t length =
          pg_span_parse_i64_decimal(value_span, &value_valid);
      if (!value_valid || length <= 0)
        return BC_ME_LENGTH_INVALID_VALUE;

      metainfo->length = (uint64_t)length;
    } else if (in_info && key_kind == BC_KIND_STRING &&
               pg_span_eq(pieces_key, key_span) &&
               value_kind == BC_KIND_STRING) {
      if (value_span.len == 0 || value_span.len % 20 != 0)
        return BC_ME_PIECES_INVALID_VALUE;
      metainfo->pieces = value_span;
    }
  }

  if (metainfo->announce.len == 0)
    return BC_ME_ANNOUNCE_NOT_FOUND;
  if (metainfo->length == 0)
    return BC_ME_LENGTH_NOT_FOUND;
  if (metainfo->piece_length == 0)
    return BC_ME_PIECE_LENGTH_NOT_FOUND;
  if (metainfo->pieces.len == 0)
    return BC_ME_PIECES_NOT_FOUND;
  if (metainfo->name.len == 0)
    return BC_ME_NAME_NOT_FOUND;

  // Compute QoL values
  metainfo->pieces_count = (uint32_t)(metainfo->pieces.len / 20);
  metainfo->blocks_per_piece = metainfo->piece_length / BC_BLOCK_LENGTH;
  metainfo->last_piece_length =
      (uint32_t)(metainfo->length -
                 (metainfo->pieces_count - 1) * metainfo->piece_length);
  metainfo->last_piece_block_count =
      (uint32_t)((ceil((double)metainfo->last_piece_length / BC_BLOCK_LENGTH)));
  metainfo->blocks_count =
      (uint32_t)(ceil((double)metainfo->length / BC_BLOCK_LENGTH));
  return BC_ME_NONE;
}

__attribute__((unused)) static bool
metainfo_is_last_piece(bc_metainfo_t *metainfo, uint32_t piece) {
  return piece == metainfo->pieces_count - 1;
}

__attribute__((unused)) static uint32_t
metainfo_block_count_for_piece(bc_metainfo_t *metainfo, uint32_t piece) {
  if (metainfo_is_last_piece(metainfo, piece))
    return metainfo->last_piece_block_count;
  else
    return metainfo->blocks_per_piece;
}

__attribute__((unused)) static uint32_t
metainfo_block_for_piece_length(bc_metainfo_t *metainfo, uint32_t piece,
                                uint32_t block_for_piece) {
  assert(block_for_piece < metainfo->blocks_per_piece);

  // Special case for last block of last piece
  if (metainfo_is_last_piece(metainfo, piece) &&
      block_for_piece == metainfo->last_piece_block_count - 1)
    return (uint32_t)(metainfo->length - (piece * metainfo->piece_length +
                                          block_for_piece * BC_BLOCK_LENGTH));

  return BC_BLOCK_LENGTH;
}

__attribute__((unused)) static uint32_t
metainfo_piece_length(bc_metainfo_t *metainfo, uint32_t piece) {
  if (metainfo_is_last_piece(metainfo, piece))
    return metainfo->last_piece_length;

  return metainfo->piece_length;
}

__attribute__((unused)) static uint32_t
metainfo_block_to_block_for_piece(bc_metainfo_t *metainfo, uint32_t piece,
                                  uint32_t block) {
  assert(piece < metainfo->pieces_count);
  assert(block < metainfo->blocks_count);

  const uint32_t block_for_piece =
      (block * BC_BLOCK_LENGTH - piece * metainfo->piece_length) /
      BC_BLOCK_LENGTH;

  // Handle special case of last block of last piece
  if (metainfo_is_last_piece(metainfo, piece) &&
      block_for_piece >= metainfo->last_piece_block_count) {
    return metainfo->last_piece_block_count - 1;
  }
  return block_for_piece;
}

__attribute__((unused)) static uint32_t
metainfo_block_for_piece_to_block(bc_metainfo_t *metainfo, uint32_t piece,
                                  uint32_t block_for_piece) {
  assert(piece < metainfo->pieces_count);
  assert(block_for_piece < metainfo_block_count_for_piece(metainfo, piece));

  const uint32_t block = piece * metainfo->blocks_per_piece + block_for_piece;
  assert(block < metainfo->blocks_count);
  return block;
}
