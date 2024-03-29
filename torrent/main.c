#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_types/_off_t.h>

#include "../pg/pg.h"
#include "bencode.h"
#include "peer.h"
#include "sha1.h"
#include "tracker.h"
#include "uv.h"

int main(int argc, char *argv[]) {
  assert(argc == 2);

  // pg_logger_t logger = {.level = PG_LOG_DEBUG};
  pg_logger_t logger = {.level = PG_LOG_INFO};

  pg_array_t(uint8_t) torrent_file_data = {0};
  pg_array_init_reserve(torrent_file_data, 0, pg_heap_allocator());
  if (!pg_read_file(argv[1], &torrent_file_data)) {
    pg_log_fatal(&logger, errno, "Failed to read file %s: %s", argv[1],
                 strerror(errno));
  }

  if (pg_array_len(torrent_file_data) > UINT32_MAX) {
    fprintf(stderr, "Too much data, must be under %u bytes, was %llu\n",
            UINT32_MAX, pg_array_len(torrent_file_data));
    exit(EINVAL);
  }
  pg_span_t torrent_file_span = {.data = (char *)torrent_file_data,
                                 .len = pg_array_len(torrent_file_data)};
  bc_parser_t parser = {0};
  bc_parser_init(pg_heap_allocator(), &parser, 100);
  bc_parse_error_t bc_err = bc_parse(&parser, &torrent_file_span);
  if (bc_err != BC_PE_NONE) {
    pg_log_fatal(&logger, EINVAL, "Failed to parse: %s",
                 bc_parse_error_to_string((int)bc_err));
  }

  bc_metainfo_t metainfo = {0};
  pg_span_t info_span = {0};
  bc_metainfo_error_t err_metainfo =
      bc_parser_init_metainfo(&parser, &metainfo, &info_span);
  if (err_metainfo != BC_ME_NONE) {
    pg_log_fatal(&logger, EINVAL, "Failed to bc_metainfo_init_from_value: %s",
                 bc_metainfo_error_to_string((int)err_metainfo));
  }

  if (!pg_span_starts_with(metainfo.announce, pg_span_make_c("http://"))) {
    pg_log_fatal(&logger, EINVAL,
                 "Tracker url is not http, not supported (yet): %.*s",
                 (int)metainfo.announce.len, metainfo.announce.data);
  }

  tracker_query_t tracker_query = {
      .port = 6881,
      .url = metainfo.announce,
      .left = metainfo.length,
  };
  assert(mbedtls_sha1((uint8_t *)info_span.data, info_span.len,
                      tracker_query.info_hash) == 0);

  pg_array_t(tracker_peer_address_ipv4_t) peer_addresses_ipv4 = {0};
  pg_array_init_reserve(peer_addresses_ipv4,
                        /* Default is 50 peers returned from tracker */ 50,
                        pg_heap_allocator());
  pg_array_t(tracker_peer_address_ipv6_t) peer_addresses_ipv6 = {0};
  pg_array_init_reserve(peer_addresses_ipv6,
                        /* Default is 50 peers returned from tracker */ 50,
                        pg_heap_allocator());

  pg_log_debug(&logger, "Fetching peers from tracker");
  tracker_error_t tracker_err =
      tracker_fetch_peers(&logger, pg_heap_allocator(), &tracker_query,
                          &peer_addresses_ipv4, &peer_addresses_ipv6);
  if (tracker_err != TK_ERR_NONE) {
    pg_log_fatal(&logger, EINVAL, "Failed to contact tracker: %s",
                 tracker_error_to_string((int)tracker_err));
  }
  if (pg_array_len(peer_addresses_ipv4) == 0 &&
      pg_array_len(peer_addresses_ipv6) == 0) {
    pg_log_fatal(&logger, EINVAL, "No peers returned from tracker");
  }

  pg_log_debug(&logger, "Fetched %llu peers from tracker",
               pg_array_len(peer_addresses_ipv4));

  pg_string_t name = pg_string_make_length(
      pg_heap_allocator(), metainfo.name.data, metainfo.name.len);
  int fd = open(name, O_RDWR | O_CREAT, 0666);
  pg_string_free(name);
  if (fd == -1) {
    pg_log_fatal(&logger, errno, "Failed to open file: path=%.*s err=%s",
                 (int)metainfo.name.len, metainfo.name.data, strerror(errno));
  }
  if (ftruncate(fd, (off_t)metainfo.length) == -1) {
    pg_log_fatal(&logger, errno, "Failed to truncate(2) file: path=%.*s err=%s",
                 (int)metainfo.name.len, metainfo.name.data, strerror(errno));
  }

  download_t download = {0};
  download_init(&download, tracker_query.info_hash, fd);

  picker_t picker = {0};
  picker_init(pg_heap_allocator(), &logger, &picker, &metainfo);

  peer_error_t peer_err = picker_checksum_all(pg_heap_allocator(), &logger,
                                              &picker, &metainfo, &download);
  if (peer_err.kind != PEK_NONE)
    pg_log_error(&logger, "Failed to checksum file: path=%.*s err=%s",
                 (int)metainfo.name.len, metainfo.name.data, strerror(errno));

  pg_pool_t peer_pool = {0};
  pg_pool_init(&peer_pool, sizeof(peer_t), pg_array_len(peer_addresses_ipv4));

  for (uint64_t i = 0; i < pg_array_len(peer_addresses_ipv4); i++) {
    const tracker_peer_address_ipv4_t addr = peer_addresses_ipv4[i];
    peer_t *peer = pg_pool_alloc(&peer_pool);
    assert(peer != NULL);
    peer_init(peer, &logger, &peer_pool, &download, &metainfo, &picker, addr);
    peer_connect(peer, addr);
  }
  pg_array_free(peer_addresses_ipv4);
  pg_array_free(peer_addresses_ipv6);

  uv_run(uv_default_loop(), 0);
}
