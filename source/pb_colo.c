#include "pb_colo.h"
#include "sha1.h"
#include <string.h>
#include <stdio.h>

static uint16_t rd_u16be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t rd_u32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/* GameCube-style names are stored as big-endian 16-bit chars in 22-byte
 * trash buffers. We treat values 0x20..0x7E as ASCII passthrough -- works
 * for English saves; non-Latin scripts come out as '?'. */
static void decode_name(const uint8_t *src, int max_chars,
                        char *out, int out_cap) {
    int j = 0;
    for (int i = 0; i < max_chars && j < out_cap - 1; i++) {
        uint16_t c = rd_u16be(src + i * 2);
        if (c == 0) break;
        if (c >= 0x20 && c < 0x7F) out[j++] = (char)c;
        else                       out[j++] = '?';
    }
    out[j] = 0;
}

/* ColoCrypto decrypt -- mirrors PKHeX's algorithm (GPL-3.0). */
static void colo_decrypt(uint8_t *slot) {
    uint8_t digest[20];
    /* At-rest key = inverted last 20 bytes. */
    const uint8_t *stored = slot + PB_COLO_SLOT_SIZE - 20;
    for (int i = 0; i < 20; i++) digest[i] = (uint8_t)(~stored[i]);

    uint8_t hash[20];
    uint8_t *p = slot + PB_COLO_ENC_START;
    int remaining = PB_COLO_SLOT_SIZE - PB_COLO_ENC_START - 20;
    while (remaining >= 20) {
        /* Hash the encrypted bytes first (becomes the next digest). */
        pb_sha1(hash, p, 20);
        /* Un-XOR with current digest to recover plaintext. */
        for (int j = 0; j < 20; j++) p[j] ^= digest[j];
        /* Advance: new digest is the hash of the still-encrypted block we
         * just processed (computed above). */
        memcpy(digest, hash, 20);
        p += 20;
        remaining -= 20;
    }
}

bool pb_colo_load(pb_colo_save_t *out, const uint8_t *data, size_t len) {
    if (!out || !data) return false;
    memset(out, 0, sizeof *out);

    /* Strip any wrapper -- the actual XD/Colosseum save body is the last
     * PB_COLO_SAVE_SIZE bytes of the file. GCSAVE wrapper is 336 bytes,
     * DATELGC_SAVE wrapper is 192 bytes, .gci raw has no wrapper. */
    const uint8_t *body = data;
    size_t body_len = len;
    if (len >= PB_COLO_SAVE_SIZE) {
        body = data + (len - PB_COLO_SAVE_SIZE);
        body_len = PB_COLO_SAVE_SIZE;
    }
    if (body_len < PB_COLO_SAVE_SIZE) return false;
    memcpy(out->body, body, PB_COLO_SAVE_SIZE);

    /* Pick slot with highest BE u32 counter at offset 4. */
    int best = -1;
    uint32_t best_count = 0;
    for (int s = 0; s < PB_COLO_SLOT_COUNT; s++) {
        uint8_t *slot = out->body + PB_COLO_SLOT_START + s * PB_COLO_SLOT_SIZE;
        uint32_t ctr = rd_u32be(slot + 4);
        if (best < 0 || ctr > best_count) { best = s; best_count = ctr; }
    }
    if (best < 0) return false;
    out->active_slot = best;
    out->save_count = best_count;
    out->slot = out->body + PB_COLO_SLOT_START + best * PB_COLO_SLOT_SIZE;

    /* Decrypt in place. */
    colo_decrypt(out->slot);

    /* Trainer name + IDs at known offsets. */
    decode_name(out->slot + PB_COLO_TRAINER_OFF, 10,
                out->trainer_name, sizeof out->trainer_name);
    out->secret_id  = rd_u16be(out->slot + 0xA4);
    out->trainer_id = rd_u16be(out->slot + 0xA6);

    /* Party count by scanning species fields. */
    uint8_t pc = 0;
    for (int i = 0; i < 6; i++) {
        const uint8_t *slot_p = out->slot + PB_COLO_PARTY_OFF + i * PB_COLO_PKM_SIZE;
        if (rd_u16be(slot_p) != 0) pc++;
    }
    out->party_count = pc;
    return true;
}

/* ColoCrypto encrypt -- mirror of decrypt: XOR first to encrypt, then hash
 * the newly-encrypted bytes for the next digest. */
static void colo_encrypt(uint8_t *slot) {
    uint8_t digest[20];
    const uint8_t *stored = slot + PB_COLO_SLOT_SIZE - 20;
    for (int i = 0; i < 20; i++) digest[i] = (uint8_t)(~stored[i]);

    uint8_t *p = slot + PB_COLO_ENC_START;
    int remaining = PB_COLO_SLOT_SIZE - PB_COLO_ENC_START - 20;
    while (remaining >= 20) {
        for (int j = 0; j < 20; j++) p[j] ^= digest[j];
        pb_sha1(digest, p, 20);
        p += 20;
        remaining -= 20;
    }
}

/* Header checksum: -sum of u32 BE words 0..0x17, plus XOR with the new body
 * hash for the last two u32 words. Mirrors PKHeX ComputeHeaderChecksum. */
static int32_t compute_header_checksum(const uint8_t *header, const uint8_t *hash) {
    int32_t result = 0;
    for (int i = 0; i < 0x18; i += 4) result -= (int32_t)rd_u32be(header + i);
    /* `header[0x18..]` and `header[0x1C..]` are technically out of the 0x18
     * header range above, but PKHeX still references them. They appear to
     * be the two u32 words right after the strict 0x18 header. */
    int32_t h18 = (int32_t)rd_u32be(header + 0x18);
    int32_t h1C = (int32_t)rd_u32be(header + 0x1C);
    int32_t hashA = (int32_t)rd_u32be(hash + 0);
    int32_t hashB = (int32_t)rd_u32be(hash + 4);
    result -= (h18 ^ ~hashA);
    result -= (h1C ^ ~hashB);
    return result;
}

static void wr_u32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >>  8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}
static void wr_u16be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

void pb_colo_finalize_slot(pb_colo_save_t *cs) {
    if (!cs || !cs->slot) return;
    uint8_t *slot = cs->slot;
    /* Clear header checksum field at 0x0C before recomputing. */
    wr_u32be(slot + 0x0C, 0);
    /* Body checksum: SHA-1 of bytes [0, end - 20]; store at last 20 bytes. */
    pb_sha1(slot + PB_COLO_SLOT_SIZE - 20, slot, PB_COLO_SLOT_SIZE - 20);
    /* Header checksum: -sum + xor formula. */
    int32_t hc = compute_header_checksum(slot, slot + PB_COLO_SLOT_SIZE - 20);
    wr_u32be(slot + 0x0C, (uint32_t)hc);
    /* Encrypt the body. */
    colo_encrypt(slot);
}

void pb_colo_redecrypt_slot(pb_colo_save_t *cs) {
    if (!cs || !cs->slot) return;
    colo_decrypt(cs->slot);
}

bool pb_colo_write_file(pb_colo_save_t *cs, const char *path) {
    if (!cs || !path) return false;
    pb_colo_finalize_slot(cs);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t n = fwrite(cs->body, 1, PB_COLO_SAVE_SIZE, f);
    fclose(f);
    /* Re-decrypt so further in-memory edits stay valid. */
    colo_decrypt(cs->slot);
    return n == PB_COLO_SAVE_SIZE;
}

uint32_t pb_colo_box_slot_offset(const pb_colo_save_t *cs, int box_index, int slot) {
    if (!cs || box_index < 0 || box_index >= PB_COLO_BOX_COUNT) return 0;
    if (slot < 0 || slot >= PB_COLO_BOX_SIZE) return 0;
    return PB_COLO_BOX_OFF
         + (uint32_t)box_index * PB_COLO_BOX_STRIDE
         + PB_COLO_BOX_INFO_BYTES
         + (uint32_t)slot * PB_COLO_PKM_SIZE;
}

/* --- CK3 <-> pb_pkm_t conversion --- */

void pb_ck3_to_pkm(pb_pkm_t *out, const uint8_t *r) {
    pb_colo_pkm_t cp;
    pb_colo_pkm_decode(&cp, r);
    memset(out, 0, sizeof *out);
    out->is_empty = cp.is_empty;
    if (cp.is_empty) return;

    out->pid       = cp.pid;
    out->ot_id     = ((uint32_t)cp.secret_id << 16) | cp.trainer_id;
    out->checksum_ok = true;
    out->g.species = cp.species_natdex ? cp.species_natdex : cp.species_internal;
    out->g.held_item  = cp.held_item;
    out->g.experience = cp.exp;
    /* friendship at 0xB0 (BE u16, low byte) */
    out->g.friendship = r[0xB1];
    for (int i = 0; i < 4; i++) {
        out->a.moves[i] = cp.moves[i];
        out->a.pp[i]    = r[0x7A + i * 4];
    }
    for (int i = 0; i < 6; i++) pb_pkm_set_iv(out, i, cp.iv[i]);
    /* EVs from CK3 (BE u16 at 0x98..0xA3) in XK3-like order (HP/Atk/Def/SpA/SpD/Spe).
     * Remap to our slot order (HP/Atk/Def/Spe/SpA/SpD). */
    out->e.ev[0] = (uint8_t)rd_u16be(r + 0x98); /* HP  */
    out->e.ev[1] = (uint8_t)rd_u16be(r + 0x9A); /* Atk */
    out->e.ev[2] = (uint8_t)rd_u16be(r + 0x9C); /* Def */
    out->e.ev[3] = (uint8_t)rd_u16be(r + 0xA2); /* Spe */
    out->e.ev[4] = (uint8_t)rd_u16be(r + 0x9E); /* SpA */
    out->e.ev[5] = (uint8_t)rd_u16be(r + 0xA0); /* SpD */
}

void pb_ck3_apply_pkm_edits(const pb_pkm_t *p, uint8_t *r) {
    if (!p || !r) return;
    wr_u32be(r + 0x04, p->pid);
    wr_u32be(r + 0x14, ((uint32_t)(p->ot_id & 0xFFFF) << 16) | (p->ot_id & 0xFFFF));
    /* Actually ID32 packs SID first then TID in BE; we already write the
     * combined word above. Also overwrite TID16 at +0x16 explicitly. */
    wr_u16be(r + 0x14, (uint16_t)((p->ot_id >> 16) & 0xFFFF));  /* SID */
    wr_u16be(r + 0x16, (uint16_t)(p->ot_id & 0xFFFF));          /* TID */

    wr_u16be(r + 0x88, p->g.held_item);
    r[0xB1] = p->g.friendship; /* low byte of BE u16 at 0xB0 */

    for (int i = 0; i < 4; i++) {
        wr_u16be(r + 0x78 + i * 4, p->a.moves[i]);
        r[0x7A + i * 4] = p->a.pp[i];
    }

    uint8_t ivs[6]; pb_pkm_ivs(p, ivs);
    /* IVs as low byte of BE u16; CK3 order = HP/Atk/Def/SpA/SpD/Spe. */
    wr_u16be(r + 0xA4, ivs[0]); /* HP  */
    wr_u16be(r + 0xA6, ivs[1]); /* Atk */
    wr_u16be(r + 0xA8, ivs[2]); /* Def */
    wr_u16be(r + 0xAA, ivs[4]); /* SpA */
    wr_u16be(r + 0xAC, ivs[5]); /* SpD */
    wr_u16be(r + 0xAE, ivs[3]); /* Spe */

    /* EVs similarly. */
    wr_u16be(r + 0x98, p->e.ev[0]);
    wr_u16be(r + 0x9A, p->e.ev[1]);
    wr_u16be(r + 0x9C, p->e.ev[2]);
    wr_u16be(r + 0x9E, p->e.ev[4]);
    wr_u16be(r + 0xA0, p->e.ev[5]);
    wr_u16be(r + 0xA2, p->e.ev[3]);
}

void pb_colo_pkm_decode(pb_colo_pkm_t *out, const uint8_t *r) {
    memset(out, 0, sizeof *out);
    out->species_internal = rd_u16be(r + 0x00);
    if (out->species_internal == 0) { out->is_empty = true; return; }
    /* XD/Colosseum share the same internal -> natdex convention; for Gen 1-3
     * the IDs match. Beyond that the table differs but for our viewer we
     * just pass through. */
    out->species_natdex = (out->species_internal <= 386) ? out->species_internal : 0;

    out->pid        = rd_u32be(r + 0x04);
    out->secret_id  = rd_u16be(r + 0x14);
    out->trainer_id = rd_u16be(r + 0x16);
    out->exp        = rd_u32be(r + 0x5C);
    out->level      = r[0x60];

    out->moves[0]   = rd_u16be(r + 0x78);
    out->moves[1]   = rd_u16be(r + 0x7C);
    out->moves[2]   = rd_u16be(r + 0x80);
    out->moves[3]   = rd_u16be(r + 0x84);

    out->held_item  = rd_u16be(r + 0x88);

    /* CK3 IVs are BE u16 at 0xA4/A6/A8/AA/AC/AE in order HP, Atk, Def,
     * SpA, SpD, Spe. The low byte is the actual value (0-31). Map to our
     * stat order (HP, Atk, Def, Spe, SpA, SpD). */
    out->iv[0] = r[0xA5] & 0x1F; /* HP  */
    out->iv[1] = r[0xA7] & 0x1F; /* Atk */
    out->iv[2] = r[0xA9] & 0x1F; /* Def */
    out->iv[3] = r[0xAF] & 0x1F; /* Spe */
    out->iv[4] = r[0xAB] & 0x1F; /* SpA */
    out->iv[5] = r[0xAD] & 0x1F; /* SpD */

    decode_name(r + 0x18, 11, out->ot_name,  sizeof out->ot_name);
    decode_name(r + 0x44, 11, out->nickname, sizeof out->nickname);

    /* Shadow-mon flag lives in the per-mon flags byte; PKHeX CK3 reads it
     * from offset 0x11 bit 0. Best-effort. */
    out->is_shadow = (r[0x11] & 0x01) != 0;
}
