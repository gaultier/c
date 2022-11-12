#pragma once

#include <arpa/inet.h>
#include <inttypes.h>
#include <math.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <uv.h>

#include "bencode.h"
#include "sha1.h"
#include "tracker.h"

#define PEER_HANDSHAKE_HEADER_LENGTH ((uint64_t)19)
#define PEER_HANDSHAKE_LENGTH                                                  \
  ((uint64_t)(1 + PEER_HANDSHAKE_HEADER_LENGTH + 8 + 20 + 20))
#define PEER_MAX_MESSAGE_LENGTH ((uint64_t)1 << 27)
#define PEER_MAX_IN_FLIGHT_REQUESTS ((uint64_t)5)

typedef struct {
  int fd;
  uint8_t info_hash[20];
  uint8_t peer_id[20];
  uint32_t downloaded_pieces_count, downloaded_blocks_count;
  uint64_t downloaded_bytes;
  uint64_t start_ts;
} download_t;

typedef enum : uint8_t {
  PEK_NONE,
  PEK_NEED_MORE,
  PEK_UV,
  PEK_WRONG_HANDSHAKE_HEADER,
  PEK_WRONG_HANDSHAKE_HASH,
  PEK_INVALID_ANNOUNCED_LENGTH,
  PEK_INVALID_MESSAGE_TAG,
  PEK_INVALID_BITFIELD,
  PEK_INVALID_HAVE,
  PEK_INVALID_PIECE,
  PEK_OS,
  PEK_CHECKSUM_FAILED,
} peer_error_kind_t;

typedef enum : uint8_t {
  PEER_ACTION_NONE,
  PEER_ACTION_REQUEST_MORE,
  PEER_ACTION_STOP_REQUESTING,
} peer_action_t;

typedef struct {
  peer_error_kind_t kind;
  union {
    int uv_err;
    int errno_err;
  } v;
} peer_error_t;

typedef enum : uint8_t {
  PT_CHOKE,
  PT_UNCHOKE,
  PT_INTERESTED,
  PT_UNINTERESTED,
  PT_HAVE,
  PT_BITFIELD,
  PT_REQUEST,
  PT_PIECE,
  PT_CANCEL,
} peer_tag_t;

typedef enum : uint8_t {
  PMK_NONE,
  PMK_HEARTBEAT,
  PMK_CHOKE,
  PMK_UNCHOKE,
  PMK_INTERESTED,
  PMK_UNINTERESTED,
  PMK_BITFIELD,
  PMK_HAVE,
  PMK_REQUEST,
  PMK_PIECE,
  PMK_CANCEL,
} peer_message_kind_t;

typedef struct {
  uint32_t have;
} peer_message_have_t;

typedef struct {
  uint32_t index, begin, length;
} peer_message_request_t;

typedef struct {
  uint32_t index, begin, length;
} peer_message_cancel_t;

typedef struct {
  pg_array_t(uint8_t) bitfield;
} peer_message_bitfield_t;

typedef struct {
  uint32_t index, begin;
  pg_array_t(uint8_t) data;
} peer_message_piece_t;

typedef struct {
  peer_message_kind_t kind;
  union {
    peer_message_have_t have;
    peer_message_request_t request;
    peer_message_piece_t piece;
    peer_message_bitfield_t bitfield;
  } v;
} peer_message_t;

typedef struct {
  pg_bitarray_t blocks_to_download;
  pg_bitarray_t blocks_downloading;
  pg_bitarray_t blocks_downloaded;
  pg_logger_t *logger;
  bc_metainfo_t *metainfo;
} picker_t;

typedef struct {
  pg_allocator_t allocator;
  pg_logger_t *logger;
  pg_pool_t *peer_pool;
  pg_pool_t write_ctx_pool;
  pg_pool_t read_buf_pool;
  pg_pool_t block_pool;

  download_t *download;
  bc_metainfo_t *metainfo;
  bool me_choked, me_interested, them_choked, them_interested, handshaked;
  uint8_t in_flight_requests;
  pg_bitarray_t them_have_pieces;

  uv_tcp_t connection;
  uv_connect_t connect_req;
  uv_idle_t idle_handle;

  pg_ring_t recv_data;
  char addr_s[INET_ADDRSTRLEN + /* :port */ 6]; // TODO: ipv6

  picker_t *picker;
} peer_t;

typedef struct {
  peer_t *peer;
  char *data;
  uv_write_t req;
} peer_write_ctx_t;

void picker_init(pg_allocator_t allocator, pg_logger_t *logger,
                 picker_t *picker, bc_metainfo_t *metainfo) {
  picker->metainfo = metainfo;
  assert(metainfo->blocks_count > 0);
  assert(metainfo->pieces_count > 0);
  assert(metainfo->blocks_per_piece > 0);
  assert(metainfo->last_piece_block_count > 0);
  assert(metainfo->last_piece_length > 0);

  pg_bitarray_init(allocator, &picker->blocks_to_download,
                   metainfo->blocks_count - 1);
  pg_bitarray_init(allocator, &picker->blocks_downloading,
                   metainfo->blocks_count - 1);
  pg_bitarray_init(allocator, &picker->blocks_downloaded,
                   metainfo->blocks_count - 1);

  pg_bitarray_set_all(&picker->blocks_to_download);

  picker->logger = logger;
}

// TODO: randomness, rarity
uint32_t picker_pick_block(const picker_t *picker,
                           const pg_bitarray_t *them_have_pieces, bool *found) {
  uint64_t i = 0;
  bool is_set = false;
  while (pg_bitarray_next(&picker->blocks_to_download, &i, &is_set)) {
    assert(i > 0);
    if (!is_set)
      continue;

    const uint32_t block = (uint32_t)i - 1;
    assert(block < picker->metainfo->blocks_count);

    const uint32_t piece = block / picker->metainfo->blocks_per_piece;
    assert(piece < picker->metainfo->pieces_count);

    const bool them_have = pg_bitarray_get(them_have_pieces, piece);

    if (!them_have) {
      pg_log_debug(picker->logger,
                   "[%s] need block=%u for piece=%u but they "
                   "don't have it",
                   __func__, block, piece);
      continue;
    }

    pg_log_debug(picker->logger, "[%s] found piece %u", __func__, piece);
    *found = true;

    return block;
  }
  return 0;
}

bool picker_have_all_blocks_for_piece(const picker_t *picker, uint32_t piece) {
  assert(piece < picker->metainfo->pieces_count);

  const uint32_t first_block = piece * picker->metainfo->blocks_per_piece;
  assert(first_block < picker->metainfo->blocks_count);

  const uint32_t last_block =
      first_block + metainfo_block_count_for_piece(picker->metainfo, piece) - 1;
  assert(last_block < picker->metainfo->blocks_count);

  uint64_t i = first_block;
  bool is_set = false;
  while (pg_bitarray_next(&picker->blocks_downloaded, &i, &is_set)) {
    assert(i > 0);
    const uint32_t block = i - 1;
    assert(block < picker->metainfo->blocks_count);

    if (block > last_block)
      return true;
    if (!is_set)
      return false;
  }

  return true;
}

void picker_mark_block_as_downloading(picker_t *picker, uint32_t block) {
  assert(block < picker->metainfo->blocks_count);
  pg_bitarray_set(&picker->blocks_downloading, block);
  pg_bitarray_unset(&picker->blocks_to_download, block);
}

void picker_mark_piece_as_to_download(picker_t *picker, uint32_t piece) {
  assert(piece < picker->metainfo->pieces_count);

  const uint32_t first_block = piece * picker->metainfo->blocks_per_piece;
  assert(first_block < picker->metainfo->blocks_count);

  const uint32_t last_block =
      first_block + metainfo_block_count_for_piece(picker->metainfo, piece) - 1;
  assert(last_block < picker->metainfo->blocks_count);
  assert(first_block <= last_block);

  for (uint32_t block = first_block; block <= last_block; block++) {
    pg_bitarray_set(&picker->blocks_downloaded, block);
    pg_bitarray_unset(&picker->blocks_to_download, block);
    pg_bitarray_unset(&picker->blocks_downloading, block);
  }
}

void picker_mark_block_as_downloaded(picker_t *picker, uint32_t block) {
  assert(block < picker->metainfo->blocks_count);
  pg_bitarray_set(&picker->blocks_downloaded, block);
  pg_bitarray_unset(&picker->blocks_downloading, block);
}

void picker_destroy(picker_t *picker) {
  pg_bitarray_destroy(&picker->blocks_to_download);
  pg_bitarray_destroy(&picker->blocks_downloading);
  pg_bitarray_destroy(&picker->blocks_downloaded);
}

void peer_message_destroy(peer_t *peer, peer_message_t *msg) {
  switch (msg->kind) {
  case PMK_BITFIELD:
    pg_array_free(msg->v.bitfield.bitfield);
    return;
  case PMK_PIECE:
    pg_pool_free(&peer->block_pool, msg->v.piece.data);

  default:; // no-op
  }
}
void peer_close(peer_t *peer);

void peer_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  peer_t *peer = handle->data;

  assert(suggested_size <= peer->read_buf_pool.chunk_size);
  buf->base = pg_pool_alloc(&peer->read_buf_pool);
  buf->len = peer->read_buf_pool.chunk_size;
}

peer_error_t peer_check_handshaked(peer_t *peer) {
  if (peer->handshaked)
    return (peer_error_t){0};

  const uint64_t recv_data_len = pg_ring_len(&peer->recv_data);
  if (recv_data_len < PEER_HANDSHAKE_LENGTH)
    return (peer_error_t){.kind = PEK_NEED_MORE};

  const char handshake_header_expected[] = "\x13"
                                           "BitTorrent protocol";
  char handshake_got[PEER_HANDSHAKE_LENGTH] = "";
  for (uint64_t i = 0; i < PEER_HANDSHAKE_LENGTH; i++)
    handshake_got[i] = pg_ring_pop_front(&peer->recv_data);

  if (memcmp(handshake_got, handshake_header_expected,
             sizeof(handshake_header_expected) - 1) != 0)
    return (peer_error_t){.kind = PEK_WRONG_HANDSHAKE_HEADER};

  if (memcmp(handshake_got + 28, peer->download->info_hash, 20) != 0)
    return (peer_error_t){.kind = PEK_WRONG_HANDSHAKE_HASH};

  peer->handshaked = true;

  pg_log_debug(peer->logger, "[%s] Handshaked", peer->addr_s);

  assert(pg_ring_len(&peer->recv_data) + PEER_HANDSHAKE_LENGTH ==
         recv_data_len);

  return (peer_error_t){0};
}

uint32_t peer_read_u32(pg_ring_t *ring) {
  assert(pg_ring_len(ring) >= sizeof(uint32_t));
  const uint8_t parts[] = {
      pg_ring_pop_front(ring),
      pg_ring_pop_front(ring),
      pg_ring_pop_front(ring),
      pg_ring_pop_front(ring),
  };
  return ntohl(*(uint32_t *)parts);
}

uint32_t peer_peek_read_u32(pg_ring_t *ring) {
  assert(pg_ring_len(ring) >= sizeof(uint32_t));
  const uint8_t parts[] = {
      pg_ring_get(ring, 0),
      pg_ring_get(ring, 1),
      pg_ring_get(ring, 2),
      pg_ring_get(ring, 3),
  };
  return ntohl(*(uint32_t *)parts);
}

peer_error_t peer_message_parse(peer_t *peer, peer_message_t *msg) {
  peer_error_t err = peer_check_handshaked(peer);
  if (err.kind > PEK_NEED_MORE)
    return err;

  if (pg_ring_len(&peer->recv_data) <
      sizeof(uint32_t)) // Check there is room for the announced_len
    return (peer_error_t){.kind = PEK_NEED_MORE};

  const uint32_t announced_len = peer_peek_read_u32(&peer->recv_data);
  if (announced_len == 0) {
    // Heartbeat
    msg->kind = PMK_HEARTBEAT;

    pg_ring_consume_front(&peer->recv_data, 4); // Consume the announced_len
    return err;
  }
  if (announced_len > PEER_MAX_MESSAGE_LENGTH)
    return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

  if (pg_ring_len(&peer->recv_data) <
      4 + 1) // Check there is room for the announced_len + tag
    return (peer_error_t){.kind = PEK_NEED_MORE};

  const uint8_t tag = pg_ring_get(&peer->recv_data, 4);
  pg_log_debug(
      peer->logger,
      "[%s] msg tag=%d announced_len=%u recv_data.len=%llu recv_data.cap=%llu "
      "recv_data.space=%llu "
      "recv_data[0..7]=%#x %#x %#x %#x %#x %#x %#x ",
      peer->addr_s, tag, announced_len, pg_ring_len(&peer->recv_data),
      pg_ring_cap(&peer->recv_data), pg_ring_space(&peer->recv_data),
      pg_ring_len(&peer->recv_data) > 0 ? pg_ring_get(&peer->recv_data, 0) : 0,
      pg_ring_len(&peer->recv_data) > 1 ? pg_ring_get(&peer->recv_data, 1) : 0,
      pg_ring_len(&peer->recv_data) > 2 ? pg_ring_get(&peer->recv_data, 2) : 0,
      pg_ring_len(&peer->recv_data) > 3 ? pg_ring_get(&peer->recv_data, 3) : 0,
      pg_ring_len(&peer->recv_data) > 4 ? pg_ring_get(&peer->recv_data, 4) : 0,
      pg_ring_len(&peer->recv_data) > 5 ? pg_ring_get(&peer->recv_data, 5) : 0,
      pg_ring_len(&peer->recv_data) > 6 ? pg_ring_get(&peer->recv_data, 6) : 0);

  switch (tag) {
  case PT_CHOKE:
    msg->kind = PMK_CHOKE;
    pg_ring_consume_front(&peer->recv_data,
                          4 + 1); // consume announced_len + tag
    return (peer_error_t){0};
  case PT_UNCHOKE:
    msg->kind = PMK_UNCHOKE;

    pg_ring_consume_front(&peer->recv_data,
                          4 + 1); // consume announced_len + tag

    return (peer_error_t){0};
  case PT_INTERESTED:
    msg->kind = PMK_INTERESTED;

    pg_ring_consume_front(&peer->recv_data,
                          4 + 1); // consume announced_len + tag

    return (peer_error_t){0};
  case PT_UNINTERESTED:
    msg->kind = PMK_UNINTERESTED;

    pg_ring_consume_front(&peer->recv_data,
                          4 + 1); // consume announced_len + tag

    return (peer_error_t){0};
  case PT_HAVE: {
    if (announced_len != 5)
      return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

    if (pg_ring_len(&peer->recv_data) < announced_len + 4)
      return (peer_error_t){.kind = PEK_NEED_MORE};

    pg_ring_consume_front(&peer->recv_data,
                          4 + 1); // consume announced_len + tag

    const uint32_t have = peer_read_u32(&peer->recv_data);
    if (have > peer->metainfo->pieces_count)
      return (peer_error_t){.kind = PEK_INVALID_HAVE};

    msg->kind = PMK_HAVE;
    msg->v.have = (peer_message_have_t){have};
    return (peer_error_t){0};
  }
  case PT_BITFIELD: {
    const uint64_t expected_bitfield_len =
        (uint64_t)ceil(((double)peer->metainfo->pieces_count - 1) / 8.0);
    if (announced_len != expected_bitfield_len + /* tag */ 1)
      return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

    if (pg_ring_len(&peer->recv_data) < announced_len + 4)
      return (peer_error_t){.kind = PEK_NEED_MORE};

    msg->kind = PMK_BITFIELD;
    msg->v.bitfield = (peer_message_bitfield_t){0};
    pg_array_init_reserve(msg->v.bitfield.bitfield,
                          peer->metainfo->pieces_count - 1, peer->allocator);

    pg_ring_consume_front(&peer->recv_data,
                          4 + 1); // consume announced_len + tag

    for (uint64_t i = 0; i < announced_len - 1; i++) {
      const uint8_t byte = pg_ring_pop_front(&peer->recv_data);
      pg_array_append(msg->v.bitfield.bitfield, __builtin_bitreverse8(byte));
    }
    pg_log_debug(peer->logger, "[%s] bitfield: last=%#x", peer->addr_s,
                 (uint8_t)msg->v.bitfield.bitfield[announced_len - 2]);
    return (peer_error_t){0};
  }
  case PT_REQUEST: {
    if (announced_len != 1 + 3 * 4)
      return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

    if (pg_ring_len(&peer->recv_data) < announced_len + 4)
      return (peer_error_t){.kind = PEK_NEED_MORE};

    pg_ring_consume_front(&peer->recv_data,
                          4 + 1); // consume announced_len + tag

    msg->kind = PMK_REQUEST;
    msg->v.request = (peer_message_request_t){
        .index = peer_read_u32(&peer->recv_data),
        .begin = peer_read_u32(&peer->recv_data),
        .length = peer_read_u32(&peer->recv_data),
    };

    return (peer_error_t){0};
  }
  case PT_PIECE: {
    if (announced_len < 1 + 2 * 4 + /* Require at least 1 byte of data */ 1)
      return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

    if (pg_ring_len(&peer->recv_data) < announced_len + 4)
      return (peer_error_t){.kind = PEK_NEED_MORE};

    pg_ring_consume_front(&peer->recv_data,
                          4 + 1); // consume announced_len + tag

    msg->kind = PMK_PIECE;
    msg->v.piece = (peer_message_piece_t){
        .index = peer_read_u32(&peer->recv_data),
        .begin = peer_read_u32(&peer->recv_data),
    };
    const uint64_t data_len = announced_len - (1 + 2 * 4);
    // TODO: validate block is in 'downloading'
    if (msg->v.piece.index >= peer->metainfo->pieces_count ||
        msg->v.piece.begin + data_len >
            metainfo_piece_length(peer->metainfo, msg->v.piece.index))
      return (peer_error_t){.kind = PEK_INVALID_PIECE};

    const uint32_t block_for_piece = msg->v.piece.begin / BC_BLOCK_LENGTH;
    if (data_len != metainfo_block_for_piece_length(
                        peer->metainfo, msg->v.piece.index, block_for_piece))
      return (peer_error_t){.kind = PEK_INVALID_PIECE};

    msg->v.piece.data = pg_pool_alloc(&peer->block_pool);
    for (uint64_t i = 0; i < data_len; i++) {
      msg->v.piece.data[i] = pg_ring_pop_front(&peer->recv_data);
    }
    pg_log_debug(peer->logger, "[%s] piece: begin=%u index=%u len=%llu",
                 peer->addr_s, msg->v.piece.begin, msg->v.piece.index,
                 pg_array_len(msg->v.piece.data));

    return (peer_error_t){0};
  }
  case PT_CANCEL: {
    if (announced_len != 1 + 3 * 4)
      return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

    if (pg_ring_len(&peer->recv_data) < announced_len + 4)
      return (peer_error_t){.kind = PEK_NEED_MORE};

    pg_ring_consume_front(&peer->recv_data,
                          4 + 1); // consume announced_len + tag
    msg->kind = PMK_CANCEL;
    msg->v.request = (peer_message_request_t){
        .index = peer_read_u32(&peer->recv_data),
        .begin = peer_read_u32(&peer->recv_data),
        .length = peer_read_u32(&peer->recv_data),
    };
    return (peer_error_t){0};
  }

  default:
    return (peer_error_t){.kind = PEK_INVALID_MESSAGE_TAG};
  }
  __builtin_unreachable();
}

const char *peer_message_kind_to_string(int k) {
  switch (k) {
  case PMK_NONE:
    return "PMK_NONE";
  case PMK_HEARTBEAT:
    return "PMK_HEARTBEAT";
  case PMK_CHOKE:
    return "PMK_CHOKE";
  case PMK_UNCHOKE:
    return "PMK_UNCHOKE";
  case PMK_INTERESTED:
    return "PMK_INTERESTED";
  case PMK_UNINTERESTED:
    return "PMK_UNINTERESTED";
  case PMK_BITFIELD:
    return "PMK_BITFIELD";
  case PMK_HAVE:
    return "PMK_HAVE";
  case PMK_REQUEST:
    return "PMK_REQUEST";
  case PMK_PIECE:
    return "PMK_PIECE";
  case PMK_CANCEL:
    return "PMK_CANCEL";

  default:
    __builtin_unreachable();
  }
}

peer_error_t download_checksum_piece(pg_logger_t *logger,
                                     pg_allocator_t allocator,
                                     download_t *download,
                                     bc_metainfo_t *metainfo, uint32_t piece) {
  const uint64_t offset = piece * metainfo->piece_length;
  const uint64_t length = metainfo_piece_length(metainfo, piece);

  if (lseek(download->fd, offset, SEEK_SET) == -1) {
    pg_log_error(logger, "Failed to lseek(2): err=%s", strerror(errno));
    return (peer_error_t){.kind = PEK_OS, .v = {.errno_err = errno}};
  }

  pg_array_t(uint8_t) data = {0};
  pg_array_init_reserve(data, length, allocator);

  peer_error_t err = {0};
  // TODO: handle partial reads
  ssize_t ret = read(download->fd, data, length);
  if (ret <= 0) {
    pg_log_error(logger, "Failed to read(2): err=%s", strerror(errno));
    err = (peer_error_t){.kind = PEK_OS, .v = {.errno_err = errno}};
    goto end;
  }
  if ((uint64_t)ret != length) {
    pg_log_error(logger, "Failed to read(2) all: %llu/%llu", (uint64_t)ret,
                 length);
    err = (peer_error_t){.kind = PEK_OS};
    goto end;
  }

  uint8_t hash[20] = {0};
  assert(mbedtls_sha1(data, length, hash) == 0);

  assert(piece * 20 + 20 <= metainfo->pieces.len);
  const uint8_t *const expected = (uint8_t *)metainfo->pieces.data + 20 * piece;

  if (memcmp(hash, expected, sizeof(hash)) != 0) {
    err = (peer_error_t){.kind = PEK_CHECKSUM_FAILED};
    goto end;
  }

end:
  pg_array_free(data);
  return err;
}

peer_error_t peer_send_heartbeat(peer_t *peer);
peer_error_t peer_put_block(peer_t *peer, uint32_t piece, uint32_t block,
                            pg_span32_t data) {
  assert(piece < peer->metainfo->pieces_count);
  assert(block < peer->metainfo->blocks_count);

  pg_log_debug(peer->logger, "[%s] peer_put_block: piece=%u block_for_piece=%u",
               peer->addr_s, piece, block);

  const uint64_t offset = (uint64_t)block * BC_BLOCK_LENGTH;
  assert(offset + BC_BLOCK_LENGTH <= peer->metainfo->length);

  if (lseek(peer->download->fd, offset, SEEK_SET) == -1) {
    pg_log_error(peer->logger, "[%s] Failed to lseek(2): err=%s", peer->addr_s,
                 strerror(errno));
    return (peer_error_t){.kind = PEK_OS, .v = {.errno_err = errno}};
  }

  ssize_t ret = write(peer->download->fd, data.data, data.len);
  if (ret <= 0) {
    pg_log_error(peer->logger, "[%s] Failed to write(2): err=%s", peer->addr_s,
                 strerror(errno));
    return (peer_error_t){.kind = PEK_OS, .v = {.errno_err = errno}};
  }
  if (ret != data.len) {
    // TODO: handle partial writes
    pg_log_error(peer->logger, "[%s] Failed to write(2) all data: %zd/%u",
                 peer->addr_s, ret, data.len);
    return (peer_error_t){.kind = PEK_OS};
  }

  picker_mark_block_as_downloaded(peer->picker, block);
  assert(peer->in_flight_requests > 0);
  peer->in_flight_requests -= 1;
  peer->download->downloaded_bytes += data.len;
  assert(peer->download->downloaded_bytes <= peer->metainfo->length);
  peer->download->downloaded_blocks_count += 1;
  assert(peer->download->downloaded_blocks_count <=
         peer->metainfo->blocks_count);

  if (picker_have_all_blocks_for_piece(peer->picker, piece)) {
    pg_log_debug(peer->logger,
                 "[%s] peer_put_block: have all blocks: piece=%u block=%u",
                 peer->addr_s, piece, block);

    peer_error_t err = download_checksum_piece(
        peer->logger, peer->allocator, peer->download, peer->metainfo, piece);
    if (err.kind != PEK_NONE) {
      pg_log_error(peer->logger,
                   "[%s] peer_put_block: piece failed checksum: piece=%u "
                   "block=%u err=%d",
                   peer->addr_s, piece, block, err.kind);
      const uint64_t length = metainfo_piece_length(peer->metainfo, piece);

      assert(peer->download->downloaded_bytes >= length);
      peer->download->downloaded_bytes -= length;
      const uint64_t blocks_count =
          metainfo_block_count_for_piece(peer->metainfo, piece);
      assert(peer->download->downloaded_blocks_count >= blocks_count);
      peer->download->downloaded_blocks_count -= blocks_count;
      return err;
    }
    peer->download->downloaded_pieces_count += 1;
    assert(peer->download->downloaded_pieces_count <=
           peer->metainfo->pieces_count);
  }

  const uint64_t now = uv_hrtime();
  const uint64_t time_diff_s = (now - peer->download->start_ts) / 1e9;
  const double rate = (double)peer->download->downloaded_bytes / time_diff_s;
  pg_log_info(peer->logger,
              "[%s] Downloaded %u/%u pieces, %u/%u blocks, %.2f MiB / %.2f "
              "MiB, %2.f B/s",
              peer->addr_s, peer->download->downloaded_pieces_count,
              peer->metainfo->pieces_count,
              peer->download->downloaded_blocks_count,
              peer->metainfo->blocks_count,
              (double)peer->download->downloaded_bytes / 1024 / 1024,
              (double)peer->metainfo->length / 1024 / 1024, rate);

  return (peer_error_t){0};
}

peer_error_t peer_message_handle(peer_t *peer, peer_message_t *msg,
                                 peer_action_t *action) {
  switch (msg->kind) {
  case PMK_HEARTBEAT:
    return peer_send_heartbeat(peer);
  case PMK_CHOKE:
    peer->them_choked = true;
    *action = PEER_ACTION_STOP_REQUESTING;
    return (peer_error_t){0};
  case PMK_UNCHOKE:
    peer->them_choked = false;
    *action = PEER_ACTION_REQUEST_MORE;
    return (peer_error_t){0};
  case PMK_INTERESTED:
    peer->them_interested = true;
    return (peer_error_t){0};
  case PMK_UNINTERESTED:
    peer->them_interested = false;
    return (peer_error_t){0};
  case PMK_HAVE: {
    const uint32_t have = msg->v.have.have;
    pg_bitarray_set(&peer->them_have_pieces, have);

    *action = PEER_ACTION_REQUEST_MORE;
    return (peer_error_t){0};
  }
  case PMK_BITFIELD: {
    pg_array_t(uint8_t) bitfield = msg->v.bitfield.bitfield;

    pg_bitarray_setv(&peer->them_have_pieces, bitfield, pg_array_len(bitfield));

    *action = PEER_ACTION_REQUEST_MORE;
    return (peer_error_t){0};
  }
  case PMK_PIECE: {
    const peer_message_piece_t piece_msg = msg->v.piece;
    const uint32_t piece = piece_msg.index;

    if (peer->in_flight_requests == 0) {
      pg_log_error(peer->logger, "Received unwanted piece: piece=%u", piece);
      return (peer_error_t){.kind = PEK_INVALID_PIECE};
    }

    const uint32_t block_for_piece = piece_msg.begin / BC_BLOCK_LENGTH;
    assert(block_for_piece <
           metainfo_block_count_for_piece(peer->metainfo, piece));

    const uint32_t block = metainfo_block_for_piece_to_block(
        peer->metainfo, piece, block_for_piece);
    assert(block < peer->metainfo->blocks_count);

    const pg_span32_t span = (pg_span32_t){
        .data = (char *)piece_msg.data,
        .len = metainfo_block_for_piece_length(peer->metainfo, piece,
                                               block_for_piece),
    };

    pg_log_debug(
        peer->logger,
        "[%s] piece: begin=%u piece=%u len=%llu block_for_piece=%u block=%u",
        peer->addr_s, piece_msg.begin, piece, pg_array_len(piece_msg.data),
        block_for_piece, block);
    peer_put_block(peer, piece, block, span);

    *action = PEER_ACTION_REQUEST_MORE;
    return (peer_error_t){0};
  }
  case PMK_REQUEST:
    // TODO
    return (peer_error_t){0};
  case PMK_CANCEL:
    // TODO
    return (peer_error_t){0};

  default:
    __builtin_unreachable();
  }
}

peer_error_t peer_send_request(peer_t *peer, uint32_t block);

peer_error_t peer_request_more_blocks(peer_t *peer, peer_action_t *action) {
  if (peer->in_flight_requests >= PEER_MAX_IN_FLIGHT_REQUESTS ||
      peer->them_choked) {
    pg_log_debug(peer->logger,
                 "[%s] request_more_blocks stop: in_flight_requests=%hhu "
                 "them_choked=%d",
                 peer->addr_s, peer->in_flight_requests, peer->them_choked);

    *action = PEER_ACTION_STOP_REQUESTING;
    return (peer_error_t){0};
  }

  while (peer->in_flight_requests < PEER_MAX_IN_FLIGHT_REQUESTS) {
    bool found = false;
    const uint32_t block =
        picker_pick_block(peer->picker, &peer->them_have_pieces, &found);

    // Nothing to download anymore
    if (!found) {
      pg_log_debug(peer->logger,
                   "[%s] request_more_blocks no more pieces to download: "
                   "in_flight_requests=%hhu "
                   "them_choked=%d",
                   peer->addr_s, peer->in_flight_requests, peer->them_choked);
      *action = PEER_ACTION_STOP_REQUESTING;
      return (peer_error_t){0};
    }

    picker_mark_block_as_downloading(peer->picker, block);
    assert(peer->in_flight_requests <= PEER_MAX_IN_FLIGHT_REQUESTS);
    peer->in_flight_requests += 1;

    peer_error_t err = peer_send_request(peer, block);
    if (err.kind != PEK_NONE)
      return err;
  }

  return (peer_error_t){0};
}

void peer_on_idle(uv_idle_t *handle) {
  peer_t *peer = handle->data;

  peer_action_t action = PEER_ACTION_NONE;
  peer_error_t err = peer_request_more_blocks(peer, &action);
  if (err.kind != PEK_NONE) {
    pg_log_error(peer->logger, "[%s] failed to request more blocks: %d",
                 peer->addr_s, err.kind);
    peer_close(peer);
    return;
  }

  if (action == PEER_ACTION_STOP_REQUESTING) {
    uv_idle_stop(&peer->idle_handle);
  }
}

void peer_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  peer_t *peer = stream->data;
  pg_log_debug(peer->logger, "[%s] peer_on_read: %ld", peer->addr_s, nread);

  if (nread > 0) {
    assert(buf != NULL);
    assert(buf->base != NULL);
    assert(buf->len > 0);

    pg_ring_push_backv(&peer->recv_data, (uint8_t *)buf->base, nread);
  }
  if (buf != NULL && buf->base != NULL)
    pg_pool_free(&peer->read_buf_pool, buf->base);

  if (nread <= 0) {
    pg_log_error(peer->logger, "[%s] peer_on_read failed: %s", peer->addr_s,
                 strerror(-nread));
    peer_close(peer);
    return;
  }

  bool idle_started = false;
  while (true) { // Parse as many messages as available in the recv_data
    peer_message_t msg = {0};
    peer_error_t err = peer_message_parse(peer, &msg);
    if (err.kind == PEK_NEED_MORE)
      break;
    if (err.kind != PEK_NONE) {
      pg_log_error(peer->logger, "[%s] peer_message_parse failed: %d",
                   peer->addr_s, err.kind);
      peer_close(peer);
      return;
    }

    pg_log_debug(peer->logger, "[%s] msg=%s", peer->addr_s,
                 peer_message_kind_to_string(msg.kind));
    assert(msg.kind != PMK_NONE);

    peer_action_t action = PEER_ACTION_NONE;
    err = peer_message_handle(peer, &msg, &action);
    peer_message_destroy(peer, &msg);

    if (err.kind != PEK_NONE) {
      pg_log_error(peer->logger, "[%s] peer_message_handle failed: %d\n",
                   peer->addr_s, err.kind);
      peer_close(peer);
    }

    if (action == PEER_ACTION_REQUEST_MORE && !idle_started) {
      idle_started = true;
      uv_idle_start(&peer->idle_handle, peer_on_idle);
    } else if (action == PEER_ACTION_STOP_REQUESTING)
      uv_idle_stop(&peer->idle_handle);
  }
}

void peer_on_write(uv_write_t *req, int status) {
  peer_write_ctx_t *ctx = req->data;
  peer_t *peer = ctx->peer;

  pg_log_debug(peer->logger, "[%s] peer_on_write status=%d", peer->addr_s,
               status);

  peer->allocator.free(ctx->data);
  pg_pool_free(&peer->write_ctx_pool, ctx);

  if (status != 0) {
    pg_log_error(peer->logger, "[%s] on_write failed: %d %s", peer->addr_s,
                 -status, strerror(-status));
    peer_close(peer);
  }
}

peer_error_t peer_send_buf(peer_t *peer, uv_buf_t buf) {
  peer_write_ctx_t *ctx = pg_pool_alloc(&peer->write_ctx_pool);
  assert(ctx != NULL);
  ctx->req.data = ctx;
  ctx->data = buf.base;
  ctx->peer = peer;

  int ret = 0;
  if ((ret = uv_write(&ctx->req, (uv_stream_t *)&peer->connection, &buf, 1,
                      peer_on_write)) != 0) {
    pg_log_error(peer->logger, "[%s] uv_write failed: %d", peer->addr_s, ret);

    pg_pool_free(&peer->write_ctx_pool, ctx);
    peer->allocator.free(buf.base);

    return (peer_error_t){.kind = PEK_UV, .v = {-ret}};
  }

  return (peer_error_t){0};
}

peer_error_t peer_send_heartbeat(peer_t *peer) {
  uv_buf_t buf = uv_buf_init(peer->allocator.realloc(sizeof(uint32_t), NULL, 0),
                             sizeof(uint32_t));
  return peer_send_buf(peer, buf);
}

peer_error_t peer_send_handshake(peer_t *peer) {
  uv_buf_t buf =
      uv_buf_init(peer->allocator.realloc(PEER_HANDSHAKE_LENGTH, NULL, 0),
                  PEER_HANDSHAKE_LENGTH);

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
  memcpy(buf.base, handshake_header, sizeof(handshake_header));
  memcpy(buf.base + sizeof(handshake_header), peer->download->info_hash,
         sizeof(peer->download->info_hash));
  memcpy(buf.base + sizeof(handshake_header) +
             sizeof(peer->download->info_hash),
         peer->download->peer_id, sizeof(peer->download->peer_id));

  return peer_send_buf(peer, buf);
}

uint8_t *peer_write_u32(uint8_t *buf, uint64_t *buf_len, uint32_t x) {
  *(uint32_t *)buf = htonl(x);
  *buf_len += 4;
  return buf + 4;
}

uint8_t *peer_write_u8(uint8_t *buf, uint64_t *buf_len, uint8_t x) {
  buf[0] = x;
  *buf_len += 1;
  return buf + 1;
}

peer_error_t peer_send_request(peer_t *peer, uint32_t block) {
  const uint32_t piece = block / peer->metainfo->blocks_per_piece;
  const uint32_t begin =
      block * BC_BLOCK_LENGTH - piece * peer->metainfo->piece_length;
  const uint32_t block_for_piece =
      metainfo_block_to_block_for_piece(peer->metainfo, piece, block);
  const uint32_t length =
      metainfo_block_for_piece_length(peer->metainfo, piece, block_for_piece);

  uv_buf_t buf =
      uv_buf_init(peer->allocator.realloc(4 + 1 + 3 * 4, NULL, 0), 0);

  uint8_t *bytes = (uint8_t *)buf.base;
  bytes = peer_write_u32(bytes, (uint64_t *)&buf.len, 1 + 3 * 4);
  bytes = peer_write_u8(bytes, (uint64_t *)&buf.len, PT_REQUEST);

  bytes = peer_write_u32(bytes, (uint64_t *)&buf.len, piece);
  bytes = peer_write_u32(bytes, (uint64_t *)&buf.len, begin);
  bytes = peer_write_u32(bytes, (uint64_t *)&buf.len, length);

  pg_log_debug(
      peer->logger,
      "[%s] Sent Request: index=%u begin=%u length=%u in_flight_requests=%u",
      peer->addr_s, piece, begin, length, peer->in_flight_requests);

  if (peer->download->start_ts == 0ULL)
    peer->download->start_ts = uv_hrtime();
  return peer_send_buf(peer, buf);
}

peer_error_t peer_send_choke(peer_t *peer) {
  uv_buf_t buf = uv_buf_init(peer->allocator.realloc(4 + 1, NULL, 0), 0);

  uint8_t *bytes = (uint8_t *)buf.base;
  bytes = peer_write_u32(bytes, (uint64_t *)&buf.len, 1);
  bytes = peer_write_u8(bytes, (uint64_t *)&buf.len, PT_CHOKE);

  return peer_send_buf(peer, buf);
}

peer_error_t peer_send_interested(peer_t *peer) {
  uv_buf_t buf = uv_buf_init(peer->allocator.realloc(4 + 1, NULL, 0), 0);

  uint8_t *bytes = (uint8_t *)buf.base;
  bytes = peer_write_u32(bytes, (uint64_t *)&buf.len, 1);
  bytes = peer_write_u8(bytes, (uint64_t *)&buf.len, PT_INTERESTED);

  return peer_send_buf(peer, buf);
}

peer_error_t peer_send_prologue(peer_t *peer) {
  peer_error_t err = {0};
  err = peer_send_handshake(peer);
  if (err.kind != PEK_NONE)
    return err;

  err = peer_send_interested(peer);
  if (err.kind != PEK_NONE)
    return err;

  peer->me_interested = true;

  return err;
}

void peer_on_connect(uv_connect_t *handle, int status) {
  peer_t *peer = handle->data;
  assert(peer != NULL);

  if (status != 0) {
    pg_log_error(peer->logger, "[%s] on_connect: %d %s", peer->addr_s, -status,
                 strerror(-status));
    peer_close(peer);
    return;
  }
  pg_log_info(peer->logger, "[%s] Connected", peer->addr_s);

  int ret = 0;
  if ((ret = uv_read_start((uv_stream_t *)&peer->connection, peer_alloc,
                           peer_on_read)) != 0) {
    pg_log_error(peer->logger, "[%s] uv_read_start failed: %d", peer->addr_s,
                 ret);
    peer_close(peer);
    return;
  }

  peer_error_t err = peer_send_prologue(peer);
  if (err.kind != PEK_NONE) {
    peer_close(peer);
    return;
  }

  uv_idle_init(uv_default_loop(), &peer->idle_handle);
}

void peer_init(peer_t *peer, pg_logger_t *logger, pg_pool_t *peer_pool,
               download_t *download, bc_metainfo_t *metainfo, picker_t *picker,
               tracker_peer_address_t address) {
  peer->allocator = pg_heap_allocator(); // FIXME
  peer->picker = picker;

  pg_pool_init(
      &peer->write_ctx_pool, sizeof(peer_write_ctx_t),
      (PEER_MAX_IN_FLIGHT_REQUESTS +
       /* arbitrary, account for handshake, heartbeats and so on */ 20));

  pg_pool_init(&peer->read_buf_pool,
               /* suggested size from libuv + embeded linked list */ 65536 +
                   sizeof(void *),
               30);

  pg_pool_init(&peer->block_pool, BC_BLOCK_LENGTH,
               PEER_MAX_IN_FLIGHT_REQUESTS); // TODO: increase when starting to
                                             // handle Request msg

  peer->peer_pool = peer_pool;
  peer->logger = logger;
  peer->download = download;
  peer->metainfo = metainfo;
  pg_bitarray_init(peer->allocator, &peer->them_have_pieces,
                   metainfo->pieces_count - 1);
  peer->connect_req.data = peer;
  peer->connection.data = peer;
  peer->idle_handle.data = peer;
  pg_ring_init(peer->allocator, &peer->recv_data,
               /* semi-arbitrary */ 2 * UINT16_MAX);

  snprintf(peer->addr_s, sizeof(peer->addr_s), "%s:%hu",
           inet_ntoa(*(struct in_addr *)&address.ip), htons(address.port));

  peer->them_choked = true;
  peer->them_interested = false;
  peer->me_choked = true;
  peer->me_interested = false;
}

peer_error_t peer_connect(peer_t *peer, tracker_peer_address_t address) {
  int ret = 0;
  if ((ret = uv_tcp_init(uv_default_loop(), &peer->connection)) != 0) {
    pg_log_error(peer->logger, "[%s] Failed to uv_tcp_init: %d %s",
                 peer->addr_s, ret, strerror(ret));
    return (peer_error_t){.kind = PEK_UV, .v = {.uv_err = -ret}};
  }

  struct sockaddr_in addr = (struct sockaddr_in){
      .sin_port = address.port,
      .sin_family = AF_INET,
      .sin_addr = {.s_addr = address.ip},
  };

  if ((ret = uv_tcp_connect(&peer->connect_req, &peer->connection,
                            (struct sockaddr *)&addr, peer_on_connect)) != 0) {
    pg_log_error(peer->logger, "[%s] Failed to uv_tcp_connect: %d %s",
                 peer->addr_s, ret, strerror(ret));
    return (peer_error_t){.kind = PEK_UV, .v = {.uv_err = -ret}};
  }
  return (peer_error_t){.kind = PEK_NONE};
}

void peer_destroy(peer_t *peer) {
  pg_bitarray_destroy(&peer->them_have_pieces);
  pg_ring_destroy(&peer->recv_data);

  pg_pool_destroy(&peer->write_ctx_pool);
  pg_pool_destroy(&peer->read_buf_pool);
  pg_pool_destroy(&peer->block_pool);

  pg_pool_free(peer->peer_pool, peer);
}

void peer_on_close(uv_handle_t *handle) {
  peer_t *peer = handle->data;
  assert(peer != NULL);

  pg_log_debug(peer->logger, "[%s] Closing peer", peer->addr_s);

  // FIXME: mark piece as to download

  uv_idle_stop(&peer->idle_handle);

  peer_destroy(peer);
}

void peer_close(peer_t *peer) {
  // `peer_close` is thus idempotent
  if (!uv_is_closing((uv_handle_t *)&peer->connection)) {
    uv_tcp_close_reset(&peer->connection, peer_on_close);
  }
}

void download_init(download_t *download, uint8_t *info_hash, uint8_t *peer_id,
                   int fd) {
  assert(fd >= 0);

  download->fd = fd;
  download->start_ts = 0;

  memcpy(download->info_hash, info_hash, 20);
  memcpy(download->peer_id, peer_id, 20);
}

peer_error_t picker_checksum_all(pg_allocator_t allocator, pg_logger_t *logger,
                                 picker_t *picker, bc_metainfo_t *metainfo,
                                 download_t *download) {
  pg_log_debug(logger, "Checksumming file");

  peer_error_t err = {0};
  pg_array_t(uint8_t) file_data = {0};
  const int64_t ret = pg_read_file_fd(allocator, download->fd, &file_data);
  if (ret != 0) {
    err = (peer_error_t){.kind = PEK_OS, .v = {.errno_err = ret}};
    goto end;
  }

  for (uint32_t piece = 0; piece < metainfo->pieces_count; piece++) {
    const uint64_t length = metainfo_piece_length(metainfo, piece);

    const uint64_t offset = piece * metainfo->piece_length;
    uint8_t hash[20] = {0};
    pg_log_debug(logger,
                 "%s: checksumming: piece=%u length=%llu "
                 "offset=%llu file_length=%llu",
                 __func__, piece, length, offset, pg_array_len(file_data));
    assert(offset + length <= pg_array_len(file_data));
    assert(mbedtls_sha1(file_data + offset, length, hash) == 0);

    assert(piece * 20 + 20 <= metainfo->pieces.len);
    const uint8_t *const expected =
        (uint8_t *)metainfo->pieces.data + 20 * piece;

    if (memcmp(hash, expected, sizeof(hash)) != 0) {
      pg_log_error(logger,
                   "download_checksum_all: piece failed checksum: piece=%u "
                   " err=%d",
                   piece, err.kind);
    } else {
      pg_log_debug(logger,
                   "download_checksum_all: piece passed checksum: piece=%u ",
                   piece);

      picker_mark_piece_as_to_download(picker, piece);

      assert(download->downloaded_pieces_count < metainfo->pieces_count);
      download->downloaded_pieces_count += 1;

      download->downloaded_bytes += length;
      assert(download->downloaded_bytes <= metainfo->length);

      assert(download->downloaded_blocks_count < metainfo->blocks_count);
      download->downloaded_blocks_count +=
          metainfo_block_count_for_piece(metainfo, piece);
    }
  }

  pg_log_info(logger, "%s: have %u/%u pieces", __func__,
              download->downloaded_pieces_count, metainfo->pieces_count);

end:
  pg_array_free(file_data);
  return err;
}
