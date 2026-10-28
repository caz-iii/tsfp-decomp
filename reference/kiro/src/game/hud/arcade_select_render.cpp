// Arcade select-screen render helpers: separator bars, label text (with
// fit-scaling and marquee scroll), the notification/award banner, and the
// scrolling row-highlight text.
//
// Batch: batch_833
// Module: rendering helpers for the arcade select screen (companion to
//   arcade_select_screen.cpp / _intro_fx.cpp). Called from the screen render
//   dispatchers (0x802e6ec4 family) and hud_fullscreen_fade.
//   Uses the higher-level gx_draw_sprite_quad* / text_draw_char helpers
//   rather than raw FIFO writes. Subsystem: graphics.
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns.

#include "common.h"

// ---- Per-file externs (raw symbols) ----
extern void  text_set_font_table(int table);
extern void  text_set_color_rgb(double r, double g, double b);
extern int   text_set_wrap_mode(int mode);
extern double text_calc_fit_scale(double w, byte *str, int a);
extern double text_count_lines(double w, char *str);
extern double text_measure_word_wrap(void);
extern byte *text_check_scale(double w, byte *str);
extern double text_get_char_height(byte *str);
extern double text_get_char_width(byte *str);
extern void  text_draw_char(double x, double y, byte *str, u32 color);
extern void  gx_set_sub_material_id(int id);
extern void  gx_set_blend_mode(int mode);
extern void  gx_set_active_material(int tex);
extern void  gx_set_scissor_rect(int x, int y, int w, int h);
extern void  gx_draw_fill_rect_f(double x0, double y0, double x1, double y1);
extern void  gx_fill_rect_clipped(int x0, int y0, int x1, int y1, int a);
extern void  gx_draw_sprite_quad(double x0, double y0, double x1, double y1, int a, int b, uint u, uint v);
extern void  gx_draw_sprite_quad_c4(double x0, double y0, double x1, double y1, int a, int b, uint u, uint v);
extern int   sprite_get_texture_id(int slot);
extern u32   color_scale_lo(double factor, u32 color);
extern void  hud_element_set_pos_y(double y, int a);
extern int   hud_screen_find_root(int handle);
extern u32   award_notify_format_string(double a, double b, double c, double d, double e,
                                        u64 f6, u64 f7, u64 f8);
extern int   FUN_803825c0(double v);       // color/alpha byte from float
extern u64   FUN_803826e4(void);
extern u64   FUN_803826e8(void);
extern u64   FUN_803826d4(void);
extern void  FUN_8038b4cc(int dst, int src, int len);
extern int   FUN_8038b378(uint *a, uint *b);
extern void  FUN_80382720(void);
extern void  FUN_80382730(void);
extern void  FUN_80382734(void);
extern void  FUN_80366428(void);
extern u64   __save_gpr_25(void);
extern void  __restore_gpr_25(void);

// ---- Raw data symbols ----
extern int  *DAT_80610a20;      // sprite/texture descriptor table
extern int  *DAT_80611748;      // scissor/layout rect array
extern int   DAT_80612b64;      // arcsel owner object ptr
extern int   DAT_806109c0;      // frame-rate scale
extern int   DAT_806109c4;      // screen width
extern int   DAT_806109c8;      // screen height
extern int   DAT_806119e0;
extern int   DAT_806119ec;      // active viewport/player index
extern int   DAT_8060f948;      // controller/layout variant
extern int   DAT_8051a820;      // per-player screen handle table (stride 0x1e0)

extern float FLOAT_806108e8;
extern float FLOAT_80611674;
extern float FLOAT_80611a64;
extern float FLOAT_80611a68;
extern float FLOAT_80611a6c;
extern float FLOAT_80611a70;
extern float FLOAT_8061a180;
extern float FLOAT_8061a184;
extern float FLOAT_8061a1a4;
extern float FLOAT_8061a1a8;
extern float FLOAT_8061a1ac;
extern float FLOAT_8061a1cc;
extern float FLOAT_8061a1d0;
extern float FLOAT_8061a1d4;
extern float FLOAT_8061a1d8;
extern float FLOAT_8061a1dc;
extern float FLOAT_8061a1e0;
extern float FLOAT_8061a1e4;
extern float FLOAT_8061a1e8;
extern float FLOAT_8061a1ec;
extern float FLOAT_8061a1f0;
extern float FLOAT_8061a1f4;
extern float FLOAT_8061a200;
extern float FLOAT_8061a204;
extern float FLOAT_8061a208;
extern float FLOAT_8061a20c;
extern float FLOAT_8061a210;
extern float FLOAT_8061a214;
extern float FLOAT_8061a218;
extern float FLOAT_8061a21c;
extern double DOUBLE_8061a190;
extern double DOUBLE_8061a1f8;

static inline float i2f(int x) { return (float)x; }

// ---------------------------------------------------------------------------
// arcsel_row_highlight_alpha (0x802e48b8)
//   Returns the highlight alpha for a row: dim for index>1, bright otherwise.
// ---------------------------------------------------------------------------
double arcsel_row_highlight_alpha(int index)
{
    if (1 < index) {
        return (double)FLOAT_8061a1cc;
    }
    return (double)FLOAT_8061a1d0;
}

// ---------------------------------------------------------------------------
// arcsel_draw_separator (0x802e4560)
//   Draws a two-material horizontal separator bar (glow texture 1 + base
//   texture 0) at the given y position.
// ---------------------------------------------------------------------------
void arcsel_draw_separator(u64 param_1, double y, double x1, double param_4, double param_5)
{
    (void)param_1;
    ushort u1, v1;
    int tex, desc;
    uint tint;
    double half;

    tint = 0x4b697f32;
    if (FUN_803826e4() != 0) {
        tint = 0x325a7f7f;
    }
    color_scale_lo(param_4, tint);
    color_scale_lo(param_5, tint);
    color_scale_lo(param_4, 0xa2d5632);
    color_scale_lo(param_5, 0xa2d5632);
    half = (double)(FLOAT_8061a1a8 * FLOAT_80611a64);

    gx_set_sub_material_id(2);
    tex = sprite_get_texture_id(1);
    desc = *DAT_80610a20 + tex * 0x58;
    u1 = *(ushort *)(desc + 0x4e);
    v1 = *(ushort *)(desc + 0x52);
    gx_set_active_material(tex);
    gx_draw_sprite_quad_c4(0, (double)(float)(y - half), x1, (double)(float)(y + half),
                           0, 0, (uint)u1 << 4, (uint)v1 << 4);

    gx_set_sub_material_id(1);
    tex = sprite_get_texture_id(0);
    desc = *DAT_80610a20 + tex * 0x58;
    u1 = *(ushort *)(desc + 0x4e);
    v1 = *(ushort *)(desc + 0x52);
    gx_set_active_material(tex);
    gx_draw_sprite_quad_c4(0, y, x1, (double)(float)(y + (double)FLOAT_8061a1a8),
                           0, 0, (uint)u1 << 4, (uint)v1 << 4);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// arcsel_draw_row_bar (0x802e4760)
//   Draws a highlighted row background bar (two halves) from a float[4] rect.
// ---------------------------------------------------------------------------
void arcsel_draw_row_bar(u32 param_1, u32 param_2, float *rect)
{
    (void)param_1; (void)param_2;
    uint u1, v1;
    int tex, desc;
    double mid, mid2;
    int ctx;

    ctx = __save_gpr_25();
    mid  = (double)(FLOAT_8061a1ac * (rect[2] + rect[0]));
    mid2 = (double)(FLOAT_8061a1ac * (rect[3] + rect[1]));
    FUN_803825c0((double)(FLOAT_8061a1a4 * *(float *)(ctx + 0xf0)));
    gx_set_sub_material_id(1);
    tex = sprite_get_texture_id(0);
    desc = *DAT_80610a20 + tex * 0x58;
    u1 = (uint)*(ushort *)(desc + 0x4e) << 4;
    v1 = (uint)*(ushort *)(desc + 0x52) << 4;
    gx_set_active_material(tex);
    gx_draw_sprite_quad_c4((double)rect[0], mid2, mid,
                           (double)(float)(mid2 + (double)FLOAT_8061a1a8), 0, 0, u1, v1);
    gx_draw_sprite_quad_c4(mid, mid2, (double)rect[2],
                           (double)(float)(mid2 + (double)FLOAT_8061a1a8), 0, 0, u1, v1);
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// arcsel_draw_label (0x802e48d0)
//   Draws a label string fit into the available width, shrinking the font and
//   word-wrapping / switching font table as needed, then draws it.
// ---------------------------------------------------------------------------
void arcsel_draw_label(double param_1, double param_2, int strId, uint color)
{
    byte buf[512];
    long long lineCount;
    double margin;
    double avail, scale, m2;
    byte *fitted;
    int prevWrap;
    double h;

    if (strId != 0) {
        margin = (double)(FLOAT_8061a1d4 * FLOAT_80611a68 + FLOAT_80611a70);
        text_set_font_table(9);
        text_set_color_rgb((double)FLOAT_8061a184, (double)FLOAT_8061a184, (double)FLOAT_8061a184);
        FUN_8038b4cc((int)buf, strId, 0x1ff);

        if ((double)FLOAT_8061a180 < param_2) {
            avail = (double)(float)(param_2 - margin);
            prevWrap = text_set_wrap_mode(0);
            scale = text_calc_fit_scale(avail, buf, 0);
            if ((double)FLOAT_8061a1d8 <= scale) {
                text_set_color_rgb(scale, scale, (double)FLOAT_8061a184);
            } else {
                lineCount = (long long)(int)text_count_lines(avail, (char *)buf);
                (void)lineCount;
                double measured = (double)text_measure_word_wrap();
                m2 = (double)FLOAT_8061a1d8;
                if (m2 <= measured) {
                    text_set_color_rgb(measured, measured, (double)FLOAT_8061a184);
                } else {
                    text_set_color_rgb(m2, m2, (double)FLOAT_8061a184);
                }
                fitted = text_check_scale(avail, buf);
                if (fitted != buf) {
                    FUN_8038b4cc((int)buf, (int)fitted, 0x1ff);
                }
            }
            if ((double)FLOAT_8061a1a8 < text_count_lines(avail, (char *)buf)) {
                text_set_color_rgb((double)FLOAT_8061a184, (double)FLOAT_8061a184, (double)FLOAT_8061a184);
                text_set_font_table(2);
            }
            text_set_wrap_mode(prevWrap);
        }

        h = text_get_char_height(buf);
        h = (double)(float)(-(double)(float)((double)FLOAT_8061a1dc * (double)FLOAT_80611a64 - param_1) - h);
        gx_set_sub_material_id(1);
        gx_set_blend_mode(0);
        text_draw_char(margin, h, buf, (color & 0xff) | 0x64646400);
        text_set_color_rgb((double)FLOAT_8061a184, (double)FLOAT_8061a184, (double)FLOAT_8061a184);
    }
}

// ---------------------------------------------------------------------------
// arcsel_clamp_element_y (0x802e4ac4)
//   Clamps an element's y position (+0x60) to a computed minimum.
// ---------------------------------------------------------------------------
void arcsel_clamp_element_y(int elem)
{
    double minY = (double)(FLOAT_8061a1e0 +
                  -(FLOAT_8061a1e4 * (FLOAT_8061a184 - *(float *)(DAT_80612b64 + 0x644)) - FLOAT_8061a1dc));
    if ((double)*(float *)(elem + 0x60) < minY) {
        hud_element_set_pos_y(minY, DAT_806119e0);
    }
}

// ---------------------------------------------------------------------------
// arcsel_draw_header (0x802e4b18)
//   Draws the screen header: background fill, two separator bars, the header
//   icon sprite (texture 7 or 8), and the title label.
// ---------------------------------------------------------------------------
void arcsel_draw_header(void)
{
    ushort u1, v1;
    int tex, desc;
    uint color;
    u64 ctx;
    double t, fade, barY, iconW;
    double a, b, c, d, e, f;

    ctx = FUN_803826e8();
    tex = (DAT_8060f948 == 4) ? 8 : 7;
    tex = sprite_get_texture_id(tex);
    t    = (double)*(float *)(DAT_80612b64 + 0x644);
    fade = (double)(FLOAT_8061a184 - *(float *)(DAT_80612b64 + 0xa84));
    desc = *DAT_80610a20 + tex * 0x58;
    u1 = *(ushort *)(desc + 0x4e);
    iconW = (double)(float)((double)(float)((double)FLOAT_8061a1e8 * t) * fade);
    v1 = *(ushort *)(desc + 0x52);
    barY = (double)(*(float *)(DAT_80612b64 + 0x648) * FLOAT_80611a64 + FLOAT_80611a6c);

    gx_set_scissor_rect(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    gx_set_sub_material_id(1);
    FUN_803825c0((double)(float)((double)FLOAT_8061a1a4 * t));
    gx_draw_fill_rect_f((double)FLOAT_8061a180, (double)FLOAT_8061a180, i2f(DAT_806109c4), barY);

    double d68 = (double)FLOAT_80611a68;
    a = (double)(float)((double)FLOAT_8061a1ec * d68 + (double)FLOAT_80611a70);
    b = -(double)(float)((double)FLOAT_8061a1f0 * d68 - a);
    c = (double)(float)((double)FLOAT_8061a1a8 * d68 + b);
    d = (double)FLOAT_80611a64;
    e = -(double)(float)((double)FLOAT_8061a1f4 * d68 - c);
    f = (double)(float)(-(double)(FLOAT_8061a1e4 * (FLOAT_8061a184 - *(float *)(DAT_80612b64 + 0x644)) - FLOAT_8061a1dc) * d + (double)FLOAT_80611a6c);

    arcsel_draw_separator((double)FLOAT_8061a180, barY, e, (double)FLOAT_8061a180, iconW);
    arcsel_draw_separator(e, barY, c, iconW, (double)FLOAT_8061a180);

    gx_set_sub_material_id(1);
    gx_set_active_material(tex);
    FUN_803825c0((double)(FLOAT_8061a1a4 * (float)(t * fade)));
    gx_draw_sprite_quad(b, f, a, (double)(float)((double)FLOAT_8061a1e0 * d + f), 0, 0, (uint)u1 << 4, (uint)v1 << 4);

    color = FUN_803825c0((double)((float)(i2f((int)ctx) - DOUBLE_8061a1f8) * *(float *)(DAT_80612b64 + 0x644)));
    arcsel_draw_label(barY, e, (int)((u64)ctx >> 0x20), color);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// arcsel_marquee_reset (0x802e4ec4)
//   Resets a marquee-scroll state struct (x=y=0, flag=0).
// ---------------------------------------------------------------------------
void arcsel_marquee_reset(u32 *state)
{
    float z = FLOAT_8061a180;
    *(unsigned char *)(state + 3) = 0;
    state[0] = (u32)z;
    state[1] = (u32)z;
}

// ---------------------------------------------------------------------------
// arcsel_draw_row_divider (0x802e4dd0)
//   Draws a clipped fill rect plus two separators forming a row divider at y.
// ---------------------------------------------------------------------------
void arcsel_draw_row_divider(double y)
{
    gx_fill_rect_clipped(DAT_80611748[0], (int)y, DAT_80611748[1], DAT_80611748[6], 0x7f);
    arcsel_draw_separator(i2f(DAT_80611748[0]), y, i2f(DAT_80611748[2]),
                          (double)FLOAT_8061a180, (double)FLOAT_8061a184);
    arcsel_draw_separator(i2f(DAT_80611748[2]), y, i2f(DAT_80611748[1]),
                          (double)FLOAT_8061a184, (double)FLOAT_8061a180);
}

// ---------------------------------------------------------------------------
// arcsel_draw_award_banner (0x802e4edc)
//   Animates and draws the award/notification banner: fades in the panel,
//   the separators, and the notification text pulled from the focused item.
// ---------------------------------------------------------------------------
void arcsel_draw_award_banner(void)
{
    float fVar1, fVar2;
    int root, item;
    uint color;
    uint *text;
    double d8, d9, d7, d11, d14, d15, d13, d10, d12;

    d8 = (double)FLOAT_80611a68;
    d9 = (double)FLOAT_80611a70;
    d7 = (double)FLOAT_8061a1a8;
    d11 = (double)FLOAT_80611a64;
    d14 = (double)(float)(d7 * d8 + -(double)(float)((double)FLOAT_8061a1f0 * d8 -
              (double)(float)((double)FLOAT_8061a1ec * d8 + d9)));
    d15 = (double)(float)((double)*(float *)(DAT_80612b64 + 0x648) * d11);
    d13 = -(double)(float)((double)FLOAT_8061a1f4 * d8 - d14);
    d10 = (double)(float)(d14 + (double)(float)((double)FLOAT_8061a200 * d8 + d9));
    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    text = (uint *)0;
    text_set_font_table(0x12);
    d12 = (double)(float)((double)FLOAT_8061a1a8 * (double)(float)((double)FLOAT_8061a1ac * d11) +
              (double)(float)(d15 + (double)FLOAT_80611674));

    if ((root != 0) && (item = *(int *)(root + 0x194), item != 0)) {
        if ((*(uint *)(item + 0x2c) & 1) == 0) {
            if ((*(uint *)(item + 0x28) & 0x800000) == 0) {
                text = *(uint **)(item + 0x24);
            }
        } else {
            text = (uint *)award_notify_format_string((double)(float)((double)FLOAT_8061a1ac * d11),
                        (double)FLOAT_8061a1a8, d7, d8, d9, 0, 0, 0);
        }
    }

    if ((text == (uint *)0) ||
        (item = FUN_8038b378(text, (uint *)(DAT_80612b64 + 0x684)), item != 0)) {
        fVar1 = FLOAT_8061a180;
        *(float *)(DAT_80612b64 + 0x67c) =
            -(FLOAT_8061a1dc * FLOAT_806108e8 * (FLOAT_8061a184 / i2f(DAT_806109c0)) -
              *(float *)(DAT_80612b64 + 0x67c));
        if (*(float *)(DAT_80612b64 + 0x67c) <= fVar1) {
            *(float *)(DAT_80612b64 + 0x67c) = fVar1;
            if (text == (uint *)0) {
                *(char *)(DAT_80612b64 + 0x684) = 0;
            } else {
                FUN_8038b4cc(DAT_80612b64 + 0x684, (int)text, 0x1ff);
                *(char *)(DAT_80612b64 + 0x883) = 0;
                if ((*(uint *)(*(int *)(root + 0x194) + 0x28) & 0x800000) == 0) {
                    *(float *)(DAT_80612b64 + 0x678) = (float)d10;
                } else {
                    *(float *)(DAT_80612b64 + 0x678) =
                        (float)(d10 + (double)(FLOAT_8061a204 * FLOAT_80611a68 + FLOAT_80611a70));
                }
            }
        }
    } else if ((double)*(float *)(DAT_80612b64 + 0x678) <= d10) {
        *(float *)(DAT_80612b64 + 0x67c) =
            FLOAT_806108e8 * (FLOAT_8061a184 / i2f(DAT_806109c0)) + *(float *)(DAT_80612b64 + 0x67c);
        if (FLOAT_8061a184 < *(float *)(DAT_80612b64 + 0x67c)) {
            *(float *)(DAT_80612b64 + 0x67c) = FLOAT_8061a184;
        }
    }

    fVar2 = FLOAT_8061a184;
    fVar1 = FLOAT_8061a180;
    if ((text == (uint *)0) || (d10 <= (double)*(float *)(DAT_80612b64 + 0x678))) {
        *(float *)(DAT_80612b64 + 0x680) =
            -(FLOAT_806108e8 * (FLOAT_8061a184 / i2f(DAT_806109c0)) - *(float *)(DAT_80612b64 + 0x680));
        if (*(float *)(DAT_80612b64 + 0x680) < fVar1) {
            *(float *)(DAT_80612b64 + 0x680) = fVar1;
        }
    } else {
        *(float *)(DAT_80612b64 + 0x680) =
            FLOAT_806108e8 * (FLOAT_8061a184 / i2f(DAT_806109c0)) + *(float *)(DAT_80612b64 + 0x680);
        if (fVar2 < *(float *)(DAT_80612b64 + 0x680)) {
            *(float *)(DAT_80612b64 + 0x680) = fVar2;
        }
    }

    if ((double)FLOAT_8061a180 < (double)*(float *)(DAT_80612b64 + 0x680)) {
        arcsel_draw_separator((double)FLOAT_8061a180, d12, d13, (double)FLOAT_8061a180,
            (double)(float)((double)(FLOAT_8061a1e8 * *(float *)(DAT_80612b64 + 0x644)) *
                            (double)*(float *)(DAT_80612b64 + 0x680)));
        arcsel_draw_separator(d13, d12, d14,
            (double)(FLOAT_8061a1e8 * *(float *)(DAT_80612b64 + 0x644) * *(float *)(DAT_80612b64 + 0x680)),
            (double)FLOAT_8061a180);
    }

    fVar1 = FLOAT_8061a180;
    if (*(char *)(DAT_80612b64 + 0x684) == '\0') {
        *(float *)(DAT_80612b64 + 0x680) =
            -(FLOAT_806108e8 * (FLOAT_8061a184 / i2f(DAT_806109c0)) - *(float *)(DAT_80612b64 + 0x680));
        if (*(float *)(DAT_80612b64 + 0x680) < fVar1) {
            *(float *)(DAT_80612b64 + 0x680) = fVar1;
        }
    } else {
        double cw = text_get_char_width((byte *)(DAT_80612b64 + 0x684));
        double ch = text_get_char_height((byte *)(DAT_80612b64 + 0x684));
        double ty = (double)(float)((double)FLOAT_8061a1ac *
                        (double)(float)((double)(float)(d12 - d15) - ch) + d15);
        gx_set_scissor_rect((int)FLOAT_8061a180, (int)d15, (int)d14, (int)d12);
        *(float *)(DAT_80612b64 + 0x678) =
            -(FLOAT_8061a1dc * FLOAT_806108e8 - *(float *)(DAT_80612b64 + 0x678));
        if (*(float *)(DAT_80612b64 + 0x678) < (float)((double)FLOAT_8061a180 - cw)) {
            if ((*(int *)(root + 0x194) != 0) &&
                ((*(uint *)(*(int *)(root + 0x194) + 0x28) & 0x800000) != 0)) {
                FUN_80366428();
            }
            *(float *)(DAT_80612b64 + 0x678) = (float)d10;
        }
        color = color_scale_lo((double)(*(float *)(DAT_80612b64 + 0x67c) * *(float *)(DAT_80612b64 + 0x644)), 0x64646464);
        text_draw_char((double)*(float *)(DAT_80612b64 + 0x678), ty, (byte *)(DAT_80612b64 + 0x684), color);
    }
}

// ---------------------------------------------------------------------------
// arcsel_draw_row_text (0x802e53ec)
//   Draws a horizontal run of `count` glyph cells (0x80 bytes each) with a
//   fade based on scroll position, computing per-cell x offsets from widths.
// ---------------------------------------------------------------------------
void arcsel_draw_row_text(u64 param_1, double param_2, u32 param_3, u32 param_4, int count)
{
    (void)param_1; (void)param_3; (void)param_4;
    uint xPos[4];
    uint yPad[4];
    int   glyphs;
    int   idx, off;
    int   i;
    uint  cursor;
    double scale, gap, fade, base;
    u64  ctx;

    ctx = FUN_803826d4();
    glyphs = (int)((u64)ctx >> 0x20);
    scale = (double)FLOAT_8061a184;
    fade = (double)(FLOAT_8061a208 * i2f(DAT_80611748[3]));

    double f1 = param_2;   // scroll offset arg (comes in as f1 in orig)
    if (fade < f1) {
        double lo = i2f((count - 1) * 0x14);
        scale = (double)((float)(fade - lo) / (float)(f1 - lo));
        text_set_color_rgb(scale, (double)FLOAT_8061a184, (double)FLOAT_8061a184);
        f1 = (double)(float)(f1 * scale);
    }

    cursor = (uint)(FLOAT_8061a1ac * (float)(i2f(DAT_80611748[3]) - f1));
    text_set_font_table(0x15);
    gap = (double)(float)((double)FLOAT_8061a20c * scale);

    off = 0;
    idx = 0;
    for (i = 0; i < count; i = i + 1) {
        double w = text_get_char_width((byte *)(glyphs + off));
        off = off + 0x80;
        xPos[idx / 4] = cursor;   // record cell x
        yPad[idx / 4] = 0;
        idx = idx + 4;
        cursor = (uint)((float)i2f((int)cursor) + (float)((float)w + gap));
        (void)w;
    }

    *(float *)(DAT_80612b64 + 0x64c) = FLOAT_8061a210;
    if ((double)FLOAT_8061a180 == param_2) {
        double amax = arcsel_row_highlight_alpha(1);
        if (((double)*(float *)(DAT_80612b64 + 0x650) < amax) &&
            (*(float *)(DAT_80612b64 + 0x650) =
                 (float)((double)*(float *)(DAT_80612b64 + 0x650) + (double)FLOAT_8061a214),
             amax < (double)*(float *)(DAT_80612b64 + 0x650))) {
            *(float *)(DAT_80612b64 + 0x650) = (float)amax;
        }
        base = i2f(DAT_80611748[8]);
        cursor = (uint)((FLOAT_8061a184 - *(float *)(DAT_80612b64 + 0x650)) * (float)base);
    } else {
        base = i2f(DAT_80611748[8]);
        cursor = (uint)((float)((double)FLOAT_8061a184 - param_2) * (float)base);
    }

    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    gx_set_scissor_rect(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    arcsel_draw_row_divider(i2f((int)cursor));

    double dy = (double)FLOAT_8061a218;
    double dx = (double)FLOAT_8061a21c;
    off = 0;
    idx = 0;
    for (i = 0; i < count; i = i + 1) {
        uint yc = (uint)((float)i2f(DAT_80611748[8]) *
                         (float)(dx * i2f((int)yPad[idx / 4])) +
                         (float)(dy * i2f(DAT_80611748[8]) + i2f((int)cursor)));
        text_draw_char(i2f((int)xPos[idx / 4]), i2f((int)yc),
                       (byte *)(glyphs + off), *(u32 *)((int)ctx + idx));
        off = off + 0x80;
        idx = idx + 4;
    }
    text_set_color_rgb((double)FLOAT_8061a184, (double)FLOAT_8061a184, (double)FLOAT_8061a184);
    FUN_80382720();
}
