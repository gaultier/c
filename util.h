#pragma once
#define GB_IMPLEMENTATION
#define GB_STATIC
#include "./vendor/gb/gb.h"

static void pg_array_swap_remove_at_index(void *array, usize elem_size,
                                          isize *count, isize index) {
  void *elem_to_remove = array + elem_size * index;
  void *elem_last = array + elem_size * (*count - 1);
  GB_ASSERT(elem_to_remove <= elem_last);

  memcpy(elem_to_remove, elem_last, elem_size);
  bzero(elem_last, elem_size);
  *count -= 1;
}

static bool pg_string_array_contains(char **stack, isize count,
                                     const char *needle) {
  for (isize i = 0; i < count; i++) {
    if (strcmp(stack[i], needle) == 0)
      return true;
  }
  return false;
}
