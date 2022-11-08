#pragma once

#include <_types/_uint64_t.h>
#include <stdio.h>
#include <sys/types.h>

#include "../pg/pg.h"

typedef enum {
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
  pg_array_t(pg_span_t) tokens;
  pg_array_t(uint64_t) lengths;
  pg_array_t(bc_kind_t) kinds;
} bc_parser_t;

void bc_parser_init(pg_allocator_t allocator, bc_parser_t* parser,
                    uint64_t expected_token_counts) {
  pg_array_init_reserve(parser->tokens, expected_token_counts, allocator);
  pg_array_init_reserve(parser->lengths, expected_token_counts, allocator);
  pg_array_init_reserve(parser->kinds, expected_token_counts, allocator);
}

void bc_parser_destroy(bc_parser_t* parser) {
  pg_array_free(parser->tokens);
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

bc_parse_error_t bc_parse(bc_parser_t* parser, pg_span_t* input) {
  assert(pg_array_count(parser->tokens) == pg_array_count(parser->lengths));
  assert(pg_array_count(parser->lengths) == pg_array_count(parser->kinds));

  const char c = pg_peek(*input);

  switch (c) {
    case 'i': {
      pg_span_t left = {0}, right = {0};
      const bool found = pg_span_split(*input, 'e', &left, &right);
      if (!found) return BC_PE_INVALID_NUMBER;

      assert(left.len >= 1);

      if (left.len == 1) return BC_PE_INVALID_NUMBER;  // `ie`
      pg_span_consume_left(&left, 1);                  // Skip 'i'

      assert(left.len > 0);

      if (left.data[0] == '-' && left.len == 1)
        return BC_PE_INVALID_NUMBER;  // `i-e`

      if (!(pg_char_is_digit(left.data[0]) ||
            left.data[0] == '-'))  // `iae` or `i-e`
        return BC_PE_INVALID_NUMBER;

      pg_span_consume_left(&right, 1);  // Skip 'e'

      for (uint64_t i = 1; i < left.len; i++) {
        if (!pg_char_is_digit(left.data[i])) return BC_PE_INVALID_NUMBER;
      }

      pg_array_append(parser->tokens, left);
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
      pg_span_t left = {0}, right = {0};
      const bool found = pg_span_split(*input, ':', &left, &right);
      if (!found) return BC_PE_INVALID_STRING;

      assert(left.len >= 1);

      pg_span_consume_left(&right, 1);  // Skip ':'

      uint64_t len = 0;
      for (uint64_t i = 0; i < left.len; i++) {
        if (!pg_char_is_digit(left.data[i])) return BC_PE_INVALID_STRING;
        len *= 10;
        len += left.data[0] - '0';
      }
      assert(len > 0);
      if (right.len < len) return BC_PE_INVALID_STRING;  // `5:a`

      pg_span_t string = {.data = right.data, .len = len};
      pg_array_append(parser->tokens, string);
      pg_array_append(parser->lengths, len);
      pg_array_append(parser->kinds, BC_KIND_STRING);

      *input = right;
      pg_span_consume_left(input, len);  // Skip over string content
      break;
    }
    case 'l': {
      pg_span_consume_left(input, 1);  // Skip 'l'

      pg_array_append(parser->tokens, (pg_span_t){0});  // Does not matter
      pg_array_append(parser->lengths, 0);  // Will be patched at the end
      pg_array_append(parser->kinds, BC_KIND_ARRAY);

      const uint64_t prev_token_count = pg_array_count(parser->tokens);

      while (pg_peek(*input) != 'e' && pg_peek(*input) != 0) {
        bc_parse_error_t err = bc_parse(parser, input);
        if (err != BC_PE_NONE) return err;
      }
      if (pg_peek(*input) != 'e') return BC_PE_UNEXPECTED_CHARACTER;
      pg_span_consume_left(input, 1);  // Skip 'e'

      parser->lengths[prev_token_count - 1] =
          pg_array_count(parser->tokens) - prev_token_count;
      break;
    }
    case 'd': {
      const pg_span_t original = *input;

      pg_span_consume_left(input, 1);  // Skip 'l'

      pg_array_append(parser->tokens, original);  // Will be patched at the end
      pg_array_append(parser->lengths, 0);        // Will be patched at the end
      pg_array_append(parser->kinds, BC_KIND_DICTIONARY);

      const uint64_t prev_token_count = pg_array_count(parser->tokens);

      while (pg_peek(*input) != 'e' && pg_peek(*input) != 0) {
        bc_parse_error_t err = bc_parse(parser, input);
        if (err != BC_PE_NONE) return err;
      }
      if (pg_peek(*input) != 'e') return BC_PE_UNEXPECTED_CHARACTER;
      pg_span_consume_left(input, 1);  // Skip 'e'

      const uint64_t kv_count =
          pg_array_count(parser->tokens) - prev_token_count;
      if (kv_count % 2 != 0) return BC_PE_INVALID_DICT;

      for (uint64_t i = prev_token_count; i < kv_count; i += 2) {
        if (parser->kinds[i] != BC_KIND_STRING) return BC_PE_INVALID_DICT;
      }

      parser->tokens[prev_token_count - 1].len -= input->len;
      parser->lengths[prev_token_count - 1] = kv_count;

      break;
    }
    case 0:
      return BC_PE_NONE;

    default:
      return BC_PE_UNEXPECTED_CHARACTER;  // FIXME
  }
  return BC_PE_NONE;
}

// bc_parse_error_t bc_parse_i64(pg_span_t* span, int64_t* res) {
//   assert(span != NULL);
//   assert(span->data != NULL);
//
//   if (span->len == 0) return BC_PE_EOF;
//
//   uint64_t i = 0;
//   for (; i < span->len; i++) {
//     const char c = span->data[i];
//     if ('0' <= c && c <= '9') {
//       *res *= 10;
//       *res += c - '0';
//     } else if (c == '-' && i == 0) {
//       continue;
//     } else {
//       break;
//     }
//   }
//   if (i == 1 && span->data[0] == '-') return BC_PE_INVALID_NUMBER;
//   if (i == 0) return BC_PE_INVALID_NUMBER;
//   if (span->data[0] == '-') *res *= -1;
//
//   pg_span_consume(span, i);
//
//   return BC_PE_NONE;
// }
//
// bc_parse_error_t bc_parse_string(pg_allocator_t allocator, pg_span_t* span,
//                                  bc_value_t* value) {
//   assert(span != NULL);
//   assert(value != NULL);
//
//   bc_parse_error_t err = BC_PE_NONE;
//   int64_t len = 0;
//   pg_span_t res_span = *span;
//   if ((err = bc_parse_i64(&res_span, &len)) != BC_PE_NONE) return err;
//   if ((err = bc_consume_char(&res_span, ':')) != BC_PE_NONE) return err;
//   if (len <= 0 || (uint64_t)len > res_span.len)
//     return BC_PE_INVALID_STRING_LENGTH;
//
//   value->kind = BC_KIND_STRING;
//   value->v.string = pg_string_make_length(allocator, res_span.data, len);
//
//   pg_span_consume(&res_span, len);
//
//   *span = res_span;
//
//   return BC_PE_NONE;
// }
//
// bc_parse_error_t bc_parse_number(pg_span_t* span, bc_value_t* value) {
//   assert(span != NULL);
//   assert(value != NULL);
//
//   bc_parse_error_t err = BC_PE_NONE;
//   pg_span_t res_span = *span;
//
//   if ((err = bc_consume_char(&res_span, 'i')) != BC_PE_NONE) return err;
//   int64_t val = 0;
//   if ((err = bc_parse_i64(&res_span, &val)) != BC_PE_NONE) return err;
//   if ((err = bc_consume_char(&res_span, 'e')) != BC_PE_NONE) return err;
//
//   value->kind = BC_KIND_INTEGER;
//   value->v.integer = val;
//
//   *span = res_span;
//   return BC_PE_NONE;
// }
//
// void bc_value_destroy(bc_value_t* value) {
//   switch (value->kind) {
//     case BC_KIND_INTEGER: {
//       // No-op
//       break;
//     }
//     case BC_KIND_STRING:
//       pg_string_free(value->v.string);
//       break;
//     case BC_KIND_DICTIONARY:
//       pg_hashtable_destroy(&value->v.dictionary);
//       break;
//     case BC_KIND_ARRAY:
//       for (uint64_t i = 0; i < pg_array_count(value->v.array); i++)
//         bc_value_destroy(&value->v.array[i]);
//
//       pg_array_free(value->v.array);
//       break;
//     default:
//       __builtin_unreachable();
//   }
// }
//
// bc_parse_error_t bc_parse_value(pg_allocator_t allocator, pg_span_t* span,
//                                 bc_value_t* value, pg_span_t* info_span);
//
// bc_parse_error_t bc_parse_array(pg_allocator_t allocator, pg_span_t* span,
//                                 bc_value_t* value, pg_span_t* info_span) {
//   assert(span != NULL);
//   assert(value != NULL);
//   assert(info_span != NULL);
//
//   bc_parse_error_t err = BC_PE_NONE;
//   pg_span_t res_span = *span;
//
//   if ((err = bc_consume_char(&res_span, 'l')) != BC_PE_NONE) return err;
//
//   pg_array_t(bc_value_t) values = {0};
//   pg_array_init_reserve(values, 20, allocator);
//
//   for (uint64_t i = 0; i < res_span.len; i++) {
//     const char c = pg_peek(res_span);
//     if (c == 0) {
//       err = BC_PE_EOF;
//       goto fail;
//     }
//     if (c == 'e') break;
//
//     bc_value_t value = {0};
//
//     if ((err = bc_parse_value(allocator, &res_span, &value, info_span)) !=
//         BC_PE_NONE)
//       goto fail;
//
//     pg_array_append(values, value);
//   }
//
//   if ((err = bc_consume_char(&res_span, 'e')) != BC_PE_NONE) goto fail;
//
//   value->kind = BC_KIND_ARRAY;
//   value->v.array = values;
//
//   *span = res_span;
//
//   return BC_PE_NONE;
//
// fail:
//   for (uint64_t i = 0; i < pg_array_count(values); i++)
//     bc_value_destroy(&values[i]);
//   pg_array_free(values);
//   return err;
// }
//
// bc_parse_error_t bc_parse_dictionary(pg_allocator_t allocator, pg_span_t*
// span,
//                                      bc_value_t* value, pg_span_t* info_span)
//                                      {
//   assert(span != NULL);
//   assert(value != NULL);
//   assert(info_span != NULL);
//
//   bc_parse_error_t err = BC_PE_NONE;
//   pg_span_t res_span = *span;
//
//   if ((err = bc_consume_char(&res_span, 'd')) != BC_PE_NONE) return err;
//
//   bc_dictionary_t dict = {0};
//   pg_hashtable_init(&dict, 30, allocator);
//
//   for (uint64_t i = 0; i < res_span.len; i++) {
//     const char c = pg_peek(res_span);
//     if (c == 0) {
//       err = BC_PE_EOF;
//       goto fail;
//     }
//     if (c == 'e') break;
//
//     bc_value_t key = {0};
//     if ((err = bc_parse_value(allocator, &res_span, &key, info_span)) !=
//         BC_PE_NONE)
//       goto fail;
//
//     if (key.kind != BC_KIND_STRING) {
//       err = BC_PE_DICT_KEY_NOT_STRING;
//       goto fail;
//     }
//
//     // TODO: check that depth == 1?
//     const char info_key[] = "info";
//     if (pg_string_length(key.v.string) == sizeof(info_key) - 1 &&
//         memcmp(key.v.string, info_key, pg_string_length(key.v.string)) == 0)
//         {
//       *info_span = res_span;
//     }
//
//     bc_value_t value = {0};
//     if ((err = bc_parse_value(allocator, &res_span, &value, info_span)) !=
//         BC_PE_NONE)
//       goto fail;
//
//     if (value.kind == BC_KIND_DICTIONARY) {
//       info_span->len -= res_span.len;
//       assert(info_span->data[0] == 'd');
//       assert(info_span->data[info_span->len - 1] == 'e');
//     }
//
//     pg_hashtable_upsert(&dict, key.v.string, &value);
//   }
//   if ((err = bc_consume_char(&res_span, 'e')) != BC_PE_NONE) goto fail;
//
//   value->kind = BC_KIND_DICTIONARY;
//   value->v.dictionary = dict;
//
//   *span = res_span;
//
//   return BC_PE_NONE;
//
// fail:
//   pg_hashtable_destroy(&dict);
//   return err;
// }
//
// bc_parse_error_t bc_parse_value(pg_allocator_t allocator, pg_span_t* span,
//                                 bc_value_t* value, pg_span_t* info_span) {
//   assert(span != NULL);
//   assert(value != NULL);
//   assert(info_span != NULL);
//
//   const char c = pg_peek(*span);
//   if (c == 'i')
//     return bc_parse_number(span, value);
//   else if (c == 'l')
//     return bc_parse_array(allocator, span, value, info_span);
//   else if (c == 'd')
//     return bc_parse_dictionary(allocator, span, value, info_span);
//   else
//     return bc_parse_string(allocator, span, value);
// }
//
// void bc_value_dump_indent(FILE* f, uint64_t indent) {
//   for (uint64_t i = 0; i < indent; i++) fprintf(f, " ");
// }
//
// void bc_value_dump(bc_value_t* value, FILE* f, uint64_t indent) {
//   switch (value->kind) {
//     case BC_KIND_INTEGER:
//       fprintf(f, "%lld", value->v.integer);
//       break;
//     case BC_KIND_STRING: {
//       fprintf(f, "\"");
//       for (uint64_t i = 0; i < pg_string_length(value->v.string); i++) {
//         uint8_t c = (uint8_t)value->v.string[i];
//         if (32 <= c && c < 127)
//           fprintf(f, "%c", c);
//         else
//           fprintf(f, "\\u%04x", c);
//       }
//       fprintf(f, "\"");
//
//       break;
//     }
//     case BC_KIND_ARRAY:
//       fprintf(f, "[\n");
//       for (uint64_t i = 0; i < pg_array_count(value->v.array); i++) {
//         bc_value_dump_indent(f, indent + 2);
//         bc_value_dump(&value->v.array[i], f, indent + 2);
//         if (i < pg_array_count(value->v.array) - 1) fprintf(f, ",");
//         fprintf(f, "\n");
//       }
//       bc_value_dump_indent(f, indent);
//       fprintf(f, "]");
//       break;
//     case BC_KIND_DICTIONARY: {
//       fprintf(f, "{\n");
//
//       bc_dictionary_t* dict = &value->v.dictionary;
//
//       uint64_t count = 0;
//       for (uint64_t i = 0; i < pg_array_capacity(dict->keys); i++) {
//         if (dict->hashes[i] == 0) {
//           continue;
//         }
//         count++;
//
//         bc_value_dump_indent(f, indent + 2);
//         fprintf(f, "\"%s\": ", dict->keys[i]);
//         bc_value_dump(&dict->values[i], f, indent + 2);
//         if (count < pg_hashtable_count(dict)) fprintf(f, ",");
//
//         fprintf(f, "\n");
//       }
//
//       bc_value_dump_indent(f, indent);
//       fprintf(f, "}");
//       break;
//     }
//     default:
//       __builtin_unreachable();
//   }
// }
//
// typedef struct {
//   pg_string_t announce;
//   uint64_t piece_length;
//   uint64_t length;
//   pg_string_t name;
//   pg_array_t(uint8_t) pieces;
// } bc_metainfo_t;
//
// typedef enum {
//   BC_MI_NONE,
//   BC_MI_METAINFO_NOT_DICTIONARY,
//   BC_ME_ANNOUNCE_NOT_FOUND,
//   BC_ME_ANNOUNCE_INVALID_KIND,
//   BC_ME_INFO_NOT_FOUND,
//   BC_ME_INFO_INVALID_KIND,
//   BC_ME_PIECE_LENGTH_NOT_FOUND,
//   BC_ME_PIECE_LENGTH_INVALID_KIND,
//   BC_ME_PIECE_LENGTH_INVALID_VALUE,
//   BC_ME_NAME_NOT_FOUND,
//   BC_ME_NAME_INVALID_KIND,
//   BC_ME_NAME_INVALID_VALUE,
//   BC_ME_LENGTH_NOT_FOUND,
//   BC_ME_LENGTH_INVALID_KIND,
//   BC_ME_LENGTH_INVALID_VALUE,
//   BC_ME_PIECES_NOT_FOUND,
//   BC_ME_PIECES_INVALID_KIND,
//   BC_ME_PIECES_INVALID_VALUE,
// } bc_metainfo_error_t;
//
// const char* bc_metainfo_error_to_string(int err) {
//   switch (err) {
//     case BC_MI_NONE:
//       return "BC_MI_NONE";
//     case BC_MI_METAINFO_NOT_DICTIONARY:
//       return "BC_MI_METAINFO_NOT_DICTIONARY";
//     case BC_ME_ANNOUNCE_NOT_FOUND:
//       return "BC_ME_ANNOUNCE_NOT_FOUND";
//     case BC_ME_ANNOUNCE_INVALID_KIND:
//       return "BC_ME_ANNOUNCE_INVALID_KIND";
//     case BC_ME_INFO_NOT_FOUND:
//       return "BC_ME_INFO_NOT_FOUND";
//     case BC_ME_INFO_INVALID_KIND:
//       return "BC_ME_INFO_INVALID_KIND";
//     case BC_ME_PIECE_LENGTH_NOT_FOUND:
//       return "BC_ME_PIECE_LENGTH_NOT_FOUND";
//     case BC_ME_PIECE_LENGTH_INVALID_KIND:
//       return "BC_ME_PIECE_LENGTH_INVALID_KIND";
//     case BC_ME_PIECE_LENGTH_INVALID_VALUE:
//       return "BC_ME_PIECE_LENGTH_INVALID_VALUE";
//     case BC_ME_NAME_NOT_FOUND:
//       return "BC_ME_NAME_NOT_FOUND";
//     case BC_ME_NAME_INVALID_KIND:
//       return "BC_ME_NAME_INVALID_KIND";
//     case BC_ME_NAME_INVALID_VALUE:
//       return "BC_ME_NAME_INVALID_VALUE";
//     case BC_ME_LENGTH_NOT_FOUND:
//       return "BC_ME_LENGTH_NOT_FOUND";
//     case BC_ME_LENGTH_INVALID_KIND:
//       return "BC_ME_LENGTH_INVALID_KIND";
//     case BC_ME_LENGTH_INVALID_VALUE:
//       return "BC_ME_LENGTH_INVALID_VALUE";
//     case BC_ME_PIECES_NOT_FOUND:
//       return "BC_ME_PIECES_NOT_FOUND";
//     case BC_ME_PIECES_INVALID_KIND:
//       return "BC_ME_PIECES_INVALID_KIND";
//     case BC_ME_PIECES_INVALID_VALUE:
//       return "BC_ME_PIECES_INVALID_VALUE";
//     default:
//       __builtin_unreachable();
//   }
// }
//
// void bc_metainfo_destroy(bc_metainfo_t* metainfo) {
//   if (metainfo->pieces != NULL) pg_array_free(metainfo->pieces);
//   if (metainfo->name != NULL) pg_string_free(metainfo->name);
// }
//
// bc_metainfo_error_t bc_metainfo_init_from_value(pg_allocator_t allocator,
//                                                 bc_value_t* val,
//                                                 bc_metainfo_t* metainfo) {
//   bc_metainfo_error_t err = BC_MI_NONE;
//
//   if (val->kind != BC_KIND_DICTIONARY) {
//     err = BC_MI_METAINFO_NOT_DICTIONARY;
//     goto end;
//   }
//   bc_dictionary_t* root = &val->v.dictionary;
//
//   // Announce
//   {
//     uint64_t index = -1;
//     pg_span_t announce_key = pg_span_make_c("announce");
//     if (!pg_hashtable_find(root, announce_key, &index)) {
//       err = BC_ME_ANNOUNCE_NOT_FOUND;
//       goto end;
//     }
//
//     bc_value_t* announce_value = &root->values[index];
//     if (announce_value->kind != BC_KIND_STRING) {
//       err = BC_ME_ANNOUNCE_INVALID_KIND;
//       goto end;
//     }
//
//     metainfo->announce = pg_string_make(allocator, announce_value->v.string);
//   }
//
//   // Info
//   {
//     uint64_t index = -1;
//     pg_span_t info_key = pg_span_make_c("info");
//     if (!pg_hashtable_find(root, info_key, &index)) {
//       err = BC_ME_INFO_NOT_FOUND;
//       goto end;
//     }
//
//     bc_value_t* info_value = &root->values[index];
//     if (info_value->kind != BC_KIND_DICTIONARY) {
//       err = BC_ME_INFO_INVALID_KIND;
//       goto end;
//     }
//
//     bc_dictionary_t* info = &info_value->v.dictionary;
//
//     // Piece length
//     {
//       index = -1;
//       pg_span_t piece_length_key = pg_span_make_c("piece length");
//       if (!pg_hashtable_find(info, piece_length_key, &index)) {
//         err = BC_ME_PIECE_LENGTH_NOT_FOUND;
//         goto end;
//       }
//
//       bc_value_t* piece_length_value = &info->values[index];
//       if (piece_length_value->kind != BC_KIND_INTEGER) {
//         err = BC_ME_PIECE_LENGTH_INVALID_KIND;
//         goto end;
//       }
//
//       if (piece_length_value->v.integer <= 0) {
//         err = BC_ME_PIECE_LENGTH_INVALID_VALUE;
//         goto end;
//       }
//
//       metainfo->piece_length = (uint64_t)piece_length_value->v.integer;
//     }
//
//     // Name
//     {
//       index = -1;
//       pg_span_t name_key = pg_span_make_c("name");
//       if (!pg_hashtable_find(info, name_key, &index)) {
//         err = BC_ME_NAME_NOT_FOUND;
//         goto end;
//       }
//
//       bc_value_t* name_value = &info->values[index];
//       if (name_value->kind != BC_KIND_STRING) {
//         err = BC_ME_NAME_INVALID_KIND;
//         goto end;
//       }
//
//       pg_string_t s = name_value->v.string;
//       // TODO: revisit when we parse `path`
//       if (pg_string_length(s) == 0) {
//         err = BC_ME_NAME_INVALID_VALUE;
//         goto end;
//       }
//
//       metainfo->name = pg_string_make(allocator, s);
//     }
//     // Length
//     {
//       index = -1;
//       pg_span_t length_key = pg_span_make_c("length");
//       if (!pg_hashtable_find(info, length_key, &index)) {
//         err = BC_ME_LENGTH_NOT_FOUND;
//         goto end;
//       }
//
//       bc_value_t* length_value = &info->values[index];
//       if (length_value->kind != BC_KIND_INTEGER) {
//         err = BC_ME_LENGTH_INVALID_KIND;
//         goto end;
//       }
//
//       if (length_value->v.integer <= 0) {
//         err = BC_ME_LENGTH_INVALID_VALUE;
//         goto end;
//       }
//
//       metainfo->length = (uint64_t)length_value->v.integer;
//     }
//
//     // Pieces
//     {
//       index = -1;
//       pg_span_t pieces_key = pg_span_make_c("pieces");
//       if (!pg_hashtable_find(info, pieces_key, &index)) {
//         err = BC_ME_PIECES_NOT_FOUND;
//         goto end;
//       }
//
//       bc_value_t* pieces_value = &info->values[index];
//       if (pieces_value->kind != BC_KIND_STRING) {
//         err = BC_ME_PIECES_INVALID_KIND;
//         goto end;
//       }
//
//       pg_string_t s = pieces_value->v.string;
//       if (pg_string_length(s) == 0 || pg_string_length(s) % 20 != 0) {
//         err = BC_ME_PIECES_INVALID_VALUE;
//         goto end;
//       }
//       pg_array_init_reserve(metainfo->pieces, pg_string_length(s),
//       allocator); memcpy(metainfo->pieces, (uint8_t*)s, pg_string_length(s));
//       pg_array_resize(metainfo->pieces, pg_string_length(s));
//     }
//   }
//
// end:
//   if (err != BC_MI_NONE) bc_metainfo_destroy(metainfo);
//   return err;
// }
