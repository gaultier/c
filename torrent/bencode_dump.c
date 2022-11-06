#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../pg/pg.h"
#include "bencode.h"
#include "sha1.h"
#include "tracker.h"

int main(int argc, char* argv[]) {
  pg_array_t(uint8_t) buf = {0};
  int64_t ret = 0;
  if (argc == 2) {
    if ((ret = pg_read_file(pg_heap_allocator(), argv[1], &buf)) != 0) {
      fprintf(stderr, "Failed to read file: %s\n", strerror(ret));
      exit(ret);
    }
  } else if (argc == 1) {
    if ((ret = pg_read_file_fd(pg_heap_allocator(), 0, &buf)) != 0) {
      fprintf(stderr, "Failed to read from stdin: %s\n", strerror(ret));
      exit(ret);
    }
  }

  pg_span_t span = {.data = (char*)buf, .len = pg_array_count(buf)};
  pg_span_t info_span = {0};
  bc_value_t bencode = {0};
  {
    bc_parse_error_t err =
        bc_parse_value(pg_heap_allocator(), &span, &bencode, &info_span);
    if (err != BC_PE_NONE) {
      fprintf(stderr, "Failed to parse: %s\n", bc_parse_error_to_string(err));
      exit(EINVAL);
    }
  }
  printf("info_span: start=%ld len=%llu\n", (uint8_t*)info_span.data - buf,
         info_span.len);
  bc_value_dump(&bencode, stdout, 0);
  puts("");

  bc_metainfo_t metainfo = {0};
  {
    bc_metainfo_error_t err = BC_MI_NONE;
    if ((err = bc_metainfo_init_from_value(pg_heap_allocator(), &bencode,
                                           &metainfo)) != BC_MI_NONE) {
      fprintf(stderr, "Failed to bc_metainfo_init_from_value: %s\n",
              bc_metainfo_error_to_string(err));
      exit(EINVAL);
    }
  }
  __builtin_dump_struct(&metainfo, &printf);
  puts("");

  uint8_t sha1[20] = {0};
  assert(mbedtls_sha1((uint8_t*)info_span.data, info_span.len, sha1) == 0);
  printf("info_hash: ");
  for (uint64_t i = 0; i < sizeof(sha1); i++) {
    printf("%02x ", sha1[i]);
  }
  puts("");

  tracker_query_t tracker_query = {
      .peer_id = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                  11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
      .port = 6881,
      .url = pg_span_make(metainfo.announce),
      .left = metainfo.length,
  };
  memcpy(&tracker_query.info_hash, sha1, sizeof(sha1));
  pg_string_t tracker_url =
      tracker_build_url_from_query(pg_heap_allocator(), &tracker_query);
  puts(tracker_url);
}
