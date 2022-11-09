#pragma once

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "../pg/pg.h"

typedef enum : uint8_t {
  BC_KIND_NONE,
  BC_KIND_INTEGER,
  BC_KIND_STRING,
  BC_KIND_ARRAY,
  BC_KIND_DICTIONARY,
} bc_kind_t;

const char* bc_value_kind_to_string(int n) {
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
  pg_array_t(pg_span32_t) spans;
  pg_array_t(uint32_t) lengths;
  pg_array_t(bc_kind_t) kinds;
  uint32_t parent;
} bc_parser_t;

void bc_parser_init(pg_allocator_t allocator, bc_parser_t* parser,
                    uint32_t estimate_items_count) {
  pg_array_init_reserve(parser->spans, estimate_items_count, allocator);
  pg_array_init_reserve(parser->lengths, estimate_items_count, allocator);
  pg_array_init_reserve(parser->kinds, estimate_items_count, allocator);
}

void bc_parser_destroy(bc_parser_t* parser) {
  pg_array_free(parser->spans);
  pg_array_free(parser->lengths);
  pg_array_free(parser->kinds);
}

typedef enum : uint8_t {
  BC_PE_NONE,
  BC_PE_UNEXPECTED_CHARACTER,
  BC_PE_INVALID_NUMBER,
  BC_PE_INVALID_STRING,
  BC_PE_INVALID_DICT,
} bc_parse_error_t;

const char* bc_parse_error_to_string(int e) {
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
      __builtin_unreachable();
  }
}

bc_parse_error_t bc_parse(bc_parser_t* parser, pg_span32_t* input) {
  assert(pg_array_count(parser->spans) == pg_array_count(parser->lengths));
  assert(pg_array_count(parser->lengths) == pg_array_count(parser->kinds));

  const char c = pg_span32_peek(*input);

  switch (c) {
    case 'i': {
      if (parser->parent != -1U) parser->lengths[parser->parent] += 1;

      pg_span32_t left = {0}, right = {0};
      const bool found = pg_span32_split(*input, 'e', &left, &right);
      if (!found) return BC_PE_INVALID_NUMBER;

      assert(left.len >= 1);

      if (left.len == 1) return BC_PE_INVALID_NUMBER;  // `ie`
      pg_span32_consume_left(&left, 1);                // Skip 'i'

      assert(left.len > 0);

      if (left.data[0] == '-' && left.len == 1)
        return BC_PE_INVALID_NUMBER;  // `i-e`

      if (!(pg_char_is_digit(left.data[0]) ||
            left.data[0] == '-'))  // `iae` or `i-e`
        return BC_PE_INVALID_NUMBER;

      pg_span32_consume_left(&right, 1);  // Skip 'e'

      for (uint32_t i = 1; i < left.len; i++) {
        if (!pg_char_is_digit(left.data[i])) return BC_PE_INVALID_NUMBER;
      }

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
      if (parser->parent != -1U) parser->lengths[parser->parent] += 1;

      pg_span32_t left = {0}, right = {0};
      const bool found = pg_span32_split(*input, ':', &left, &right);
      if (!found) return BC_PE_INVALID_STRING;

      assert(left.len >= 1);

      pg_span32_consume_left(&right, 1);  // Skip ':'

      uint32_t len = 0;
      for (uint32_t i = 0; i < left.len; i++) {
        if (!pg_char_is_digit(left.data[i])) return BC_PE_INVALID_STRING;
        len *= 10;
        len += left.data[i] - '0';
      }
      assert(len > 0);
      if (right.len < len) return BC_PE_INVALID_STRING;  // `5:a`

      pg_span32_t string = {.data = right.data, .len = len};
      pg_array_append(parser->spans, string);
      pg_array_append(parser->lengths, len);
      pg_array_append(parser->kinds, BC_KIND_STRING);

      *input = right;
      pg_span32_consume_left(input, len);  // Skip over string content
      break;
    }
    case 'l': {
      if (parser->parent != -1U) parser->lengths[parser->parent] += 1;

      pg_span32_consume_left(input, 1);  // Skip 'l'

      pg_array_append(parser->spans, (pg_span32_t){0});  // Does not matter
      pg_array_append(parser->lengths, 0);  // Will be patched at the end
      pg_array_append(parser->kinds, BC_KIND_ARRAY);

      const uint32_t parent = parser->parent;
      parser->parent = pg_array_count(parser->kinds) - 1;

      while (pg_span32_peek(*input) != 'e' && pg_span32_peek(*input) != 0) {
        bc_parse_error_t err = bc_parse(parser, input);
        if (err != BC_PE_NONE) return err;
      }
      if (pg_span32_peek(*input) != 'e') return BC_PE_UNEXPECTED_CHARACTER;
      pg_span32_consume_left(input, 1);  // Skip 'e'

      parser->parent = parent;
      break;
    }
    case 'd': {
      if (parser->parent != -1U) parser->lengths[parser->parent] += 1;

      const pg_span32_t original = *input;

      pg_span32_consume_left(input, 1);  // Skip 'l'

      pg_array_append(parser->spans, original);  // Will be patched at the end
      pg_array_append(parser->lengths, 0);       // Will be patched at the end
      pg_array_append(parser->kinds, BC_KIND_DICTIONARY);

      const uint32_t parent = parser->parent;
      parser->parent = pg_array_count(parser->kinds) - 1;
      const uint32_t me = parser->parent;

      const uint32_t prev_token_count = pg_array_count(parser->spans);

      while (pg_span32_peek(*input) != 'e' && pg_span32_peek(*input) != 0) {
        bc_parse_error_t err = bc_parse(parser, input);
        if (err != BC_PE_NONE) return err;
      }
      if (pg_span32_peek(*input) != 'e') return BC_PE_UNEXPECTED_CHARACTER;
      pg_span32_consume_left(input, 1);  // Skip 'e'

      assert(me < pg_array_count(parser->kinds));
      const uint32_t kv_count = parser->lengths[me];
      if (kv_count % 2 != 0) return BC_PE_INVALID_DICT;

      for (uint32_t i = prev_token_count; i < kv_count; i += 2) {
        if (parser->kinds[i] != BC_KIND_STRING) return BC_PE_INVALID_DICT;
      }

      parser->spans[prev_token_count - 1].len -= input->len;

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

void bc_dump_value_indent(FILE* f, uint64_t indent) {
  for (uint64_t i = 0; i < indent; i++) fprintf(f, " ");
}

uint32_t bc_dump_value(bc_parser_t* parser, FILE* f, uint64_t indent,
                       uint32_t index) {
  assert(index < pg_array_count(parser->kinds));

  const bc_kind_t kind = parser->kinds[index];
  const pg_span32_t span = parser->spans[index];
  const uint32_t len = parser->lengths[index];

  switch (kind) {
    case BC_KIND_INTEGER:
      fprintf(f, "%.*s", (int)span.len, span.data);
      return 1;
    case BC_KIND_STRING: {
      fprintf(f, "\"");
      for (uint32_t i = 0; i < span.len; i++) {
        uint8_t c = span.data[i];
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
      uint32_t j = index + 1;
      for (uint32_t i = 0; i < len; i++) {
        bc_dump_value_indent(f, indent + 2);
        j += bc_dump_value(parser, f, indent + 2, j);

        if (i < len - 1) fprintf(f, ",");

        fprintf(f, "\n");
      }
      bc_dump_value_indent(f, indent);
      fprintf(f, "]");

      return j - index;
    }
    case BC_KIND_DICTIONARY: {
      fprintf(f, "{\n");
      uint32_t j = index + 1;
      for (uint32_t i = 0; i < len; i += 2) {
        bc_dump_value_indent(f, indent + 2);

        j += bc_dump_value(parser, f, indent + 2, j);

        fprintf(f, ": ");
        j += bc_dump_value(parser, f, indent + 2, j);
        if (i < len - 2) fprintf(f, ",");

        fprintf(f, "\n");
      }
      bc_dump_value_indent(f, indent);
      fprintf(f, "}");

      return j - index;
    }
    default:
      __builtin_unreachable();
  }
  __builtin_unreachable();
}

void bc_dump_values(bc_parser_t* parser, FILE* f, uint64_t indent) {
  assert(pg_array_count(parser->spans) == pg_array_count(parser->lengths));
  assert(pg_array_count(parser->lengths) == pg_array_count(parser->kinds));

  bc_dump_value(parser, f, indent, 0);
}

typedef struct {
  pg_span32_t announce;
  uint32_t piece_length;
  uint64_t length;
  pg_span32_t name;
  pg_span32_t pieces;
} bc_metainfo_t;

typedef enum : uint8_t {
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

const char* bc_metainfo_error_to_string(int err) {
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
      __builtin_unreachable();
  }
}

bc_metainfo_error_t bc_parser_init_metainfo(bc_parser_t* parser,
                                            bc_metainfo_t* metainfo,
                                            pg_span32_t* info_span) {
  if (pg_array_count(parser->kinds) == 0) return BC_ME_METAINFO_NOT_DICTIONARY;
  if (parser->kinds[0] != BC_KIND_DICTIONARY)
    return BC_ME_METAINFO_NOT_DICTIONARY;

  uint32_t cur = 1;
  const pg_span32_t announce_key = pg_span32_make_c("announce");
  const pg_span32_t info_key = pg_span32_make_c("info");
  const uint32_t root_len = parser->lengths[0];

  for (uint32_t i = 0; i < root_len; i += 2) {
    bc_kind_t key_kind = parser->kinds[cur + i];
    pg_span32_t key_span = parser->spans[cur + i];
    bc_kind_t value_kind = parser->kinds[cur + i + 1];
    pg_span32_t value_span = parser->spans[cur + i + 1];

    if (key_kind == BC_KIND_STRING && pg_span32_eq(announce_key, key_span) &&
        value_kind == BC_KIND_STRING) {
      metainfo->announce = value_span;
    } else if (key_kind == BC_KIND_STRING && pg_span32_eq(info_key, key_span) &&
               value_kind == BC_KIND_DICTIONARY) {
      const uint32_t info_len = parser->lengths[cur + i + 1];
      *info_span = value_span;

      const pg_span32_t piece_length_key = pg_span32_make_c("piece length");
      const pg_span32_t name_key = pg_span32_make_c("name");
      const pg_span32_t length_key = pg_span32_make_c("length");
      const pg_span32_t pieces_key = pg_span32_make_c("pieces");

      for (uint32_t j = 0; j < info_len; j += 2) {
        key_kind = parser->kinds[cur + i + 2 + j];
        key_span = parser->spans[cur + i + 2 + j];
        value_kind = parser->kinds[cur + i + 2 + j + 1];
        value_span = parser->spans[cur + i + 2 + j + 1];

        if (key_kind == BC_KIND_STRING &&
            pg_span32_eq(piece_length_key, key_span) &&
            value_kind == BC_KIND_INTEGER) {
          metainfo->piece_length = pg_span32_parse_u64(value_span);
          if (metainfo->piece_length == 0)
            return BC_ME_PIECE_LENGTH_INVALID_VALUE;
        } else if (key_kind == BC_KIND_STRING &&
                   pg_span32_eq(name_key, key_span) &&
                   value_kind == BC_KIND_STRING) {
          // TODO: more validation
          if (value_span.len == 0) return BC_ME_NAME_INVALID_VALUE;

          metainfo->name = value_span;
        } else if (key_kind == BC_KIND_STRING &&
                   pg_span32_eq(length_key, key_span) &&
                   value_kind == BC_KIND_INTEGER) {
          metainfo->length = pg_span32_parse_u64(value_span);
          if (metainfo->length == 0) return BC_ME_LENGTH_INVALID_VALUE;
        } else if (key_kind == BC_KIND_STRING &&
                   pg_span32_eq(pieces_key, key_span) &&
                   value_kind == BC_KIND_STRING) {
          if (value_span.len == 0 || value_span.len % 20 != 0)
            return BC_ME_PIECES_INVALID_VALUE;
          metainfo->pieces = value_span;
        }
      }
    }
  }

  if (metainfo->announce.len == 0) return BC_ME_ANNOUNCE_NOT_FOUND;
  if (metainfo->length == 0) return BC_ME_LENGTH_NOT_FOUND;
  if (metainfo->piece_length == 0) return BC_ME_PIECE_LENGTH_NOT_FOUND;
  if (metainfo->pieces.len == 0) return BC_ME_PIECES_NOT_FOUND;
  if (metainfo->name.len == 0) return BC_ME_NAME_NOT_FOUND;

  return BC_ME_NONE;
}
