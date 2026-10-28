// =============================================================================
// anim_state.cpp - Animation-state accessors + blend/playback control
// Address range: 0x80022ea4 - 0x80023280
// Batch: 389 | Date: 2026-09-05
//
// Small accessors and control helpers around a model's animation state block,
// continuing the pose pipeline (batches 385-388). anim_get_keyframe_pair is the
// heavily-used FUN_80022ea4 wrapper the whole pipeline calls to bracket the
// current frame's keyframes.
//
//   * anim_get_keyframe_pair       (0x80022ea4) bracket current-frame keyframes (FUN_80022ea4)
//   * anim_get_frame_times         (0x80022ed8) current/prev frame time as fixed-point ints
//   * anim_secondary_clip_finished (0x80022f2c) secondary blend clip past its length?
//   * anim_clear_blend_weight      (0x80022f6c) blend weight +0x100 = 0
//   * anim_set_blend_weight        (0x80022f78) blend weight +0x100 = value
//   * anim_reset_blend_and_clip    (0x80022f80) clear blend weight + reset clip state
//   * anim_set_blend_weight_alt    (0x80022fac) blend weight +0x100 = value (alt entry)
//   * anim_stub_noop               (0x80022fb4) empty stub
//   * anim_compute_root_velocity   (0x80022fb8) root-motion velocity from clip endpoints
//   * anim_play_scaled             (0x80023280) play anim with difficulty-scaled speed
//
// Compile-clean non-matching reconstruction. FUN_80022ea4 is defined extern "C"
// to match the signature the rest of the pipeline already declares.
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // anim_keyframe_binary_search, game_level_supports_scoring

// ---- raw runtime globals ---------------------------------------------------
extern int    DAT_80611824;    // animation descriptor table base (idx*4)
extern short  DAT_804d1cd0;    // anim id -> descriptor index table (short)
extern int    DAT_804d34dc;    // per-anim flag table (word 1, idx*8)
extern uint   DAT_806109c0;    // frame-rate divisor
extern char   DAT_805015bd;    // region/mode selector ('\n' special)
extern char   DAT_805015bf;    // secondary mode gate

// ---- numeric constants -----------------------------------------------------
extern float  FLOAT_80613930;  // 0.0
extern double DOUBLE_80613938; // uint->double magic bias
extern float  FLOAT_8060faf8;  // fixed-point scale (keyframe times)
extern float  FLOAT_80613944;  // 1.0
extern float  FLOAT_80613948;  // 0.5 rounding threshold
extern float  FLOAT_80613990;  // difficulty speed scale

// ---- raw runtime helpers (self-extern at call-site arity) ------------------
extern "C" {
    unsigned long long FUN_803826e8(void);   // scratch: packed (recHi<<32 | animId)
    void   FUN_80382734(void);                // scratch free (e8)
    void   FUN_80024800(int animTbl, int bone, uint key, float* out);  // sample keyframe
    void   FUN_80026d00(double t, int clipState);   // reset clip state
    int    FUN_801bb118(int recHi, int animId);      // begin-play validate/setup
    void   FUN_801bb390(void);                        // begin-play commit
    void   FUN_80026bcc(int recHi);                   // resolve interp weight
    void   FUN_80023514(unsigned long long ctx, double speed);  // start playback
}

// anim_get_keyframe_pair (0x80022ea4)  [FUN_80022ea4]
// Bracket the two keyframes surrounding the model's current animation time
// (+8), on the model's current descriptor channel, writing the lo/hi key indices
// to outLo/outHi. The pipeline's universal keyframe-lookup entry point.
extern "C" void FUN_80022ea4(int animState, unsigned int* outLo, unsigned int* outHi)
{
    anim_keyframe_binary_search((double)*(float*)(animState + 8),
                                *(int*)(DAT_80611824 + *(int*)(animState + 4) * 4),
                                outLo, outHi);
}

// anim_get_frame_times (0x80022ed8)
// Return the current (+8) and previous (+0xc) animation times converted to the
// engine's fixed-point integer units, when the respective out-pointer is given.
void anim_get_frame_times(int animState, int* outCur, int* outPrev)
{
    if (outCur != (int*)0) {
        *outCur = (int)(*(float*)(animState + 8) * FLOAT_8060faf8);
    }
    if (outPrev != (int*)0) {
        *outPrev = (int)(*(float*)(animState + 0xc) * FLOAT_8060faf8);
    }
}

// anim_secondary_clip_finished (0x80022f2c)
// True when a secondary/overlay clip is active (+0x11c != 0) and its play time
// (+0xd4) has reached or passed the clip length (descriptor +0x18 for the anim
// id at +0xd0).
int anim_secondary_clip_finished(int animState)
{
    if (*(int*)(animState + 0x11c) != 0) {
        if (*(float*)(*(int*)(DAT_80611824 + *(int*)(animState + 0xd0) * 4) + 0x18) <=
            *(float*)(animState + 0xd4)) {
            return 1;
        }
    }
    return 0;
}

// anim_clear_blend_weight (0x80022f6c)
void anim_clear_blend_weight(int animState)
{
    *(float*)(animState + 0x100) = FLOAT_80613930;
}

// anim_set_blend_weight (0x80022f78)
void anim_set_blend_weight(double weight, int animState)
{
    *(float*)(animState + 0x100) = (float)weight;
}

// anim_reset_blend_and_clip (0x80022f80)
// Clear the blend weight (+0x100) and reset the associated clip state block
// (+0xcc) to time 0.
void anim_reset_blend_and_clip(int animState)
{
    *(float*)(animState + 0x100) = FLOAT_80613930;
    FUN_80026d00((double)FLOAT_80613930, animState + 0xcc);
}

// anim_set_blend_weight_alt (0x80022fac)
void anim_set_blend_weight_alt(double weight, int animState)
{
    *(float*)(animState + 0x100) = (float)weight;
}

// anim_stub_noop (0x80022fb4)
void anim_stub_noop(void)
{
}

// anim_compute_root_velocity (0x80022fb8)
// Compute a root-motion velocity contribution for a looping clip: samples the
// first and last keyframe of the active clip (primary +0xcc or secondary +0x124
// depending on blend weight +400==0), computes the per-loop root displacement,
// finds the fractional loop position at the sample time, and — if the clip is
// flagged looping (flag word bit 0) — writes the scaled velocity into the clip
// state (+0x4c) and sets the applied bit (0x20 at +0x54). Clamped to +/-maxMag.
void anim_compute_root_velocity(double sampleTime, unsigned long long, double outScale,
                                double maxMag, int rec, unsigned int rateBits)
{
    if ((double)FLOAT_80613930 < outScale) {
        int clip = *(int*)(rec + 4);
        double absMax = ABS(maxMag);
        clip = ((double)FLOAT_80613930 == (double)*(float*)(clip + 400)) ? (clip + 0xcc) : (clip + 0x124);

        float first[7], last[7];
        FUN_80024800(*(int*)(DAT_80611824 + *(int*)(clip + 4) * 4), 0, 0, first);
        int desc = *(int*)(DAT_80611824 + *(int*)(clip + 4) * 4);
        FUN_80024800(desc, 0, (uint)(*(int*)(desc + 0x10) - 1), last);

        double invRate = (double)CONCAT44(0x43300000, rateBits ^ 0x80000000);
        double dispPerLoop = (double)((last[3] - first[3]) / (float)(invRate - DOUBLE_80613938));
        if (((double)FLOAT_80613930 < dispPerLoop) &&
            ((*(uint*)(&DAT_804d34dc + *(int*)(clip + 4) * 8) & 1) != 0)) {
            double loopLen = (double)(*(float*)(*(int*)(DAT_80611824 + *(int*)(clip + 4) * 4) + 0x18) /
                                      (float)(invRate - DOUBLE_80613938));
            double phase;
            for (phase = (double)*(float*)(clip + 8) - sampleTime; phase = (double)(float)phase,
                 phase < (double)FLOAT_80613930; phase = phase + loopLen) {}
            for (; loopLen < phase; phase = (double)(float)(phase - loopLen)) {}
            phase = (double)(FLOAT_80613944 - (float)(phase / loopLen));
            float steps = (float)((double)(float)(outScale - (double)(float)(phase * dispPerLoop)) / dispPerLoop);
            if (steps < FLOAT_80613930) { steps = FLOAT_80613930; }
            uint fl = (int)steps ^ 0x80000000;
            if (FLOAT_80613948 <= steps - (float)((double)CONCAT44(0x43300000, fl) - DOUBLE_80613938)) {
                fl = ((int)steps + 1) ^ 0x80000000;
            }
            phase = (double)(float)(phase + (double)(float)((double)CONCAT44(0x43300000, fl) - DOUBLE_80613938));
            double vel = (double)((*(float*)(clip + 0x34) * -(float)(dispPerLoop * phase - outScale)) /
                                  ((float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80613938) *
                                   (float)(phase * loopLen)));
            double clamped = vel;
            if ((absMax < ABS(vel)) && (clamped = absMax, vel <= (double)FLOAT_80613930)) {
                clamped = -absMax;
            }
            *(float*)(clip + 0x4c) = (float)clamped;
            *(uint*)(clip + 0x54) = *(uint*)(clip + 0x54) | 0x20;
        }
    }
}

// anim_play_scaled (0x80023280)
// Begin playing an animation on the entity from the packed scratch frame, with
// its playback speed scaled by the difficulty factor (FLOAT_80613990) when in a
// scored, non-special mode and the anim isn't already flagged. Validates the id
// range (0x24 < id < 0xbfe) and begin-play setup (FUN_801bb118) before committing.
void anim_play_scaled(unsigned long long ctx, double speed)
{
    unsigned long long packed = FUN_803826e8();
    int recHi = (int)(unsigned int)(packed >> 0x20);
    int animId = (int)(unsigned int)packed;
    int idx = (int)(short)(&DAT_804d1cd0)[animId];

    if (((DAT_805015bd != '\n') && (DAT_805015bf == '\0') &&
         (game_level_supports_scoring() != 0)) &&
        ((*(int*)(DAT_80611824 + idx * 4) == 0) ||
         ((*(uint*)(&DAT_804d34dc + (&DAT_804d1cd0)[animId] * 8) & 0x20) == 0))) {
        speed = (double)(float)(speed * (double)FLOAT_80613990);
    }
    if (((*(int*)(DAT_80611824 + idx * 4) != 0) && (0x24 < animId)) &&
        ((animId < 0xbfe) && ((-1 < idx) && (FUN_801bb118(recHi, animId) != 0)))) {
        FUN_801bb390();
        FUN_80026bcc(recHi);
        FUN_80023514(ctx, speed);
    }
    FUN_80382734();
}
