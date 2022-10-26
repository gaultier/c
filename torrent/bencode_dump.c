#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/errno.h>
#include <unistd.h>

#include "../pg/pg.h"
#include "bencode.h"

int main(int argc, char* argv[]) {
  assert(argc == 2);
  int fd = open(argv[1], O_RDONLY);
  if (fd == -1) {
    fprintf(stderr, "Failed to open(2) %s: %s\n", argv[1], strerror(errno));
    exit(errno);
  }

  pg_array_t(char) buf = {0};
  pg_array_init_reserve(buf, 4096, pg_heap_allocator());
  for (;;) {
    int64_t ret =
        read(fd, buf + pg_array_count(buf), pg_array_available_space(buf));
    if (ret == -1) {
      fprintf(stderr, "Failed to read(2): %s\n", strerror(errno));
      exit(errno);
    }
    if (ret == 0) break;
    pg_array_resize(buf, pg_array_count(buf) + ret);
    pg_array_grow(buf, 1.5 * pg_array_capacity(buf));
  }

  pg_string_span_t span = {.data = buf, .len = pg_array_count(buf)};
  bc_value_t bencode = {0};
  bc_parse_error_t err = bc_parse_value(pg_heap_allocator(), &span, &bencode);
  if (err != BC_PE_NONE) {
    fprintf(stderr, "Failed to parse: %s\n", bc_parse_error_to_string(err));
    exit(EINVAL);
  }
  bc_value_dump(&bencode, stdout, 0);
}
