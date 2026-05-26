/* Genius Sonority stream cipher used by Pokémon XD save data.
 *
 * Ported from PKHeX.Core/Saves/Encryption/GeniusCrypto.cs (GPL-3.0).
 *
 * Operates on big-endian 16-bit words. Each 8-byte block uses 4 keys (one per
 * word); after each block the key state is advanced via a 4-bit nibble
 * rotation. The decrypt/encrypt operations are paired -- subtract on decrypt,
 * add on encrypt, both modulo 0x10000.
 */
#ifndef POKEBRIDGE_GENIUS_CRYPTO_H
#define POKEBRIDGE_GENIUS_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

/* Decrypt `data` (len bytes, must be multiple of 8) in place using `keys`.
 * Keys are mutated as the stream advances. */
void pb_genius_decrypt(uint8_t *data, size_t len, uint16_t keys[4]);

/* Encrypt `data` in place using `keys`. Inverse of decrypt -- adds the key
 * to each u16 BE word rather than subtracting. */
void pb_genius_encrypt(uint8_t *data, size_t len, uint16_t keys[4]);

/* Read 4 big-endian u16 keys from an 8-byte buffer. */
void pb_genius_read_keys(const uint8_t *src, uint16_t keys[4]);

#endif
