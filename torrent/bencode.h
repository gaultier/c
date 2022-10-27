#pragma once

#include <_types/_uint32_t.h>
#include <_types/_uint64_t.h>
#include <stdio.h>
#include <sys/types.h>

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

typedef struct {
  pg_array_t(pg_string_t) keys;
  pg_array_t(bc_value_t) values;
  pg_array_t(uint32_t) hashes;
  pg_allocator_t allocator;
} bc_dictionary_t;

struct bc_value_t {
  bc_kind_t kind;
  union {
    int64_t integer;
    pg_string_t string;
    pg_array_t(bc_value_t) array;
    bc_dictionary_t dictionary;
  } v;
};

void pg_hashtable_init(bc_dictionary_t* hashtable, uint64_t cap,
                       pg_allocator_t allocator) {
  assert(hashtable != NULL);
  if (cap < 2) cap = 2;

  hashtable->allocator = allocator;
  pg_array_init_reserve(hashtable->keys, cap, allocator);
  pg_array_init_reserve(hashtable->values, cap, allocator);
  pg_array_init_reserve(hashtable->hashes, cap, allocator);

  assert(hashtable->keys != NULL);
  assert(hashtable->values != NULL);
  assert(hashtable->hashes != NULL);
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->values));
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->hashes));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->values));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->hashes));
}

bool pg_hashtable_find(bc_dictionary_t* hashtable, pg_string_t key,
                       uint64_t* index) {
  assert(hashtable != NULL);
  assert(hashtable->keys != NULL);
  assert(hashtable->values != NULL);
  assert(hashtable->hashes != NULL);
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->values));
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->hashes));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->values));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->hashes));

  const uint32_t hash = pg_hash((uint8_t*)key, pg_string_length(key));
  *index = hash % pg_array_capacity(hashtable->keys);

  for (;;) {
    const uint32_t index_hash = hashtable->hashes[*index];
    if (index_hash == 0) return false; /* Not found but suitable empty slot */
    if (index_hash == hash &&
        pg_string_length(key) == pg_string_length(hashtable->keys[*index]) &&
        memcmp(key, hashtable->keys[*index], pg_string_length(key)) == 0) {
      /* Found after checking for collision */
      return true;
    }
    /* Keep going to find either an empty slot or a matching hash */
    *index = (*index + 1) % pg_array_capacity(hashtable->keys);
  }
  __builtin_unreachable();
}

#define PG_HASHTABLE_LOAD_FACTOR 0.75

void pg_hashtable_upsert(bc_dictionary_t* hashtable, pg_string_t key,
                         bc_value_t* val);
void pg_hashtable_grow(bc_dictionary_t* hashtable, uint64_t new_cap) {
  assert(hashtable != NULL);
  assert(hashtable->keys != NULL);
  assert(hashtable->values != NULL);
  assert(hashtable->hashes != NULL);
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->values));
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->hashes));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->values));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->hashes));

  bc_dictionary_t new_hashtable = {0};
  pg_hashtable_init(&new_hashtable, new_cap, hashtable->allocator);

  for (uint64_t i = 0; i < pg_array_capacity(hashtable->keys); i++) {
    if (hashtable->hashes[i] == 0) continue;
    pg_hashtable_upsert(&new_hashtable, hashtable->keys[i],
                        &hashtable->values[i]);
  }

  pg_array_free(hashtable->keys);
  pg_array_free(hashtable->values);
  pg_array_free(hashtable->hashes);

  memcpy(hashtable, &new_hashtable, sizeof(bc_dictionary_t));
}

void pg_hashtable_upsert(bc_dictionary_t* hashtable, pg_string_t key,
                         bc_value_t* val) {
  assert(hashtable != NULL);
  assert(hashtable->keys != NULL);
  assert(hashtable->values != NULL);
  assert(hashtable->hashes != NULL);
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->values));
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->hashes));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->values));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->hashes));

  const uint64_t cap = pg_array_capacity(hashtable->keys);
  assert(cap > 0);
  const uint64_t len = pg_array_count(hashtable->keys);
  if ((double)len / cap >= PG_HASHTABLE_LOAD_FACTOR) {
    const uint64_t new_cap = 1.5 * cap;
    pg_hashtable_grow(hashtable, new_cap);
  }
  uint64_t index = -1;
  if (pg_hashtable_find(hashtable, key, &index)) { /* Update */
    hashtable->values[index] = *val;
  } else {
    hashtable->keys[index] = key;
    hashtable->hashes[index] = pg_hash((uint8_t*)key, pg_string_length(key));
    hashtable->values[index] = *val;

    const uint64_t new_len = pg_array_count(hashtable->keys) + 1;
    pg_array_resize(hashtable->keys, new_len);
    pg_array_resize(hashtable->values, new_len);
    pg_array_resize(hashtable->hashes, new_len);
  }
}

void bc_value_destroy(bc_value_t* value);

void pg_hashtable_destroy(bc_dictionary_t* hashtable) {
  assert(hashtable != NULL);
  assert(hashtable->keys != NULL);
  assert(hashtable->values != NULL);
  assert(hashtable->hashes != NULL);
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->values));
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->hashes));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->values));
  assert(pg_array_count(hashtable->keys) == pg_array_count(hashtable->hashes));

  for (uint64_t i = 0; i < pg_array_capacity(hashtable->keys); i++) {
    if (hashtable->hashes[i] != 0) pg_string_free(hashtable->keys[i]);
  }
  pg_array_free(hashtable->keys);

  for (uint64_t i = 0; i < pg_array_capacity(hashtable->values); i++) {
    if (hashtable->hashes[i] != 0) bc_value_destroy(&hashtable->values[i]);
  }
  pg_array_free(hashtable->values);

  pg_array_free(hashtable->hashes);
}

uint64_t pg_hashtable_count(bc_dictionary_t* hashtable) {
  return pg_array_count(hashtable->keys);
}

char bc_peek(pg_span_t span) {
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

bc_parse_error_t bc_consume_char(pg_span_t* span, char c) {
  assert(span != NULL);
  assert(span->data != NULL);

  if (span->len == 0) return BC_PE_EOF;
  if (span->data[0] != c) return BC_PE_UNEXPECTED_CHARACTER;
  pg_span_consume(span, 1);

  return BC_PE_NONE;
}

bc_parse_error_t bc_parse_i64(pg_span_t* span, int64_t* res) {
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

bc_parse_error_t bc_parse_string(pg_allocator_t allocator, pg_span_t* span,
                                 bc_value_t* value) {
  bc_parse_error_t err = BC_PE_NONE;
  int64_t len = 0;
  pg_span_t res_span = *span;
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

bc_parse_error_t bc_parse_number(pg_span_t* span, bc_value_t* res) {
  bc_parse_error_t err = BC_PE_NONE;
  pg_span_t res_span = *span;

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
    case BC_KIND_INTEGER: {
      // No-op
      break;
    }
    case BC_KIND_STRING:
      pg_string_free(value->v.string);
      break;
    case BC_KIND_DICTIONARY:
      pg_hashtable_destroy(&value->v.dictionary);
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

bc_parse_error_t bc_parse_value(pg_allocator_t allocator, pg_span_t* span,
                                bc_value_t* res);

bc_parse_error_t bc_parse_array(pg_allocator_t allocator, pg_span_t* span,
                                bc_value_t* res) {
  bc_parse_error_t err = BC_PE_NONE;
  pg_span_t res_span = *span;

  if ((err = bc_consume_char(&res_span, 'l')) != BC_PE_NONE) return err;

  pg_array_t(bc_value_t) values = {0};
  pg_array_init_reserve(values, 20, allocator);

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

bc_parse_error_t bc_parse_dictionary(pg_allocator_t allocator, pg_span_t* span,
                                     bc_value_t* res) {
  bc_parse_error_t err = BC_PE_NONE;
  pg_span_t res_span = *span;

  if ((err = bc_consume_char(&res_span, 'd')) != BC_PE_NONE) return err;

  bc_dictionary_t dict = {0};
  pg_hashtable_init(&dict, 30, allocator);

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

    pg_hashtable_upsert(&dict, key.v.string, &value);
  }
  if ((err = bc_consume_char(&res_span, 'e')) != BC_PE_NONE) goto fail;

  res->kind = BC_KIND_DICTIONARY;
  res->v.dictionary = dict;

  *span = res_span;

  return BC_PE_NONE;

fail:
  pg_hashtable_destroy(&dict);
  return err;
}

bc_parse_error_t bc_parse_value(pg_allocator_t allocator, pg_span_t* span,
                                bc_value_t* res) {
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

void bc_value_dump_indent(FILE* f, uint64_t indent) {
  for (uint64_t i = 0; i < indent; i++) fprintf(f, " ");
}

void bc_value_dump(bc_value_t* value, FILE* f, uint64_t indent) {
  switch (value->kind) {
    case BC_KIND_INTEGER:
      fprintf(f, "%lld", value->v.integer);
      break;
    case BC_KIND_STRING: {
      fprintf(f, "\"");
      for (uint64_t i = 0; i < pg_string_length(value->v.string); i++) {
        uint8_t c = (uint8_t)value->v.string[i];
        if (32 <= c && c < 127)
          fprintf(f, "%c", c);
        else
          fprintf(f, "\\u%04x", c);
      }
      fprintf(f, "\"");

      break;
    }
    case BC_KIND_ARRAY:
      fprintf(f, "[\n");
      for (uint64_t i = 0; i < pg_array_count(value->v.array); i++) {
        bc_value_dump_indent(f, indent + 2);
        bc_value_dump(&value->v.array[i], f, indent + 2);
        if (i < pg_array_count(value->v.array) - 1) fprintf(f, ",");
        fprintf(f, "\n");
      }
      bc_value_dump_indent(f, indent);
      fprintf(f, "]");
      break;
    case BC_KIND_DICTIONARY: {
      fprintf(f, "{\n");

      bc_dictionary_t* dict = &value->v.dictionary;

      uint64_t count = 0;
      for (uint64_t i = 0; i < pg_array_capacity(dict->keys); i++) {
        if (dict->hashes[i] == 0) {
          continue;
        }
        count++;

        bc_value_dump_indent(f, indent + 2);
        fprintf(f, "\"%s\": ", dict->keys[i]);
        bc_value_dump(&dict->values[i], f, indent + 2);
        if (count < pg_hashtable_count(dict)) fprintf(f, ",");

        fprintf(f, "\n");
      }

      bc_value_dump_indent(f, indent);
      fprintf(f, "}");
      break;
    }
  }
}

typedef struct {
  pg_string_t announce;
  uint64_t piece_length;
  uint64_t length;
  pg_string_t name;
  pg_array_t(uint8_t*) pieces;
} bc_metainfo_t;

typedef enum {
  BC_MI_NONE,
  BC_MI_METAINFO_NOT_DICTIONARY,
  BC_ME_ANNOUNCE_NOT_FOUND,
  BC_ME_ANNOUNCE_INVALID_KIND,
  BC_ME_INFO_NOT_FOUND,
  BC_ME_INFO_INVALID_KIND,
  BC_ME_PIECE_LENGTH_NOT_FOUND,
  BC_ME_PIECE_LENGTH_INVALID_KIND,
  BC_ME_PIECE_LENGTH_INVALID_VALUE,
  BC_ME_NAME_NOT_FOUND,
  BC_ME_NAME_INVALID_KIND,
  BC_ME_NAME_INVALID_VALUE,
  BC_ME_LENGTH_NOT_FOUND,
  BC_ME_LENGTH_INVALID_KIND,
  BC_ME_LENGTH_INVALID_VALUE,
  BC_ME_PIECES_NOT_FOUND,
  BC_ME_PIECES_INVALID_KIND,
  BC_ME_PIECES_INVALID_VALUE,
} bc_metainfo_error_t;

const char* bc_metainfo_error_to_string(int err) {
  switch (err) {
    case BC_MI_NONE:
      return "BC_MI_NONE";
    case BC_MI_METAINFO_NOT_DICTIONARY:
      return "BC_MI_METAINFO_NOT_DICTIONARY";
    case BC_ME_ANNOUNCE_NOT_FOUND:
      return "BC_ME_ANNOUNCE_NOT_FOUND";
    case BC_ME_ANNOUNCE_INVALID_KIND:
      return "BC_ME_ANNOUNCE_INVALID_KIND";
    case BC_ME_INFO_NOT_FOUND:
      return "BC_ME_INFO_NOT_FOUND";
    case BC_ME_INFO_INVALID_KIND:
      return "BC_ME_INFO_INVALID_KIND";
    case BC_ME_PIECE_LENGTH_NOT_FOUND:
      return "BC_ME_PIECE_LENGTH_NOT_FOUND";
    case BC_ME_PIECE_LENGTH_INVALID_KIND:
      return "BC_ME_PIECE_LENGTH_INVALID_KIND";
    case BC_ME_PIECE_LENGTH_INVALID_VALUE:
      return "BC_ME_PIECE_LENGTH_INVALID_VALUE";
    case BC_ME_NAME_NOT_FOUND:
      return "BC_ME_NAME_NOT_FOUND";
    case BC_ME_NAME_INVALID_KIND:
      return "BC_ME_NAME_INVALID_KIND";
    case BC_ME_NAME_INVALID_VALUE:
      return "BC_ME_NAME_INVALID_VALUE";
    case BC_ME_LENGTH_NOT_FOUND:
      return "BC_ME_LENGTH_NOT_FOUND";
    case BC_ME_LENGTH_INVALID_KIND:
      return "BC_ME_LENGTH_INVALID_KIND";
    case BC_ME_LENGTH_INVALID_VALUE:
      return "BC_ME_LENGTH_INVALID_VALUE";
    case BC_ME_PIECES_NOT_FOUND:
      return "BC_ME_PIECES_NOT_FOUND";
    case BC_ME_PIECES_INVALID_KIND:
      return "BC_ME_PIECES_INVALID_KIND";
    case BC_ME_PIECES_INVALID_VALUE:
      return "BC_ME_PIECES_INVALID_VALUE";
    default:
      __builtin_unreachable();
  }
}

void bc_metainfo_destroy(bc_metainfo_t* metainfo) {
  if (metainfo->pieces != NULL) pg_array_free(metainfo->pieces);
  if (metainfo->name != NULL) pg_string_free(metainfo->name);
}

bc_metainfo_error_t bc_metainfo_init_from_value(pg_allocator_t allocator,
                                                bc_value_t* val,
                                                bc_metainfo_t* metainfo) {
  bc_metainfo_error_t err = BC_MI_NONE;

  if (val->kind != BC_KIND_DICTIONARY) {
    err = BC_MI_METAINFO_NOT_DICTIONARY;
    goto end;
  }
  bc_dictionary_t* root = &val->v.dictionary;

  // Announce
  {
    uint64_t index = -1;
    pg_string_t announce_key = pg_string_make(pg_stack_allocator(), "announce");
    if (!pg_hashtable_find(root, announce_key, &index)) {
      err = BC_ME_ANNOUNCE_NOT_FOUND;
      goto end;
    }

    bc_value_t* announce_value = &root->values[index];
    if (announce_value->kind != BC_KIND_STRING) {
      err = BC_ME_ANNOUNCE_INVALID_KIND;
      goto end;
    }

    metainfo->announce = pg_string_make(allocator, announce_value->v.string);
  }

  // Info
  {
    uint64_t index = -1;
    pg_string_t info_key = pg_string_make(pg_stack_allocator(), "info");
    if (!pg_hashtable_find(root, info_key, &index)) {
      err = BC_ME_INFO_NOT_FOUND;
      goto end;
    }

    bc_value_t* info_value = &root->values[index];
    if (info_value->kind != BC_KIND_DICTIONARY) {
      err = BC_ME_INFO_INVALID_KIND;
      goto end;
    }

    bc_dictionary_t* info = &info_value->v.dictionary;

    // Piece length
    {
      index = -1;
      pg_string_t piece_length_key =
          pg_string_make(pg_stack_allocator(), "piece length");
      if (!pg_hashtable_find(info, piece_length_key, &index)) {
        err = BC_ME_PIECE_LENGTH_NOT_FOUND;
        goto end;
      }

      bc_value_t* piece_length_value = &info->values[index];
      if (piece_length_value->kind != BC_KIND_INTEGER) {
        err = BC_ME_PIECE_LENGTH_INVALID_KIND;
        goto end;
      }

      if (piece_length_value->v.integer <= 0) {
        err = BC_ME_PIECE_LENGTH_INVALID_VALUE;
        goto end;
      }

      metainfo->piece_length = (uint64_t)piece_length_value->v.integer;
    }

    // Name
    {
      index = -1;
      pg_string_t name_key = pg_string_make(pg_stack_allocator(), "name");
      if (!pg_hashtable_find(info, name_key, &index)) {
        err = BC_ME_NAME_NOT_FOUND;
        goto end;
      }

      bc_value_t* name_value = &info->values[index];
      if (name_value->kind != BC_KIND_STRING) {
        err = BC_ME_NAME_INVALID_KIND;
        goto end;
      }

      pg_string_t s = name_value->v.string;
      // TODO: revisit when we parse `path`
      if (pg_string_length(s) == 0) {
        err = BC_ME_NAME_INVALID_VALUE;
        goto end;
      }

      metainfo->name = pg_string_make(allocator, s);
    }
    // Length
    {
      index = -1;
      pg_string_t length_key = pg_string_make(pg_stack_allocator(), "length");
      if (!pg_hashtable_find(info, length_key, &index)) {
        err = BC_ME_LENGTH_NOT_FOUND;
        goto end;
      }

      bc_value_t* length_value = &info->values[index];
      if (length_value->kind != BC_KIND_INTEGER) {
        err = BC_ME_LENGTH_INVALID_KIND;
        goto end;
      }

      if (length_value->v.integer <= 0) {
        err = BC_ME_LENGTH_INVALID_VALUE;
        goto end;
      }

      metainfo->length = (uint64_t)length_value->v.integer;
    }

    // Pieces
    {
      index = -1;
      pg_string_t pieces_key = pg_string_make(pg_stack_allocator(), "pieces");
      if (!pg_hashtable_find(info, pieces_key, &index)) {
        err = BC_ME_PIECES_NOT_FOUND;
        goto end;
      }

      bc_value_t* pieces_value = &info->values[index];
      if (pieces_value->kind != BC_KIND_STRING) {
        err = BC_ME_PIECES_INVALID_KIND;
        goto end;
      }

      pg_string_t s = pieces_value->v.string;
      if (pg_string_length(s) == 0 || pg_string_length(s) % 20 != 0) {
        err = BC_ME_PIECES_INVALID_VALUE;
        goto end;
      }
      pg_array_init_reserve(metainfo->pieces, pg_string_length(s), allocator);
      memcpy(s, metainfo->pieces, pg_string_length(s));
      pg_array_resize(metainfo->pieces, pg_string_length(s));
    }
  }

end:
  if (err != BC_MI_NONE) bc_metainfo_destroy(metainfo);
  return err;
}

pg_string_t bc_value_marshal(pg_allocator_t allocator, bc_value_t* value) {
  switch (value->kind) {
    case BC_KIND_INTEGER: {
      char buf[28] = "";
      const uint64_t len =
          snprintf(buf, sizeof(buf), "i%llde", value->v.integer);
      return pg_string_make_length(allocator, buf, len);
    }
    case BC_KIND_STRING: {
      char buf[27] = "";
      const uint64_t len = snprintf(buf, sizeof(buf),
                                    "%lld:", pg_string_length(value->v.string));
      pg_string_t res = pg_string_make_reserve(
          allocator, pg_string_length(value->v.string) + sizeof(buf));
      res = pg_string_append_length(res, buf, len);
      return pg_string_append(res, value->v.string);
    }
    case BC_KIND_DICTIONARY: {
      bc_dictionary_t* dict = &value->v.dictionary;
      pg_string_t res =
          pg_string_make_reserve(allocator, pg_hashtable_count(dict) * 10);
      res = pg_string_appendc(res, "d");

      for (uint64_t i = 0; i < pg_array_capacity(dict->keys); i++) {
        if (dict->hashes[i] == 0) continue;

        char buf[27] = "";
        const uint64_t len = snprintf(buf, sizeof(buf),
                                      "%lld:", pg_string_length(dict->keys[i]));
        res = pg_string_append_length(res, buf, len);
        res = pg_string_append(res, dict->keys[i]);

        res = pg_string_append(res,
                               bc_value_marshal(allocator, &dict->values[i]));
      }
      res = pg_string_appendc(res, "e");
      return res;
    }
    case BC_KIND_ARRAY: {
      pg_string_t res = pg_string_make_reserve(
          allocator, pg_array_count(value->v.array) * 10);
      res = pg_string_appendc(res, "l");
      for (uint64_t i = 0; i < pg_array_count(value->v.array); i++) {
        res = pg_string_append(res,
                               bc_value_marshal(allocator, &value->v.array[i]));
      }
      res = pg_string_appendc(res, "e");

      return res;
    }
    default:
      __builtin_unreachable();
  }
}
