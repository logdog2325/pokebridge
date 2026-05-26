#include "sha1.h"
#include <string.h>

static inline uint32_t rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void sha1_compress(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    /* Load 16 big-endian u32 words from the block. */
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4 + 0] << 24)
             | ((uint32_t)block[i * 4 + 1] << 16)
             | ((uint32_t)block[i * 4 + 2] <<  8)
             | ((uint32_t)block[i * 4 + 3]      );
    }
    /* Extend to 80 words. */
    for (int i = 16; i < 80; i++) {
        w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)       { f = (b & c) | ((~b) & d);        k = 0x5A827999; }
        else if (i < 40)  { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60)  { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else              { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        uint32_t t = rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void pb_sha1_init(pb_sha1_t *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->bitlen_hi = 0;
    ctx->bitlen_lo = 0;
    ctx->buflen = 0;
}

void pb_sha1_update(pb_sha1_t *ctx, const uint8_t *data, size_t len) {
    /* Bookkeep bit count first. */
    uint32_t add = (uint32_t)(len << 3);
    ctx->bitlen_lo += add;
    if (ctx->bitlen_lo < add) ctx->bitlen_hi += 1;
    ctx->bitlen_hi += (uint32_t)(len >> 29);

    while (len > 0) {
        int avail = 64 - ctx->buflen;
        int take = (int)(len < (size_t)avail ? len : (size_t)avail);
        memcpy(ctx->buf + ctx->buflen, data, (size_t)take);
        ctx->buflen += take;
        data += take;
        len  -= (size_t)take;
        if (ctx->buflen == 64) {
            sha1_compress(ctx->state, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

void pb_sha1_final(pb_sha1_t *ctx, uint8_t digest[20]) {
    /* Append 0x80, then pad with zeros until length mod 64 == 56,
     * then append 8-byte big-endian bit length. */
    ctx->buf[ctx->buflen++] = 0x80;
    if (ctx->buflen > 56) {
        while (ctx->buflen < 64) ctx->buf[ctx->buflen++] = 0;
        sha1_compress(ctx->state, ctx->buf);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 56) ctx->buf[ctx->buflen++] = 0;
    /* Bit count: bitlen_hi then bitlen_lo, big-endian. */
    ctx->buf[56] = (uint8_t)(ctx->bitlen_hi >> 24);
    ctx->buf[57] = (uint8_t)(ctx->bitlen_hi >> 16);
    ctx->buf[58] = (uint8_t)(ctx->bitlen_hi >>  8);
    ctx->buf[59] = (uint8_t)(ctx->bitlen_hi      );
    ctx->buf[60] = (uint8_t)(ctx->bitlen_lo >> 24);
    ctx->buf[61] = (uint8_t)(ctx->bitlen_lo >> 16);
    ctx->buf[62] = (uint8_t)(ctx->bitlen_lo >>  8);
    ctx->buf[63] = (uint8_t)(ctx->bitlen_lo      );
    sha1_compress(ctx->state, ctx->buf);
    /* Output digest big-endian. */
    for (int i = 0; i < 5; i++) {
        digest[i * 4 + 0] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >>  8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]      );
    }
}

void pb_sha1(uint8_t digest[20], const uint8_t *data, size_t len) {
    pb_sha1_t ctx;
    pb_sha1_init(&ctx);
    pb_sha1_update(&ctx, data, len);
    pb_sha1_final(&ctx, digest);
}
