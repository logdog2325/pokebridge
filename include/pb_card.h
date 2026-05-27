/* GameCube memory card scanning.
 *
 * Reads .gci-style save files directly from a physical memory card in
 * slot A or B (and from the Wii's GameCube memcard slots when running in
 * GC mode). Looks specifically for XD / Colosseum / Pokémon Box game
 * codes and feeds the raw bytes into the existing pb_xd_load /
 * pb_colo_load parsers.
 *
 * Backed by libogc's <ogc/card.h>: CARD_Mount / CARD_FindFirst /
 * CARD_FindNext / CARD_Open / CARD_Read / CARD_Close / CARD_Unmount.
 */
#ifndef POKEBRIDGE_CARD_H
#define POKEBRIDGE_CARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PB_CARD_MAX_ENTRIES 32
#define PB_CARD_MAX_FILE_BYTES (512 * 1024)  /* covers Pokemon Box (~470 KB) */

typedef enum {
    PB_CARD_GAME_UNKNOWN = 0,
    PB_CARD_GAME_XD,
    PB_CARD_GAME_COLOSSEUM,
    PB_CARD_GAME_BOX,
} pb_card_game_t;

typedef struct {
    int      slot;        /* 0 = slot A, 1 = slot B */
    char     gamecode[5]; /* 4 chars + NUL */
    char     company[3];  /* 2 chars + NUL */
    char     filename[33];/* 32 chars + NUL */
    uint32_t length;      /* file size in bytes (set after CARD_Open) */
    pb_card_game_t game;  /* detected game by gamecode */
} pb_card_entry_t;

/* Scan slot A and slot B for Pokémon save files. Fills `out` with up to
 * `max` entries, returns the actual count. Slots that aren't present /
 * unformatted are silently skipped. */
int pb_card_scan(pb_card_entry_t *out, int max);

/* Read the bytes of a card file into `out_buf`. Returns the number of
 * bytes read, or 0 on failure. The card must still be present in the
 * slot (we don't keep mounts open between calls). */
size_t pb_card_read_file(const pb_card_entry_t *entry, uint8_t *out_buf, size_t max_bytes);

const char *pb_card_game_name(pb_card_game_t g);

#endif
