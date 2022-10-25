#pragma once

#include "../pg/pg.h"

typedef enum {
  BC_KIND_INTEGER,
  BC_KIND_STRING,
  BC_KIND_ARRAY,
  BC_KIND_DICTIONARY,
} bc_kind_t;

const char* bc_value_kind_to_string(int n) {
  switch (n) {
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

typedef struct bc_value_t bc_value_t;

PG_HASHTABLE(pg_string_t, bc_value_t, bc_dictionary_t);

struct bc_value_t {
  bc_kind_t kind;
  union {
    int64_t integer;
    pg_string_t string;
    pg_array_t(bc_value_t) array;
    bc_dictionary_t dictionary;
  } v;
};

char bc_peek(pg_string_span_t span) {
  if (span.len > 0)
    return span.data[0];
  else
    return 0;
}

typedef enum {
  BC_PE_NONE,
  BC_PE_EOF,
  BC_PE_UNEXPECTED_CHARACTER,
  BC_PE_INVALID_NUMBER,
  BC_PE_INVALID_STRING_LENGTH,
  BC_PE_DICT_KEY_NOT_STRING,
} bc_parse_error_t;

const char* bc_parse_error_to_string(int e) {
  switch (e) {
    case BC_PE_NONE:
      return "BC_PE_NONE";
    case BC_PE_EOF:
      return "BC_PE_EOF";
    case BC_PE_UNEXPECTED_CHARACTER:
      return "BC_PE_UNEXPECTED_CHARACTER";
    case BC_PE_INVALID_NUMBER:
      return "BC_PE_INVALID_NUMBER";
    case BC_PE_INVALID_STRING_LENGTH:
      return "BC_PE_INVALID_STRING_LENGTH";
    case BC_PE_DICT_KEY_NOT_STRING:
      return "BC_PE_DICT_KEY_NOT_STRING";
    default:
      __builtin_unreachable();
  }
}

bc_parse_error_t bc_consume_char(pg_string_span_t* span, char c) {
  assert(span != NULL);
  assert(span->data != NULL);

  if (span->len == 0) return BC_PE_EOF;
  if (span->data[0] != c) return BC_PE_UNEXPECTED_CHARACTER;
  pg_span_consume(span, 1);

  return BC_PE_NONE;
}

bc_parse_error_t bc_parse_i64(pg_string_span_t* span, int64_t* res) {
  assert(span != NULL);
  assert(span->data != NULL);

  if (span->len == 0) return BC_PE_EOF;

  uint64_t i = 0;
  for (; i < span->len; i++) {
    const char c = span->data[i];
    if ('0' <= c && c <= '9') {
      *res *= 10;
      *res += c - '0';
    } else if (c == '-' && i == 0) {
      continue;
    } else {
      break;
    }
  }
  if (i == 1 && span->data[0] == '-') return BC_PE_INVALID_NUMBER;
  if (i == 0) return BC_PE_INVALID_NUMBER;
  if (span->data[0] == '-') *res *= -1;

  pg_span_consume(span, i);

  return BC_PE_NONE;
}

bc_parse_error_t bc_parse_string(pg_allocator_t allocator,
                                 pg_string_span_t* span, bc_value_t* value) {
  bc_parse_error_t err = BC_PE_NONE;
  int64_t len = 0;
  pg_string_span_t res_span = *span;
  if ((err = bc_parse_i64(&res_span, &len)) != BC_PE_NONE) return err;
  if ((err = bc_consume_char(&res_span, ':')) != BC_PE_NONE) return err;
  if (len <= 0 || (uint64_t)len > res_span.len)
    return BC_PE_INVALID_STRING_LENGTH;

  value->kind = BC_KIND_STRING;
  value->v.string = pg_string_make_length(allocator, res_span.data, len);

  pg_span_consume(&res_span, len);

  *span = res_span;

  return BC_PE_NONE;
}

bc_parse_error_t bc_parse_number(pg_string_span_t* span, bc_value_t* res) {
  bc_parse_error_t err = BC_PE_NONE;
  pg_string_span_t res_span = *span;

  if ((err = bc_consume_char(&res_span, 'i')) != BC_PE_NONE) return err;
  int64_t val = 0;
  if ((err = bc_parse_i64(&res_span, &val)) != BC_PE_NONE) return err;
  if ((err = bc_consume_char(&res_span, 'e')) != BC_PE_NONE) return err;

  res->kind = BC_KIND_INTEGER;
  res->v.integer = val;

  *span = res_span;
  return BC_PE_NONE;
}

void bc_value_destroy(bc_value_t* value) {
  switch (value->kind) {
    case BC_KIND_STRING:
      pg_string_free(value->v.string);
      break;
    case BC_KIND_DICTIONARY:
      // TODO
      break;
    case BC_KIND_ARRAY:
      for (uint64_t i = 0; i < pg_array_count(value->v.array); i++)
        bc_value_destroy(&value->v.array[i]);

      pg_array_free(value->v.array);
      break;
    default:
      __builtin_unreachable();
  }
}

bc_parse_error_t bc_parse_value(pg_allocator_t allocator,
                                pg_string_span_t* span, bc_value_t* res);

bc_parse_error_t bc_parse_array(pg_allocator_t allocator,
                                pg_string_span_t* span, bc_value_t* res) {
  bc_parse_error_t err = BC_PE_NONE;
  pg_string_span_t res_span = *span;

  if ((err = bc_consume_char(&res_span, 'l')) != BC_PE_NONE) return err;

  pg_array_t(bc_value_t) values = {0};
  pg_array_init_reserve(values, 8, allocator);

  for (uint64_t i = 0; i < res_span.len; i++) {
    const char c = bc_peek(res_span);
    if (c == 0) {
      err = BC_PE_EOF;
      goto fail;
    }
    if (c == 'e') break;

    bc_value_t value = {0};

    if ((err = bc_parse_value(allocator, &res_span, &value)) != BC_PE_NONE)
      goto fail;

    pg_array_append(values, value);
  }

  if ((err = bc_consume_char(&res_span, 'e')) != BC_PE_NONE) goto fail;

  res->kind = BC_KIND_ARRAY;
  res->v.array = values;

  *span = res_span;

  return BC_PE_NONE;

fail:
  for (uint64_t i = 0; i < pg_array_count(values); i++)
    bc_value_destroy(&values[i]);
  pg_array_free(values);
  return err;
}

bc_parse_error_t bc_parse_dictionary(pg_allocator_t allocator,
                                     pg_string_span_t* span, bc_value_t* res) {
  bc_parse_error_t err = BC_PE_NONE;
  pg_string_span_t res_span = *span;

  if ((err = bc_consume_char(&res_span, 'd')) != BC_PE_NONE) return err;

  bc_dictionary_t dict = {0};
  pg_hashtable_init(dict, 5, allocator);

  for (uint64_t i = 0; i < res_span.len; i++) {
    const char c = bc_peek(res_span);
    if (c == 0) {
      err = BC_PE_EOF;
      goto fail;
    }
    if (c == 'e') break;

    bc_value_t key = {0};
    if ((err = bc_parse_value(allocator, &res_span, &key)) != BC_PE_NONE)
      goto fail;

    if (key.kind != BC_KIND_STRING) {
      err = BC_PE_DICT_KEY_NOT_STRING;
      goto fail;
    }

    bc_value_t value = {0};
    if ((err = bc_parse_value(allocator, &res_span, &value)) != BC_PE_NONE)
      goto fail;

    pg_hashtable_upsert(dict, key.v.string, value);
  }
  if ((err = bc_consume_char(&res_span, 'e')) != BC_PE_NONE) goto fail;

  res->kind = BC_KIND_DICTIONARY;
  res->v.dictionary = dict;

  *span = res_span;

  return BC_PE_NONE;

fail:
  pg_hashtable_destroy(dict, pg_string_free_ptr, bc_value_destroy);
  return err;
}

bc_parse_error_t bc_parse_value(pg_allocator_t allocator,
                                pg_string_span_t* span, bc_value_t* res) {
  const char c = bc_peek(*span);
  if (c == 'i')
    return bc_parse_number(span, res);
  else if (c == 'l')
    return bc_parse_array(allocator, span, res);
  else if (c == 'd')
    return bc_parse_dictionary(allocator, span, res);
  else
    return bc_parse_string(allocator, span, res);
}
