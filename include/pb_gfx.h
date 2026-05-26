/* SDL2-based graphics for PokéBridge.
 *
 * Coexists with libogc's framebuffer console: when a graphics screen is
 * active, SDL2 owns the framebuffer; when we return to a text screen the
 * old console takes back over (best-effort -- some screens may need a
 * pb_ui_reinit after a graphics screen exits).
 *
 * Aesthetic target: Pokémon Box: Ruby & Sapphire (GameCube 2003). Soft
 * pastel palette, rounded translucent panels, white text on dark inset
 * panels, sprite slot grid. We're not chasing pixel parity -- just the
 * GameCube-era polish vibe.
 */
#ifndef POKEBRIDGE_GFX_H
#define POKEBRIDGE_GFX_H

#include <stdint.h>
#include <stdbool.h>

/* Palette inspired by Pokémon Box's "Sea" wallpaper variant. */
#define PB_GFX_COLOR_BG_TOP        0x4488CC  /* sky blue gradient top    */
#define PB_GFX_COLOR_BG_BOTTOM     0x1E4D7B  /* deeper blue at bottom    */
#define PB_GFX_COLOR_PANEL         0x183454  /* dark translucent panel   */
#define PB_GFX_COLOR_PANEL_LIGHT   0x6FA3D1  /* lighter selection panel  */
#define PB_GFX_COLOR_PANEL_ACCENT  0xF4D858  /* warm yellow accent (Pikachu yellow!) */
#define PB_GFX_COLOR_TEXT          0xFFFFFF
#define PB_GFX_COLOR_TEXT_DIM      0xC8DDF5
#define PB_GFX_COLOR_TEXT_ACCENT   0xFFE872
#define PB_GFX_COLOR_SHADOW        0x000000
#define PB_GFX_COLOR_BORDER        0xFFFFFF
#define PB_GFX_COLOR_SHINY         0xFFD800

/* Init creates an SDL_Window/Renderer at 640x480. Returns false on failure
 * (out of memory, GX hosed, etc.). */
bool pb_gfx_init(void);
void pb_gfx_deinit(void);

/* Frame lifecycle. clear() paints the gradient background; flip() presents. */
void pb_gfx_clear(void);
void pb_gfx_flip(void);

/* Primitive shapes. All coords in pixels, top-left origin. */
void pb_gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void pb_gfx_rounded_panel(int x, int y, int w, int h, int radius, uint32_t rgb, int alpha);
void pb_gfx_border(int x, int y, int w, int h, int thickness, uint32_t rgb);

/* Bitmap font: 8x12 ASCII printable range. Built-in -- no TTF file needed. */
void pb_gfx_text(int x, int y, uint32_t rgb, const char *s);
void pb_gfx_text_scale(int x, int y, uint32_t rgb, int scale, const char *s);
int  pb_gfx_text_width(const char *s);     /* in pixels, scale 1 */
#define PB_GFX_CHAR_W  8
#define PB_GFX_CHAR_H 12

/* Sprite slot: 56x56 placeholder with species color-coded background and
 * the species number printed in the center. Real sprites slot in later by
 * swapping this implementation. */
void pb_gfx_pkm_slot(int x, int y, uint16_t species, bool selected, bool shiny);

/* Stylized "box art" for the mainstream Gen 3 games. Renders a rounded
 * panel with the game's theme colors + cover legendary sprite + game name.
 * Not real artwork (avoids copyright) but evokes each game. */
typedef enum {
    PB_BOXART_RUBY = 0,
    PB_BOXART_SAPPHIRE,
    PB_BOXART_EMERALD,
    PB_BOXART_FIRERED,
    PB_BOXART_LEAFGREEN,
    PB_BOXART_COLOSSEUM,
    PB_BOXART_XD,
    PB_BOXART_BOX,
    PB_BOXART_HACK,        /* generic placeholder for ROM hacks */
    PB_BOXART_UNKNOWN,
} pb_boxart_t;

void pb_gfx_boxart(int x, int y, int w, int h, pb_boxart_t game);

/* Block until any button. Returns PAD_BUTTON_* bitmask. */
uint16_t pb_gfx_wait_button(void);

#endif
