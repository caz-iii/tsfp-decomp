// Module: prop_target_scan
// TSFP GameCube main.dol non-matching decompilation.
//
// Turret-prop target acquisition. These run from the prop AI tick (state 2/3)
// to find and validate a target: prop_can_target tests one candidate against
// the turret's range / pitch / yaw arc (and optionally a line-of-sight raycast);
// prop_acquire_target sweeps the player + AI-actor lists (with a proximity
// raycast for detector props); prop_scan_projectile_targets sweeps the live
// projectile/pickup list.
//
// Batch: batch_713  |  Addresses: 0x80240fd4 - 0x8024181b (subset)
// NON-MATCHING decompilation.

#include "common.h"

// Ghidra float-sqrt intrinsic (common.h owns no math intrinsics).
static inline float tsfp_sqrtf(float x) {
    float r = x;
    if (x > 0.0f) {
        r = x;
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
    }
    return r;
}
#ifndef SQRT
#define SQRT(x) tsfp_sqrtf((float)(x))
#endif

// ---------------------------------------------------------------------------
// Shared constants / lists (file-local externs to raw symbols).
// ---------------------------------------------------------------------------
extern float FLOAT_80618908;   // deg->rad numerator (PI)
extern float FLOAT_8061890c;   // deg->rad denominator (180)
extern float FLOAT_80618910;   // 0.0f
extern float FLOAT_80618944;   // angle-wrap low bound
extern float FLOAT_80618948;   // 360 (angle wrap span)

extern int  DAT_80610ba0;      // slot-record table base (0x29c stride)
extern int  DAT_80611928;      // frame phase counter (& 3 select)
extern int  DAT_80611d78;      // player/owner table base (0x141c stride)
extern int  DAT_80612610;      // player count
extern int  DAT_80612098;      // projectile/pickup list count
extern unsigned char DAT_80588610[];  // projectile/pickup pointer list
extern char DAT_805015bd;      // mode discriminator ('\n' arcade)

// ---------------------------------------------------------------------------
// Cross-module helpers (file-local; common.h owns no raw symbols).
// ---------------------------------------------------------------------------
extern int  prop_def_by_index(int idx);                 // prop_table_loader.cpp
extern int  prop_is_destroyed(int obj);                 // prop_object_util.cpp
extern void prop_read_aim_anchor(int obj, float *out);  // prop_object_behavior.cpp
extern void prop_compute_muzzle_origin(int obj, float *out); // prop_object_behavior.cpp

extern double math_sqrt(double x);
extern double math_atan2(double y, double x);
extern double FUN_80390324(double x);                   // roundf
extern int  scene_raycast(int world, void *from, void *to, int a4, float *a5, float *a6,
                          int *a7, unsigned int mask, int a9, int *a10);
extern void sound_init_play_params(int outBuf);
extern void sound_play_request(int *req);

extern unsigned long long FUN_803826d8(void);           // acquire-target work context
extern unsigned long long FUN_803826e4(void);           // projectile-scan work context (int)
extern unsigned long long FUN_803826e8(void);           // can-target work context
extern void FUN_80382724(void);                          // release acquire-target context
extern void FUN_80382730(void);                          // release projectile-scan context
extern void FUN_80382734(void);                          // release can-target context

// 0x80241370 | prop_can_target
// Test whether target entity 'target' is a valid turret target for the prop:
// within max range (rec+100) and min range (rec+0xc4), inside the pitch arc
// (rec+200) and yaw arc (rec+0xcc), and - when doRaycast is set - with a clear
// line of sight. Returns 1 when targetable, 0 otherwise.
int prop_can_target(int self_unused, int target, int doRaycast, unsigned int rayMask)
{
    (void)self_unused;
    (void)target;
    unsigned long long ctx = FUN_803826e8();
    int self = (int)(ctx >> 0x20);
    int tgt = (int)ctx;
    int rec = *(int *)(self + 0x7c);
    int result = 0;

    if (prop_is_destroyed(self) == 0) {
        float from[3];   // local_80/7c/78
        float d[3];      // local_74/70/6c
        if ((*(unsigned int *)(rec + 8) & 0x40) == 0) {
            prop_read_aim_anchor(self, from);
        } else {
            prop_compute_muzzle_origin(self, from);
        }

        bool skip = false;
        if (*(int *)(tgt + 0x20) == 8) {
            int trec = *(int *)(tgt + 0x7c);
            d[0] = *(float *)(trec + 0xbb4) - from[0];
            d[1] = *(float *)(trec + 0xbb8) - from[1];
            d[2] = *(float *)(trec + 0xbbc) - from[2];
            if ((*(unsigned int *)(trec + 0xc6c) & 0x10) != 0) {
                skip = true;
            }
        } else {
            d[0] = (*(float *)(tgt + 100) + *(float *)(tgt + 0x1a8)) - from[0];
            d[1] = (*(float *)(tgt + 0x68) + *(float *)(tgt + 0x1ac)) - from[1];
            d[2] = (*(float *)(tgt + 0x6c) + *(float *)(tgt + 0x1b0)) - from[2];
        }

        if (!skip) {
            float maxR = *(float *)(rec + 100);
            float flat2 = d[0] * d[0] + d[2] * d[2];
            if (((FLOAT_80618910 == maxR) || (flat2 <= maxR * maxR)) &&
                (*(float *)(rec + 0xc4) * *(float *)(rec + 0xc4) <= flat2)) {
                // Desired vs current yaw (wrapped) and pitch.
                double curYaw = (double)(*(float *)(rec + 0x1c) + *(float *)(self + 0xc0));
                double ry = FUN_80390324((double)((float)(curYaw - (double)FLOAT_80618944) / FLOAT_80618948));
                double curPitch = (double)(*(float *)(rec + 0x24) + *(float *)(self + 0x70));
                double curYawW = -(double)(float)((double)FLOAT_80618948 * (double)(float)ry - curYaw);
                double rp = FUN_80390324((double)(float)((double)(float)(curPitch - (double)FLOAT_80618944) /
                                                         (double)FLOAT_80618948));
                double curPitchW = (double)(float)rp;

                double flat = math_sqrt((double)(d[0] * d[0] + d[2] * d[2]));
                double pitchA = math_atan2((double)d[1], flat);
                double desiredPitch = -(double)((FLOAT_8061890c * (float)pitchA) / FLOAT_80618908);
                double rpp = FUN_80390324((double)((float)(desiredPitch - (double)FLOAT_80618944) / FLOAT_80618948));
                double desiredPitchW = -(double)(float)((double)FLOAT_80618948 * (double)(float)rpp - desiredPitch);
                double yawA = math_atan2((double)d[0], (double)d[2]);
                double desiredYaw = (double)((FLOAT_8061890c * (float)yawA) / FLOAT_80618908);
                double ry2 = FUN_80390324((double)((float)(desiredYaw - (double)FLOAT_80618944) / FLOAT_80618948));

                float yawErr = -(float)((double)FLOAT_80618948 * curPitchW - curPitch) -
                               -(float)((double)FLOAT_80618948 * (double)(float)ry2 - desiredYaw);
                for (; (double)yawErr <= (double)FLOAT_80618944; yawErr = (float)((double)yawErr + (double)FLOAT_80618948)) { }
                double yawErrD = (double)yawErr;
                for (; (double)FLOAT_8061890c < yawErrD; yawErrD = (double)(float)(yawErrD - (double)FLOAT_80618948)) { }

                if (ABS(yawErrD) < (double)*(float *)(rec + 200)) {
                    float pitchErr = (float)(curYawW - desiredPitchW);
                    for (; pitchErr <= FLOAT_80618944; pitchErr = pitchErr + FLOAT_80618948) { }
                    for (; FLOAT_8061890c < pitchErr; pitchErr = pitchErr - FLOAT_80618948) { }
                    if (ABS(pitchErr) < *(float *)(rec + 0xcc)) {
                        if (doRaycast != 0) {
                            unsigned int flags = *(unsigned int *)(self + 0x28);
                            *(unsigned int *)(self + 0x28) = flags & 0xffffff9f;
                            int hit = scene_raycast(*(int *)(rec + 0xb0), from, d, 0, 0, 0, 0,
                                                    rayMask | 0x2111070, 1, 0);
                            *(unsigned int *)(self + 0x28) = flags;
                            result = (int)(countLeadingZeros((unsigned int)(tgt - hit)) >> 5);
                        } else {
                            result = 1;
                        }
                    }
                }
            }
        }
    }
    FUN_80382734();
    return result;
}

// 0x80240fd4 | prop_acquire_target
// Sweep the player list (and, for detector props, the AI-actor list) for a valid
// target and, on the first hit, play the activation SFX (0xbf8), advance the prop
// state (10 detector -> 0xb charge; else -> 3 aim), reset the charge timer, and
// store the target. Returns the acquired target entity (0 if none).
int prop_acquire_target(void)
{
    int self = (int)(((unsigned long long)FUN_803826d8()) >> 0x20);
    int rec = *(int *)(self + 0x7c);
    int def = prop_def_by_index(*(int *)(rec + 0xc));
    int found = 0;

    if ((*(unsigned int *)(rec + 8) & 0x800000) == 0) {
        if (*(void **)(rec + 0x1b4) == 0) {
            if ((*(unsigned int *)(self + 0x88) & 3) == (DAT_80611928 & 3)) {
                int off = 0;
                for (int i = 0; (i < DAT_80612610) && (found == 0); i = i + 1) {
                    int slot = DAT_80611d78 + off;
                    int tgt = *(int *)(slot + 0x14);
                    int trec = *(int *)(tgt + 0x7c);
                    if (((*(unsigned int *)(trec + 0xc6c) & 0x10) == 0) &&
                        ((*(unsigned int *)(trec + 0x24) & 8) == 0) &&
                        ((*(int *)(rec + 0x18) == 0) || (*(int *)(rec + 0x18) != *(int *)(trec + 0x84)))) {
                        if (*(int *)(rec + 0x10) == 10) {
                            float d[3];   // local_70/6c/68
                            d[0] = (*(float *)(self + 100) + *(float *)(self + 0x1a8)) - *(float *)(slot + 0xb4);
                            d[1] = (*(float *)(self + 0x68) + *(float *)(self + 0x1ac)) - *(float *)(slot + 0xb8);
                            d[2] = (*(float *)(self + 0x6c) + *(float *)(self + 0x1b0)) - *(float *)(slot + 0xbc);
                            float maxR = *(float *)(rec + 100);
                            if ((FLOAT_80618910 == maxR) ||
                                (d[2] * d[2] + d[0] * d[0] + d[1] * d[1] < maxR * maxR)) {
                                unsigned int tf = *(unsigned int *)(tgt + 0x28);
                                unsigned int sf = *(unsigned int *)(self + 0x28);
                                *(unsigned int *)(tgt + 0x28) = tf & 0xffffff9f;
                                *(unsigned int *)(self + 0x28) = *(unsigned int *)(self + 0x28) & 0xffffff9f;
                                int hit = scene_raycast(*(int *)(slot + 0xdec), (void *)(slot + 0xb4), d, 0,
                                                        0, 0, 0, 0x2111070, 1, 0);
                                if (hit == 0) {
                                    found = tgt;
                                }
                                *(unsigned int *)(self + 0x28) = sf;
                                *(unsigned int *)(tgt + 0x28) = tf;
                            }
                        } else {
                            if (prop_can_target(self, tgt, 1, 0) != 0) {
                                found = tgt;
                            }
                        }
                    }
                    off = off + 0x141c;
                }

                if (((*(unsigned int *)(rec + 8) & 0x200) != 0) &&
                    ((*(int *)(self + 0x88) + 1U & 3) == (DAT_80611928 & 3))) {
                    int off2 = DAT_80612610 << 2;
                    for (int i = DAT_80612610; (i < DAT_80612098) && (found == 0); i = i + 1) {
                        int *p = *(int **)((int)DAT_80588610 + off2);
                        if ((p != 0) && ((p[9] & 8) == 0)) {
                            bool ok;
                            if (DAT_805015bd == '\n') {
                                ok = (*(int *)(rec + 0x18) != *(int *)(p[0x2fc] + 8));
                            } else {
                                ok = ((*(int *)(rec + 0x18) == 0) || (*(int *)(rec + 0x18) != p[0x21]));
                            }
                            if (ok && (prop_can_target(self, *p, 1, 0) != 0)) {
                                found = *p;
                            }
                        }
                        off2 = off2 + 4;
                    }
                }
            }
        } else {
            found = ((int (*)(int))*(void **)(rec + 0x1b4))(self);
        }

        if (found != 0) {
            if ((*(unsigned int *)(rec + 8) & 0x20000) == 0) {
                int req[7];   // local_64
                sound_init_play_params((int)req);
                req[0] = 0xbf8;
                // (request also carries the object position + a 1 flag, matching
                // the original spill layout)
                sound_play_request(req);
            }
            if (*(int *)(rec + 0x10) == 10) {
                *(int *)(rec + 0x10) = 0xb;
            } else {
                *(int *)(rec + 0x10) = 3;
            }
            *(float *)(rec + 0xac) = FLOAT_80618910;
            *(int *)(rec + 0x68) = found;
            *(unsigned int *)(self + 0x28) = *(unsigned int *)(self + 0x28) | 0x10;
            *(unsigned int *)(*(int *)(self + 4) + 0x30) =
                *(unsigned int *)(*(int *)(self + 4) + 0x30) & ~*(unsigned int *)(def + 0x7c);
            *(unsigned int *)(*(int *)(self + 4) + 0x30) =
                *(unsigned int *)(*(int *)(self + 4) + 0x30) | *(unsigned int *)(def + 0x80);
        }
    }
    FUN_80382724();
    return found;
}

// 0x80241710 | prop_scan_projectile_targets
// For detector props (rec+8 bit0x200) on their phase slot, sweep the live
// projectile/pickup list (DAT_80588610) for the first entity targetable without
// a raycast, returning it (0 if none).
int prop_scan_projectile_targets(void)
{
    int self = (int)FUN_803826e4();
    int rec = *(int *)(self + 0x7c);
    int found = 0;

    if (((*(unsigned int *)(rec + 8) & 0x200) != 0) &&
        ((*(int *)(self + 0x88) + 2U & 3) == (DAT_80611928 & 3))) {
        int off = 0;
        for (int i = 0; i < DAT_80612098; i = i + 1) {
            int *p = *(int **)((int)DAT_80588610 + off);
            if ((p != 0) && ((p[9] & 8) == 0)) {
                bool ok;
                if (DAT_805015bd == '\n') {
                    ok = (*(int *)(rec + 0x18) != *(int *)(p[0x2fc] + 8));
                } else {
                    ok = ((*(int *)(rec + 0x18) != 0) && (*(int *)(rec + 0x18) == p[0x21]));
                }
                if (ok && (prop_can_target(self, *p, 0, 0) != 0)) {
                    found = *p;
                    break;
                }
            }
            off = off + 4;
        }
    }
    FUN_80382730();
    return found;
}
