#pragma once

#include <stdint.h>
#include <string.h>

#define MBEDTLS_BYTE_0(x) ((uint8_t)((x)&0xff))
#define MBEDTLS_BYTE_1(x) ((uint8_t)(((x) >> 8) & 0xff))
#define MBEDTLS_BYTE_2(x) ((uint8_t)(((x) >> 16) & 0xff))
#define MBEDTLS_BYTE_3(x) ((uint8_t)(((x) >> 24) & 0xff))
#ifndef MBEDTLS_PUT_UINT32_BE
#define MBEDTLS_PUT_UINT32_BE(n, data, offset)                                 \
  {                                                                            \
    (data)[(offset)] = MBEDTLS_BYTE_3(n);                                      \
    (data)[(offset) + 1] = MBEDTLS_BYTE_2(n);                                  \
    (data)[(offset) + 2] = MBEDTLS_BYTE_1(n);                                  \
    (data)[(offset) + 3] = MBEDTLS_BYTE_0(n);                                  \
  }
#endif
typedef struct mbedtls_sha1_context {
  uint32_t total[2];        /*!< The number of Bytes processed.  */
  uint32_t state[5];        /*!< The intermediate digest state.  */
  unsigned char buffer[64]; /*!< The data block being processed. */
}

mbedtls_sha1_context;
__attribute__((unused)) static void
mbedtls_sha1_init(mbedtls_sha1_context *ctx) {
  memset(ctx, 0, sizeof(mbedtls_sha1_context));
}

__attribute__((unused)) static void
mbedtls_sha1_free(mbedtls_sha1_context *ctx) {
  if (ctx == NULL)
    return;

  memset(ctx, 0, sizeof(mbedtls_sha1_context));
}

__attribute__((unused)) static void
mbedtls_sha1_clone(mbedtls_sha1_context *dst, const mbedtls_sha1_context *src) {
  *dst = *src;
}

/*
 * SHA-1 context setup
 */
__attribute__((unused)) static int
mbedtls_sha1_starts(mbedtls_sha1_context *ctx) {
  ctx->total[0] = 0;
  ctx->total[1] = 0;

  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xEFCDAB89;
  ctx->state[2] = 0x98BADCFE;
  ctx->state[3] = 0x10325476;
  ctx->state[4] = 0xC3D2E1F0;

  return (0);
}

#ifndef MBEDTLS_GET_UINT32_BE
#define MBEDTLS_GET_UINT32_BE(data, offset)                                    \
  (((uint32_t)(data)[(offset)] << 24) |                                        \
   ((uint32_t)(data)[(offset) + 1] << 16) |                                    \
   ((uint32_t)(data)[(offset) + 2] << 8) | ((uint32_t)(data)[(offset) + 3]))
#endif

__attribute__((unused)) static int
mbedtls_internal_sha1_process(mbedtls_sha1_context *ctx,
                              const unsigned char data[64]) {
  struct {
    uint32_t temp, W[16], A, B, C, D, E;
  } local;

  local.W[0] = MBEDTLS_GET_UINT32_BE(data, 0);
  local.W[1] = MBEDTLS_GET_UINT32_BE(data, 4);
  local.W[2] = MBEDTLS_GET_UINT32_BE(data, 8);
  local.W[3] = MBEDTLS_GET_UINT32_BE(data, 12);
  local.W[4] = MBEDTLS_GET_UINT32_BE(data, 16);
  local.W[5] = MBEDTLS_GET_UINT32_BE(data, 20);
  local.W[6] = MBEDTLS_GET_UINT32_BE(data, 24);
  local.W[7] = MBEDTLS_GET_UINT32_BE(data, 28);
  local.W[8] = MBEDTLS_GET_UINT32_BE(data, 32);
  local.W[9] = MBEDTLS_GET_UINT32_BE(data, 36);
  local.W[10] = MBEDTLS_GET_UINT32_BE(data, 40);
  local.W[11] = MBEDTLS_GET_UINT32_BE(data, 44);
  local.W[12] = MBEDTLS_GET_UINT32_BE(data, 48);
  local.W[13] = MBEDTLS_GET_UINT32_BE(data, 52);
  local.W[14] = MBEDTLS_GET_UINT32_BE(data, 56);
  local.W[15] = MBEDTLS_GET_UINT32_BE(data, 60);

#define S(x, n) (((x) << (n)) | (((x)&0xFFFFFFFF) >> (32 - (n))))

#define R(t)                                                                   \
  (local.temp = local.W[((t)-3) & 0x0F] ^ local.W[((t)-8) & 0x0F] ^            \
                local.W[((t)-14) & 0x0F] ^ local.W[(t)&0x0F],                  \
   (local.W[(t)&0x0F] = S(local.temp, 1)))

#define P(a, b, c, d, e, x)                                                    \
  do {                                                                         \
    (e) += S((a), 5) + F((b), (c), (d)) + K + (x);                             \
    (b) = S((b), 30);                                                          \
  } while (0)

  local.A = ctx->state[0];
  local.B = ctx->state[1];
  local.C = ctx->state[2];
  local.D = ctx->state[3];
  local.E = ctx->state[4];

#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define K 0x5A827999

  P(local.A, local.B, local.C, local.D, local.E, local.W[0]);
  P(local.E, local.A, local.B, local.C, local.D, local.W[1]);
  P(local.D, local.E, local.A, local.B, local.C, local.W[2]);
  P(local.C, local.D, local.E, local.A, local.B, local.W[3]);
  P(local.B, local.C, local.D, local.E, local.A, local.W[4]);
  P(local.A, local.B, local.C, local.D, local.E, local.W[5]);
  P(local.E, local.A, local.B, local.C, local.D, local.W[6]);
  P(local.D, local.E, local.A, local.B, local.C, local.W[7]);
  P(local.C, local.D, local.E, local.A, local.B, local.W[8]);
  P(local.B, local.C, local.D, local.E, local.A, local.W[9]);
  P(local.A, local.B, local.C, local.D, local.E, local.W[10]);
  P(local.E, local.A, local.B, local.C, local.D, local.W[11]);
  P(local.D, local.E, local.A, local.B, local.C, local.W[12]);
  P(local.C, local.D, local.E, local.A, local.B, local.W[13]);
  P(local.B, local.C, local.D, local.E, local.A, local.W[14]);
  P(local.A, local.B, local.C, local.D, local.E, local.W[15]);
  P(local.E, local.A, local.B, local.C, local.D, R(16));
  P(local.D, local.E, local.A, local.B, local.C, R(17));
  P(local.C, local.D, local.E, local.A, local.B, R(18));
  P(local.B, local.C, local.D, local.E, local.A, R(19));

#undef K
#undef F

#define F(x, y, z) ((x) ^ (y) ^ (z))
#define K 0x6ED9EBA1

  P(local.A, local.B, local.C, local.D, local.E, R(20));
  P(local.E, local.A, local.B, local.C, local.D, R(21));
  P(local.D, local.E, local.A, local.B, local.C, R(22));
  P(local.C, local.D, local.E, local.A, local.B, R(23));
  P(local.B, local.C, local.D, local.E, local.A, R(24));
  P(local.A, local.B, local.C, local.D, local.E, R(25));
  P(local.E, local.A, local.B, local.C, local.D, R(26));
  P(local.D, local.E, local.A, local.B, local.C, R(27));
  P(local.C, local.D, local.E, local.A, local.B, R(28));
  P(local.B, local.C, local.D, local.E, local.A, R(29));
  P(local.A, local.B, local.C, local.D, local.E, R(30));
  P(local.E, local.A, local.B, local.C, local.D, R(31));
  P(local.D, local.E, local.A, local.B, local.C, R(32));
  P(local.C, local.D, local.E, local.A, local.B, R(33));
  P(local.B, local.C, local.D, local.E, local.A, R(34));
  P(local.A, local.B, local.C, local.D, local.E, R(35));
  P(local.E, local.A, local.B, local.C, local.D, R(36));
  P(local.D, local.E, local.A, local.B, local.C, R(37));
  P(local.C, local.D, local.E, local.A, local.B, R(38));
  P(local.B, local.C, local.D, local.E, local.A, R(39));

#undef K
#undef F

#define F(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define K 0x8F1BBCDC

  P(local.A, local.B, local.C, local.D, local.E, R(40));
  P(local.E, local.A, local.B, local.C, local.D, R(41));
  P(local.D, local.E, local.A, local.B, local.C, R(42));
  P(local.C, local.D, local.E, local.A, local.B, R(43));
  P(local.B, local.C, local.D, local.E, local.A, R(44));
  P(local.A, local.B, local.C, local.D, local.E, R(45));
  P(local.E, local.A, local.B, local.C, local.D, R(46));
  P(local.D, local.E, local.A, local.B, local.C, R(47));
  P(local.C, local.D, local.E, local.A, local.B, R(48));
  P(local.B, local.C, local.D, local.E, local.A, R(49));
  P(local.A, local.B, local.C, local.D, local.E, R(50));
  P(local.E, local.A, local.B, local.C, local.D, R(51));
  P(local.D, local.E, local.A, local.B, local.C, R(52));
  P(local.C, local.D, local.E, local.A, local.B, R(53));
  P(local.B, local.C, local.D, local.E, local.A, R(54));
  P(local.A, local.B, local.C, local.D, local.E, R(55));
  P(local.E, local.A, local.B, local.C, local.D, R(56));
  P(local.D, local.E, local.A, local.B, local.C, R(57));
  P(local.C, local.D, local.E, local.A, local.B, R(58));
  P(local.B, local.C, local.D, local.E, local.A, R(59));

#undef K
#undef F

#define F(x, y, z) ((x) ^ (y) ^ (z))
#define K 0xCA62C1D6

  P(local.A, local.B, local.C, local.D, local.E, R(60));
  P(local.E, local.A, local.B, local.C, local.D, R(61));
  P(local.D, local.E, local.A, local.B, local.C, R(62));
  P(local.C, local.D, local.E, local.A, local.B, R(63));
  P(local.B, local.C, local.D, local.E, local.A, R(64));
  P(local.A, local.B, local.C, local.D, local.E, R(65));
  P(local.E, local.A, local.B, local.C, local.D, R(66));
  P(local.D, local.E, local.A, local.B, local.C, R(67));
  P(local.C, local.D, local.E, local.A, local.B, R(68));
  P(local.B, local.C, local.D, local.E, local.A, R(69));
  P(local.A, local.B, local.C, local.D, local.E, R(70));
  P(local.E, local.A, local.B, local.C, local.D, R(71));
  P(local.D, local.E, local.A, local.B, local.C, R(72));
  P(local.C, local.D, local.E, local.A, local.B, R(73));
  P(local.B, local.C, local.D, local.E, local.A, R(74));
  P(local.A, local.B, local.C, local.D, local.E, R(75));
  P(local.E, local.A, local.B, local.C, local.D, R(76));
  P(local.D, local.E, local.A, local.B, local.C, R(77));
  P(local.C, local.D, local.E, local.A, local.B, R(78));
  P(local.B, local.C, local.D, local.E, local.A, R(79));

#undef K
#undef F

  ctx->state[0] += local.A;
  ctx->state[1] += local.B;
  ctx->state[2] += local.C;
  ctx->state[3] += local.D;
  ctx->state[4] += local.E;

  /* Zeroise buffers and variables to clear sensitive data from memory. */
  memset(&local, 0, sizeof(local));

  return (0);
}

/*
 * SHA-1 process buffer
 */
#define MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED -0x006E

__attribute__((unused)) static int
mbedtls_sha1_update(mbedtls_sha1_context *ctx, const unsigned char *input,
                    size_t ilen) {
  int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
  size_t fill;
  uint32_t left;

  if (ilen == 0)
    return (0);

  left = ctx->total[0] & 0x3F;
  fill = 64 - left;

  ctx->total[0] += (uint32_t)ilen;
  ctx->total[0] &= 0xFFFFFFFF;

  if (ctx->total[0] < (uint32_t)ilen)
    ctx->total[1]++;

  if (left && ilen >= fill) {
    memcpy((void *)(ctx->buffer + left), input, fill);

    if ((ret = mbedtls_internal_sha1_process(ctx, ctx->buffer)) != 0)
      return (ret);

    input += fill;
    ilen -= fill;
    left = 0;
  }

  while (ilen >= 64) {
    if ((ret = mbedtls_internal_sha1_process(ctx, input)) != 0)
      return (ret);

    input += 64;
    ilen -= 64;
  }

  if (ilen > 0)
    memcpy((void *)(ctx->buffer + left), input, ilen);

  return (0);
}

/*
 * SHA-1 final digest
 */

__attribute__((unused)) static int
mbedtls_sha1_finish(mbedtls_sha1_context *ctx, unsigned char output[20]) {
  int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
  uint32_t used;
  uint32_t high, low;

  /*
   * Add padding: 0x80 then 0x00 until 8 bytes remain for the length
   */
  used = ctx->total[0] & 0x3F;

  ctx->buffer[used++] = 0x80;

  if (used <= 56) {
    /* Enough room for padding + length in current block */
    memset(ctx->buffer + used, 0, 56 - used);
  } else {
    /* We'll need an extra block */
    memset(ctx->buffer + used, 0, 64 - used);

    if ((ret = mbedtls_internal_sha1_process(ctx, ctx->buffer)) != 0)
      return (ret);

    memset(ctx->buffer, 0, 56);
  }

  /*
   * Add message length
   */
  high = (ctx->total[0] >> 29) | (ctx->total[1] << 3);
  low = (ctx->total[0] << 3);

  MBEDTLS_PUT_UINT32_BE(high, ctx->buffer, 56)
  MBEDTLS_PUT_UINT32_BE(low, ctx->buffer, 60)

  if ((ret = mbedtls_internal_sha1_process(ctx, ctx->buffer)) != 0)
    return (ret);

  /*
   * Output final state
   */
  MBEDTLS_PUT_UINT32_BE(ctx->state[0], output, 0)
  MBEDTLS_PUT_UINT32_BE(ctx->state[1], output, 4)
  MBEDTLS_PUT_UINT32_BE(ctx->state[2], output, 8)
  MBEDTLS_PUT_UINT32_BE(ctx->state[3], output, 12)
  MBEDTLS_PUT_UINT32_BE(ctx->state[4], output, 16)

  return (0);
}

/*
 * output = SHA-1( input buffer )
 */
__attribute__((unused)) static int mbedtls_sha1(const unsigned char *input,
                                                size_t ilen,
                                                unsigned char output[20]) {
  int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
  mbedtls_sha1_context ctx;

  mbedtls_sha1_init(&ctx);

  if ((ret = mbedtls_sha1_starts(&ctx)) != 0)
    goto exit;

  if ((ret = mbedtls_sha1_update(&ctx, input, ilen)) != 0)
    goto exit;

  if ((ret = mbedtls_sha1_finish(&ctx, output)) != 0)
    goto exit;

exit:
  mbedtls_sha1_free(&ctx);

  return (ret);
}
