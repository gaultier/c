#pragma once

#include <_types/_uint64_t.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_types/_int64_t.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

typedef struct pg_allocator_t pg_allocator_t;
struct pg_allocator_t {
  //  void *backing_memory;
  //  uint64_t backing_memory_cap;
  void *(*realloc)(uint64_t new_size, void *old_memory, uint64_t old_size);
  void (*free)(void *memory);
};

void *pg_heap_realloc(uint64_t new_size, void *old_memory, uint64_t old_size) {
  void *res = realloc(old_memory, new_size);
  memset(res + old_size, 0, new_size - old_size);
  return res;
}

void pg_heap_free(void *memory) { free(memory); }

pg_allocator_t pg_heap_allocator() {
  return (pg_allocator_t){.realloc = pg_heap_realloc, .free = pg_heap_free};
}

void *pg_null_realloc(uint64_t new_size, void *old_memory, uint64_t old_size) {
  (void)new_size;
  (void)old_memory;
  (void)old_size;
  __builtin_unreachable();
}

void pg_null_free(void *memory) {
  (void)memory;
  __builtin_unreachable();
}

pg_allocator_t pg_null_allocator() {
  return (pg_allocator_t){.realloc = pg_null_realloc, .free = pg_null_free};
}

// void *pg_arena_realloc(pg_allocator_t *allocator, uint64_t new_size,
//                        void *old_memory, uint64_t old_size) {
//   assert(allocator != NULL);
//   if (allocator->backing_memory_cap < old_size) return NULL;
//
//   return allocator->backing_memory;
// }
//
// void * pg_arena_free(pg_allocator_t* allocator, );

typedef struct pg_array_header_t {
  uint64_t count;
  uint64_t capacity;
  pg_allocator_t allocator;
} pg_array_header_t;

#define pg_array_t(Type) Type *

#ifndef PG_ARRAY_GROW_FORMULA
#define PG_ARRAY_GROW_FORMULA(x) (1.5 * (x) + 8)
#endif

#define PG_ARRAY_HEADER(x) ((pg_array_header_t *)(x)-1)
#define pg_array_count(x) (PG_ARRAY_HEADER(x)->count)
#define pg_array_capacity(x) (PG_ARRAY_HEADER(x)->capacity)
#define pg_array_available_space(x) (pg_array_capacity(x) - pg_array_count(x))

#define pg_array_init_reserve(x, cap, my_allocator)                          \
  do {                                                                       \
    void **pg__array_ = (void **)&(x);                                       \
    pg_array_header_t *pg__ah =                                              \
        (pg_array_header_t *)(my_allocator)                                  \
            .realloc(sizeof(pg_array_header_t) + sizeof(*(x)) * (cap), NULL, \
                     0);                                                     \
    pg__ah->count = 0;                                                       \
    pg__ah->capacity = cap;                                                  \
    pg__ah->allocator = my_allocator;                                        \
    *pg__array_ = (void *)(pg__ah + 1);                                      \
  } while (0)

#define pg_array_init(x, my_allocator) pg_array_init_reserve(x, 0, my_allocator)

#define pg_array_free(x)                            \
  do {                                              \
    pg_array_header_t *pg__ah = PG_ARRAY_HEADER(x); \
    pg__ah->allocator.free(pg__ah);                 \
    x = NULL;                                       \
  } while (0)

#define pg_array_set_capacity(x, capacity)                                 \
  do {                                                                     \
    if (x) {                                                               \
      void **pg__array_ = (void **)&(x);                                   \
      *pg__array_ = pg__array_set_capacity((x), (capacity), sizeof(*(x))); \
    }                                                                      \
  } while (0)

void *pg__array_set_capacity(void *array, uint64_t capacity,
                             uint64_t element_size) {
  pg_array_header_t *h = PG_ARRAY_HEADER(array);

  assert(element_size > 0);

  if (capacity == h->capacity) return array;

  if (capacity < h->count) {
    if (h->capacity < capacity) {
      uint64_t new_capacity = PG_ARRAY_GROW_FORMULA(h->capacity);
      if (new_capacity < capacity) new_capacity = capacity;
      pg__array_set_capacity(array, new_capacity, element_size);
    }
    h->count = capacity;
  }

  {
    uint64_t size = sizeof(pg_array_header_t) + element_size * capacity;
    pg_array_header_t *nh = calloc(1, size);
    memmove(nh, h, sizeof(pg_array_header_t) + element_size * h->count);
    nh->count = h->count;
    nh->capacity = capacity;
    nh->allocator.free(h);
    return nh + 1;
  }
}

#define pg_array_grow(x, min_capacity)                                   \
  do {                                                                   \
    uint64_t new_capacity = PG_ARRAY_GROW_FORMULA(pg_array_capacity(x)); \
    if (new_capacity < (min_capacity)) new_capacity = (min_capacity);    \
    pg_array_set_capacity(x, new_capacity);                              \
  } while (0)

#define pg_array_append(x, item)                                           \
  do {                                                                     \
    if (pg_array_capacity(x) < pg_array_count(x) + 1) pg_array_grow(x, 0); \
    (x)[pg_array_count(x)++] = (item);                                     \
  } while (0)

#define pg_array_appendv(x, items, item_count)                           \
  do {                                                                   \
    pg_array_header_t *pg__ah = PG_ARRAY_HEADER(x);                      \
    if (pg__ah->capacity < pg__ah->count + (item_count))                 \
      pg_array_grow(x, pg__ah->count + (item_count));                    \
    memcpy(&(x)[pg__ah->count], (items), sizeof((x)[0]) * (item_count)); \
    pg__ah->count += (item_count);                                       \
  } while (0)

#define pg_array_appendv0(x, items0) pg_array_appendv(x, items0, strlen(items0))

#define pg_array_pop(x)                    \
  do {                                     \
    assert(PG_ARRAY_HEADER(x)->count > 0); \
    PG_ARRAY_HEADER(x)->count--;           \
  } while (0)
#define pg_array_clear(x)          \
  do {                             \
    PG_ARRAY_HEADER(x)->count = 0; \
  } while (0)

#define pg_array_resize(x, new_count)                         \
  do {                                                        \
    if (PG_ARRAY_HEADER(x)->capacity < (uint64_t)(new_count)) \
      pg_array_grow(x, (uint64_t)(new_count));                \
    PG_ARRAY_HEADER(x)->count = (uint64_t)(new_count);        \
  } while (0)

#define pg_array_reserve(x, new_capacity)              \
  do {                                                 \
    if (PG_ARRAY_HEADER(x)->capacity < (new_capacity)) \
      pg_array_set_capacity(x, new_capacity);          \
  } while (0)

// Set cap(x) == len(x)
#define pg_array_shrink(x)                       \
  do {                                           \
    pg_array_set_capacity(x, pg_array_count(x)); \
  } while (0)

char pg_char_to_lower(char c) {
  if (c >= 'A' && c <= 'Z') return 'a' + (c - 'A');
  return c;
}

bool pg_char_is_space(char c) {
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
    return true;
  return false;
}

bool pg_char_is_digit(char c) {
  if (c >= '0' && c <= '9') return true;
  return false;
}

bool pg_str_has_prefix(char *haystack0, char *needle0) {
  uint64_t haystack0_len = strlen(haystack0);
  uint64_t needle0_len = strlen(needle0);
  if (needle0_len > haystack0_len) return false;
  return memcmp(haystack0, needle0, needle0_len) == 0;
}

// ------------------ Strings

typedef char *pg_string_t;

// NOTE(bill): If you only need a small string, just use a standard c string or
// change the size from uint64_t to u16, etc.
typedef struct pg_string_header_t {
  pg_allocator_t allocator;
  uint64_t length;
  uint64_t capacity;
} pg_string_header_t;

#define PG_STRING_HEADER(str) ((pg_string_header_t *)(str)-1)

void pg__set_string_length(pg_string_t str, uint64_t len) {
  PG_STRING_HEADER(str)->length = len;
}

void pg__set_string_capacity(pg_string_t str, uint64_t cap) {
  PG_STRING_HEADER(str)->capacity = cap;
}

pg_string_t pg_string_make_reserve(pg_allocator_t a, uint64_t capacity) {
  uint64_t header_size = sizeof(pg_string_header_t);
  void *ptr = a.realloc(header_size + capacity + 1, NULL, 0);

  pg_string_t str;
  pg_string_header_t *header;

  if (ptr == NULL) return NULL;
  memset(ptr, 0, header_size + capacity + 1);

  str = (char *)ptr + header_size;
  header = PG_STRING_HEADER(str);
  header->allocator = a;
  header->length = 0;
  header->capacity = capacity;
  str[capacity] = '\0';

  return str;
}

pg_string_t pg_string_make_length(pg_allocator_t a, void const *init_str,
                                  uint64_t num_bytes) {
  uint64_t header_size = sizeof(pg_string_header_t);
  void *ptr = a.realloc(header_size + num_bytes + 1, NULL, 0);

  pg_string_t str;
  pg_string_header_t *header;

  if (ptr == NULL) return NULL;
  if (!init_str) memset(ptr, 0, header_size + num_bytes + 1);

  str = (char *)ptr + header_size;
  header = PG_STRING_HEADER(str);
  header->allocator = a;
  header->length = num_bytes;
  header->capacity = num_bytes;
  if (num_bytes && init_str) {
    memcpy(str, init_str, num_bytes);
  }
  str[num_bytes] = '\0';

  return str;
}

pg_string_t pg_string_make(pg_allocator_t a, char const *str) {
  uint64_t len = str ? strlen(str) : 0;
  return pg_string_make_length(a, str, len);
}

void pg_string_free(pg_string_t str) {
  if (str) {
    pg_string_header_t *header = PG_STRING_HEADER(str);
    header->allocator.free(header);
  }
}

void pg_string_free_ptr(pg_string_t *str) { pg_string_free(*str); }

uint64_t pg_string_length(pg_string_t const str) {
  return PG_STRING_HEADER(str)->length;
}

pg_string_t pg_string_duplicate(pg_allocator_t a, pg_string_t const str) {
  return pg_string_make_length(a, str, pg_string_length(str));
}

uint64_t pg_string_capacity(pg_string_t const str) {
  return PG_STRING_HEADER(str)->capacity;
}

uint64_t pg_string_available_space(pg_string_t const str) {
  pg_string_header_t *h = PG_STRING_HEADER(str);
  if (h->capacity > h->length) {
    return h->capacity - h->length;
  }
  return 0;
}

void pg_string_clear(pg_string_t str) {
  pg__set_string_length(str, 0);
  str[0] = '\0';
}

pg_string_t pg_string_make_space_for(pg_string_t str, int64_t add_len) {
  int64_t available = pg_string_available_space(str);

  // NOTE(bill): Return if there is enough space left
  if (available >= add_len) {
    return str;
  } else {
    int64_t new_len, old_size, new_size;
    void *ptr, *new_ptr;
    pg_allocator_t a = PG_STRING_HEADER(str)->allocator;
    pg_string_header_t *header;

    new_len = pg_string_length(str) + add_len;
    ptr = PG_STRING_HEADER(str);
    old_size = sizeof(pg_string_header_t) + pg_string_length(str) + 1;
    new_size = sizeof(pg_string_header_t) + new_len + 1;

    new_ptr = PG_STRING_HEADER(str)->allocator.realloc(new_size, ptr, old_size);
    if (new_ptr == NULL) return NULL;

    header = (pg_string_header_t *)new_ptr;
    header->allocator = a;

    str = (pg_string_t)(header + 1);
    pg__set_string_capacity(str, new_len);

    return str;
  }
}
pg_string_t pg_string_append_length(pg_string_t str, void const *other,
                                    uint64_t other_len) {
  if (other_len > 0) {
    uint64_t curr_len = pg_string_length(str);

    str = pg_string_make_space_for(str, other_len);
    if (str == NULL) {
      return NULL;
    }

    memcpy(str + curr_len, other, other_len);
    str[curr_len + other_len] = '\0';
    pg__set_string_length(str, curr_len + other_len);
  }
  return str;
}

pg_string_t pg_string_append(pg_string_t str, pg_string_t const other) {
  return pg_string_append_length(str, other, pg_string_length(other));
}

pg_string_t pg_string_appendc(pg_string_t str, char const *other) {
  return pg_string_append_length(str, other, strlen(other));
}

// ---------------- Hashtable

// FNV-1a
uint32_t pg_hash(uint8_t *n, uint64_t len) {
  uint32_t hash = 2166136261u;
  for (uint64_t i = 0; i < len; i++) {
    hash ^= (uint8_t)n[i];
    hash *= 16777619;
  }
  return hash;
}
// ------------------ Span

typedef struct {
  char *data;
  uint64_t len;
} pg_string_span_t;

void pg_span_consume(pg_string_span_t *span, uint64_t n) {
  assert(span != NULL);
  assert(span->data != NULL);
  assert(span->len >= n);

  span->data += n;
  span->len -= n;
}
