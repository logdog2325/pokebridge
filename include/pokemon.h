/* Gen 3 Pokémon substructure decoder.
 *
 * Each pokemon record is 80 bytes (boxed) or 100 bytes (party, adds 20 bytes
 * of derived stats at the end). Bytes 0–31 are plaintext header. Bytes 32–79
 * hold four 12-byte substructures (G/A/E/M) in one of 24 orderings, encrypted
 * by XORing each 32-bit word with (PID ^ OT_ID).
 *
 * Substructure ordering is determined by (PID % 24). See pb_pkm_order_table.
 *
 *   G (Growth)  : u16 species, u16 held_item, u32 experience, u8 pp_bonuses,
 *                 u8 friendship, u16 unused
 *   A (Attacks) : u16 moves[4], u8 pp[4]
 *   E (EVs)     : u8 ev_hp,atk,def,spe,spa,spd, u8 condition[5]
 *   M (Misc)    : u8 pokerus, u8 met_loc, u16 origins (level/game/ball/ot_gender),
 *                 u32 ivs_egg_ability (bitpacked), u32 ribbons_obedience
 */
#ifndef POKEBRIDGE_POKEMON_H
#define POKEBRIDGE_POKEMON_H

#include <stdint.h>
#include <stdbool.h>

#define PB_PKM_BOX_SIZE   80
#define PB_PKM_PARTY_SIZE 100

typedef struct {
    uint16_t species;
    uint16_t held_item;
    uint32_t experience;
    uint8_t  pp_bonuses;
    uint8_t  friendship;
    uint16_t _unused;
} pb_substr_growth_t;

typedef struct {
    uint16_t moves[4];
    uint8_t  pp[4];
} pb_substr_attacks_t;

typedef struct {
    uint8_t ev[6];        /* HP, ATK, DEF, SPE, SPA, SPD */
    uint8_t condition[5]; /* cool, beauty, cute, smart, tough */
    uint8_t _unused[1];
} pb_substr_evs_t;

typedef struct {
    uint8_t  pokerus;
    uint8_t  met_loc;
    uint16_t origins;        /* bits 0–6: level met, 7–10: game, 11–14: ball, 15: OT gender */
    uint32_t iv_egg_ability; /* bits 0–4 HP, 5–9 ATK, 10–14 DEF, 15–19 SPE, 20–24 SPA, 25–29 SPD, 30 egg, 31 ability */
    uint32_t ribbons;
} pb_substr_misc_t;

typedef struct {
    /* Plaintext header */
    uint32_t pid;
    uint32_t ot_id;          /* low 16 = TID, high 16 = SID */
    uint8_t  nickname[10];   /* Gen 3 charset (NOT ASCII) */
    uint8_t  language;
    uint8_t  egg_flag;       /* and other flags */
    uint8_t  ot_name[7];     /* Gen 3 charset */
    uint8_t  markings;
    uint16_t checksum;
    uint16_t _unused;
    /* Decrypted substructures */
    pb_substr_growth_t  g;
    pb_substr_attacks_t a;
    pb_substr_evs_t     e;
    pb_substr_misc_t    m;
    /* Validity */
    bool checksum_ok;
    bool is_empty;           /* All zeros → empty slot */
} pb_pkm_t;

/* Decode an 80-byte boxed pokemon record. */
bool pb_pkm_decode(pb_pkm_t *out, const uint8_t *raw);

/* Convenience: IVs as a 6-byte array, decoded from the bitpacked m.iv_egg_ability. */
void pb_pkm_ivs(const pb_pkm_t *p, uint8_t ivs[6]);

/* Convert a Gen 3 charset string (nickname/OT) to ASCII. Unknown bytes → '?'.
 * out_buf must be at least len+1 bytes. */
void pb_gen3_to_ascii(const uint8_t *in, int len, char *out_buf);

/* Inverse: encode an ASCII string into the Gen 3 charset, padding the
 * fixed-length output with 0xFF (terminator). Used by the nickname
 * editor to write back into pb_pkm_t.nickname or .ot_name. */
void pb_ascii_to_gen3(const char *in, uint8_t *out, int dst_len);

/* Re-encrypt a pokemon record after edits. Recomputes the checksum and writes
 * back to the 80-byte buffer in the original substructure order. */
void pb_pkm_encode(const pb_pkm_t *p, uint8_t raw_out[80]);

/* Look up a Gen 3 official species name (#1..#386). Returns "???" for invalid IDs. */
const char *pb_species_name(uint16_t species);

/* Look up a Gen 3 move name (#1..#354). Returns "???" for invalid IDs. */
const char *pb_move_name(uint16_t move_id);

/* Look up a Gen 3 hold-item name. Returns "(none)" for 0, "(other)"
 * for IDs not in our curated table (mostly key items / TMs / Pokéballs
 * that don't make sense to hold). */
const char *pb_item_name(uint16_t item_id);

/* --- Editor helpers --- */

/* Get/set one IV (0..5 = HP, Atk, Def, Spe, SpA, SpD). Values clamp to [0,31]. */
uint8_t pb_pkm_get_iv(const pb_pkm_t *p, int stat);
void    pb_pkm_set_iv(pb_pkm_t *p, int stat, uint8_t value);

/* Get/set one EV (same stat order). Values clamp to [0,255]; Gen 3 rules cap
 * single-stat at 252 and total at 510, but we don't enforce -- the user can
 * decide. */
uint8_t pb_pkm_get_ev(const pb_pkm_t *p, int stat);
void    pb_pkm_set_ev(pb_pkm_t *p, int stat, uint8_t value);

/* Replace one of the 4 move slots. Sets PP to a sane default (20) since we
 * don't carry a Gen 3 base-PP table. The game's healing NPCs will normalize. */
void pb_pkm_set_move(pb_pkm_t *p, int slot, uint16_t move_id);

/* Gen 3 shininess: (TID ^ SID ^ PID_high ^ PID_low) < 8. */
bool pb_pkm_is_shiny(const pb_pkm_t *p);

/* Re-roll the PID until shininess matches `want_shiny`, preserving the
 * current nature. Gender and ability slot may change. Returns new PID. */
uint32_t pb_pkm_toggle_shiny(pb_pkm_t *p, bool want_shiny);

/* Nature: 0..24. PID % 25. */
uint8_t pb_pkm_get_nature(const pb_pkm_t *p);

/* Re-roll PID until nature == `nature_id` (0..24), preserving the current
 * shiny status. Gender and ability slot may change. */
void pb_pkm_set_nature(pb_pkm_t *p, uint8_t nature_id);

/* Look up a Gen 3 nature name (0..24). */
const char *pb_nature_name(uint8_t nature_id);

#endif
