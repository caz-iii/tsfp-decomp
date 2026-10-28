// =============================================================================
// anim_pose_sample.cpp - Per-bone pose sampling + clip (re)init/blend setup
// Address range: 0x80026fa8 - 0x800279f4 (exclusive of the cutscene block)
// Batch: 396 | Date: 2026-09-05
//
// The pose sampler that anim_commit_pose_matrices (batch 395) drives, plus the
// clip (re)initialisation / blend-slot setup pass. These sit at the heart of the
// animation pipeline (batches 385-395).
//
//   * anim_sample_pose_range   (0x80026fa8) sample & blend a bone range into the model matrices (FUN_80026fa8)
//   * anim_clip_init_blend     (0x80027764) (re)initialise a clip into the primary/secondary slot (FUN_80027764)
//
// Compile-clean non-matching reconstruction. The wide paired-single float-ABI
// spill locals Ghidra surfaced (in_f25..in_f31 / in_ps*) are dropped; the sampled
// key data comes from the anim_sample_keyframe (FUN_80024800) out-params. Named
// callees defined in prior batches are reached by their FUN_ address-symbols.
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // anim_phase_advance, quat_slerp, quat_to_matrix,
                               // player_get_active_count, leaf_31e8

// ---- anim helpers defined in prior batches (by FUN_ symbol) ----------------
extern "C" {
    double FUN_80026bcc(int rec);                                  // anim_clip_field_b08 (batch 395)
    void   FUN_80026d00(double speed, int slot);                   // anim_slot_set_speed (batch 395)
    void   FUN_80022ea4(int slot, unsigned int* outLo, unsigned int* outHi); // anim_get_keyframe_pair (batch 391)
    void   FUN_80024800(int animTbl, int bone, unsigned int key, float* out); // anim_sample_keyframe (batch 390)
    double FUN_80390324(double x);                                 // periodic reduce (frac/poly)

    // this-batch entries referenced by each other / other TUs
    void   FUN_80026fa8(int frame, int primSlot, int* secSlot, int outCtx,
                        int startBone, int endBone);
}

// ---- raw runtime helpers (scratch-frame getters / free) --------------------
extern "C" {
    unsigned long long FUN_803826c8(void);   // scratch: packed (recHi<<32 | outCtx)
    unsigned long long FUN_803826e8(void);   // scratch: packed (recHi<<32 | stateBase)
    void   FUN_80382714(void);                // scratch free (c8)
    void   FUN_80382734(void);                // scratch free (e8)
}

// ---- raw runtime globals ---------------------------------------------------
extern int    DAT_80611824;    // animation descriptor table base (idx*4)
extern short  DAT_804d1cd0;    // anim id -> descriptor index table (short)
extern uint   DAT_804d34d8;    // per-anim flag table word 0 (idx*8)
extern uint   DAT_804d34dc;    // per-anim flag table word 1 (idx*8)
extern int    DAT_806117e4;    // sampler scratch flag (cleared each pass)
extern uint   DAT_806109c0;    // per-frame delta-time numerator (biased uint)

extern float  FLOAT_80613930;  // 0.0
extern float  FLOAT_80613940;  // 1.0 (period / normalizer)
extern float  FLOAT_80613944;  // 1.0
extern float  FLOAT_8060faf8;  // fixed-point scale (keyframe times)
extern float  FLOAT_806108ec;  // per-frame delta-time increment
extern double DOUBLE_80613938; // uint->double magic bias

// convert a biased-uint keyframe tick to seconds
static inline float ticks_to_seconds(unsigned int rawTicks)
{
    unsigned int t = rawTicks ^ 0x80000000u;
    return (float)((double)CONCAT44(0x43300000, t) - DOUBLE_80613938) / FLOAT_8060faf8;
}

// -----------------------------------------------------------------------------
// anim_sample_pose_range (0x80026fa8)
// Sample the animation for a range of bones [startBone..endBone] and write the
// interpolated rotation matrices + translations into the model's matrix block
// (state[0x10]). Advances the primary clip time (delta-time accumulation at
// +0xc4), retiring the secondary clip when it finishes; resolves the bracketing
// keyframe pair for the primary (and, when a secondary blend is active, the
// secondary) clip, then for each bone slerps the two quats, converts to a matrix
// and lerps the translation. When both slots are active it cross-blends the two
// sampled poses by the model blend weight (+0x194).
// -----------------------------------------------------------------------------
extern "C" void FUN_80026fa8(int frame, int primSlot, int* secSlot, int outCtx,
                             int startBone, int endBone)
{
    (void)frame;

    unsigned long long packed = FUN_803826c8();
    int recHi   = (int)(unsigned int)(packed >> 0x20);   // animation-state record
    int stateBase = (int)(unsigned int)packed;           // per-clip primary state
    int model   = *(int*)(recHi + 4);

    // advance primary clip time, wrap through periodic reduce, mirror to +0x70
    double curT  = (double)*(float*)(recHi + 0xc4);
    double wrap  = FUN_80390324((double)((float)(curT - (double)FLOAT_80613930) / FLOAT_80613940));
    *(float*)(recHi + 0xc4) = -(float)((double)FLOAT_80613940 * (double)(float)wrap - curT);
    *(int*)(recHi + 0x70)   = *(int*)(recHi + 0xc4);

    // secondary clip: advance its blend timer, retire when it reaches 1.0
    if (FLOAT_80613930 < *(float*)(outCtx + 0xc)) {
        *(float*)(outCtx + 0x14) += FLOAT_806108ec /
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000u) - DOUBLE_80613938);
        *(float*)(outCtx + 0x10) = *(float*)(outCtx + 0x14) / *(float*)(outCtx + 0xc);
        if (FLOAT_80613944 <= *(float*)(outCtx + 0x10)) {
            *(float*)(outCtx + 0xc) = FLOAT_80613930;
            *(float*)(outCtx + 0x10) = FLOAT_80613930;
            *(float*)(outCtx + 0x14) = FLOAT_80613930;
            leaf_31e8((u32)stateBase, (u32)(uintptr_t)secSlot, 0x58);
            secSlot[0] = 0;
            secSlot[0x15] = 0;
        }
    }

    // roll the primary slot's sampled offsets forward one frame
    *(int*)(stateBase + 0x1c) = *(int*)(stateBase + 0x10);
    *(int*)(stateBase + 0x20) = *(int*)(stateBase + 0x14);
    *(int*)(stateBase + 0x24) = *(int*)(stateBase + 0x18);
    secSlot[7] = secSlot[4];
    secSlot[8] = secSlot[5];
    secSlot[9] = secSlot[6];

    FUN_80026bcc(recHi);   // resolve clip curve/speed field A (side effects on rec)

    int primIdx  = *(int*)(stateBase + 4);
    unsigned int flagsLo = *(unsigned int*)(&DAT_804d34d8 + primIdx * 8);
    unsigned int flagsHi = *(unsigned int*)(&DAT_804d34dc + primIdx * 8);
    int primTbl  = *(int*)(DAT_80611824 + primIdx * 4);
    if ((flagsLo & 0x200) == 0)
        FUN_80026bcc(recHi);

    anim_phase_advance();

    unsigned int loP, hiP;
    FUN_80022ea4(stateBase, &loP, &hiP);
    float fracP = FLOAT_80613930;
    if (loP != hiP) {
        float t0 = ticks_to_seconds(*(unsigned int*)(*(int*)(primTbl + 0x1c) + loP * 4));
        float t1 = ticks_to_seconds(*(unsigned int*)(*(int*)(primTbl + 0x1c) + hiP * 4));
        fracP = (*(float*)(stateBase + 8) - t0) / (t1 - t0);
    }
    double blendP = (double)fracP;

    // secondary clip sampling (only when its blend weight is live)
    int  secTbl = 0;
    unsigned int loS = 0, hiS = 0;
    double blendS = (double)FLOAT_80613930;
    bool hasSecondary = (FLOAT_80613930 < *(float*)(outCtx + 0xc));
    if (hasSecondary) {
        secTbl = *(int*)(DAT_80611824 + secSlot[1] * 4);
        if ((*(unsigned int*)(&DAT_804d34d8 + secSlot[1] * 8) & 0x200) == 0)
            FUN_80026bcc(recHi);
        anim_phase_advance();
        FUN_80022ea4((int)(uintptr_t)secSlot, &loS, &hiS);
        if (loS != hiS) {
            float t0 = ticks_to_seconds(*(unsigned int*)(*(int*)(secTbl + 0x1c) + loS * 4));
            float t1 = ticks_to_seconds(*(unsigned int*)(*(int*)(secTbl + 0x1c) + hiS * 4));
            blendS = (double)(((float)secSlot[2] - t0) / (t1 - t0));
        }
    }

    DAT_806117e4 = 0;
    *(unsigned int*)(recHi + 0x18) |= 2;
    *(unsigned int*)(recHi + 0x28) |= 0x200000;

    float keyA[16];   // primary lo/hi rotation+translation scratch (local_d4 block)
    float keyB[16];   // secondary lo/hi scratch (local_10c block)
    float qLoP[4], qHiP[4], qLoS[4], qHiS[4], qBlendP[4], qBlendS[4], qOut[4];

    if (FLOAT_80613930 == *(float*)(outCtx + 0xc)) {
        // primary only
        for (int bone = startBone; bone <= endBone; ++bone) {
            int mtxBone = *(int*)(*(int*)(model + 0x84) + bone * 4);
            FUN_80024800(primTbl, bone, loP, keyA);
            FUN_80024800(primTbl, bone, hiP, keyA + 8);
            qLoP[0]=keyA[5]; qLoP[1]=keyA[6]; qLoP[2]=keyA[7]; qLoP[3]=keyA[4];
            qHiP[0]=keyA[13]; qHiP[1]=keyA[14]; qHiP[2]=keyA[15]; qHiP[3]=keyA[12];
            quat_slerp(blendP, qLoP, qHiP, qOut);
            int m = mtxBone * 0x40;
            quat_to_matrix((float*)(*(int*)(model + 0x10) + m), qOut);

            float tx = (float)(blendP * (double)(keyA[10] - keyA[2]) + (double)keyA[2]);
            float ty = (float)(blendP * (double)(keyA[9]  - keyA[1]) + (double)keyA[1]);
            float tz = (float)(blendP * (double)(keyA[8]  - keyA[0]) + (double)keyA[0]);
            if (bone == 0) {
                unsigned int fLo = flagsLo, fHi = flagsHi;
                if ((*(unsigned int*)(outCtx + 0x18) & 1) != 0) { fLo |= 0x1000; fHi |= 0x80; }
                *(float*)(stateBase + 0x10) = tx;
                *(float*)(stateBase + 0x14) = ty;
                *(float*)(stateBase + 0x18) = tz;
                if ((fHi & 0x80) != 0) tx = FLOAT_80613930;
                if (((fLo & 1) != 0) || (player_get_active_count() == 9)) ty = FLOAT_80613930;
                if ((fLo & 0x1000) != 0) tz = FLOAT_80613930;
            }
            *(float*)(*(int*)(model + 0x10) + m + 0x30) = tx;
            *(float*)(*(int*)(model + 0x10) + m + 0x34) = ty;
            *(float*)(*(int*)(model + 0x10) + m + 0x38) = tz;
        }
    } else {
        // cross-blend primary + secondary by the model blend weight (+0x194)
        double crossW = (double)*(float*)(model + 0x194);
        float wSlot = *(float*)(outCtx + 0x10);
        for (int bone = startBone; bone <= endBone; ++bone) {
            int mtxBone = *(int*)(*(int*)(model + 0x84) + bone * 4);
            FUN_80024800(primTbl, bone, loP, keyA);
            FUN_80024800(primTbl, bone, hiP, keyA + 8);
            FUN_80024800(secTbl,  bone, loS, keyB);
            FUN_80024800(secTbl,  bone, hiS, keyB + 8);
            qLoP[0]=keyA[5]; qLoP[1]=keyA[6]; qLoP[2]=keyA[7]; qLoP[3]=keyA[4];
            qHiP[0]=keyA[13]; qHiP[1]=keyA[14]; qHiP[2]=keyA[15]; qHiP[3]=keyA[12];
            qLoS[0]=keyB[5]; qLoS[1]=keyB[6]; qLoS[2]=keyB[7]; qLoS[3]=keyB[4];
            qHiS[0]=keyB[13]; qHiS[1]=keyB[14]; qHiS[2]=keyB[15]; qHiS[3]=keyB[12];
            quat_slerp(blendP, qLoP, qHiP, qBlendP);
            quat_slerp(blendS, qLoS, qHiS, qBlendS);
            quat_slerp(crossW, qBlendP, qBlendS, qOut);
            int m = mtxBone * 0x40;
            quat_to_matrix((float*)(*(int*)(model + 0x10) + m), qOut);

            float pTx = (float)(blendP * (double)(keyA[10] - keyA[2]) + (double)keyA[2]);
            float pTy = (float)(blendP * (double)(keyA[9]  - keyA[1]) + (double)keyA[1]);
            float pTz = (float)(blendP * (double)(keyA[8]  - keyA[0]) + (double)keyA[0]);
            float sTx = (float)(blendS * (double)(keyB[10] - keyB[2]) + (double)keyB[2]);
            float sTy = (float)(blendS * (double)(keyB[9]  - keyB[1]) + (double)keyB[1]);
            float sTz = (float)(blendS * (double)(keyB[8]  - keyB[0]) + (double)keyB[0]);
            *(float*)(*(int*)(model + 0x10) + m + 0x30) = wSlot * (sTx - pTx) + pTx;
            *(float*)(*(int*)(model + 0x10) + m + 0x34) = wSlot * (sTy - pTy) + pTy;
            *(float*)(*(int*)(model + 0x10) + m + 0x38) = wSlot * (sTz - pTz) + pTz;
        }
    }

    *(float*)(stateBase + 0x28) = FLOAT_80613930;
    *(float*)(stateBase + 0x2c) = FLOAT_80613930;
    *(float*)(stateBase + 0x30) = FLOAT_80613930;
    secSlot[10] = 0;
    secSlot[0xb] = 0;
    secSlot[0xc] = 0;

    FUN_80382714();
}

// -----------------------------------------------------------------------------
// anim_clip_init_blend (0x80027764)
// (Re)initialise a clip into the primary (param_5) or secondary (param_6) blend
// slot. When starting fresh (blend weight 0) the clip goes into the primary slot;
// otherwise it is cross-faded into the secondary slot (leaf_31e8 copies the old
// primary block first, unless the incoming clip forbids blending). Primes the
// slot id/descriptor, resets its offset accumulators, samples the first keyframe,
// and clears the per-slot delta fields.
// -----------------------------------------------------------------------------
extern "C" void FUN_80027764(double blendWeight, double speed, int primState,
                             int* primSlot, int* secSlot)
{
    (void)primState;

    unsigned long long packed = FUN_803826e8();
    int animId = (int)(unsigned int)(packed >> 0x20);
    int stateBase = (int)(unsigned int)packed;
    int descIdx = (int)(short)(&DAT_804d1cd0)[animId];

    float key[4];   // local_68 block (first keyframe sample)

    int* slot;
    if ((double)FLOAT_80613930 == blendWeight) {
        // fresh start -> primary slot
        *(float*)(stateBase + 0xc) = (float)blendWeight;
        *(float*)(stateBase + 0x14) = FLOAT_80613930;
        slot = primSlot;
        slot[0] = animId;
        slot[1] = descIdx;
        slot[0xd] = (int)(float)speed;
        FUN_80026d00((double)FLOAT_80613930, (int)(uintptr_t)slot);
        secSlot[0] = 0;
        secSlot[1] = (int)(short)DAT_804d1cd0;
    } else {
        // cross-fade -> secondary slot
        if ((*secSlot != 0) && (*secSlot != animId)) {
            if (((*(unsigned int*)(&DAT_804d34dc + descIdx * 8) & 1) != 0) &&
                ((*(unsigned int*)(&DAT_804d34dc + descIdx * 8) & 0x200) != 0)) {
                FUN_80382734();
                return;
            }
            leaf_31e8((u32)(uintptr_t)primSlot, (u32)(uintptr_t)secSlot, 0x58);
        }
        *(float*)(stateBase + 0xc) = (float)speed;
        *(float*)(stateBase + 0x14) = FLOAT_80613930;
        slot = secSlot;
        slot[0] = animId;
        slot[1] = descIdx;
        slot[0xd] = (int)(float)speed;
        FUN_80026d00((double)FLOAT_80613930, (int)(uintptr_t)slot);
    }

    slot[0xe] = 0;
    slot[0xf] = 0;
    slot[0x10] = 0;
    slot[0x11] = 0;
    slot[0x12] = 0;
    slot[0x14] = 0;
    slot[0x15] = slot[0x15] & 0xfffff01b;
    FUN_80024800(*(int*)(DAT_80611824 + descIdx * 4), 0, 0, key);
    slot[7] = (int)key[0];
    slot[4] = (int)key[0];
    slot[5] = (int)key[1];
    slot[9] = (int)key[3];
    slot[6] = (int)key[3];
    slot[8] = slot[5];

    // roll offsets forward + clear deltas on both slots
    primSlot[7] = primSlot[4]; primSlot[8] = primSlot[5]; primSlot[9] = primSlot[6];
    secSlot[7]  = secSlot[4];  secSlot[8]  = secSlot[5];  secSlot[9]  = secSlot[6];
    primSlot[10] = 0; primSlot[0xb] = 0; primSlot[0xc] = 0;
    secSlot[10]  = 0; secSlot[0xb]  = 0; secSlot[0xc]  = 0;

    FUN_80382734();
}
