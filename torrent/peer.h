#pragma once

#include <sys/socket.h>
#include <uv.h>

#include "bencode.h"
#include "tracker.h"

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
  bc_metainfo_t* metainfo;
  bool me_choked, me_interested, them_choked, them_interested, handshaked;
  uint8_t in_flight_requests;

  uv_tcp_t connection;
  uv_connect_t connect_req;

  char addr[14];
} peer_t;

void peer_on_connect(uv_connect_t* handle, int status) {
  peer_t* peer = handle->data;
  assert(peer != NULL);

  if (status != 0) {
    fprintf(stderr, "[%s] on_connect: %d %s\n", peer->addr, status,
            strerror(status));
    // TODO remove_peer_connection
    return;
  }
  fprintf(stderr, "[%s] Connected\n", peer->addr);
}

peer_t* peer_make(pg_allocator_t allocator, bc_metainfo_t* metainfo) {
  peer_t* peer = allocator.realloc(sizeof(peer_t), NULL, 0);
  peer->metainfo = metainfo;
  peer->connect_req.data = peer;
  peer->connection.data = peer;
  return peer;
}

peer_error_t peer_connect(peer_t* peer, const tracker_peer_address_t* address) {
  int ret = 0;
  if ((ret = uv_tcp_init(uv_default_loop(), &peer->connection)) != 0) {
    fprintf(stderr, "[%s] Failed to uv_tcp_init: %d %s\n", peer->addr, ret,
            strerror(ret));
    return (peer_error_t){.kind = PEK_UV, .v = {.uv_err = ret}};
  }

  struct sockaddr_in addr = {
      .sin_port = address->port,
      .sin_family = AF_INET,
      .sin_addr = {.s_addr = address->ip},
  };

  if ((ret = uv_tcp_connect(&peer->connect_req, &peer->connection,
                            (struct sockaddr*)&addr, peer_on_connect)) != 0) {
    fprintf(stderr, "[%s] Failed to uv_tcp_connect: %d %s\n", peer->addr, ret,
            strerror(ret));
    return (peer_error_t){.kind = PEK_UV, .v = {.uv_err = ret}};
  }
  return (peer_error_t){.kind = PEK_NONE};
}
