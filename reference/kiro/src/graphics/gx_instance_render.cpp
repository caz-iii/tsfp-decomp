// ============================================================================
// Address range: 0x80182184 - 0x80183b40 | batch_472 | Date: 2026-09-06
// Subsystem: graphics (per-instance mesh submit + light-glow billboard build + present).
// tag gx-instrender.
//
// The render-instance submit layer plus light-glow billboard construction and a couple of
// GX helpers.  render_instance_submit_pass is the heavy per-frame worker that walks a
// sub-object's strip list, programs the TEV indirect stages, optionally builds a
// screen-space heat/deform warp grid, binds per-strip materials, and issues the GX
// display lists; render_instance_draw_two_pass wraps a base + glow overlay pass.
// light_glow_build_record/billboard construct the additive glow-billboard records the
// light-probe system emits.
//
//   gx_present_loading_frame       0x80182184  present a single frame during loading
//   gx_set_frame_flags             0x8018259c  set frame-stall / in-progress flags
//   light_glow_lut_index           0x801825b8  x*3 & -(x<10) glow palette index
//   light_glow_build_billboard     0x801825d0  build one additive glow billboard record
//   light_glow_build_record        0x80182740  build the 3-source glow record for a light
//   render_instance_submit_pass    0x80182934  MASSIVE sub-object strip submit + deform grid
//   render_instance_draw           0x80183984  thunk -> two-pass
//   render_instance_draw_two_pass  0x801839a8  base pass + additive glow overlay pass
//   gx_load_env_matrix_by_bit      0x80183b18  thunk (1<<bit) -> flagged loader
//   gx_load_env_matrices_flagged   0x80183b40  load env/tex matrices from a bone-flag list

#include "common.h"
#include "generated_decls.h"
#include "graphics.h"

// ---- typed aliases for helpers whose generated_decls proto is void / simplified ------
typedef float (*ExprEvalStackFn)(void);
#define EXPR_EVAL_STACK ((ExprEvalStackFn)expr_evaluate_stack)

typedef unsigned int (*MemAdvSecFn)(int);
#define MEM_ADV_SEC ((MemAdvSecFn)mem_advance_secondary)

typedef float *(*RenderPropEvalFn)(int *prop, int lightSet, int channel);
#define RENDER_PROP_EVAL ((RenderPropEvalFn)render_prop_evaluate_channel)

// ---- raw runtime helpers ----------------------------------------------------
extern unsigned long long FUN_803826b4(void);      // spawn-frame scratch (pair)
extern unsigned long long FUN_803826dc(void);       // spawn-frame scratch (pair)
extern void        FUN_80382728(void);              // spawn-frame pop
extern void        FUN_80382700(void);              // spawn-frame pop
extern unsigned long long FUN_803c6b7c(void);       // GX flush FIFO (returns frame handle)
extern unsigned int FUN_803c766c(int pass);         // GX set channel count / begin pass
extern unsigned int mem_advance_secondary_aligned(unsigned int size);
extern unsigned int FUN_8039955c(unsigned int base, unsigned int size); // close display list
extern void        FUN_801863ec(int entity, int lightSet, int matTable, short *strip);
extern void        FUN_8018d7ac(void);              // deform-grid warp precompute
extern void        FUN_803cb218(unsigned int slot, unsigned int dst); // GX load tex-mtx idx
extern void        FUN_803cb248(float *mtx, unsigned int dst);         // GX load tex-mtx
extern void        FUN_803cb298(unsigned int idx);  // GX select env-tex matrix
extern void        FUN_803c8610(int rec, void *color);       // glow set colour
extern void        FUN_803c84e4(double x, double y, double z, int rec); // glow set pos
extern void        FUN_803c83e8(double a, double b, double c, double d, double e, double f, int rec); // glow set size/uv
extern unsigned int FUN_801c0894(void);             // fx enable flags
extern unsigned long long __save_gpr_28(void);
extern void        __restore_gpr_28(void);
extern unsigned long long __save_gpr_25(void);
extern void        __restore_gpr_25(void);

// ---- forward decls (same-file cross calls) ----------------------------------
void render_instance_draw_two_pass(unsigned long long p1, double p2);
void gx_load_env_matrices_flagged(unsigned int a, unsigned int b, unsigned int mask, int slot);
void render_instance_submit_pass(unsigned int p1, unsigned int p2, int passIdx, unsigned int subIdx);

// ---- globals ----------------------------------------------------------------
extern unsigned char DAT_80612360;   // frame-stall request flag
extern char  DAT_80612361;           // frame-in-progress flag
extern int   DAT_80611d54;           // active render context
extern float DAT_80412660;           // glow r scale
extern float DAT_80412664;           // glow g scale
extern float DAT_80412668;           // glow b scale
extern float FLOAT_8061670c;         // glow intensity scale
extern float FLOAT_80616708;         // glow colour clamp max (255)
extern float FLOAT_80616710;         // glow uv/size const a
extern float FLOAT_80616714;         // glow uv/size const b
extern float DAT_8041266c;           // glow tint r scale
extern float DAT_80412670;           // glow tint g scale
extern float DAT_80412674;           // glow tint b scale
extern float DAT_80412678;           // glow tint r bias
extern float DAT_8041267c;           // glow tint g bias
extern float DAT_80412680;           // glow tint b bias

// render_instance_submit_pass globals
extern int   DAT_806123d4;           // per-frame heat-warp cache stamp
extern int   DAT_80611928;           // frame counter
extern int   DAT_806125fc;           // fx enable mask A
extern int   DAT_80612600;           // fx enable mask B
extern int   DAT_80612614;           // game-mode id
extern int   DAT_805cc614;           // sub-mode id
extern int   DAT_80611b48;           // heat-warp active flag
extern int   DAT_80611b40;           // saved env-tex a
extern int   DAT_80611b44;           // saved env-tex b
extern int   DAT_80611b28;           // deform-grid entity slot
extern float FLOAT_806123d0;         // deform amount
extern int   DAT_80610444;           // saved sub-material id
extern int   DAT_80610440;           // saved blend mode
extern int   DAT_80610448;           // saved state c0
extern int   DAT_80611570;           // draw-stat tri count
extern int   DAT_80611574;           // draw-stat vert count
extern int   DAT_8061157c;           // draw-stat opaque count
extern int   DAT_80611588;           // draw-stat special count
extern int   *DAT_80610a20;          // material pool table
extern int   DAT_80612254;           // active material record ptr
extern int   DAT_80412740;           // fallback normal table base
extern int   DAT_80412744;
extern int   DAT_80412748;
extern float FLOAT_80616718;         // vtx dequant scale
extern float FLOAT_8061671c;         // normal dequant scale
extern float FLOAT_80616720;         // uv branch threshold
extern float FLOAT_80616724;         // uv branch bias
extern float FLOAT_80616728;         // extrude scale
extern float FLOAT_8061672c;         // extrude len min
extern float FLOAT_80616730;         // rsqrt refine const
extern float FLOAT_80616734;         // output quantize scale
extern double DOUBLE_80616738;       // int->float magic bias
extern int   DAT_80610a62;           // fallback normal ptr sentinel

// gx_present_loading_frame (0x80182184)
// Present a single frame during a loading/wait phase: flush pending occlusion query,
// re-establish present state, flush the FIFO, wait for the GPU, and flip the buffer.
// Called from inner_game_loop and the resource loader.
void gx_present_loading_frame(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                              unsigned long long p4, unsigned long long p5, unsigned long long p6,
                              unsigned long long p7, unsigned long long p8)
{
    occ_query_flush_guard();
    gx_post_present_setup();
    unsigned long long fh = FUN_803c6b7c();
    gx_frame_flush_wait(fh, p2, p3, p4, p5, p6, p7, p8);
    vi_present_frame(1);
}

// gx_set_frame_flags (0x8018259c)
// Set the frame-stall request (param_1) and/or frame-in-progress (param_2) flags; -1
// leaves a field unchanged.
void gx_set_frame_flags(int stall, int inProgress)
{
    if (stall != -1) {
        DAT_80612360 = (unsigned char)stall;
    }
    if (inProgress == -1) {
        return;
    }
    DAT_80612361 = (char)inProgress;
}

// light_glow_lut_index (0x801825b8)
// Glow-palette index: x*3 when x<10, else 0 (branchless via -(x<10) mask).
unsigned int light_glow_lut_index(unsigned int x)
{
    return x * 3 & -(unsigned int)(x < 10);
}

// light_glow_build_billboard (0x801825d0)
// Build one additive glow-billboard record: modulate the source colour by per-channel
// scales, clamp to [0,255], skip if fully black; else project the world point through the
// view matrix and program the billboard colour/position/size. Returns 1 if emitted.
unsigned int light_glow_build_billboard(int rec, float *worldPos, float *srcColor)
{
    float r = FLOAT_8061670c * srcColor[0] * DAT_80412660;
    if (r >= FLOAT_80616708) r = FLOAT_80616708;
    unsigned char cr = (unsigned char)(int)r;

    float g = FLOAT_8061670c * srcColor[1] * DAT_80412664;
    if (g >= FLOAT_80616708) g = FLOAT_80616708;
    unsigned char cg = (unsigned char)(int)g;

    float b = FLOAT_8061670c * srcColor[2] * DAT_80412668;
    if (b >= FLOAT_80616708) b = FLOAT_80616708;
    unsigned char cb = (unsigned char)(int)b;

    if ((cr == 0) && (cg == 0) && (cb == 0)) {
        return 0;
    }
    float projected[4];
    unsigned int packed = ((unsigned int)cr << 24) | ((unsigned int)cg << 16) |
                          ((unsigned int)cb << 8) | 0xff;
    mtx4_transform_point4(*(float **)(DAT_80611d54 + 0x208), worldPos, projected);
    FUN_803c8610(rec, &packed);
    FUN_803c84e4((double)projected[0], (double)projected[1], (double)projected[2], rec);
    double sz = (double)FLOAT_80616714;
    FUN_803c83e8((double)FLOAT_80616710, sz, sz, (double)FLOAT_80616710, sz, sz, rec);
    return 1;
}

// light_glow_build_record (0x80182740)
// Build the glow record for a light source: resolve its world position (optionally through
// the inverse view matrix), allocate a 0xc0-byte glow-billboard block, emit up to 3
// billboards (light_glow_build_billboard), and pack a tint colour byte-triple.  From the
// light-probe lighting update. r6/r7 arrive as arrays of colour/position float pointers.
void light_glow_build_record(void)
{
    unsigned long long frame = FUN_803826dc();
    int *rec = (int *)(unsigned int)frame;      // r3 = glow record
    int lightBase = *rec;
    int attach = rec[4];
    // r6 = per-source position ptr table, r7 = per-source colour ptr table (register args)
    // (kept as reads from the frame layout used by the callers)
    // resolve world position into local
    float world[4];
    float local[4];
    if (attach == 0) {
        if (*(char *)(lightBase + 0x31) == '\x01') {
            mtx4_invert(local, *(float **)(DAT_80611d54 + 0x208));
            mtx4_transform_point4(local, (float *)(lightBase + 100), world);
        } else {
            world[0] = *(float *)(lightBase + 100);
            world[1] = *(float *)(lightBase + 0x68);
            world[2] = *(float *)(lightBase + 0x6c);
        }
    } else {
        world[0] = *(float *)(attach + 0x30) + *(float *)(lightBase + 100);
        world[1] = *(float *)(attach + 0x34) + *(float *)(lightBase + 0x68);
        world[2] = *(float *)(attach + 0x38) + *(float *)(lightBase + 0x6c);
        if (*(char *)(lightBase + 0x31) == '\x01') {
            mtx4_invert(local, *(float **)(DAT_80611d54 + 0x208));
            mtx4_transform_point4(local, world, world);
        }
    }

    short emitted = 0;
    int recOff = 0;
    int block = (int)MEM_ADV_SEC(0xc0);
    rec[0x1c] = block;
    // NOTE: per-source pos/color pointer tables arrive in r6/r7; the loop iterates 3 sources.
    // We express them via the frame's stored table pointers to keep the body faithful.
    float **posTbl = *(float ***)((int)&frame);   // placeholder resolved by caller registers
    (void)posTbl;
    for (int i = 0; i < 3; i++) {
        // pos/color pointers are the i-th entries of the r6/r7 tables in the original;
        // expressed here through the light record's source arrays.
        float *color = (float *)(*(int *)((int)rec + 0x40 + i * 4)); // r7 table entry
        float *pos   = (float *)(*(int *)((int)rec + 0x50 + i * 4)); // r6 table entry
        if (light_glow_build_billboard(block + recOff, pos, color) != 0) {
            emitted = emitted + 1;
            recOff = recOff + 0x40;
        }
    }
    *(short *)(rec + 0x1d) = emitted;

    float *tint = *(float **)(lightBase + 0xc);
    *(char *)(rec + 0x1b)       = (char)(int)(tint[0] * DAT_8041266c + DAT_80412678);
    *(char *)((int)rec + 0x6d)  = (char)(int)(tint[1] * DAT_80412670 + DAT_8041267c);
    *(char *)((int)rec + 0x6e)  = (char)(int)(tint[2] * DAT_80412674 + DAT_80412680);
    *(unsigned char *)((int)rec + 0x6f) = 0xff;
    FUN_80382728();
}

// render_instance_draw (0x80183984) - thunk to the two-pass renderer.
void render_instance_draw(unsigned long long p1, double p2)
{
    render_instance_draw_two_pass(p1, p2);
}

// render_instance_draw_two_pass (0x801839a8)
// Render a sub-object instance in two passes: a base pass and an additive glow/outline
// overlay pass, choosing sub-material and blend mode by the pass selector, then restoring
// the previous state.  Delegates the actual submission to render_instance_draw_pass.
void render_instance_draw_two_pass(unsigned long long p1, double p2)
{
    unsigned long long frame = __save_gpr_28();
    unsigned int hi = (unsigned int)(frame >> 0x20);
    int sel = (int)frame;

    unsigned int v = FUN_803c766c(1);
    int savedMat = 0, savedBlend = 0;
    if (sel == 1) {
        savedMat = gx_set_sub_material_id(1);
        savedBlend = gx_set_blend_mode(2);
    } else if (sel < 1) {
        if (sel >= 0) {
            savedMat = gx_set_sub_material_id(0);
            savedBlend = gx_set_blend_mode(4);
        }
    } else if (sel < 3) {
        savedMat = gx_set_sub_material_id(1);
        savedBlend = gx_set_blend_mode(5);
    }
    render_instance_draw_pass((int *)v, (float)p2, (int)hi, sel, 0);

    v = FUN_803c766c(2);
    if (sel == 1) {
        gx_set_state_c0(0x1f);
        gx_set_sub_material_id(0xf);
        gx_set_blend_mode(2);
    } else if (sel < 1) {
        if (sel >= 0) {
            gx_set_state_c0(0x1f);
            gx_set_sub_material_id(0x10);
            gx_set_blend_mode(2);
        }
    } else if (sel < 3) {
        gx_set_state_c0(0x1f);
        gx_set_sub_material_id(0xf);
        gx_set_blend_mode(5);
    }
    render_instance_draw_pass((int *)v, (float)p2, (int)hi, sel, 0);

    gx_set_blend_mode(savedBlend);
    gx_set_sub_material_id(savedMat);
    FUN_803c766c(0);
    __restore_gpr_28();
}

// gx_load_env_matrix_by_bit (0x80183b18) - thunk that maps a bit index to a mask.
void gx_load_env_matrix_by_bit(unsigned int a, unsigned int b, int bitIndex, int slot)
{
    gx_load_env_matrices_flagged(a, b, 1 << bitIndex, slot);
}

// gx_load_env_matrices_flagged (0x80183b40)
// Walk a bone/env matrix flag list from the spawn-frame scratch (r25:r26 pair): for each
// entry whose flag byte matches param_mask, either copy the identity/base matrix into the
// shared env-tex reset slot (entry 0) or load it into GX tex-matrix slot uVar8 via
// FUN_803cb218/248.  slot indices advance by 3 per entry.
void gx_load_env_matrices_flagged(unsigned int a, unsigned int b, unsigned int mask, int slot)
{
    (void)a; (void)b;
    unsigned long long frame = __save_gpr_25();
    unsigned char *p = (unsigned char *)(frame >> 0x20);
    if ((p != (unsigned char *)0xffffffff) && (0x9ffffffffULL < frame)) {
        unsigned char count = *p;
        p = p + 1;
        int table = *(int *)((int)frame + slot * 4 + 0x20);
        if (table != 0) {
            unsigned int dst = 0;
            for (int i = 0; i < (int)(unsigned int)count; i++) {
                unsigned char flag = p[0];
                unsigned char idx = p[1];
                p = p + 2;
                if ((flag & mask) != 0) {
                    if (i == 0) {
                        // copy the 8-word matrix into the shared env-tex reset slot 0x8058e3c8
                        unsigned int *src = (unsigned int *)(table + (unsigned int)idx * 0x40 - 4);
                        unsigned int *out = (unsigned int *)0x8058e3c8;
                        for (int w = 8; w != 0; w--) {
                            unsigned int lo = src[1];
                            unsigned int hi2 = src[2];
                            src = src + 2;
                            out[1] = lo;
                            out = out + 2;
                            out[0] = hi2;
                        }
                        *(int *)0x80612250 = 1;   // DAT_80612250 dirty
                        *(int *)0x80612258 = 0;    // DAT_80612258 clear reflect latch
                    } else {
                        FUN_803cb218((unsigned int)idx, dst);
                        FUN_803cb248((float *)(table + (unsigned int)idx * 0x40), dst);
                    }
                }
                dst = dst + 3;
            }
        }
    }
    __restore_gpr_25();
}

// ---- additional raw helpers/globals for render_instance_submit_pass ---------
extern unsigned int sprite_get_texture_id(int index);

// render_instance_submit_pass (0x80182934)
// The heavy per-frame sub-object mesh submit worker (called by render_instance_submit_node).
// For the given sub-object (p1:p2 packed = entity index : instance ptr) and pass index it:
//  - lazily recomputes the heat-warp active flag (fx bit 0x8000) once per frame;
//  - resolves the strip list, material table, and optional per-strip deform/indirect
//    channel data (piVar21);
//  - saves the current material/blend/state, programs the TEV indirect stages from the
//    per-strip indirect-matrix set (gx_indirect_matrix_update for slots 9/0xb/0xd/0xa/0xc),
//    and builds a screen-space heat/deform warp grid when a strip is flagged (type 3/4),
//    projecting each vertex through the inverse object matrix and re-quantizing into a
//    scratch normal buffer;
//  - per strip: sets the blend tag, binds the material (render_instance_apply_material or
//    a fallback / streamed sprite 0x326 / heat-warp sprite 0x3f7), evaluates the render
//    prop channel to load an env-tex matrix, and loads the position matrix + display list;
//  - restores saved state and, on the primary pass with a shadow flag, projects shadows.
// Structurally-faithful non-matching port; the FP register-spill shadow locals (in_f21..
// in_f31 / in_psNN_1) are ABI artifacts and are dropped.
void render_instance_submit_pass(unsigned int p1, unsigned int p2, int passIdx, unsigned int subIdx)
{
    float warpGridA[16];
    float warpGridB[16];
    float invObj[16];
    float localMtx[16];
    float shadowMv[16];
    float shadowProj[16];
    float tmpVec[4];
    (void)p1; (void)p2;

    unsigned long long frame = FUN_803826b4();
    int *pInst = (int *)(frame >> 0x20);   // r3 = instance table ptr
    int idx = (int)frame;                  // r4 = entity index
    unsigned int savedFog = gx_get_fog_params();
    int haveProp = 0;
    int savedEnvA = DAT_80611b40;
    int savedEnvB = DAT_80611b44;
    float *lastPropMtx = (float *)0;

    if (DAT_806123d4 != DAT_80611928) {
        int active = 0;
        unsigned int fx = FUN_801c0894();
        if (((DAT_806125fc & DAT_80612600 & fx & 0x8000) != 0) &&
            ((DAT_80612614 != 0x65) || (DAT_805cc614 == 5))) {
            active = 1;
        }
        DAT_806123d4 = DAT_80611928;
        DAT_80611b48 = active;
    }

    int *pModel = (int *)pInst[1];
    int lightSet = pInst[0x79];
    int entity = *pInst;
    int *pSub = pModel + *pModel * -0x30 + idx * 0x30;
    if (lightSet == 0) lightSet = pModel[4];
    int matTable = pInst[0x7a];
    if (matTable == 0) matTable = pModel[5];

    int *pChannel;
    if (pInst[0x6c] == 0) {
        pChannel = (int *)0;
    } else {
        pChannel = (int *)(pInst[0x6c] + idx * 0x3c + passIdx * 0x14);
    }
    if (pSub[0x29] != 0) {
        FLOAT_806123d0 = EXPR_EVAL_STACK();
    }
    DAT_80610444 = (int)gx_get_sub_material_id();
    DAT_80610440 = gx_get_blend_mode();
    DAT_80610448 = gx_get_state_c0();

    if ((pChannel != (int *)0) &&
        ((pChannel[0] != 0) || (pChannel[1] != 0) || (pChannel[2] != 0) ||
         (pChannel[4] != 0) || (pChannel[3] != 0))) {
        haveProp = 1;
    }
    int lightSetB = pInst[subIdx + 8];
    unsigned int stripFlag = (unsigned int)(((unsigned int)*(unsigned char *)((int)pSub + 0x6b) & (0x10 << passIdx)) != 0);

    if (passIdx == 0) {
        DAT_80611570 = DAT_80611570 + (unsigned int)*(unsigned short *)(pSub + 0x19);
        DAT_80611574 = DAT_80611574 + (unsigned int)*(unsigned short *)((int)pSub + 0x66);
        if ((*(int *)(DAT_80611d54 + 0x214) == 0) && (*(int *)(entity + 0x20) == 2)) {
            DAT_8061157c = DAT_8061157c + (unsigned int)*(unsigned short *)((int)pSub + 0x66);
            if (*(int *)(DAT_80611d54 + 0x2a0) == *(unsigned short *)(entity + 0x94) - 0x8fe) {
                DAT_80611588 = DAT_80611588 + (unsigned int)*(unsigned short *)((int)pSub + 0x66);
            }
        }
    }

    if ((stripFlag == 0) || ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 2) == 0)) {
        gx_set_indirect_blend_mode(0);
    } else {
        gx_set_indirect_blend_mode(1);
    }

    short *pStrip = (short *)pSub[passIdx + 0x15];
    if (*(short *)((int)pInst + 0x76) != 0) {
        DAT_80611b28 = idx;
        FUN_8018d7ac();
    }

    if (pStrip != (short *)0) {
        int posMtx = pSub[passIdx * 5 + 1];
        int normMtx = pSub[passIdx * 5 + 3];
        unsigned int noNormal = pModel[3] & 0x10;
        unsigned int texMtx = pSub[passIdx * 5 + 2];
        unsigned int uvStride = 4;
        unsigned int packedNorm = pModel[3] & 0x40;
        unsigned int nrmMtx2 = pSub[passIdx * 5 + 4];
        unsigned int *pDL = (unsigned int *)pSub[passIdx + 0x2c];
        int gridBuilt = 0;
        int didWarp = 0;

        if ((pChannel != (int *)0) && haveProp) {
            int nVerts = collision_count_total_vertices((int)pStrip);
            if ((pChannel[1] != 0) || ((texMtx == 0) && (pChannel[2] != 0)) ||
                ((nrmMtx2 == 0) && (pChannel[4] != 0))) {
                noNormal = 0;
                FUN_8039955c(pChannel[1], (unsigned int)(nVerts * 0xc + 0x1f) & 0xffffffe0);
            }
            posMtx = pChannel[1];  if (posMtx == 0) posMtx = pSub[passIdx * 5 + 1];
            texMtx = pChannel[2];  if (texMtx == 0) texMtx = pSub[passIdx * 5 + 2];
            unsigned int n4 = pChannel[4];
            nrmMtx2 = (n4 == 0) ? (unsigned int)pSub[passIdx * 5 + 4] : n4;
            normMtx = pChannel[3]; if (normMtx == 0) normMtx = pSub[passIdx * 5 + 3];
            if (n4 != 0) {
                FUN_8039955c(n4, (unsigned int)(nVerts * 4 + 0x1f) & 0xffffffe0);
            }
            if (pChannel[2] != 0) {
                FUN_8039955c(pChannel[2], (unsigned int)(nVerts * 0xc + 0x1f) & 0xffffffe0);
            }
            if (pChannel[3] != 0) {
                packedNorm = 0;
                FUN_8039955c(pChannel[3], (unsigned int)(nVerts * 0x10 + 0x1f) & 0xffffffe0);
                uvStride = 0x10;
            }
        }

        unsigned int stageMask = 3;
        if (noNormal == 0) gx_indirect_matrix_update(9, posMtx, 0xc);
        else               gx_indirect_matrix_update(9, posMtx, 6);
        if (nrmMtx2 != 0) { stageMask = 0xf; gx_indirect_matrix_update(0xb, nrmMtx2, 4); }
        if (texMtx != 0) {
            stageMask = stageMask | 0x30;
            if ((texMtx & 2) == 0) gx_indirect_matrix_update(0xd, texMtx, 8);
            else                   gx_indirect_matrix_update(0xd, texMtx, 4);
        }
        if (normMtx != 0) {
            stageMask = stageMask | 0x180;
            if (packedNorm == 0) gx_indirect_matrix_update(10, normMtx, uvStride);
            else                 gx_indirect_matrix_update(10, (unsigned int)(uintptr_t)&DAT_80412740, 0xc);
        }
        if (stripFlag != 0) {
            stageMask = stageMask | 0x600;
            if (nrmMtx2 == 0) gx_indirect_matrix_update(0xc, 0x80610a62, 4);
            else              gx_indirect_matrix_update(0xc, nrmMtx2 + 2, 4);
        }
        if ((*(char *)(pSub + 0xf) != '\0') || ((pModel[3] & 0x10000U) != 0)) {
            stageMask = stageMask | 0x40;
        }
        gx_indirect_stage_config();

        if (*(char *)(pSub + 0xf) == '\0') {
            if (((stageMask & 0x40) == 0) && (0 < idx) && (*(int *)(entity + 0x20) != 2) &&
                (1 < pModel[1])) {
                FUN_803cb298(light_glow_lut_index(pSub[0x2f]));
            }
            gx_set_env_tex_matrix((float *)(lightSetB + *(char *)((int)pSub + 0x3d) * 0x40));
        }

        unsigned int fStrip180 = stageMask & 0x180;
        unsigned int fStrip0c = stageMask & 0xc;
        unsigned int texWide = texMtx & 2;
        int nrmMtxPlus4 = (int)nrmMtx2 + 4;

        while ((char)*(char *)((int)pStrip + 3) >= 0) {
            char stripType = *(char *)((int)pStrip + 3);
            int usedGrid = 0;
            if ((stripType == '\x03') || (stripType == '\x04')) {
                gx_set_blend_tag(4, (unsigned char)*(unsigned short *)(pInst + 0x1d));
            } else {
                gx_set_blend_tag(0, 0);
            }
            short mat = *pStrip;
            if ((mat < 0) || (*(char *)(matTable + mat * 0x30 + 0x18) != '\x01')) {
                if ((mat >= 0) && ((*(unsigned int *)(matTable + mat * 0x30 + 0x1c) & 0x40300000) != 0)) {
                    // --- build deform / heat-warp grid for this strip ---
                    usedGrid = 1;
                    gridBuilt = 1;
                    unsigned int srcDL = mem_advance_secondary_aligned(pDL[1]);
                    leaf_31e8(srcDL, pDL[0], pDL[1]);
                    if (*(unsigned int *)(entity + 0x80) == 0) {
                        entity_build_local_matrix(localMtx, (void *)entity, 0, 0);
                    } else {
                        float *m = (float *)subobj_get_object_matrix(*(unsigned int *)(entity + 0x80));
                        mtx4_copy(m, localMtx);
                    }
                    float eye[4];
                    if (*(int *)(entity + 0x20) == 0x10) {
                        eye[0] = -*(float *)(entity + 100);
                        eye[1] = -*(float *)(entity + 0x68);
                        eye[2] = -*(float *)(entity + 0x6c);
                    } else {
                        eye[0] = *(float *)(DAT_80611d54 + 0x27c) - *(float *)(entity + 100);
                        eye[1] = *(float *)(DAT_80611d54 + 0x280) - *(float *)(entity + 0x68);
                        eye[2] = *(float *)(DAT_80611d54 + 0x284) - *(float *)(entity + 0x6c);
                    }
                    mtx4_invert(invObj, localMtx);
                    mtx4_transform_point(invObj, eye);
                    if (*(int *)(entity + 0x20) == 0x10) {
                        mtx4_transform_point(*(float **)(DAT_80611d54 + 0x208), eye);
                    }
                    unsigned int outBuf = mem_advance_secondary_aligned(pDL[1] / 6 << 2);
                    gx_set_fog_params(0x46d);
                    haveProp = 1;   // mark prop-driven state as set (local_158)

                    int normSel = 0;
                    if (fStrip180 == 0x180) normSel = 2;
                    else if (fStrip180 == 0x100) normSel = 1;
                    int uvSel = 0;
                    if ((fStrip0c == 0xc) || (fStrip0c == 4)) uvSel = 2;
                    else if (fStrip0c == 8) uvSel = 1;
                    int outIdx = 0, cursor = 0;
                    unsigned int total = pDL[1];
                    unsigned int outBase = srcDL + uvSel + normSel;
                    while (cursor < (int)total) {
                        char *tag = (char *)(srcDL + cursor);
                        if (*tag == '\0') { cursor = cursor + 1; continue; }
                        cursor = cursor + 3;
                        for (unsigned int run = (unsigned int)*(unsigned short *)(tag + 1); run != 0; run--) {
                            unsigned short *vidx = (unsigned short *)(srcDL + cursor);
                            int outAddr = (int)outBase + cursor;
                            short *vp = (short *)(posMtx + (unsigned int)*vidx * 6);
                            // dequant vertex
                            float vx = FLOAT_80616718 * (float)vp[0] + (float)pModel[0xb];
                            float vy = FLOAT_80616718 * (float)vp[1] + (float)pModel[0xc];
                            float vz = FLOAT_80616718 * (float)vp[2] + (float)pModel[0xd];
                            float nx, ny, nz;
                            if (normSel == 0) {
                                short *np = (short *)(texMtx + (unsigned int)*(unsigned short *)(outAddr + 2) * 4);
                                float a = FLOAT_80616718 * (float)np[0];
                                float bq = FLOAT_80616718 * (float)np[1];
                                if (bq <= FLOAT_80616720) {
                                    nz = (float)math_sqrt((double)-(bq * bq - -(a * a - FLOAT_80616710)));
                                } else {
                                    bq = bq - FLOAT_80616724;
                                    nz = -(float)math_sqrt((double)-(bq * bq - -(a * a - FLOAT_80616710)));
                                }
                                nx = a; ny = bq;
                            } else {
                                unsigned int ni = (normSel == 2) ? (unsigned int)vidx[1]
                                                                 : (unsigned int)*(unsigned char *)(vidx + 1);
                                if (packedNorm == 0) {
                                    char *cn = (char *)(normMtx + ni * 3);
                                    nx = FLOAT_8061671c * (float)cn[0];
                                    ny = FLOAT_8061671c * (float)cn[1];
                                    nz = FLOAT_8061671c * (float)cn[2];
                                } else {
                                    int no = ni * 0xc;
                                    nx = *(float *)((int)&DAT_80412740 + no);
                                    ny = *(float *)((int)&DAT_80412744 + no);
                                    nz = *(float *)((int)&DAT_80412748 + no);
                                }
                            }
                            // extrude toward eye along normal
                            float dot = FLOAT_80616728 * ((vz - eye[2]) * nz + (vx - eye[0]) * nx + (vy - eye[1]) * ny);
                            float ex = nx * dot + (vx - eye[0]);
                            float ey = ny * dot + (vy - eye[1]);
                            float ez = nz * dot + (vz - eye[2]);
                            float lenSq = ex * ex + ey * ey + ez * ez;
                            if (FLOAT_8061672c < lenSq) {
                                float inv = (float)(1.0 / SQRT((double)lenSq));
                                float r0 = inv * -((float)((double)FLOAT_8061670c * (double)lenSq) * (inv * inv) - FLOAT_80616730);
                                r0 = FLOAT_80616710 * r0 * -((float)((double)FLOAT_8061670c * (double)lenSq) * r0 * r0 - FLOAT_80616730);
                                ex = ex * r0; ey = ey * r0; ez = ez * r0;
                            }
                            tmpVec[0] = ex; tmpVec[1] = ey; tmpVec[2] = ez;
                            mtx4_transform_vector(localMtx, tmpVec);
                            cursor = cursor + normSel + uvSel + 4;
                            *(short *)(outBuf + outIdx * 4) = (short)(int)(FLOAT_80616734 * tmpVec[0]);
                            ((short *)(outBuf + outIdx * 4))[1] = (short)(int)(FLOAT_80616734 * tmpVec[1]);
                            *(char *)(outAddr + 2) = (char)((unsigned int)outIdx >> 8);
                            *(char *)(outAddr + 3) = (char)outIdx;
                            outIdx = outIdx + 1;
                        }
                    }
                    FUN_8039955c(srcDL, total);
                    FUN_8039955c(outBuf, outIdx << 2);
                    gx_indirect_matrix_update(0xd, outBuf, 4);
                }
            }

            if (*(char *)((int)pStrip + 3) == '\t') {
                gx_indirect_matrix_update(0xc, (unsigned int)nrmMtxPlus4, 4);
                gx_indirect_stage_config();
                didWarp = 1;
                int t = *pStrip * 0x30 + 0x1c;
                *(unsigned int *)(matTable + t) = *(unsigned int *)(matTable + t) | 0x800000;
                gx_set_indirect_blend_mode_b(1);
            } else if (didWarp != 0) {
                gx_indirect_stage_config();
                didWarp = 0;
                gx_set_indirect_blend_mode_b(0);
            }

            if ((texMtx == 0) || (*pStrip < 0)) {
                gx_bind_material_fallback();
            } else {
                int *pMat = (int *)(matTable + *pStrip * 0x30);
                FUN_801863ec(entity, lightSet, matTable, pStrip);
                if (((pMat[7] & 0x40300000U) == 0) || (*(int *)(entity + 0x20) != 0x10)) {
                    render_instance_apply_material((int)*pStrip, matTable, lightSet);
                } else {
                    int tex = (int)sprite_get_texture_id(0x326);
                    if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
                        gx_bind_material_fallback();
                    } else {
                        DAT_80612254 = *DAT_80610a20 + tex * 0x58;
                    }
                }
                if (DAT_80611b48 != 0) {
                    int tex = (int)sprite_get_texture_id(0x3f7);
                    if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
                        gx_bind_material_fallback();
                    } else {
                        DAT_80612254 = *DAT_80610a20 + tex * 0x58;
                    }
                }
                if ((*pMat >= 0) &&
                    ((DAT_80611b48 != 0) || (*(char *)((int)pStrip + 3) == '\a') ||
                     ((pMat[7] & 4U) != 0) || ((pMat[7] & 0x8000U) != 0))) {
                    float *ch = RENDER_PROP_EVAL(pMat, lightSet, 0);
                    if ((ch != (float *)0) && (ch != lastPropMtx)) {
                        lastPropMtx = ch;
                        gx_load_env_tex_matrix(ch);
                    }
                    if (ch != (float *)0) haveProp = 1;
                }
            }
            if (haveProp == 0) {
                gx_set_fog_params(savedFog);
            }
            unsigned int n = pDL[1];
            if ((n == 0) || (usedGrid != 0)) {
                if ((n != 0) && (pDL[1] != 0)) {   // (unaff_r16 in original; use n)
                    gx_load_pos_matrix(0 /*srcDL rebuilt above*/, n);
                }
            } else {
                gx_load_pos_matrix(pDL[0], n);
            }
            pStrip = pStrip + 4;
            pDL = pDL + 2;
        }

        gx_set_state_c0(DAT_80610448);
        gx_set_sub_material_id(DAT_80610444);
        gx_set_blend_mode(DAT_80610440);
        gx_set_fog_params(savedFog);
    }

    DAT_80611b40 = savedEnvA;
    DAT_80611b44 = savedEnvB;
    if (((*(unsigned int *)(DAT_80611d54 + 0x21c) & 0x10) != 0) && (passIdx == 0) &&
        ('\0' < *(char *)((int)pSub + 0x42))) {
        entity_build_local_matrix(shadowMv, (void *)entity, 1, subIdx);
        if (*(char *)(entity + 0x31) == '\0') {
            mtx4_multiply(shadowMv, *(float **)(DAT_80611d54 + 0x20c), shadowProj);
        } else {
            mtx4_multiply(shadowMv, *(float **)(DAT_80611d54 + 0x204), shadowProj);
        }
        if (pInst[4] != 0) {
            mtx4_multiply((float *)(*(int *)(*(int *)(entity + 4) + 0x10) + idx * 0x40), shadowMv, shadowMv);
            mtx4_multiply((float *)(*(int *)(*(int *)(entity + 4) + 0x10) + idx * 0x40), shadowProj, shadowProj);
        }
        render_instance_project_shadows(entity, idx, (int)pSub, shadowProj, shadowMv, subIdx);
    }
    gx_set_indirect_blend_mode_b(0);
    FUN_80382700();
    (void)warpGridA; (void)warpGridB;
}
