/* GBA link-cable (joybus) reader for PokéBridge.
 *
 * Reference: FIX94/gba-link-cable-dumper (MIT). Ported here; the GBA-side
 * multiboot ROM is built from FIX94's gba/ subproject and embedded as
 * pb_embedded_gba_mb[].
 *
 * Hardware target: GameCube SI port 1 (the second physical port). Plug a
 * GBA via the GameCube–GBA link cable; with no cart inserted, power the
 * GBA on past the boot screen to the multiboot prompt; pokebridge sends
 * the dumper ROM over, then drives a save-dump exchange.
 *
 * Untested -- requires a real link cable + GBA + cart. Code is a faithful
 * port; treat as "should work" until verified on hardware.
 */
#ifndef POKEBRIDGE_JOYBUS_H
#define POKEBRIDGE_JOYBUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PB_JOYBUS_OK = 0,
    PB_JOYBUS_NO_GBA,
    PB_JOYBUS_MULTIBOOT_FAIL,
    PB_JOYBUS_NO_CART,
    PB_JOYBUS_SAVE_TOO_BIG,
    PB_JOYBUS_USER_CANCEL,
} pb_joybus_status_t;

typedef struct {
    char     game_name[16];   /* "POKEMON FIRE" etc. (12 chars + NUL slack) */
    char     game_id[5];      /* "BPRE" + NUL                              */
    char     company_id[3];   /* "01" + NUL                                */
    uint32_t rom_size;
    uint32_t save_size;
} pb_joybus_cart_info_t;

/* Progress callback type. Called with current/total bytes during long
 * transfers. Return false to abort. */
typedef bool (*pb_joybus_progress_cb)(uint32_t cur, uint32_t total, void *ctx);

/* Probe SI ports 0-3 for a GBA. Returns first port that responds, or -1. */
int pb_joybus_detect_gba_port(void);

/* Multiboot the GBA-side dumper ROM onto the given port. May take a few
 * seconds (~60 KB transfer at ~250 KB/s). */
pb_joybus_status_t pb_joybus_multiboot(int port,
                                       pb_joybus_progress_cb cb, void *ctx);

/* After the dumper is running on the GBA, fetch the cart header (the
 * dumper waits for the user to insert a cart on the GBA's prompt). */
pb_joybus_status_t pb_joybus_get_cart_info(int port,
                                           pb_joybus_cart_info_t *info);

/* Read the cart's save into `out`. The dumper supports up to 128 KB saves
 * (Gen 3 = 128 KB). out_len is set to the actual size read. */
pb_joybus_status_t pb_joybus_dump_save(int port, uint8_t *out, size_t max_len,
                                       size_t *out_len,
                                       pb_joybus_progress_cb cb, void *ctx);

#endif
