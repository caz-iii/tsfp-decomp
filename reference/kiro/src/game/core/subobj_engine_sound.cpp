// Address range: 0x801500c4 | batch_423 | 2026-09-06
//
// Entity render sub-object ENGINE/ROTOR 3D-SOUND tick (subsystem:game). A
// per-frame update (spawn-frame entry via FUN_803826f0) driven by an unnamed
// parent tick (FUN_80150af0). It maintains a mechanical sub-object's rev/rotor
// state and emits up to three positional 3D sounds (drive loop, secondary rev,
// and a rotor/whine channel) via sound_update_3d_source, plays a one-shot
// spin-down/stop sample when the rev falls below a threshold, and advances the
// smoothed rev accumulators (+0x560 / +0x564).
//
// Sub-object record (via *(entity+0x7c)):
//   +0x50  owner/definition pointer   +0x58  state flags (bit 8 = running)
//   +0x528 primary rev level          +0x55c spin-down timer stamp
//   +0x560 smoothed rev A             +0x564 smoothed rev B
//   +0x5c  gear/wear index            +0x968/+0x96c rotor channel state
// Sound def block (owner +0x50 -> +0xb8/+0xbc/+0xcc bank ids; +0xd0/+0xd4 base
// pitch/volume; +200 stop sample).

#include "common.h"
#include "generated_decls.h"

extern "C" {
extern unsigned long long FUN_803826f0(void);
extern void               FUN_8038273c(void);
}

// sound_update_3d_source is declared with fixed pointer-typed params in
// generated_decls; the real call site passes raw int struct/handle pointers and
// uses the (nonzero => active) return. Call through a typed alias matching the
// PPC call site: (rangeOrVol, pitch, volOrPan, statePtr, listenerPtr,
// bankId, flags) -> int.
typedef int (*snd3d_fn)(double a, double b, double c, int statePtr, int listenerPtr,
                        int bankId, int flags);
#define SND_UPDATE_3D ((snd3d_fn)&sound_update_3d_source)

extern float  FLOAT_80615fa0;  // default modulation
extern float  FLOAT_80615fa4;  // rev-idle threshold
extern float  FLOAT_80615fa8;  // 1.0f
extern float  FLOAT_80615fac;  // stop threshold
extern float  FLOAT_80615fb0;  // spin-down rate
extern float  FLOAT_80615fb4;  // rev decay factor
extern float  FLOAT_80615fb8;  // rev approach factor
extern float  FLOAT_80615fbc;  // pitch bias
extern float  FLOAT_80615fc0;  // pitch * rev coefficient
extern float  FLOAT_80615fc4;  // pitch * gear coefficient
extern float  FLOAT_80615fc8;  // secondary volume scale
extern float  FLOAT_80615fcc;  // rotor base pitch
extern float  FLOAT_80615fd0;  // rotor pitch coefficient
extern double DOUBLE_80615fd8; // dequant magic bias
extern int    DAT_806109c0;    // frame counter / rng

// ===========================================================================
// 0x801500c4  subobj_engine_sound_tick
// ===========================================================================
void subobj_engine_sound_tick(void)
{
    int entity = (int)FUN_803826f0();
    double revMod = (double)FLOAT_80615fa0;
    int rec = *(int*)(entity + 0x7c);
    double secMod;

    if (*(float*)(rec + 0x528) <= FLOAT_80615fa4) {
        // Idle / spinning down.
        if (*(float*)(rec + 0x560) <= FLOAT_80615fac) {
            *(unsigned int*)(rec + 0x58) = *(unsigned int*)(rec + 0x58) | 8;
        }
        secMod = revMod;
        double stampAge = (double)(unsigned int)(*(unsigned int*)(rec + 0x55c)) - DOUBLE_80615fd8;
        double frameAge = (double)(unsigned int)(DAT_806109c0) - DOUBLE_80615fd8;
        if (FLOAT_80615fb0 * (float)frameAge < (float)stampAge) {
            revMod = (double)FLOAT_80615fa8;
        }
    } else {
        // Running: on the first idle->run edge, fire the spin-up/stop sample.
        secMod = (double)FLOAT_80615fa8;
        if ((*(unsigned int*)(rec + 0x58) & 8) != 0) {
            int sampleId = *(int*)(*(int*)(rec + 0x50) + 200);
            int params[7];
            sound_init_play_params((int)params);
            *(unsigned char*)((int)params + 0x3c) = 1;
            *(unsigned int*)((int)params + 0x30) = *(unsigned int*)(entity + 100);
            *(unsigned int*)((int)params + 0x34) = *(unsigned int*)(entity + 0x68);
            *(unsigned int*)((int)params + 0x38) = *(unsigned int*)(entity + 0x6c);
            params[0] = sampleId;
            sound_play_request(params);
            *(unsigned int*)(rec + 0x58) = *(unsigned int*)(rec + 0x58) & 0xfffffff7;
        }
    }

    // Advance the two smoothed rev accumulators.
    *(float*)(rec + 0x560) = *(float*)(rec + 0x560) * FLOAT_80615fb4;
    *(float*)(rec + 0x560) = (float)((double)FLOAT_80615fb8 * secMod + (double)*(float*)(rec + 0x560));
    *(float*)(rec + 0x564) = *(float*)(rec + 0x564) * FLOAT_80615fb4;
    *(float*)(rec + 0x564) = (float)((double)FLOAT_80615fb8 * revMod + (double)*(float*)(rec + 0x564));

    int owner = *(int*)(rec + 0x50);
    double pitch = (double)(*(float*)(owner + 0xd4) *
        (FLOAT_80615fc4 * ((float)((double)(unsigned int)(5U - *(int*)(rec + 0x5c)) - DOUBLE_80615fd8)
                           / FLOAT_80615fa4) +
         FLOAT_80615fc0 * *(float*)(rec + 0x528) + FLOAT_80615fbc));

    int r0 = SND_UPDATE_3D((double)*(float*)(rec + 0x564), (double)*(float*)(owner + 0xd0),
                           (double)FLOAT_80615fa8, entity + 0x228, entity + 100,
                           *(int*)(owner + 0xbc), 0);
    int r1 = SND_UPDATE_3D((double)(FLOAT_80615fc8 * *(float*)(rec + 0x560)),
                           (double)*(float*)(*(int*)(rec + 0x50) + 0xd0), pitch,
                           entity + 0x22c, entity + 100,
                           *(int*)(*(int*)(rec + 0x50) + 0xb8), 1);
    int r2 = SND_UPDATE_3D((double)*(float*)(rec + 0x96c), (double)FLOAT_80615fcc,
                           -(double)(float)((double)FLOAT_80615fd0 * (double)*(float*)(rec + 0x96c) -
                                            (double)FLOAT_80615fa8),
                           rec + 0x968, entity + 100,
                           *(int*)(*(int*)(rec + 0x50) + 0xcc), 1);
    if ((r0 != 0) || (r1 != 0) || (r2 != 0)) {
        *(unsigned int*)(entity + 0x18) = *(unsigned int*)(entity + 0x18) | 0x40;
    }
    FUN_8038273c();
}
