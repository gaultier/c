#include <inttypes.h>
#include <stdio.h>

#include "hashmap_u64.h"

int main() {
    pg_hashmap_u64_t hashmap = {0};
    pg_hashmap_init(&hashmap, 50);
    pg_hashmap_add(&hashmap, 2, 32);
    pg_hashmap_add(&hashmap, 4, 64);
    pg_hashmap_add(&hashmap, 1, 3);
    pg_hashmap_add(&hashmap, 9, 10);

    uint64_t val = 0;
    bool found = false;
    uint32_t hash = 0;
    uint64_t index = 0;

    index = pg_hashmap_find_entry_index(&hashmap, 2, &val, &found, &hash);
    printf("k=%u: v=%llu found=%d index=%llu\n", 2, val, found, index);

    index = pg_hashmap_find_entry_index(&hashmap, 4, &val, &found, &hash);
    printf("k=%u: v=%llu found=%d index=%llu\n", 4, val, found, index);

    index = pg_hashmap_find_entry_index(&hashmap, 1, &val, &found, &hash);
    printf("k=%u: v=%llu found=%d index=%llu\n", 1, val, found, index);

    index = pg_hashmap_find_entry_index(&hashmap, 9, &val, &found, &hash);
    printf("k=%u: v=%llu found=%d index=%llu\n", 9, val, found, index);

    index = pg_hashmap_find_entry_index(&hashmap, 10, &val, &found, &hash);
    printf("k=%u: v=%llu found=%d index=%llu\n", 10, val, found, index);
}
