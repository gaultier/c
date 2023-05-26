#include <bits/stdint-uintn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define pg_assert(condition)                                                   \
  do {                                                                         \
    if (!(condition))                                                          \
      __builtin_trap();                                                        \
  } while (0)

// 0: 0..8
// 1: 9..17
// 2: 18..26
// 3: 27..35
// 4: 36..44
// 5: 45..53
// 6: 54..62
// 7: 63..71
// 8: 72..80
static const uint64_t primes[10] = {1, 2, 3, 5, 7, 11, 13, 17, 19, 23};

static const uint64_t filled_id = 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19 * 23;

static bool is_row_valid(const uint8_t *grid, uint8_t row) {
  pg_assert(grid != 0);
  pg_assert(row < 9);

  uint64_t id = 1;
  for (uint8_t k = row * 9; k < row * 9 + 9; k++) {
    pg_assert(k < 9 * 9);

    const uint8_t value = grid[k];
    pg_assert(value <= 9);

    const uint64_t prime = primes[value];
    pg_assert(prime > 0);
    pg_assert(prime <= 23);

    pg_assert(id > 0);
    id *= prime;
    pg_assert(id > 0);
  }

  return id == filled_id;
}

static bool is_block_valid(const uint8_t *grid, uint8_t block) {
  pg_assert(grid != 0);

  const uint8_t position = block * 3;
  const uint8_t start_i = (position / 3) * 3;
  const uint8_t start_j = position % 9;

  uint64_t id = 1;
  for (uint8_t i = start_i; i < start_i + 3; i++) {
    for (uint8_t j = start_j; j < start_j + 3; j++) {
      const uint8_t block_position = i * 9 + j;
      const uint8_t value = grid[block_position];
      pg_assert(value <= 9);

      const uint64_t prime = primes[value];
      pg_assert(prime > 0);
      pg_assert(prime <= 23);

      pg_assert(id > 0);
      id *= prime;
      pg_assert(id > 0);
    }
  }

  return id == filled_id;
}

static bool is_column_valid(const uint8_t *grid, uint8_t column) {
  pg_assert(grid != 0);
  pg_assert(column < 9);

  uint64_t id = 1;
  for (uint8_t k = column; k < 9 * 9; k += 9) {
    pg_assert(k < 9 * 9);

    const uint8_t value = grid[k];
    pg_assert(value <= 9);

    const uint64_t prime = primes[value];
    pg_assert(prime > 0);
    pg_assert(prime <= 23);

    pg_assert(id > 0);
    id *= prime;
    pg_assert(id > 0);
  }

  return id == filled_id;
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

static bool is_grid_valid(const uint8_t *grid) {
  for (uint8_t r = 0; r < 9; r++) {
    if (!is_row_valid(grid, r))
      return false;
  }
  for (uint8_t c = 0; c < 9; c++) {
    if (!is_column_valid(grid, c))
      return false;
  }

  for (uint8_t b = 0; b < 9; b++) {
    if (!is_block_valid(grid, b))
      return false;
  }

  return true;
}

int main() {
  const uint8_t grid[9 * 9] = {
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
  print_grid(grid);
  printf("is_grid_valid=%d\n", is_grid_valid(grid));

  uint8_t work_grid[9 * 9] = {0};
  __builtin_memcpy(work_grid, grid, 9 * 9);

  for (uint8_t position = 0; position < 9 * 9; position++) {
    const uint8_t value = grid[position];
    if (value != 0)
      continue;

    for (uint8_t v = 1; v <= 9; v++) {
      work_grid[position] = v;
      if (is_grid_valid(work_grid)) {
        break;
      } else {
        print_grid(work_grid);
        printf("is_grid_valid=%d\n", is_grid_valid(work_grid));

        pg_assert(0);
      }
    }
  }

  puts("============");
  print_grid(grid);
  printf("is_grid_valid=%d\n", is_grid_valid(grid));
}
