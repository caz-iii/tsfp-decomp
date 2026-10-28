// =============================================================================
// model_anim_eval.cpp - Animation pose-eval helpers (model_pose_eval cluster)
// Address range: 0x80020698 - 0x80021f78
// Batch: 387 | Date: 2026-09-05
//
// Continuation of the skeletal-animation pipeline (model_render.cpp batch_385,
// model_skel_buffers.cpp batch_386). These are the remaining helpers reached from
// model_pose_eval and the procedural-bone / aim-blend layer.
//
//   * model_apply_part_scales_b   (0x80020698) 2nd part-scale pass: barrel-roll + squash on weapon/attach bones
//   * anim_eval_root_bone         (0x80020908) root-bone slerp eval + root-motion delta (+0x3d..0x3f)
//   * anim_phase_advance          (0x80020be0) advance anim phase, wrap/loop, footstep-sound trigger
//   * weapon_aim_lean_update      (0x80020e98) aim-lean/recoil damper -> 2D aim-blend indices
//   * anim_procedural_bone_sample (0x8002118c) procedural bone override: 2D aim-blend quat grid sample
//   * anim_look_dir_solve         (0x80021808) look-at direction solve (atan2 + poly_trig), smoothed
//   * anim_head_aim_matrix        (0x80021a38) head-aim matrix build + inverse-xform to local aim angles
//   * anim_zone_path_step         (0x80021be8) sound-zone path step for root-motion nav
//   * text_find_line_match        (0x80021e6c) line-anchored substring search
//   * anim_pack_load_descriptors  (0x80021f78) load anim descriptors from a pack into the descriptor table
//
// Compile-clean non-matching reconstruction. Register-pair scratch frames
// (FUN_803826e8/e4/d4/f0 return a packed hi=recordPtr/lo=index/ptr u64) are split
// with shifts. mtx4_set_scale is invoked through a typed 3-double pointer to match
// the register-implicit call-site ABI.
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // quat_*, mtx4_*, anim_time_wrap, anim_motion_continuity_check,
                               // math_sqrt/atan2/poly_trig, nav_movement_dispatch,
                               // soundpad_get_zone_reverb_by_id, pack_get_asset_ptr,
                               // player_ai_ambient_sound_start

// ---- raw runtime globals ---------------------------------------------------
extern int    DAT_80611824;    // animation descriptor table base
extern int    DAT_806125fc;    // per-frame effect gate mask A
extern int    DAT_80612600;    // per-frame effect gate mask B
extern int    DAT_80612614;    // active switch/mode selector
extern int    DAT_805cc614;    // sub-mode selector
extern uint   DAT_806109c0;    // frame-rate divisor
extern char   DAT_805015bd;    // region/mode selector ('\n' special)
extern int    DAT_805015c0;    // global feature flags
extern int    DAT_804d34d8;    // per-anim flag table (word 0, stride 8)
extern int    DAT_804d34dc;    // per-anim flag table (word 1, stride 8)
extern short  DAT_804d1cd0;    // anim id -> descriptor index table (short)
extern int    DAT_804d1d1a;    // fallback descriptor index
extern int    DAT_80611d90;    // AI actor pool base (0xea0 stride)
extern int    DAT_80611b8c;    // sound-zone node table base (0x20 stride)
extern float  DAT_8048ca94;    // per-actor nav param table (0x30 stride)
extern float  DAT_8048ca9c;
extern int    DAT_806117e4;    // LOD-active flag
extern int    DAT_806117d8;    // descriptor-index allocation cursor
// aim-blend index/weight outputs (weapon_aim_lean_update -> anim_procedural_bone_sample)
extern uint   DAT_80611818;
extern uint   DAT_80611814;
extern uint   DAT_80611810;
extern uint   DAT_8061180c;
extern float  FLOAT_80611808;
extern float  FLOAT_80611804;
// anim_pack_load_descriptors scratch
extern int    DAT_806117f0;    // descriptor-list count
extern int    DAT_806117f4;    // pack base fixup delta
extern int    DAT_806117f8;    // pack header ptr
extern char*  DAT_804d5bf8;    // per-descriptor ptr list (base of array)

// ---- numeric constants -----------------------------------------------------
extern float  FLOAT_80613930;  // 0.0
extern double DOUBLE_80613938; // uint->double conversion magic bias
extern float  FLOAT_806108ec;  // frame delta seconds
extern float  FLOAT_8060faf8;  // fixed-point scale (keyframe times)
extern float  FLOAT_80613940;  // wrap period (2pi-ish)
extern float  FLOAT_80613944;  // 1.0
extern float  FLOAT_80613948;  // 0.5 threshold
extern float  FLOAT_8061394c;  // rad scale
extern float  FLOAT_80613950;  // 180 / half period
extern float  FLOAT_80613954;  // roll rate
extern float  FLOAT_80613958;  // clamp hi
extern float  FLOAT_80613964;  // squash scale factor
extern float  FLOAT_80613968;  // clamp lo
extern float  FLOAT_8061396c;  // sentinel (init marker)
extern float  FLOAT_80613970;  // damper rate a
extern float  FLOAT_80613974;  // damper rate b
extern float  FLOAT_80613978;  // damper step
extern float  FLOAT_8061397c;  // aim-cell size (1/9)
extern float  FLOAT_80613980;  // wrap lo bound
extern float  FLOAT_80613984;  // quarter-period phase offset
extern float  FLOAT_806117e8;  // barrel-roll accumulator

// ---- raw runtime helpers (self-extern at call-site arity) ------------------
extern "C" {
    unsigned long long FUN_803826e8(void);   // scratch: packed (recPtr<<32 | shortId)
    unsigned long long FUN_803826e4(void);   // scratch: packed (recPtr<<32 | ...)
    unsigned long long FUN_803826d4(void);   // scratch: packed (recPtr<<32 | ...)
    unsigned long long FUN_803826f0(void);   // scratch: packed (recPtr<<32 | ptr)
    void   FUN_80382734(void);                // scratch free (e8)
    void   FUN_80382730(void);                // scratch free (e4)
    void   FUN_80382720(void);                // scratch free (d4)
    void   FUN_8038273c(void);                // scratch free (f0)
    int    FUN_801c08fc(void);                // effect enabled?
    uint   FUN_801c0894(void);                // active effect flags
    uint   FUN_803642b4(int a, int mask);     // flag test
    void   FUN_803a1508(float* out, float* a, float* b); // matrix multiply
    void   FUN_80022ea4(int animState, uint* outA, uint* outB); // keyframe pair
    void   FUN_80024800(int animTbl, int bone, uint key, float* out); // sample keyframe
    void   FUN_80022a38(int* model, int mtxBase, int a, int b);       // build world matrices
    void   FUN_80020e98(int rec);             // weapon_aim_lean_update (nearby, fwd)
    void   FUN_80021808(void);                // anim_look_dir_solve
    double FUN_801b6f68(int inner);           // aim offset X
    double FUN_801b6f3c(int inner);           // aim offset Y
    int    FUN_8038b5c8(int str);             // strlen-ish
    void   FUN_80027a90(double a, double b, double c, unsigned long long d, unsigned long long e,
                        unsigned long long f, unsigned long long g, unsigned long long h,
                        void* idTbl, int count, char* p7, int hdr, char* p8, char* p9,
                        uint r9, void* flagTbl); // descriptor finalize
}

// mtx4_set_scale shared proto is (float*, f,f,f); the call site passes 3 doubles
// into an implicit working matrix. Invoke through a matching typed pointer.
typedef void (*mtx4_set_scale3_fn)(double, double, double);
static const mtx4_set_scale3_fn __mtx4_set_scale3 = (mtx4_set_scale3_fn)&mtx4_set_scale;

// model_apply_part_scales_b (0x80020698)
// Second part-scale pass (sibling of model_apply_part_scales): applies a
// continuous barrel-roll (mtx4_set_rotation_x on FLOAT_806117e8 accumulator) and
// a squash scale to the weapon/attach bone sub-matrices when the matching effect
// flags (0x800 roll, 0x40 squash) are active, plus a distance-normalized uniform
// scale when the projectile/entity is flagged (0x20 / non-'\n' mode).
void model_apply_part_scales_b(unsigned int, unsigned int, int projRec)
{
    unsigned long long packed = FUN_803826e8();
    int recHi = (int)(unsigned int)(packed >> 0x20);
    int idLo  = (int)(unsigned int)packed;
    double distScale = 0.0;   // f1 carry-in from caller (interp/distance scale)

    float work[24];

    if (FUN_801c08fc() != 0) {
        uint eff = FUN_801c0894();
        if (((DAT_806125fc & DAT_80612600 & (int)eff & 0x800) != 0) &&
            ((DAT_80612614 != 0x65) || (DAT_805cc614 == 5))) {
            int shortId = 0;
            if (idLo == 0) {
                if (projRec != 0) { shortId = *(int*)(projRec + 8); }
            } else {
                shortId = (int)*(short*)(idLo + 0x48);
            }
            if (((idLo != 0) || (projRec != 0)) &&
                (FUN_803642b4(shortId, 0x10000) == 0) &&
                (*(int*)(*(int*)(recHi + 0x80) + 4) == 0)) {
                FLOAT_806117e8 = FLOAT_806117e8 + FLOAT_80613954;
                if (FLOAT_80613940 < FLOAT_806117e8) {
                    FLOAT_806117e8 = FLOAT_806117e8 - FLOAT_80613940;
                }
                mtx4_set_rotation_x((double)((FLOAT_8061394c * FLOAT_806117e8) / FLOAT_80613950), (int)work);
                float* m = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x54) * 0x40);
                FUN_803a1508(work, m, m);
            }
        }
        eff = FUN_801c0894();
        if ((((DAT_806125fc & DAT_80612600 & (int)eff & 0x40) != 0) &&
             ((DAT_80612614 != 0x65) || (DAT_805cc614 == 5))) &&
            ((idLo != 0) || (projRec != 0))) {
            __mtx4_set_scale3((double)FLOAT_80613944, (double)FLOAT_80613964, (double)FLOAT_80613964);
            int off = 0;
            for (int i = 0; i < *(int*)(*(int*)(recHi + 4) + 4); ++i) {
                float* m = (float*)(*(int*)(recHi + 0x10) + off);
                FUN_803a1508(work, m, m);
                off += 0x40;
            }
        }
    }

    if (((idLo != 0) && ((*(uint*)(idLo + 0x28) & 0x20) != 0)) ||
        ((projRec != 0) && (DAT_805015bd != '\n'))) {
        float denom;
        if (projRec == 0) { denom = *(float*)(idLo + 0xb0c); }
        else { denom = *(float*)(projRec + 0xc); }
        double s = (double)(float)(distScale / (double)denom);
        __mtx4_set_scale3(s, s, s);
        int off = 0;
        for (int i = 0; i < *(int*)(*(int*)(recHi + 4) + 4); ++i) {
            float* m = (float*)(*(int*)(recHi + 0x10) + off);
            FUN_803a1508(work, m, m);
            off += 0x40;
        }
    }
    FUN_80382734();
}

// anim_eval_root_bone (0x80020908)
// Root-bone animation evaluator: resolves the keyframe pair for the model's anim
// state, slerps the root rotation (with optional motion-continuity snap to 0/1),
// writes the root matrix + translation, builds the child world matrices
// (FUN_80022a38), and computes the per-frame root-motion delta (+0x3d..0x3f) when
// the clip is flagged as root-motion (flags 0x82 / 0x1800), else zeroes it.
void anim_eval_root_bone(unsigned int, unsigned int, int* model)
{
    unsigned long long packed = FUN_803826e4();
    int* pEntity = (int*)(unsigned int)(packed >> 0x20);
    int flagIdx = model[0x34] * 8;
    int descTbl = *(int*)(DAT_80611824 + model[0x34] * 4);
    uint flags0 = *(uint*)(&DAT_804d34d8 + flagIdx);
    uint flags1 = *(uint*)(&DAT_804d34dc + flagIdx);
    anim_time_wrap((int)pEntity, (int)(unsigned int)packed, (int)(model + 0x33));

    if (model[4] != 0) {
        uint keyA, keyB;
        FUN_80022ea4((int)(model + 0x33), &keyA, &keyB);
        float weight = FLOAT_80613930;
        if (keyA != keyB) {
            float t0 = (float)((double)CONCAT44(0x43300000, *(uint*)(*(int*)(descTbl + 0x1c) + keyA * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
            float t1 = (float)((double)CONCAT44(0x43300000, *(uint*)(*(int*)(descTbl + 0x1c) + keyB * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
            weight = ((float)model[0x35] - t0) / (t1 - t0);
        }
        double w = (double)weight;
        if (((DAT_805015c0 & 0x20000000) != 0) &&
            (anim_motion_continuity_check(pEntity) != 0)) {
            w = ((double)FLOAT_80613948 <= w) ? (double)FLOAT_80613944 : (double)FLOAT_80613930;
        }
        float sampleA[7], sampleB[7], qA[4], qB[4], qOut[4];
        FUN_80024800(descTbl, 0, keyA, sampleA);
        FUN_80024800(descTbl, 0, keyB, sampleB);
        qA[0] = sampleA[3]; qA[1] = sampleA[4]; qA[2] = sampleA[5]; qA[3] = sampleA[6];
        qB[0] = sampleB[3]; qB[1] = sampleB[4]; qB[2] = sampleB[5]; qB[3] = sampleB[6];
        quat_slerp(w, qA, qB, qOut);
        quat_to_matrix((float*)model[4], qOut);
        *(float*)(model[4] + 0x30) = (float)(w * (double)(float)((double)sampleB[0] - (double)sampleA[0]) + (double)sampleA[0]);
        *(float*)(model[4] + 0x34) = (float)(w * (double)(float)((double)sampleB[1] - (double)sampleA[1]) + (double)sampleA[1]);
        *(float*)(model[4] + 0x38) = (float)(w * (double)(float)((double)sampleB[2] - (double)sampleA[2]) + (double)sampleA[2]);
        FUN_80022a38(model, model[4], 0, -1);

        if ((flags1 & 0x82) == 0 && (flags0 & 0x1800) == 0) {
            model[0x3d] = (int)FLOAT_80613930;
            model[0x3e] = (int)FLOAT_80613930;
            model[0x3f] = (int)FLOAT_80613930;
        } else {
            model[0x3d] = (int)((float)model[0x37] - (float)model[0x3a]);
            model[0x3e] = (int)((float)model[0x38] - (float)model[0x3b]);
            model[0x3f] = (int)((float)model[0x39] - (float)model[0x3c]);
            model[0x3a] = model[0x37];
            model[0x3b] = model[0x38];
            model[0x3c] = model[0x39];
        }
        *(uint*)(*model + 0x28) = *(uint*)(*model + 0x28) | 8;
    }
    FUN_80382730();
}

// anim_phase_advance (0x80020be0)
// The pre-sample setup called by model_pose_eval / anim_blend_eval_*: advances
// the model's animation phase (+8) by frame-delta * playback-rate (+0x34), wraps
// or clamps to the clip length depending on loop flag (bit 0 of the anim flag
// table), fires footstep ambient sounds on wrap for type-8 actors, and (for
// looping clips) accumulates the root-motion wrap-around offset (+0x1c..0x24).
void anim_phase_advance(void)
{
    unsigned long long packed = FUN_803826d4();
    int rec = (int)(unsigned int)(packed >> 0x20);
    int idLo = (int)(unsigned int)packed;
    int flagIdx = *(int*)(rec + 4) * 8;
    int descTbl = *(int*)(DAT_80611824 + *(int*)(rec + 4) * 4);
    float clipLen = *(float*)(descTbl + 0x18);
    double len = (double)clipLen;
    uint flags0 = *(uint*)(&DAT_804d34d8 + flagIdx);
    int dir = 0;
    uint loop = *(uint*)(&DAT_804d34dc + flagIdx) & 1;

    if ((double)FLOAT_80613930 < len) {
        *(int*)(rec + 0x50) = 0;
        *(int*)(rec + 0xc) = *(int*)(rec + 8);
        *(float*)(rec + 8) = *(float*)(rec + 8) +
            (FLOAT_806108ec * *(float*)(rec + 0x34)) /
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80613938);

        if (loop == 0) {
            if (len < (double)*(float*)(rec + 8)) {
                *(int*)(rec + 0x50) = 1;
                dir = 1;
                *(float*)(rec + 8) = clipLen;
            }
        } else {
            while (len < (double)*(float*)(rec + 8)) {
                dir = 1;
                *(float*)(rec + 8) = (float)((double)*(float*)(rec + 8) - len);
                *(int*)(rec + 0x50) = 1;
                if ((idLo != 0) && (*(int*)(idLo + 0x20) == 8) && ((flags0 & 0x1000000) != 0)) {
                    player_ai_ambient_sound_start();
                }
            }
        }
        if (loop == 0) {
            if (*(float*)(rec + 8) < FLOAT_80613930) {
                dir = -1;
                *(int*)(rec + 0x50) = 1;
                *(float*)(rec + 8) = FLOAT_80613930;
            }
        } else {
            while (*(float*)(rec + 8) < FLOAT_80613930) {
                dir = -1;
                *(float*)(rec + 8) = (float)((double)*(float*)(rec + 8) + len);
                *(int*)(rec + 0x50) = 1;
            }
        }
        if ((*(int*)(rec + 0x50) != 0) && (loop != 0)) {
            float first[7], last[7];
            if (dir < 1) {
                FUN_80024800(descTbl, 0, 0, first);
                FUN_80024800(descTbl, 0, (uint)(*(int*)(descTbl + 0x10) - 1), last);
            } else {
                FUN_80024800(descTbl, 0, (uint)(*(int*)(descTbl + 0x10) - 1), first);
                FUN_80024800(descTbl, 0, 0, last);
            }
            *(float*)(rec + 0x1c) = (last[0] + *(float*)(rec + 0x1c)) - first[0];
            *(float*)(rec + 0x20) = (last[1] + *(float*)(rec + 0x20)) - first[1];
            *(float*)(rec + 0x24) = (last[2] + *(float*)(rec + 0x24)) - first[2];
        }
    }
    FUN_80382720();
}

// weapon_aim_lean_update (0x80020e98)
// Damps the model's aim-lean angles (+0xb0/+0xb4) toward their targets (+0xb8/
// +0xbc) with a rate-limited step (snapping when close or when the damper term
// exceeds 1), clamps them to [FLOAT_80613968, FLOAT_80613958], then converts the
// smoothed angles into the 2D aim-blend grid indices/weights (DAT_80611818/14/10/
// 0c + FLOAT_80611808/04) consumed by anim_procedural_bone_sample. Only for bone
// types {0, 0x45, 0x7c}.
void weapon_aim_lean_update(int param_1)
{
    int inner = *(int*)(param_1 + 0x7c);
    DAT_806117e4 = 0;
    float aimX, aimY;

    if (*(int*)(inner + 0x44) == 0) {
        int model = *(int*)(param_1 + 4);
        bool hiX = FLOAT_80613958 < *(float*)(model + 0xb0);
        if (hiX) { *(float*)(model + 0xb0) = FLOAT_80613958; }
        bool loX = *(float*)(model + 0xb0) < FLOAT_80613968;
        if (loX) { *(float*)(model + 0xb0) = FLOAT_80613968; }
        bool hiY = FLOAT_80613958 < *(float*)(model + 0xb4);
        if (hiY) { *(float*)(model + 0xb4) = FLOAT_80613958; }
        bool loY = *(float*)(model + 0xb4) < FLOAT_80613968;
        if (loY) { *(float*)(model + 0xb4) = FLOAT_80613968; }

        if (FLOAT_8061396c == *(float*)(model + 0xb8)) {
            *(int*)(model + 0xb8) = *(int*)(model + 0xb0);
            *(int*)(model + 0xbc) = *(int*)(model + 0xb4);
        }
        double invRate = (double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000);
        float damperA = FLOAT_80613970 * FLOAT_806108ec;
        float damperB = FLOAT_80613974 / (float)(invRate - DOUBLE_80613938);

        if (hiY || loY || hiX || loX) {
            *(int*)(inner + 0xb9c) = 0;
        }
        if ((*(int*)(inner + 0xb9c) != 0) ||
            ((ABS(*(float*)(model + 0xb0) - *(float*)(model + 0xb8)) < FLOAT_80613948 &&
              ABS(*(float*)(model + 0xb4) - *(float*)(model + 0xbc)) < FLOAT_80613948) ||
             (FLOAT_80613944 < damperA * damperB))) {
            *(int*)(inner + 0xb9c) = 1;
            aimX = *(float*)(model + 0xb0);
            aimY = *(float*)(model + 0xb4);
        } else {
            aimX = *(float*)(model + 0xb0);
            float refX = *(float*)(model + 0xb8);
            float step = FLOAT_80613978 * FLOAT_806108ec;
            if (step <= ABS(aimX - refX)) {
                aimX = (aimX <= refX) ? (refX - step) : (refX + step);
            }
            aimY = *(float*)(model + 0xb4);
            float refY = *(float*)(model + 0xbc);
            if (step <= ABS(aimY - refY)) {
                aimY = (aimY <= refY) ? (refY - step) : (refY + step);
            }
        }
    } else {
        int model = *(int*)(param_1 + 4);
        aimX = *(float*)(model + 0xb0);
        aimY = *(float*)(model + 0xb4);
    }

    int model = *(int*)(param_1 + 4);
    *(float*)(model + 0xb8) = aimX;
    *(float*)(model + 0xbc) = aimY;
    int boneType = *(int*)(*(int*)(model + 0x80) + 4);
    if (boneType != 0x45) {
        if (boneType < 0x45) {
            if (boneType != 0) return;
        } else if (boneType != 0x7c) {
            return;
        }
    }
    DAT_80611818 = (uint)((FLOAT_80613958 + aimX) / FLOAT_8061397c);
    DAT_80611814 = DAT_80611818 + 1;
    if (DAT_80611818 == 9) { DAT_80611814 = 9; }
    DAT_80611810 = (uint)((FLOAT_80613958 + aimY) / FLOAT_8061397c);
    DAT_8061180c = DAT_80611810 + 1;
    if (DAT_80611810 == 9) { DAT_8061180c = 9; }
    FLOAT_80611808 = -(FLOAT_8061397c *
        (float)((double)CONCAT44(0x43300000, DAT_80611818 ^ 0x80000000) - DOUBLE_80613938) -
        (FLOAT_80613958 + aimX)) / FLOAT_8061397c;
    FLOAT_80611804 = -(FLOAT_8061397c *
        (float)((double)CONCAT44(0x43300000, DAT_80611810 ^ 0x80000000) - DOUBLE_80613938) -
        (FLOAT_80613958 + aimY)) / FLOAT_8061397c;
}

// anim_procedural_bone_sample (0x8002118c)
// Procedural bone-override sampler (the FUN_8002118c used by model_pose_eval when
// a bone is flagged for aim-driven posing). Refreshes the 2D aim-blend indices
// (weapon_aim_lean_update) then bilinearly slerps the requested bone's rotation
// across the 2x2 aim-blend grid cell (four keyframe samples per axis), writing the
// resulting position+quaternion (7 floats) to the output. Supports a two-clip
// blend (secondary descriptor at +0x9c weighted by +0xa4) when +0xa0 != 0.
void anim_procedural_bone_sample(unsigned int, unsigned int, int boneIdx)
{
    unsigned long long packed = FUN_803826e8();
    int recHi = (int)(unsigned int)(packed >> 0x20);
    float* out = (float*)(unsigned int)packed;
    int inner = *(int*)(recHi + 0x7c);
    int model = *(int*)(recHi + 4);
    if (DAT_806117e4 == 1) {
        FUN_80020e98(recHi);
    }

    float sA[7], sB[7], sC[7], sD[7];   // 4 grid-corner samples (primary desc)
    float sE[7], sF[7], sG[7], sH[7];   // 4 grid-corner samples (secondary desc)
    float qAB[4], qCD[4], qEF[4], qGH[4], qABCD[4], qEFGH[4];

    if (*(int*)(model + 0xa0) == 0) {
        int descA = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[*(int*)(model + 0x98)] * 4);
        if ((*(int*)(inner + 0x80) == 0) || (boneIdx < 9) || (0xc < boneIdx)) {
            FUN_80024800(descA, boneIdx, DAT_80611818 + DAT_80611810 * 10, sA);
            FUN_80024800(descA, boneIdx, DAT_80611814 + DAT_80611810 * 10, sB);
            FUN_80024800(descA, boneIdx, DAT_80611818 + DAT_8061180c * 10, sC);
            FUN_80024800(descA, boneIdx, DAT_80611814 + DAT_8061180c * 10, sD);
            qAB[0]=sA[3]; qAB[1]=sA[4]; qAB[2]=sA[5]; qAB[3]=sA[6];
            float qB2[4]={sB[3],sB[4],sB[5],sB[6]};
            float qC2[4]={sC[3],sC[4],sC[5],sC[6]};
            float qD2[4]={sD[3],sD[4],sD[5],sD[6]};
            quat_slerp((double)FLOAT_80611808, qAB, qB2, qABCD);
            quat_slerp((double)FLOAT_80611808, qC2, qD2, qEFGH);
            float qOut[4];
            quat_slerp((double)FLOAT_80611804, qABCD, qEFGH, qOut);
            out[0] = sA[0]; out[1] = sA[1]; out[2] = sA[2];
            out[3] = qOut[0]; out[4] = qOut[1]; out[5] = qOut[2]; out[6] = qOut[3];
        } else {
            FUN_80024800(*(int*)(DAT_80611824 + DAT_804d1d1a * 4), boneIdx, 0, out);
        }
    } else {
        int descA = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[*(int*)(model + 0x98)] * 4);
        int descB = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[*(int*)(model + 0x9c)] * 4);
        FUN_80024800(descA, boneIdx, DAT_80611818 + DAT_80611810 * 10, sA);
        FUN_80024800(descA, boneIdx, DAT_80611814 + DAT_80611810 * 10, sB);
        FUN_80024800(descA, boneIdx, DAT_80611818 + DAT_8061180c * 10, sC);
        FUN_80024800(descA, boneIdx, DAT_80611814 + DAT_8061180c * 10, sD);
        FUN_80024800(descB, boneIdx, DAT_80611818 + DAT_80611810 * 10, sE);
        FUN_80024800(descB, boneIdx, DAT_80611814 + DAT_80611810 * 10, sF);
        FUN_80024800(descB, boneIdx, DAT_80611818 + DAT_8061180c * 10, sG);
        FUN_80024800(descB, boneIdx, DAT_80611814 + DAT_8061180c * 10, sH);
        float qA2[4]={sA[3],sA[4],sA[5],sA[6]}, qB2[4]={sB[3],sB[4],sB[5],sB[6]};
        float qC2[4]={sC[3],sC[4],sC[5],sC[6]}, qD2[4]={sD[3],sD[4],sD[5],sD[6]};
        float qE2[4]={sE[3],sE[4],sE[5],sE[6]}, qF2[4]={sF[3],sF[4],sF[5],sF[6]};
        float qG2[4]={sG[3],sG[4],sG[5],sG[6]}, qH2[4]={sH[3],sH[4],sH[5],sH[6]};
        quat_slerp((double)FLOAT_80611808, qA2, qB2, qAB);
        quat_slerp((double)FLOAT_80611808, qC2, qD2, qCD);
        quat_slerp((double)FLOAT_80611808, qE2, qF2, qEF);
        quat_slerp((double)FLOAT_80611808, qG2, qH2, qGH);
        quat_slerp((double)FLOAT_80611804, qAB, qCD, qABCD);
        quat_slerp((double)FLOAT_80611804, qEF, qGH, qEFGH);
        float qOut[4];
        quat_slerp((double)*(float*)(model + 0xa4), qABCD, qEFGH, qOut);
        out[0] = sA[0]; out[1] = sA[1]; out[2] = sA[2];
        out[3] = qOut[0]; out[4] = qOut[1]; out[5] = qOut[2]; out[6] = qOut[3];
    }
    FUN_80382734();
}

// anim_look_dir_solve (0x80021808)
// Solves the model's look direction toward its aim target and writes the smoothed
// look position (out[0..2]). When not yet initialized (+0xb9c==0) it snaps to the
// stored aim point (+0xb88..+0xb90); otherwise it computes the target angle via
// atan2, rate-limits the yaw toward it (wrapping into range), and reconstructs the
// look point from the radius (math_sqrt) and math_poly_trig(angle).
void anim_look_dir_solve(void)
{
    unsigned long long packed = FUN_803826f0();
    int rec = (int)(unsigned int)(packed >> 0x20);
    float* out = (float*)(unsigned int)packed;
    int inner = *(int*)(rec + 0x7c);

    if (*(int*)(inner + 0xb9c) == 0) {
        out[0] = *(float*)(inner + 0xb88);
        out[1] = *(float*)(inner + 0xb8c);
        out[2] = *(float*)(inner + 0xb90);
        *(float*)(inner + 0xb94) = FLOAT_8061396c;
    } else {
        double dz = (double)(*(float*)(inner + 0xb90) - *(float*)(rec + 0x6c));
        double dx = (double)(*(float*)(inner + 0xb88) - *(float*)(rec + 100));
        double damper = (double)(FLOAT_80613970 * FLOAT_806108ec *
            (FLOAT_80613974 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80613938)));
        double radius = math_sqrt((double)(float)(dx * dx + (double)(float)(dz * dz)));
        double ang = math_atan2(dx, dz);
        ang = (double)((FLOAT_80613950 * (float)ang) / FLOAT_8061394c);
        if (ang < (double)FLOAT_80613930) { ang = (double)(float)(ang + (double)FLOAT_80613940); }

        if (FLOAT_8061396c == *(float*)(inner + 0xb94)) {
            *(int*)(inner + 0xb98) = *(int*)(inner + 0xb8c);
        }
        double prev = (double)*(float*)(inner + 0xb94);
        if (((double)FLOAT_8061396c != prev) && (damper < (double)FLOAT_80613944)) {
            for (ang = ang - prev; ang = (double)(float)ang, ang <= (double)FLOAT_80613980; ang = ang + (double)FLOAT_80613940) {}
            for (; (double)FLOAT_80613950 < ang; ang = (double)(float)(ang - (double)FLOAT_80613940)) {}
            ang = (double)(float)(ang * damper + prev);
            if ((double)FLOAT_80613930 <= ang) {
                if ((double)FLOAT_80613940 <= ang) { ang = (double)(float)(ang - (double)FLOAT_80613940); }
            } else {
                ang = (double)(float)(ang + (double)FLOAT_80613940);
            }
        }
        *(float*)(inner + 0xb94) = (float)ang;
        double phase = (double)((float)((double)FLOAT_8061394c * ang) / FLOAT_80613950);
        double cosv = math_poly_trig((double)(float)(phase - (double)FLOAT_80613984));
        out[0] = (float)(radius * cosv + (double)*(float*)(rec + 100));
        float lerpY = (float)(damper * (double)(float)((double)*(float*)(inner + 0xb8c) - (double)*(float*)(inner + 0xb98)) + (double)*(float*)(inner + 0xb98));
        out[1] = lerpY;
        *(float*)(inner + 0xb98) = lerpY;
        double sinv = math_poly_trig(phase);
        out[2] = (float)(radius * sinv + (double)*(float*)(rec + 0x6c));
    }
    FUN_8038273c();
}

// anim_head_aim_matrix (0x80021a38)
// Builds the head/aim bone matrix: composes a translate+rotZ from the entity's
// position/yaw with two parent bone matrices, inverts the result, resolves the
// look point (anim_look_dir_solve), transforms it into the head-local frame, and
// stores the resulting pitch/yaw aim angles into the model (+0xb0/+0xb4), setting
// the aim-dirty bit (0x10 at +200).
void anim_head_aim_matrix(void)
{
    int rec = (int)FUN_803826f0();
    int inner = *(int*)(rec + 0x7c);
    float* dst = (float*)(inner + 0xc24);
    float local[25];
    float inv[16];

    mtx4_set_translate_rotz((double)*(float*)(rec + 100), (double)*(float*)(rec + 0x68),
                            (double)*(float*)(rec + 0x6c),
                            (double)((FLOAT_8061394c * *(float*)(rec + 0x70)) / FLOAT_80613950), local);
    FUN_803a1508(dst, (float*)(*(int*)(*(int*)(rec + 4) + 0x10) + *(int*)(*(int*)(*(int*)(rec + 4) + 0x84) + 0x10) * 0x40), dst);
    FUN_803a1508(dst, (float*)(*(int*)(*(int*)(rec + 4) + 0x10) + *(int*)(*(int*)(*(int*)(rec + 4) + 0x84) + 0xc) * 0x40), dst);
    FUN_803a1508(dst, local, inv);
    mtx4_invert(local, inv);
    FUN_80021808();

    float look[3];
    look[0] = (float)((double)0.0 + FUN_801b6f68(inner));
    look[1] = (float)((double)0.0 + FUN_801b6f3c(inner));
    look[2] = 0.0f;
    // NB: look[] is seeded from the anim_look_dir_solve output stashed by the game
    // in the scratch frame; the two offsets add the per-actor aim bias.
    mtx4_transform_point_alias(local, look);
    double ly = (double)look[1];
    double lz = (double)look[2];
    double lx = (double)look[0];
    double yaw = math_atan2(ly, lz);
    *(float*)(*(int*)(rec + 4) + 0xb0) = -((FLOAT_80613950 * (float)yaw) / FLOAT_8061394c);
    double pitch = math_atan2(lx, math_sqrt((double)(float)(lz * lz + (double)(float)(ly * ly))));
    *(float*)(*(int*)(rec + 4) + 0xb4) = (FLOAT_80613950 * (float)pitch) / FLOAT_8061394c;
    *(uint*)(*(int*)(rec + 4) + 200) = *(uint*)(*(int*)(rec + 4) + 200) | 0x10;
    FUN_8038273c();
}

// anim_zone_path_step (0x80021be8)
// Computes a nav step for root-motion driven actors: samples the anim clip's
// first/last-frame root offset, rotates it by the actor yaw (+0x70), adds it to
// the entity position, and if the target is far enough from the current sound-zone
// centre runs nav_movement_dispatch to slide the actor along the floor. Writes the
// resolved position to out (param_3) when provided.
void anim_zone_path_step(unsigned int, unsigned int, float* out)
{
    unsigned long long packed = FUN_803826e8();
    int rec = (int)(unsigned int)(packed >> 0x20);
    int idLo = (int)(unsigned int)packed;
    int inner = *(int*)(rec + 0x7c);
    int descTbl = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[idLo] * 4);

    float first[7], last[7];
    FUN_80024800(descTbl, 0, 0, first);
    uint lastKey = (uint)((double)(float)((double)CONCAT44(0x43300000, (*(int*)(descTbl + 0x10) - 1U) ^ 0x80000000) - DOUBLE_80613938) * 1.0);
    FUN_80024800(descTbl, 0, lastKey, last);

    float d[3];
    d[0] = last[0] - first[0];
    d[1] = 0.0f;
    d[2] = last[2] - first[2];
    float rot[16];
    mtx4_set_rotation_y((double)((FLOAT_8061394c * *(float*)(rec + 0x70)) / FLOAT_80613950), rot);
    mtx4_transform_vector_alias(rot, d);
    float px = *(float*)(rec + 100) + d[0];
    float py = *(float*)(rec + 0x68) + d[1];
    float pz = *(float*)(rec + 0x6c) + d[2];

    int zone = DAT_80611b8c + *(int*)(inner + 0x678) * 0x20;
    double zx = (double)(*(float*)(zone + 0xc) - px);
    double zz = (double)(*(float*)(zone + 0x14) - pz);
    float r0 = soundpad_get_zone_reverb_by_id(*(int*)(inner + 0x678));
    float r1 = soundpad_get_zone_reverb_by_id(*(int*)(inner + 0x678));
    float pos[3] = { px, py, pz };
    if ((float)((double)r1 * (double)r0) <= (float)(zx * zx + (double)(float)(zz * zz))) {
        int navIdx = *(short*)(inner + 0x48) * 0x30;
        nav_movement_dispatch((double)*(float*)(&DAT_8048ca94 + navIdx),
                              (double)*(float*)(&DAT_8048ca9c + navIdx),
                              (rec - DAT_80611d90) / 0xea0, rec + 100, pos, (float*)0, (int*)0, 0, 0);
        px = pos[0]; py = pos[1]; pz = pos[2];
    }
    if (out != (float*)0) {
        out[0] = px;
        out[1] = py;
        out[2] = pz;
    }
    (void)first; (void)last;
    FUN_80382734();
}

// text_find_line_match (0x80021e6c)
// Line-anchored substring search: scans the haystack (packed scratch lo ptr, len
// param_3) for an occurrence of the needle (packed scratch hi ptr) that begins at
// the start of a line (offset 0 or preceded by '\n') and matches for the needle's
// full length.
void text_find_line_match(unsigned int, unsigned int, int hayLen)
{
    unsigned long long packed = FUN_803826f0();
    int needle = (int)(unsigned int)(packed >> 0x20);
    char* hay = (char*)(unsigned int)packed;

    if ((needle != 0) && (hay != (char*)0) && (*hay != '\0')) {
        int needleLen = FUN_8038b5c8(needle);
        int i = 0;
        int remaining = hayLen;
        if (hayLen > 0) {
            do {
                if (((i == 0) || (*(char*)(needle + i - 1) == '\n')) &&
                    (*hay == *(char*)(needle + i))) {
                    // NB: preserved from decomp - compares needle head against
                    // the line-anchored haystack window
                    int k = 1;
                    char* h = hay;
                    char* n = (char*)(needle + i);
                    while (true) {
                        n = n + 1; h = h + 1;
                        if ((needleLen <= k) || (hayLen <= i + k) || (*n != *h)) break;
                        k = k + 1;
                    }
                    if (k == needleLen) break;
                }
                i = i + 1;
                remaining = remaining - 1;
            } while (remaining != 0);
        }
    }
    FUN_8038273c();
}

// anim_pack_load_descriptors (0x80021f78)
// Loads a pack of animation descriptors into the global descriptor table: fetches
// the pack asset (pack_get_asset_ptr), walks its sub-entries to build the
// per-descriptor pointer list (DAT_804d5bf8), assigns each a table slot (id ->
// index in DAT_804d1cd0, descriptor at DAT_80611824[idx]), sets the per-anim flag
// words (0x1800 / 0x82), fixes up embedded pointers by the load-base delta, then
// hands the finished list to FUN_80027a90 for final linkage.
void anim_pack_load_descriptors(double, unsigned long long, unsigned long long,
                                unsigned long long p4, unsigned long long p5,
                                unsigned long long p6, unsigned long long p7,
                                unsigned long long p8, unsigned char* packName)
{
    int slotStride = 10;
    int hdr = (int)(size_t)pack_get_asset_ptr((const char*)packName, (u32*)0);
    char* cur = (char*)(hdr + 0x28);
    DAT_806117f0 = 0;
    DAT_806117f4 = hdr;
    DAT_806117f8 = hdr;

    // build the per-descriptor pointer list
    for (int i = 0; i < *(int*)(hdr + 0xc); ++i) {
        (&DAT_804d5bf8)[DAT_806117f0] = cur;
        cur = cur + *(int*)((&DAT_804d5bf8)[DAT_806117f0] + 0x14) * 8 + 0x48;
        DAT_806117f0 = DAT_806117f0 + 1;
    }

    char* last = cur;
    for (int i = 0; i < *(int*)(DAT_806117f8 + 0xc); ++i) {
        uint aligned = ((uint)(size_t)(last + 7)) & 0xfffffff8;
        *(short*)((int)&DAT_804d1cd0 + slotStride) = (short)DAT_806117d8;
        int idx4 = DAT_806117d8 * 4;
        int idx8 = DAT_806117d8 * 8;
        DAT_806117d8 = DAT_806117d8 + 1;
        *(uint*)(DAT_80611824 + idx4) = aligned;
        int desc = *(int*)(DAT_80611824 + idx4);
        uint rawLen = *(uint*)(desc + 0xc) ^ 0x80000000;
        float clipLen = (float)(((double)(float)((double)CONCAT44(0x43300000, rawLen) - DOUBLE_80613938) - (double)FLOAT_80613944) / (double)FLOAT_8060faf8);
        *(float*)(desc + 0x18) = clipLen;
        *(int*)(&DAT_804d34dc + idx8) = 0x82;
        *(int*)(&DAT_804d34d8 + idx8) = 0x1800;

        // fixup embedded pointers by the load-base delta
        char* p20 = *(char**)(aligned + 0x20);
        char* body = (char*)(aligned + 0x38);
        *(char**)(aligned + 0x20) = (p20 == (char*)0) ? (char*)0 : (p20 + DAT_806117f4);
        if (i != 0) {
            *(char**)(aligned + 0x28) = body;
            *(char**)(aligned + 0x1c) = body + *(int*)(aligned + 0x24) * 0x20;
            last = body + *(int*)(aligned + 0x24) * 0x20 + *(int*)(aligned + 0x10) * 4;
            for (int j = 0; j < *(int*)(aligned + 0x24); ++j) {
                uint v = *(uint*)(aligned + 0x1c);
                char* base = *(char**)(aligned + 0x28);
                *(uint*)(base + j * 0x20 + 0x10) = v;
            }
        } else {
            last = cur;
        }
        slotStride = slotStride + 2;
    }
    FUN_80027a90(0.0, (double)FLOAT_80613944, DOUBLE_80613938, p4, p5, p6, p7, p8,
                 &DAT_804d1cd0, *(int*)(DAT_806117f8 + 0xc), last, DAT_806117f8, (char*)0, (char*)0, 0, &DAT_804d34d8);
}
