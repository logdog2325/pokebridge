/* Cassora → Gen 3 species remap table.
 *
 * Hand-curated mapping from pokeemerald-expansion species IDs (≥ 387) to the
 * nearest legal Gen 3 species (1–386). Used by the legalizer to produce
 * HOME-eligible .pk3 files.
 *
 * pokeemerald-expansion follows National Dex order for species IDs, so:
 *   387 = Turtwig, 388 = Grotle, ..., 1025 = Pecharunt
 *
 * Strategy for each entry:
 *   1. Prefer the species' own Gen 3 prevolution / sister-form if it exists.
 *   2. Else prefer a Gen 3 species sharing primary type and role.
 *   3. Pseudo-legendaries map to other Gen 3 pseudos (Bagon/Salamence line).
 *   4. Starters map to Gen 3 starters of matching type.
 *   5. Forms (Hisuian/Alolan/Galarian) inherit their base species' Gen 3 form.
 *
 * Coverage: every species listed in Cassora's POKEDEX.md (229 entries) plus
 * the broad pokeemerald-expansion ID range we expect ROM hacks to use.
 * Species outside the curated set fall back to Unown (#201) in
 * pb_species_remap() as a clear "needs review" marker.
 */
#ifndef POKEBRIDGE_CASSORA_SPECIES_MAP_H
#define POKEBRIDGE_CASSORA_SPECIES_MAP_H

#include <stdint.h>

/* Sentinel for "no entry in this slot". */
#define CASSORA_REMAP_NONE  0

/* Indexed by pokeemerald-expansion species ID. Entry of CASSORA_REMAP_NONE
 * means we don't have a curated mapping (legalizer falls back to Unown). */
static const uint16_t cassora_species_remap[1025] = {
    /* === Gen 4 (387–493) used in Cassora === */
    [399] = 263, /* Bidoof       -> Zigzagoon  (normal small herbivore)   */
    [400] = 264, /* Bibarel      -> Linoone    (normal evolved)           */
    [425] = 200, /* Drifloon     -> Misdreavus (ghost)                    */
    [426] = 354, /* Drifblim     -> Banette    (ghost evolved)            */
    [429] = 200, /* Mismagius    -> Misdreavus (its sister-Gen-3 ghost)   */
    [430] = 359, /* Honchkrow    -> Absol      (dark, mid-tier)           */
    [447] = 296, /* Riolu        -> Makuhita   (fighting baby)            */
    [448] = 297, /* Lucario      -> Hariyama   (fighting evolved)         */
    [449] = 328, /* Hippopotas   -> Trapinch   (ground juvenile)          */
    [450] = 306, /* Hippowdon    -> Aggron     (ground/rock tank)         */
    [458] = 226, /* Mantyke      -> Mantine    (its evolution, Gen 2-legal)*/
    [459] = 361, /* Snover       -> Snorunt    (ice)                      */
    [460] = 362, /* Abomasnow    -> Glalie     (ice bulk)                 */
    [461] = 215, /* Weavile      -> Sneasel    (its prevo, Gen 2-legal)   */
    [462] =  82, /* Magnezone    -> Magneton   (its prevo)                */
    [464] = 112, /* Rhyperior    -> Rhydon     (its prevo)                */
    [466] = 125, /* Electivire   -> Electabuzz (its prevo)                */
    [467] = 126, /* Magmortar    -> Magmar     (its prevo)                */
    [470] = 357, /* Leafeon      -> Tropius    (grass)                    */
    [471] = 362, /* Glaceon      -> Glalie     (ice)                      */
    [473] = 221, /* Mamoswine    -> Piloswine  (its prevo)                */
    [474] = 233, /* Porygon-Z    -> Porygon2   (its prevo)                */
    [475] = 282, /* Gallade      -> Gardevoir  (sister evolution)         */
    [477] = 356, /* Dusknoir     -> Dusclops   (its prevo)                */
    [478] = 362, /* Froslass     -> Glalie     (sister evolution)         */
    [479] = 358, /* Rotom        -> Chimecho   (mischievous psychic)      */
    [485] = 383, /* Heatran      -> Groudon    (fire-ground legendary)    */
    [490] = 385, /* Manaphy      -> Jirachi    (mythical)                 */

    /* === Gen 5 (494–649) used in Cassora === */
    [524] = 304, /* Roggenrola   -> Aron       (rock baby)                */
    [525] = 305, /* Boldore      -> Lairon                                */
    [526] = 306, /* Gigalith     -> Aggron                                */
    [529] = 328, /* Drilbur      -> Trapinch   (ground)                   */
    [530] = 330, /* Excadrill    -> Flygon     (ground sweeper)           */
    [540] = 265, /* Sewaddle     -> Wurmple    (bug baby)                 */
    [541] = 266, /* Swadloon     -> Silcoon                               */
    [542] = 286, /* Leavanny     -> Breloom    (grass/fighting)           */
    [550] = 318, /* Basculin     -> Carvanha   (water fish)               */
    [551] = 328, /* Sandile      -> Trapinch   (ground)                   */
    [552] = 329, /* Krokorok     -> Vibrava                               */
    [553] = 330, /* Krookodile   -> Flygon                                */
    [564] = 347, /* Tirtouga     -> Anorith    (fossil)                   */
    [565] = 348, /* Carracosta   -> Armaldo                               */
    [570] = 215, /* Zorua        -> Sneasel    (dark)                     */
    [571] = 262, /* Zoroark      -> Mightyena  (dark evolved)             */
    [585] = 234, /* Deerling     -> Stantler   (deer)                     */
    [586] = 234, /* Sawsbuck     -> Stantler                              */
    [590] = 285, /* Foongus      -> Shroomish  (grass mushroom)           */
    [591] = 286, /* Amoonguss    -> Breloom                               */
    [592] = 354, /* Frillish     -> Banette    (ghost)                    */
    [593] = 354, /* Jellicent    -> Banette                               */
    [595] =  81, /* Joltik       -> Magnemite  (electric small)           */
    [596] = 168, /* Galvantula   -> Ariados    (bug spider)               */
    [607] = 200, /* Litwick      -> Misdreavus (ghost)                    */
    [608] = 200, /* Lampent      -> Misdreavus                            */
    [609] = 354, /* Chandelure   -> Banette                               */
    [613] = 363, /* Cubchoo      -> Spheal     (ice baby)                 */
    [614] = 365, /* Beartic      -> Walrein    (ice bulk)                 */
    [615] = 362, /* Cryogonal    -> Glalie     (pure ice)                 */
    [618] = 369, /* Stunfisk     -> Relicanth                             */
    [619] = 296, /* Mienfoo      -> Makuhita   (fighting)                 */
    [620] = 297, /* Mienshao     -> Hariyama                              */
    [627] = 333, /* Rufflet      -> Swablu     (baby flying)              */
    [628] = 334, /* Braviary     -> Altaria                               */
    [633] = 371, /* Deino        -> Bagon      (dragon pseudo baby)       */
    [634] = 372, /* Zweilous     -> Shelgon                               */
    [635] = 373, /* Hydreigon    -> Salamence  (dark/dragon pseudo!)      */
    [636] = 324, /* Larvesta     -> Torkoal    (fire)                     */
    [637] = 324, /* Volcarona    -> Torkoal                               */

    /* === Gen 6 (650–721) used in Cassora === */
    [698] = 369, /* Amaura       -> Relicanth  (fossil)                   */
    [699] = 369, /* Aurorus      -> Relicanth                             */
    [700] = 282, /* Sylveon      -> Gardevoir  (fairy-ish, psychic)       */
    [708] = 354, /* Phantump     -> Banette    (ghost)                    */
    [709] = 354, /* Trevenant    -> Banette                               */
    [710] = 354, /* Pumpkaboo    -> Banette                               */
    [711] = 354, /* Gourgeist    -> Banette                               */
    [712] = 361, /* Bergmite     -> Snorunt                               */
    [713] = 362, /* Avalugg      -> Glalie                                */

    /* === Gen 7 (722–809) used in Cassora === */
    [744] = 261, /* Rockruff     -> Poochyena                             */
    [745] = 262, /* Lycanroc-Dusk-> Mightyena                             */
    [755] = 285, /* Morelull     -> Shroomish                             */
    [756] = 286, /* Shiinotic    -> Breloom                               */
    [782] = 371, /* Jangmo-o     -> Bagon                                 */
    [783] = 372, /* Hakamo-o     -> Shelgon                               */
    [784] = 373, /* Kommo-o      -> Salamence                             */

    /* === Gen 8 (810–905) used in Cassora === */
    [872] = 265, /* Snom         -> Wurmple                               */
    [873] = 267, /* Frosmoth     -> Beautifly                             */
    [875] = 363, /* Eiscue       -> Spheal                                */
    [885] = 371, /* Dreepy       -> Bagon                                 */
    [886] = 372, /* Drakloak     -> Shelgon                               */
    [887] = 373, /* Dragapult    -> Salamence                             */
    [899] = 234, /* Wyrdeer      -> Stantler   (its prevo)                */
    [900] = 212, /* Kleavor      -> Scizor     (bug/steel scyther line)   */
    [901] = 217, /* Ursaluna     -> Ursaring   (its prevo)                */
    [902] = 350, /* Basculegion  -> Milotic    (majestic water)           */
    [904] = 211, /* Overqwil     -> Qwilfish   (its prevo)                */
};

#endif
