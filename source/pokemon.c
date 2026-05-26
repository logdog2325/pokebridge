#include "pokemon.h"
#include "endian_le.h"
#include <string.h>

/* PID % 24 → substructure order. Each entry lists the substructure type
 * (G=0, A=1, E=2, M=3) in physical position 0..3 inside the encrypted blob. */
static const uint8_t order_table[24][4] = {
    {0,1,2,3}, {0,1,3,2}, {0,2,1,3}, {0,3,1,2},
    {0,2,3,1}, {0,3,2,1}, {1,0,2,3}, {1,0,3,2},
    {2,0,1,3}, {3,0,1,2}, {2,0,3,1}, {3,0,2,1},
    {1,2,0,3}, {1,3,0,2}, {2,1,0,3}, {3,1,0,2},
    {2,3,0,1}, {3,2,0,1}, {1,2,3,0}, {1,3,2,0},
    {2,1,3,0}, {3,1,2,0}, {2,3,1,0}, {3,2,1,0},
};

static void decrypt_block(uint8_t *dst, const uint8_t *src, uint32_t key) {
    /* 48 bytes XORed with the 4-byte LE key pattern. Byte-wise so endianness
     * of the host is irrelevant -- the key bytes match the save's LE byte
     * order, which is what the crypto spec demands. */
    uint8_t kb[4] = {
        (uint8_t)(key      ),
        (uint8_t)(key >>  8),
        (uint8_t)(key >> 16),
        (uint8_t)(key >> 24),
    };
    for (int i = 0; i < 48; i++) dst[i] = src[i] ^ kb[i & 3];
}

static uint16_t checksum16(const uint8_t *decrypted_48) {
    /* Sum 24 little-endian 16-bit words, reduce to 16 bits. */
    uint32_t sum = 0;
    for (int i = 0; i < 48; i += 2) sum += pb_rd_u16le(decrypted_48 + i);
    return (uint16_t)(sum & 0xFFFF);
}

bool pb_pkm_decode(pb_pkm_t *out, const uint8_t *raw) {
    memset(out, 0, sizeof *out);
    /* Detect empty slot: PID and OT_ID both zero AND species zero after best-
     * effort decrypt is the cleanest test, but a cheaper one: first 16 bytes
     * all zero. */
    bool all_zero = true;
    for (int i = 0; i < 32; i++) if (raw[i]) { all_zero = false; break; }
    if (all_zero) { out->is_empty = true; return true; }

    out->pid       = pb_rd_u32le(raw + 0);
    out->ot_id     = pb_rd_u32le(raw + 4);
    memcpy(out->nickname, raw + 8, 10);
    out->language  = raw[18];
    out->egg_flag  = raw[19];
    memcpy(out->ot_name, raw + 20, 7);
    out->markings  = raw[27];
    out->checksum  = pb_rd_u16le(raw + 28);
    out->_unused   = pb_rd_u16le(raw + 30);

    uint32_t key = out->pid ^ out->ot_id;
    uint8_t decrypted[48];
    decrypt_block(decrypted, raw + 32, key);

    out->checksum_ok = (checksum16(decrypted) == out->checksum);

    const uint8_t *order = order_table[out->pid % 24];
    for (int slot = 0; slot < 4; slot++) {
        const uint8_t *sub = decrypted + slot * 12;
        switch (order[slot]) {
            case 0: /* Growth */
                out->g.species    = pb_rd_u16le(sub + 0);
                out->g.held_item  = pb_rd_u16le(sub + 2);
                out->g.experience = pb_rd_u32le(sub + 4);
                out->g.pp_bonuses = sub[8];
                out->g.friendship = sub[9];
                out->g._unused    = pb_rd_u16le(sub + 10);
                break;
            case 1: /* Attacks */
                for (int i = 0; i < 4; i++)
                    out->a.moves[i] = pb_rd_u16le(sub + i * 2);
                memcpy(out->a.pp, sub + 8, 4);
                break;
            case 2: /* EVs */
                memcpy(out->e.ev, sub, 6);
                memcpy(out->e.condition, sub + 6, 5);
                out->e._unused[0] = sub[11];
                break;
            case 3: /* Misc */
                out->m.pokerus        = sub[0];
                out->m.met_loc        = sub[1];
                out->m.origins        = pb_rd_u16le(sub + 2);
                out->m.iv_egg_ability = pb_rd_u32le(sub + 4);
                out->m.ribbons        = pb_rd_u32le(sub + 8);
                break;
        }
    }
    return true;
}

void pb_pkm_ivs(const pb_pkm_t *p, uint8_t ivs[6]) {
    uint32_t v = p->m.iv_egg_ability;
    ivs[0] = (v >>  0) & 0x1F; /* HP  */
    ivs[1] = (v >>  5) & 0x1F; /* ATK */
    ivs[2] = (v >> 10) & 0x1F; /* DEF */
    ivs[3] = (v >> 15) & 0x1F; /* SPE */
    ivs[4] = (v >> 20) & 0x1F; /* SPA */
    ivs[5] = (v >> 25) & 0x1F; /* SPD */
}

void pb_pkm_encode(const pb_pkm_t *p, uint8_t raw_out[80]) {
    /* Reassemble the 48-byte decrypted block in the original substructure order,
     * then XOR with PID^OT_ID and write to raw_out[32..79]. */
    uint8_t decrypted[48] = {0};
    const uint8_t *order = order_table[p->pid % 24];
    for (int slot = 0; slot < 4; slot++) {
        uint8_t *sub = decrypted + slot * 12;
        switch (order[slot]) {
            case 0:
                pb_wr_u16le(sub + 0, p->g.species);
                pb_wr_u16le(sub + 2, p->g.held_item);
                pb_wr_u32le(sub + 4, p->g.experience);
                sub[8] = p->g.pp_bonuses;
                sub[9] = p->g.friendship;
                pb_wr_u16le(sub + 10, p->g._unused);
                break;
            case 1:
                for (int i = 0; i < 4; i++)
                    pb_wr_u16le(sub + i * 2, p->a.moves[i]);
                memcpy(sub + 8, p->a.pp, 4);
                break;
            case 2:
                memcpy(sub, p->e.ev, 6);
                memcpy(sub + 6, p->e.condition, 5);
                sub[11] = p->e._unused[0];
                break;
            case 3:
                sub[0] = p->m.pokerus;
                sub[1] = p->m.met_loc;
                pb_wr_u16le(sub + 2, p->m.origins);
                pb_wr_u32le(sub + 4, p->m.iv_egg_ability);
                pb_wr_u32le(sub + 8, p->m.ribbons);
                break;
        }
    }
    uint16_t cksum = checksum16(decrypted);
    pb_wr_u32le(raw_out + 0, p->pid);
    pb_wr_u32le(raw_out + 4, p->ot_id);
    memcpy(raw_out + 8, p->nickname, 10);
    raw_out[18] = p->language;
    raw_out[19] = p->egg_flag;
    memcpy(raw_out + 20, p->ot_name, 7);
    raw_out[27] = p->markings;
    pb_wr_u16le(raw_out + 28, cksum);
    pb_wr_u16le(raw_out + 30, p->_unused);

    /* Encrypt: byte-wise XOR with the 4-byte LE key pattern. */
    uint32_t key = p->pid ^ p->ot_id;
    uint8_t kb[4] = {
        (uint8_t)(key      ),
        (uint8_t)(key >>  8),
        (uint8_t)(key >> 16),
        (uint8_t)(key >> 24),
    };
    for (int i = 0; i < 48; i++) raw_out[32 + i] = decrypted[i] ^ kb[i & 3];
}

/* Gen 3 charset decode mirror of save.c's table — duplicated here so other
 * modules can call it without pulling in save.c. Kept simple to limit churn. */
void pb_gen3_to_ascii(const uint8_t *in, int len, char *out_buf) {
    static char tbl[256];
    static bool built = false;
    if (!built) {
        memset(tbl, '?', sizeof tbl);
        tbl[0x00] = ' ';
        for (int i = 0; i < 10; i++) tbl[0xA1 + i] = '0' + i;
        for (int i = 0; i < 26; i++) tbl[0xBB + i] = 'A' + i;
        for (int i = 0; i < 26; i++) tbl[0xD5 + i] = 'a' + i;
        tbl[0xB6] = '.';
        tbl[0xB7] = '-';
        tbl[0xAB] = '!';
        tbl[0xAC] = '?';
        tbl[0xFF] = '\0';
        built = true;
    }
    int j = 0;
    for (int i = 0; i < len; i++) {
        uint8_t b = in[i];
        if (b == 0xFF) break;
        out_buf[j++] = tbl[b];
    }
    out_buf[j] = 0;
}

#include "species_names_gen3.h"

const char *pb_species_name(uint16_t species) {
    if (species > 386) return "Hack-mon";
    if (species < (sizeof pb_species_names_gen3 / sizeof pb_species_names_gen3[0])
        && pb_species_names_gen3[species]) {
        return pb_species_names_gen3[species];
    }
    return "???";
}

#include "move_names_gen3.h"

const char *pb_move_name(uint16_t move_id) {
    if (move_id == 0) return "-";
    if (move_id > 354) return "???";
    if (pb_move_names_gen3[move_id]) return pb_move_names_gen3[move_id];
    return "???";
}

/* --- Editor helpers --- */

uint8_t pb_pkm_get_iv(const pb_pkm_t *p, int stat) {
    if (stat < 0 || stat > 5) return 0;
    return (p->m.iv_egg_ability >> (stat * 5)) & 0x1F;
}

void pb_pkm_set_iv(pb_pkm_t *p, int stat, uint8_t value) {
    if (stat < 0 || stat > 5) return;
    if (value > 31) value = 31;
    uint32_t mask = 0x1FU << (stat * 5);
    p->m.iv_egg_ability = (p->m.iv_egg_ability & ~mask)
                        | ((uint32_t)value << (stat * 5));
}

uint8_t pb_pkm_get_ev(const pb_pkm_t *p, int stat) {
    if (stat < 0 || stat > 5) return 0;
    return p->e.ev[stat];
}

void pb_pkm_set_ev(pb_pkm_t *p, int stat, uint8_t value) {
    if (stat < 0 || stat > 5) return;
    p->e.ev[stat] = value;
}

void pb_pkm_set_move(pb_pkm_t *p, int slot, uint16_t move_id) {
    if (slot < 0 || slot > 3) return;
    p->a.moves[slot] = move_id;
    /* Default PP -- the game will recompute on heal. 20 covers most Gen 3 moves. */
    p->a.pp[slot] = move_id ? 20 : 0;
}

bool pb_pkm_is_shiny(const pb_pkm_t *p) {
    uint16_t tid = (uint16_t)(p->ot_id & 0xFFFF);
    uint16_t sid = (uint16_t)((p->ot_id >> 16) & 0xFFFF);
    uint16_t ph  = (uint16_t)((p->pid >> 16) & 0xFFFF);
    uint16_t pl  = (uint16_t)(p->pid & 0xFFFF);
    return (uint16_t)(tid ^ sid ^ ph ^ pl) < 8;
}

uint32_t pb_pkm_toggle_shiny(pb_pkm_t *p, bool want_shiny) {
    uint16_t tid = (uint16_t)(p->ot_id & 0xFFFF);
    uint16_t sid = (uint16_t)((p->ot_id >> 16) & 0xFFFF);
    uint8_t want_nature = pb_pkm_get_nature(p);
    uint32_t seed = p->pid ? p->pid : 0xCAFEBABE;
    for (int iter = 0; iter < 1000000; iter++) {
        seed = seed * 1103515245u + 12345u;
        if (seed == 0) continue;
        if (seed % 25 != want_nature) continue;  /* preserve nature */
        uint16_t ph = (uint16_t)((seed >> 16) & 0xFFFF);
        uint16_t pl = (uint16_t)(seed & 0xFFFF);
        bool shiny = (uint16_t)(tid ^ sid ^ ph ^ pl) < 8;
        if (shiny == want_shiny) {
            p->pid = seed;
            return seed;
        }
    }
    return p->pid;
}

uint8_t pb_pkm_get_nature(const pb_pkm_t *p) {
    return (uint8_t)(p->pid % 25);
}

void pb_pkm_set_nature(pb_pkm_t *p, uint8_t nature_id) {
    if (nature_id >= 25) return;
    if (pb_pkm_get_nature(p) == nature_id) return;
    uint16_t tid = (uint16_t)(p->ot_id & 0xFFFF);
    uint16_t sid = (uint16_t)((p->ot_id >> 16) & 0xFFFF);
    bool want_shiny = pb_pkm_is_shiny(p);
    uint32_t seed = p->pid ? p->pid : 0xCAFEBABE;
    for (int iter = 0; iter < 1000000; iter++) {
        seed = seed * 1103515245u + 12345u;
        if (seed == 0) continue;
        if (seed % 25 != nature_id) continue;
        uint16_t ph = (uint16_t)((seed >> 16) & 0xFFFF);
        uint16_t pl = (uint16_t)(seed & 0xFFFF);
        bool shiny = (uint16_t)(tid ^ sid ^ ph ^ pl) < 8;
        if (shiny == want_shiny) {
            p->pid = seed;
            return;
        }
    }
}

static const char *nature_names[25] = {
    "Hardy",   "Lonely",  "Brave",   "Adamant", "Naughty",
    "Bold",    "Docile",  "Relaxed", "Impish",  "Lax",
    "Timid",   "Hasty",   "Serious", "Jolly",   "Naive",
    "Modest",  "Mild",    "Quiet",   "Bashful", "Rash",
    "Calm",    "Gentle",  "Sassy",   "Careful", "Quirky",
};

const char *pb_nature_name(uint8_t nature_id) {
    if (nature_id >= 25) return "???";
    return nature_names[nature_id];
}
