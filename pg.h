#pragma once
#include <assert.h>
#include <malloc/_malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct pg_allocator_t pg_allocator_t;
struct pg_allocator_t {
  //  void *backing_memory;
  //  uint64_t backing_memory_cap;
  void *(*realloc)(uint64_t new_size, void *old_memory, uint64_t old_size);
  void (*free)(void *memory);
};

void *pg_heap_realloc(uint64_t new_size, void *old_memory, uint64_t old_size) {
  return realloc(old_memory, new_size);
}

void pg_heap_free(void *memory) { free(memory); }

pg_allocator_t pg_heap_allocator() {
  return (pg_allocator_t){.realloc = pg_heap_realloc, .free = pg_heap_free};
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
#define PG_ARRAY_GROW_FORMULA(x) (2 * (x) + 8)
#endif

#define PG_ARRAY_HEADER(x) ((pg_array_header_t *)(x)-1)
#define pg_array_count(x) (PG_ARRAY_HEADER(x)->count)
#define pg_array_capacity(x) (PG_ARRAY_HEADER(x)->capacity)

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

#define pg_array_init(x) pg_array_init_reserve(x, PG_ARRAY_GROW_FORMULA(0))

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

static void *pg__array_set_capacity(void *array, uint64_t capacity,
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

static char pg_char_to_lower(char c) {
  if (c >= 'A' && c <= 'Z') return 'a' + (c - 'A');
  return c;
}

static bool pg_char_is_space(char c) {
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
    return true;
  return false;
}

static bool pg_char_is_digit(char c) {
  if (c >= '0' && c <= '9') return true;
  return false;
}

static bool pg_str_has_prefix(char *haystack0, char *needle0) {
  uint64_t haystack0_len = strlen(haystack0);
  uint64_t needle0_len = strlen(needle0);
  if (needle0_len > haystack0_len) return false;
  return memcmp(haystack0, needle0, needle0_len) == 0;
}

