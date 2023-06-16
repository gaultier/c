#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  uint64_t len;
  uint8_t data[];
} buf_t;

typedef struct {
  uint64_t v;
  void *ptr;
  buf_t bytes;
} entity_t;

static bool distinct_small(const entity_t *x, uint64_t x_len) {
  for (uint64_t i = 0; i < x_len; i++) {
    for (uint64_t j = 0; j < i; j++) {
      const entity_t *const a = &x[i];
      const entity_t *const b = &x[j];

      if (a->bytes.len != b->bytes.len)
        continue;

      if (memcmp(a->bytes.data, b->bytes.data, a->bytes.len) == 0)
        return false;
    }
  }
  return true;
}

static int cmp_lexicographic_buffers(const uint8_t *a, const uint8_t *b,
                                     uint64_t n) {
  for (uint64_t i = 0; i < n; i++) {
    const uint8_t a_item = a[i];
    const uint8_t b_item = b[i];
    if (a_item != b_item)
      return a_item - b_item;
  }
  return 0;
}

static int cmp(const void *va, const void *vb) {
  const entity_t *const a = va;
  const entity_t *const b = vb;

  if (a->bytes.len != b->bytes.len)
    return a->bytes.len - b->bytes.len;

  return cmp_lexicographic_buffers(a->bytes.data, b->bytes.data, a->bytes.len);
}

static bool distinct_big(entity_t *x, uint64_t x_len, uint64_t bytes_len) {
  const uint64_t item_size = sizeof(entity_t) + bytes_len;

  qsort(x, x_len, item_size, cmp);

  for (uint64_t i = 0; i < x_len - 1; i++) {
    const entity_t *const a =
        (const entity_t *const)((uint8_t *)x + item_size * (i + 0));
    const entity_t *const b =
        (const entity_t *const)((uint8_t *)x + item_size * (i + 1));

    assert((uint8_t *)b - (uint8_t *)a == item_size);

    if (a->bytes.len != b->bytes.len)
      continue;

    if (memcmp(a->bytes.data, b->bytes.data, a->bytes.len) == 0)
      return false;
  }
  return true;
}

static void distinct_small_bench_n(uint64_t n) {
  const uint64_t bytes_len = 16;
  entity_t *const x = malloc((sizeof(entity_t) + bytes_len) * n);

  __int128_t v = 0;
  for (uint64_t i = 0; i < n; i++) {
    x[i].bytes.len = bytes_len;

    memcpy(x[i].bytes.data, &v, sizeof(v));
    v++;
  }

  // Run twice to warm-up cache.
  assert(distinct_small(x, n) == true);
  assert(distinct_small(x, n) == true);

  struct timespec start = {0};
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);

  const uint64_t rep_count = 16;
  for (uint64_t rep = 0; rep < rep_count; rep++) {
    assert(distinct_small(x, n) == true);
  }

  struct timespec end = {0};
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);

  const uint64_t duration_ns =
      (end.tv_sec - start.tv_sec) * 1000 * 1000 * 1000 +
      (end.tv_nsec - start.tv_nsec);
  const double rep_duration_ns = (double)duration_ns / (double)rep_count;
  printf("impl=small n=%lu duration_ns=%lu rep_duration_ns=%f\n", n,
         duration_ns, rep_duration_ns);
  free(x);
}

static void distinct_big_bench_n(uint64_t n) {
  const uint64_t bytes_len = 16;
  const uint64_t item_size = sizeof(entity_t) + bytes_len;
  entity_t *const x = malloc(item_size * n);

  __int128_t v = 0;
  assert(sizeof(v) == bytes_len);
  for (uint64_t i = 0; i < n; i++) {
    entity_t *const p = (entity_t *)((uint8_t *)x + item_size * i);
    p->bytes.len = bytes_len;

    memcpy(p->bytes.data, &v, bytes_len);
    v++;
  }

  // Run twice to warm-up cache.
  assert(distinct_big(x, n, bytes_len) == true);
  assert(distinct_big(x, n, bytes_len) == true);

  struct timespec start = {0};
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);

  const uint64_t rep_count = 16;
  for (uint64_t rep = 0; rep < rep_count; rep++) {
    assert(distinct_big(x, n, bytes_len) == true);
  }

  struct timespec end = {0};
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);

  const uint64_t duration_ns =
      (end.tv_sec - start.tv_sec) * 1000 * 1000 * 1000 +
      (end.tv_nsec - start.tv_nsec);
  const double rep_duration_ns = (double)duration_ns / (double)rep_count;
  printf("impl=big n=%lu duration_ns=%lu rep_duration_ns=%f\n", n, duration_ns,
         rep_duration_ns);
  free(x);
}

int main() {

  {
    const uint64_t bytes_len = 4;
    const uint64_t item_size = sizeof(entity_t) + bytes_len;
    entity_t *const test = malloc(item_size * 2);
    entity_t *v = (entity_t *)((uint8_t *)test + item_size * 0);
    v->bytes.len = 4;
    v->bytes.data[0] = 1;
    v->bytes.data[1] = 2;
    v->bytes.data[2] = 3;
    v->bytes.data[3] = 3;

    v = (entity_t *)((uint8_t *)test + item_size * 1);
    v->bytes.len = 4;
    v->bytes.data[0] = 1;
    v->bytes.data[1] = 2;
    v->bytes.data[2] = 3;
    v->bytes.data[3] = 4;

    assert(distinct_big(test, 2, 4) == true);

    free(test);
  }

  {
    const uint64_t bytes_len = 4;
    const uint64_t item_size = sizeof(entity_t) + bytes_len;
    entity_t *const test = malloc(item_size * 2);
    entity_t *v = (entity_t *)((uint8_t *)test + item_size * 0);
    v->bytes.len = 4;
    v->bytes.data[0] = 1;
    v->bytes.data[1] = 2;
    v->bytes.data[2] = 3;
    v->bytes.data[3] = 4;

    v = (entity_t *)((uint8_t *)test + item_size * 1);
    v->bytes.len = 4;
    v->bytes.data[0] = 1;
    v->bytes.data[1] = 2;
    v->bytes.data[2] = 3;
    v->bytes.data[3] = 4;

    assert(distinct_big(test, 2, 4) == false);
  }

  distinct_small_bench_n(5);
  distinct_big_bench_n(5);
  puts("");
  distinct_small_bench_n(10);
  distinct_big_bench_n(10);
  puts("");
  distinct_small_bench_n(15);
  distinct_big_bench_n(15);
  puts("");
  distinct_small_bench_n(16);
  distinct_big_bench_n(16);
  puts("");
  distinct_small_bench_n(17);
  distinct_big_bench_n(17);
  puts("");
  distinct_small_bench_n(18);
  distinct_big_bench_n(18);
  puts("");
  distinct_small_bench_n(19);
  distinct_big_bench_n(19);
  puts("");
  distinct_small_bench_n(20);
  distinct_big_bench_n(20);
  puts("");
  distinct_small_bench_n(100);
  distinct_big_bench_n(100);
  puts("");
  distinct_small_bench_n(200);
  distinct_big_bench_n(200);
  puts("");
  distinct_small_bench_n(300);
  distinct_big_bench_n(300);
  puts("");
  distinct_small_bench_n(400);
  distinct_big_bench_n(400);
  puts("");
  distinct_small_bench_n(500);
  distinct_big_bench_n(500);
  puts("");
  distinct_small_bench_n(600);
  distinct_big_bench_n(600);
  puts("");
  distinct_small_bench_n(700);
  distinct_big_bench_n(700);
  puts("");
  distinct_small_bench_n(800);
  distinct_big_bench_n(800);
  puts("");
  distinct_small_bench_n(900);
  distinct_big_bench_n(900);
  puts("");
  distinct_small_bench_n(1000);
  distinct_big_bench_n(1000);
  puts("");
  distinct_small_bench_n(10000);
  distinct_big_bench_n(10000);
  puts("");
}
