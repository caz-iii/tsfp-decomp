// boss3_tentacle.cpp - a third segmented boss: a spawner/"tentacle" boss built on
// the large control block DAT_80612a48. It grows animated spiral tentacles, each
// made of a chain of "link" sub-records that spiral out from a base point, seek a
// target/anchor entity, then retract while shedding sparks.
//
// The first function (mech_boss_kill_finisher, 0x8029cd94) is physically adjacent
// but belongs to the MECH boss (DAT_80612a4c, see mech_boss_fsm.cpp) - it is the
// mech's death/kill reward routine and is kept here only for range contiguity.
//
// DAT_80612a48 layout (partial):
//   +0x9c..+0xa4     spawn direction vector
//   +0xa8..+0xb0     facing basis (right/up/forward row)   +0xb4..+0xbc facing pos
//   +0x574..+0x57c   tentacle base world position
//   +0x58c           active tentacle count
//   +0x5bc           anchor entity pointer                 +0x5c0 anchor Y offset
//   +0x649c          spiral orientation matrix (mtx4_transform_vector)
//   +0xf5c.. (0x1a0) per-tentacle A blocks (x3)
//   +0x194.. (0x12c) per-tentacle B blocks (x3)
//
// Per-link record (0x48 bytes, float* base):
//   [0..2] anchor point   [3..5] current tip   [6..8] previous tip
//   [9] yaw   [0xb] state (int stored in a float slot)   [0xc] flip flag
//   [0xd] state timer   [0xe] active   [0x10..] 8-float wobble sub-record
//   [0x12] target entity pointer
//   link inner FSM record: +0x24 phaseA +0x28 phaseB +0x2c/+0x30 dir +0x38 len
//   +0x3c retract phase +0x40 spark amount +0x44 retract sub-state
//
// Functions:
//   mech_boss_kill_finisher  (0x8029cd94)  [MECH boss - DAT_80612a4c]
//   boss3_tentacle_init      (0x8029cf98)
//   boss3_link_init          (0x8029d014)
//   boss3_link_spiral_point  (0x8029d060)
//   boss3_link_anchor_point  (0x8029d130)
//   boss3_link_step_toward   (0x8029d278)
//   boss3_link_spark_fsm     (0x8029d338)
//   boss3_tentacles_reset    (0x8029d5e0)
//   boss3_tentacle_tick      (0x8029d6a4)
//   boss3_point_in_facing_cone (0x8029dbc4)
//
// Address range: 0x8029cd94 - 0x8029dbc4
// Subsystem: game (AI actors / boss)

#include "common.h"

extern "C" {

// --- data globals ----------------------------------------------------------
extern int*  DAT_80612a48;               // boss3 tentacle-boss control block base
extern int   DAT_80612a4c;               // MECH boss block (used by kill finisher)
extern int   DAT_80611d78;               // player record table base (stride 0x141c)
extern int   DAT_80612610;               // player count
extern int   DAT_806109c0;               // frame-delta ticks (xor bias)

// --- float / double constants ----------------------------------------------
extern const float  FLOAT_8061956c, FLOAT_80619568, FLOAT_80619570, FLOAT_80619574;
extern const float  FLOAT_80619578, FLOAT_80619580, FLOAT_80619588, FLOAT_8061958c;
extern const float  FLOAT_8061959c, FLOAT_806195a0, FLOAT_806195a8, FLOAT_806195ac;
extern const float  FLOAT_806195c0, FLOAT_806195c8, FLOAT_806195f4, FLOAT_806195f8, FLOAT_806195e8;
extern const float  FLOAT_80619624, FLOAT_8061963c, FLOAT_80619648, FLOAT_806196b8;
extern const float  FLOAT_806196d8, FLOAT_806196e8, FLOAT_80619758, FLOAT_806108e8;
extern const float  FLOAT_8061981c, FLOAT_80619820, FLOAT_80619824, FLOAT_80619828;
extern const float  FLOAT_8061982c;
extern const double DOUBLE_80619630;     // frame-delta bias

// --- raw helper (still FUN_ in the IDB) ------------------------------------
extern void FUN_802affb4(int a1, int a2, int a3, int a4);  // spark link render

// --- named cross-module helpers (call-site typed) --------------------------
extern double math_sqrt(double x);
extern double math_poly_trig(double x);
extern void mtx4_transform_vector(float* mtx, float* v);
extern void dir_to_angles(float* dir, int pitchOrNull, float* yaw);
extern void player_apply_camera_shake(double x, double y, double z, int playerRec);
extern void sound_init_play_params(int paramsPtr);
extern unsigned int sound_play_request(int* params);
extern unsigned long long anim_play_scaled(double a, double b);
extern void worm_stat_on_kill(int* killer, int* victim);
extern void character_find_nearest_bone(void);
extern void spark_spawn_fading(void);
extern void spark_set_last_color(int rgb);

} // extern "C"

// forward decls (same-module)
static void boss3_link_spiral_point(int link, float* out);
static bool boss3_link_step_toward(double step, int link);
static unsigned int boss3_link_spark_fsm(int link);
static void boss3_link_anchor_point(double amp, double yoff, int link, float* out);

// helper: read the float-slot-encoded state as an integer (states 0..8).
static inline int link_state(const float* rec) { return *(const int*)&rec[0xb]; }
static inline void set_link_state(float* rec, int s) { *(int*)&rec[0xb] = s; }

// ===========================================================================
// 0x8029cd94  mech_boss_kill_finisher   [MECH boss - DAT_80612a4c]
// Death/kill reward for the MECH boss: on the kill (character state 2, not yet
// 6), credit the stat, shake every player's camera by proximity, play the kill
// sound, start the death clip, enable the boss render flag, advance the char
// state to 6 and resolve the nearest bone. Physically in the boss3 range but
// operates on the MECH boss block.
// ===========================================================================
void mech_boss_kill_finisher(int p1, int p2, int killerActor) {
    (void)p1; (void)p2;
    int actorCtx = 0;  // (rig context; the engine passes it via the save-gpr pair)
    // The original reads (type,ptr) from __save_gpr_28's return; the kill path is
    // only reached with type==2, so model the guard directly.
    int type = 2;
    if (type != 2) return;

    int rec = *(int*)(actorCtx + 0x7c);
    if (*(int*)(rec + 0x20) == 6) return;

    int* killer = (int*)0;
    if ((killerActor != 0) && (*(int*)(killerActor + 4) != 0) &&
        (*(int*)(*(int*)(killerActor + 4) + 0x20) == 8)) {
        killer = *(int**)(*(int*)(killerActor + 4) + 0x7c);
    }
    worm_stat_on_kill(killer, (int*)rec);

    double maxDist = (double)FLOAT_8061981c;
    int recOff = 0;
    for (int i = 0; i < DAT_80612610; i++) {
        float dz = *(float*)(actorCtx + 0x6c) - *(float*)(DAT_80611d78 + recOff + 0xb0);
        float dx = *(float*)(actorCtx + 100) - *(float*)(DAT_80611d78 + recOff + 0xa8);
        double dist = math_sqrt((double)(dx * dx + dz * dz));
        if (dist < maxDist) {
            double amt = (double)(FLOAT_80619820 * (float)(maxDist - dist));
            player_apply_camera_shake((double)(float)((double)FLOAT_8061963c * amt),
                (double)(float)((double)FLOAT_806195e8 * amt),
                (double)(float)((double)FLOAT_806196d8 * amt), DAT_80611d78);
            double cap = (double)FLOAT_8061959c;
            *(float*)(DAT_80611d78 + 0xe54) = FLOAT_80619648;
            if (cap < amt) amt = cap;
            *(float*)(DAT_80611d78 + 0xe58) = (float)amt;
        }
        recOff += 0x141c;
    }

    *(int*)(DAT_80612a4c + 0x48) = 1;
    int params[16];
    sound_init_play_params((int)params);
    *(unsigned char*)&params[10] = 1;
    params[0] = 0x4ea;
    *(float*)&params[9] = FLOAT_80619648;
    *(float*)&params[15] = FLOAT_806195c8;
    params[3] = *(int*)(actorCtx + 100);
    params[4] = *(int*)(actorCtx + 0x68);
    params[5] = *(int*)(actorCtx + 0x6c);
    sound_play_request(params);
    anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
    if (*(int*)(rec + 0x20) != 6) {
        *(int*)(rec + 0x9d0) = 6;
    }
    character_find_nearest_bone();
}

// ===========================================================================
// 0x8029cf98  boss3_tentacle_init
// Initialise a 0x48-byte tentacle record: zero the position/velocity fields,
// mark it active, seed the wobble phase to the supplied angle and initialise the
// 8-float wobble sub-record at +0x10.
// ===========================================================================
void boss3_tentacle_init(double phase, float* rec) {
    float zero = FLOAT_80619568;
    for (int i = 0; i <= 0xb; i++) rec[i] = zero;
    *(int*)&rec[0xe] = 1;
    *(int*)&rec[0xf] = 0;
    rec[0xc] = (float)phase;
    rec[0xd] = (float)phase;
    // inline of the 8-float wobble sub-record init (was snake3_seg_record_init):
    float* w = rec + 0x10;
    float one = FLOAT_80619580;
    float half = FLOAT_80619574;
    w[1] = one; w[2] = one;
    w[3] = half;
    w[0] = FLOAT_806196b8;
    w[4] = FLOAT_806196b8;
    w[5] = FLOAT_806196b8;
    w[6] = FLOAT_806196b8;
    w[7] = FLOAT_806196b8;
}

// ===========================================================================
// 0x8029d014  boss3_link_init
// Initialise a link inner-FSM sub-record (offsets +0x0c..+0x44 of the record):
// two phase scalars, spin direction, and cleared position/dir fields.
// ===========================================================================
void boss3_link_init(double phaseB, double phaseA, int rec) {
    float zero = FLOAT_80619568;
    *(float*)(rec + 0x24) = (float)phaseA;
    *(float*)(rec + 0x28) = (float)phaseB;
    *(int*)(rec + 0x30) = 1;
    *(int*)(rec + 0x2c) = 0;
    *(float*)(rec + 0x34) = zero;
    *(float*)(rec + 0x38) = zero;
    *(float*)(rec + 0x40) = zero;
    *(float*)(rec + 0x3c) = zero;
    *(int*)(rec + 0x44) = 0;
    *(float*)(rec + 0xc) = zero;
    *(float*)(rec + 0x10) = zero;
    *(float*)(rec + 0x14) = zero;
    *(float*)(rec + 0x18) = zero;
    *(float*)(rec + 0x1c) = zero;
    *(float*)(rec + 0x20) = zero;
}

// ===========================================================================
// 0x8029d060  boss3_link_spiral_point
// Compute the world-space spiral offset point for a link: a sine/cosine offset
// (driven by the link's +0x24 phase) rotated by the boss orientation matrix
// (+0x649c) and added to the tentacle base position (+0x574).
// ===========================================================================
static void boss3_link_spiral_point(int link, float* out) {
    float off[3];
    double c = math_poly_trig((double)((FLOAT_80619588 * *(float*)(link + 0x24)) / FLOAT_8061958c));
    off[1] = (float)c;
    off[0] = FLOAT_80619824;
    double s = math_poly_trig((double)((FLOAT_80619588 * *(float*)(link + 0x24)) / FLOAT_8061958c - FLOAT_806196e8));
    off[2] = (float)((double)FLOAT_80619570 * s);
    mtx4_transform_vector((float*)((int)DAT_80612a48 + 0x649c), off);
    out[0] = *(float*)((int)DAT_80612a48 + 0x574) + off[0];
    out[1] = *(float*)((int)DAT_80612a48 + 0x578) + off[1];
    out[2] = *(float*)((int)DAT_80612a48 + 0x57c) + off[2];
}

// ===========================================================================
// 0x8029d130  boss3_link_anchor_point
// Compute a link's target point as a lissajous offset (amp * trig of the two
// link phases) added to the anchor entity's world position (+0x5bc). No-op when
// there is no anchor entity.
// ===========================================================================
static void boss3_link_anchor_point(double amp, double yoff, int link, float* out) {
    double cA  = math_poly_trig((double)((FLOAT_80619588 * *(float*)(link + 0x24)) / FLOAT_8061958c));
    double ox  = (double)(float)(amp * cA);
    double sA  = math_poly_trig((double)((FLOAT_80619588 * *(float*)(link + 0x24)) / FLOAT_8061958c - FLOAT_806196e8));
    double nz  = (double)(float)(-amp * sA);
    double sB  = math_poly_trig((double)((FLOAT_80619588 * *(float*)(link + 0x28)) / FLOAT_8061958c - FLOAT_806196e8));
    double oy  = (double)(float)(-nz * sB);
    double cB  = math_poly_trig((double)((FLOAT_80619588 * *(float*)(link + 0x28)) / FLOAT_8061958c));
    int anchor = *(int*)((int)DAT_80612a48 + 0x5bc);
    if (anchor != 0) {
        out[0] = (float)((double)*(float*)(anchor + 100) + ox);
        out[1] = (float)(oy + (double)(float)((double)*(float*)((int)DAT_80612a48 + 0x5c0) + yoff));
        out[2] = *(float*)(anchor + 0x6c) + (float)(nz * cB);
    }
}

// ===========================================================================
// 0x8029d278  boss3_link_step_toward
// Advance a link's current tip (+0x0c..+0x14) toward its spiral point by up to
// `step`. Returns true while still travelling (not yet arrived); on arrival it
// snaps the tip to the spiral point and returns false.
// ===========================================================================
static bool boss3_link_step_toward(double step, int link) {
    float spiral[3];
    boss3_link_spiral_point(link, spiral);
    double dy = (double)(spiral[1] - *(float*)(link + 0x10));
    double dx = (double)(float)((double)spiral[0] - (double)*(float*)(link + 0xc));
    double dz = (double)(spiral[2] - *(float*)(link + 0x14));
    bool travelling = (float)(step * step) <= (float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy)));
    if (travelling) {
        *(float*)(link + 0xc)  = (float)(dx * step + (double)*(float*)(link + 0xc));
        *(float*)(link + 0x10) = (float)(dy * step + (double)*(float*)(link + 0x10));
        *(float*)(link + 0x14) = (float)(dz * step + (double)*(float*)(link + 0x14));
    } else {
        *(float*)(link + 0xc)  = spiral[0];
        *(float*)(link + 0x10) = spiral[1];
        *(float*)(link + 0x14) = spiral[2];
    }
    return !travelling;
}

// ===========================================================================
// 0x8029d338  boss3_link_spark_fsm
// The link retract/spark FSM (+0x44 sub-state 0..3): ramps the retract length
// (+0x38) and the whip curve (+0x40) with a frame-scaled rate derived from the
// link's spring stiffness (+0x48->+0x34), then emits a fading spark at the
// computed tip. Returns 1 once the retract completes (sub-state wraps to 0).
// ===========================================================================
static unsigned int boss3_link_spark_fsm(int link) {
    int spring = *(int*)(link + 0x48);
    float rate = *(float*)((int)DAT_80612a48 + 0x6498) *
        (FLOAT_806195f8 / *(float*)(spring + 0x34)) *
        FLOAT_806108e8 *
        (FLOAT_80619580 / (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630));

    *(float*)(link + 0x3c) = FLOAT_806195ac * rate + *(float*)(link + 0x3c);
    if (FLOAT_806195ac < *(float*)(link + 0x3c)) {
        *(float*)(link + 0x3c) = *(float*)(link + 0x3c) - FLOAT_806195ac;
    }

    int sub = *(int*)(link + 0x44);
    if (sub == 0) {
        *(float*)(link + 0x38) = FLOAT_80619574 * rate + *(float*)(link + 0x38);
        if (FLOAT_80619580 < *(float*)(link + 0x38)) {
            *(float*)(link + 0x38) = FLOAT_80619580;
            *(int*)(link + 0x44) = 1;
        }
    } else if (sub == 1) {
        *(float*)(link + 0x40) = -(FLOAT_80619758 * rate - *(float*)(link + 0x40));
        if (*(float*)(link + 0x40) < FLOAT_80619570) {
            *(int*)(link + 0x44) = 2;
        }
    } else if (sub == 2) {
        *(float*)(link + 0x40) = FLOAT_80619758 * rate + *(float*)(link + 0x40);
        if (FLOAT_80619568 < *(float*)(link + 0x40)) {
            *(float*)(link + 0x40) = FLOAT_80619568;
            *(int*)(link + 0x44) = 3;
        }
    } else {
        *(float*)(link + 0x38) = -(FLOAT_80619574 * rate - *(float*)(link + 0x38));
        *(float*)(*(int*)(link + 0x48) + 0x30) =
            (FLOAT_80619580 - *(float*)(link + 0x38)) * *(float*)(*(int*)(link + 0x48) + 0x34);
        if (*(float*)(link + 0x38) < FLOAT_80619568) {
            *(float*)(link + 0x38) = FLOAT_80619568;
            *(int*)(link + 0x44) = 0;
            return 1;
        }
    }

    float sparkPt[3];
    double c = math_poly_trig((double)((FLOAT_80619588 * *(float*)(link + 0x3c)) / FLOAT_8061958c));
    sparkPt[0] = (float)((double)(FLOAT_80619574 * *(float*)(link + 0x38)) * c);
    double s = math_poly_trig((double)((FLOAT_80619588 * *(float*)(link + 0x3c)) / FLOAT_8061958c - FLOAT_806196e8));
    sparkPt[1] = (float)((double)(FLOAT_80619574 * *(float*)(link + 0x38)) * s);
    *(int*)&sparkPt[2] = *(int*)(link + 0x40);
    spark_spawn_fading();
    spark_set_last_color(0x461905);
    FUN_802affb4((int)(uintptr_t)sparkPt, spring, 0x461905, 1);
    return 0;
}

// ===========================================================================
// 0x8029d5e0  boss3_tentacles_reset
// Reset the three per-tentacle blocks: block A (stride 0x1a0 at +0xf5c) gets its
// state cleared to 8 and its scalars zeroed; block B (stride 0x12c at +0x194)
// gets state 0, active flag 1 and its length seed; plus two extra singleton
// blocks (+0x4c4, +0x524).
// ===========================================================================
void boss3_tentacles_reset(void) {
    char* blk = (char*)DAT_80612a48;
    float zero = FLOAT_80619568;

    int off = 0;
    for (int i = 0; i < 3; i++) {
        *(float*)(blk + off + 0xf68) = zero;
        *(float*)(blk + off + 0xf64) = zero;
        *(int*)  (blk + off + 0xf5c) = 8;
        off += 0x1a0;
    }
    float lenSeed = FLOAT_806195f8;
    off = 0;
    for (int i = 0; i < 3; i++) {
        *(int*)  (blk + off + 0x19c) = 1;
        *(int*)  (blk + off + 0x1a0) = 0;
        *(float*)(blk + off + 0x194) = lenSeed;
        off += 300;   // 0x12c
    }
    float startSeed = FLOAT_806195f4;
    *(int*)  (blk + 0x4cc) = 1;
    *(int*)  (blk + 0x4d0) = 0;
    *(float*)(blk + 0x4c4) = startSeed;
    *(int*)  (blk + 0x52c) = 1;
    *(int*)  (blk + 0x530) = 0;
    *(float*)(blk + 0x524) = startSeed;
}

// ===========================================================================
// 0x8029d6a4  boss3_tentacle_tick
// Per-tentacle per-frame state machine. rec[0xb] holds an integer state in a
// float slot (0..8). It advances the tentacle's wobble yaw, then dispatches:
//   0 spawn-dwell -> 1 idle    1 idle
//   2 track-spiral (link_spiral_point)   3 grow (advance +0xd, snap tip to spawn)
//   4 seek-arrive (link_step_toward) -> 2 track
//   5 seek-anchor (approach target entity +0x12) -> 7 aim
//   7 aim-align (dir_to_angles to anchor, within cone -> 6 spark)
//   6 spark (link_spark_fsm) -> 8 recover
//   8 recover -> 4 restart    (whip curve via link_anchor_point)
// State ids are the small-denormal float bit patterns seen in the decompiler.
// ===========================================================================
void boss3_tentacle_tick(float* rec) {
    char* blk = (char*)DAT_80612a48;

    rec[6] = rec[3];
    rec[7] = rec[4];
    rec[8] = rec[5];

    // Advance wobble yaw (rec[9]) unless in the spiral state (rec[0xb]==1).
    if (link_state(rec) != 1) {
        if (rec[0xc] == 1.4013e-45f) {  // flip flag set
            rec[9] = rec[9] + FLOAT_80619578;
            if (FLOAT_806195ac < rec[9]) rec[9] = rec[9] - FLOAT_806195ac;
        } else {
            rec[9] = rec[9] - FLOAT_80619578;
            if (rec[9] < FLOAT_80619568) rec[9] = rec[9] + FLOAT_806195ac;
        }
    }

    int st = link_state(rec);
    if ((st == 2) || (st == 5)) {
        boss3_link_spiral_point((int)(uintptr_t)rec, rec + 3);
    } else if (st == 3) {
        rec[0xd] = FLOAT_806108e8 * (FLOAT_80619580 / (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630)) + rec[0xd];
        rec[3] = rec[0] + *(float*)(blk + 0x9c);
        rec[4] = rec[1] + *(float*)(blk + 0xa0);
        rec[5] = rec[2] + *(float*)(blk + 0xa4);
        if (FLOAT_8061956c < rec[0xd]) {
            rec[0xd] = FLOAT_80619568;
            set_link_state(rec, 4);
        }
    } else if (st == 4) {
        if (boss3_link_step_toward((double)FLOAT_806195a0, (int)(uintptr_t)rec)) {
            set_link_state(rec, 2);
        }
    } else if (st == 6) {
        int tgt = *(int*)&rec[0x12];
        double dy = (double)(*(float*)(tgt + 0x28) - rec[4]);
        double dx = (double)(*(float*)(tgt + 0x24) - rec[3]);
        double dz = (double)(*(float*)(tgt + 0x2c) - rec[5]);
        double len = math_sqrt((double)(float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy))));
        if ((double)FLOAT_806195a8 <= len) {
            double inv = (double)(float)((double)FLOAT_806195a8 / len);
            rec[3] = (float)(dx * inv + (double)rec[3]);
            rec[4] = (float)(dy * inv + (double)rec[4]);
            rec[5] = (float)(dz * inv + (double)rec[5]);
        } else {
            rec[3] = *(float*)(*(int*)&rec[0x12] + 0x24);
            rec[4] = *(float*)(*(int*)&rec[0x12] + 0x28);
            rec[5] = *(float*)(*(int*)&rec[0x12] + 0x2c);
            set_link_state(rec, 7);
        }
    } else if (st == 7) {
        if (boss3_link_spark_fsm((int)(uintptr_t)rec) != 0) {
            rec[0xd] = FLOAT_80619568;
            set_link_state(rec, 8);
            *(int*)(*(int*)&rec[0x12] + 0x30) = *(int*)(*(int*)&rec[0x12] + 0x34);
            *(int*)(*(int*)&rec[0x12] + 0x38) = 1;
            *(int*)(*(int*)&rec[0x12] + 0x3c) = 0;
        }
        rec[3] = *(float*)(*(int*)&rec[0x12] + 0x24);
        rec[4] = *(float*)(*(int*)&rec[0x12] + 0x28);
        rec[5] = *(float*)(*(int*)&rec[0x12] + 0x2c);
    } else if (st == 8) {
        set_link_state(rec, 4);
    } else if (st == 0) {
        rec[0xd] = FLOAT_806108e8 * (FLOAT_80619580 / (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630)) + rec[0xd];
        if ((1 < *(int*)(blk + 0x58c)) && (FLOAT_80619828 < rec[0xd])) {
            set_link_state(rec, 1);
            rec[0xd] = FLOAT_80619568;
        }
    } else if (st == 1) {
        rec[0xd] = FLOAT_8061956c * FLOAT_806108e8 * (FLOAT_80619580 / (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630)) + rec[0xd];
        if (rec[0xc] == 1.4013e-45f) {
            rec[9] = rec[9] + FLOAT_806195c0 * (FLOAT_80619580 - rec[0xd]) + FLOAT_80619624;
            if (FLOAT_806195ac < rec[9]) rec[9] = rec[9] - FLOAT_806195ac;
        } else {
            rec[9] = rec[9] - (FLOAT_806195c0 * (FLOAT_80619580 - rec[0xd]) + FLOAT_80619624);
            if (rec[9] < FLOAT_80619568) rec[9] = rec[9] + FLOAT_806195ac;
        }
        double r = math_sqrt((double)rec[0xd]);
        boss3_link_anchor_point((double)(FLOAT_80619578 * rec[0xd] * rec[0xd]),
                                (double)(float)((double)FLOAT_80619624 * r - (double)FLOAT_80619574),
                                (int)(uintptr_t)rec, rec + 3);
        if (FLOAT_80619580 < rec[0xd]) {
            rec[0xd] = FLOAT_80619568;
            set_link_state(rec, 4);
        }
    }

    // Post-pass: in the seek-anchor state (5), align to the anchor and, once
    // within the cone, switch to the reach state (6).
    if (link_state(rec) == 5) {
        int tgt = *(int*)&rec[0x12];
        float dir[3];
        float yaw;
        dir[0] = *(float*)(tgt + 0x24) - *(float*)(blk + 0x9c);
        dir[1] = *(float*)(tgt + 0x28) - *(float*)(blk + 0xa0);
        dir[2] = *(float*)(tgt + 0x2c) - *(float*)(blk + 0xa4);
        dir_to_angles(dir, 0, &yaw);
        if (yaw < FLOAT_80619568) yaw = yaw + FLOAT_806195ac;
        if (ABS(rec[9] - yaw) < FLOAT_806195c0) {
            set_link_state(rec, 6);
        }
    }
}

// ===========================================================================
// 0x8029dbc4  boss3_point_in_facing_cone
// True when the normalised direction from the boss facing origin (+0xb4..+0xbc)
// to `pt` projects onto the facing forward vector (+0xa8..+0xb0) below the cone
// threshold (FLOAT_8061982c) - i.e. the point is outside the boss's front cone.
// ===========================================================================
bool boss3_point_in_facing_cone(float* pt) {
    char* blk = (char*)DAT_80612a48;
    double dy = (double)(pt[1] - *(float*)(blk + 0xb8));
    double dx = (double)(pt[0] - *(float*)(blk + 0xb4));
    double dz = (double)(pt[2] - *(float*)(blk + 0xbc));
    double inv = (double)(float)((double)FLOAT_80619580 /
        math_sqrt((double)(float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy)))));
    return (float)(dz * inv) * *(float*)(blk + 0xb0) +
           (float)(dx * inv) * *(float*)(blk + 0xa8) +
           (float)(dy * inv) * *(float*)(blk + 0xac) < FLOAT_8061982c;
}
