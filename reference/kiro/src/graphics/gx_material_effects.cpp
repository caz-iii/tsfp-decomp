// gx_material_effects.cpp
// Per-material "effect" dispatch layer plus a handful of TEV-stage builder
// callbacks that back it. A material effect is selected by index
// (gx_material_effect_set_id -> DAT_806124b0/DAT_806124a0); begin/end install and
// restore the pre/override/post material callbacks from the per-effect table at
// DAT_8041e7c8 (stride 0x10: pre, override, post, teardown). The TEV builders here
// are installed as those callbacks and program stage(s) for a specific effect
// (grey-scale modulate, additive, indirect distortion, full-screen refraction).
//
// Address range: 0x8018f32c - 0x8018fbac
// Batch: batch_497
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// GX TEV cache helpers (see gx_tev_cache.cpp) - permissive arities matching call sites
extern void gx_cached_set_tev_order(unsigned int stage, int texMap, int texCoord, int channel);
extern void gx_cached_set_tev_color_op(unsigned int stage, int a2, int a3, int a4);
extern void gx_cached_set_tev_color_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_color_env(unsigned int stage, int a, int b, int c, int d, int e);
extern void gx_cached_set_tev_alpha_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_alpha_env(unsigned int stage, int a, int b, int c, int d, int e);

// Material callback install/restore (return previous callback pointer)
extern int  gx_set_material_pre_cb(int cb);
extern int  gx_set_material_override_cb(int cb);
extern int  gx_set_material_post_cb(int cb);

extern int  sprite_get_texture_id(unsigned int spriteId);

// Matrix helpers
extern void mtx4_set_translation(double x, double y, double z);
extern void mtx4_transpose(float *out, float *in);

// Indirect / tex-gen / konst helpers (raw addresses, arity per call site)
extern void FUN_803c9288(unsigned int *tex, int slot);       // bind tex object to slot
extern void FUN_803ca238(int slot, unsigned int *rgba);      // set TEV konst colour (ksel A)
extern void FUN_803ca174(int slot, unsigned int *rgba);      // set TEV konst colour (ksel B)
extern void FUN_803ca29c(unsigned int stage, int kSel);      // set konst selector for stage
extern void FUN_803c9ecc(int numIndStages);                  // GXSetNumIndStages
extern void FUN_803c9ef0(int stage);                         // disable indirect stage
extern void FUN_803c9de0(int indStage, int texStage, int texMap);   // GXSetIndTexOrder
extern void FUN_803c9c9c(int a1, int a2, int a3);            // indirect coord scale config
extern void FUN_803c9b24(int coord, float *params, char a3); // program tex-coord gen
extern void FUN_803c9ab8(unsigned int stage, int a2, int a3, int a4, int a5, int a6,
                         int a7, int a8, int a9, int a10);   // GXSetTevIndirect (full)
extern double FUN_80390ae4(double a, double b);              // powf/scaled-pow helper

// Spill-frame accessors: return a packed (stage-counter-ptr : next-free-stage)
// pair in a 64-bit value. hi32 = next free TEV stage index, ptr = counter cell.
extern unsigned long long FUN_803826e4(void);
extern unsigned long long FUN_803826e8(void);
extern unsigned long long FUN_803826f0(void);
extern void FUN_80382730(void);   // spill-frame stage-pair release (matches e4)
extern void FUN_80382734(void);   // spill-frame stage-pair release (matches e8)
extern void FUN_8038273c(void);   // spill-frame stage-pair release (matches f0)

// ---- Data ----
extern int   DAT_806124a0;   // active material-effect index (0 = none)
extern int   DAT_806124b0;   // secondary effect index (used by grey/additive builders)
extern int   DAT_806124b4;   // saved post callback
extern int   DAT_806124b8;   // saved override callback
extern int   DAT_806124bc;   // saved pre callback
extern int  *DAT_80610a20;   // render-target group table ([0] = tex-object slot base)
extern unsigned int *DAT_80612254;  // active material pointer
extern int   DAT_806109c0;   // distortion source coord A (packed float)
extern int   DAT_80612020;   // distortion source coord B (packed float)

// Per-effect material-callback table: stride 0x10 = { pre, override, post, teardown }.
extern int DAT_8041e7c8;   // table base: pre callback of effect 0
extern int DAT_8041e7cc;   // override callback of effect 0
extern int DAT_8041e7d0;   // post callback of effect 0
extern int DAT_8041e7d4;   // teardown callback of effect 0

extern float  FLOAT_80616870;
extern float  FLOAT_806168a0;
extern float  FLOAT_80616878;
extern float  FLOAT_80612070;
extern float  FLOAT_806168d0;
extern float  FLOAT_806168d4;
extern float  FLOAT_806168d8;
extern float  FLOAT_806168dc;
extern double DOUBLE_80616888;
extern double DOUBLE_806168c8;
extern double DOUBLE_80616898;
} // extern "C"

// The int<->double "magic bias" constant 0x43300000 in the high word converts a
// 32-bit unsigned to double (classic PPC int->float idiom). We reproduce the same
// bit pattern via a union so the arithmetic stays faithful without matching.
static inline double u32_to_double_bias(unsigned int lo) {
    union { unsigned int u[2]; double d; } bias, val;
    bias.u[0] = 0x43300000u; bias.u[1] = 0x00000000u;   // 2^52
    val.u[0]  = 0x43300000u; val.u[1]  = lo;
    return val.d - bias.d;
}

// ---------------------------------------------------------------------------
// gx_material_effect_set_id (FUN_8018f32c)
// Store the secondary material-effect index consulted by the grey/additive
// TEV builders below.
// ---------------------------------------------------------------------------
void gx_material_effect_set_id(int effectId) {
    DAT_806124b0 = effectId;
}

// ---------------------------------------------------------------------------
// gx_tev_stage_effect_modulate2 (FUN_8018f334)
// Two-stage builder: fetch the per-effect record (DAT_806124b0 * 0x58 into the
// tex-object slot table) and program a modulate + add stage pair. Uses the
// spill-frame accessor for the base colour stage counter.
// param_3 = coord/tex-stage counter (post-incremented twice).
// ---------------------------------------------------------------------------
void gx_tev_stage_effect_modulate2(int param_1, int param_2, unsigned int *stageCounter) {
    unsigned long long frame = FUN_803826e4();
    unsigned int stage0 = (unsigned int)(frame >> 0x20);
    unsigned int stage1 = stage0 + 1;
    int *counterCell = (int *)(unsigned int)frame;
    *counterCell = *counterCell + 1;

    unsigned int coordA = *stageCounter; *stageCounter = coordA + 1;
    unsigned int coordB = *stageCounter; *stageCounter = coordB + 1;

    int effOffset = DAT_806124b0 * 0x58;
    int slotBase  = *DAT_80610a20;
    FUN_803c9288(DAT_80612254, coordA);
    FUN_803c9288((unsigned int *)(slotBase + effOffset), coordB);

    gx_cached_set_tev_color_op(stage0, 0, coordA, 4);
    gx_cached_set_tev_color_in(stage0, 0xf, 10, 8, 0xf);
    gx_cached_set_tev_color_env(stage0, 0, 0, 2, 1, 0);
    gx_cached_set_tev_alpha_in(stage0, 7, 7, 7, 5);
    gx_cached_set_tev_alpha_env(stage0, 0, 0, 0, 1, 0);

    gx_cached_set_tev_color_op(stage1, 0, coordB, 0xff);
    gx_cached_set_tev_color_in(stage1, 0xf, 0, 8, 0xf);
    gx_cached_set_tev_color_env(stage1, 0, 0, 1, 1, 0);
    gx_cached_set_tev_alpha_in(stage1, 7, 7, 7, 0);
    gx_cached_set_tev_alpha_env(stage1, 0, 0, 1, 1, 0);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// gx_tev_stage_effect_alpha (FUN_8018f4c4)
// Single-stage builder: modulate the active material with saturated alpha.
// Uses the FUN_803826f0 spill-frame slot.
// ---------------------------------------------------------------------------
void gx_tev_stage_effect_alpha(int param_1, int param_2, unsigned int *stageCounter) {
    unsigned long long frame = FUN_803826f0();
    unsigned int stage = (unsigned int)(frame >> 0x20);
    int *counterCell = (int *)(unsigned int)frame;
    *counterCell = *counterCell + 1;

    unsigned int coord = *stageCounter; *stageCounter = coord + 1;
    FUN_803c9288(DAT_80612254, coord);

    gx_cached_set_tev_color_op(stage, 0, coord, 4);
    gx_cached_set_tev_color_in(stage, 0xf, 10, 8, 0xf);
    gx_cached_set_tev_color_env(stage, 0, 0, 1, 1, 0);
    gx_cached_set_tev_alpha_in(stage, 5, 7, 4, 7);
    gx_cached_set_tev_alpha_env(stage, 0, 0, 1, 1, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// gx_tev_effect_indirect_teardown (FUN_8018f5a0)
// Disable indirect stages used by the distortion effects.
// ---------------------------------------------------------------------------
void gx_tev_effect_indirect_teardown(void) {
    FUN_803c9ecc(0);
    FUN_803c9ef0(0);
    FUN_803c9ef0(1);
}

// ---------------------------------------------------------------------------
// gx_tev_stage_effect_indirect2 (FUN_8018f5d4)
// Two-stage indirect builder with three konst colours (0x2a grey, 0x54, 0x15).
// Uses the FUN_803826e8 spill-frame slot.
// ---------------------------------------------------------------------------
void gx_tev_stage_effect_indirect2(int param_1, int param_2, unsigned int *stageCounter) {
    unsigned long long frame = FUN_803826e8();
    unsigned int stage0 = (unsigned int)(frame >> 0x20);
    int *counterCell = (int *)(unsigned int)frame;
    unsigned int baseStage = *counterCell;
    unsigned int stage1 = stage0 + 1;
    *counterCell = baseStage + 1;

    unsigned int coord = *stageCounter; *stageCounter = coord + 1;
    gx_cached_set_tev_order(baseStage, 1, 4, 0x3c);
    FUN_803c9288(DAT_80612254, coord);

    unsigned int konstGrey = 0x2a2a2a00u | 0x2au;   // 0x2a2a2a2a grey
    FUN_803ca238(3, &konstGrey);
    unsigned int konstB = 0x54545400u | 0x54u;       // 0x54545454
    FUN_803ca174(1, &konstB);
    unsigned int konstC = 0x15151500u | 0x15u;       // 0x15151515
    FUN_803ca174(2, &konstC);

    FUN_803ca29c(stage0, 0xf);
    FUN_803ca29c(stage1, 0xf);

    gx_cached_set_tev_color_op(stage0, baseStage, coord, 4);
    gx_cached_set_tev_color_in(stage0, 8, 0xe, 0xd, 4);
    gx_cached_set_tev_alpha_in(stage0, 7, 7, 7, 5);
    gx_cached_set_tev_color_env(stage0, 0xe, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage0, 0, 0, 1, 1, 0);

    gx_cached_set_tev_color_op(stage1, baseStage, coord, 4);
    gx_cached_set_tev_color_in(stage1, 8, 2, 0xe, 0);
    gx_cached_set_tev_alpha_in(stage1, 7, 7, 7, 5);
    gx_cached_set_tev_color_env(stage1, 0xe, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage1, 0, 0, 1, 1, 0);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// gx_tev_stage_effect_screen_distort (FUN_8018f7d4)
// Full-screen refraction/distortion: builds a scrolling tex-gen matrix (slot
// 0x21) from a depth ratio, binds the noise texture (sprite 0x333), programs an
// indirect distortion stage and one colour stage. Distortion amount derives from
// the two packed source coords negated + FUN_80390ae4 pow curve. Uses spill frame
// FUN_803826e4. FP register-spill scalars are captured as leading params.
// ---------------------------------------------------------------------------
void gx_tev_stage_effect_screen_distort(float inScaleX, float inScaleY,
                                        unsigned int *stageCounter) {
    (void)inScaleX; (void)inScaleY;

    float texMtx[16];
    float texMtxT[16];
    float coordParams[5];

    unsigned long long frame = FUN_803826e4();
    int *counterCell = (int *)(unsigned int)frame;
    unsigned int indStage = (unsigned int)(frame >> 0x20);

    unsigned int stage0 = *counterCell; *counterCell = stage0 + 1;
    unsigned int stage1 = *counterCell; *counterCell = stage1 + 1;
    unsigned int coordA = *stageCounter; *stageCounter = coordA + 1;
    unsigned int coordB = *stageCounter; *stageCounter = coordB + 1;

    // Depth ratio -> distortion magnitude (negate packed source coords, classic
    // int->float bias dequant, then FUN_80390ae4 pow curve).
    unsigned int srcA = (unsigned int)DAT_80612020 ^ 0x80000000u;
    unsigned int srcB = (unsigned int)DAT_806109c0 ^ 0x80000000u;
    double dA = u32_to_double_bias(srcA) - DOUBLE_80616888;
    double dB = u32_to_double_bias(srcB) - DOUBLE_80616888;
    float  ratio = FLOAT_80616870 / (float)dB;
    double dist  = FUN_80390ae4((double)(float)(dA * ratio), DOUBLE_806168c8);
    dist = (double)(float)dist;

    gx_cached_set_tev_order(stage0, 1, 4, 0x3c);
    mtx4_set_translation((double)(float)((double)FLOAT_806168a0 * dist),
                         (double)(float)((double)FLOAT_806168a0 * dist),
                         (double)FLOAT_80616878);
    mtx4_transpose(texMtxT, texMtx);
    // FUN_803cb2cc: load tex-mtx immediate into slot 0x21
    extern void FUN_803cb2cc(float *mtx, int slot, int a3);
    FUN_803cb2cc(texMtx, 0x21, 0);
    gx_cached_set_tev_order(stage1, 1, 4, 0x21);
    FUN_803c9288(DAT_80612254, coordA);

    int noiseTex = sprite_get_texture_id(0x333);
    FUN_803c9288((unsigned int *)(*DAT_80610a20 + noiseTex * 0x58), coordB);

    FUN_803c9ecc(1);
    FUN_803c9de0(0, stage1, coordB);
    FUN_803c9c9c(0, 0, 0);

    // Screen-space scroll params for the distortion tex-coord gen. Material fields
    // +0x4e / +0x52 are the RT half-dimensions (u16) dequantised via the same bias.
    unsigned int dimW = *(unsigned short *)((int)DAT_80612254 + 0x4e);
    unsigned int dimH = *(unsigned short *)((int)DAT_80612254 + 0x52);
    coordParams[0] = FLOAT_80616878;
    coordParams[2] = FLOAT_80616878;
    coordParams[3] = FLOAT_80616878;
    coordParams[1] = (FLOAT_806168d0 * FLOAT_80612070 *
                      (float)(u32_to_double_bias(dimW) - DOUBLE_80616898)) / FLOAT_806168d4;
    coordParams[4] = (FLOAT_806168d8 * FLOAT_80612070 *
                      (float)(u32_to_double_bias(dimH) - DOUBLE_80616898)) / FLOAT_806168dc;
    FUN_803c9b24(1, coordParams, '\x01');
    FUN_803c9ab8(indStage, 0, 0, 3, 1, 0, 0, 0, 0, 0);

    gx_cached_set_tev_color_op(indStage, stage0, coordA, 4);
    gx_cached_set_tev_color_in(indStage, 0xf, 10, 8, 0xf);
    gx_cached_set_tev_alpha_in(indStage, 7, 5, 4, 7);
    gx_cached_set_tev_color_env(indStage, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(indStage, 0, 0, 0, 1, 0);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// gx_material_effect_begin (FUN_8018fac4)
// Select material effect `effectId` and install its pre/override/post callbacks
// from the per-effect table (stride 0x10). Saves the previous callbacks for
// gx_material_effect_end to restore. Caller: render_instance_draw.
// ---------------------------------------------------------------------------
void gx_material_effect_begin(short effectId) {
    DAT_806124a0 = (int)effectId;
    int off = DAT_806124a0 * 0x10;
    DAT_806124bc = gx_set_material_pre_cb(*(int *)(&DAT_8041e7c8 + off));
    DAT_806124b8 = gx_set_material_override_cb(*(int *)(&DAT_8041e7cc + off));
    DAT_806124b4 = gx_set_material_post_cb(*(int *)(&DAT_8041e7d0 + off));
}

// ---------------------------------------------------------------------------
// gx_material_effect_end (FUN_8018fb38)
// Restore the saved material callbacks, invoke the effect's teardown callback
// (table +0x0c) if present, and clear the active effect index.
// ---------------------------------------------------------------------------
void gx_material_effect_end(void) {
    DAT_806124bc = gx_set_material_pre_cb(DAT_806124bc);
    DAT_806124b8 = gx_set_material_override_cb(DAT_806124b8);
    DAT_806124b4 = gx_set_material_post_cb(DAT_806124b4);

    int off = DAT_806124a0 * 0x10;
    void (**teardown)(void) = (void (**)(void))(&DAT_8041e7d4 + off);
    if (*teardown != (void (*)(void))0) {
        (*teardown)();
    }
    DAT_806124a0 = 0;
}

// ---------------------------------------------------------------------------
// gx_material_effect_stub_a / _b (FUN_8018fba8 / FUN_8018fbac)
// Empty material-effect callback stubs (used as null pre/post entries).
// ---------------------------------------------------------------------------
void gx_material_effect_stub_a(void) {}
void gx_material_effect_stub_b(void) {}

// ---------------------------------------------------------------------------
// gx_tev_stage_effect_modulate2 (FUN_8018f334) | batch_504
// Program a 2-stage TEV chain for a material effect: stage N modulates the base
// texture by the effect's konst/texture (color op 4 = GX_TEV_ADD scale 1,
// combine c*d), stage N+1 adds the secondary effect texture (record at
// DAT_806124b0*0x58 in the tex-object slot table DAT_80610a20[0]).  Advances the
// caller's stage counter (*param_3) by 2.  The stage-pair index arrives in the
// FUN_803826e4 spill frame (low = first stage, high = base = first+1... here the
// decompiler surfaced the pair as uVar2 / uVar2+1).
// ---------------------------------------------------------------------------
void gx_tev_stage_effect_modulate2(unsigned int param_1, unsigned int param_2, unsigned int *param_3)
{
    unsigned long long frame = FUN_803826e4();
    unsigned int stage0 = (unsigned int)(frame >> 0x20);
    unsigned int stage1 = stage0 + 1;
    *(int *)(unsigned int)frame = *(int *)(unsigned int)frame + 1;

    unsigned int tc0 = *param_3; *param_3 = tc0 + 1;
    unsigned int tc1 = *param_3; *param_3 = tc1 + 1;

    int recOff = DAT_806124b0 * 0x58;
    int slotBase = *DAT_80610a20;

    FUN_803c9288(DAT_80612254, tc0);
    FUN_803c9288((unsigned int *)(slotBase + recOff), tc1);

    gx_cached_set_tev_color_op(stage0, 0, tc0, 4);
    gx_cached_set_tev_color_in(stage0, 0xf, 10, 8, 0xf);
    gx_cached_set_tev_color_env(stage0, 0, 0, 2, 1, 0);
    gx_cached_set_tev_alpha_in(stage0, 7, 7, 7, 5);
    gx_cached_set_tev_alpha_env(stage0, 0, 0, 0, 1, 0);

    gx_cached_set_tev_color_op(stage1, 0, tc1, 0xff);
    gx_cached_set_tev_color_in(stage1, 0xf, 0, 8, 0xf);
    gx_cached_set_tev_color_env(stage1, 0, 0, 1, 1, 0);
    gx_cached_set_tev_alpha_in(stage1, 7, 7, 7, 0);
    gx_cached_set_tev_alpha_env(stage1, 0, 0, 1, 1, 0);

    FUN_80382730();
}

// ---------------------------------------------------------------------------
// gx_tev_stage_effect_alpha (FUN_8018f4c4) | batch_504
// Program a single TEV stage that modulates the active material by the bound
// effect texture with a saturated alpha combine (color op 4).  Advances the
// caller's stage counter (*param_3) by 1.  Stage index from FUN_803826f0 spill.
// ---------------------------------------------------------------------------
void gx_tev_stage_effect_alpha(unsigned int param_1, unsigned int param_2, unsigned int *param_3)
{
    unsigned long long frame = FUN_803826f0();
    unsigned int stage = (unsigned int)(frame >> 0x20);
    *(int *)(unsigned int)frame = *(int *)(unsigned int)frame + 1;

    unsigned int tc = *param_3; *param_3 = tc + 1;
    FUN_803c9288(DAT_80612254, tc);

    gx_cached_set_tev_color_op(stage, 0, tc, 4);
    gx_cached_set_tev_color_in(stage, 0xf, 10, 8, 0xf);
    gx_cached_set_tev_color_env(stage, 0, 0, 1, 1, 0);
    gx_cached_set_tev_alpha_in(stage, 5, 7, 4, 7);
    gx_cached_set_tev_alpha_env(stage, 0, 0, 1, 1, 0);

    FUN_8038273c();
}

// NOTE: gx_tev_effect_indirect_teardown (0x8018f5a0) is already defined earlier in
// this file (a prior batch); batch_504 does not redefine it here.

// ---------------------------------------------------------------------------
// gx_tev_stage_effect_indirect2 (FUN_8018f5d4) | batch_504
// Program 2 indirect TEV stages driving a distortion/warp effect: sets a tex
// order, binds the active material texture, loads three konst colours (grey
// 0x2a2a2a via ksel-A slot 3, plus 0x545454 and 0x151515 via ksel-B slots 1/2),
// selects konst for both stages, and programs the colour/alpha combiners that
// blend the indirect-sampled texel (input 0xe) over the base.  Advances the
// caller's stage counter (*param_3) by 1.  Stage pair from FUN_803826e8 spill.
// ---------------------------------------------------------------------------
void gx_tev_stage_effect_indirect2(unsigned int param_1, unsigned int param_2, unsigned int *param_3)
{
    unsigned long long frame = FUN_803826e8();
    unsigned int stage0 = (unsigned int)(frame >> 0x20);
    unsigned int stageBase = *(unsigned int *)(unsigned int)frame;
    unsigned int stage1 = stage0 + 1;
    *(unsigned int *)(unsigned int)frame = stageBase + 1;

    unsigned int tc = *param_3; *param_3 = tc + 1;

    gx_cached_set_tev_order(stageBase, 1, 4, 0x3c);
    FUN_803c9288(DAT_80612254, tc);

    unsigned int kGrey = 0x2a2a2a00u | 0x2a;   // 0x2a in all four bytes
    kGrey = 0x2a2a2a2au;
    FUN_803ca238(3, &kGrey);
    unsigned int kMid = 0x54545454u;
    FUN_803ca174(1, &kMid);
    unsigned int kLow = 0x15151515u;
    FUN_803ca174(2, &kLow);

    FUN_803ca29c(stage0, 0xf);
    FUN_803ca29c(stage1, 0xf);

    gx_cached_set_tev_color_op(stage0, stageBase, tc, 4);
    gx_cached_set_tev_color_in(stage0, 8, 0xe, 0xd, 4);
    gx_cached_set_tev_alpha_in(stage0, 7, 7, 7, 5);
    gx_cached_set_tev_color_env(stage0, 0xe, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage0, 0, 0, 1, 1, 0);

    gx_cached_set_tev_color_op(stage1, stageBase, tc, 4);
    gx_cached_set_tev_color_in(stage1, 8, 2, 0xe, 0);
    gx_cached_set_tev_alpha_in(stage1, 7, 7, 7, 5);
    gx_cached_set_tev_color_env(stage1, 0xe, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage1, 0, 0, 1, 1, 0);

    FUN_80382734();
}
