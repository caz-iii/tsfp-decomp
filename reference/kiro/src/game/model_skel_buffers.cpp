// =============================================================================
// model_skel_buffers.cpp - Skeletal bone-matrix buffer mgmt + anim blend helpers
// Address range: 0x8001f7cc - 0x800201e8
// Batch: 386 | Date: 2026-09-05
//
// The bone-matrix scratch buffer layer + per-part animation-blend evaluators
// used by model_pose_eval (batch_385, model_render.cpp). model_pose_eval calls
// bone_buffer_acquire (0x8001f7cc) on the LOD path and bone_buffer_release
// (0x8001f92c) on the no-LOD path to (de)allocate a 0xf8 bone-matrix block from a
// freelist keyed on the current frame stamp; bone_buffer_store (0x8001f994)
// copies the resolved bone world positions into that block.
//
//   * bone_buffer_acquire        (0x8001f7cc) alloc/reuse 0xf8 bone block (freelist -0x7fb2adc8)
//   * bone_buffer_release        (0x8001f92c) return block to freelist if stamp matches
//   * bone_buffer_store          (0x8001f994) copy bone positions into block
//   * bone_local_delta           (0x8001fa54) bone world-pos delta in entity-local space
//   * model_instance_anim_init   (0x8001fbc0) register model for update + clear anim state
//   * anim_blend_eval_indexed    (0x8001fc78) slerp/blend two keyframes -> bone palette (idx path)
//   * anim_blend_eval_direct     (0x8001fe84) slerp/blend two keyframes -> bone palette (ptr path)
//   * anim_blend_eval_flagged    (0x80020034) anim_blend_eval_indexed + set render/proj bits
//   * anim_event_squash_stretch  (0x8002007c) one-shot squash/stretch on anim event
//   * model_apply_part_scales    (0x800201e8) per-part scale-fx applier (weapon/attach bones)
//
// Compile-clean non-matching reconstruction. Register-pair scratch frames
// (FUN_803826f0 returns a packed hi=recordPtr/lo=index u64) are expressed as an
// unsigned long long and split with shifts, matching model_render.cpp. A couple
// of cross-module named helpers whose shared-header prototype does not match the
// (register-implicit) call-site ABI are invoked through typed function-pointer
// casts: mtx4_set_scale(3 doubles -> implicit matrix) and anim_event_scan_active
// (no explicit args -> reads the scratch frame).
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // memset_wrapper, freelist_pop/push, quat_*, mtx4_*,
                               // entity_build_local_matrix, entity_register_for_update

// ---- raw runtime globals ---------------------------------------------------
extern int    DAT_80611c24;    // trigger/mover pool base (0x2e8 stride)
extern int    DAT_80611824;    // animation descriptor table base
extern int    DAT_806125fc;    // per-frame effect gate mask A
extern int    DAT_80612600;    // per-frame effect gate mask B
extern int    DAT_80612614;    // active switch/mode selector
extern int    DAT_805cc614;    // sub-mode selector
extern uint   DAT_806109c0;    // frame-rate divisor
extern int    DAT_804d34dc;    // per-anim flag table (word 1, stride 8)

// ---- numeric constants -----------------------------------------------------
extern float  FLOAT_80613930;  // 0.0
extern double DOUBLE_80613938; // uint->double conversion magic bias
extern float  FLOAT_806108ec;  // frame delta seconds
extern float  FLOAT_8060faf8;  // fixed-point scale (keyframe times)
extern float  FLOAT_80613944;  // 1.0
extern float  FLOAT_80613948;  // 0.5 threshold
extern float  FLOAT_80613950;  // squash delta (down)
extern float  FLOAT_80613958;  // squash delta (up/large)
extern float  FLOAT_8061395c;  // scale-fx factor
extern float  FLOAT_80613954;  // scale-fx factor
extern float  FLOAT_80613960;  // scale-fx factor (attach bones)

// ---- raw runtime helpers (self-extern at call-site arity) ------------------
extern "C" {
    unsigned long long FUN_803826f0(void);   // scratch: packed (recPtr<<32 | index)
    void   FUN_8038273c(void);                // scratch free
    void   FUN_80020be0(void);                // pre-sample setup
    void   FUN_80022ea4(int animState, uint* outA, uint* outB); // keyframe pair
    void   FUN_80024800(int animTbl, int bone, uint key, float* out); // sample keyframe
    void   FUN_80023514(unsigned long long ctx, double t);    // instance init base
    void   FUN_803a14c4(float* dstMtx, float* srcMtx);        // matrix copy
    void   FUN_803a1508(float* out, float* a, float* b);      // matrix multiply
    int    FUN_801c08fc(void);                // effect enabled?
    uint   FUN_801c0894(void);                // active effect flags
    void   FUN_801c01b8(int* ctx);            // finalize effect pass
    int*   __save_gpr_28(void);
    void   __restore_gpr_28(void);
}

// mtx4_set_scale's shared prototype is (float* mtx, float,float,float); the
// PowerPC call site passes three doubles into an implicit working matrix. Invoke
// through a matching typed pointer to preserve the observed ABI.
typedef void (*mtx4_set_scale3_fn)(double, double, double);
static const mtx4_set_scale3_fn __mtx4_set_scale3 = (mtx4_set_scale3_fn)&mtx4_set_scale;

// anim_event_scan_active's shared prototype takes (int,u16); the call site reads
// the scratch frame and takes no explicit args.
typedef int (*anim_event_scan_active_fn)(void);
static const anim_event_scan_active_fn __anim_event_scan_active0 =
    (anim_event_scan_active_fn)&anim_event_scan_active;

// bone_buffer_acquire (0x8001f7cc)
// Ensure the model has a bone-matrix scratch block (0xf8 bytes) tagged with the
// current frame stamp (*(entity+0x2c)). Pull one from the freelist; if empty,
// scan the trigger/mover pool for a reclaimable block, else give up. Clamps the
// stored LOD bone count to 0x14.
void bone_buffer_acquire(int* model, int lodBones)
{
    if ((model[0x24] == 0) ||
        (*(int*)(*model + 0x2c) != *(int*)(model[0x24] + 0xf4))) {
        u32* block = (u32*)freelist_pop(-0x7fb2adc8);
        model[0x24] = 0;
        int poolBase = DAT_80611c24;
        u32* chosen = block;
        if (block == (u32*)0x0) {
            int tries = 10;
            int off = 0x9b0;
            do {
                bool last = tries == 0;
                tries = tries - 1;
                chosen = block;
                if (last) goto done;
                chosen = (u32*)(off + -0x7fb2aeb0);
                int* slotPtr = (int*)(off + -0x7fb2adbc);
                uint slot = *(uint*)(off + -0x7fb2adbc);
                int rec;
                if (slot == 0) {
                    rec = 0;
                } else {
                    rec = poolBase + (slot >> 0x16) * 0x2e8;
                    if (*(uint*)(rec + 0x2c) != slot) {
                        rec = 0;
                        *slotPtr = 0;
                    }
                }
                if ((rec == 0) || (*(int*)(rec + 4) == 0)) goto done;
                if ((*(uint*)(rec + 0x28) & 1) == 0) goto done;
                off = off + -0xf8;
                if (*(int*)(rec + 0x2c) != *slotPtr) goto done;
                *(u32*)(*(int*)(rec + 4) + 0x90) = 0;
            } while (true);
        }
    done:
        if (chosen != (u32*)0x0) {
            model[0x24] = (int)chosen;
            memset_wrapper((void*)model[0x24], 0, 0xf8);
            int clamped = 0x14;
            if (lodBones < 0x14) {
                clamped = lodBones;
            }
            *(int*)(model[0x24] + 0xf0) = clamped;
            *(u32*)(model[0x24] + 0xf4) = *(u32*)(*model + 0x2c);
        }
    }
}

// bone_buffer_release (0x8001f92c)
// Return the model's bone block to the freelist when its frame stamp matches the
// entity's, then clear the slot.
void bone_buffer_release(int* model)
{
    u32* block = (u32*)model[0x24];
    if (block != (u32*)0x0) {
        if (*(int*)(*model + 0x2c) == (int)block[0x3d]) {
            freelist_push(-0x7fb2adc8, (void**)block);
            *(u32*)(model[0x24] + 0xf0) = 0;
        }
        model[0x24] = 0;
    }
}

// bone_buffer_store (0x8001f994)
// Copy each LOD bone's world position (from the bone palette at model[4]) into
// the bone block, indexed through the bone-remap table (*model[0x21]).
void bone_buffer_store(int* model)
{
    int block = model[0x24];
    if (block == 0) return;
    if (model[4] == 0) return;
    if ((*model != 0) && ((*(uint*)(*model + 0x28) & 0x200000) == 0)) return;

    int count = *(int*)(block + 0xf0);
    int idxOff = count << 2;
    int dstOff = count * 0xc;
    if (count == 0) return;
    do {
        dstOff = dstOff - 0xc;
        idxOff = idxOff - 4;
        u32* dst = (u32*)(block + dstOff);
        int bone = *(int*)(*(int*)model[0x21] + idxOff);
        int src = model[4] + bone * 0x40;
        *dst   = *(u32*)(src + 0x30);
        dst[1] = *(u32*)(src + 0x34);
        dst[2] = *(u32*)(src + 0x38);
        count = count - 1;
    } while (count != 0);
}

// bone_local_delta (0x8001fa54)
// Compute the delta between a bone's palette world position and the block-stored
// position, transformed into the entity's local space, scaled by the frame-rate
// divisor. Writes a Vec3 to out (zero if the model/buffers are absent).
void bone_local_delta(unsigned int, unsigned int, float* out)
{
    unsigned long long packed = FUN_803826f0();
    int* model = (int*)(unsigned int)(packed >> 0x20);
    int index = (int)(unsigned int)packed;
    float zero = FLOAT_80613930;

    if ((model == (int*)0x0) || (model[0x24] == 0) || (model[0x21] == 0)) {
        out[0] = FLOAT_80613930;
        out[1] = zero;
        out[2] = zero;
    } else {
        float mtx[17];
        entity_build_local_matrix(mtx, (void*)*model, 0, 0);
        int blkOff = index * 0xc;
        int blk = model[0x24] + blkOff;
        int src = model[4] + *(int*)(*(int*)model[0x21] + index * 4) * 0x40;
        float d[3];
        d[0] = *(float*)(src + 0x30) - *(float*)(model[0x24] + blkOff);
        d[1] = *(float*)(src + 0x34) - *(float*)(blk + 4);
        d[2] = *(float*)(src + 0x38) - *(float*)(blk + 8);
        mtx4_transform_vector_alias(mtx, d);
        float invRate = (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
        out[0] = d[0] * invRate;
        out[1] = d[1] * invRate;
        out[2] = d[2] * invRate;
    }
    FUN_8038273c();
}

// model_instance_anim_init (0x8001fbc0)
// Initialize a model instance's animation/render state: register it for the
// per-frame update pass (event 0xb) and clear the interpolation accumulators.
void model_instance_anim_init(unsigned long long ctx, double t, int model)
{
    int inner = *(int*)(model + 0x7c);
    FUN_80023514(ctx, t);
    entity_register_for_update(model, 0xb, -0x7ffe03cc);
    float zero = FLOAT_80613930;
    *(float*)(model + 0xc4) = FLOAT_80613930;
    *(float*)(model + 100)  = zero;
    *(float*)(model + 0x68) = zero;
    *(float*)(model + 0x6c) = zero;
    *(uint*)(model + 0x10) = *(uint*)(model + 0x10) | 0xffff;
    *(float*)(inner + 0xb3c) = zero;
}

// anim_blend_eval_indexed (0x8001fc78)
// gpr-scratch (indexed) variant. Writes the interpolated root translation into
// the entity record (entity+100/0x68/0x6c) and zeroes the palette root at +0x30.
void anim_blend_eval_indexed(void)
{
    int* rec = __save_gpr_28();
    int entity = *rec;
    *(u32*)(entity + 0x70) = *(u32*)(entity + 0xc4);
    FUN_80020be0();
    int descTbl = *(int*)(DAT_80611824 + rec[0x34] * 4);
    uint keyA, keyB;
    FUN_80022ea4((int)(rec + 0x33), &keyA, &keyB);
    float weight = FLOAT_80613930;
    if (keyA != keyB) {
        float t0 = (float)((double)CONCAT44(0x43300000, *(uint*)(*(int*)(descTbl + 0x1c) + keyA * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
        float t1 = (float)((double)CONCAT44(0x43300000, *(uint*)(*(int*)(descTbl + 0x1c) + keyB * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
        weight = ((float)rec[0x35] - t0) / (t1 - t0);
    }
    double w = (double)weight;

    float sampleA[7], sampleB[7], qA[4], qB[4], qOut[4];
    FUN_80024800(descTbl, 0, keyA, sampleA);
    FUN_80024800(descTbl, 0, keyB, sampleB);
    qA[0] = sampleA[3]; qA[1] = sampleA[4]; qA[2] = sampleA[5]; qA[3] = sampleA[6];
    qB[0] = sampleB[3]; qB[1] = sampleB[4]; qB[2] = sampleB[5]; qB[3] = sampleB[6];
    quat_slerp(w, qA, qB, qOut);
    quat_to_matrix((float*)rec[4], qOut);

    int off = 0;
    for (int i = 0; i < *(int*)(rec[1] + 4); ++i) {
        FUN_803a14c4((float*)rec[4], (float*)(rec[4] + off));
        off += 0x40;
    }
    *(float*)(entity + 100)  = (float)(w * (double)(float)((double)sampleB[0] - (double)sampleA[0]) + (double)sampleA[0]);
    *(float*)(entity + 0x68) = (float)(w * (double)(float)((double)sampleB[1] - (double)sampleA[1]) + (double)sampleA[1]);
    *(float*)(entity + 0x6c) = (float)(w * (double)(float)((double)sampleB[2] - (double)sampleA[2]) + (double)sampleA[2]);
    *(float*)(rec[4] + 0x30) = FLOAT_80613930;
    *(float*)(rec[4] + 0x34) = FLOAT_80613930;
    *(float*)(rec[4] + 0x38) = FLOAT_80613930;
    __restore_gpr_28();
}

// anim_blend_eval_direct (0x8001fe84)
// direct-ptr variant. Same math as _indexed but reaches the model record through
// the packed scratch pair, and writes the root translation into the palette
// matrix rows (palette[0xc..0xe]) instead of the entity record.
void anim_blend_eval_direct(void)
{
    unsigned long long packed = FUN_803826f0();
    int recHi = (int)(unsigned int)(packed >> 0x20);
    float* palette = (float*)(unsigned int)packed;
    FUN_80020be0();
    int descTbl = *(int*)(DAT_80611824 + *(int*)(recHi + 4) * 4);
    uint keyA, keyB;
    FUN_80022ea4(recHi, &keyA, &keyB);
    float weight = FLOAT_80613930;
    if (keyA != keyB) {
        float t0 = (float)((double)CONCAT44(0x43300000, *(uint*)(*(int*)(descTbl + 0x1c) + keyA * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
        float t1 = (float)((double)CONCAT44(0x43300000, *(uint*)(*(int*)(descTbl + 0x1c) + keyB * 4) ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8;
        weight = (*(float*)(recHi + 8) - t0) / (t1 - t0);
    }
    double w = (double)weight;

    float sampleA[7], sampleB[7], qA[4], qB[4], qOut[4];
    FUN_80024800(descTbl, 0, keyA, sampleA);
    FUN_80024800(descTbl, 0, keyB, sampleB);
    qA[0] = sampleA[3]; qA[1] = sampleA[4]; qA[2] = sampleA[5]; qA[3] = sampleA[6];
    qB[0] = sampleB[3]; qB[1] = sampleB[4]; qB[2] = sampleB[5]; qB[3] = sampleB[6];
    quat_slerp(w, qA, qB, qOut);
    quat_to_matrix(palette, qOut);
    palette[0xc] = (float)(w * (double)(float)((double)sampleB[0] - (double)sampleA[0]) + (double)sampleA[0]);
    palette[0xd] = (float)(w * (double)(float)((double)sampleB[1] - (double)sampleA[1]) + (double)sampleA[1]);
    palette[0xe] = (float)(w * (double)(float)((double)sampleB[2] - (double)sampleA[2]) + (double)sampleA[2]);
    FUN_8038273c();
}

// anim_blend_eval_flagged (0x80020034)
// Wrapper: run the indexed blend evaluator then mark the record dirty (bit 1 at
// +0x18) and force the full-skinning path (bit 0x200000 at +0x28).
void anim_blend_eval_flagged(int rec)
{
    anim_blend_eval_indexed();
    *(uint*)(rec + 0x18) = *(uint*)(rec + 0x18) | 2;
    *(uint*)(rec + 0x28) = *(uint*)(rec + 0x28) | 0x200000;
}

// anim_event_squash_stretch (0x8002007c)
// When an animation event fires this frame (and hasn't already been consumed via
// flag 4), nudge the model's squash/stretch phase (+0x70) up or down per the
// per-anim flag table, then clamp the returned weight to {0,1}. Returns the
// (possibly clamped) weight.
double anim_event_squash_stretch(double weight, int* model, int* outFired)
{
    *outFired = 0;
    if (((model != (int*)0x0) && (*model != 0)) &&
        (__anim_event_scan_active0() != 0) && ((model[0x48] & 4U) == 0)) {
        model[0x48] = model[0x48] | 4;
        *outFired = 1;
        uint animFlags = *(uint*)(&DAT_804d34dc + model[0x34] * 8);
        if ((animFlags & 0x800) == 0) {
            if ((animFlags & 0x400) == 0) {
                if ((animFlags & 0x1000) != 0) {
                    *(float*)(*model + 0x70) = *(float*)(*model + 0x70) + FLOAT_80613950;
                }
            } else {
                *(float*)(*model + 0x70) = *(float*)(*model + 0x70) + FLOAT_80613958;
            }
        } else {
            *(float*)(*model + 0x70) = *(float*)(*model + 0x70) - FLOAT_80613958;
        }
        float clamped = FLOAT_80613930;
        if ((double)FLOAT_80613948 < weight) {
            clamped = FLOAT_80613944;
        }
        weight = (double)clamped;
        *(u32*)(*model + 0xc4) = *(u32*)(*model + 0x70);
    }
    return weight;
}

// ---------------------------------------------------------------------------
// model_apply_part_scales (0x800201e8)
// Per-part scale-effect applier run at the end of pose evaluation. For the active
// effect flags (FUN_801c0894 gated by DAT_806125fc & DAT_80612600 and the mode
// selector), it scales specific bone sub-matrices via mtx4_set_scale +
// FUN_803a1508 (matrix multiply into the working matrix). Handles the four
// effect classes (0x400 whole-model, 0x1/0x2/0x8 head/limb by bone type) and the
// weapon/attach bone-visibility bits (0xfc0) from the model or projectile record.
// ---------------------------------------------------------------------------
static bool part_scale_type_ok(uint boneType)
{
    // preserved predicate: type <= 1 OR type == 0x15 OR type == 0x7c
    bool inLow = true;
    if ((1 < boneType) && (boneType != 0x15)) {
        inLow = false;
    }
    if ((!inLow) && (boneType != 0x7c)) {
        return false;
    }
    return true;
}

void model_apply_part_scales(unsigned int, unsigned int, int projRec)
{
    unsigned long long packed = FUN_803826f0();
    int recHi = (int)(unsigned int)(packed >> 0x20);
    int* model = (int*)(unsigned int)packed;

    float work[22];

    if ((FUN_801c08fc() != 0) &&
        ((model != (int*)0x0) || ((projRec != 0) && (*(int*)(projRec + 8) != 0)))) {
        uint eff;

        // whole-model uniform scale (flag 0x400)
        eff = FUN_801c0894();
        if (((DAT_806125fc & DAT_80612600 & (int)eff & 0x400) != 0) &&
            ((DAT_80612614 != 0x65) || (DAT_805cc614 == 5))) {
            __mtx4_set_scale3((double)FLOAT_80613944, (double)FLOAT_8061395c, (double)FLOAT_80613944);
            FUN_803a1508(work, *(float**)(recHi + 0x10), *(float**)(recHi + 0x10));
        }

        // head-bone scale (flag 0x1)
        eff = FUN_801c0894();
        if (((DAT_806125fc & DAT_80612600 & (int)eff & 1) != 0) &&
            ((DAT_80612614 != 0x65) || (DAT_805cc614 == 5))) {
            if (part_scale_type_ok(*(uint*)(*(int*)(recHi + 0x80) + 4))) {
                double s = (double)FLOAT_80613954;
                __mtx4_set_scale3(s, s, s);
                float* m = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x18) * 0x40);
                FUN_803a1508(work, m, m);
            }
        }

        // limb-bone scale (flag 0x2)
        eff = FUN_801c0894();
        if (((DAT_806125fc & DAT_80612600 & (int)eff & 2) != 0) &&
            ((DAT_80612614 != 0x65) || (DAT_805cc614 == 5))) {
            if (part_scale_type_ok(*(uint*)(*(int*)(recHi + 0x80) + 4))) {
                double s = (double)FLOAT_80613948;
                __mtx4_set_scale3(s, s, s);
                float* m = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x18) * 0x40);
                FUN_803a1508(work, m, m);
            }
        }

        // dual-bone scale (flag 0x8)
        eff = FUN_801c0894();
        if (((DAT_806125fc & DAT_80612600 & (int)eff & 8) != 0) &&
            ((DAT_80612614 != 0x65) || (DAT_805cc614 == 5))) {
            if (part_scale_type_ok(*(uint*)(*(int*)(recHi + 0x80) + 4))) {
                double s = (double)FLOAT_80613954;
                __mtx4_set_scale3(s, s, s);
                float* m0 = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x2c) * 0x40);
                FUN_803a1508(work, m0, m0);
                float* m1 = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x30) * 0x40);
                FUN_803a1508(work, m1, m1);
            }
        }
    }

    // weapon / attachment bone-visibility scale bits (0xfc0) from model or proj record
    if (((model != (int*)0x0) && ((model[0x2fa] & 0xfc0U) != 0)) ||
        ((projRec != 0) && ((*(uint*)(projRec + 0x34) & 0xfc0) != 0))) {
        uint bits;
        if (projRec == 0) {
            bits = (uint)model[0x2fa];
        } else {
            bits = *(uint*)(projRec + 0x34);
        }
        double s = (double)FLOAT_80613960;
        if ((bits & 0x40) != 0) {
            __mtx4_set_scale3(s, s, s);
            float* m = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x54) * 0x40);
            FUN_803a1508(work, m, m);
        }
        if ((bits & 0x80) != 0) {
            __mtx4_set_scale3(s, s, s);
            float* m = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x34) * 0x40);
            FUN_803a1508(work, m, m);
        }
        if ((bits & 0x100) != 0) {
            __mtx4_set_scale3(s, s, s);
            float* m = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x38) * 0x40);
            FUN_803a1508(work, m, m);
        }
        if ((bits & 0x200) != 0) {
            __mtx4_set_scale3(s, s, s);
            float* m = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x44) * 0x40);
            FUN_803a1508(work, m, m);
        }
        if ((bits & 0x400) != 0) {
            __mtx4_set_scale3(s, s, s);
            float* m = (float*)(*(int*)(recHi + 0x10) + *(int*)(*(int*)(recHi + 0x84) + 0x48) * 0x40);
            FUN_803a1508(work, m, m);
        }
    }

    if (model != (int*)0x0) {
        FUN_801c01b8(model);
    }
    FUN_8038273c();
}
