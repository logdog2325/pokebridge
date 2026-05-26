#include "pb_gfx.h"
#include "embedded_sprites.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_test_font.h>
#include <ogc/pad.h>
#include <ogc/video.h>
#include <gccore.h>
#include <string.h>
#include <stdio.h>

static SDL_Window   *g_window   = NULL;
static SDL_Renderer *g_renderer = NULL;
/* Two-bank cache: index 0 = normal, index 1 = shiny. */
static SDL_Texture *g_sprite_cache[2][1025] = {{0}};

/* Decode a 4bpp sprite + 16-color RGBA palette into a 64x64 RGBA scratch
 * buffer. */
static void decode_4bpp(uint8_t out_rgba[64 * 64 * 4],
                       const uint8_t *pixels, const uint8_t *palette) {
    /* pixels: 2048 bytes, each byte holds two 4-bit indices (hi nibble = even pixel). */
    for (int i = 0; i < 2048; i++) {
        uint8_t byte = pixels[i];
        uint8_t hi = (byte >> 4) & 0x0F;
        uint8_t lo = byte & 0x0F;
        int px_index_l = i * 2;
        int px_index_r = i * 2 + 1;
        const uint8_t *pl = palette + hi * 4;
        const uint8_t *pr = palette + lo * 4;
        uint8_t *dl = out_rgba + px_index_l * 4;
        uint8_t *dr = out_rgba + px_index_r * 4;
        dl[0] = pl[0]; dl[1] = pl[1]; dl[2] = pl[2]; dl[3] = pl[3];
        dr[0] = pr[0]; dr[1] = pr[1]; dr[2] = pr[2]; dr[3] = pr[3];
    }
}

static SDL_Texture *sprite_for(uint16_t species, bool shiny) {
    if (species == 0 || species >= 1025 || !g_renderer) return NULL;
    int bank = shiny ? 1 : 0;
    if (g_sprite_cache[bank][species]) return g_sprite_cache[bank][species];

    for (int i = 0; i < PB_SPRITE_TABLE_LEN; i++) {
        if (pb_sprite_table[i].species != species) continue;
        const uint8_t *pal = shiny ? pb_sprite_table[i].pal_shiny
                                   : pb_sprite_table[i].pal_normal;
        if (!pal) pal = pb_sprite_table[i].pal_normal;
        if (!pal) return NULL;

        static uint8_t scratch[64 * 64 * 4];
        decode_4bpp(scratch, pb_sprite_table[i].pixels, pal);

        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
            scratch, PB_SPRITE_W, PB_SPRITE_H, 32, PB_SPRITE_W * 4,
            SDL_PIXELFORMAT_RGBA32);
        if (!surf) return NULL;
        SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
        SDL_FreeSurface(surf);
        if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        g_sprite_cache[bank][species] = tex;
        return tex;
    }
    return NULL;
}

static inline uint8_t rch(uint32_t rgb) { return (uint8_t)((rgb >> 16) & 0xFF); }
static inline uint8_t gch(uint32_t rgb) { return (uint8_t)((rgb >>  8) & 0xFF); }
static inline uint8_t bch(uint32_t rgb) { return (uint8_t)( rgb        & 0xFF); }

bool pb_gfx_init(void) {
    if (g_renderer) return true; /* already up */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
    g_window = SDL_CreateWindow("PokeBridge", 0, 0, 640, 480, SDL_WINDOW_SHOWN);
    if (!g_window) { SDL_Quit(); return false; }
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_renderer) g_renderer = SDL_CreateRenderer(g_window, -1, 0);
    if (!g_renderer) { SDL_DestroyWindow(g_window); g_window = NULL; SDL_Quit(); return false; }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    return true;
}

void pb_gfx_deinit(void) {
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 1025; i++) {
            if (g_sprite_cache[b][i]) {
                SDL_DestroyTexture(g_sprite_cache[b][i]);
                g_sprite_cache[b][i] = NULL;
            }
        }
    }
    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = NULL; }
    if (g_window)   { SDL_DestroyWindow(g_window);     g_window   = NULL; }
    SDL_Quit();
}

void pb_gfx_clear(void) {
    if (!g_renderer) return;
    /* Pokémon Box-inspired vertical gradient: lighter blue at top, deeper at
     * bottom. Drawn as horizontal bands for a smooth gradient on a
     * fixed-function pipeline. */
    uint8_t tr = rch(PB_GFX_COLOR_BG_TOP), tg = gch(PB_GFX_COLOR_BG_TOP), tb = bch(PB_GFX_COLOR_BG_TOP);
    uint8_t br = rch(PB_GFX_COLOR_BG_BOTTOM), bg = gch(PB_GFX_COLOR_BG_BOTTOM), bb = bch(PB_GFX_COLOR_BG_BOTTOM);
    int bands = 48;
    int band_h = 480 / bands;
    for (int i = 0; i < bands; i++) {
        float t = (float)i / (float)(bands - 1);
        uint8_t r = (uint8_t)(tr + (br - tr) * t);
        uint8_t g = (uint8_t)(tg + (bg - tg) * t);
        uint8_t b = (uint8_t)(tb + (bb - tb) * t);
        SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
        SDL_Rect rect = { 0, i * band_h, 640, band_h };
        SDL_RenderFillRect(g_renderer, &rect);
    }
}

void pb_gfx_flip(void) {
    if (g_renderer) SDL_RenderPresent(g_renderer);
}

void pb_gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    if (!g_renderer) return;
    SDL_SetRenderDrawColor(g_renderer, rch(rgb), gch(rgb), bch(rgb), 255);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(g_renderer, &r);
}

/* "Rounded" panel: a filled rect with the four corners shaved off by small
 * inset rectangles in the background gradient color. Not as smooth as a
 * proper SDL2_gfx roundedBox, but it gives the visual rounded feel. */
void pb_gfx_rounded_panel(int x, int y, int w, int h, int radius, uint32_t rgb, int alpha) {
    if (!g_renderer) return;
    SDL_SetRenderDrawColor(g_renderer, rch(rgb), gch(rgb), bch(rgb), (uint8_t)alpha);
    /* Center rect (full width, inset top/bottom by radius) */
    SDL_Rect center = { x, y + radius, w, h - 2 * radius };
    SDL_RenderFillRect(g_renderer, &center);
    /* Top + bottom strips (full height of radius, inset left/right by radius) */
    SDL_Rect top    = { x + radius, y,                w - 2 * radius, radius };
    SDL_Rect bot    = { x + radius, y + h - radius,   w - 2 * radius, radius };
    SDL_RenderFillRect(g_renderer, &top);
    SDL_RenderFillRect(g_renderer, &bot);
    /* Approximate rounded corners with a simple staircase. */
    for (int i = 0; i < radius; i++) {
        int chord = (int)(radius - radius * (1.0f - (float)i / (float)radius));
        (void)chord;
        /* Calc how far in from corner this row extends */
        int dy = radius - 1 - i;
        int dx = (int)(radius - SDL_sqrtf((float)(radius * radius - dy * dy)));
        SDL_Rect tl = { x + dx, y + i,             radius - dx, 1 };
        SDL_Rect tr = { x + w - radius, y + i,     radius - dx, 1 };
        SDL_Rect bl = { x + dx, y + h - 1 - i,     radius - dx, 1 };
        SDL_Rect br = { x + w - radius, y + h - 1 - i, radius - dx, 1 };
        SDL_RenderFillRect(g_renderer, &tl);
        SDL_RenderFillRect(g_renderer, &tr);
        SDL_RenderFillRect(g_renderer, &bl);
        SDL_RenderFillRect(g_renderer, &br);
    }
}

void pb_gfx_border(int x, int y, int w, int h, int thickness, uint32_t rgb) {
    if (!g_renderer) return;
    SDL_SetRenderDrawColor(g_renderer, rch(rgb), gch(rgb), bch(rgb), 255);
    for (int i = 0; i < thickness; i++) {
        SDL_Rect r = { x + i, y + i, w - 2 * i, h - 2 * i };
        SDL_RenderDrawRect(g_renderer, &r);
    }
}

void pb_gfx_text(int x, int y, uint32_t rgb, const char *s) {
    if (!g_renderer || !s) return;
    SDL_SetRenderDrawColor(g_renderer, rch(rgb), gch(rgb), bch(rgb), 255);
    SDLTest_DrawString(g_renderer, x, y, s);
}

void pb_gfx_text_scale(int x, int y, uint32_t rgb, int scale, const char *s) {
    /* SDLTest_DrawString is fixed at 8x8. Faux-bold by drawing a few times
     * with 1-pixel offsets. */
    pb_gfx_text(x, y, rgb, s);
    if (scale > 1) {
        pb_gfx_text(x + 1, y,     rgb, s);
        pb_gfx_text(x,     y + 1, rgb, s);
        pb_gfx_text(x + 1, y + 1, rgb, s);
    }
}

int pb_gfx_text_width(const char *s) {
    return s ? (int)strlen(s) * 8 : 0;
}

void pb_gfx_pkm_slot(int x, int y, uint16_t species, bool selected, bool shiny) {
    static const uint32_t tints[] = {
        0x6FA3D1, 0xE56B6F, 0x88BB66, 0x77CCEE,
        0xFFD86B, 0xBB99DD, 0xCCBB99, 0xAA8855,
    };
    uint32_t tint = tints[species % (sizeof tints / sizeof tints[0])];

    /* Selection halo (yellow accent ring around the slot) */
    if (selected) {
        pb_gfx_rounded_panel(x - 4, y - 4, 72, 72, 10, PB_GFX_COLOR_PANEL_ACCENT, 230);
    }
    /* Drop shadow */
    pb_gfx_rounded_panel(x + 2, y + 2, 64, 64, 8, PB_GFX_COLOR_SHADOW, 90);
    /* Tinted backdrop card */
    pb_gfx_rounded_panel(x, y, 64, 64, 8, tint, 240);
    /* Sprite on top. Picks the shiny variant when toggled. */
    SDL_Texture *tex = sprite_for(species, shiny);
    if (tex) {
        SDL_Rect dst = { x, y, 64, 64 };
        SDL_RenderCopy(g_renderer, tex, NULL, &dst);
    } else {
        /* Placeholder: show species number */
        char buf[8];
        snprintf(buf, sizeof buf, "#%u", species);
        int tw = (int)strlen(buf) * 8;
        pb_gfx_text(x + (64 - tw) / 2, y + (64 - 8) / 2, PB_GFX_COLOR_TEXT, buf);
    }
    /* Border last so it sits above the sprite */
    pb_gfx_border(x, y, 64, 64, shiny ? 2 : 1,
                  shiny ? PB_GFX_COLOR_SHINY : PB_GFX_COLOR_BORDER);
    if (shiny) pb_gfx_text(x + 52, y + 4, PB_GFX_COLOR_SHINY, "*");
}

uint16_t pb_gfx_wait_button(void) {
    while (1) {
        VIDEO_WaitVSync();
        PAD_ScanPads();
        uint16_t pressed = PAD_ButtonsDown(0);
        if (pressed) return pressed;
    }
}

void pb_gfx_boxart(int x, int y, int w, int h, pb_boxart_t game) {
    /* Per-game theme: top color, bottom color, cover species, title text. */
    struct {
        uint32_t top, bot;
        uint16_t species;
        const char *title;
        const char *subtitle;
    } themes[] = {
        [PB_BOXART_RUBY]      = { 0xB81E1E, 0x420808, 383, "POKEMON",  "RUBY"      },
        [PB_BOXART_SAPPHIRE]  = { 0x1E64B8, 0x081E42, 382, "POKEMON",  "SAPPHIRE"  },
        [PB_BOXART_EMERALD]   = { 0x1E9B5C, 0x074224, 384, "POKEMON",  "EMERALD"   },
        [PB_BOXART_FIRERED]   = { 0xD83228, 0x5C1408, 6,   "POKEMON",  "FIRERED"   },
        [PB_BOXART_LEAFGREEN] = { 0x88B82C, 0x2E4A14, 3,   "POKEMON",  "LEAFGREEN" },
        [PB_BOXART_COLOSSEUM] = { 0x6B1A6B, 0x1E0A28, 250, "POKEMON",  "COLOSSEUM" },
        [PB_BOXART_XD]        = { 0x4A1858, 0x100416, 249, "POKEMON",  "XD"        },
        [PB_BOXART_BOX]       = { 0x3CB4D8, 0x144C68, 151, "POKEMON",  "BOX RS"    },
        [PB_BOXART_HACK]      = { 0x6A5A4A, 0x1E1810, 384, "ROM HACK", "(custom)"  },
        [PB_BOXART_UNKNOWN]   = { 0x444444, 0x1A1A1A, 0,   "UNKNOWN",  ""          },
    };
    if (game < 0 || game >= (int)(sizeof themes / sizeof themes[0])) game = PB_BOXART_UNKNOWN;
    uint32_t top = themes[game].top;
    uint32_t bot = themes[game].bot;

    /* Vertical-gradient fill for the boxart. */
    int bands = 32;
    int band_h = h / bands;
    if (band_h < 1) band_h = 1;
    uint8_t tr = rch(top), tg = gch(top), tb = bch(top);
    uint8_t br = rch(bot), bg = gch(bot), bb = bch(bot);
    for (int i = 0; i < bands; i++) {
        float t = (float)i / (float)(bands - 1);
        uint8_t r = (uint8_t)(tr + (br - tr) * t);
        uint8_t g = (uint8_t)(tg + (bg - tg) * t);
        uint8_t b = (uint8_t)(tb + (bb - tb) * t);
        SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
        SDL_Rect rect = { x, y + i * band_h, w, band_h };
        SDL_RenderFillRect(g_renderer, &rect);
    }
    /* Frame */
    pb_gfx_border(x, y, w, h, 2, PB_GFX_COLOR_BORDER);
    pb_gfx_border(x + 4, y + 4, w - 8, h - 8, 1, PB_GFX_COLOR_TEXT_DIM);

    /* Cover sprite */
    uint16_t sp = themes[game].species;
    if (sp) {
        int sx = x + (w - 64) / 2;
        int sy = y + (h - 64) / 2 - 16;
        SDL_Texture *tex = sprite_for(sp, false);
        if (tex) {
            SDL_Rect dst = { sx, sy, 64, 64 };
            SDL_RenderCopy(g_renderer, tex, NULL, &dst);
        }
    }
    /* Title text (centered) */
    const char *t1 = themes[game].title;
    const char *t2 = themes[game].subtitle;
    int t1w = (int)strlen(t1) * 8;
    int t2w = (int)strlen(t2) * 8;
    pb_gfx_text(x + (w - t1w) / 2, y + h - 36, PB_GFX_COLOR_TEXT, t1);
    pb_gfx_text(x + (w - t2w) / 2, y + h - 22, PB_GFX_COLOR_TEXT_ACCENT, t2);
}
