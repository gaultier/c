#pragma once

#include <uv.h>

#include "bencode.h"
#include "tracker.h"

typedef struct {
  bc_metainfo_t* metainfo;
  bool me_choked, me_interested, them_choked, them_interested, handshaked;
  uint8_t in_flight_requests;

  uv_tcp_t connection;
  uv_connect_t connect_req;

  char addr[14];
} peer_t;

peer_t* peer_make(pg_allocator_t allocator, tracker_peer_address_t* address,
                  bc_metainfo_t* metainfo) {
  peer_t* peer = allocator.realloc(sizeof(peer_t), NULL, 0);
  peer->metainfo = metainfo;
  return peer;
}

void peer_connect(pg_allocator_t allocator, tracker_peer_address_t* address,
                  bc_metainfo_t* metainfo) {
  peer_t* peer = peer_make(allocator, address, metainfo);
}
