#include "pb_audio.h"
#include "embedded_title_audio.h"
#include "embedded_match_audio.h"
#include "embedded_colosseum_audio.h"
#include <aesndlib.h>
#include <ogc/lwp_watchdog.h>  /* gettime() + ticks_to_millisecs */
#include <stddef.h>

/* libaesnd is the GameCube audio library that COEXISTS with libcard.
 * libasnd's DSP task spin-loops on mailbox handshakes from IRQ context
 * and races EXI completion, deadlocking CARD_Mount. Diagnosed by
 * Extrems in Genesis-Plus-GX PR #538. */

/* Playlist of background tracks. The PREVIOUS implementation tried to
 * use AESND's voice-state callback to detect "buffer ended" and start
 * the next track, but the callback does not fire reliably on natural
 * buffer drain (Dolphin confirmed: track 1 ended, then silence).
 *
 * This version uses a SOFTWARE TIMER instead: each track plays with
 * the hardware loop enabled (so AESND keeps outputting samples with
 * no dead air), and pb_audio_tick (called from the main UI loop)
 * compares elapsed real time vs the track's natural duration. When
 * elapsed > duration, we re-arm the voice with the next track's
 * buffer + frequency. */

typedef struct {
    const uint8_t *data;
    uint32_t       len;        /* bytes */
    uint32_t       rate;       /* Hz */
    const char    *label;
} pb_track_t;

static const pb_track_t s_playlist[] = {
    { pb_embedded_title_audio,
      pb_embedded_title_audio_len,
      pb_embedded_title_audio_sample_rate,
      "Title" },
    { pb_embedded_match_audio,
      pb_embedded_match_audio_len,
      pb_embedded_match_audio_sample_rate,
      "Match Intro" },
    { pb_embedded_colosseum_audio,
      pb_embedded_colosseum_audio_len,
      pb_embedded_colosseum_audio_sample_rate,
      "Colosseum" },
};
#define PLAYLIST_LEN ((int)(sizeof s_playlist / sizeof s_playlist[0]))

static AESNDPB *s_voice = NULL;
static bool s_inited = false;
static bool s_muted  = false;
static int  s_suspend_depth = 0;

static int       s_track_index = 0;
static uint64_t  s_track_start_ms = 0;
static uint64_t  s_track_duration_ms = 0;

/* Duration of a 16-bit mono PCM buffer in milliseconds. */
static uint64_t track_duration_ms(const pb_track_t *t) {
    if (!t->rate) return 0;
    /* samples = bytes / 2 (16-bit mono); ms = samples * 1000 / rate. */
    return ((uint64_t)t->len / 2u) * 1000ull / (uint64_t)t->rate;
}

static void start_track(int idx) {
    if (!s_voice) return;
    if (idx < 0 || idx >= PLAYLIST_LEN) idx = 0;
    const pb_track_t *t = &s_playlist[idx];
    if (t->len < 64 || !t->data) return;
    s_track_index       = idx;
    s_track_start_ms    = ticks_to_millisecs(gettime());
    s_track_duration_ms = track_duration_ms(t);
    /* Hardware loop = true: the voice keeps outputting if pb_audio_tick
     * is briefly late; transitions between tracks happen on tick, and
     * the new SetVoiceBuffer is the visible cutover point. */
    AESND_PlayVoice(s_voice,
                    VOICE_MONO16,
                    t->data,
                    t->len,
                    (f32)t->rate,
                    0,
                    true);
}

void pb_audio_init(void) {
    if (s_inited) return;
    int total_len = 0;
    for (int i = 0; i < PLAYLIST_LEN; i++) total_len += (int)s_playlist[i].len;
    if (total_len < 64) {
        s_inited = true;
        return;
    }

    AESND_Init();
    AESND_Pause(false);

    s_voice = AESND_AllocateVoice(NULL);
    if (!s_voice) {
        s_inited = true;
        return;
    }
    AESND_SetVoiceVolume(s_voice, 0xFF, 0xFF);
    s_inited = true;
    start_track(0);
}

void pb_audio_tick(void) {
    if (!s_inited || !s_voice) return;
    if (s_track_duration_ms == 0) return;
    /* If we're suspended (during CARD I/O), pause the playlist clock
     * too -- otherwise multiple tracks could "fly by" silently during
     * a memcard write. Cheapest way: just bump start_ms so the timer
     * doesn't trigger an advance until we get an unsuspended tick. */
    if (s_suspend_depth > 0) {
        s_track_start_ms = ticks_to_millisecs(gettime());
        return;
    }
    uint64_t now = ticks_to_millisecs(gettime());
    if (now - s_track_start_ms >= s_track_duration_ms) {
        int next = s_track_index + 1;
        if (next >= PLAYLIST_LEN) next = 0;
        start_track(next);
    }
}

bool pb_audio_toggle_mute(void) {
    s_muted = !s_muted;
    if (s_voice) AESND_SetVoiceMute(s_voice, s_muted);
    return s_muted;
}

bool pb_audio_is_muted(void) {
    return s_muted;
}

void pb_audio_suspend(void) {
    if (!s_inited) return;
    if (s_suspend_depth++ == 0) AESND_Pause(true);
}

void pb_audio_resume(void) {
    if (!s_inited) return;
    if (s_suspend_depth > 0 && --s_suspend_depth == 0) AESND_Pause(false);
}

/* Full AESND teardown for use around libcard operations.
 *
 * Why this exists: Pokemon-format memory cards are LOCKED per power
 * cycle, and CARD_Mount unlocks them by scheduling a DSP task at
 * priority 255. libaesnd's mixer runs a persistent DSP task ALSO at
 * priority 255. In libogc's DSP task queue (libogc/dsp.c
 * __dsp_inserttask), same-priority tasks queue at the TAIL, and
 * DSP_AddTask only boots a task if it's already at head. So once
 * AESND is running, the card unlock task queues behind it and never
 * boots -- CARD_Mount then blocks in LWP_ThreadSleep forever.
 *
 * AESND_Pause(true) is not enough: it only sets a "skip mixing" flag
 * inside the audio DMA callback; the DSP task keeps running and
 * keeps holding the queue-head. Only AESND_Reset (which sends the
 * 0xfacedead shutdown mail to the DSP binary) actually removes
 * __aesnddsptask from the queue.
 *
 * Cost: audio silence for ~2-3 seconds around each card op, plus
 * ~50ms DSP task reload on the way back. Acceptable price for
 * "memcard scan actually works after SD operations". */
void pb_audio_hard_teardown_for_card(void) {
    if (!s_inited) return;
    if (s_voice) {
        AESND_SetVoiceStop(s_voice, true);
        AESND_FreeVoice(s_voice);
        s_voice = NULL;
    }
    AESND_Reset();       /* This is the critical step */
    s_inited = false;    /* Force pb_audio_init to fully rebuild. */
}

void pb_audio_rebuild_after_card(void) {
    if (s_inited) return;
    pb_audio_init();     /* Re-inits AESND, re-allocates voice, restarts music. */
}
