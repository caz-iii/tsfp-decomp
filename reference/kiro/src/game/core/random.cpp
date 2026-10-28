// Address block: 0x80130f8c - 0x8013116c | Batch: batch_356 | Date: 2026-09-04
//
// Core PRNG + small math/util helpers (contiguous block in main.dol).
//
// Contains:
//   - An edge-triggered toggle latch (DAT_8061202c)
//   - Game-time-to-float timestamp cache (DAT_80583220)
//   - A 64-bit LCG counter (DAT_806108f0/f4) advanced per some tick
//   - The 15-bit LCG game rand()/frand() used all over gameplay
//     (particles, gibs, HUD, blood types, etc.)
//   - rand-state push/pop (save/restore of the deterministic rand seed)
//   - A 0x800-entry sine lookup table builder (DAT_80583228)
//
// Non-matching decomp: behaviour preserved, types concretised.

#include "common.h"

// ---------------------------------------------------------------------------
// Per-file externs for raw Ghidra symbols referenced below.
// ---------------------------------------------------------------------------

// Edge-triggered toggle latch state: 0 = off, 1 = on, -1 = falling edge seen.
extern int DAT_8061202c;

// Cached game-time value stored as a float.
extern float DAT_80583220;

// 64-bit LCG counter (hi/lo). Advanced by rng_counter_advance().
extern uint DAT_806108f0; // high 32 bits
extern uint DAT_806108f4; // low 32 bits
extern int  DAT_80612038; // advance tick count

// 15-bit deterministic game rand() state and its saved copy.
extern uint DAT_8061203c; // live rand seed
extern uint DAT_80612040; // saved rand seed (for push/pop)

// frand() scale/bias constants (IEEE double-magic reconstruction).
extern float  FLOAT_80615ac8;  // 1.0f / 32768.0f scale factor
extern double DOUBLE_80615ad0; // 0x4330000000000000 magic bias

// Sine table build constants.
extern float  FLOAT_80615ad8;  // amplitude
extern float  FLOAT_80615adc;  // divisor (table span)
extern double DOUBLE_80615ae0; // index bias

// Destination sine lookup table: 0x800 floats.
extern float DAT_80583228[0x800];

// FUN_80390914: sin() (or equivalent trig) taking/returning a double.
extern double FUN_80390914(double x);

// Cached game time -> frame count (double). Named function in the IDB.
extern double game_time_to_frames(void);

// PPC register save/restore prologue helpers (compiler runtime).
extern void __save_gpr_28(void);
extern void __restore_gpr_28(void);

// ---------------------------------------------------------------------------
// Edge-triggered toggle latch.  Address: 0x80130f8c
//
// Rising edge (param goes non-zero while latch is 0) sets latch to 1.
// Falling edge (param goes zero while latch is on) sets latch to -1.
// ---------------------------------------------------------------------------
void toggle_latch_update(int active)
{
    if ((DAT_8061202c == 0) && (active != 0)) {
        DAT_8061202c = 1;
    }
    if (DAT_8061202c == 0) {
        return;
    }
    if (active != 0) {
        return;
    }
    DAT_8061202c = -1;
}

// Address: 0x80130fc8
// Returns true while the latch is engaged (state != 0).
bool toggle_latch_is_set(void)
{
    return (DAT_8061202c != 0);
}

// ---------------------------------------------------------------------------
// Game-time timestamp cache.  Address: 0x80130fd8
// Samples the current game time (in frames) and stores it as a float.
// ---------------------------------------------------------------------------
void game_time_snapshot(void)
{
    double frames = game_time_to_frames();
    DAT_80583220 = (float)frames;
}

// ---------------------------------------------------------------------------
// 64-bit LCG counter advance.  Address: 0x80131004
// Classic Numerical Recipes multiplier 0x343fd, increment 0x269ec3,
// carried across a 64-bit state (hi:DAT_806108f0, lo:DAT_806108f4).
// ---------------------------------------------------------------------------
void rng_counter_advance(void)
{
    unsigned long long lo64 = (unsigned long long)DAT_806108f4;
    uint next_lo = DAT_806108f4 * 0x343fd;
    DAT_806108f4 = next_lo + 0x269ec3;
    DAT_806108f0 = (uint)(lo64 * 0x343fd >> 0x20)
                 + DAT_806108f0 * 0x343fd
                 + (uint)(0xffd9613c < next_lo);
}

// Address: 0x8013104c
// Bumps the advance tick count then advances the 64-bit counter.
void rng_counter_tick(void)
{
    DAT_80612038 = DAT_80612038 + 1;
    rng_counter_advance();
}

// ---------------------------------------------------------------------------
// srand(seed).  Address: 0x80131078
// Resets the advance tick count, seeds the 64-bit counter and the 15-bit
// game rand state, and clears the saved-seed slot.
// ---------------------------------------------------------------------------
void game_srand(uint seed)
{
    DAT_80612038 = 0;
    DAT_806108f4 = seed + 1;
    DAT_806108f0 = (uint)(0xfffffffe < seed);
    DAT_8061203c = 0;
    DAT_80612040 = 0;
}

// ---------------------------------------------------------------------------
// rand().  Address: 0x8013109c
// 15-bit LCG: seed = seed*0x343fd + 0x269ec3; return (seed >> 16) & 0x7fff.
// This is the deterministic gameplay RNG used by particles, gibs, HUD, etc.
// ---------------------------------------------------------------------------
uint game_rand(void)
{
    DAT_8061203c = DAT_8061203c * 0x343fd + 0x269ec3;
    return (DAT_8061203c >> 0x10) & 0x7fff;
}

// ---------------------------------------------------------------------------
// frand().  Address: 0x801310c0
// Returns game_rand() scaled to a float in [0, 1) via the IEEE-double magic.
// ---------------------------------------------------------------------------
double game_frand(void)
{
    uint r = game_rand();
    return (double)(FLOAT_80615ac8
                    * (float)((double)CONCAT44(0x43300000, r) - DOUBLE_80615ad0));
}

// ---------------------------------------------------------------------------
// rand-state push/pop.  Address: 0x80131100
// mode == 0 : restore live seed from the saved slot (pop)
// mode != 0 : save live seed into the saved slot   (push)
// ---------------------------------------------------------------------------
void game_rand_state_save_restore(int save)
{
    if (save == 0) {
        DAT_8061203c = DAT_80612040;
        return;
    }
    DAT_80612040 = DAT_8061203c;
}

// ---------------------------------------------------------------------------
// Sine lookup table builder.  Address: 0x80131124
// Fills DAT_80583228[0..0x7ff] with amplitude*sin() over a normalised span.
// Built once at startup (called from game_tick_update path).
// ---------------------------------------------------------------------------
void sine_table_build(void)
{
    uint i;
    double amplitude;
    double divisor;
    double indexBias;
    double s;

    __save_gpr_28();
    amplitude = (double)FLOAT_80615ad8;
    divisor   = (double)FLOAT_80615adc;
    indexBias = DOUBLE_80615ae0;
    i = 0;
    do {
        double idx = (double)(float)((double)CONCAT44(0x43300000, i ^ 0x80000000) - indexBias);
        s = FUN_80390914((double)(float)(amplitude * (double)(float)(idx / divisor)));
        DAT_80583228[i] = (float)s;
        i = i + 1;
    } while ((int)i < 0x800);
    __restore_gpr_28();
}
