#include <stdio.h>

#include "pg.h"

int main() {
    pg_array_t(int) array;
    pg_array_init_reserve(array, 10, pg_heap_allocator());

    pg_array_append(array, 1);
    pg_array_append(array, 2);

    for (uint64_t i = 0; i < pg_array_count(array); i++) {
        printf("%d ", array[i]);
    }
    pg_array_free(array);
    assert(array == NULL);
}
