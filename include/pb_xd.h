/* Pokémon XD: Gale of Darkness save support.
 *
 * Format reference: PKHeX SAV3XD.cs / XK3.cs (GPL-3.0). XD saves live as
 * .gci files on a GameCube memory card; some tools wrap them in a 192-byte
 * "DATELGC_SAVE" header for export.
 *
 * Container (file or .gci body):
 *   - SIZE_G3XD = 0x56000 bytes (352,256)
 *   - 0x00000 - 0x05FFF: outer header (game info, language, comment)
 *   - 0x06000 - 0x2DFFF: slot 0 (0x28000 bytes)
 *   - 0x2E000 - 0x55FFF: slot 1
 *
 * Per slot:
 *   - 0x04 - 0x08: save counter (BIG-ENDIAN u32). Highest = latest slot.
 *   - 0x08 - 0x10: 4 big-endian u16 encryption keys
 *   - 0x10 - 0x27FD8: encrypted body (decrypt with GeniusCrypto)
 *
 * After decryption (offsets relative to slot start):
 *   - 0xCCD8: Trainer info block
 *   - 0xCD08: Party (6 × XK3, each 196 bytes)
 *   - 0x10E08: Box storage (8 boxes × 30 slots × 196 bytes + box metadata)
 *
 * Per Pokémon (XK3, 196 bytes, BIG-ENDIAN unless noted):
 *   - 0x00: species (XD-internal ID; needs SpeciesConverter for natdex)
 *   - 0x02: held item
 *   - 0x04: HP current
 *   - 0x06: friendship (1 byte at +1)
 *   - 0x08: met location
 *   - 0x0E: met level
 *   - 0x0F: ball
 *   - 0x11: level
 *   - 0x20: experience (u32)
 *   - 0x24: SID
 *   - 0x26: TID
 *   - 0x28: PID (u32)
 *   - 0x38-0x4D: OT name (22 bytes, big-endian 16-bit chars)
 *   - 0x64-0x79: nickname (22 bytes)
 *   - 0x80-0x8F: 4 moves (u16 + u8 PP + u8 PPUps each)
 *   - 0xA8-0xAD: IVs (6 × 1 byte, just the value 0-31)
 */
#ifndef POKEBRIDGE_XD_H
#define POKEBRIDGE_XD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PB_XD_SAVE_SIZE        0x56000
#define PB_XD_WRAPPER_SIZE     192        /* "DATELGC_SAVE" + GCI metadata    */
#define PB_XD_SLOT_START       0x6000
#define PB_XD_SLOT_SIZE        0x28000
#define PB_XD_SLOT_COUNT       2
#define PB_XD_KEYS_OFFSET      0x08
#define PB_XD_ENC_START        0x10
#define PB_XD_ENC_END          0x27FD8
#define PB_XD_PARTY_OFFSET     0xCD08
#define PB_XD_PARTY_COUNT_OFF  0xCCFC   /* approx; PKHeX uses Trainer1 + ... */
#define PB_XD_PKM_SIZE         196

typedef struct {
    /* Holds the full XD save body (no wrapper). After load, the active slot
     * is decrypted in place inside this buffer. */
    uint8_t  body[PB_XD_SAVE_SIZE];
    int      active_slot;        /* 0 or 1 */
    uint32_t save_count;
    uint8_t *slot;               /* points into body, length PB_XD_SLOT_SIZE */
    /* Trainer info, lifted from the decrypted slot. */
    char     trainer_name[32];
    uint16_t trainer_id;
    uint16_t secret_id;
    uint8_t  party_count;
    /* Runtime-resolved offsets (computed from subOffsets[] in the slot). */
    uint32_t trainer_offset;
    uint32_t party_offset;
    uint32_t box_offset;
} pb_xd_save_t;

/* Loads + decrypts the latest slot of an XD save. Handles a "DATELGC_SAVE"
 * 192-byte wrapper if present. Returns true on success. */
bool pb_xd_load(pb_xd_save_t *out, const uint8_t *data, size_t len);

/* Lightweight XD Pokémon, decoded from the 196-byte XK3 record. */
typedef struct {
    uint16_t species_internal;
    uint16_t species_natdex;    /* converted via XD-internal -> natdex */
    uint16_t held_item;
    uint32_t exp;
    uint16_t trainer_id;
    uint16_t secret_id;
    uint32_t pid;
    uint8_t  level;
    uint8_t  iv[6];             /* HP, Atk, Def, Spe, SpA, SpD (XD-order) */
    uint16_t moves[4];
    char     nickname[16];      /* ASCII conversion of XD's 16-bit chars  */
    char     ot_name[16];
    bool     is_shadow;
    bool     is_empty;
} pb_xd_pkm_t;

void pb_xd_pkm_decode(pb_xd_pkm_t *out, const uint8_t *raw196);

/* Convert an XD-internal species ID to natdex (1..386). Returns 0 if no
 * mapping (XD has gaps where Gen 4+ slots existed). For v1 this is a thin
 * passthrough -- the table is small but data-heavy; we mark unmapped values
 * as 0 so the UI can show the internal ID. */
uint16_t pb_xd_species_to_natdex(uint16_t internal_id);

/* --- Conversion / write helpers for editor interop --- */

/* Decode an XK3 record into the GBA-style pb_pkm_t struct so the existing
 * editor / show-pkm UI can operate on it. Approximation -- only the fields
 * the editor touches are filled accurately. Nickname / OT name slots are
 * left zeroed (XD encoding differs). */
#include "pokemon.h"
void pb_xk3_to_pkm(pb_pkm_t *out, const uint8_t *raw196);

/* After the editor mutates pb_pkm_t, write the editable fields back into the
 * original XK3 record. Preserves all other bytes (nickname, OT name, contest
 * stats, etc.) so we don't corrupt anything we don't understand. */
void pb_xk3_apply_pkm_edits(const pb_pkm_t *p, uint8_t *raw196);

/* Box geometry: 30 mons per box, 8 boxes total in XD. */
#define PB_XD_BOX_COUNT       8
#define PB_XD_BOX_SIZE        30
#define PB_XD_BOX_INFO_BYTES  20   /* per-box wallpaper + name header */
#define PB_XD_BOX_STRIDE      (PB_XD_BOX_SIZE * PB_XD_PKM_SIZE + PB_XD_BOX_INFO_BYTES)

/* Offset of the first Pokémon slot in box `index` (0..7), relative to the
 * decrypted slot start. */
uint32_t pb_xd_box_slot_offset(const pb_xd_save_t *s, int box_index, int slot);

/* Recompute the active slot's header + body checksums and re-encrypt the
 * slot in place. Call this just before writing the save back to disk.
 * Mirrors PKHeX's XDCrypto.SetChecksums + EncryptSlot. */
void pb_xd_finalize_slot(pb_xd_save_t *xs);

/* Inverse of finalize: decrypt the slot in place. Call after writing
 * the encrypted bytes somewhere (memcard / SD) so further in-memory
 * edits operate on plaintext again. */
void pb_xd_redecrypt_slot(pb_xd_save_t *xs);

/* Write the full XD save (after finalize) back to a path. Returns true on
 * success. The file written is the raw 0x56000-byte XD body without the
 * Datel "DATELGC_SAVE" wrapper -- most tools accept this form. */
bool pb_xd_write_file(pb_xd_save_t *xs, const char *path);

#endif
