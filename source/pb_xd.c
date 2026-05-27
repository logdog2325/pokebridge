#include "pb_xd.h"
#include "genius_crypto.h"
#include <string.h>
#include <stdio.h>

/* Read big-endian helpers (XD is BIG-ENDIAN throughout, unlike GBA Gen 3). */
static uint16_t rd_u16be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t rd_u32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/* XD encodes OT and nickname as 22-byte fields holding 11 big-endian 16-bit
 * characters. For Latin alphabets the high byte is 0 and the low byte is
 * ASCII-ish. Stop on 0x0000 terminator. */
static void decode_xd_name(const uint8_t *src, int max_chars, char *out, int out_cap) {
    int j = 0;
    for (int i = 0; i < max_chars && j < out_cap - 1; i++) {
        uint16_t c = rd_u16be(src + i * 2);
        if (c == 0) break;
        if (c >= 0x20 && c < 0x7F) out[j++] = (char)c;
        else                       out[j++] = '?';
    }
    out[j] = 0;
}

bool pb_xd_load(pb_xd_save_t *out, const uint8_t *data, size_t len) {
    if (!out || !data) return false;
    memset(out, 0, sizeof *out);

    /* Strip the 192-byte "DATELGC_SAVE" wrapper if present. */
    const uint8_t *body = data;
    size_t body_len = len;
    if (len == PB_XD_SAVE_SIZE + PB_XD_WRAPPER_SIZE &&
        memcmp(data, "DATELGC_SAVE", 12) == 0) {
        body = data + PB_XD_WRAPPER_SIZE;
        body_len = PB_XD_SAVE_SIZE;
    }
    if (body_len < PB_XD_SAVE_SIZE) return false;
    memcpy(out->body, body, PB_XD_SAVE_SIZE);

    /* Walk both slots, pick the one with the highest save counter. */
    int best_slot = -1;
    uint32_t best_count = 0;
    for (int s = 0; s < PB_XD_SLOT_COUNT; s++) {
        uint8_t *slot = out->body + PB_XD_SLOT_START + s * PB_XD_SLOT_SIZE;
        uint32_t ctr = rd_u32be(slot + 4);
        if (best_slot == -1 || ctr > best_count) {
            best_slot = s;
            best_count = ctr;
        }
    }
    if (best_slot < 0) return false;
    out->active_slot = best_slot;
    out->save_count = best_count;
    out->slot = out->body + PB_XD_SLOT_START + best_slot * PB_XD_SLOT_SIZE;

    /* Decrypt the active slot body. */
    uint16_t keys[4];
    pb_genius_read_keys(out->slot + PB_XD_KEYS_OFFSET, keys);
    pb_genius_decrypt(out->slot + PB_XD_ENC_START,
                      PB_XD_ENC_END - PB_XD_ENC_START, keys);

    /* PKHeX SAV3XD reads dynamic sub-offsets via:
     *   subOffsets[i] = BE u16 at 0x40+4i | (BE u16 at 0x40+4i+2) << 16
     * i.e. each u32 is stored as two BE u16 words, LOW word first.
     * Trainer1 = subOffsets[1] + 0xA8. */
    uint32_t sub1 = (uint32_t)rd_u16be(out->slot + 0x44)
                  | ((uint32_t)rd_u16be(out->slot + 0x46) << 16);
    uint32_t trainer1 = sub1 + 0xA8;
    uint32_t party    = trainer1 + 0x30;
    if (trainer1 + 0x100 >= PB_XD_SLOT_SIZE || party + 6 * PB_XD_PKM_SIZE >= PB_XD_SLOT_SIZE) {
        /* Offsets nonsensical -- decryption likely failed. */
        return false;
    }

    const uint8_t *tr = out->slot + trainer1;
    decode_xd_name(tr + 0x00, 11, out->trainer_name, sizeof out->trainer_name);
    out->trainer_id = rd_u16be(tr + 0x18);
    out->secret_id  = rd_u16be(tr + 0x1A);

    /* Count active party slots. */
    uint8_t pc = 0;
    for (int i = 0; i < 6; i++) {
        const uint8_t *slot_p = out->slot + party + i * PB_XD_PKM_SIZE;
        if (rd_u16be(slot_p) != 0) pc++;
    }
    out->party_count = pc;

    out->party_offset = party;
    out->trainer_offset = trainer1;

    /* Box offset: subOffsets[2] + 0xA8. Same encoding as subOffsets[1]. */
    uint32_t sub2 = (uint32_t)rd_u16be(out->slot + 0x48)
                  | ((uint32_t)rd_u16be(out->slot + 0x4A) << 16);
    if (sub2 + 0xA8 + PB_XD_BOX_COUNT * PB_XD_BOX_STRIDE < PB_XD_SLOT_SIZE) {
        out->box_offset = sub2 + 0xA8;
    } else {
        out->box_offset = 0; /* Couldn't resolve cleanly. */
    }
    return true;
}

void pb_xd_pkm_decode(pb_xd_pkm_t *out, const uint8_t *r) {
    memset(out, 0, sizeof *out);
    out->species_internal = rd_u16be(r + 0x00);
    if (out->species_internal == 0) { out->is_empty = true; return; }
    out->species_natdex   = pb_xd_species_to_natdex(out->species_internal);
    out->held_item        = rd_u16be(r + 0x02);
    out->exp              = rd_u32be(r + 0x20);
    out->secret_id        = rd_u16be(r + 0x24);
    out->trainer_id       = rd_u16be(r + 0x26);
    out->pid              = rd_u32be(r + 0x28);
    out->level            = r[0x11];
    out->moves[0]         = rd_u16be(r + 0x80);
    out->moves[1]         = rd_u16be(r + 0x84);
    out->moves[2]         = rd_u16be(r + 0x88);
    out->moves[3]         = rd_u16be(r + 0x8C);
    /* XK3 IV layout: 0xA8..0xAD = HP, Atk, Def, SpA, SpD, Spe (consecutive
     * bytes, NOT every 2 bytes). Reorder into our (HP, Atk, Def, Spe, SpA, SpD)
     * convention so the existing UI displays them in the expected slots. */
    out->iv[0] = r[0xA8] & 0x1F; /* HP  */
    out->iv[1] = r[0xA9] & 0x1F; /* Atk */
    out->iv[2] = r[0xAA] & 0x1F; /* Def */
    out->iv[3] = r[0xAD] & 0x1F; /* Spe */
    out->iv[4] = r[0xAB] & 0x1F; /* SpA */
    out->iv[5] = r[0xAC] & 0x1F; /* SpD */
    decode_xd_name(r + 0x38, 11, out->ot_name,  sizeof out->ot_name);
    decode_xd_name(r + 0x64, 11, out->nickname, sizeof out->nickname);
    out->is_shadow = (r[0x1D] & (1 << 5)) != 0;
}

/* --- XK3 <-> pb_pkm_t conversion --- */

static void wr_u16be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF);
}
static void wr_u32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF); p[3] = (uint8_t)(v & 0xFF);
}

void pb_xk3_to_pkm(pb_pkm_t *out, const uint8_t *r) {
    pb_xd_pkm_t xd;
    pb_xd_pkm_decode(&xd, r);
    memset(out, 0, sizeof *out);
    out->is_empty = xd.is_empty;
    if (xd.is_empty) return;

    out->pid       = xd.pid;
    out->ot_id     = ((uint32_t)xd.secret_id << 16) | xd.trainer_id;
    out->checksum_ok = true;
    out->g.species = xd.species_natdex ? xd.species_natdex : xd.species_internal;
    out->g.held_item = xd.held_item;
    out->g.experience = xd.exp;
    /* Friendship in XK3 is at offset 0x06 (u16 BE, low byte only). */
    out->g.friendship = r[0x07];

    for (int i = 0; i < 4; i++) {
        out->a.moves[i] = xd.moves[i];
        out->a.pp[i]    = r[0x82 + i * 4];
    }

    /* Pack IVs into m.iv_egg_ability so pb_pkm_get/set_iv work. */
    for (int i = 0; i < 6; i++) pb_pkm_set_iv(out, i, xd.iv[i]);

    /* EVs from XK3 (BE u16 at 0x9C..0xA7) in XK3 order, remapped to our slots. */
    out->e.ev[0] = (uint8_t)rd_u16be(r + 0x9C); /* HP  */
    out->e.ev[1] = (uint8_t)rd_u16be(r + 0x9E); /* Atk */
    out->e.ev[2] = (uint8_t)rd_u16be(r + 0xA0); /* Def */
    out->e.ev[3] = (uint8_t)rd_u16be(r + 0xA6); /* Spe */
    out->e.ev[4] = (uint8_t)rd_u16be(r + 0xA2); /* SpA */
    out->e.ev[5] = (uint8_t)rd_u16be(r + 0xA4); /* SpD */
}

void pb_xk3_apply_pkm_edits(const pb_pkm_t *p, uint8_t *r) {
    if (!p || !r) return;
    /* Only fields the editor touches. Leave everything else untouched. */
    wr_u32be(r + 0x28, p->pid);
    wr_u16be(r + 0x02, p->g.held_item);
    r[0x07] = p->g.friendship;  /* low byte of the BE u16 at 0x06 */

    /* Moves + PP */
    for (int i = 0; i < 4; i++) {
        wr_u16be(r + 0x80 + i * 4, p->a.moves[i]);
        r[0x82 + i * 4] = p->a.pp[i];
    }

    /* IVs back into XK3 byte layout (HP, Atk, Def, SpA, SpD, Spe). */
    uint8_t ivs[6]; pb_pkm_ivs(p, ivs);
    r[0xA8] = ivs[0];           /* HP  */
    r[0xA9] = ivs[1];           /* Atk */
    r[0xAA] = ivs[2];           /* Def */
    r[0xAB] = ivs[4];           /* SpA */
    r[0xAC] = ivs[5];           /* SpD */
    r[0xAD] = ivs[3];           /* Spe */

    /* EVs back into XK3 BE u16 layout. */
    wr_u16be(r + 0x9C, p->e.ev[0]);
    wr_u16be(r + 0x9E, p->e.ev[1]);
    wr_u16be(r + 0xA0, p->e.ev[2]);
    wr_u16be(r + 0xA2, p->e.ev[4]);
    wr_u16be(r + 0xA4, p->e.ev[5]);
    wr_u16be(r + 0xA6, p->e.ev[3]);
}

uint32_t pb_xd_box_slot_offset(const pb_xd_save_t *s, int box_index, int slot) {
    if (!s || box_index < 0 || box_index >= PB_XD_BOX_COUNT) return 0;
    if (slot < 0 || slot >= PB_XD_BOX_SIZE) return 0;
    return s->box_offset
         + (uint32_t)box_index * PB_XD_BOX_STRIDE
         + PB_XD_BOX_INFO_BYTES
         + (uint32_t)slot * PB_XD_PKM_SIZE;
}

/* Write u16 BE (wr_u32be is defined above for the encoder). */
static void wr_u16be_p(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

/* Port of PKHeX XDCrypto.SetChecksums (GPL-3.0). Operates on the decrypted
 * active slot data. */
static void set_checksums(uint8_t *slot, uint32_t sub_off0) {
    /* Header checksum: sum of bytes 0..7, written as int32 BE at
     * 0xA8 + sub_off0 + 0x38. */
    int32_t hc = 0;
    for (int i = 0; i < 8; i++) hc += slot[i];
    wr_u32be(slot + 0xA8 + sub_off0 + 0x38, (uint32_t)hc);

    /* Body checksum: clear the old checksum bytes (0x10..0x20), then sum
     * BE u16 words across 4 chunks of 0x9FF4 bytes, starting at offset 8. */
    memset(slot + 0x10, 0, 0x10);
    uint32_t checksums[4] = {0};
    int dt = 8;
    for (int i = 0; i < 4; i++) {
        uint32_t v = 0;
        int end = dt + 0x9FF4;
        for (int j = dt; j < end; j += 2) {
            v += rd_u16be(slot + j);
        }
        dt = end;
        checksums[i] = v;
    }
    /* PKHeX XDCrypto.SetChecksums splits each u32 into [hi, lo] u16 pairs,
     * then writes the 8 resulting words to 0x10..0x20 IN REVERSE ORDER.
     * Getting this wrong (writing forward) produces a body checksum the
     * game refuses -- which is what corrupted the first real-hardware
     * test save. Mirror PKHeX exactly.
     *
     *   newchks = [hi0, lo0, hi1, lo1, hi2, lo2, hi3, lo3]
     *   for i in 0..8: data[0x10 + 2*i] = newchks[7 - i]   (BE u16 each)
     *
     * Net layout on disk:
     *   0x10: lo3 0x12: hi3 0x14: lo2 0x16: hi2
     *   0x18: lo1 0x1A: hi1 0x1C: lo0 0x1E: hi0
     */
    uint16_t newchks[8];
    for (int i = 0; i < 4; i++) {
        newchks[i * 2]     = (uint16_t)(checksums[i] >> 16);
        newchks[i * 2 + 1] = (uint16_t)(checksums[i] & 0xFFFF);
    }
    for (int i = 0; i < 8; i++) {
        wr_u16be_p(slot + 0x10 + 2 * i, newchks[8 - 1 - i]);
    }
}

void pb_xd_finalize_slot(pb_xd_save_t *xs) {
    if (!xs || !xs->slot) return;
    /* subOffsets[0] is read like sub1: (BE u16 at 0x40+0) | ((BE u16 at 0x40+2) << 16). */
    uint32_t sub0 = (uint32_t)rd_u16be(xs->slot + 0x40)
                  | ((uint32_t)rd_u16be(xs->slot + 0x40 + 2) << 16);
    set_checksums(xs->slot, sub0);

    /* Re-encrypt the body with the same keys stored at slot+0x08. */
    uint16_t keys[4];
    pb_genius_read_keys(xs->slot + PB_XD_KEYS_OFFSET, keys);
    pb_genius_encrypt(xs->slot + PB_XD_ENC_START,
                      PB_XD_ENC_END - PB_XD_ENC_START, keys);
}

void pb_xd_redecrypt_slot(pb_xd_save_t *xs) {
    if (!xs || !xs->slot) return;
    uint16_t keys[4];
    pb_genius_read_keys(xs->slot + PB_XD_KEYS_OFFSET, keys);
    pb_genius_decrypt(xs->slot + PB_XD_ENC_START,
                      PB_XD_ENC_END - PB_XD_ENC_START, keys);
}

bool pb_xd_write_file(pb_xd_save_t *xs, const char *path) {
    if (!xs || !path) return false;
    pb_xd_finalize_slot(xs);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t n = fwrite(xs->body, 1, PB_XD_SAVE_SIZE, f);
    fclose(f);
    /* After writing, decrypt the slot again so further in-memory edits
     * still work against decrypted data. */
    uint16_t keys[4];
    pb_genius_read_keys(xs->slot + PB_XD_KEYS_OFFSET, keys);
    pb_genius_decrypt(xs->slot + PB_XD_ENC_START,
                      PB_XD_ENC_END - PB_XD_ENC_START, keys);
    return n == PB_XD_SAVE_SIZE;
}

uint16_t pb_xd_species_to_natdex(uint16_t internal_id) {
    /* XD uses a different species enumeration than natdex. PKHeX has a
     * conversion table in SpeciesConverter; we haven't ported it yet. For
     * legendaries/starters and most mons the IDs DIFFER -- so this stub
     * returns the raw value and the UI labels it as "(XD #N)".
     *
     * TODO: port the full mapping from PKHeX.Core/PKM/Util/SpeciesConverter
     * (Gen3 internal -> natdex). It's a ~412 entry table.                    */
    if (internal_id > 0 && internal_id <= 386) return internal_id;
    return 0;
}
