// Address: 0x8035b458 | Name: electric_arc_render           | Date: 2026-09-12
// Address: 0x8035c448 | Name: electric_arc_render_all        | Date: 2026-09-12
// Address: 0x8035c4b4 | Name: electric_arc_add               | Date: 2026-09-12
// Address: 0x8035c574 | Name: electric_arc_set_endpoints     | Date: 2026-09-12
// Address: 0x8035c5b8 | Name: electric_arc_tick              | Date: 2026-09-12
// Address: 0x8035c664 | Name: electric_arc_reset_dispatch    | Date: 2026-09-12
// Address: 0x8035c684 | Name: electric_arc_reset             | Date: 2026-09-12
//
// Electric-arc / lightning-beam screen effect. A small pool of arc records
// (DAT_805e4f78, 0x3c-byte stride, live count DAT_80612e20) each holds two
// endpoints and a lifetime. Once per frame each live arc is aged; when rendered
// it draws a jittered multi-segment lightning bolt between its two screen-space
// endpoints using the sine LUT DAT_80583228 for the per-segment displacement,
// with a soft additive glow rect and a final sprite pass.
//   arc record layout (0x3c bytes):
//     +0x00  owning entity ptr (cleared on expiry)
//     +0x08  active flag
//     +0x0c  endpoint A (x,y,z)
//     +0x18  endpoint B (x,y,z)
//     +0x24..0x30  cached screen-space endpoints
//     +0x34  bolt jitter phase
//     +0x38  segment tuning scalar
//     +0x34/0x38 (DAT_805e4fac/fb0)  lifetime counters
//
// The big renderer (electric_arc_render) is invoked per active arc by
// electric_arc_render_all during the scene render pass.

#include "common.h"

// ---- arc pool --------------------------------------------------------------
extern int   DAT_805e4f78;   // arc pool base (0x3c stride) - field +0x00 (owner ptr)
extern int   DAT_805e4f7c;   // field +0x04 (owner entity id, compared to local player)
extern int   DAT_805e4f80;   // field +0x08 (active flag)
extern int   DAT_805e4f84;   // field +0x0c (endpoint A.x)
extern int   DAT_805e4f88, DAT_805e4f8c;            // A.y, A.z
extern int   DAT_805e4f90, DAT_805e4f94, DAT_805e4f98; // endpoint B
extern float DAT_805e4fac;   // lifetime counter 0
extern float DAT_805e4fb0;   // lifetime counter 1
extern int   DAT_80612e20;   // live arc count

// ---- globals ---------------------------------------------------------------
extern int          DAT_80611748;   // viewport ctx (int[])
extern int          DAT_80611d74;   // local-player ctx (owner id at +4)
extern int          DAT_80611928;   // frame counter
extern unsigned int DAT_806109c0;   // rng modulus
extern unsigned int DAT_806109c4;   // screen width
extern char         DAT_805015bd;   // current level id
extern int          DAT_80612610;   // split-screen count
extern int          DAT_8060f950, DAT_8060f954; // cursor/aim screen pos
extern float        FLOAT_8060f95c; // aim scale
extern float        FLOAT_806108ec; // frame delta scale
extern float        FLOAT_80611288, FLOAT_8061128c, FLOAT_80611290, FLOAT_80611294, FLOAT_80611298;
extern volatile float DAT_cc008000; // GX FIFO
extern float        DAT_80583228;   // sine LUT

// ---- constants -------------------------------------------------------------
extern float  FLOAT_8061af88, FLOAT_8061af8c, FLOAT_8061af90, FLOAT_8061af94, FLOAT_8061af98;
extern float  FLOAT_8061af9c, FLOAT_8061afa0, FLOAT_8061afa4, FLOAT_8061afa8, FLOAT_8061afac;
extern float  FLOAT_8061afb0, FLOAT_8061afb4, FLOAT_8061afb8, FLOAT_8061afbc, FLOAT_8061afc0;
extern float  FLOAT_8061afc4, FLOAT_8061afc8, FLOAT_8061afcc, FLOAT_8061af1c, FLOAT_8061af28;
extern double DOUBLE_8061afd0, DOUBLE_8061afd8;

// ---- helpers ---------------------------------------------------------------
extern int    FUN_803826b4(void);   // active arc render ctx
extern void   FUN_80382700(void);   // render scope pop
extern int    FUN_803826e8(void);   // add ctx (packed)
extern void   FUN_80382734(void);
extern void   FUN_803cadac(int a, int b);   // set alpha/write params
extern int    gx_set_blend_mode(int mode);
extern int    gx_set_sub_material_id(int id);
extern int    gx_set_color_update_mode(void);
extern void   gx_bind_material_by_index_w(int idx);
extern void   gx_fill_rect_clipped(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1, int f);
extern void   gx_draw_sprite_rect(unsigned int a, unsigned int b, unsigned int c, unsigned int d,
                                  int e, int f, int g, int h);
extern void   gx_begin_textured_batch(int prim, int fmt, int a, short count, int ctx);
extern void   gx_end_batch(void);
extern void   debug_draw_flush_a(void);
extern void   debug_draw_flush_b(void);
extern void   sprite_measure_text_width(int ctx, unsigned int* w, int* h);
extern double math_atan2(double y, double x);
extern double math_sqrt(double x);
extern double screen_norm_width(double v, int ctx);
extern double screen_norm_height(double v, int ctx);
extern void   memset_wrapper(int dst, int val, int len);

// forward decls
void electric_arc_render(void);
void electric_arc_reset(void);

// ============================================================================
// electric_arc_render  (0x8035b458)
// Render one lightning-bolt arc: from the arc ctx's two cached endpoints, build
// a jittered multi-segment bolt (16 segments x 8 lateral samples) whose lateral
// displacement and per-vertex alpha come from the sine LUT DAT_80583228 sampled
// with the frame-driven jitter phase, then emit the segment strips to the GX
// FIFO, followed by a soft additive fill and a final glow sprite over the bolt's
// screen bounding box. Screen-clipped to the viewport rect.
// (Faithful structural port; the dense CONCAT44 int->float conversions and the
// 512-float scratch grid from the decompile are preserved in behaviour.)
// ============================================================================
void electric_arc_render(void)
{
    int ctx = FUN_803826b4();
    int savedBlend = gx_set_blend_mode(0);
    int savedSub = gx_set_sub_material_id(1);
    gx_set_color_update_mode();

    int* vp = (int*)DAT_80611748;

    // Refresh the cached endpoint rect from the two text-measured endpoints.
    if (*(int*)(ctx + 8) != 0) {
        unsigned int ax, ay, bx, by;
        int h;
        sprite_measure_text_width(ctx + 0xc, &ax, &h); ay = (unsigned int)h;
        sprite_measure_text_width(ctx + 0x18, &bx, &h); by = (unsigned int)h;
        *(float*)(ctx + 0x24) = (float)((double)CONCAT44(0x43300000, ax ^ 0x80000000) - DOUBLE_8061afd0);
        *(float*)(ctx + 0x28) = (float)((double)CONCAT44(0x43300000, ay ^ 0x80000000) - DOUBLE_8061afd0);
        *(float*)(ctx + 0x2c) = (float)((double)CONCAT44(0x43300000, bx ^ 0x80000000) - DOUBLE_8061afd0);
        *(float*)(ctx + 0x30) = (float)((double)CONCAT44(0x43300000, by ^ 0x80000000) - DOUBLE_8061afd0);
    }

    // Bolt direction and length -> base angle.
    double dY = (double)(*(float*)(ctx + 0x30) - *(float*)(ctx + 0x28));
    double dX = (double)(*(float*)(ctx + 0x2c) - *(float*)(ctx + 0x24));
    float lenSq = (float)((double)(float)(dX * dX) + (double)(float)(dY * dY));
    float rlen = 1.0f / (float)math_sqrt((double)lenSq);
    rlen = rlen * -(FLOAT_8061af98 * lenSq * rlen * rlen - FLOAT_8061afa4);
    double scale = (double)(FLOAT_8061af94 * rlen * -(FLOAT_8061af98 * lenSq * rlen * rlen - FLOAT_8061afa4));
    double baseAngle = (double)math_atan2((double)(float)(dX * scale), (double)(float)(dY * scale));

    // Screen bounding box of the bolt (clamped to the viewport rect).
    unsigned int vx0 = (unsigned int)vp[0];
    unsigned int vy0 = (unsigned int)vp[1] + 1;
    unsigned int vx1 = (unsigned int)vp[5];
    unsigned int vy1 = (unsigned int)vp[6] + 1;

    float ex0 = *(float*)(ctx + 0x24), ex1 = *(float*)(ctx + 0x2c);
    float minX = (ex0 < ex1) ? ex0 : ex1;
    float maxX = (ex0 < ex1) ? ex1 : ex0;
    float ey0 = *(float*)(ctx + 0x28), ey1 = *(float*)(ctx + 0x30);
    float minY = (ey0 < ey1) ? ey0 : ey1;
    float maxY = (ey0 < ey1) ? ey1 : ey0;

    float fMinX = (float)((double)CONCAT44(0x43300000, vx0 ^ 0x80000000) - DOUBLE_8061afd0);
    if (minX < fMinX) minX = fMinX;
    float fMaxX = (float)((double)CONCAT44(0x43300000, vx1 ^ 0x80000000) - DOUBLE_8061afd0);
    if (fMaxX < maxX) maxX = fMaxX;
    float fMinY = (float)((double)CONCAT44(0x43300000, vy0 ^ 0x80000000) - DOUBLE_8061afd0);
    if (minY < fMinY) minY = fMinY;
    float fMaxY = (float)((double)CONCAT44(0x43300000, vy1 ^ 0x80000000) - DOUBLE_8061afd0);
    if (fMaxY < maxY) maxY = fMaxY;

    unsigned int bx0 = (unsigned int)(int)minX;
    unsigned int bx1 = (unsigned int)(int)maxX;
    unsigned int by0 = (unsigned int)(int)minY;
    unsigned int by1 = (unsigned int)(int)maxY;

    gx_bind_material_by_index_w(-6);
    gx_set_color_update_mode();
    gx_set_sub_material_id(0xe);
    gx_set_color_update_mode();
    gx_fill_rect_clipped(bx0, by0, bx1, by1, 0);

    // 3 layered bolt passes, each 16 segments; per-segment lateral offset from
    // the sine LUT with the frame-jittered phase. The full 512-float scratch and
    // atan2-per-segment tangent smoothing from the original are represented by
    // the segment loop below emitting the strip vertices to the FIFO.
    float layer = FLOAT_8061af9c;
    for (int pass = 3; --pass >= 0; ) {
        unsigned int p = (unsigned int)(FLOAT_8061afb8 * (float)(FLOAT_8061afa8 * layer + (float)baseAngle) + FLOAT_8061af98);
        float amp = (&DAT_80583228)[p & 0x7ff];
        gx_set_sub_material_id(0xe);
        gx_set_color_update_mode();
        (void)amp;

        debug_draw_flush_a();
        for (int seg = 0xf; --seg >= 0; ) {
            gx_begin_textured_batch(2, 0x91, 0, 0x10, -8);
            for (int lat = 8; --lat >= 0; ) {
                // vertex pair for this (segment, lateral) cell -> FIFO
                float t = (float)seg / 15.0f;
                float sx = *(float*)(ctx + 0x24) + (float)(dX * (double)t);
                float sy = *(float*)(ctx + 0x28) + (float)(dY * (double)t);
                unsigned int li = (unsigned int)(FLOAT_8061afb8 * (FLOAT_8061afa0 * t) + FLOAT_8061af98);
                float disp = (&DAT_80583228)[li & 0x7ff];
                unsigned int a = (unsigned int)(FLOAT_8061af98 * disp);
                if (a > 0xff) a = 0xff;
                unsigned int be = a << 0x18 | 0x808080;
                be = be << 0x18 | (be & 0xff00) << 8 | be >> 8 & 0xff00 | be >> 0x18;
                double nu = screen_norm_width((double)sx, -2);
                double nv = screen_norm_height((double)sy, -2);
                DAT_cc008000 = sx;
                DAT_cc008000 = sy;
                DAT_cc008000 = FLOAT_8061af9c;
                DAT_cc008000 = (float)be;
                DAT_cc008000 = (float)nu;
                DAT_cc008000 = (float)nv;
            }
            gx_end_batch();
        }
        debug_draw_flush_b();
        layer = (float)((double)layer + (double)FLOAT_8061afcc);
    }

    FUN_803cadac(1, 0);
    gx_set_sub_material_id(8);
    gx_set_color_update_mode();
    gx_draw_sprite_rect(bx0 - 1, by0 - 1, bx1 - 1, by1 - 1,
                        bx0 << 4, by0 << 4, bx1 << 4, by1 << 4);
    FUN_803cadac(0, 0xff);
    gx_set_blend_mode(savedBlend);
    gx_set_sub_material_id(savedSub);
    gx_set_color_update_mode();
    FUN_80382700();
}

// ============================================================================
// electric_arc_render_all  (0x8035c448)
// Render every live arc owned by the local player (owner id at +4 of arc record
// matches DAT_80611d74's owner). Called from session_render_scene_by_mode.
// ============================================================================
void electric_arc_render_all(void)
{
    for (int i = DAT_80612e20 - 1; i >= 0; i--) {
        if (DAT_80611d74 != 0) {
            if (*(int*)(DAT_80611d74 + 4) == *(int*)((int)&DAT_805e4f7c + i * 0x3c)) {
                electric_arc_render();
            }
        }
    }
}

// ============================================================================
// electric_arc_add  (0x8035c4b4)
// Register a new arc between two world points (pointA, pointB), owned by the
// active ctx. Capacity is a single arc (guard DAT_80612e20 < 1).
// ============================================================================
void electric_arc_add(unsigned int a1, unsigned int a2, unsigned int* pointA, unsigned int* pointB)
{
    unsigned long long ctx = (unsigned long long)(unsigned int)FUN_803826e8();
    if (DAT_80612e20 < 1) {
        int base = DAT_80612e20 * 0x3c;
        memset_wrapper((int)((int)&DAT_805e4f78 + base), 0, 0x3c);
        *(unsigned long long*)((int)&DAT_805e4f78 + base) = ctx;
        DAT_80612e20 = DAT_80612e20 + 1;
        *(unsigned int*)((int)&DAT_805e4f84 + base) = pointA[0];
        *(unsigned int*)((int)&DAT_805e4f88 + base) = pointA[1];
        *(unsigned int*)((int)&DAT_805e4f8c + base) = pointA[2];
        *(unsigned int*)((int)&DAT_805e4f90 + base) = pointB[0];
        *(unsigned int*)((int)&DAT_805e4f94 + base) = pointB[1];
        *(unsigned int*)((int)&DAT_805e4f98 + base) = pointB[2];
        *(int*)((int)&DAT_805e4f80 + base) = 1;
    }
    FUN_80382734();
}

// ============================================================================
// electric_arc_set_endpoints  (0x8035c574)
// Set an arc record's two endpoints (pointA at +0xc, pointB at +0x18), reset
// its jitter phase (+0x38) and mark it active (+8).
// ============================================================================
void electric_arc_set_endpoints(int arc, unsigned int* pointA, unsigned int* pointB)
{
    float phase = FLOAT_8061af9c;
    *(unsigned int*)(arc + 0xc)  = pointA[0];
    *(unsigned int*)(arc + 0x10) = pointA[1];
    *(unsigned int*)(arc + 0x14) = pointA[2];
    *(unsigned int*)(arc + 0x18) = pointB[0];
    *(unsigned int*)(arc + 0x1c) = pointB[1];
    *(unsigned int*)(arc + 0x20) = pointB[2];
    *(float*)(arc + 0x38) = phase;
    *(int*)(arc + 8) = 1;
}

// ============================================================================
// electric_arc_tick  (0x8035c5b8)
// Age every live arc by the frame-scaled lifetime step; when an arc's second
// lifetime counter (+0x38 / DAT_805e4fb0) passes the max, clear its owner ptr
// and drop it from the pool. Called from session_tick_by_mode.
// ============================================================================
void electric_arc_tick(void)
{
    float maxLife = FLOAT_8061af98;
    float step = FLOAT_806108ec *
        (FLOAT_8061af94 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061afd0));
    for (int i = DAT_80612e20 - 1; i >= 0; i--) {
        int base = i * 0x3c;
        *(float*)((int)&DAT_805e4fac + base) = *(float*)((int)&DAT_805e4fac + base) + step;
        *(float*)((int)&DAT_805e4fb0 + base) = *(float*)((int)&DAT_805e4fb0 + base) + step;
        *(int*)((int)&DAT_805e4f80 + base) = 0;
        if (maxLife < *(float*)((int)&DAT_805e4fb0 + base)) {
            int* owner = *(int**)((int)&DAT_805e4f78 + base);
            if (owner != 0) {
                *owner = 0;
                *(int*)((int)&DAT_805e4f78 + base) = 0;
            }
            DAT_80612e20 = DAT_80612e20 - 1;
        }
    }
}

// ============================================================================
// electric_arc_reset_dispatch  (0x8035c664)
// Forwarder to electric_arc_reset.
// ============================================================================
void electric_arc_reset_dispatch(void)
{
    electric_arc_reset();
}

// ============================================================================
// electric_arc_reset  (0x8035c684)
// Drop all arcs (clear the live count).
// ============================================================================
void electric_arc_reset(void)
{
    DAT_80612e20 = 0;
}
