#include "legalizer.h"
#include "cassora_species_map.h"
#include <string.h>

/* Map a pokeemerald-expansion species ID to a legal Gen 3 species (1..386).
 *
 *   0           -> 0 (empty slot, no remap needed)
 *   1..386      -> passthrough (already legal)
 *   387..1024   -> cassora_species_remap[] table lookup
 *   1025+       -> form variant (Hisuian/Alolan/Galarian extended IDs); we
 *                  don't have ID-level knowledge of the form scheme, so we
 *                  fall back to Unown (#201) as a clear "needs review" marker.
 *   table miss  -> Unown
 */
uint16_t pb_species_remap(uint16_t custom_id) {
    if (custom_id == 0) return 0;
    if (custom_id <= 386) return custom_id;
    if (custom_id < (sizeof cassora_species_remap / sizeof cassora_species_remap[0])) {
        uint16_t mapped = cassora_species_remap[custom_id];
        if (mapped != CASSORA_REMAP_NONE) return mapped;
    }
    return 201; /* Unown -- the "I don't have a mapping for this" marker */
}

uint16_t pb_move_remap(uint16_t custom_id) {
    /* Gen 3 has moves 0..354. Anything above must be remapped or dropped. */
    if (custom_id <= 354) return custom_id;
    /* TODO: pokeemerald-expansion adds Gen 4+ moves with stable IDs we can
     * map (e.g. Bullet Punch → ThunderPunch, Aqua Jet → Quick Attack). */
    return 0;
}

bool pb_legalize(const pb_pkm_t *p, uint8_t out80[80], pb_legalize_report_t *report) {
    if (!p || p->is_empty) return false;
    if (!p->checksum_ok) return false;

    pb_pkm_t legal = *p;
    memset(report, 0, sizeof *report);
    report->orig_species = p->g.species;

    /* Species */
    uint16_t new_sp = pb_species_remap(p->g.species);
    if (new_sp == 0) return false;
    legal.g.species = new_sp;
    report->legal_species = new_sp;

    /* Moves */
    for (int i = 0; i < 4; i++) {
        uint16_t m = p->a.moves[i];
        if (m == 0) continue;
        if (m > 354) {
            uint16_t r = pb_move_remap(m);
            if (r) { legal.a.moves[i] = r; report->moves_remapped++; }
            else   { legal.a.moves[i] = 0; legal.a.pp[i] = 0; report->moves_dropped++; }
        }
    }

    /* Item — Gen 3 items go up to ~376. Anything beyond → none. */
    if (p->g.held_item > 376) { legal.g.held_item = 0; report->item_cleared = true; }

    /* Ability — flip to ability 0 if the species' Gen 3 entry doesn't have a
     * second ability. For now we keep it; the .pk3 will fail a strict legality
     * check in PKHeX which will then prompt the user to flip it manually. */

    /* PID: we leave the PID as-is. Gen 3 nature/gender/shiny/ability are all
     * derived from PID — changing PID changes all four. PKHeX's auto-legalize
     * will adjust if needed; doing it here adds risk and complexity. */

    pb_pkm_encode(&legal, out80);
    return true;
}
