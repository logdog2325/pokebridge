#include "genius_crypto.h"

void pb_genius_read_keys(const uint8_t *src, uint16_t keys[4]) {
    for (int i = 0; i < 4; i++) {
        keys[i] = (uint16_t)(((uint16_t)src[i * 2] << 8) | src[i * 2 + 1]);
    }
}

static void advance_keys(uint16_t keys[4]) {
    /* Per PKHeX's GeniusCrypto.AdvanceKeys: bias each key, then rotate 4-bit
     * groups across the diagonal of the 4x4 matrix. */
    int k3 = keys[3] + 0x13;
    int k2 = keys[2] + 0x17;
    int k1 = keys[1] + 0x29;
    int k0 = keys[0] + 0x43;

    keys[3] = (uint16_t)(((k0 >> 12) & 0x000F) | ((k1 >>  8) & 0x00F0) | ((k2 >> 4) & 0x0F00) | ( k3        & 0xF000));
    keys[2] = (uint16_t)(((k0 >>  8) & 0x000F) | ((k1 >>  4) & 0x00F0) | ( k2       & 0x0F00) | ((k3 <<  4) & 0xF000));
    keys[1] = (uint16_t)(((k0 >>  4) & 0x000F) | ( k1        & 0x00F0) | ((k2 <<  4) & 0x0F00) | ((k3 <<  8) & 0xF000));
    keys[0] = (uint16_t)(( k0        & 0x000F) | ((k1 <<  4) & 0x00F0) | ((k2 <<  8) & 0x0F00) | ((k3 << 12) & 0xF000));
}

void pb_genius_decrypt(uint8_t *data, size_t len, uint16_t keys[4]) {
    for (size_t i = 0; i + 8 <= len; i += 8) {
        for (int k = 0; k < 4; k++) {
            size_t off = i + (size_t)k * 2;
            uint16_t v = (uint16_t)(((uint16_t)data[off] << 8) | data[off + 1]);
            v = (uint16_t)(v - keys[k]);
            data[off]     = (uint8_t)(v >> 8);
            data[off + 1] = (uint8_t)(v & 0xFF);
        }
        advance_keys(keys);
    }
}

void pb_genius_encrypt(uint8_t *data, size_t len, uint16_t keys[4]) {
    /* Mirror of decrypt: add the key instead of subtracting. */
    for (size_t i = 0; i + 8 <= len; i += 8) {
        for (int k = 0; k < 4; k++) {
            size_t off = i + (size_t)k * 2;
            uint16_t v = (uint16_t)(((uint16_t)data[off] << 8) | data[off + 1]);
            v = (uint16_t)(v + keys[k]);
            data[off]     = (uint8_t)(v >> 8);
            data[off + 1] = (uint8_t)(v & 0xFF);
        }
        advance_keys(keys);
    }
}
