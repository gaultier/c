#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hashmap_u64.h"

#define NUMS_LEN 7

bool is_sum_of_2_present(uint64_t* nums, uint64_t target_sum) {
    pg_hashmap_u64_t hashmap = {0};
    pg_hashmap_init(&hashmap, 50);
    for (uint64_t i = 0; i < NUMS_LEN; i++) {
        const uint64_t n = nums[i];
        pg_hashmap_add(&hashmap, n, 0);
    }

    for (uint64_t i = 0; i < NUMS_LEN - 1; i++) {
        const uint64_t a = nums[i];
        if (pg_hashmap_exists(&hashmap, target_sum - a)) {
            return true;
        }
    }

    return false;
}

int main() {
    uint64_t nums[NUMS_LEN] = {5, 7, 1, 2, 8, 4, 3};

    printf("%d\n", is_sum_of_2_present(nums, 15));
}
