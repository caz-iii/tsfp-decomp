// pad_rumble.cpp
// Controller rumble (motor) manager plus the input-system init entry point.
// The rumble layer tracks a current + target intensity per pad (4 channels) in
// the parallel arrays DAT_805ac9e8 (current) / DAT_805ac9f8 (target) and drives
// the low-level PAD motor command FUN_803a8de0(channel, mode) each frame,
// ramping the current level toward the target.
//   FUN_803a8de0 mode: 0 = motor off, 1 = motor on, 2 = hard stop.
//
// Batch: batch_492
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Low-level PAD motor command and pad-subsystem init.
extern void FUN_803a8de0(unsigned int channel, unsigned int mode);
extern void FUN_803a88c4(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, unsigned int a9,
                         unsigned int a10, int a11, unsigned int a12, unsigned int a13,
                         unsigned int a14, unsigned int a15, unsigned int a16);

// Input logical->physical pad mapping.
extern void input_set_pad_mapping(int logical, int physical);

// memset shim.
extern void memset_wrapper(int dst, int val, int len);

// Compiler-runtime spill helpers.
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);

// Frame-guard pair used by rumble_shutdown.
extern void FUN_803826e4(void);
extern void FUN_80382730(void);

// ---- Data ----
extern int          DAT_805ac9e8;  // per-channel current rumble level (4 x int)
extern int          DAT_805ac9f8;  // per-channel target rumble level (4 x int)
extern unsigned int DAT_806116b8;  // input flags / edge state (cleared on init)
}

// ---------------------------------------------------------------------------
// Address: 0x8018c1d0 | Name: rumble_stop_primary | Date: 2026-09-06
// Turn off the motor on pad 0 (mode 0).
// ---------------------------------------------------------------------------
void rumble_stop_primary(void)
{
    FUN_803a8de0(0, 0);
}

// ---------------------------------------------------------------------------
// Address: 0x8018c1f8 | Name: rumble_clear_state | Date: 2026-09-06
// Zero the current and target rumble levels for all four pad channels.
// ---------------------------------------------------------------------------
void rumble_clear_state(void)
{
    int *pCur = &DAT_805ac9e8;
    int *pTgt = &DAT_805ac9f8;
    for (int i = 0; i < 4; i++) {
        pCur[i] = 0;
        pTgt[i] = 0;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018c230 | Name: rumble_update | Date: 2026-09-06
// Per-frame rumble servicer for all four pads. When the current level is below
// its target, step it up and turn the motor on (mode 1); otherwise step it down
// (clamped at 0) and turn the motor off (mode 0).
// ---------------------------------------------------------------------------
void rumble_update(void)
{
    __save_gpr_28();
    int *pCur = &DAT_805ac9e8;
    int *pTgt = &DAT_805ac9f8;
    for (unsigned int ch = 0; (int)ch < 4; ch++) {
        if (pCur[ch] < pTgt[ch]) {
            FUN_803a8de0(ch, 1);
            pCur[ch] = pCur[ch] + 1;
        } else {
            FUN_803a8de0(ch, 0);
            pCur[ch] = pCur[ch] - 1;
            if (pCur[ch] < 0) {
                pCur[ch] = 0;
            }
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x8018c2dc | Name: rumble_set_target | Date: 2026-09-06
// Set the target rumble level (duration/intensity) for one pad channel.
// ---------------------------------------------------------------------------
void rumble_set_target(int channel, int level)
{
    (&DAT_805ac9f8)[channel] = level;
}

// ---------------------------------------------------------------------------
// Address: 0x8018c2f0 | Name: rumble_shutdown | Date: 2026-09-06
// Hard-stop the motor (mode 2) on all four pads and clear their current/target
// levels.
// ---------------------------------------------------------------------------
void rumble_shutdown(void)
{
    FUN_803826e4();
    int *pCur = &DAT_805ac9e8;
    int *pTgt = &DAT_805ac9f8;
    for (unsigned int ch = 0; (int)ch < 4; ch++) {
        FUN_803a8de0(ch, 2);
        pCur[ch] = 0;
        pTgt[ch] = 0;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// Address: 0x8018c360 | Name: input_init | Date: 2026-09-06
// Initialise the input system: run the low-level pad init (FUN_803a88c4), clear
// the input edge/flag state, wipe the 0x30-byte pad-state block, and set the
// default 1:1 logical->physical pad mapping for the four controllers.
// Caller: input_system_init.
// ---------------------------------------------------------------------------
void input_init(unsigned long long param_1, unsigned long long param_2, unsigned long long param_3,
                unsigned long long param_4, unsigned long long param_5, unsigned long long param_6,
                unsigned long long param_7, unsigned long long param_8, unsigned int param_9,
                unsigned int param_10, int param_11, unsigned int param_12, unsigned int param_13,
                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    FUN_803a88c4(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 param_9, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
    DAT_806116b8 = 0;
    // -0x7fa535f8 == 0x805aca08 : the 0x30-byte pad-state block.
    memset_wrapper((int)0x805aca08, 0, 0x30);
    input_set_pad_mapping(0, 0);
    input_set_pad_mapping(1, 1);
    input_set_pad_mapping(2, 2);
    input_set_pad_mapping(3, 3);
}
