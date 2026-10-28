// switch_object_system.cpp - Switch/gate object registry + per-frame state machine.
//
// A level's switch/gate objects (doors, timed gates, activation panels) are held
// in a fixed table of up to 0x12 records (0x20 stride) based at DAT_805cb938:
//   +0x00 id, +0x04 <flags>, +0x08 mask, +0x0c payload, +0x1c option,
//   +0x10 linked-switch ptr, +0x14 state (0=closed,1=open,2=held-open),
//   +0x18 open timer. DAT_806129d0 is the live record count.
// switch_object_register adds records at level spawn; switch_object_tick_all
// advances open timers and auto-closes; switch_object_set_state opens/closes a
// switch and fires the associated triggers; switch_object_toggle_by_mask flips
// switches selected by an alert mask.
//
// Address range: 0x8025c884 - 0x8025cbf8

#include "common.h"

extern "C" {
extern unsigned char DAT_805015bd;   // co-op sub-mode selector

extern int   DAT_806129d0;   // live switch-record count
extern int   DAT_806129cc;   // pending auto-open state pushed into the tick
extern float FLOAT_806129c8; // shared 3d-sound loop param accumulator
extern int   DAT_806120dc;   // count of currently-open switches
extern int   DAT_80610ff0;   // sound source slot (-1 = none)
extern int   DAT_80610ff4;   // sound bank ref (-1 = none)
extern float FLOAT_806108ec; // per-frame time scale numerator
extern int   DAT_806109c0;   // frame delta ticks (signed, xor-0x80000000 bias)

// switch-record table fields (base symbols, indexed +N*0x20)
extern int   DAT_805cb938;   // +0x00 id
extern int   DAT_805cb93c;   // +0x04 flags
extern int   DAT_805cb940;   // +0x08 mask
extern int   DAT_805cb944;   // +0x0c payload / open-duration
extern int   DAT_805cb948;   // +0x10 linked switch ptr
extern int   DAT_805cb94c;   // +0x14 state
extern int   DAT_805cb950;   // +0x18 open timer
extern int   DAT_805cb954;   // +0x1c option flags

extern const float  FLOAT_80618f98;   // 0.0
extern const float  FLOAT_80618fac;   // sound param delta (state!=0)
extern const float  FLOAT_80618fb0;   // sound param delta (state==0)
extern const float  FLOAT_80618fb4;   // sound param clamp lo
extern const float  FLOAT_80618fb8;   // sound param clamp hi / 1.0 base
extern const float  FLOAT_80618fbc;   // sound param constant
extern const float  FLOAT_80618fc0;   // open-duration sentinel
extern const float  FLOAT_80618fc4;   // open-duration epsilon
extern const double DOUBLE_80618fc8;  // frame-delta bias (double)

extern void sound_update_3d_source(double a, double b, double c, int* slotPtr, int d, int bankRef,
        int e);
extern void entity_trigger_check_player(void);
extern void FUN_8035d8c0(int mode);                       // switch open/close SFX + fx
extern void FUN_8025ff4c(int a, int* recPtr, int b);      // fire switch payload/trigger
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);
extern unsigned int __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern void FUN_803826e8(void);
extern void FUN_80382734(void);
}

// forward decls
void switch_object_set_state(unsigned int idx, unsigned int mode, int linkPtr);  // 0x8025caf0
void switch_object_update_sound(int active);                                     // 0x8025c930

// ---------------------------------------------------------------------------
// 0x8025c884 - register a switch/gate record from its source descriptor
// (param+0/0xc/0x10/0x8/0x14). Initializes state/timer to closed. Capped at 0x12.
void switch_object_register(unsigned int* desc)
{
    int off;
    float zero;

    if (0x11 < DAT_806129d0) {
        return;
    }
    off = DAT_806129d0 * 0x20;
    (&DAT_805cb938)[DAT_806129d0 * 8] = desc[0];
    zero = FLOAT_80618f98;
    *(unsigned int*)(&DAT_805cb93c + off) = desc[3];
    (&DAT_805cb940)[DAT_806129d0 * 8] = desc[4];
    (&DAT_805cb944)[DAT_806129d0 * 8] = desc[2];
    *(unsigned int*)(&DAT_805cb954 + off) = desc[5];
    *(unsigned int*)(&DAT_805cb948 + off) = 0;
    (&DAT_805cb94c)[DAT_806129d0 * 8] = 0;
    *(float*)((int)(&DAT_805cb950) + DAT_806129d0 * 0x20) = zero;
    DAT_806129d0 = DAT_806129d0 + 1;
}

// ---------------------------------------------------------------------------
// 0x8025c8e8 - linear scan of switch records for id==param, return its index
// (0 when not found).
int switch_object_index_by_id(int id)
{
    int idx;
    int off;
    int remaining;

    idx = 0;
    off = 0;
    remaining = DAT_806129d0;
    if (0 < DAT_806129d0) {
        do {
            if (*(int*)((int)&DAT_805cb938 + off) == id) {
                return idx;
            }
            idx = idx + 1;
            off = off + 0x20;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8025c930 - drive the shared switch 3D loop sound: ramp FLOAT_806129c8 by an
// active/idle delta, clamp to [lo,hi], and update the 3D source. No-op without
// a registered bank ref.
void switch_object_update_sound(int active)
{
    float delta;
    double param;
    double clamped;

    if (DAT_80610ff4 != -1) {
        delta = FLOAT_80618fb0;
        if (active != 0) {
            delta = FLOAT_80618fac;
        }
        param = (double)(FLOAT_806129c8 + delta);
        clamped = param;
        if ((double)FLOAT_80618fb8 < param) {
            clamped = (double)FLOAT_80618fb8;
        }
        if ((double)FLOAT_80618fb4 <= clamped) {
            if ((double)FLOAT_80618fb8 < param) {
                param = (double)FLOAT_80618fb8;
            }
        } else {
            param = (double)FLOAT_80618fb4;
        }
        FLOAT_806129c8 = (float)param;
        sound_update_3d_source(param, (double)FLOAT_80618fbc, (double)FLOAT_80618fb8,
            &DAT_80610ff0, 0, DAT_80610ff4, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x8025c9d0 - set the pending auto-open state carried into the next tick.
void switch_object_set_global_flag(int value)
{
    DAT_806129cc = value;
}

// ---------------------------------------------------------------------------
// 0x8025c9d8 - per-frame switch tick: advance each open switch's timer, and for
// held-open (state 1) switches auto-close once the timer passes the duration
// (unless duration is the never-close sentinel). Then update the loop sound.
void switch_object_tick_all(void)
{
    int off;
    int anyOpen;
    int i;
    double one;
    double bias;

    FUN_803826e8();
    one = (double)FLOAT_80618fb8;
    off = 0;
    anyOpen = DAT_806129cc;
    bias = DOUBLE_80618fc8;
    for (i = 0; i < DAT_806129d0; i = i + 1) {
        *(float*)((int)&DAT_805cb950 + off) =
            FLOAT_806108ec *
            (float)(one / (double)(float)((double)CONCAT44(0x43300000,
                DAT_806109c0 ^ 0x80000000) - bias)) +
            *(float*)((int)&DAT_805cb950 + off);
        if (*(int*)((int)&DAT_805cb94c + off) == 1) {
            if ((ABS((double)(FLOAT_80618fc0 - *(float*)((int)&DAT_805cb944 + off))) <
                 (double)FLOAT_80618fc4) ||
                (*(float*)((int)&DAT_805cb950 + off) < *(float*)((int)&DAT_805cb944 + off))) {
                anyOpen = 1;
            } else {
                switch_object_set_state((unsigned int)i, 0, 0);
            }
        }
        off = off + 0x20;
    }
    switch_object_update_sound(anyOpen);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x8025caf0 - open (mode 1) or close (mode 0) switch idx. Opening (only from a
// non-open state): play open SFX, reset timer, store optional link, fire the
// payload/trigger, bump the open count. Closing (from open): play close SFX,
// decrement count, and settle to closed (or "held" state 2 when +0x1c bit0 set).
void switch_object_set_state(unsigned int idx, unsigned int mode, int linkPtr)
{
    int i;
    int off;
    unsigned long long ctx;

    ctx = FUN_803826f0();
    i = (int)(ctx >> 0x20);
    int m = (int)ctx;
    off = i * 0x20;
    (void)idx; (void)mode;

    if (m == 1) {
        if (((&DAT_805cb94c)[i * 8] != 1) && ((&DAT_805cb94c)[i * 8] != 2)) {
            FUN_8035d8c0(2);
            (&DAT_805cb950)[i * 8] = (int)FLOAT_80618f98;
            if (linkPtr == 0) {
                *(unsigned int*)(&DAT_805cb948 + off) = 0;
            } else {
                *(int*)(&DAT_805cb948 + off) = linkPtr;
            }
            (&DAT_805cb94c)[i * 8] = 1;
            FUN_8025ff4c(1, &DAT_805cb938 + i * 8, 0);
            entity_trigger_check_player();
            DAT_806120dc = DAT_806120dc + 1;
        }
    } else if ((m < 1) && (-1 < m)) {
        if ((&DAT_805cb94c)[i * 8] == 1) {
            FUN_8035d8c0(0);
            DAT_806120dc = DAT_806120dc + -1;
        }
        if ((*(unsigned int*)(&DAT_805cb954 + off) & 1) == 0) {
            (&DAT_805cb94c)[i * 8] = 0;
        } else {
            (&DAT_805cb94c)[i * 8] = 2;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8025cbf8 - open every not-already-open switch whose mask (+0x08) intersects
// an alert mask supplied in the incoming register context. From
// aiactor_alert_group_arm (0x8013fec0).
void switch_object_toggle_by_mask(void)
{
    unsigned int mask;
    int i;
    int off;

    mask = __save_gpr_28();
    off = 0;
    for (i = 0; i < DAT_806129d0; i = i + 1) {
        if ((*(int*)((int)&DAT_805cb94c + off) != 1) &&
            ((mask & *(unsigned int*)((int)&DAT_805cb940 + off)) != 0)) {
            switch_object_set_state((unsigned int)i, 1, 0);
        }
        off = off + 0x20;
    }
    __restore_gpr_28();
}
