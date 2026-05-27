/* Pokémon Colosseum save support.
 *
 * Format reference: PKHeX SAV3Colosseum.cs / ColoCrypto.cs / CK3.cs (GPL-3.0).
 *
 * Container:
 *   - SIZE_G3COLO = 0x60000 bytes (393216)
 *   - 0x00000 - 0x05FFF: outer header / memory card data
 *   - 0x06000 - 0x23FFF: slot 0 (0x1E000 bytes)
 *   - 0x24000 - 0x41FFF: slot 1
 *   - 0x42000 - 0x5FFFF: slot 2
 *
 * Per slot (0x1E000 bytes):
 *   - 0x04 - 0x08: save counter (BIG-ENDIAN u32). Highest = latest slot.
 *   - 0x00 - 0x17: 24-byte plaintext header (game name, counter, etc.)
 *   - 0x18 - 0x1DFEC: encrypted body
 *   - last 0x14 bytes (0x1DFEC..0x1DFFF): stored SHA-1 hash (the at-rest key)
 *
 * Encryption: iterative XOR with SHA-1 hash chain. The stored 20-byte hash is
 * inverted to form the initial digest; for each 20-byte block we hash the
 * encrypted data (next digest), then XOR with the current digest to decrypt.
 *
 * After decryption (offsets relative to slot start):
 *   - 0x0078: OT name (20 bytes, BE 16-bit chars, 10 chars + terminator)
 *   - 0x00A4: SID (BE u16)
 *   - 0x00A6: TID (BE u16)
 *   - 0x00A8: Party (6 × CK3 = 6 × 312 bytes)
 *   - 0x0B90: Box storage start
 *
 * CK3 (Colosseum Pokémon, 312 bytes, BIG-ENDIAN unless noted):
 *   - 0x00: species (XD-internal -> natdex via converter)
 *   - 0x04: PID (u32)
 *   - 0x08: GBA version origin
 *   - 0x14: ID32 (u32: SID16 + TID16)
 *   - 0x18-0x2D: OT name (22 bytes)
 *   - 0x44-0x59: nickname (22 bytes)
 *   - 0x5C: experience (u32)
 *   - 0x60: level
 *   - 0x78-0x87: 4 moves (u16 + u8 PP + u8 PPUps each)
 *   - 0x88: held item (u16)
 *   - 0x98-0xA2: 6 EVs (each as low byte of BE u16)
 *   - 0xA4-0xAE: 6 IVs (each as low byte of BE u16; HP, Atk, Def, SpA, SpD, Spe)
 *   - 0xB0: friendship (low byte of BE u16)
 */
#ifndef POKEBRIDGE_COLO_H
#define POKEBRIDGE_COLO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PB_COLO_SAVE_SIZE      0x60000
#define PB_COLO_SLOT_START     0x6000
#define PB_COLO_SLOT_SIZE      0x1E000
#define PB_COLO_SLOT_COUNT     3
#define PB_COLO_ENC_START      0x18
#define PB_COLO_HASH_BYTES     20
#define PB_COLO_TRAINER_OFF    0x0078
#define PB_COLO_PARTY_OFF      0x00A8
#define PB_COLO_BOX_OFF        0x0B90
#define PB_COLO_PKM_SIZE       312
#define PB_COLO_BOX_COUNT      3
#define PB_COLO_BOX_SIZE       30
#define PB_COLO_BOX_INFO_BYTES 0x14      /* 20 bytes per-box wallpaper/name header */
#define PB_COLO_BOX_STRIDE     (PB_COLO_BOX_SIZE * PB_COLO_PKM_SIZE + PB_COLO_BOX_INFO_BYTES)

typedef struct {
    uint8_t  body[PB_COLO_SAVE_SIZE];
    int      active_slot;        /* 0..2 */
    uint32_t save_count;
    uint8_t *slot;               /* points into body */
    char     trainer_name[32];
    uint16_t trainer_id;
    uint16_t secret_id;
    uint8_t  party_count;
} pb_colo_save_t;

bool pb_colo_load(pb_colo_save_t *out, const uint8_t *data, size_t len);

/* Decoded CK3 (Colosseum) Pokémon. */
typedef struct {
    uint16_t species_internal;
    uint16_t species_natdex;
    uint16_t held_item;
    uint32_t exp;
    uint16_t trainer_id;
    uint16_t secret_id;
    uint32_t pid;
    uint8_t  level;
    uint8_t  iv[6];                  /* HP, Atk, Def, Spe, SpA, SpD (our order) */
    uint16_t moves[4];
    char     nickname[16];
    char     ot_name[16];
    bool     is_shadow;
    bool     is_empty;
} pb_colo_pkm_t;

void pb_colo_pkm_decode(pb_colo_pkm_t *out, const uint8_t *raw312);

/* Offset of box `index` slot `slot`, relative to the decrypted slot start. */
uint32_t pb_colo_box_slot_offset(const pb_colo_save_t *cs, int box_index, int slot);

/* --- Editor + writeback helpers --- */
#include "pokemon.h"
void pb_ck3_to_pkm(pb_pkm_t *out, const uint8_t *raw312);
void pb_ck3_apply_pkm_edits(const pb_pkm_t *p, uint8_t *raw312);

/* Build a fresh 312-byte CK3 record from scratch with sensible defaults
 * (level 5, Tackle, Poké Ball, no shadow, friendship 70, all IVs 0).
 * Used when the user creates a new Pokémon in an empty Colo party/box
 * slot. Caller is responsible for calling pb_colo_finalize_slot before
 * writing the save back so checksums + crypto match. */
void pb_ck3_create_default(uint8_t raw_out[312], uint16_t species_natdex,
                            const char *trainer_name_gen3,
                            uint16_t tid, uint16_t sid);

/* After in-memory edits, recompute checksums and re-encrypt the slot. */
void pb_colo_finalize_slot(pb_colo_save_t *cs);

/* Inverse of finalize: decrypt the slot in place. Call after writing
 * the encrypted bytes somewhere (memcard / SD) so further in-memory
 * edits operate on plaintext again. */
void pb_colo_redecrypt_slot(pb_colo_save_t *cs);

/* Write the full Colosseum save (after finalize) back to a path. */
bool pb_colo_write_file(pb_colo_save_t *cs, const char *path);

#endif
