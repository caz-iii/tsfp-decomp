// snake_boss_rope.cpp - segmented "snake"/spine boss trail-rope particle subsystem.
//
// This module manages a 500-entry "rope point" ring buffer plus a small set of
// attractor nodes hanging off the same per-boss global block as snake_boss_fsm
// (DAT_80612a58). Each rope point is a 0x20-byte record laid out as:
//   +0x00 pos.x  +0x04 pos.y  +0x08 pos.z   (float3 position)
//   +0x0c vel.x  +0x10 vel.y  +0x14 vel.z   (float3 velocity)
//   +0x18 life                              (float lifetime 0..1)
//   +0x1c userTag                           (u32 tag / owner)
// The 500-record array starts at DAT_80612a58 + 0; the ring write cursor lives
// at +16000 (0x3e80), the live-count at +0x3e84. A parallel 6-slot attractor/
// target table sits at +0x4a30 (stride 0x44) and a fire/aim probe FSM at +0x4160.
//
// The subsystem is an autonomous Verlet-style rope: points are integrated with
// gravity + a length/speed clamp (rsqrt Newton step), optionally collided against
// 6 planes (built from the DAT_803e05cc rodata block) and against nearby attractor
// nodes, then rendered by sampling live points back out with RNG jitter.
//
// Functions:
//   snake_rope_reset_state       (0x8028e37c): zero/seed the rope + attractor
//     arrays and the fire-probe FSM at boss (re)spawn.
//   snake_boss_alloc             (0x8028e69c): allocate the 0x4c7c-byte boss
//     control block, register it and run reset_state.
//   snake_rope_fire_probe        (0x8028e6d0): anim-phase-gated 3-state FSM that
//     charges then fires a rope/projectile toward the current target.
//   snake_rope_integrate_simple  (0x8028e904): per-frame integrate of 500 rope
//     points (gravity + speed clamp), no collision.
//   snake_rope_nearest_node      (0x8028ebcc): return the index (0..12) of the
//     attractor node nearest a point.
//   snake_rope_integrate_full    (0x8028ec58): full integrate: gravity + node
//     collision push against 6 planes + 6-slot angle/proximity targeting.
//   snake_rope_ring_push         (0x8028f210): push a point/dir pair into the
//     500-entry ring at the write cursor.
//   snake_rope_ring_reset        (0x8028f2e4): clear all point lifetimes and set
//     the write cursor to 499.
//   snake_rope_sample_point      (0x8028f324): walk the ring backward, pick the
//     Nth live point, interpolate toward it and add life^2-scaled RNG jitter.
//   snake_rope_sample_point_scaled(0x8028f5ec): same, jitter scaled by a
//     per-point (FLOAT_806195a0 * life) factor.
//
// Address range: 0x8028e37c - 0x8028f8ac
// Subsystem: game (AI actors / boss FX)

#include "common.h"

extern "C" {

// --- raw actor-context accessors (still FUN_ in the IDB) -------------------
extern int    FUN_803826e8(void);        // current actor entity record
extern void   FUN_80382734(void);        // tick epilogue
extern void   FUN_8028e37c(void);         // snake_rope_reset_state (self, pre-rename)
extern int    __save_gpr_28(void);        // compiler save-regs stub
extern void   __restore_gpr_28(void);     // compiler restore-regs stub

// --- scalar / data globals -------------------------------------------------
extern int   DAT_80612a58;               // snake-boss global control block base
extern int   DAT_80612614;               // current game/level mode id
extern int   DAT_80611b8c;               // attractor-node record table base
extern int   DAT_806109c0;               // frame delta ticks (signed, xor bias)
extern int   DAT_806108e4;               // frame delta ticks (2nd, xor bias)
extern int   DAT_80473200;               // 8-entry anim-pattern id table
extern unsigned int DAT_803e05cc;        // collision-plane table (rodata, 6 x 8 words)

// --- float / double constants (rodata, snake block) ------------------------
extern const float  FLOAT_8061956c;      // 0.0 velocity-clamp floor
extern const float  FLOAT_80619568;      // 0.0
extern const float  FLOAT_80619574;      // 0.5 (rng midpoint)
extern const float  FLOAT_80619578;      // spread / collision-push scale
extern const float  FLOAT_80619580;      // 1.0 (unit / life-max)
extern const float  FLOAT_80619588;      // 180.0
extern const float  FLOAT_8061958c;      // PI
extern const float  FLOAT_806195a0;      // per-point jitter scale
extern const float  FLOAT_806195a8;      // rope-point step increment
extern const float  FLOAT_806195ac;      // 2*PI angle wrap
extern const float  FLOAT_806195b0;      // -PI wrap lower bound
extern const float  FLOAT_806195e4;      // rng denominator
extern const float  FLOAT_806195f4;      // big const / activation flag value
extern const float  FLOAT_8061959c;      // gravity coeff (full integrate)
extern const float  FLOAT_80619608;      // gravity coeff (simple integrate)
extern const float  FLOAT_8061963c;      // fire projectile speed
extern const float  FLOAT_80619624;      // integrate accel bias A
extern const float  FLOAT_80619628;      // integrate accel bias B
extern const float  FLOAT_80619638;      // fire-phase threshold
extern const float  FLOAT_80619640;      // fire aim y-gate
extern const float  FLOAT_80619644;      // aim y-damp (simple)
extern const float  FLOAT_80619648;      // rsqrt Newton const 1.5
extern const float  FLOAT_8061964c;      // -1.0 (dead-life sentinel)
extern const float  FLOAT_80619650;      // aim y-damp (full)
extern const float  FLOAT_80619654;      // target dy hi gate
extern const float  FLOAT_80619658;      // target dy lo gate
extern const float  FLOAT_8061965c;      // aim angle window
extern const float  FLOAT_80619660;      // sample life floor
extern const double DOUBLE_80619610;     // rng bias
extern const double DOUBLE_80619630;     // frame-delta bias

// --- named cross-module helpers (call-site typed) --------------------------
extern double math_sqrt(double x);
extern double math_atan2(double y, double x);
extern unsigned int rng_counter_tick(void);

extern int  mem_alloc(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                      int size, int tag, int a11, int a12, int a13, int a14, int a15, int a16);
extern void FUN_802ad7d8(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                         int block, int flag, int a11, int a12, int a13, int a14, int a15, int a16);

extern double anim_primary_clip_phase(int animCtx);

extern void particle_cached_set_position(int* handleSlot, float* pos);
extern void particle_cached_set_drag(double drag, int* handleSlot);

extern int  player_ai_projectile_spawn_simple(double speed, double life, int type,
                                              int posPtr, int* dirPtr, int a6, int* actor,
                                              int a8, int a9, int a10, int a11, int a12);

} // extern "C"

// Reconstruct the "(double)CONCAT44(0x43300000, bits) - bias" int->float idiom.
static inline float sr_from_bits(unsigned int bits, double bias) {
    return (float)((double)CONCAT44(0x43300000, bits) - bias);
}

// Per-frame delta factor = deltaTicksA * (deltaTicksB) reconstructed from the
// xor-biased signed loads. Matches the compiler's CONCAT44(...)^0x80000000 idiom.
static inline float sr_dt(void) {
    float a = (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630);
    float b = (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806108e4 ^ 0x80000000)) - DOUBLE_80619630);
    return (float)((double)FLOAT_80619580 / (double)a) * b;
}

// ===========================================================================
// 0x8028e37c  snake_rope_reset_state
// Zero and seed the rope-point array, the 6-slot attractor table and the fire
// probe FSM. Called at boss (re)spawn and from snake_boss_alloc.
// ===========================================================================
void snake_rope_reset_state(void) {
    FUN_803826e8();
    int base = DAT_80612a58;
    float zeroF = FLOAT_80619628;
    float f1    = FLOAT_80619568;

    *(int*)(base + 16000)   = 0;      // ring write cursor
    *(int*)(base + 0x3e84)  = 0;      // live count
    *(int*)(base + 0x3e88)  = 0;
    *(float*)(base + 0x4274) = zeroF;
    *(float*)(base + 0x4bc8) = f1;
    *(int*)(base + 0x42a8)  = 0;
    *(int*)(base + 0x42ac)  = 0;
    *(float*)(base + 0x423c) = f1;
    *(int*)(base + 0x4be4)  = 0;
    *(int*)(base + 0x4be8)  = 1;
    *(float*)(base + 0x4bec) = f1;
    *(int*)(base + 0x4270)  = 0;
    *(int*)(base + 0x4bd8)  = 0;
    *(int*)(base + 0x4bd4)  = 0;
    *(int*)(base + 0x4bdc)  = 0;
    *(int*)(base + 0x4be0)  = 0;
    *(int*)(base + 0x4228)  = 0;
    *(int*)(base + 0x4230)  = 0;

    // Seed the target-angle cache with a scaled RNG value.
    unsigned int r = rng_counter_tick();
    float unit = sr_from_bits(r, DOUBLE_80619610) / FLOAT_806195e4;
    *(int*)(base + 0x422c) =
        (int)(FLOAT_806195f4 +
              (float)((double)CONCAT44(0x43300000, (unsigned)((int)(FLOAT_806195f4 * unit) ^ 0x80000000))
                      - DOUBLE_80619630));

    // Clear the 10 head-region rope anchors (8-byte stride, 2 floats each).
    float f2 = FLOAT_80619568;
    {
        int off = 0;
        for (int i = 0; i < 10; i++) {
            *(float*)(base + off + 0x417c) = f1;
            *(float*)(base + off + 0x4180) = f1;
            *(float*)(base + off + 0x41cc) = f1;
            *(float*)(base + off + 0x41d0) = f1;
            off += 8;
        }
    }

    *(float*)(base + 0x4164) = FLOAT_80619568;
    f1 = FLOAT_80619628;
    *(float*)(base + 0x4168) = f2;
    *(float*)(base + 0x416c) = f2;
    *(float*)(base + 0x4170) = f2;
    *(float*)(base + 0x4174) = f2;
    *(float*)(base + 0x4178) = f2;
    *(int*)(base + 0x4158)  = 0;
    *(int*)(base + 0x421c)  = 0;
    *(int*)(base + 0x4220)  = 0;
    *(int*)(base + 0x4234)  = 0;
    *(int*)(base + 0x4240)  = 0;
    *(int*)(base + 0x4244)  = 0;
    *(int*)(base + 0x4248)  = 0;
    *(float*)(base + 0x4238) = f2;
    *(float*)(base + 0x424c) = f2;
    *(float*)(base + 0x4250) = f2;
    *(float*)(base + 0x4254) = f2;
    *(float*)(base + 0x4258) = f2;
    *(float*)(base + 0x425c) = f2;
    *(float*)(base + 0x4260) = f2;
    *(float*)(base + 0x4264) = f2;
    *(float*)(base + 17000)  = f2;
    *(float*)(base + 0x426c) = f2;

    // Clear the 6-slot attractor/target table (stride 0x44).
    {
        int off = 0;
        for (int i = 0; i < 6; i++) {
            *(int*)(base + off + 0x4a30)   = 0;
            *(int*)(base + off + 0x4a34)   = -1;
            *(float*)(base + off + 0x4a48) = f2;
            *(int*)(base + off + 0x4a54)   = 0;
            *(float*)(base + off + 0x4a4c) = FLOAT_806195f4;  // fVar3 in decomp (seeded above)
            *(float*)(base + off + 0x4a50) = f2;
            *(int*)(base + off + 0x4a58)   = 1;
            *(float*)(base + off + 19000)  = f2;
            *(float*)(base + off + 0x4a3c) = f2;
            *(float*)(base + off + 0x4a40) = f2;
            *(float*)(base + off + 0x4a44) = f2;
            *(float*)(base + off + 0x4a5c) = f2;
            *(float*)(base + off + 0x4a70) = f1;
            *(float*)(base + off + 0x4a60) = f2;
            *(int*)(base + off + 0x4a54)   = 0;
            *(int*)(base + off + 0x4a64)   = 0;
            off += 0x44;
        }
    }

    FUN_80382734();
}

// ===========================================================================
// 0x8028e69c  snake_boss_alloc
// Allocate the 0x4c7c-byte boss control block, register it via FUN_802ad7d8,
// then run the rope reset. The many params are the pass-through allocator args.
// ===========================================================================
void snake_boss_alloc(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                      int a11, int a12, int a13, int a14, int a15, int a16) {
    int flag = 1;
    DAT_80612a58 = mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8, 0x4c7c, 1,
                             a11, a12, a13, a14, a15, a16);
    FUN_802ad7d8(a1, a2, a3, a4, a5, a6, a7, a8, DAT_80612a58, flag,
                 a11, a12, a13, a14, a15, a16);
    FUN_8028e37c();
}

// ===========================================================================
// 0x8028e6d0  snake_rope_fire_probe
// Anim-phase-gated fire FSM (state at base+0x4160): when the boss's primary clip
// reaches FLOAT_80619638 in a firing state, aim a rope/projectile toward the
// currently selected attractor slot (base+0x4158) then spawn it. Reads the
// incoming float3 aim point from the saved GPR frame (compiler save/restore).
// ===========================================================================
void snake_rope_fire_probe(void) {
    unsigned long long frame = (unsigned long long)__save_gpr_28();
    int base   = DAT_80612a58;
    int hi      = (int)(frame >> 0x20);
    float* aim  = (float*)(unsigned)frame;
    int* actor  = *(int**)(hi + 0x7c);

    int firing = ((DAT_80612614 == 1)  && ((&DAT_80473200)[actor[0x277]] == 6)) ||
                 ((DAT_80612614 == 0x11) && (actor[8] == 6));

    if (firing) {
        double phase = anim_primary_clip_phase(*(int*)(hi + 4));
        int state = *(int*)(base + 0x4160);
        if (state == 0) {
            if ((double)FLOAT_80619638 <= phase) {
                int slot = base + *(int*)(base + 0x4158) * 0xc;
                float dy = *(float*)(slot + 0x4168) - aim[1];
                float dx = *(float*)(slot + 0x4164) - aim[0];
                float dz = *(float*)(slot + 0x416c) - aim[2];
                double len = math_sqrt((double)(float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy))));
                float inv = (float)((double)FLOAT_80619580 / len);
                *(float*)(base + 0x413c) = aim[0];
                *(float*)(base + 0x4140) = aim[1];
                *(float*)(base + 0x4144) = aim[2];
                *(float*)(base + 0x4148) = dx * inv;
                *(float*)(base + 0x414c) = dy * inv;
                *(float*)(base + 0x4150) = dz * inv;
                int h = *(int*)(base + 0x4154);
                particle_cached_set_drag((double)FLOAT_80619568, &h);
                *(int*)(base + 0x4160) = 1;
            } else {
                *(float*)(base + 0x415c) = (float)(phase * (double)FLOAT_80619628);
                int hpos = *(int*)(base + 0x4154);
                particle_cached_set_position(&hpos, aim);
                int hdrag = *(int*)(base + 0x4154);
                particle_cached_set_drag((double)*(float*)(base + 0x415c), &hdrag);
            }
        } else if (state == 1) {
            int slot = base + *(int*)(base + 0x4158) * 0xc;
            player_ai_projectile_spawn_simple(
                (double)FLOAT_8061963c, (double)FLOAT_806195a8, 0x34, base + 0x413c,
                (int*)(base + 0x4148), slot, actor, 2, 1, *(int*)(slot + 0x34), 0xe, 0xc);
            int h = *(int*)(base + 0x4154);
            particle_cached_set_drag((double)FLOAT_80619568, &h);
            *(int*)(base + 0x4160) = 2;
        }
    } else {
        *(float*)(base + 0x415c) = FLOAT_80619568;
        *(int*)(base + 0x4160)  = 2;
        int h = *(int*)(base + 0x4154);
        particle_cached_set_drag((double)FLOAT_80619568, &h);
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x8028e904  snake_rope_integrate_simple
// Advance all 500 rope points by one frame: age the life field, integrate
// position by velocity*dt, apply a downward accel while alive, then clamp the
// speed with a single rsqrt Newton step. Points past life>=1 are marked dead.
// ===========================================================================
void snake_rope_integrate_simple(void) {
    __save_gpr_28();
    int base  = DAT_80612a58;
    float lifeMax = FLOAT_80619580;
    *(int*)(base + 0x3e84) = 0;

    int off = 0;
    for (int i = 0; i < 500; i++) {
        float* p = (float*)(base + off);
        float dt = sr_dt();
        if ((double)lifeMax <= (double)p[6]) {
            p[6] = FLOAT_8061964c;                    // dead sentinel
        } else {
            p[6] = (float)((double)FLOAT_80619608 * (double)((float)((double)lifeMax / (double)dt) * dt)
                           + (double)p[6]);
            p[0] += p[3] * dt;
            p[1] += p[4] * dt;
            p[2] += p[5] * dt;
            if (FLOAT_8061956c < p[6]) {
                p[4] += dt;                            // gravity on y-velocity
            }
            float vy = p[4];
            if ((vy < FLOAT_80619568) && (p[1] < FLOAT_80619640)) {
                double mag = math_sqrt((double)(p[5]*p[5] + vy*vy + p[3]*p[3]));
                float c15 = FLOAT_80619648;
                float half = FLOAT_80619574;
                p[4] = FLOAT_80619644 * p[4];
                float vx = p[3];
                float sumSq = p[5]*p[5] + vx*vx + p[4]*p[4];
                float rs = 1.0f / (float)math_sqrt((double)sumSq);
                float t = half * sumSq;
                rs = rs * -(t * rs * rs - c15);
                float scale = (float)(mag * (double)(rs * -(t * rs * rs - c15)));
                p[3] = vx * scale;
                p[4] = p[4] * scale;
                p[5] = p[5] * scale;
            }
            *(int*)(base + 0x3e84) += 1;
        }
        off += 0x20;
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x8028ebcc  snake_rope_nearest_node
// Return the index (0..12) of the attractor node nearest 'pt'. Node records live
// in the DAT_80611b8c table indexed by base+0x4bf0[i] (0x20-byte stride,
// position at +0x0c). 13 candidate slots.
// ===========================================================================
int snake_rope_nearest_node(float* pt) {
    int best = 0;
    float bestD = FLOAT_8061964c;   // starts as -1.0; first real candidate wins
    int slotOff = 0;
    for (int i = 0; i < 13; i++) {
        int rec = DAT_80611b8c + *(int*)(DAT_80612a58 + slotOff + 0x4bf0) * 0x20;
        float dx = *(float*)(rec + 0xc)  - pt[0];
        float dy = *(float*)(rec + 0x10) - pt[1];
        float dz = *(float*)(rec + 0x14) - pt[2];
        float d = dz*dz + dx*dx + dy*dy;
        if (d < bestD) {
            best = i;
            bestD = d;
        }
        slotOff += 4;
    }
    return best;
}

// ===========================================================================
// 0x8028ec58  snake_rope_integrate_full
// Full rope integrate: like the simple variant but with a velocity-dependent
// accel term, collision against 6 planes copied from the DAT_803e05cc rodata
// block, per-point node collision push (keeps points off the nearest attractor),
// and a trailing 6-slot angle/proximity targeting pass that flags slots whose
// bearing to the boss head is within a window and which have a rope point nearby.
// ===========================================================================
void snake_rope_integrate_full(void) {
    // 6 collision planes (each 8 words) copied off the rodata block; only the
    // x/z components (offsets +0, +8 within each 0xc-stride entry) are read below.
    float planes[12];
    {
        unsigned int* src = &DAT_803e05cc;
        unsigned int* dst = (unsigned int*)planes;
        for (int i = 0; i < 6; i++) {
            dst[0] = src[1];
            dst[1] = src[2];
            src += 2;
            dst += 2;
        }
    }

    FUN_803826e8();
    int base = DAT_80612a58;
    float lifeMax = FLOAT_80619580;
    *(int*)(base + 0x3e84) = 0;

    int off = 0;
    for (int i = 0; i < 500; i++) {
        float* p = (float*)(base + off);
        float dt = sr_dt();
        float accelBiasB = FLOAT_80619628;
        float accelBiasA = FLOAT_80619624;
        if ((double)lifeMax <= (double)p[6]) {
            p[6] = FLOAT_8061964c;
        } else {
            float accel = accelBiasA * (float)((double)lifeMax - (double)p[6]) + accelBiasB;
            p[6] = (float)((double)FLOAT_8061959c * (double)((float)((double)lifeMax / (double)dt) * dt)
                           + (double)p[6]);
            p[0] += p[3] * dt * accel;
            p[1] += p[4] * dt * accel;
            p[2] += p[5] * dt * accel;
            if (FLOAT_8061956c < p[6]) {
                p[4] += dt;
            }
            {
                float vx = p[3];
                float sumSq0 = p[5]*p[5] + vx*vx + p[4]*p[4];
                if ((p[4] < FLOAT_80619568) &&
                    (p[1] < *(float*)(DAT_80611b8c +
                                      *(int*)(base + (int)p[7]*4 + 0x4bf0) * 0x20 + 0x10))) {
                    float c15 = FLOAT_80619648;
                    float unit = FLOAT_80619580;
                    float half = FLOAT_80619574;
                    p[4] = FLOAT_80619650 * p[4];
                    float vx2 = p[3];
                    float sumSq = p[5]*p[5] + vx2*vx2 + p[4]*p[4];
                    float rs = 1.0f / (float)math_sqrt((double)sumSq);
                    float t = half * sumSq;
                    rs = rs * -(t * rs * rs - c15);
                    float scale = unit * rs * -(t * rs * rs - c15);
                    p[3] = vx2 * scale;
                    p[4] = p[4] * scale;
                    p[5] = p[5] * scale;
                }
                (void)sumSq0;
            }
            // Collision push against the 6 planes (x/z only).
            int pl = 0;
            for (int c = 0; c < 4; c++) {
                float pz = p[2] - *(float*)((int)planes + pl + 8);
                float px = p[0] - *(float*)((int)planes + pl);
                float d2 = (float)(px*px + (double)(float)(pz*pz));
                if (d2 < (double)FLOAT_80619578) {
                    p[6] = p[6] + FLOAT_806195a8;
                    double d = math_sqrt((double)d2);
                    double push = (double)FLOAT_80619578;
                    double nx = (double)(float)(px * (double)(float)((double)FLOAT_80619580 / d));
                    double nz = (double)(float)(pz * (double)(float)((double)FLOAT_80619580 / d));
                    p[0] = (float)(push * nx + (double)*(float*)((int)planes + pl));
                    p[2] = (float)(push * nz + (double)*(float*)((int)planes + pl + 8));
                    double rvz = -(double)p[5];
                    double rvx = -(double)p[3];
                    double rlen = math_sqrt((double)(float)(rvx*rvx + (double)(float)(rvz*rvz)));
                    rvz = (double)(float)(rvz * (double)(float)((double)FLOAT_80619580 / rlen));
                    rvx = (double)(float)(rvx * (double)(float)((double)FLOAT_80619580 / rlen));
                    double refl = (double)(FLOAT_80619578 * (float)(nx * rvx + (double)(float)(nz * rvz)));
                    p[3] = (float)(nx * refl - rvx);
                    p[5] = (float)(nz * refl - rvz);
                    break;
                }
                pl += 0xc;
            }
            *(int*)(base + 0x3e84) += 1;
        }
        off += 0x20;
    }

    // Trailing 6-slot targeting pass: for each active attractor slot, compute the
    // bearing of its record to the boss head (record at base+0x4bd0), and if the
    // wrapped angle is within the window and a rope point is nearby, flag it.
    int si = 0;
    int slotOff = 0;
    for (;;) {
        int rec = *(int*)(base + slotOff + 0x4a30);
        if (rec != 0) {
            int head = *(int*)(base + 0x4bd0);
            double dyRec = (double)(*(float*)(rec + 100) - *(float*)(head + 100));
            double ang;
            if ((dyRec < (double)FLOAT_80619568) || ((double)FLOAT_80619654 <= dyRec)) {
                if (((double)FLOAT_80619568 <= dyRec) || (dyRec <= (double)FLOAT_80619658)) {
                    double a = (double)math_atan2(dyRec,
                                  (double)(*(float*)(rec + 0x6c) - *(float*)(head + 0x6c)));
                    ang = (double)((FLOAT_8061958c * (float)a) / FLOAT_80619588);
                } else {
                    ang = (double)FLOAT_8061958c;
                }
            } else {
                ang = (double)FLOAT_8061958c;
            }
            if (ang < (double)FLOAT_80619568) {
                ang = (double)(float)(ang + (double)FLOAT_806195ac);
            }
            float rel = (float)(ang - (double)*(float*)(head + 0x70));
            for (; rel <= FLOAT_806195b0; rel += FLOAT_806195ac) {}
            for (; FLOAT_8061958c < rel; rel -= FLOAT_806195ac) {}
            if (ABS(rel) < FLOAT_8061965c) {
                int po = 0;
                for (int k = 0; k < 500; k++) {
                    float* p = (float*)(base + po);
                    if (p[6] < FLOAT_80619580) {
                        float dz = *(float*)(rec + 0x6c) - p[2];
                        float dx = *(float*)(rec + 100) - p[0];
                        if (dx*dx + dz*dz < FLOAT_80619580) {
                            *(float*)(base + slotOff + 0x4a70) = FLOAT_806195f4;
                            break;
                        }
                    }
                    po += 0x20;
                }
            }
        }
        si++;
        slotOff += 0x44;
        if (5 < si) {
            FUN_80382734();
            return;
        }
    }
}

// ===========================================================================
// 0x8028f210  snake_rope_ring_push
// Push a point (param_1) + direction (param_2) pair into the 500-entry ring at
// the write cursor (base+16000), tagging life=1.0 for the new point and -1.0 for
// the next slot (as the ring terminator). When param_3==0 this only advances the
// cursor past a live terminator (a no-op "close current run").
// ===========================================================================
void snake_rope_ring_push(float* pos, float* dir, int emit, unsigned int tag) {
    float dead = FLOAT_8061964c;
    float alive = FLOAT_80619568;
    int base = DAT_80612a58;
    if (emit != 0) {
        *(int*)(base + 16000) = (*(int*)(base + 16000) + 1) % 500;
        int cur = *(int*)(base + 16000);
        int rec = base + cur * 0x20;
        *(float*)(rec + 0x00) = pos[0];
        *(float*)(rec + 0x04) = pos[1];
        *(float*)(rec + 0x08) = pos[2];
        *(float*)(rec + 0x0c) = dir[0];
        *(float*)(rec + 0x10) = dir[1];
        *(float*)(rec + 0x14) = dir[2];
        *(unsigned int*)(rec + 0x1c) = tag;
        *(float*)(rec + 0x18) = alive;
        *(float*)(base + ((cur + 1) % 500) * 0x20 + 0x18) = dead;
        return;
    }
    if (dead == *(float*)(base + *(int*)(base + 16000) * 0x20 + 0x18)) {
        return;
    }
    *(int*)(base + 16000) = (*(int*)(base + 16000) + 1) % 500;
}

// ===========================================================================
// 0x8028f2e4  snake_rope_ring_reset
// Mark every rope point dead (life = -1.0), park the write cursor at 499 and
// clear the live count.
// ===========================================================================
void snake_rope_ring_reset(void) {
    float dead = FLOAT_8061964c;
    int off = 0;
    for (int i = 0; i < 500; i++) {
        *(float*)(DAT_80612a58 + off + 0x18) = dead;
        off += 0x20;
    }
    *(int*)(DAT_80612a58 + 16000)  = 499;
    *(int*)(DAT_80612a58 + 0x3e84) = 0;
}

// ===========================================================================
// 0x8028f324  snake_rope_sample_point
// Walk the ring backward from the write cursor, skipping runs of live points,
// and pick a randomly-offset live point (index chosen from live-count * rng).
// Interpolate the output toward that point's stored source (in the saved-GPR
// float3) and add RNG jitter scaled by the point's life squared.
// ===========================================================================
void snake_rope_sample_point(float* out) {
    unsigned int r = rng_counter_tick();
    double lerp = (double)(sr_from_bits(r, DOUBLE_80619610) / FLOAT_806195e4);
    r = rng_counter_tick();
    float zero = FLOAT_80619568;
    int wraps = 0;
    bool inRun = false;

    // Incoming source point carried in the compiler's saved FP regs.
    double srcX = 0.0, srcY = 0.0, srcZ = 0.0;

    int pick = (int)((float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(DAT_80612a58 + 0x3e84) ^ 0x80000000))
                             - DOUBLE_80619630)
                     * (sr_from_bits(r, DOUBLE_80619610) / FLOAT_806195e4)
                     + FLOAT_80619574);
    int start = (*(int*)(DAT_80612a58 + 16000) + 1) % 500;

    if (*(unsigned int*)(DAT_80612a58 + 0x3e84) == 0) {
        out[0] = FLOAT_80619568;
        out[1] = zero;
        out[2] = zero;
        return;
    }

    int idx = start;
    do {
        int rec = idx * 0x20;
        float* p = (float*)(DAT_80612a58 + rec);
        float life = p[6];
        if ((double)FLOAT_80619580 <= (double)life) {
            inRun = false;
        } else {
            pick -= 1;
            if (inRun && (pick < 0) && ((double)FLOAT_80619660 < (double)life)) {
                out[0] = (float)((double)(float)(srcX - (double)*(float*)(DAT_80612a58 + rec)) * lerp
                                 + (double)*(float*)(DAT_80612a58 + rec));
                out[1] = (float)((double)(float)(srcY - (double)p[1]) * lerp
                                 + (double)*(float*)(DAT_80612a58 + rec + 4));
                out[2] = (float)((double)(float)(srcZ - (double)p[2]) * lerp
                                 + (double)*(float*)(DAT_80612a58 + rec + 8));
                unsigned int r2 = rng_counter_tick();
                double jx = (double)(FLOAT_80619578 *
                    ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80619610) / FLOAT_806195e4 - FLOAT_80619574));
                unsigned int r3 = rng_counter_tick();
                double jy = (double)(FLOAT_80619578 *
                    ((float)((double)CONCAT44(0x43300000, r3) - DOUBLE_80619610) / FLOAT_806195e4 - FLOAT_80619574));
                unsigned int r4 = rng_counter_tick();
                float denom = FLOAT_806195e4;
                float spread = FLOAT_80619578;
                float half = FLOAT_80619574;
                double life2 = (double)(float)(life * life);
                double rz = (double)CONCAT44(0x43300000, r4) - DOUBLE_80619610;
                out[0] += (float)(jx * life2);
                out[1] += (float)(jy * life2);
                out[2] += (float)((double)(spread * ((float)rz / denom - half)) * life2);
                return;
            }
            srcX = (double)p[0];
            inRun = true;
            srcY = (double)p[1];
            srcZ = (double)p[2];
        }
        idx = (idx + 1) % 500;
        if (start == idx) {
            wraps++;
        }
    } while (wraps < 2);
}

// ===========================================================================
// 0x8028f5ec  snake_rope_sample_point_scaled
// Identical walk to snake_rope_sample_point, but the jitter magnitude is scaled
// by (FLOAT_806195a0 * life) instead of life alone (thicker jitter for younger
// points). Life is read as a plain float here.
// ===========================================================================
void snake_rope_sample_point_scaled(float* out) {
    unsigned int r = rng_counter_tick();
    double lerp = (double)(sr_from_bits(r, DOUBLE_80619610) / FLOAT_806195e4);
    r = rng_counter_tick();
    float zero = FLOAT_80619568;
    int wraps = 0;
    bool inRun = false;

    double srcX = 0.0, srcY = 0.0, srcZ = 0.0;

    int pick = (int)((float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(DAT_80612a58 + 0x3e84) ^ 0x80000000))
                             - DOUBLE_80619630)
                     * (sr_from_bits(r, DOUBLE_80619610) / FLOAT_806195e4)
                     + FLOAT_80619574);
    int start = (*(int*)(DAT_80612a58 + 16000) + 1) % 500;
    int idx = start;

    if (*(unsigned int*)(DAT_80612a58 + 0x3e84) == 0) {
        out[0] = FLOAT_80619568;
        out[1] = zero;
        out[2] = zero;
        return;
    }

    do {
        int rec = idx * 0x20;
        float* p = (float*)(DAT_80612a58 + rec);
        float life = p[6];
        if (FLOAT_80619580 <= life) {
            inRun = false;
        } else {
            pick -= 1;
            if (inRun && (pick < 0) && (FLOAT_80619660 < life)) {
                double lifeScaled = (double)(FLOAT_806195a0 * life);
                out[0] = (float)((double)(float)(srcX - (double)*(float*)(DAT_80612a58 + rec)) * lerp
                                 + (double)*(float*)(DAT_80612a58 + rec));
                out[1] = (float)((double)(float)(srcY - (double)p[1]) * lerp
                                 + (double)*(float*)(DAT_80612a58 + rec + 4));
                out[2] = (float)((double)(float)(srcZ - (double)p[2]) * lerp
                                 + (double)*(float*)(DAT_80612a58 + rec + 8));
                unsigned int r2 = rng_counter_tick();
                double jx = (double)(FLOAT_80619578 *
                    ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80619610) / FLOAT_806195e4 - FLOAT_80619574));
                unsigned int r3 = rng_counter_tick();
                double jy = (double)(FLOAT_80619578 *
                    ((float)((double)CONCAT44(0x43300000, r3) - DOUBLE_80619610) / FLOAT_806195e4 - FLOAT_80619574));
                unsigned int r4 = rng_counter_tick();
                float denom = FLOAT_806195e4;
                float spread = FLOAT_80619578;
                float half = FLOAT_80619574;
                double life2 = (double)(float)(lifeScaled * lifeScaled);
                double rz = (double)CONCAT44(0x43300000, r4) - DOUBLE_80619610;
                out[0] += (float)(jx * life2);
                out[1] += (float)(jy * life2);
                out[2] += (float)((double)(spread * ((float)rz / denom - half)) * life2);
                return;
            }
            srcX = (double)p[0];
            inRun = true;
            srcY = (double)p[1];
            srcZ = (double)p[2];
        }
        idx = (idx + 1) % 500;
        if (start == idx) {
            wraps++;
        }
    } while (wraps < 2);
}
