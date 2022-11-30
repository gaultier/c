#pragma once

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define Ki (1024ULL)
#define Mi (1024ULL * Ki)
#define Gi (1024ULL * Mi)
#define Ti (1024ULL * Gi)

// Check that __COUNTER__ is defined and that __COUNTER__ increases by 1
// every time it is expanded. X + 1 == X + 0 is used in case X is defined to be
// empty. If X is empty the expression becomes (+1 == +0).
#if defined(__COUNTER__) && (__COUNTER__ + 1 == __COUNTER__ + 0)
#define PG_PRIVATE_UNIQUE_ID __COUNTER__
#else
#define PG_PRIVATE_UNIQUE_ID __LINE__
#endif

// Helpers for generating unique variable names
#define PG_PRIVATE_NAME(n) PG_PRIVATE_CONCAT(n, PG_PRIVATE_UNIQUE_ID)
#define PG_PRIVATE_CONCAT(a, b) PG_PRIVATE_CONCAT2(a, b)
#define PG_PRIVATE_CONCAT2(a, b) a##b
#define PG_PAD(n) uint8_t PG_PRIVATE_NAME(_padding)[n]

typedef struct {
  char *data;
  uint64_t len;
} pg_span_t;

typedef struct pg_allocator_t pg_allocator_t;
struct pg_allocator_t {
  //  void *backing_memory;
  //  uint64_t backing_memory_cap;
  void *(*realloc)(void *old_memory, uint64_t new_size, uint64_t old_size);
  void (*free)(void *memory);
};

__attribute__((unused)) static void *
pg_heap_realloc(void *old_memory, uint64_t new_size, uint64_t old_size) {
  void *res = realloc(old_memory, new_size);
  memset((uint8_t *)res + old_size, 0, new_size - old_size);
  return res;
}

__attribute__((unused)) static void pg_heap_free(void *memory) { free(memory); }

__attribute__((unused)) static pg_allocator_t pg_heap_allocator(void) {
  return (pg_allocator_t){.realloc = pg_heap_realloc, .free = pg_heap_free};
}

// memmem
__attribute__((unused)) static void *pg_memmem(const void *big,
                                               uint64_t big_len,
                                               const void *little,
                                               uint64_t little_len) {
  const char *sbig = (const char *)big;
  const char *slittle = (const char *)little;
  char *s = memchr(big, slittle[0], big_len);
  if (!s)
    return NULL;

  uint64_t rem_len = big_len + (uint64_t)(s - sbig);
  if (rem_len < little_len)
    return NULL;

  for (uint64_t i = 0; i < little_len; i++) {
    if (s[i] != slittle[i])
      return NULL;
  }
  return s;
}

// -------------------------- Pool

__attribute__((unused)) static bool pg_is_power_of_two(uint64_t x) {
  return (x & (x - 1)) == 0;
}

__attribute__((unused)) static uint64_t pg_align_forward(uint64_t ptr,
                                                         uint64_t align) {
  uint64_t p = 0, a = 0, modulo = 0;

  assert(pg_is_power_of_two(align));

  p = ptr;
  a = (uintptr_t)align;
  // Same as (p % a) but faster as 'a' is a power of two
  modulo = p & (a - 1);

  if (modulo != 0) {
    // If 'p' address is not aligned, push the address to the
    // next value which is aligned
    p += a - modulo;
  }
  return p;
}

#define PG_DEFAULT_ALIGNEMENT 16

typedef struct pg_pool_free_node_t {
  struct pg_pool_free_node_t *next;
} pg_pool_free_node_t;

typedef struct {
  uint8_t *buf;
  uint64_t buf_len;
  uint64_t chunk_size;
  pg_pool_free_node_t *head;
} pg_pool_t;

__attribute__((unused)) static void pg_pool_free_all(pg_pool_t *pool) {
  for (uint64_t i = 0; i < pool->buf_len / pool->chunk_size; i++) {
    void *ptr = &pool->buf[i * pool->chunk_size];
    pg_pool_free_node_t *node = (pg_pool_free_node_t *)ptr;
    node->next = pool->head;
    pool->head = node;
  }
}

__attribute__((unused)) static void *pg_pool_alloc(pg_pool_t *pool) {
  pg_pool_free_node_t *node = pool->head;
  if (node == NULL)
    return NULL; // No more space

  pool->head = pool->head->next;

  return memset(node, 0, pool->chunk_size);
}

__attribute__((unused)) static void pg_pool_free(pg_pool_t *pool, void *ptr) {
  assert(ptr != NULL);
  assert(ptr >= (void *)pool->buf);
  assert((uint8_t *)ptr <
         (uint8_t *)pool->buf + pool->buf_len - sizeof(pg_pool_free_node_t *));

  pg_pool_free_node_t *node = (pg_pool_free_node_t *)ptr;
  node->next = pool->head;
  pool->head = node;
}

__attribute__((unused)) static void
pg_pool_init(pg_pool_t *pool, uint64_t chunk_size, uint64_t max_items_count) {
  // TODO: allow using existing chunk of mem
  // TODO: alignement

  chunk_size = pg_align_forward(chunk_size, PG_DEFAULT_ALIGNEMENT);
  assert(chunk_size >= sizeof(pg_pool_free_node_t));

  pool->chunk_size = chunk_size;
  pool->buf_len = max_items_count * chunk_size;
  pool->buf = calloc(max_items_count, chunk_size);
  assert(pool->buf);
  pool->head = NULL;

  pg_pool_free_all(pool);
}

__attribute__((unused)) static void pg_pool_destroy(pg_pool_t *pool) {
  free(pool->buf);
}

// --------------------------- Array

typedef struct pg_array_header_t {
  uint64_t len;
  uint64_t capacity;
  pg_allocator_t allocator;
} pg_array_header_t;

#define pg_array_t(Type) Type *

#ifndef PG_ARRAY_GROW_FORMULA
#define PG_ARRAY_GROW_FORMULA(x) ((uint64_t)(1.5 * ((double)x) + 8))
#endif

#define PG_ARRAY_HEADER(x) (((pg_array_header_t *)((void *)x)) - 1)
#define PG_CONST_ARRAY_HEADER(x)                                               \
  (((const pg_array_header_t *)((const void *)x)) - 1)
#define pg_array_len(x) (PG_CONST_ARRAY_HEADER(x)->len)
#define pg_array_capacity(x) (PG_CONST_ARRAY_HEADER(x)->capacity)
#define pg_array_available_space(x) (pg_array_capacity(x) - pg_array_len(x))

#define pg_array_init_reserve(x, cap, my_allocator)                            \
  do {                                                                         \
    void **pg__array_ = (void **)&(x);                                         \
    pg_array_header_t *pg__ah =                                                \
        (pg_array_header_t *)(my_allocator)                                    \
            .realloc(NULL,                                                     \
                     sizeof(pg_array_header_t) +                               \
                         sizeof(*(x)) * ((uint64_t)cap),                       \
                     0);                                                       \
    pg__ah->len = 0;                                                           \
    pg__ah->capacity = (uint64_t)cap;                                          \
    pg__ah->allocator = my_allocator;                                          \
    *pg__array_ = (void *)(pg__ah + 1);                                        \
  } while (0)

#define pg_array_init(x, my_allocator) pg_array_init_reserve(x, 0, my_allocator)

#define pg_array_free(x)                                                       \
  do {                                                                         \
    pg_array_header_t *pg__ah = PG_ARRAY_HEADER(x);                            \
    pg__ah->allocator.free(pg__ah);                                            \
    x = NULL;                                                                  \
  } while (0)

#define pg_array_grow(x, min_capacity)                                         \
  do {                                                                         \
    uint64_t new_capacity = PG_ARRAY_GROW_FORMULA(pg_array_capacity(x));       \
    if (new_capacity < (min_capacity))                                         \
      new_capacity = (min_capacity);                                           \
    const uint64_t old_size =                                                  \
        sizeof(pg_array_header_t) + pg_array_capacity(x) * sizeof(*x);         \
    const uint64_t new_size =                                                  \
        sizeof(pg_array_header_t) + new_capacity * sizeof(*x);                 \
    pg_array_header_t *pg__new_header = PG_ARRAY_HEADER(x)->allocator.realloc( \
        PG_ARRAY_HEADER(x), new_size, old_size);                               \
    pg__new_header->capacity = new_capacity;                                   \
    x = (void *)(pg__new_header + 1);                                          \
  } while (0)

#define pg_array_append(x, item)                                               \
  do {                                                                         \
    if (pg_array_capacity(x) < pg_array_len(x) + 1)                            \
      pg_array_grow(x, 0);                                                     \
    (x)[PG_ARRAY_HEADER(x)->len++] = (item);                                   \
  } while (0)

#define pg_array_pop(x)                                                        \
  do {                                                                         \
    assert(PG_ARRAY_HEADER(x)->len > 0);                                       \
    PG_ARRAY_HEADER(x)->len--;                                                 \
  } while (0)
#define pg_array_clear(x)                                                      \
  do {                                                                         \
    PG_ARRAY_HEADER(x)->len = 0;                                               \
  } while (0)

#define pg_array_resize(x, new_count)                                          \
  do {                                                                         \
    if (PG_ARRAY_HEADER(x)->capacity < (uint64_t)(new_count))                  \
      pg_array_grow(x, (uint64_t)(new_count));                                 \
    PG_ARRAY_HEADER(x)->len = (uint64_t)(new_count);                           \
  } while (0)

__attribute__((unused)) static char const *pg_char_last_occurence(char const *s,
                                                                  char c) {
  char const *result = NULL;
  do {
    if (*s == c) {
      result = s;
    }
  } while (*s++);

  return result;
}

__attribute__((unused)) static char const *
pg_char_first_occurence(char const *s, char c) {
  char ch = c;
  for (; *s != ch; s++) {
    if (*s == '\0') {
      return NULL;
    }
  }
  return s;
}

__attribute__((unused)) static char pg_char_to_lower(char c) {
  if (c >= 'A' && c <= 'Z')
    return 'a' + (c - 'A');
  return c;
}

__attribute__((unused)) static bool pg_char_is_space(char c) {
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
    return true;
  return false;
}

__attribute__((unused)) static bool pg_char_is_digit(char c) {
  if (c >= '0' && c <= '9')
    return true;
  return false;
}

__attribute__((unused)) static bool pg_char_is_alpha(char c) {
  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
    return true;
  return false;
}

__attribute__((unused)) static bool pg_char_is_alphanumeric(char c) {
  return pg_char_is_alpha(c) || pg_char_is_digit(c);
}

// ------------------ Strings

typedef char *pg_string_t;

// NOTE(bill): If you only need a small string, just use a standard c
// string or change the size from uint64_t to u16, etc.
typedef struct pg_string_header_t {
  pg_allocator_t allocator;
  uint64_t length;
  uint64_t capacity;
} pg_string_header_t;

#define PG_STRING_HEADER(str) ((pg_string_header_t *)((void *)str) - 1)

__attribute__((unused)) static void pg__set_string_len(pg_string_t str,
                                                       uint64_t len) {
  PG_STRING_HEADER(str)->length = len;
}

__attribute__((unused)) static void pg__set_string_capacity(pg_string_t str,
                                                            uint64_t cap) {
  PG_STRING_HEADER(str)->capacity = cap;
}

__attribute__((unused)) static pg_string_t
pg_string_make_reserve(pg_allocator_t a, uint64_t capacity) {
  uint64_t header_size = sizeof(pg_string_header_t);
  void *ptr = a.realloc(NULL, header_size + capacity + 1, 0);

  pg_string_t str;
  pg_string_header_t *header;

  if (ptr == NULL)
    return NULL;
  memset(ptr, 0, header_size + capacity + 1);

  str = (char *)ptr + header_size;
  header = PG_STRING_HEADER(str);
  header->allocator = a;
  header->length = 0;
  header->capacity = capacity;
  str[capacity] = '\0';

  return str;
}

__attribute__((unused)) static pg_string_t
pg_string_make_length(pg_allocator_t a, void const *init_str,
                      uint64_t num_bytes) {
  uint64_t header_size = sizeof(pg_string_header_t);
  void *ptr = a.realloc(NULL, header_size + num_bytes + 1, 0);

  pg_string_t str;
  pg_string_header_t *header;

  if (ptr == NULL)
    return NULL;
  if (!init_str)
    memset(ptr, 0, header_size + num_bytes + 1);

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

__attribute__((unused)) static pg_string_t pg_string_make(pg_allocator_t a,
                                                          char const *str) {
  uint64_t len = str ? strlen(str) : 0;
  return pg_string_make_length(a, str, len);
}

__attribute__((unused)) static void pg_string_free(pg_string_t str) {
  if (str) {
    pg_string_header_t *header = PG_STRING_HEADER(str);
    header->allocator.free(header);
  }
}

__attribute__((unused)) static void pg_string_free_ptr(pg_string_t *str) {
  pg_string_free(*str);
}

__attribute__((unused)) static uint64_t pg_string_len(pg_string_t const str) {
  return PG_STRING_HEADER(str)->length;
}

__attribute__((unused)) static pg_string_t
pg_string_duplicate(pg_allocator_t a, pg_string_t const str) {
  return pg_string_make_length(a, str, pg_string_len(str));
}

__attribute__((unused)) static uint64_t pg_string_cap(pg_string_t const str) {
  return PG_STRING_HEADER(str)->capacity;
}

__attribute__((unused)) static uint64_t
pg_string_available_space(pg_string_t const str) {
  pg_string_header_t *h = PG_STRING_HEADER(str);
  if (h->capacity > h->length) {
    return h->capacity - h->length;
  }
  return 0;
}

__attribute__((unused)) static void pg_string_clear(pg_string_t str) {
  pg__set_string_len(str, 0);
  str[0] = '\0';
}

__attribute__((unused)) static bool pg_str_has_suffix(char const *str,
                                                      char const *suffix) {
  uint64_t i = strlen(str);
  uint64_t j = strlen(suffix);
  if (j <= i) {
    return strcmp(str + i - j, suffix) == 0;
  }
  return false;
}

__attribute__((unused)) static bool pg_str_has_prefix(char const *str,
                                                      char const *prefix) {
  while (*prefix) {
    if (*str++ != *prefix++) {
      return false;
    }
  }
  return true;
}

__attribute__((unused)) static pg_string_t
pg_string_make_space_for(pg_string_t str, uint64_t add_len) {
  const uint64_t available = pg_string_available_space(str);

  // NOTE(bill): Return if there is enough space left
  if (available >= add_len) {
    return str;
  } else {
    uint64_t new_len = 0, old_size = 0, new_size = 0;
    void *ptr = NULL, *new_ptr = NULL;
    pg_allocator_t a = PG_STRING_HEADER(str)->allocator;
    pg_string_header_t *header;

    new_len = pg_string_len(str) + add_len;
    ptr = PG_STRING_HEADER(str);
    old_size = sizeof(pg_string_header_t) + pg_string_len(str) + 1;
    new_size = sizeof(pg_string_header_t) + new_len + 1;

    new_ptr = PG_STRING_HEADER(str)->allocator.realloc(ptr, new_size, old_size);
    if (new_ptr == NULL)
      return NULL;

    header = (pg_string_header_t *)new_ptr;
    header->allocator = a;

    str = (pg_string_t)(header + 1);
    pg__set_string_capacity(str, new_len);

    return str;
  }
}
__attribute__((unused)) static pg_string_t
pg_string_append_length(pg_string_t str, void const *other,
                        uint64_t other_len) {
  if (other_len > 0) {
    uint64_t curr_len = pg_string_len(str);

    str = pg_string_make_space_for(str, other_len);
    if (str == NULL) {
      return NULL;
    }

    memmove(str + curr_len, other, other_len);
    str[curr_len + other_len] = '\0';
    pg__set_string_len(str, curr_len + other_len);
  }
  return str;
}

__attribute__((unused)) static pg_string_t
pg_string_append(pg_string_t str, pg_string_t const other) {
  return pg_string_append_length(str, other, pg_string_len(other));
}

__attribute__((unused)) static pg_string_t
pg_string_appendc(pg_string_t str, char const *other) {
  return pg_string_append_length(str, other, strlen(other));
}

__attribute__((unused)) static pg_string_t
pg_span_url_encode(pg_allocator_t allocator, pg_span_t src);

__attribute__((unused)) static pg_string_t
pg_string_url_encode(pg_allocator_t allocator, pg_string_t src) {
  pg_span_t span = {.data = src, .len = pg_string_len(src)};
  return pg_span_url_encode(allocator, span);
}

__attribute__((unused)) static pg_string_t pg_string_trim(pg_string_t str,
                                                          char const *cut_set) {
  char *start = NULL, *end = NULL, *start_pos = NULL, *end_pos = NULL;
  uint64_t len = 0;

  start_pos = start = str;
  end_pos = end = str + pg_string_len(str) - 1;

  while (start_pos <= end && pg_char_first_occurence(cut_set, *start_pos)) {
    start_pos++;
  }
  while (end_pos > start_pos && pg_char_first_occurence(cut_set, *end_pos)) {
    end_pos--;
  }

  len = (start_pos > end_pos) ? 0ULL : ((uint64_t)(end_pos - start_pos) + 1);

  if (str != start_pos)
    memmove(str, start_pos, len);
  str[len] = '\0';

  pg__set_string_len(str, len);

  return str;
}

// ---------------- Hashtable

// FNV-1a
__attribute__((unused)) static uint32_t pg_hash(uint8_t *n, uint64_t len) {
  uint32_t hash = 2166136261u;
  for (uint64_t i = 0; i < len; i++) {
    hash ^= (uint8_t)n[i];
    hash *= 16777619;
  }
  return hash;
}
// ------------------ Span

__attribute__((unused)) static char pg_span_peek_left(pg_span_t span,
                                                      bool *more_chars) {
  if (span.len > 0) {
    if (more_chars != NULL)
      *more_chars = true;
    return span.data[0];
  } else {
    if (more_chars != NULL)
      *more_chars = false;
    return 0;
  }
}

__attribute__((unused)) static char pg_span_peek_right(pg_span_t span,
                                                       bool *found) {
  if (span.len > 0) {
    if (found != NULL)
      *found = true;
    return span.data[span.len - 1];
  } else {
    if (found != NULL)
      *found = false;
    return 0;
  }
}

__attribute__((unused)) static void pg_span_consume_left(pg_span_t *span,
                                                         uint64_t n) {
  assert(span != NULL);

  if (span->len == 0)
    return;

  assert(span->data != NULL);
  assert(span->len >= n);

  span->data += n;
  span->len -= n;
}

__attribute__((unused)) static void pg_span_consume_right(pg_span_t *span,
                                                          uint64_t n) {
  assert(span != NULL);

  if (span->len == 0)
    return;

  assert(span->data != NULL);
  assert(span->len >= n);

  span->len -= n;
}

__attribute__((unused)) static bool pg_span_split_at_first(pg_span_t span,
                                                           char needle,
                                                           pg_span_t *left,
                                                           pg_span_t *right) {
  *left = (pg_span_t){0};
  *right = (pg_span_t){0};

  for (uint64_t i = 0; i < span.len; i++) {
    if (span.data[i] == needle) {
      left->data = span.data;
      left->len = i;
      right->data = span.data + i;
      right->len = span.len - i;
      assert(right->data[0] == needle);

      return true;
    }
  }

  *left = span;
  return false;
}

__attribute__((unused)) static bool pg_span_split_at_last(pg_span_t span,
                                                          char needle,
                                                          pg_span_t *left,
                                                          pg_span_t *right) {
  *left = (pg_span_t){0};
  *right = (pg_span_t){0};
  for (int64_t i = (int64_t)(span.len - 1); i >= 0; i--) {
    if (span.data[i] == needle) {
      left->data = span.data;
      left->len = (uint64_t)i;

      right->data = &span.data[i];
      right->len = span.len - (uint64_t)i;
      assert(right->data[0] == needle);

      return true;
    }
  }
  *left = span;
  return false;
}

__attribute__((unused)) static bool
pg_span_skip_left_until_inclusive(pg_span_t *span, char needle) {
  pg_span_t left = {0}, right = {0};
  if (!pg_span_split_at_first(*span, needle, &left, &right)) {
    return false;
  }

  *span = right;
  pg_span_consume_left(span, 1);
  return true;
}

__attribute__((unused)) static void pg_span_trim_left(pg_span_t *span) {
  bool more_chars = false;
  char c = 0;
  while (true) {
    c = pg_span_peek_left(*span, &more_chars);
    if (!more_chars)
      return;
    if (pg_char_is_space(c))
      pg_span_consume_left(span, 1);
    else
      return;
  }
}

__attribute__((unused)) static void pg_span_trim_right(pg_span_t *span) {
  bool more_chars = false;
  char c = 0;
  while (true) {
    c = pg_span_peek_right(*span, &more_chars);
    if (!more_chars)
      return;
    if (pg_char_is_space(c))
      pg_span_consume_right(span, 1);
    else
      return;
  }
}

__attribute__((unused)) static void pg_span_trim(pg_span_t *span) {
  pg_span_trim_left(span);
  pg_span_trim_right(span);
}

__attribute__((unused)) static bool pg_span_contains(pg_span_t haystack,
                                                     pg_span_t needle) {
  if (needle.len > haystack.len)
    return false;
  return pg_memmem(haystack.data, haystack.len, needle.data, needle.len) !=
         NULL;
}

__attribute__((unused)) static bool pg_span_ends_with(pg_span_t haystack,
                                                      pg_span_t needle) {
  if (needle.len > haystack.len)
    return false;
  return pg_memmem(haystack.data + haystack.len - needle.len, needle.len,
                   needle.data, needle.len) != NULL;
}

__attribute__((unused)) static pg_string_t
pg_span_url_encode(pg_allocator_t allocator, pg_span_t src) {
  pg_string_t res = pg_string_make_reserve(allocator, 3 * src.len);

  for (uint64_t i = 0; i < src.len; i++) {
    char buf[4] = {0};
    const uint64_t len =
        (uint64_t)(snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)src.data[i]));
    res = pg_string_append_length(res, buf, len);
  }

  return res;
}

__attribute__((unused)) static pg_span_t pg_span_make(pg_string_t s) {
  return (pg_span_t){.data = s, .len = pg_string_len(s)};
}

__attribute__((unused)) static pg_span_t pg_span_make_c(char *s) {
  return (pg_span_t){.data = s, .len = strlen(s)};
}

__attribute__((unused)) static bool pg_span_starts_with(pg_span_t haystack,
                                                        pg_span_t needle) {
  if (needle.len > haystack.len)
    return false;
  return pg_memmem(haystack.data, needle.len, needle.data, needle.len) != NULL;
}

__attribute__((unused)) static bool pg_span_eq(pg_span_t a, pg_span_t b) {
  return a.len == b.len && memcmp(a.data, b.data, a.len) == 0;
}

__attribute__((unused)) static bool pg_span_ieq(pg_span_t a, pg_span_t b) {
  if (a.len != b.len)
    return false;

  for (uint64_t i = 0; i < a.len; i++) {
    char a_c = pg_char_to_lower(a.data[i]);
    char b_c = pg_char_to_lower(b.data[i]);
    if (a_c != b_c)
      return false;
  }
  return true;
}

__attribute__((unused)) static int64_t pg_span_parse_i64_hex(pg_span_t span,
                                                             bool *valid) {
  pg_span_trim(&span);

  uint64_t res = 0;
  int64_t sign = 1;
  if (pg_span_peek_left(span, NULL) == '-') {
    sign = -1;
    pg_span_consume_left(&span, 1);
  } else if (pg_span_peek_left(span, NULL) == '+') {
    pg_span_consume_left(&span, 1);
  }

  if (pg_span_starts_with(span, pg_span_make_c("0x")))
    pg_span_consume_left(&span, 2);

  for (uint64_t i = 0; i < span.len; i++) {
    if (!pg_char_is_alphanumeric(span.data[i])) {
      *valid = false;
      return 0;
    }

    res *= 16;
    uint64_t n = 0;
    char c = pg_char_to_lower(span.data[i]);

    if (c == 'a')
      n = 10;
    else if (c == 'b')
      n = 11;
    else if (c == 'c')
      n = 12;
    else if (c == 'd')
      n = 13;
    else if (c == 'e')
      n = 14;
    else if (c == 'f')
      n = 15;
    else if (pg_char_is_digit(c))
      n = ((uint8_t)c) - '0';
    else {
      *valid = false;
      return 0;
    }
    res += n;
  }

  *valid = true;
  return sign * (int64_t)res;
}

__attribute__((unused)) static uint64_t pg_span_parse_u64_hex(pg_span_t span,
                                                              bool *valid) {
  pg_span_trim(&span);

  uint64_t res = 0;
  if (pg_span_peek_left(span, NULL) == '-') {
    *valid = false;
    return 0;
  } else if (pg_span_peek_left(span, NULL) == '+') {
    pg_span_consume_left(&span, 1);
  }

  if (pg_span_starts_with(span, pg_span_make_c("0x")))
    pg_span_consume_left(&span, 2);

  for (uint64_t i = 0; i < span.len; i++) {
    if (!pg_char_is_alphanumeric(span.data[i])) {
      *valid = false;
      return 0;
    }

    res *= 16;
    uint64_t n = 0;
    char c = pg_char_to_lower(span.data[i]);

    if (c == 'a')
      n = 10;
    else if (c == 'b')
      n = 11;
    else if (c == 'c')
      n = 12;
    else if (c == 'd')
      n = 13;
    else if (c == 'e')
      n = 14;
    else if (c == 'f')
      n = 15;
    else if (pg_char_is_digit(c))
      n = ((uint8_t)c) - '0';
    else {
      *valid = false;
      return 0;
    }
    res += n;
  }

  *valid = true;
  return res;
}

__attribute__((unused)) static int64_t pg_span_parse_i64_decimal(pg_span_t span,
                                                                 bool *valid) {
  pg_span_trim(&span);

  int64_t res = 0;
  int64_t sign = 1;

  if (pg_span_peek_left(span, NULL) == '-') {
    sign = -1;
    pg_span_consume_left(&span, 1);
  } else if (pg_span_peek_left(span, NULL) == '+') {
    pg_span_consume_left(&span, 1);
  }

  for (uint64_t i = 0; i < span.len; i++) {
    if (!pg_char_is_digit(span.data[i])) {
      *valid = false;
      return 0;
    }

    res *= 10;
    res += (uint8_t)span.data[i] - '0';
  }
  *valid = true;
  return sign * res;
}

__attribute__((unused)) static uint64_t
pg_span_parse_u64_decimal(pg_span_t span, bool *valid) {
  pg_span_trim(&span);

  uint64_t res = 0;

  if (pg_span_peek_left(span, NULL) == '-') {
    *valid = false;
    return 0;
  } else if (pg_span_peek_left(span, NULL) == '+') {
    pg_span_consume_left(&span, 1);
  }

  for (uint64_t i = 0; i < span.len; i++) {
    if (!pg_char_is_digit(span.data[i])) {
      *valid = false;
      return 0;
    }

    res *= 10;
    res += (uint8_t)span.data[i] - '0';
  }
  *valid = true;
  return res;
}

// -------------------------- Log

typedef enum {
  PG_LOG_DEBUG,
  PG_LOG_INFO,
  PG_LOG_ERROR,
  PG_LOG_FATAL,
} pg_log_level_t;

typedef struct {
  pg_log_level_t level;
} pg_logger_t;

#define pg_log_debug(logger, fmt, ...)                                         \
  do {                                                                         \
    if ((logger) != NULL && (logger)->level <= PG_LOG_DEBUG)                   \
      fprintf(stderr, "%s[DEBUG] " fmt "%s\n",                                 \
              (isatty(2) ? "\x1b[38:5:240m" : ""), ##__VA_ARGS__,              \
              (isatty(2) ? "\x1b[0m" : ""));                                   \
  } while (0)

#define pg_log_info(logger, fmt, ...)                                          \
  do {                                                                         \
    if ((logger) != NULL && (logger)->level <= PG_LOG_INFO)                    \
      fprintf(stderr, "%s[INFO] " fmt "%s\n", (isatty(2) ? "\x1b[32m" : ""),   \
              ##__VA_ARGS__, (isatty(2) ? "\x1b[0m" : ""));                    \
  } while (0)

#define pg_log_error(logger, fmt, ...)                                         \
  do {                                                                         \
    if ((logger) != NULL && (logger)->level <= PG_LOG_ERROR)                   \
      fprintf(stderr, "%s[ERROR] " fmt "%s\n", (isatty(2) ? "\x1b[31m" : ""),  \
              ##__VA_ARGS__, (isatty(2) ? "\x1b[0m" : ""));                    \
  } while (0)

#define pg_log_fatal(logger, exit_code, fmt, ...)                              \
  do {                                                                         \
    if ((logger) != NULL && (logger)->level <= PG_LOG_FATAL) {                 \
      fprintf(stderr, "%s[FATAL] " fmt "%s\n",                                 \
              (isatty(2) ? "\x1b[38:5:124m" : ""), ##__VA_ARGS__,              \
              (isatty(2) ? "\x1b[0m" : ""));                                   \
      exit((int)exit_code);                                                    \
    }                                                                          \
  } while (0)

// -------------------------- Ring buffer of bytes

typedef struct {
  uint8_t *data;
  uint64_t len, offset, cap;
  pg_allocator_t allocator;
} pg_ring_t;

__attribute__((unused)) static void
pg_ring_init(pg_allocator_t allocator, pg_ring_t *ring, uint64_t cap) {
  assert(cap > 0);

  ring->len = ring->offset = 0;
  ring->data = allocator.realloc(NULL, cap, 0);
  ring->cap = cap;
  ring->allocator = allocator;
}

__attribute__((unused)) static uint64_t pg_ring_len(pg_ring_t *ring) {
  return ring->len;
}

__attribute__((unused)) static uint64_t pg_ring_cap(pg_ring_t *ring) {
  return ring->cap;
}

__attribute__((unused)) static void pg_ring_destroy(pg_ring_t *ring) {
  ring->allocator.free(ring->data);
}

__attribute__((unused)) static uint8_t *pg_ring_get_ptr(pg_ring_t *ring,
                                                        uint64_t i) {
  if (ring->cap == 0)
    return NULL;

  assert(i < ring->cap);
  const uint64_t index = (i + ring->offset) % ring->cap;
  return &ring->data[index];
}

__attribute__((unused)) static uint8_t pg_ring_get(pg_ring_t *ring,
                                                   uint64_t i) {
  return *pg_ring_get_ptr(ring, i);
}

__attribute__((unused)) static uint8_t *pg_ring_front_ptr(pg_ring_t *ring) {
  assert(ring->offset < ring->cap);
  return &ring->data[ring->offset];
}

__attribute__((unused)) static uint8_t pg_ring_front(pg_ring_t *ring) {
  return *pg_ring_front_ptr(ring);
}

__attribute__((unused)) static uint8_t *pg_ring_back_ptr(pg_ring_t *ring) {
  if (ring->cap == 0)
    return NULL;

  const uint64_t index = (ring->offset + ring->len - 1) % ring->cap;
  return &ring->data[index];
}

__attribute__((unused)) static uint8_t pg_ring_back(pg_ring_t *ring) {
  return *pg_ring_back_ptr(ring);
}

__attribute__((unused)) static void pg_ring_push_back(pg_ring_t *ring,
                                                      uint8_t x) {
  assert(ring->len < ring->cap);

  const uint64_t index = (ring->offset + ring->len) % ring->cap;
  assert(index < ring->cap);
  ring->data[index] = x;
  ring->len += 1;
}

__attribute__((unused)) static void pg_ring_push_front(pg_ring_t *ring,
                                                       uint8_t x) {
  assert(ring->len < ring->cap);

  ring->offset = (ring->offset - 1 + ring->cap) % ring->cap;
  assert(ring->offset < ring->cap);
  ring->data[ring->offset] = x;
  ring->len += 1;
}

__attribute__((unused)) static uint8_t pg_ring_pop_back(pg_ring_t *ring) {
  assert(ring->len > 0);
  ring->len -= 1;
  const uint64_t index = (ring->offset + ring->len) % ring->cap;
  assert(index < ring->cap);
  return ring->data[index];
}

__attribute__((unused)) static uint8_t pg_ring_pop_front(pg_ring_t *ring) {
  assert(ring->len > 0);
  assert(ring->offset < ring->cap);
  const uint8_t res = ring->data[ring->offset];

  ring->offset = (ring->offset + 1) % ring->cap;
  ring->len -= 1;

  return res;
}

__attribute__((unused)) static void pg_ring_consume_front(pg_ring_t *ring,
                                                          uint64_t n) {
  assert(n <= ring->len);
  ring->offset = (ring->offset + n) % ring->cap;
  ring->len -= n;
}

__attribute__((unused)) static void pg_ring_consume_back(pg_ring_t *ring,
                                                         uint64_t n) {
  assert(n <= ring->len);
  ring->len -= n;
}

__attribute__((unused)) static void pg_ring_clear(pg_ring_t *ring) {
  ring->offset = 0;
  ring->len = 0;
}

__attribute__((unused)) static uint64_t pg_ring_space(pg_ring_t *ring) {
  return ring->cap - ring->len;
}

__attribute__((unused)) static void
pg_ring_push_backv(pg_ring_t *ring, const uint8_t *data, uint64_t len) {
  assert(ring->len + len <= ring->cap);

  const uint64_t index = (ring->offset + ring->len) % ring->cap;

  // Fill the tail
  const uint64_t space_tail = ring->cap - index;
  uint64_t to_write_tail_count = len;
  if (to_write_tail_count > space_tail)
    to_write_tail_count = space_tail;
  assert(index + to_write_tail_count <= ring->cap);
  memcpy(ring->data + index, data, to_write_tail_count);

  // Fill the head
  const uint64_t remain = len - to_write_tail_count;
  memcpy(ring->data, data + to_write_tail_count, remain);
  ring->len += len;
}

// -------------------------- bitarray

typedef struct {
  pg_array_t(uint8_t) data;
  uint64_t max_index;
} pg_bitarray_t;

__attribute__((unused)) static void pg_bitarray_init(pg_allocator_t allocator,
                                                     pg_bitarray_t *bitarr,
                                                     uint64_t max_index) {
  bitarr->max_index = max_index;
  const uint64_t len = 1 + (uint64_t)(ceil(((double)max_index) / 8.0));
  pg_array_init_reserve(bitarr->data, len, allocator);
  pg_array_resize(bitarr->data, len);
}

__attribute__((unused)) static void
pg_bitarray_setv(pg_bitarray_t *bitarr, uint8_t *data, uint64_t len) {
  assert(len <= 1 + bitarr->max_index);

  memcpy(bitarr->data, data, len);
}

__attribute__((unused)) static void pg_bitarray_destroy(pg_bitarray_t *bitarr) {
  pg_array_free(bitarr->data);
}

__attribute__((unused)) static uint64_t
pg_bitarray_len(const pg_bitarray_t *bitarr) {
  return bitarr->max_index + 1;
}

__attribute__((unused)) static void pg_bitarray_set(pg_bitarray_t *bitarr,
                                                    uint64_t index) {
  const uint64_t i = (uint64_t)((double)(index) / 8.0);
  assert(i < pg_array_len(bitarr->data));

  bitarr->data[i] |= 1 << (index % 8);
}

__attribute__((unused)) static bool pg_bitarray_get(const pg_bitarray_t *bitarr,
                                                    uint64_t index) {
  const uint64_t i = (uint64_t)((double)(index) / 8.0);
  assert(i < pg_array_len(bitarr->data));

  return bitarr->data[i] & (1 << (index % 8));
}

__attribute__((unused)) static bool
pg_bitarray_next(const pg_bitarray_t *bitarr, uint64_t *index, bool *is_set) {
  if (*index >= pg_bitarray_len(bitarr))
    return false;

  *is_set = pg_bitarray_get(bitarr, *index);
  *index += 1;
  return true;
}

__attribute__((unused)) static void pg_bitarray_unset(pg_bitarray_t *bitarr,
                                                      uint64_t index) {
  const uint64_t i = (uint64_t)((double)(index) / 8.0);
  assert(i < pg_array_len(bitarr->data));

  bitarr->data[i] &= ~(1 << (index % 8));
}

__attribute__((unused)) static uint64_t
pg_bitarray_count_set(pg_bitarray_t *bitarr) {
  uint64_t res = 0;
  for (uint64_t i = 0; i < pg_bitarray_len(bitarr); i++) {
    res += pg_bitarray_get(bitarr, i);
  }
  return res;
}

__attribute__((unused)) static uint64_t
pg_bitarray_count_unset(pg_bitarray_t *bitarr) {
  uint64_t res = 0;
  for (uint64_t i = 0; i < pg_bitarray_len(bitarr); i++) {
    res += !pg_bitarray_get(bitarr, i);
  }
  return res;
}

__attribute__((unused)) static bool
pg_bitarray_is_all_set(pg_bitarray_t *bitarr) {
  for (uint64_t i = 0; i < pg_bitarray_len(bitarr); i++) {
    if (!pg_bitarray_get(bitarr, i))
      return false;
  }
  return true;
}

__attribute__((unused)) static bool
pg_bitarray_is_all_unset(pg_bitarray_t *bitarr) {
  for (uint64_t i = 0; i < pg_bitarray_len(bitarr); i++) {
    if (pg_bitarray_get(bitarr, i))
      return false;
  }
  return true;
}

__attribute__((unused)) static void pg_bitarray_set_all(pg_bitarray_t *bitarr) {
  memset(bitarr->data, 0xff, pg_array_len(bitarr->data));
}

__attribute__((unused)) static void
pg_bitarray_unset_all(pg_bitarray_t *bitarr) {
  memset(bitarr->data, 0, pg_array_len(bitarr->data));
}

__attribute__((unused)) static void pg_bitarray_resize(pg_bitarray_t *bitarr,
                                                       uint64_t max_index) {
  bitarr->max_index = max_index;
  const uint64_t len = (uint64_t)(ceil(((double)max_index) / 8.0));
  pg_array_resize(bitarr->data, len);
}

// ------------- File utils

__attribute__((unused)) static bool
pg_array_read_file_fd(int fd, pg_array_t(uint8_t) * buf) {
  struct stat st = {0};
  if (fstat(fd, &st) == -1) {
    return false;
  }
  const uint64_t read_buffer_size =
      MIN((uint64_t)INT32_MAX, (uint64_t)st.st_size);
  pg_array_grow(*buf, (uint64_t)st.st_size);
  while (pg_array_len(*buf) < (uint64_t)st.st_size) {
    int64_t ret = read(fd, *buf + pg_array_len(*buf), read_buffer_size);
    if (ret == -1) {
      return false;
    }
    if (ret == 0)
      return true;
    pg_array_resize(*buf, pg_array_len(*buf) + (uint64_t)ret);
  }
  return true;
}

__attribute__((unused)) static bool pg_string_read_file_fd(int fd,
                                                           pg_string_t *str) {
  struct stat st = {0};
  if (fstat(fd, &st) == -1) {
    return false;
  }
  const uint64_t read_buffer_size =
      MIN((uint64_t)INT32_MAX, (uint64_t)st.st_size);
  *str = pg_string_make_space_for(*str, (uint64_t)st.st_size);

  while (pg_string_len(*str) < (uint64_t)st.st_size) {
    int64_t ret = read(fd, *str + pg_string_len(*str), read_buffer_size);
    if (ret == -1) {
      return false;
    }
    if (ret == 0)
      return true;
    const uint64_t new_len = pg_string_len(*str) + (uint64_t)ret;
    *str = pg_string_make_space_for(*str, new_len);
    pg__set_string_len(*str, new_len);
  }
  return true;
}

__attribute__((unused)) static bool pg_read_file(char *path,
                                                 pg_array_t(uint8_t) * buf) {
  const int fd = open(path, O_RDONLY);
  if (fd == -1) {
    return errno;
  }
  const bool ok = pg_array_read_file_fd(fd, buf);
  close(fd);
  return ok;
}

__attribute__((unused)) static char const *pg_path_base_name(char const *path) {
  char const *ls;
  assert(path != NULL);
  ls = pg_char_last_occurence(path, '/');
  return (ls == NULL) ? path : ls + 1;
}
__attribute__((unused)) static bool
pg_string_read_from_stream_once(int fd, pg_string_t *str) {
  const uint64_t read_batch_size = 4096;
  if (pg_string_available_space(*str) < read_batch_size)
    *str =
        pg_string_make_space_for(*str, pg_string_len(*str) + read_batch_size);

  const int64_t ret = read(fd, *str + pg_string_len(*str), read_batch_size);
  if (ret == -1)
    return false;
  if (ret == 0)
    return true;

  const uint64_t new_len = pg_string_len(*str) + (uint64_t)ret;
  pg__set_string_len(*str, new_len);

  return true;
}

__attribute__((unused)) static bool
pg_string_read_from_stream(int fd, pg_string_t *str) {
  while (true) {
    if (!pg_string_read_from_stream_once(fd, str))
      return false;
  }
  __builtin_unreachable();
}

// ------------------------------------- Child process
__attribute__((unused)) static bool pg_exec(char **argv, pg_string_t *cmd_stdio,
                                            pg_string_t *cmd_stderr,
                                            int *exit_status) {
  int stdio_pipe[2] = {0};
  if (pipe(stdio_pipe) != 0) {
    return false;
  }

  int stderr_pipe[2] = {0};
  if (pipe(stderr_pipe) != 0) {
    return false;
  }

  const pid_t pid = fork();
  if (pid == -1)
    return false;

  if (pid == 0) {          // Child
    close(stdio_pipe[0]);  // Child does not read from parent
    close(stderr_pipe[0]); // Child does not read from parent
    close(0);              // Close child's stdin

    if (dup2(stdio_pipe[1], 1) ==
        -1) // Direct child's stdout to the pipe for the parent to read
      exit(errno);

    if (dup2(stderr_pipe[1], 2) ==
        -1) // Direct child's stderr to the pipe for the parent to read
      exit(errno);

    close(stdio_pipe[1]);  // Not needed anymore
    close(stderr_pipe[1]); // Not needed anymore

    if (execvp(argv[0], argv) == -1)
      exit(errno);

    __builtin_unreachable();
  }

  struct pollfd fds[] = {{.fd = stdio_pipe[0], .events = POLLIN},
                         {.fd = stderr_pipe[0], .events = POLLIN}};
  pid_t ret_pid = 0;
  while (1) {
    const int ret = poll(fds, 2, 200);
    if (ret == -1)
      break;

    for (uint64_t i = 0; i < (uint64_t)ret; i++) {
      if (i == 0 && (fds[i].revents & POLLIN)) {
        if (!pg_string_read_from_stream_once(fds[i].fd, cmd_stdio))
          break;
      }
      if (i == 1 && (fds[i].revents & POLLIN)) {
        if (!pg_string_read_from_stream_once(fds[i].fd, cmd_stderr))
          break;
      }
    }

    ret_pid = waitpid(pid, exit_status, 0);
    if (ret_pid == -1)
      continue;

    if (WIFEXITED(*exit_status)) {
      goto end;
    }
  }

  ret_pid = waitpid(pid, exit_status, 0);
  if (ret_pid == -1) {
    fprintf(stderr, "Failed to wait(2): %d %s\n", errno, strerror(errno));
    exit(errno);
  }

end:
  close(stdio_pipe[0]);
  close(stderr_pipe[0]);

  return true;
}
// --------------------- Path

__attribute__((unused)) static bool pg_path_is_directory(const char *path) {
  assert(path != NULL);

  struct stat s = {0};
  if (stat(path, &s) == -1) {
    return false;
  }
  return S_ISDIR(s.st_mode);
}
