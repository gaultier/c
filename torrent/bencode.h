#pragma once

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
  BC_PE_UNEXPECTED_CHARACTER
} bc_parse_error_t;

bc_parse_error_t bc_consume_rune(pg_string_span_t* span, char c) {
  if (span->len == 0) return BC_PE_EOF;
  if (span->data[0] != c) return BC_PE_UNEXPECTED_CHARACTER;
  pg_span_consume(span);

  return BC_PE_NONE;
}
