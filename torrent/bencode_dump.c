#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "../pg/pg.h"
#include "bencode.h"

int main(int argc, char* argv[]) {
  assert(argc == 2);
  pg_array_t(uint8_t) buf = {0};
  int64_t ret = 0;
  if ((ret = pg_read_file(pg_heap_allocator(), argv[1], &buf)) != 0) {
    fprintf(stderr, "Failed to read file: %s\n", strerror(ret));
    exit(ret);
  }

  pg_span_t span = {.data = (char*)buf, .len = pg_array_count(buf)};
  bc_value_t bencode = {0};
  {
    bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &bencode);
    if (err != BC_PE_NONE) {
      fprintf(stderr, "Failed to parse: %s\n", bc_parse_error_to_string(err));
      exit(EINVAL);
    }
  }
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
}
