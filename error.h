#pragma once

#define GB_IMPLEMENTATION
// FIXME
#define GB_ALLOC malloc
#define GB_FREE free
#include "./vendor/gb.h"

typedef struct {
  gbArray(gbString) call_stack;
  gbArray(gbString) errors;
} error;

static void error_record(error* err, gbString location, gbString message) {
  GB_ASSERT(err != NULL);

  gb_array_append(err->call_stack, location);
  gb_array_append(err->errors, message);
}

static error* error_make(gbAllocator allocator, gbString location,
                         gbString message) {
  error* err = gb_alloc(allocator, sizeof(error));
  GB_ASSERT(err != NULL);

  error_record(err, location, message);
  return err;
}

#define error_make_location(allocator) \
  gb_string_append_fmt("%s:%s:%s", __FILE__, __line__, __FUNCTION__)
