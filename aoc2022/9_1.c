#include <assert.h>
#include <complex.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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

int main(void) {
  coord_t head = {0}, tail = {0};
  for (uint64_t i = 0; i < sizeof(moves) / sizeof(moves[0]); i++) {
    move_t move = moves[i];
    const coord_t delta = direction_to_vector[move.direction];

    for (uint64_t j = 0; j < move.distance; j++) {
      if (is_diagonal(head, tail)) {
        tail = head;
      } else if (chebychev_dist(head, tail) >= 1) {
        tail.x += delta.x;
        tail.y += delta.y;
      }

      head.x += delta.x;
      head.y += delta.y;
    }

    printf("head=%lld %lld tail=%lld %lld\n", head.x, head.y, tail.x, tail.y);
  }
}
