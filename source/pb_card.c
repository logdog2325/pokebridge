#include "pb_card.h"
#include <ogc/card.h>
#include <ogc/system.h>
#include <malloc.h>
#include <string.h>

/* Workarea for card operations -- must be 32-byte aligned and at least
 * CARD_WORKAREA_SIZE bytes per active mount. We share one across both
 * slots since we mount-then-unmount per slot rather than holding both
 * simultaneously. */
static uint8_t s_workarea[CARD_WORKAREA_SIZE] __attribute__((aligned(32)));
static bool s_card_inited = false;

static void ensure_inited(void) {
    if (s_card_inited) return;
    CARD_Init("PBRG", "01");  /* arbitrary 4-char gamecode + 2-char maker */
    s_card_inited = true;
}

/* Game code dispatch. Real Pokémon game codes (regional variants):
 *   XD:        GXXE (US), GXXP (PAL), GXXJ (JP)
 *   Colosseum: GC6E (US), GC6P (PAL), GC6J (JP), GC6S (KO?)
 *   Pokemon Box: G3RE / G3RP / G3RJ
 */
static pb_card_game_t classify(const char gamecode[4]) {
    if (gamecode[0] == 'G' && gamecode[1] == 'X' && gamecode[2] == 'X') return PB_CARD_GAME_XD;
    if (gamecode[0] == 'G' && gamecode[1] == 'C' && gamecode[2] == '6') return PB_CARD_GAME_COLOSSEUM;
    if (gamecode[0] == 'G' && gamecode[1] == '3' && gamecode[2] == 'R') return PB_CARD_GAME_BOX;
    return PB_CARD_GAME_UNKNOWN;
}

const char *pb_card_game_name(pb_card_game_t g) {
    switch (g) {
        case PB_CARD_GAME_XD:        return "Pokemon XD";
        case PB_CARD_GAME_COLOSSEUM: return "Pokemon Colosseum";
        case PB_CARD_GAME_BOX:       return "Pokemon Box: R&S";
        default:                      return "(unknown)";
    }
}

static int scan_slot(int slot, pb_card_entry_t *out, int max, int existing_count) {
    int err = CARD_Mount(slot, s_workarea, NULL);
    if (err < 0) return 0;  /* no card / unformatted / etc. */

    card_dir dir;
    int ret = CARD_FindFirst(slot, &dir, true);
    int found = 0;
    while (ret == 0 && existing_count + found < max) {
        pb_card_game_t g = classify((const char *)dir.gamecode);
        if (g != PB_CARD_GAME_UNKNOWN) {
            pb_card_entry_t *e = &out[existing_count + found++];
            e->slot = slot;
            memcpy(e->gamecode, dir.gamecode, 4);  e->gamecode[4] = 0;
            memcpy(e->company, dir.company, 2);    e->company[2] = 0;
            memcpy(e->filename, dir.filename, 32); e->filename[32] = 0;
            /* Length isn't in card_dir; we'd need CARD_GetStatus or open
             * the file. Defer to read time. */
            e->length = 0;
            e->game = g;
        }
        ret = CARD_FindNext(&dir);
    }
    CARD_Unmount(slot);
    return found;
}

int pb_card_scan(pb_card_entry_t *out, int max) {
    ensure_inited();
    int total = 0;
    total += scan_slot(CARD_SLOTA, out, max, total);
    total += scan_slot(CARD_SLOTB, out, max, total);
    return total;
}

size_t pb_card_read_file(const pb_card_entry_t *entry, uint8_t *out_buf, size_t max_bytes) {
    if (!entry || !out_buf) return 0;
    ensure_inited();
    if (CARD_Mount(entry->slot, s_workarea, NULL) < 0) return 0;

    card_file cf;
    int err = CARD_Open(entry->slot, (char *)entry->filename, &cf);
    if (err < 0) { CARD_Unmount(entry->slot); return 0; }

    /* CARD_Read requires aligned multi-sector reads (sector = 0x2000 bytes
     * on standard memcards). Round up to sector boundary and clamp to
     * max_bytes. */
    uint32_t flen = (uint32_t)cf.len;
    uint32_t to_read = flen;
    if (to_read > max_bytes) to_read = (uint32_t)max_bytes;
    /* CARD_Read offset/len must be sector-aligned. We allocate a sector-
     * aligned scratch buffer, read in chunks, then copy into out_buf. */
    const uint32_t SECTOR = 0x2000;
    uint32_t rounded = ((to_read + SECTOR - 1) / SECTOR) * SECTOR;
    uint8_t *scratch = memalign(32, rounded);
    if (!scratch) { CARD_Close(&cf); CARD_Unmount(entry->slot); return 0; }

    size_t got = 0;
    for (uint32_t off = 0; off < rounded; off += SECTOR) {
        if (CARD_Read(&cf, scratch + off, SECTOR, off) < 0) break;
        got += SECTOR;
    }
    CARD_Close(&cf);
    CARD_Unmount(entry->slot);

    if (got > to_read) got = to_read;
    memcpy(out_buf, scratch, got);
    free(scratch);
    return got;
}
