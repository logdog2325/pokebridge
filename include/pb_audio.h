/* Background music for PokéBridge. Plays an embedded PCM track on a
 * gapless loop using libasnd's voice + queued-buffer API. The current
 * single track ("title") is wired up at init; everything else is just
 * mute/volume controls. */
#ifndef POKEBRIDGE_AUDIO_H
#define POKEBRIDGE_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/* Brings up libasnd, queues the title loop, and starts playback at
 * full volume. Safe to call more than once -- subsequent calls just
 * re-arm the loop if it has stopped. */
void pb_audio_init(void);

/* Mute/unmute the music. Returns the new muted state. The track keeps
 * playing under the hood (so the loop position doesn't reset); we
 * just set the voice volume to 0. */
bool pb_audio_toggle_mute(void);

/* Whether audio is currently muted. */
bool pb_audio_is_muted(void);

/* Pause/resume the entire ASND mixer. Use this around long blocking
 * I/O (CARD_Mount / CARD_Read / CARD_Write) -- the ~1 kHz DSP IRQ
 * libasnd fires can starve the EXI completion interrupt that libcard
 * needs, deadlocking the mount. Pair every pb_audio_suspend with a
 * matching pb_audio_resume. Nesting is safe (a counter is kept). */
void pb_audio_suspend(void);
void pb_audio_resume(void);

/* Full AESND teardown around libcard ops. Fully unloads the AESND DSP
 * task so the card-unlock DSP task can boot. Pair every call with
 * pb_audio_rebuild_after_card. Music re-starts from the current
 * playlist position after rebuild.
 *
 * Use this INSTEAD OF suspend/resume around CARD_* calls; the "pause"
 * variant only stops mixing, not the DSP task itself, and is why
 * CARD_Mount hangs after the first SD operation. */
void pb_audio_hard_teardown_for_card(void);
void pb_audio_rebuild_after_card(void);

/* Drive the playlist forward. Must be called regularly (~vsync rate
 * is fine) so the software-timer-based track advance can run. The
 * main pb_gfx_wait_button loop is the natural place. Cheap: just a
 * gettime() compare against the current track's known duration. */
void pb_audio_tick(void);

#endif
