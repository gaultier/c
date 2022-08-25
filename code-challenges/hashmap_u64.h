#pragma once
#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "../pg.h"

typedef struct {
    pg_array_t(uint32_t) hashes;
    pg_array_t(uint64_t) keys;
    pg_array_t(uint64_t) values;
} pg_hashmap_u64_t;

void pg_hashmap_init(pg_hashmap_u64_t* hashmap, uint64_t capacity) {
    assert(hashmap != NULL);

    pg_array_init_reserve(hashmap->hashes, capacity);
    memset(hashmap->hashes, 0, capacity);

    pg_array_init_reserve(hashmap->keys, capacity);
    memset(hashmap->keys, 0, capacity);

    pg_array_init_reserve(hashmap->values, capacity);
    memset(hashmap->values, 0, capacity);
}

// FNV-1a
uint32_t pg_hash(uint8_t* n, uint64_t len) {
    uint32_t hash = 2166136261u;
    for (uint64_t i = 0; i < len; i++) {
        hash ^= (uint8_t)n[i];
        hash *= 16777619;
    }
    return hash;
}

uint64_t pg_hashmap_find_entry_index(const pg_hashmap_u64_t* hashmap,
                                     uint64_t key, uint64_t* value, bool* found,
                                     uint32_t* hash) {
    assert(hashmap != NULL);
    assert(value != NULL);
    assert(found != NULL);
    assert(hash != NULL);

    *found = false;
    *value = 0;

    const uint64_t capacity = pg_array_capacity(hashmap->keys);
    *hash = pg_hash((uint8_t*)&key, 8);
    uint64_t index = *hash % capacity;

    for (;;) {
        const uint32_t h = hashmap->hashes[index];
        if (h == 0)
            break;  // Not found but empty slot so suitable for `pg_hashmap_add`
        if (h == *hash &&
            key == hashmap->keys[index]) {  // Found, after checking for
                                            // collision to be sure
            *value = hashmap->values[index];
            *found = true;
            break;
        }
        // Keep going to find either an empty slot or a matching hash
    }

    return index;
}

void pg_hashmap_add(pg_hashmap_u64_t* hashmap, uint64_t key, uint64_t value) {
    assert(hashmap != NULL);

    uint64_t val = 0;
    bool found = false;
    uint32_t hash = 0;
    const uint64_t index =
        pg_hashmap_find_entry_index(hashmap, key, &val, &found, &hash);
    if (found) {
        // Update the value
        hashmap->values[index] = value;
        return;
    }

    hashmap->hashes[index] = hash;
    pg_array_resize(hashmap->hashes, pg_array_count(hashmap->hashes) + 1);

    hashmap->keys[index] = key;
    pg_array_resize(hashmap->keys, pg_array_count(hashmap->keys) + 1);

    hashmap->values[index] = value;
    pg_array_resize(hashmap->values, pg_array_count(hashmap->values) + 1);
}

bool pg_hashmap_exists(const pg_hashmap_u64_t* hashmap, uint64_t key) {
    uint64_t val = 0;
    bool found = false;
    uint32_t hash = 0;
    pg_hashmap_find_entry_index(hashmap, key, &val, &found, &hash);
    return found;
}
