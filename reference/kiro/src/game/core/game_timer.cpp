// ============================================================================
// Batch 355 | Date: 2026-09-04
// Game frame timing / pause manager.
//
//   game_time_to_frames  (0x80130d3c) convert time-base ticks to frame units
//   game_frame_timer_tick(0x80130d9c) per-frame timer + pause/resume dispatch
//
// The GameCube time-base register (os_get_time_base) counts at bus_clock/4; the
// bus clock is stored by the bootrom at 0x800000F8. game_time_to_frames scales
// elapsed ticks into 60 Hz frame units. game_frame_timer_tick samples the frame
// counter, computes elapsed frames (clamped 1..3), runs the pause/resume state
// machine (sound + clock), and advances the accumulators once per frame.
// Subsystem: game (core timing). Compile-Clean Standard.
// ============================================================================

#include "common.h"

// --- raw runtime symbols (per-file externs) --------------------------------
extern int   os_get_time_base(void);     // 0x803a0c48  reads TBL
extern int   os_get_frame_count(void);   // 0x803a7928  global frame counter
extern void  sound_pause_all(void);      // pause all voices
extern void  sound_resume_all(void);     // resume all voices
extern void  hud_clock_tick(void);       // per-frame HUD clock advance
extern int   FUN_801a30cc(void);         // game-state predicate (paused/menu == state 0xD/0xE)

extern unsigned int DAT_800000f8;        // bootrom-stored bus clock (Hz)

extern float FLOAT_80615aa8;             // 60.0 (frames per second)
extern float FLOAT_80615ab8;             // 1.0
extern float FLOAT_80615abc;             // 0.0 (paused delta)
extern double DOUBLE_80615ab0;           // int->float conversion magic base
extern double DOUBLE_80615ac0;           // int->float conversion magic base

// --- timing state globals ---------------------------------------------------
extern int   DAT_805831e8;   // ring buffer of recent frame-count samples (idx 0)
extern int   DAT_805831ec;   // ...shifted-in tail
extern float DAT_805831f8;   // last computed seconds-per-tick scale
extern int   DAT_805831fc;   // second ring buffer (idx 0)
extern int   DAT_80583200;
extern int   DAT_80583208;   // previous frame-count sample
extern int   DAT_8058320c;   // current frame-count sample
extern int   DAT_80583210;   // third ring buffer (idx 0)
extern int   DAT_80583214;

extern int   DAT_806108e0;   // elapsed frames this tick (clamped 1..3)
extern int   DAT_806108e4;   // effective (pause-gated) elapsed frames
extern float FLOAT_806108e8; // elapsed frames as float
extern float FLOAT_806108ec; // effective elapsed frames as float (0 when paused)
extern int   DAT_80610aa0;   // active something-id (-1 = none)
extern int   DAT_8061202c;   // pause request counter (>0 pause, <0 resume window)
extern int   DAT_80612020;   // frame-count snapshot A
extern int   DAT_80612024;   // frame-count snapshot B
extern int   DAT_80612028;   // total elapsed-frame accumulator
extern int   DAT_80612030;   // tick counter
extern int   DAT_80612034;   // multi-frame tick counter
extern int   DAT_8061237c;   // suppress-pause flag

// ----------------------------------------------------------------------------
// game_time_to_frames (0x80130d3c)
//   elapsed_ticks / ((bus_clock/4) / 60) -> elapsed time expressed in 60 Hz
//   frame units. os_get_time_base() reads the current tick; DAT_800000f8 >> 2
//   is the time-base frequency (bus_clock / 4).
// ----------------------------------------------------------------------------
float game_time_to_frames(void)
{
    int ticks = os_get_time_base();
    float ticksF = (float)((double)ticks - DOUBLE_80615ab0 + (double)ticks);  // int->float
    float tbFreq = (float)((double)(int)(DAT_800000f8 >> 2) - DOUBLE_80615ab0
                           + (double)(int)(DAT_800000f8 >> 2));
    return ticksF / (tbFreq / FLOAT_80615aa8);
}

// ----------------------------------------------------------------------------
// game_frame_timer_tick (0x80130d9c)
//   Sample the frame counter, compute elapsed frames (clamped to 1..3), run the
//   pause/resume state machine (sound + HUD clock) and advance the frame
//   accumulators. Called once per game frame.
// ----------------------------------------------------------------------------
void game_frame_timer_tick(void)
{
    // shift the three ring buffers down by one (4 dwords each)
    int* ring0 = &DAT_805831e8;
    int* ring1 = &DAT_805831fc;
    int* ring2 = &DAT_80583210;
    for (int i = 0; i < 4; i++) {
        ring0[i] = ring0[i + 1];
        ring1[i] = ring1[i + 1];
        ring2[i] = ring2[i + 1];
    }

    DAT_8058320c = os_get_frame_count();
    DAT_806108e0 = DAT_8058320c - DAT_80583208;
    DAT_80612020 = DAT_8058320c;
    DAT_805831f8 = game_time_to_frames();
    DAT_80612024 = DAT_8058320c;

    if (3 < DAT_806108e0) { DAT_806108e0 = 3; }
    if (DAT_806108e0 == 0) { DAT_806108e0 = 1; }

    DAT_80612030 = DAT_80612030 + 1;
    FLOAT_806108e8 = (float)DAT_806108e0;
    if (1 < DAT_806108e0) {
        DAT_80612034 = DAT_80612034 + 1;
    }

    if (DAT_8061202c == 0) {
        // running normally
        DAT_806108e4 = DAT_806108e0;
        FLOAT_806108ec = (float)DAT_806108e0;
    } else if (DAT_8061202c < 0 && -2 < DAT_8061202c) {
        // resume window
        DAT_806108e0 = 1;
        DAT_806108e4 = 1;
        FLOAT_806108e8 = FLOAT_80615ab8;
        FLOAT_806108ec = FLOAT_80615ab8;
        DAT_8061202c = 0;
        sound_resume_all();
    } else {
        // paused
        if (DAT_806108e4 != 0) {
            sound_pause_all();
        }
        DAT_806108e4 = 0;
        FLOAT_806108ec = FLOAT_80615abc;
    }

    // force-pause when the game is in a paused/menu state
    if (DAT_80610aa0 != -1 && DAT_8061237c == 0 && FUN_801a30cc() == 0) {
        DAT_806108e4 = 0;
        FLOAT_806108ec = FLOAT_80615abc;
    }

    DAT_80612028 = DAT_80612028 + DAT_806108e0;
    hud_clock_tick();
}
