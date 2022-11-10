#include <unistd.h>

#include "bencode.h"
#include "peer.h"
#include "tracker.h"
#include "uv.h"
#include "vendor/greatest/greatest.h"

static uint8_t peer_id[20] = {0};
static uint8_t info_hash[20] = {0};
const uint8_t handshake_header[] = {
    PEER_HANDSHAKE_HEADER_LENGTH,
    'B',
    'i',
    't',
    'T',
    'o',
    'r',
    'r',
    'e',
    'n',
    't',
    ' ',
    'p',
    'r',
    'o',
    't',
    'o',
    'c',
    'o',
    'l',
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

static pg_logger_t logger = {.level = PG_LOG_DEBUG};

TEST test_on_read() {
  pg_pool_t peer_pool = {0};
  pg_pool_init(&peer_pool, sizeof(peer_t), 1);

  bc_metainfo_t metainfo = {
      .announce = pg_span32_make_c("http://localhost"),
      .length = 3 * PEER_BLOCK_LENGTH + 1,
      .piece_length = 2 * PEER_BLOCK_LENGTH,
      .pieces = pg_span32_make_c("0000000000000000000000000000000000000000"),
      .name = pg_span32_make_c("foo")};

  download_t download = {0};
  download_init(pg_heap_allocator(), &download, &metainfo, info_hash, peer_id,
                0);

  const tracker_peer_address_t addr = {0};

  peer_t* peer = pg_pool_alloc(&peer_pool);
  assert(peer != NULL);
  peer_init(peer, &logger, &peer_pool, &download, &metainfo, addr);

  {
    uv_buf_t buf1 = {0};
    peer_alloc((uv_handle_t*)&peer->connection, 65536, &buf1);
    ASSERT(buf1.base != NULL);

    memcpy(buf1.base, handshake_header, sizeof(handshake_header));
    ASSERT_EQ_FMT((uint8_t)PEER_HANDSHAKE_HEADER_LENGTH, buf1.base[0], "%d");

    memcpy(buf1.base + sizeof(handshake_header), peer->download->info_hash,
           sizeof(peer->download->info_hash));
    memcpy(buf1.base + sizeof(handshake_header) +
               sizeof(peer->download->info_hash),
           peer->download->peer_id, sizeof(peer->download->peer_id));
    buf1.len = PEER_HANDSHAKE_LENGTH;

    uv_stream_t stream = {.data = peer};
    peer_on_read(&stream, PEER_HANDSHAKE_LENGTH, &buf1);

    ASSERT_EQ(true, peer->handshaked);
  }

  {
    uv_buf_t buf1 = {0};
    peer_alloc((uv_handle_t*)&peer->connection, 5, &buf1);
    ASSERT(buf1.base != NULL);
    buf1.base[0] = 0;
    buf1.base[1] = 0;
    buf1.base[2] = 0;
    buf1.base[3] = 5;
    buf1.base[4] = PT_CHOKE;

    uv_stream_t stream = {.data = peer};
    peer_on_read(&stream, 5, &buf1);
  }
  {
    uv_buf_t buf1 = {0};
    peer_alloc((uv_handle_t*)&peer->connection, 3, &buf1);
    ASSERT(buf1.base != NULL);
    buf1.base[0] = 0;
    buf1.base[1] = 0;
    buf1.base[2] = 0;
    buf1.len = 3;

    uv_stream_t stream = {.data = peer};
    peer_on_read(&stream, 3, &buf1);

    uv_buf_t buf2 = {0};
    peer_alloc((uv_handle_t*)&peer->connection, 1, &buf2);
    ASSERT(buf2.base != NULL);
    buf2.base[0] = 5;
    buf2.len = 1;
    peer_on_read(&stream, 1, &buf2);

    uv_buf_t buf3 = {0};
    peer_alloc((uv_handle_t*)&peer->connection, 1, &buf3);
    ASSERT(buf3.base != NULL);
    buf3.base[0] = PT_INTERESTED;
    buf3.len = 1;
    peer_on_read(&stream, 1, &buf3);

    ASSERT_EQ(true, peer->them_interested);
  }

  PASS();
}

TEST test_picker() {
  const uint32_t pieces_count = 2;
  bc_metainfo_t metainfo = {
      .announce = pg_span32_make_c("http://localhost"),
      .length = 3 * PEER_BLOCK_LENGTH + 1,
      .piece_length = 2 * PEER_BLOCK_LENGTH,
      .pieces = pg_span32_make_c("0000000000000000000000000000000000000000"),
      .name = pg_span32_make_c("foo")};

  picker_t picker = {0};
  picker_init(pg_heap_allocator(), &logger, &picker, &metainfo);

  pg_bitarray_t them_have_pieces = {0};
  pg_bitarray_init(pg_heap_allocator(), &them_have_pieces, pieces_count);
  // `them_have_pieces` is only 0s
  {
    bool found = false;
    ASSERT_EQ_FMT(-1U, picker_pick_block(&picker, &them_have_pieces, &found),
                  "%u");
    ASSERT_EQ(false, found);
  }
  {
    bool found = false;
    pg_bitarray_set(&them_have_pieces, 1);
    ASSERT_EQ_FMT(2U, picker_pick_block(&picker, &them_have_pieces, &found),
                  "%u");
    ASSERT_EQ(true, found);
  }
  // `them_have_pieces` is only 1s and all blocks are already downloaded
  {
    bool found = false;
    pg_bitarray_unset_all(&picker.blocks_to_download);
    ASSERT_EQ_FMT(-1U, picker_pick_block(&picker, &them_have_pieces, &found),
                  "%u");
    ASSERT_EQ(false, found);
  }
  picker_destroy(&picker);

  PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv) {
  GREATEST_MAIN_BEGIN(); /* command-line options, initialization. */

  RUN_TEST(test_on_read);
  RUN_TEST(test_picker);

  GREATEST_MAIN_END(); /* display results */
}
