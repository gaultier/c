#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/fcntl.h>

#include "../pg/pg.h"
#include "bencode.h"
#include "peer.h"
#include "sha1.h"
#include "tracker.h"
#include "uv.h"

int main(int argc, char* argv[]) {
  assert(argc == 2);

  pg_logger_t logger = {.level = PG_LOG_DEBUG};

  pg_array_t(uint8_t) buf = {0};
  int64_t ret = 0;
  if ((ret = pg_read_file(pg_heap_allocator(), argv[1], &buf)) != 0) {
    pg_log_fatal(&logger, ret, "Failed to read file %s: %s", argv[1],
                 strerror(ret));
  }

  pg_span_t span = {.data = (char*)buf, .len = pg_array_count(buf)};
  pg_span_t info_span = {0};
  bc_value_t bencode = {0};
  {
    bc_parse_error_t err =
        bc_parse_value(pg_heap_allocator(), &span, &bencode, &info_span);
    if (err != BC_PE_NONE) {
      pg_log_fatal(&logger, EINVAL, "Failed to parse: %s",
                   bc_parse_error_to_string(err));
    }
  }
  bc_metainfo_t metainfo = {0};
  {
    bc_metainfo_error_t err = BC_MI_NONE;
    if ((err = bc_metainfo_init_from_value(pg_heap_allocator(), &bencode,
                                           &metainfo)) != BC_MI_NONE) {
      pg_log_fatal(&logger, EINVAL, "Failed to bc_metainfo_init_from_value: %s",
                   bc_metainfo_error_to_string(err));
    }
  }

  if (pg_span_starts_with(pg_span_make(metainfo.announce),
                          pg_span_make_c("http://"))) {
    pg_log_fatal(&logger, EINVAL,
                 "Tracker url is not http, not supported (yet): %s",
                 metainfo.announce);
  }

  tracker_query_t tracker_query = {
      .peer_id = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                  11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
      .port = 6881,
      .url = pg_span_make(metainfo.announce),
      .left = metainfo.length,
  };
  assert(mbedtls_sha1((uint8_t*)info_span.data, info_span.len,
                      tracker_query.info_hash) == 0);

  pg_array_t(tracker_peer_address_t) peer_addresses = {0};
  pg_array_init_reserve(peer_addresses, 15, pg_heap_allocator());

  pg_log_debug(&logger, "Fetching peers from tracker");
  tracker_error_t tracker_err =
      tracker_fetch_peers(pg_heap_allocator(), &tracker_query, &peer_addresses);
  if (tracker_err != TK_ERR_NONE) {
    pg_log_fatal(&logger, EINVAL, "Failed to contact tracker: %s",
                 tracker_error_to_string(tracker_err));
  }
  if (pg_array_count(peer_addresses) == 0) {
    pg_log_fatal(&logger, EINVAL, "No peers returned from tracker");
  }

  pg_log_debug(&logger, "Fetched %llu peers from tracker",
               pg_array_count(peer_addresses));

  int fd = open(metainfo.name, O_RDWR | O_CREAT, 0666);
  if (fd == -1) {
    pg_log_fatal(&logger, errno, "Failed to open file: path=%s err=%s",
                 metainfo.name, strerror(errno));
  }
  if (ftruncate(fd, metainfo.length) == -1) {
    pg_log_fatal(&logger, errno, "Failed to truncate(2) file: path=%s err=%s",
                 metainfo.name, strerror(errno));
  }

  download_t download = {0};
  download_init(pg_heap_allocator(), &download, &metainfo,
                tracker_query.info_hash, tracker_query.peer_id, fd);
  peer_error_t err =
      download_checksum_all(pg_heap_allocator(), &logger, &download, &metainfo);
  if (err.kind != PEK_NONE) {
    // Gracefully recover
    pg_log_error(&logger, "Failed to checksum file: path=%s err=%s",
                 metainfo.name, strerror(errno));
    pg_bitarray_set_all(&download.pieces_to_download);
  }

  for (uint64_t i = 0; i < pg_array_count(peer_addresses); i++) {
    const tracker_peer_address_t addr = peer_addresses[i];
    peer_t* peer =
        peer_make(pg_heap_allocator(), &logger, &download, &metainfo, addr);
    peer_connect(peer, addr);
  }
  uv_run(uv_default_loop(), 0);
}
