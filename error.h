#pragma once

#define GB_IMPLEMENTATION
// FIXME
#define GB_ALLOC malloc
#define GB_FREE free
#include "./vendor/gb.h"

typedef struct {
  gbArray(gbString) call_stack;
} error;

error* error_make() {
  error* err = malloc(sizeof(error));
  GB_ASSERT(err != NULL);

  return err;
}
void error_record() {}
