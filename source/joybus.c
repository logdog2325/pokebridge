/* GBA link-cable joybus implementation.
 *
 * Ported from FIX94/gba-link-cable-dumper (MIT, 2016 FIX94). The multiboot
 * + save-backup protocol is preserved; only the harness (logging, UI,
 * memory layout) is replaced with PokéBridge primitives.
 */
#include "joybus.h"
#include "embedded_gba_mb.h"
#include <ogc/si.h>
#include <ogc/video.h>
#include <ogc/pad.h>
#include <malloc.h>
#include <string.h>

#define SI_TRANS_DELAY 50  /* per FIX94: ~50us is the safe minimum */

static uint8_t *g_cmdbuf;
static uint8_t *g_resbuf;
static volatile uint32_t g_transval;
static volatile uint32_t g_resval;

static void transcb(s32 chan, u32 ret) { (void)chan; (void)ret; g_transval = 1; }
static void acb(s32 res, u32 val)       { (void)res; g_resval = val; }

static void ensure_bufs(void) {
    if (!g_cmdbuf) g_cmdbuf = memalign(32, 32);
    if (!g_resbuf) g_resbuf = memalign(32, 32);
}

static uint32_t docrc(uint32_t crc, uint32_t val) {
    for (int i = 0; i < 32; i++) {
        if ((crc ^ val) & 1) {
            crc >>= 1;
            crc ^= 0xa1c1;
        } else {
            crc >>= 1;
        }
        val >>= 1;
    }
    return crc;
}

static uint32_t calckey(uint32_t size) {
    /* Bit-magic from the Nintendo multiboot protocol. Verbatim from FIX94. */
    uint32_t ret = 0;
    size = (size - 0x200) >> 3;
    int res1 = (size & 0x3F80) << 1;
    res1 |= (size & 0x4000) << 2;
    res1 |= (size & 0x7F);
    res1 |= 0x380000;
    int res2 = res1;
    res1 = res2 >> 0x10;
    int res3 = res2 >> 8;
    res3 += res1;
    res3 += res2;
    res3 <<= 24;
    res3 |= res2;
    res3 |= 0x80808080;

    if ((res3 & 0x200) == 0) {
        ret |= (((res3      ) & 0xFF) ^ 0x4B) << 24;
        ret |= (((res3 >>  8) & 0xFF) ^ 0x61) << 16;
        ret |= (((res3 >> 16) & 0xFF) ^ 0x77) <<  8;
        ret |= (((res3 >> 24) & 0xFF) ^ 0x61);
    } else {
        ret |= (((res3      ) & 0xFF) ^ 0x73) << 24;
        ret |= (((res3 >>  8) & 0xFF) ^ 0x65) << 16;
        ret |= (((res3 >> 16) & 0xFF) ^ 0x64) <<  8;
        ret |= (((res3 >> 24) & 0xFF) ^ 0x6F);
    }
    return ret;
}

/* Synchronous joybus primitives. Block until SI_Transfer completes. */
static void doreset(int port) {
    g_cmdbuf[0] = 0xFF;
    g_transval = 0;
    SI_Transfer(port, g_cmdbuf, 1, g_resbuf, 3, transcb, SI_TRANS_DELAY);
    while (g_transval == 0) ;
}
static void getstatus(int port) {
    g_cmdbuf[0] = 0x00;
    g_transval = 0;
    SI_Transfer(port, g_cmdbuf, 1, g_resbuf, 3, transcb, SI_TRANS_DELAY);
    while (g_transval == 0) ;
}
static uint32_t jb_recv(int port) {
    memset(g_resbuf, 0, 32);
    g_cmdbuf[0] = 0x14;
    g_transval = 0;
    SI_Transfer(port, g_cmdbuf, 1, g_resbuf, 5, transcb, SI_TRANS_DELAY);
    while (g_transval == 0) ;
    return *(volatile uint32_t *)g_resbuf;
}
static void jb_send(int port, uint32_t msg) {
    g_cmdbuf[0] = 0x15;
    g_cmdbuf[1] = (uint8_t)(msg      );
    g_cmdbuf[2] = (uint8_t)(msg >>  8);
    g_cmdbuf[3] = (uint8_t)(msg >> 16);
    g_cmdbuf[4] = (uint8_t)(msg >> 24);
    g_transval = 0;
    g_resbuf[0] = 0;
    SI_Transfer(port, g_cmdbuf, 5, g_resbuf, 1, transcb, SI_TRANS_DELAY);
    while (g_transval == 0) ;
}

int pb_joybus_detect_gba_port(void) {
    ensure_bufs();
    for (int port = 0; port < 4; port++) {
        g_resval = 0;
        SI_GetTypeAsync(port, acb);
        /* Wait briefly for the async probe to complete. */
        for (int i = 0; i < 4 && g_resval == 0; i++) VIDEO_WaitVSync();
        if (g_resval == 0) continue;
        if (g_resval == 0x80 || (g_resval & 8)) continue;
        if (g_resval & SI_GBA) return port;
    }
    return -1;
}

pb_joybus_status_t pb_joybus_multiboot(int port,
                                       pb_joybus_progress_cb cb, void *ctx) {
    ensure_bufs();

    /* Wait for the GBA BIOS to be ready for multiboot. */
    g_resbuf[2] = 0;
    int timeout = 200; /* ~200 vsyncs (~3 seconds) */
    while (!(g_resbuf[2] & 0x10)) {
        doreset(port);
        getstatus(port);
        if (--timeout <= 0) return PB_JOYBUS_NO_GBA;
    }

    /* Multiboot transfer per Nintendo protocol. */
    uint32_t sendsize = (pb_embedded_gba_mb_len + 7) & ~7u;
    uint32_t ourkey = calckey(sendsize);
    uint32_t sessionkeyraw = jb_recv(port);
    uint32_t sessionkey = __builtin_bswap32(sessionkeyraw ^ 0x7365646F);
    jb_send(port, __builtin_bswap32(ourkey));

    uint32_t fcrc = 0x15a0;
    /* First 0xC0 bytes (ROM header) are sent unencrypted. */
    for (uint32_t i = 0; i < 0xC0; i += 4) {
        uint32_t v = ((uint32_t)pb_embedded_gba_mb[i + 3] << 24)
                   | ((uint32_t)pb_embedded_gba_mb[i + 2] << 16)
                   | ((uint32_t)pb_embedded_gba_mb[i + 1] <<  8)
                   | ((uint32_t)pb_embedded_gba_mb[i + 0]      );
        jb_send(port, __builtin_bswap32(v));
    }
    /* Rest of the ROM is XOR-encrypted with the rolling session key. */
    uint32_t i;
    for (i = 0xC0; i < sendsize; i += 4) {
        uint32_t enc = ((uint32_t)pb_embedded_gba_mb[i + 3] << 24)
                     | ((uint32_t)pb_embedded_gba_mb[i + 2] << 16)
                     | ((uint32_t)pb_embedded_gba_mb[i + 1] <<  8)
                     | ((uint32_t)pb_embedded_gba_mb[i + 0]      );
        fcrc = docrc(fcrc, enc);
        sessionkey = (sessionkey * 0x6177614Bu) + 1;
        enc ^= sessionkey;
        enc ^= ((~(i + (0x20u << 20))) + 1);
        enc ^= 0x20796220;
        jb_send(port, enc);
        if (cb && (i & 0x3FF) == 0) {
            if (!cb(i, sendsize, ctx)) return PB_JOYBUS_USER_CANCEL;
        }
    }
    /* Final CRC. */
    fcrc |= (sendsize << 16);
    sessionkey = (sessionkey * 0x6177614Bu) + 1;
    fcrc ^= sessionkey;
    fcrc ^= ((~(i + (0x20u << 20))) + 1);
    fcrc ^= 0x20796220;
    jb_send(port, fcrc);
    jb_recv(port);  /* echo CRC, unused */
    return PB_JOYBUS_OK;
}

pb_joybus_status_t pb_joybus_get_cart_info(int port, pb_joybus_cart_info_t *info) {
    if (!info) return PB_JOYBUS_NO_CART;
    memset(info, 0, sizeof *info);
    /* Wait for the dumper to signal ready (sends 0). */
    if (jb_recv(port) != 0) return PB_JOYBUS_NO_CART;

    /* Dumper sends gbasize then savesize, both BE u32. */
    uint32_t gbasize = 0;
    int wait = 0;
    while (gbasize == 0 && wait < 200) {
        gbasize = __builtin_bswap32(jb_recv(port));
        wait++;
    }
    jb_send(port, 0);
    uint32_t savesize = __builtin_bswap32(jb_recv(port));
    jb_send(port, 0);
    if (gbasize == (uint32_t)-1) return PB_JOYBUS_NO_CART;

    /* Fetch 0xC0-byte ROM header. */
    uint8_t hdr[0xC0];
    for (uint32_t i = 0; i < 0xC0; i += 4) {
        *(volatile uint32_t *)(hdr + i) = jb_recv(port);
    }
    memcpy(info->game_name, hdr + 0xA0, 12);
    info->game_name[12] = 0;
    memcpy(info->game_id, hdr + 0xAC, 4);
    info->game_id[4] = 0;
    memcpy(info->company_id, hdr + 0xB0, 2);
    info->company_id[2] = 0;
    info->rom_size = gbasize;
    info->save_size = savesize;
    return PB_JOYBUS_OK;
}

pb_joybus_status_t pb_joybus_dump_save(int port, uint8_t *out, size_t max_len,
                                       size_t *out_len,
                                       pb_joybus_progress_cb cb, void *ctx) {
    if (!out || !out_len) return PB_JOYBUS_SAVE_TOO_BIG;
    *out_len = 0;
    /* Send command 2 = backup save. */
    jb_send(port, 2);
    /* Dumper performs cart->host SRAM read; after some prep it streams the
     * save size back as BE u32, then the bytes. */
    uint32_t savesize = 0;
    int wait = 0;
    while (savesize == 0 && wait < 400) {
        savesize = __builtin_bswap32(jb_recv(port));
        wait++;
    }
    if (savesize == 0) return PB_JOYBUS_NO_CART;
    if (savesize > max_len) return PB_JOYBUS_SAVE_TOO_BIG;
    jb_send(port, 0);  /* ack savesize */

    for (uint32_t i = 0; i < savesize; i += 4) {
        uint32_t w = jb_recv(port);
        out[i + 0] = (uint8_t)(w      );
        out[i + 1] = (uint8_t)(w >>  8);
        out[i + 2] = (uint8_t)(w >> 16);
        out[i + 3] = (uint8_t)(w >> 24);
        if (cb && (i & 0xFFF) == 0) {
            if (!cb(i, savesize, ctx)) return PB_JOYBUS_USER_CANCEL;
        }
    }
    *out_len = savesize;
    return PB_JOYBUS_OK;
}
