// Address range: 0x801444f4 - 0x80144798 | batch_408 (+ 0x80144798 batch_409) | 2026-09-05
//
// Generic delayed-callback "timer bank". A small fixed table (DAT_80588c98,
// 0x18-byte entries, DAT_80612158 live count, cap 0x4b) of one-shot timers.
// Each timer fires a stored callback(arg) after a frame-based duration and is
// then removed. Registered from gameplay sites that need a deferred action
// (e.g. player_ai_event_sound_dispatch, spawn_pickup_collected).
//
// Entry layout (index k, ints at &DAT_80588c98 + k*0x18):
//   +0x00 DAT_80588c98  type/mode: 1 = count-up toward a threshold time,
//                       0 (>= -0 && < 1) = count-down of remaining frames,
//                       2 = expired/pending-removal (set by tick).
//   +0x04 DAT_80588c9c  accumulator: elapsed frames (type 1) or remaining
//                       frames (type 0).
//   +0x08 DAT_80588ca0  last-update game-frame stamp (DAT_80611928).
//   +0x0c DAT_80588ca4  threshold time in frames (float, type 1 only).
//   +0x10 DAT_80588ca8  callback argument (passed to the callback).
//   +0x14 DAT_80588cac  callback function pointer  void(*)(int arg).
//
// The register call takes a duration in seconds (param_1, double) and converts
// it to frames using the frame-rate divisor DAT_806109c0.

#include "common.h"
#include "generated_decls.h"

extern "C" {
extern int  __timer_frame_begin(void);  // FUN_803826e4 (packed gpr save frame)
extern void __timer_frame_end(void);     // FUN_80382730
}
// leaf_31e8 (block copy) comes from generated_decls.h.

// swap-remove entry `index` by copying the last live entry over it (batch_409).
void timer_bank_swap_remove(int index);

// ---- timer-bank table (per-file externs) -----------------------------------
extern int   DAT_80612158;   // live timer count (cap 0x4b)
extern int   DAT_80611928;   // current game-frame counter
extern int   DAT_806109c0;   // frames-per-tick / rate divisor
extern int   DAT_80588c98;   // type field     [k].+0x00 (table base)
extern int   DAT_80588c9c;   // accumulator    [k].+0x04
extern int   DAT_80588ca0;   // last stamp     [k].+0x08
extern float DAT_80588ca4;   // threshold      [k].+0x0c
extern int   DAT_80588ca8;   // callback arg   [k].+0x10
extern int   DAT_80588cac;   // callback ptr   [k].+0x14
extern double DOUBLE_80615e98; // int->double magic bias
extern float  FLOAT_80615ea0;  // count-down expiry threshold (<= 0)

// s32 -> float via the Gekko magic-double trick (bias DOUBLE_80615e98).
static inline float i2f_98(int v)
{
    return (float)((double)CONCAT44(0x43300000, (unsigned int)v ^ 0x80000000) - DOUBLE_80615e98);
}

// ---------------------------------------------------------------------------
// timer_bank_reset (0x801444f4)
void timer_bank_reset(void)
{
    DAT_80612158 = 0;
}

// ---------------------------------------------------------------------------
// timer_bank_init (0x80144500)  -- wrapper over reset.
void timer_bank_init(void)
{
    timer_bank_reset();
}

// ---------------------------------------------------------------------------
// timer_bank_register (0x80144520)
// Add a one-shot timer. durationSec is converted to a frame count using
// DAT_806109c0. Returns the slot index, or -1 if the table is full.
//   type == 1  -> count-up: store threshold = durationSec * frameRate.
//   type in [0,1) i.e. 0 -> count-down: store remaining = durationSec * frames.
int timer_bank_register(double durationSec, int type, int arg, int callback)
{
    int idx = DAT_80612158;
    int frame = DAT_80611928;
    if (DAT_80612158 < 0x4b)
    {
        (&DAT_80588c98)[DAT_80612158 * 6] = type;
        (&DAT_80588ca8)[idx * 6] = arg;
        (&DAT_80588ca0)[idx * 6] = frame;
        (&DAT_80588cac)[idx * 6] = callback;

        float frames = i2f_98(DAT_806109c0);
        if (type == 1)
        {
            (&DAT_80588c9c)[idx * 6] = 0;
            (&DAT_80588ca4)[idx * 6] = (float)(durationSec * (double)frames);
        }
        else if ((type < 1) && (-1 < type))
        {
            (&DAT_80588c9c)[idx * 6] = (int)(durationSec * (double)frames);
        }
        idx = DAT_80612158;
        DAT_80612158 = DAT_80612158 + 1;
    }
    else
    {
        idx = -1;
    }
    return idx;
}

// ---------------------------------------------------------------------------
// timer_bank_tick (0x801445f4)
// Advance every live timer by the frames elapsed since its last update, fire
// callbacks that reach their threshold, then compact out expired entries.
void timer_bank_tick(void)
{
    __timer_frame_begin();

    int off = 0;
    for (int i = 0; i < DAT_80612158; ++i)
    {
        int now = DAT_80611928;
        int prevStamp = *(int*)((int)&DAT_80588ca0 + off);
        *(int*)((int)&DAT_80588ca0 + off) = now;
        int elapsed = now - prevStamp;
        int type = *(int*)((int)&DAT_80588c98 + off);

        if (type == 1)
        {
            // count-up: accumulate elapsed; fire (and re-arm) when threshold hit
            int* acc = (int*)((int)&DAT_80588c9c + off);
            *acc += elapsed;
            if (*(float*)((int)&DAT_80588ca4 + off) <= i2f_98(*acc))
            {
                (*(void(**)(int))((int)&DAT_80588cac + off))(*(int*)((int)&DAT_80588ca8 + off));
                *acc = (int)(i2f_98(*acc) - *(float*)((int)&DAT_80588ca4 + off));
            }
        }
        else if ((type < 1) && (-1 < type))
        {
            // count-down: subtract elapsed; fire once and mark expired (type 2)
            int* acc = (int*)((int)&DAT_80588c9c + off);
            *acc -= elapsed;
            if (i2f_98(*acc) <= FLOAT_80615ea0)
            {
                (*(void(**)(int))((int)&DAT_80588cac + off))(*(int*)((int)&DAT_80588ca8 + off));
                *(int*)((int)&DAT_80588c98 + off) = 2;
            }
        }
        off += 0x18;
    }

    // compact: swap-remove expired (type 2) entries, scanning back-to-front.
    for (int i = DAT_80612158 - 1, soff = (DAT_80612158 - 1) * 0x18; -1 < i; --i, soff -= 0x18)
    {
        if (*(int*)((int)&DAT_80588c98 + soff) == 2)
            timer_bank_swap_remove(i);
    }

    __timer_frame_end();
}

// ---------------------------------------------------------------------------
// timer_bank_stub_nop (0x80144794)  -- empty stub.
void timer_bank_stub_nop(void)
{
}

// ---------------------------------------------------------------------------
// timer_bank_swap_remove (0x80144798)
// Remove entry `index` by copying the last live entry (0x18 bytes) over it
// (unless it is already the last), then decrement the live count.
void timer_bank_swap_remove(int index)
{
    if (index != DAT_80612158 - 1)
    {
        leaf_31e8((unsigned int)(&DAT_80588c98 + index * 6),
                  (unsigned int)(&DAT_80588c98 + (DAT_80612158 - 1) * 6),
                  0x18);
    }
    DAT_80612158 = DAT_80612158 - 1;
}
