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

  const uint8_t position = block / 3 * 3 * 9 + (block % 3) * 3;
  pg_assert(position < 9 * 9);
  const uint8_t start_i = position/9;
  pg_assert(start_i < 9 * 9);
  const uint8_t start_j = position % 9;
  pg_assert(start_j < 9 * 9);

  uint64_t id = 1;
  for (uint8_t i = start_i; i < start_i + 3; i++) {
    for (uint8_t j = start_j; j < start_j + 3; j++) {
      const uint8_t block_position = i * 9 + j;
      pg_assert(block_position < 9 * 9);
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

// [start]
// [ 0  ]: 1 1 1 1
// [ 1  ]: 1 1 1 2
// [ 2  ]: 1 1 1 3
// [ 3  ]: 1 1 1 4
// [...]
// [ 9  ]: 1 1 1 9
// [10  ]: 1 1 2 1
// [11  ]: 1 1 2 2
// [...]
// [ N  ]: 1 1 2 9
// [ N+1]: 1 1 3 1
// [...]
// [ M  ]: 2 1 1 1
// [ M+1]: 2 1 1 2
// [...]
// [ P  ]: 9 9 9 9
// [end]
static void increment_solution_vector(uint8_t *solution,
                                      uint8_t solution_size) {
  pg_assert(solution != 0);

  if (solution_size == 0)
    return;

  uint8_t *const value = &solution[solution_size - 1];
  if (*value == 0) {
    increment_solution_vector(solution, solution_size - 1);
    return;
  }

  pg_assert(*value > 0);
  pg_assert(*value <= 9);

  // Easy case, single increment.
  if (*value != 9) {
    *value += 1;
    return;
  }

  // Harder cases, need to wrap around.
  *value = 1;
  increment_solution_vector(solution, solution_size - 1);
}

static void merge_grids(uint8_t *dst, uint8_t *src) {
  pg_assert(dst != 0);
  pg_assert(src != 0);

  for (uint8_t i = 0; i < 9 * 9; i++) {
    if (src[i] != 0) {
      dst[i] = src[i];
    }
  }
}

int main() {
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
  pg_assert(is_row_valid(final, 0));
  pg_assert(is_row_valid(final, 1));
  pg_assert(is_row_valid(final, 2));
  pg_assert(is_row_valid(final, 3));
  pg_assert(is_row_valid(final, 4));
  pg_assert(is_row_valid(final, 5));
  pg_assert(is_row_valid(final, 6));
  pg_assert(is_row_valid(final, 7));
  pg_assert(is_row_valid(final, 8));

  pg_assert(is_column_valid(final, 0));
  pg_assert(is_column_valid(final, 1));
  pg_assert(is_column_valid(final, 2));
  pg_assert(is_column_valid(final, 3));
  pg_assert(is_column_valid(final, 4));
  pg_assert(is_column_valid(final, 5));
  pg_assert(is_column_valid(final, 6));
  pg_assert(is_column_valid(final, 7));
  pg_assert(is_column_valid(final, 8));

  pg_assert(is_block_valid(final, 0));
  pg_assert(is_block_valid(final, 1));
  pg_assert(is_block_valid(final, 2));
  pg_assert(is_block_valid(final, 3));
  pg_assert(is_block_valid(final, 4));
  pg_assert(is_block_valid(final, 5));
  pg_assert(is_block_valid(final, 6));
  pg_assert(is_block_valid(final, 7));
  pg_assert(is_block_valid(final, 8));

  pg_assert(is_grid_valid(final));

  // Init the solution vector. E.g.:
  // 0 0 1 1 0 1 1 1 1
  // 0 1 1 0 0 0 1 1 1
  // 1 0 0 1 1 1 1 0 1
  // 0 1 1 1 0 1 1 1 0
  // 0 1 1 0 1 0 1 1 0
  // 0 1 1 1 0 1 1 1 0
  // 1 0 1 1 1 1 0 0 1
  // 1 1 1 0 0 0 1 1 0
  // 1 1 1 1 0 1 1 0 0
  uint8_t solution[9 * 9] = {0};
  for (uint8_t i = 0; i < 9 * 9; i++) {
    if (grid[i] == 0)
      solution[i] = 1;
  }

  for (;;) {
    /* print_grid(solution); */
    increment_solution_vector(solution, PG_ARRAY_SIZE(solution));
    /* puts("--------------"); */

    merge_grids(grid, solution);

    const bool valid = is_grid_valid(grid);

    if (valid) {
      printf("is_grid_valid=%d\n", valid);
      print_grid(grid);
      return 0;
    }

    /* puts("=============="); */
    /* puts(""); */
  }
}
