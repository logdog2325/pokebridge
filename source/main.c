/* PokeBridge entry point.
 *
 * Build:   make
 * Run:     - Dolphin (open pokebridge.dol). No SD setup needed -- the .dol
 *            embeds firered.sav and a Pokemon XD save as demos.
 *          - Swiss on real hardware (load .dol from SD).
 */
#include "ui.h"
#include <gccore.h>
#include <ogc/pad.h>
#include <fat.h>
#include <stdio.h>

bool pb_sd_available = false;

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* PAD needs initialization before SDL2 takes the framebuffer. */
    PAD_Init();

    /* Try SD silently in the background; if it works we'll show user-facing
     * saves on the menu later. */
    pb_sd_available = fatInitDefault();

    /* Boot straight into the graphics app (no libogc console screens). */
    pb_ui_run_graphics_app();
    return 0;
}
