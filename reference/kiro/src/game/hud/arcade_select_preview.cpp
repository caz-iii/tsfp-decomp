// Arcade select-screen 3D preview subsystem: the top-level render dispatch,
// the UV/angle wrap-fold helper, the billboard-ribbon and grid mesh builders,
// the per-element spin/phase tick helpers, the grid-vertex animator, and the
// two GP-FIFO mesh draw routines (billboard column + ribbon element).
//
// Batch: batch_835
// Module: arcade-select-preview (new file). Companion to arcade_select_frame.cpp
//   / _screen.cpp / _render.cpp. arcsel_render_bg_dispatch is the render entry
//   invoked per frame; the builders (arcsel_preview_add_billboard / _add_grid)
//   are called from arcsel_state_build_full; the draw routines
//   (arcsel_render_element / _render_billboard_element) stream vertices to the
//   GameCube GP command FIFO at 0xCC008000. Subsystem: graphics.
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns.

#include "common.h"

// ---- GP command FIFO (write-only MMIO port at 0xCC008000). Every store of a
//      f32/u32 pushes one word into the graphics command stream. ----
#define GX_FIFO_ADDR 0xCC008000u
static inline void gx_fifo_push_f(float v)    { *(volatile float *)(GX_FIFO_ADDR) = v; }
static inline void gx_fifo_push_u(unsigned v) { *(volatile unsigned *)(GX_FIFO_ADDR) = v; }

// ---- Per-file externs (raw symbols) ----
extern u64   FUN_803826c4(void);   // begin billboard-builder frame  (ret packed ptr/count)
extern void  FUN_80382710(void);   // end billboard-builder frame
extern u64   FUN_803826c8(void);   // begin element-render frame
extern void  FUN_80382714(void);   // end element-render frame
extern u64   FUN_803826cc(void);   // begin billboard-render frame
extern void  FUN_80382718(void);   // end billboard-render frame
extern u64   FUN_803826d0(void);   // begin grid-builder frame
extern void  FUN_8038271c(void);   // end grid-builder frame
extern u64   __save_gpr_25(void);
extern void  __restore_gpr_25(void);

extern double math_poly_trig(double x);
extern int   mem_alloc(double a, double b, double c, double d, double e, double f6, double f7,
                       double f8, int size, int align, int flags, int g, int h, int i, int j,
                       u32 k);
extern int   rng_counter_tick(void);
extern float *mtx4_pool_alloc(void);
extern void  mtx4_set_rotation_y(double angle, float *out);
extern void  mtx4_multiply(float *a, float *b, float *out);
extern void  mtx4_transpose(float *src, float *dst);
extern void  gx_material_state_nop(void);
extern void  gx_begin_textured_batch(int prim, int vtxfmt, int a, short count, int tex);
extern void  gx_end_batch(void);
extern int   text_string_to_upper(uint str);

// arcsel intra-module helpers (sibling files).
extern void  arcsel_render_background(void);          // 0x802e6b60
extern void  arcsel_render_loadbar_overlay(void);     // 0x802e6e28
extern void  arcsel_draw_header(void);                // 0x802e4b18
extern void  arcsel_draw_award_banner(void);          // 0x802e4edc
extern int   hud_screen_destroy_current(void);
extern void  spark_fx_render(double a, double b, double c, float *state);

// ---- Raw data symbols ----
extern int   DAT_80612614;     // current arcade mode id
extern int   DAT_805cc6e0;     // intro/anim phase flag
extern int   DAT_805cc6dc;     // spark-fx intensity
extern int   DAT_80612b64;     // arcsel owner object ptr
extern int   DAT_80612250;     // gx tex-matrix-share flag
extern int   DAT_80612258;     // gx tex-matrix index
extern int   DAT_8058e3cc;     // active view matrix dst

extern float  FLOAT_8061a180;  // 0.0f
extern float  FLOAT_8061a184;  // 1.0f
extern float  FLOAT_8061a2d0;
extern float  FLOAT_8061a2d4;
extern double DOUBLE_8061a2d8; // u32->double bias (4503599627370496.0)
extern float  FLOAT_8061a2e0;
extern float  FLOAT_8061a2e4;
extern float  FLOAT_8061a2e8;
extern float  FLOAT_8061a2ec;
extern float  FLOAT_8061a2f0;
extern float  FLOAT_8061a2f4;
extern float  FLOAT_8061a2f8;
extern double DOUBLE_8061a300;
extern float  FLOAT_8061a308;

// ---------------------------------------------------------------------------
// Address: 0x802e6ec4 | Name: arcsel_render_bg_dispatch
// Top-level per-frame render for the arcade-select screen: background (or the
// load-bar overlay for the two loading modes), header text, award banner, and
// the spark effect overlay for the active intro phases.
void arcsel_render_bg_dispatch(void)
{
    int  screen;
    uint titleStr;

    screen = hud_screen_destroy_current();
    titleStr = 0;
    if (screen != 0) {
        titleStr = *(uint *)(screen + 0x200);
    }
    if ((DAT_80612614 == 0x6a) || (DAT_80612614 == 0x6b)) {
        arcsel_render_loadbar_overlay();
    } else {
        arcsel_render_background();
        text_string_to_upper(titleStr);
        arcsel_draw_header();
    }
    arcsel_draw_award_banner();
    if ((DAT_80612614 != 0x6b) && ((DAT_805cc6e0 == 0) || (DAT_805cc6e0 == 1))) {
        spark_fx_render((double)FLOAT_8061a184, (double)FLOAT_8061a180, (double)DAT_805cc6dc,
                        (float *)(DAT_80612b64 + 0x654));
    }
}

// ---------------------------------------------------------------------------
// Address: 0x802e6f7c | Name: arcsel_render_nop
void arcsel_render_nop(void)
{
}

// ---------------------------------------------------------------------------
// Address: 0x802e6f80 | Name: arcsel_wrap_uv_triangle
// Folds a UV/parametric coordinate into a triangle wave within [0, FLOAT_8061a2d0]
// so scrolling texture coords bounce rather than clamp. Operates in place.
void arcsel_wrap_uv_triangle(float *param_1)
{
    uint  ip;
    float v;

    v = *param_1;
    if (v <= FLOAT_8061a2d0) {
        if (v < FLOAT_8061a2d4) {
            ip = (uint)v;
            int parity = (int)((ip & 1) ^ -((int)ip >> 0x1f)) + ((int)ip >> 0x1f);
            if (parity < 0) {
                *param_1 = FLOAT_8061a2d0 - (v - (float)((double)(float)(uint)(ip ^ 0x80000000) -
                                                         DOUBLE_8061a2d8));
            } else {
                *param_1 = v - (float)((double)(float)(uint)(ip ^ 0x80000000) - DOUBLE_8061a2d8);
                *param_1 = -*param_1;
            }
        }
    } else {
        ip = (uint)v;
        int parity = (int)((ip & 1) ^ -((int)ip >> 0x1f)) + ((int)ip >> 0x1f);
        if (parity < 1) {
            *param_1 = v - (float)((double)(float)(uint)(ip ^ 0x80000000) - DOUBLE_8061a2d8);
        } else {
            *param_1 = FLOAT_8061a2d0 - (v - (float)((double)(float)(uint)(ip ^ 0x80000000) -
                                                     DOUBLE_8061a2d8));
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x802e7098 | Name: arcsel_preview_add_billboard
// Builds a rows*cols vertex ribbon for one preview billboard element: allocates
// the per-row vertex arrays, then fills position (sinusoidal x, lerped y/z) and
// UV coords (optionally triangle-wrapped) for each vertex. Stores geometry into
// the packed record returned by FUN_803826c4.
void arcsel_preview_add_billboard(u64 param_1, double param_2, double param_3, double param_4,
                                  double param_5, double param_6, double param_7, double param_8,
                                  u32 param_9, u32 param_10, int param_11, int param_12,
                                  int param_13, int param_14, int param_15, u32 param_16)
{
    int   *rec;
    int    cols;
    int    vbytes;
    int    row;
    int    col;
    int    rowOff;
    uint   colsM1;
    uint   halfCols;
    uint   rowsM1;
    float *vtx;
    double phase, ang, sinX, u0, v0, uCoord;
    double dCols, dPhaseStep;
    u64    packed;
    float  uv[2];
    float  vScroll;

    (void)param_9; (void)param_10;
    packed = FUN_803826c4();
    rec  = (int *)(unsigned)(packed >> 0x20);
    cols = (int)packed;

    rec[1] = (int)(float)param_2;    // stored y (from the f1 pair Ghidra split)
    rec[2] = (int)(float)param_3;
    rec[3] = (int)(float)param_2;
    rec[4] = (int)FLOAT_8061a2d4;
    rec[5] = (int)FLOAT_8061a2e0;
    rec[6] = param_12;
    rec[7] = cols;
    rec[8] = param_11;
    rec[9] = param_15;

    rec[0] = mem_alloc((double)FLOAT_8061a2e0, param_2, param_3, param_4, param_5, param_6, param_7,
                       param_8, cols << 2, 1, param_11, param_12, param_13, param_14, param_15,
                       param_16);
    rowOff = 0;
    for (row = 0; row < cols; row++) {
        int arr = mem_alloc((double)FLOAT_8061a2e0, param_2, param_3, param_4, param_5, param_6,
                            param_7, param_8, param_11 * 0x14, 1, param_11, param_12, param_13,
                            param_14, param_15, param_16);
        *(int *)(rec[0] + rowOff) = arr;
        rowOff = rowOff + 4;
    }

    colsM1   = (uint)(param_11 - 1);
    halfCols = ((int)colsM1 >> 1) + (uint)((int)colsM1 < 0 && (colsM1 & 1) != 0);
    rowsM1   = (uint)(cols - 1) ^ 0x80000000;
    dPhaseStep = (double)FLOAT_8061a2ec;
    dCols    = (double)FLOAT_8061a2f0;

    rowOff = 0;
    for (row = 0; row < cols; row++) {
        int colOff = 0;
        double rowT = (double)(float)((double)(float)(uint)((uint)row ^ 0x80000000) - DOUBLE_8061a2d8);
        float rowScroll = (float)(rowT * (double)(float)(param_6 /
            (double)(float)((double)(float)(uint)rowsM1 - DOUBLE_8061a2d8)) + param_5);
        double zLerp = (double)(float)(rowT * (double)(float)(param_3 /
            (double)(float)((double)(float)(uint)rowsM1 - DOUBLE_8061a2d8)) + param_2);
        vScroll = rowScroll;
        for (col = 0; col < param_11; col++) {
            vtx = (float *)(*(int *)(rowOff + rec[0]) + colOff);
            double colT = (double)(float)(uint)((uint)col ^ 0x80000000);
            double ph = (double)((float)(dPhaseStep /
                (double)(float)((double)(float)(uint)(colsM1 ^ 0x80000000) - DOUBLE_8061a2d8)) *
                (float)(colT - DOUBLE_8061a2d8));
            double s1 = math_poly_trig(ph);
            double xPos = (double)(float)(param_4 * (double)(float)(FLOAT_8061a184 * s1));
            double s2 = math_poly_trig((double)(float)(ph - dCols));
            if (col < (int)halfCols) {
                double num = (double)(float)(uint)((uint)col ^ 0x80000000);
                double den = (double)(float)(uint)(halfCols ^ 0x80000000);
                uv[0] = (float)((num - DOUBLE_8061a2d8) *
                                (float)(param_7 / (double)(float)(den - DOUBLE_8061a2d8)));
            } else {
                double den = (double)(float)(uint)(halfCols ^ 0x80000000);
                double num = (double)(float)(uint)((uint)(col - halfCols) ^ 0x80000000);
                uv[0] = (float)((double)(float)(param_8 /
                                (double)(float)(den - DOUBLE_8061a2d8)) *
                                (double)(float)(num - DOUBLE_8061a2d8) + param_7);
            }
            vtx[0] = (float)(param_4 * (double)(float)(FLOAT_8061a184 * s2));
            vtx[1] = (float)zLerp;
            vtx[2] = (float)xPos;
            if (param_14 == 1) {
                arcsel_wrap_uv_triangle(uv);
                arcsel_wrap_uv_triangle(&vScroll);
            }
            if (param_13 == 0) {
                vtx[3] = uv[0];
                vtx[4] = vScroll;
            } else {
                vtx[3] = vScroll;
                vtx[4] = uv[0];
            }
            colOff = colOff + 0x14;
        }
        rowOff = rowOff + 4;
    }
    FUN_80382710();
}

// ---------------------------------------------------------------------------
// Address: 0x802e741c | Name: arcsel_element_transform_tick
// Advances a preview element's scalar phase field (+0x10) by rate (+0x14)*dt,
// wrapping it back into [FLOAT_8061a2f8, FLOAT_8061a2f4].
void arcsel_element_transform_tick(double param_1, int param_2)
{
    float wrap = FLOAT_8061a2f4;
    float v;

    *(float *)(param_2 + 0x10) =
        (float)((double)*(float *)(param_2 + 0x14) * param_1 + (double)*(float *)(param_2 + 0x10));
    v = *(float *)(param_2 + 0x10);
    if (wrap < v) {
        *(float *)(param_2 + 0x10) = v - wrap;
        return;
    }
    if (FLOAT_8061a2f8 <= v) {
        return;
    }
    *(float *)(param_2 + 0x10) = v + wrap;
}

// ---------------------------------------------------------------------------
// Address: 0x802e7460 | Name: arcsel_render_billboard_element
// Draws one billboard element to the GP FIFO: rotate-Y by the element's spin
// field, transpose into the active view matrix, then emit a triangle-strip of
// (rows-1) strips, each cols wide, translated by param_3 and coloured by the
// packed vertex colour rec[9].
void arcsel_render_billboard_element(u32 param_1, u32 param_2, float *param_3)
{
    float *pRow;
    float *pNext;
    int   *rec;
    float *mtx;
    int    rows;
    int    cols;
    int    strip;
    int    v;
    int    rowOff;
    uint   color;
    u64    packed;

    (void)param_1; (void)param_2;
    packed = FUN_803826cc();
    rec   = (int *)(unsigned)(packed >> 0x20);
    color = (uint)rec[9];
    mtx   = mtx4_pool_alloc();
    mtx4_set_rotation_y((double)((FLOAT_8061a2e4 * (float)rec[4]) / FLOAT_8061a2e8), mtx);
    mtx4_multiply(mtx, (float *)(unsigned)packed, mtx);
    gx_material_state_nop();
    mtx4_transpose(mtx, (float *)&DAT_8058e3cc);
    rowOff = 0;
    rows   = rec[7];
    DAT_80612250 = 1;
    cols   = rec[8];
    DAT_80612258 = 0;
    int base = rec[0];
    for (strip = 0; strip < rows - 1; strip++) {
        pRow  = *(float **)(base + rowOff);
        pNext = (float *)((int *)(base + rowOff))[1];
        gx_begin_textured_batch(2, 0x92, 0, (short)(cols << 1), rec[6]);
        v = cols;
        if (v > 0) {
            do {
                gx_fifo_push_f(pRow[0] + param_3[0]);
                gx_fifo_push_f(pRow[1] + param_3[1]);
                gx_fifo_push_f(pRow[2] + param_3[2]);
                gx_fifo_push_u((color << 0x18) | ((color & 0xff00) << 8) |
                               ((color >> 8) & 0xff00) | (color >> 0x18));
                gx_fifo_push_f(pRow[3]);
                gx_fifo_push_f(pRow[4]);
                gx_fifo_push_f(pNext[0] + param_3[0]);
                gx_fifo_push_f(pNext[1] + param_3[1]);
                gx_fifo_push_f(pNext[2] + param_3[2]);
                gx_fifo_push_u((color << 0x18) | ((color & 0xff00) << 8) |
                               ((color >> 8) & 0xff00) | (color >> 0x18));
                pRow  = pRow + 5;
                gx_fifo_push_f(pNext[3]);
                gx_fifo_push_f(pNext[4]);
                pNext = pNext + 5;
                v = v - 1;
            } while (v != 0);
        }
        gx_end_batch();
        rowOff = rowOff + 4;
    }
    FUN_80382718();
}

// ---------------------------------------------------------------------------
// Address: 0x802e75f0 | Name: arcsel_preview_add_grid
// Builds a rows*cols vertex grid for a preview element (the character-stand
// backdrop). Seeds a per-build random phase, allocates the per-row vertex
// arrays, and fills position (double-sinusoidal) + UV coords for each vertex.
void arcsel_preview_add_grid(u64 param_1, double param_2, double param_3, double param_4,
                             double param_5, double param_6, double param_7, double param_8,
                             u32 param_9, u32 param_10, int param_11, int param_12, u32 param_13,
                             u32 param_14, u32 param_15, u32 param_16, float param_17, int param_18)
{
    u32   *srcRow;
    int    rows;
    int   *rec;
    int    rndTick;
    int    row;
    int    col;
    float *vtx;
    uint   colM1;
    uint   rowM1;
    int    rowOff;
    int    copyIdx;
    int    n;
    double phaseSeed, rndF, rndScaled, xRot0, xRot1, dPhase, colPos, rowPos, ang;
    u64    packed;

    (void)param_9; (void)param_10; (void)param_16;
    packed = FUN_803826d0();
    rec  = (int *)(unsigned)(packed >> 0x20);
    rows = (int)packed;
    rec[0xf] = 0;
    rec[1] = (int)(float)param_2;
    rec[2] = (int)(float)param_3;
    rec[3] = (int)(float)param_2;
    rec[4] = (int)(float)param_5;
    rec[5] = (int)(float)param_4;
    rec[9] = param_18;

    rndTick = rng_counter_tick();
    rndF = (double)(float)((double)(float)(uint)rndTick - DOUBLE_8061a300);
    rndScaled = (double)(float)(rndF / (double)FLOAT_8061a308);
    rec[8] = (int)(float)((double)FLOAT_8061a2f4 * rndScaled);

    // Copy two words of the src texture-id list into the record header (+0x30).
    copyIdx = 0;
    n = 2;
    do {
        srcRow = (u32 *)(param_12 + copyIdx);
        *(u32 *)((int)rec + copyIdx + 0x30) = *srcRow;
        copyIdx = copyIdx + 4;
        n = n - 1;
    } while (n != 0);

    rec[0xe] = 0;
    rec[10] = rows;
    rec[0xb] = param_11;

    rec[0] = mem_alloc(rndScaled, rndF, DOUBLE_8061a300, param_4, param_5, param_6, param_7,
                       param_8, param_11 << 2, 1, param_11, param_12, param_13, param_14, param_15,
                       param_16);
    rowOff = 0;
    for (row = 0; row < param_11; row++) {
        int arr = mem_alloc(rndScaled, rndF, DOUBLE_8061a300, param_4, param_5, param_6, param_7,
                            param_8, rows * 0x14, 1, param_11, param_12, param_13, param_14,
                            param_15, param_16);
        *(int *)(rec[0] + rowOff) = arr;
        rowOff = rowOff + 4;
    }

    dPhase = (double)FLOAT_8061a2f0;
    rowM1  = (uint)(rows - 1) ^ 0x80000000;
    colM1  = (uint)(param_11 - 1) ^ 0x80000000;
    xRot0  = (double)((float)((double)FLOAT_8061a2e4 * param_5) / FLOAT_8061a2e8);
    xRot1  = (double)((float)((double)FLOAT_8061a2e4 * param_4) / FLOAT_8061a2e8);

    rowOff = 0;
    for (row = 0; row < param_11; row++) {
        int colOff = 0;
        double rowFrac = (double)(float)(uint)((uint)row ^ 0x80000000);
        double vCoord = (double)(float)((double)(float)(rowFrac - DOUBLE_8061a2d8) *
            (double)(float)((double)param_17 /
                (double)(float)((double)(float)(uint)colM1 - DOUBLE_8061a2d8)) + param_8);
        double yPos = (double)(float)((double)(float)(rowFrac - DOUBLE_8061a2d8) *
            (double)(float)(param_3 /
                (double)(float)((double)(float)(uint)colM1 - DOUBLE_8061a2d8)) + param_2);
        for (col = 0; col < rows; col++) {
            vtx = (float *)(*(int *)(rowOff + rec[0]) + colOff);
            double colFrac = (double)(float)(uint)((uint)col ^ 0x80000000);
            double xr = (double)(float)((double)(float)(colFrac - DOUBLE_8061a2d8) *
                (double)(float)(xRot0 /
                    (double)(float)((double)(float)(uint)rowM1 - DOUBLE_8061a2d8)) + xRot1);
            double s1 = math_poly_trig((double)(float)(xr - dPhase));
            vtx[0] = (float)(param_2 * s1);           // (dVar20 is the f1 pair)
            vtx[1] = (float)yPos;
            double s2 = math_poly_trig(xr);
            colOff = colOff + 0x14;
            vtx[2] = (float)(param_2 * s2);
            vtx[3] = (float)((double)(float)(colFrac - DOUBLE_8061a2d8) *
                (double)(float)(param_7 /
                    (double)(float)((double)(float)(uint)rowM1 - DOUBLE_8061a2d8)) + param_6);
            vtx[4] = (float)vCoord;
        }
        rowOff = rowOff + 4;
    }
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// Address: 0x802e7948 | Name: arcsel_grid_animate
// Re-computes the grid vertex X/Z positions from a phase pair (the two f-pair
// inputs) each frame, giving the character-stand grid its rippling motion.
void arcsel_grid_animate(u64 param_1, double param_2)
{
    int   *rec;
    float *vtx;
    uint   row;
    uint   col;
    int    rowOff;
    int    colOff;
    double amp, phaseA, phaseB, dPhase, rowStepX, rowStepZ;
    double zLerp, xr, ang;
    double f1pair;   // Ghidra's extraout_f1 (second phase input)

    rec = (int *)__save_gpr_25();
    amp = (double)FLOAT_8061a2e4;
    // The two incoming f-register pairs are the animator's phase inputs.
    f1pair = (double)param_2;
    rowStepZ = (double)(float)rec[3];
    rowStepX = (double)(float)rec[2];
    double baseAmp = (double)(float)rec[1];
    double dt = (double)((float)(amp * param_2) / FLOAT_8061a2e8);
    rec[4] = (int)(float)param_2;
    rec[5] = (int)(float)f1pair;
    dPhase = (double)FLOAT_8061a2f0;
    double dt2 = (double)((float)(amp * f1pair) / FLOAT_8061a2e8);

    rowOff = 0;
    for (row = 0; (int)row < rec[0xb]; row++) {
        colOff = 0;
        double rowT = (double)(float)(uint)(row ^ 0x80000000);
        zLerp = (double)(float)((double)(float)(rowT - DOUBLE_8061a2d8) *
            (double)(float)(rowStepX /
                (double)(float)((double)(float)(uint)((uint)(rec[0xb] - 1) ^ 0x80000000) -
                                DOUBLE_8061a2d8)) + rowStepZ);
        for (col = 0; (int)col < rec[10]; col++) {
            vtx = (float *)(*(int *)(rowOff + rec[0]) + colOff);
            double colT = (double)(float)(uint)(col ^ 0x80000000);
            xr = (double)(float)((double)(float)(colT - DOUBLE_8061a2d8) *
                (double)(float)(dt /
                    (double)(float)((double)(float)(uint)((uint)(rec[10] - 1) ^ 0x80000000) -
                                    DOUBLE_8061a2d8)) + dt2);
            double s1 = math_poly_trig((double)(float)(xr - dPhase));
            vtx[0] = (float)(baseAmp * s1);
            vtx[1] = (float)zLerp;
            double s2 = math_poly_trig(xr);
            colOff = colOff + 0x14;
            vtx[2] = (float)(baseAmp * s2);
        }
        rowOff = rowOff + 4;
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// Address: 0x802e7b18 | Name: arcsel_element_matrix_tick
// Advances a grid element's spin field (+0x20) by rate (+0x24)*dt, latches a
// "wrapped" flag (+0x3c) when it crosses FLOAT_8061a2d4, and cycles a 2-state
// counter (+0x38) once the phase leaves the active band.
void arcsel_element_matrix_tick(double param_1, int param_2)
{
    float thresh = FLOAT_8061a2d4;
    double v;

    *(float *)(param_2 + 0x20) =
        (float)((double)*(float *)(param_2 + 0x24) * param_1 + (double)*(float *)(param_2 + 0x20));
    if (thresh < *(float *)(param_2 + 0x20)) {
        *(int *)(param_2 + 0x3c) = 1;
    }
    v = (double)*(float *)(param_2 + 0x20);
    if (v <= (double)FLOAT_8061a2e8) {
        return;
    }
    if ((double)(float)((double)*(float *)(param_2 + 0x24) * param_1 + (double)FLOAT_8061a2e8) < v) {
        if (v < (double)FLOAT_8061a2f4) {
            return;
        }
        *(float *)(param_2 + 0x20) = (float)(v - (double)FLOAT_8061a2f4);
        return;
    }
    if (*(int *)(param_2 + 0x3c) != 1) {
        return;
    }
    *(int *)(param_2 + 0x38) = *(int *)(param_2 + 0x38) + 1;
    if (*(int *)(param_2 + 0x38) < 2) {
        return;
    }
    *(int *)(param_2 + 0x38) = 0;
}

// ---------------------------------------------------------------------------
// Address: 0x802e7bb0 | Name: arcsel_render_element
// Draws one grid element to the GP FIFO: rotate-Y by the element's animated
// spin, transpose into the active view matrix, then emit (rows-1) triangle
// strips of cols vertices each, translated by param_3 with a fixed 0x7f7f7f5a
// vertex colour.
void arcsel_render_element(u32 param_1, u32 param_2, float *param_3)
{
    float *pRow;
    float *pNext;
    int    texIdx;
    int   *rec;
    float *mtx;
    int    rows;
    int    cols;
    int    strip;
    int    v;
    int    rowOff;
    u64    packed;
    double f1pair;

    (void)param_1; (void)param_2;
    packed = FUN_803826c8();
    rec    = (int *)(unsigned)(packed >> 0x20);
    f1pair = (double)(float)rec[8];
    mtx    = mtx4_pool_alloc();
    mtx4_set_rotation_y((double)((FLOAT_8061a2e4 * (float)((double)(float)rec[8] * f1pair)) /
                                 FLOAT_8061a2e8), mtx);
    mtx4_multiply(mtx, (float *)(unsigned)packed, mtx);
    mtx4_transpose(mtx, (float *)&DAT_8058e3cc);
    texIdx = rec[0xe];
    rowOff = 0;
    rows   = rec[0xb];
    DAT_80612250 = 1;
    cols   = rec[10];
    DAT_80612258 = 0;
    int base = rec[0];
    for (strip = 0; strip < rows - 1; strip++) {
        pRow  = *(float **)(base + rowOff);
        pNext = (float *)((int *)(base + rowOff))[1];
        gx_begin_textured_batch(2, 0x91, 0, (short)(cols << 1), rec[texIdx + 0xc]);
        v = cols;
        if (v > 0) {
            do {
                gx_fifo_push_f(pRow[0] + param_3[0]);
                gx_fifo_push_f(pRow[1] + param_3[1]);
                gx_fifo_push_f(pRow[2] + param_3[2]);
                gx_fifo_push_u(0x7f7f7f5a);
                gx_fifo_push_f(pRow[3]);
                gx_fifo_push_f(pRow[4]);
                gx_fifo_push_f(pNext[0] + param_3[0]);
                gx_fifo_push_f(pNext[1] + param_3[1]);
                gx_fifo_push_f(pNext[2] + param_3[2]);
                gx_fifo_push_u(0x7f7f7f5a);
                pRow  = pRow + 5;
                gx_fifo_push_f(pNext[3]);
                gx_fifo_push_f(pNext[4]);
                pNext = pNext + 5;
                v = v - 1;
            } while (v != 0);
        }
        gx_end_batch();
        rowOff = rowOff + 4;
    }
    FUN_80382714();
}
