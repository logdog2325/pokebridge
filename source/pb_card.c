#include "pb_card.h"
#include "pb_audio.h"
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

/* Game code dispatch. Real Pokémon GameCube product codes (regional
 * variants), verified against PKHeX SaveHandlerGCI.cs and GameTDB:
 *   XD:        GXXE (US), GXXP (PAL), GXXJ (JP)
 *   Colosseum: GC6E (US), GC6P (PAL), GC6J (JP)
 *   Pokemon Box: GPXE (US), GPXP (PAL), GPXJ (JP)
 * NOTE: "G3R" was an early incorrect guess for Pokemon Box -- the real
 * code is GPX. Don't put it back.
 */
static pb_card_game_t classify(const char gamecode[4]) {
    if (gamecode[0] == 'G' && gamecode[1] == 'X' && gamecode[2] == 'X') return PB_CARD_GAME_XD;
    if (gamecode[0] == 'G' && gamecode[1] == 'C' && gamecode[2] == '6') return PB_CARD_GAME_COLOSSEUM;
    if (gamecode[0] == 'G' && gamecode[1] == 'P' && gamecode[2] == 'X') return PB_CARD_GAME_BOX;
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

/* Build-time sanity: our opaque blob must be at least sizeof(card_dir). */
typedef char pb_card_blob_size_check[(sizeof(card_dir) <= PB_CARD_DIR_BLOB_SIZE) ? 1 : -1];

static int scan_slot(int slot, pb_card_entry_t *out, int max, int existing_count) {
    int err = CARD_Mount(slot, s_workarea, NULL);
    if (err < 0) return 0;  /* no card / unformatted / etc. */

    /* GCMM pattern: SetGamecode(NULL)/SetCompany(NULL) disables the
     * directory filter so FindFirst sees ALL entries regardless of
     * which gamecode CARD_Init was called with. (showall=true on
     * FindFirst handles this too, but doing both is defensive.) */
    CARD_SetGamecode(NULL);
    CARD_SetCompany(NULL);

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
            e->length = dir.filelen;
            e->game = g;
            /* Stash the whole card_dir so the read side can call
             * CARD_OpenEntry without re-doing the lookup. */
            memcpy(e->_dir_blob, &dir, sizeof dir);
        }
        ret = CARD_FindNext(&dir);
    }
    CARD_Unmount(slot);
    return found;
}

int pb_card_scan(pb_card_entry_t *out, int max) {
    /* Pause libasnd's ~1 kHz DSP IRQ for the duration of the scan --
     * otherwise the EXI completion interrupts libcard waits on can be
     * starved and CARD_Mount deadlocks. */
    pb_audio_suspend();
    ensure_inited();
    int total = 0;
    total += scan_slot(CARD_SLOTA, out, max, total);
    total += scan_slot(CARD_SLOTB, out, max, total);
    pb_audio_resume();
    return total;
}

const char *pb_card_err_str(pb_card_err_t e) {
    switch (e) {
        case PB_CARD_OK:           return "OK";
        case PB_CARD_ERR_BAD_ARGS: return "bad args";
        case PB_CARD_ERR_MOUNT:    return "CARD_Mount failed";
        case PB_CARD_ERR_OPEN:     return "CARD_Open failed";
        case PB_CARD_ERR_ZERO_LEN: return "file length = 0";
        case PB_CARD_ERR_ALLOC:    return "out of memory";
        case PB_CARD_ERR_READ:     return "CARD_Read failed";
    }
    return "?";
}

static size_t do_card_read(const pb_card_entry_t *entry, uint8_t *out_buf,
                           size_t max_bytes, pb_card_read_status_t *status) {
    pb_card_read_status_t local = {0};
    if (!status) status = &local;
    status->stage = PB_CARD_OK;
    status->libogc_rc = 0;
    status->cf_len = 0;
    status->bytes_read = 0;

    if (!entry || !out_buf) { status->stage = PB_CARD_ERR_BAD_ARGS; return 0; }
    ensure_inited();

    int rc = CARD_Mount(entry->slot, s_workarea, NULL);
    if (rc < 0) {
        status->stage = PB_CARD_ERR_MOUNT;
        status->libogc_rc = rc;
        return 0;
    }

    /* GCMM canonical pattern: tell libogc which game's file we're
     * about to open (the directory filter matches gamecode+company),
     * then CARD_Open looks up by filename within those filters.
     * Same approach used by suloku's GCMM mcard.c. */
    CARD_SetGamecode((const char *)entry->gamecode);
    CARD_SetCompany((const char *)entry->company);

    card_file cf;
    rc = CARD_Open(entry->slot, (char *)entry->filename, &cf);
    if (rc < 0) {
        status->stage = PB_CARD_ERR_OPEN;
        status->libogc_rc = rc;
        CARD_Unmount(entry->slot);
        return 0;
    }

    uint32_t flen = (uint32_t)cf.len;
    status->cf_len = flen;
    if (flen == 0) {
        status->stage = PB_CARD_ERR_ZERO_LEN;
        CARD_Close(&cf);
        CARD_Unmount(entry->slot);
        return 0;
    }

    /* CARD_Read requires offset and len to be multiples of CARD_READSIZE
     * (512 bytes) and offset+len must not exceed file->len. We clamp to
     * cf.len, then round DOWN to a 512 multiple. The tail (< 512 bytes,
     * unlikely for save files) is lost but no Pokemon save format relies
     * on it. */
    const uint32_t READSIZE = 512;
    uint32_t to_read = flen;
    if (to_read > max_bytes) to_read = (uint32_t)max_bytes;
    uint32_t aligned = (to_read / READSIZE) * READSIZE;
    if (aligned == 0) {
        status->stage = PB_CARD_ERR_ZERO_LEN;
        CARD_Close(&cf);
        CARD_Unmount(entry->slot);
        return 0;
    }

    uint8_t *scratch = memalign(32, aligned);
    if (!scratch) {
        status->stage = PB_CARD_ERR_ALLOC;
        CARD_Close(&cf);
        CARD_Unmount(entry->slot);
        return 0;
    }

    /* Single big read. libogc CARD_Read internally chunks down to the
     * physical sector size. */
    rc = CARD_Read(&cf, scratch, aligned, 0);
    CARD_Close(&cf);
    CARD_Unmount(entry->slot);

    if (rc < 0) {
        status->stage = PB_CARD_ERR_READ;
        status->libogc_rc = rc;
        free(scratch);
        return 0;
    }

    memcpy(out_buf, scratch, aligned);
    free(scratch);
    status->bytes_read = aligned;
    return aligned;
}

static size_t do_card_write(const pb_card_entry_t *entry, const uint8_t *buf,
                            size_t len, pb_card_read_status_t *status) {
    pb_card_read_status_t local = {0};
    if (!status) status = &local;
    status->stage = PB_CARD_OK;
    status->libogc_rc = 0;
    status->cf_len = 0;
    status->bytes_read = 0;

    if (!entry || !buf || len == 0) {
        status->stage = PB_CARD_ERR_BAD_ARGS;
        return 0;
    }
    ensure_inited();

    int rc = CARD_Mount(entry->slot, s_workarea, NULL);
    if (rc < 0) {
        status->stage = PB_CARD_ERR_MOUNT;
        status->libogc_rc = rc;
        return 0;
    }

    /* CARD_Write requires sector-size alignment, not the 512 used by
     * CARD_Read. Sector is 0x2000 on standard cards; query to be sure. */
    uint32_t sector_size = 0x2000;
    CARD_GetSectorSize(entry->slot, &sector_size);
    if (sector_size == 0) sector_size = 0x2000;

    if ((len % sector_size) != 0) {
        /* Caller must pre-pad. We refuse rather than silently truncate. */
        status->stage = PB_CARD_ERR_BAD_ARGS;
        CARD_Unmount(entry->slot);
        return 0;
    }

    CARD_SetGamecode((const char *)entry->gamecode);
    CARD_SetCompany((const char *)entry->company);

    card_file cf;
    rc = CARD_Open(entry->slot, (char *)entry->filename, &cf);
    if (rc < 0) {
        status->stage = PB_CARD_ERR_OPEN;
        status->libogc_rc = rc;
        CARD_Unmount(entry->slot);
        return 0;
    }
    status->cf_len = (uint32_t)cf.len;

    /* Refuse to overwrite if buffer length doesn't match the file's
     * stored length -- CARD_Write doesn't extend files and writing
     * less would leave garbage in the trailing sectors. */
    if (len != (size_t)cf.len) {
        status->stage = PB_CARD_ERR_BAD_ARGS;
        CARD_Close(&cf);
        CARD_Unmount(entry->slot);
        return 0;
    }

    /* CARD_Write requires the buffer to be 32-byte aligned (DMA
     * constraint). Copy into an aligned scratch buffer. */
    uint8_t *scratch = memalign(32, len);
    if (!scratch) {
        status->stage = PB_CARD_ERR_ALLOC;
        CARD_Close(&cf);
        CARD_Unmount(entry->slot);
        return 0;
    }
    memcpy(scratch, buf, len);

    size_t written = 0;
    for (uint32_t off = 0; off < len; off += sector_size) {
        rc = CARD_Write(&cf, scratch + off, sector_size, off);
        if (rc < 0) {
            status->stage = PB_CARD_ERR_READ; /* reuse READ stage for "i/o failed" */
            status->libogc_rc = rc;
            break;
        }
        written += sector_size;
    }

    CARD_Close(&cf);
    CARD_Unmount(entry->slot);
    free(scratch);

    status->bytes_read = written;
    return written;
}

/* Public wrappers: bracket each libcard call with pb_audio_suspend so
 * the EXI completion IRQs aren't starved by libasnd's DSP IRQ. */
size_t pb_card_read_file(const pb_card_entry_t *entry, uint8_t *out_buf,
                         size_t max_bytes, pb_card_read_status_t *status) {
    pb_audio_suspend();
    size_t r = do_card_read(entry, out_buf, max_bytes, status);
    pb_audio_resume();
    return r;
}

size_t pb_card_write_file(const pb_card_entry_t *entry, const uint8_t *buf,
                          size_t len, pb_card_read_status_t *status) {
    pb_audio_suspend();
    size_t r = do_card_write(entry, buf, len, status);
    pb_audio_resume();
    return r;
}
