#pragma once

typedef enum {
  BENCODE_KIND_INTEGER,
  BENCODE_KIND_STRING,
  BENCODE_KIND_ARRAY,
  BENCODE_KIND_OBJECT,
} bencode_kind_t;

typedef struct bencode_value_t bencode_value_t;
struct bencode_value_t {
  bencode_kind_t kind;
  union v {
    int64_t integer;
    pg_string_t string;
    pg_array_t(bencode_value_t) array;
    pg_hash_table_t(pg_string_t, bencode_value_t) object;
  };
};
