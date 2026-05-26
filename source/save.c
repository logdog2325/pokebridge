#include "save.h"
#include "pokemon.h"
#include "endian_le.h"
#include <string.h>
#include <stdio.h>

static uint16_t section_checksum(const uint8_t *sec) {
    /* Each section's checksum is the sum of its first N words (N depends on
     * section id), reduced to 16 bits by adding low+high halves. */
    static const uint16_t bytes_to_sum[PB_SECTION_COUNT] = {
        3884, 3968, 3968, 3968, 3848, 3968, 3968,
        3968, 3968, 3968, 3968, 3968, 3968, 2000
    };
    uint16_t section_id = pb_rd_u16le(sec + PB_SECTION_FOOTER + 0);
    if (section_id >= PB_SECTION_COUNT) return 0;
    uint32_t sum = 0;
    int n = bytes_to_sum[section_id];
    for (int i = 0; i < n; i += 4) sum += pb_rd_u32le(sec + i);
    return (uint16_t)((sum & 0xFFFF) + (sum >> 16));
}

/* Parse one slot's 14 physical sectors into the caller-supplied sec[] pointer
 * array, sorted by section_id. Returns true on success.
 * sec_out must be uint8_t*[PB_SECTION_COUNT]. */
static bool parse_slot(uint8_t *sec_out[PB_SECTION_COUNT],
                       const uint8_t *slot_bytes,
                       uint32_t *save_idx_out) {
    uint8_t *ordered[PB_SECTION_COUNT] = {0};
    uint32_t save_idx = 0;
    bool got_idx = false;
    uint8_t *base = (uint8_t *)slot_bytes;
    for (int phys = 0; phys < PB_SECTION_COUNT; phys++) {
        uint8_t *sec = base + (phys * PB_SECTION_SIZE);
        uint32_t sig = pb_rd_u32le(sec + PB_SECTION_FOOTER + 4);
        if (sig != PB_SECTION_SIG) return false;
        uint16_t id = pb_rd_u16le(sec + PB_SECTION_FOOTER + 0);
        if (id >= PB_SECTION_COUNT) return false;
        if (ordered[id]) return false;
        ordered[id] = sec;
        uint32_t idx = pb_rd_u32le(sec + PB_SECTION_FOOTER + 8);
        if (!got_idx) { save_idx = idx; got_idx = true; }
        else if (idx != save_idx) {
            if (idx < save_idx) save_idx = idx;
        }
    }
    for (int i = 0; i < PB_SECTION_COUNT; i++) {
        if (!ordered[i]) return false;
        sec_out[i] = ordered[i];
    }
    *save_idx_out = save_idx;
    return true;
}

static pb_game_t identify_game(pb_save_t *s) {
    /* Section 0, offset 0xAC: u32 game_code. 0 = RS, 1 = FRLG. Emerald keeps
     * 0 there but has a non-zero security_key at offset 0xAC of section 0 in
     * a different field — we use the conventional approach: read security_key
     * from section 0 at offset 0xAC (Emerald) vs game_code from same offset
     * (FRLG). Robustly distinguish by checking sec[0][0xAC..0xAF]:
     *   RS       : 0x00000000
     *   FRLG     : 0x00000001
     *   Emerald  : a non-trivial 32-bit security key. Practically: if the
     *              value is != 0 and != 1 it's Emerald.
     * ROM hacks based on pokeemerald-expansion keep Emerald's layout but may
     * change the game title at sec[0][0]; we treat any "Emerald-shaped" save
     * whose game_code looks Emerald as PB_GAME_HACK if title doesn't match. */
    uint32_t v = pb_rd_u32le(s->sec[0] + 0xAC);
    if (v == 0)       s->game = PB_GAME_RS;
    else if (v == 1)  s->game = PB_GAME_FRLG;
    else              s->game = PB_GAME_EMERALD;

    s->security_key = (s->game == PB_GAME_EMERALD) ? v
                     : (s->game == PB_GAME_FRLG)   ? pb_rd_u32le(s->sec[0] + 0xAF8)
                                                   : 0;
    return s->game;
}

/* Gen 3 charset (subset). 0x00–0x9F are letters/numbers/punct in a custom
 * arrangement. 0xFF terminates a string. This is the official Western table;
 * Japanese saves use a different table — we don't decode those here. */
static const char gen3_charset[0x100] = {
    /* 0x00 */ ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x10 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x20 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x30 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x40 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x50 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x60 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x70 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x80 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x90 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0xA0 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0xB0 */ 0,   0,   0,   0,   0,   0,   '.', '-', 0,   0,   '?', '!', 0,   0,   0,   0,
    /* 0xC0 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0xD0 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0xE0 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0xF0 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

/* Initialize the alphanumeric block (decimal digits at 0xA1-0xAA, A-Z at 0xBB-0xD4,
 * a-z at 0xD5-0xEE). Done lazily on first decode to keep the table above readable. */
static char gen3_table[256];
static bool gen3_table_built = false;
static void build_gen3_table(void) {
    if (gen3_table_built) return;
    memset(gen3_table, '?', sizeof gen3_table);
    gen3_table[0x00] = ' ';
    for (int i = 0; i < 10; i++) gen3_table[0xA1 + i] = '0' + i;
    for (int i = 0; i < 26; i++) gen3_table[0xBB + i] = 'A' + i;
    for (int i = 0; i < 26; i++) gen3_table[0xD5 + i] = 'a' + i;
    gen3_table[0xB6] = '.';
    gen3_table[0xB7] = '-';
    gen3_table[0xBA] = '?';
    gen3_table[0xAB] = '!';
    gen3_table[0xAC] = '?';
    gen3_table[0xFF] = '\0';
    gen3_table_built = true;
    (void)gen3_charset; /* silence unused */
}

static void decode_gen3(const uint8_t *in, int max_len, char *out) {
    build_gen3_table();
    int j = 0;
    for (int i = 0; i < max_len; i++) {
        uint8_t b = in[i];
        if (b == 0xFF) break;
        char c = gen3_table[b];
        out[j++] = c ? c : '?';
    }
    out[j] = 0;
}

static void extract_trainer(pb_save_t *s) {
    /* Section 0 starts with: 7 bytes name (Gen 3 charset), 1 byte gender,
     * 1 byte unused, 1 byte ??, 4 bytes player TID/SID, then time played. */
    decode_gen3(s->sec[0], 7, s->trainer_name);
    s->gender    = s->sec[0][0x08];
    s->trainer_id = pb_rd_u16le(s->sec[0] + 0x0A);
    s->secret_id  = pb_rd_u16le(s->sec[0] + 0x0C);
}

bool pb_save_load(pb_save_t *out, const uint8_t *data, size_t len) {
    if (!out || !data) return false;
    if (len < PB_SLOT_SIZE) return false;
    memset(out, 0, sizeof *out);
    memcpy(out->raw, data, len < PB_SAVE_SIZE ? len : PB_SAVE_SIZE);

    /* sec_a/sec_b are 14 pointers each (~112 bytes), stack-cheap. */
    uint8_t *sec_a[PB_SECTION_COUNT] = {0};
    uint8_t *sec_b[PB_SECTION_COUNT] = {0};
    uint32_t idx_a = 0, idx_b = 0;
    bool ok_a = parse_slot(sec_a, out->raw, &idx_a);
    bool ok_b = false;
    if (len >= 2 * PB_SLOT_SIZE) {
        ok_b = parse_slot(sec_b, out->raw + PB_SLOT_SIZE, &idx_b);
    }

    if (!ok_a && !ok_b) return false;
    bool a_wins;
    if (ok_a && ok_b) a_wins = (idx_a >= idx_b);
    else              a_wins = ok_a;
    out->save_index = a_wins ? idx_a : idx_b;
    memcpy(out->sec, a_wins ? sec_a : sec_b, sizeof out->sec);

    /* Verify checksums on every section — a bad checksum on Trainer Info is
     * fatal because we can't identify the game. Other sections are advisory. */
    uint16_t stored = pb_rd_u16le(out->sec[0] + PB_SECTION_FOOTER + 2);
    uint16_t computed = section_checksum(out->sec[0]);
    if (stored != computed) return false;

    identify_game(out);
    extract_trainer(out);
    return true;
}

bool pb_save_load_file(pb_save_t *out, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    static uint8_t buf[PB_SAVE_SIZE];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return pb_save_load(out, buf, n);
}

bool pb_save_party(const pb_save_t *s, uint8_t *out_count, const uint8_t **out_party) {
    if (!s || !s->sec[1]) return false;
    /* Section 1 (Team/Items) layout differs slightly between RS / FRLG /
     * Emerald due to bag-size differences. Party count + party array offset
     * are stable per game-type. */
    uint32_t party_count_off;
    uint32_t party_array_off;
    switch (s->game) {
        case PB_GAME_RS:      party_count_off = 0x0234; party_array_off = 0x0238; break;
        case PB_GAME_EMERALD: party_count_off = 0x0234; party_array_off = 0x0238; break;
        case PB_GAME_FRLG:    party_count_off = 0x0034; party_array_off = 0x0038; break;
        case PB_GAME_HACK:    party_count_off = 0x0234; party_array_off = 0x0238; break;
        default: return false;
    }
    *out_count = s->sec[1][party_count_off];
    if (*out_count > 6) *out_count = 6;
    *out_party = s->sec[1] + party_array_off;
    return true;
}

const uint8_t *pb_save_box(const pb_save_t *s, int box_index) {
    if (!s || box_index < 0 || box_index >= 14) return NULL;
    /* PC storage is sections 5–13 concatenated. Total PC blob is 33,744 bytes:
     *   4 (current box) + 14*30*80 (pokemon) + 14*9 (names) + 14 (wallpapers).
     * The first 4 bytes are the active-box index. Each box is 30 mons × 80 bytes
     * = 2400 bytes. Box i starts at PC + 4 + i*2400. */
    static uint8_t pc_flat[33744];
    /* Reassemble: sections 5–12 give 3968 usable bytes each, section 13 gives 2000. */
    static const int sec_bytes[9] = { 3968, 3968, 3968, 3968, 3968, 3968, 3968, 3968, 2000 };
    int off = 0;
    for (int i = 0; i < 9; i++) {
        memcpy(pc_flat + off, s->sec[5 + i], sec_bytes[i]);
        off += sec_bytes[i];
    }
    return pc_flat + 4 + box_index * (30 * PB_PKM_BOX_SIZE);
}

bool pb_save_box_write_slot(pb_save_t *s, int box_index, int slot,
                            const uint8_t raw80[80]) {
    if (!s || box_index < 0 || box_index >= 14 || slot < 0 || slot >= 30)
        return false;
    /* PC storage is a flat blob distributed across sections 5..13. The
     * usable byte count per section is: sections 5..12 = 3968, section 13
     * = 2000. The first 4 bytes hold the active-box index; each box is
     * 30 mons × 80 bytes = 2400 bytes; so slot bytes start at:
     *   flat = 4 + box*2400 + slot*80                                       */
    static const int sec_bytes[9] = {
        3968, 3968, 3968, 3968, 3968, 3968, 3968, 3968, 2000
    };
    int flat = 4 + box_index * 2400 + slot * 80;
    int written = 0;
    int acc = 0;
    for (int seci = 0; seci < 9 && written < 80; seci++) {
        int sec_start = acc;
        int sec_end   = acc + sec_bytes[seci];
        acc = sec_end;
        if (flat + 80 <= sec_start) break;
        if (flat       >= sec_end)   continue;
        int local_off  = (flat + written) - sec_start;
        int avail      = sec_end - sec_start - local_off;
        int to_write   = 80 - written;
        if (to_write > avail) to_write = avail;
        if (!s->sec[5 + seci]) return false;
        memcpy(s->sec[5 + seci] + local_off, raw80 + written, (size_t)to_write);
        pb_save_update_section_checksum(s, 5 + seci);
        written += to_write;
    }
    return written == 80;
}

void pb_save_update_section_checksum(pb_save_t *s, int section_id) {
    if (!s || section_id < 0 || section_id >= PB_SECTION_COUNT) return;
    if (!s->sec[section_id]) return;
    uint16_t cksum = section_checksum(s->sec[section_id]);
    pb_wr_u16le(s->sec[section_id] + PB_SECTION_FOOTER + 2, cksum);
}

bool pb_save_write_file(const pb_save_t *s, const char *path) {
    if (!s || !path) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t n = fwrite(s->raw, 1, PB_SAVE_SIZE, f);
    fclose(f);
    return n == PB_SAVE_SIZE;
}

const char *pb_game_name(pb_game_t g) {
    switch (g) {
        case PB_GAME_RS:      return "Ruby/Sapphire";
        case PB_GAME_FRLG:    return "FireRed/LeafGreen";
        case PB_GAME_EMERALD: return "Emerald";
        case PB_GAME_HACK:    return "ROM Hack (Emerald-based)";
        default:              return "Unknown";
    }
}
