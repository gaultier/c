#pragma once

#include <_types/_uint32_t.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <stdint.h>

#include "../pg/pg.h"
#include "bencode.h"

typedef enum {
  TK_ERR_NONE,
  TK_ERR_CURL,
  TK_ERR_BENCODE_PARSE,
  TK_ERR_INVALID_PEERS,
} tracker_error_t;

const char* tracker_error_to_string(int err) {
  switch (err) {
    case TK_ERR_NONE:
      return "TK_ERR_NONE";
    case TK_ERR_CURL:
      return "TK_ERR_CURL";
    case TK_ERR_BENCODE_PARSE:
      return "TK_ERR_BENCODE_PARSE";
    case TK_ERR_INVALID_PEERS:
      return "TK_ERR_INVALID_PEERS";
    default:
      __builtin_unreachable();
  }
}

typedef struct {
  pg_span32_t url;
  uint8_t peer_id[20];
  uint16_t port;
  uint8_t info_hash[20];
  uint64_t uploaded;
  uint64_t downloaded;
  uint64_t left;
} tracker_query_t;

typedef struct {
  uint32_t ip;
  uint16_t port;
} tracker_peer_address_t;

tracker_error_t tracker_parse_peer_addresses(
    bc_parser_t* parser, pg_array_t(tracker_peer_address_t) * peer_addresses) {
  if (pg_array_count(parser->kinds) == 0) return TK_ERR_INVALID_PEERS;
  if (parser->kinds[0] != BC_KIND_DICTIONARY) return TK_ERR_INVALID_PEERS;

  uint32_t cur = 1;
  const pg_span32_t peers_key = pg_span32_make_c("peers");
  const uint32_t root_len = parser->lengths[0];

  for (uint32_t i = 0; i < root_len; i += 2) {
    bc_kind_t key_kind = parser->kinds[cur + i];
    pg_span32_t key_span = parser->spans[cur + i];
    bc_kind_t value_kind = parser->kinds[cur + i + 1];
    pg_span32_t value_span = parser->spans[cur + i + 1];

    if (key_kind == BC_KIND_STRING && pg_span32_eq(peers_key, key_span) &&
        value_kind == BC_KIND_STRING) {
      if (value_span.len % 6 != 0) return TK_ERR_INVALID_PEERS;

      for (uint64_t j = 0; j < value_span.len; j += 6) {
        tracker_peer_address_t addr = {
            .ip = *(uint32_t*)(&value_span.data[j]),
            .port = *(uint16_t*)(&value_span.data[j + 4]),
        };
        pg_array_append(*peer_addresses, addr);
      }
    }
  }

  return TK_ERR_NONE;
}

pg_string_t tracker_build_url_from_query(pg_allocator_t allocator,
                                         tracker_query_t* q) {
  pg_span32_t info_hash_span =
      (pg_span32_t){.data = (char*)q->info_hash, .len = sizeof(q->info_hash)};
  pg_string_t info_hash_url_encoded =
      pg_span32_url_encode(allocator, info_hash_span);

  pg_span32_t peer_id_span =
      (pg_span32_t){.data = (char*)q->peer_id, .len = sizeof(q->peer_id)};
  pg_string_t peer_id_url_encoded =
      pg_span32_url_encode(allocator, peer_id_span);

  pg_string_t res = pg_string_make_reserve(allocator, 5000);
  assert(q->url.len < 4196);
  snprintf(
      res, pg_string_capacity(res),
      "%.*s?info_hash=%s&peer_id=%s&port=%hu&uploaded=%llu&downloaded=%llu&"
      "left=%llu",
      (int)q->url.len, q->url.data, info_hash_url_encoded, peer_id_url_encoded,
      q->port, q->uploaded, q->downloaded, q->left);
  return res;
}

uint64_t tracker_on_response_chunk(void* ptr, uint64_t size, uint64_t nmemb,
                                   void* user_data) {
  const uint64_t ptr_len = size * nmemb;
  pg_array_t(char)* response = user_data;

  const uint64_t new_len = pg_array_count(*response) + ptr_len;
  if (new_len > UINT16_MAX) return 0;

  pg_array_grow(*response, new_len);
  assert(pg_array_capacity(*response) >= ptr_len);

  memcpy(*response + pg_array_count(*response), ptr, ptr_len);
  pg_array_resize(*response, new_len);

  return new_len;
}

tracker_error_t tracker_fetch_peers(pg_allocator_t allocator,
                                    tracker_query_t* q,
                                    pg_array_t(tracker_peer_address_t) *
                                        peer_addresses) {
  tracker_error_t err = TK_ERR_NONE;

  pg_string_t url = tracker_build_url_from_query(allocator, q);

  CURL* curl = curl_easy_init();
  assert(curl != NULL);

  assert(curl_easy_setopt(curl, CURLOPT_URL, url) == 0);
  assert(curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10) == 0);

  pg_array_t(char) response = {0};
  pg_array_init_reserve(response, 512, allocator);
  assert(curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response) == 0);
  assert(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                          tracker_on_response_chunk) == 0);

  int ret = curl_easy_perform(curl);
  if (ret != CURLE_OK) {
    fprintf(stderr, "Failed to contact tracker: url=%s err=%s\n", url,
            curl_easy_strerror(ret));
    err = TK_ERR_CURL;
    goto end;
  }

  pg_span32_t response_span = {.data = response,
                               .len = pg_array_count(response)};

  bc_parser_t parser = {0};
  bc_parser_init(pg_heap_allocator(), &parser, 100);
  bc_parse_error_t parse_err = bc_parse(&parser, &response_span);
  if (parse_err != BC_PE_NONE) {
    err = TK_ERR_BENCODE_PARSE;
    goto end;
  }

  if ((err = tracker_parse_peer_addresses(&parser, peer_addresses)) !=
      TK_ERR_NONE)
    goto end;

end:
  pg_array_free(response);
  curl_easy_cleanup(curl);
  pg_string_free(url);
  return err;
}
