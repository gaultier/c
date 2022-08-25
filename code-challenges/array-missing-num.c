#include <stdint.h>
#include <stdio.h>

uint64_t find_missing_num(uint64_t* nums, uint64_t nums_len) {
    uint64_t i = 0, j = 0;
    for (i = 1; i <= nums_len; i++) {
        for (j = 0; j < nums_len; j++) {
            if (i == nums[j]) goto found;
        }
        return i;

    found : {}
    }
    __builtin_unreachable();
}

int main() {
    uint64_t nums[] = {3, 7, 1, 2, 8, 4, 5};

    printf("%llu\n", find_missing_num(nums, sizeof(nums) / sizeof(nums[0])));
}
