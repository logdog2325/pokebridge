/* Legalizer: map a ROM-hacked Gen 3 pokemon onto its closest legal Gen 3
 * counterpart so the resulting .pk3 can be injected via PKHeX and ride the
 * legitimate transfer chain up to Pokémon HOME.
 *
 * Strategy:
 *   1. If species > 386 → remap via the project's species_map table
 *      (a hand-curated 387..N → 1..386 lookup based on type and role).
 *   2. If any move > 354 (last Gen 3 move) → remap via move_map; if no good
 *      analogue, drop the slot to move 0 (no move) and fix PP.
 *   3. If held item > 376 → set to 0.
 *   4. If ability bit selects an ability the legal species doesn't have →
 *      flip to ability 0.
 *   5. Re-roll PID consistent with desired nature/gender (gender ratios are
 *      inherited from the official species after remap, so usually fine).
 *   6. Recompute checksum, re-encrypt.
 *
 * The original is not modified — output goes into a fresh buffer that callers
 * can write to /pokebridge/export/<name>.pk3 (80 bytes, no party stats).
 */
#ifndef POKEBRIDGE_LEGALIZER_H
#define POKEBRIDGE_LEGALIZER_H

#include "pokemon.h"
#include <stdbool.h>

typedef struct {
    uint16_t orig_species;
    uint16_t legal_species;
    int      moves_remapped;
    int      moves_dropped;
    bool     ability_changed;
    bool     pid_rerolled;
    bool     item_cleared;
} pb_legalize_report_t;

/* Try to legalize p. On success, writes the 80-byte legal .pk3 to out80 and
 * fills the report. Returns true if a legal mon was produced. Returns false
 * only if the input is so broken (zeroed species, corrupt checksum) that
 * nothing sensible can come out. */
bool pb_legalize(const pb_pkm_t *p, uint8_t out80[80], pb_legalize_report_t *report);

/* Custom-species → official-species mapping. species_id 387..MAX → 1..386. */
uint16_t pb_species_remap(uint16_t custom_id);

/* Custom-move → official-move mapping. Move IDs > 354 are remapped or zeroed. */
uint16_t pb_move_remap(uint16_t custom_id);

#endif
