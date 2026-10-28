// =============================================================================
// snake3_boss_seg_render.cpp - render + geometry helpers for the snake3 boss
// spawned-segment eruption effect (companion to snake3_boss_segments.cpp).
//
// When a segment erupts it emits a 300-point particle spray plus an arc "beam"
// ribbon. The spray is driven by the 300-entry jitter table seeded by
// snake3_seg_seed_jitter (each entry is 3 floats: value / velocity / phase at
// 0xc stride). These functions advance that table, solve the nearest-approach
// distance of a swept ray against the arena rects, and render the spray/beam as
// GX billboard batches.
//
// Functions (all subsystem: game):
//   * snake3_seg_jitter_tick    (0x802b34a4) - integrate the 300-entry table.
//   * snake3_seg_closest_dist   (0x802b34fc) - swept-ray closest approach vs the
//     arena rect + 20 node rects; returns the clamped travel distance.
//   * snake3_seg_render_spray_a (0x802b3b9c) - render the 300-point spray using a
//     sin/cos table wobble and a per-point transform matrix.
//   * snake3_seg_render_spray_b (0x802b3fc8) - spray variant using math_poly_trig
//     for the angle and a phase offset argument.
//   * snake3_seg_render_beam    (0x802b43e4) - render the tapered arc beam ribbon.
//
// Address range: 0x802b34a4 - 0x802b47e3
// Subsystem: game (snake3 boss segment eruption render)
// =============================================================================

#include "common.h"

extern "C" {

extern const float FLOAT_80619ab8;  // 0.5
extern const float FLOAT_80619ad4;  // uv 1.0 (or edge)
extern const float FLOAT_80619adc;  // 0.0
extern const float FLOAT_80619b2c;  // floor plane
extern const float FLOAT_80619b34;  // 1.0
extern const float FLOAT_80619b40;
extern const float FLOAT_80619b78;  // gravity factor
extern const float FLOAT_80619b80;  // phase->index scale
extern const float FLOAT_80619b9c;  // jitter value clamp
extern const float FLOAT_80619ba0;  // big init distance
extern const float FLOAT_80619ba4;  // rect inset a
extern const float FLOAT_80619ba8;  // rect inset b
extern const float FLOAT_80619bac;  // spray radius base
extern const float FLOAT_80619bb0;  // colour scale B
extern const float FLOAT_80619bb4;  // colour scale G
extern const float FLOAT_80619bb8;  // colour scale R
extern const float FLOAT_80619bbc;  // beam segment count scale
extern const float FLOAT_80619bc0;  // beam length scale
extern const float FLOAT_80619bc4;  // beam angle step

extern float DAT_80583228[];   // sin/cos lookup table

// frame-enter / frame-leave intrinsics (scratch base as register pair)
extern unsigned long long FUN_803826d4(void);
extern unsigned long long FUN_803826d8(void);
extern void FUN_80382720(void);
extern void FUN_80382724(void);

extern double math_sqrt(double x);
extern double math_poly_trig(double x);
extern int    FUN_803825c0(double x);          // float->byte colour quantiser
extern unsigned int color_lerp(double t, unsigned int a, unsigned int b);
extern void   gx_set_blend_mode(int mode);
extern int    sprite_get_texture_id(int id);
extern void   gx_begin_batch_mode5(int prim, int a, int count, int tex);
extern void   gx_begin_textured_batch(int a, int prim, int b, int count, int tex);
extern void   gx_end_batch(void);

static volatile float        *const gx_fifo_f32 = (volatile float *)0xCC008000u;
static volatile unsigned int *const gx_fifo_u32 = (volatile unsigned int *)0xCC008000u;

// -----------------------------------------------------------------------------
// snake3_seg_jitter_tick - advance the 300-entry jitter table by one frame.
// Each entry: [0] value, [1] velocity, [2] secondary accumulator. The value
// wraps back to 0 when it exceeds the clamp, then integrates velocity; the
// accumulator adds a gravity-scaled slice of velocity.
// -----------------------------------------------------------------------------
void snake3_seg_jitter_tick(int base)
{
    float clamp = FLOAT_80619b9c;
    float grav  = FLOAT_80619b78;
    float zero  = FLOAT_80619adc;
    int off = 0;
    int n = 300;
    do {
        float *e = (float *)(base + off);
        if (clamp < e[0]) {
            e[0] = zero;
        }
        off = off + 0xc;
        e[0] = e[0] + e[1];
        e[2] = grav * e[1] + e[2];
        n = n - 1;
    } while (n != 0);
}

// -----------------------------------------------------------------------------
// snake3_seg_closest_dist - compute how far a swept ray (org + t*dir) can travel
// before its closest approach to the arena bounding rect (rect) or any of the
// 20 node rects (nodeList[i]) drops below the interaction radius. Returns the
// resulting travel distance, clamped so the ray never dips below the floor.
//
// nodeList : int[] of 20 pointers to 6-float node rects (x,y,minX,maxY,cx,cy)
// rect     : 4-float arena rect (minX,minY,maxX,maxY)
// org, dir : ray origin/direction (x,y,z as [0],[1],[2])
// -----------------------------------------------------------------------------
double snake3_seg_closest_dist(int nodeList, float *rect, float *org, float *dir)
{
    double best   = (double)FLOAT_80619ba0;
    double bestD2 = best;
    float dx0 = dir[0];
    float invY, slope;
    if (FLOAT_80619adc == dx0) {
        invY  = dir[1];
        slope = FLOAT_80619adc;
    } else {
        invY  = dir[1] / dx0;
        slope = dir[2] / dx0;
    }
    float invSlope = FLOAT_80619b34 / slope;
    float ox = org[0];
    float oz = org[2];
    float c  = -(ox * slope - oz);

    // arena rect: pick the entry edge by dir.x sign, then by dir.z sign
    if (FLOAT_80619adc <= dx0) {
        if (FLOAT_80619adc < dx0) {
            float hz = rect[2] * slope + c;
            if ((rect[1] <= hz) && (hz <= rect[3])) {
                float dxr = rect[2] - ox;
                double d2 = (double)(dxr * dxr + (hz - oz) * (hz - oz));
                if (d2 < (double)FLOAT_80619ba0) {
                    best   = (double)(float)((double)(invY * dxr) * (double)(invY * dxr) + d2);
                    bestD2 = d2;
                }
            }
        }
    } else {
        float hz = org[0] * slope + c;
        if ((rect[1] <= hz) && (hz <= rect[3])) {
            float dxr = org[0] - ox;
            double d2 = (double)(dxr * dxr + (hz - oz) * (hz - oz));
            if (d2 < (double)FLOAT_80619ba0) {
                best   = (double)(float)((double)(invY * dxr) * (double)(invY * dxr) + d2);
                bestD2 = d2;
            }
        }
    }
    float dz0 = dir[2];
    if (FLOAT_80619adc <= dz0) {
        if (FLOAT_80619adc < dz0) {
            float hx = invSlope * (rect[3] - c);
            if ((org[0] <= hx) && (hx <= rect[2])) {
                float dxr = hx - ox;
                float dzr = rect[3] - oz;
                double d2 = (double)(dxr * dxr + dzr * dzr);
                if (d2 < bestD2) {
                    best   = (double)(float)((double)(invY * dxr) * (double)(invY * dxr) + d2);
                    bestD2 = d2;
                }
            }
        }
    } else {
        float hx = invSlope * (rect[1] - c);
        if ((org[0] <= hx) && (hx <= rect[2])) {
            float dxr = hx - ox;
            float dzr = rect[1] - oz;
            double d2 = (double)(dxr * dxr + dzr * dzr);
            if (d2 < bestD2) {
                best   = (double)(float)((double)(invY * dxr) * (double)(invY * dxr) + d2);
                bestD2 = d2;
            }
        }
    }

    // 20 node rects: test each of the 4 inset edges (+/- ba4, +/- ba8)
    int off = 0;
    int left = 0x14;
    do {
        float *nr = *(float **)(nodeList + off);
        if ((((nr[4] < ox) && (dx0 < FLOAT_80619adc)) ||
             ((ox < nr[4]) && (FLOAT_80619adc < dx0))) &&
            (((nr[5] < oz) && (dz0 < FLOAT_80619adc)) ||
             ((oz < nr[5]) && (FLOAT_80619adc < dz0)))) {
            float minX = nr[0];
            float minY = nr[1];
            float ia = FLOAT_80619ba4;
            float ib = FLOAT_80619ba8;
            float loX = ia + minX;
            float loY = ia + minY;
            // eight inset-edge intersection candidates (ba4 ring then ba8 ring)
            float cand[16];
            cand[0]  = loX;                cand[1]  = loY;
            cand[2]  = nr[2] - ia;         cand[3]  = nr[3] - ia;
            cand[4]  = ib + minX;          cand[5]  = ib + minY;
            cand[6]  = nr[2] - ib;         cand[7]  = nr[3] - ib;
            cand[8]  = minX;               cand[9]  = minY;
            cand[10] = nr[2];              cand[11] = nr[3];
            cand[12] = ia;                 cand[13] = ib;
            cand[14] = loX;                cand[15] = loY;
            for (int e = 0; e < 12; ++e) {
                // horizontal edge candidate at cand row
                float ex = cand[(e & 7)];
                float hz = ex * slope + c;
                float dxr = ex - ox;
                double d2 = (double)(dxr * dxr + (hz - oz) * (hz - oz));
                if (d2 < bestD2) {
                    best   = (double)(float)((double)(invY * dxr) * (double)(invY * dxr) + d2);
                    bestD2 = d2;
                }
            }
        }
        off = off + 4;
        left = left - 1;
    } while (left != 0);

    double dist = math_sqrt(best);
    if ((double)(float)(dist * (double)dir[1] + (double)org[1]) < (double)FLOAT_80619b2c) {
        dist = (double)(float)((double)(float)((double)FLOAT_80619b2c - (double)org[1]) / (double)dir[1]);
    }
    return dist;
}

// Shared helper: emit one spray point's 4-vertex quad given the transformed
// position and packed colour, to the GX FIFO.
static void seg_spray_emit_point(double px, double py, double pz,
                                 unsigned int color, float u0, float v0)
{
    gx_fifo_f32[0] = (float)px;
    gx_fifo_f32[0] = (float)py;
    gx_fifo_f32[0] = (float)pz;
    gx_fifo_u32[0] = color;
    gx_fifo_f32[0] = u0;
    gx_fifo_f32[0] = v0;
}

// -----------------------------------------------------------------------------
// snake3_seg_render_spray_a - render the 300-point eruption spray. Each point is
// placed by a per-point transform matrix (columns at table[0/4/8], [1/5/9]) with
// a sin/cos wobble from DAT_80583228 sized by the entry's decaying value; colour
// fades on the same value through the three colour scales.
//
// The scratch base + per-point table come from the frame-enter register pair;
// the origin f1 is an unrecoverable spill modelled as 0 (documented). The exact
// per-quad FIFO fan is condensed into a structural emit loop over the typed
// volatile port (compile-clean, non-matching).
// -----------------------------------------------------------------------------
void snake3_seg_render_spray_a(float *origin, float *dir)
{
    unsigned long long pair = FUN_803826d4();
    float *mtx = (float *)(unsigned int)pair;
    int base = (int)(pair >> 0x20);
    double m00 = (double)mtx[0], m10 = (double)mtx[4], m20 = (double)mtx[8];
    double m01 = (double)mtx[1], m11 = (double)mtx[5], m21 = (double)mtx[9];
    double f1 = 0.0;   // origin spill (unrecoverable)

    gx_set_blend_mode(2);
    int tex = sprite_get_texture_id(0x28d);
    gx_begin_batch_mode5(0x92, 0, 0x4b0, tex);
    float phaseScale = FLOAT_80619b80;
    float half = FLOAT_80619ab8;
    double radius = (double)FLOAT_80619bac;
    double one = (double)FLOAT_80619b34;
    double scaleR = (double)FLOAT_80619bb8;
    double scaleG = (double)FLOAT_80619bb4;
    double scaleB = (double)FLOAT_80619bb0;

    int i = 0;
    int off = 0;
    do {
        float *e = (float *)(base + off);
        unsigned int idx = (unsigned int)(phaseScale * e[2] + half);
        double fade = (double)(float)(one + (double)(float)(one - (double)e[1]));
        float amp = (float)(radius * (double)(float)(one - (double)e[1]));
        float s = amp * DAT_80583228[idx & 0x7ff];
        float cco = amp * *(float *)((int)&DAT_80583228[0] + ((idx + 0x200) * 4 & 0x1ffc));

        unsigned int r = (unsigned int)FUN_803825c0((double)(float)(scaleR * fade)) & 0xff;
        int g = FUN_803825c0((double)(float)(scaleG * fade));
        unsigned int b = (unsigned int)FUN_803825c0((double)(float)(scaleB * fade));
        unsigned int color = (unsigned int)g << 0x18 | r << 0x10 | (b & 0xff) << 8 | 0x7f;

        double t = (double)e[0];
        double ox, oy, oz;
        if (f1 <= t) {
            ox = f1 * (double)dir[0] + (double)origin[0];
            oy = f1 * (double)dir[1] + (double)origin[1];
            oz = f1 * (double)dir[2] + (double)origin[2];
        } else {
            ox = (double)dir[0] * t + (double)origin[0];
            oy = (double)dir[1] * t + (double)origin[1];
            oz = (double)dir[2] * t + (double)origin[2];
        }
        // the four wobble corners transformed by the matrix columns
        double wx = (double)(s * (float)m00 + cco * (float)m01);
        double wy = (double)(s * (float)m10 + cco * (float)m11);
        double wz = (double)(s * (float)m20 + cco * (float)m21);
        seg_spray_emit_point(ox + wx, oy + wy, oz + wz, color, FLOAT_80619ad4, FLOAT_80619ad4);
        seg_spray_emit_point(ox - wx, oy - wy, oz - wz, color, FLOAT_80619ad4, FLOAT_80619b34);
        seg_spray_emit_point(ox + wz, oy + wx, oz + wy, color, FLOAT_80619b34, FLOAT_80619b34);
        seg_spray_emit_point(ox - wz, oy - wx, oz - wy, color, FLOAT_80619b34, FLOAT_80619ad4);
        i = i + 1;
        off = off + 0xc;
    } while (i < 300);
    gx_end_batch();
    FUN_80382720();
}

// -----------------------------------------------------------------------------
// snake3_seg_render_spray_b - identical spray layout to _a but the wobble angle
// comes from math_poly_trig with a per-frame phase offset (phase) rather than
// the direct sin/cos table lookup, giving a slightly different swirl.
// -----------------------------------------------------------------------------
void snake3_seg_render_spray_b(double phase, float *origin, float *dir)
{
    unsigned long long pair = FUN_803826d8();
    float *mtx = (float *)(unsigned int)pair;
    int base = (int)(pair >> 0x20);
    double m00 = (double)mtx[0], m10 = (double)mtx[4], m20 = (double)mtx[8];
    double m01 = (double)mtx[1], m11 = (double)mtx[5], m21 = (double)mtx[9];
    double f1 = 0.0;

    gx_set_blend_mode(2);
    int tex = sprite_get_texture_id(0x28d);
    gx_begin_batch_mode5(0x92, 0, 0x4b0, tex);
    float radiusBase = FLOAT_80619bac;
    float angleBias  = FLOAT_80619b40;
    double one = (double)FLOAT_80619b34;
    double scaleR = (double)FLOAT_80619bb8;
    double scaleG = (double)FLOAT_80619bb4;
    double scaleB = (double)FLOAT_80619bb0;

    int i = 0;
    int off = 0;
    do {
        float *e = (float *)(base + off);
        double amp = (double)(radiusBase * (float)(one - (double)e[1]));
        double c = math_poly_trig((double)(e[2] - angleBias));
        double sxz = (double)(float)(amp * c);
        double c2 = math_poly_trig((double)e[2]);
        double syz = (double)(float)(amp * c2);
        double fade = (double)(float)(one + (double)(float)(one - (double)e[1]));

        unsigned int r = (unsigned int)FUN_803825c0((double)(float)(scaleR * fade)) & 0xff;
        int g = FUN_803825c0((double)(float)(scaleG * fade));
        unsigned int b = (unsigned int)FUN_803825c0((double)(float)(scaleB * fade));
        unsigned int color = (unsigned int)g << 0x18 | r << 0x10 | (b & 0xff) << 8 | 0x7f;

        double t = (double)(float)(phase + (double)e[0]);
        double ox, oy, oz;
        if (f1 <= t) {
            ox = f1 * (double)dir[0] + (double)origin[0];
            oy = f1 * (double)dir[1] + (double)origin[1];
            oz = f1 * (double)dir[2] + (double)origin[2];
        } else {
            ox = t * (double)dir[0] + (double)origin[0];
            oy = t * (double)dir[1] + (double)origin[1];
            oz = t * (double)dir[2] + (double)origin[2];
        }
        double wx = (double)(float)(sxz * m00 + (double)(float)(syz * m01));
        double wy = (double)(float)(sxz * m10 + (double)(float)(syz * m11));
        double wz = (double)(float)(sxz * m20 + (double)(float)(syz * m21));
        seg_spray_emit_point(ox + wx, oy + wy, oz + wz, color, FLOAT_80619ad4, FLOAT_80619ad4);
        seg_spray_emit_point(ox - wx, oy - wy, oz - wz, color, FLOAT_80619ad4, FLOAT_80619b34);
        seg_spray_emit_point(ox + wz, oy + wx, oz + wy, color, FLOAT_80619b34, FLOAT_80619b34);
        seg_spray_emit_point(ox - wz, oy - wx, oz - wy, color, FLOAT_80619b34, FLOAT_80619ad4);
        i = i + 1;
        off = off + 0xc;
    } while (i < 300);
    gx_end_batch();
    FUN_80382724();
}

// -----------------------------------------------------------------------------
// snake3_seg_render_beam - render the tapered arc "beam" ribbon: up to 12
// segments of a spiral traced by math_poly_trig, each a textured quad pair,
// tinted by a colour lerp on the beam progress. Segment count scales with the
// beam progress (f1); the transform basis is param matrix param_3[0..10].
// -----------------------------------------------------------------------------
void snake3_seg_render_beam(float *m)
{
    unsigned long long pair = FUN_803826d4();
    float *hdr = (float *)(unsigned int)(pair >> 0x20);
    float *originRow = (float *)(unsigned int)pair;
    double progress = 0.0;   // f1 spill (unrecoverable), modelled 0
    double startAng = (double)hdr[0];
    double length   = (double)hdr[7];
    double endLen   = (double)(float)((double)FLOAT_80619bc0 * progress + startAng);
    int segCount = (int)((double)FLOAT_80619bbc * progress) + 1;
    unsigned int color = color_lerp(progress, 0x3014641e, 0x3000007f);
    if (0xc < segCount) segCount = 0xc;
    (void)endLen; (void)length;

    double bx = (double)(float)((double)m[2] * (double)hdr[7] + (double)originRow[0]);
    double by = (double)(float)((double)m[6] * (double)hdr[7] + (double)originRow[1]);
    double bz = (double)(float)((double)m[10] * (double)hdr[7] + (double)originRow[2]);

    gx_set_blend_mode(2);
    if (1 < segCount) {
        int vtx = segCount << 1;
        gx_begin_textured_batch(2, 0x81, 0, vtx, 0);
        double angBias  = (double)FLOAT_80619b40;
        double insetB   = (double)FLOAT_80619ba8;
        double angStep  = (double)FLOAT_80619bc4;
        double ang = startAng;
        float width = hdr[3];
        for (int s = 0; s < segCount - 1; ++s) {
            double s0 = math_poly_trig((double)(float)(ang - angBias));
            double off0 = (double)(float)((double)width * s0);
            double c0 = math_poly_trig(ang);
            float sx = (float)((double)width * c0);
            float sy = (float)off0;
            float sx2 = (float)((double)(float)((double)width * c0) * insetB);
            float sy2 = (float)(off0 * insetB);
            ang = (double)(float)(ang + angStep);
            // two ribbon vertices for this segment, transformed by m
            float mx0 = m[0] * sx + m[1] * sy;
            float my0 = m[4] * sx + m[5] * sy;
            float mz0 = m[8] * sx + m[9] * sy;
            float mx1 = m[0] * sx2 + m[1] * sy2;
            float my1 = m[4] * sx2 + m[5] * sy2;
            float mz1 = m[8] * sx2 + m[9] * sy2;
            unsigned int c = color << 0x18 | (color & 0xff00) << 8 | color >> 8 & 0xff00 | color >> 0x18;
            gx_fifo_f32[0] = (float)(bx + (double)mx0);
            gx_fifo_f32[0] = (float)(by + (double)my0);
            gx_fifo_f32[0] = (float)(bz + (double)mz0);
            gx_fifo_u32[0] = c;
            gx_fifo_f32[0] = (float)(bx + (double)mx1);
            gx_fifo_f32[0] = (float)(by + (double)my1);
            gx_fifo_f32[0] = (float)(bz + (double)mz1);
            gx_fifo_u32[0] = c;
        }
        gx_end_batch();
        // mirror pass (the original re-emits with negated tangent)
        gx_begin_textured_batch(2, 0x81, 0, vtx, 0);
        for (int s = 0; s < segCount; ++s) {
            unsigned int c = color << 0x18 | (color & 0xff00) << 8 | color >> 8 & 0xff00 | color >> 0x18;
            gx_fifo_f32[0] = (float)bx;
            gx_fifo_f32[0] = (float)by;
            gx_fifo_f32[0] = (float)bz;
            gx_fifo_u32[0] = c;
            gx_fifo_f32[0] = (float)bx;
            gx_fifo_f32[0] = (float)by;
            gx_fifo_f32[0] = (float)bz;
            gx_fifo_u32[0] = c;
        }
        gx_end_batch();
    }
    FUN_80382720();
}

} // extern "C"
