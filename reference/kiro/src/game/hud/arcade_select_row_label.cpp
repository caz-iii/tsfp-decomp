// Arcade select-screen row label rendering + score/profile helpers.
//
// Module: companion to arcade_select_lifecycle.cpp. arcsel_render_row_label is
//   the per-row draw callback registered by arcsel_style_define - it resolves
//   the row's status string (locked/unlocked/complete/time) and its award or
//   score text, then draws them with the appropriate shadow/color at the row's
//   layout rect. The remaining helpers format a score/time value string and
//   count/prefetch player profiles. Subsystem: graphics (HUD).
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns.
//
// Address map (this file):
//   0x802f3a8c  arcsel_render_row_label      draw a row's status + value text
//   0x802f41ec  arcsel_prefetch_profiles     touch all players' profile slots
//   0x802f42dc  arcsel_format_score          format score/time string per mode
//   0x802f4494  arcsel_count_profiles_for_mode  count profiles for a gamemode

#include "common.h"

// ---- Per-file externs: helpers ----
extern u64  FUN_803826c0(void);                    // fetch row context (packed pair)
extern u64  FUN_803826e8(void);                    // prefetch scope enter
extern void FUN_80382734(void);                    // prefetch scope leave
extern void FUN_8038270c(void);                    // row-label scope leave
extern int  FUN_802f5204(int slot);                // layout query (single-column?)
extern int  FUN_80387fb8(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h,
                         int dst, void *fmt, int v0, int v1, int v2, int v3, uint v4, u32 v5);
extern void FUN_8038b4a0(int dst, int suffix);

extern int    get_string_by_id(int id);
extern double text_get_char_width(byte *str);
extern double text_get_char_height(byte *str);
extern byte  *text_check_scale(double w, void *str);
extern void   text_set_font_table(int table);
extern void   text_draw_char_shadowed(double x, double y, byte *str, uint color, uint shadow);
extern void   text_draw_string_shadowed(double x, double y, double a, double b,
                                        double c, double d, double e);
extern void   text_draw_string_colored(double x, double y, double a, double b,
                                        double c, double d, double e);
extern void   gx_set_sub_material_id(int id);
extern void   time_format_short(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h,
                                int dst, int value);
extern u64    award_resolve_string(double a, double b, double c, double d,
                                    u64 e, u64 f, u64 g, u64 h, int ctx);
extern u64    statlvl_object_get_ids(int ctx, u32 *out_a, u32 *out_b);
extern byte  *debug_log_formatted(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h,
                                  void *fmt, u32 v0, u32 v1, uint v2, uint v3, uint v4,
                                  uint v5, int v6);
extern int    session_get_state_flag(void);
extern void   profile_get_slot(int slot);

// ---- Raw data symbols ----
extern int   DAT_80611d74;      // per-frame context ptr (+0x14 slot)
extern int   DAT_80611d90;      // profile record base
extern int   DAT_805014a0;      // per-player profile-slot index table
extern int   DAT_806110dc;      // default row-label string
extern int   DAT_806110e0;      // stat "n/m" format string
extern int   DAT_806110e8;      // "x N" count format string
extern int   DAT_806110ec;      // signed score format string
extern int   DAT_80611934;      // session sub-mode
extern int   DAT_80612244;      // widescreen/layout variant flag
extern int   DAT_80612610;      // demo/attract player count
extern int   DAT_806120fc;      // extra (AI) player count
extern int   DAT_806120d8;      // per-player time-valid mask
extern char  DAT_805015bd;      // language/demo mode byte
extern char  DAT_805015be;      // sub-mode byte
extern int   DAT_805015dc;      // score-mode flag

extern float FLOAT_8061a418;    // 0.0
extern float FLOAT_8061a444;    // 0.5
extern float FLOAT_8061a494;
extern float FLOAT_80611674;
extern float FLOAT_80611a68;
extern double DOUBLE_8061a430;  // unsigned magic bias

// ----------------------------------------------------------------------------
// 0x802f3a8c  arcsel_render_row_label
// Draws one select-row's label: resolves the status string keyed off the row
// type byte (ctx[0xb]) - locked / two-part / complete / time / etc. - along
// with the appropriate color+shadow, then the award/score string, and finally
// draws both (shadowed strings when a sub-context is present, plain shadowed
// glyphs otherwise) at the row rect (param_11..14). The f29..f31 incoming
// float registers are callee-save spills preserved as locals.
// ----------------------------------------------------------------------------
void arcsel_render_row_label(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                             u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                             u32 param_9, u32 param_10, uint param_11, uint param_12,
                             uint param_13, uint param_14, uint param_15, int param_16) {
    byte  type;
    int   ctx;
    int   sub;
    int   single_col;
    int   font;
    byte *status_str;
    byte *tmp;
    uint  color;
    uint  shadow;
    double cw;
    double ch;
    double x;
    double bias = DOUBLE_8061a430;
    u32   ids_a;
    u32   ids_b;
    byte  name_buf[104];
    u64   packed;

    packed = FUN_803826c0();
    ctx = (int)((ulonglong)packed >> 0x20);
    sub = (int)packed;
    color = param_15 & 0xff;
    shadow = 0;
    uint base_col = color;
    status_str = (byte *)&DAT_806110dc;

    single_col = (sub == 0) ? 0 : FUN_802f5204(*(int *)(sub + 0x38));
    if (param_16 != 0) {
        gx_set_sub_material_id(1);
    }
    font = (session_get_state_flag() < 3) ? 3 : 0x12;
    text_set_font_table(font);

    type = *(byte *)(ctx + 0xb);
    if (type == 4) {
        status_str = (byte *)(uintptr_t)get_string_by_id(0x17c9);
        color = base_col | 0x64000000;
        shadow = base_col;
        if (DAT_805015bd == '\x03') {
            time_format_short(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              (int)name_buf, *(int *)(ctx + 0x34));
            status_str = name_buf;
        }
    } else if (type < 4) {
        if (1 < type) {
            if (sub == 0) {
                if (type == 2) {
                    tmp = (byte *)(uintptr_t)get_string_by_id(0x17c6);
                    status_str = (byte *)(uintptr_t)get_string_by_id(0x17c7);
                    cw = text_get_char_width(status_str);
                    color = base_col | 0x64640000;
                    uint x0 = (uint)-(float)((double)FLOAT_8061a444 * cw -
                                             (double)(float)((double)CONCAT44(0x43300000, param_13 ^ 0x80000000) - bias));
                    if (param_16 != 0) {
                        cw = text_get_char_width(tmp);
                        double xa = (double)CONCAT44(0x43300000, x0 ^ 0x80000000);
                        int x1 = (int)-(float)((double)FLOAT_8061a444 * cw -
                                               (double)(float)(xa - bias));
                        double xb = (double)(CONCAT44(0x43300000, x1) ^ 0x80000000);
                        text_draw_char_shadowed(
                            (double)(float)(xb - bias),
                            (double)(float)((double)CONCAT44(0x43300000, param_14 ^ 0x80000000) - bias),
                            tmp, color, base_col);
                    }
                    param_14 = (uint)((float)((double)CONCAT44(0x43300000, param_14 ^ 0x80000000) - bias) +
                                      FLOAT_80611674);
                    (void)x0;
                    shadow = base_col;
                } else {
                    status_str = (byte *)(uintptr_t)get_string_by_id(0x17c8);
                    color = base_col | 0x64320a00;
                    shadow = base_col;
                }
            } else {
                if ((DAT_805015bd == '\x03') && (7 < DAT_80611934)) {
                    if (*(int *)(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c) + 0x84) == 1) {
                        status_str = (byte *)(uintptr_t)get_string_by_id(0x17c9);
                        color = base_col | 0x64000000;
                        shadow = base_col;
                    } else {
                        status_str = (byte *)(uintptr_t)get_string_by_id(0x17ca);
                        color = base_col | 0x78787800;
                        shadow = base_col | 0x5000;
                    }
                } else {
                    status_str = (byte *)(uintptr_t)get_string_by_id(0x17c5);
                    color = base_col | 0x3c3c3c00;
                    shadow = base_col;
                }
                if ((*(byte *)(ctx + 9) & 1) != 0) {
                    u64 r = statlvl_object_get_ids(ctx, &ids_a, &ids_b);
                    status_str = debug_log_formatted(r, param_2, param_3, param_4, param_5, param_6,
                                                     param_7, param_8, &DAT_806110e0, ids_a, ids_b,
                                                     param_12, param_13, param_14, param_15, param_16);
                }
            }
        }
    } else if (type < 6) {
        status_str = (byte *)(uintptr_t)get_string_by_id(0x17ca);
        color = base_col | 0x78787800;
        shadow = base_col | 0x5000;
        if (DAT_805015bd == '\x03') {
            time_format_short(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              (int)name_buf, *(int *)(ctx + 0x34));
            if (*(int *)(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c) + 0x84) == 2) {
                color = base_col | 0x64000000;
                shadow = base_col;
            }
            status_str = name_buf;
        }
    }

    // Compute the value-column x position (single-column vs paired layout).
    if (single_col == 0) {
        cw = text_get_char_width(status_str);
        x = -(double)(float)((double)FLOAT_8061a494 * (double)FLOAT_80611a68 -
                             (double)(float)((double)(float)((double)CONCAT44(0x43300000, param_13 ^ 0x80000000) - bias) - cw));
        x = x - (double)(float)((double)CONCAT44(0x43300000, param_11 ^ 0x80000000) - bias);
    } else {
        x = (double)(float)((double)CONCAT44(0x43300000, param_13 ^ 0x80000000) - bias);
        x = x - (double)(float)((double)FLOAT_8061a494 * (double)FLOAT_80611a68 +
                                (double)(float)((double)CONCAT44(0x43300000, param_11 ^ 0x80000000) - bias));
    }
    double fit_w = (double)(float)x;

    font = (session_get_state_flag() < 3) ? 3 : 0x12;
    text_set_font_table(font);
    u64 award = award_resolve_string((double)fit_w, (double)FLOAT_8061a494, (double)0, (double)0,
                                     param_5, param_6, param_7, param_8, ctx);
    byte *fit = text_check_scale(fit_w, (void *)award);
    ch = text_get_char_height(fit);

    if (param_16 != 0) {
        if (sub == 0) {
            text_draw_char_shadowed(
                (double)(float)((double)CONCAT44(0x43300000, param_11 ^ 0x80000000) - bias),
                (double)(float)((double)CONCAT44(0x43300000, param_12 ^ 0x80000000) - bias),
                fit, base_col | 0x6e6e6e00, base_col | 0x1e1e1e00);
        } else {
            text_draw_string_colored(
                (double)(float)((double)CONCAT44(0x43300000, param_11 ^ 0x80000000) - bias),
                (double)(float)((double)CONCAT44(0x43300000, param_12 ^ 0x80000000) - bias),
                (double)FLOAT_8061a418, (double)FLOAT_8061a418,
                (double)FLOAT_8061a418, (double)FLOAT_8061a418, (double)FLOAT_8061a418);
        }
        if (single_col != 0) {
            param_14 = (uint)((double)(float)((double)CONCAT44(0x43300000, param_14 ^ 0x80000000) - bias) + ch);
            param_12 = (uint)((double)(float)((double)CONCAT44(0x43300000, param_12 ^ 0x80000000) - bias) + ch);
        }
        if (sub == 0) {
            cw = text_get_char_width(status_str);
            text_draw_char_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, param_13 ^ 0x80000000) - bias) - cw),
                (double)(float)((double)CONCAT44(0x43300000, param_14 ^ 0x80000000) - bias),
                status_str, color, shadow);
        } else {
            cw = text_get_char_width(status_str);
            text_draw_string_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, param_13 ^ 0x80000000) - bias) - cw),
                (double)(float)((double)CONCAT44(0x43300000, param_12 ^ 0x80000000) - bias),
                (double)FLOAT_8061a418, (double)FLOAT_8061a418,
                (double)FLOAT_8061a418, (double)FLOAT_8061a418, (double)FLOAT_8061a418);
        }
    }
    FUN_8038270c();
}

// ----------------------------------------------------------------------------
// 0x802f41ec  arcsel_prefetch_profiles
// Touches (loads) the profile slot for every human + AI player so the select
// screen can display their stats without a load hitch.
// ----------------------------------------------------------------------------
void arcsel_prefetch_profiles(void) {
    int off = 0;
    FUN_803826e8();
    for (int i = 0; i < DAT_80612610 + DAT_806120fc; i = i + 1) {
        if (-1 < *(int *)((int)&DAT_805014a0 + off)) {
            profile_get_slot(*(int *)((int)&DAT_805014a0 + off));
        }
        off = off + 4;
    }
    FUN_80382734();
}

// ----------------------------------------------------------------------------
// 0x802f42dc  arcsel_format_score
// Formats a score/time value into dst (param_13) depending on the current mode
// bytes: race/time modes format as a time (optionally negated or offset by
// 1000), story mode uses a signed count, otherwise an "x N" style string with a
// pluralized suffix. Returns 1 for a time string, -1 for a formatted count, 0
// for the default numeric path.
// ----------------------------------------------------------------------------
u32 arcsel_format_score(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                        u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                        int param_9, u32 param_10, int param_11, int param_12,
                        int param_13, int param_14, u32 param_15, u32 param_16) {
    int   str;
    u32   result = 0;
    uint  mode = (uint)(byte)DAT_805015bd;

    if ((((mode == 1) && (param_14 != 1)) && (param_14 != 2)) || (DAT_805015be == 0xc)) {
        result = 1;
        uint sub = (uint)(byte)DAT_805015be;
        if ((sub == 0xc) && ((DAT_806120d8 & (1 << param_9)) == 0)) {
            time_format_short(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              param_13, 0);
        } else if (sub == 0xc) {
            if (param_11 == -1000) {
                void *fmt = (void *)(uintptr_t)get_string_by_id(0x1678);
                FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             param_13, fmt, param_11, (int)sub, param_13, param_14, mode, param_16);
                result = 0xffffffff;
            } else {
                time_format_short(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                  param_8, param_13, param_11 + 1000);
            }
        } else {
            time_format_short(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              param_13, param_11);
        }
    } else if (DAT_805015dc == 0) {
        if (mode == 5) {
            time_format_short(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              param_13, -param_11);
            result = 1;
        } else {
            FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                         param_13, &DAT_806110ec, param_11, param_12, param_13, param_14, mode, param_16);
        }
    } else if (param_12 < 1) {
        time_format_short(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                          param_13, param_11);
        result = 1;
    } else if (param_12 == 1) {
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     param_13, &DAT_806110e8, 1, 1, param_13, param_14, mode, param_16);
        str = get_string_by_id(0x66b);
        FUN_8038b4a0(param_13, str);
        result = 0xffffffff;
    } else {
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     param_13, &DAT_806110e8, param_12, param_12, param_13, param_14, mode, param_16);
        str = get_string_by_id(0x66a);
        FUN_8038b4a0(param_13, str);
        result = 0xffffffff;
    }
    return result;
}

// ----------------------------------------------------------------------------
// 0x802f4494  arcsel_count_profiles_for_mode
// Counts how many active player profiles have their current gamemode field
// (record +0x84) equal to `param_1`.
// ----------------------------------------------------------------------------
int arcsel_count_profiles_for_mode(int param_1) {
    int count = 0;
    int remaining = DAT_80612610 + DAT_806120fc;
    int off = 0;
    if (0 < remaining) {
        do {
            if ((-1 < *(int *)((int)&DAT_805014a0 + off)) &&
                (DAT_80611d90 + *(int *)((int)&DAT_805014a0 + off) * 0xea0 != 0) &&
                (*(int *)(DAT_80611d90 + *(int *)((int)&DAT_805014a0 + off) * 0xea0 + 0x84) == param_1)) {
                count = count + 1;
            }
            off = off + 4;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return count;
}
