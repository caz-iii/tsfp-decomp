// Address: 0x803551a4 | Name: weather_build_wave_table   | Date: 2026-09-12
// Address: 0x80355248 | Name: weather_flare_pool_init    | Date: 2026-09-12
// Address: 0x80355348 | Name: weather_flare_pool_add     | Date: 2026-09-12
// Address: 0x803554b8 | Name: weather_flare_pool_remove  | Date: 2026-09-12
// Address: 0x8035550c | Name: weather_flare_set_texture  | Date: 2026-09-12
// Address: 0x80355574 | Name: weather_flare_activate     | Date: 2026-09-12
//
// Weather flare/glow sprite-billboard pool. A fixed 3-slot pool (records of
// 0x260 bytes at DAT_805e30f0, live count in DAT_80612e00) of animated flare
// sprites used by the weather system (lens/glow effects on the sky). Init is
// invoked from weather_system_reset; the wave-table builder is invoked from
// weather_system_alloc. Each slot crossfades between two sprite textures over
// a duration (fields at +0x74..+0x88).

#include "common.h"

// ---- per-file raw symbol externs ------------------------------------------
extern int   DAT_80612e00;      // live flare-slot count
extern int   DAT_80612de4;
extern int   DAT_80612dfc;

// slot pool: 3 records of 0x260 bytes. &DAT_805e30f0 is record[0].
extern int   DAT_805e30f0;      // record[0] +0x00 (state / used flag: -1 = free)
extern int   DAT_805e30f4;      // record[0] +0x04
extern int   DAT_805e3100;      // record[0] +0x10
// particle-def / emitter scratch used by the pool init
extern int   DAT_805e3810;
extern int   DAT_805e38dc;
extern int   DAT_805e3050;      // wave-table destination (2 dwords per step)
extern int   DAT_805e3054;

// wave lookup table (2048 f32 entries)
extern float DAT_80583228[];

// float / double constants
extern double DOUBLE_8061add0;  // int->double bias
extern float  FLOAT_8061add8;   // 0.0 / base
extern float  FLOAT_8061addc;
extern float  FLOAT_8061ade0;
extern float  FLOAT_8061ade4;
extern float  FLOAT_8061ade8;
extern float  FLOAT_8061adec;
extern float  FLOAT_8061adf0;   // 1.0
extern float  FLOAT_8061adf4;   // 0.0-ish default
extern float  FLOAT_8061adf8;
extern float  FLOAT_8061adfc;
extern float  FLOAT_8061ae00;
extern float  FLOAT_8061ae04;
extern float  FLOAT_8061ae08;
extern float  FLOAT_8061ae0c;
extern float  FLOAT_8061ae10;
extern float  FLOAT_8061ae14;

// helpers (raw addresses; typed to the call sites)
extern void memset_wrapper(int dst, int val, int len);
extern void FUN_80357888(void);              // reset flare draw state
extern void FUN_80356c58(int slot);          // release a slot's sprite instance
extern int  sprite_get_texture_id(int ref);
extern void sound_stop_handle(void);
extern void particle_def_create(double a, double b, int* def, int c, int d, int e, int f, int g);
extern void particle_def_set_size_keyframes(double a, double b, double c, double d,
                                            unsigned int e, unsigned int f, unsigned int g);
extern void particle_def_set_spin_range(double a, double b);
extern void particle_def_set_color_end(double a, double b, double c, double d);
extern void particle_emitter_create(double a, double b, double c, int* emitter,
                                     int* def, int d, int e);
extern void particle_emitter_set_color_range(double a, double b, double c, double d);

// ============================================================================
// weather_build_wave_table  (0x803551a4)
// Precompute a 20-step (x, y) wave curve into DAT_805e3050 by evaluating the
// sin/cos lookup table at evenly spaced phases scaled by the weather config
// constants. Called from weather_system_alloc.
// ============================================================================
void weather_build_wave_table(void)
{
    float scaleE = FLOAT_8061ade8;
    float scaleD = FLOAT_8061ade4;
    float scaleC = FLOAT_8061ade0;
    float scaleB = FLOAT_8061addc;
    float base   = FLOAT_8061add8;
    double bias  = DOUBLE_8061add0;

    unsigned int step = 0;
    int remaining = 0x14;
    int off = 0;
    do {
        unsigned int neg = step ^ 0x80000000;
        step += 1;
        unsigned int idx = (unsigned int)(scaleB * scaleC * scaleD *
                          ((float)((double)CONCAT44(0x43300000, neg) - bias) / scaleE) + base);
        float cosv = (float)DAT_80583228[idx & 0x7ff];
        *(float*)((int)&DAT_805e3050 + off) =
             *(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc));  // sin
        *(float*)((int)&DAT_805e3054 + off) = cosv;                          // cos
        remaining -= 1;
        off += 8;
    } while (remaining != 0);
}

// ============================================================================
// weather_flare_pool_init  (0x80355248)
// Reset the flare-slot pool: mark all 3 records free, then register the flare
// particle def (size/spin/color keyframes) and its emitter. Called from
// weather_system_reset.
// ============================================================================
void weather_flare_pool_init(void)
{
    int off = 0;
    DAT_80612e00 = 0;
    int i = 3;
    do {
        *(int*)((int)&DAT_805e30f0 + off) = -1;   // free
        off += 0x260;
        i -= 1;
    } while (i != 0);

    particle_def_create((double)FLOAT_8061adec, (double)FLOAT_8061adf0, &DAT_805e3810,
                        2, 1, 1, 0x33a, 0);
    particle_def_set_size_keyframes((double)FLOAT_8061adf0, (double)FLOAT_8061adf4,
                        (double)FLOAT_8061adf8, (double)FLOAT_8061adfc,
                        0x40507f00, 0x40507f00, 0x14141400);
    particle_def_set_spin_range((double)FLOAT_8061ae00, (double)FLOAT_8061ae04);
    particle_def_set_color_end((double)FLOAT_8061ae00, (double)FLOAT_8061add8,
                        (double)FLOAT_8061ae08, (double)FLOAT_8061adf4);
    particle_emitter_create((double)FLOAT_8061ae0c, (double)FLOAT_8061ade0,
                        (double)FLOAT_8061ae10, &DAT_805e38dc, &DAT_805e3810, 0, 3);
    {
        double col = (double)FLOAT_8061adf4;
        particle_emitter_set_color_range(col, col, (double)FLOAT_8061ae14, col);
    }
    FUN_80357888();
    DAT_80612de4 = 0;
    DAT_80612dfc = 0;
}

// ============================================================================
// weather_flare_pool_add  (0x80355348)
// Allocate a free flare slot (first record with +0x00 == -1) and initialise it
// from the caller's saved GPR block: a source vec3 written into four vec3 slots
// (+0x104, +0x110, +0x11c, +0x114-ish), spawn time, color id, and a duration.
// The caller passes the block through r28+ via __save_gpr_28 (arg0 high word
// is the block pointer, and param_2 is the duration).
// ============================================================================
void weather_flare_pool_add(int* srcBlock, double duration, int arg3, int arg4, int colorId)
{
    if (DAT_80612e00 >= 3) {
        return;
    }

    int slotOff = 0;
    int i = 3;
    int* rec = (int*)&DAT_805e30f0;
    int used = 0;
    for (;;) {
        rec = (int*)((int)&DAT_805e30f0 + slotOff);
        if (*rec == -1) break;
        used += 1;
        i -= 1;
        if (i == 0) break;
        slotOff += 0x260;
    }
    if (used >= 3) {
        return;
    }

    // 0x260-byte record cleared then populated.
    memset_wrapper((int)rec, 0, 0x260);

    float endColor = FLOAT_8061adf4;
    *(int*)((int)&DAT_805e3100 + slotOff) = colorId;
    float startScale = FLOAT_8061adf0;

    *rec = arg3;  // owner / valid marker (block ptr high word from caller)

    // Four vec3 copies of the source position.
    int base = slotOff - 0x7fa1cefc;  // == (int)&record + 0x104 region (matches decomp offsets)
    *(int*)(base + 0x00) = srcBlock[0];
    *(int*)(base + 0x04) = srcBlock[1];
    *(int*)(base + 0x08) = srcBlock[2];
    *(int*)(base + 0x18) = srcBlock[0];
    *(int*)(base + 0x1c) = srcBlock[1];
    *(int*)(base + 0x20) = srcBlock[2];
    *(int*)(base + 0x24) = srcBlock[0];
    *(int*)(base + 0x28) = srcBlock[1];
    *(int*)(base + 0x2c) = srcBlock[2];
    *(int*)(base + 0x0c) = srcBlock[0];
    *(int*)(base + 0x10) = srcBlock[1];
    *(int*)(base + 0x14) = srcBlock[2];

    *(float*)(base + 0x30) = (float)duration;      // -0x7fa1cecc
    *(float*)(base + 0x58) = endColor;             // -0x7fa1cea4
    *(float*)(base + 0x5c) = endColor;             // -0x7fa1cea0
    *(int*)  (base + 0x60) = -1;                   // -0x7fa1ce9c
    *(int*)  (base + 0x64) = -1;                   // -0x7fa1ce98
    *(float*)(base + 0x70) = startScale;           // -0x7fa1ce8c
    *(int*)((int)&DAT_805e30f4 + slotOff) = 0;

    *(int*)  (slotOff - 0x7fa1cd38) = -1;
    *(int*)  (slotOff - 0x7fa1ccb8) = -1;
    *(float*)(slotOff - 0x7fa1ccb4) = (float)duration;

    DAT_80612e00 += 1;
    (void)arg4;
}

// ============================================================================
// weather_flare_pool_remove  (0x803554b8)
// Free a flare slot: mark it free (-1), decrement the live count, and stop its
// looping sound if one is attached (+0x258, index 0x96 dwords).
// ============================================================================
void weather_flare_pool_remove(int* rec)
{
    DAT_80612e00 -= 1;
    *rec = -1;
    if (-1 < rec[0x96]) {
        sound_stop_handle();
        rec[0x96] = -1;
    }
}

// ============================================================================
// weather_flare_set_texture  (0x8035550c)
// Crossfade a flare slot to a new sprite texture. If already showing the target
// texture, nothing to do. Otherwise push the current texture into the "from"
// slot and start a fade over `dur`; if dur is not positive, snap instantly.
//   +0x74 from-tex   +0x78 to-tex   +0x7c from-user  +0x80 to-user
//   +0x84 fade-progress   +0x88 fade-duration
// ============================================================================
void weather_flare_set_texture(double dur, int slot, int texId, int userData)
{
    if (*(int*)(slot + 0x78) == texId) {
        return;
    }
    *(int*)(slot + 0x74) = *(int*)(slot + 0x78);
    double zero = (double)FLOAT_8061adf4;
    *(int*)(slot + 0x7c) = *(int*)(slot + 0x80);
    *(int*)(slot + 0x78) = texId;
    *(int*)(slot + 0x80) = userData;
    float one = FLOAT_8061adf0;

    if (zero < dur) {
        *(float*)(slot + 0x84) =
            (FLOAT_8061adf0 - *(float*)(slot + 0x84)) *
            (float)((double)*(float*)(slot + 0x88) / dur);
        *(float*)(slot + 0x88) = (float)dur;
        return;
    }
    *(float*)(slot + 0x84) = FLOAT_8061adf0;
    *(float*)(slot + 0x88) = one;
    *(int*)(slot + 0x74)   = texId;
    *(int*)(slot + 0x7c)   = userData;
}

// ============================================================================
// weather_flare_activate  (0x80355574)
// Put a flare slot into the active state (state 1). If it was in the uninit
// state (not 1 and not 2), seed its scale/alpha fields and release any prior
// sprite instance, then set the base flare texture (0x318) with a full-white
// tint via weather_flare_set_texture.
// ============================================================================
void weather_flare_activate(double startTime, int slot)
{
    float endColor = FLOAT_8061adf4;
    float one      = FLOAT_8061adf0;

    if ((slot != 0) && (*(int*)(slot + 4) != 2)) {
        if (*(int*)(slot + 4) != 1) {
            *(float*)(slot + 8)    = FLOAT_8061adf4;
            *(float*)(slot + 0x9c) = one;
            *(float*)(slot + 0x98) = endColor;
            FUN_80356c58(slot);
        }
        *(int*)(slot + 4) = 1;
        *(float*)(slot + 0xc) = (float)startTime;
        int tex = sprite_get_texture_id(0x318);
        weather_flare_set_texture((double)FLOAT_8061adf4, slot, tex, 0x7f7f7f7f);
    }
}
