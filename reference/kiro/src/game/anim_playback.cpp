// =============================================================================
// anim_playback.cpp - Animation clip playback: start / rate / root-motion
// Address range: 0x800233f8 - 0x8002413c
// Batch: 390 | Date: 2026-09-05
//
// Continuation of the animation pipeline (batches 385-389). This module drives
// per-clip playback: sampling a channel curve at a time, starting a clip into
// one of the two blend slots (primary +0xcc / secondary +0x124), applying the
// resulting root-motion / floor deltas, and the playback-rate state machine.
//
//   * anim_sample_curve_value      (0x800233f8) sample channel curve -> radians (FUN_800233f8)
//   * anim_start_clip              (0x80023514) start playback of a clip into a blend slot (FUN_80023514)
//   * anim_apply_root_motion       (0x80023b40) post-play root-motion / floor adjust deltas (FUN_80023b40)
//   * anim_get_clip_end_time       (0x80023e08) end-time accessor (returns 0.0) (FUN_80023e08)
//   * anim_set_playback_rate       (0x80023e10) gate + dispatch a new playback rate (FUN_80023e10)
//   * anim_update_rate_state       (0x80023f68) rate/direction ramp state machine (FUN_80023f68)
//   * anim_set_pair_9498           (0x80024130) store two words at +0x94/+0x98 (FUN_80024130)
//   * anim_set_render_scale        (0x8002413c) set squash-scale flag + fill render scales (FUN_8002413c)
//
// Compile-clean non-matching reconstruction. The FUN_* entry points that other
// modules already reference (FUN_80023514, FUN_800233f8, FUN_80023b40,
// FUN_80023e08, FUN_80023e10, FUN_80023f68, FUN_8002413c) are defined extern "C"
// with the signatures those call sites expect.
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // anim_keyframe_binary_search, anim_get_keyframe_pair,
                               // player_slot_get_pool_base, player_ai_* , leaf_31e8

// ---- raw runtime globals ---------------------------------------------------
extern int    DAT_80611824;    // animation descriptor table base (idx*4)
extern short  DAT_804d1cd0;    // anim id -> descriptor index table (short)
extern uint   DAT_804d34d8;    // per-anim flag table (word 0, idx*8)
extern uint   DAT_804d34dc;    // per-anim flag table (word 1, idx*8)
extern uint   DAT_805015c0;    // global mode flags (0x800 = scoring-related gate)
extern uint   DAT_805015c4;    // global mode flags (0x2000000 = disable gate)
extern int    DAT_80611d78;    // render-instance table base (idx * 0x141c)
extern int    DAT_80612610;    // render-instance count

// ---- numeric constants -----------------------------------------------------
extern float  FLOAT_80613930;  // 0.0
extern float  FLOAT_80613940;  // 1.0 (period / normalizer)
extern float  FLOAT_80613944;  // 1.0
extern float  FLOAT_8060faf8;  // fixed-point scale (keyframe times)
extern double DOUBLE_80613938; // uint->double magic bias
extern float  FLOAT_8061394c;  // curve output normalizer
extern float  FLOAT_80613950;  // curve output scale
extern float  FLOAT_80613994;  // curve default (0.0)
extern float  FLOAT_80613998;  // floor epsilon
extern float  FLOAT_8061399c;  // rate deadzone threshold
extern float  FLOAT_806139a0;  // rate change hysteresis
extern float  FLOAT_806139a4;  // render scale X factor
extern float  FLOAT_806139a8;  // render scale Z factor
extern float  FLOAT_806139ac;  // render scale W floor
extern float  FLOAT_8061396c;  // ramp default value
extern float  FLOAT_806118ec;  // score-mode speed divisor

// ---- raw runtime helpers (self-extern at call-site arity) ------------------
extern "C" {
    // scratch record getters (packed hi:lo in a register pair for the first)
    unsigned long long FUN_803826dc(void);   // -> (ctxPtr<<32 | recPtr)
    int    FUN_803826f0(void);                // -> scratch record (root-motion ctx)
    void   FUN_80382728(void);                // scratch free (dc)
    void   FUN_8038273c(void);                // scratch free (f0)
    int*   FUN_80025868(int recPtr);          // resolve secondary clip record
    void   FUN_80024800(int animTbl, int bone, unsigned int key, float* out); // sample keyframe
    void   FUN_80026d00(double t, int clipState);  // reset/prime clip state at time t
    double FUN_80390324(double x);            // periodic reduce (frac/poly)
    void   FUN_801bb84c(int* rec);            // notify audio of rate change

    // forward declarations for the entry points defined later in this file
    double FUN_800233f8(double t, int rec);
    void   FUN_80023514(unsigned long long ctx, double speed);
    void   FUN_80023b40(int recPtr, int st, unsigned int flags);
    double FUN_80023e08(void);
    void   FUN_80023f68(double rate, int* rec, int dir);
    void   FUN_80022ea4(int animState, unsigned int* outLo, unsigned int* outHi);
}

// floor_query_with_interact is declared in generated_decls with a (float*,...)
// prototype, but this call site uses the double-returning register ABI. Invoke
// through a typed function-pointer cast to avoid a redeclaration conflict.
typedef double (*floor_query_dd_fn)(double, double, int, int, int*, int);
#define FLOOR_QUERY_DD ((floor_query_dd_fn)&floor_query_with_interact)

// ---------------------------------------------------------------------------
// anim_sample_curve_value (0x800233f8)  [FUN_800233f8]
// Sample the scalar curve channel of a clip record at time `t`. Binary-searches
// the keyframe times, linearly interpolates the value between the bracketing
// keys, scales it, then wraps it through the periodic reduce so the result is a
// normalized angle-like value. Returns the curve default (0.0) when the record
// has no curve channel (+0x2c == 0).
// ---------------------------------------------------------------------------
extern "C" double FUN_800233f8(double t, int rec)
{
    double result = (double)FLOAT_80613994;
    if (*(int*)(rec + 0x2c) == 0)
        return result;

    unsigned int lo[1];
    unsigned int hi;
    anim_keyframe_binary_search(t, rec, lo, &hi);

    float frac = FLOAT_80613930;
    if (lo[0] != hi) {
        // convert the two keyframe times (stored as biased uints) to seconds
        unsigned int loTicks = *(unsigned int*)(*(int*)(rec + 0x1c) + lo[0] * 4) ^ 0x80000000u;
        unsigned int hiTicks = *(unsigned int*)(*(int*)(rec + 0x1c) + hi   * 4) ^ 0x80000000u;
        double loTime = (double)((float)((double)CONCAT44(0x43300000, loTicks) - DOUBLE_80613938)
                                 / FLOAT_8060faf8);
        double hiTime = (double)((float)((double)CONCAT44(0x43300000, hiTicks) - DOUBLE_80613938)
                                 / FLOAT_8060faf8);
        frac = (float)(t - loTime) / (float)(hiTime - loTime);
    }

    float loVal = *(float*)(*(int*)(rec + 0x2c) + lo[0] * 4);
    float hiVal = *(float*)(*(int*)(rec + 0x2c) + hi   * 4);
    double scaled = (double)((FLOAT_80613950 * (frac * (hiVal - loVal) + loVal)) / FLOAT_8061394c);
    double reduced = FUN_80390324((double)((float)(scaled - (double)FLOAT_80613930) / FLOAT_80613940));
    result = -(double)(float)((double)FLOAT_80613940 * (double)(float)reduced - scaled);
    return result;
}

// ---------------------------------------------------------------------------
// anim_start_clip (0x80023514)  [FUN_80023514]
// Start playback of an animation clip. `speed` is the requested playback speed;
// the incoming context/record pair is fetched from the scratch getter (the
// declared first parameter is part of the original ABI but the body sources the
// real context from FUN_803826dc). Chooses the primary (+0xcc) or secondary
// (+0x124) blend slot depending on whether a clip is already active, primes the
// slot's key/time fields, samples the initial pose, then applies root-motion.
// ---------------------------------------------------------------------------
extern "C" void FUN_80023514(unsigned long long ctx, double speed)
{
    (void)ctx;

    float local80, local7c;
    unsigned int local78;
    unsigned int keyLo, keyHi;

    unsigned long long packed = FUN_803826dc();
    int recPtr = (int)(packed >> 0x20);   // animation-state record
    int animId = (int)packed;             // requested anim id

    unsigned int flags0 = *(unsigned int*)(&DAT_804d34d8 + (short)(&DAT_804d1cd0)[animId] * 8);

    int* secRec = FUN_80025868(recPtr);
    int secClip = *(int*)(DAT_80611824 + secRec[1] * 4);
    unsigned int secFlags = *(unsigned int*)(&DAT_804d34d8 + (short)(&DAT_804d1cd0)[*secRec] * 8);
    (void)secFlags;
    int descIdx = (int)(short)(&DAT_804d1cd0)[animId];

    // let a still-loading secondary clip finish sampling first
    if (secClip != 0)
        FUN_800233f8((double)(float)secRec[2], secClip);

    // score-mode speed remap
    if (((DAT_805015c0 & 0x800) != 0) && ((DAT_805015c4 & 0x2000000) == 0)) {
        int pool = (int)player_slot_get_pool_base();
        if (pool != 0) {
            pool = (int)player_slot_get_pool_base();
            if (*(int*)(pool + 0x218) != 0x1b && *(int*)(recPtr + 0x20) != 0x2000000 &&
                *(int*)(recPtr + 0x20) != 0x10) {
                // (rate-history side effects folded into the two slot fields below)
                *(float*)(*(int*)(recPtr + 4) + 0x1a4) = (float)speed;
                speed = (double)(float)(speed * (double)FLOAT_806118ec);
            }
        }
    }

    int st = *(int*)(recPtr + 4);
    bool primarySlotFree = (*(int*)(st + 0x124) == 0) || (*(int*)(st + 0x124) == animId);
    (void)primarySlotFree;

    // Choose slot. When no secondary clip is active we fill the primary slot
    // (+0xcc block), otherwise the secondary slot (+0x124 block).
    if (*(int*)(st + 0x124) == 0) {
        *(float*)(st + 0x190) = (float)speed;       // +400
        *(float*)(st + 0x198) = FLOAT_80613930;
        *(int*)(st + 0xcc)    = animId;
        *(int*)(st + 0xd0)    = descIdx;
        *(float*)(st + 0x100) = (float)speed;
        FUN_80026d00((double)FLOAT_80613930, st + 0xcc);
        *(float*)(st + 0x104) = FLOAT_80613930;
        *(float*)(st + 0x108) = FLOAT_80613930;
        *(float*)(st + 0x10c) = FLOAT_80613930;
        *(float*)(st + 0x110) = FLOAT_80613930;
        *(float*)(st + 0x114) = FLOAT_80613930;
        *(unsigned int*)(st + 0x11c) = 0;
        *(unsigned int*)(st + 0x120) &= 0xfffff01bu;
        FUN_80024800(*(int*)(DAT_80611824 + descIdx * 4), 0, 0, &local80);
        if ((*(unsigned int*)(&DAT_804d34dc + descIdx * 8) & 0x20000) != 0) {
            *(float*)(st + 0xf8) = local7c - *(float*)(st + 0xec);
            *(float*)(st + 0xe0) = local7c;
        }
        *(float*)(st + 0xe8) = local80;
        *(float*)(st + 0xdc) = local80;
        *(float*)(st + 0xe0) = local7c;
        *(unsigned int*)(st + 0xf0) = local78;
        *(unsigned int*)(st + 0xe4) = local78;
        *(unsigned int*)(st + 0xec) = *(unsigned int*)(st + 0xe0);
        *(unsigned int*)(st + 0x124) = 0;
    } else {
        if (*(int*)(st + 0x124) != 0 && *(int*)(st + 0x124) != animId) {
            if (((*(unsigned int*)(&DAT_804d34dc + descIdx * 8) & 1) != 0) &&
                ((*(unsigned int*)(&DAT_804d34dc + descIdx * 8) & 0x200) != 0)) {
                FUN_80382728();
                return;
            }
            leaf_31e8(st + 0xcc, st + 0x124, 0x58);
        }
        *(float*)(st + 0x190) = (float)speed;
        *(float*)(st + 0x198) = FLOAT_80613930;
        *(int*)(st + 0x124) = animId;
        *(int*)(st + 0x128) = descIdx;
        *(float*)(st + 0x158) = (float)speed;
        FUN_80026d00((double)FLOAT_80613930, st + 0x124);
        *(float*)(st + 0x15c) = FLOAT_80613930;
        *(float*)(st + 0x160) = FLOAT_80613930;
        *(float*)(st + 0x164) = FLOAT_80613930;
        *(float*)(st + 0x168) = FLOAT_80613930;
        *(float*)(st + 0x16c) = FLOAT_80613930;
        *(unsigned int*)(st + 0x174) = 0;
        *(unsigned int*)(st + 0x178) &= 0xfffff01bu;
        FUN_80024800(*(int*)(DAT_80611824 + descIdx * 4), 0, 0, &local80);
        if ((*(unsigned int*)(&DAT_804d34dc + descIdx * 8) & 0x20000) != 0) {
            *(float*)(st + 0xf8) = local7c - *(float*)(st + 0xec);
            *(float*)(st + 0xe0) = local7c;
        }
        *(float*)(st + 0x140) = local80;
        *(float*)(st + 0x134) = local80;
        *(float*)(st + 0x138) = local7c;
        *(unsigned int*)(st + 0x148) = local78;
        *(unsigned int*)(st + 0x13c) = local78;

        double endTime = FUN_80023e08();
        if ((double)FLOAT_80613930 < endTime) {
            int animTbl = *(int*)(DAT_80611824 + descIdx * 4);
            FUN_80026d00(endTime, st + 0x124);
            FUN_80022ea4(st + 0x124, &keyLo, &keyHi);
            FUN_80024800(animTbl, 0, keyLo, &local80);
            *(float*)(st + 0x140) = local80;
            *(float*)(st + 0x134) = local80;
            *(float*)(st + 0x138) = local7c;
            *(unsigned int*)(st + 0x148) = local78;
            *(unsigned int*)(st + 0x13c) = local78;
        }
    }

    // finalize root-motion base fields and hand off to the delta applier
    *(float*)(recPtr + 0x68) -= *(float*)(st + 0xf8);
    *(unsigned int*)(st + 0xe8) = *(unsigned int*)(st + 0xdc);
    *(unsigned int*)(st + 0xec) = *(unsigned int*)(st + 0xe0);
    *(unsigned int*)(st + 0xf0) = *(unsigned int*)(st + 0xe4);
    *(unsigned int*)(st + 0x140) = *(unsigned int*)(st + 0x134);
    *(unsigned int*)(st + 0x144) = *(unsigned int*)(st + 0x138);
    *(unsigned int*)(st + 0x148) = *(unsigned int*)(st + 0x13c);
    *(float*)(st + 0xf4)  = FLOAT_80613930;
    *(float*)(st + 0xf8)  = FLOAT_80613930;
    *(float*)(st + 0xfc)  = FLOAT_80613930;
    *(float*)(st + 0x14c) = FLOAT_80613930;
    *(float*)(st + 0x150) = FLOAT_80613930;
    *(float*)(st + 0x154) = FLOAT_80613930;

    FUN_80023b40(recPtr, st, flags0);

    if (*(int*)(recPtr + 0x20) == 8) {
        if ((flags0 & 0x1000000) == 0)
            player_ai_stop_ambient_sound(recPtr);
        else
            player_ai_ambient_sound_start();
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// anim_apply_root_motion (0x80023b40)  [FUN_80023b40]
// After a clip is (re)started, resolve the per-frame root-motion delta for the
// active slot. Depending on the clip's flags it either copies the sampled root
// offsets straight into the delta block, or performs a floor query to keep the
// root planted on the ground, setting the appropriate "has delta" flag bits.
// ---------------------------------------------------------------------------
extern "C" void FUN_80023b40(int recPtr, int st, unsigned int flags)
{
    int ctx = FUN_803826f0();
    float one = FLOAT_80613940;
    int state = *(int*)(ctx + 4);
    (void)st;

    bool useSecondary = FLOAT_80613930 < *(float*)(state + 0x190);   // +400
    int slotAnim = useSecondary ? *(int*)(state + 0x124) : *(int*)(state + 0xcc);
    unsigned int slotFlags = *(unsigned int*)(&DAT_804d34d8 + (short)(&DAT_804d1cd0)[slotAnim] * 8);

    if (((flags & 0x20000) == 0) || ((slotFlags & 0x20000) != 0)) {
        if (((flags & 0x20000) == 0) && ((slotFlags & 0x20000) != 0)) {
            if (useSecondary) {
                *(float*)(state + 0x108) = *(float*)(ctx + 100) - *(float*)(state + 0x134);
                *(float*)(state + 0x10c) = *(float*)(ctx + 0x68) - *(float*)(state + 0x138);
                *(float*)(state + 0x110) = *(float*)(ctx + 0x6c) - *(float*)(state + 0x13c);
                *(unsigned int*)(state + 0x120) |= 0x100;
                *(float*)(state + 0x114) = one - *(float*)(ctx + 0x70);
                *(unsigned int*)(state + 0x120) |= 0x200;
            }
        } else if (((flags & 0x80) != 0)) {
            // clip carries an end-of-anim event window
            double window = (double)FLOAT_80613930;
            if ((double)FLOAT_80613930 <= window) {
                if (useSecondary) {
                    *(float*)(state + 0x114) = (float)((double)FLOAT_80613940 - window);
                    *(unsigned int*)(state + 0x120) |= 0x200;
                    *(float*)(state + 0x16c) = (float)window;
                    *(unsigned int*)(state + 0x178) |= 0x800;
                } else {
                    *(float*)(state + 0x114) = (float)window;
                    *(unsigned int*)(state + 0x120) |= 0x800;
                }
            }
        }
    } else {
        double window = (double)FLOAT_80613930;
        double floorY = FLOOR_QUERY_DD((double)FLOAT_80613944, (double)FLOAT_80613930,
                                       ctx + 100, 0, (int*)0, 0);
        float zero = FLOAT_80613930;
        if ((double)FLOAT_80613998 < floorY) {
            if (useSecondary) {
                *(float*)(state + 0x160) = FLOAT_80613930;
                *(float*)(state + 0x164) = (float)(floorY - (double)*(float*)(ctx + 0x68));
                *(float*)(state + 0x168) = zero;
                *(unsigned int*)(state + 0x178) |= 0x400;
                *(float*)(state + 0x108) = zero;
                *(float*)(state + 0x10c) = (float)((double)*(float*)(ctx + 0x68) - floorY);
                *(float*)(state + 0x110) = zero;
                *(unsigned int*)(state + 0x120) |= 0x100;
            } else {
                *(float*)(state + 0x108) = FLOAT_80613930;
                *(float*)(state + 0x10c) = (float)(floorY - (double)*(float*)(ctx + 0x68));
                *(float*)(state + 0x110) = zero;
                *(unsigned int*)(state + 0x120) |= 0x400;
            }
        }
        if ((double)FLOAT_80613930 <= window) {
            if (useSecondary) {
                *(float*)(state + 0x114) = (float)((double)FLOAT_80613940 - window);
                *(unsigned int*)(state + 0x120) |= 0x200;
                *(float*)(state + 0x16c) = (float)window;
                *(unsigned int*)(state + 0x178) |= 0x800;
            } else {
                *(float*)(state + 0x114) = (float)window;
                *(unsigned int*)(state + 0x120) |= 0x800;
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// anim_get_clip_end_time (0x80023e08)  [FUN_80023e08]
// Accessor stub: returns the clip end-time sentinel (0.0f on this build).
// ---------------------------------------------------------------------------
extern "C" double FUN_80023e08(void)
{
    return (double)FLOAT_80613930;
}

// ---------------------------------------------------------------------------
// anim_set_playback_rate (0x80023e10)  [FUN_80023e10]
// Public entry to change a clip's playback rate. Applies the score-mode speed
// remap, then either forwards to the rate state machine (for normal anim ids)
// or latches a pending forward/reverse rate directly into the rate block
// (+0x30..+0x32) for the special "scripted" ids.
// ---------------------------------------------------------------------------
extern "C" void FUN_80023e10(double rate, int* rec, int dir)
{
    if (((DAT_805015c0 & 0x800) != 0) && ((DAT_805015c4 & 0x2000000) == 0)) {
        int pool = (int)player_slot_get_pool_base();
        if (pool != 0) {
            pool = (int)player_slot_get_pool_base();
            if (*(int*)(pool + 0x218) != 0x1b && (double)FLOAT_80613930 < rate) {
                rec[0x6b] = (int)(float)rate;
                rate = (double)(float)(rate *
                        (double)(float)(rate / (double)(float)(rate * (double)FLOAT_806118ec)));
            }
        }
    }

    unsigned int animKind = *(unsigned int*)(rec[0x20] + 4);
    bool scripted = true;
    bool inRange = true;
    if ((1 < animKind) && (animKind != 0x15))
        inRange = false;
    if (!inRange && (animKind != 0x7c))
        scripted = false;

    if (scripted) {
        FUN_80023f68(rate, rec, dir);
    } else if ((dir == 2) && ((rec[0x32] & 6U) == 0)) {
        rec[0x32] = 4;
        rec[0x30] = (int)(float)rate;
    } else if ((dir == 1) && ((rec[0x32] & 9U) == 0)) {
        rec[0x32] = 8;
        rec[0x30] = (int)(float)rate;
    }
}

// ---------------------------------------------------------------------------
// anim_update_rate_state (0x80023f68)  [FUN_80023f68]
// Rate/direction ramp state machine. `dir` = 1 (reverse) or 2 (forward). When
// the requested rate exceeds the deadzone it latches the new rate and blends the
// previous ramp value forward; below the deadzone it snaps to the idle state.
// Direction bits live in rec[0x32]: 4 = forward-active, 8 = reverse-active.
// ---------------------------------------------------------------------------
extern "C" void FUN_80023f68(double rate, int* rec, int dir)
{
    float span = FLOAT_80613944;
    float zero = FLOAT_80613930;

    if ((double)FLOAT_8061399c <= ABS(rate)) {
        if (dir == 2) {
            unsigned int bits = (unsigned int)rec[0x32];
            if ((bits & 6) == 0) {
                if ((bits & 8) == 0) {
                    rec[0x32] = 4;
                    rec[0x30] = (int)(float)rate;
                    rec[0x31] = (int)zero;
                    if (*(int*)(*rec + 0x20) == 8) {
                        int aud = *(int*)(*rec + 0x7c);
                        *(float*)(aud + 0xb94) = FLOAT_8061396c;
                        *(int*)(aud + 0xb9c) = 1;
                    }
                    rec[0x2f] = (int)FLOAT_8061396c;
                    rec[0x2e] = (int)FLOAT_8061396c;
                } else {
                    float prev = (float)rec[0x30];
                    rec[0x32] = 4;
                    rec[0x30] = (int)(float)rate;
                    rec[0x31] = (int)(float)(rate * (double)(span - (float)rec[0x31] / prev));
                }
            } else if (((bits & 4) != 0) &&
                       (FLOAT_806139a0 <= ABS((float)(rate - (double)(float)rec[0x30])))) {
                float prev = (float)rec[0x30];
                rec[0x30] = (int)(float)rate;
                rec[0x31] = (int)(float)(rate * (double)(float)((double)(float)rec[0x31] / (double)prev));
            }
            if (*(int*)(*rec + 0x20) == 8)
                FUN_801bb84c(rec);
        } else if (dir == 1) {
            unsigned int bits = (unsigned int)rec[0x32];
            if ((bits & 9) == 0) {
                if ((bits & 4) == 0) {
                    rec[0x32] = 8;
                    rec[0x30] = (int)(float)rate;
                    rec[0x31] = (int)zero;
                } else {
                    float prev = (float)rec[0x30];
                    rec[0x32] = 8;
                    rec[0x30] = (int)(float)rate;
                    rec[0x31] = (int)(float)(rate * (double)(span - (float)rec[0x31] / prev));
                }
            } else if (((bits & 8) != 0) &&
                       (FLOAT_806139a0 <= ABS((float)(rate - (double)(float)rec[0x30])))) {
                float prev = (float)rec[0x30];
                rec[0x30] = (int)(float)rate;
                rec[0x31] = (int)(float)(rate * (double)(float)((double)(float)rec[0x31] / (double)prev));
            }
        }
    } else {
        rec[0x32] = (dir == 2) ? 2 : 1;
        rec[0x31] = (int)FLOAT_80613930;
    }
}

// ---------------------------------------------------------------------------
// anim_set_pair_9498 (0x80024130)  [FUN_80024130]
// Store a pair of words into the record at +0x94 / +0x98 (a 2-component target,
// e.g. a look-at / aim reference).
// ---------------------------------------------------------------------------
extern "C" void FUN_80024130(int rec, unsigned int a, unsigned int b)
{
    *(unsigned int*)(rec + 0x94) = a;
    *(unsigned int*)(rec + 0x98) = b;
}

// ---------------------------------------------------------------------------
// anim_set_render_scale (0x8002413c)  [FUN_8002413c]
// Set the squash/stretch amount for a record: flags |= 0x20, stores the amount
// at +0xb08, and (when the record has a live render-instance slot) fills the
// derived X/Y/Z scale factors into the render-instance table, keeping the W
// component clamped to a floor.
// ---------------------------------------------------------------------------
extern "C" void FUN_8002413c(double amount, int rec)
{
    *(unsigned int*)(rec + 0x28) |= 0x20;
    *(float*)(rec + 0xb08) = (float)amount;

    float wFloor = FLOAT_806139ac;
    float zFactor = FLOAT_806139a8;
    if (DAT_80612610 <= *(int*)(rec + 8))
        return;

    int inst = DAT_80611d78 + *(int*)(rec + 8) * 0x141c;
    *(float*)(inst + 0x80) = FLOAT_806139a4 * *(float*)(rec + 0xb08);
    *(unsigned int*)(inst + 0x84) = *(unsigned int*)(rec + 0xb08);
    *(float*)(inst + 0x8c) = zFactor * *(float*)(rec + 0xb08);
    if (wFloor <= *(float*)(inst + 0x90))
        return;
    *(float*)(inst + 0x90) = wFloor;
}
