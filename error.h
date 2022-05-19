#pragma once
#include <stdio.h>

#define GB_IMPLEMENTATION
// FIXME
#define GB_ALLOC malloc
#define GB_FREE free
#include "./vendor/gb.h"

typedef struct {
  int line;
  gbString file;
  gbString function;
} location;

#define CURRENT_LOCATION(allocator)                                      \
  ((location){.line = __LINE__,                                          \
              .file = gb_string_make_length(allocator, __FILE__,         \
                                            strlen(__FILE__) + 1),       \
              .function = gb_string_make_length(allocator, __FUNCTION__, \
                                                strlen(__FILE__) + 1)})

typedef struct {
  gbArray(location) call_stack;
  gbArray(gbString) errors;
} error;

#define error_record(allocator, err, fmt, ...)                                 \
  do {                                                                         \
    if (err == NULL) err = error_make(allocator);                              \
    GB_ASSERT(fmt != NULL);                                                    \
                                                                               \
    gb_array_append(err->call_stack, CURRENT_LOCATION(allocator));             \
                                                                               \
    gbString msg = gb_string_append_fmt(gb_string_make_reserve(allocator, 40), \
                                        fmt, __VA_ARGS__);                     \
                                                                               \
    gb_array_append(err->errors, msg);                                         \
    return err;                                                                \
  } while (0)

static error* error_make(gbAllocator allocator) {
  error* err = gb_alloc(allocator, sizeof(error));
  GB_ASSERT(err != NULL);

  gb_array_init(err->call_stack, allocator);
  gb_array_init(err->errors, allocator);
  return err;
}

static void error_print(error* err) {
  GB_ASSERT(err != NULL);

  for (int i = 0; i < gb_array_count(err->call_stack); i++) {
    const location* loc = &err->call_stack[i];
    const gbString msg = err->errors[i];
    fprintf(stderr, "%s:%d:%s: %s\n", loc->file, loc->line, loc->function, msg);
  }
}
