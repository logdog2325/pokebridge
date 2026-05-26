/* Gen 3 Pokémon save file parser.
 *
 * Save layout (Ruby / Sapphire / FireRed / LeafGreen / Emerald, also ROM hacks):
 *   - File is 128 KB (sometimes 64 KB).
 *   - Two 57344-byte slots (Slot A at 0x00000, Slot B at 0xE000) — the slot
 *     with the higher save_index is the most recent.
 *   - Each slot is 14 sections of 4096 bytes. Each section has a footer at
 *     offset 0xFF4 of: u16 section_id, u16 checksum, u32 signature (0x08012025),
 *     u32 save_index.
 *   - Section IDs are not in physical order — must reorder by section_id.
 */
#ifndef POKEBRIDGE_SAVE_H
#define POKEBRIDGE_SAVE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PB_SAVE_SIZE       (128 * 1024)
#define PB_SLOT_SIZE       (57344)        /* 14 * 4096 */
#define PB_SECTION_SIZE    (4096)
#define PB_SECTION_COUNT   (14)
#define PB_SECTION_SIG     (0x08012025u)
#define PB_SECTION_FOOTER  (0xFF4)

typedef enum {
    PB_GAME_UNKNOWN = 0,
    PB_GAME_RS,        /* Ruby / Sapphire */
    PB_GAME_FRLG,      /* FireRed / LeafGreen */
    PB_GAME_EMERALD,
    PB_GAME_HACK       /* pokeemerald or pokeemerald-expansion based ROM hack */
} pb_game_t;

typedef struct {
    uint8_t  raw[PB_SAVE_SIZE];
    /* Section pointers in logical order (sec[i] = section with id == i in the
     * most recent slot). May be NULL if a section is missing (corrupt save). */
    uint8_t *sec[PB_SECTION_COUNT];
    uint32_t save_index;
    pb_game_t game;
    uint32_t security_key;     /* Used by FRLG/Emerald to obfuscate money & some fields. RS = 0. */
    /* Trainer info, lifted from section 0 once we identify the game. */
    char     trainer_name[8];  /* 7 chars + NUL (decoded from Gen 3 charset) */
    uint16_t trainer_id;       /* public TID */
    uint16_t secret_id;        /* SID */
    uint8_t  gender;
} pb_save_t;

/* Load and parse a Gen 3 save from raw bytes. Returns true on success. */
bool pb_save_load(pb_save_t *out, const uint8_t *data, size_t len);

/* Load directly from a file path on the mounted SD card. */
bool pb_save_load_file(pb_save_t *out, const char *path);

/* Get a pointer to the start of the party data inside the save (section 1).
 * Out params receive the party count (0..6) and a pointer to the first
 * 100-byte party-pokemon struct. */
bool pb_save_party(const pb_save_t *s, uint8_t *out_count, const uint8_t **out_party);

/* Iterate boxed pokemon. There are 14 boxes × 30 mons × 80 bytes each, split
 * across sections 5–13. Returns pointer to box i (0..13) start, or NULL. */
const uint8_t *pb_save_box(const pb_save_t *s, int box_index);

const char *pb_game_name(pb_game_t g);

/* After mutating bytes inside a section's data area, recompute and write
 * the section's checksum (footer offset 0xFF6). Use this whenever the
 * editor changes party / box data so the save stays self-consistent. */
void pb_save_update_section_checksum(pb_save_t *s, int section_id);

/* Write 80 bytes (an edited boxed pokemon) into PC storage at the right
 * sections, walking section boundaries and updating affected checksums.
 * Returns true on success. */
bool pb_save_box_write_slot(pb_save_t *s, int box_index, int slot,
                            const uint8_t raw80[80]);

/* Write the in-memory save buffer back to a file. Useful when SD is mounted
 * so edits persist across sessions. Returns true on success. */
bool pb_save_write_file(const pb_save_t *s, const char *path);

#endif
