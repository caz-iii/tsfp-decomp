// gamemode_dispatch.cpp - Per-game-mode subsystem reset + tick dispatch.
//
// The active game mode (DAT_80612614) selects a mode-specific subsystem for the
// AI/spawn/visibility system. gamemode_subsystem_reset clears the shared state
// block and dispatches to the mode's reset handler; gamemode_subsystem_tick
// runs the shared spawn-visibility tick then the mode's tick handler and
// clears any sound-bank ref it registered. Called from aiactor_system_reset
// (0x80135440) and aiactor_system_alloc (0x80135734).
//
// Address range: 0x8025c568 - 0x8025c614

#include "common.h"

extern "C" {
extern int   DAT_80612614;     // active game-mode id
extern int   DAT_806129d0;     // mode-shared counter
extern int   DAT_806129cc;     // mode-shared counter
extern float FLOAT_806129c8;   // mode-shared accumulator
extern int   DAT_80610ff0;     // last-processed spawn slot (-1 = none)
extern int   DAT_80610ff4;     // sound-bank ref registered this tick (-1 = none)

extern const float FLOAT_80618f98;   // 0.0

extern void memset_wrapper(int dst, int val, unsigned int len);
extern void aiactor_spawnvis_reset_globals(void);
extern void sound_clear_bank_ref(int bankRef);

// shared spawn-visibility reset/tick
extern void FUN_802792a8(void);
extern unsigned long long FUN_80279210(unsigned long long p1, unsigned long long p2,
        unsigned long long p3, unsigned long long p4, unsigned long long p5,
        unsigned long long p6, unsigned long long p7, unsigned long long p8);

// per-mode reset handlers
extern void FUN_802960b8(void);   // mode 0x0e reset (a)
extern void FUN_8029ac68(void);   // mode 0x0e reset (b)
extern void FUN_8037eec4(void);   // mode 0x16 reset
extern void FUN_8029f22c(void);   // mode 0x51 reset
extern void FUN_8028e37c(void);   // mode 0x01 / 0x11 reset

// per-mode tick handlers (varargs float/int spill; typed to match call site)
extern void FUN_8029f290(unsigned long long f1, unsigned long long p2, unsigned long long p3,
        unsigned long long p4, unsigned long long p5, unsigned long long p6,
        unsigned long long p7, unsigned long long p8, unsigned int p9, unsigned int p10,
        unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14,
        unsigned int p15, unsigned int p16);
extern void FUN_8037ee84(unsigned long long f1, unsigned long long p2, unsigned long long p3,
        unsigned long long p4, unsigned long long p5, unsigned long long p6,
        unsigned long long p7, unsigned long long p8, unsigned int p9, unsigned int p10,
        unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14,
        unsigned int p15, unsigned int p16);
extern unsigned long long FUN_80296374(unsigned long long f1, unsigned long long p2,
        unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6,
        unsigned long long p7, unsigned long long p8, unsigned int p9, unsigned int p10,
        unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14,
        unsigned int p15, unsigned int p16);
extern unsigned long long FUN_8029ad14(unsigned long long f1, unsigned long long p2,
        unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6,
        unsigned long long p7, unsigned long long p8, int p9, int p10,
        unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14,
        unsigned int p15, unsigned int p16);
extern void FUN_802ad7d8(unsigned long long f1, unsigned long long p2, unsigned long long p3,
        unsigned long long p4, unsigned long long p5, unsigned long long p6,
        unsigned long long p7, unsigned long long p8, int p9, int p10,
        unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14,
        unsigned int p15, unsigned int p16);
extern void FUN_8028e69c(unsigned long long f1, unsigned long long p2, unsigned long long p3,
        unsigned long long p4, unsigned long long p5, unsigned long long p6,
        unsigned long long p7, unsigned long long p8, unsigned int p9, unsigned int p10,
        unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14,
        unsigned int p15, unsigned int p16);
}

// ---------------------------------------------------------------------------
// 0x8025c568 - reset the AI/spawn-visibility subsystem for the current mode:
// clear the shared 0x240 state block (@0x805cb938) + reset globals, then run
// the mode-specific reset handler(s).
void gamemode_subsystem_reset(void)
{
    int mode;

    mode = DAT_80612614;
    memset_wrapper(0x805cb938, 0, 0x240);
    DAT_806129d0 = 0;
    aiactor_spawnvis_reset_globals();
    FUN_802792a8();
    if (mode == 0xe) {
        FUN_802960b8();
        FUN_8029ac68();
    } else if (mode == 0x16) {
        FUN_8037eec4();
    } else if (mode == 0x51) {
        FUN_8029f22c();
    } else if ((mode == 1) || (mode == 0x11)) {
        FUN_8028e37c();
    }
    DAT_80610ff0 = 0xffffffff;
    FLOAT_806129c8 = FLOAT_80618f98;
    DAT_806129cc = 0;
}

// ---------------------------------------------------------------------------
// 0x8025c614 - per-frame subsystem tick: run the shared spawn-visibility tick,
// then dispatch the mode-specific tick handler (modes 0x01/0x11 share one,
// 0x0e runs a 3-stage pipeline, 0x16/0x51 have their own, 0x06 only registers
// bank 0x80). Finally clear whatever sound-bank ref was registered this tick.
void gamemode_subsystem_tick(unsigned long long param_1, unsigned long long param_2,
        unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
        unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
        unsigned int param_9, unsigned int param_10, unsigned int param_11, unsigned int param_12,
        unsigned int param_13, unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    unsigned int hi;
    unsigned int lo;
    unsigned long long pair;

    (void)param_9; (void)param_10;
    DAT_80610ff4 = -1;
    pair = FUN_80279210(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    hi = (unsigned int)(pair >> 0x20);
    lo = (unsigned int)pair;

    if (DAT_80612614 != 0x11) {
        if (0x10 < DAT_80612614) {
            if (DAT_80612614 == 0x51) {
                FUN_8029f290((unsigned long long)0, param_2, param_3, param_4, param_5, param_6,
                    param_7, param_8, hi, lo, param_11, param_12, param_13, param_14,
                    param_15, param_16);
            } else if ((DAT_80612614 < 0x51) && (DAT_80612614 == 0x16)) {
                DAT_80610ff4 = 0x80;
                FUN_8037ee84((unsigned long long)0, param_2, param_3, param_4, param_5, param_6,
                    param_7, param_8, hi, lo, param_11, param_12, param_13, param_14,
                    param_15, param_16);
            }
            goto LAB_done;
        }
        if (DAT_80612614 == 6) {
            DAT_80610ff4 = 0x80;
            goto LAB_done;
        }
        if (5 < DAT_80612614) {
            if (DAT_80612614 == 0xe) {
                pair = FUN_80296374((unsigned long long)0, param_2, param_3, param_4, param_5,
                    param_6, param_7, param_8, hi, lo, param_11, param_12, param_13, param_14,
                    param_15, param_16);
                pair = FUN_8029ad14((unsigned long long)0, param_2, param_3, param_4, param_5,
                    param_6, param_7, param_8, (int)(pair >> 0x20), (int)pair, param_11,
                    param_12, param_13, param_14, param_15, param_16);
                FUN_802ad7d8((unsigned long long)0, param_2, param_3, param_4, param_5, param_6,
                    param_7, param_8, (int)(pair >> 0x20), (int)pair, param_11, param_12,
                    param_13, param_14, param_15, param_16);
            }
            goto LAB_done;
        }
        if (DAT_80612614 != 1) goto LAB_done;
    }
    FUN_8028e69c((unsigned long long)0, param_2, param_3, param_4, param_5, param_6, param_7,
        param_8, hi, lo, param_11, param_12, param_13, param_14, param_15, param_16);
LAB_done:
    sound_clear_bank_ref(DAT_80610ff4);
}
