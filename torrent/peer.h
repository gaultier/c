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
#include "tracker.h"

#define PEER_HANDSHAKE_HEADER_LENGTH ((uint64_t)19)
#define PEER_HANDSHAKE_LENGTH \
  ((uint64_t)(1 + PEER_HANDSHAKE_HEADER_LENGTH + 8 + 20 + 20))
#define PEER_MAX_MESSAGE_LENGTH ((uint64_t)1 << 27)
#define PEER_MAX_IN_FLIGHT_REQUESTS ((uint64_t)5)
#define PEER_BLOCK_LENGTH ((uint32_t)1 << 14)

typedef struct {
  int fd;
  uint8_t info_hash[20];
  uint8_t peer_id[20];
  uint32_t pieces_downloaded_count, pieces_count;
  uint64_t blocks_per_piece, last_piece_length, last_piece_block_count,
      downloaded_bytes;
  pg_bitarray_t pieces_downloaded, pieces_downloading, pieces_to_download;
} download_t;

typedef enum {
  PEK_NONE,
  PEK_NEED_MORE,
  PEK_UV,
  PEK_WRONG_HANDSHAKE_HEADER,
  PEK_WRONG_HANDSHAKE_HASH,
  PEK_INVALID_ANNOUNCED_LENGTH,
  PEK_INVALID_MESSAGE_TAG,
  PEK_INVALID_BITFIELD,
  PEK_INVALID_HAVE,
  PEK_OS,
} peer_error_kind_t;

typedef struct {
  peer_error_kind_t kind;
  union {
    int uv_err;
    int errno_err;
  } v;
} peer_error_t;

typedef enum {
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

typedef enum {
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

void peer_message_destroy(peer_message_t* msg) {
  switch (msg->kind) {
    case PMK_BITFIELD:
      pg_array_free(msg->v.bitfield.bitfield);
      return;
    case PMK_PIECE:
      pg_array_free(msg->v.piece.data);

    default:;  // no-op
  }
}

typedef struct {
  pg_allocator_t allocator;
  pg_logger_t* logger;

  download_t* download;
  bc_metainfo_t* metainfo;
  bool me_choked, me_interested, them_choked, them_interested, handshaked;
  uint8_t in_flight_requests;
  pg_bitarray_t them_have_pieces, blocks_for_piece_downloaded,
      blocks_for_piece_downloading, blocks_for_piece_to_download;
  uint32_t downloading_piece;

  uv_tcp_t connection;
  uv_connect_t connect_req;
  uv_idle_t idle_handle;

  pg_ring_t recv_data;
  char addr_s[INET_ADDRSTRLEN + /* :port */ 6];  // TODO: ipv6
} peer_t;

void peer_close(peer_t* peer);

// TODO: use pool allocator?
void peer_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  peer_t* peer = handle->data;
  buf->base = peer->allocator.realloc(suggested_size, NULL, 0);
  buf->len = suggested_size;
}

bool peer_is_last_piece(peer_t* peer, uint32_t piece) {
  return piece == peer->download->pieces_count;
}

void peer_mark_block_as_downloading(peer_t* peer, uint32_t block) {
  assert(pg_bitarray_get(&peer->blocks_for_piece_to_download, block) == true);
  assert(pg_bitarray_get(&peer->blocks_for_piece_downloading, block) == false);
  assert(pg_bitarray_get(&peer->blocks_for_piece_downloaded, block) == false);

  pg_bitarray_unset(&peer->blocks_for_piece_to_download, block);
  pg_bitarray_set(&peer->blocks_for_piece_downloading, block);

  assert(peer->in_flight_requests < PEER_MAX_IN_FLIGHT_REQUESTS);

  peer->in_flight_requests += 1;

  pg_log_debug(peer->logger, "[%s] peer_mark_block_as_downloading: block=%u",
               peer->addr_s, block);
}

void peer_mark_block_as_downloaded(peer_t* peer, uint32_t block) {
  assert(pg_bitarray_get(&peer->blocks_for_piece_to_download, block) == false);
  assert(pg_bitarray_get(&peer->blocks_for_piece_downloading, block) == true);
  assert(pg_bitarray_get(&peer->blocks_for_piece_downloaded, block) == false);

  pg_bitarray_unset(&peer->blocks_for_piece_downloading, block);
  pg_bitarray_set(&peer->blocks_for_piece_downloaded, block);

  assert(peer->in_flight_requests <= PEER_MAX_IN_FLIGHT_REQUESTS);

  peer->in_flight_requests -= 1;
}

void peer_mark_piece_as_to_download(peer_t* peer, uint32_t piece) {
  if (piece == UINT32_MAX) return;

  pg_bitarray_set(&peer->download->pieces_to_download, piece);
  pg_bitarray_unset(&peer->download->pieces_downloading, piece);
  assert(pg_bitarray_get(&peer->download->pieces_downloaded, piece) == false);
}

void peer_mark_piece_as_downloading(peer_t* peer, uint32_t piece) {
  assert(pg_bitarray_get(&peer->download->pieces_to_download, piece) == true);
  assert(pg_bitarray_get(&peer->download->pieces_downloading, piece) == false);
  assert(pg_bitarray_get(&peer->download->pieces_downloaded, piece) == false);

  peer->downloading_piece = piece;

  pg_bitarray_unset(&peer->download->pieces_to_download, piece);
  pg_bitarray_set(&peer->download->pieces_downloading, piece);

  pg_bitarray_set_all(&peer->blocks_for_piece_to_download);
}

void peer_mark_piece_as_downloaded(peer_t* peer, uint32_t piece) {
  assert(pg_bitarray_get(&peer->download->pieces_to_download, piece) == false);
  pg_bitarray_unset(&peer->download->pieces_downloading, piece);
  pg_bitarray_set(&peer->download->pieces_downloaded, piece);

  peer->download->pieces_downloaded_count += 1;
  assert(peer->download->pieces_downloaded_count <=
         peer->download->pieces_count);

  pg_bitarray_clear(&peer->blocks_for_piece_downloaded);
  pg_bitarray_clear(&peer->blocks_for_piece_downloading);
  pg_bitarray_clear(&peer->blocks_for_piece_to_download);

  peer->downloading_piece = UINT32_MAX;
}

uint32_t peer_block_count_per_piece(peer_t* peer, uint32_t piece) {
  if (peer_is_last_piece(peer, piece))
    return peer->download->last_piece_block_count;
  else
    return peer->download->blocks_per_piece;
}

bool peer_have_all_blocks_for_downloading_piece(peer_t* peer) {
  return pg_bitarray_count_set(&peer->blocks_for_piece_downloaded) ==
         peer_block_count_per_piece(peer, peer->downloading_piece);
}

peer_error_t peer_check_handshaked(peer_t* peer) {
  if (peer->handshaked) return (peer_error_t){0};

  const uint64_t recv_data_len = pg_ring_len(&peer->recv_data);
  if (recv_data_len < PEER_HANDSHAKE_LENGTH)
    return (peer_error_t){.kind = PEK_NEED_MORE};

  const char handshake_header_expected[] =
      "\x13"
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

uint32_t peer_read_u32(pg_ring_t* ring) {
  assert(pg_ring_len(ring) >= sizeof(uint32_t));
  const uint8_t parts[] = {
      pg_ring_pop_front(ring),
      pg_ring_pop_front(ring),
      pg_ring_pop_front(ring),
      pg_ring_pop_front(ring),
  };
  return ntohl(*(uint32_t*)parts);
}

uint32_t peer_peek_read_u32(pg_ring_t* ring) {
  assert(pg_ring_len(ring) >= sizeof(uint32_t));
  const uint8_t parts[] = {
      pg_ring_get(ring, 0),
      pg_ring_get(ring, 1),
      pg_ring_get(ring, 2),
      pg_ring_get(ring, 3),
  };
  return ntohl(*(uint32_t*)parts);
}

peer_error_t peer_message_parse(peer_t* peer, peer_message_t* msg) {
  peer_error_t err = peer_check_handshaked(peer);
  if (err.kind > PEK_NEED_MORE) return err;

  if (pg_ring_len(&peer->recv_data) <
      sizeof(uint32_t))  // Check there is room for the announced_len
    return (peer_error_t){.kind = PEK_NEED_MORE};

  const uint32_t announced_len = peer_peek_read_u32(&peer->recv_data);
  if (announced_len == 0) {
    // Heartbeat
    msg->kind = PMK_HEARTBEAT;

    pg_ring_consume_front(&peer->recv_data, 4);  // Consume the announced_len
    return err;
  }
  if (announced_len > PEER_MAX_MESSAGE_LENGTH)
    return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

  if (pg_ring_len(&peer->recv_data) <
      4 + 1)  // Check there is room for the announced_len + tag
    return (peer_error_t){.kind = PEK_NEED_MORE};

  const uint8_t tag = pg_ring_get(&peer->recv_data, 4);
  pg_log_debug(
      peer->logger,
      "[%s] msg tag=%d announced_len=%u recv_data.len=%llu recv_data.cap=%llu "
      "recv_data[0..7]=%#x %#x %#x %#x %#x %#x %#x ",
      peer->addr_s, tag, announced_len, pg_ring_len(&peer->recv_data),
      pg_ring_cap(&peer->recv_data),
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
                            4 + 1);  // consume announced_len + tag
      return (peer_error_t){0};
    case PT_UNCHOKE:
      msg->kind = PMK_UNCHOKE;

      pg_ring_consume_front(&peer->recv_data,
                            4 + 1);  // consume announced_len + tag

      return (peer_error_t){0};
    case PT_INTERESTED:
      msg->kind = PMK_INTERESTED;

      pg_ring_consume_front(&peer->recv_data,
                            4 + 1);  // consume announced_len + tag

      return (peer_error_t){0};
    case PT_UNINTERESTED:
      msg->kind = PMK_UNINTERESTED;

      pg_ring_consume_front(&peer->recv_data,
                            4 + 1);  // consume announced_len + tag

      return (peer_error_t){0};
    case PT_HAVE: {
      if (announced_len != 5)
        return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

      if (pg_ring_len(&peer->recv_data) < announced_len + 4)
        return (peer_error_t){.kind = PEK_NEED_MORE};

      pg_ring_consume_front(&peer->recv_data,
                            4 + 1);  // consume announced_len + tag

      const uint32_t have = peer_read_u32(&peer->recv_data);
      msg->kind = PMK_HAVE;
      msg->v.have = (peer_message_have_t){have};
      return (peer_error_t){0};
    }
    case PT_BITFIELD: {
      if (announced_len < 1)
        return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

      if (pg_ring_len(&peer->recv_data) < announced_len + 4)
        return (peer_error_t){.kind = PEK_NEED_MORE};

      msg->kind = PMK_BITFIELD;
      msg->v.bitfield = (peer_message_bitfield_t){0};
      pg_array_init_reserve(msg->v.bitfield.bitfield, announced_len - 1,
                            peer->allocator);

      pg_ring_consume_front(&peer->recv_data,
                            4 + 1);  // consume announced_len + tag

      for (uint64_t i = 0; i < announced_len - 1; i++) {
        pg_array_append(msg->v.bitfield.bitfield,
                        pg_ring_pop_front(&peer->recv_data));
      }
      return (peer_error_t){0};
    }
    case PT_REQUEST: {
      if (announced_len != 1 + 3 * 4)
        return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

      if (pg_ring_len(&peer->recv_data) < announced_len + 4)
        return (peer_error_t){.kind = PEK_NEED_MORE};

      msg->kind = PMK_REQUEST;
      // TODO .v

      pg_ring_consume_front(&peer->recv_data,
                            4 + announced_len);  // consume all
      return (peer_error_t){0};
    }
    case PT_PIECE: {
      if (announced_len < 1 + 2 * 4 + /* Require at least 1 byte of data */ 1)
        return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

      if (pg_ring_len(&peer->recv_data) < announced_len + 4)
        return (peer_error_t){.kind = PEK_NEED_MORE};

      pg_ring_consume_front(&peer->recv_data,
                            4 + 1);  // consume announced_len + tag

      msg->kind = PMK_PIECE;
      msg->v.piece = (peer_message_piece_t){
          .index = peer_read_u32(&peer->recv_data),
          .begin = peer_read_u32(&peer->recv_data),
      };
      pg_array_init_reserve(msg->v.piece.data, announced_len - (1 + 2 * 4),
                            peer->allocator);
      for (uint64_t i = 0; i < announced_len - (1 + 2 * 4); i++) {
        msg->v.piece.data[i] = pg_ring_pop_front(&peer->recv_data);
      }
      pg_log_debug(peer->logger, "[%s] piece: begin=%u index=%u len=%llu",
                   peer->addr_s, msg->v.piece.begin, msg->v.piece.index,
                   pg_array_count(msg->v.piece.data));

      return (peer_error_t){0};
    }
    case PT_CANCEL: {
      if (announced_len != 1 + 3 * 4)
        return (peer_error_t){.kind = PEK_INVALID_ANNOUNCED_LENGTH};

      if (pg_ring_len(&peer->recv_data) < announced_len + 4)
        return (peer_error_t){.kind = PEK_NEED_MORE};

      msg->kind = PMK_CANCEL;
      // TODO msg->v.have = have;
      pg_ring_consume_front(&peer->recv_data, announced_len + 4);
      return (peer_error_t){0};
    }

    default:
      return (peer_error_t){.kind = PEK_INVALID_MESSAGE_TAG};
  }
  __builtin_unreachable();
}

const char* peer_message_kind_to_string(int k) {
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

peer_error_t peer_send_heartbeat(peer_t* peer);
peer_error_t peer_put_block(peer_t* peer, uint32_t block_for_piece,
                            pg_span_t data) {
  const uint32_t piece = peer->downloading_piece;

  pg_log_debug(peer->logger, "[%s] peer_put_block: piece=%u block_for_piece=%u",
               peer->addr_s, piece, block_for_piece);

  if (pg_bitarray_get(&peer->blocks_for_piece_downloaded, block_for_piece)) {
    pg_log_debug(peer->logger,
                 "[%s] peer_put_block already have block, duplicate?: piece=%u "
                 "block_for_piece=%u",
                 peer->addr_s, piece, block_for_piece);
    return (peer_error_t){0};
  }

  const uint64_t offset =
      (uint64_t)piece * (uint64_t)peer->metainfo->piece_length +
      (uint64_t)block_for_piece * PEER_BLOCK_LENGTH;

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

  peer_mark_block_as_downloaded(peer, block_for_piece);
  peer->download->downloaded_bytes += data.len;

  if (peer_have_all_blocks_for_downloading_piece(peer)) {
    pg_log_debug(
        peer->logger,
        "[%s] peer_put_block: have all blocks: piece=%u block_for_piece=%u",
        peer->addr_s, piece, block_for_piece);
    // TODO: checksum piece

    peer_mark_piece_as_downloaded(peer, peer->downloading_piece);
  }

  pg_log_info(peer->logger, "[%s] Downloaded %u/%u pieces, %.2f MiB / %.2f MiB",
              peer->addr_s, peer->download->pieces_downloaded_count,
              peer->download->pieces_count,
              (double)peer->download->downloaded_bytes / 1024 / 1024,
              (double)peer->metainfo->length / 1024 / 1024);

  return (peer_error_t){0};
}

peer_error_t peer_message_handle(peer_t* peer, peer_message_t* msg,
                                 bool* request_more) {
  switch (msg->kind) {
    case PMK_HEARTBEAT:
      return peer_send_heartbeat(peer);
    case PMK_CHOKE:
      peer->them_choked = true;
      return (peer_error_t){0};
    case PMK_UNCHOKE:
      peer->them_choked = false;
      return (peer_error_t){0};
    case PMK_INTERESTED:
      peer->them_interested = false;
      return (peer_error_t){0};
    case PMK_UNINTERESTED:
      peer->them_interested = false;
      return (peer_error_t){0};
    case PMK_HAVE: {
      const uint32_t have = msg->v.have.have;

      if (have >= peer->download->pieces_count) {
        return (peer_error_t){.kind = PEK_INVALID_HAVE};
      }
      pg_bitarray_set(&peer->them_have_pieces, have);

      *request_more = true;
      return (peer_error_t){0};
    }
    case PMK_BITFIELD: {
      const uint64_t expected_length =
          (uint64_t)ceil((double)peer->download->pieces_count / 8);

      pg_array_t(uint8_t) bitfield = msg->v.bitfield.bitfield;
      if (pg_array_count(bitfield) != expected_length) {
        pg_log_error(peer->logger,
                     "[%s] Invalid bitfield length: expected=%llu got=%llu",
                     peer->addr_s, expected_length, pg_array_count(bitfield));
        return (peer_error_t){.kind = PEK_INVALID_BITFIELD};
      }

      pg_bitarray_setv(&peer->them_have_pieces, bitfield,
                       pg_array_count(bitfield));

      *request_more = true;
      return (peer_error_t){0};
    }
    case PMK_PIECE: {
      assert(peer->in_flight_requests > 0);

      const peer_message_piece_t piece_msg = msg->v.piece;
      const uint32_t block_for_piece = piece_msg.begin / PEER_BLOCK_LENGTH;
      const pg_span_t span =
          (pg_span_t){.data = (char*)piece_msg.data, .len = PEER_BLOCK_LENGTH};

      pg_log_debug(peer->logger,
                   "[%s] piece: begin=%u index=%u len=%llu block_for_piece=%u",
                   peer->addr_s, piece_msg.begin, piece_msg.index,
                   pg_array_count(piece_msg.data), block_for_piece);
      peer_put_block(peer, block_for_piece, span);

      *request_more = true;
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

uint32_t peer_pick_next_piece_to_download(peer_t* peer, bool* found) {
  *found = false;

  if (peer->downloading_piece !=
      UINT32_MAX) {  // Already downloading a piece, and not finished with it
    *found = true;
    return peer->downloading_piece;
  }

  int64_t i = -1;
  bool is_set = false;
  while (pg_bitarray_next(&peer->download->pieces_to_download, &i, &is_set)) {
    if (!is_set) continue;
    const bool them_have = pg_bitarray_get(&peer->them_have_pieces, i);
    if (!them_have) {
      continue;
    }

    const uint32_t piece = (uint32_t)i;
    pg_log_debug(peer->logger,
                 "[%s] pick_next_piece_to_download: found piece %u",
                 peer->addr_s, piece);
    *found = true;
    peer_mark_piece_as_downloading(peer, piece);
    return piece;
  }
  return UINT32_MAX;
}

uint32_t peer_pick_next_block_to_download(peer_t* peer, bool* found) {
  *found = false;

  int64_t i = -1;
  bool is_set = false;
  while (pg_bitarray_next(&peer->blocks_for_piece_to_download, &i, &is_set)) {
    if (is_set) {
      *found = true;
      pg_log_debug(peer->logger,
                   "[%s] peer_pick_next_block_to_download: found block=%u",
                   peer->addr_s, (uint32_t)i);
      return (uint32_t)i;
    }
  }
  pg_log_debug(peer->logger,
               "[%s] peer_pick_next_block_to_download: exhausted search",
               peer->addr_s);
  return UINT32_MAX;
}

peer_error_t peer_send_request(peer_t* peer, uint32_t block_for_piece);

peer_error_t peer_request_more_blocks(peer_t* peer, bool* stop_requesting) {
  if (peer->in_flight_requests >= PEER_MAX_IN_FLIGHT_REQUESTS ||
      peer->them_choked) {
    pg_log_debug(peer->logger,
                 "[%s] request_more_blocks stop: in_flight_requests=%hhu "
                 "downloading_piece=%u them_choked=%d",
                 peer->addr_s, peer->in_flight_requests,
                 peer->downloading_piece, peer->them_choked);

    *stop_requesting = true;
    return (peer_error_t){0};
  }

  bool found = false;
  peer->downloading_piece = peer_pick_next_piece_to_download(peer, &found);

  // Nothing to download anymore
  if (!found) {
    pg_log_debug(peer->logger,
                 "[%s] request_more_blocks no more pieces to download: "
                 "in_flight_requests=%hhu "
                 "downloading_piece=%u them_choked=%d",
                 peer->addr_s, peer->in_flight_requests,
                 peer->downloading_piece, peer->them_choked);
    *stop_requesting = true;
    return (peer_error_t){0};
  }

  while (peer->in_flight_requests < PEER_MAX_IN_FLIGHT_REQUESTS) {
    const uint32_t block = peer_pick_next_block_to_download(peer, &found);

    if (!found) {
      pg_log_debug(
          peer->logger,
          "[%s] peer_request_more_blocks stop: no next block to download",
          peer->addr_s);
      *stop_requesting = true;
      return (peer_error_t){0};
    }

    assert(block < peer_block_count_per_piece(peer, peer->downloading_piece));
    peer_mark_block_as_downloading(peer, block);

    peer_error_t err = peer_send_request(peer, block);
    if (err.kind != PEK_NONE) return err;
  }

  return (peer_error_t){0};
}

void peer_on_idle(uv_idle_t* handle) {
  peer_t* peer = handle->data;

  bool stop_requesting = false;
  peer_error_t err = peer_request_more_blocks(peer, &stop_requesting);
  if (err.kind != PEK_NONE) {
    pg_log_error(peer->logger, "[%s] failed to request more blocks: %d",
                 peer->addr_s, err.kind);
    peer_close(peer);
    return;
  }

  if (stop_requesting) {
    uv_idle_stop(&peer->idle_handle);
  }
}

void peer_on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  peer_t* peer = stream->data;
  pg_log_debug(peer->logger, "[%s] peer_on_read: %ld", peer->addr_s, nread);

  if (nread <= 0) return;  // Nothing to do

  assert(buf != NULL);
  assert(buf->base != NULL);
  assert(buf->len > 0);

  fprintf(stderr, "[D001] ");
  for (uint64_t i = 0; i < MIN(nread, 150); i++)
    fprintf(stderr, "%#x ", (uint8_t)buf->base[i]);
  fprintf(stderr, "\n");

  pg_ring_push_backv(&peer->recv_data, (uint8_t*)buf->base, nread);
  peer->allocator.free(buf->base);

  bool request_more = false;
  while (true) {  // Parse as many messages as available in the recv_data
    peer_message_t msg = {0};
    peer_error_t err = peer_message_parse(peer, &msg);
    if (err.kind == PEK_NEED_MORE) break;
    if (err.kind != PEK_NONE) {
      pg_log_error(peer->logger, "[%s] peer_message_parse failed: %d",
                   peer->addr_s, err.kind);
      peer_close(peer);
      return;
    }

    pg_log_debug(peer->logger, "[%s] msg=%s", peer->addr_s,
                 peer_message_kind_to_string(msg.kind));
    assert(msg.kind != PMK_NONE);

    err = peer_message_handle(peer, &msg, &request_more);
    peer_message_destroy(&msg);

    if (err.kind != PEK_NONE) {
      pg_log_error(peer->logger, "[%s] peer_message_handle failed: %d\n",
                   peer->addr_s, err.kind);
      peer_close(peer);
    }
  }

  if (request_more)
    uv_idle_start(&peer->idle_handle, peer_on_idle);
  else
    uv_idle_stop(&peer->idle_handle);
}

void peer_on_write(uv_write_t* req, int status) {
  peer_t* peer = req->data;
  // TODO: free bufs

  if (req->bufs != NULL && req->bufs[0].base != NULL)
    peer->allocator.free(req->bufs[0].base);

  peer->allocator.free(req);

  if (status != 0) {
    pg_log_error(peer->logger, "[%s] on_write failed: %d %s", peer->addr_s,
                 -status, strerror(-status));
    peer_close(peer);
  }
}

peer_error_t peer_send_buf(peer_t* peer, uv_buf_t* buf) {
  uv_write_t* write_req = peer->allocator.realloc(sizeof(uv_write_t), NULL, 0);
  write_req->data = peer;

  int ret = 0;
  if ((ret = uv_write(write_req, (uv_stream_t*)&peer->connection, buf, 1,
                      peer_on_write)) != 0) {
    pg_log_error(peer->logger, "[%s] uv_write failed: %d", peer->addr_s, ret);

    peer->allocator.free(write_req);

    return (peer_error_t){.kind = PEK_UV, .v = {-ret}};
  }

  return (peer_error_t){0};
}

peer_error_t peer_send_heartbeat(peer_t* peer) {
  uv_buf_t* buf = peer->allocator.realloc(sizeof(uv_buf_t), NULL, 0);
  buf->base = peer->allocator.realloc(sizeof(uint32_t), NULL, 0);
  buf->len = sizeof(uint32_t);
  return peer_send_buf(peer, buf);
}

peer_error_t peer_send_handshake(peer_t* peer) {
  uv_buf_t* buf = peer->allocator.realloc(sizeof(uv_buf_t), NULL, 0);
  buf->base = peer->allocator.realloc(PEER_HANDSHAKE_LENGTH, NULL, 0);
  buf->len = PEER_HANDSHAKE_LENGTH;

  const uint8_t handshake_header[] = {PEER_HANDSHAKE_HEADER_LENGTH,
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
                                      0};
  memcpy(buf->base, handshake_header, sizeof(handshake_header));
  memcpy(buf->base + sizeof(handshake_header), peer->download->info_hash,
         sizeof(peer->download->info_hash));
  memcpy(
      buf->base + sizeof(handshake_header) + sizeof(peer->download->info_hash),
      peer->download->peer_id, sizeof(peer->download->peer_id));

  return peer_send_buf(peer, buf);
}

uint8_t* peer_write_u32(uint8_t* buf, uint64_t* buf_len, uint32_t x) {
  *(uint32_t*)buf = htonl(x);
  *buf_len += 4;
  return buf + 4;
}

uint8_t* peer_write_u8(uint8_t* buf, uint64_t* buf_len, uint8_t x) {
  buf[0] = x;
  *buf_len += 1;
  return buf + 1;
}

peer_error_t peer_send_request(peer_t* peer, uint32_t block_for_piece) {
  uv_buf_t* buf = peer->allocator.realloc(sizeof(uv_buf_t), NULL, 0);
  buf->base = peer->allocator.realloc(4 + 1 + 3 * 4, NULL, 0);

  uint8_t* bytes = (uint8_t*)buf->base;
  bytes = peer_write_u32(bytes, (uint64_t*)&buf->len, 1 + 3 * 4);
  bytes = peer_write_u8(bytes, (uint64_t*)&buf->len, PT_REQUEST);

  const uint32_t begin = block_for_piece * PEER_BLOCK_LENGTH;
  uint32_t length = 0;
  if (peer_is_last_piece(peer, peer->downloading_piece)) {
    if (block_for_piece <
        peer_block_count_per_piece(peer, peer->downloading_piece) - 1) {
      length = PEER_BLOCK_LENGTH;
    } else {
      // Last block of last piece
      length = peer->metainfo->length -
               (peer->downloading_piece * peer->metainfo->piece_length + begin);
    }
  } else {
    length = PEER_BLOCK_LENGTH;
  }

  bytes = peer_write_u32(bytes, (uint64_t*)&buf->len, peer->downloading_piece);
  bytes = peer_write_u32(bytes, (uint64_t*)&buf->len, begin);
  bytes = peer_write_u32(bytes, (uint64_t*)&buf->len, length);

  pg_log_debug(
      peer->logger,
      "[%s] Sent Request: index=%u begin=%u length=%u in_flight_requests=%u",
      peer->addr_s, peer->downloading_piece, begin, length,
      peer->in_flight_requests);
  return peer_send_buf(peer, buf);
}
peer_error_t peer_send_choke(peer_t* peer) {
  uv_buf_t* buf = peer->allocator.realloc(sizeof(uv_buf_t), NULL, 0);
  buf->base = peer->allocator.realloc(4 + 1, NULL, 0);

  uint8_t* bytes = (uint8_t*)buf->base;
  bytes = peer_write_u32(bytes, (uint64_t*)&buf->len, 1);
  bytes = peer_write_u8(bytes, (uint64_t*)&buf->len, PT_CHOKE);

  return peer_send_buf(peer, buf);
}

peer_error_t peer_send_interested(peer_t* peer) {
  uv_buf_t* buf = peer->allocator.realloc(sizeof(uv_buf_t), NULL, 0);
  buf->base = peer->allocator.realloc(4 + 1, NULL, 0);

  uint8_t* bytes = (uint8_t*)buf->base;
  bytes = peer_write_u32(bytes, (uint64_t*)&buf->len, 1);
  bytes = peer_write_u8(bytes, (uint64_t*)&buf->len, PT_INTERESTED);

  return peer_send_buf(peer, buf);
}

peer_error_t peer_send_prologue(peer_t* peer) {
  peer_error_t err = {0};
  err = peer_send_handshake(peer);
  if (err.kind != PEK_NONE) return err;

  err = peer_send_choke(peer);
  if (err.kind != PEK_NONE) return err;

  err = peer_send_interested(peer);
  if (err.kind != PEK_NONE) return err;

  return err;
}

void peer_on_connect(uv_connect_t* handle, int status) {
  peer_t* peer = handle->data;
  assert(peer != NULL);

  if (status != 0) {
    pg_log_error(peer->logger, "[%s] on_connect: %d %s", peer->addr_s, -status,
                 strerror(-status));
    peer_close(peer);
    return;
  }
  pg_log_info(peer->logger, "[%s] Connected", peer->addr_s);

  int ret = 0;
  if ((ret = uv_read_start((uv_stream_t*)&peer->connection, peer_alloc,
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

peer_t* peer_make(pg_allocator_t allocator, pg_logger_t* logger,
                  download_t* download, bc_metainfo_t* metainfo,
                  tracker_peer_address_t address) {
  peer_t* peer = allocator.realloc(sizeof(peer_t), NULL, 0);
  peer->allocator = allocator;
  peer->logger = logger;
  peer->download = download;
  peer->metainfo = metainfo;
  pg_bitarray_init(allocator, &peer->them_have_pieces,
                   peer->download->pieces_count - 1);
  pg_bitarray_init(allocator, &peer->blocks_for_piece_downloaded,
                   peer->download->blocks_per_piece - 1);
  pg_bitarray_init(allocator, &peer->blocks_for_piece_downloading,
                   peer->download->blocks_per_piece - 1);
  pg_bitarray_init(allocator, &peer->blocks_for_piece_to_download,
                   peer->download->blocks_per_piece - 1);
  peer->downloading_piece = -1;
  peer->connect_req.data = peer;
  peer->connection.data = peer;
  peer->idle_handle.data = peer;
  pg_ring_init(allocator, &peer->recv_data, /* arbitrary */ 512);

  snprintf(peer->addr_s, sizeof(peer->addr_s), "%s:%hu",
           inet_ntoa(*(struct in_addr*)&address.ip), htons(address.port));

  peer->them_choked = true;
  peer->them_interested = false;

  return peer;
}

peer_error_t peer_connect(peer_t* peer, tracker_peer_address_t address) {
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
                            (struct sockaddr*)&addr, peer_on_connect)) != 0) {
    pg_log_error(peer->logger, "[%s] Failed to uv_tcp_connect: %d %s",
                 peer->addr_s, ret, strerror(ret));
    return (peer_error_t){.kind = PEK_UV, .v = {.uv_err = -ret}};
  }
  return (peer_error_t){.kind = PEK_NONE};
}

void peer_destroy(peer_t* peer) {
  pg_bitarray_destroy(&peer->them_have_pieces);
  pg_bitarray_destroy(&peer->blocks_for_piece_downloaded);
  pg_bitarray_destroy(&peer->blocks_for_piece_downloading);
  pg_bitarray_destroy(&peer->blocks_for_piece_to_download);
  pg_ring_destroy(&peer->recv_data);
  peer->allocator.free(peer);
}

void peer_on_close(uv_handle_t* handle) {
  peer_t* peer = handle->data;
  assert(peer != NULL);

  pg_log_debug(peer->logger, "[%s] Closing peer", peer->addr_s);

  peer_mark_piece_as_to_download(peer, peer->downloading_piece);

  uv_idle_stop(&peer->idle_handle);

  peer_destroy(peer);
}

void peer_close(peer_t* peer) {
  // `peer_close` is thus idempotent
  if (!uv_is_closing((uv_handle_t*)&peer->connection)) {
    uv_tcp_close_reset(&peer->connection, peer_on_close);
  }
}

void download_init(pg_allocator_t allocator, download_t* download,
                   bc_metainfo_t* metainfo, uint8_t* info_hash,
                   uint8_t* peer_id, int fd) {
  assert(fd >= 0);

  download->fd = fd;
  download->pieces_count = pg_array_count(metainfo->pieces) / 20;

  download->blocks_per_piece = metainfo->piece_length / PEER_BLOCK_LENGTH;
  download->last_piece_length =
      metainfo->length - (download->pieces_count - 1) * metainfo->piece_length;
  download->last_piece_block_count =
      (uint64_t)ceil((double)download->last_piece_length / PEER_BLOCK_LENGTH);
  memcpy(download->info_hash, info_hash, 20);
  memcpy(download->peer_id, peer_id, 20);

  pg_bitarray_init(allocator, &download->pieces_downloaded,
                   download->pieces_count - 1);
  pg_bitarray_init(allocator, &download->pieces_downloading,
                   download->pieces_count - 1);
  pg_bitarray_init(allocator, &download->pieces_to_download,
                   download->pieces_count - 1);
  pg_bitarray_set_all(&download->pieces_to_download);
}

void download_destroy(download_t* download) {
  pg_bitarray_destroy(&download->pieces_downloaded);
  pg_bitarray_destroy(&download->pieces_downloading);
  pg_bitarray_destroy(&download->pieces_to_download);
}
