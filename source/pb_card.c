#include "pb_card.h"
#include "pb_audio.h"
#include <ogc/card.h>
#include <ogc/exi.h>
#include <ogc/system.h>
#include <ogc/video.h>
#include <fat.h>
#include <malloc.h>
#include <stdbool.h>
#include <string.h>

/* Cold-boot CARD_Mount hang fix (the GCMM-vs-PokeBridge difference):
 *
 * fatInitDefault() at boot causes libfat's __io_gcsda backend to call
 * EXI_Lock(ch0) since the SD adapter shares EXI channel 0 with memcard
 * slot A (and channel 1 with slot B for slot-B SD adapters). When
 * fatUnmount() releases the FAT layer, __io_gcsda does NOT release the
 * EXI lock -- not part of libfat's contract. EXI_ProbeReset doesn't
 * clear EXI_FLAG_LOCKED either. So CARD_Mount's internal EXI_Lock
 * call silently fails, the mount state machine never advances, the
 * EXI IRQ never fires, and LWP_ThreadSleep blocks forever.
 *
 * The trick GCMM gets for free: it doesn't call fatInitDefault at
 * boot, so EXI is never locked in the first place. Swiss's reload
 * stub also re-zeros EXI on .dol transitions, which is why
 * GCMM-then-PokéBridge works.
 *
 * Our fix: force-release EXI locks + detach devices BEFORE every
 * CARD_* operation, then re-fatInitDefault after. This mimics what
 * Swiss's reload stub does manually. */
extern bool pb_sd_available;
static bool s_fat_was_unmounted = false;

static void pb_fat_release_for_card(void) {
    /* 1. Tear down the FAT layer. */
    if (pb_sd_available && !s_fat_was_unmounted) {
        fatUnmount("sd");
        s_fat_was_unmounted = true;
    }

    /* 2. Drain any EXI locks libfat's disc backend may have left.
     *    EXI_Unlock returns 1 while there are queued waiters / held
     *    locks on the channel; loop until 0. */
    for (int chn = 0; chn < 2; chn++) {
        int drain = 0;
        while (EXI_Unlock(chn) == 1 && drain++ < 16) { /* drain */ }
    }

    /* 3. Detach the SD device from each channel. This is what
     *    __exi_init does at libogc startup, but it never runs again
     *    after fatInitDefault has attached __io_gcsda. */
    EXI_Detach(EXI_CHANNEL_0);
    EXI_Detach(EXI_CHANNEL_1);

    /* 4. Settle delay -- EXI debounce is ~30us per __exi_probe; give
     *    the bus ~500ms to come back to idle before CARD_Mount tries. */
    for (int i = 0; i < 30; i++) VIDEO_WaitVSync();
}

static void pb_fat_reacquire_after_card(void) {
    if (s_fat_was_unmounted) {
        fatInitDefault();
        s_fat_was_unmounted = false;
    }
}

/* Workarea for card operations -- must be 32-byte aligned and at least
 * CARD_WORKAREA_SIZE bytes per active mount. We share one across both
 * slots since we mount-then-unmount per slot rather than holding both
 * simultaneously. Statically allocated (BSS) -- heap or stack buffers
 * can land in regions where Swiss leaves stale uncached state and
 * silently fail EXI DMA. */
static uint8_t s_workarea[CARD_WORKAREA_SIZE] __attribute__((aligned(32)));

/* Mount the given slot with GCMM's defensive retry pattern:
 *   - EXI_ProbeReset() before each attempt clears stale EXI channel
 *     state from whatever .dol ran before us (Swiss, IPL, prior app).
 *   - CARD_Init(NULL, NULL) per attempt (NULL = "see all gamecodes").
 *   - CARD_Probe BEFORE CARD_Mount -- non-blocking check that returns
 *     immediately if no card is inserted. Without this, CARD_Mount
 *     can block indefinitely waiting on a card-extraction IRQ that
 *     never fires when there's no card to extract.
 *   - Up to 10 retries with VSync waits between, since real cards
 *     genuinely miss the first 1-2 probes on some hardware. */
static int pb_card_mount_retry(int slot) {
    int ret = -1;
    for (int tries = 0; tries < 10 && ret < 0; tries++) {
        EXI_ProbeReset();
        CARD_Init(NULL, NULL);
        CARD_SetCompany(NULL);
        CARD_SetGamecode(NULL);

        /* Quick check first. CARD_Probe returns: positive if card
         * present, 0 if no card, negative on EXI error. If no card,
         * return early -- attempting CARD_Mount on an empty slot
         * blocks indefinitely. */
        s32 probed = CARD_Probe(slot);
        if (probed <= 0) {
            ret = -3;  /* CARD_ERROR_NOCARD-ish; tells caller to skip slot */
            VIDEO_WaitVSync();
            continue;
        }

        ret = CARD_Mount(slot, s_workarea, NULL);
        if (ret >= 0) break;
        VIDEO_WaitVSync();
    }
    return ret;
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
    int err = pb_card_mount_retry(slot);
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

int pb_card_scan_slot(int slot, pb_card_entry_t *out, int max, int existing_count) {
    pb_audio_suspend();
    pb_fat_release_for_card();
    int n = scan_slot(slot, out, max, existing_count);
    pb_fat_reacquire_after_card();
    pb_audio_resume();
    return n;
}

int pb_card_scan(pb_card_entry_t *out, int max) {
    /* Batch both slots inside ONE FAT unmount/remount cycle to avoid
     * remounting between scans -- the SD adapter takes a noticeable
     * fraction of a second to re-initialize. */
    pb_audio_suspend();
    pb_fat_release_for_card();
    int total = 0;
    total += scan_slot(CARD_SLOTA, out, max, total);
    total += scan_slot(CARD_SLOTB, out, max, total);
    pb_fat_reacquire_after_card();
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

    int rc = pb_card_mount_retry(entry->slot);
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

    int rc = pb_card_mount_retry(entry->slot);
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

/* Public wrappers: bracket each libcard call with audio suspend (EXI
 * IRQ priority) AND libfat unmount (EXI channel ownership). Without
 * the unmount, CARD_Mount on the slot that holds the SD adapter
 * deadlocks on EXI_Lock forever. */
size_t pb_card_read_file(const pb_card_entry_t *entry, uint8_t *out_buf,
                         size_t max_bytes, pb_card_read_status_t *status) {
    pb_audio_suspend();
    pb_fat_release_for_card();
    size_t r = do_card_read(entry, out_buf, max_bytes, status);
    pb_fat_reacquire_after_card();
    pb_audio_resume();
    return r;
}

size_t pb_card_write_file(const pb_card_entry_t *entry, const uint8_t *buf,
                          size_t len, pb_card_read_status_t *status) {
    pb_audio_suspend();
    pb_fat_release_for_card();
    size_t r = do_card_write(entry, buf, len, status);
    pb_fat_reacquire_after_card();
    pb_audio_resume();
    return r;
}
