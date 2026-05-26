#include "ui.h"
#include "legalizer.h"
#include "joybus.h"
#include "embedded_save.h"
#include "embedded_emerald_save.h"
#include "embedded_xd_save.h"
#include "embedded_colo_save.h"
#include "pb_xd.h"
#include "pb_colo.h"
#include "pb_gfx.h"
#include "endian_le.h"
#include <gccore.h>
#include <wiiuse/wpad.h>  /* harmless on GC; provides PAD_BUTTON_* via libogc */
#include <ogc/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

extern bool pb_sd_available;

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

void pb_ui_init(void) {
    VIDEO_Init();
    PAD_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

void pb_ui_clear(void) {
    /* VT100 clear-screen + home */
    printf("\x1b[2J\x1b[H");
}

void pb_ui_header(const char *title) {
    pb_ui_clear();
    printf("\x1b[36;1m== PokeBridge :: %s ==\x1b[0m\n\n", title);
}

void pb_ui_footer(const char *hint) {
    printf("\n\x1b[2m%s\x1b[0m\n", hint);
}

uint16_t pb_ui_wait_button(void) {
    while (1) {
        VIDEO_WaitVSync();
        PAD_ScanPads();
        uint16_t pressed = PAD_ButtonsDown(0);
        if (pressed) return pressed;
    }
}

static int list_saves(char paths[][256], int max) {
    DIR *d = opendir("sd:/pokebridge/saves");
    if (!d) {
        /* Try root /saves and root SD too. */
        d = opendir("sd:/saves");
        if (!d) d = opendir("sd:/");
        if (!d) return 0;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        size_t L = strlen(e->d_name);
        if (L > 4 && (strcasecmp(e->d_name + L - 4, ".sav") == 0
                  ||  strcasecmp(e->d_name + L - 4, ".sa1") == 0)) {
            snprintf(paths[n], 256, "sd:/pokebridge/saves/%s", e->d_name);
            n++;
        }
    }
    closedir(d);
    return n;
}

void pb_ui_show_save_summary(const pb_save_t *s) {
    pb_ui_header("Save loaded");
    printf("Game     : %s\n", pb_game_name(s->game));
    printf("Trainer  : %s\n", s->trainer_name);
    printf("TID/SID  : %05u / %05u\n", s->trainer_id, s->secret_id);
    printf("Gender   : %s\n", s->gender ? "F" : "M");
    printf("Save idx : %u\n", (unsigned)s->save_index);
    uint8_t party_count = 0;
    const uint8_t *party = NULL;
    if (pb_save_party(s, &party_count, &party)) {
        printf("Party    : %u/6\n", party_count);
    }
    pb_ui_footer("A: browse party   X: browse boxes   B: back");
}

void pb_ui_show_pkm(const pb_pkm_t *p) {
    pb_ui_header("Pokemon");
    if (p->is_empty) { printf("(empty slot)\n"); pb_ui_footer("B: back"); return; }
    char nickname[16] = {0};
    char otname[12] = {0};
    pb_gen3_to_ascii(p->nickname, 10, nickname);
    pb_gen3_to_ascii(p->ot_name, 7, otname);
    uint8_t ivs[6];
    pb_pkm_ivs(p, ivs);
    printf("Nickname : %s\n", nickname);
    printf("Species  : %s  (#%u)\n", pb_species_name(p->g.species), p->g.species);
    printf("OT       : %s   ID %05u\n", otname, p->ot_id & 0xFFFF);
    printf("Friend.  : %u   PID 0x%08X\n", p->g.friendship, (unsigned)p->pid);
    printf("Item     : %u\n", p->g.held_item);
    printf("Moves    : %u / %u / %u / %u\n",
           p->a.moves[0], p->a.moves[1], p->a.moves[2], p->a.moves[3]);
    printf("EVs      : %u/%u/%u/%u/%u/%u\n",
           p->e.ev[0], p->e.ev[1], p->e.ev[2],
           p->e.ev[3], p->e.ev[4], p->e.ev[5]);
    printf("IVs      : %u/%u/%u/%u/%u/%u\n",
           ivs[0], ivs[1], ivs[2], ivs[3], ivs[4], ivs[5]);
    printf("Checksum : %s\n", p->checksum_ok ? "OK" : "BAD");

    printf("Nature   : %s (#%u)\n", pb_nature_name(pb_pkm_get_nature(p)), pb_pkm_get_nature(p));
    printf("Shiny    : %s\n", pb_pkm_is_shiny(p) ? "YES" : "no");
    if (p->g.species > 386) {
        printf("\n\x1b[33m! Hack-mon (species %u). Press Y to legalize.\x1b[0m\n", p->g.species);
    }
    pb_ui_footer("X: edit   B: back   Y: legalize (writes .pk3)");
}

/* ---- Editor sub-screens ---- */

static const char *stat_names[6] = { "HP ", "Atk", "Def", "Spe", "SpA", "SpD" };

static void edit_stat_screen(pb_pkm_t *p, const char *title, int max_val,
                             uint8_t (*getter)(const pb_pkm_t *, int),
                             void (*setter)(pb_pkm_t *, int, uint8_t)) {
    int sel = 0;
    for (;;) {
        pb_ui_header(title);
        for (int i = 0; i < 6; i++) {
            const char *cur = (i == sel) ? "\x1b[32m>\x1b[0m" : " ";
            printf("  %s %s : %3u\n", cur, stat_names[i], getter(p, i));
        }
        pb_ui_footer("DPad: select   L/R: -/+   A: max   START: zero   B: done");
        uint16_t bt = pb_ui_wait_button();
        if (bt & PAD_BUTTON_B) return;
        if (bt & PAD_BUTTON_UP)   sel = (sel + 5) % 6;
        if (bt & PAD_BUTTON_DOWN) sel = (sel + 1) % 6;
        uint8_t v = getter(p, sel);
        if (bt & PAD_TRIGGER_L)   setter(p, sel, v > 0 ? v - 1 : 0);
        if (bt & PAD_TRIGGER_R)   setter(p, sel, v < max_val ? v + 1 : max_val);
        if (bt & PAD_BUTTON_LEFT) setter(p, sel, v >= 10 ? v - 10 : 0);
        if (bt & PAD_BUTTON_RIGHT)setter(p, sel, v + 10 <= max_val ? v + 10 : max_val);
        if (bt & PAD_BUTTON_A)    setter(p, sel, max_val);
        if (bt & PAD_BUTTON_START) setter(p, sel, 0);
    }
}

/* Move picker -- 354 moves paged 12 at a time. Returns -1 on cancel,
 * else the chosen move id (0..354). */
static int pick_move(int current) {
    int sel = current > 354 ? 0 : current;
    const int per_page = 12;
    for (;;) {
        int page = sel / per_page;
        int start = page * per_page;
        pb_ui_header("Pick a move");
        printf("Page %d/%d   (current: %u %s)\n\n",
               page + 1, (355 + per_page - 1) / per_page, current, pb_move_name(current));
        for (int i = 0; i < per_page; i++) {
            int idx = start + i;
            if (idx > 354) break;
            const char *cur = (idx == sel) ? "\x1b[32m>\x1b[0m" : " ";
            printf("  %s #%3d  %s\n", cur, idx, pb_move_name((uint16_t)idx));
        }
        pb_ui_footer("DPad: select   L/R: page   A: pick   Y: clear   B: cancel");
        uint16_t bt = pb_ui_wait_button();
        if (bt & PAD_BUTTON_B) return -1;
        if (bt & PAD_BUTTON_Y) return 0;
        if (bt & PAD_BUTTON_A) return sel;
        if (bt & PAD_BUTTON_UP)   sel = (sel > 0) ? sel - 1 : 354;
        if (bt & PAD_BUTTON_DOWN) sel = (sel < 354) ? sel + 1 : 0;
        if (bt & PAD_TRIGGER_L)   sel = (sel >= per_page) ? sel - per_page : 0;
        if (bt & PAD_TRIGGER_R)   sel = (sel + per_page <= 354) ? sel + per_page : 354;
    }
}

static void edit_moves_screen(pb_pkm_t *p) {
    int sel = 0;
    for (;;) {
        pb_ui_header("Edit moves");
        for (int i = 0; i < 4; i++) {
            const char *cur = (i == sel) ? "\x1b[32m>\x1b[0m" : " ";
            printf("  %s Move %d : #%3u  %s\n", cur, i + 1,
                   p->a.moves[i], pb_move_name(p->a.moves[i]));
        }
        pb_ui_footer("DPad: select   A: change   B: done");
        uint16_t bt = pb_ui_wait_button();
        if (bt & PAD_BUTTON_B) return;
        if (bt & PAD_BUTTON_UP)   sel = (sel + 3) % 4;
        if (bt & PAD_BUTTON_DOWN) sel = (sel + 1) % 4;
        if (bt & PAD_BUTTON_A) {
            int picked = pick_move(p->a.moves[sel]);
            if (picked >= 0) pb_pkm_set_move(p, sel, (uint16_t)picked);
        }
    }
}

bool pb_ui_edit_pkm(pb_pkm_t *p) {
    if (!p || p->is_empty) return false;
    int sel = 0;
    /* Fields: 0=IVs, 1=EVs, 2=Moves, 3=Nature, 4=Shiny, 5=Friendship, 6=Item */
    static const char *fields[] = {
        "IVs", "EVs", "Moves", "Nature", "Shiny", "Friendship", "Held item"
    };
    const int nfields = (int)(sizeof fields / sizeof fields[0]);
    for (;;) {
        pb_ui_header("Edit Pokemon");
        char nickname[16] = {0};
        pb_gen3_to_ascii(p->nickname, 10, nickname);
        printf("\"%s\"  %s (#%u)\n\n", nickname, pb_species_name(p->g.species), p->g.species);

        for (int i = 0; i < nfields; i++) {
            const char *cur = (i == sel) ? "\x1b[32m>\x1b[0m" : " ";
            printf("  %s %-12s : ", cur, fields[i]);
            switch (i) {
                case 0: { /* IVs */
                    for (int s = 0; s < 6; s++) printf("%2u ", pb_pkm_get_iv(p, s));
                    break;
                }
                case 1: { /* EVs */
                    for (int s = 0; s < 6; s++) printf("%3u ", pb_pkm_get_ev(p, s));
                    break;
                }
                case 2: { /* Moves */
                    for (int m = 0; m < 4; m++) printf("%u ", p->a.moves[m]);
                    break;
                }
                case 3: printf("%s (#%u)", pb_nature_name(pb_pkm_get_nature(p)),
                                          pb_pkm_get_nature(p)); break;
                case 4: printf("%s", pb_pkm_is_shiny(p) ? "YES" : "no"); break;
                case 5: printf("%u", p->g.friendship); break;
                case 6: printf("%u", p->g.held_item); break;
            }
            printf("\n");
        }
        pb_ui_footer("DPad: select   A: edit   L/R: -/+   Y: save+back   B: cancel");
        uint16_t bt = pb_ui_wait_button();
        if (bt & PAD_BUTTON_B) return false;
        if (bt & PAD_BUTTON_Y) return true;
        if (bt & PAD_BUTTON_UP)   sel = (sel + nfields - 1) % nfields;
        if (bt & PAD_BUTTON_DOWN) sel = (sel + 1) % nfields;

        if (bt & PAD_BUTTON_A) {
            switch (sel) {
                case 0: edit_stat_screen(p, "Edit IVs", 31, pb_pkm_get_iv, pb_pkm_set_iv); break;
                case 1: edit_stat_screen(p, "Edit EVs", 255, pb_pkm_get_ev, pb_pkm_set_ev); break;
                case 2: edit_moves_screen(p); break;
                case 3: { /* Nature picker */
                    int nsel = pb_pkm_get_nature(p);
                    for (;;) {
                        pb_ui_header("Pick a nature");
                        printf("(Re-roll preserves shiny status.)\n\n");
                        for (int i = 0; i < 25; i++) {
                            const char *cur = (i == nsel) ? "\x1b[32m>\x1b[0m" : " ";
                            printf("  %s %-8s", cur, pb_nature_name((uint8_t)i));
                            if ((i % 3) == 2 || i == 24) printf("\n");
                        }
                        pb_ui_footer("DPad: select   A: pick   B: cancel");
                        uint16_t nb = pb_ui_wait_button();
                        if (nb & PAD_BUTTON_B) break;
                        if (nb & PAD_BUTTON_UP)    nsel = (nsel + 22) % 25;
                        if (nb & PAD_BUTTON_DOWN)  nsel = (nsel + 3) % 25;
                        if (nb & PAD_BUTTON_LEFT)  nsel = (nsel + 24) % 25;
                        if (nb & PAD_BUTTON_RIGHT) nsel = (nsel + 1) % 25;
                        if (nb & PAD_BUTTON_A) { pb_pkm_set_nature(p, (uint8_t)nsel); break; }
                    }
                    break;
                }
                case 4: pb_pkm_toggle_shiny(p, !pb_pkm_is_shiny(p)); break;
                case 5: p->g.friendship = 255; break;
                case 6: p->g.held_item = 0; break;
            }
        }
        if (sel == 5) {
            uint8_t f = p->g.friendship;
            if (bt & PAD_TRIGGER_L) p->g.friendship = f > 0 ? f - 1 : 0;
            if (bt & PAD_TRIGGER_R) p->g.friendship = f < 255 ? f + 1 : 255;
        }
        if (sel == 6) {
            uint16_t it = p->g.held_item;
            if (bt & PAD_TRIGGER_L) p->g.held_item = it > 0 ? it - 1 : 0;
            if (bt & PAD_TRIGGER_R) p->g.held_item = it + 1;
        }
    }
}

void pb_ui_browse_party(pb_save_t *s) {
    uint8_t count = 0;
    const uint8_t *party_c = NULL;
    if (!pb_save_party(s, &count, &party_c) || count == 0) {
        pb_ui_header("Party");
        printf("No party found.\n");
        pb_ui_footer("B: back");
        pb_ui_wait_button();
        return;
    }
    /* Cast away const: pb_save_party hands us a pointer into the save buffer
     * which we DO want to mutate via the editor. */
    uint8_t *party = (uint8_t *)party_c;
    int sel = 0;
    for (;;) {
        pb_ui_header("Party");
        for (int i = 0; i < count; i++) {
            pb_pkm_t p;
            pb_pkm_decode(&p, party + i * PB_PKM_PARTY_SIZE);
            char nickname[16] = {0};
            pb_gen3_to_ascii(p.nickname, 10, nickname);
            printf("%s %d. %-12s  %s (#%u)%s\n",
                   i == sel ? "\x1b[32m>\x1b[0m" : " ", i + 1,
                   nickname, pb_species_name(p.g.species), p.g.species,
                   pb_pkm_is_shiny(&p) ? " *" : "");
        }
        pb_ui_footer("D-Pad: select   A: view   B: back");
        uint16_t b = pb_ui_wait_button();
        if (b & PAD_BUTTON_B) return;
        if (b & PAD_BUTTON_UP)   { sel = (sel + count - 1) % count; }
        if (b & PAD_BUTTON_DOWN) { sel = (sel + 1) % count; }
        if (b & PAD_BUTTON_A) {
            pb_pkm_t p;
            uint8_t *slot_raw = party + sel * PB_PKM_PARTY_SIZE;
            pb_pkm_decode(&p, slot_raw);
            pb_ui_show_pkm(&p);
            uint16_t b2 = pb_ui_wait_button();
            if (b2 & PAD_BUTTON_X && !p.is_empty) {
                if (pb_ui_edit_pkm(&p)) {
                    /* User confirmed -- re-encode and write back. */
                    pb_pkm_encode(&p, slot_raw);
                    pb_save_update_section_checksum(s, 1);
                    pb_ui_header("Saved");
                    printf("Edits written to in-memory save.\n");
                    if (pb_sd_available) {
                        mkdir("sd:/pokebridge", 0777);
                        mkdir("sd:/pokebridge/saves", 0777);
                        if (pb_save_write_file(s, "sd:/pokebridge/saves/edited.sav")) {
                            printf("Wrote sd:/pokebridge/saves/edited.sav\n");
                        } else {
                            printf("(could not write to SD)\n");
                        }
                    } else {
                        printf("(no SD card -- edit lives only in this session)\n");
                    }
                    pb_ui_footer("Press any button");
                    pb_ui_wait_button();
                }
            } else if (b2 & PAD_BUTTON_Y && p.g.species > 386) {
                uint8_t out80[80];
                pb_legalize_report_t r;
                if (pb_legalize(&p, out80, &r)) {
                    if (pb_sd_available) {
                        mkdir("sd:/pokebridge", 0777);
                        mkdir("sd:/pokebridge/export", 0777);
                        char path[128];
                        snprintf(path, sizeof path,
                                 "sd:/pokebridge/export/party%d_sp%u.pk3",
                                 sel + 1, p.g.species);
                        FILE *f = fopen(path, "wb");
                        if (f) { fwrite(out80, 1, 80, f); fclose(f); }
                    }
                    pb_ui_header("Legalized");
                    printf("%u -> %u  (%d moves remapped, %d dropped)\n",
                           r.orig_species, r.legal_species,
                           r.moves_remapped, r.moves_dropped);
                    if (!pb_sd_available) printf("(no SD; .pk3 not written)\n");
                    pb_ui_footer("Press any button");
                    pb_ui_wait_button();
                }
            }
        }
    }
}

void pb_ui_browse_boxes(pb_save_t *s) {
    int box = 0, slot = 0;
    for (;;) {
        pb_ui_header("PC Boxes");
        printf("Box %d/14\n\n", box + 1);
        const uint8_t *b = pb_save_box(s, box);
        if (!b) { printf("(missing)\n"); pb_ui_wait_button(); return; }
        /* Show 10 mons per page (slot pages 0–2). */
        int page = slot / 10;
        for (int i = 0; i < 10; i++) {
            int idx = page * 10 + i;
            if (idx >= 30) break;
            pb_pkm_t p;
            pb_pkm_decode(&p, b + idx * PB_PKM_BOX_SIZE);
            char nickname[16] = {0};
            pb_gen3_to_ascii(p.nickname, 10, nickname);
            const char *cur = (idx == slot) ? "\x1b[32m>\x1b[0m" : " ";
            if (p.is_empty) printf("%s %2d. ----\n", cur, idx + 1);
            else            printf("%s %2d. %-12s  %s (#%u)\n", cur, idx + 1,
                                   nickname, pb_species_name(p.g.species), p.g.species);
        }
        pb_ui_footer("DPad: nav   L/R: box   A: view   B: back");
        uint16_t bt = pb_ui_wait_button();
        if (bt & PAD_BUTTON_B) return;
        if (bt & PAD_BUTTON_UP)    slot = (slot + 30 - 1) % 30;
        if (bt & PAD_BUTTON_DOWN)  slot = (slot + 1) % 30;
        if (bt & PAD_TRIGGER_L)    box  = (box + 14 - 1) % 14;
        if (bt & PAD_TRIGGER_R)    box  = (box + 1) % 14;
        if (bt & PAD_BUTTON_A) {
            pb_pkm_t p;
            pb_pkm_decode(&p, b + slot * PB_PKM_BOX_SIZE);
            pb_ui_show_pkm(&p);
            pb_ui_wait_button();
        }
    }
}

/* Forward decls for the graphics-mode pokemon detail + editor. */
typedef enum { PB_FMT_PK3 = 0, PB_FMT_XK3 = 1, PB_FMT_CK3 = 2 } pb_fmt_t;
static bool gfx_show_pkm_detail(pb_pkm_t *p, uint8_t *raw, pb_fmt_t fmt);
static bool gfx_edit_pkm(pb_pkm_t *p);
static void gfx_pkm_box_screen(pb_save_t *s);
static void gfx_xd_box_screen(pb_xd_save_t *xs);
static void gfx_colo_party_screen(pb_colo_save_t *cs);
static void gfx_colo_box_screen(pb_colo_save_t *cs);

/* Detect a save file's game by header / size. */
static pb_boxart_t detect_boxart_from_save(const pb_save_t *s) {
    switch (s->game) {
        case PB_GAME_RS:      return PB_BOXART_RUBY;       /* generic R/S */
        case PB_GAME_FRLG:    return PB_BOXART_FIRERED;    /* generic FR/LG */
        case PB_GAME_EMERALD: return PB_BOXART_EMERALD;
        case PB_GAME_HACK:    return PB_BOXART_HACK;
        default:              return PB_BOXART_UNKNOWN;
    }
}

/* Lenient size detection: GameCube saves come with various wrapper headers
 * (DATELGC_SAVE = 192 bytes, GCSAVE = 336 bytes, raw .gci = 0). Treat any
 * file in [body, body + 1024] as matching. */
static bool size_in_range(long sz, long body) {
    return sz >= body && sz <= body + 1024;
}

/* Quickly determine which Gen 3 / GameCube game a save belongs to. */
static pb_boxart_t detect_boxart_by_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return PB_BOXART_UNKNOWN;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size_in_range(sz, 0x76000)) { fclose(f); return PB_BOXART_BOX; }
    if (size_in_range(sz, 0x60000)) { fclose(f); return PB_BOXART_COLOSSEUM; }
    if (size_in_range(sz, PB_XD_SAVE_SIZE)) { fclose(f); return PB_BOXART_XD; }

    /* Gen 3 GBA save: typically 128 KB (or 64 KB half). Some emulator tools
     * add a small trailer (mGBA appends 16 bytes), so accept ranges. */
    if (sz < PB_SAVE_SIZE / 2 || sz > PB_SAVE_SIZE + 1024) {
        fclose(f); return PB_BOXART_UNKNOWN;
    }
    static uint8_t slot[PB_SLOT_SIZE];
    if (fread(slot, 1, PB_SLOT_SIZE, f) != PB_SLOT_SIZE) {
        fclose(f); return PB_BOXART_UNKNOWN;
    }
    fclose(f);
    for (int phys = 0; phys < PB_SECTION_COUNT; phys++) {
        const uint8_t *sec = slot + phys * PB_SECTION_SIZE;
        if (pb_rd_u32le(sec + PB_SECTION_FOOTER + 4) != PB_SECTION_SIG) continue;
        if (pb_rd_u16le(sec + PB_SECTION_FOOTER + 0) != 0) continue;
        uint32_t code = pb_rd_u32le(sec + 0xAC);
        if (code == 0) return PB_BOXART_RUBY;
        if (code == 1) return PB_BOXART_FIRERED;
        return PB_BOXART_EMERALD;
    }
    return PB_BOXART_HACK;
}

/* ---- Pokemon Box-style graphics mode for XD ---- */

static void gfx_draw_title_bar(const char *title) {
    pb_gfx_rounded_panel(16, 12, 608, 40, 10, PB_GFX_COLOR_PANEL, 230);
    pb_gfx_border(16, 12, 608, 40, 1, PB_GFX_COLOR_PANEL_LIGHT);
    pb_gfx_text(32, 28, PB_GFX_COLOR_TEXT_ACCENT, "POKEBRIDGE");
    pb_gfx_text(132, 28, PB_GFX_COLOR_TEXT_DIM, "*");
    pb_gfx_text(148, 28, PB_GFX_COLOR_TEXT, title);
}

static void gfx_draw_hint_bar(const char *hint) {
    pb_gfx_rounded_panel(16, 444, 608, 24, 6, PB_GFX_COLOR_PANEL, 200);
    pb_gfx_text(28, 452, PB_GFX_COLOR_TEXT_DIM, hint);
}

static void gfx_draw_panel(int x, int y, int w, int h, const char *title) {
    /* Shadow behind */
    pb_gfx_rounded_panel(x + 3, y + 3, w, h, 10, PB_GFX_COLOR_SHADOW, 100);
    /* Panel body */
    pb_gfx_rounded_panel(x, y, w, h, 10, PB_GFX_COLOR_PANEL, 235);
    pb_gfx_border(x, y, w, h, 1, PB_GFX_COLOR_PANEL_LIGHT);
    if (title) {
        /* Title chip */
        pb_gfx_rounded_panel(x + 10, y - 8, pb_gfx_text_width(title) + 20, 18, 6, PB_GFX_COLOR_PANEL_LIGHT, 240);
        pb_gfx_text(x + 20, y - 4, PB_GFX_COLOR_PANEL, title);
    }
}

/* The big graphics-mode XD party viewer. Renders Pokemon Box-inspired layout
 * with a title bar, 6 sprite slots at the top, and a detail panel below. */
static void gfx_xd_party_screen(pb_xd_save_t *xs) {
    int sel = 0;
    for (;;) {
        pb_gfx_clear();

        char title[64];
        snprintf(title, sizeof title, "XD Party - %s", xs->trainer_name);
        gfx_draw_title_bar(title);

        /* Decode all six slots so we can render selection metadata. */
        pb_xd_pkm_t party[6];
        for (int i = 0; i < 6; i++) {
            pb_xd_pkm_decode(&party[i], xs->slot + xs->party_offset + i * PB_XD_PKM_SIZE);
        }

        /* Trainer info card (left) */
        gfx_draw_panel(28, 70, 168, 110, "TRAINER");
        pb_gfx_text(40, 100, PB_GFX_COLOR_TEXT, xs->trainer_name);
        char tidbuf[32];
        snprintf(tidbuf, sizeof tidbuf, "ID %05u", xs->trainer_id);
        pb_gfx_text(40, 120, PB_GFX_COLOR_TEXT_DIM, tidbuf);
        snprintf(tidbuf, sizeof tidbuf, "SID %05u", xs->secret_id);
        pb_gfx_text(40, 138, PB_GFX_COLOR_TEXT_DIM, tidbuf);
        snprintf(tidbuf, sizeof tidbuf, "Saves: %u", (unsigned)xs->save_count);
        pb_gfx_text(40, 156, PB_GFX_COLOR_TEXT_DIM, tidbuf);

        /* Party slots row (right of trainer card). Slots are 64x64 with 8 px
         * gap, six wide = 6*64 + 5*8 = 424 px row width. */
        gfx_draw_panel(212, 70, 412, 110, "PARTY");
        int row_x = 218, row_y = 96;
        for (int i = 0; i < 6; i++) {
            int sx = row_x + i * 72;
            pb_gfx_pkm_slot(sx, row_y, party[i].species_internal,
                            i == sel, false);
        }

        /* Detail panel for the selected mon */
        gfx_draw_panel(28, 210, 596, 222, "DETAIL");
        pb_xd_pkm_t *p = &party[sel];
        if (p->is_empty) {
            pb_gfx_text(48, 238, PB_GFX_COLOR_TEXT_DIM, "(empty slot)");
        } else {
            /* Big sprite slot on the left of detail (also 64x64). */
            pb_gfx_pkm_slot(48, 238, p->species_internal, false, false);
            /* Name + level */
            char buf[64];
            snprintf(buf, sizeof buf, "%s", p->nickname);
            pb_gfx_text_scale(128, 238, PB_GFX_COLOR_TEXT_ACCENT, 2, buf);
            snprintf(buf, sizeof buf, "L%u  XD#%u  %s", p->level,
                     p->species_internal, p->is_shadow ? "[SHADOW]" : "");
            pb_gfx_text(124, 260, PB_GFX_COLOR_TEXT, buf);
            snprintf(buf, sizeof buf, "OT %s  ID %05u", p->ot_name, p->trainer_id);
            pb_gfx_text(124, 278, PB_GFX_COLOR_TEXT_DIM, buf);
            snprintf(buf, sizeof buf, "PID 0x%08X", (unsigned)p->pid);
            pb_gfx_text(124, 294, PB_GFX_COLOR_TEXT_DIM, buf);

            /* Moves block */
            pb_gfx_text(48, 326, PB_GFX_COLOR_TEXT_ACCENT, "MOVES");
            for (int m = 0; m < 4; m++) {
                int mx = 48 + (m % 2) * 280;
                int my = 344 + (m / 2) * 18;
                snprintf(buf, sizeof buf, "%d. #%-3u %s", m + 1,
                         p->moves[m], pb_move_name(p->moves[m]));
                pb_gfx_text(mx, my, PB_GFX_COLOR_TEXT, buf);
            }

            /* IVs strip */
            pb_gfx_text(48, 392, PB_GFX_COLOR_TEXT_ACCENT, "IVs");
            snprintf(buf, sizeof buf, "%2u/%2u/%2u/%2u/%2u/%2u",
                     p->iv[0], p->iv[1], p->iv[2], p->iv[3], p->iv[4], p->iv[5]);
            pb_gfx_text(88, 392, PB_GFX_COLOR_TEXT, buf);
            pb_gfx_text(48, 410, PB_GFX_COLOR_TEXT_DIM,
                        "(HP / Atk / Def / Spe / SpA / SpD)");
        }

        gfx_draw_hint_bar("D-Pad / L-R: select   B: back   (gfx demo)");

        pb_gfx_flip();
        uint16_t b = pb_gfx_wait_button();
        if (b & PAD_BUTTON_B) break;
        if (b & PAD_BUTTON_LEFT)  sel = (sel + 5) % 6;
        if (b & PAD_BUTTON_RIGHT) sel = (sel + 1) % 6;
        if (b & PAD_BUTTON_UP)    sel = (sel + 5) % 6;
        if (b & PAD_BUTTON_DOWN)  sel = (sel + 1) % 6;
        if (b & PAD_TRIGGER_L)    sel = (sel + 5) % 6;
        if (b & PAD_TRIGGER_R)    sel = (sel + 1) % 6;
        if (b & PAD_BUTTON_A) {
            uint8_t *raw = xs->slot + xs->party_offset + sel * PB_XD_PKM_SIZE;
            pb_pkm_t p; pb_xk3_to_pkm(&p, raw);
            if (!p.is_empty) gfx_show_pkm_detail(&p, raw, PB_FMT_XK3);
        }
    }
}

/* ---- Graphics-mode full-screen Pokémon detail (drills into a party slot) ---- */

static bool gfx_show_pkm_detail(pb_pkm_t *p, uint8_t *raw, pb_fmt_t fmt) {
    if (p->is_empty) return false;
    bool edited = false;
    for (;;) {
        pb_gfx_clear();

        char nick[16] = {0};
        pb_gen3_to_ascii(p->nickname, 10, nick);
        char title[64];
        snprintf(title, sizeof title, "%s  L?  %s",
                 nick[0] ? nick : pb_species_name(p->g.species),
                 pb_pkm_is_shiny(p) ? "* SHINY" : "");
        gfx_draw_title_bar(title);

        /* Big sprite card on the left */
        gfx_draw_panel(28, 70, 200, 200, NULL);
        pb_gfx_pkm_slot(60, 100, p->g.species, false, pb_pkm_is_shiny(p));
        pb_gfx_text(60, 180, PB_GFX_COLOR_TEXT_ACCENT, pb_species_name(p->g.species));
        char buf[80];
        snprintf(buf, sizeof buf, "#%u", p->g.species);
        pb_gfx_text(60, 198, PB_GFX_COLOR_TEXT_DIM, buf);
        snprintf(buf, sizeof buf, "%s", pb_nature_name(pb_pkm_get_nature(p)));
        pb_gfx_text(60, 216, PB_GFX_COLOR_TEXT, buf);

        /* Stats panel on the right */
        gfx_draw_panel(244, 70, 380, 200, "STATS");
        char ot[12] = {0};
        pb_gen3_to_ascii(p->ot_name, 7, ot);
        snprintf(buf, sizeof buf, "OT     %s", ot);
        pb_gfx_text(260, 100, PB_GFX_COLOR_TEXT, buf);
        snprintf(buf, sizeof buf, "ID     %05u", (unsigned)(p->ot_id & 0xFFFF));
        pb_gfx_text(260, 118, PB_GFX_COLOR_TEXT_DIM, buf);
        snprintf(buf, sizeof buf, "PID    0x%08X", (unsigned)p->pid);
        pb_gfx_text(260, 136, PB_GFX_COLOR_TEXT_DIM, buf);
        snprintf(buf, sizeof buf, "Item   %u", p->g.held_item);
        pb_gfx_text(260, 154, PB_GFX_COLOR_TEXT, buf);
        snprintf(buf, sizeof buf, "Friend %u", p->g.friendship);
        pb_gfx_text(260, 172, PB_GFX_COLOR_TEXT, buf);
        snprintf(buf, sizeof buf, "Exp    %u", (unsigned)p->g.experience);
        pb_gfx_text(260, 190, PB_GFX_COLOR_TEXT, buf);

        uint8_t ivs[6]; pb_pkm_ivs(p, ivs);
        pb_gfx_text(260, 220, PB_GFX_COLOR_TEXT_ACCENT, "IVs   ");
        snprintf(buf, sizeof buf, "%2u/%2u/%2u/%2u/%2u/%2u",
                 ivs[0], ivs[1], ivs[2], ivs[3], ivs[4], ivs[5]);
        pb_gfx_text(312, 220, PB_GFX_COLOR_TEXT, buf);
        pb_gfx_text(260, 238, PB_GFX_COLOR_TEXT_ACCENT, "EVs   ");
        snprintf(buf, sizeof buf, "%2u/%2u/%2u/%2u/%2u/%2u",
                 p->e.ev[0], p->e.ev[1], p->e.ev[2],
                 p->e.ev[3], p->e.ev[4], p->e.ev[5]);
        pb_gfx_text(312, 238, PB_GFX_COLOR_TEXT, buf);

        /* Moves panel */
        gfx_draw_panel(28, 290, 596, 130, "MOVES");
        for (int m = 0; m < 4; m++) {
            int mx = 48 + (m % 2) * 290;
            int my = 320 + (m / 2) * 28;
            snprintf(buf, sizeof buf, "%d.  %s", m + 1, pb_move_name(p->a.moves[m]));
            pb_gfx_text(mx, my,     PB_GFX_COLOR_TEXT, buf);
            snprintf(buf, sizeof buf, "    #%-3u", p->a.moves[m]);
            pb_gfx_text(mx, my + 12, PB_GFX_COLOR_TEXT_DIM, buf);
        }

        if (p->g.species > 386) {
            pb_gfx_text(28, 432, PB_GFX_COLOR_PANEL_ACCENT,
                        "! Hack-mon -- press Y to legalize");
        }
        gfx_draw_hint_bar("X: edit   Y: legalize (if hack-mon)   B: back");
        pb_gfx_flip();

        uint16_t b = pb_gfx_wait_button();
        if (b & PAD_BUTTON_B) return edited;
        if (b & PAD_BUTTON_X) {
            if (gfx_edit_pkm(p)) {
                edited = true;
                if (raw) {
                    if (fmt == PB_FMT_PK3)      pb_pkm_encode(p, raw);
                    else if (fmt == PB_FMT_XK3) pb_xk3_apply_pkm_edits(p, raw);
                    else if (fmt == PB_FMT_CK3) pb_ck3_apply_pkm_edits(p, raw);
                }
            }
        }
        if ((b & PAD_BUTTON_Y) && p->g.species > 386) {
            /* Legalizer preview screen. */
            uint16_t new_sp = pb_species_remap(p->g.species);
            for (;;) {
                pb_gfx_clear();
                gfx_draw_title_bar("Legalize");
                gfx_draw_panel(28, 70, 596, 360, NULL);

                /* Before / After columns */
                pb_gfx_text(80, 90, PB_GFX_COLOR_TEXT_ACCENT, "BEFORE  (hack)");
                pb_gfx_pkm_slot(80, 120, p->g.species, false, pb_pkm_is_shiny(p));
                char buf[64];
                snprintf(buf, sizeof buf, "#%u", p->g.species);
                pb_gfx_text(80, 200, PB_GFX_COLOR_TEXT, buf);
                pb_gfx_text(80, 220, PB_GFX_COLOR_TEXT_DIM, "(no Gen 3 entry)");

                /* Arrow */
                pb_gfx_text(220, 152, PB_GFX_COLOR_TEXT_ACCENT, "----->");

                pb_gfx_text(340, 90, PB_GFX_COLOR_TEXT_ACCENT, "AFTER  (Gen 3 legal)");
                pb_gfx_pkm_slot(340, 120, new_sp, false, pb_pkm_is_shiny(p));
                snprintf(buf, sizeof buf, "%s  #%u", pb_species_name(new_sp), new_sp);
                pb_gfx_text(340, 200, PB_GFX_COLOR_TEXT, buf);
                pb_gfx_text(340, 220, PB_GFX_COLOR_TEXT_DIM, "(HOME-eligible chain)");

                /* Effect description */
                pb_gfx_text(80, 280, PB_GFX_COLOR_TEXT, "Output: 80-byte .pk3 file");
                if (pb_sd_available) {
                    pb_gfx_text(80, 298, PB_GFX_COLOR_TEXT, "Location: sd:/pokebridge/export/");
                } else {
                    pb_gfx_text(80, 298, PB_GFX_COLOR_TEXT_DIM,
                                "(No SD - .pk3 will not be written this run)");
                }
                pb_gfx_text(80, 326, PB_GFX_COLOR_TEXT_DIM, "Moves out of Gen 3 range will be cleared.");
                pb_gfx_text(80, 344, PB_GFX_COLOR_TEXT_DIM, "Held items out of Gen 3 range will be cleared.");
                pb_gfx_text(80, 362, PB_GFX_COLOR_PANEL_ACCENT, "Press A to confirm, B to cancel.");

                gfx_draw_hint_bar("A: legalize+export   B: cancel");
                pb_gfx_flip();
                uint16_t bb = pb_gfx_wait_button();
                if (bb & PAD_BUTTON_B) break;
                if (bb & PAD_BUTTON_A) {
                    uint8_t out80[80];
                    pb_legalize_report_t r;
                    if (pb_legalize(p, out80, &r) && pb_sd_available) {
                        mkdir("sd:/pokebridge", 0777);
                        mkdir("sd:/pokebridge/export", 0777);
                        char path[128];
                        snprintf(path, sizeof path,
                                 "sd:/pokebridge/export/sp%u_to_%u.pk3",
                                 r.orig_species, r.legal_species);
                        FILE *f = fopen(path, "wb");
                        if (f) { fwrite(out80, 1, 80, f); fclose(f); }
                    }
                    /* Briefly show result */
                    pb_gfx_clear();
                    gfx_draw_title_bar("Legalized");
                    gfx_draw_panel(120, 150, 400, 180, NULL);
                    pb_gfx_text(140, 180, PB_GFX_COLOR_TEXT_ACCENT, "Done!");
                    char res[64];
                    snprintf(res, sizeof res, "%u -> %u (moves remapped: %d)",
                             p->g.species, new_sp, 0);
                    pb_gfx_text(140, 210, PB_GFX_COLOR_TEXT, res);
                    if (pb_sd_available) {
                        pb_gfx_text(140, 240, PB_GFX_COLOR_TEXT_DIM, "Wrote .pk3 to sd:/");
                    } else {
                        pb_gfx_text(140, 240, PB_GFX_COLOR_TEXT_DIM, "(no SD - exported in memory only)");
                    }
                    gfx_draw_hint_bar("Press any button");
                    pb_gfx_flip();
                    pb_gfx_wait_button();
                    break;
                }
            }
        }
    }
}

/* ---- Graphics-mode editor sub-screens ---- */

static const char *g_stat_short[6] = { "HP ", "Atk", "Def", "Spe", "SpA", "SpD" };

static void gfx_edit_stat_screen(pb_pkm_t *p, const char *title, int max_val,
                                 uint8_t (*getter)(const pb_pkm_t *, int),
                                 void (*setter)(pb_pkm_t *, int, uint8_t)) {
    int sel = 0;
    for (;;) {
        pb_gfx_clear();
        gfx_draw_title_bar(title);

        /* Live preview of mon on left */
        gfx_draw_panel(28, 70, 200, 290, NULL);
        pb_gfx_pkm_slot(60, 100, p->g.species, false, pb_pkm_is_shiny(p));
        pb_gfx_text(60, 180, PB_GFX_COLOR_TEXT_ACCENT, pb_species_name(p->g.species));

        /* Stat editor list on right */
        gfx_draw_panel(244, 70, 380, 290, "STATS");
        for (int i = 0; i < 6; i++) {
            int yy = 110 + i * 32;
            uint32_t col = (i == sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
            if (i == sel) {
                pb_gfx_rounded_panel(255, yy - 6, 358, 24, 6, PB_GFX_COLOR_PANEL_LIGHT, 200);
            }
            char buf[64];
            uint8_t v = getter(p, i);
            snprintf(buf, sizeof buf, "%s    %3u", g_stat_short[i], v);
            pb_gfx_text(275, yy, col, buf);
            /* draw a tiny progress bar */
            int barx = 380, bary = yy + 4, barw = 200;
            pb_gfx_fill_rect(barx, bary, barw, 4, PB_GFX_COLOR_PANEL_LIGHT);
            int fill = (v * barw) / max_val;
            pb_gfx_fill_rect(barx, bary, fill, 4, PB_GFX_COLOR_PANEL_ACCENT);
        }

        gfx_draw_hint_bar("DPad: pick   L/R: -/+   A: max   START: zero   B: done");
        pb_gfx_flip();

        uint16_t bt = pb_gfx_wait_button();
        if (bt & PAD_BUTTON_B) return;
        if (bt & PAD_BUTTON_UP)   sel = (sel + 5) % 6;
        if (bt & PAD_BUTTON_DOWN) sel = (sel + 1) % 6;
        uint8_t v = getter(p, sel);
        if (bt & PAD_TRIGGER_L)    setter(p, sel, v > 0 ? v - 1 : 0);
        if (bt & PAD_TRIGGER_R)    setter(p, sel, v < max_val ? v + 1 : (uint8_t)max_val);
        if (bt & PAD_BUTTON_LEFT)  setter(p, sel, v >= 10 ? v - 10 : 0);
        if (bt & PAD_BUTTON_RIGHT) setter(p, sel, v + 10 <= max_val ? v + 10 : (uint8_t)max_val);
        if (bt & PAD_BUTTON_A)     setter(p, sel, (uint8_t)max_val);
        if (bt & PAD_BUTTON_START) setter(p, sel, 0);
    }
}

static int gfx_pick_move(int current) {
    int sel = current > 354 ? 0 : current;
    const int per_page = 14;
    for (;;) {
        int page = sel / per_page;
        int start = page * per_page;
        pb_gfx_clear();
        gfx_draw_title_bar("Pick a move");
        gfx_draw_panel(60, 70, 520, 360, "MOVES");
        char buf[80];
        snprintf(buf, sizeof buf, "Page %d / %d   (currently: #%u %s)",
                 page + 1, (355 + per_page - 1) / per_page,
                 (unsigned)current, pb_move_name((uint16_t)current));
        pb_gfx_text(80, 100, PB_GFX_COLOR_TEXT_DIM, buf);
        for (int i = 0; i < per_page; i++) {
            int idx = start + i;
            if (idx > 354) break;
            int yy = 130 + i * 20;
            uint32_t col = (idx == sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
            if (idx == sel) {
                pb_gfx_rounded_panel(75, yy - 4, 490, 18, 4, PB_GFX_COLOR_PANEL_LIGHT, 200);
            }
            snprintf(buf, sizeof buf, "#%3d   %s", idx, pb_move_name((uint16_t)idx));
            pb_gfx_text(90, yy, col, buf);
        }
        gfx_draw_hint_bar("DPad: select   L/R: page   A: pick   Y: clear   B: cancel");
        pb_gfx_flip();
        uint16_t bt = pb_gfx_wait_button();
        if (bt & PAD_BUTTON_B) return -1;
        if (bt & PAD_BUTTON_Y) return 0;
        if (bt & PAD_BUTTON_A) return sel;
        if (bt & PAD_BUTTON_UP)   sel = (sel > 0) ? sel - 1 : 354;
        if (bt & PAD_BUTTON_DOWN) sel = (sel < 354) ? sel + 1 : 0;
        if (bt & PAD_TRIGGER_L)   sel = (sel >= per_page) ? sel - per_page : 0;
        if (bt & PAD_TRIGGER_R)   sel = (sel + per_page <= 354) ? sel + per_page : 354;
    }
}

static void gfx_edit_moves_screen(pb_pkm_t *p) {
    int sel = 0;
    for (;;) {
        pb_gfx_clear();
        gfx_draw_title_bar("Edit moves");
        gfx_draw_panel(28, 70, 200, 290, NULL);
        pb_gfx_pkm_slot(60, 100, p->g.species, false, pb_pkm_is_shiny(p));
        pb_gfx_text(60, 180, PB_GFX_COLOR_TEXT_ACCENT, pb_species_name(p->g.species));

        gfx_draw_panel(244, 70, 380, 290, "MOVES");
        for (int i = 0; i < 4; i++) {
            int yy = 100 + i * 50;
            uint32_t col = (i == sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
            if (i == sel) {
                pb_gfx_rounded_panel(255, yy - 8, 358, 44, 6, PB_GFX_COLOR_PANEL_LIGHT, 200);
            }
            char buf[80];
            snprintf(buf, sizeof buf, "Slot %d", i + 1);
            pb_gfx_text(275, yy, col, buf);
            snprintf(buf, sizeof buf, "%s", pb_move_name(p->a.moves[i]));
            pb_gfx_text(275, yy + 18, col, buf);
        }
        gfx_draw_hint_bar("DPad: select   A: change   B: done");
        pb_gfx_flip();
        uint16_t bt = pb_gfx_wait_button();
        if (bt & PAD_BUTTON_B) return;
        if (bt & PAD_BUTTON_UP)   sel = (sel + 3) % 4;
        if (bt & PAD_BUTTON_DOWN) sel = (sel + 1) % 4;
        if (bt & PAD_BUTTON_A) {
            int picked = gfx_pick_move(p->a.moves[sel]);
            if (picked >= 0) pb_pkm_set_move(p, sel, (uint16_t)picked);
        }
    }
}

static void gfx_pick_nature(pb_pkm_t *p) {
    int sel = pb_pkm_get_nature(p);
    for (;;) {
        pb_gfx_clear();
        gfx_draw_title_bar("Pick a nature");
        gfx_draw_panel(28, 70, 200, 290, NULL);
        pb_gfx_pkm_slot(60, 100, p->g.species, false, pb_pkm_is_shiny(p));
        pb_gfx_text(60, 180, PB_GFX_COLOR_TEXT_ACCENT, pb_species_name(p->g.species));
        pb_gfx_text(60, 200, PB_GFX_COLOR_TEXT_DIM, "Current:");
        pb_gfx_text(60, 218, PB_GFX_COLOR_TEXT, pb_nature_name(pb_pkm_get_nature(p)));

        gfx_draw_panel(244, 70, 380, 360, "NATURES");
        /* 5 rows x 5 cols of small chips */
        for (int i = 0; i < 25; i++) {
            int col_i = i % 5, row_i = i / 5;
            int x = 260 + col_i * 70;
            int y = 110 + row_i * 50;
            uint32_t col = (i == sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
            if (i == sel) {
                pb_gfx_rounded_panel(x - 4, y - 4, 64, 30, 6, PB_GFX_COLOR_PANEL_LIGHT, 200);
            }
            pb_gfx_text(x, y, col, pb_nature_name((uint8_t)i));
        }
        gfx_draw_hint_bar("DPad: select   A: pick   B: cancel");
        pb_gfx_flip();
        uint16_t bt = pb_gfx_wait_button();
        if (bt & PAD_BUTTON_B) return;
        if (bt & PAD_BUTTON_UP)    sel = (sel + 20) % 25;
        if (bt & PAD_BUTTON_DOWN)  sel = (sel + 5) % 25;
        if (bt & PAD_BUTTON_LEFT)  sel = (sel + 24) % 25;
        if (bt & PAD_BUTTON_RIGHT) sel = (sel + 1) % 25;
        if (bt & PAD_BUTTON_A) { pb_pkm_set_nature(p, (uint8_t)sel); return; }
    }
}

/* The main graphics-mode editor screen. Returns true if user pressed Y
 * (commit), false on B (cancel). Mutates p in place. */
static bool gfx_edit_pkm(pb_pkm_t *p) {
    if (!p || p->is_empty) return false;
    int sel = 0;
    static const char *fields[] = {
        "IVs", "EVs", "Moves", "Nature", "Shiny", "Friendship", "Held item"
    };
    const int n = (int)(sizeof fields / sizeof fields[0]);
    for (;;) {
        pb_gfx_clear();
        char title[64];
        char nickname[16] = {0};
        pb_gen3_to_ascii(p->nickname, 10, nickname);
        snprintf(title, sizeof title, "Edit  -  %s",
                 nickname[0] ? nickname : pb_species_name(p->g.species));
        gfx_draw_title_bar(title);

        /* Live preview on left -- the sprite swaps in real-time when shiny
         * is toggled. */
        gfx_draw_panel(28, 70, 200, 360, NULL);
        pb_gfx_pkm_slot(60, 110, p->g.species, false, pb_pkm_is_shiny(p));
        pb_gfx_text(60, 200, PB_GFX_COLOR_TEXT_ACCENT, pb_species_name(p->g.species));
        char buf[80];
        snprintf(buf, sizeof buf, "#%u", p->g.species);
        pb_gfx_text(60, 218, PB_GFX_COLOR_TEXT_DIM, buf);
        snprintf(buf, sizeof buf, "%s", pb_nature_name(pb_pkm_get_nature(p)));
        pb_gfx_text(60, 240, PB_GFX_COLOR_TEXT, buf);
        if (pb_pkm_is_shiny(p)) {
            pb_gfx_text(60, 260, PB_GFX_COLOR_SHINY, "* SHINY");
        }

        /* Field list on right */
        gfx_draw_panel(244, 70, 380, 360, "FIELDS");
        for (int i = 0; i < n; i++) {
            int yy = 100 + i * 38;
            uint32_t col = (i == sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
            if (i == sel) {
                pb_gfx_rounded_panel(255, yy - 8, 358, 32, 6, PB_GFX_COLOR_PANEL_LIGHT, 200);
            }
            pb_gfx_text(275, yy, col, fields[i]);
            char val[64];
            switch (i) {
                case 0: {
                    uint8_t ivs[6]; pb_pkm_ivs(p, ivs);
                    snprintf(val, sizeof val, "%2u/%2u/%2u/%2u/%2u/%2u",
                             ivs[0], ivs[1], ivs[2], ivs[3], ivs[4], ivs[5]);
                    break;
                }
                case 1:
                    snprintf(val, sizeof val, "%3u/%3u/%3u/%3u/%3u/%3u",
                             p->e.ev[0], p->e.ev[1], p->e.ev[2],
                             p->e.ev[3], p->e.ev[4], p->e.ev[5]);
                    break;
                case 2:
                    snprintf(val, sizeof val, "%s, %s, ...",
                             pb_move_name(p->a.moves[0]), pb_move_name(p->a.moves[1]));
                    break;
                case 3:
                    snprintf(val, sizeof val, "%s", pb_nature_name(pb_pkm_get_nature(p)));
                    break;
                case 4:
                    snprintf(val, sizeof val, "%s", pb_pkm_is_shiny(p) ? "YES" : "no");
                    break;
                case 5:
                    snprintf(val, sizeof val, "%u", p->g.friendship);
                    break;
                case 6:
                    snprintf(val, sizeof val, "%u", p->g.held_item);
                    break;
            }
            pb_gfx_text(385, yy + 16, col, val);
        }
        gfx_draw_hint_bar("DPad: pick   A: edit   Y: save+back   B: cancel");
        pb_gfx_flip();

        uint16_t bt = pb_gfx_wait_button();
        if (bt & PAD_BUTTON_B) return false;
        if (bt & PAD_BUTTON_Y) return true;
        if (bt & PAD_BUTTON_UP)   sel = (sel + n - 1) % n;
        if (bt & PAD_BUTTON_DOWN) sel = (sel + 1) % n;

        if (bt & PAD_BUTTON_A) {
            switch (sel) {
                case 0: gfx_edit_stat_screen(p, "Edit IVs", 31, pb_pkm_get_iv, pb_pkm_set_iv); break;
                case 1: gfx_edit_stat_screen(p, "Edit EVs", 255, pb_pkm_get_ev, pb_pkm_set_ev); break;
                case 2: gfx_edit_moves_screen(p); break;
                case 3: gfx_pick_nature(p); break;
                case 4: pb_pkm_toggle_shiny(p, !pb_pkm_is_shiny(p)); break;
                case 5: p->g.friendship = 255; break;
                case 6: p->g.held_item = 0; break;
            }
        }
        /* L/R quick-adjust on friendship + item */
        if (sel == 5) {
            uint8_t f = p->g.friendship;
            if (bt & PAD_TRIGGER_L) p->g.friendship = f > 0 ? f - 1 : 0;
            if (bt & PAD_TRIGGER_R) p->g.friendship = f < 255 ? f + 1 : 255;
        }
        if (sel == 6) {
            uint16_t it = p->g.held_item;
            if (bt & PAD_TRIGGER_L) p->g.held_item = it > 0 ? it - 1 : 0;
            if (bt & PAD_TRIGGER_R) p->g.held_item = it + 1;
        }
    }
}

/* ---- Graphics-mode FireRed party viewer ---- */

static void gfx_pkm_party_screen(pb_save_t *s) {
    uint8_t count = 0;
    const uint8_t *party_c = NULL;
    if (!pb_save_party(s, &count, &party_c) || count == 0) return;
    int sel = 0;
    for (;;) {
        pb_gfx_clear();
        char title[64];
        snprintf(title, sizeof title, "Party - %s", s->trainer_name);
        gfx_draw_title_bar(title);

        /* Decode all six party slots up-front. */
        pb_pkm_t cached[6];
        for (int i = 0; i < count; i++) {
            pb_pkm_decode(&cached[i], party_c + i * PB_PKM_PARTY_SIZE);
        }

        /* Trainer card */
        gfx_draw_panel(28, 70, 168, 110, "TRAINER");
        pb_gfx_text(40, 100, PB_GFX_COLOR_TEXT, s->trainer_name);
        char ids[32];
        snprintf(ids, sizeof ids, "ID %05u", s->trainer_id);
        pb_gfx_text(40, 120, PB_GFX_COLOR_TEXT_DIM, ids);
        pb_gfx_text(40, 138, PB_GFX_COLOR_TEXT_DIM, pb_game_name(s->game));

        /* Party slot row */
        gfx_draw_panel(212, 70, 412, 110, "PARTY");
        int row_x = 218, row_y = 96;
        for (int i = 0; i < count; i++) {
            pb_gfx_pkm_slot(row_x + i * 72, row_y, cached[i].g.species,
                            i == sel, pb_pkm_is_shiny(&cached[i]));
        }

        /* Detail panel for selected mon */
        gfx_draw_panel(28, 210, 596, 222, "DETAIL");
        pb_pkm_t *p = &cached[sel];
        if (p->is_empty) {
            pb_gfx_text(48, 238, PB_GFX_COLOR_TEXT_DIM, "(empty slot)");
        } else {
            pb_gfx_pkm_slot(48, 238, p->g.species, false, pb_pkm_is_shiny(p));
            char nickname[16] = {0};
            pb_gen3_to_ascii(p->nickname, 10, nickname);
            pb_gfx_text_scale(128, 238, PB_GFX_COLOR_TEXT_ACCENT, 2,
                              nickname[0] ? nickname : pb_species_name(p->g.species));
            char buf[64];
            snprintf(buf, sizeof buf, "#%u  %s  %s", p->g.species,
                     pb_nature_name(pb_pkm_get_nature(p)),
                     pb_pkm_is_shiny(p) ? "SHINY" : "");
            pb_gfx_text(128, 260, PB_GFX_COLOR_TEXT, buf);
            snprintf(buf, sizeof buf, "PID 0x%08X  OT ID %05u",
                     (unsigned)p->pid, (unsigned)(p->ot_id & 0xFFFF));
            pb_gfx_text(128, 278, PB_GFX_COLOR_TEXT_DIM, buf);

            pb_gfx_text(48, 326, PB_GFX_COLOR_TEXT_ACCENT, "MOVES");
            for (int m = 0; m < 4; m++) {
                int mx = 48 + (m % 2) * 280;
                int my = 344 + (m / 2) * 18;
                snprintf(buf, sizeof buf, "%d. #%-3u %s", m + 1,
                         p->a.moves[m], pb_move_name(p->a.moves[m]));
                pb_gfx_text(mx, my, PB_GFX_COLOR_TEXT, buf);
            }
            uint8_t ivs[6]; pb_pkm_ivs(p, ivs);
            pb_gfx_text(48, 392, PB_GFX_COLOR_TEXT_ACCENT, "IVs");
            snprintf(buf, sizeof buf, "%2u/%2u/%2u/%2u/%2u/%2u",
                     ivs[0], ivs[1], ivs[2], ivs[3], ivs[4], ivs[5]);
            pb_gfx_text(88, 392, PB_GFX_COLOR_TEXT, buf);
            pb_gfx_text(48, 410, PB_GFX_COLOR_TEXT_DIM,
                        "(HP / Atk / Def / Spe / SpA / SpD)");
        }
        gfx_draw_hint_bar("D-Pad/L-R: select   A: open   B: back");
        pb_gfx_flip();

        uint16_t b = pb_gfx_wait_button();
        if (b & PAD_BUTTON_B) return;
        if (b & (PAD_BUTTON_LEFT | PAD_BUTTON_UP | PAD_TRIGGER_L))
            sel = (sel + count - 1) % count;
        if (b & (PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN | PAD_TRIGGER_R))
            sel = (sel + 1) % count;
        if (b & PAD_BUTTON_A) {
            uint8_t *slot_raw = (uint8_t *)party_c + sel * PB_PKM_PARTY_SIZE;
            pb_pkm_t p; pb_pkm_decode(&p, slot_raw);
            if (gfx_show_pkm_detail(&p, slot_raw, PB_FMT_PK3)) {
                pb_save_update_section_checksum(s, 1);
            }
        }
    }
}

/* ---- Graphics-mode Gen 3 box browser ---- */

static void gfx_pkm_box_screen(pb_save_t *s) {
    if (!s) return;
    int box = 0, sel = 0;
    for (;;) {
        pb_gfx_clear();
        char title[64];
        snprintf(title, sizeof title, "Box %d / 14  -  %s", box + 1, s->trainer_name);
        gfx_draw_title_bar(title);

        const uint8_t *box_bytes = pb_save_box(s, box);
        if (!box_bytes) return;

        /* 6 x 5 grid of slots */
        const int cell = 60;  /* 56px slot + 4px gap */
        const int slot_size = 56;
        int grid_x = (640 - 6 * cell) / 2;
        int grid_y = 70;
        pb_gfx_rounded_panel(grid_x - 12, grid_y - 12,
                             6 * cell + 24, 5 * cell + 24, 10,
                             PB_GFX_COLOR_PANEL, 215);

        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 6; c++) {
                int idx = r * 6 + c;
                int sx = grid_x + c * cell;
                int sy = grid_y + r * cell;
                pb_pkm_t p;
                pb_pkm_decode(&p, box_bytes + idx * PB_PKM_BOX_SIZE);
                if (p.is_empty) {
                    pb_gfx_rounded_panel(sx, sy, slot_size, slot_size, 6,
                                         PB_GFX_COLOR_PANEL_LIGHT, 70);
                    if (idx == sel) {
                        pb_gfx_border(sx, sy, slot_size, slot_size, 2,
                                      PB_GFX_COLOR_PANEL_ACCENT);
                    }
                } else {
                    /* slim 56px slot using a downscaled sprite via pkm_slot
                     * (which draws 64x64 by default). Approximate at 56px by
                     * drawing a tinted panel + sprite scaled. */
                    if (idx == sel) {
                        pb_gfx_rounded_panel(sx - 3, sy - 3, slot_size + 6,
                                             slot_size + 6, 8,
                                             PB_GFX_COLOR_PANEL_ACCENT, 230);
                    }
                    pb_gfx_rounded_panel(sx, sy, slot_size, slot_size, 6,
                                         PB_GFX_COLOR_PANEL_LIGHT, 230);
                    pb_gfx_pkm_slot(sx - 4, sy - 4, p.g.species, false,
                                    pb_pkm_is_shiny(&p));
                }
            }
        }

        /* Detail strip for the selected slot */
        pb_pkm_t sp;
        pb_pkm_decode(&sp, box_bytes + sel * PB_PKM_BOX_SIZE);
        gfx_draw_panel(28, 392, 596, 48, NULL);
        if (sp.is_empty) {
            pb_gfx_text(48, 412, PB_GFX_COLOR_TEXT_DIM, "(empty slot)");
        } else {
            char nick[16] = {0};
            pb_gen3_to_ascii(sp.nickname, 10, nick);
            char line[96];
            snprintf(line, sizeof line, "\"%s\"   %s  #%u   %s",
                     nick[0] ? nick : pb_species_name(sp.g.species),
                     pb_species_name(sp.g.species), sp.g.species,
                     pb_pkm_is_shiny(&sp) ? "* SHINY" : "");
            pb_gfx_text(48, 408, PB_GFX_COLOR_TEXT, line);
            char ivs[64]; uint8_t iv[6]; pb_pkm_ivs(&sp, iv);
            snprintf(ivs, sizeof ivs, "Nature %s   IVs %u/%u/%u/%u/%u/%u",
                     pb_nature_name(pb_pkm_get_nature(&sp)),
                     iv[0], iv[1], iv[2], iv[3], iv[4], iv[5]);
            pb_gfx_text(48, 424, PB_GFX_COLOR_TEXT_DIM, ivs);
        }
        gfx_draw_hint_bar("L/R: box   D-Pad: select   A: open   B: back");
        pb_gfx_flip();

        uint16_t b = pb_gfx_wait_button();
        if (b & PAD_BUTTON_B) return;
        if (b & PAD_TRIGGER_L) box = (box + 14 - 1) % 14;
        if (b & PAD_TRIGGER_R) box = (box + 1) % 14;
        if (b & PAD_BUTTON_UP)    sel = (sel + 30 - 6) % 30;
        if (b & PAD_BUTTON_DOWN)  sel = (sel + 6) % 30;
        if (b & PAD_BUTTON_LEFT)  sel = (sel + 30 - 1) % 30;
        if (b & PAD_BUTTON_RIGHT) sel = (sel + 1) % 30;
        if (b & PAD_BUTTON_A) {
            pb_pkm_t p;
            pb_pkm_decode(&p, box_bytes + sel * PB_PKM_BOX_SIZE);
            if (p.is_empty) continue;
            /* Capture raw bytes locally; if edited, persist via section
             * write helper. */
            uint8_t raw_local[80];
            memcpy(raw_local, box_bytes + sel * PB_PKM_BOX_SIZE, 80);
            if (gfx_show_pkm_detail(&p, raw_local, PB_FMT_PK3)) {
                /* gfx_show_pkm_detail already called pb_pkm_encode into
                 * raw_local; commit to the live save. */
                pb_save_box_write_slot(s, box, sel, raw_local);
            }
        }
    }
}

/* ---- Graphics-mode Colosseum party + box browsers ---- */

static void gfx_colo_party_screen(pb_colo_save_t *cs) {
    if (!cs) return;
    int sel = 0;
    for (;;) {
        pb_gfx_clear();
        char title[64];
        snprintf(title, sizeof title, "Colosseum Party - %s", cs->trainer_name);
        gfx_draw_title_bar(title);

        pb_colo_pkm_t party[6];
        for (int i = 0; i < 6; i++) {
            pb_colo_pkm_decode(&party[i],
                cs->slot + PB_COLO_PARTY_OFF + i * PB_COLO_PKM_SIZE);
        }

        /* Trainer card */
        gfx_draw_panel(28, 70, 168, 110, "TRAINER");
        pb_gfx_text(40, 100, PB_GFX_COLOR_TEXT, cs->trainer_name);
        char buf[64];
        snprintf(buf, sizeof buf, "ID %05u", cs->trainer_id);
        pb_gfx_text(40, 120, PB_GFX_COLOR_TEXT_DIM, buf);
        snprintf(buf, sizeof buf, "Saves: %u", (unsigned)cs->save_count);
        pb_gfx_text(40, 138, PB_GFX_COLOR_TEXT_DIM, buf);

        /* Party slot row */
        gfx_draw_panel(212, 70, 412, 110, "PARTY");
        for (int i = 0; i < 6; i++) {
            int sx = 218 + i * 72;
            pb_gfx_pkm_slot(sx, 96, party[i].species_internal, i == sel,
                            false);
        }

        /* Detail panel */
        gfx_draw_panel(28, 210, 596, 222, "DETAIL");
        pb_colo_pkm_t *p = &party[sel];
        if (p->is_empty) {
            pb_gfx_text(48, 238, PB_GFX_COLOR_TEXT_DIM, "(empty slot)");
        } else {
            pb_gfx_pkm_slot(48, 238, p->species_internal, false, false);
            pb_gfx_text_scale(128, 238, PB_GFX_COLOR_TEXT_ACCENT, 2, p->nickname);
            snprintf(buf, sizeof buf, "L%u  CXD#%u%s", p->level,
                     p->species_internal, p->is_shadow ? "  [SHADOW]" : "");
            pb_gfx_text(128, 260, PB_GFX_COLOR_TEXT, buf);
            snprintf(buf, sizeof buf, "OT %s  TID %u",
                     p->ot_name, p->trainer_id);
            pb_gfx_text(128, 278, PB_GFX_COLOR_TEXT_DIM, buf);
            pb_gfx_text(48, 326, PB_GFX_COLOR_TEXT_ACCENT, "MOVES");
            for (int m = 0; m < 4; m++) {
                int mx = 48 + (m % 2) * 280;
                int my = 344 + (m / 2) * 18;
                snprintf(buf, sizeof buf, "%d. #%-3u %s", m + 1,
                         p->moves[m], pb_move_name(p->moves[m]));
                pb_gfx_text(mx, my, PB_GFX_COLOR_TEXT, buf);
            }
            pb_gfx_text(48, 392, PB_GFX_COLOR_TEXT_ACCENT, "IVs");
            snprintf(buf, sizeof buf, "%u/%u/%u/%u/%u/%u",
                     p->iv[0], p->iv[1], p->iv[2],
                     p->iv[3], p->iv[4], p->iv[5]);
            pb_gfx_text(88, 392, PB_GFX_COLOR_TEXT, buf);
            pb_gfx_text(48, 410, PB_GFX_COLOR_TEXT_DIM,
                        "(HP / Atk / Def / Spe / SpA / SpD)");
        }
        gfx_draw_hint_bar("D-Pad/L-R: select   A: open   B: back");
        pb_gfx_flip();

        uint16_t b = pb_gfx_wait_button();
        if (b & PAD_BUTTON_B) return;
        if (b & (PAD_BUTTON_LEFT | PAD_BUTTON_UP | PAD_TRIGGER_L))
            sel = (sel + 5) % 6;
        if (b & (PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN | PAD_TRIGGER_R))
            sel = (sel + 1) % 6;
        if (b & PAD_BUTTON_A) {
            uint8_t *raw = cs->slot + PB_COLO_PARTY_OFF + sel * PB_COLO_PKM_SIZE;
            pb_pkm_t p; pb_ck3_to_pkm(&p, raw);
            if (!p.is_empty) gfx_show_pkm_detail(&p, raw, PB_FMT_CK3);
        }
    }
}

static void gfx_colo_box_screen(pb_colo_save_t *cs) {
    if (!cs) return;
    int box = 0, sel = 0;
    for (;;) {
        pb_gfx_clear();
        char title[64];
        snprintf(title, sizeof title, "Colosseum Box %d / %d  -  %s",
                 box + 1, PB_COLO_BOX_COUNT, cs->trainer_name);
        gfx_draw_title_bar(title);

        const int cell = 60, slot_size = 56;
        int grid_x = (640 - 6 * cell) / 2, grid_y = 70;
        pb_gfx_rounded_panel(grid_x - 12, grid_y - 12,
                             6 * cell + 24, 5 * cell + 24, 10,
                             PB_GFX_COLOR_PANEL, 215);

        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 6; c++) {
                int idx = r * 6 + c;
                int sx = grid_x + c * cell, sy = grid_y + r * cell;
                uint32_t off = pb_colo_box_slot_offset(cs, box, idx);
                pb_colo_pkm_t cp;
                pb_colo_pkm_decode(&cp, cs->slot + off);
                if (cp.is_empty) {
                    pb_gfx_rounded_panel(sx, sy, slot_size, slot_size, 6,
                                         PB_GFX_COLOR_PANEL_LIGHT, 70);
                    if (idx == sel) pb_gfx_border(sx, sy, slot_size, slot_size, 2,
                                                  PB_GFX_COLOR_PANEL_ACCENT);
                } else {
                    if (idx == sel)
                        pb_gfx_rounded_panel(sx - 3, sy - 3, slot_size + 6,
                                             slot_size + 6, 8,
                                             PB_GFX_COLOR_PANEL_ACCENT, 230);
                    pb_gfx_rounded_panel(sx, sy, slot_size, slot_size, 6,
                                         PB_GFX_COLOR_PANEL_LIGHT, 230);
                    pb_gfx_pkm_slot(sx - 4, sy - 4, cp.species_internal, false, false);
                }
            }
        }

        uint32_t sel_off = pb_colo_box_slot_offset(cs, box, sel);
        pb_colo_pkm_t sp;
        pb_colo_pkm_decode(&sp, cs->slot + sel_off);
        gfx_draw_panel(28, 392, 596, 48, NULL);
        if (sp.is_empty) {
            pb_gfx_text(48, 412, PB_GFX_COLOR_TEXT_DIM, "(empty slot)");
        } else {
            char line[96];
            snprintf(line, sizeof line, "\"%s\"   CXD#%u   L%u%s",
                     sp.nickname, sp.species_internal, sp.level,
                     sp.is_shadow ? "  [SHADOW]" : "");
            pb_gfx_text(48, 408, PB_GFX_COLOR_TEXT, line);
        }
        gfx_draw_hint_bar("L/R: box   D-Pad: select   A: open   B: back");
        pb_gfx_flip();

        uint16_t b = pb_gfx_wait_button();
        if (b & PAD_BUTTON_B) return;
        if (b & PAD_TRIGGER_L) box = (box + PB_COLO_BOX_COUNT - 1) % PB_COLO_BOX_COUNT;
        if (b & PAD_TRIGGER_R) box = (box + 1) % PB_COLO_BOX_COUNT;
        if (b & PAD_BUTTON_UP)    sel = (sel + 30 - 6) % 30;
        if (b & PAD_BUTTON_DOWN)  sel = (sel + 6) % 30;
        if (b & PAD_BUTTON_LEFT)  sel = (sel + 30 - 1) % 30;
        if (b & PAD_BUTTON_RIGHT) sel = (sel + 1) % 30;
        if (b & PAD_BUTTON_A) {
            uint8_t *raw = cs->slot + pb_colo_box_slot_offset(cs, box, sel);
            pb_pkm_t p; pb_ck3_to_pkm(&p, raw);
            if (!p.is_empty) gfx_show_pkm_detail(&p, raw, PB_FMT_CK3);
        }
    }
}

/* ---- Graphics-mode XD box browser ---- */

static void gfx_xd_box_screen(pb_xd_save_t *xs) {
    if (!xs || !xs->box_offset) return;
    int box = 0, sel = 0;
    for (;;) {
        pb_gfx_clear();
        char title[64];
        snprintf(title, sizeof title, "XD Box %d / %d  -  %s",
                 box + 1, PB_XD_BOX_COUNT, xs->trainer_name);
        gfx_draw_title_bar(title);

        const int cell = 60;
        const int slot_size = 56;
        int grid_x = (640 - 6 * cell) / 2;
        int grid_y = 70;
        pb_gfx_rounded_panel(grid_x - 12, grid_y - 12,
                             6 * cell + 24, 5 * cell + 24, 10,
                             PB_GFX_COLOR_PANEL, 215);

        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 6; c++) {
                int idx = r * 6 + c;
                int sx = grid_x + c * cell;
                int sy = grid_y + r * cell;
                uint32_t off = pb_xd_box_slot_offset(xs, box, idx);
                pb_xd_pkm_t xp;
                pb_xd_pkm_decode(&xp, xs->slot + off);
                if (xp.is_empty) {
                    pb_gfx_rounded_panel(sx, sy, slot_size, slot_size, 6,
                                         PB_GFX_COLOR_PANEL_LIGHT, 70);
                    if (idx == sel) {
                        pb_gfx_border(sx, sy, slot_size, slot_size, 2,
                                      PB_GFX_COLOR_PANEL_ACCENT);
                    }
                } else {
                    if (idx == sel) {
                        pb_gfx_rounded_panel(sx - 3, sy - 3, slot_size + 6,
                                             slot_size + 6, 8,
                                             PB_GFX_COLOR_PANEL_ACCENT, 230);
                    }
                    pb_gfx_rounded_panel(sx, sy, slot_size, slot_size, 6,
                                         PB_GFX_COLOR_PANEL_LIGHT, 230);
                    pb_gfx_pkm_slot(sx - 4, sy - 4, xp.species_internal, false,
                                    false);
                }
            }
        }

        /* Detail strip */
        uint32_t sel_off = pb_xd_box_slot_offset(xs, box, sel);
        pb_xd_pkm_t sp;
        pb_xd_pkm_decode(&sp, xs->slot + sel_off);
        gfx_draw_panel(28, 392, 596, 48, NULL);
        if (sp.is_empty) {
            pb_gfx_text(48, 412, PB_GFX_COLOR_TEXT_DIM, "(empty slot)");
        } else {
            char line[96];
            snprintf(line, sizeof line, "\"%s\"   XD#%u   L%u   %s",
                     sp.nickname, sp.species_internal, sp.level,
                     sp.is_shadow ? "[SHADOW]" : "");
            pb_gfx_text(48, 408, PB_GFX_COLOR_TEXT, line);
            char line2[96];
            snprintf(line2, sizeof line2, "OT %s  TID %u  IVs %u/%u/%u/%u/%u/%u",
                     sp.ot_name, sp.trainer_id,
                     sp.iv[0], sp.iv[1], sp.iv[2], sp.iv[3], sp.iv[4], sp.iv[5]);
            pb_gfx_text(48, 424, PB_GFX_COLOR_TEXT_DIM, line2);
        }
        gfx_draw_hint_bar("L/R: box   D-Pad: select   A: open   B: back");
        pb_gfx_flip();

        uint16_t b = pb_gfx_wait_button();
        if (b & PAD_BUTTON_B) return;
        if (b & PAD_TRIGGER_L) box = (box + PB_XD_BOX_COUNT - 1) % PB_XD_BOX_COUNT;
        if (b & PAD_TRIGGER_R) box = (box + 1) % PB_XD_BOX_COUNT;
        if (b & PAD_BUTTON_UP)    sel = (sel + 30 - 6) % 30;
        if (b & PAD_BUTTON_DOWN)  sel = (sel + 6) % 30;
        if (b & PAD_BUTTON_LEFT)  sel = (sel + 30 - 1) % 30;
        if (b & PAD_BUTTON_RIGHT) sel = (sel + 1) % 30;
        if (b & PAD_BUTTON_A) {
            uint8_t *raw = xs->slot + pb_xd_box_slot_offset(xs, box, sel);
            pb_pkm_t p; pb_xk3_to_pkm(&p, raw);
            if (!p.is_empty) gfx_show_pkm_detail(&p, raw, PB_FMT_XK3);
        }
    }
}

/* ---- Graphics-mode main menu (boot screen) ---- */

void pb_ui_run_graphics_app(void) {
    if (!pb_gfx_init()) return;
    int sel = 0;
    static const char *items[] = {
        "Load FireRed save (demo)",
        "Load Emerald save (demo)",
        "Load Pokemon XD save (demo)",
        "Load Pokemon Colosseum save (demo)",
        "Read GBA cart via link cable",
        "Open save from SD card",
        "Game art gallery",
        "About PokeBridge",
        "Exit",
    };
    const int n = (int)(sizeof items / sizeof items[0]);

    for (;;) {
        pb_gfx_clear();
        gfx_draw_title_bar("Welcome");

        /* Decorative sprite row across the top -- starter trios + Mew. */
        static const uint16_t deco[] = { 1, 4, 7, 252, 255, 258, 151 };
        const int n_deco = (int)(sizeof deco / sizeof deco[0]);
        int dx = (640 - (n_deco * 72)) / 2;
        for (int i = 0; i < n_deco; i++) {
            pb_gfx_pkm_slot(dx + i * 72, 80, deco[i], false, false);
        }

        /* Menu panel (left) */
        gfx_draw_panel(40, 180, 360, 240, "MENU");
        for (int i = 0; i < n; i++) {
            int yy = 215 + i * 32;
            uint32_t col = (i == sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
            if (i == sel) {
                pb_gfx_rounded_panel(55, yy - 6, 330, 24, 6, PB_GFX_COLOR_PANEL_LIGHT, 200);
            }
            pb_gfx_text(75, yy, col, items[i]);
        }

        /* Box art preview (right) for the highlighted item. */
        static const pb_boxart_t art_for[] = {
            PB_BOXART_FIRERED,    /* 0: FireRed demo         */
            PB_BOXART_EMERALD,    /* 1: Emerald demo         */
            PB_BOXART_XD,         /* 2: XD demo              */
            PB_BOXART_COLOSSEUM,  /* 3: Colosseum demo       */
            PB_BOXART_SAPPHIRE,   /* 4: GBA link cable       */
            PB_BOXART_RUBY,       /* 5: Open from SD         */
            PB_BOXART_LEAFGREEN,  /* 6: Game art gallery     */
            PB_BOXART_BOX,        /* 7: About                */
            PB_BOXART_UNKNOWN,    /* 8: Exit                 */
        };
        if (sel >= 0 && sel < (int)(sizeof art_for / sizeof art_for[0]) &&
            art_for[sel] != PB_BOXART_UNKNOWN) {
            pb_gfx_boxart(420, 180, 180, 240, art_for[sel]);
        } else {
            /* Empty placeholder panel when no art applies */
            gfx_draw_panel(420, 180, 180, 240, NULL);
        }

        gfx_draw_hint_bar("D-Pad: select   A (X-key): choose   START: exit");
        pb_gfx_flip();

        uint16_t b = pb_gfx_wait_button();
        if (b & PAD_BUTTON_START) return;
        if (b & (PAD_BUTTON_UP | PAD_TRIGGER_L))   sel = (sel + n - 1) % n;
        if (b & (PAD_BUTTON_DOWN | PAD_TRIGGER_R)) sel = (sel + 1) % n;
        if (b & PAD_BUTTON_A) {
            switch (sel) {
                case 0: {
                    static pb_save_t s;
                    if (!pb_save_load(&s, pb_embedded_firered_sav, pb_embedded_firered_sav_len)) break;
                    /* Sub-menu: party / boxes */
                    int sub_sel = 0;
                    static const char *subs[] = { "Browse party", "Browse boxes", "Back" };
                    for (;;) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("FireRed save");
                        gfx_draw_panel(60, 80, 520, 110, "TRAINER");
                        char buf[64];
                        snprintf(buf, sizeof buf, "%s   ID %05u   Game: %s",
                                 s.trainer_name, s.trainer_id, pb_game_name(s.game));
                        pb_gfx_text(80, 120, PB_GFX_COLOR_TEXT, buf);
                        gfx_draw_panel(60, 210, 520, 180, NULL);
                        for (int i = 0; i < 3; i++) {
                            int yy = 240 + i * 32;
                            uint32_t col = (i == sub_sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
                            if (i == sub_sel) pb_gfx_rounded_panel(75, yy - 6, 490, 24, 4, PB_GFX_COLOR_PANEL_LIGHT, 200);
                            pb_gfx_text(95, yy, col, subs[i]);
                        }
                        gfx_draw_hint_bar("DPad: select   A: open   B: back");
                        pb_gfx_flip();
                        uint16_t sb = pb_gfx_wait_button();
                        if (sb & PAD_BUTTON_B) break;
                        if (sb & PAD_BUTTON_UP)   sub_sel = (sub_sel + 2) % 3;
                        if (sb & PAD_BUTTON_DOWN) sub_sel = (sub_sel + 1) % 3;
                        if (sb & PAD_BUTTON_A) {
                            if (sub_sel == 0) gfx_pkm_party_screen(&s);
                            else if (sub_sel == 1) gfx_pkm_box_screen(&s);
                            else break;
                        }
                    }
                    break;
                }
                case 1: {
                    /* Load embedded Emerald save (shares Gen 3 .sav engine). */
                    static pb_save_t es;
                    if (!pb_save_load(&es, pb_embedded_emerald_sav, pb_embedded_emerald_sav_len)) break;
                    int sub_sel = 0;
                    static const char *subs[] = { "Browse party", "Browse boxes", "Back" };
                    for (;;) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Emerald save");
                        gfx_draw_panel(60, 80, 520, 110, "TRAINER");
                        char buf[64];
                        snprintf(buf, sizeof buf, "%s   ID %05u   Game: %s",
                                 es.trainer_name, es.trainer_id, pb_game_name(es.game));
                        pb_gfx_text(80, 120, PB_GFX_COLOR_TEXT, buf);
                        gfx_draw_panel(60, 210, 520, 180, NULL);
                        for (int i = 0; i < 3; i++) {
                            int yy = 240 + i * 32;
                            uint32_t col = (i == sub_sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
                            if (i == sub_sel) pb_gfx_rounded_panel(75, yy - 6, 490, 24, 4, PB_GFX_COLOR_PANEL_LIGHT, 200);
                            pb_gfx_text(95, yy, col, subs[i]);
                        }
                        gfx_draw_hint_bar("DPad: select   A: open   B: back");
                        pb_gfx_flip();
                        uint16_t sb = pb_gfx_wait_button();
                        if (sb & PAD_BUTTON_B) break;
                        if (sb & PAD_BUTTON_UP)   sub_sel = (sub_sel + 2) % 3;
                        if (sb & PAD_BUTTON_DOWN) sub_sel = (sub_sel + 1) % 3;
                        if (sb & PAD_BUTTON_A) {
                            if (sub_sel == 0) gfx_pkm_party_screen(&es);
                            else if (sub_sel == 1) gfx_pkm_box_screen(&es);
                            else break;
                        }
                    }
                    break;
                }
                case 2: {
                    static pb_xd_save_t xs;
                    if (!pb_xd_load(&xs, pb_embedded_xd_sav, pb_embedded_xd_sav_len)) break;
                    int sub_sel = 0;
                    static const char *subs[] = { "Browse party", "Browse boxes",
                                                  "Write to SD (encrypted)", "Back" };
                    const int n_subs = 4;
                    for (;;) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Pokemon XD save");
                        gfx_draw_panel(60, 80, 520, 110, "TRAINER");
                        char buf[64];
                        snprintf(buf, sizeof buf, "%s   ID %u   Saves %u",
                                 xs.trainer_name, xs.trainer_id, (unsigned)xs.save_count);
                        pb_gfx_text(80, 120, PB_GFX_COLOR_TEXT, buf);
                        gfx_draw_panel(60, 210, 520, 200, NULL);
                        for (int i = 0; i < n_subs; i++) {
                            int yy = 240 + i * 32;
                            uint32_t col = (i == sub_sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
                            if (i == sub_sel) pb_gfx_rounded_panel(75, yy - 6, 490, 24, 4, PB_GFX_COLOR_PANEL_LIGHT, 200);
                            pb_gfx_text(95, yy, col, subs[i]);
                        }
                        gfx_draw_hint_bar("DPad: select   A: open   B: back");
                        pb_gfx_flip();
                        uint16_t sb = pb_gfx_wait_button();
                        if (sb & PAD_BUTTON_B) break;
                        if (sb & PAD_BUTTON_UP)   sub_sel = (sub_sel + n_subs - 1) % n_subs;
                        if (sb & PAD_BUTTON_DOWN) sub_sel = (sub_sel + 1) % n_subs;
                        if (sb & PAD_BUTTON_A) {
                            if (sub_sel == 0) gfx_xd_party_screen(&xs);
                            else if (sub_sel == 1) gfx_xd_box_screen(&xs);
                            else if (sub_sel == 2) {
                                /* Write XD save back to SD card. */
                                pb_gfx_clear();
                                gfx_draw_title_bar("Write XD save");
                                gfx_draw_panel(60, 110, 520, 240, NULL);
                                if (!pb_sd_available) {
                                    pb_gfx_text(90, 150, PB_GFX_COLOR_PANEL_ACCENT,
                                                "No SD card -- cannot write.");
                                    pb_gfx_text(90, 178, PB_GFX_COLOR_TEXT_DIM,
                                                "Connect a Swiss-formatted SD on real");
                                    pb_gfx_text(90, 196, PB_GFX_COLOR_TEXT_DIM,
                                                "hardware to write encrypted XD saves.");
                                } else {
                                    mkdir("sd:/pokebridge", 0777);
                                    mkdir("sd:/pokebridge/saves", 0777);
                                    bool ok = pb_xd_write_file(&xs, "sd:/pokebridge/saves/xd_edited.gci");
                                    if (ok) {
                                        pb_gfx_text(90, 150, PB_GFX_COLOR_TEXT_ACCENT, "Saved!");
                                        pb_gfx_text(90, 178, PB_GFX_COLOR_TEXT,
                                                    "Wrote: sd:/pokebridge/saves/xd_edited.gci");
                                        pb_gfx_text(90, 200, PB_GFX_COLOR_TEXT_DIM,
                                                    "Slot encrypted + checksums recomputed.");
                                        pb_gfx_text(90, 218, PB_GFX_COLOR_TEXT_DIM,
                                                    "352 KB raw XD body, no Datel wrapper.");
                                    } else {
                                        pb_gfx_text(90, 150, PB_GFX_COLOR_PANEL_ACCENT,
                                                    "Write failed.");
                                    }
                                }
                                gfx_draw_hint_bar("Press any button");
                                pb_gfx_flip();
                                pb_gfx_wait_button();
                            }
                            else break;
                        }
                    }
                    break;
                }
                case 3: {
                    /* Load embedded Pokemon Colosseum save. */
                    static pb_colo_save_t cs;
                    if (!pb_colo_load(&cs, pb_embedded_colo_sav, pb_embedded_colo_sav_len)) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Colosseum");
                        gfx_draw_panel(60, 110, 520, 200, NULL);
                        pb_gfx_text(80, 150, PB_GFX_COLOR_PANEL_ACCENT,
                                    "Failed to load Colosseum save.");
                        gfx_draw_hint_bar("Press any button");
                        pb_gfx_flip();
                        pb_gfx_wait_button();
                        break;
                    }
                    int sub_sel = 0;
                    static const char *subs[] = { "Browse party", "Browse boxes",
                                                  "Write to SD (encrypted)", "Back" };
                    const int n_subs = 4;
                    for (;;) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Pokemon Colosseum save");
                        gfx_draw_panel(60, 80, 520, 110, "TRAINER");
                        char buf[64];
                        snprintf(buf, sizeof buf, "%s   ID %u   Saves %u",
                                 cs.trainer_name, cs.trainer_id, (unsigned)cs.save_count);
                        pb_gfx_text(80, 120, PB_GFX_COLOR_TEXT, buf);
                        gfx_draw_panel(60, 210, 520, 200, NULL);
                        for (int i = 0; i < n_subs; i++) {
                            int yy = 240 + i * 32;
                            uint32_t col = (i == sub_sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
                            if (i == sub_sel) pb_gfx_rounded_panel(75, yy - 6, 490, 24, 4, PB_GFX_COLOR_PANEL_LIGHT, 200);
                            pb_gfx_text(95, yy, col, subs[i]);
                        }
                        gfx_draw_hint_bar("DPad: select   A: open   B: back");
                        pb_gfx_flip();
                        uint16_t sb = pb_gfx_wait_button();
                        if (sb & PAD_BUTTON_B) break;
                        if (sb & PAD_BUTTON_UP)   sub_sel = (sub_sel + n_subs - 1) % n_subs;
                        if (sb & PAD_BUTTON_DOWN) sub_sel = (sub_sel + 1) % n_subs;
                        if (sb & PAD_BUTTON_A) {
                            if (sub_sel == 0) gfx_colo_party_screen(&cs);
                            else if (sub_sel == 1) gfx_colo_box_screen(&cs);
                            else if (sub_sel == 2) {
                                pb_gfx_clear();
                                gfx_draw_title_bar("Write Colosseum save");
                                gfx_draw_panel(60, 110, 520, 260, NULL);
                                if (!pb_sd_available) {
                                    pb_gfx_text(90, 150, PB_GFX_COLOR_PANEL_ACCENT,
                                                "No SD card -- cannot write.");
                                    pb_gfx_text(90, 178, PB_GFX_COLOR_TEXT_DIM,
                                                "Connect a Swiss-formatted SD on real");
                                    pb_gfx_text(90, 196, PB_GFX_COLOR_TEXT_DIM,
                                                "hardware to write encrypted Colosseum saves.");
                                } else {
                                    mkdir("sd:/pokebridge", 0777);
                                    mkdir("sd:/pokebridge/saves", 0777);
                                    bool ok = pb_colo_write_file(&cs, "sd:/pokebridge/saves/colo_edited.gci");
                                    if (ok) {
                                        pb_gfx_text(90, 150, PB_GFX_COLOR_TEXT_ACCENT, "Saved!");
                                        pb_gfx_text(90, 178, PB_GFX_COLOR_TEXT,
                                                    "Wrote: sd:/pokebridge/saves/colo_edited.gci");
                                        pb_gfx_text(90, 200, PB_GFX_COLOR_TEXT_DIM,
                                                    "Body SHA-1 + header checksum recomputed,");
                                        pb_gfx_text(90, 218, PB_GFX_COLOR_TEXT_DIM,
                                                    "slot re-encrypted with the same key chain.");
                                        pb_gfx_text(90, 244, PB_GFX_COLOR_TEXT_DIM,
                                                    "393 KB raw Colosseum body.");
                                    } else {
                                        pb_gfx_text(90, 150, PB_GFX_COLOR_PANEL_ACCENT,
                                                    "Write failed.");
                                    }
                                }
                                gfx_draw_hint_bar("Press any button");
                                pb_gfx_flip();
                                pb_gfx_wait_button();
                            }
                            else break;
                        }
                    }
                    break;
                }
                case 4: {
                    /* GBA cart via link cable. Untested -- no cable on hand,
                     * but the multiboot + save-dump protocol port mirrors
                     * FIX94's reference exactly. */
                    pb_gfx_clear();
                    gfx_draw_title_bar("GBA Link Cable");
                    gfx_draw_panel(60, 80, 520, 110, "STATUS");
                    pb_gfx_text(80, 110, PB_GFX_COLOR_TEXT,
                                "Plug a GBA into a controller port via the");
                    pb_gfx_text(80, 126, PB_GFX_COLOR_TEXT,
                                "GameCube-GBA link cable. Power on the GBA");
                    pb_gfx_text(80, 142, PB_GFX_COLOR_TEXT,
                                "with NO cart, reach the multiboot prompt,");
                    pb_gfx_text(80, 158, PB_GFX_COLOR_TEXT,
                                "then press A here to start.");
                    pb_gfx_text(80, 178, PB_GFX_COLOR_TEXT_DIM,
                                "(Press B to cancel)");

                    /* Boxart hint */
                    pb_gfx_boxart(60, 210, 200, 220, PB_BOXART_SAPPHIRE);

                    /* Right column: live port status */
                    gfx_draw_panel(280, 210, 300, 220, "PORT");
                    int port = pb_joybus_detect_gba_port();
                    char buf[64];
                    if (port < 0) {
                        pb_gfx_text(300, 240, PB_GFX_COLOR_PANEL_ACCENT,
                                    "No GBA detected.");
                    } else {
                        snprintf(buf, sizeof buf, "GBA found on port %d!", port + 1);
                        pb_gfx_text(300, 240, PB_GFX_COLOR_TEXT_ACCENT, buf);
                    }
                    pb_gfx_text(300, 270, PB_GFX_COLOR_TEXT_DIM,
                                "Expected sequence:");
                    pb_gfx_text(316, 290, PB_GFX_COLOR_TEXT_DIM,
                                "1. Multiboot dumper ROM");
                    pb_gfx_text(316, 306, PB_GFX_COLOR_TEXT_DIM,
                                "2. Wait for cart insertion");
                    pb_gfx_text(316, 322, PB_GFX_COLOR_TEXT_DIM,
                                "3. Read save into memory");
                    pb_gfx_text(316, 338, PB_GFX_COLOR_TEXT_DIM,
                                "4. Open in party browser");
                    pb_gfx_text(300, 372, PB_GFX_COLOR_PANEL_ACCENT,
                                "UNTESTED (no cable on hand).");
                    pb_gfx_text(300, 388, PB_GFX_COLOR_PANEL_ACCENT,
                                "Treat output cautiously.");

                    gfx_draw_hint_bar("A: start   B: cancel");
                    pb_gfx_flip();
                    uint16_t gb = pb_gfx_wait_button();
                    if (gb & PAD_BUTTON_B) break;
                    if (!(gb & PAD_BUTTON_A)) break;
                    if (port < 0) {
                        /* Try harder to detect */
                        port = pb_joybus_detect_gba_port();
                        if (port < 0) break;
                    }

                    /* Run multiboot with simple progress UI. */
                    pb_gfx_clear();
                    gfx_draw_title_bar("Multiboot");
                    gfx_draw_panel(60, 110, 520, 260, "SENDING DUMPER");
                    pb_gfx_text(90, 150, PB_GFX_COLOR_TEXT,
                                "Sending 60 KB GBA dumper ROM...");
                    pb_gfx_text(90, 170, PB_GFX_COLOR_TEXT_DIM,
                                "(takes a few seconds)");
                    pb_gfx_flip();

                    pb_joybus_status_t st = pb_joybus_multiboot(port, NULL, NULL);
                    if (st != PB_JOYBUS_OK) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Multiboot failed");
                        gfx_draw_panel(120, 180, 400, 140, NULL);
                        pb_gfx_text(140, 220, PB_GFX_COLOR_PANEL_ACCENT,
                                    "Multiboot did not complete.");
                        pb_gfx_text(140, 240, PB_GFX_COLOR_TEXT_DIM,
                                    "Try power-cycling the GBA and reconnecting.");
                        gfx_draw_hint_bar("Press any button");
                        pb_gfx_flip();
                        pb_gfx_wait_button();
                        break;
                    }

                    /* Get cart info. */
                    pb_joybus_cart_info_t info;
                    st = pb_joybus_get_cart_info(port, &info);
                    if (st != PB_JOYBUS_OK) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Insert a cart");
                        gfx_draw_panel(120, 180, 400, 140, NULL);
                        pb_gfx_text(140, 220, PB_GFX_COLOR_TEXT_ACCENT,
                                    "No cart detected on the GBA.");
                        pb_gfx_text(140, 240, PB_GFX_COLOR_TEXT_DIM,
                                    "Insert a Gen 3 Pokemon cart, retry.");
                        gfx_draw_hint_bar("Press any button");
                        pb_gfx_flip();
                        pb_gfx_wait_button();
                        break;
                    }

                    /* Dump the save. */
                    static uint8_t cart_save[PB_SAVE_SIZE + 1024];
                    size_t got = 0;
                    st = pb_joybus_dump_save(port, cart_save, sizeof cart_save,
                                             &got, NULL, NULL);
                    if (st != PB_JOYBUS_OK || got < PB_SLOT_SIZE) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Dump failed");
                        gfx_draw_panel(120, 180, 400, 140, NULL);
                        pb_gfx_text(140, 220, PB_GFX_COLOR_PANEL_ACCENT,
                                    "Save dump did not complete.");
                        gfx_draw_hint_bar("Press any button");
                        pb_gfx_flip();
                        pb_gfx_wait_button();
                        break;
                    }

                    /* Hand off to the existing graphics-mode party browser. */
                    static pb_save_t cart_s;
                    if (pb_save_load(&cart_s, cart_save, got)) {
                        int sub_sel = 0;
                        static const char *subs[] = { "Browse party", "Browse boxes",
                                                      "Save to SD", "Back" };
                        for (;;) {
                            pb_gfx_clear();
                            gfx_draw_title_bar("GBA cart loaded");
                            char buf2[64];
                            snprintf(buf2, sizeof buf2, "%s   %s   %s",
                                     info.game_name, info.game_id,
                                     pb_game_name(cart_s.game));
                            gfx_draw_panel(60, 80, 520, 110, "TRAINER");
                            pb_gfx_text(80, 120, PB_GFX_COLOR_TEXT, buf2);
                            snprintf(buf2, sizeof buf2, "%s   ID %u",
                                     cart_s.trainer_name, cart_s.trainer_id);
                            pb_gfx_text(80, 140, PB_GFX_COLOR_TEXT_DIM, buf2);
                            gfx_draw_panel(60, 210, 520, 200, NULL);
                            for (int j = 0; j < 4; j++) {
                                int yy = 240 + j * 32;
                                uint32_t col = (j == sub_sel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
                                if (j == sub_sel) pb_gfx_rounded_panel(75, yy - 6, 490, 24, 4, PB_GFX_COLOR_PANEL_LIGHT, 200);
                                pb_gfx_text(95, yy, col, subs[j]);
                            }
                            gfx_draw_hint_bar("DPad: select   A: open   B: back");
                            pb_gfx_flip();
                            uint16_t sb = pb_gfx_wait_button();
                            if (sb & PAD_BUTTON_B) break;
                            if (sb & PAD_BUTTON_UP)   sub_sel = (sub_sel + 3) % 4;
                            if (sb & PAD_BUTTON_DOWN) sub_sel = (sub_sel + 1) % 4;
                            if (sb & PAD_BUTTON_A) {
                                if (sub_sel == 0) gfx_pkm_party_screen(&cart_s);
                                else if (sub_sel == 1) gfx_pkm_box_screen(&cart_s);
                                else if (sub_sel == 2 && pb_sd_available) {
                                    mkdir("sd:/pokebridge", 0777);
                                    mkdir("sd:/pokebridge/saves", 0777);
                                    char savepath[64];
                                    snprintf(savepath, sizeof savepath,
                                             "sd:/pokebridge/saves/%s.sav", info.game_id);
                                    pb_save_write_file(&cart_s, savepath);
                                } else break;
                            }
                        }
                    } else {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Bad save data");
                        gfx_draw_panel(120, 180, 400, 140, NULL);
                        pb_gfx_text(140, 220, PB_GFX_COLOR_PANEL_ACCENT,
                                    "Save data didn't parse as Gen 3.");
                        gfx_draw_hint_bar("Press any button");
                        pb_gfx_flip();
                        pb_gfx_wait_button();
                    }
                    break;
                }
                case 5: {
                    /* SD save picker */
                    char paths[16][256];
                    int found = 0;
                    if (pb_sd_available) {
                        found = list_saves(paths, 16);
                    }
                    if (!pb_sd_available || found == 0) {
                        for (;;) {
                            pb_gfx_clear();
                            gfx_draw_title_bar("SD card");
                            gfx_draw_panel(60, 110, 520, 240, NULL);
                            if (!pb_sd_available) {
                                pb_gfx_text(90, 150, PB_GFX_COLOR_PANEL_ACCENT, "No SD card detected.");
                                pb_gfx_text(90, 174, PB_GFX_COLOR_TEXT_DIM, "Dolphin: no GameCube SD emulation in this build.");
                                pb_gfx_text(90, 192, PB_GFX_COLOR_TEXT_DIM, "Real hardware: insert an SD card and reboot via Swiss.");
                            } else {
                                pb_gfx_text(90, 150, PB_GFX_COLOR_PANEL_ACCENT, "No .sav files found.");
                                pb_gfx_text(90, 174, PB_GFX_COLOR_TEXT_DIM, "Put saves at:");
                                pb_gfx_text(90, 192, PB_GFX_COLOR_TEXT, "sd:/pokebridge/saves/");
                                pb_gfx_text(90, 220, PB_GFX_COLOR_TEXT_DIM, "PokeBridge will also scan:");
                                pb_gfx_text(90, 238, PB_GFX_COLOR_TEXT, "sd:/saves/  and  sd:/  (root)");
                            }
                            pb_gfx_text(90, 280, PB_GFX_COLOR_TEXT_ACCENT, "Exported .pk3 files land at:");
                            pb_gfx_text(90, 298, PB_GFX_COLOR_TEXT, "sd:/pokebridge/export/");
                            pb_gfx_text(90, 318, PB_GFX_COLOR_TEXT_ACCENT, "Edited saves land at:");
                            pb_gfx_text(90, 336, PB_GFX_COLOR_TEXT, "sd:/pokebridge/saves/edited.sav");
                            gfx_draw_hint_bar("B: back");
                            pb_gfx_flip();
                            uint16_t bb = pb_gfx_wait_button();
                            if (bb & PAD_BUTTON_B) break;
                        }
                        break;
                    }
                    /* List + pick. We peek each file's content/size to detect
                     * its game and show the matching box art. */
                    int psel = 0;
                    static pb_boxart_t arts[16];
                    for (int i = 0; i < found; i++) {
                        arts[i] = detect_boxart_by_file(paths[i]);
                    }
                    for (;;) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Pick a save");
                        gfx_draw_panel(28, 70, 340, 370, "SD CARD SAVES");
                        for (int i = 0; i < found; i++) {
                            int yy = 100 + i * 22;
                            uint32_t col = (i == psel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
                            if (i == psel) {
                                pb_gfx_rounded_panel(43, yy - 4, 310, 20, 4, PB_GFX_COLOR_PANEL_LIGHT, 180);
                            }
                            const char *base = paths[i];
                            const char *p = strrchr(paths[i], '/');
                            if (p) base = p + 1;
                            pb_gfx_text(55, yy, col, base);
                        }
                        /* Box art preview for the highlighted save */
                        pb_gfx_boxart(390, 70, 220, 320, arts[psel]);
                        gfx_draw_hint_bar("D-Pad: select   A: load   B: cancel");
                        pb_gfx_flip();
                        uint16_t bb = pb_gfx_wait_button();
                        if (bb & PAD_BUTTON_B) break;
                        if (bb & PAD_BUTTON_UP)   psel = (psel + found - 1) % found;
                        if (bb & PAD_BUTTON_DOWN) psel = (psel + 1) % found;
                        if (bb & PAD_BUTTON_A) {
                            /* Size-detect: 128KB = Gen 3 .sav, 352KB+wrapper = XD */
                            FILE *f = fopen(paths[psel], "rb");
                            if (!f) continue;
                            fseek(f, 0, SEEK_END);
                            long sz = ftell(f);
                            fseek(f, 0, SEEK_SET);
                            static uint8_t fbuf[PB_XD_SAVE_SIZE + PB_XD_WRAPPER_SIZE];
                            size_t maxr = (size_t)sz < sizeof fbuf ? (size_t)sz : sizeof fbuf;
                            size_t nr = fread(fbuf, 1, maxr, f);
                            fclose(f);
                            if (sz == PB_SAVE_SIZE || sz == PB_SAVE_SIZE / 2) {
                                static pb_save_t s;
                                if (pb_save_load(&s, fbuf, nr)) gfx_pkm_party_screen(&s);
                            } else if ((unsigned long)sz == PB_XD_SAVE_SIZE + PB_XD_WRAPPER_SIZE ||
                                       sz == PB_XD_SAVE_SIZE) {
                                static pb_xd_save_t xs;
                                if (pb_xd_load(&xs, fbuf, nr)) gfx_xd_party_screen(&xs);
                            }
                            /* else: unknown format; silently return to list */
                        }
                    }
                    break;
                }
                case 6: {
                    /* Game art gallery -- cycle through all 8 mainstream
                     * Gen 3 boxarts. */
                    static const pb_boxart_t gallery[] = {
                        PB_BOXART_RUBY,    PB_BOXART_SAPPHIRE,
                        PB_BOXART_EMERALD, PB_BOXART_FIRERED,
                        PB_BOXART_LEAFGREEN, PB_BOXART_COLOSSEUM,
                        PB_BOXART_XD,      PB_BOXART_BOX,
                    };
                    static const char *labels[] = {
                        "Pokemon Ruby (GBA)",
                        "Pokemon Sapphire (GBA)",
                        "Pokemon Emerald (GBA)",
                        "Pokemon FireRed (GBA)",
                        "Pokemon LeafGreen (GBA)",
                        "Pokemon Colosseum (GCN)",
                        "Pokemon XD: Gale of Darkness (GCN)",
                        "Pokemon Box: Ruby & Sapphire (GCN)",
                    };
                    const int gn = (int)(sizeof gallery / sizeof gallery[0]);
                    int gsel = 0;
                    for (;;) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("Game art gallery");

                        /* Big preview on the left */
                        pb_gfx_boxart(40, 70, 240, 360, gallery[gsel]);

                        /* Thumbnail strip on the right */
                        gfx_draw_panel(300, 70, 304, 360, "ALL GAMES");
                        for (int i = 0; i < gn; i++) {
                            int yy = 100 + i * 36;
                            uint32_t col = (i == gsel) ? PB_GFX_COLOR_TEXT_ACCENT : PB_GFX_COLOR_TEXT;
                            if (i == gsel) {
                                pb_gfx_rounded_panel(315, yy - 6, 274, 30, 4, PB_GFX_COLOR_PANEL_LIGHT, 200);
                            }
                            pb_gfx_text(330, yy + 8, col, labels[i]);
                        }
                        gfx_draw_hint_bar("DPad: cycle   B: back");
                        pb_gfx_flip();
                        uint16_t gb = pb_gfx_wait_button();
                        if (gb & PAD_BUTTON_B) break;
                        if (gb & (PAD_BUTTON_UP | PAD_TRIGGER_L))   gsel = (gsel + gn - 1) % gn;
                        if (gb & (PAD_BUTTON_DOWN | PAD_TRIGGER_R)) gsel = (gsel + 1) % gn;
                    }
                    break;
                }
                case 7: {
                    /* About */
                    for (;;) {
                        pb_gfx_clear();
                        gfx_draw_title_bar("About PokeBridge");

                        /* Header with version + tagline */
                        gfx_draw_panel(28, 64, 580, 62, NULL);
                        pb_gfx_text_scale(48, 76,  PB_GFX_COLOR_TEXT_ACCENT, 2, "PokeBridge v0.4");
                        pb_gfx_text(48, 100, PB_GFX_COLOR_TEXT_DIM,
                                    "Gen 3 save reader, editor, and HOME-chain bridge for GameCube.");

                        /* Reads column */
                        gfx_draw_panel(28, 140, 280, 280, "READS");
                        pb_gfx_text(48, 168, PB_GFX_COLOR_TEXT, "* Ruby / Sapphire (GBA)");
                        pb_gfx_text(48, 184, PB_GFX_COLOR_TEXT, "* Emerald (GBA)");
                        pb_gfx_text(48, 200, PB_GFX_COLOR_TEXT, "* FireRed / LeafGreen (GBA)");
                        pb_gfx_text(48, 216, PB_GFX_COLOR_TEXT, "* pokeemerald-expansion ROM hacks");
                        pb_gfx_text(64, 230, PB_GFX_COLOR_TEXT_DIM, "(Seaglass, Lazarus, etc.)");
                        pb_gfx_text(48, 250, PB_GFX_COLOR_TEXT, "* Pokemon XD: Gale of Darkness");
                        pb_gfx_text(64, 264, PB_GFX_COLOR_TEXT_DIM, "(decrypts both save slots)");
                        pb_gfx_text(48, 290, PB_GFX_COLOR_TEXT_DIM, "Coming soon:");
                        pb_gfx_text(48, 306, PB_GFX_COLOR_TEXT_DIM, "* Pokemon Colosseum");
                        pb_gfx_text(48, 322, PB_GFX_COLOR_TEXT_DIM, "* Pokemon Box: R&S");
                        pb_gfx_text(48, 338, PB_GFX_COLOR_TEXT_DIM, "* GBA cart via link cable");
                        pb_gfx_text(48, 372, PB_GFX_COLOR_TEXT_ACCENT, "Sprites:");
                        pb_gfx_text(48, 388, PB_GFX_COLOR_TEXT, "1025 species + shiny");
                        pb_gfx_text(48, 402, PB_GFX_COLOR_TEXT_DIM, "(4bpp + palette, pokeemerald-expansion)");

                        /* Edits column */
                        gfx_draw_panel(322, 140, 286, 280, "EDITS & EXPORTS");
                        pb_gfx_text(342, 168, PB_GFX_COLOR_TEXT, "Per-Pokemon editor:");
                        pb_gfx_text(360, 184, PB_GFX_COLOR_TEXT_DIM, "* IVs (live progress bars)");
                        pb_gfx_text(360, 200, PB_GFX_COLOR_TEXT_DIM, "* EVs (capped 0..255)");
                        pb_gfx_text(360, 216, PB_GFX_COLOR_TEXT_DIM, "* Moves (full 354 picker)");
                        pb_gfx_text(360, 232, PB_GFX_COLOR_TEXT_DIM, "* Nature (PID re-roll)");
                        pb_gfx_text(360, 248, PB_GFX_COLOR_TEXT_DIM, "* Shiny (live sprite swap)");
                        pb_gfx_text(360, 264, PB_GFX_COLOR_TEXT_DIM, "* Friendship / item");
                        pb_gfx_text(342, 290, PB_GFX_COLOR_TEXT_ACCENT, "Save writeback:");
                        pb_gfx_text(360, 306, PB_GFX_COLOR_TEXT_DIM, "* Gen 3 (.sav + checksums)");
                        pb_gfx_text(360, 322, PB_GFX_COLOR_TEXT_DIM, "* XD (encrypt + checksums)");
                        pb_gfx_text(342, 348, PB_GFX_COLOR_TEXT_ACCENT, "Legalizer:");
                        pb_gfx_text(360, 364, PB_GFX_COLOR_TEXT_DIM, "* Hack-mon -> Gen 3 legal .pk3");
                        pb_gfx_text(360, 380, PB_GFX_COLOR_TEXT_DIM, "* ROM-hack species remap");
                        pb_gfx_text(360, 396, PB_GFX_COLOR_TEXT_DIM, "* HOME chain documented");

                        gfx_draw_hint_bar("B: back");
                        pb_gfx_flip();
                        uint16_t bb = pb_gfx_wait_button();
                        if (bb & PAD_BUTTON_B) break;
                    }
                    break;
                }
                case 8: return;
            }
        }
    }
}

/* ---- XD party / box browsers ---- */

static void show_xd_pkm_detail(const pb_xd_pkm_t *xp) {
    pb_ui_header("Pokemon (XD)");
    printf("Nickname : \"%s\"\n", xp->nickname);
    printf("Species  : XD#%u%s\n", xp->species_internal,
           xp->is_shadow ? "  [SHADOW]" : "");
    printf("Level    : %u\n", xp->level);
    printf("OT       : \"%s\"   TID:%u  SID:%u\n",
           xp->ot_name, xp->trainer_id, xp->secret_id);
    printf("PID      : 0x%08X\n", (unsigned)xp->pid);
    printf("EXP      : %u\n", (unsigned)xp->exp);
    printf("Item     : %u\n", xp->held_item);
    printf("Moves    : %u / %u / %u / %u\n",
           xp->moves[0], xp->moves[1], xp->moves[2], xp->moves[3]);
    printf("IVs (XD) : %u/%u/%u/%u/%u/%u\n",
           xp->iv[0], xp->iv[1], xp->iv[2],
           xp->iv[3], xp->iv[4], xp->iv[5]);
    pb_ui_footer("X: edit   B: back");
}

void pb_ui_browse_xd_party(pb_xd_save_t *xs) {
    if (!xs->party_offset) {
        pb_ui_header("XD party");
        printf("Party offset not resolved.\n");
        pb_ui_footer("B: back"); pb_ui_wait_button(); return;
    }
    int sel = 0;
    for (;;) {
        pb_ui_header("XD party");
        for (int i = 0; i < 6; i++) {
            uint8_t *slot_raw = xs->slot + xs->party_offset + i * PB_XD_PKM_SIZE;
            pb_xd_pkm_t xp; pb_xd_pkm_decode(&xp, slot_raw);
            const char *cur = (i == sel) ? "\x1b[32m>\x1b[0m" : " ";
            if (xp.is_empty) printf("%s %d. ----\n", cur, i + 1);
            else printf("%s %d. %-12s  XD#%u  L%u%s\n", cur, i + 1,
                        xp.nickname, xp.species_internal, xp.level,
                        xp.is_shadow ? " *S" : "");
        }
        pb_ui_footer("DPad: select   A: view   B: back");
        uint16_t b = pb_ui_wait_button();
        if (b & PAD_BUTTON_B) return;
        if (b & PAD_BUTTON_UP)   sel = (sel + 5) % 6;
        if (b & PAD_BUTTON_DOWN) sel = (sel + 1) % 6;
        if (b & PAD_BUTTON_A) {
            uint8_t *slot_raw = xs->slot + xs->party_offset + sel * PB_XD_PKM_SIZE;
            pb_xd_pkm_t xp; pb_xd_pkm_decode(&xp, slot_raw);
            if (xp.is_empty) continue;
            show_xd_pkm_detail(&xp);
            uint16_t b2 = pb_ui_wait_button();
            if (b2 & PAD_BUTTON_X) {
                pb_pkm_t p; pb_xk3_to_pkm(&p, slot_raw);
                if (pb_ui_edit_pkm(&p)) {
                    pb_xk3_apply_pkm_edits(&p, slot_raw);
                    pb_ui_header("Saved");
                    printf("Edits written to in-memory XD save.\n");
                    printf("(Encrypt+save-to-SD comes in a follow-up.)\n");
                    pb_ui_footer("Press any button");
                    pb_ui_wait_button();
                }
            }
        }
    }
}

void pb_ui_browse_xd_boxes(pb_xd_save_t *xs) {
    if (!xs->box_offset) {
        pb_ui_header("XD boxes");
        printf("Box offset not resolved.\n");
        pb_ui_footer("B: back"); pb_ui_wait_button(); return;
    }
    int box = 0, slot = 0;
    for (;;) {
        pb_ui_header("XD PC Boxes");
        printf("Box %d/%d\n\n", box + 1, PB_XD_BOX_COUNT);
        int page = slot / 10;
        for (int i = 0; i < 10; i++) {
            int idx = page * 10 + i;
            if (idx >= PB_XD_BOX_SIZE) break;
            uint32_t off = pb_xd_box_slot_offset(xs, box, idx);
            pb_xd_pkm_t xp; pb_xd_pkm_decode(&xp, xs->slot + off);
            const char *cur = (idx == slot) ? "\x1b[32m>\x1b[0m" : " ";
            if (xp.is_empty) printf("%s %2d. ----\n", cur, idx + 1);
            else             printf("%s %2d. %-12s  XD#%u  L%u%s\n", cur, idx + 1,
                                    xp.nickname, xp.species_internal, xp.level,
                                    xp.is_shadow ? " *S" : "");
        }
        pb_ui_footer("DPad: nav   L/R: box   A: view   B: back");
        uint16_t bt = pb_ui_wait_button();
        if (bt & PAD_BUTTON_B) return;
        if (bt & PAD_BUTTON_UP)    slot = (slot + PB_XD_BOX_SIZE - 1) % PB_XD_BOX_SIZE;
        if (bt & PAD_BUTTON_DOWN)  slot = (slot + 1) % PB_XD_BOX_SIZE;
        if (bt & PAD_TRIGGER_L)    box  = (box + PB_XD_BOX_COUNT - 1) % PB_XD_BOX_COUNT;
        if (bt & PAD_TRIGGER_R)    box  = (box + 1) % PB_XD_BOX_COUNT;
        if (bt & PAD_BUTTON_A) {
            uint32_t off = pb_xd_box_slot_offset(xs, box, slot);
            uint8_t *slot_raw = xs->slot + off;
            pb_xd_pkm_t xp; pb_xd_pkm_decode(&xp, slot_raw);
            if (xp.is_empty) continue;
            show_xd_pkm_detail(&xp);
            uint16_t b2 = pb_ui_wait_button();
            if (b2 & PAD_BUTTON_X) {
                pb_pkm_t p; pb_xk3_to_pkm(&p, slot_raw);
                if (pb_ui_edit_pkm(&p)) {
                    pb_xk3_apply_pkm_edits(&p, slot_raw);
                }
            }
        }
    }
}

void pb_ui_menu_main(void) {
    int sel = 0;
    static const char *items[] = {
        "Load save from SD card",
        "Load embedded firered.sav (demo)",
        "Load embedded Pokemon XD save (demo)",
        "Read GBA cart via link cable (TODO)",
        "About PokeBridge",
        "Exit",
    };
    int n = sizeof items / sizeof items[0];
    for (;;) {
        pb_ui_header("Main menu");
        for (int i = 0; i < n; i++) {
            printf("  %s %s\n", i == sel ? "\x1b[32m>\x1b[0m" : " ", items[i]);
        }
        int gba_port = pb_joybus_detect_gba_port();
        if (gba_port) printf("\n\x1b[36mGBA detected on port %d.\x1b[0m\n", gba_port);
        else          printf("\n\x1b[2m(no GBA detected)\x1b[0m\n");
        pb_ui_footer("D-Pad: select   A: choose   START: exit");
        uint16_t b = pb_ui_wait_button();
        if (b & PAD_BUTTON_START) return;
        if (b & PAD_BUTTON_UP)    sel = (sel + n - 1) % n;
        if (b & PAD_BUTTON_DOWN)  sel = (sel + 1) % n;
        if (b & PAD_BUTTON_A) {
            if (sel == 0) {
                char paths[16][256];
                int found = list_saves(paths, 16);
                if (found == 0) {
                    pb_ui_header("Load save");
                    printf("No .sav files found.\n");
                    printf("Put your save at sd:/pokebridge/saves/your.sav\n");
                    pb_ui_footer("Press any button");
                    pb_ui_wait_button();
                    continue;
                }
                int psel = 0;
                for (;;) {
                    pb_ui_header("Load save");
                    for (int i = 0; i < found; i++) {
                        printf("  %s %s\n", i == psel ? "\x1b[32m>\x1b[0m" : " ", paths[i]);
                    }
                    pb_ui_footer("A: load   B: cancel");
                    uint16_t bb = pb_ui_wait_button();
                    if (bb & PAD_BUTTON_B) break;
                    if (bb & PAD_BUTTON_UP)   psel = (psel + found - 1) % found;
                    if (bb & PAD_BUTTON_DOWN) psel = (psel + 1) % found;
                    if (bb & PAD_BUTTON_A) {
                        static pb_save_t s;
                        if (!pb_save_load_file(&s, paths[psel])) {
                            pb_ui_header("Load save");
                            printf("FAILED to parse %s\n", paths[psel]);
                            pb_ui_footer("Press any button");
                            pb_ui_wait_button();
                            continue;
                        }
                        for (;;) {
                            pb_ui_show_save_summary(&s);
                            uint16_t b2 = pb_ui_wait_button();
                            if (b2 & PAD_BUTTON_B) break;
                            if (b2 & PAD_BUTTON_A) pb_ui_browse_party(&s);
                            if (b2 & PAD_BUTTON_X) pb_ui_browse_boxes(&s);
                        }
                        break;
                    }
                }
            } else if (sel == 1) {
                static pb_save_t s;
                if (!pb_save_load(&s, pb_embedded_firered_sav, pb_embedded_firered_sav_len)) {
                    pb_ui_header("Embedded save");
                    printf("Embedded save failed to parse.\n");
                    pb_ui_footer("Press any button");
                    pb_ui_wait_button();
                    continue;
                }
                for (;;) {
                    pb_ui_show_save_summary(&s);
                    uint16_t b2 = pb_ui_wait_button();
                    if (b2 & PAD_BUTTON_B) break;
                    if (b2 & PAD_BUTTON_A) pb_ui_browse_party(&s);
                    if (b2 & PAD_BUTTON_X) pb_ui_browse_boxes(&s);
                }
            } else if (sel == 2) {
                /* Load embedded XD save */
                static pb_xd_save_t xs;
                if (!pb_xd_load(&xs, pb_embedded_xd_sav, pb_embedded_xd_sav_len)) {
                    pb_ui_header("XD save");
                    printf("Failed to load embedded XD save.\n");
                    pb_ui_footer("Press any button");
                    pb_ui_wait_button();
                    continue;
                }
                for (;;) {
                    pb_ui_header("Pokemon XD save");
                    printf("Trainer    : \"%s\"\n", xs.trainer_name);
                    printf("TID / SID  : %u / %u\n", xs.trainer_id, xs.secret_id);
                    printf("Active slot: %d\n", xs.active_slot);
                    printf("Save count : %u\n", (unsigned)xs.save_count);
                    printf("Party      : %u/6\n", xs.party_count);
                    printf("Box offset : 0x%X%s\n", (unsigned)xs.box_offset,
                           xs.box_offset ? "" : " (could not resolve)");
                    pb_ui_footer("A: party   X: boxes   Y: Pokemon Box-style view   B: back");
                    uint16_t b2 = pb_ui_wait_button();
                    if (b2 & PAD_BUTTON_B) break;
                    if (b2 & PAD_BUTTON_A) pb_ui_browse_xd_party(&xs);
                    if (b2 & PAD_BUTTON_X) pb_ui_browse_xd_boxes(&xs);
                    if (b2 & PAD_BUTTON_Y) gfx_xd_party_screen(&xs);
                }
            } else if (sel == 3) {
                pb_ui_header("Link cable");
                printf("Not implemented yet - coming in v0.2.\n");
                printf("Joybus + GBA multiboot save dumper.\n");
                pb_ui_footer("Press any button");
                pb_ui_wait_button();
            } else if (sel == 4) {
                pb_ui_header("About");
                printf("PokeBridge v0.2\n");
                printf("Reads Gen 3 Pokemon saves on GameCube.\n");
                printf("Includes XD: Gale of Darkness support (read-only).\n");
                pb_ui_footer("Press any button");
                pb_ui_wait_button();
            } else if (sel == 5) {
                return;
            }
        }
    }
}
