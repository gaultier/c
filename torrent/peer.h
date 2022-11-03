#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <uv.h>

#include "bencode.h"
#include "tracker.h"

#define PEER_HANDSHAKE_LENGTH ((uint64_t)68)
#define PEER_HANDSHAKE_HEADER_LENGTH ((uint64_t)19)

typedef struct {
  uint8_t info_hash[20];
  uint8_t peer_id[20];
} download_t;

typedef enum {
  PEK_NONE,
  PEK_NEED_MORE,
  PEK_UV,
  PEK_WRONG_HANDSHAKE_HEADER,
  PEK_WRONG_HANDSHAKE_HASH,
} peer_error_kind_t;

typedef struct {
  peer_error_kind_t kind;
  union {
    int uv_err;
  } v;
} peer_error_t;

typedef struct {
  pg_allocator_t allocator;
  pg_logger_t* logger;

  download_t* download;
  bc_metainfo_t* metainfo;
  bool me_choked, me_interested, them_choked, them_interested, handshaked;
  uint8_t in_flight_requests;

  uv_tcp_t connection;
  uv_connect_t connect_req;

  pg_ring_t recv_data;
  char addr_s[INET_ADDRSTRLEN + /* :port */ 6];  // TODO: ipv6
} peer_t;

void peer_close(peer_t* peer);

void peer_alloc(uv_handle_t* handle, size_t nread, uv_buf_t* buf) {
  peer_t* peer = handle->data;
  buf->base = peer->allocator.realloc(nread, NULL, 0);
  buf->len = nread;
}

peer_error_t peer_check_handshaked(peer_t* peer) {
  if (peer->handshaked) return (peer_error_t){0};
  if (pg_ring_len(&peer->recv_data) < PEER_HANDSHAKE_LENGTH)
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

  return (peer_error_t){0};
}

peer_error_t peer_parse_message(peer_t* peer) {
  // TODO
  return peer_check_handshaked(peer);
}

void peer_on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  peer_t* peer = stream->data;
  pg_log_debug(peer->logger, "[%s] peer_on_read: %ld", peer->addr_s, nread);

  if (nread <= 0) return;  // Nothing to do

  assert(buf != NULL);
  assert(buf->base != NULL);
  assert(buf->len > 0);

  pg_ring_push_backv(&peer->recv_data, (uint8_t*)buf->base, buf->len);
  peer->allocator.free(buf->base);

  peer_error_t err = peer_parse_message(peer);
  if (err.kind != PEK_NONE && err.kind != PEK_NEED_MORE) {
    pg_log_error(peer->logger, "[%s] peer_parse_message failed: %d\n",
                 peer->addr_s, err.kind);
    peer_close(peer);
  }
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

peer_error_t peer_send_choke(peer_t* peer) {
  peer_error_t err = {0};
  return err;
}

peer_error_t peer_send_interested(peer_t* peer) {
  peer_error_t err = {0};
  return err;
}

peer_error_t peer_send_prologue(peer_t* peer) {
  peer_error_t err = {0};
  err = peer_send_handshake(peer);
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
}

peer_t* peer_make(pg_allocator_t allocator, pg_logger_t* logger,
                  download_t* download, bc_metainfo_t* metainfo,
                  tracker_peer_address_t address) {
  peer_t* peer = allocator.realloc(sizeof(peer_t), NULL, 0);
  peer->allocator = allocator;
  peer->logger = logger;
  peer->download = download;
  peer->metainfo = metainfo;
  peer->connect_req.data = peer;
  peer->connection.data = peer;
  pg_ring_init(allocator, &peer->recv_data, /* arbitrary */ 512);

  snprintf(peer->addr_s, sizeof(peer->addr_s), "%s:%hu",
           inet_ntoa(*(struct in_addr*)&address.ip), htons(address.port));
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

void peer_destroy(peer_t* peer) { peer->allocator.free(peer); }

void peer_on_close(uv_handle_t* handle) {
  peer_t* peer = handle->data;
  assert(peer != NULL);

  pg_log_debug(peer->logger, "[%s] Closing peer", peer->addr_s);

  peer_destroy(peer);
}

void peer_close(peer_t* peer) {
  // `peer_close` is thus idempotent
  if (!uv_is_closing((uv_handle_t*)&peer->connection)) {
    uv_tcp_close_reset(&peer->connection, peer_on_close);
  }
}
