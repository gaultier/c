#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#define CHECK_BIT(var, pos) ((var) & (1 << (pos)))
void print_combination(uint32_t* combination, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        printf("%d ", combination[i]);
    }
    puts("");
}

int main() {
    uint32_t numbers[] = {1, 2, 3, 4};
    const uint32_t len = sizeof(numbers) / sizeof(numbers[0]);

    uint32_t out[len] = {0};
    uint32_t out_len = 0;

    for (uint64_t i = 1; i <= pow(2, len); i++) {
        for (uint32_t j = 0; j < len; j++) {
            if (out_len >= 4) break;
            if (CHECK_BIT(i, j)) {
                out[out_len++] = numbers[j];
            }
        }
        print_combination(out, out_len);
        out_len = 0;
    }
}
