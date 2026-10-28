// Arcade select-screen wind-ripple animation + the on-screen button-prompt bar.
//
// Module: companion to arcade_select_bg_mesh.cpp. The wind functions drive a
//   40-entry ripple table that perturbs the background mesh over time; the
//   prompt-bar renderer lays out and draws the context button labels along the
//   bottom of the screen (with fit-scaling, split-screen positioning, and a
//   selection scissor). Subsystem: graphics.
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns.
//
// Address map (this file):
//   0x802f20a0  arcsel_wind_oscillate     wind oscillation scalar (sine table)
//   0x802f2138  arcsel_wind_seed_ripple   seed one ripple's phase/period (RNG)
//   0x802f21d8  arcsel_wind_tick          advance all 40 ripples, reseed expired
//   0x802f22a0  arcsel_draw_flash_gated   draw selection flash when gated
//   0x802f22e8  arcsel_render_prompt_bar  layout + draw context button prompts

#include "common.h"

// ---- Per-file externs: named cross-module helpers ----
extern int    FUN_803825c0(double v);           // color/alpha byte from float
extern u64    FUN_803826d4(void);               // fetch prompt/context base
extern void   FUN_803826f0(void);               // render scope enter
extern void   FUN_8038273c(void);               // render scope leave
extern void   FUN_80382720(void);               // prompt scope leave
extern int    rng_counter_tick(void);
extern int    session_get_state_flag(void);
extern uint   is_widescreen(void);

extern void   text_set_font_table(int table);
extern void   text_set_color_rgb(double r, double g, double b);
extern double text_get_char_width(byte *str);
extern void   text_prepend_label(byte *dst, int glyph, int label);
extern void   text_draw_char(double x, double y, byte *str, u32 color);

extern void   gx_set_scissor_rect(int x, int y, int w, int h);
extern void   gx_set_sub_material_id(int id);
extern void   gx_set_blend_mode(int mode);

extern void   arcsel_fx_draw_flash(int obj);
extern uint   arcsel_menu_column_count(int slot);
extern int    hud_button_prompt_is_active(int base, int idx);
extern int    hud_button_prompt_update_state(void);

extern int    FUN_802f527c(void);               // selection-flash gate query
extern int    FUN_802f5204(int slot);           // menu layout query

// ---- Raw data symbols ----
extern int    DAT_80611748;      // scissor/layout rect array base (int, +0xc/+0x20 accessed)
extern int    DAT_80611d74;      // per-frame context ptr (+4 slot)
extern int    DAT_806119ec;      // active screen index
extern int    DAT_805cf280;      // screen-object table (main)
extern int    DAT_80612020;      // wind animation frame counter
extern int    DAT_806109c0;      // frame-rate scale
extern int    DAT_806109c4;      // screen width
extern int    DAT_806109c8;      // screen height
extern int    DAT_80612bc8;      // damage-ring / split enable flag
extern int    DAT_80612610;      // demo/attract player count
extern char   DAT_805015bd;      // language/demo mode byte
extern float  DAT_80583228;      // sine lookup table (2K entries)

extern float  FLOAT_806108e8;
extern float  FLOAT_80611a68;
extern float  FLOAT_8061a418;    // 0.0
extern float  FLOAT_8061a41c;    // 1.0
extern float  FLOAT_8061a444;    // 0.5
extern float  FLOAT_8061a448;
extern float  FLOAT_8061a464;
extern float  FLOAT_8061a470;
extern float  FLOAT_8061a480;
extern float  FLOAT_8061a490;
extern float  FLOAT_8061a494;
extern float  FLOAT_8061a4b0;
extern float  FLOAT_8061a4bc;
extern float  FLOAT_8061a4c0;
extern float  FLOAT_8061a4cc;
extern float  FLOAT_8061a4d0;
extern float  FLOAT_8061a4f0;
extern float  FLOAT_8061a4f4;
extern double DOUBLE_8061a430;   // unsigned magic bias
extern double DOUBLE_8061a478;   // signed magic bias

// ----------------------------------------------------------------------------
// 0x802f20a0  arcsel_wind_oscillate
// Returns param_1 scaled by a slow sine oscillation keyed off the global wind
// frame counter (DAT_80612020) and frame-rate scale, sampled from the shared
// sine table. Used to modulate the background warp amplitude.
// ----------------------------------------------------------------------------
double arcsel_wind_oscillate(double param_1) {
    int idx = (int)(FLOAT_8061a470 * FLOAT_8061a494 * FLOAT_8061a448 *
                    (float)((double)CONCAT44(0x43300000, DAT_80612020 ^ 0x80000000) - DOUBLE_8061a430) *
                    (FLOAT_8061a41c /
                     (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061a430)) +
                    FLOAT_8061a444);
    return (double)(float)(param_1 *
                           (double)(FLOAT_8061a4bc *
                                    (FLOAT_8061a41c + (&DAT_80583228)[idx & 0x7ff]) +
                                    FLOAT_8061a444));
}

// ----------------------------------------------------------------------------
// 0x802f2138  arcsel_wind_seed_ripple
// Seeds one ripple entry (index param_2 in the table at param_1): random
// initial amplitude (+0x70), zeroed phase (+0x110), and a randomized period
// (+0x1b0) via two RNG draws.
// ----------------------------------------------------------------------------
void arcsel_wind_seed_ripple(int param_1, int param_2) {
    int rv;
    int slot = param_1 + param_2 * 4;

    rv = rng_counter_tick();
    *(float *)(slot + 0x70) =
        (float)((double)CONCAT44(0x43300000, rv) - DOUBLE_8061a478) / FLOAT_8061a464;
    *(float *)(slot + 0x110) = FLOAT_8061a418;

    rv = rng_counter_tick();
    *(float *)(param_1 + param_2 * 4 + 0x1b0) =
        FLOAT_8061a4f0 *
            ((float)((double)CONCAT44(0x43300000, rv) - DOUBLE_8061a478) / FLOAT_8061a464) +
        FLOAT_8061a4f0;
}

// ----------------------------------------------------------------------------
// 0x802f21d8  arcsel_wind_tick
// Advances all 40 ripples: adds a frame-rate-scaled delta to each phase
// (+0x110) and reseeds any ripple whose phase reaches its period (+0x1b0).
// ----------------------------------------------------------------------------
void arcsel_wind_tick(void) {
    int idx;
    int off;
    int base;
    double delta;

    FUN_803826f0();
    idx = 0;
    off = 0;
    base = (&DAT_805cf280)[DAT_806119ec];
    delta = (double)(FLOAT_806108e8 *
                     (FLOAT_8061a41c /
                      (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061a430)));
    do {
        int slot = base + off;
        *(float *)(slot + 0x110) = (float)((double)*(float *)(slot + 0x110) + delta);
        if (*(float *)(slot + 0x1b0) <= *(float *)(slot + 0x110)) {
            arcsel_wind_seed_ripple(base, idx);
        }
        idx = idx + 1;
        off = off + 4;
    } while (idx < 0x28);
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// 0x802f22a0  arcsel_draw_flash_gated
// Draws the selection flash for the screen object when the gate is open
// (FUN_802f527c==0) or the object has the force-flash flag (0x200).
// ----------------------------------------------------------------------------
void arcsel_draw_flash_gated(int param_1) {
    int gate = FUN_802f527c();
    if ((gate == 0) || ((*(uint *)(param_1 + 0x40) & 0x200) != 0)) {
        arcsel_fx_draw_flash(param_1);
    }
}

// ----------------------------------------------------------------------------
// 0x802f22e8  arcsel_render_prompt_bar
// Collects the active context button prompts (up to 4), measures their total
// width, decides the origin (centered, split-screen positioned, or fit-scaled
// when the row overflows), sets the appropriate scissor, then draws each label
// with its color at its computed x. The two incoming float registers (f30/f31)
// are SDK callee-save spills, not inputs; preserved as locals.
// ----------------------------------------------------------------------------
void arcsel_render_prompt_bar(void) {
    float sel_bump;
    bool  demo_tick;
    double cw;
    int   ctx;
    int   active;
    uint  color;
    int   slot;
    int   obj;
    int   count;
    int   col_off;
    int   lbl_off;
    double total_w;
    double avail;
    uint  origin_x;
    uint  colors[4];
    uint  xs[4];
    uint  x2s[4];
    byte  labels[512];

    ctx = FUN_803826d4();
    DAT_806119ec = *(int *)(DAT_80611d74 + 4);
    double rect_w = (double)(CONCAT44(0x43300000, *(u32 *)(DAT_80611748 + 0xc)) ^ 0x80000000);
    count = 0;
    col_off = 0;
    lbl_off = 0;
    total_w = (double)FLOAT_8061a418;
    obj = (&DAT_805cf280)[DAT_806119ec];
    double scaled_limit = (double)(FLOAT_8061a4f4 * (float)(rect_w - DOUBLE_8061a430));
    text_set_font_table(0x15);

    int idx = 0;
    int stride = 0;
    do {
        int p = ctx + stride;
        if ((*(int *)(p + 0x124) != -1) &&
            (active = hud_button_prompt_is_active(ctx, idx), active != 0)) {
            text_prepend_label(labels + lbl_off, *(int *)(p + 0x128), *(int *)(p + 0x124));
            cw = text_get_char_width(labels + lbl_off);
            total_w = (double)(float)(total_w + cw);
            if (hud_button_prompt_update_state() == 0) {
                color = (uint)FUN_803825c0((double)(FLOAT_8061a480 * *(float *)(ctx + 0xf0)));
                *(uint *)((int)colors + col_off) = (color & 0xff) | 0x28282800;
            } else {
                color = (uint)FUN_803825c0((double)(FLOAT_8061a480 * *(float *)(ctx + 0xf0)));
                *(uint *)((int)colors + col_off) = (color & 0xff) | 0x64646400;
            }
            if (0 < count) {
                total_w = (double)(float)(total_w + (double)FLOAT_8061a490);
            }
            count = count + 1;
            col_off = col_off + 4;
            lbl_off = lbl_off + 0x80;
        }
        idx = idx + 1;
        stride = stride + 0x10;
    } while (idx < 4);

    int layout = FUN_802f5204(*(int *)(DAT_80611d74 + 4));
    if ((layout == 0) || (DAT_80612bc8 == 0)) {
        if ((FUN_802f527c() == 0) || (DAT_80612bc8 == 0)) {
            double w = (double)(CONCAT44(0x43300000, *(u32 *)(DAT_80611748 + 0xc)) ^ 0x80000000);
            origin_x = (uint)(FLOAT_8061a444 * (float)((double)(float)(w - DOUBLE_8061a430) - total_w));
        } else {
            double w = (double)(CONCAT44(0x43300000, DAT_806109c4) ^ 0x80000000);
            origin_x = (uint)(FLOAT_8061a444 * (float)((double)(float)(w - DOUBLE_8061a430) - total_w));
        }
    } else {
        if (scaled_limit < total_w) {
            double n = (double)CONCAT44(0x43300000, (count - 1U) ^ 0x80000000);
            double gap = (double)(FLOAT_8061a490 * (float)(n - DOUBLE_8061a430));
            double s = (double)((float)(scaled_limit - gap) / (float)(total_w - gap));
            text_set_color_rgb(s, (double)FLOAT_8061a41c, (double)FLOAT_8061a41c);
            total_w = (double)(float)(total_w * s);
        }
        double w = (double)(CONCAT44(0x43300000, *(u32 *)(DAT_80611748 + 0xc)) ^ 0x80000000);
        origin_x = (uint)(FLOAT_8061a444 * (float)((double)(float)(w - DOUBLE_8061a430) - total_w));
        int st = session_get_state_flag();
        if (st == 4) {
            if ((*(int *)(DAT_80611d74 + 4) == 1) || (*(int *)(DAT_80611d74 + 4) == 3)) {
                int half = ((int)DAT_806109c4 >> 1) +
                           (int)((uint)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0));
                double hw = (double)(CONCAT44(0x43300000, half) ^ 0x80000000);
                double ox = (double)CONCAT44(0x43300000, origin_x ^ 0x80000000);
                origin_x = (uint)((float)(ox - DOUBLE_8061a430) +
                                  -(FLOAT_8061a4b0 * FLOAT_80611a68 - (float)(hw - DOUBLE_8061a430)));
            }
        } else if (*(int *)(DAT_80611d74 + 4) == 2) {
            int half = ((int)DAT_806109c4 >> 1) +
                       (int)((uint)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0));
            double hw = (double)(CONCAT44(0x43300000, half) ^ 0x80000000);
            double ox = (double)CONCAT44(0x43300000, origin_x ^ 0x80000000);
            origin_x = (uint)((float)(hw - DOUBLE_8061a430) +
                              -(FLOAT_8061a4b0 * FLOAT_80611a68 - (float)(ox - DOUBLE_8061a430)));
        }
    }

    // Compute each label's x position across the row.
    double advance = (double)FLOAT_8061a490;
    col_off = 0;
    lbl_off = 0;
    double xbias = DOUBLE_8061a430;
    for (idx = 0; idx < count; idx = idx + 1) {
        cw = text_get_char_width(labels + lbl_off);
        lbl_off = lbl_off + 0x80;
        double this_w = (double)(CONCAT44(0x43300000, (int)cw) ^ 0x80000000);
        double cur_x = (double)CONCAT44(0x43300000, origin_x ^ 0x80000000);
        double dx = cur_x - xbias;
        *(uint *)((int)xs + col_off) = origin_x;
        *(uint *)((int)x2s + col_off) = 0;
        col_off = col_off + 4;
        origin_x = (uint)((float)dx + (float)(advance + (double)(float)(this_w - xbias)));
    }

    // Scissor: full screen, or the split half depending on layout.
    if ((FUN_802f527c() == 0) || (DAT_80612bc8 == 0)) {
        gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
    } else {
        uint cols = arcsel_menu_column_count(*(int *)(DAT_80611d74 + 4));
        int halfh = ((int)DAT_806109c8 >> 1) +
                    (int)((uint)((int)DAT_806109c8 < 0 && (DAT_806109c8 & 1) != 0));
        if (cols == 0) {
            gx_set_scissor_rect(0, halfh, DAT_806109c4, DAT_806109c8);
        } else {
            gx_set_scissor_rect(0, 0, DAT_806109c4, halfh);
        }
    }

    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);

    // Draw each label at its computed x, with a small selection bump in demo.
    double sel_scale = (double)FLOAT_8061a4cc;
    lbl_off = 0;
    col_off = 0;
    double ybias = DOUBLE_8061a430;
    for (idx = 0; idx < count; idx = idx + 1) {
        demo_tick = false;
        if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
            demo_tick = true;
        }
        sel_bump = FLOAT_8061a4c0;
        if (demo_tick) {
            sel_bump = FLOAT_8061a4d0;
        }
        uint row_y = *(uint *)(DAT_80611748 + 0x20) ^ 0x80000000;
        double x2 = (double)CONCAT44(0x43300000, *(uint *)((int)x2s + col_off) ^ 0x80000000);
        double ry = (double)CONCAT44(0x43300000, row_y);
        double x0 = (double)CONCAT44(0x43300000, *(uint *)((int)xs + col_off) ^ 0x80000000);
        uint y_pos = (uint)((float)(ry - ybias) *
                            (float)(sel_scale * (double)(float)(x2 - ybias)) +
                            (float)(ry - ybias) * sel_bump + *(float *)(obj + 600));
        (void)y_pos;
        uint y_swapped = y_pos ^ 0x80000000;
        text_draw_char((double)(float)(x0 - ybias),
                       (double)(float)((double)CONCAT44(0x43300000, y_swapped) - ybias),
                       labels + lbl_off, *(u32 *)((int)colors + col_off));
        lbl_off = lbl_off + 0x80;
        col_off = col_off + 4;
    }

    double white = (double)FLOAT_8061a41c;
    text_set_color_rgb(white, white, white);
    FUN_80382720();
    (void)slot;
}
