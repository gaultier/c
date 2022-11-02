#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <uv.h>

#include "bencode.h"
#include "tracker.h"

#define PEER_HANDSHAKE_LENGTH ((uint64_t)68)

typedef struct {
  uint8_t info_hash[20];
} download_t;

typedef enum {
  PEK_NONE,
  PEK_UV,
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

  char addr_s[INET_ADDRSTRLEN + /* :port */ 6];  // TODO: ipv6
} peer_t;

void peer_close(peer_t* peer);

void peer_alloc(uv_handle_t* handle, size_t nread, uv_buf_t* buf) {
  peer_t* peer = handle->data;
  buf->base = peer->allocator.realloc(nread, NULL, 0);
  buf->len = nread;
}

void peer_on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  peer_t* peer = stream->data;
  pg_log_debug(peer->logger, "[%s] peer_on_read: %ld", peer->addr_s, nread);

  if (nread >= 0 && buf != NULL && buf->base != NULL) {
    peer->allocator.free((void*)buf);
  }
}

peer_error_t peer_send_handshake(peer_t* peer) {
  peer_error_t err = {0};

  uv_buf_t* buf = peer->allocator.realloc(sizeof(uv_buf_t), NULL, 0);
  buf->base = peer->allocator.realloc(PEER_HANDSHAKE_LENGTH, NULL, 0);
  buf->len = PEER_HANDSHAKE_LENGTH;

  const uint8_t handshake_header[PEER_HANDSHAKE_LENGTH] = {
      19,  'B', 'i', 't', 'T', 'o', 'r', 'r', 'e', 'n', 't', ' ', 'p', 'r',
      'o', 't', 'o', 'c', 'o', 'l', 0,   0,   0,   0,   0,   0,   0,   0};
  memcpy(buf->base, handshake_header, sizeof(handshake_header));
  memcpy(buf->base + sizeof(handshake_header), peer->download->info_hash,
         sizeof(peer->download->info_hash));

  return err;
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
