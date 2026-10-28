// =============================================================================
// beam_spark_fx.cpp - boss "beam / spark" weapon VFX (batch 795).
//
// A small family of GX-FIFO effect renderers used by the boss-3 set-piece
// (boss3_render / boss3_link_render) and a few effect-updater dispatchers. They
// build and draw an animated helix/tube "beam" plus a layered "spark/flash"
// billboard burst. All vertices are streamed directly to the GX write-gather
// FIFO port at 0xCC008000.
//
//   - beam_fx_get_segment_count (0x802c72b0): returns the fixed beam segment
//     count (0xc). Trivial accessor used by the callers to size their loops.
//   - beam_fx_build_segments (0x802c66bc): build the beam's per-ring vertex +
//     uv table into the caller's record (param_3). Sweeps <rows> latitude bands
//     x <cols> longitude steps of a swept tube using math_poly_trig, storing
//     pos[3]+uv[2]+size per vertex at 0xf0 stride; the ring/col counts come from
//     the packed frame-helper return (FUN_803826c8).
//   - beam_fx_render_segments (0x802c6970): stream the segment table built above
//     as GX textured quad strips (mode 0x92), two rows at a time, colour = the
//     byte-swapped frame-helper word; skips segments outside the visible Y span.
//   - spark_fx_init_params (0x802c6b38): initialise a spark descriptor
//     (param_1[0..8]) with the fixed geometry constants and pick the two sprite
//     textures for the current weapon mode (DAT_8060f948 == 4 -> ids 4/8, else
//     6/5).
//   - spark_fx_render (0x802c6bd8): the multi-pass spark/flash billboard render.
//     For life param_3 >= threshold it emits (a) a small additive core quad,
//     (b) a 16-vertex cross/flare strip, (c) a textured halo quad, and (d) a
//     textured expanding-ring quad, animating size/alpha off the descriptor's
//     growth field (param_4[7]). Below the threshold it draws a full-screen
//     flash fill plus (past a second threshold) a single fading quad.
//
// Address range: 0x802c66bc - 0x802c72b8 (exclusive)
// Subsystem: graphics (GX FIFO vertex submission, 0xCC008000)
//
// NON-MATCHING: frame-helper register reads (FUN_803826xx) -> beam_fx_frame_word()
// / a packed dims value; CONCAT44 int->double bias -> plain (float)(int); the
// long unrolled FIFO store sequences are kept literal (each store pushes one word
// to the command processor). __save/__restore_gpr stubs dropped.
// =============================================================================

#include "common.h"

extern "C" {

// GX write-gather FIFO port (0xCC008000): each typed store pushes one word.
static inline void gx_fifo_f32(float v) { *(volatile float*)0xCC008000u = v; }
static inline void gx_fifo_u32(unsigned int v) { *(volatile unsigned int*)0xCC008000u = v; }

// frame-helper word (PPC frame stub read-back): low = colour/dims, hi = ring dims
extern unsigned long long beam_fx_frame_word(void);

extern double math_poly_trig(double x);

// GX batch helpers (named cross-module)
extern void gx_begin_batch_mode5(int prim, int a, int count, int matId);
extern void gx_begin_textured_batch(int sub, int prim, int a, int count, int matId);
extern void gx_end_batch(void);
extern void gx_fill_rect_clipped(int x0, int y0, int x1, int y1, int c);
extern int  sprite_get_texture_id(int spriteId);
extern unsigned int FUN_803825c0(double t);   // colour-channel quantiser (raw, per-file)

extern int  DAT_8060f948;    // active weapon/mode selector
extern int  DAT_80611748;    // screen-rect record base ptr

// int->double bias
extern unsigned int DAT_806109c0;

// tuning pool (beam)
extern const float FLOAT_80619e70;   // uv-U base
extern const float FLOAT_80619e74;   // uv scale divisor
extern const float FLOAT_80619e78;   // ring row step
extern const float FLOAT_80619e7c;   // uv-V span
extern const float FLOAT_80619e80;   // ring col step
extern const float FLOAT_80619e84;   // trig phase offset
extern const float FLOAT_80619e88;   // size-out scale
extern const double DOUBLE_80619e90; // 2^52 int->double bias
extern const float FLOAT_80619e98;   // render Y-slope factor

// tuning pool (spark geometry + animation)
extern const float FLOAT_80619ea0;   // 0.0
extern const float FLOAT_80619ea4;   // uv const
extern const float FLOAT_80619ea8;   // 1.0 / core size
extern const float FLOAT_80619eac;   // param[0] base size
extern const float FLOAT_80619eb0;   // param[1] aspect
extern const float FLOAT_80619eb4;   // life threshold A (main burst)
extern const float FLOAT_80619eb8;   // life threshold B (low-life flash quad)
extern const float FLOAT_80619ebc;   // 1.0
extern const float FLOAT_80619ec0;   // halo/ring extent
extern const float FLOAT_80619ec4;   // low-life alpha ramp
extern const float FLOAT_80619ec8;   // grow phase gate lo
extern const float FLOAT_80619ecc;   // grow phase gate hi
extern const float FLOAT_80619ed0;   // grow rate (param[7])
extern const float FLOAT_80619ed4;   // shrink gate
extern const float FLOAT_80619ed8;   // param[2] shrink rate
extern const float FLOAT_80619edc;   // param[4] shrink slope
extern const float FLOAT_80619ee0;   // ring uv offset

// -----------------------------------------------------------------------------
// beam_fx_get_segment_count (0x802c72b0)
// -----------------------------------------------------------------------------
unsigned int beam_fx_get_segment_count(void)
{
    return 0xc;
}

// -----------------------------------------------------------------------------
// spark_fx_init_params (0x802c6b38)
// Initialise a spark descriptor and pick the two textures for the weapon mode.
// -----------------------------------------------------------------------------
void spark_fx_init_params(float* desc)
{
    desc[2] = FLOAT_80619ea0;
    desc[3] = FLOAT_80619ea4;
    desc[4] = FLOAT_80619ea8;
    desc[0] = FLOAT_80619eac;
    desc[1] = FLOAT_80619eb0 * desc[0];
    desc[7] = FLOAT_80619ea0;
    desc[8] = FLOAT_80619ea0;
    if (DAT_8060f948 == 4) {
        desc[5] = (float)sprite_get_texture_id(4);
        desc[6] = (float)sprite_get_texture_id(8);
    } else {
        desc[5] = (float)sprite_get_texture_id(6);
        desc[6] = (float)sprite_get_texture_id(5);
    }
}

// -----------------------------------------------------------------------------
// beam_fx_build_segments (0x802c66bc)
// Build the beam's per-vertex table (pos[3]+uv[2]+size @ 0xf0 stride) into the
// caller record. Ring/col counts are packed in the frame-helper return.
// -----------------------------------------------------------------------------
void beam_fx_build_segments(int a1, int a2, unsigned long long* rec)
{
    unsigned long long dims;
    int rows, cols;
    unsigned int ri, ci;
    int rowByte, colByte;
    double bias = DOUBLE_80619e90;
    double vSpan   = (double)FLOAT_80619e7c;
    double colStep = (double)FLOAT_80619e80;
    double rowStep = (double)FLOAT_80619e78;
    double phaseOff= (double)FLOAT_80619e84;
    double sizeOut = (double)FLOAT_80619e88;
    double rad = 1.0;   // swept-tube radius carried from the frame-helper FP reg

    (void)a1; (void)a2;
    dims = beam_fx_frame_word();
    rows = (int)(dims >> 0x20);
    cols = (int)dims;
    *(float*)((int)rec + 4) = (float)rad;
    *(int*)rec = rows;
    *(int*)((int)rec + 4) = cols;   // Ghidra view: low word overwritten below by uv math

    double uScale = (double)(FLOAT_80619e70 / (FLOAT_80619e74 * (float)rad));

    colByte = 0;
    for (ci = 0; (int)ci < cols; ci = ci + 1) {
        double latPhase = (double)((float)(rowStep /
            (double)(float)((float)(int)((cols - 1) ^ 0x80000000) - (float)bias)) *
            (float)((float)(int)(ci ^ 0x80000000u) - (float)bias));
        double vCoord = (double)((float)(vSpan /
            (double)(float)((float)(int)((int)(dims ^ 0x8000000080000000ull)) - (float)bias)) *
            (float)((float)(int)(ci ^ 0x80000000u) - (float)bias));
        rowByte = 0;
        for (ri = 0; (int)ri < rows; ri = ri + 1) {
            float* vtx = (float*)((int)rec + colByte + rowByte + 0xc);
            double lonPhase = (double)((float)(colStep /
                (double)(float)((float)(int)((rows - 1) ^ 0x80000000) - (float)bias)) *
                (float)((float)(int)(ri ^ 0x80000000u) - (float)bias));
            double cLon = math_poly_trig(lonPhase);
            double sLat = math_poly_trig((double)(float)(latPhase - phaseOff));
            double px   = (double)(float)((double)(float)(rad * sLat) * cLon);
            double cLon2 = math_poly_trig((double)(float)(lonPhase - phaseOff));
            double sLat2 = math_poly_trig((double)(float)(latPhase - phaseOff));
            double pz   = (double)(float)((double)(float)(rad * sLat2) * cLon2);
            double sLatY = math_poly_trig(latPhase);
            vtx[0] = (float)pz;
            vtx[1] = (float)(rad * sLatY);
            vtx[2] = (float)px;
            vtx[3] = (float)(vSpan /
                (double)(float)((float)(int)((int)((dims ^ 0x8000000080000000ull) >> 0x20)) - (float)bias)) *
                (float)((float)(int)(ri ^ 0x80000000u) - (float)bias);
            vtx[4] = (float)vCoord;
            vtx[5] = (float)(int)(sizeOut * (double)(float)((double)vtx[0] * uScale));
            rowByte = rowByte + 0xf0;
        }
        colByte = colByte + 0x18;
    }
}

// -----------------------------------------------------------------------------
// beam_fx_render_segments (0x802c6970)
// Stream the segment table as GX textured quad strips, two rows at a time.
// -----------------------------------------------------------------------------
void beam_fx_render_segments(unsigned long long p1, double scale, unsigned int p3,
                             unsigned int p4, float* origin, int* rec)
{
    unsigned long long fw;
    unsigned int colr;
    int seg, colByte;
    double bias = DOUBLE_80619e90;

    (void)p1; (void)p3; (void)p4;
    fw = beam_fx_frame_word();
    colr = (unsigned int)fw;

    // upper visible-Y bound derived from the descriptor row 0
    double yTop = (double)(origin[1] +
        (float)((double)(float)rec[2] * scale -
                (double)(float)(scale * (double)(float)((double)(float)((double)FLOAT_80619e98 *
                    (double)(float)rec[2]) * scale))));

    colByte = 0;
    for (seg = 0; seg < rec[0] - 1; seg = seg + 1) {
        double yA = (double)(float)((double)*(float*)((int)rec + colByte + 0x10) * scale + (double)origin[1]);
        if (yTop < yA) {
            double yB = (double)(float)((double)*(float*)((int)rec + colByte + 0x28) * scale + (double)origin[1]);
            if (yB < yTop) yB = yTop;
            gx_begin_textured_batch(2, 0x92, 0, (short)(rec[1] << 1), (int)(fw >> 0x20));
            int col = 0, colStep = 0;
            while (col < rec[1]) {
                float* rowA = (float*)((int)rec + colByte + 0xc + colStep);
                float* rowB = (float*)((int)rec + (seg + 1) * 0x18 + 0xc + colStep);
                unsigned int swapped = colr << 0x18 | (colr & 0xff00) << 8 | colr >> 8 & 0xff00 | colr >> 0x18;
                gx_fifo_f32((float)((double)rowA[0] * scale + (double)origin[0]));
                gx_fifo_f32((float)yA);
                gx_fifo_f32((float)((double)rowA[2] * scale + (double)origin[2]));
                gx_fifo_u32(swapped);
                gx_fifo_f32(rowA[3]);
                gx_fifo_f32(rowA[4]);
                gx_fifo_f32((float)((double)rowB[0] * scale + (double)origin[0]));
                gx_fifo_f32((float)yB);
                gx_fifo_f32((float)((double)rowB[2] * scale + (double)origin[2]));
                gx_fifo_u32(swapped);
                gx_fifo_f32(rowA[3]);
                gx_fifo_f32(rowA[4]);
                col = col + 1;
                colStep = colStep + 0xf0;
            }
            gx_end_batch();
        }
        colByte = colByte + 0x18;
    }
}

// -----------------------------------------------------------------------------
// spark_fx_render (0x802c6bd8)
// Multi-pass additive/textured spark burst. life (param_3) drives which passes
// fire and how the size/alpha animate.
// -----------------------------------------------------------------------------
void spark_fx_render(double dx, double dz, double life, float* desc)
{
    double px  = (double)(float)((double)desc[0] * dx);
    double py1 = (double)desc[1];
    double sy  = (double)desc[4];
    double pz  = (double)(float)((double)desc[0] * dz);
    double bx  = (double)desc[2];
    double bz  = (double)desc[3];

    if ((double)FLOAT_80619eb4 <= life) {
        // grow / shrink animation of the descriptor size fields
        if (life <= (double)FLOAT_80619ec8 || (double)FLOAT_80619ecc <= life) {
            if ((double)FLOAT_80619ed4 < life) {
                float t = (float)(life - (double)FLOAT_80619ed4);
                desc[2] = FLOAT_80619ed8 * t;
                desc[4] = FLOAT_80619edc * t + FLOAT_80619ea8;
            }
        } else {
            desc[7] = FLOAT_80619ed0 * (float)(life - (double)FLOAT_80619ec8);
        }

        float ax = (float)(bx - px);
        float ay = (float)(bz + py1);
        float az = (float)(sy - pz);
        float ay2 = (float)(bz - py1);
        float bx2 = (float)(bx + px);
        float az2 = (float)(sy + pz);

        // (a) additive core quad
        gx_begin_batch_mode5(0x91, 0, 4, (int)desc[5]);
        gx_fifo_f32(ax); gx_fifo_f32(ay); gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32(FLOAT_80619ebc); gx_fifo_f32(FLOAT_80619ea0);
        gx_fifo_f32(ax); gx_fifo_f32(ay2); gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32(FLOAT_80619ebc); gx_fifo_f32(FLOAT_80619ebc);
        gx_fifo_f32(bx2); gx_fifo_f32(ay2); gx_fifo_f32(az2); gx_fifo_u32(0x7f);
        gx_fifo_f32(FLOAT_80619ea0); gx_fifo_f32(FLOAT_80619ebc);
        gx_fifo_f32(bx2); gx_fifo_f32(ay); gx_fifo_f32(az2); gx_fifo_u32(0x7f);
        gx_fifo_f32(FLOAT_80619ea0); gx_fifo_f32(FLOAT_80619ea0);
        gx_end_batch();

        // (b) 16-vertex untextured cross/flare strip
        float lo = (float)(bx - (double)(float)((double)FLOAT_80619ec0 * py1));
        float hi = (float)(bx + (double)(float)((double)FLOAT_80619ec0 * py1));
        float sx = (float)(bz - (double)(float)((double)FLOAT_80619ea8 * px));
        float sx2 = (float)(bz + (double)(float)((double)FLOAT_80619ea8 * px));
        gx_begin_batch_mode5(0x81, 0, 0x10, -1);
        gx_fifo_f32(ax); gx_fifo_f32(ay2); gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32(ax); gx_fifo_f32(lo);  gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32((float)px); gx_fifo_f32(lo); gx_fifo_f32((float)az2); gx_fifo_u32(0x7f);
        gx_fifo_f32((float)px); gx_fifo_f32(ay2); gx_fifo_f32((float)az2); gx_fifo_u32(0x7f);
        gx_fifo_f32(ax); gx_fifo_f32(ay);  gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32(ax); gx_fifo_f32(hi);  gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32((float)px); gx_fifo_f32(hi); gx_fifo_f32((float)az2); gx_fifo_u32(0x7f);
        gx_fifo_f32((float)px); gx_fifo_f32(ay); gx_fifo_f32((float)az2); gx_fifo_u32(0x7f);
        gx_fifo_f32(sx); gx_fifo_f32(hi);  gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32(sx); gx_fifo_f32(lo);  gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32(ax); gx_fifo_f32(lo);  gx_fifo_f32((float)az2); gx_fifo_u32(0x7f);
        gx_fifo_f32(ax); gx_fifo_f32(hi);  gx_fifo_f32((float)az2); gx_fifo_u32(0x7f);
        gx_fifo_f32(sx2); gx_fifo_f32(hi); gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32(sx2); gx_fifo_f32(lo); gx_fifo_f32(az); gx_fifo_u32(0x7f);
        gx_fifo_f32((float)bx2); gx_fifo_f32(lo); gx_fifo_f32((float)az2); gx_fifo_u32(0x7f);
        gx_fifo_f32((float)bx2); gx_fifo_f32(hi); gx_fifo_f32((float)az2); gx_fifo_u32(0x7f);
        gx_end_batch();

        // (c) textured halo quad
        float grow = desc[7];
        float g1 = (float)((double)(FLOAT_80619ec0 * grow) * px + (double)ax);
        gx_begin_batch_mode5(0x91, 0, 4, (int)desc[6]);
        gx_fifo_f32((float)bx2); gx_fifo_f32(ay); gx_fifo_f32(az); gx_fifo_u32(0x7f7f7f7f);
        gx_fifo_f32(FLOAT_80619ea0); gx_fifo_f32(FLOAT_80619ea0);
        gx_fifo_f32((float)bx2); gx_fifo_f32(ay2); gx_fifo_f32(az); gx_fifo_u32(0x7f7f7f7f);
        gx_fifo_f32(FLOAT_80619ea0); gx_fifo_f32(FLOAT_80619ebc);
        gx_fifo_f32(g1); gx_fifo_f32(ay2); gx_fifo_f32(az2); gx_fifo_u32(0x7f7f7f7f);
        gx_fifo_f32(FLOAT_80619ebc - grow); gx_fifo_f32(FLOAT_80619ebc);
        gx_fifo_f32(g1); gx_fifo_f32(ay); gx_fifo_f32(az2); gx_fifo_u32(0x7f7f7f7f);
        gx_fifo_f32(FLOAT_80619ebc - grow); gx_fifo_f32(FLOAT_80619ea0);
        gx_end_batch();

        // (d) textured expanding-ring quad
        float gOut = (float)((double)(FLOAT_80619ec0 * (FLOAT_80619ee0 + grow)) * px + (double)ax);
        float gIn  = (float)((double)(FLOAT_80619ec0 * grow) * px + (double)ax);
        float gNeg = (float)((double)(FLOAT_80619ec0 * grow) * px + -px);
        float gPrev= (float)((double)(FLOAT_80619ec0 * (grow - FLOAT_80619ee0)) * px + (double)ax);
        gx_begin_textured_batch(2, 0x91, 0, 6, (int)desc[6]);
        gx_fifo_f32(gOut); gx_fifo_f32(ay); gx_fifo_f32(az); gx_fifo_u32(0x7f7f7f7f);
        gx_fifo_f32(FLOAT_80619ebc - (FLOAT_80619ee0 + grow)); gx_fifo_f32(FLOAT_80619ea0);
        gx_fifo_f32(gOut); gx_fifo_f32(ay2); gx_fifo_f32(az); gx_fifo_u32(0x7f7f7f7f);
        gx_fifo_f32(FLOAT_80619ebc - (FLOAT_80619ee0 + grow)); gx_fifo_f32(FLOAT_80619ebc);
        gx_fifo_f32(gIn); gx_fifo_f32(ay); gx_fifo_f32(az); gx_fifo_u32(0x5a506e7f);
        gx_fifo_f32(FLOAT_80619ebc - grow); gx_fifo_f32(FLOAT_80619ea0);
        gx_fifo_f32(gNeg); gx_fifo_f32(ay2); gx_fifo_f32(az); gx_fifo_u32(0x5a506e7f);
        gx_fifo_f32(FLOAT_80619ebc - grow); gx_fifo_f32(FLOAT_80619ebc);
        gx_fifo_f32(gPrev); gx_fifo_f32(ay); gx_fifo_f32(az); gx_fifo_u32(0x5a506e00);
        gx_fifo_f32(FLOAT_80619ebc - (grow - FLOAT_80619ee0)); gx_fifo_f32(FLOAT_80619ea0);
        gx_fifo_f32(gPrev); gx_fifo_f32(ay2); gx_fifo_f32(az); gx_fifo_u32(0x5a506e00);
        gx_fifo_f32(FLOAT_80619ebc - (grow - FLOAT_80619ee0)); gx_fifo_f32(FLOAT_80619ebc);
        gx_end_batch();
    } else {
        // low life: full-screen flash fill + optional single fading quad
        gx_fill_rect_clipped(*(int*)DAT_80611748, *(int*)(DAT_80611748 + 0x14),
                             *(int*)(DAT_80611748 + 4) + 1, *(int*)(DAT_80611748 + 0x18) + 1, 0x7f);
        if ((double)FLOAT_80619eb8 < life) {
            gx_begin_batch_mode5(0x91, 0, 4, (int)desc[6]);
            float ay = (float)(bz + py1);
            float az = (float)(sy - pz);
            float lo = (float)(bz - py1);
            float az2 = (float)(sy + pz);
            unsigned int a = FUN_803825c0((double)(FLOAT_80619ec4 *
                ((float)(life - (double)FLOAT_80619eb8) / FLOAT_80619ea8)));
            gx_fifo_f32((float)px); gx_fifo_f32(ay); gx_fifo_f32(az);
            gx_fifo_u32(a & 0xff | 0x7f7f7f00);
            gx_fifo_f32(FLOAT_80619ea0); gx_fifo_f32(FLOAT_80619ea0);
            gx_fifo_f32((float)px); gx_fifo_f32(lo); gx_fifo_f32(az);
            gx_fifo_u32(a & 0xff | 0x7f7f7f00);
            gx_fifo_f32(FLOAT_80619ea0); gx_fifo_f32(FLOAT_80619ebc);
            gx_fifo_f32((float)bx); gx_fifo_f32(lo); gx_fifo_f32(az2);
            gx_fifo_u32(a & 0xff | 0x7f7f7f00);
            gx_fifo_f32(FLOAT_80619ebc - desc[7]); gx_fifo_f32(FLOAT_80619ebc);
            gx_fifo_f32((float)bx); gx_fifo_f32(ay); gx_fifo_f32(az2);
            gx_fifo_u32(a & 0xff | 0x7f7f7f00);
            gx_fifo_f32(FLOAT_80619ebc - desc[7]); gx_fifo_f32(FLOAT_80619ea0);
            gx_end_batch();
        }
    }
}

} // extern "C"
