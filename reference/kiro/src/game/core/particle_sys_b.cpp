// =============================================================================
// particle_sys_b.cpp - the second bounded particle system ("field B").
//
// A double-buffered pool of DAT_80612a78 particles (0x2c = 44 bytes each) lives
// at two buffers DAT_80612a90 / DAT_80612a8c; DAT_80612a88 is the "active" write
// buffer and DAT_80612a84 the "back" buffer, swapped every tick. DAT_80612a7c is
// the live count, DAT_80612a80 the owning draw-context handle (frame-enter token).
//
// Per-particle layout (float offsets unless noted):
//   +0x00 pos.x   +0x04 pos.y   +0x08 pos.z
//   +0x0c vel.x   +0x10 vel.y   +0x14 vel.z
//   +0x18 life    +0x1c ext0    +0x20 colour(u32)  +0x24 matrix-ptr(int)
//   +0x28 ext1
//
// Functions (all subsystem: game):
//   * particle_sys_b_alloc      (0x802af304) - pick count (0x578 or 500) and
//     allocate the two 0x2c-stride buffers.
//   * particle_sys_b_set_owner  (0x802af384) - store the owning context handle.
//   * particle_sys_b_init       (0x802af38c) - seed all DAT_80612a78 particles at
//     a common origin with rng-jittered velocity/colour, mirror into the back
//     buffer.
//   * particle_sys_b_tick       (0x802af678) - gravity integration + lifetime
//     decay + live-compaction + buffer swap.
//   * particle_sys_b_tick_mtx   (0x802af834) - variant tick that re-derives each
//     particle's position from a per-particle matrix (offset 0x24).
//   * particle_sys_b_spawn      (0x802af9e4) - append one particle at a position
//     pointer with rng velocity/colour/life.
//   * particle_sys_b_spawn_xyz  (0x802afcd4) - append one particle from explicit
//     (x,y,z) with a different colour/velocity profile.
//   * particle_sys_b_spawn_burst(0x802affb4) - append `count` particles around
//     the current transform origin, all sharing a colour and matrix handle.
//
// Address range: 0x802af304 - 0x802b01e3
// Subsystem: game (particle system)
// =============================================================================

#include "common.h"

extern "C" {

extern int DAT_80612a78;   // configured pool capacity
extern int DAT_80612a7c;   // live particle count
extern int DAT_80612a80;   // owning context handle
extern int DAT_80612a84;   // back buffer base
extern int DAT_80612a88;   // active buffer base
extern int DAT_80612a8c;   // buffer B allocation
extern int DAT_80612a90;   // buffer A allocation

extern const float  FLOAT_806199dc;
extern const float  FLOAT_80619a50, FLOAT_80619a54, FLOAT_80619a58, FLOAT_80619a5c;
extern const float  FLOAT_80619a60, FLOAT_80619a64, FLOAT_80619a68, FLOAT_80619a6c;
extern const float  FLOAT_80619a70, FLOAT_80619a74, FLOAT_80619a80, FLOAT_80619a84;
extern const float  FLOAT_80619a88, FLOAT_80619a8c, FLOAT_80619a90, FLOAT_80619a94;
extern const float  FLOAT_80619a98;
extern const double DOUBLE_80619a78, DOUBLE_80619a58;

extern unsigned int rng_counter_tick(void);
extern int  mem_alloc(int size, int type);
extern void leaf_31e8(int dst, int src, int n);   // small memcpy

// frame-enter intrinsics
extern int  FUN_803826e8(void);
extern unsigned long long FUN_803826e4(void);
extern void FUN_80382730(void);
extern void FUN_80382734(void);
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);

} // extern "C"

// forward decls
void particle_sys_b_alloc(int mode);                    // 0x802af304
void particle_sys_b_set_owner(int owner);               // 0x802af384
void particle_sys_b_init(void);                         // 0x802af38c
void particle_sys_b_tick(void);                         // 0x802af678
void particle_sys_b_tick_mtx(void);                     // 0x802af834
void particle_sys_b_spawn(float* pos);                  // 0x802af9e4
void particle_sys_b_spawn_xyz(double x, double y, double z); // 0x802afcd4
void particle_sys_b_spawn_burst(int color, int count);  // 0x802affb4

// rng in [0,1): (rng_counter_tick() magic-double - bias) / range
static inline float rng_unit(void)
{
    u32 r = rng_counter_tick();
    return (float)((double)CONCAT44(0x43300000, r) - DOUBLE_80619a78) / FLOAT_80619a58;
}

// ---------------------------------------------------------------------------
// 0x802af304 - allocate the two particle buffers. mode 0 -> 1400 particles
// (0xf0a0 bytes each); otherwise 500 particles (22000 bytes each).
// ---------------------------------------------------------------------------
void particle_sys_b_alloc(int mode)
{
    if (mode == 0) {
        DAT_80612a78 = 0x578; // 1400
        DAT_80612a90 = mem_alloc(0xf0a0, 1);
        DAT_80612a8c = mem_alloc(0xf0a0, 1);
    } else {
        DAT_80612a78 = 500;
        DAT_80612a90 = mem_alloc(22000, 1);
        DAT_80612a8c = mem_alloc(22000, 1);
    }
}

// ---------------------------------------------------------------------------
// 0x802af384 - store the owning draw-context handle.
// ---------------------------------------------------------------------------
void particle_sys_b_set_owner(int owner)
{
    DAT_80612a80 = owner;
}

// ---------------------------------------------------------------------------
// 0x802af38c - seed the whole pool at a common origin with rng velocity/colour
// and mirror each entry into the back buffer.
// ---------------------------------------------------------------------------
void particle_sys_b_init(void)
{
    DAT_80612a80 = FUN_803826e8();
    DAT_80612a7c = 0;
    DAT_80612a88 = DAT_80612a90;
    DAT_80612a84 = DAT_80612a8c;

    const float origin = FLOAT_80619a50;
    const double velScale = (double)FLOAT_80619a54;
    const double velBiasXY = (double)FLOAT_80619a5c;
    const double velBiasX  = (double)FLOAT_80619a60;
    const double colR = (double)FLOAT_80619a64;
    const double colG = (double)FLOAT_80619a68;
    const double colB = (double)FLOAT_80619a6c;
    const double colA = (double)FLOAT_80619a70;
    const double lifeScale = (double)FLOAT_80619a74;

    int off = 0;
    for (int i = 0; i < DAT_80612a78; i++) {
        float* p = (float*)(DAT_80612a88 + off);
        p[0] = origin;
        p[1] = origin;
        p[2] = origin;
        p[3] = (float)(velScale * (double)rng_unit() - velBiasXY);
        p[4] = -(float)(velScale * (double)rng_unit() - velBiasX);
        p[5] = (float)(velScale * (double)rng_unit() - velBiasXY);
        p[6] = origin; // life

        p[7] = (float)(colR * (double)rng_unit() - colG);
        p[10] = rng_unit(); // ext (spin seed)

        double cg = colB * (double)rng_unit();
        double cb = colA * (double)rng_unit();
        u32 packed = ((u32)(int)(lifeScale * (double)rng_unit()) + 0x30) * 0x10000 |
                     ((u32)(int)cg + 0x10) |
                     (((u32)(int)cb + 0x10) << 8);
        *(u32*)(DAT_80612a88 + off + 0x20) = packed;

        leaf_31e8(DAT_80612a84 + off, DAT_80612a88 + off, 0x2c);
        off += 0x2c;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802af678 - per-frame update: gravity/velocity integration with a floor
// bounce, lifetime decay, then compact live particles into the back buffer and
// swap.
// ---------------------------------------------------------------------------
void particle_sys_b_tick(void)
{
    double floorY = (double)__save_gpr_28();
    float deadLife = FLOAT_80619a50;
    int liveOut = 0;
    int outOff = 0;

    int off = 0;
    for (int i = 0; i < DAT_80612a7c; i++, off += 0x2c) {
        float* p = (float*)(DAT_80612a88 + off);
        if (deadLife < p[6]) {
            p[1] += p[4]; // y += vy
            float drag = FLOAT_80619a80;
            if (floorY <= (double)p[1]) {
                if (FLOAT_80619a84 <= p[9]) {
                    p[0] += p[3];
                    p[2] += p[5];
                } else {
                    p[0] -= p[3];
                    p[2] -= p[5];
                }
            } else {
                p[0] = FLOAT_80619a80 * p[3] + p[0];
                p[1] = (float)floorY;
                p[2] = drag * p[5] + p[2];
            }
            p[6] -= FLOAT_80619a88; // life decay
        }
    }

    double liveFloor = (double)FLOAT_80619a50;
    int scanOff = 0;
    for (int i = 0; i < DAT_80612a7c; i++, scanOff += 0x2c) {
        if (liveFloor < (double)*(float*)(DAT_80612a88 + scanOff + 0x18)) {
            leaf_31e8(DAT_80612a84 + outOff, DAT_80612a88 + scanOff, 0x2c);
            liveOut++;
            outOff += 0x2c;
        }
    }

    int newActive = DAT_80612a84;
    DAT_80612a84 = DAT_80612a88;
    DAT_80612a88 = newActive;
    DAT_80612a7c = liveOut;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x802af834 - variant tick: particles that carry a matrix handle (p[9]) have
// their position re-derived from that matrix each frame; the rest integrate
// their y velocity. Then compact + swap as in particle_sys_b_tick.
// ---------------------------------------------------------------------------
void particle_sys_b_tick_mtx(void)
{
    __save_gpr_28();
    float lifeDecay = FLOAT_80619a5c;
    float deadLife = FLOAT_80619a50;
    int liveOut = 0;
    int outOff = 0;

    int off = 0;
    for (int i = 0; i < DAT_80612a7c; i++, off += 0x2c) {
        float* p = (float*)(DAT_80612a88 + off);
        if (deadLife < p[6]) {
            if (deadLife == p[7]) {
                p[1] += p[4];
            } else {
                float* m = (float*)(int)p[9]; // matrix pointer stored as float bits
                p[0] = m[6] * p[5] + m[3] * p[4] + m[0] * p[3] + m[9];
                p[1] = m[7] * p[5] + m[4] * p[4] + m[1] * p[3] + m[10];
                p[2] = m[8] * p[5] + m[5] * p[4] + m[2] * p[3] + m[11];
            }
        }
        p[6] -= lifeDecay;
    }

    double liveFloor = (double)FLOAT_80619a50;
    int scanOff = 0;
    for (int i = 0; i < DAT_80612a7c; i++, scanOff += 0x2c) {
        if (liveFloor < (double)*(float*)(DAT_80612a88 + scanOff + 0x18)) {
            leaf_31e8(DAT_80612a84 + outOff, DAT_80612a88 + scanOff, 0x2c);
            liveOut++;
            outOff += 0x2c;
        }
    }

    int newActive = DAT_80612a84;
    DAT_80612a84 = DAT_80612a88;
    DAT_80612a88 = newActive;
    DAT_80612a7c = liveOut;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x802af9e4 - append one particle at *pos with rng velocity/colour/life.
// No-op when the pool is (nearly) full.
// ---------------------------------------------------------------------------
void particle_sys_b_spawn(float* pos)
{
    if (DAT_80612a7c >= DAT_80612a78 - 1)
        return;

    int base = DAT_80612a88 + DAT_80612a7c * 0x2c;
    *(float*)(base + 0x00) = pos[0];
    *(float*)(base + 0x04) = pos[1];
    *(float*)(base + 0x08) = pos[2];

    *(float*)(base + 0x18) = -(FLOAT_80619a64 * rng_unit() - FLOAT_80619a8c);

    float cg = FLOAT_80619a6c * rng_unit();
    float cb = FLOAT_80619a70 * rng_unit();
    *(u32*)(base + 0x20) =
        ((u32)(int)(FLOAT_80619a74 * rng_unit()) + 0x30) * 0x10000 |
        ((u32)(int)cg + 0xc) |
        (((u32)(int)cb + 0x10) << 8);

    *(float*)(base + 0x1c) = FLOAT_80619a64 * rng_unit() - FLOAT_80619a68;
    *(float*)(base + 0x28) = rng_unit();
    *(float*)(base + 0x0c) = FLOAT_80619a54 * rng_unit() - FLOAT_80619a5c;
    *(float*)(base + 0x10) = -(FLOAT_80619a54 * rng_unit() - FLOAT_80619a60);
    *(float*)(base + 0x14) = FLOAT_80619a54 * rng_unit() - FLOAT_80619a5c;

    DAT_80612a7c++;
}

// ---------------------------------------------------------------------------
// 0x802afcd4 - append one particle from explicit (x,y,z) with a distinct
// colour/velocity profile.
// ---------------------------------------------------------------------------
void particle_sys_b_spawn_xyz(double x, double y, double z)
{
    if (DAT_80612a7c >= DAT_80612a78)
        return;

    int base = DAT_80612a88 + DAT_80612a7c * 0x2c;
    *(float*)(base + 0x00) = (float)x;
    *(float*)(base + 0x04) = (float)y;
    *(float*)(base + 0x08) = (float)z;

    *(float*)(base + 0x18) = -(FLOAT_80619a68 * rng_unit() - FLOAT_80619a8c);

    float cg = FLOAT_80619a74 * rng_unit();
    float cb = FLOAT_80619a90 * rng_unit();
    *(u32*)(base + 0x20) =
        ((u32)(int)(FLOAT_80619a70 * rng_unit()) + 0xc) * 0x10000 |
        ((u32)(int)cg + 0x30) |
        (((u32)(int)cb + 0x10) << 8);

    *(float*)(base + 0x1c) = FLOAT_80619a64 * rng_unit() - FLOAT_80619a68;
    *(float*)(base + 0x28) = rng_unit();
    *(float*)(base + 0x0c) = FLOAT_80619a54 * rng_unit() - FLOAT_80619a5c;
    *(float*)(base + 0x10) = FLOAT_80619a94 * rng_unit() + FLOAT_80619a54;
    *(float*)(base + 0x14) = FLOAT_80619a54 * rng_unit() - FLOAT_80619a5c;

    DAT_80612a7c++;
}

// ---------------------------------------------------------------------------
// 0x802affb4 - append `count` particles around the current transform origin,
// all sharing a colour word and a matrix handle (the current draw context).
// ---------------------------------------------------------------------------
void particle_sys_b_spawn_burst(int color, int count)
{
    unsigned long long ctx = FUN_803826e4();
    float* origin = (float*)(unsigned int)(ctx >> 0x20);

    if (DAT_80612a7c + count >= DAT_80612a78 - 1) {
        FUN_80382730();
        return;
    }

    const double spread = (double)FLOAT_80619a5c;
    const double centre = (double)FLOAT_80619a98;
    const double velR   = (double)FLOAT_80619a64;
    const double velB   = (double)FLOAT_80619a8c;

    for (int i = 0; i < count; i++) {
        int base = DAT_80612a88 + DAT_80612a7c * 0x2c;
        *(float*)(base + 0x0c) = (float)((double)(float)(spread * (double)rng_unit() + (double)origin[0]) - centre);
        *(float*)(base + 0x10) = (float)((double)(float)(spread * (double)rng_unit() + (double)origin[1]) - centre);
        *(float*)(base + 0x14) = (float)((double)(float)(spread * (double)rng_unit() + (double)origin[2]) - centre);
        *(float*)(base + 0x18) = -(float)(velR * (double)rng_unit() - velB);
        *(u32*)(base + 0x20) = (u32)color;
        *(float*)(base + 0x1c) = (float)velB;
        *(float*)(base + 0x28) = rng_unit();
        *(int*)(base + 0x24) = (int)ctx;
        DAT_80612a7c++;
    }
    FUN_80382730();
}
