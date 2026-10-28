// Batch 340 | Date: 2026-09-04
// HUD end-of-round results / stats screen. A game-mode controller
// (FUN_80222cdc) drives a 6-stage scroll state machine that counts values up
// on screen (with a skip-on-A-button), and hud_render_player_view calls the
// renderers each frame. State lives in the DAT_80611fa0.. block:
//   +0x00 DAT_80611fa0  target value (stage-dependent)
//   +0x08 DAT_80611fa8  active count-up sound handle
//   +0x0c DAT_80611fac  flags (bit0 = show accuracy line / doubled target)
//   +0x10 DAT_80611fb0  stage (0=idle 1..6)
//   +0x14 DAT_80611fb4  animated running value (drawn)
//   +0x18 DAT_80611fb8  secondary running value
//   +0x1c DAT_80611fbc  tertiary running value
//   +0x20 DAT_80611fc0  base value
//   +0x24 DAT_80611fc4  bonus value (>=0)
//   +0x34 DAT_80611fd4  per-stage countdown timer
//
//   0x8011e6b4 hud_results_reset             (clear state block)
//   0x8011e6cc hud_results_init              (multi-value scroll setup + sound)
//   0x8011e784 hud_results_init_simple       (single-value setup + sound)
//   0x8011e7dc hud_results_scroll_tick       (6-stage scroll state machine)
//   0x8011eb24 hud_results_render            (title + stat table)
//   0x8011f660 hud_results_poll_finished     (returns 1 once at stage 6)
//   0x8011f684 hud_results_top_margin        (top Y offset for split-screen)
//   0x8011f6f4 hud_results_mode_shows_stats  (game-mode gate)
//   0x8011f768 hud_results_should_show       (gate wrapper, returns -1/-2)
//   0x8011f7bc hud_results_render_award_icons(award/medal icon row)
//   0x8011fb84 hud_results_render_player_icons(per-player portrait row)
//   0x8011ff48 hud_results_render_team_names (winning team / player name banner)
//
// Subsystem: game (HUD rendering)

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================

// scalar float constants
extern float FLOAT_80615718;   // 1.0
extern float FLOAT_80615730;   // 0.0
extern float FLOAT_80615734;   // 0.5
extern float FLOAT_80615750;
extern float FLOAT_8061576c;
extern float FLOAT_8061577c;
extern float FLOAT_806157c0;
extern float FLOAT_806157c8;
extern float FLOAT_806157f0;
extern float FLOAT_80615754;   // 1.5-ish column offset scale
extern float FLOAT_80615868;
extern float FLOAT_80615884;
extern float FLOAT_8061588c;
extern float FLOAT_8061082c;
extern float FLOAT_80610830;

extern double DOUBLE_80615738; // 0x4330..0 int->double bias

// results scroll state block (individually-addressed globals)
extern int DAT_80611fa0;
extern int DAT_80611fa4;
extern int DAT_80611fa8;
extern int DAT_80611fac;
extern int DAT_80611fb0;
extern int DAT_80611fb4;
extern int DAT_80611fb8;
extern int DAT_80611fbc;
extern int DAT_80611fc0;
extern int DAT_80611fc4;
extern int DAT_80611fc8;
extern int DAT_80611fcc;
extern int DAT_80611fd0;
extern int DAT_80611fd4;

// other globals
extern uint  DAT_806109c0;      // render width
extern int   DAT_806108e4;      // per-frame scroll speed
extern int*  DAT_80611748;      // viewport rect
extern int   DAT_80611720;      // HUD layout Y (center row)
extern int   DAT_80611734;      // HUD layout X (center col)
extern int   DAT_80611928;      // wobble phase accumulator
extern int   DAT_80611fe0;      // player-count-derived margin input
extern int*  DAT_80611d74;      // current player-view pointer
extern int   DAT_80611d78;      // player-slot table base
extern int   DAT_80611d90;      // score record table base
extern int   DAT_80612098;      // tracked-player count
extern int   DAT_806120d8;      // player mask (excluded)
extern int   DAT_806120fc;      // extra score-record count
extern int   DAT_80612244;      // widescreen flag
extern int   DAT_80612610;      // real-player count threshold
extern int   DAT_80612614;      // special mode id (100 = ...)
extern unsigned char DAT_805015bd; // game phase
extern unsigned char DAT_805015be; // game mode id
extern int   DAT_80579de8;      // team-name definition record ptr
extern int   DAT_805014a0;      // award-slot player index table (int[])
extern int   DAT_8057ff50;      // layout offset
extern int*  DAT_80610a20;      // texture descriptor table base
extern float DAT_80583228;      // sin/cos LUT
extern int   DAT_803f2c7c;      // per-team color table (5-word stride)
extern int   DAT_803f2c90;      // team-name color A
extern int   DAT_803f2ca4;      // team-name color B
extern int   DAT_80610858;      // number-format context
extern int   DAT_80610864;      // value-format context
extern int   DAT_806108a0;      // column-format context

// helpers (raw)
extern int    FUN_801c1714(void);              // active player count
extern int    sound_init_play_params(int buf);
extern int    sound_play_request(int* buf);
extern int    sound_stop_handle(void);
extern uint   input_get_buttons_held(int pad);
extern unsigned long long FUN_803826f0(void);  // HUD state push (variant)
extern int    FUN_8038273c(void);              // HUD state pop (variant)
extern unsigned long long FUN_803826b8(void);  // HUD state push (returns rect)
extern int    FUN_80382704(void);              // HUD state pop (variant)
extern unsigned long long FUN_803826e0(void);  // HUD state push (variant)
extern int    FUN_8038272c(void);              // HUD state pop (variant)
extern int    FUN_8016f00c(int mode);
extern int    FUN_8016e77c(int texId);
extern int    text_set_font_table(int table);
extern int    get_string_by_id(uint id);
extern unsigned char* FUN_8012e878(unsigned long long, double, double, double, double, double,
                                   double, double, int fmt, int arg, uint, uint, uint,
                                   uint, uint, uint); // format text (varied use)
extern int    text_get_dimensions_int(int* w, int* h, unsigned char* str);
extern double text_get_char_width(unsigned char* str);
extern double text_get_char_height(unsigned char* str);
extern unsigned long long text_draw_char_shadowed(double x, double y, unsigned long long str, uint c0, uint c1);
extern unsigned long long text_draw_char(double x, double y, unsigned char* str, uint color);
extern int    text_apply_alpha(double a, unsigned char* str, int flags);
extern int    text_set_color_rgb(double r, double g, double b);
extern unsigned long long time_format_long(double, double, double, double, double, double, double, double);
extern int    leaf_92c0(int mode);              // GX layer toggle
extern int    sprite_get_texture_id(int spriteId);
extern int    FUN_803825c0(double v);           // float -> clamped color byte
extern int    FUN_801708b4(double, double, double, double, int, int, uint, uint); // wobbled quad
extern int    FUN_80170410(int, int, int, int, int, int, uint, uint);             // int-rect quad
extern int    score_get_special(int rec);
extern int    FUN_802c7788(void);               // award -> sprite id
extern unsigned char* FUN_80350540(double, double, double, double, double, double, double, double); // special team name

// ============================================================================
// 0x8011e6b4 | hud_results_reset
// Clears the results scroll state (target, sound handle, stage, running value).
// ============================================================================
extern "C" void hud_results_reset(void) {
    DAT_80611fa0 = 0;
    DAT_80611fac = 0;
    DAT_80611fb0 = 0;
    DAT_80611fb4 = 0;
    return;
}

// ============================================================================
// 0x8011e6cc | hud_results_init
// Sets up a multi-value count-up sequence (base + bonus, optionally doubled
// when flag bit0 is set), seeds stage 2, and starts the looping count-up sound
// (0xd65). base + bonus + the two extra columns (col2/col3) are stored.
// ============================================================================
extern "C" void hud_results_init(int base, uint bonus, int col2, int col3, uint flags) {
    int localBuf[12];
    DAT_80611fc4 = bonus & ~((int)bonus >> 0x1f);      // max(bonus, 0)
    DAT_80611fa0 = base + bonus;
    DAT_80611fc8 = 0;
    if ((flags & 1) != 0) {
        DAT_80611fa0 = DAT_80611fa0 * 2;
    }
    DAT_80611fd4 = DAT_806109c0 << 1;
    DAT_80611fb4 = 0;
    DAT_80611fb0 = 2;
    DAT_80611fac = flags;
    DAT_80611fc0 = base;
    DAT_80611fcc = col2;
    DAT_80611fd0 = col3;
    sound_init_play_params((int)localBuf);
    localBuf[0] = 0xd65;
    DAT_80611fa8 = sound_play_request(localBuf);
    DAT_80611fb8 = base;
    DAT_80611fbc = bonus & ~((int)bonus >> 0x1f);
    return;
}

// ============================================================================
// 0x8011e784 | hud_results_init_simple
// Single-value count-up setup (stage 1). Starts the count-up sound (0xd65).
// ============================================================================
extern "C" void hud_results_init_simple(int target, int flags) {
    int localBuf[14];
    DAT_80611fa4 = 0;
    DAT_80611fb4 = 0xfffffff6;   // -10 (start below zero so first ramp is visible)
    DAT_80611fb0 = 1;
    DAT_80611fa0 = target;
    DAT_80611fac = flags;
    sound_init_play_params((int)localBuf);
    localBuf[0] = 0xd65;
    DAT_80611fa8 = sound_play_request(localBuf);
    return;
}

// ============================================================================
// 0x8011e7dc | hud_results_scroll_tick
// The scroll state machine. Ramps the animated running value toward each
// stage's target, plays the tick sound (0xd66) on stage transitions, and lets
// the player skip a stage by holding the pad button. Stage order:
//   1 -> 4 (single value), 2 -> 3 -> 5, and a final 5/6 wait stage.
// ============================================================================
extern "C" void hud_results_scroll_tick(void) {
    int buf1[12], buf2[12], buf3[12], buf4[12], buf5[12], buf6[14];

    if (DAT_80611fb0 == 3) {
        DAT_80611fd4 = DAT_80611fd4 - DAT_806108e4;
        if (DAT_80611fd4 < 0) {
            int tgt = DAT_80611fc0 + DAT_80611fc4;
            if (DAT_80611fb4 < tgt) {
                DAT_80611fb4 = DAT_80611fb4 + DAT_806108e4 * 10;
                DAT_80611fbc = DAT_80611fbc + DAT_806108e4 * -10;
            }
            if (tgt <= DAT_80611fb4) {
                DAT_80611fd4 = DAT_806109c0 * 3;
                DAT_80611fbc = 0;
                DAT_80611fb0 = 5;
                DAT_80611fb4 = tgt;
                sound_stop_handle();
                DAT_80611fa8 = 0;
                sound_init_play_params((int)buf2);
                buf2[0] = 0xd66;
                sound_play_request(buf2);
            }
            uint held = input_get_buttons_held(0);
            if ((*(uint*)(DAT_80611d74 + 0x20) & held) != 0) {
                DAT_80611fb0 = 5;
                DAT_80611fd4 = DAT_806109c0 * 3;
                sound_stop_handle();
                DAT_80611fa8 = 0;
                sound_init_play_params((int)buf1);
                buf1[0] = 0xd66;
                sound_play_request(buf1);
                DAT_80611fbc = 0;
                DAT_80611fb4 = DAT_80611fc0 + DAT_80611fc4;
            }
        }
    } else if (DAT_80611fb0 < 3) {
        if (DAT_80611fb0 == 1) {
            if (DAT_80611fb4 < DAT_80611fa0) {
                DAT_80611fb4 = DAT_80611fb4 + 10;
            }
            if (DAT_80611fa0 <= DAT_80611fb4) {
                DAT_80611fb4 = DAT_80611fa0;
                DAT_80611fd4 = DAT_806109c0 * 3;
                DAT_80611fb0 = 4;
                sound_stop_handle();
                DAT_80611fa8 = 0;
                sound_init_play_params((int)buf6);
                buf6[0] = 0xd66;
                sound_play_request(buf6);
            }
            uint held = input_get_buttons_held(0);
            if ((*(uint*)(DAT_80611d74 + 0x20) & held) != 0) {
                DAT_80611fb0 = 4;
                DAT_80611fd4 = DAT_806109c0 * 3;
                sound_stop_handle();
                DAT_80611fa8 = 0;
                sound_init_play_params((int)buf4);
                buf4[0] = 0xd66;
                sound_play_request(buf4);
                DAT_80611fb4 = DAT_80611fa0;
            }
        } else if ((0 < DAT_80611fb0) &&
                   (DAT_80611fd4 = DAT_80611fd4 - DAT_806108e4, DAT_80611fd4 < 0)) {
            if (DAT_80611fb4 < DAT_80611fc0) {
                DAT_80611fb4 = DAT_80611fb4 + DAT_806108e4 * 10;
                DAT_80611fb8 = DAT_80611fb8 + DAT_806108e4 * -10;
            }
            if (DAT_80611fc0 <= DAT_80611fb4) {
                DAT_80611fb4 = DAT_80611fc0;
                DAT_80611fd4 = DAT_806109c0 * 3;
                DAT_80611fb8 = 0;
                DAT_80611fb0 = 3;
                sound_stop_handle();
                DAT_80611fa8 = 0;
                sound_init_play_params((int)buf5);
                buf5[0] = 0xd66;
                sound_play_request(buf5);
            }
            uint held = input_get_buttons_held(0);
            if ((*(uint*)(DAT_80611d74 + 0x20) & held) != 0) {
                DAT_80611fb0 = 3;
                DAT_80611fd4 = DAT_806109c0;
                sound_stop_handle();
                DAT_80611fa8 = 0;
                sound_init_play_params((int)buf3);
                buf3[0] = 0xd66;
                sound_play_request(buf3);
                DAT_80611fb8 = 0;
                DAT_80611fb4 = DAT_80611fc0;
            }
        }
    } else if ((DAT_80611fb0 != 6) && (DAT_80611fb0 < 6)) {
        DAT_80611fd4 = DAT_80611fd4 - DAT_806108e4;
        if (DAT_80611fd4 < 1) {
            DAT_80611fb0 = 6;
        }
        uint held = input_get_buttons_held(0);
        if ((*(uint*)(DAT_80611d74 + 0x20) & held) != 0) {
            DAT_80611fb0 = 6;
        }
    }
    return;
}

// ============================================================================
// 0x8011f660 | hud_results_poll_finished
// Returns 1 (and resets to idle) once the sequence has reached the final wait
// stage (6); otherwise 0. The controller uses this to advance the flow.
// ============================================================================
extern "C" uint hud_results_poll_finished(void) {
    if (DAT_80611fb0 == 6) {
        DAT_80611fb0 = 0;
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x8011f684 | hud_results_top_margin
// Top Y offset for the results panel. 0 in single-screen; in split-screen it
// scales a player-count-derived value (DAT_80611fe0) into pixels.
// ============================================================================
extern "C" int hud_results_top_margin(void) {
    int margin;
    if (FUN_801c1714() < 2) {
        margin = 0;
    } else {
        int halved = ((int)DAT_80611fe0 >> 1) +
                     (uint)((int)DAT_80611fe0 < 0 && (DAT_80611fe0 & 1) != 0) + 6;
        margin = (int)(FLOAT_80610830 *
                       (float)((double)(longlong)(int)halved - (double)0.0));
    }
    return margin;
}

// ============================================================================
// 0x8011f6f4 | hud_results_mode_shows_stats
// Game-mode gate: returns 1 when the current phase/mode should display the
// stats screen. A set of specific mode ids (5,6,7,9,0xb,0xc,0xf,0x12) qualify.
// ============================================================================
extern "C" uint hud_results_mode_shows_stats(void) {
    if ((DAT_805015bd != 6) &&
        (((5 < DAT_805015bd || (3 < DAT_805015bd)) || (DAT_805015bd == 0)))) {
        switch (DAT_805015be) {
            case 5: case 6: case 7: case 9:
            case 0xb: case 0xc: case 0xf: case 0x12:
                return 1;
            default:
                return 0;
        }
    }
    return 1;
}

// ============================================================================
// 0x8011f768 | hud_results_should_show
// Wrapper over hud_results_mode_shows_stats (also true for phase 9). Returns
// the game's -1 (true) / -2 (false) tri-state encoding.
// ============================================================================
extern "C" char hud_results_should_show(void) {
    bool show = false;
    if ((hud_results_mode_shows_stats() != 0) || (DAT_805015bd == 9)) {
        show = true;
    }
    return (char)(-1 - !show);
}

// ============================================================================
// 0x8011eb24 | hud_results_render
// Draws the results panel for the current scroll stage. At stage 4 (or 1) it
// draws the title + a single value; otherwise it lays out the full stat table:
// four columns (label strings 0x1602..0x1605) with per-player formatted times
// and running values, centered on the HUD. Called from hud_render_player_view.
// (Faithful non-matching translation; the exact text-format argument packing
//  is preserved via FUN_8012e878.)
// ============================================================================
extern "C" void hud_results_render(void) {
    unsigned long long ctx = FUN_803826f0();
    double d0 = 0.0;
    int w, h;

    if (DAT_80611fb0 == 4 || DAT_80611fb0 == 1) {
        if (DAT_80611fa0 != 0) {
            if ((DAT_80611fac & 1) != 0) {
                // optional accuracy/bonus title line (string 0x15aa)
                FUN_8016f00c(1);
                text_set_font_table(0xd);
                int sid = get_string_by_id(0x15aa);
                unsigned char* str = (unsigned char*)FUN_8012e878(ctx, d0, d0, d0, d0, d0, d0, d0,
                                                                  sid, 0, 0, 0, 0, 0, 0, 0);
                text_get_dimensions_int(&w, &h, str);
                int cx = DAT_80611734 - (w >> 1);
                int cy = (h >> 1) + 0x4b;
                unsigned char* str2 = (unsigned char*)FUN_8012e878(ctx, d0, d0, d0, d0, d0, d0, d0,
                                                                   get_string_by_id(0x15aa),
                                                                   (h >> 1), DAT_80611734, (w >> 1),
                                                                   0, 0, 0, 0);
                text_draw_char_shadowed((double)cx, (double)(0xfa - cy),
                                        (unsigned long long)(uintptr_t)str2, 0x4069697f, 0x507f);
            }
            // main value line (string 0x15ab) centered
            text_set_font_table(9);
            int sid = get_string_by_id(0x15ab);
            unsigned char* str = (unsigned char*)FUN_8012e878(ctx, d0, d0, d0, d0, d0, d0, d0,
                                                              sid, 0, 0, 0, 0, 0, 0, 0);
            text_get_dimensions_int(&w, &h, str);
            int cx = DAT_80611734 - (w >> 1);
            int cy = DAT_80611720 - (h >> 1);
            unsigned char* str2 = (unsigned char*)FUN_8012e878(ctx, d0, d0, d0, d0, d0, d0, d0,
                                                               get_string_by_id(0x15ab),
                                                               (h >> 1), DAT_80611734, (w >> 1),
                                                               0, 0, 0, 0);
            text_draw_char_shadowed((double)cx, (double)cy,
                                    (unsigned long long)(uintptr_t)str2, 0x4069697f, 0x507f);

            // the running value beneath, offset by half its height
            leaf_92c0(1);
            double halfH = (double)FLOAT_80615754 * (double)(float)((double)(longlong)h - d0);
            int vy = (int)(halfH + (double)(float)((double)(longlong)(cy) - d0));
            unsigned char* vstr = (unsigned char*)FUN_8012e878((unsigned long long)(uintptr_t)str,
                                                               d0, (double)FLOAT_80615754, d0, d0, d0, d0, d0,
                                                               (int)&DAT_80610864, DAT_80611fb4,
                                                               0, 0, 0, 0, 0, 0);
            text_get_dimensions_int(&w, &h, vstr);
            int vx = DAT_80611734 - (w >> 1);
            text_draw_char_shadowed((double)vx, (double)vy,
                                    (unsigned long long)(uintptr_t)vstr, 0x4069697f, 0x507f);
            leaf_92c0(0);
        }
    } else {
        bool draw = true;
        if (DAT_80611fb0 < 4) {
            if (DAT_80611fb0 == 1) { draw = true; }        // (handled above)
            else if (DAT_80611fb0 < 1) { draw = false; }
        } else if ((DAT_80611fb0 == 6) || (5 < DAT_80611fb0)) {
            draw = false;
        }
        if (draw && DAT_80611fa0 != 0) {
            // full stat table: title (0x15ab) + four labeled columns
            // (0x1602..0x1605) each with base/bonus/tertiary formatted times.
            text_set_font_table(9);
            int sid = get_string_by_id(0x15ab);
            unsigned char* title = (unsigned char*)FUN_8012e878(ctx, d0, d0, d0, d0, d0, d0, d0,
                                                                sid, 0, 0, 0, 0, 0, 0, 0);
            int tw, th;
            text_get_dimensions_int(&tw, &th, title);
            int titleX = DAT_80611734 - (tw >> 1);
            int titleY = (int)((double)FLOAT_806157c8 * (double)FLOAT_80610830 +
                               (double)(float)((double)(longlong)(int)(DAT_8057ff50 + *(int*)(DAT_80611748 + 0x14)) - (double)0.0));
            unsigned char* title2 = (unsigned char*)FUN_8012e878((unsigned long long)(uintptr_t)title,
                                                                 (double)FLOAT_806157c8, d0, d0, d0, d0, d0, d0,
                                                                 get_string_by_id(0x15ab), (tw >> 1),
                                                                 DAT_8057ff50, tw, 0, 0, 0, 0);
            text_draw_char_shadowed((double)titleX, (double)titleY,
                                    (unsigned long long)(uintptr_t)title2, 0x4069697f, 0x507f);

            // resolve the widest column-label width (strings 0x1602..0x1605)
            text_set_font_table(3);
            int colW = 0;
            for (uint colId = 0x1602; colId <= 0x1605; colId++) {
                unsigned char* lbl = (unsigned char*)get_string_by_id(colId);
                int lw = (int)text_get_char_width(lbl);
                if (lw > colW) colW = lw;
            }
            colW = colW + 0x32;

            int rowY = titleY + th;
            int labelX = DAT_80611734 - (colW >> 1);
            // draw each column: label + the matching running value / time
            struct { uint labelId; int value; } cols[4] = {
                { 0x1602, DAT_80611fb8 },
                { 0x1603, DAT_80611fb4 },
                { 0x1604, DAT_80611fbc },
                { 0x1605, DAT_80611fbc },
            };
            for (int c = 0; c < 4; c++) {
                unsigned char* lbl = (unsigned char*)get_string_by_id(cols[c].labelId);
                unsigned char* lstr = (unsigned char*)FUN_8012e878(ctx, d0, d0, d0, d0, d0, d0, d0,
                                                                   (int)&DAT_806108a0,
                                                                   (int)(uintptr_t)lbl, 0, 0, 0, 0, 0, 0);
                text_draw_char((double)labelX, (double)(rowY + c * colW),
                               lstr, 0x4069697f);
                unsigned long long tv = time_format_long((double)cols[c].value, d0, d0, d0, d0, d0, d0, d0);
                unsigned char* vstr = (unsigned char*)FUN_8012e878(tv, d0, d0, d0, d0, d0, d0, d0,
                                                                   (int)&DAT_80610858,
                                                                   cols[c].value, 0, 0, 0, 0, 0, 0);
                text_draw_char((double)(labelX + colW), (double)(rowY + c * colW),
                               vstr, 0x4069697f);
            }
        }
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8011f7bc | hud_results_render_award_icons
// Draws a centered row of award/medal icons for each player that earned a
// special score. Icons within the "front half" of the count wobble (via the
// sin LUT) using the animated FUN_801708b4 quad; the rest use a static quad.
// ============================================================================
extern "C" void hud_results_render_award_icons(int p1, int p2, int cellW, int cellH) {
    (void)p1; (void)p2;
    unsigned long long rect = FUN_803826b8();
    int centerX = (int)rect;
    int count = 0;
    int off = 0;
    for (int i = 0; i < DAT_80612098; i++) {
        if ((-1 < *(int*)((int)&DAT_805014a0 + off)) &&
            (score_get_special(DAT_80611d90 + *(int*)((int)&DAT_805014a0 + off) * 0xea0) != 0)) {
            count++;
        }
        off += 4;
    }
    if (count != 0) {
        int cap = (FUN_801c1714() == 1) ? 10 : 0x14;
        if (cap <= count) count = cap;
        int cellHalfW = (cellW / (cap >> 1) + -2) >> 1;
        if ((cap >> 1) < count) cellH = cellH >> 1;
        int cellHalfH = cellH >> 1;
        int rowY = cellHalfW + ((int)(rect >> 0x20) - (cellW >> 1)) + 1;

        FUN_8016f00c(1);
        double wobble = (double)(FLOAT_80615868 *
                        (&DAT_80583228)[(int)(FLOAT_806157c0 * FLOAT_80615884 *
                            (float)((double)(longlong)(int)DAT_80611928 - (double)0.0) +
                            FLOAT_80615734) & 0x7ff]);
        FUN_803825c0((double)(float)((double)FLOAT_80615750 * (double)0.0));

        int x = rowY;
        int islot = 0;
        for (int i = 0; (i < DAT_80612098) && (i < count); i++) {
            int award = -1;
            if (-1 < *(int*)((int)&DAT_805014a0 + islot)) {
                award = FUN_802c7788();
            }
            if (award != -1) {
                int tex = sprite_get_texture_id(award);
                int desc = *DAT_80610a20 + tex * 0x58;
                uint tw = (uint)*(unsigned short*)(desc + 0x4e);
                uint th = (uint)*(unsigned short*)(desc + 0x52);
                int colY = centerX;
                if ((cap >> 1) < count) {
                    colY = (centerX - cellHalfH) + -1;
                    if ((cap >> 1) <= i) {
                        colY = (centerX + cellHalfH) - ((countLeadingZeros(DAT_80612244)) >> 5);
                    }
                }
                FUN_8016e77c(tex);
                if (*(int*)((int)&DAT_805014a0 + islot) < DAT_80612610) {
                    FUN_801708b4((double)((float)(x - cellHalfW) - (float)(wobble * (double)FLOAT_8061082c)),
                                 (double)((float)(colY - cellHalfH) - (float)(wobble * (double)FLOAT_80610830)),
                                 (double)((float)(x + cellHalfW) + (float)(wobble * (double)FLOAT_8061082c)),
                                 (double)((float)(colY + cellHalfH) + (float)(wobble * (double)FLOAT_80610830)),
                                 0, 0, tw << 4, th << 4);
                } else {
                    FUN_80170410(x - cellHalfW, colY - cellHalfH, x + cellHalfW, colY + cellHalfH,
                                 0, 0, tw << 4, th << 4);
                }
                int cap2 = (FUN_801c1714() == 1) ? 10 : 0x14;
                if (i + 1 != cap2 >> 1) {
                    x = cellHalfW * 2 + x + 2;   // advance column
                }
            }
            islot += 4;
        }
    }
    FUN_80382704();
    return;
}

// ============================================================================
// 0x8011fb84 | hud_results_render_player_icons
// Same row layout as the award icons but iterating the score-record table
// (DAT_80611d90, 0xea0 stride) and skipping masked-out players (DAT_806120d8).
// Draws each player's portrait icon with the shared wobble animation.
// ============================================================================
extern "C" void hud_results_render_player_icons(int p1, int p2, int cellW, int cellH) {
    (void)p1; (void)p2;
    unsigned long long rect = FUN_803826b8();
    int centerX = (int)rect;
    int total = DAT_80612610 + DAT_806120fc;
    int count = 0;
    int off = 0;
    if (0 < total) {
        int n = total;
        do {
            if ((DAT_806120d8 & 1 << *(int*)(DAT_80611d90 + off + 8)) == 0) {
                count++;
            }
            off += 0xea0;
            n += -1;
        } while (n != 0);
    }
    if (count != 0) {
        int cap = (FUN_801c1714() == 1) ? 10 : 0x14;
        if (cap <= count) count = cap;
        int cellHalfW = (cellW / (cap >> 1) + -2) >> 1;
        if ((cap >> 1) < count) cellH = cellH >> 1;
        int cellHalfH = cellH >> 1;
        int rowY = cellHalfW + ((int)(rect >> 0x20) - (cellW >> 1)) + 1;

        FUN_8016f00c(1);
        double wobble = (double)(FLOAT_80615868 *
                        (&DAT_80583228)[(int)(FLOAT_806157c0 * FLOAT_80615884 *
                            (float)((double)(longlong)(int)DAT_80611928 - (double)0.0) +
                            FLOAT_80615734) & 0x7ff]);
        FUN_803825c0((double)(float)((double)FLOAT_80615750 * (double)0.0));

        int x = rowY;
        int idx = 0;
        int recOff = 0;
        int drawn = 0;
        while ((idx < total) && (drawn < count)) {
            if (((DAT_806120d8 & 1 << *(int*)(DAT_80611d90 + recOff + 8)) == 0) &&
                (FUN_802c7788() != -1)) {
                int award = FUN_802c7788();
                int tex = sprite_get_texture_id(award);
                int desc = *DAT_80610a20 + tex * 0x58;
                uint tw = (uint)*(unsigned short*)(desc + 0x4e);
                uint th = (uint)*(unsigned short*)(desc + 0x52);
                int colY = centerX;
                if ((cap >> 1) < count) {
                    colY = (centerX - cellHalfH) + -1;
                    if ((cap >> 1) <= drawn) {
                        colY = (centerX + cellHalfH) - ((countLeadingZeros(DAT_80612244)) >> 5);
                    }
                }
                FUN_8016e77c(tex);
                if (idx < DAT_80612610) {
                    FUN_801708b4((double)((float)(x - cellHalfW) - (float)(wobble * (double)FLOAT_8061082c)),
                                 (double)((float)(colY - cellHalfH) - (float)(wobble * (double)FLOAT_80610830)),
                                 (double)((float)(x + cellHalfW) + (float)(wobble * (double)FLOAT_8061082c)),
                                 (double)((float)(colY + cellHalfH) + (float)(wobble * (double)FLOAT_80610830)),
                                 0, 0, tw << 4, th << 4);
                } else {
                    FUN_80170410(x - cellHalfW, colY - cellHalfH, x + cellHalfW, colY + cellHalfH,
                                 0, 0, tw << 4, th << 4);
                }
                int cap2 = (FUN_801c1714() == 1) ? 10 : 0x14;
                x = cellHalfW * 2 + x + 2;
                if (drawn + 1 == cap2 >> 1) {
                    x = rowY;
                }
                drawn++;
            }
            idx++;
            recOff += 0xea0;
        }
    }
    FUN_80382704();
    return;
}

// ============================================================================
// 0x8011ff48 | hud_results_render_team_names
// Draws the winning-team / player-name banner. Walks the player slots to see
// whether all share one team (bVar3 stays false) or two teams are present
// (bVar3 = true); then draws either one name (per-player color) or two team
// names (DAT_803f2c90 / DAT_803f2ca4 colors) from the team-name record
// (DAT_80579de8, string ids at +2/+4). Special mode 100 substitutes names.
// ============================================================================
extern "C" void hud_results_render_team_names(void) {
    unsigned long long ctx = FUN_803826e0();
    int rec = DAT_80579de8;
    uint hi = (uint)(ctx >> 0x20);
    uint lo = (uint)ctx;
    int owner = 0;
    bool twoTeams = false;
    double d0 = 0.0;

    int i = FUN_801c1714();
    while (true) {
        i = i + -1;
        if (i < 0) break;
        int slot = DAT_80611d78 + i * 0x141c;
        int t = *(int*)(*(int*)(*(int*)(slot + 0x14) + 0x7c) + 0x84);
        if ((owner != 0) && (owner != t)) {
            twoTeams = true;
            break;
        }
        owner = t;
    }

    if ((rec != 0) && (0 < owner)) {
        if (twoTeams) {
            unsigned char* nameA = (unsigned char*)get_string_by_id((uint)*(unsigned short*)(rec + 2));
            unsigned char* nameB = (unsigned char*)get_string_by_id((uint)*(unsigned short*)(rec + 4));
            int prevFont = text_set_font_table(0x13);
            if (DAT_80612614 == 100) {
                nameA = (unsigned char*)FUN_80350540(d0, d0, d0, d0, d0, d0, d0, d0);
                nameB = (unsigned char*)FUN_80350540(d0, d0, d0, d0, d0, d0, d0, d0);
            }
            text_apply_alpha((double)(FLOAT_8061588c * FLOAT_8061082c), nameA, 0);
            double aw = text_get_char_width(nameA);
            double ah = text_get_char_height(nameA);
            uint col = FUN_803825c0((double)(float)((double)FLOAT_806157f0 * d0));
            double ax = (double)(float)((float)((double)(longlong)(int)hi - (double)0.0) -
                                        (float)((double)FLOAT_8061576c * (double)FLOAT_8061082c + aw));
            double ay = (double)(float)(-(float)((double)FLOAT_80615734 * ah -
                                        (double)(float)((double)(longlong)(int)lo - (double)0.0)));
            text_draw_char(ax, ay, nameA, DAT_803f2c90 & 0xffffff00 | col);

            text_apply_alpha((double)(FLOAT_8061588c * FLOAT_8061082c), nameB, 0);
            double bh = text_get_char_height(nameB);
            double bx = (double)(float)(FLOAT_8061576c * FLOAT_8061082c +
                                        (float)((double)(longlong)(int)hi - (double)0.0));
            double by = (double)(float)(-(float)((double)FLOAT_80615734 * bh -
                                        (double)(float)((double)(longlong)(int)lo - (double)0.0)));
            text_draw_char(bx, by, nameB, DAT_803f2ca4 & 0xffffff00 | col);
            text_set_color_rgb((double)FLOAT_80615718, (double)FLOAT_80615718, (double)FLOAT_80615718);
            text_set_font_table(prevFont);
        } else {
            unsigned short sid = (owner == 1) ? *(unsigned short*)(rec + 2)
                                              : *(unsigned short*)(rec + 4);
            unsigned char* name = (unsigned char*)get_string_by_id((uint)sid);
            int prevFont = text_set_font_table(0x13);
            if (DAT_80612614 == 100) {
                name = (unsigned char*)FUN_80350540(d0, d0, d0, d0, d0, d0, d0, d0);
            }
            text_apply_alpha((double)(FLOAT_8061588c * FLOAT_8061082c), name, 0);
            double nw = text_get_char_width(name);
            double nh = text_get_char_height(name);
            uint col = FUN_803825c0((double)(float)((double)FLOAT_806157f0 * d0));
            double nx = (double)(float)(-(float)((double)FLOAT_80615734 * nw -
                                        (double)(float)((double)(longlong)(int)hi - (double)0.0)));
            double ny = (double)(float)(-(float)((double)FLOAT_80615734 * nh -
                                        (double)(float)((double)(longlong)(int)lo - (double)0.0)));
            text_draw_char(nx, ny, name, col | (&DAT_803f2c7c)[owner * 5] & 0xffffff00);
            text_set_color_rgb((double)FLOAT_80615718, (double)FLOAT_80615718, (double)FLOAT_80615718);
            text_set_font_table(prevFont);
        }
    }
    FUN_8038272c();
    return;
}
