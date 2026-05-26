/* Little-endian byte-order helpers.
 *
 * Gen 3 Pokémon save data is stored little-endian (it lives on a GBA, which
 * is little-endian ARM). The GameCube CPU is big-endian PowerPC, so a naive
 * `*(uint32_t *)ptr` reads bytes in the wrong order. These inline helpers do
 * the right thing on any host.
 */
#ifndef POKEBRIDGE_ENDIAN_LE_H
#define POKEBRIDGE_ENDIAN_LE_H

#include <stdint.h>

static inline uint16_t pb_rd_u16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t pb_rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static inline void pb_wr_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v       );
    p[1] = (uint8_t)(v >>   8);
}

static inline void pb_wr_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v       );
    p[1] = (uint8_t)(v >>   8);
    p[2] = (uint8_t)(v >>  16);
    p[3] = (uint8_t)(v >>  24);
}

#endif
