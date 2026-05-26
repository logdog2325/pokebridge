/* SHA-1 (FIPS PUB 180-4). Used by ColoCrypto for Pokémon Colosseum saves.
 *
 * Self-contained, no external deps. Big-endian output (matches spec). */
#ifndef POKEBRIDGE_SHA1_H
#define POKEBRIDGE_SHA1_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[5];
    uint32_t bitlen_hi;
    uint32_t bitlen_lo;
    uint8_t  buf[64];
    int      buflen;
} pb_sha1_t;

void pb_sha1_init(pb_sha1_t *ctx);
void pb_sha1_update(pb_sha1_t *ctx, const uint8_t *data, size_t len);
void pb_sha1_final(pb_sha1_t *ctx, uint8_t digest[20]);

/* One-shot helper. */
void pb_sha1(uint8_t digest[20], const uint8_t *data, size_t len);

#endif
