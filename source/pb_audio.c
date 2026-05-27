#include "pb_audio.h"
#include "embedded_title_audio.h"
#include <aesndlib.h>

/* libaesnd is the GameCube audio library that COEXISTS with libcard.
 * The other option (libasnd) is known to deadlock CARD_Mount because
 * its DSP task spin-loops on mailbox handshakes that race the EXI
 * completion path. Bug diagnosed publicly by Extrems (libogc dev) in
 * Genesis-Plus-GX PR #538. Swiss, Wii64, and pcsxgc all use libaesnd
 * alongside memcard I/O for this exact reason -- libaesnd's DSP
 * microcode runs cooperatively from the AUDIO DMA callback, not from
 * the DSP IRQ context, so it doesn't starve EXI.
 *
 * AESND's voice API has built-in hardware looping via
 * AESND_SetVoiceLoop(pb, true), so we don't need a refill callback. */

static AESNDPB *s_voice = NULL;
static bool s_inited = false;
static bool s_muted  = false;
static int  s_suspend_depth = 0;

void pb_audio_init(void) {
    if (s_inited) return;

    /* If the embedded audio header is the empty stub (created by
     * setup_assets.sh when no song was supplied), don't bother
     * bringing the mixer up. */
    if (pb_embedded_title_audio_len < 64) {
        s_inited = true;
        return;
    }

    AESND_Init();
    AESND_Pause(false);

    s_voice = AESND_AllocateVoice(NULL);
    if (!s_voice) {
        s_inited = true; /* prevent retry storm */
        return;
    }

    /* Mono 16-bit BE PCM. The embed tool byte-swapped each sample so
     * the bytes in pb_embedded_title_audio are already BE. */
    AESND_SetVoiceFormat(s_voice, VOICE_MONO16);
    AESND_SetVoiceFrequency(s_voice, (f32)pb_embedded_title_audio_sample_rate);
    AESND_SetVoiceVolume(s_voice, 0xFF, 0xFF);
    AESND_SetVoiceLoop(s_voice, true);
    AESND_SetVoiceBuffer(s_voice,
                         pb_embedded_title_audio,
                         pb_embedded_title_audio_len);
    /* Start: false=unstop. AESND's loop flag handles the wrap. */
    AESND_SetVoiceStop(s_voice, false);

    s_inited = true;
}

bool pb_audio_toggle_mute(void) {
    s_muted = !s_muted;
    if (s_voice) {
        AESND_SetVoiceMute(s_voice, s_muted);
    }
    return s_muted;
}

bool pb_audio_is_muted(void) {
    return s_muted;
}

void pb_audio_suspend(void) {
    if (!s_inited) return;
    if (s_suspend_depth++ == 0) {
        AESND_Pause(true);
    }
}

void pb_audio_resume(void) {
    if (!s_inited) return;
    if (s_suspend_depth > 0 && --s_suspend_depth == 0) {
        AESND_Pause(false);
    }
}
