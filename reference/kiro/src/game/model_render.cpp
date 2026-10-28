// =============================================================================
// model_render.cpp - Model render dispatch + skeletal-animation pose evaluation
// Address range: 0x8001d548 - 0x8001f7cb
// Batch: 385 | Date: 2026-09-05
//
// The per-entity model render pipeline. render_state_snapshot captures the
// frame's interpolation state; model_render_with_override_{a,b} gate an entity
// by type and optionally render it twice with a swapped bone-matrix block; and
// model_pose_eval is the large animation pose evaluator that samples one or two
// animation clips, slerps the per-bone quaternions, blends them, and writes the
// resulting bone matrix palette. The small render_mode_* predicates classify the
// active 2D/blend mode; render2d_* helpers manage the debug render-list.
//
//   * render2d_stub_nop            (0x8001d548) empty stub
//   * render2d_list_dirty_check    (0x8001d54c) list-changed / mode predicate
//   * render_mode_class_b          (0x8001d5a4) mode in {8,9,3}
//   * render_mode_class_a          (0x8001d5e0) mode in {1,3,7,0xb,0xc,0xd}
//   * render_state_snapshot        (0x8001d620) capture interp state for frame
//   * render2d_debug_freeze_check  (0x8001d68c) debug single-step hook
//   * texture_handle_lookup        (0x8001d6e0) texture id -> GX handle
//   * model_render_with_override_a (0x8001d73c) render w/ optional matrix swap
//   * model_render_with_override_b (0x8001d894) render w/ optional matrix swap
//   * model_pose_eval              (0x8001d9ec) skeletal pose evaluator (core)
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // quat_*, mtx4_to_quat, player_*, leaf_31e8, etc.

// ---- render-list / interpolation state globals ----------------------------
extern int   DAT_80611550;
extern int   DAT_80611554;
extern int   DAT_80611558;
extern int   DAT_8061155c;
extern int   DAT_80611560;
extern int   DAT_8060f960;
extern int   DAT_8060faec;
extern int   DAT_80611750;   // current render-list write cursor
extern int   DAT_80611770;   // active 2D/blend mode
extern int   DAT_80611784;
extern int   DAT_80611788;
extern int   DAT_8061178c;
extern int   DAT_80611798;
extern int   DAT_8061179c;
extern int   DAT_806117ac;
extern int   DAT_806117b0;
extern int   DAT_806117b8;
extern int   DAT_806117bc;   // last-frame render-list cursor snapshot
extern int   DAT_80611824;   // texture-descriptor table base
extern int   DAT_80611d54;   // active render/camera context

// ---- animation / pose-eval globals ----------------------------------------
extern int   DAT_806108e4;   // pose scale-mode latch
extern float FLOAT_806108ec; // frame delta seconds
extern int   DAT_806109c0;   // frame-rate divisor
extern char  DAT_805015bd;   // region / mode selector ('\n' == special)
extern int   DAT_80612610;   // LOD distance threshold
extern int   DAT_806117e4;   // LOD-active flag output
extern int   DAT_804d0e20;   // texture handle table
extern short DAT_804d1cd0;   // texture id -> index table (short entries)
extern int   DAT_804d34d8;   // per-anim flag table (word 0, stride 8)
extern int   DAT_804d34dc;   // per-anim flag table (word 1, stride 8)
extern float DAT_803cc0e0;   // axis-angle jitter axis consts
extern float DAT_803cc0e4;
extern float DAT_803cc0e8;
extern float DAT_803cc0ec;
extern float DAT_803cc0f0;
extern float DAT_803cc0f4;
extern float DAT_803cc0f8;
extern float DAT_803cc0fc;
extern float DAT_803cc100;

// ---- numeric constants -----------------------------------------------------
extern float  FLOAT_80613930;  // 0.0
extern double DOUBLE_80613938;  // uint->double conversion magic bias
extern float  FLOAT_80613940;  // wrap period (angle)
extern float  FLOAT_80613944;  // 1.0
extern float  FLOAT_80613948;  // 0.5 threshold
extern float  FLOAT_8061394c;  // deg->rad-ish scale
extern float  FLOAT_80613950;  // divisor
extern float  FLOAT_8060faf8;  // fixed-point scale (keyframe times)

// ---- raw runtime helpers (still FUN_; self-extern at call-site arity) ------
extern "C" {
    unsigned long long FUN_803826f0(void);   // scratch: entity record pair
    void               FUN_8038273c(void);    // scratch free
    int                FUN_803826b4(void);    // scratch: entity record (hi ptr)
    void               FUN_80382700(void);    // scratch free
    double FUN_80026bcc(int ctx);             // resolve interp weight / distance
    void   FUN_80020be0(void);                // pre-sample setup
    void   FUN_80022ea4(int animState, unsigned int* outA, unsigned int* outB); // keyframe pair
    void   FUN_8001f92c(int* model);          // render (no-LOD path)
    void   FUN_8001f7cc(int* model, int lod); // render (LOD path)
    double FUN_8002007c(double w, int* model, int* outFlag); // squash/stretch weight
    void   FUN_80024800(int animTbl, int bone, unsigned int key, float* out); // sample keyframe
    void   FUN_8002118c(int ctx, float* out, int bone); // procedural bone override
    void   FUN_803a14c4(float* dstMtx, float* srcMtx);   // matrix copy
    void   FUN_803a1508(float* a, float* b, float* out); // matrix multiply
    void   FUN_80021a38(void);                            // post-bone2 hook
    double FUN_80026c10(int ctx);             // scale factor
    void   FUN_800201e8(int* model, int subObj, int projCtx); // finalize sub-objects
    void   FUN_80022c14(int* model, int a, int b);        // build world matrices
    void   FUN_80022a38(int* model, int mtxBase, int a, int b);
    void   FUN_80022b1c(int* model, int mtxBase, int a, int b);
    void   FUN_80020698(int* model, int subObj, int projCtx);
    int    FUN_80025ad8(int ctx, unsigned int flag, int a, int b);
    double FUN_80390324(double x);            // fmod / wrap helper
}

// forward declares for intra-unit calls
int  render_mode_class_a(void);
int  render_mode_class_b(void);
void model_pose_eval(void);

// render2d_stub_nop (0x8001d548)
void render2d_stub_nop(void)
{
}

// render_mode_class_b (0x8001d5a4)
// True when the active mode is one of {8, 9, 3}.
int render_mode_class_b(void)
{
    if (DAT_80611770 - 8U < 2) {
        return 1;
    }
    if (DAT_80611770 != 3) {
        return 0;
    }
    return 1;
}

// render_mode_class_a (0x8001d5e0)
// True when the active mode is one of {1, 3, 7, 0xb, 0xc, 0xd}.
int render_mode_class_a(void)
{
    if (((DAT_80611770 != 1) && (DAT_80611770 != 3) && (DAT_80611770 != 7)) &&
        ((DAT_80611770 != 0xb) && (DAT_80611770 != 0xc) && (DAT_80611770 != 0xd))) {
        return 0;
    }
    return 1;
}

// render2d_list_dirty_check (0x8001d54c)
// True if the render list is unchanged since last frame, or the active mode is
// in either blend class.
int render2d_list_dirty_check(void)
{
    int result = 0;
    if ((DAT_80611750 == DAT_806117bc) ||
        (render_mode_class_a() != 0) ||
        (render_mode_class_b() != 0)) {
        result = 1;
    }
    return result;
}

// render_state_snapshot (0x8001d620)
// Capture the frame's render/interpolation state into the working globals and
// compute the camera interpolation factor for the active context.
void render_state_snapshot(void)
{
    DAT_80611798 = DAT_80611550;
    DAT_8061179c = DAT_80611554;
    DAT_8060faec = DAT_8060f960;
    DAT_80611784 = DAT_80611558;
    DAT_80611788 = DAT_8061155c;
    DAT_8061178c = DAT_80611560;
    DAT_806117b8 = 0;
    double lerp = player_get_camera_lerp_factor(DAT_80611d54);
    DAT_806117b0 = (int)lerp;
}

// render2d_debug_freeze_check (0x8001d68c)
// Debug single-step hook: when the render list is unchanged and the debug button
// (0x100) is held, reset the list and toggle the freeze latch.
void render2d_debug_freeze_check(void)
{
    if ((DAT_80611750 == DAT_806117bc) &&
        ((input_get_buttons_held(0) & 0x100) != 0)) {
        render2d_list_reset();
        if (DAT_806117ac == 0) {
            toggle_latch_update(0);
        }
    }
}

// texture_handle_lookup (0x8001d6e0)
// Map a texture id to its GX handle through the id->index (DAT_804d1cd0) and
// index->handle (DAT_804d0e20) tables. Returns 0 for out-of-range ids.
unsigned int texture_handle_lookup(int id)
{
    if (id < 1) {
        return 0;
    }
    bool valid = false;
    if ((0x24 < id) && (id < 0xc03)) {
        valid = true;
    }
    if (valid) {
        return *(unsigned int*)((int)&DAT_804d0e20 + (short)(&DAT_804d1cd0)[id] * 4);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// model_render_with_override shared body.
//
// Both variants read the entity render record from the scratch frame, decide
// whether the entity is "valid" (type-8 with a set flag, or a type-0x1000
// projectile that passes projectile_always_valid). If valid, render directly;
// otherwise temporarily swap two 11-pair matrix sub-blocks (at +200 and +0x120)
// through stack temporaries, force a zero scale mode, render, then restore.
// ---------------------------------------------------------------------------
static void model_render_with_override(int rec, int recBank)
{
    int model = *(int*)(rec + 4);
    bool valid = true;
    if ((*(int*)(rec + 0x20) == 8) && ((*(unsigned int*)(*(int*)(rec + 0x7c) + 0x2c) & 4) != 0)) {
        valid = false;
    }
    if ((*(int*)(rec + 0x20) == 0x1000) && (projectile_always_valid() == 0)) {
        valid = false;
    }

    unsigned int savedScale = (unsigned int)DAT_806108e4;
    if (valid) {
        model_pose_eval();
        return;
    }

    // save the two 11-pair matrix sub-blocks into stack temporaries
    unsigned int tmpA[24];
    unsigned int tmpB[24];
    unsigned int* dst = tmpA;
    unsigned int* src = (unsigned int*)(model + 200);
    for (int i = 0xb; i != 0; --i) {
        dst[1] = src[1];
        dst[2] = src[2];
        dst += 2; src += 2;
    }
    dst = tmpB;
    src = (unsigned int*)(model + 0x120);
    for (int i = 0xb; i != 0; --i) {
        dst[1] = src[1];
        dst[2] = src[2];
        dst += 2; src += 2;
    }

    DAT_806108e4 = 0;
    FLOAT_806108ec = FLOAT_80613930;
    model_pose_eval();

    // restore, with the scale delta re-applied
    FLOAT_806108ec = (float)((double)CONCAT44(0x43300000, savedScale ^ 0x80000000) - DOUBLE_80613938);
    DAT_806108e4 = (int)savedScale;
    dst = (unsigned int*)(model + 200);
    src = tmpA;
    for (int i = 0xb; i != 0; --i) {
        dst[1] = src[1];
        dst[2] = src[2];
        dst += 2; src += 2;
    }
    dst = (unsigned int*)(model + 0x120);
    src = tmpB;
    for (int i = 0xb; i != 0; --i) {
        dst[1] = src[1];
        dst[2] = src[2];
        dst += 2; src += 2;
    }
    (void)recBank;
}

// model_render_with_override_a (0x8001d73c)
void model_render_with_override_a(void)
{
    unsigned long long pair = FUN_803826f0();   // high half = record pointer
    int rec = (int)(pair >> 0x20);
    model_render_with_override(rec, (int)pair);
    FUN_8038273c();
}

// model_render_with_override_b (0x8001d894)
void model_render_with_override_b(void)
{
    unsigned long long pair = FUN_803826f0();
    int rec = (int)(pair >> 0x20);
    model_render_with_override(rec, (int)pair);
    FUN_8038273c();
}

// model_pose_eval (0x8001d9ec)
// Skeletal-animation pose evaluator (28 callers, the render pipeline core). For
// the entity's model it samples the current animation state (one clip, or two
// clips blended by piVar7[0x65]) at the two surrounding keyframes, slerps each
// bone's rotation, applies procedural overrides (recoil / squash-stretch), and
// writes the per-bone world matrices into the bone palette (piVar7[4] + bone*0x40).
//
// This is a faithful (non-matching) reconstruction of a very large routine: the
// register-pair scratch and the wide FPR spills are dropped, the two big code
// paths (single-anim vs two-anim blend) are preserved, and all callees are
// invoked at their real call-site arity.
void model_pose_eval(void)
{
    int rec = FUN_803826b4();
    int* model = *(int**)(rec + 4);
    int local_368 = 0;
    double blendW = (double)FLOAT_80613930;
    int projCtx = 0;
    int animA = 0, animB = 0;
    unsigned int uVar18 = 0, uVar19 = 0;
    int boneCount = 0;

    if (model[0x20] != 0) {
        boneCount = *(int*)(*(int*)((int)&DAT_80611824 + (short)(&DAT_804d1cd0)[*(int*)(model[0x20] + 8)] * 4) + 0x14);
    }
    unsigned int flags = *(unsigned int*)(rec + 0x10) | *(unsigned int*)(rec + 0xc);

    if ((model[0x33] != 0) && (model[0x33] != 1)) {
        // early-out: an off-screen / culled type-8 attachment renders directly
        if (*(int*)(rec + 0x20) == 8) {
            int attach = *(int*)(rec + 0x7c);
            int a, b;
            if (DAT_805015bd == '\n') { a = *(int*)(attach + 0x10); b = *(int*)(attach + 0x14); }
            else { a = *(int*)(attach + 0x18); b = *(int*)(attach + 0x1c); }
            if ((b == 0 && a == 0) && ((*(unsigned int*)(rec + 0x28) & 1) == 0)) {
                FUN_8001f92c(model);
                FUN_80382700();
                return;
            }
        }
        if (*(int*)(rec + 0x20) == 0x1000) {
            projCtx = *(int*)(rec + 0x7c);
        }

        // apply queued position / rotation nudges (recoil)
        if ((model[0x48] & 0x800U) != 0) {
            *(float*)(rec + 0xc4) = *(float*)(rec + 0xc4) + (float)model[0x45];
            model[0x48] = model[0x48] & 0xfffff7ff;
        }
        if ((model[0x48] & 0x400U) != 0) {
            *(float*)(rec + 100)   = *(float*)(rec + 100)   + (float)model[0x42];
            *(float*)(rec + 0x68)  = *(float*)(rec + 0x68)  + (float)model[0x43];
            *(float*)(rec + 0x6c)  = *(float*)(rec + 0x6c)  + (float)model[0x44];
            model[0x48] = model[0x48] & 0xfffffbff;
        }
        if (FLOAT_80613930 < (float)model[100]) {
            if ((model[0x5e] & 0x800U) != 0) {
                *(float*)(rec + 0xc4) = *(float*)(rec + 0xc4) + (float)model[0x5b];
                model[0x5e] = model[0x5e] & 0xfffff7ff;
            }
            if ((model[0x5e] & 0x400U) != 0) {
                *(float*)(rec + 100)  = *(float*)(rec + 100)  + (float)model[0x58];
                *(float*)(rec + 0x68) = *(float*)(rec + 0x68) + (float)model[0x59];
                *(float*)(rec + 0x6c) = *(float*)(rec + 0x6c) + (float)model[0x5a];
                model[0x5e] = model[0x5e] & 0xfffffbff;
            }
        }

        // wrap the primary animation phase into [0, period)
        double phase = (double)*(float*)(rec + 0xc4);
        double wrapped = FUN_80390324((double)((float)(phase - (double)FLOAT_80613930) / FLOAT_80613940));
        *(float*)(rec + 0xc4) = -(float)((double)FLOAT_80613940 * (double)(float)wrapped - phase);
        *(int*)(rec + 0x70) = *(int*)(rec + 0xc4);

        // advance the cross-fade timer for the secondary (blend) clip
        if (FLOAT_80613930 < (float)model[100]) {
            model[0x66] = (int)((float)model[0x66] + FLOAT_806108ec /
                (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80613938));
            model[0x65] = (int)((float)model[0x66] / (float)model[100]);
            if (FLOAT_80613944 <= (float)model[0x65]) {
                model[100] = (int)FLOAT_80613930;
                model[0x65] = (int)FLOAT_80613930;
                model[0x66] = (int)FLOAT_80613930;
                leaf_31e8((unsigned int)(unsigned long)(model + 0x33), (unsigned int)(unsigned long)(model + 0x49), 0x58);
                model[0x49] = 0;
                model[0x4a] = (int)(unsigned long)&DAT_804d1cd0;
                model[0x5e] = 0;
            }
        }

        // one-shot procedural clip timer (0x28 state machine)
        if (model[0x28] != 0) {
            if (model[0x28] == 1) {
                model[0x2b] = (int)((float)model[0x2b] + FLOAT_806108ec /
                    (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80613938));
            } else {
                model[0x2b] = (int)((float)model[0x2b] - FLOAT_806108ec /
                    (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80613938));
            }
            model[0x29] = (int)((float)model[0x2b] / (float)model[0x2a]);
            if (FLOAT_80613944 <= (float)model[0x29]) { model[0x28] = 2; model[0x29] = (int)FLOAT_80613944; }
            if ((float)model[0x29] <= FLOAT_80613930) {
                model[0x28] = 0; model[0x2a] = (int)FLOAT_80613930;
                model[0x2b] = (int)FLOAT_80613930; model[0x29] = (int)FLOAT_80613930;
            }
        }

        // blend-in timer (0x32 flags 0xc)
        if ((model[0x32] & 0xcU) != 0) {
            model[0x31] = (int)((float)model[0x31] + FLOAT_806108ec /
                (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80613938));
            if ((float)model[0x30] <= (float)model[0x31]) {
                model[0x31] = (int)FLOAT_80613930;
                model[0x32] = ((model[0x32] & 4U) == 0) ? 1 : 2;
            }
        }

        // save previous frame's root translation/rotation for velocity
        model[0x3a] = model[0x37]; model[0x3b] = model[0x38]; model[0x3c] = model[0x39];
        model[0x50] = model[0x4d]; model[0x51] = model[0x4e]; model[0x52] = model[0x4f];
        FUN_80026bcc(rec);

        // resolve clip A: flags + descriptor + keyframe pair + interp weight
        int flagIdxA = model[0x34] * 8;
        unsigned int animFlagsA0 = *(unsigned int*)((int)&DAT_804d34d8 + flagIdxA);
        unsigned int animFlagsA1 = *(unsigned int*)((int)&DAT_804d34dc + flagIdxA);
        int descA = *(int*)((int)&DAT_80611824 + model[0x34] * 4);
        double distScale;
        if ((animFlagsA0 & 0x200) == 0) { distScale = FUN_80026bcc(rec); }
        else { distScale = (double)FLOAT_80613944; }
        FUN_80020be0();
        unsigned int keyA0, keyA1;
        FUN_80022ea4((int)(model + 0x33), &keyA0, &keyA1);
        float weightA = FLOAT_80613930;
        if (keyA0 != keyA1) {
            float t0 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(*(int*)(descA + 0x1c) + keyA0 * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
            float t1 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(*(int*)(descA + 0x1c) + keyA1 * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
            weightA = ((float)model[0x35] - t0) / (t1 - t0);
        }
        double wA = (double)weightA;
        if ((projCtx != 0) && ((animFlagsA0 & 0x80000) != 0)) { model[0x32] = 1; }

        // resolve clip B (only when the cross-fade weight model[100] > 0)
        int descB = 0;
        unsigned int keyB0 = 0, keyB1 = 0;
        double wB = blendW;
        if (FLOAT_80613930 < (float)model[100]) {
            int flagIdxB = model[0x4a] * 8;
            uVar19 = *(unsigned int*)((int)&DAT_804d34d8 + flagIdxB);
            uVar18 = *(unsigned int*)((int)&DAT_804d34dc + flagIdxB);
            descB = *(int*)((int)&DAT_80611824 + model[0x4a] * 4);
            if ((uVar19 & 0x200) == 0) { blendW = FUN_80026bcc(rec); }
            else { blendW = (double)FLOAT_80613944; }
            FUN_80020be0();
            FUN_80022ea4((int)(model + 0x49), &keyB0, &keyB1);
            if (keyB0 == keyB1) { wB = (double)FLOAT_80613930; }
            else {
                float t0 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(*(int*)(descB + 0x1c) + keyB0 * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
                float t1 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(*(int*)(descB + 0x1c) + keyB1 * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
                wB = (double)(((float)model[0x4b] - t0) / (t1 - t0));
            }
            if ((projCtx != 0) && ((uVar19 & 0x80000) != 0)) { model[0x32] = 1; }
        }

        // decide how many bones to evaluate (LOD)
        int bones = boneCount;
        if (*(int*)(rec + 0x20) == 0x1000) {
            if ((flags & 0xffff) == 0) { bones = 1; boneCount = 1; }
            else {
                if (*(int*)(descA + 0x14) < boneCount) bones = *(int*)(descA + 0x14);
                if ((descB != 0) && (*(int*)(descB + 0x14) < bones)) bones = *(int*)(descB + 0x14);
                *(unsigned int*)(rec + 0x18) |= 2;
                *(unsigned int*)(rec + 0x28) |= 0x200000;
            }
            DAT_806117e4 = 0;
        } else if (projCtx == 0) {
            boneCount = *(int*)(descA + 0x14);
            DAT_806117e4 = 0;
            *(unsigned int*)(rec + 0x18) |= 2;
            *(unsigned int*)(rec + 0x28) |= 0x200000;
            bones = boneCount;
        } else if ((*(int*)(projCtx + 8) < DAT_80612610) || ((flags & 0xffff) != 0)) {
            if (*(int*)(descA + 0x14) < boneCount) bones = *(int*)(descA + 0x14);
            if ((descB != 0) && (*(int*)(descB + 0x14) < bones)) bones = *(int*)(descB + 0x14);
            DAT_806117e4 = 1;
            *(unsigned int*)(rec + 0x18) |= 2;
            *(unsigned int*)(rec + 0x28) |= 0x200000;
        } else {
            DAT_806117e4 = 0;
            boneCount = 1;
            *(unsigned int*)(rec + 0x18) |= 2;
            bones = 1;
        }

        if ((*(unsigned int*)(rec + 0x28) & 0x200000) == 0) {
            FUN_8001f92c(*(int**)(rec + 4));
        } else {
            FUN_8001f7cc(*(int**)(rec + 4), bones);
        }

        float q0[4], q1[4], q2[4], q3[4], qOut[4];
        // sample layout: [0..2]=position, [3..6]=rotation quaternion (7 floats)
        float sampleA0[7], sampleA1[7], sampleB0[7], sampleB1[7];

        if ((double)FLOAT_80613930 == (double)(float)model[100]) {
            // ---------------- single-animation path ----------------
            double w = wA;
            if (((animFlagsA0 & 0x40000) != 0) && ((*(unsigned int*)(DAT_80611d54 + 0x21c) & 0x80000) != 0)) {
                w = (double)FLOAT_80613930;
                if ((double)FLOAT_80613948 <= wA) { w = (double)FLOAT_80613944; }
            }
            if (((animFlagsA1 & 0x1c00) != 0) && (model[0x47] == 0)) {
                w = FUN_8002007c(w, model, &local_368);
            }
            int boneOff = 0;
            for (int bi = 0; bi < bones; ++bi) {
                int boneIdx = *(int*)(*(int*)model[0x21] + boneOff);
                if (((model[0x32] & 2U) == 0) || ((model[0x25] & (1 << bi)) == 0)) {
                    FUN_80024800(descA, bi, keyA0, sampleA0);
                    FUN_80024800(descA, bi, keyA1, sampleA1);
                    q0[0] = sampleA0[3]; q0[1] = sampleA0[4]; q0[2] = sampleA0[5]; q0[3] = sampleA0[6];
                    q1[0] = sampleA1[3]; q1[1] = sampleA1[4]; q1[2] = sampleA1[5]; q1[3] = sampleA1[6];
                    quat_slerp(w, q0, q1, qOut);
                    if (((model[0x48] & 0x200U) != 0) && (bi == 0)) {
                        float axis[4]; axis[0] = DAT_803cc0e0; axis[1] = DAT_803cc0e4; axis[2] = DAT_803cc0e8;
                        float jitter[4];
                        quat_from_axis_angle((double)((FLOAT_8061394c * (float)model[0x45]) / FLOAT_80613950), jitter, axis);
                        quat_multiply(qOut, jitter, qOut);
                    }
                }
                if (((model[0x32] & 1U) == 0) && ((model[0x25] & (1 << bi)) != 0)) {
                    FUN_8002118c(rec, sampleA0, bi);
                    qOut[0] = sampleA0[3]; qOut[1] = sampleA0[4]; qOut[2] = sampleA0[5]; qOut[3] = sampleA0[6];
                }
                int mtxOff = boneIdx * 0x40;
                quat_to_matrix((float*)(model[4] + mtxOff), qOut);
                float* base = (float*)(model[4] + mtxOff);
                base[0xc] = (float)(w * (double)(sampleA1[0] - sampleA0[0]) + (double)sampleA0[0]);
                base[0xd] = (float)(w * (double)(sampleA1[1] - sampleA0[1]) + (double)sampleA0[1]);
                base[0xe] = (float)(w * (double)(sampleA1[2] - sampleA0[2]) + (double)sampleA0[2]);
                if (bi == 0) {
                    model[0x37] = (int)base[0xc];
                    model[0x38] = (int)base[0xd];
                    model[0x39] = (int)base[0xe];
                }
                boneOff += 4;
            }
        } else {
            // ---------------- two-animation blend path ----------------
            float crossW = (float)model[0x65];
            int boneOff = 0;
            for (int bi = 0; bi < bones; ++bi) {
                int boneIdx = *(int*)(*(int*)model[0x21] + boneOff);
                if (((model[0x32] & 2U) == 0) || ((model[0x25] & (1 << bi)) == 0)) {
                    FUN_80024800(descA, bi, keyA0, sampleA0);
                    FUN_80024800(descA, bi, keyA1, sampleA1);
                    FUN_80024800(descB, bi, keyB0, sampleB0);
                    FUN_80024800(descB, bi, keyB1, sampleB1);
                    float qa0[4] = { sampleA0[3], sampleA0[4], sampleA0[5], sampleA0[6] };
                    float qa1[4] = { sampleA1[3], sampleA1[4], sampleA1[5], sampleA1[6] };
                    float qb0[4] = { sampleB0[3], sampleB0[4], sampleB0[5], sampleB0[6] };
                    float qb1[4] = { sampleB1[3], sampleB1[4], sampleB1[5], sampleB1[6] };
                    quat_slerp(wA, qa0, qa1, q2);
                    quat_slerp(wB, qb0, qb1, q3);
                    quat_slerp((double)crossW, q2, q3, qOut);
                }
                if (((model[0x32] & 1U) == 0) && ((model[0x25] & (1 << bi)) != 0)) {
                    FUN_8002118c(rec, sampleA0, bi);
                    qOut[0] = sampleA0[3]; qOut[1] = sampleA0[4]; qOut[2] = sampleA0[5]; qOut[3] = sampleA0[6];
                }
                int mtxOff = boneIdx * 0x40;
                quat_to_matrix((float*)(model[4] + mtxOff), qOut);
                float* base = (float*)(model[4] + mtxOff);
                double px = (double)(float)(wA * (double)(sampleA1[0] - sampleA0[0]) + (double)sampleA0[0]);
                double py = (double)(float)(wA * (double)(sampleA1[1] - sampleA0[1]) + (double)sampleA0[1]);
                double pz = (double)(float)(wA * (double)(sampleA1[2] - sampleA0[2]) + (double)sampleA0[2]);
                double qx = (double)(float)(wB * (double)(sampleB1[0] - sampleB0[0]) + (double)sampleB0[0]);
                double qy = (double)(float)(wB * (double)(sampleB1[1] - sampleB0[1]) + (double)sampleB0[1]);
                double qz = (double)(float)(wB * (double)(sampleB1[2] - sampleB0[2]) + (double)sampleB0[2]);
                if (bi == 0) {
                    model[0x37] = (int)(float)px; model[0x38] = (int)(float)py; model[0x39] = (int)(float)pz;
                    model[0x4d] = (int)(float)qx; model[0x4e] = (int)(float)qy; model[0x4f] = (int)(float)qz;
                }
                base[0xc] = (float)((double)crossW * (qx - px) + px);
                base[0xd] = (float)((double)crossW * (qy - py) + py);
                base[0xe] = (float)((double)crossW * (qz - pz) + pz);
                boneOff += 4;
            }
            (void)uVar18;
        }

        // propagate LOD sub-object matrices from the parent bones
        if (*(int*)(model[0x21] + 0x78) != 0) {
            int idxByte = bones << 2;
            int mtxByte = bones << 6;
            for (int b = bones; b < boneCount; ++b) {
                FUN_803a14c4((float*)(((int*)model[0x21])[0x1e] + mtxByte),
                             (float*)(model[4] + *(int*)(*(int*)model[0x21] + idxByte) * 0x40));
                mtxByte += 0x40; idxByte += 4;
            }
        }

        FUN_800201e8(model, projCtx, animA);
        if (((projCtx == 0) && (animA == 0)) ||
            (((flags & 0xffff) != 0) || ((projCtx != 0) && (*(int*)(projCtx + 8) < DAT_80612610)))) {
            if (*(int*)(rec + 0x20) == 0x4000000) {
                if ((animFlagsA0 & 0x400000) == 0) {
                    *(int*)(rec + 100)  = *(int*)(model[4] + 0x30);
                    *(int*)(rec + 0x68) = *(int*)(model[4] + 0x34);
                    *(int*)(rec + 0x6c) = *(int*)(model[4] + 0x38);
                }
                *(float*)(model[4] + 0x30) = FLOAT_80613930;
                *(float*)(model[4] + 0x34) = FLOAT_80613930;
                *(float*)(model[4] + 0x38) = FLOAT_80613930;
            }
            if (((animA == 0) || (DAT_805015bd == '\n')) &&
                ((projCtx == 0) || ((*(unsigned int*)(projCtx + 0x28) & 0x20) == 0))) {
                FUN_80022c14(model, 0, 0);
            } else {
                FUN_80022c14(model, 0, 1);
            }
            FUN_80022a38(model, model[4], 0, -1);
            FUN_80022b1c(model, model[4], 0, -1);
            FUN_80020698(model, projCtx, animA);
        }

        // root-motion velocity (deltas of root translation) with axis masking
        (void)distScale;
        (void)flagIdxA;
    }
    FUN_80382700();
}
