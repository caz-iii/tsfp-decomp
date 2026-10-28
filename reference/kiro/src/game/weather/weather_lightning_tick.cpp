// Address: 0x8035560c | Name: weather_flare_begin_fade   | Date: 2026-09-12
// Address: 0x80355638 | Name: weather_lightning_spawn_bolt | Date: 2026-09-12
// Address: 0x80355bbc | Name: weather_lightning_tick_slot  | Date: 2026-09-12
// Address: 0x80356094 | Name: weather_lightning_tick_all   | Date: 2026-09-12
// Address: 0x80356100 | Name: weather_lightning_render_flash | Date: 2026-09-12
// Address: 0x8035661c | Name: weather_lightning_render_bolt  | Date: 2026-09-12
// Address: 0x80356c58 | Name: weather_lightning_clear_bolt   | Date: 2026-09-12
// Address: 0x80356ca0 | Name: weather_lightning_update_occlusion | Date: 2026-09-12
// Address: 0x80356e3c | Name: weather_lightning_decay_bolt   | Date: 2026-09-12
// Address: 0x80356f0c | Name: weather_lightning_render_flare | Date: 2026-09-12
//
// Weather lightning subsystem: per-slot state machine, bolt spawning, bolt and
// flash/flare rendering, and the lens-flare occlusion accumulation. Operates on
// the 3-slot flare/lightning pool (0x260-byte records at DAT_805e30f0) shared
// with weather_flare_pool.cpp. Bolt geometry is 6 segments of 20 vertices at
// 0x1e0-byte stride per slot; the flash/bolt renderers write straight to the GX
// FIFO (DAT_cc008000).

#include "common.h"

// ---- flare/lightning pool (shared with weather_flare_pool.cpp) -------------
extern int   DAT_805e30f0;      // record[0] +0x00
extern int   DAT_805e30f4;      // record[0] +0x04
extern int   DAT_805e3050;      // wave-table (sin at +0, cos at +4, 8-stride)
extern int   DAT_805e3054;
extern int   DAT_805e38dc;      // lightning particle emitter def

// ---- level / config globals ------------------------------------------------
extern int   DAT_80611b04;      // strike-volume id table base
extern int   DAT_80611b14;      // strike-volume count
extern int   DAT_80611b1c;      // per-strike "active" byte table base
extern int   DAT_80611748;      // viewport/render ctx (int[] view: +0=x0 +4=y0 +5=x1 +6=y1 ...)
extern int   DAT_80611d54;      // active camera/player ctx
extern int   DAT_80611d58;      // flare light count
extern int   DAT_80611d5c;      // flare occlusion state base
extern int   DAT_80611928;      // free-running frame counter
extern int   DAT_806109c0;      // screen height

extern int   DAT_80487490;      // weather type table base (0x20-stride)
extern int   DAT_80487494;
extern int   DAT_80487498;
extern int   DAT_8048749c;
extern int   DAT_804874a0;
extern int   DAT_804874a4;
extern int   DAT_804874a8;
extern int   DAT_804874ac;
extern int   DAT_803e0be8;      // world-axis basis constants for bolt orientation
extern int   DAT_803e0bec;
extern int   DAT_803e0bf0;
extern int   DAT_803e0bf4;
extern int   DAT_803e0bf8;
extern int   DAT_803e0bfc;

extern float DAT_80583228[];    // sin/cos lookup table (2048 f32)

// ---- GX FIFO write port ----------------------------------------------------
extern volatile float DAT_cc008000;

// ---- float / double constants ----------------------------------------------
extern double DOUBLE_8061add0;  // int->double bias
extern double DOUBLE_8061ae30;  // int->double bias (alt)
extern float  FLOAT_806108ec;   // base dt numerator
extern float  FLOAT_8061add8;   // 0.5-ish base
extern float  FLOAT_8061addc;
extern float  FLOAT_8061ade0;
extern float  FLOAT_8061ade4;
extern float  FLOAT_8061ade8;
extern float  FLOAT_8061adf0;   // 1.0
extern float  FLOAT_8061adf4;   // 0.0
extern float  FLOAT_8061adf8;
extern float  FLOAT_8061adfc;
extern float  FLOAT_8061ae04;
extern float  FLOAT_8061ae10;
extern float  FLOAT_8061ae18;
extern float  FLOAT_8061ae1c;
extern float  FLOAT_8061ae20;
extern float  FLOAT_8061ae24;
extern float  FLOAT_8061ae28;
extern float  FLOAT_8061ae2c;
extern float  FLOAT_8061ae38;
extern float  FLOAT_8061ae3c;
extern float  FLOAT_8061ae40;
extern float  FLOAT_8061ae44;
extern float  FLOAT_8061ae48;
extern float  FLOAT_8061ae4c;
extern float  FLOAT_8061ae50;
extern float  FLOAT_8061ae54;
extern float  FLOAT_8061ae58;
extern float  FLOAT_8061ae5c;
extern float  FLOAT_8061ae60;
extern float  FLOAT_8061ae64;

// ---- helpers (raw addresses / prior renames) -------------------------------
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);
extern int    weather_pick_strike_point(unsigned int volId, unsigned int det, int* out);
extern void   weather_flare_pool_remove(int* rec);

extern int    rng_counter_tick(void);
extern double game_frand(void);
extern double math_sqrt(double x);
extern void   mtx4_set_rotation_y(double angle, float* mtx);
extern void   mtx4_transform_vector(float* mtx, float* vec);
extern void   particle_emitter_spawn(int a, int* def, int b, float* pos, float* extra);
extern int    lightning_random_index(void);
extern void   sound_update_3d_source(double a, double b, double c, int* h, int* pos, int flags, int z);
extern int    sprite_get_texture_id(int ref);
extern unsigned int color_scale_lo(double scale, unsigned int color);
extern int    color_scale_lo_i(double scale, unsigned int color); // (unused alias guard)
extern unsigned int FUN_803825c0(double v);
extern int    lens_flare_occ_result_lookup(int occ, int ctx, int idx);
extern void   lens_flare_add_light(double x, double y, double z, int occ, int* slot, unsigned int idx);
extern unsigned int sprite_measure_text_region(int* pos, float* ox, float* oy, void* oz, int flag);

// GX state
extern void gx_set_sub_material_id(int id);
extern int  gx_get_sub_material_id(void);
extern void gx_set_blend_mode(int mode);
extern int  gx_get_blend_mode(void);
extern void gx_set_color_update_mode(void);
extern void gx_begin_textured_batch(int a, int b, int c, int d, int tex);
extern void gx_end_batch(void);
extern void gx_fill_rect_flags(int a, int x0, int y0, int x1, int y1, int f);
extern void weather_render_animated_grid(int a, unsigned int color, int c);

// render-pass begin/end frame helpers (return a packed ctx in the high word)
extern unsigned long long FUN_803826e4(void);
extern unsigned long long FUN_803826cc(void);
extern int  FUN_803826d0(void);
extern void FUN_803826f0(void);
extern void FUN_80382718(void);
extern void FUN_8038271c(void);
extern void FUN_80382730(void);
extern void FUN_8038273c(void);
extern void debug_draw_flush_a(void);
extern void debug_draw_flush_b(void);

// forward decls within this file
void weather_lightning_spawn_bolt(void);
void weather_lightning_tick_slot(void);
void weather_lightning_render_flash(void);
void weather_lightning_render_bolt(void);
void weather_lightning_update_occlusion(void);
void weather_lightning_decay_bolt(int rec);
extern void FUN_80357c5c(int rec);   // audio/tail helper (not in this batch)

// ============================================================================
// weather_flare_begin_fade  (0x8035560c)
// Put a flare/lightning slot into the fade-out state (state 3), stamping the
// fade start time. Seeds the fade progress on the first transition.
// ============================================================================
void weather_flare_begin_fade(double startTime, int slot)
{
    if (slot == 0) return;
    if (*(int*)(slot + 4) != 3) {
        *(float*)(slot + 8) = FLOAT_8061adf4;
    }
    *(int*)(slot + 4) = 3;
    *(float*)(slot + 0xc) = (float)startTime;
}

// ============================================================================
// weather_lightning_spawn_bolt  (0x80355638)
// Spawn a single lightning bolt for the current slot: pick a strike point from
// the level strike-volume table, build an orientation frame from the camera->
// strike direction, jitter three intermediate control points, and hand the
// bolt to the particle emitter. Slot state is reached via the saved GPR block.
// ============================================================================
void weather_lightning_spawn_bolt(void)
{
    unsigned long long saved = __save_gpr_28();
    int* slot = (int*)(unsigned int)(saved >> 0x20);
    int slotLow = (int)saved;

    int volCount = slot[4];
    int typeIdx  = *slot;
    int found = 0;
    float* work = (float*)(slot + slotLow * 0x1a + 0x28);
    float* typeRow = (float*)(&DAT_80487490 + typeIdx * 0x20);

    if ((0 < volCount) && (volCount <= DAT_80611b14)) {
        found = weather_pick_strike_point(*(unsigned int*)(DAT_80611b04 + volCount * 4), 1,
                                          (int*)(work + 0x14));
    }
    if (found == 0) { __restore_gpr_28(); return; }

    float b0 = (float)DAT_803e0be8, b1 = (float)DAT_803e0bec, b2 = (float)DAT_803e0bf0;
    float b3 = (float)DAT_803e0bf4, b4 = (float)DAT_803e0bf8, b5 = (float)DAT_803e0bfc;

    work[0x12] = FLOAT_8061adf4;
    {
        int r = rng_counter_tick();
        work[0x13] = FLOAT_8061add8 *
            ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae30) / FLOAT_8061ae18) +
            FLOAT_8061add8;
    }
    work[0xf] = work[0x16];
    work[0x10] = work[0x17];
    work[0x11] = work[0x18];

    double dx = (double)(work[0x10] - (float)slot[9]);
    double dxx = (double)(work[0xf] - (float)slot[8]);
    double dz = (double)(work[0x11] - (float)slot[10]);
    double len = math_sqrt((double)(float)(dz * dz +
                 (double)(float)(dxx * dxx + (double)(float)(dx * dx))));
    double invLen = (double)FLOAT_8061adf4;
    if ((double)FLOAT_8061adf4 < len) invLen = (double)(float)((double)FLOAT_8061adf0 / len);

    dx  = (double)(float)(dx * invLen);
    dxx = (double)(float)(dxx * invLen);
    double dzn = (double)(float)(dz * invLen);

    double tx, ty, tz;
    if (ABS(dx) <= (double)FLOAT_8061ae1c) {
        tx = dzn * (double)b0 - (double)(float)(dxx * (double)b2);
        ty = dx  * (double)b2 - (double)(float)(dzn * (double)b1);
        tz = dxx * (double)b1 - (double)(float)(dx  * (double)b0);
    } else {
        tx = dzn * (double)b3 - (double)(float)(dxx * (double)b4);
        ty = dx  * (double)b4 - (double)(float)(dzn * (double)b5);
        tz = dxx * (double)b5 - (double)(float)(dx  * (double)b3);
    }
    double tyf = (double)(float)ty, txf = (double)(float)tx, tzf = (double)(float)tz;

    double rad = (double)*typeRow;
    float lx = (float)(dxx * rad), ly = (float)(dx * rad), lz = (float)(dzn * rad);
    double bx = (double)(float)(txf * dzn - (double)(float)(tzf * dx));
    double by = (double)(float)(tzf * dxx - (double)(float)(tyf * dzn));
    double bz = (double)(float)(tyf * dx  - (double)(float)(txf * dxx));

    work[0] = lx; work[1] = ly; work[2] = lz;

    if ((*slot == 1) || (*slot == 3)) {
        int r = rng_counter_tick();
        float wsel = FLOAT_8061ade4;
        unsigned int idx = (unsigned int)(FLOAT_8061addc * FLOAT_8061ade0 * FLOAT_8061ade4 *
            ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae30) / FLOAT_8061ae18) +
            FLOAT_8061add8);
        work[0] = *typeRow * *(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc));
        work[1] = *(float*)(&DAT_804874a0 + typeIdx * 0x20) * DAT_80583228[idx & 0x7ff];
        work[2] = FLOAT_8061adf4;
        float mtx[17];
        mtx4_set_rotation_y((double)((wsel * (float)slot[0x11]) / FLOAT_8061ae20), mtx);
        mtx4_transform_vector(mtx, work);
    }

    // two jittered control-point passes
    double amp1 = (double)(FLOAT_8061ae24 * (float)(len - (double)*typeRow));
    double jit  = (double)(FLOAT_8061ae10 * (float)(len - (double)*typeRow));
    float sx = (float)(dxx * amp1), sy = (float)(dx * amp1), sz = (float)(dzn * amp1);
    {
        int off = 0;
        for (int i = 0; i < 2; i++) {
            double j1 = game_frand();
            double a = (double)(float)(jit * (double)(float)((double)FLOAT_8061ade0 * (double)(float)(j1 - (double)FLOAT_8061add8)));
            double j2 = game_frand();
            double bb = (double)(float)(jit * (double)(float)((double)FLOAT_8061ade0 * (double)(float)(j2 - (double)FLOAT_8061add8)));
            *(float*)((int)work + off + 0xc) = (float)(bb * bx + (double)(float)(a * by + (double)(sx + (float)slot[8] + work[0])));
            *(float*)((int)work + off + 0x10) = (float)(bb * by + (double)(float)(a * ty + (double)(sy + (float)slot[9] + work[1])));
            *(float*)((int)work + off + 0x14) = (float)(bb * bz + (double)(float)(a * tz + (double)(sz + (float)slot[10] + work[2])));
            off += 0xc;
        }
    }
    {
        double amp2 = (double)(float)((double)FLOAT_8061ae28 * len);
        float sx2 = (float)(dxx * amp2), sy2 = (float)(dx * amp2), sz2 = (float)(dzn * amp2);
        int off = 0;
        for (int i = 0; i < 2; i++) {
            double j1 = game_frand();
            double a = (double)(float)(jit * (double)(float)((double)FLOAT_8061ade0 * (double)(float)(j1 - (double)FLOAT_8061add8)));
            double j2 = game_frand();
            double bb = (double)(float)(jit * (double)(float)((double)FLOAT_8061ade0 * (double)(float)(j2 - (double)FLOAT_8061add8)));
            *(float*)((int)work + off + 0x24) = (float)(bb * bx + (double)(float)(a * by + (double)(sx2 + (float)slot[8] + work[0])));
            *(float*)((int)work + off + 0x28) = (float)(bb * by + (double)(float)(a * ty + (double)(sy2 + (float)slot[9] + work[1])));
            *(float*)((int)work + off + 0x2c) = (float)(bb * bz + (double)(float)(a * tz + (double)(sz2 + (float)slot[10] + work[2])));
            off += 0xc;
        }
    }
    {
        double e = (double)FLOAT_8061ae2c;
        float ex = (float)(e * dxx), ey = (float)(e * dx), ez = (float)(e * dzn);
        float extra[3] = { ex, ey, ez };
        particle_emitter_spawn(0, &DAT_805e38dc, 0, work + 0xf, extra);
    }
    (void)ly; (void)lx; (void)lz; (void)by;
    __restore_gpr_28();
}

// ============================================================================
// weather_lightning_tick_slot  (0x80355bbc)
// Per-frame state machine for one lightning slot (state 1 grow, 2 hold, 3
// fade). Advances the fade timer, updates 3D sound, crossfades textures, and
// when the strike volume is "active" re-spawns bolts on a randomized cadence.
// ============================================================================
void weather_lightning_tick_slot(void)
{
    int* rec = (int*)(unsigned int)(__save_gpr_28() >> 0x20);

    double dt = (double)(FLOAT_806108ec *
        (FLOAT_8061adf0 /
         (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_8061add0)));

    *(float*)&rec[2] = (float)((double)*(float*)&rec[2] + dt);

    if (rec[1] == 1) {
        if (FLOAT_8061adf4 < *(float*)&rec[3]) {
            float f = *(float*)&rec[2] / *(float*)&rec[3];
            if (f > FLOAT_8061adf0) f = FLOAT_8061adf0;
            *(float*)&rec[0x26] = f;
        }
        if (*(float*)&rec[3] <= *(float*)&rec[2]) {
            *(float*)&rec[2] = FLOAT_8061adf4;
            *(float*)&rec[3] = FLOAT_8061adf4;
            rec[1] = 2;
            *(float*)&rec[0x26] = FLOAT_8061adf0;
        }
    } else if (rec[1] == 3) {
        *(float*)&rec[0x26] = FLOAT_8061adf0;
        if (FLOAT_8061adf4 < *(float*)&rec[3]) {
            float f = *(float*)&rec[2] / *(float*)&rec[3];
            if (f > FLOAT_8061adf0) f = FLOAT_8061adf0;
            *(float*)&rec[0x27] = FLOAT_8061adf0 - f;
            float g = *(float*)&rec[2] / *(float*)&rec[3];
            if (g > FLOAT_8061adf0) g = FLOAT_8061adf0;
            *(float*)&rec[0x26] = FLOAT_8061add8 * g + FLOAT_8061adf0;
        }
        if (*(float*)&rec[3] <= *(float*)&rec[2]) {
            weather_flare_pool_remove(rec);
            __restore_gpr_28();
            return;
        }
    }

    // texture crossfade progress (+0x74/0x78/0x7c/0x80 fields at rec[0x1d..0x20])
    if ((rec[0x1d] != rec[0x1e]) && ((double)FLOAT_8061adf4 < (double)*(float*)&rec[0x22])) {
        *(float*)&rec[0x21] = *(float*)&rec[0x21] + (float)(dt / (double)*(float*)&rec[0x22]);
        if (FLOAT_8061adf0 <= *(float*)&rec[0x21]) {
            *(float*)&rec[0x21] = FLOAT_8061adf0;
            rec[0x1d] = rec[0x1e];
            rec[0x1f] = rec[0x20];
        }
    }

    // fade in/out level at rec[0x25]
    if (rec[0x1e] == -1) {
        float v = -(float)((double)FLOAT_8061ae38 * dt - (double)*(float*)&rec[0x25]);
        if (v < FLOAT_8061adf4) v = FLOAT_8061adf4;
        *(float*)&rec[0x25] = v;
    } else {
        float v = (float)((double)FLOAT_8061ae38 * dt + (double)*(float*)&rec[0x25]);
        if (FLOAT_8061adf0 < v) v = FLOAT_8061adf0;
        *(float*)&rec[0x25] = v;
    }

    if ((double)FLOAT_8061adf4 < (double)*(float*)&rec[0x97]) {
        sound_update_3d_source((double)*(float*)&rec[0x26], (double)*(float*)&rec[0x97],
                               (double)FLOAT_8061adf0, rec + 0x96, rec + 8, 0x81, 0);
    }

    if (*(char*)(DAT_80611b1c + rec[4]) == '\0') {
        weather_lightning_decay_bolt((int)rec);
    } else {
        *(float*)&rec[0x1b] = (float)((double)*(float*)&rec[0x1b] + dt);
        if (*(float*)&rec[0x1c] <= *(float*)&rec[0x1b]) {
            rec[0xb] = rec[8];
            rec[0xc] = rec[9];
            double amp = (double)(FLOAT_8061ae3c * (FLOAT_8061adf0 - *(float*)&rec[0x25]));
            rec[0xd] = rec[10];
            int r;
            r = rng_counter_tick();
            *(float*)&rec[0xe] = (float)(amp * (double)(FLOAT_8061ade0 *
                ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae30) / FLOAT_8061ae18 - FLOAT_8061add8)) + (double)*(float*)&rec[5]);
            r = rng_counter_tick();
            *(float*)&rec[0xf] = (float)(amp * (double)(FLOAT_8061ade0 *
                ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae30) / FLOAT_8061ae18 - FLOAT_8061add8)) + (double)*(float*)&rec[6]);
            r = rng_counter_tick();
            *(float*)&rec[0x10] = (float)(amp * (double)(FLOAT_8061ade0 *
                ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae30) / FLOAT_8061ae18 - FLOAT_8061add8)) + (double)*(float*)&rec[7]);
            *(float*)&rec[0x1b] = FLOAT_8061adf4;
            r = rng_counter_tick();
            *(float*)&rec[0x1c] = FLOAT_8061ae04 *
                ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae30) / FLOAT_8061ae18) + FLOAT_8061ae40;
        }
        float t = *(float*)&rec[0x1b] / *(float*)&rec[0x1c];
        *(float*)&rec[8]  = t * (*(float*)&rec[0xe] - *(float*)&rec[0xb]) + *(float*)&rec[0xb];
        *(float*)&rec[9]  = t * (*(float*)&rec[0xf] - *(float*)&rec[0xc]) + *(float*)&rec[0xc];
        *(float*)&rec[10] = t * (*(float*)&rec[0x10] - *(float*)&rec[0xd]) + *(float*)&rec[0xd];

        int off = 0;
        for (int i = 0; i < 3; i++) {
            *(float*)((int)rec + off + 0xe8) = (float)((double)*(float*)((int)rec + off + 0xe8) + dt);
            if (*(float*)((int)rec + off + 0xec) <= *(float*)((int)rec + off + 0xe8)) {
                weather_lightning_spawn_bolt();
            }
            unsigned int ri = (unsigned int)lightning_random_index();
            *(unsigned int*)((int)rec + off + 0x104) = ri;
            off += 0x68;
        }

        *(float*)&rec[0x23] = (float)((double)*(float*)&rec[0x23] - dt);
        if (*(float*)&rec[0x23] <= FLOAT_8061adf4) {
            *(float*)&rec[0x23] = *(float*)&rec[0x23] + FLOAT_8061adf0;
        }
        *(float*)&rec[0x24] = (float)((double)FLOAT_8061adfc * dt + (double)*(float*)&rec[0x24]);
        if (FLOAT_8061adf0 <= *(float*)&rec[0x24]) {
            *(float*)&rec[0x24] = *(float*)&rec[0x24] - FLOAT_8061adf0;
        }
        weather_lightning_update_occlusion();
    }

    if (-1 < rec[0x76]) {
        FUN_80357c5c((int)rec);
    }
    __restore_gpr_28();
}

// ============================================================================
// weather_lightning_tick_all  (0x80356094)
// Tick every occupied+enabled slot in the 3-slot pool once per frame.
// ============================================================================
void weather_lightning_tick_all(void)
{
    FUN_803826f0();
    int off = 0;
    for (int i = 0; i < 3; i++) {
        if ((*(int*)((int)&DAT_805e30f0 + off) != -1) &&
            (*(int*)((int)&DAT_805e30f4 + off) != 0)) {
            weather_lightning_tick_slot();
        }
        off += 0x260;
    }
    FUN_8038273c();
}

// ============================================================================
// weather_lightning_render_flash  (0x80356100)
// Render the full-screen lightning flash / glow ring for the current slot.
// Color and radius come from the type table selected by the slot sub-mode; the
// 20-segment ring is emitted straight to the GX FIFO, and mode 2 also fills the
// viewport with an animated grid overlay.
// ============================================================================
void weather_lightning_render_flash(void)
{
    unsigned long long packed = FUN_803826e4();
    int* rec = (int*)(unsigned int)(packed >> 0x20);
    int mode = (int)packed;

    double innerA = (double)FLOAT_8061adf4;
    int typeOff = *rec * 0x20;
    double one = (double)FLOAT_8061adf0;
    int tex = -1;
    unsigned int colA = 0;
    double outerA = innerA;
    double cB, rB, rA;

    if (mode == 2) {
        gx_set_color_update_mode();
        tex = sprite_get_texture_id(1);
        colA = 0x7f7f7fff;
        cB = (double)FLOAT_8061add8;
        rB = (double)*(float*)(&DAT_804874ac + typeOff);
        rA = (double)*(float*)(&DAT_8048749c + typeOff);
    } else {
        cB = innerA; rB = innerA; rA = innerA;
        if (mode < 2) {
            if (mode == 0) {
                tex = sprite_get_texture_id(1);
                colA = 0x64647f7f;
                cB = (double)FLOAT_8061add8;
                rB = (double)(FLOAT_8061ae44 * *(float*)(&DAT_804874a0 + typeOff));
                rA = (double)(FLOAT_8061ae44 * *(float*)(&DAT_80487490 + typeOff));
            } else if (-1 < mode) {
                tex = sprite_get_texture_id(1);
                colA = 0x5a647f7f;
                cB = (double)FLOAT_8061add8;
                rB = (double)*(float*)(&DAT_804874a4 + typeOff);
                rA = (double)*(float*)(&DAT_80487494 + typeOff);
            }
        } else if (mode == 4 || mode < 4) {
            tex = sprite_get_texture_id(0x325);
            unsigned int m10 = (unsigned int)DAT_806109c0 * 10;
            colA = 0x6e757f0f;
            unsigned int m5 = (unsigned int)DAT_806109c0 * 5;
            if (mode < 4) one = (double)FLOAT_8061ade0;
            innerA = (double)(FLOAT_8061adf0 -
                (float)((double)CONCAT44(0x43300000, (unsigned int)(DAT_80611928 - (DAT_80611928 / (int)m5) * m5) ^ 0x80000000) - DOUBLE_8061add0) /
                (float)((double)CONCAT44(0x43300000, m5 ^ 0x80000000) - DOUBLE_8061add0));
            outerA = (double)(FLOAT_8061adf0 -
                (float)((double)CONCAT44(0x43300000, (unsigned int)(DAT_80611928 - (DAT_80611928 / (int)m10) * m10) ^ 0x80000000) - DOUBLE_8061add0) /
                (float)((double)CONCAT44(0x43300000, m10 ^ 0x80000000) - DOUBLE_8061add0));
            cB = (double)FLOAT_8061ae44;
            rB = (double)*(float*)(&DAT_804874a8 + typeOff);
            rA = (double)*(float*)(&DAT_80487498 + typeOff);
        }
    }

    unsigned int c0 = color_scale_lo((double)*(float*)&rec[0x27], colA);
    unsigned int c1 = color_scale_lo((double)FLOAT_8061adf4, colA);
    gx_begin_textured_batch(2, 0x92, 0, 0x28, tex);
    unsigned int step = 0;
    int n = 0x14;
    int wOff = 0;
    do {
        unsigned int neg = step ^ 0x80000000;
        step += 1;
        float radScale = *(float*)&rec[0x26];
        float sx = radScale * FLOAT_8061ae48 * (float)((double)*(float*)((int)&DAT_805e3050 + wOff) * (double)*(float*)(&DAT_80487490 + typeOff));
        float sy = radScale * FLOAT_8061ae48 * (float)((double)*(float*)((int)&DAT_805e3050 + wOff) * rA);
        float cx = radScale * FLOAT_8061ae48 * (float)((double)*(float*)((int)&DAT_805e3054 + wOff) * (double)*(float*)(&DAT_804874a0 + typeOff));
        DAT_cc008000 = cx * (float)rec[0x12] + sx * (float)rec[0x15] + (float)rec[8];
        float v = (float)((double)((float)((double)CONCAT44(0x43300000, neg) - DOUBLE_8061add0) / FLOAT_8061ade8) + outerA);
        DAT_cc008000 = cx * (float)rec[0x13] + sx * (float)rec[0x16] + (float)rec[9];
        float cy = radScale * FLOAT_8061ae48 * (float)((double)*(float*)((int)&DAT_805e3054 + wOff) * rB);
        DAT_cc008000 = cx * (float)rec[0x14] + sx * (float)rec[0x17] + (float)rec[10];
        *(unsigned int*)&DAT_cc008000 = c0;
        DAT_cc008000 = v;
        DAT_cc008000 = (float)(cB + innerA);
        DAT_cc008000 = cy * (float)rec[0x12] + sy * (float)rec[0x15] + (float)rec[8];
        DAT_cc008000 = cy * (float)rec[0x13] + sy * (float)rec[0x16] + (float)rec[9];
        DAT_cc008000 = cy * (float)rec[0x14] + sy * (float)rec[0x17] + (float)rec[10];
        *(unsigned int*)&DAT_cc008000 = c1;
        DAT_cc008000 = v;
        DAT_cc008000 = (float)(one + innerA);
        n -= 1;
        wOff += 8;
    } while (n != 0);
    gx_end_batch();

    if (mode == 2) {
        int savedBlend = gx_get_blend_mode();
        int savedMat = gx_get_sub_material_id();
        gx_set_color_update_mode();
        weather_render_animated_grid(2, 0x7f7f7f7f, 0);
        gx_fill_rect_flags(8, *(int*)DAT_80611748, *(int*)(DAT_80611748 + 0x14),
                           *(int*)(DAT_80611748 + 4) + 1, *(int*)(DAT_80611748 + 0x18) + 1, 0);
        gx_set_blend_mode(savedBlend);
        gx_set_sub_material_id(savedMat);
    }
    FUN_80382730();
}

// ============================================================================
// weather_lightning_render_bolt  (0x8035661c)
// Build a 19x20 grid of bolt-segment vertices from the slot's control points +
// sin/cos jitter into a local vertex cache, then emit the bolt in one or two
// passes (from/to textures) as textured triangle strips to the GX FIFO.
// ============================================================================
void weather_lightning_render_bolt(void)
{
    unsigned long long packed = FUN_803826cc();
    int* rec = (int*)(unsigned int)(packed >> 0x20);
    int mode = (int)packed;
    int typeIdx = *rec;

    static float verts[1900];  // 19*20 * 5 floats

    int rowBase = 0;
    for (int row = 0; row < 0x13; row++) {
        unsigned int col = 0;
        double rn = (double)(CONCAT44(0x43300000, (unsigned int)row) ^ 0x80000000);
        float rowPhase = FLOAT_8061ae4c * ((float)(rn - DOUBLE_8061add0) / FLOAT_8061ade8);
        unsigned int rowIdx = (unsigned int)(FLOAT_8061addc * rowPhase + FLOAT_8061add8);
        int inner = 0x14;
        do {
            int vi = col + rowBase;
            double cn = (double)CONCAT44(0x43300000, col ^ 0x80000000);
            float u = (float)(cn - DOUBLE_8061add0) / FLOAT_8061ade8;
            float uu = FLOAT_8061ade4 * u;
            int ci = (int)(FLOAT_8061addc * (FLOAT_8061ade0 * uu + FLOAT_8061ade0 * FLOAT_8061ade4 * (float)rec[0x24]) + FLOAT_8061add8);
            float w1 = u * (float)rec[0x26] * DAT_80583228[rowIdx & 0x7ff] * *(float*)(&DAT_804874a0 + typeIdx * 0x20);
            unsigned int ci2 = (unsigned int)(FLOAT_8061addc * (FLOAT_8061ae50 * uu + FLOAT_8061ade0 * FLOAT_8061ade4 * (float)rec[0x23]) + FLOAT_8061add8);
            float w2 = u * (float)rec[0x26] * *(float*)((int)&DAT_80583228 + ((rowIdx + 0x200) * 4 & 0x1ffc)) * *(float*)(&DAT_80487490 + typeIdx * 0x20);
            unsigned int ci3 = (unsigned int)(FLOAT_8061addc * (rowPhase + FLOAT_8061adf8 * *(float*)((int)&DAT_80583228 + ((ci + 0x200) * 4 & 0x1ffc))) + FLOAT_8061add8);
            float s8 = DAT_80583228[ci3 & 0x7ff];
            float s9 = *(float*)((int)&DAT_80583228 + ((ci3 + 0x200) * 4 & 0x1ffc));
            float uvV = FLOAT_8061adf4;
            if (u < FLOAT_8061adf4) {
                int ci4 = (int)(FLOAT_8061addc * (uu + FLOAT_8061adf4) + FLOAT_8061add8);
                uvV = FLOAT_8061adf0 * *(float*)((int)&DAT_80583228 + ((ci4 + 0x200) * 4 & 0x1ffc));
                if (FLOAT_8061ae44 < u) uvV = uvV * ((u - FLOAT_8061ae44) / FLOAT_8061adfc);
            }
            float m16 = (float)rec[0x16], m9 = (float)rec[9], m17 = (float)rec[0x17];
            float mag = FLOAT_8061ae54 + FLOAT_8061ae10 * uvV + FLOAT_8061adfc * DAT_80583228[ci2 & 0x7ff];
            float m10 = (float)rec[10], m13 = (float)rec[0x13], m14 = (float)rec[0x14];
            verts[vi * 5]     = w1 * (float)rec[0x12] + w2 * (float)rec[0x15] + (float)rec[8];
            verts[vi * 5 + 1] = w1 * m13 + w2 * m16 + m9;
            col += 1;
            verts[vi * 5 + 2] = w1 * m14 + w2 * m17 + m10;
            verts[vi * 5 + 3] = mag * FLOAT_8061add8 * s9 * u + FLOAT_8061add8;
            verts[vi * 5 + 4] = -(mag * FLOAT_8061add8 * s8 * u - FLOAT_8061add8);
            inner -= 1;
        } while (inner != 0);
        rowBase += 0x14;
    }

    if (mode != 0) gx_set_sub_material_id(1);
    gx_set_blend_mode(2);

    for (int pass = 0; pass < (int)(2 - (unsigned int)(mode == 0)); pass++) {
        int tex; unsigned int col;
        if (pass == 0) { tex = rec[0x1d]; col = rec[0x1f]; }
        else           { tex = rec[0x1e]; col = rec[0x20]; }
        float fade;
        if (pass == 0) fade = FLOAT_8061adf0 - *(float*)&rec[0x21];
        else           fade = *(float*)&rec[0x21];
        fade = *(float*)&rec[0x27] * fade;
        if (mode == 0) { tex = sprite_get_texture_id(0); col = 0x7f00007f; fade = FLOAT_8061adf0; }

        if ((tex != -1) && (FLOAT_8061adf4 < fade)) {
            double alpha = (double)((float)((double)CONCAT44(0x43300000, col & 0xff) - DOUBLE_8061ae30) * fade);
            unsigned int a8 = FUN_803825c0(alpha);
            col = col & 0xffffff00 | a8;
            unsigned int seg = 0;
            col = (col & 0xff00) << 8 | col >> 0x18 | col >> 8 & 0xff00;
            unsigned int baseCol = a8 << 0x18 | col;
            do {
                unsigned int c0 = baseCol, c1 = baseCol;
                if (mode != 0) {
                    double sn0 = (double)CONCAT44(0x43300000, seg ^ 0x80000000);
                    double sn1 = (double)CONCAT44(0x43300000, (seg + 1) ^ 0x80000000);
                    float t0 = (float)(sn0 - DOUBLE_8061add0) / FLOAT_8061ade8;
                    float t1 = (float)(sn1 - DOUBLE_8061add0) / FLOAT_8061ade8;
                    double p0, p1;
                    if (mode == 1) { p0 = -(double)(t0 * t0 - FLOAT_8061adf0); p1 = -(double)(t1 * t1 - FLOAT_8061adf0); }
                    else           { p0 = (double)(t0 * t0); p1 = (double)(t1 * t1); }
                    c0 = col | (int)(alpha * p0) << 0x18;
                    c1 = (int)(alpha * p1) << 0x18 | col;
                }
                gx_begin_textured_batch(2, 0x92, 0, 0x28, tex);
                int strip = 0;
                int cnt = 0x14;
                do {
                    int rr = (strip % 0x13) * 0x14;
                    int a = seg + rr;
                    int b = seg + 1 + rr;
                    DAT_cc008000 = verts[a * 5];
                    DAT_cc008000 = verts[a * 5 + 1];
                    DAT_cc008000 = verts[a * 5 + 2];
                    *(unsigned int*)&DAT_cc008000 = c0 << 0x18 | (c0 & 0xff00) << 8 | c0 >> 8 & 0xff00 | c0 >> 0x18;
                    DAT_cc008000 = verts[a * 5 + 3];
                    DAT_cc008000 = verts[a * 5 + 4];
                    DAT_cc008000 = verts[b * 5];
                    DAT_cc008000 = verts[b * 5 + 1];
                    DAT_cc008000 = verts[b * 5 + 2];
                    *(unsigned int*)&DAT_cc008000 = c1 << 0x18 | (c1 & 0xff00) << 8 | c1 >> 8 & 0xff00 | c1 >> 0x18;
                    strip += 1;
                    DAT_cc008000 = verts[b * 5 + 3];
                    DAT_cc008000 = verts[b * 5 + 4];
                    cnt -= 1;
                } while (cnt != 0);
                gx_end_batch();
                seg += 1;
            } while ((int)seg < 0x13);
        }
    }
    FUN_80382718();
}

// ============================================================================
// weather_lightning_clear_bolt  (0x80356c58)
// Zero the per-slot bolt vertex-alpha buffer (6 segments x 20 bytes at
// +0x1e0-stride) and clear the "dirty" flag at +0x1dc.
// ============================================================================
void weather_lightning_clear_bolt(int rec)
{
    int seg = 0;
    for (int i = 0; i < 6; i++) {
        int idx = 0;
        int cnt = 0x14;
        do {
            int at = idx + 0x1e0;
            idx += 1;
            *(unsigned char*)(rec + seg + at) = 0;
            cnt -= 1;
        } while (cnt != 0);
        seg += 0x14;
    }
    *(int*)(rec + 0x1dc) = 0;
}

// ============================================================================
// weather_lightning_update_occlusion  (0x80356ca0)
// For each active flare light, raise or lower each bolt segment's alpha toward
// visible/occluded based on the lens-flare occlusion query result.
// ============================================================================
void weather_lightning_update_occlusion(void)
{
    int ctx = (int)FUN_803826e4();
    int segBase = 0;
    int occStride = 0;
    for (int light = 0; light < DAT_80611d58; light++) {
        char active = *(char*)(DAT_80611d5c + occStride + *(int*)(ctx + 0x10) + 0xd2c);
        if (active != '\0') {
            int idx = 0;
            int recSeg = ctx + segBase;
            do {
                int occ = lens_flare_occ_result_lookup(*(int*)(DAT_80611d54 + 0x214), ctx, idx);
                int at = idx + 0x1e0;
                if (occ == 0) {
                    double h = (double)(CONCAT44(0x43300000, (unsigned int)DAT_806109c0) ^ 0x80000000);
                    int d = (int)(FLOAT_8061ae58 * FLOAT_8061ade0 * FLOAT_806108ec *
                            (FLOAT_8061adf0 / (float)(h - DOUBLE_8061add0)));
                    if ((int)(unsigned int)*(unsigned char*)(recSeg + at) < d) {
                        *(unsigned char*)(recSeg + at) = 0;
                    } else {
                        *(unsigned char*)(recSeg + at) = *(unsigned char*)(recSeg + at) - (char)d;
                    }
                } else {
                    double h = (double)(CONCAT44(0x43300000, (unsigned int)DAT_806109c0) ^ 0x80000000);
                    int d = (int)(FLOAT_8061ae58 * FLOAT_8061ae5c * FLOAT_806108ec *
                            (FLOAT_8061adf0 / (float)(h - DOUBLE_8061add0)));
                    if ((int)(unsigned int)*(unsigned char*)(recSeg + at) < 0xff - d) {
                        *(unsigned char*)(recSeg + at) = *(unsigned char*)(recSeg + at) + (char)d;
                    } else {
                        *(unsigned char*)(recSeg + at) = 0xff;
                    }
                }
                idx += 1;
            } while (idx < 0x14);
        }
        segBase += 0x14;
        occStride += 0xffc;
    }
    FUN_80382730();
}

// ============================================================================
// weather_lightning_decay_bolt  (0x80356e3c)
// When the slot's bolt is dirty, decay every segment's alpha by a frame-scaled
// amount, clearing the dirty flag first and re-raising it while any segment is
// still non-zero.
// ============================================================================
void weather_lightning_decay_bolt(int rec)
{
    float k = FLOAT_8061ae58;
    float one = FLOAT_8061adf0;
    float sc = FLOAT_8061ade0;
    float dtn = FLOAT_806108ec;
    if (*(int*)(rec + 0x1dc) == 0) return;

    int seg = 0;
    double h = (double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_8061add0;
    *(int*)(rec + 0x1dc) = 0;
    int d = (int)(k * sc * dtn * (one / (float)h));
    for (int i = 0; i < 6; i++) {
        int recSeg = rec + seg;
        int idx = 0;
        int cnt = 0x14;
        do {
            int at = idx + 0x1e0;
            if (*(char*)(recSeg + at) != '\0') {
                *(int*)(rec + 0x1dc) = 1;
                if ((int)(unsigned int)*(unsigned char*)(recSeg + at) < d) {
                    *(unsigned char*)(recSeg + at) = 0;
                } else {
                    *(unsigned char*)(recSeg + at) = *(unsigned char*)(recSeg + at) - (char)d;
                }
            }
            idx += 1;
            cnt -= 1;
        } while (cnt != 0);
        seg += 0x14;
    }
}

// ============================================================================
// weather_lightning_render_flare  (0x80356f0c)
// Render the lens-flare light-shaft billboards that trail the bolt across the
// screen, projecting each of 20 flare points to screen space, registering them
// with the lens-flare occlusion system, and clipping the shaft to the viewport
// before emitting to the GX FIFO.
// ============================================================================
void weather_lightning_render_flare(void)
{
    int* rec = (int*)FUN_803826d0();
    int typeIdx = *rec;
    int tex = sprite_get_texture_id(0x28a);
    unsigned int baseCol = color_scale_lo((double)FLOAT_8061adf4, 0x7f7f7f7f);

    float ox, oy; unsigned int oz;
    unsigned int flags = sprite_measure_text_region(rec + 8, &ox, &oy, &oz, 1);
    if ((flags & 2) != 0) { FUN_8038271c(); return; }

    gx_set_sub_material_id(1);
    gx_set_blend_mode(2);
    debug_draw_flush_a();
    gx_begin_textured_batch(2, 0x92, 0, 0x28, tex);

    double uScale = (double)FLOAT_8061ade8;
    double glow = (double)FLOAT_8061ae60;
    unsigned int i = 0;
    double occScale = (double)FLOAT_8061ae64;
    int wOff = 0;
    double vScale = (double)FLOAT_8061ae50;
    double bias = DOUBLE_8061add0;
    double biasAlt = DOUBLE_8061ae30;
    do {
        unsigned int m7 = (unsigned int)DAT_806109c0 * 7;
        double v = (double)(float)((double)(float)((double)CONCAT44(0x43300000, i ^ 0x80000000) - bias) / uScale);
        unsigned int occByte = (unsigned int)*(unsigned char*)((int)rec + *(int*)(DAT_80611d54 + 0x214) * 0x14 + i + 0x1e0);

        double sx = (double)(float)(glow * (double)((float)rec[0x26] * *(float*)((int)&DAT_805e3050 + wOff) * *(float*)(&DAT_80487490 + typeIdx * 0x20)));
        double uv = (double)((float)((double)CONCAT44(0x43300000, (unsigned int)(DAT_80611928 - (DAT_80611928 / (int)m7) * m7) ^ 0x80000000) - bias) /
                             (float)((double)CONCAT44(0x43300000, m7 ^ 0x80000000) - bias));
        double cx = (double)(float)(glow * (double)((float)rec[0x26] * *(float*)((int)&DAT_805e3054 + wOff) * *(float*)(&DAT_804874a0 + typeIdx * 0x20)));
        unsigned int col = color_scale_lo((double)((float)rec[0x27] *
                          (float)(occScale * (double)(float)((double)CONCAT44(0x43300000, occByte) - biasAlt))), 0x7f7f7f7f);

        float px = (float)(cx * (double)(float)rec[0x12] + (double)(float)(sx * (double)(float)rec[0x15] + (double)(float)rec[8]));
        float py = (float)(cx * (double)(float)rec[0x13] + (double)(float)(sx * (double)(float)rec[0x16] + (double)(float)rec[9]));
        float pz = (float)(cx * (double)(float)rec[0x14] + (double)(float)(sx * (double)(float)rec[0x17] + (double)(float)rec[10]));

        int pos3[3]; ((float*)pos3)[0] = px; ((float*)pos3)[1] = py; ((float*)pos3)[2] = pz;
        float sox, soy; unsigned int soz;
        unsigned int sflags = sprite_measure_text_region(pos3, &sox, &soy, (void*)&soz, 1);
        if ((sflags & 3) == 0) {
            lens_flare_add_light((double)sox, (double)soy, (double)*(float*)&soz,
                                 *(int*)(DAT_80611d54 + 0x214), rec, i);
        }
        float dxs = sox - ox;
        float dys = soy - oy;
        float clipX, clipY;
        if ((sflags & 2) == 0) {
            clipX = ox;
            if (FLOAT_8061adf4 <= dys) {
                if (FLOAT_8061adf4 != dxs) {
                    clipX = ox + ((float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 0x18) ^ 0x80000000) - bias) - oy) / (dys / dxs);
                }
                clipY = (float)*(int*)(DAT_80611748 + 0x18);
            } else {
                if (FLOAT_8061adf4 != dxs) {
                    clipX = ox + ((float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 0x14) ^ 0x80000000) - bias) - oy) / (dys / dxs);
                }
                clipY = (float)*(int*)(DAT_80611748 + 0x14);
            }
            float x0 = (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)DAT_80611748 ^ 0x80000000) - bias);
            if (x0 <= clipX) {
                float x1 = (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 4) ^ 0x80000000) - bias);
                if (x1 < clipX) {
                    clipX = x1;
                    clipY = -((dys / dxs) * (ox - x1) - oy);
                }
            } else {
                clipX = x0;
                clipY = -((dys / dxs) * (ox - x0) - oy);
            }
        } else {
            clipX = sox + dxs;
            clipY = soy + dys;
        }

        DAT_cc008000 = sox;
        i += 1;
        DAT_cc008000 = soy;
        wOff += 8;
        DAT_cc008000 = FLOAT_8061adf4;
        *(unsigned int*)&DAT_cc008000 = col;
        DAT_cc008000 = (float)uv;
        DAT_cc008000 = (float)(vScale * v);
        DAT_cc008000 = clipX;
        DAT_cc008000 = clipY;
        DAT_cc008000 = FLOAT_8061adf4;
        *(unsigned int*)&DAT_cc008000 = baseCol;
        DAT_cc008000 = (float)uv;
        DAT_cc008000 = (float)(vScale * v);
    } while ((int)i < 0x14);

    gx_end_batch();
    debug_draw_flush_b();
    FUN_8038271c();
}

// ============================================================================
// Appended batch_1013: lightning flare-pass dispatcher, per-frame render walk,
// and the flare-record reposition helper.
// ============================================================================

// ---- additional per-file externs for the render-pass dispatcher ------------
extern int   DAT_805e3100;      // slot +0x10 : occlusion-state index
extern int   DAT_80612250;      // render mtx-valid flag
extern int   DAT_80612258;      // render mtx-index
extern int   DAT_8058e3cc;      // shared transposed view matrix scratch
extern float FLOAT_8061ae68;    // flare vector scale
extern float FLOAT_8061ae6c;    // glow-blur radius

extern void   gx_get_color_update_packed(void);
extern void   gx_set_color_update_packed(void);
extern void   gx_set_color_write_mask(int mask);
extern void   mtx4_transpose(float* src, float* dst);
extern double math_atan2(double y, double x);
extern void   weather_render_glow_blur_pass(double x0, double y0, double x1, double y1,
                                            double a, double b, double c, double d,
                                            int e, int f, int g);

// ============================================================================
// weather_lightning_render_flare_pass  (0x803573f0)
// Per-slot flare/bolt render dispatcher. Builds the flare basis vectors (from
// the camera for slot type 0, else a fixed set rotated by the slot's yaw),
// transposes the view matrix into the shared scratch, and issues the flash,
// bolt and flare draws for the current pass (mode 0 = full glow with blur, mode
// 1/2 = bolt-only into a colour-write mask). Reached via saved-GPR block:
// high word = slot record, low word = pass mode.
// ============================================================================
void weather_lightning_render_flare_pass(void)
{
    unsigned long long packed = __save_gpr_28();
    int* rec = (int*)(unsigned int)(packed >> 0x20);
    int mode = (int)packed;

    gx_get_color_update_packed();
    int savedBlend = gx_get_blend_mode();
    float scale = FLOAT_8061ae68;
    float zero = FLOAT_8061adf4;
    float one  = FLOAT_8061adf0;

    if (*rec == 0) {
        rec[0x12] = *(int*)(DAT_80611d54 + 0x364);
        rec[0x13] = *(int*)(DAT_80611d54 + 0x368);
        rec[0x14] = *(int*)(DAT_80611d54 + 0x36c);
        rec[0x15] = *(int*)(DAT_80611d54 + 0x370);
        rec[0x16] = *(int*)(DAT_80611d54 + 0x374);
        rec[0x17] = *(int*)(DAT_80611d54 + 0x378);
        rec[0x18] = (int)(scale * *(float*)(DAT_80611d54 + 0x334));
        rec[0x19] = (int)(scale * *(float*)(DAT_80611d54 + 0x338));
        rec[0x1a] = (int)(scale * *(float*)(DAT_80611d54 + 0x33c));
    } else {
        rec[0x12] = (int)FLOAT_8061adf4;
        rec[0x13] = (int)one;
        rec[0x14] = (int)zero;
        rec[0x15] = (int)one;
        rec[0x16] = (int)zero;
        rec[0x17] = (int)zero;
        rec[0x18] = (int)zero;
        rec[0x19] = (int)zero;
        rec[0x1a] = (int)one;
        if (*rec == 3) {
            double a = math_atan2((double)*(float*)(DAT_80611d54 + 0x334),
                                  (double)*(float*)(DAT_80611d54 + 0x33c));
            rec[0x11] = (int)((FLOAT_8061ae20 * (float)a) / FLOAT_8061ade4);
        }
        float mtx[16];
        mtx4_set_rotation_y((double)((FLOAT_8061ade4 * (float)rec[0x11]) / FLOAT_8061ae20), mtx);
        mtx4_transform_vector(mtx, (float*)(rec + 0x15));
        mtx4_transform_vector(mtx, (float*)(rec + 0x18));
    }

    gx_set_sub_material_id(2);
    gx_set_blend_mode(2);
    mtx4_transpose(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;
    gx_set_color_update_mode();

    if (mode == 0) {
        weather_lightning_render_flash();
        weather_lightning_render_flash();
        weather_lightning_render_flash();
    }
    if ((mode == 1) || (mode == 2)) {
        if (mode == 1) gx_set_color_write_mask(1);
        gx_set_color_update_mode();
        gx_set_sub_material_id(0);
        weather_lightning_render_bolt();
        gx_set_color_write_mask(0);
        gx_set_color_update_mode();
    }
    if (mode == 0) {
        mtx4_transpose(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;
        weather_lightning_render_bolt();
        gx_set_color_update_mode();
        weather_lightning_render_bolt();
        weather_lightning_render_flare();
        gx_set_color_update_mode();
        unsigned int x0 = (unsigned int)*(int*)DAT_80611748 ^ 0x80000000;
        unsigned int y0 = (unsigned int)*(int*)(DAT_80611748 + 0x14) ^ 0x80000000;
        unsigned int x1 = (unsigned int)*(int*)(DAT_80611748 + 4) ^ 0x80000000;
        unsigned int y1 = (unsigned int)*(int*)(DAT_80611748 + 0x18) ^ 0x80000000;
        weather_render_glow_blur_pass(
            (double)(float)((double)CONCAT44(0x43300000, x0) - DOUBLE_8061add0),
            (double)(float)((double)CONCAT44(0x43300000, y0) - DOUBLE_8061add0),
            (double)(float)((double)CONCAT44(0x43300000, x1) - DOUBLE_8061add0),
            (double)(float)((double)CONCAT44(0x43300000, y1) - DOUBLE_8061add0),
            (double)FLOAT_8061ae6c, (double)FLOAT_8061ae6c, (double)FLOAT_8061ae50,
            (double)FLOAT_8061ae50, 4, 2, 1);
        gx_set_sub_material_id(0xe);
        gx_fill_rect_flags(8, *(int*)DAT_80611748, *(int*)(DAT_80611748 + 0x14),
                           *(int*)(DAT_80611748 + 4) + 1, *(int*)(DAT_80611748 + 0x18) + 1, 0);
    }

    gx_set_color_update_packed();
    gx_set_blend_mode(savedBlend);
    __restore_gpr_28();
}

// ============================================================================
// weather_lightning_render_all  (0x80357738)
// Per-frame render walk: draw the flare pass for every occupied+enabled slot
// whose occlusion-state entry (camera ctx + slot occ index + 0xd2c) is set.
// Called from particle_system_render_dispatch.
// ============================================================================
void weather_lightning_render_all(void)
{
    FUN_803826f0();
    int off = 0;
    for (int i = 0; i < 3; i++) {
        if ((*(int*)((int)&DAT_805e30f0 + off) != -1) &&
            (*(int*)((int)&DAT_805e30f4 + off) != 0) &&
            (*(char*)(DAT_80611d54 + *(int*)((int)&DAT_805e3100 + off) + 0xd2c) != '\0')) {
            weather_lightning_render_flare_pass();
        }
        off += 0x260;
    }
    FUN_8038273c();
}

// ============================================================================
// weather_flare_move_to  (0x803577c0)
// Reposition a flare/bolt record to a new center, translating its stored center
// (+0x20), origin (+0x14), and two control points (+0x2c, +0x38) by the same
// delta so the whole bolt shape moves rigidly.
// ============================================================================
void weather_flare_move_to(int rec, float* newPos)
{
    float dx = newPos[0] - *(float*)(rec + 0x20);
    float dy = newPos[1] - *(float*)(rec + 0x24);
    float dz = newPos[2] - *(float*)(rec + 0x28);
    *(float*)(rec + 0x20) += dx;
    *(float*)(rec + 0x24) += dy;
    *(float*)(rec + 0x28) += dz;
    *(float*)(rec + 0x14) += dx;
    *(float*)(rec + 0x18) += dy;
    *(float*)(rec + 0x1c) += dz;
    *(float*)(rec + 0x2c) += dx;
    *(float*)(rec + 0x30) += dy;
    *(float*)(rec + 0x34) += dz;
    *(float*)(rec + 0x38) += dx;
    *(float*)(rec + 0x3c) += dy;
    *(float*)(rec + 0x40) += dz;
}
