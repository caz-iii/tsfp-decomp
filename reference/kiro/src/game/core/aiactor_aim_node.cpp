// ============================================================================
// Address range: 0x8037ef48 - 0x8037f358 | batch_1060 | 2026-09-12
//
// AI-actor "aim node" sub-system. An AI actor that operates a mounted weapon /
// turret keeps an aim-node record (actor+0x7c[+0xaa0]) that tracks a target
// direction and drives a small look/aim state machine (states at +0x20 / +0x9d0).
// This slice covers the aim table lookup, the aim-direction solver that reads a
// shared sin/cos LUT (DAT_80583228, 0x800 entries / 0x2000 bytes), the per-frame
// look-decision, the rotation write + scaled anim, and the world-space muzzle
// point builder driven by the 4x4 matrix helpers.
//
//   aiactor_aim_predicate_true  0x8037ef48  const predicate (returns 1)
//   aiactor_aim_table_lookup    0x8037ef50  find the aim-table entry for an id
//   aiactor_aim_solve_dir       0x8037efc8  solve + clamp the aim direction vector
//   aiactor_aim_decide_look     0x8037f174  per-frame look/aim state decision
//   aiactor_aim_apply_rotation  0x8037f298  write the aim-node rotation + play anim
//   aiactor_aim_build_muzzle    0x8037f358  build the aim-node world muzzle point
//
// NOTE: non-matching decomp. GameCube prologue spills and paired-single float
// shuffles are dropped. The LUT index math (& 0x7ff for the value, ((i+0x200)*4
// & 0x1ffc) for the quarter-phase-shifted companion) reproduces the original
// sin/cos sampling of DAT_80583228. Engine helpers whose generated prototype is
// narrower than the true ABI (aiactor_evaluate_target_los, mtx4_set_translation,
// anim_play_scaled) are reached through typed function-pointer casts.
#include "common.h"
#include "generated_decls.h"

// ---- shared LUT + cached state --------------------------------------------
extern float DAT_80583228;    // sin/cos lookup table base (0x800 f32 entries)
extern int   DAT_8061208c;    // aim-table owner record
extern int   DAT_80612610;    // coop-node activity threshold

// ---- aim tuning constants --------------------------------------------------
extern float FLOAT_8061b8f0, FLOAT_8061b8f4, FLOAT_8061b8f8, FLOAT_8061b8fc;
extern float FLOAT_8061b900, FLOAT_8061b904, FLOAT_8061b908, FLOAT_8061b90c;
extern float FLOAT_8061b910, FLOAT_8061b914, FLOAT_8061b918;

// ---- raw helpers reached via typed casts (narrowed generated prototypes) ---
typedef int    (*LosFn)(void);              // aiactor_evaluate_target_los -> int
typedef void   (*SetTransFn)(double, double, double);  // mtx4_set_translation (3 doubles here)
typedef void   (*AnimPlayFn)(double, double);          // anim_play_scaled (2 doubles here)

// cache-op frame helpers (begin/end, shared with prop_impact_fx.cpp)
extern "C" int  FUN_803826f0(void);   // begin cache op -> actor record ptr
extern "C" void FUN_8038273c(void);    // end cache op

// ============================================================================
// 0x8037ef48  aiactor_aim_predicate_true  (const gate used by decide_look)
// ============================================================================
int aiactor_aim_predicate_true(void)
{
    return 1;
}

// ============================================================================
// 0x8037ef50  aiactor_aim_table_lookup
// Walk the owner's aim table (base +0x54, count +0x58, stride 8) for the entry
// whose id (word 0) matches param, accumulating a running node offset from the
// per-entry span (halfword at +6). Returns CONCAT of the matched entry's two
// halfwords and the resolved node pointer (base +0x5c + accum*0x18).
// ============================================================================
unsigned long long aiactor_aim_table_lookup(int id)
{
    unsigned short lo = 0, hi = 0;
    int node = 0;
    int accum = 0;
    int count = *(int*)(DAT_8061208c + 0x58);
    if (count > 0) {
        for (int off = 0; count != 0; --count, off += 8) {
            int* entry = (int*)(*(int*)(DAT_8061208c + 0x54) + off);
            if (id == *entry) {
                lo = *(unsigned short*)(entry + 1);
                hi = *(unsigned short*)((int)entry + 6);
                node = *(int*)(DAT_8061208c + 0x5c) + accum * 0x18;
                break;
            }
            accum += (unsigned)*(unsigned short*)(entry + 1);
        }
    }
    unsigned pack = ((unsigned)lo << 16) | hi;
    return ((unsigned long long)pack << 32) | (unsigned)node;
}

// ============================================================================
// 0x8037efc8  aiactor_aim_solve_dir
// Solve the aim-direction vector for an actor. With no aim node it falls back
// to the default vector; otherwise it samples the sin/cos LUT for pitch (from
// node+0x20) and yaw (from actor+0x70 + node+0x24) and clamps the magnitude via
// one Newton normalisation step when it exceeds the threshold.
// ============================================================================
void aiactor_aim_solve_dir(int actor, float* out)
{
    float c0 = FLOAT_8061b8f0, c1 = FLOAT_8061b8f4, c2 = FLOAT_8061b8f8, c3 = FLOAT_8061b8fc;
    float* lut = &DAT_80583228;
    int node = *(int*)(*(int*)(actor + 0x7c) + 0xaa0);

    if (node == 0) {
        out[0] = FLOAT_8061b900;
        out[1] = FLOAT_8061b900;
        out[2] = FLOAT_8061b904;
    } else {
        int pIdx = (int)(c1 * ((c2 * *(float*)(node + 0x20)) / c3) + c0);
        int yIdx = (int)(c1 * ((c2 * (*(float*)(actor + 0x70) + *(float*)(node + 0x24))) / c3) + c0);
        // x = cos(pitch) * sin(yaw): companion sample uses the +0x200 phase shift
        out[0] = *(float*)((int)lut + ((pIdx + 0x200) * 4 & 0x1ffc)) * lut[yIdx & 0x7ff];
        // y = sin(pitch)
        out[1] = lut[pIdx & 0x7ff];
        // z = cos(pitch) * cos(yaw)
        out[2] = *(float*)((int)lut + ((pIdx + 0x200) * 4 & 0x1ffc))
               * *(float*)((int)lut + ((yIdx + 0x200) * 4 & 0x1ffc));
    }

    float x = out[0];
    float mag2 = out[2] * out[2] + x * x + out[1] * out[1];
    if (FLOAT_8061b908 < mag2) {
        float inv = 1.0f / SQRT(mag2);
        inv = inv * -(FLOAT_8061b8f0 * mag2 * inv * inv - FLOAT_8061b90c);
        float scale = FLOAT_8061b904 * inv * -(FLOAT_8061b8f0 * mag2 * inv * inv - FLOAT_8061b90c);
        out[0] = x * scale;
        out[1] = out[1] * scale;
        out[2] = out[2] * scale;
    }
}

// ============================================================================
// 0x8037f174  aiactor_aim_decide_look
// Per-frame look/aim decision. When the actor has line of sight to a type-8
// target and the target is either an idle/expired coop node or the const gate
// passes with no alerted group, the actor enters aim state 3 (subject to the
// cooldown at +0xaf4 and the timer at +0xb30); otherwise it drops to look
// state 2.
// ============================================================================
void aiactor_aim_decide_look(int actor)
{
    int rec = *(int*)(actor + 0x7c);
    int los = ((LosFn)&aiactor_evaluate_target_los)();
    if ((los != 0) && (*(int*)(*(int*)(rec + 4) + 0x20) == 8)) {
        int tgtRec = *(int*)(*(int*)(rec + 4) + 0x7c);
        if ((((tgtRec == 0) || (DAT_80612610 <= *(int*)(tgtRec + 8))) &&
             (tracker_actor_coop_node_active(tgtRec) == 0)) ||
            ((aiactor_aim_predicate_true() != 0) &&
             (aiactor_alert_group_flagA_any(*(unsigned*)(*(int*)(rec + 0xbf0) + 0x70)) == 0))) {
            if (((*(float*)(rec + 0xaf4) <= FLOAT_8061b910) && (*(int*)(rec + 0x20) == 1)) &&
                ((FLOAT_8061b914 < *(float*)(rec + 0xb30) ||
                  (*(int*)(rec + 4) != *(int*)(rec + 0xa8c))))) {
                *(float*)(rec + 0xb30) = FLOAT_8061b900;
                if (*(int*)(rec + 0x20) != 3) {
                    *(int*)(rec + 0x9d0) = 3;
                }
            }
        } else if (*(int*)(rec + 0x20) != 2) {
            *(float*)(rec + 0xb30) = FLOAT_8061b900;
            *(float*)(rec + 0xb34) = FLOAT_8061b900;
            if (*(int*)(rec + 0x20) != 2) {
                *(int*)(rec + 0x9d0) = 2;
            }
        }
    }
}

// ============================================================================
// 0x8037f298  aiactor_aim_apply_rotation
// Write the aim-node rotation (node+0x38..+0x40) from the LUT sampled off the
// actor yaw (actor+0x70), play the scaled aim anim, and enter aim state 6.
// ============================================================================
void aiactor_aim_apply_rotation(int actor)
{
    float c0 = FLOAT_8061b8f0, c1 = FLOAT_8061b8f4, c2 = FLOAT_8061b8f8, c3 = FLOAT_8061b8fc;
    float* lut = &DAT_80583228;
    int rec = *(int*)(actor + 0x7c);
    int node = *(int*)(rec + 0xaa0);

    int idx = (int)(c1 * ((c2 * *(float*)(actor + 0x70)) / c3) + c0);
    *(float*)(node + 0x38) = lut[idx & 0x7ff];
    *(float*)(node + 0x3c) = FLOAT_8061b900;
    *(float*)(node + 0x40) = *(float*)((int)lut + ((idx + 0x200) * 4 & 0x1ffc));

    ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b900, (double)FLOAT_8061b918);
    if (*(int*)(rec + 0x20) != 6) {
        *(int*)(rec + 0x9d0) = 6;
    }
}

// ============================================================================
// 0x8037f358  aiactor_aim_build_muzzle
// Build the aim-node world muzzle point (node+0x14..+0x1c). When the actor's
// interp flags (+0x0c/+0x10) are clear it copies the actor's cached world point
// (+0x64..+0x6c) directly; otherwise it reads the muzzle bone offset from the
// model, applies the aim pitch/yaw rotations about the model pivot into the
// actor's local matrix, then transforms the bone point into world space.
// ============================================================================
void aiactor_aim_build_muzzle(void)
{
    float mtxA[16];   // afStack_8c
    float mtxB[16];   // afStack_cc
    float pt[3];      // local_d8/d4/d0

    int actor = FUN_803826f0();
    int rec = *(int*)(actor + 0x7c);
    int node = *(int*)(rec + 0xaa0);
    model_pose_eval();

    if (((*(unsigned*)(actor + 0x10) | *(unsigned*)(actor + 0xc)) & 0xffff) == 0) {
        *(int*)(node + 0x14) = *(int*)(actor + 0x64);
        *(int*)(node + 0x18) = *(int*)(actor + 0x68);
        *(int*)(node + 0x1c) = *(int*)(actor + 0x6c);
    } else {
        int* bones = *(int**)(*(int*)(actor + 4) + 4);
        int bone = bones[*bones * -0x30 + 0x7b];
        pt[0] = *(float*)(bone + 0x30);
        pt[1] = *(float*)(bone + 0x34);
        pt[2] = *(float*)(bone + 0x38);

        if (*(int*)(rec + 0x20) != 0xc) {
            int model = *(int*)(*(int*)(actor + 4) + 0x10);
            double px = (double)*(float*)(model + 0xb0);
            double py = (double)*(float*)(model + 0xb4);
            double pz = (double)*(float*)(model + 0xb8);
            float* mm80 = (float*)(*(int*)(*(int*)(actor + 4) + 0x10) + 0x80);
            float* mm40 = (float*)(*(int*)(*(int*)(actor + 4) + 0x10) + 0x40);

            ((SetTransFn)&mtx4_set_translation)(-px, -py, -pz);
            mtx4_multiply(mm80, mtxA, mm80);
            mtx4_set_rotation_x(-(double)((FLOAT_8061b8f8 * *(float*)(node + 0x20)) / FLOAT_8061b8fc),
                                (int)mtxA);
            mtx4_multiply(mm80, mtxA, mm80);
            mtx4_set_rotation_y((double)((FLOAT_8061b8f8 * *(float*)(node + 0x24)) / FLOAT_8061b8fc),
                                mtxB);
            mtx4_multiply(mm80, mtxB, mm80);
            ((SetTransFn)&mtx4_set_translation)(px, py, pz);
            mtx4_multiply(mm80, mtxA, mm80);
            mtx4_set_rotation_y((double)((FLOAT_8061b8f8 * *(float*)(node + 0x24)) / FLOAT_8061b8fc),
                                mtxA);
            mtx4_multiply(mm40, mtxA, mm40);
        }
        entity_build_local_matrix(mtxA, (void*)(long long)actor, 1, 0);
        mtx4_transform_point((float*)(*(int*)(*(int*)(actor + 4) + 0x10) + 0x80), pt);
        mtx4_transform_point(mtxA, pt);
        *(float*)(node + 0x14) = pt[0];
        *(int*)(node + 0x18) = *(int*)&pt[1];
        *(int*)(node + 0x1c) = *(int*)&pt[2];
    }
    FUN_8038273c();
}

// ============================================================================
// batch_1061 additions: AI aim-node spline path follow + firing + targeting
//   (0x8037f628 - 0x8038058c), appended to the aim-node sub-system.
//
// The aim node can follow a looping/one-shot spline path (control points at
// node[+4] stride 0x18, count/loop packed by aiactor_aim_table_lookup). This
// slice covers the 4-point segment selector, the arc-length path integrator,
// the nearest-node/interp-t finder, the aim-angle solver toward a target, the
// AI fire routine (spread + trail + projectile + sound), the player target
// scan, the muzzle-point copy, the turret mount, and the spectator recoil/HUD.
// ============================================================================

// ---- additional raw state --------------------------------------------------
extern int    DAT_80588610;   // player-slot pointer table base
extern int    DAT_80612098;   // active player-slot count (end index)
extern unsigned DAT_806109c0; // rng lo word (packed-double reconstruct)
extern unsigned DAT_806108e4; // rng hi word (packed-double reconstruct)
extern int    DAT_803e10c8, DAT_803e10cc, DAT_803e10d0;  // default fire dir vec
extern int    DAT_80612614;   // current level id
extern int    DAT_80611d74;   // pending mount player slot

extern float  FLOAT_806118ec;
extern float  FLOAT_80611718;
extern float  FLOAT_8061b91c, FLOAT_8061b928, FLOAT_8061b92c, FLOAT_8061b930;
extern float  FLOAT_8061b934, FLOAT_8061b938, FLOAT_8061b93c, FLOAT_8061b940;
extern float  FLOAT_8061b950, FLOAT_8061b954;
extern double DOUBLE_8061b920, DOUBLE_8061b948;

// cache/frame helpers (begin/end variants used by this slice)
extern "C" int  FUN_803826e8(void);   // begin spline op -> actor record ptr
extern "C" void FUN_80382734(void);    // end spline op
extern "C" int  FUN_803826dc(void);    // begin target-scan -> actor record ptr
extern "C" void FUN_80382728(void);    // end target-scan

// narrowed-prototype helpers reached via typed casts
typedef float (*SplineTangentFn)(double, float*, float*, float*, float*, float*);
typedef unsigned (*RngTickFn)(void);
typedef void  (*DirToAnglesFn)(float*, void*, float*);
typedef void  (*ProjSpawnFn)(double, double, int, float*, int*, int, int*, int, int, int, unsigned, int);

// forward decls for the helpers defined in this slice
void aiactor_aim_muzzle_point(int actor, unsigned* out);

// ============================================================================
// 0x8037f628  aiactor_spline_pick_points
// Select the four control points (prev2, prev1, cur, next) for spline segment
// param_2 of the path descriptor param_1 (count word 0, base at +2, stride
// 0x18). Loop flag (halfword 1) chooses wrap-around vs clamp-at-ends indexing.
// ============================================================================
void aiactor_spline_pick_points(unsigned short* desc, int seg, int* p0, int* p1, int* p2, int* p3)
{
    int base = *(int*)(desc + 2);
    int count = *desc;
    if (desc[1] == 0) {
        // clamp-at-ends: negative indices collapse to 0, last clamps to count-1
        unsigned m2 = seg - 2;
        unsigned m1 = seg - 1;
        *p0 = base + (m2 & (int)(-m2 & ~m2) >> 0x1f) * 0x18;
        *p1 = base + (m1 & (int)(-m1 & ~m1) >> 0x1f) * 0x18;
        *p2 = base + seg * 0x18;
        int nxt = count - 1;
        if (seg + 1 < count - 1) nxt = seg + 1;
        *p3 = base + nxt * 0x18;
    } else {
        // looping: wrap modulo count
        int i0 = (seg < 2) ? seg + (count - 2) : seg - 2;
        *p0 = base + i0 * 0x18;
        int i1 = (seg < 1) ? seg + (count - 1) : seg - 1;
        *p1 = base + i1 * 0x18;
        *p2 = base + seg * 0x18;
        int i3 = (seg + 1) - ((seg + 1) / count) * count;
        *p3 = base + i3 * 0x18;
    }
}

// ============================================================================
// 0x8037f758  aiactor_spline_follow
// Arc-length path integrator: step the aim node along its spline toward the
// actor's world point, converging when the sampled point is within the speed
// window or after 1000 iterations, then set the actor look angles from the
// path tangent and mark it arrived (byte +0x32 = 2).
// ============================================================================
void aiactor_spline_follow(void)
{
    float *cp0, *cp1, *cp2, *cp3;
    unsigned long long desc;
    unsigned char yawByte[4];
    float yaw;
    float pos[3];       // local_b8/b4/b0
    float tangent[3];   // afStack_ac

    int actor = FUN_803826e8();
    int rec = *(int*)(actor + 0x7c);
    int node = *(int*)(rec + 0xaa0);
    bool done = false;
    int iter = 0;
    cp0 = cp1 = cp2 = cp3 = 0;

    double speed = (double)(*(float*)(node + 0xc) * FLOAT_806118ec);
    desc = aiactor_aim_table_lookup(*(int*)(*(int*)(rec + 0xbf0) + 0x28));
    unsigned short segCount = (unsigned short)desc;
    unsigned short segLoop  = (unsigned short)(desc >> 16);

    aiactor_spline_pick_points((unsigned short*)&desc, *(int*)(node + 4),
                               (int*)&cp0, (int*)&cp1, (int*)&cp2, (int*)&cp3);
    spline_eval_pos((double)*(float*)(node + 8), (float*)(actor + 0x64), cp0, cp1, cp2, cp3);
    spline_eval_angles((double)*(float*)(node + 8), (float*)(node + 0x44),
                       cp0 + 3, cp1 + 3, cp2 + 3, cp3 + 3);
    if ((*(unsigned*)(*(int*)(rec + 0xbf0) + 0x58) & 0x1000000) != 0) {
        *(float*)(node + 0x48) = *(float*)(node + 0x48) + FLOAT_8061b8fc;
    }
    double tlen = (double)((SplineTangentFn)&spline_eval_tangent)((double)*(float*)(node + 8),
                                                                  tangent, cp0, cp1, cp2, cp3);
    if ((double)FLOAT_8061b900 == tlen) tlen = (double)FLOAT_8061b904;

    double zero = (double)FLOAT_8061b900;
    double one  = (double)FLOAT_8061b904;
    *(float*)(node + 0x10) = FLOAT_8061b900;

    do {
        // dt = speed * (rngScale) / tangentLen, applied to the path parameter
        float p = (*(int*)(node + 0x50) == 0) ? *(float*)(node + 0x6c) : *(float*)(node + 0x64);
        float dt = (float)((speed) / tlen) * p;
        *(float*)(node + 8)   = *(float*)(node + 8) + dt;
        *(float*)(node + 0x10) = *(float*)(node + 0x10) + dt;

        // wrap the path parameter forward past segment ends
        while ((double)*(float*)(node + 8) > one) {
            *(float*)(node + 8) = (float)((double)*(float*)(node + 8) - one);
            int s = *(int*)(node + 4) + 1;
            *(int*)(node + 4) = s - (s / (int)segCount) * (int)segCount;
            if ((*(int*)(node + 4) == 0) && (segLoop == 0)) {
                *(float*)(node + 8) = FLOAT_8061b900;
                break;
            }
        }
        // and backward below segment start
        while ((double)*(float*)(node + 8) < zero) {
            *(float*)(node + 8) = (float)((double)*(float*)(node + 8) + one);
            int s = *(int*)(node + 4) - 1;
            if (s < 0) s = segCount - 1;
            *(int*)(node + 4) = s;
        }

        aiactor_spline_pick_points((unsigned short*)&desc, *(int*)(node + 4),
                                   (int*)&cp0, (int*)&cp1, (int*)&cp2, (int*)&cp3);
        spline_eval_pos((double)*(float*)(node + 8), pos, cp0, cp1, cp2, cp3);

        // distance test against the actor world point
        float dx = *(float*)(actor + 0x64) - pos[0];
        float dy = *(float*)(actor + 0x68) - pos[1];
        float dz = *(float*)(actor + 0x6c) - pos[2];
        float step = (float)(speed);
        bool within;
        if (*(int*)(node + 0x50) == 0) {
            within = (step * step <= dz * dz + dx * dx + dy * dy);
        } else {
            float rad = step * ABS(*(float*)(node + 100));
            within = (dz * dz + dx * dx + dy * dy < rad * rad);
        }
        if (!within) {
            tlen = (double)((SplineTangentFn)&spline_eval_tangent)((double)*(float*)(node + 8),
                                                                   (float*)0, cp0, cp1, cp2, cp3);
            if ((double)FLOAT_8061b900 == tlen) tlen = (double)FLOAT_8061b904;
        } else {
            done = true;
        }

        ++iter;
        if (done || iter > 999) {
            *(unsigned char*)(actor + 0x32) = 2;
            ((DirToAnglesFn)&dir_to_angles)(tangent, yawByte, &yaw);
            *(int*)(actor + 0xc0) = *(int*)yawByte;
            *(float*)(actor + 0x70) = yaw;
            *(float*)(actor + 0xc4) = yaw;
            *(float*)(actor + 0xc8) = FLOAT_8061b900;
            FUN_80382734();
            return;
        }
    } while (true);
}

// ============================================================================
// 0x8037fc24  aiactor_spline_find_nearest
// Find the spline control point nearest the actor and compute the segment
// interpolation t (node+8) from the distances to the two bracketing points.
// ============================================================================
void aiactor_spline_find_nearest(void)
{
    int actor = FUN_803826e8();
    double best = (double)FLOAT_8061b928;
    int node = *(int*)(*(int*)(actor + 0x7c) + 0xaa0);
    unsigned long long desc = aiactor_aim_table_lookup(*(int*)(*(int*)(*(int*)(actor + 0x7c) + 0xbf0) + 0x28));
    int base = (int)desc;
    unsigned short count = (unsigned short)desc;
    short loop = (short)(desc >> 32);

    int idx = 0, off = 0;
    for (unsigned n = count; n != 0; --n) {
        float* cp = (float*)(base + off);
        float dx = cp[0] - *(float*)(actor + 0x64);
        float dy = cp[1] - *(float*)(actor + 0x68);
        float dz = cp[2] - *(float*)(actor + 0x6c);
        double d = (double)(dz * dz + dx * dx + dy * dy);
        if (d < best) { *(int*)(node + 4) = idx; best = d; }
        ++idx; off += 0x18;
    }

    if (loop != 0) {
        int cur = *(int*)(node + 4);
        int base8 = base + 8;
        int prev = cur - 1;
        int nxt = (cur + 1) - ((cur + 1) / (int)count) * (int)count;
        int o = nxt * 0x18;
        float dnx = *(float*)(base + o) - *(float*)(actor + 0x64);
        float dnz = *(float*)(base8 + o) - *(float*)(actor + 0x6c);
        (void)(*(float*)(base + 4 + o) - *(float*)(actor + 0x68));
        if (prev < 0) prev = prev + count;
        o = prev * 0x18;
        float dpx = *(float*)(base + o) - *(float*)(actor + 0x64);
        float dpz = *(float*)(base8 + o) - *(float*)(actor + 0x6c);
        (void)(*(float*)(base + 4 + o) - *(float*)(actor + 0x68));
        // pick the closer bracketing neighbour
        // (compare squared distances of next vs prev)
        float dny = *(float*)(base + 4 + nxt * 0x18) - *(float*)(actor + 0x68);
        float dpy = *(float*)(base + 4 + prev * 0x18) - *(float*)(actor + 0x68);
        if (dpz * dpz + dpx * dpx + dpy * dpy < dnz * dnz + dnx * dnx + dny * dny) {
            *(int*)(node + 4) = prev;
            nxt = cur;
        }
        int oc = *(int*)(node + 4) * 0x18;
        float ax = *(float*)(base + oc) - *(float*)(actor + 0x64);
        float az = *(float*)(base8 + oc) - *(float*)(actor + 0x6c);
        double da = math_sqrt((double)(ax * ax + az * az));
        float bx = *(float*)(base + nxt * 0x18) - *(float*)(actor + 0x64);
        float bz = *(float*)(base8 + nxt * 0x18) - *(float*)(actor + 0x6c);
        double db = math_sqrt((double)(bx * bx + bz * bz));
        *(float*)(node + 8) = (float)(da / (double)(float)(da + db));
    }
    FUN_80382734();
}

// ============================================================================
// 0x8037fe14  aiactor_aim_face_target
// Compute the aim-node facing angles (node+0x2c pitch word, node+0x30 yaw)
// toward the current target's aim point (target+0xbb4..+0xbbc), relative to the
// actor yaw.
// ============================================================================
void aiactor_aim_face_target(void)
{
    unsigned dir[3];   // local_1c/18/14
    unsigned rel[3];   // local_28/24/20

    int actor = FUN_803826f0();
    int rec = *(int*)(actor + 0x7c);
    int node = *(int*)(rec + 0xaa0);
    if (*(int*)(rec + 4) != 0) {
        aiactor_aim_muzzle_point(actor, dir);
        int tgt = *(int*)(*(int*)(rec + 4) + 0x7c);
        // rel = target aim point - muzzle point (float vector)
        *(float*)&rel[0] = *(float*)(tgt + 0xbb4) - *(float*)&dir[0];
        *(float*)&rel[1] = *(float*)(tgt + 0xbb8) - *(float*)&dir[1];
        *(float*)&rel[2] = *(float*)(tgt + 0xbbc) - *(float*)&dir[2];
        ((DirToAnglesFn)&dir_to_angles)((float*)rel, (void*)(node + 0x2c), (float*)(node + 0x30));
        *(float*)(node + 0x30) = *(float*)(node + 0x30) - *(float*)(actor + 0x70);
    }
    FUN_8038273c();
}

// ============================================================================
// 0x8037feb8  aiactor_aim_fire
// AI turret fire: build the muzzle point + aim direction, add RNG spread scaled
// by the fire mode, play the shot sound (level 0x16 only, gated by cooldown
// node+0x5c), spawn a trail particle, and spawn the projectile.
// ============================================================================
void aiactor_aim_fire(void)
{
    float dir[3];    // local_90/8c/88
    float muzzle[3]; // local_84/80/7c
    int sndParams[6];
    float sndPos[4];

    int actor = FUN_803826f0();
    dir[0] = *(float*)&DAT_803e10c8;
    dir[1] = *(float*)&DAT_803e10cc;
    dir[2] = *(float*)&DAT_803e10d0;
    int* rec = *(int**)(actor + 0x7c);
    int node = rec[0x2a8];   // rec+0xaa0

    aiactor_aim_muzzle_point(actor, (unsigned*)muzzle);
    aiactor_aim_solve_dir(actor, dir);
    muzzle[0] = FLOAT_8061b92c * dir[0] + muzzle[0];
    muzzle[1] = FLOAT_8061b92c * dir[1] + muzzle[1];
    muzzle[2] = FLOAT_8061b92c * dir[2] + muzzle[2];

    // three RNG samples -> a spread offset added to the aim direction
    double sx = (double)(FLOAT_8061b930 * ((float)(((RngTickFn)&rng_counter_tick)()) / FLOAT_8061b934 - FLOAT_8061b8f0));
    double sy = (double)(FLOAT_8061b930 * ((float)(((RngTickFn)&rng_counter_tick)()) / FLOAT_8061b934 - FLOAT_8061b8f0));
    double sz = (double)(FLOAT_8061b930 * ((float)(((RngTickFn)&rng_counter_tick)()) / FLOAT_8061b934 - FLOAT_8061b8f0));
    double spread = (*(int*)(node + 0x50) == 0) ? (double)FLOAT_8061b938 : (double)FLOAT_8061b91c;
    dir[0] = (float)(spread * sx + (double)dir[0]);
    dir[1] = (float)(spread * sy + (double)dir[1]);
    dir[2] = (float)(spread * sz + (double)dir[2]);

    if ((*(float*)(node + 0x5c) <= FLOAT_8061b900) && (DAT_80612614 == 0x16)) {
        sound_init_play_params((int)sndParams);
        sndParams[0] = 0x62e;
        sndPos[0] = FLOAT_80611718;
        sndPos[1] = muzzle[0];
        sndPos[2] = muzzle[1];
        sndPos[3] = muzzle[2];
        *(unsigned char*)((char*)sndParams + 0x28) = 1;
        sound_play_request(sndParams);
        *(float*)(node + 0x5c) = FLOAT_8061b93c;
    }
    if (rec[8] != 5) {
        player_ai_spawn_trail_particle(0, 2, 0xffffffff, muzzle, dir, -1);
    }
    int mode = (*(int*)(node + 0x50) == 0) ? 8 : 0x1e;
    ((ProjSpawnFn)&player_ai_projectile_spawn_simple)(
        (double)FLOAT_8061b940, (double)FLOAT_8061b938, 0x1e, muzzle, (int*)dir, 0,
        rec, 2, 0, *(int*)(actor + 0x34), 0, mode);
    FUN_8038273c();
}

// ============================================================================
// 0x80380194  aiactor_aim_select_target
// Scan the active player slots for the nearest valid (non-heavy-weapon, team
// or free) target that is roughly in front of the aim direction; fall back to
// the visible-list retarget scan when nothing is found.
// ============================================================================
void aiactor_aim_select_target(void)
{
    float dir[3];   // local_58/54/50
    int actor = FUN_803826dc();
    double bestFront = (double)FLOAT_8061b928;
    int rec = *(int*)(actor + 0x7c);
    int frontIdx = -1;
    double bestAny = bestFront;
    aiactor_aim_solve_dir(actor, dir);

    int anyIdx = -1;
    for (int slot = DAT_80612610; slot < DAT_80612098; ++slot) {
        int* p = *(int**)((int)&DAT_80588610 + slot * 4);
        int heavy = player_ai_is_heavy_weapon((int)*(short*)((int)p + 0x4a));
        if ((heavy == 0) &&
            ((*(int*)(p[0x2fc] + 8) < 0) ||
             (*(int*)(p[0x2fc] + 8) == *(int*)(*(int*)(rec + 0xbf0) + 0x74)))) {
            int ent = *p;
            float dx = *(float*)(ent + 0x64) - *(float*)(actor + 0x64);
            float dy = *(float*)(ent + 0x68) - *(float*)(actor + 0x68);
            float dz = *(float*)(ent + 0x6c) - *(float*)(actor + 0x6c);
            double d = (double)(dz * dz + dx * dx + dy * dy);
            bool inFront = (dir[2] * dz + dir[0] * dx + dir[1] * dy > FLOAT_8061b900);
            if ((d < bestAny) && inFront) {
                anyIdx = slot; bestAny = d;
            } else if ((anyIdx == -1) && (d < bestFront)) {
                frontIdx = slot; bestFront = d;
            }
        }
    }
    if ((anyIdx == -1) && (frontIdx == -1)) {
        aiactor_retarget_scan_visible_list(actor);
    }
    FUN_80382728();
}

// ============================================================================
// 0x80380330  aiactor_aim_muzzle_point
// Copy the aim node's cached world muzzle point (node+0x14..+0x1c) into out.
// No-op when the actor has no aim node.
// ============================================================================
void aiactor_aim_muzzle_point(int actor, unsigned* out)
{
    int node = *(int*)(*(int*)(actor + 0x7c) + 0xaa0);
    if (node == 0) return;
    out[0] = *(unsigned*)(node + 0x14);
    out[1] = *(unsigned*)(node + 0x18);
    out[2] = *(unsigned*)(node + 0x1c);
}

// ============================================================================
// 0x8038035c  aiactor_aim_mount_player
// Mount the currently selected player onto the turret aim node: stash the
// player id byte (node+0x9c), set player state 0x8000, enter aim state 5 and
// flag the node active/mounted (node+0x50/+0x54 = 1). Referenced by the prop
// selection panel (prop_select_set_active).
// ============================================================================
void aiactor_aim_mount_player(void)
{
    int sel = prop_mount_get_selected();
    int player = aiactor_find_player_by_team(sel);
    if (player != 0) {
        int rec = *(int*)(player + 0x7c);
        int node = *(int*)(rec + 0xaa0);
        if (node != 0) {
            *(char*)(node + 0x9c) = (char)*(int*)(DAT_80611d74 + 4);
            player_set_state(DAT_80611d74, 0x8000);
            if (*(int*)(rec + 0x20) != 5) {
                *(int*)(rec + 0x9d0) = 5;
            }
            *(int*)(node + 0x54) = 1;
            *(int*)(node + 0x50) = 1;
        }
    }
}

// ============================================================================
// 0x803803e4  aiactor_aim_update_recoil
// Spectator view: advance the 3-slot recoil/kick ring buffer of the aim node
// (each slot: value +0x74, target +0x78, owner +0x7c at 0xc stride) and, in
// aim state 3, push a new kick sample once the fire timer elapses.
// ============================================================================
void aiactor_aim_update_recoil(void)
{
    int slot = spectator_get_slot();
    int rec = *(int*)(*(int*)(slot + 0x2ac) + 0x7c);
    int node = *(int*)(rec + 0xaa0);
    int owner = *(int*)(rec + 4);
    if (node == 0) return;

    float rngScale = (FLOAT_8061b904 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061b920))
                   * (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_8061b920);
    if (*(int*)(rec + 0x20) == 3) {
        if (*(float*)(rec + 0xb30) <= rngScale) {
            *(float*)(node + *(int*)(node + 0x70) * 0xc + 0x78) = FLOAT_8061b950;
            *(int*)(node + 0x70) = (*(int*)(node + 0x70) + 1) % 3;
            *(int*)(node + *(int*)(node + 0x70) * 0xc + 0x7c) = owner;
            *(float*)(node + *(int*)(node + 0x70) * 0xc + 0x78) = FLOAT_8061b904;
        }
    }
    int off = 0;
    for (int i = 3; i != 0; --i) {
        int s = node + off;
        *(float*)(s + 0x74) = FLOAT_8061b954 * rngScale * *(float*)(s + 0x78) + *(float*)(s + 0x74);
        if (*(float*)(s + 0x74) <= FLOAT_8061b90c) {
            if (*(float*)(s + 0x74) < FLOAT_8061b900) {
                *(float*)(s + 0x74) = FLOAT_8061b900;
                *(float*)(s + 0x78) = FLOAT_8061b900;
                *(int*)(s + 0x7c) = 0;
            }
        } else {
            *(float*)(s + 0x74) = FLOAT_8061b90c;
            if (*(int*)(rec + 0x20) != 2) {
                *(float*)(s + 0x78) = -*(float*)(s + 0x78);
            }
        }
        off += 0xc;
    }
}

// ============================================================================
// 0x8038058c  aiactor_aim_draw_monitor
// Spectator view: draw the static monitor overlay when the spectated player
// has an aim node.
// ============================================================================
void aiactor_aim_draw_monitor(void)
{
    int slot = spectator_get_slot();
    if (*(int*)(*(int*)(*(int*)(slot + 0x2ac) + 0x7c) + 0xaa0) != 0) {
        zcsc_monitor_static_draw();
    }
}

// ============================================================================
// batch_1062 additions: AI aim-node per-frame tick + event handlers + support
//   (0x803805c4 - 0x80382578), appended to the aim-node sub-system.
//
// The centrepiece is aiactor_aim_tick (0x80380e2c), the aim-node state machine
// (states 0..0xc: spawn, spline approach, engage, retreat, death, mounted, and
// the sentry-gun deploy/redeploy/pack-up animation states). Around it: the
// reticle draw, the spectator look-input applier, the mount-UI update, the
// linked-node proximity toggle, the soundpad inter-portal path search that
// picks a fallback sound zone, the anim/damage and destroy event handlers, a
// variadic bytecode arg walker, and the deferred cleanup-list runner.
// ============================================================================

// ---- additional raw state / tables ----------------------------------------
extern int      DAT_80611d78;   // input/player context table base (0x141c stride)
extern int      DAT_80611b90;   // soundpad inter-portal edge table base
extern int      DAT_80611824;   // anim clip-def table base
extern int*     DAT_80612fc8;   // deferred cleanup callback list head
extern unsigned DAT_806109c4;   // screen width (int, rng-packed)
extern unsigned DAT_806109c8;   // screen height (int, rng-packed)

extern float  FLOAT_806108ec;
extern float  FLOAT_8061b958, FLOAT_8061b95c, FLOAT_8061b960, FLOAT_8061b964;
extern float  FLOAT_8061b968, FLOAT_8061b96c, FLOAT_8061b970, FLOAT_8061b974;
extern float  FLOAT_8061b978, FLOAT_8061b97c, FLOAT_8061b980, FLOAT_8061b984;
extern float  FLOAT_8061b988, FLOAT_8061b98c, FLOAT_8061b990, FLOAT_8061b994;
extern float  FLOAT_8061b998, FLOAT_8061b99c, FLOAT_8061b9a0, FLOAT_8061b9a4;
extern float  FLOAT_8061b9a8, FLOAT_8061b9ac, FLOAT_8061b9b0, FLOAT_8061b9b4;
extern float  FLOAT_8061b9b8, FLOAT_8061b9bc;

// cache/frame helpers used by this slice
extern "C" int  FUN_803826b8(void);   // begin soundpad-path op -> actor record ptr
extern "C" void FUN_80382704(void);    // end soundpad-path op

// fractional/mod wrap helper (returns double); referenced only by address
extern "C" double FUN_80390324(double);

// state shared with prop_impact_fx.cpp
extern int  DAT_80611d54;   // active local-player record
extern int  DAT_805ef568;   // particle-cache freelist control block

// narrowed-prototype helpers reached via typed casts
typedef double   (*Fmod01Fn)(double);           // FUN_80390324: fractional/mod helper
typedef int      (*PropForeachFn)(void);         // prop_type_foreach used arg-less w/ return
typedef unsigned long long (*FollowPathFn)(void);// aiactor_follow_zone_path w/ return
typedef float*   (*WorldPosFn)(int);             // entity_get_world_position

// forward decls (mutual references inside this slice)
int  aiactor_aim_anim_event(int actor, int ev);
void aiactor_soundpad_pick_zone(void);
void aiactor_linked_node_toggle(int* rec);
void aiactor_run_cleanup_list(void);

// ============================================================================
// 0x803805c4  aiactor_aim_draw_reticle
// Draw the aim reticle crosshair (two full-screen lines). For a non-mounted
// local-player aim node it derives the crosshair position from the raw mouse
// axes; otherwise it uses the passed screen coords.
// ============================================================================
void aiactor_aim_draw_reticle(double x, double y)
{
    if (((DAT_80611d54 != 0) && (*(int*)(DAT_80611d54 + 0x2ac) != 0))) {
        int node = *(int*)(*(int*)(*(int*)(DAT_80611d54 + 0x2ac) + 0x7c) + 0xaa0);
        if ((node != 0) && (*(int*)(node + 0x50) == 0)) {
            double mx = (double)(CONCAT44(0x43300000, DAT_806109c4) ^ 0x80000000);
            double my = (double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000);
            x = (double)((float)(mx - DOUBLE_8061b920) * FLOAT_8061b8f0);
            y = (double)((float)(my - DOUBLE_8061b920) * FLOAT_8061b8f0);
        }
    }
    gx_set_sub_material_id(2);
    gx_set_blend_mode(0);
    gx_draw_line_i(0, (int)y, DAT_806109c4, (int)y, 0x7f00007f);
    gx_draw_line_i((int)x, 0, (int)x, DAT_806109c8, 0x7f00007f);
}

// ============================================================================
// 0x80380904  aiactor_aim_mount_ui_update
// Iterate the prop-type table; when a mount prop is active, run the panel loop
// and clear the owning pad's input buttons.
// ============================================================================
void aiactor_aim_mount_ui_update(void)
{
    int prop = ((PropForeachFn)&prop_type_foreach)();
    if (prop != 0) {
        prop_panel_update_loop();
        input_clear_buttons(**(int**)(prop + 0x14), (*(int**)(prop + 0x14))[9]);
    }
}

// ============================================================================
// 0x80380958  aiactor_linked_node_toggle
// Resolve the aim node's tag-linked sibling nodes; when one is within range and
// closer in the secondary axis set, flip both nodes' sign bit (word 0x1b) and
// set their flag bit 0 (the paired laser-link toggle).
// ============================================================================
void aiactor_linked_node_toggle(int* rec)
{
    unsigned* self = (unsigned*)rec[0x2a8];
    int recs[8];
    int n = entity_tag_resolve_all(*(int*)(rec[0x2fc] + 0x28), (int)recs, 5);
    for (int i = 0, k = 0; k < n; ++k, i += 4) {
        int other = *(int*)((int)recs + i);
        unsigned* onode = *(unsigned**)(*(int*)(other + 0x7c) + 0xaa0);
        if ((onode != 0) && (self != onode)) {
            int ent = *rec;
            float dx = *(float*)(other + 0x64) - *(float*)(ent + 0x64);
            float dy = *(float*)(other + 0x68) - *(float*)(ent + 0x68);
            float dz = *(float*)(other + 0x6c) - *(float*)(ent + 0x6c);
            float d = dz * dz + dx * dx + dy * dy;
            if (d < FLOAT_8061b904) {
                float ex = *(float*)(other + 0xec) - *(float*)(ent + 0xec);
                float ey = *(float*)(other + 0xf0) - *(float*)(ent + 0xf0);
                float ez = *(float*)(other + 0xf4) - *(float*)(ent + 0xf4);
                if (d < ez * ez + ex * ex + ey * ey) {
                    *(float*)&self[0x1b] = -*(float*)&self[0x1b];
                    *(float*)&onode[0x1b] = -*(float*)&onode[0x1b];
                    onode[0] |= 1;
                    self[0] |= 1;
                    return;
                }
            }
        }
    }
}

// ============================================================================
// 0x80382468  aiactor_aim_on_destroy  (entity event 5 -> free the aim node)
// ============================================================================
int aiactor_aim_on_destroy(int actor, int ev)
{
    int ret = 0;
    if (ev == 5) {
        freelist_push((int)(long long)&DAT_805ef568, *(void***)(*(int*)(actor + 0x7c) + 0xaa0));
        ret = -1;
    }
    return ret;
}

// ============================================================================
// 0x80382578  aiactor_run_cleanup_list
// Drain the deferred cleanup callback list (each node: next[0], fn[1], arg[2]);
// invoke fn(arg, -1) for each.
// ============================================================================
void aiactor_run_cleanup_list(void)
{
    while (DAT_80612fc8 != 0) {
        int* node = DAT_80612fc8;
        int fn = node[1];
        int arg = node[2];
        DAT_80612fc8 = (int*)node[0];
        ((void (*)(int, int))(long long)fn)(arg, 0xffffffff);
    }
}

// ============================================================================
// 0x803824b0  aiactor_arg_walker
// Bytecode arg-slot walker used by the aim-node script interpreter. Reads a
// mode byte from the record and advances the argument cursor, returning the
// pointer to (or value of, mode 0) the next argument slot.
// ============================================================================
int* aiactor_arg_walker(char* rec, int mode)
{
    char cur = *rec;
    int limit = 8;
    int align = 4;
    char inc = 1;
    unsigned bias = 0;
    int addBase = 0;
    int stride = 4;
    char* slotByte = rec;

    if (mode == 3) {
        cur = rec[1];
        slotByte = rec + 1;
        align = 8;
        addBase = 0x20;
        stride = 8;
    }
    unsigned v = (unsigned)cur;
    if (mode == 2) {
        align = 8;
        limit = 7;
        bias = (unsigned)((v & 1) != 0);
        inc = 2;
    }

    int* slot;
    if ((int)v < limit) {
        int base = *(int*)(rec + 8);
        *slotByte = (char)(v + bias) + inc;
        slot = (int*)(base + addBase + (v + bias) * stride);
    } else {
        *slotByte = '\b';
        slot = (int*)(~(align - 1U) & (align + *(int*)(rec + 4)) - 1U);
        *(int*)(rec + 4) = (int)slot + align;
    }
    if (mode == 0) {
        slot = (int*)*slot;
    }
    return slot;
}

// ============================================================================
// 0x803806d8  aiactor_aim_spectator_input
// Spectator/mounted view: apply the look input (param_1 pitch, param_2 yaw,
// param_3 stick vector) to the aim node's view rotation, wrapping pitch/yaw
// through FUN_80390324 and clamping pitch into the allowed band.
// ============================================================================
void aiactor_aim_spectator_input(double pitchIn, double yawIn, float* stick)
{
    int slot = spectator_get_slot();
    float spd = FLOAT_8061b930;
    if (((slot != 0) && ((slot = *(int*)(slot + 0x2ac)) != 0))) {
        int node = *(int*)(*(int*)(slot + 0x7c) + 0xaa0);
        if ((node != 0) && (*(int*)(node + 0x50) != 0)) {
            double negYaw = -yawIn;
            unsigned short flags = *(unsigned short*)(*(int*)(DAT_80611d74 + 0x1398) + 2);
            unsigned i1 = (unsigned)(FLOAT_8061b8f4 * ((FLOAT_8061b8f8 * (*(float*)(slot + 0x70) + *(float*)(node + 0x24))) / FLOAT_8061b8fc) + FLOAT_8061b8f0);
            unsigned i2 = (unsigned)(FLOAT_8061b8f4 * ((FLOAT_8061b8f8 * *(float*)(slot + 0x70)) / FLOAT_8061b8fc) + FLOAT_8061b8f0);
            float* lut = &DAT_80583228;
            double c1 = (double)*(float*)((int)lut + ((i1 + 0x200) * 4 & 0x1ffc));
            float  c2 = *(float*)((int)lut + ((i2 + 0x200) * 4 & 0x1ffc));
            // world-project the stick delta into the aim node pitch (node+0x64)
            *(float*)(node + 100) =
                ((float)(c1 * pitchIn) * c2
                 + (float)((double)lut[i1 & 0x7ff] * pitchIn) * lut[i2 & 0x7ff]
                 + (float)((double)FLOAT_8061b900 * pitchIn) * FLOAT_8061b900
                 + (float)(-(double)lut[i1 & 0x7ff] * negYaw) * c2
                 + (float)(c1 * negYaw) * lut[i2 & 0x7ff]
                 + (float)((double)FLOAT_8061b900 * negYaw) * FLOAT_8061b900) / FLOAT_8061b90c;
            float sens = ((flags & 1) != 0) ? FLOAT_8061b950 : FLOAT_8061b904;
            double p = (double)(spd * stick[1] * sens + *(float*)(node + 0x2c));
            double wrapped = ((Fmod01Fn)&FUN_80390324)((double)((float)(p - (double)FLOAT_8061b900) / FLOAT_8061b958));
            *(float*)(node + 0x2c) = -(float)((double)FLOAT_8061b958 * (double)(float)wrapped - p);
            float pitch = *(float*)(node + 0x2c);
            if ((pitch <= FLOAT_8061b95c) || (FLOAT_8061b960 <= pitch)) {
                if ((pitch < FLOAT_8061b964) && (FLOAT_8061b968 < pitch)) {
                    *(float*)(node + 0x2c) = FLOAT_8061b964;
                }
            } else {
                *(float*)(node + 0x2c) = FLOAT_8061b95c;
            }
            double yy = (double)(FLOAT_8061b930 * stick[0] + *(float*)(node + 0x30));
            double wy = ((Fmod01Fn)&FUN_80390324)((double)((float)(yy - (double)FLOAT_8061b900) / FLOAT_8061b958));
            *(float*)(node + 0x30) = -(float)((double)FLOAT_8061b958 * (double)(float)wy - yy);
        }
    }
}

// ============================================================================
// 0x80380a94  aiactor_soundpad_pick_zone
// Breadth-first search over the sound-zone inter-portal graph from the actor's
// current zone to pick the best fallback zone (node+0x684) for a retreating AI,
// scoring candidate zones by distance from the target and avoiding zones that
// contain a heavy-weapon enemy. Runs inside the FUN_803826b8/FUN_80382704 frame.
// ============================================================================
void aiactor_soundpad_pick_zone(void)
{
    int actor = FUN_803826b8();
    int tgtRec = *(int*)(*(int*)(actor + 4) + 0x7c);
    int* padTgt = soundpad_resolve_zone_node(*(int*)(tgtRec + 0x678));
    int* padSelf = soundpad_resolve_zone_node(*(int*)(actor + 0x678));
    int startZone = (int)*(short*)(padTgt + 2);
    int best = -1;

    int edgeList;
    int edgeCount = soundpad_get_inter_portal_list(startZone, (unsigned**)&edgeList);
    double bestScore = (double)FLOAT_8061b96c;

    int queue[10];
    memset_wrapper((int)queue, 0xff, 0x28);
    queue[0] = startZone;
    int* padQ = soundpad_resolve_zone_node(*(int*)(actor + 0x678));
    queue[1] = soundpad_get_entity_zone_id((int)padQ);
    int qCount = 2;
    int qTail = 8;
    padQ = soundpad_resolve_zone_node(*(int*)(actor + 0x678));
    int selfZone = soundpad_find_zone_for_entity(soundpad_get_entity_zone_id((int)padQ));

    int e = 0, eo = 0;
    for (;;) {
        if ((edgeCount <= e) || (qCount > 9)) {
            // exhausted the frontier: score every queued zone's entities
            int qi = 0, qo = 0;
            for (;;) {
                if (qCount <= qi) {
                    if (best < 0) {
                        *(int*)(actor + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    } else {
                        *(int*)(actor + 0x684) = best;
                    }
                    *(int*)(actor + 0x9ac) = 0;
                    *(int*)(actor + 0x9a8) = 0;
                    FUN_80382704();
                    return;
                }
                int entList;
                int entCount = soundpad_get_entity_data(*(int*)((int)queue + qo), &entList);
                int io = 0;
                for (int ii = 0; ii < entCount; ++ii) {
                    int* padE = soundpad_resolve_zone_node(*(int*)(entList + io));
                    if ((ABS((double)((float)padSelf[4] - (float)padE[4])) < (double)FLOAT_8061b938) &&
                        ((*(int*)(actor + 0x678) != *(int*)(entList + io)) || (best < 0)) &&
                        ((*(int*)(tgtRec + 0x678) != *(int*)(entList + io)) || (best < 0))) {
                        double dist = (double)(((float)padE[3] - (float)padTgt[3]) * ((float)padE[3] - (float)padTgt[3])
                                             + ((float)padE[5] - (float)padTgt[5]) * ((float)padE[5] - (float)padTgt[5]));
                        if ((dist <= (double)FLOAT_8061b970) || ((double)FLOAT_8061b970 <= bestScore) || (best < 0)) {
                            // penalise zones holding a heavy-weapon enemy
                            int po = DAT_80612610 * 4;
                            for (int slot = DAT_80612610; slot < DAT_80612098; ++slot) {
                                int* p = *(int**)((int)&DAT_80588610 + po);
                                if ((p[2] != *(int*)(actor + 8)) &&
                                    (player_ai_is_heavy_weapon((int)*(short*)((int)p + 0x4a)) != 0) &&
                                    (6 < p[8] && p[8] < 0xb)) {
                                    float dx = (float)padE[3] - *(float*)(*p + 0x64);
                                    float dz = (float)padE[5] - *(float*)(*p + 0x6c);
                                    if ((p[0x1a1] == *(int*)(io + entList)) ||
                                        (dx * dx + dz * dz < FLOAT_8061b974)) {
                                        dist = (double)(float)(dist * (double)FLOAT_8061b8f0);
                                        break;
                                    }
                                }
                                po += 4;
                            }
                            if ((bestScore <= dist) || ((double)FLOAT_8061b970 <= bestScore)) {
                                best = *(int*)(entList + io);
                                bestScore = dist;
                            }
                        }
                    }
                    io += 4;
                }
                ++qi; qo += 4;
            }
        }
        // expand the next inter-portal edge into the queue
        int edge = DAT_80611b90 + *(int*)(edgeList + eo) * 0x14;
        int* padA = soundpad_resolve_zone_node(*(int*)(edge + 4));
        int* padB = soundpad_resolve_zone_node(*(int*)(edge + 8));
        int nz = (int)*(short*)(padA + 2);
        if ((nz == startZone) && ((nz = -1, *(short*)(padB + 2) != startZone))) {
            nz = (int)*(short*)(padB + 2);
        }
        int zoneOf = soundpad_find_zone_for_entity(nz);
        if ((selfZone == zoneOf) && (-1 < nz)) {
            int scan = qCount + 1;
            int j = 0;
            if (-1 < qCount) {
                do {
                    if (nz == *(int*)((int)queue + j)) break;
                    if (*(int*)((int)queue + j) < 0) {
                        *(int*)((int)queue + qTail) = nz;
                        ++qCount;
                        qTail += 4;
                        break;
                    }
                    j += 4;
                    --scan;
                } while (scan != 0);
            }
        }
        ++e; eo += 4;
    }
}

// ============================================================================
// 0x80382228  aiactor_aim_anim_event
// Aim-node animation/damage event handler. Event 2 (hit/impact) either fires
// the sentry-gun destruction sequence (sound 0x63a, anim, state 0xb, debris
// particle) for the gun variant, or a generic damage puff (state 4). Event 1
// latches the reload flag; event 7 is acknowledged.
// ============================================================================
int aiactor_aim_anim_event(int actor, int ev)
{
    int ret = 0;
    if (ev == 2) {
        int rec = *(int*)(actor + 0x7c);
        if ((*(short*)(actor + 0x94) == 0x621) && (6 < *(int*)(rec + 0x20))) {
            if (*(int*)(rec + 0x20) != 0xb) {
                double vol = (double)(FLOAT_8061b90c * FLOAT_80611718);
                // sound param block: int[6] header then float vol(+0x18), pos(+0x1c..+0x24),
                // byte flag(+0x28), range(+0x2c)  (matches FUN_80382228 stack layout)
                unsigned char sp[0x34];
                sound_init_play_params((int)sp);
                *(int*)(sp + 0x00) = 0x63a;
                *(float*)(sp + 0x18) = (float)vol;
                *(int*)(sp + 0x1c) = *(int*)(actor + 100);
                *(int*)(sp + 0x20) = *(int*)(actor + 0x68);
                *(int*)(sp + 0x24) = *(int*)(actor + 0x6c);
                *(unsigned char*)(sp + 0x28) = 1;
                *(float*)(sp + 0x2c) = FLOAT_8061b9b4;
                sound_play_request((int*)sp);
                ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b900, (double)FLOAT_8061b904);
                if (*(int*)(rec + 0x20) != 0xb) {
                    *(int*)(rec + 0x9d0) = 0xb;
                }
                int sflag = session_get_state_flag();
                float* wp = entity_get_world_position(actor);
                particle_spawn_directed(0, 0x16, ~-(unsigned)(sflag == 1) & 0x80000, wp, 0);
                entity_get_world_position(actor);
                character_find_nearest_bone(actor, 0);
            }
        } else if ((*(int*)(rec + 0x20) != 4) && (*(int*)(rec + 0x20) != 6)) {
            float puff[3];
            puff[0] = *(float*)(actor + 100) + *(float*)(actor + 0x1a8);
            puff[1] = *(float*)(actor + 0x68) + *(float*)(actor + 0x1ac);
            puff[2] = *(float*)(actor + 0x6c) + *(float*)(actor + 0x1b0);
            int sflag = session_get_state_flag();
            particle_spawn_directed(0, 0x15, ~-(unsigned)(sflag == 1) & 0x80000, puff, 0);
            *(float*)(rec + 0xb30) = FLOAT_8061b900;
            if (*(int*)(rec + 0x20) != 4) {
                *(int*)(rec + 0x9d0) = 4;
            }
        }
        ret = -1;
    } else if (ev < 2) {
        if (0 < ev) {
            if ((*(short*)(actor + 0x94) == 0x621) &&
                (*(float*)(actor + 0x1cc) < FLOAT_8061b8f0 * *(float*)(actor + 0x1d8)) &&
                (*(int*)(*(int*)(actor + 0x7c) + 0x20) < 6)) {
                *(int*)(*(int*)(*(int*)(actor + 0x7c) + 0xaa0) + 0x60) = 1;
            }
            ret = -1;
        }
    } else if (ev == 7) {
        ret = -1;
    }
    return ret;
}

// ============================================================================
// 0x80380e2c  aiactor_aim_tick
// The aim-node state machine, run once per frame inside FUN_803826e8 /
// FUN_80382734. The actor record (actor+0x7c) holds the state at word[8], the
// prev/next-state bookkeeping at [0x274..0x276], and the aim-node pointer at
// [0x2a8]. States: 0 spawn/init, 1 spline approach, 2 engage/fire, 3 track,
// 4 death, 5 player-mounted, 6 deploy raise, 7 idle scan, 8/9 turn clips,
// 10 burst fire, 0xb self-destruct spawn, 0xc dead.
//
// Helper aim_set_state performs the standard prev<-cur / cur<-new / timer<-0
// transition. aim_rng_dt reconstructs the per-frame delta the original builds
// from the two packed rng words and DOUBLE_8061b920.
// ============================================================================
void aim_set_state(int* rec, int st)
{
    if (rec[8] != st) {
        rec[0x275] = rec[8];
        rec[8] = st;
        rec[0x276] = (int)FLOAT_8061b900;
        rec[0x274] = rec[8];
    }
}
float aim_rng_dt(void)
{
    return (FLOAT_8061b904 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061b920))
         * (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_8061b920);
}

void aiactor_aim_tick(void)
{
    int actor = FUN_803826e8();
    int* rec = *(int**)(actor + 0x7c);
    int teammate = aiactor_retarget_visible_teammate();

    // pending queued state (word 0x274) applied first
    if (-1 < rec[0x274]) {
        if (rec[0x274] != rec[8]) {
            rec[0x276] = (int)FLOAT_8061b900;
            rec[0x275] = rec[8];
            rec[8] = rec[0x274];
        }
        rec[0x274] = -1;
    }

    // charge/heat accumulator (word 0x2a4) advances only while flag 0x80 set
    if ((rec[0xb] & 0x80U) == 0) {
        rec[0x2a4] = (int)FLOAT_8061b900;
    } else {
        rec[0x2a4] = (int)(FLOAT_806108ec * aim_rng_dt() + (float)rec[0x2a4]);
    }

    unsigned* node = (unsigned*)rec[0x2a8];
    float dtHeat = aim_rng_dt();
    // node[0x17] cooldown decays toward FLOAT_8061b97c
    if ((node != 0) &&
        (*(float*)&node[0x17] = -(float)((double)FLOAT_806108ec * (double)dtHeat - (double)*(float*)&node[0x17]),
         *(float*)&node[0x17] <= FLOAT_8061b97c)) {
        *(float*)&node[0x17] = FLOAT_8061b97c;
    }

    // states 5 or 4..0xc handle the linked-node proximity toggle + aim rotation
    int st = rec[8];
    if ((st == 5) || (st < 5 && st >= 4) || (st > 5 && st <= 0xc)) {
        if (node != 0) {
            if ((node[0] & 1) == 0) {
                aiactor_linked_node_toggle(rec);
            } else {
                node[0] &= 0xfffffffe;
            }
            if (node[0x18] != 0) {
                aiactor_aim_apply_rotation(actor);
            }
        }
    }

    // deferred state pushes from the aim node's request flags
    if ((node != 0) && (node[0x14] != 0) && (rec[8] != 5)) {
        rec[0x274] = 5;
        goto done;
    }
    if ((node != 0) && (node[0x15] != 0) && (node[0x14] == 0) &&
        (*(int*)(DAT_80611d78 + (unsigned)*(unsigned char*)((char*)node + 0x9c) * 0x141c + 0x114) == 0x8000)) {
        unsigned char pad = *(unsigned char*)((char*)node + 0x9c);
        unsigned held = input_get_buttons_held(pad);
        if ((*(unsigned*)(DAT_80611d78 + pad * 0x141c + 0x24) & held) == 0) {
            held = input_get_buttons_held(pad);
            if ((*(unsigned*)(DAT_80611d78 + pad * 0x141c + 0x20) & held) != 0) {
                node[0x14] = 1;
                aim_set_state(rec, 5);
            }
        } else {
            aiactor_aim_mount_ui_update();
        }
    }

    rec[0x2a3] = rec[1];
    rec[1] = aiactor_acquire_nearest_target(actor, *(int*)(DAT_80611d78 + 0x14), 0);

    // states 7..0xa maintain the "primary target present" latch (word 0x277)
    {
        int s = rec[8];
        if ((s >= 7 && s < 9) || (s == 9) || (s == 0xa)) {
            int prev = rec[0x277];
            if (prev == 0) {
                if (teammate == 0) rec[0x2cd] = (int)FLOAT_8061b900;
                else if ((double)(float)rec[0x2cd] < (double)FLOAT_8061b904) teammate = prev;
                rec[0x277] = teammate;
            } else {
                if (teammate == 0) {
                    if ((double)(float)rec[0x2cd] < (double)FLOAT_8061b904) teammate = prev;
                } else rec[0x2cd] = (int)FLOAT_8061b900;
                rec[0x277] = teammate;
            }
        }
    }

    float dt = dtHeat;
    switch (rec[8]) {
    case 0: {
        entity_register_for_update(actor, 2, (int)(long long)(void*)&aiactor_aim_tick);
        node = (unsigned*)prop_fx_cache_node_alloc();
        rec[0x2a8] = (int)node;
        node[1] = 0;
        node[2] = (unsigned)FLOAT_8061b900;
        node[0x16] = (unsigned)FLOAT_8061b900;
        node[0x1b] = (unsigned)FLOAT_8061b904;
        node[0x17] = (unsigned)FLOAT_8061b900;
        node[3] = (unsigned)FLOAT_8061b930;
        rec[0x1d] = (int)(long long)&aiactor_aim_on_destroy;
        aim_set_state(rec, 1);
        unsigned short kind = *(unsigned short*)(actor + 0x94);
        if (kind == 0x621) {
            if ((*(unsigned*)(rec[0x2fc] + 0x58) & 0x8000000) == 0) {
                ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b900, (double)FLOAT_8061b900);
                aiactor_spline_find_nearest();
            } else {
                aiactor_aim_apply_rotation(actor);
                node[0xe] = (unsigned)((float)node[0xe] * FLOAT_8061b940);
                node[0xf] = (unsigned)((float)node[0xf] * FLOAT_8061b940);
                node[0x10] = (unsigned)((float)node[0x10] * FLOAT_8061b940);
            }
        } else if ((kind < 0x621) && (0x61f < kind)) {
            ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b900, (double)FLOAT_8061b900);
            aiactor_spline_find_nearest();
        }
        break;
    }
    case 1:
        aiactor_spline_follow();
        aiactor_aim_decide_look(actor);
        node[0x16] = (unsigned)FLOAT_8061b900;
        if (((float)node[8] == (float)node[0xb]) && ((float)node[9] == (float)node[0xc]) &&
            ((float)node[10] == (float)node[0xd])) {
            double d = (double)(((float)node[0x11] + (FLOAT_8061b980 * ((float)(((RngTickFn)&rng_counter_tick)()) / FLOAT_8061b934) - FLOAT_8061b984)) - *(float*)(actor + 0xc0));
            double w = ((Fmod01Fn)&FUN_80390324)((double)((float)(d - (double)FLOAT_8061b900) / FLOAT_8061b958));
            node[0xb] = (unsigned)-(float)((double)FLOAT_8061b958 * (double)(float)w - d);
            d = (double)(((float)node[0x12] + (FLOAT_8061b978 * ((float)(((RngTickFn)&rng_counter_tick)()) / FLOAT_8061b934) - FLOAT_8061b988)) - *(float*)(actor + 0x70));
            w = ((Fmod01Fn)&FUN_80390324)((double)((float)(d - (double)FLOAT_8061b900) / FLOAT_8061b958));
            node[0xc] = (unsigned)-(float)((double)FLOAT_8061b958 * (double)(float)w - d);
            node[0xd] = (unsigned)FLOAT_8061b900;
        }
        dt = (float)((double)dt * (double)FLOAT_8061b8f0);
        node[0x1b] = node[0x1b] & 0x80000000 | 0x3f800000;
        teammate = aiactor_alert_group_flagA_any(*(unsigned*)(rec[0x2fc] + 0x70));
        if (teammate != 0) {
            rec[0x2cc] = (int)FLOAT_8061b900;
            rec[0x2cd] = (int)FLOAT_8061b900;
            aim_set_state(rec, 2);
        }
        break;
    case 2: {
        node[0x1a] = (unsigned)-(aim_rng_dt() - (float)node[0x1a]);
        if (rec[0x275] != 2) {
            if ((float)rec[0x276] <= aim_rng_dt()) {
                node[0x1a] = (unsigned)FLOAT_8061b950;
            }
        }
        if ((float)node[0x1a] < FLOAT_8061b900) {
            node[0x1b] = (unsigned)-(float)(node[0x1b] & 0x80000000 | 0x3f800000);
            node[0x1a] = (unsigned)(FLOAT_8061b914 * (FLOAT_8061b8f0 + (float)(((RngTickFn)&rng_counter_tick)()) / FLOAT_8061b934));
        }
        aiactor_spline_follow();
        aiactor_aim_decide_look(actor);
        aiactor_aim_face_target();
        if (teammate != 0) {
            rec[0x2cc] = (int)FLOAT_8061b900;
            if (FLOAT_8061b914 <= (float)rec[0x2cd]) {
                if (FLOAT_8061b994 < (float)rec[0x2cd]) rec[0x2cd] = (int)FLOAT_8061b900;
            } else {
                double fire = (double)FLOAT_8061b990;
                node[0x16] = (unsigned)(aim_rng_dt() + (float)node[0x16]);
                while (fire < (double)(float)node[0x16]) {
                    aiactor_aim_fire();
                    node[0x16] = (unsigned)(float)((double)(float)node[0x16] - fire);
                }
            }
        }
        if (FLOAT_8061b914 < (float)rec[0x2cc]) {
            aim_set_state(rec, 1);
            rec[0x2cc] = (int)FLOAT_8061b900;
        }
        aiactor_alert_group_arm(*(unsigned*)(rec[0x2fc] + 0x70));
        break;
    }
    case 3:
        if (rec[0x275] != 3) {
            if ((float)rec[0x276] <= aim_rng_dt()) {
                node[0x1b] = (unsigned)((float)(node[0x1b] & 0x80000000 | 0x3f800000) * FLOAT_8061b98c);
            }
        }
        aiactor_spline_follow();
        aiactor_aim_decide_look(actor);
        aiactor_aim_face_target();
        dt = (float)((double)dt * (double)FLOAT_8061b8f0);
        if ((FLOAT_8061b910 < (float)rec[0x2bd]) || (FLOAT_8061b90c < (float)rec[0x2cc])) {
            rec[0x2cc] = (int)FLOAT_8061b900;
            aim_set_state(rec, 1);
        }
        teammate = aiactor_alert_group_flagA_any(*(unsigned*)(rec[0x2fc] + 0x70));
        if (teammate != 0) {
            aim_set_state(rec, 2);
            rec[0x2cc] = (int)FLOAT_8061b900;
            rec[0x2cd] = (int)FLOAT_8061b900;
        }
        break;
    case 4:
        node[0xb] = (unsigned)FLOAT_8061b99c;
        dt = (float)((double)dt * (double)FLOAT_8061b9a0);
        if ((double)FLOAT_8061b940 < (double)(float)rec[0x2cc]) {
            tracker_death_finalize((double)FLOAT_8061b940, 0, 0, 0, 0, 0, 0, 0, actor);
            goto done;
        }
        if ((rec[0x31b] & 2U) == 0) {
            character_opacity_fade_in(FLOAT_8061b940, (int)(rec + 0x31a));
        }
        break;
    case 5: {
        unsigned char pad = *(unsigned char*)((char*)node + 0x9c);
        int pstate = *(int*)(DAT_80611d78 + pad * 0x141c + 0x114);
        if ((pstate == 0x2000) || (pstate == 0x8000)) {
            aiactor_spline_follow();
            dt = (float)((double)dt * (double)FLOAT_8061b930);
            unsigned pressed = input_get_buttons_pressed(pad);
            int pc = DAT_80611d78 + pad * 0x141c;
            if (((*(unsigned*)(pc + 0x18) | *(unsigned*)(pc + 0x1c)) & pressed) != 0) {
                double fire = (double)FLOAT_8061b998;
                node[0x16] = (unsigned)(aim_rng_dt() + (float)node[0x16]);
                while (fire < (double)(float)node[0x16]) {
                    aiactor_aim_fire();
                    node[0x16] = (unsigned)(float)((double)(float)node[0x16] - fire);
                }
            }
            unsigned held = input_get_buttons_held(pad);
            if ((*(unsigned*)(DAT_80611d78 + pad * 0x141c + 0x24) & held) != 0) {
                aiactor_aim_mount_ui_update();
            }
        }
        break;
    }
    case 6: {
        int clip = anim_active_clip_slot(actor);
        float clipT = *(float*)(clip + 8);
        float clipEnd = *(float*)(*(int*)(DAT_80611824 + *(int*)(clip + 4) * 4) + 0x18);
        dt = (float)((double)dt * (double)FLOAT_8061b914);
        float rate = FLOAT_8061b918 * FLOAT_806118ec;
        node[0xb] = (unsigned)FLOAT_8061b900;
        double frac = (double)((clipEnd - clipT) / rate);
        node[0xc] = (unsigned)FLOAT_8061b900;
        node[0x18] = 0;
        player_ai_get_floor_room(actor);
        double drop = (double)(*(float*)(actor + 0x68) - (float)rec[0x2ff]);
        if ((drop <= (double)FLOAT_8061b900) || (frac <= (double)FLOAT_8061b900)) {
            ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b900, (double)FLOAT_8061b930);
            *(float*)(actor + 0x68) = *(float*)(actor + 0x1c4) + ((float)rec[0x2ff] - *(float*)(actor + 0x1ac));
            *(unsigned char*)(actor + 0x32) = 1;
            node[0x16] = (unsigned)FLOAT_8061b900;
            rec[0x2cc] = (int)FLOAT_8061b900;
            aim_set_state(rec, 7);
            rec[0x19e] = soundpad_find_nearest_entity((double)FLOAT_8061b900, (float*)(actor + 100));
            aiactor_soundpad_pick_zone();
        } else {
            float k = FLOAT_806108ec * aim_rng_dt() * (float)(drop / frac);
            node[0xf] = (unsigned)-(k - (float)node[0xf]);
            *(float*)(actor + 100) = (float)node[0xe] * aim_rng_dt() + *(float*)(actor + 100);
            *(float*)(actor + 0x68) = (float)node[0xf] * aim_rng_dt() + *(float*)(actor + 0x68);
            *(float*)(actor + 0x6c) = (float)node[0x10] * aim_rng_dt() + *(float*)(actor + 0x6c);
        }
        break;
    }
    case 7: {
        double dxz;
        {
            double px = (double)*(float*)(actor + 100);
            float m = (float)(px - (double)(float)rec[0xc]);
            float mz = *(float*)(actor + 0x6c) - (float)rec[0xe];
            dxz = (double)(m * m + mz * mz);
        }
        if (teammate == 0) {
            node[0xb] = (unsigned)FLOAT_8061b900;
            node[0xc] = (unsigned)FLOAT_8061b900;
        } else {
            aiactor_aim_face_target();
        }
        dt = (float)((double)dt * (double)FLOAT_8061b914);
        if (anim_is_clip_active(actor, 0x760) == 0) {
            ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b9a4, (double)FLOAT_8061b930);
        }
        if (((rec[10] & 1U) != 0) || (ABS(dxz) < (double)FLOAT_8061b938)) {
            aiactor_soundpad_pick_zone();
        }
        unsigned long long mv = ((FollowPathFn)&aiactor_follow_zone_path)();
        aiactor_movement_tick(mv, 0, 0, 0, 0, 0, 0, 0);
        if ((teammate == 0) ||
            (((double)FLOAT_8061b938 <= ABS(dxz)) && ((float)rec[0x2cc] <= FLOAT_8061b9a8)) ||
            (FLOAT_8061b9ac <= (float)rec[0x2bd])) {
            double wrap = prop_hinge_wrap_angle_to_range((double)*(float*)(actor + 0x70), (float*)(actor + 100), (float*)(rec + 0xc));
            if ((double)FLOAT_8061b9b4 <= ABS(wrap)) {
                node[0] &= 0xfffffffd;
                ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b9a4, (double)FLOAT_8061b904);
                aim_set_state(rec, 9);
            } else {
                aiactor_facing_update();
            }
        } else {
            ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b9b0, (double)FLOAT_8061b904);
            aim_set_state(rec, 10);
        }
        break;
    }
    case 8:
        if (teammate == 0) {
            node[0xb] = (unsigned)FLOAT_8061b900;
            node[0xc] = (unsigned)FLOAT_8061b900;
        } else {
            aiactor_aim_face_target();
        }
        dt = (float)((double)dt * (double)FLOAT_8061b914);
        if (anim_primary_clip_userflags(actor, 0x768) == 0) {
            if (anim_is_clip_active(actor, 0x768) == 0) {
                aim_set_state(rec, 7);
            } else {
                aiactor_movement_tick(0, 0, 0, 0, 0, 0, 0, 0);
            }
        } else {
            ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b900, (double)FLOAT_8061b930);
            aim_set_state(rec, 7);
        }
        break;
    case 9:
        if (teammate == 0) {
            node[0xb] = (unsigned)FLOAT_8061b900;
            node[0xc] = (unsigned)FLOAT_8061b900;
        } else {
            aiactor_aim_face_target();
        }
        dt = (float)((double)dt * (double)FLOAT_8061b914);
        if (anim_primary_clip_userflags(actor, 0x767) == 0) {
            if (anim_is_clip_active(actor, 0x767) == 0) {
                aim_set_state(rec, 7);
            } else if ((anim_slot_is_primary_clip(actor, 0x767) != 0) &&
                       (anim_event_has_passed(actor, 0x200, 1) != 0) &&
                       (anim_event_has_passed(actor, 4, 1) == 0)) {
                double wrap = prop_hinge_wrap_angle_to_range((double)*(float*)(actor + 0x70), (float*)(actor + 100), (float*)(rec + 0xc));
                int animState = *(int*)(actor + 4) + 0xcc;
                int frames[7];
                anim_get_frame_times(animState, frames, 0);
                unsigned nth = anim_event_find_nth_by_mask(*(int*)animState, 4, 1);
                if (0 < (int)(nth - frames[0])) {
                    *(float*)(actor + 0x70) = *(float*)(actor + 0x70)
                        + (float)(wrap / (double)(float)((double)CONCAT44(0x43300000, (nth - frames[0]) ^ 0x80000000) - DOUBLE_8061b920));
                    *(int*)(actor + 0xc4) = *(int*)(actor + 0x70);
                }
                if ((node[0] & 2) == 0) {
                    double vol = (double)(FLOAT_8061b90c * FLOAT_80611718);
                    unsigned char sp[0x34];
                    sound_init_play_params((int)sp);
                    *(int*)(sp + 0x04) = 0x639;
                    *(float*)(sp + 0x1c) = (float)vol;
                    *(int*)(sp + 0x20) = *(int*)(actor + 100);
                    *(int*)(sp + 0x24) = *(int*)(actor + 0x68);
                    *(int*)(sp + 0x28) = *(int*)(actor + 0x6c);
                    *(unsigned char*)(sp + 0x2c) = 1;
                    *(float*)(sp + 0x30) = FLOAT_8061b9b4;
                    sound_play_request((int*)(sp + 0x04));
                    node[0] |= 2;
                }
            }
        } else {
            ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b900, (double)FLOAT_8061b930);
            aim_set_state(rec, 7);
        }
        break;
    case 10: {
        aiactor_aim_face_target();
        dt = (float)((double)dt * (double)FLOAT_8061b914);
        if (anim_is_clip_active(actor, 0x769) == 0) {
            ((AnimPlayFn)&anim_play_scaled)((double)FLOAT_8061b9b0, (double)FLOAT_8061b904);
        }
        double fire = (double)FLOAT_8061b990;
        node[0x16] = (unsigned)(aim_rng_dt() + (float)node[0x16]);
        while (fire < (double)(float)node[0x16]) {
            aiactor_aim_fire();
            node[0x16] = (unsigned)(float)((double)(float)node[0x16] - fire);
        }
        if ((FLOAT_8061b930 < (float)rec[0x276]) &&
            ((teammate == 0) || (FLOAT_8061b9b8 < (float)rec[0x2bd]) || (FLOAT_8061b994 < (float)rec[0x276]))) {
            aim_set_state(rec, 7);
            rec[0x2cc] = (int)FLOAT_8061b900;
            aiactor_soundpad_pick_zone();
        }
        break;
    }
    case 0xb:
        node[0xb] = (unsigned)FLOAT_8061b900;
        dt = (float)((double)dt * (double)FLOAT_8061b914);
        node[0xc] = (unsigned)FLOAT_8061b900;
        if (anim_active_slot_userflags(*(int*)(actor + 4)) != 0) {
            projectile_spawn();
            aim_set_state(rec, 0xc);
        }
        break;
    case 0xc:
        rec[0xb] = rec[0xb] | 0x100000;
        break;
    }

    // post-state: mirror the tracked look angles into the smoothed set
    if (node[0x14] != 0) {
        node[8] = node[0xb];
        node[9] = node[0xc];
        node[10] = node[0xd];
    }
    // integrate the 3 aim axes (node+0x20/+0x2c pairs) toward their targets
    {
        int off = 0;
        for (int i = 0, k = 3; k != 0; --k, ++i, off += 4) {
            float target = *(float*)((int)node + off + 0x2c);
            float delta = target - *(float*)((int)node + off + 0x20);
            double d = (double)delta;
            while (d <= (double)FLOAT_8061b9bc) d = (double)(float)(d + (double)FLOAT_8061b958);
            for (; (double)FLOAT_8061b8fc < d; d = (double)(float)(d - (double)FLOAT_8061b958)) {}
            float step = dt;
            if ((i == 1) && (node[0x14] == 0)) {
                step = (float)((double)dt / (double)FLOAT_8061b914);
            }
            if (d <= (double)step) {
                if (-(double)step <= d) {
                    *(float*)((int)node + off + 0x20) = target;
                } else {
                    *(float*)((int)node + off + 0x20) = (float)((double)*(float*)((int)node + off + 0x20) - (double)step);
                }
            } else {
                *(float*)((int)node + off + 0x20) = (float)((double)*(float*)((int)node + off + 0x20) + (double)step);
            }
        }
    }
    {
        float snap = (rec[8] == 7) ? FLOAT_8061b90c : FLOAT_8061b97c;
        scalar_step_toward_snap((double)snap, (double)FLOAT_8061b990, (float*)(node + 0x26));
    }
    ((void (*)(double, double, double, int, int, int, int))&sound_update_3d_source)(
        (double)(FLOAT_80611718 * (float)node[0x26]), (double)FLOAT_8061b9b4,
        (double)FLOAT_8061b904, actor + 0x22c, actor + 100, 0x638, 0);
    rec[10] = rec[10] & 0xfffdffff;
    rec[10] = rec[10] & 0xfffbffff;
    anim_set_playback_rate((double)FLOAT_8061b92c, *(int**)(actor + 4), 1);

done:
    FUN_80382734();
}
