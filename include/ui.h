/* Console UI built on libogc's CON_* primitives.
 *
 * v0.1 uses the framebuffer console (640x480 text on TV). A later revision
 * will swap to GX/SDL2 once we want sprites and a real grid.
 */
#ifndef POKEBRIDGE_UI_H
#define POKEBRIDGE_UI_H

#include "save.h"
#include "pokemon.h"

void pb_ui_init(void);
void pb_ui_clear(void);
/* Boot entry point: launches graphics-mode app, never returns to the
 * console UI unless START is pressed on the main menu. */
void pb_ui_run_graphics_app(void);
void pb_ui_header(const char *title);
void pb_ui_footer(const char *hint);

/* Top-level menu screens. Each returns when the user backs out (B). */
void pb_ui_menu_main(void);
void pb_ui_show_save_summary(const pb_save_t *s);
void pb_ui_browse_party(pb_save_t *s);
void pb_ui_browse_boxes(pb_save_t *s);
void pb_ui_show_pkm(const pb_pkm_t *p);

/* Edit Pokémon in-place. Returns true if user confirmed (Y), false on cancel (B).
 * Caller is responsible for re-encoding p into the save buffer and recomputing
 * the section checksum. */
bool pb_ui_edit_pkm(pb_pkm_t *p);

/* XD-specific browsers. */
#include "pb_xd.h"
void pb_ui_browse_xd_party(pb_xd_save_t *xs);
void pb_ui_browse_xd_boxes(pb_xd_save_t *xs);

/* Block until any button. Returns the PAD_BUTTON_* bitmask of buttons pressed. */
uint16_t pb_ui_wait_button(void);

#endif
