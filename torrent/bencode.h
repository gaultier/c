#pragma once

#include <_types/_uint64_t.h>
#include <sys/_types/_int64_t.h>

#include "../pg/pg.h"

typedef enum {
  BC_KIND_INTEGER,
  BC_KIND_STRING,
  BC_KIND_ARRAY,
  BC_KIND_OBJECT,
} bc_kind_t;

typedef struct bc_value_t bc_value_t;

PG_HASHTABLE(pg_string_t, bc_dictionary_t);

struct bc_value_t {
  bc_kind_t kind;
  union {
    int64_t integer;
    pg_string_t string;
    pg_array_t(bc_value_t) array;
    bc_dictionary_t object;
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
    default:
      __builtin_unreachable();
  }
}

bc_parse_error_t bc_consume_char(pg_string_span_t* span, char c) {
  assert(span != NULL);
  assert(span->data != NULL);
  assert(span->len > 0);

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
  if (span->data[0] == '-') *res *= -1;

  pg_span_consume(span, i);

  return BC_PE_NONE;
}

bc_parse_error_t bc_parse_string(pg_allocator_t allocator,
                                 pg_string_span_t* span, pg_string_t* res) {
  bc_parse_error_t err = BC_PE_NONE;
  int64_t len = 0;
  if ((err = bc_parse_i64(span, &len)) != BC_PE_NONE) return err;
  if ((err = bc_consume_char(span, ':')) != BC_PE_NONE) return err;
  if (len <= 0 || (uint64_t)len > span->len) return BC_PE_INVALID_STRING_LENGTH;

  *res = pg_string_make_length(allocator, span->data, len);

  return BC_PE_NONE;
}
