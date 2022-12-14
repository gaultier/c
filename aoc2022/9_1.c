#include <_types/_uint64_t.h>
#include <assert.h>
#include <complex.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uint8_t direction, distance;
} move_t;

typedef struct {
  int64_t x, y;
} coord_t;

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static move_t moves[] = {
    {'R', 4}, {'U', 4}, {'L', 3}, {'D', 1},
    {'R', 4}, {'D', 1}, {'L', 5}, {'R', 2},
};

static int64_t chebychev_dist(coord_t head, coord_t tail) {
  return MAX(imaxabs(head.x - tail.x), imaxabs(head.y - tail.y));
}

static bool is_diagonal(coord_t head, coord_t tail) {
  const coord_t distance = {.x = head.x - tail.x, .y = head.y - tail.y};
  return (distance.x * distance.x == 1) && (distance.y * distance.y == 1);
}

static const coord_t direction_to_vector[] = {
    ['L'] = {.x = -1},
    ['R'] = {.x = 1},
    ['U'] = {.y = 1},
    ['D'] = {.y = -1},
};

#define WIDTH 6

static void draw(bool *visited_cells, coord_t head, coord_t tail) {
  for (int64_t y = 0; y < WIDTH; y++) {
    for (int64_t x = 0; x < WIDTH; x++) {
      const bool visited = visited_cells[y * WIDTH + x];
      if (head.x == x && head.y == y)
        printf("H");
      else if (tail.x == x && tail.y == y)
        printf("T");
      else
        printf("%c", visited ? '#' : '.');
    }
    puts("");
  }
  puts("");
}

int main(void) {
  coord_t head = {0}, tail = {0};
  bool *visited_cells = calloc(WIDTH * WIDTH, 1);
  uint64_t visited_count = 0;

  draw(visited_cells, head, tail);

  for (uint64_t i = 0; i < sizeof(moves) / sizeof(moves[0]); i++) {
    move_t move = moves[i];
    const coord_t delta = direction_to_vector[move.direction];

    for (uint64_t j = 0; j < move.distance; j++) {
      const coord_t prev_head = head;
      head.x += delta.x;
      head.y += delta.y;
      assert(head.y < WIDTH);

      if (chebychev_dist(head, tail) > 1) { // Need to move tail
        if (is_diagonal(prev_head, tail)) {
          tail = prev_head;
        } else {
          tail.x += delta.x;
          tail.y += delta.y;
        }
      }
    

    visited_count += !visited_cells[tail.y * WIDTH + tail.x];
    visited_cells[tail.y * WIDTH + tail.x] = true;

    printf("move=%c %hhu count=%llu head=%lld %lld tail=%lld %lld\n",
           move.direction, move.distance, visited_count, head.x, head.y, tail.x,
           tail.y);

    draw(visited_cells, head, tail);
  }
}
printf("%llu\n", visited_count);
}
