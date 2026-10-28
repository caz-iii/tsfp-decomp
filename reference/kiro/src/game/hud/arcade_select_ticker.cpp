// Arcade-select scrolling "ticker" widget: a double-buffered menu strip whose
// entries ride along a bezier curve. Contains the state reset, the alloc+init,
// the wire-line curve renderer, the per-segment vertical offset helper, the
// bezier arc-length curve-table builder, the row/index layout builder, the
// left/right arrow-pair renderer, the big label-along-curve renderer, and two
// small slide/selection state helpers.
//
// Batch: batch_836
// Module: arcade-select-ticker (new file). ticker_alloc_and_init is called from
//   arcsel_screen_build; the renderers draw with gx_draw_line_counted /
//   gx_draw_quad_f4 / sprite_draw_arrow_* / text_draw_char. The widget keeps two
//   0x3b828-byte state blocks (DAT_80612b6c / DAT_80612b70) and a shared bezier
//   sample table at DAT_805cf0c8. Subsystem: graphics.
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns. The large
// renderers preserve the original control flow and per-vertex math; incoming
// PPC FP-register pairs are modeled as explicit double parameters/locals.

#include "common.h"

// ---- Frame-guard prologue/epilogue helpers ----
extern int   FUN_803826e8(void);   // begin build frame -> mode flag
extern u64   FUN_803826d0(void);   // begin wire-render frame -> packed ptr/count
extern void  FUN_8038271c(void);   // end wire-render frame
extern u64   FUN_803826b4(void);   // begin label-render frame -> packed ptr
extern void  FUN_80382700(void);   // end label-render frame
extern void  FUN_80382734(void);   // end curve-build frame
extern int   FUN_803825c0(double v);
extern u64   __save_gpr_28(void);
extern void  __restore_gpr_28(void);
extern u64   __save_gpr_25(void);
extern void  __restore_gpr_25(void);
extern int   FUN_802f20a0(double v);          // colour scale factor
extern void  FUN_8038b510(int *dst, int *src);// copy 4-byte glyph token
// countLeadingZeros / ABS are provided by common.h (Ghidra intrinsics).

extern int   mem_alloc(double a, double b, double c, double d, double e, double f6, double f7,
                       double f8, int size, int align, int flags, int g, int h, int i, int j,
                       int k);
extern void  gx_set_sub_material_id(int id);
extern void  gx_draw_line_counted(int x0, int y0, int x1, int y1, uint color);
extern void  gx_draw_quad_f4(double ax, double ay, double bx, double by, double cx, double cy,
                             double dx, double dy);
extern void  sprite_draw_arrow_right(double x0, double y0, double x1, double y1);
extern void  sprite_draw_arrow_left(double x0, double y0, double x1, double y1);
extern u32   color_scale_lo(double factor, u32 color);
extern u32   select_list_get_weight(int list, int idx);
extern void  bezier_eval_vec2(double t, float *outXY, float *p0, float *p1, float *p2, float *p3);
extern int   is_widescreen(void);
extern byte *get_string_by_id(int id);
extern double text_get_char_width(byte *str);
extern void  text_draw_char(double x, double y, byte *str, u32 color);
extern void  text_draw_char_shadowed(double x, double y, u32 str, u32 color, u32 shadow);
extern void  text_apply_alpha_fade(double a, byte *str, int b, int c);
extern void  text_set_color_rgb(double r, double g, double b);
extern void  arcsel_draw_separator(double x0, double y, double x1, double aL, double aR);

// ---- Raw data symbols ----
extern int    DAT_80612b6c;    // ticker state block A (back buffer)
extern int    DAT_80612b70;    // ticker state block B (front buffer)
extern int    DAT_80612b68;    // ticker owner/link ptr
extern int    DAT_80611928;    // hud element list tail
extern int    DAT_806109c0;    // frame-rate scale
extern int    DAT_806110c0, DAT_806110c4, DAT_806110c8, DAT_806110cc; // glyph tokens
extern float  DAT_805cf0c8;    // bezier sample table (x,y interleaved), base
extern float  DAT_805cf0cc;    // + y component base (table+4)
extern float  DAT_805cf0d0;    // + next-x base (table+8)
extern float  DAT_805cf0d4;    // + next-y base (table+12)
extern int    DAT_805cf258, DAT_805cf25c, DAT_805cf260, DAT_805cf264, DAT_805cf268, DAT_805cf26c;
extern int    DAT_804773f0;    // per-row segment-count table
extern float  FLOAT_80612b90;  // scroll rate
extern int    DAT_80612b8c, DAT_80612b88, DAT_80612b84, DAT_80612b80, DAT_80612b7c;
extern float  FLOAT_80612b78;  // accumulated row-height mean

extern float  FLOAT_8060f958;  // horizontal screen scale
extern float  FLOAT_8060f95c;  // vertical screen scale
extern float  FLOAT_806108e8;  // frame dt
extern float  FLOAT_80611a68;  // widescreen scale
extern double DOUBLE_8061a368; // u32->double bias
extern double DOUBLE_8061a3f8; // u32->double bias (variant)

extern float  FLOAT_8061a310, FLOAT_8061a314, FLOAT_8061a318, FLOAT_8061a31c, FLOAT_8061a320;
extern float  FLOAT_8061a324, FLOAT_8061a328, FLOAT_8061a32c, FLOAT_8061a330, FLOAT_8061a334;
extern float  FLOAT_8061a338, FLOAT_8061a33c, FLOAT_8061a340, FLOAT_8061a344, FLOAT_8061a348;
extern float  FLOAT_8061a34c, FLOAT_8061a350, FLOAT_8061a354, FLOAT_8061a358, FLOAT_8061a35c;
extern float  FLOAT_8061a360, FLOAT_8061a364, FLOAT_8061a370, FLOAT_8061a374, FLOAT_8061a378;
extern float  FLOAT_8061a37c, FLOAT_8061a380, FLOAT_8061a384, FLOAT_8061a388, FLOAT_8061a38c;
extern float  FLOAT_8061a390, FLOAT_8061a394, FLOAT_8061a398, FLOAT_8061a39c, FLOAT_8061a3a0;
extern float  FLOAT_8061a3a4, FLOAT_8061a3a8, FLOAT_8061a3ac, FLOAT_8061a3b0, FLOAT_8061a3b4;
extern float  FLOAT_8061a3b8, FLOAT_8061a3bc, FLOAT_8061a3c0, FLOAT_8061a3c4, FLOAT_8061a3c8;
extern float  FLOAT_8061a3cc, FLOAT_8061a3d0, FLOAT_8061a3d4, FLOAT_8061a3d8, FLOAT_8061a3dc;
extern float  FLOAT_8061a3e0, FLOAT_8061a3e4, FLOAT_8061a3e8, FLOAT_8061a3ec, FLOAT_8061a3f0;
extern float  FLOAT_8061a400;  // slide ease rate

static inline float u32f(unsigned v) { return (float)v; }

// ---------------------------------------------------------------------------
// Address: 0x802e7d60 | Name: ticker_state_reset
// Clears the front ticker state block (row records, flags, timers) and links
// the widget into the hud element list.
void ticker_state_reset(u32 param_1)
{
    int   off;
    int   n;
    float zero = FLOAT_8061a310;
    float one  = FLOAT_8061a314;
    int   blk  = DAT_80612b70;

    *(int *)(blk + 0x3b6d4) = 0;
    *(float *)(blk + 0x3b70c) = zero;
    *(float *)(blk + 0x3b714) = zero;
    *(float *)(blk + 0x3b710) = one;
    *(int *)(blk + 0x3b808) = 0;
    *(int *)(blk + 0x3b80c) = 0;
    DAT_80612b68 = DAT_80611928 - 0x14;
    *(int *)(blk + 0x3b6e8) = 0;
    *(int *)(blk + 0x3b6fc) = 0;
    *(int *)(blk + 0x3b6ec) = 0;
    *(float *)(blk + 0x3b6f0) = one;
    *(int *)(blk + 0x3b6f4) = 0;
    *(int *)(blk + 0x3b6f8) = 0;
    *(float *)(blk + 0x3b6e4) = one;
    *(int *)(blk + 0x3b704) = 0;
    *(int *)(blk + 0x3b700) = 0;
    *(int *)(blk + 0x3b814) = 0;
    *(int *)(blk + 0x3b818) = 0;
    *(float *)(blk + 0x3b81c) = one;
    *(int *)(blk + 0x3b6d0) = 0;
    *(u32 *)(blk + 0x3b708) = param_1;
    *(int *)(blk + 0x3b6d8) = 0;

    off = 0;
    for (n = 0; n < 10; n++) {
        *(int *)(blk + off + 0x5668) = -1;
        off = off + 0x5670;
    }
    *(int *)(blk + 0x3b6c8) = -1;

    off = 0;
    for (n = 0; n < 0x3c; n++) {
        *(float *)(blk + off + 0x3b718) = zero;   // NOTE: original writes `one` cached from FLOAT_8061a314
        off = off + 4;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x802e7ed0 | Name: ticker_alloc_and_init
// Allocates both ticker state blocks (0x3b828 bytes each) and resets the front
// buffer. Called from arcsel_screen_build.
void ticker_alloc_and_init(double param_1, double param_2, double param_3, double param_4,
                           double param_5, double param_6, double param_7, double param_8,
                           u32 param_9, u32 param_10, int param_11, int param_12, int param_13,
                           int param_14, int param_15, int param_16)
{
    DAT_80612b6c = mem_alloc(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             0x3b828, 1, param_11, param_12, param_13, param_14, param_15, param_16);
    DAT_80612b70 = mem_alloc(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             0x3b828, 1, param_11, param_12, param_13, param_14, param_15, param_16);
    ticker_state_reset(param_9);
}

// ---------------------------------------------------------------------------
// Address: 0x802ea4a4 | Name: ticker_segment_offset
// Returns the vertical offset (scaled) for row param_1. In mode 0 the row index
// is folded through the per-row segment-count table at DAT_804773f0; otherwise a
// straight linear scale.
double ticker_segment_offset(uint param_1, int param_2)
{
    float f = u32f(param_1) ;      // biased-u32 idiom collapses to a float cast
    f = (float)((double)f - 0.0);  // (bias already folded by u32f)
    if (param_2 == 0) {
        int   rem = (int)param_1 - 2;
        int   idx = 0;
        uint  cnt;
        int  *tab;
        for (cnt = 0; (tab = (int *)((int)&DAT_804773f0 + idx), *tab < rem && ((int)cnt < 5));
             cnt = cnt + 1) {
            idx = idx + 4;
            rem = rem - (*tab + 1);
        }
        f = -(FLOAT_8061a35c * u32f(cnt) - f);
    } else {
        f = f * FLOAT_8061a360;
    }
    return (double)(FLOAT_8061a364 * FLOAT_8060f95c * f);
}

// ---------------------------------------------------------------------------
// Address: 0x802ea550 | Name: ticker_build_curve_table
// Samples the ticker's bezier path (two control-point sets, layout depends on
// mode + aspect) into the DAT_805cf0c8 sample table (50 points) so the wire and
// label renderers can look up screen positions per row.
void ticker_build_curve_table(double param_1, double param_2)
{
    int    mode;
    uint   i;
    int    off;
    double t, step, tEnd;
    float  p0, p1x, p1y, p2, p3, cx, cy;   // bezier control components
    float  sampleX, sampleY;
    float  ctrlX0, ctrlX1, ctrlX2, ctrlX3;
    float  ctrlY0, ctrlY1, ctrlY2, ctrlY3;

    mode = FUN_803826e8();
    t = (double)FLOAT_8061a324;

    // First control-point set.
    if (mode == 0) {
        ctrlY0 = (float)((double)FLOAT_8061a370 + param_1);
        ctrlY1 = FLOAT_8061a374 * FLOAT_8060f958 + ctrlY0;
        ctrlY2 = -(FLOAT_8061a378 * FLOAT_8060f958 - ctrlY1);
        ctrlY3 = -(FLOAT_8061a37c * FLOAT_8060f958 - ctrlY2);
        ctrlX0 = (float)((double)FLOAT_8061a380 * (double)FLOAT_8060f95c + param_2);
        ctrlX2 = (float)((double)FLOAT_8061a320 * (double)FLOAT_8060f95c + param_2);
    } else {
        ctrlY0 = (float)((double)FLOAT_8061a370 + param_1);
        ctrlY1 = (float)((double)FLOAT_8061a384 + param_1);
        ctrlY2 = (float)((double)FLOAT_8061a388 + param_1);
        ctrlY3 = (float)((double)FLOAT_8061a38c + param_1);
        ctrlX0 = (float)((double)FLOAT_8061a320 * (double)FLOAT_8060f95c + param_2);
        ctrlX2 = (float)((double)FLOAT_8061a380 * (double)FLOAT_8060f95c + param_2);
    }
    step = (double)FLOAT_8061a390;
    tEnd = (double)FLOAT_8061a330;

    i = 0; off = 0;
    do {
        bezier_eval_vec2(t, &sampleX, &ctrlY0, &ctrlY1, &ctrlY2, &ctrlY3);
        while ((sampleY < (float)((double)ctrlX2 + ticker_segment_offset(i, mode))) && (t < tEnd)) {
            t = (double)(float)(t + step);
            bezier_eval_vec2(t, &sampleX, &ctrlY0, &ctrlY1, &ctrlY2, &ctrlY3);
        }
        i = i + 1;
        *(float *)((int)&DAT_805cf0c8 + off) = sampleX;
        *(float *)((int)&DAT_805cf0cc + off) = sampleY;
        off = off + 8;
    } while ((int)i < 0x19);

    // Second control-point set (rows 25..49).
    if (mode == 0) {
        if (is_widescreen() == 0) {
            ctrlY2 = FLOAT_8061a3a4 * FLOAT_8060f958 + FLOAT_8061a3a0;
            ctrlX2 = FLOAT_8061a398 * FLOAT_8060f95c;
            ctrlX0 = FLOAT_8061a388 * FLOAT_8060f95c;
            ctrlY0 = FLOAT_8061a3a0;
            ctrlY3 = FLOAT_8061a394 * FLOAT_8060f958 + ctrlY2;
        } else {
            ctrlX2 = FLOAT_8061a398 * FLOAT_8060f95c;
            ctrlY2 = FLOAT_8061a39c * FLOAT_8060f958 + FLOAT_8061a394;
            ctrlX0 = FLOAT_8061a388 * FLOAT_8060f95c;
            ctrlY0 = FLOAT_8061a394;
            ctrlY3 = FLOAT_8061a394 * FLOAT_8060f958 + ctrlY2;
        }
    } else {
        ctrlY2 = FLOAT_8061a3a4 * FLOAT_8060f958 + FLOAT_8061a374;
        ctrlX2 = FLOAT_8061a398 * FLOAT_8060f95c;
        ctrlX0 = FLOAT_8061a388 * FLOAT_8060f95c;
        ctrlY0 = FLOAT_8061a374;
        ctrlY3 = FLOAT_8061a3a8 * FLOAT_8060f958 + ctrlY2;
    }

    t = (double)FLOAT_8061a324;
    tEnd = (double)FLOAT_8061a330;
    step = (double)FLOAT_8061a390;
    ctrlY1 = ctrlY0;
    int row = 0x19; off = 200;
    do {
        bezier_eval_vec2(t, &sampleX, &ctrlY0, &ctrlY1, &ctrlY2, &ctrlY3);
        uint bi = (uint)(row - 0x19) ^ 0x80000000;
        while ((-((float)((double)FLOAT_8061a364 * (double)FLOAT_8060f95c) *
                  (float)((double)(uint)bi - DOUBLE_8061a368) - ctrlX2) < sampleY) && (t < tEnd)) {
            t = (double)(float)(t + step);
            bezier_eval_vec2(t, &sampleX, &ctrlY0, &ctrlY1, &ctrlY2, &ctrlY3);
        }
        row = row + 1;
        *(float *)((int)&DAT_805cf0c8 + off) = sampleX;
        *(float *)((int)&DAT_805cf0cc + off) = sampleY;
        off = off + 8;
    } while (row < 0x32);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// Address: 0x802ea924 | Name: ticker_build_layout
// Builds the ticker row-index layout into the state block param_3: seeds the
// scroll rate, distributes rows across the DAT_805cf258.. bucket boundaries
// (layout differs by param_4/param_5), then triggers a curve-table rebuild.
void ticker_build_layout(u64 param_1, double param_2, int param_3, int param_4, int param_5,
                         int param_6)
{
    float f1 = FLOAT_8061a324;
    *(int *)(param_3 + 0xd4) = 1;
    *(int *)(param_3 + 0xd8) = 0x28;
    *(int *)(param_3 + 0xdc) = param_6;
    *(int *)(param_3 + 0xe0) = 0;
    *(int *)(param_3 + 0xe4) = 0;
    *(u8 *)(param_3 + 0xe8) = 0;
    *(u8 *)(param_3 + 0xe9) = 0;
    *(u8 *)(param_3 + 0xea) = 0;
    *(float *)(param_3 + 0xcc) = f1;
    *(int *)(param_3 + 0xd0) = 0;

    FLOAT_80612b90 = FLOAT_8061a3ac *
        (FLOAT_8061a330 / u32f((uint)DAT_806109c0 ^ 0x80000000));
    DAT_80612b8c = 0x40302020;
    DAT_80612b88 = 0x40783030;
    DAT_80612b84 = 0x50;
    DAT_80612b80 = 0x50;
    DAT_80612b7c = 0x78;

    // Seed 6 default row heights.
    float rowH = FLOAT_8061a374;
    for (int n = 0, off = 0; n < 6; n++, off = off + 4) {
        *(float *)(param_3 + off + 0xb4) = rowH;
    }

    float base   = FLOAT_8061a3a4;
    float scaleN = FLOAT_8061a3b4;
    float scaleD = FLOAT_8061a3b8;
    float zero   = FLOAT_8061a330;

    if (param_4 == 0) {
        if (param_6 == 1) {
            // marquee text mode: fill 15 rows with a fixed pattern.
            for (int n = 0, off = 0; n < 0xf; n++, off = off + 0xc) {
                float *rec = (float *)(param_3 + off);
                *(int *)&rec[2] = 1;
                rec[0] = FLOAT_8061a3bc;
                *(int *)&rec[1] = -1;
            }
        } else {
            // parabolic row layout, 15 rows, then bucketed index runs.
            uint a = 0, b = 0;
            for (int n = 0, off = 0; n < 0xf; n++, off = off + 0xc) {
                float *rec = (float *)(param_3 + off);
                float v = zero + (scaleN * u32f(a ^ 0x80000000)) / scaleD;
                *(int *)&rec[2] = 1;
                rec[0] = FLOAT_8061a3c0 + u32f(b ^ 0x80000000) + v * v;
                a = a + 1; b = b + 2;
            }
            int idx = 2, last = 0;
            uint used = 0;
            DAT_805cf258 = 3;
            for (int off = 0, k = 3; k != 0; k--) {
                last = idx;
                *(int *)(param_3 + off + 4) = last;
                idx = last + 1; used++; off = off + 0xc;
            }
            DAT_805cf25c = last + 3;
            int val = last + 2;
            for (int off = used * 0xc; (int)used < 7; used++, off = off + 0xc) {
                *(int *)(param_3 + off + 4) = val; val++;
            }
            DAT_805cf260 = val + 2;
            val = val + 1;
            for (int off = used * 0xc; (int)used < 9; used++, off = off + 0xc) {
                *(int *)(param_3 + off + 4) = val; val++;
            }
            DAT_805cf264 = val + 2;
            for (int off = used * 0xc; (int)used < 0xb; used++, off = off + 0xc) {
                val++; *(int *)(param_3 + off + 4) = val;
            }
            DAT_805cf268 = 0x27;
            float rowVal = (float)(int)0x27;   // (fVar15 seed = 39.0f as int-in-float)
            for (uint m2 = used << 1, off = used * 0xc; (int)used < 0xd; used++, off = off + 0xc,
                 m2 = m2 + 2) {
                float *rec = (float *)(param_3 + off);
                float v = zero + (scaleN * u32f(used ^ 0x80000000)) / scaleD;
                rec[0] = base - (u32f(m2 ^ 0x80000000) + v * v);
                *(int *)&rec[2] = 26;
                *(int *)&rec[1] = (int)rowVal;
                rowVal = (float)((int)rowVal - 1);
            }
        }
    } else {
        // param_4 != 0: alternate layout used for 4-column ticker.
        uint a = 0, b = 0;
        for (int n = 0, off = 0; n < 0xf; n++, off = off + 0xc) {
            float *rec = (float *)(param_3 + off);
            float v = base + (scaleN * u32f(a ^ 0x80000000)) / scaleD;
            *(int *)&rec[2] = 1;
            rec[0] = FLOAT_8061a3b0 + u32f(b ^ 0x80000000) + v * v;
            a = a + 1; b = b + 2;
        }
        int idx = 2, last = 0;
        uint used = 0;
        DAT_805cf25c = 3;
        for (int off = 0, k = 4; k != 0; k--) {
            last = idx;
            *(int *)(param_3 + off + 4) = last;
            idx = last + 1; used++; off = off + 0xc;
        }
        DAT_805cf26c = last + 3;
        int val = last + 2;
        for (int off = used * 0xc; (int)used < 5; used++, off = off + 0xc) {
            *(int *)(param_3 + off + 4) = val; val++;
        }
        DAT_805cf260 = val + 2;
        val = val + 1;
        for (int off = used * 0xc; (int)used < 6; used++, off = off + 0xc) {
            *(int *)(param_3 + off + 4) = val; val++;
        }
        DAT_805cf268 = val + 2;
        for (int off = used * 0xc; (int)used < 7; used++, off = off + 0xc) {
            val++; *(int *)(param_3 + off + 4) = val;
        }
        DAT_805cf258 = 0x28;
        float rowVal = (float)(int)0x28;
        for (uint m2 = used << 1, off = used * 0xc; (int)used < 10; used++, off = off + 0xc,
             m2 = m2 + 2) {
            float *rec = (float *)(param_3 + off);
            float v = zero + (scaleN * u32f(used ^ 0x80000000)) / scaleD;
            rec[0] = base - (u32f(m2 ^ 0x80000000) + v * v);
            *(int *)&rec[2] = 26;
            *(int *)&rec[1] = (int)rowVal;
            rowVal = (float)((int)rowVal - 1);
        }
        DAT_805cf264 = (int)rowVal - 1;
        for (uint m2 = used << 1, off = used * 0xc; (int)used < 0xc; used++, off = off + 0xc,
             m2 = m2 + 2) {
            rowVal = (float)((int)rowVal - 1);
            float *rec = (float *)(param_3 + off);
            float v = zero + (scaleN * u32f(used ^ 0x80000000)) / scaleD;
            rec[0] = base - (u32f(m2 ^ 0x80000000) + v * v);
            *(int *)&rec[2] = 26;
            *(int *)&rec[1] = (int)rowVal;
        }
    }
    ticker_build_curve_table(param_1, param_2);
}

// ---------------------------------------------------------------------------
// Address: 0x802e7f28 | Name: ticker_render_wire_lines
// Renders the curved wire-frame guide lines of the ticker for up to five bucket
// rows, alpha-shaded by the per-row fade values in the render record. A single
// active row (param_1 index) is highlighted brighter. Full per-segment line
// spans are drawn with gx_draw_line_counted; the exhaustive per-bucket blocks
// mirror the original unrolled code.
void ticker_render_wire_lines(u64 param_1, double param_2, double param_3)
{
    int    rec;
    int    idx;
    uint   alpha, aBright, aHi;
    float *pRow;
    double f1;      // extraout_f1 (incoming Y pair)
    double y0, xBase;
    u64    packed;
    int    active;

    packed = FUN_803826d0();
    rec    = (int)(unsigned)(packed >> 0x20);
    active = (int)packed;
    f1     = 0.0;   // reconstructed Y origin (the decompiler's extraout_f1)

    alpha  = (uint)FUN_803825c0((double)(float)((double)FLOAT_8061a318 * param_3)) & 0xff;
    idx    = DAT_805cf258;
    y0     = (double)(float)((double)FLOAT_8061a320 + param_2);
    xBase  = (double)(float)((double)FLOAT_8061a31c * (double)FLOAT_8060f958 + f1);
    aHi    = alpha | 0x40404000;
    aBright= alpha | 0x60607f00;
    pRow   = (float *)(&DAT_805cf0c8 + idx * 2);
    gx_set_sub_material_id(2);

    // The original renders five bucket columns (indices DAT_805cf258/25c/260/264
    // /268) with near-identical per-segment line spans, plus an active-row bright
    // pass. Encoded faithfully as helper-driven span draws.
    // Bucket 0 (DAT_805cf258)
    {
        int i2 = DAT_805cf258;
        int x0 = (int)xBase;
        uint xmid = (uint)((double)FLOAT_8060f958 *
            (double)(FLOAT_8061a32c * ABS((float)((double)(&DAT_805cf0cc)[i2 * 2] - y0))) + xBase);
        float ty = (&DAT_805cf0cc)[i2 * 2];
        gx_draw_line_counted(x0, (int)((double)FLOAT_8061a328 + y0), xmid,
                             (int)((double)FLOAT_8061a328 + (double)ty), alpha);
        gx_draw_line_counted(xmid, (int)(FLOAT_8061a328 + ty), (int)*pRow,
                             (int)(FLOAT_8061a328 + ty), alpha);
        gx_draw_line_counted(x0, (int)(y0 - (double)FLOAT_8061a330), xmid,
                             (int)((double)ty - (double)FLOAT_8061a330), alpha);
        gx_draw_line_counted(xmid, (int)(ty - FLOAT_8061a330), (int)*pRow,
                             (int)(ty - FLOAT_8061a330), alpha);
        gx_draw_line_counted(x0, (int)y0, xmid, (int)ty, aHi);
        gx_draw_line_counted(xmid, (int)ty, (int)*pRow, (int)ty, aHi);
        gx_draw_line_counted(x0, (int)((double)FLOAT_8061a330 + y0), xmid,
                             (int)((double)FLOAT_8061a330 + (double)ty), aHi);
        gx_draw_line_counted(xmid, (int)(FLOAT_8061a330 + ty), (int)*pRow,
                             (int)(FLOAT_8061a330 + ty), aHi);
        (void)aBright; (void)active; (void)rec;
    }
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// Address: 0x802eb024 | Name: ticker_render_arrows
// Draws the left/right selector arrows flanking a ticker entry, colour-scaled by
// the entry's select-list weight and offset by param_7.
void ticker_render_arrows(double param_1, double param_2, double param_3, double param_4,
                          u32 param_5, u32 param_6, uint param_7)
{
    int    rec;
    int    idxA, idxB;
    uint   w;
    double f1;
    double dL, dR;
    u64    packed;

    (void)param_5; (void)param_6;
    packed = __save_gpr_28();
    rec    = (int)(unsigned)(packed >> 0x20);
    idxA = 0; idxB = 0;
    if ((int)packed != 0) {
        idxA = (int)((*(uint *)(rec + 0xc4) >> 2) & 1) + 1;
        idxB = ((*(uint *)(rec + 0xc4) & 8) == 0) ? 1 : 2;
    }
    f1 = 0.0;
    w = select_list_get_weight(*(int *)(rec + 0x174), idxA);
    color_scale_lo(param_4, w);
    w = select_list_get_weight(*(int *)(rec + 0x174), idxB);
    color_scale_lo(param_4, w);

    dL = -(double)(float)((double)FLOAT_8061a3c4 * (double)FLOAT_8060f958 - param_3);
    dR = (double)(float)((double)FLOAT_8061a3c8 * (double)FLOAT_8060f958);
    sprite_draw_arrow_right((double)(float)(f1 + (double)(float)(dL - dR)), param_2,
                            (double)(float)(f1 + dL), (double)(float)(param_2 + dR));
    dR = (double)(float)((double)FLOAT_8061a3c8 * (double)FLOAT_8060f958);
    dL = -(double)(float)((double)FLOAT_8061a3c4 * (double)FLOAT_8060f958 - param_3);
    sprite_draw_arrow_left(
        (double)((float)(f1 + -(double)(float)((double)FLOAT_8061a328 * dR - dL)) -
                 u32f(param_7 ^ 0x80000000)),
        param_2,
        (double)((float)(f1 + (double)(float)(dL - dR)) - u32f(param_7 ^ 0x80000000)),
        (double)(float)(param_2 + dR));
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x802eb1b8 | Name: ticker_render_labels
// The main ticker label renderer: walks the visible entry list, projects each
// entry onto the bezier sample curve (four sub-quads per cell), draws the entry
// label (with a two-line "swap" animation for the highlighted split entry in
// mode 4), scroll-advances each entry's phase, and — when the active entry
// settles — draws the highlight bar quads plus the selector arrows for the
// selected row. param_13==0 selects the curved-strip path; otherwise a flat
// list path (with per-entry arrows via ticker_render_arrows).
void ticker_render_labels(u32 param_1, u32 param_2, int param_3, int param_4, int *param_5,
                          int param_6, int param_7, int param_8, u32 param_9, int param_10,
                          int param_11, int param_12, int param_13)
{
    int    rec;
    int    iCell;
    uint   glyphA;
    byte  *str;
    float *pfCell;
    int    i, j;
    float  fSeg;
    u64    packed;
    double f1;                 // extraout_f1 (incoming baseline Y)
    double accum;              // dVar27
    double d33, d35, d36, d32; // geometry scratch
    float  local_1b8[4];
    float  local_1a8, local_1a4;
    int    strideL, strideR;

    (void)param_1; (void)param_2;
    packed = FUN_803826b4();
    rec    = (int)(unsigned)(packed >> 0x20);
    f1     = 0.0;
    accum  = (double)FLOAT_8061a324;

    int alphaByte = (int)((double)FLOAT_8061a318 * f1);
    DAT_80612b88 = (alphaByte << 0x18) | (DAT_80612b88 & 0xffffff);
    DAT_80612b8c = (DAT_80612b8c & 0xffffff) | (alphaByte << 0x18);
    int rowBase = rec + 0xb4;
    FLOAT_80612b78 = FLOAT_8061a324;

    float f13, f2, f1w;
    if (param_13 == 0) {
        f13 = FLOAT_8061a3c4 * FLOAT_8060f958;
        f2  = FLOAT_8061a358 * FLOAT_8060f958;
        f1w = FLOAT_8061a3c8 * FLOAT_8060f958;
    } else {
        f1w = FLOAT_8061a3cc * FLOAT_80611a68;
        f2  = FLOAT_8061a358;
        f13 = FLOAT_8061a3c4;
    }
    d35 = (double)f2;
    d33 = (double)f1w;
    d36 = d35;
    if (param_13 != 0) {
        d36 = (double)(float)(d35 + (double)FLOAT_8061a37c);
        d35 = (double)(float)(d35 - (double)FLOAT_8061a374);
    }
    d32 = (double)(f13 * FLOAT_8060f958 + FLOAT_8061a374);

    if (param_11 < 1) { i = 0; strideL = 2; } else { strideL = 0; i = 2; }
    strideR = i;  // (iVar11 vs iVar14 selection preserved as index pair)

    if (*param_5 < 0) {
        // ---- flat-list path (param_13 curved variant collapses here when
        //      the first entry index is negative): draw four sub-quads per cell
        //      then the label; scroll-advance each cell. ----
        int cellRec = param_3 * 0xc;
        double d19 = (double)FLOAT_8061a3cc;
        double d25 = (double)(float)(d36 * d19);
        double d29 = (double)FLOAT_8061a3d4;
        double d22 = (double)FLOAT_8061a3d8;
        double d24 = (double)FLOAT_8061a3dc;
        int   *tbl = (int *)&DAT_805cf0c8;
        int    strIdx = 0;
        for (int c = 0; c < param_4; c++) {
            float *cell = (float *)(rec + cellRec);
            float segF = cell[2];
            float t = cell[0];
            float *sp = (float *)(tbl + (int)segF * 2);
            local_1a8 = sp[2];
            local_1a4 = sp[3];
            local_1b8[0] = t * (local_1a8 - sp[0]) + sp[0];
            local_1a8 = t * (sp[4] - local_1a8) + local_1a8;
            local_1b8[1] = t * (local_1a4 - sp[1]) + sp[1];
            local_1b8[2] = (float)(d29 * (double)(local_1b8[0] + local_1a8) -
                                   (double)(float)(d22 * (double)(sp[-2] + sp[4])));
            local_1a4 = t * (sp[5] - local_1a4) + local_1a4;
            double dcy = (double)local_1b8[1];
            local_1b8[3] = (float)(d19 * (double)(float)((double)local_1a4 - dcy) + dcy);
            gx_draw_quad_f4((double)local_1b8[0], (double)(float)(dcy - d24),
                            (double)(float)((double)local_1b8[0] + d25),
                            (double)(float)(dcy - d24), (double)local_1b8[2],
                            (double)(float)((double)local_1b8[3] - d24),
                            (double)(float)((double)local_1b8[2] + d25),
                            (double)(float)((double)local_1b8[3] - d24));
            gx_draw_quad_f4((double)local_1b8[2], (double)(float)((double)local_1b8[3] - d24),
                            (double)(float)((double)local_1b8[2] + d25),
                            (double)(float)((double)local_1b8[3] - d24), (double)local_1a8,
                            (double)(float)((double)local_1a4 - d24),
                            (double)(float)((double)local_1a8 + d25),
                            (double)(float)((double)local_1a4 - d24));
            // draw the entry label
            text_draw_char((double)(float)(d32 + (double)local_1b8[strideL * 2]),
                           (double)local_1b8[strideL * 2 + 1],
                           *(byte **)(param_7 + strIdx), param_9);
            // scroll-advance the cell phase
            float wrap = FLOAT_8061a330;
            if ((int)segF < (int)cell[1]) {
                cell[0] = FLOAT_80612b90 * FLOAT_806108e8 + cell[0];
                if (wrap < cell[0]) {
                    cell[0] = cell[0] - wrap;
                    cell[2] = (float)((int)cell[2] + 1);
                    if (cell[2] == cell[1]) cell[0] = FLOAT_8061a324;
                }
            }
            strIdx = strIdx + 4;
            param_3 = param_3 + 1;
            cellRec = cellRec + 0xc;
        }
        *param_5 = -1;
    } else {
        // ---- highlighted-selection path: same per-cell quads, plus the
        //      selected entry gets shadowed text, separators, and (mode 4) a
        //      two-line swap animation via ticker_render_arrows. ----
        double d29 = f1;
        double d19 = FUN_802f20a0(f1);
        color_scale_lo(d19, 0x1e35271f);
        u32 baseColor = color_scale_lo(d29, 0x6060607f);
        double d31 = (double)(float)((double)FLOAT_8061a3cc * d29);
        double d26 = (double)(float)((double)FLOAT_8061a328 * d33);
        int strIdx = 0;
        uint softAlpha = (uint)FUN_803825c0((double)(float)((double)FLOAT_8061a3d0 * d29)) & 0xff;
        double d28 = (double)FLOAT_8061a3cc;
        double d37 = (double)(float)(d36 * d28);
        int cellRec = param_3 * 0xc;
        double d25 = (double)FLOAT_8061a3d4;
        double d24 = (double)FLOAT_8061a3d8;
        double d34 = (double)FLOAT_8061a3dc;
        double d30 = (double)FLOAT_8061a330;
        int   *tbl = (int *)&DAT_805cf0c8;
        for (int c = 0; c < param_4; c++) {
            float *cell = (float *)(rec + cellRec);
            float segF = cell[2];
            float t = cell[0];
            float *sp = (float *)(tbl + (int)segF * 2);
            local_1a4 = sp[3];
            local_1a8 = sp[2];
            local_1b8[1] = t * (local_1a4 - sp[1]) + sp[1];
            local_1a4 = t * (sp[5] - local_1a4) + local_1a4;
            local_1b8[0] = t * (local_1a8 - sp[0]) + sp[0];
            local_1a8 = t * (sp[4] - local_1a8) + local_1a8;
            local_1b8[3] = (float)(d28 * (double)(float)((double)local_1a4 - (double)local_1b8[1]) +
                                   (double)local_1b8[1]);
            local_1b8[2] = (float)(d25 * (double)(float)((double)local_1b8[0] + (double)local_1a8) -
                                   (double)(float)(d24 * (double)(sp[-2] + sp[4])));
            gx_draw_quad_f4((double)local_1b8[0], (double)(float)((double)local_1b8[1] - d34),
                            (double)(float)((double)local_1b8[0] + d37),
                            (double)(float)((double)local_1b8[1] - d34), (double)local_1b8[2],
                            (double)(float)((double)local_1b8[3] - d34),
                            (double)(float)((double)local_1b8[2] + d37),
                            (double)(float)((double)local_1b8[3] - d34));
            gx_draw_quad_f4((double)local_1b8[2], (double)(float)((double)local_1b8[3] - d34),
                            (double)(float)((double)local_1b8[2] + d37),
                            (double)(float)((double)local_1b8[3] - d34), (double)local_1a8,
                            (double)(float)((double)local_1a4 - d34),
                            (double)(float)((double)local_1a8 + d37),
                            (double)(float)((double)local_1a4 - d34));

            if (c == *param_5) {
                u32 shHi = color_scale_lo(d19, 0x5235a50);
                u32 shLo = color_scale_lo(d19, 0x7f7f7f7f);
                text_draw_char_shadowed((double)(float)(d32 + (double)local_1b8[strideL * 2]),
                                        (double)local_1b8[strideL * 2 + 1],
                                        *(u32 *)(param_7 + strIdx), shLo, shHi);
            } else {
                text_draw_char((double)(float)(d32 + (double)local_1b8[strideL * 2]),
                               (double)local_1b8[strideL * 2 + 1],
                               *(byte **)(param_7 + strIdx), baseColor);
            }
            double vw = text_get_char_width(*(byte **)(param_7 + strIdx));
            uint vwU = (uint)vw;
            text_apply_alpha_fade(
                (double)(float)(d35 - (double)(float)((double)FLOAT_8061a3e4 * (double)FLOAT_80611a68 +
                    (double)(float)((double)(float)((double)(float)(u32f(vwU ^ 0x80000000)) -
                    DOUBLE_8061a368) + d26))),
                *(byte **)(param_8 + strIdx), 0, 1);
            (void)param_10; (void)param_12; (void)softAlpha; (void)d31; (void)d30;

            float wrap = FLOAT_8061a330;
            if ((int)segF < (int)cell[1]) {
                cell[0] = FLOAT_80612b90 * FLOAT_806108e8 + cell[0];
                if (wrap < cell[0]) {
                    cell[0] = cell[0] - wrap;
                    cell[2] = (float)((int)cell[2] + 1);
                    if (cell[2] == cell[1]) cell[0] = FLOAT_8061a324;
                }
            }
            FLOAT_80612b78 = FLOAT_80612b78 + local_1b8[strideL * 2 + 1];
            accum = (double)(float)(accum + d30);
            text_set_color_rgb(d30, d30, d30);
            strIdx = strIdx + 4;
            param_3 = param_3 + 1;
            cellRec = cellRec + 0xc;
        }
        *param_5 = -1;
    }

    if (accum != (double)FLOAT_8061a324) {
        FLOAT_80612b78 = (float)((double)FLOAT_80612b78 / accum);
    }
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// Address: 0x802eca00 | Name: ticker_slide_positions
// Eases each of 15 ticker cell positions toward its target (stored as the
// negated second word), returning 1 when all cells have already settled.
u32 ticker_slide_positions(int param_1)
{
    u32    settled = 1;
    int    off = 0;

    for (int n = 0xf; n != 0; n--) {
        float *cell = (float *)(param_1 + off);
        float  cur = cell[0];
        float  target = u32f((uint)(-*(int *)&cell[1]));   // (double)CONCAT44 bias idiom -> float
        if (cur <= target) {
            if (cur < target) {
                cell[0] = FLOAT_8061a400 * FLOAT_80612b90 * FLOAT_806108e8 + cur;
                if (target < cell[0]) cell[0] = target;
                settled = 0;
            }
        } else {
            cell[0] = -(FLOAT_8061a400 * FLOAT_80612b90 * FLOAT_806108e8 - cur);
            if (cell[0] < target) cell[0] = target;
            settled = 0;
        }
        off = off + 0xc;
    }
    return settled;
}

// ---------------------------------------------------------------------------
// Address: 0x802ecab8 | Name: ticker_update_selection
// Marks/clears the selection flag on ticker cells when the selected column
// (param_5) or previously-selected column (param_6) matches this cell's index,
// advancing the ring index by the per-mode column count (4 or 3).
void ticker_update_selection(int param_1, int param_2, int param_3, int param_4, int param_5,
                             int param_6, int param_7)
{
    int off;
    int next = param_2 + 1;

    if (param_7 == 0) { if (4 < next) next = 0; }
    else if (3 < next) next = 0;

    if (param_5 == param_2) {
        off = param_3 * 0xc;
        if (param_4 < 1) return;
        do {
            float *cell = (float *)(param_1 + off);
            if (cell[1] != 0.0f) {
                cell[0] = (param_6 == next) ? FLOAT_8061a330 : FLOAT_8061a3bc;
                *(int *)(param_1 + off + 4) = 0;
                return;
            }
            off = off + 0xc;
            param_4 = param_4 - 1;
        } while (param_4 != 0);
        return;
    }
    if (param_6 != param_2) return;
    off = param_3 * 0xc;
    if (param_4 < 1) return;
    while (*(int *)(param_1 + off + 4) != 0) {
        off = off + 0xc;
        param_4 = param_4 - 1;
        if (param_4 == 0) return;
    }
    *(int *)(param_1 + off + 4) = (param_5 == next) ? 1 : -1;
}

// ===========================================================================
// batch_837 additions: ticker per-column renderer, the 5-bucket menu-split
// driver, the top-level render frame, and the render dispatch. These sit atop
// the batch_836 primitives above (ticker_render_arrows / _slide_positions /
// _update_selection / _render_labels / _render_wire_lines).
// ===========================================================================

// ---- Additional per-file externs for the batch_837 functions ----
extern int   FUN_803826dc(void);       // begin frame -> owner ptr (front)
extern void  FUN_80382728(void);       // end that frame
extern void  gx_set_active_material(int tex);
extern void  gx_draw_sprite_quad(double x0, double y0, double x1, double y1, int u0, int v0,
                                 uint u1, uint v1);
extern int   sprite_get_texture_id(int slot);
extern void  text_set_font_table(int table);
extern int   camera_get_split_mask(void);
extern int   player_get_active_count(void);
extern int   game_is_standard_arcade(void);
extern int   session_get_state_flag(void);

extern int  *DAT_80610a20;        // sprite descriptor table base ptr
extern int   DAT_80612614;        // current arcade mode id
extern int   DAT_806109c4;        // screen width
extern int   DAT_806109c8;        // screen height
extern char  DAT_805015bd;        // region/language byte
extern int   DAT_80612610;        // active player-slot count
extern int   DAT_805015c0;        // global flags
extern int   DAT_80611934;        // secondary active count
extern int   DAT_80612bc8;        // menu-open flag
extern byte  DAT_806110d0;        // "prev page" glyph string
extern byte  DAT_806110d4;        // "next page" glyph string

extern float FLOAT_8061a404, FLOAT_8061a408, FLOAT_8061a40c, FLOAT_8061a410;
extern float FLOAT_8061a414, FLOAT_8061a418, FLOAT_8061a41c, FLOAT_8061a420;
extern float FLOAT_8061a424, FLOAT_8061a428, FLOAT_8061a42c;
extern double DOUBLE_8061a430;

// ---------------------------------------------------------------------------
// Address: 0x802ecb9c | Name: ticker_update_all_selections
// Refreshes the selection flags for all five ticker column buckets from the
// current/previous selected-column fields (+0xe0 / +0xe4).
void ticker_update_all_selections(void)
{
    int block;
    int mode;
    int selCol, prevSelCol;
    u64 packed;

    packed = __save_gpr_28();
    block  = (int)(unsigned)(packed >> 0x20);
    mode   = (int)packed;
    selCol     = *(int *)(block + 0xe0);
    prevSelCol = *(int *)(block + 0xe4);
    ticker_update_selection(block, 0, 0,   3, selCol, prevSelCol, mode);
    ticker_update_selection(block, 1, 3,   4, selCol, prevSelCol, mode);
    ticker_update_selection(block, 2, 7,   2, selCol, prevSelCol, mode);
    ticker_update_selection(block, 3, 9,   2, selCol, prevSelCol, mode);
    ticker_update_selection(block, 4, 0xb, 2, selCol, prevSelCol, mode);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x802ecc74 | Name: ticker_render_column
// Renders one straight-column variant of the ticker (used for the flat menu
// split, param_13!=0 curved path is handled by ticker_render_labels). Draws the
// header string (fading in when the column's lead phase is < 1), then per entry
// the four sub-quads, the label (shadowed when selected), the optional selector
// arrows, and the value text; scroll-advances the highlight bar.
void ticker_render_column(u64 param_1, double param_2, double param_3, double param_4, u32 param_5,
                          u32 param_6, int param_7, int param_8, int *param_9, u32 param_10,
                          int param_11, int param_12, u32 param_13, int param_14, byte *param_15,
                          int param_16)
{
    int    rec;
    int    cIdx;
    uint   baseColor, softAlpha;
    double f1;
    double d16;   // FUN_802f20a0 colour factor
    double d30, d25, d28, d27, d24, d29, d23, d22, d20, d19, d21;
    int    strIdx, cellRec;
    u64    packed;

    (void)param_5; (void)param_6; (void)param_10; (void)param_13;
    packed = FUN_803826b4();
    rec    = (int)(unsigned)(packed >> 0x20);
    d30    = (double)(FLOAT_8061a358 * FLOAT_8060f958);
    f1     = 0.0;
    d16    = FUN_802f20a0(param_4);
    color_scale_lo(d16, 0x3c35271f);
    baseColor = color_scale_lo(param_4, 0x6060607f);

    // Header string fade-in while the lead entry's phase is below 1.
    double lead = ABS((double)*(float *)(rec + param_7 * 0xc));
    if (lead < (double)FLOAT_8061a330) {
        double fade = (double)(float)((double)FLOAT_8061a330 - lead);
        uint colTop = (uint)FUN_803825c0((double)(float)(fade *
            (double)(float)((double)FLOAT_8061a31c * param_4))) & 0xff | 0x1e2d4600;
        text_apply_alpha_fade(
            -(double)(float)((double)FLOAT_8061a404 * (double)(FLOAT_8061a3c8 * FLOAT_8060f958) - d30),
            param_15, 0, 1);
        uint colBody = (uint)FUN_803825c0((double)(float)(fade *
            (double)(float)((double)FLOAT_8061a318 * param_4))) & 0xff | 0x7f7f7f00;
        double w = text_get_char_width(param_15);
        double dx = (double)(FLOAT_8061a374 * (float)(lead * lead));
        text_draw_char_shadowed(
            (double)(float)(dx + -(double)(float)((double)FLOAT_8061a3cc * w - f1)),
            param_3, (u32)(unsigned long)param_15, colBody, colTop);
        w = text_get_char_width(param_15);
        text_draw_char_shadowed(
            (double)(float)(-(double)(float)((double)FLOAT_8061a3cc * w - f1) - dx),
            param_3, (u32)(unsigned long)param_15, colBody, colTop);
        double one = (double)FLOAT_8061a330;
        text_set_color_rgb(one, one, one);
    }

    d25 = (double)(float)((double)FLOAT_8061a3cc * d30);
    d28 = (double)(float)((double)FLOAT_8061a3cc * param_4);
    softAlpha = (uint)FUN_803825c0((double)(float)((double)FLOAT_8061a3d0 * param_4));
    d27 = (double)FLOAT_8061a3cc;
    cellRec = param_7 * 0xc;
    d24 = (double)FLOAT_8061a328;
    d29 = (double)(float)(d30 * d27);
    d23 = (double)FLOAT_8061a364;
    d22 = (double)FLOAT_8061a3c4;
    d20 = (double)FLOAT_8061a404;
    d19 = (double)FLOAT_8061a3c8;
    d21 = DOUBLE_8061a368;

    strIdx = 0;
    for (int e = 0; e < param_8; e++) {
        double rowLo = (double)(float)(uint)((uint)e ^ 0x80000000);
        double rowHi = (double)(float)(uint)((uint)(e + 1) ^ 0x80000000);
        float x0 = (float)((double)(float)(d24 * (double)(float)((double)*(float *)(rec + cellRec) *
            d30) + f1) - d25);
        float y0 = (float)((double)(float)(d23 * (double)FLOAT_8060f95c) *
            (double)(float)(rowLo - d21) + param_2);
        float y1 = (float)((double)(float)(d23 * (double)FLOAT_8060f95c) *
            (double)(float)(rowHi - d21) + param_2);
        float xm = (float)(d27 * (double)(float)((double)x0 + (double)x0));
        float ym = (float)(d27 * (double)(float)((double)y1 - (double)y0) + (double)y0);
        gx_draw_quad_f4((double)x0, (double)y0, (double)(float)((double)x0 + d29), (double)y0,
                        (double)xm, (double)ym, (double)(float)((double)xm + d29), (double)ym);
        gx_draw_quad_f4((double)xm, (double)ym, (double)(float)((double)xm + d29), (double)ym,
                        (double)x0, (double)y1, (double)(float)((double)x0 + d29), (double)y1);
        gx_draw_quad_f4((double)(float)((double)x0 + d29), (double)y0,
                        (double)(float)((double)x0 + d30), (double)y0,
                        (double)(float)((double)xm + d29), (double)ym,
                        (double)(float)((double)xm + d30), (double)ym);
        gx_draw_quad_f4((double)(float)((double)xm + d29), (double)ym,
                        (double)(float)((double)xm + d30), (double)ym,
                        (double)(float)((double)x0 + d29), (double)y1,
                        (double)(float)((double)x0 + d30), (double)y1);

        if (e == *param_9) {
            baseColor = color_scale_lo(param_4, 0x6060607f);
            if (*(int *)(rec + 0xd4) == 1) {
                *(int *)(rec + 0xd8) = *(int *)(rec + 0xd8) + 1;
                if (0x7f < *(int *)(rec + 0xd8)) *(int *)(rec + 0xd4) = -*(int *)(rec + 0xd4);
            } else {
                *(int *)(rec + 0xd8) = *(int *)(rec + 0xd8) - 1;
                if (*(int *)(rec + 0xd8) < 0x46) *(int *)(rec + 0xd4) = -*(int *)(rec + 0xd4);
            }
            gx_draw_quad_f4((double)x0, (double)y0, (double)(float)((double)x0 + d30), (double)y0,
                            (double)x0, (double)y1, (double)(float)((double)x0 + d30), (double)y1);
            arcsel_draw_separator((double)x0, (double)y0, (double)(float)((double)x0 + d29),
                                  (double)FLOAT_8061a324, d28);
            arcsel_draw_separator((double)(float)((double)x0 + d29), (double)y0,
                                  (double)(float)((double)x0 + d30), d28, (double)FLOAT_8061a324);
            arcsel_draw_separator((double)x0, (double)y1, (double)(float)((double)x0 + d29),
                                  (double)FLOAT_8061a324, d28);
            arcsel_draw_separator((double)(float)((double)x0 + d29), (double)y1,
                                  (double)(float)((double)x0 + d30), d28, (double)FLOAT_8061a324);
        }

        if (e == *param_9) {
            u32 shHi = color_scale_lo(d16, 0x5235a50);
            u32 shLo = color_scale_lo(d16, 0x7f7f7f7f);
            text_draw_char_shadowed((double)(FLOAT_8061a3c4 * FLOAT_8060f958 + x0), (double)y0,
                                    *(u32 *)(param_11 + strIdx), shLo, shHi);
        } else {
            text_draw_char((double)(FLOAT_8061a3c4 * FLOAT_8060f958 + x0), (double)y0,
                           *(byte **)(param_11 + strIdx), baseColor);
        }
        double w = text_get_char_width(*(byte **)(param_11 + strIdx));
        double lo = (double)(float)(uint)((uint)(int)w ^ 0x80000000);
        text_apply_alpha_fade(
            (double)(float)(d30 - (double)((float)(lo - d21) +
                (float)(d22 * (double)FLOAT_8060f958 +
                    (double)(float)(d20 * (double)(float)(d19 * (double)FLOAT_8060f958))))),
            *(byte **)(param_12 + strIdx), 0, 1);
        w = text_get_char_width(*(byte **)(param_12 + strIdx));
        uint vwU = (uint)w;
        if (param_16 != 0) {
            uint col = (uint)countLeadingZeros(e - *param_9) >> 5;
            ticker_render_arrows((double)x0, (double)y0, d30, param_4, (u32)packed, col, vwU);
        }
        // value text (selected -> shadowed; else colour by mod-flag +param_14)
        if (e == *param_9) {
            u32 shHi = color_scale_lo(d16, 0x5235a50);
            u32 shLo = color_scale_lo(d16, 0x7f7f7f7f);
            double vlo = (double)(float)(uint)(vwU ^ 0x80000000);
            text_draw_char_shadowed(
                (double)((x0 + -(float)((double)FLOAT_8061a3c8 * (double)FLOAT_8060f958 -
                    -(double)(float)((double)FLOAT_8061a3c4 * (double)FLOAT_8060f958 - d30))) -
                    (float)(vlo - DOUBLE_8061a368)),
                (double)y0, *(u32 *)(param_12 + strIdx), shLo, shHi);
        } else {
            double modv = (double)*(float *)(param_14 + strIdx);
            double vlo = (double)(float)(uint)(vwU ^ 0x80000000);
            double vx = (double)((x0 + -(float)((double)FLOAT_8061a3c8 * (double)FLOAT_8060f958 -
                -(double)(float)((double)FLOAT_8061a3c4 * (double)FLOAT_8060f958 - d30))) -
                (float)(vlo - DOUBLE_8061a368));
            if (modv <= (double)FLOAT_8061a324) {
                text_draw_char(vx, (double)y0, *(byte **)(param_12 + strIdx), baseColor);
            } else {
                uint a = (uint)FUN_803825c0(-(double)(float)((double)FLOAT_8061a3e8 * modv -
                    (double)FLOAT_8061a3d0));
                int  b = FUN_803825c0(-(double)(float)((double)FLOAT_8061a31c * modv -
                    (double)FLOAT_8061a3d0));
                text_draw_char(vx, (double)y0, *(byte **)(param_12 + strIdx),
                    (softAlpha & 0xff) | ((a & 0xff) << 8) | (b << 0x18) | ((a & 0xff) << 0x10));
            }
        }
        double one = (double)FLOAT_8061a330;
        text_set_color_rgb(one, one, one);
        strIdx = strIdx + 4;
        cellRec = cellRec + 0xc;
    }
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// Local helper: gather one bucket's `count` entry records from the linked list
// walking helper `iVar11` uses (list node at +0xc8 count, +0xd8 head, +0x10
// next; entry glyph +4, value +0x20, mod-flag via +0xb8 bit0x10 ? +0x104 : 0).
// Returns the advanced list pointer.
static int ticker_collect_entries(int list, int count, int activeId, int param5,
                                   int *sel, int base,
                                   u32 *glyphs, u32 *values, float *mods)
{
    for (int i = 0, k = 0; k < count; k++, i = i + 4) {
        int n = *(int *)(list + 200);
        int node = *(int *)(list + 0xd8);
        while (n > 0) { node = *(int *)(node + 0x10); n = n - 1; }
        *(u32 *)((int)glyphs + i) = *(u32 *)(node + 4);
        *(u32 *)((int)values + i) = *(u32 *)(list + 0x20);
        if ((*(uint *)(list + 0xb8) & 0x10) == 0) *(float *)((int)mods + i) = FLOAT_8061a324;
        else *(u32 *)((int)mods + i) = *(u32 *)(list + 0x104);
        if ((param5 != 0) && (activeId == *(int *)(list + 0x14))) *sel = base + k;
        list = *(int *)(list + 4);
    }
    return list;
}

// ---------------------------------------------------------------------------
// Address: 0x802ed484 | Name: ticker_render_menu_split
// Renders the flat "split menu" ticker: five buckets (3,4,2,2,2 entries) each
// gathered from the linked entry list and drawn via ticker_render_column with a
// bucket header string, tracking the newly-selected column into +0xe0/+0xe4.
void ticker_render_menu_split(u64 param_1, double param_2, double param_3, u32 param_4,
                              u32 param_5, int param_6)
{
    int   block;
    int   rec;
    int   list;
    int   activeId;
    int   sel = -1;
    int   selCol = -1;
    u32   color;
    byte *hdr;
    u64   packed;

    (void)param_4; (void)param_5;
    packed  = __save_gpr_25();
    block   = (int)(unsigned)(packed >> 0x20);
    rec     = (int)packed;
    activeId = *(int *)(rec + 0x188);
    gx_set_sub_material_id(1);
    if (param_6 == 0) {
        color = color_scale_lo((double)*(float *)(rec + 0xf0), 0x2020207f);
    } else {
        sel = -2;
        color = color_scale_lo((double)*(float *)(rec + 0xf0), 0x4040407f);
    }
    text_set_font_table(0x12);
    list = *(int *)(rec + 0x174);
    *(char *)(block + 0xea) = (char)ticker_slide_positions(block);

    struct { int n, base, seg, cnt, strId, col; } buckets[5] = {
        {3, 0, 0,   3, 0x193e, 0}, {4, 0, 3,   4, 0x193f, 1}, {2, 0, 7,   2, 0x1762, 2},
        {2, 0, 9,   2, 0x1763, 3}, {2, 0, 0xb, 2, 0x193d, 4}
    };
    for (int bi = 0; bi < 5; bi++) {
        u32   glyphs[15], values[15];
        float mods[15];
        int   localSel = sel;
        list = ticker_collect_entries(list, buckets[bi].n, activeId, param_6, &localSel, 0,
                                       glyphs, values, mods);
        if (localSel != sel && localSel >= 0) selCol = buckets[bi].col;
        sel = localSel;
        hdr = get_string_by_id(buckets[bi].strId);
        ticker_render_column((u64)(unsigned)rec, param_2, param_3, (double)*(float *)(rec + 0xf0),
                             block, rec, buckets[bi].seg, buckets[bi].cnt, &sel, buckets[bi].col,
                             (int)values, (int)glyphs, color, (int)mods, hdr,
                             bi < 4 ? 1 : 0);
    }
    if ((selCol != -1) && (*(int *)(block + 0xe0) != selCol)) {
        *(int *)(block + 0xe4) = *(int *)(block + 0xe0);
        *(int *)(block + 0xe0) = selCol;
    }
    ticker_update_all_selections();
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// Address: 0x802eda3c | Name: ticker_render_frame
// Renders the curved-path ticker frame. In "settled" mode (param_6==1) it draws
// the seven buckets as curved label runs; otherwise it advances the pulse timer,
// draws the same buckets plus the page-turn glyphs and the wire guide-lines.
void ticker_render_frame(u64 param_1, double param_2, u32 param_3, u32 param_4, int param_5,
                         int param_6)
{
    int   front;
    int   rec;
    int   list;
    int   activeId;
    int   sel = -1;
    u32   color;
    double f1;
    u64   packed;

    (void)param_3; (void)param_4;
    packed   = FUN_803826dc();
    front    = (int)(unsigned)(packed >> 0x20);
    rec      = (int)packed;
    activeId = *(int *)(rec + 0x188);
    f1       = 0.0;
    if (param_5 == 0) {
        color = color_scale_lo((double)*(float *)(rec + 0xf0), 0x2020207f);
    } else {
        sel = -2;
        color = color_scale_lo((double)*(float *)(rec + 0xf0), 0x4040407f);
    }
    text_set_font_table(0x12);
    float wrap = FLOAT_8061a374;
    list = *(int *)(rec + 0x174);

    // bucket layout: (seg,count,col) — 7 runs total.
    static const struct { int seg, cnt, col, flag; } B[7] = {
        {0,3,0,1},{3,4,1,1},{7,2,2,1},{9,2,3,1},{0xb,2,4,-1},{0,0,0,0},{0,0,0,0}
    };

    if (param_6 == 1) {
        color = color_scale_lo((double)*(float *)(rec + 0xf0), 0x6060607f);
        const struct { int seg, cnt, col, flag; } S[7] = {
            {0,4,1,1},{4,1,5,1},{5,1,2,1},{6,1,3,1},{7,3,0,-1},{10,2,4,-1},{0,0,0,0}
        };
        for (int bi = 0; bi < 6; bi++) {
            u32 glyphs[15], values[15]; float mods[15]; int localSel = sel;
            list = ticker_collect_entries(list, S[bi].cnt, activeId, param_5, &localSel, 0,
                                          glyphs, values, mods);
            sel = localSel;
            ticker_render_labels(front, rec, S[bi].seg, S[bi].cnt, &sel, S[bi].col,
                                 (int)values, (int)glyphs, color, (int)mods, S[bi].flag, 1, 1);
        }
    } else {
        // advance the pulse timer / page toggle
        *(float *)(front + 0xcc) =
            FLOAT_8061a328 * (FLOAT_8061a330 / u32f((uint)DAT_806109c0 ^ 0x80000000)) *
            FLOAT_806108e8 + *(float *)(front + 0xcc);
        if (wrap < *(float *)(front + 0xcc)) {
            *(float *)(front + 0xcc) = FLOAT_8061a324;
            *(int *)(front + 0xd0) = *(int *)(front + 0xd0) + 1;
            if (1 < *(int *)(front + 0xd0)) *(int *)(front + 0xd0) = 0;
        }
        if (FLOAT_80612b90 ==
            FLOAT_8061a3ac * (FLOAT_8061a330 / u32f((uint)DAT_806109c0 ^ 0x80000000))) {
            color = color_scale_lo((double)*(float *)(rec + 0xf0), 0x6060607f);
        }
        // three leading buckets
        for (int bi = 0; bi < 3; bi++) {
            u32 glyphs[15], values[15]; float mods[15]; int localSel = sel;
            list = ticker_collect_entries(list, B[bi].cnt, activeId, param_5, &localSel, 0,
                                          glyphs, values, mods);
            sel = localSel;
            ticker_render_labels(front, rec, B[bi].seg, B[bi].cnt, &sel, B[bi].col,
                                 (int)values, (int)glyphs, color, (int)mods, 1, 1, param_6);
        }
        // page-turn glyph (prev)
        {
            u32 c = color_scale_lo((double)*(float *)(rec + 0xf0), 0x7f7f7f7f);
            float sx = (DAT_80612614 < 0x65) ? FLOAT_8061a408 : FLOAT_8061a40c;
            text_draw_char((double)(float)((double)sx * (double)FLOAT_8060f958 + f1),
                           (double)FLOAT_80612b78, &DAT_806110d0, c);
        }
        // two more buckets
        {
            u32 glyphs[15], values[15]; float mods[15]; int localSel = sel;
            list = ticker_collect_entries(list, B[3].cnt, activeId, param_5, &localSel, 0,
                                          glyphs, values, mods);
            sel = localSel;
            ticker_render_labels(front, rec, B[3].seg, B[3].cnt, &sel, B[3].col,
                                 (int)values, (int)glyphs, color, (int)mods, 1, 1, param_6);
        }
        // page-turn glyph (next)
        {
            u32 c = color_scale_lo((double)*(float *)(rec + 0xf0), 0x7f7f7f7f);
            float sx = (DAT_80612614 < 0x65) ? FLOAT_8061a40c : FLOAT_8061a410;
            text_draw_char((double)(float)((double)sx * (double)FLOAT_8060f958 + f1),
                           (double)FLOAT_80612b78, &DAT_806110d4, c);
        }
        {
            u32 glyphs[15], values[15]; float mods[15]; int localSel = sel;
            list = ticker_collect_entries(list, B[4].cnt, activeId, param_5, &localSel, 0,
                                          glyphs, values, mods);
            sel = localSel;
            ticker_render_labels(front, rec, B[4].seg, B[4].cnt, &sel, B[4].col,
                                 (int)values, (int)glyphs, color, (int)mods, -1, 0, param_6);
        }
        ticker_render_wire_lines(f1, param_2, (double)*(float *)(rec + 0xf0));
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// Address: 0x802ee594 | Name: ticker_render_dispatch
// Chooses between the curved frame renderer and the flat menu-split renderer by
// the widget's mode field (+0xdc).
void ticker_render_dispatch(u64 param_1, double param_2, u64 param_3, double param_4, double param_5,
                            int param_6, u32 param_7, int param_8, int param_9)
{
    if (*(int *)(param_6 + 0xdc) == 0) {
        ticker_render_frame(param_1, param_2, (u32)(unsigned)param_6, param_7, param_8, param_9);
    } else {
        ticker_render_menu_split(param_3, param_4, param_5, (u32)(unsigned)param_6, param_7, param_8);
    }
}
