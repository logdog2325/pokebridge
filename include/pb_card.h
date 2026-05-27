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

/* sizeof(card_dir) is ~56 bytes in libogc; 96 leaves headroom for any
 * future struct growth. Treated as opaque from outside pb_card.c -- we
 * stash the full FindFirst/FindNext result so we can later call
 * CARD_OpenEntry without re-resolving the file by gamecode+filename
 * (which is what was failing: CARD_Open filters by current CARD_Init
 * gamecode and we're reading other games' saves). */
#define PB_CARD_DIR_BLOB_SIZE 96

typedef struct {
    int      slot;        /* 0 = slot A, 1 = slot B */
    char     gamecode[5]; /* 4 chars + NUL */
    char     company[3];  /* 2 chars + NUL */
    char     filename[33];/* 32 chars + NUL */
    uint32_t length;      /* file size in bytes (from card_dir.filelen) */
    pb_card_game_t game;  /* detected game by gamecode */
    uint8_t  _dir_blob[PB_CARD_DIR_BLOB_SIZE]; /* opaque card_dir copy */
} pb_card_entry_t;

/* Scan slot A and slot B for Pokémon save files. Fills `out` with up to
 * `max` entries, returns the actual count. Slots that aren't present /
 * unformatted are silently skipped. */
int pb_card_scan(pb_card_entry_t *out, int max);

/* Single-slot scan -- exposed so the UI can render "Scanning slot X..."
 * progress feedback between slots (and so the user can see WHICH slot
 * hangs if CARD_Mount deadlocks). `existing_count` is the number of
 * entries already in `out` from previous scans; new entries are
 * appended starting at out[existing_count]. Returns count appended. */
int pb_card_scan_slot(int slot, pb_card_entry_t *out, int max, int existing_count);

/* Diagnostic info from the last read attempt -- populated even on
 * failure so the UI can report exactly where things went wrong. */
typedef enum {
    PB_CARD_OK = 0,
    PB_CARD_ERR_BAD_ARGS,
    PB_CARD_ERR_MOUNT,
    PB_CARD_ERR_OPEN,
    PB_CARD_ERR_ZERO_LEN,
    PB_CARD_ERR_ALLOC,
    PB_CARD_ERR_READ,
} pb_card_err_t;

typedef struct {
    pb_card_err_t stage;     /* PB_CARD_OK on success, else where it failed */
    int           libogc_rc; /* raw libogc return code at the failing step */
    uint32_t      cf_len;    /* file->len reported by CARD_Open */
    size_t        bytes_read;/* how many bytes we actually copied out */
} pb_card_read_status_t;

/* Read the bytes of a card file into `out_buf`. Returns the number of
 * bytes read, or 0 on failure. If `status` is non-NULL, it's filled in
 * with diagnostic info regardless of success. The card must still be
 * present in the slot (we don't keep mounts open between calls). */
size_t pb_card_read_file(const pb_card_entry_t *entry, uint8_t *out_buf,
                          size_t max_bytes, pb_card_read_status_t *status);

/* Write the bytes in `buf` back into the file identified by `entry`,
 * overwriting in place. `len` MUST be a multiple of the card's sector
 * size (8192 bytes on standard memcards) and must equal the file's
 * stored length -- libogc CARD_Write does not extend files. Returns
 * the number of bytes written, or 0 on failure. If `status` is
 * non-NULL it gets the same diagnostic stage/code as on read. */
size_t pb_card_write_file(const pb_card_entry_t *entry, const uint8_t *buf,
                           size_t len, pb_card_read_status_t *status);

const char *pb_card_err_str(pb_card_err_t e);

const char *pb_card_game_name(pb_card_game_t g);

#endif
