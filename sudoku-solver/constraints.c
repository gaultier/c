#include <bits/stdint-uintn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define PG_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#define pg_assert(condition)                                                   \
  do {                                                                         \
    if (!(condition))                                                          \
      __builtin_trap();                                                        \
  } while (0)

typedef enum {
  VALIDITY_UNKNOWN = 0,
  VALIDITY_INVALID = 1,
  VALIDITY_VALID = 2,
  VALIDITY_INCOMPLETE = 4
} validity_t;

static validity_t is_row_valid(const uint8_t *grid, uint8_t row) {
  pg_assert(grid != 0);
  pg_assert(row < 9);

  uint8_t seen[10] = {0};
  for (uint8_t k = 0; k < 9; k++) {
    const uint8_t position = row * 9 + k;
    pg_assert(position < 9 * 9);

    const uint8_t value = grid[position];
    pg_assert(value <= 9);

    seen[value]++;
    pg_assert(seen[value] <= 9);
  }

  for (uint8_t i = 1; i <= 9; i++) {
    if (seen[i] > 1)
      return VALIDITY_INVALID;
  }
  if (seen[0] > 0)
    return VALIDITY_INCOMPLETE;

  pg_assert(seen[0] == 0 && seen[1] == 1 && seen[2] == 1 && seen[3] == 1 &&
            seen[4] == 1 && seen[5] == 1 && seen[6] == 1 && seen[7] == 1 &&
            seen[8] == 1);
  return VALIDITY_VALID;
}

static validity_t is_block_valid(const uint8_t *grid, uint8_t block) {
  pg_assert(grid != 0);
  pg_assert(block < 9);

  const uint8_t position = block / 3 * 3 * 9 + (block % 3) * 3;
  pg_assert(position < 9 * 9);
  const uint8_t start_i = position / 9;
  pg_assert(start_i < 9);
  const uint8_t start_j = position % 9;
  pg_assert(start_j < 9);

  uint8_t seen[10] = {0};
  for (uint8_t i = 0; i < 3; i++) {
    for (uint8_t j = 0; j < 3; j++) {
      const uint8_t block_position = (start_i * 9 + i * 3) + (start_j + j);
      pg_assert(block_position < 9 * 9);
      const uint8_t value = grid[block_position];
      pg_assert(value <= 9);

      seen[value]++;
      pg_assert(seen[value] <= 9);
    }
  }

  for (uint8_t i = 1; i <= 9; i++) {
    if (seen[i] > 1)
      return VALIDITY_INVALID;
  }
  if (seen[0] > 0)
    return VALIDITY_INCOMPLETE;

  pg_assert(seen[0] == 0 && seen[1] == 1 && seen[2] == 1 && seen[3] == 1 &&
            seen[4] == 1 && seen[5] == 1 && seen[6] == 1 && seen[7] == 1 &&
            seen[8] == 1);
  return VALIDITY_VALID;
}

static validity_t is_column_valid(const uint8_t *grid, uint8_t column) {
  pg_assert(grid != 0);
  pg_assert(column < 9);

  uint8_t seen[10] = {0};
  for (uint8_t k = column; k < 9 * 9; k += 9) {
    pg_assert(k < 9 * 9);

    const uint8_t value = grid[k];
    pg_assert(value <= 9);

    seen[value]++;
    pg_assert(seen[value] <= 9);
  }

  for (uint8_t i = 1; i <= 9; i++) {
    if (seen[i] > 1)
      return VALIDITY_INVALID;
  }
  if (seen[0] > 0)
    return VALIDITY_INCOMPLETE;

  pg_assert(seen[0] == 0 && seen[1] == 1 && seen[2] == 1 && seen[3] == 1 &&
            seen[4] == 1 && seen[5] == 1 && seen[6] == 1 && seen[7] == 1 &&
            seen[8] == 1);
  return VALIDITY_VALID;
}

static void print_grid(const uint8_t *grid) {
  pg_assert(grid != 0);

  for (uint8_t i = 0; i < 9; i++) {
    for (uint8_t j = 0; j < 9; j++) {
      printf("%d ", grid[i * 9 + j]);
    }
    puts("");
  }
}

static validity_t is_grid_valid(const uint8_t *grid) {
  validity_t validity = 0;
  for (uint8_t r = 0; r < 9; r++) {
    validity |= is_row_valid(grid, r);
  }
  for (uint8_t c = 0; c < 9; c++) {
    validity |= is_column_valid(grid, c);
  }

  for (uint8_t b = 0; b < 9; b++) {
    validity |= is_block_valid(grid, b);
  }

  return validity;
}

static uint8_t possibilities[9 * 9 * 9] = {0};

int main() {
#if 0
  uint8_t grid[9 * 9] = {
      // clang-format off
    5,3,0,0,7,0,0,0,0,
    6,0,0,1,9,5,0,0,0,
    0,9,8,0,0,0,0,6,0,
    8,0,0,0,6,0,0,0,3,
    4,0,0,8,0,3,0,0,1,
    7,0,0,0,2,0,0,0,6,
    0,6,0,0,0,0,2,8,0,
    0,0,0,4,1,9,0,0,5,
    0,0,0,0,8,0,0,7,9,
      // clang-format on
  };
#endif
  uint8_t grid[9 * 9] = {
      // clang-format off
    5,3,0,6,7,8,9,1,2,
    6,7,2,1,9,5,0,4,8,
    1,9,8,3,4,2,5,6,7,
    8,5,9,7,6,1,4,2,3,
    4,2,6,8,5,3,7,9,1,
    7,1,3,9,2,4,8,5,6,
    9,6,1,5,3,7,2,8,4,
    2,8,7,4,1,9,6,3,5,
    3,4,5,2,8,6,1,7,9,
      // clang-format on
  };

#if 0
  const uint8_t final[9 * 9] = {
      // clang-format off
    5,3,4,6,7,8,9,1,2,
    6,7,2,1,9,5,3,4,8,
    1,9,8,3,4,2,5,6,7,
    8,5,9,7,6,1,4,2,3,
    4,2,6,8,5,3,7,9,1,
    7,1,3,9,2,4,8,5,6,
    9,6,1,5,3,7,2,8,4,
    2,8,7,4,1,9,6,3,5,
    3,4,5,2,8,6,1,7,9,
      // clang-format on
  };
#endif

  for (uint8_t i = 0; i < 9 * 9; i++) {
    const uint8_t value = grid[i];
    pg_assert(value <= 9);

    if (value != 0)
      continue;

    const uint8_t row = i / 9;
    pg_assert(row < 9);

    const uint8_t column = i % 9;
    pg_assert(column < 9);

    const uint8_t block = row / 3 * 3 + column / 3;
    pg_assert(block < 9);

    for (uint8_t j = 1; j <= 9; j++) {
      grid[i] = j;
      if (is_row_valid(grid, row) & VALIDITY_INVALID)
        continue;
      if (is_column_valid(grid, column) & VALIDITY_INVALID)
        continue;
      if (is_block_valid(grid, block) & VALIDITY_INVALID)
        continue;

      pg_assert((uint64_t)i * 9 + (uint64_t)j < 9 * 9 * 9);
      possibilities[(uint64_t)i * 9 + (uint64_t)j] = j;
    }
    // Restore value.
    grid[i] = value;
  }

  for (uint8_t i = 0; i < 9 * 9; i++) {
    printf("[%d]: ", i);

    for (uint8_t j = 1; j <= 9; j++) {
      const uint8_t value = possibilities[(uint64_t)i * 9 + (uint64_t)j];
      pg_assert(value <= 9);
      if (value == 0)
        continue;
      printf("%d ", j);

      pg_assert(grid[i] == 0);
    }
    puts("");
  }
}
