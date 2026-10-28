// Batch: batch_863 | Module: arcade-select screen view pan/zoom animation + layout-id predicates
// Address range: 0x80300b58 - 0x803010b0
//
// The screen-view pan/zoom animation subsystem drives an eased camera-like view
// over a preview screen. It keeps two 0x20-byte parameter blocks and a per-screen
// context (DAT_80612be4, the active screen object):
//   DAT_805d5960..0x805d597c  "current" view params snapshot (pos/zoom/frame)
//   DAT_805d5980..0x805d599c  animation target block (goal pos/zoom/frame index)
// DAT_80612be0 is the sub-frame accumulator; DAT_80612bdc is the pan-target scalar.
// Status bit 0x8000000 in ctx+0x25c marks "view animation in progress".
//
//   avanim_set_pan_target    0x80300b58  store the raw pan target scalar.
//   avanim_pan_target_f      0x80300b60  int->float of the pan target (magic-bias).
//   avanim_play_move_sound   0x80300b8c  play a move SFX if ctx+0x234 is set.
//   avanim_focus_view        0x80300bd8  compute the target view pos/zoom for a
//                                        focus object (mode 2 vs other pick a scale
//                                        pair) and either commit it (avanim_begin_to)
//                                        or write it to an out param.
//   avanim_snapshot_current  0x80300ce4  copy the live view params into the current
//                                        block (DAT_805d5960..).
//   avanim_begin_snap        0x80300d90  copy current->target, set in-progress bit,
//                                        reset the sub-frame accumulator.
//   avanim_begin_to          0x80300ddc  set up an animation to an explicit target
//                                        (pos - offset*zoom bias), set in-progress.
//   avanim_tick              0x80300ecc  per-frame ease of the live params toward the
//                                        target (dead-zone clamp FLOAT_8061a608, step
//                                        scale FLOAT_806108e8); steps the frame index
//                                        toward DAT_805d599c; clears the bit on
//                                        convergence.
//   avanim_layout_is_0x22    0x8030108c  predicate: screen index's layout id == 0x22.
//   avanim_layout_is_0x21    0x803010b0  predicate: screen index's layout id == 0x21.
//
// Named cross-module helpers (sound_*, leaf_31e8) resolve through the shared headers;
// only raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get per-file externs.
//
// NON-MATCHING: the PPC int->float magic-bias idiom (CONCAT44(0x43300000, x^0x80000000)
// - DOUBLE_8061a5e0) is preserved verbatim; the eased-view math keeps the original
// double intermediates. countLeadingZeros(k - x) >> 5 is the branchless (x == k) test.

#include "common.h"
#include "hud.h"

extern "C" {

// ---- animation state blocks (current snapshot @0x805d5960, target @0x805d5980) --
extern float DAT_805d5960;
extern float DAT_805d5964;
extern float DAT_805d5968;
extern float DAT_805d596c;
extern float DAT_805d5970;
extern float DAT_805d5974;
extern float DAT_805d5978;
extern u32   DAT_805d597c;

extern float DAT_805d5988;   // target block: zoom denom
extern float DAT_805d598c;   // target block: zoom numer
extern float DAT_805d5990;   // target block: pos x
extern float DAT_805d5994;   // target block: pos y
extern float DAT_805d5998;   // target block: zoom
extern u32   DAT_805d599c;   // target block: frame index

// ---- per-screen context + scalars -------------------------------------------
extern int DAT_80612be4;     // active screen object base
extern int DAT_80612be0;     // sub-frame accumulator
extern u32 DAT_80612bdc;     // raw pan target scalar
extern int DAT_806108e0;     // per-tick sub-frame increment

// live view params (read by tick / snapshot)
extern float FLOAT_80612c68;
extern float FLOAT_80612c74;
extern float FLOAT_80612c78;
extern u32   DAT_80612c7c;
extern u32   DAT_80612c80;
extern u32   DAT_80612c84;
extern u32   DAT_80612c88;

// tuning constants
extern double DOUBLE_8061a5e0;
extern float  FLOAT_8061a5e8;
extern float  FLOAT_8061a5ec;
extern float  FLOAT_8061a5f0;
extern float  FLOAT_8061a5f4;
extern float  FLOAT_8061a5f8;
extern float  FLOAT_8061a5fc;
extern float  FLOAT_8061a600;
extern float  FLOAT_8061a604;
extern float  FLOAT_8061a608;
extern float  FLOAT_806108e8;
extern float  FLOAT_80611a64;
extern float  FLOAT_80611a68;
extern float  FLOAT_80611a6c;
extern float  FLOAT_80611a70;

// layout-index table (0x62 stride short), shared with arcsel_get_layout_desc
extern short DAT_80477bca;

// raw cross-module helpers
void FUN_803151e4(double dx, double dy);   // apply view pan delta
void FUN_80314f94(void);                    // commit/refresh view

} // extern "C" (raw symbols) — function definitions below use C++ linkage to
  //                            match generated_decls.h

// ---------------------------------------------------------------------------
// 0x80300b58  avanim_set_pan_target
// ---------------------------------------------------------------------------
void avanim_set_pan_target(u32 target)
{
    DAT_80612bdc = target;
}

// ---------------------------------------------------------------------------
// 0x80300b60  avanim_pan_target_f
// int->float of the pan target (PPC magic-bias idiom).
// ---------------------------------------------------------------------------
double avanim_pan_target_f(void)
{
    return (double)(float)((double)CONCAT44(0x43300000, DAT_80612bdc ^ 0x80000000) - DOUBLE_8061a5e0);
}

// ---------------------------------------------------------------------------
// 0x80300b8c  avanim_play_move_sound
// Play a move SFX (sound id) if the screen ctx has its enable field (ctx+0x234).
// ---------------------------------------------------------------------------
void avanim_play_move_sound(int soundId)
{
    int local_38[13];
    if (*(int*)(DAT_80612be4 + 0x234) != 0) {
        sound_init_play_params((int)local_38);
        local_38[0] = soundId;
        sound_play_request(local_38);
    }
}

// ---------------------------------------------------------------------------
// 0x80300bd8  avanim_focus_view
// Compute the target view position/zoom for a focus object; mode 2 vs other
// selects a scale pair. Commit via avanim_begin_to, or write to pOut if given.
// ---------------------------------------------------------------------------
void avanim_focus_view(float* pParams, int pFocusObj, int mode, int* pOut)
{
    float fVar1 = FLOAT_8061a5ec;
    float fVar2 = FLOAT_8061a5e8;
    if (mode != 2) {
        fVar1 = FLOAT_8061a5f4;
        fVar2 = FLOAT_8061a5f0;
    }
    double dVar5 = (double)fVar2;
    double dVar4 = (double)fVar1;
    if (pFocusObj != 0) {
        avanim_set_pan_target((u32)*(float*)(pFocusObj + 0x4c));
    }
    double dVar3 = avanim_pan_target_f();
    float fVar1b = FLOAT_80611a68 * FLOAT_8061a5f8 * (float)(dVar3 + dVar5) + FLOAT_80611a70;
    dVar3 = (double)fVar1b;
    dVar4 = dVar4 * (double)FLOAT_80611a64 + (double)FLOAT_80611a6c;
    double dVar5b = (double)(float)dVar4;
    if (pOut == (int*)0) {
        avanim_snapshot_current();
        avanim_begin_to((double)pParams[0], (double)pParams[1], dVar3, dVar5b, (u32)(int)pParams[2]);
    } else {
        pOut[0] = (int)fVar1b;
        pOut[1] = (int)dVar4;
    }
}

// ---------------------------------------------------------------------------
// 0x80300ce4  avanim_snapshot_current
// Snapshot the live view params into the current block (DAT_805d5960..).
// ---------------------------------------------------------------------------
void avanim_snapshot_current(void)
{
    DAT_805d5960 = (float)((double)CONCAT44(0x43300000, DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a5e0);
    DAT_805d5964 = (float)((double)CONCAT44(0x43300000, DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a5e0);
    DAT_805d5968 = (float)((double)CONCAT44(0x43300000, DAT_80612c80 ^ 0x80000000) - DOUBLE_8061a5e0);
    DAT_805d596c = (float)((double)CONCAT44(0x43300000, DAT_80612c7c ^ 0x80000000) - DOUBLE_8061a5e0);
    DAT_805d5970 = FLOAT_80612c78;
    DAT_805d5974 = FLOAT_80612c74;
    DAT_805d5978 = FLOAT_80612c68;
    DAT_805d597c = *(u32*)(DAT_80612be4 + 0x264);
}

// ---------------------------------------------------------------------------
// 0x80300d90  avanim_begin_snap
// Copy current->target block, set the in-progress bit, reset the accumulator.
// ---------------------------------------------------------------------------
void avanim_begin_snap(void)
{
    leaf_31e8(0x805d5980, 0x805d5960, 0x20);
    *(u32*)(DAT_80612be4 + 0x25c) = *(u32*)(DAT_80612be4 + 0x25c) | 0x8000000;
    DAT_80612be0 = 0;
}

// ---------------------------------------------------------------------------
// 0x80300ddc  avanim_begin_to
// Set up an animation to an explicit target: copy current->target, then bias the
// target pos by the focus offset scaled by zoom; set the in-progress bit.
// ---------------------------------------------------------------------------
void avanim_begin_to(double posX, double posY, double zoomX, double zoomY, u32 frameIdx)
{
    leaf_31e8(0x805d5980, 0x805d5960, 0x20);
    DAT_805d5998 = FLOAT_8061a5fc;
    DAT_805d5990 = (float)(posX - (double)(float)(zoomX * (double)(FLOAT_8061a5fc / DAT_805d5988)));
    DAT_805d5994 = (float)(posY - (double)(float)(zoomY *
                    (double)((FLOAT_8061a5fc * (DAT_805d598c / DAT_805d5988)) / DAT_805d598c)));
    DAT_805d599c = frameIdx;
    *(u32*)(DAT_80612be4 + 0x25c) = *(u32*)(DAT_80612be4 + 0x25c) | 0x8000000;
    DAT_80612be0 = 0;
}

// ---------------------------------------------------------------------------
// 0x80300ecc  avanim_tick
// Per-frame ease of the live view params toward the target block; dead-zone clamp
// and step scaling; steps the frame index toward DAT_805d599c; clears the bit on
// convergence.
// ---------------------------------------------------------------------------
void avanim_tick(void)
{
    if ((*(u32*)(DAT_80612be4 + 0x25c) & 0x8000000) != 0) {
        double dVar5 = (double)(FLOAT_8061a600 * (DAT_805d5990 - FLOAT_80612c78));
        double dVar4 = (double)(FLOAT_8061a600 * (DAT_805d5994 - FLOAT_80612c74));
        double dVar3 = (double)(FLOAT_8061a600 * (DAT_805d5998 - FLOAT_80612c68));
        int iVar1 = (int)DAT_805d599c - *(int*)(DAT_80612be4 + 0x264);
        if (ABS(dVar5) < (double)FLOAT_8061a608) dVar5 = (double)FLOAT_8061a604;
        if (ABS(dVar4) < (double)FLOAT_8061a608) dVar4 = (double)FLOAT_8061a604;
        if (ABS(dVar3) < (double)FLOAT_8061a608) dVar3 = (double)FLOAT_8061a604;
        FUN_803151e4((double)(float)(dVar5 * (double)FLOAT_806108e8),
                     (double)(float)(dVar4 * (double)FLOAT_806108e8));
        FLOAT_80612c68 = (float)(dVar3 * (double)FLOAT_806108e8 + (double)FLOAT_80612c68);
        DAT_80612be0 = DAT_80612be0 + DAT_806108e0;
        if (10 < DAT_80612be0) {
            if (iVar1 < 1) {
                if (iVar1 < 0) {
                    *(int*)(DAT_80612be4 + 0x264) = *(int*)(DAT_80612be4 + 0x264) + -1;
                }
            } else {
                *(int*)(DAT_80612be4 + 0x264) = *(int*)(DAT_80612be4 + 0x264) + 1;
            }
            int iStep = -2;
            if (-2 < *(int*)(DAT_80612be4 + 0x264)) {
                iStep = *(int*)(DAT_80612be4 + 0x264) + -1;
            }
            *(int*)(DAT_80612be4 + 0x268) = iStep;
            DAT_80612be0 = DAT_80612be0 + -10;
        }
        double dVar2 = (double)FLOAT_8061a604;
        if (((dVar2 == dVar5) && (dVar2 == dVar4)) &&
            (*(int*)(DAT_80612be4 + 0x264) == (int)DAT_805d599c) && (dVar2 == dVar3)) {
            *(u32*)(DAT_80612be4 + 0x25c) = *(u32*)(DAT_80612be4 + 0x25c) & 0xf7ffffff;
        }
        FUN_80314f94();
    }
}

// ---------------------------------------------------------------------------
// 0x8030108c  avanim_layout_is_0x22
// Predicate: the screen index's layout id (DAT_80477bca, 0x62 stride) == 0x22.
// ---------------------------------------------------------------------------
unsigned int avanim_layout_is_0x22(int index)
{
    return countLeadingZeros(0x22 - (short)(&DAT_80477bca)[index * 0x62]) >> 5;
}

// ---------------------------------------------------------------------------
// 0x803010b0  avanim_layout_is_0x21
// Predicate: the screen index's layout id (DAT_80477bca, 0x62 stride) == 0x21.
// ---------------------------------------------------------------------------
unsigned int avanim_layout_is_0x21(int index)
{
    return countLeadingZeros(0x21 - (short)(&DAT_80477bca)[index * 0x62]) >> 5;
}
