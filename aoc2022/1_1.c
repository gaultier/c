#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>

#define COUNT 4
static uint16_t input[COUNT][16] = {
    {1000, 2000, 3000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 0
    {4000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},       // 1
    {7000, 8000, 9000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 2
    {10000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 3

};

static uint64_t sums[COUNT];

int main(void) {
  const __m256i a = _mm256_loadu_epi16(input[0]);
  const __m256i b = _mm256_loadu_epi16(input[1]);
  const __m256i diffab = _mm256_sub_epi16(a, b);

  const __m256i c = _mm256_loadu_epi16(input[2]);
  const __m256i d = _mm256_loadu_epi16(input[3]);
  const __m256i diffcd = _mm256_sub_epi16(c, d);

  printf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
         (int16_t)_mm256_extract_epi16(diffab, 0),
         _mm256_extract_epi16(diffab, 1),
         (int16_t)_mm256_extract_epi16(diffab, 2),
         (int16_t)_mm256_extract_epi16(diffab, 3),
         (int16_t)_mm256_extract_epi16(diffab, 4),
         (int16_t)_mm256_extract_epi16(diffab, 5),
         (int16_t)_mm256_extract_epi16(diffab, 6),
         (int16_t)_mm256_extract_epi16(diffab, 7),
         (int16_t)_mm256_extract_epi16(diffab, 8),
         (int16_t)_mm256_extract_epi16(diffab, 9),
         (int16_t)_mm256_extract_epi16(diffab, 10),
         (int16_t)_mm256_extract_epi16(diffab, 11),
         (int16_t)_mm256_extract_epi16(diffab, 12),
         (int16_t)_mm256_extract_epi16(diffab, 13),
         (int16_t)_mm256_extract_epi16(diffab, 14),
         (int16_t)_mm256_extract_epi16(diffab, 15));

  printf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
         (int16_t)_mm256_extract_epi16(diffcd, 0),
         _mm256_extract_epi16(diffcd, 1),
         (int16_t)_mm256_extract_epi16(diffcd, 2),
         (int16_t)_mm256_extract_epi16(diffcd, 3),
         (int16_t)_mm256_extract_epi16(diffcd, 4),
         (int16_t)_mm256_extract_epi16(diffcd, 5),
         (int16_t)_mm256_extract_epi16(diffcd, 6),
         (int16_t)_mm256_extract_epi16(diffcd, 7),
         (int16_t)_mm256_extract_epi16(diffcd, 8),
         (int16_t)_mm256_extract_epi16(diffcd, 9),
         (int16_t)_mm256_extract_epi16(diffcd, 10),
         (int16_t)_mm256_extract_epi16(diffcd, 11),
         (int16_t)_mm256_extract_epi16(diffcd, 12),
         (int16_t)_mm256_extract_epi16(diffcd, 13),
         (int16_t)_mm256_extract_epi16(diffcd, 14),
         (int16_t)_mm256_extract_epi16(diffcd, 15));

  /*
  for (uint64_t i = 0; i < COUNT; i++) {
    for (uint64_t j = 0; j < 16; j++) {
      sums[i] += input[i][j];
    }
  }

  uint64_t max =0;
    for (uint64_t i = 0; i < COUNT; i++) {
        if (max < sums[i]) max = sums[i];
    }
    printf("%llu\n", max);
*/
  __m256i max = _mm256_max_epi16(diffab, diffcd);
  printf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
         (int16_t)_mm256_extract_epi16(max, 0), _mm256_extract_epi16(max, 1),
         (int16_t)_mm256_extract_epi16(max, 2),
         (int16_t)_mm256_extract_epi16(max, 3),
         (int16_t)_mm256_extract_epi16(max, 4),
         (int16_t)_mm256_extract_epi16(max, 5),
         (int16_t)_mm256_extract_epi16(max, 6),
         (int16_t)_mm256_extract_epi16(max, 7),
         (int16_t)_mm256_extract_epi16(max, 8),
         (int16_t)_mm256_extract_epi16(max, 9),
         (int16_t)_mm256_extract_epi16(max, 10),
         (int16_t)_mm256_extract_epi16(max, 11),
         (int16_t)_mm256_extract_epi16(max, 12),
         (int16_t)_mm256_extract_epi16(max, 13),
         (int16_t)_mm256_extract_epi16(max, 14),
         (int16_t)_mm256_extract_epi16(max, 15));
}
