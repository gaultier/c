#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../pg/pg.h"
#include "bencode.h"
#include "sha1.h"

int main(int argc, char *argv[]) {
  pg_array_t(uint8_t) buf = {0};
  pg_array_init_reserve(buf, 0, pg_heap_allocator());
  if (argc == 2) {
    if (!pg_read_file(argv[1], &buf)) {
      fprintf(stderr, "Failed to read file: %s\n", strerror(errno));
      exit(errno);
    }
  } else if (argc == 1) {
    if (!pg_array_read_file_fd(STDIN_FILENO, &buf)) {
      fprintf(stderr, "Failed to read from stdin: %s\n", strerror(errno));
      exit(errno);
    }
  }
  if (pg_array_len(buf) > UINT32_MAX) {
    fprintf(stderr, "Too much data, must be under %u bytes, was %llu\n",
            UINT32_MAX, pg_array_len(buf));
    exit(EINVAL);
  }

  pg_span_t input = {.data = (char *)buf, .len = pg_array_len(buf)};

  bc_parser_t parser = {0};
  bc_parser_init(pg_heap_allocator(), &parser, 100);
  bc_parse_error_t err = bc_parse(&parser, &input);
  if (err != BC_PE_NONE) {
    fprintf(stderr, "Failed to parse: %s\n",
            bc_parse_error_to_string((int)err));
    exit(EINVAL);
  }
  bc_dump_values(&parser, stdout, 0);
  puts("");

  bc_metainfo_t metainfo = {0};
  pg_span_t info_span = {0};
  bc_metainfo_error_t err_metainfo =
      bc_parser_init_metainfo(&parser, &metainfo, &info_span);
  if (err_metainfo != BC_ME_NONE) {
    fprintf(stderr, "Failed to bc_parser_init_metainfo: %s\n",
            bc_metainfo_error_to_string((int)err_metainfo));
    exit(EINVAL);
  }
  printf("Metainfo:\n"
         "  - name: %.*s\n"
         "  - announce: %.*s\n"
         "  - length: %llu\n"
         "  - piece length: %u\n",
         (int)metainfo.name.len, metainfo.name.data, (int)metainfo.announce.len,
         metainfo.announce.data, metainfo.length, metainfo.piece_length);
  puts("");

  bc_parser_destroy(&parser);

  uint8_t sha1[20] = {0};
  assert(mbedtls_sha1((uint8_t *)info_span.data, info_span.len, sha1) == 0);
  printf("info_hash: ");
  for (uint64_t i = 0; i < sizeof(sha1); i++) {
    printf("%02x ", sha1[i]);
  }
  puts("");
}
