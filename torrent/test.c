#include <_types/_uint8_t.h>

#include "bencode.h"
#include "peer.h"
#include "tracker.h"
#include "uv.h"
#include "vendor/greatest/greatest.h"

static uint8_t peer_id[20] = {0};
static uint8_t info_hash[20] = {0};

TEST test_read_bufs() {
  pg_logger_t logger = {.level = PG_LOG_FATAL};

  pg_pool_t peer_pool = {0};
  pg_pool_init(&peer_pool, sizeof(peer_t), 1);

  pg_array_t(uint8_t) pieces = {0};
  pg_array_init_reserve(pieces, 2 * 20, pg_heap_allocator());
  pg_array_resize(pieces, 2 * 20);
  bc_metainfo_t metainfo = {.announce = "http://localhost",
                            .length = 3 * PEER_BLOCK_LENGTH + 1,
                            .piece_length = 2 * PEER_BLOCK_LENGTH,
                            .pieces = pieces,
                            .name = "foo"};

  download_t download = {0};
  download_init(pg_heap_allocator(), &download, &metainfo, info_hash, peer_id,
                0);

  const tracker_peer_address_t addr = {0};

  peer_t* peer = pg_pool_alloc(&peer_pool);
  assert(peer != NULL);
  peer_init(peer, &logger, &peer_pool, &download, &metainfo, addr);

  const uv_buf_t buf1 = uv_buf_init("Hello", 5);
  const uv_buf_t buf2 = uv_buf_init(" world", 6);

  uv_stream_t stream = {.data = peer};
  // peer_on_read(&stream, 5, &buf1);

  PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  RUN_TEST(test_read_bufs);

  GREATEST_MAIN_END(); /* display results */
}
