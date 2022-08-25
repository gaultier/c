#include <_types/_uint64_t.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NUMS_LEN 7
bool is_sum_of_2_present(uint64_t* nums, uint64_t target_sum) {
    // TODO: make it a hashmap for O(1) find
    uint64_t num_minus_sum[NUMS_LEN] = {0};
    for (uint64_t i = 0; i < NUMS_LEN; i++) {
        const uint64_t n = nums[i];
        num_minus_sum[i] = target_sum - n;
    }

    for (uint64_t i = 0; i < NUMS_LEN - 1; i++) {
        const uint64_t a = nums[i];

        for (uint64_t j = 0; j < i; j++) {
            const uint64_t b = num_minus_sum[j];
            if (b == a) {
                printf("a=%llu b=%lld\n", a, target_sum - b);
                return true;
            }
        }
    }

    return false;
}

int main() {
    uint64_t nums[NUMS_LEN] = {5, 7, 1, 2, 8, 4, 3};

    printf("%d\n", is_sum_of_2_present(nums, 10));
}
