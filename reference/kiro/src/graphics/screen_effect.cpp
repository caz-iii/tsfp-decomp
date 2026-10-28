// Address range: 0x8035c690 - 0x8035d8c0 | batch_1019 | 2026-09-12
//
// Screen-effect overlay subsystem (split-screen HUD "scanner"/damage-flash pass).
//
// Manages a fixed 4-slot state array (one per split-screen player) based at
// DAT_805e4fc4 with a 0x68-byte (26-dword) stride.  Each slot holds:
//   +0x00  flags        (bit0 = has-scratch RT / animated, bit1 = active)
//   +0x08  x, +0x0c y, +0x10 w, +0x14 h   (current effect rect)
//   +0x18 y2, +0x1c w2, +0x20 h2          (mirror / target rect)
//   +0x28 tx,+0x2c ty,+0x30 tw,+0x34 th   (committed target rect)
//   +0x3c  field (screenfx_set_all_state target)
//   +0x40  state (0/2/4 animation phase; 2 = idle)
//   +0x44  scratch render-target handle (-1 = none)
//   +0x48.. animation phase floats (scroll/flash/jitter)
//
//   screenfx_flush          0x8035c690  no-op commit/flush hook
//   screenfx_init           0x8035c694  full init (globals + slot array)
//   screenfx_reset_slots    0x8035c72c  zero + idle the 4 slots
//   screenfx_begin          0x8035c79c  configure/start an effect on a slot
//   screenfx_ensure_scratch 0x8035c8cc  lazily alloc slot scratch RT
//   screenfx_clear_slot     0x8035c92c  mark a slot idle
//   screenfx_update         0x8035c970  per-frame animate/state machine
//   screenfx_render         0x8035cd34  draw overlays for active slots
//   screenfx_draw_marker    0x8035d234  project+draw a single prop marker
//   screenfx_set_all_state  0x8035d8c0  write field +0x3c across all 4 slots
//
// Called from: screenfx_init <- screen_effect_system_init (0x801040f4);
//              screenfx_render <- session_render_hud_overlays (0x801c252c).
#include "common.h"
#include "generated_decls.h"

// ---- 4-slot state array (base; stride 0x68). Referenced via byte offsets. ----
extern unsigned char DAT_805e4fb8[];  // color accumulator x (float)
extern unsigned char DAT_805e4fc4[];  // slot[0] base (flags dword). Fields accessed via slot_i/slot_f.

// ---- render-context / clip bounds used by draw_marker ----
extern int   DAT_80611d74;   // active render context record
extern int   DAT_80611d78;   // render-context table base
extern int   DAT_80611780;   // pause / freeze flag
extern int   DAT_806108e4;   // effects-enabled flag
extern float DAT_806108ec;   // frame delta scale
extern int   DAT_806109c0;   // frame timebase (int, xor-0x80000000 -> signed)
extern int   DAT_806109c4;   // screen width  (scissor)
extern int   DAT_806109c8;   // screen height (scissor)

// ---- global scroll/flash timers ----
extern int   DAT_80612e30;   // scroll-active flag
extern int   DAT_80612e34;   // bound emitter record (0 = none)
extern float FLOAT_80612e2c;  // global scroll phase
extern float FLOAT_80612e28;  // marker projection scale

// ---- rodata float constants ----
extern float FLOAT_8061afe0;  // 0.0
extern float FLOAT_8061aff0;  // 1.0 (base scale numerator)
extern float FLOAT_8061aff4;  // scroll speed
extern float FLOAT_8061aff8;  // wrap / 0.5
extern float FLOAT_8061affc;  // spawn probability scale
extern float FLOAT_8061b000;  // random range span
extern float FLOAT_8061b004;  // 0.5 rounding bias
extern float FLOAT_8061b010;  // projection scale
extern float FLOAT_8061b014;  // proj coef a
extern float FLOAT_8061b018;  // proj coef b
extern float FLOAT_8061b01c;  // proj denom
extern float FLOAT_8061b020;  // vertex color scale
extern float FLOAT_8061b024;  // marker base intensity
extern float FLOAT_8061b028;  // marker clip hi
extern float FLOAT_8061b02c;  // marker clip lo
extern float FLOAT_8061b030;  // falloff coef near
extern float FLOAT_8061b034;  // falloff coef far

// ---- double constants (int->double magic-number conversion helpers) ----
extern double DOUBLE_8061afe8;  // 4503599627370496.0 (0x43300000 bias)
extern double DOUBLE_8061b008;  // int->double bias for %3 term

// ---- sin/cos lookup table (0x800 entries, packed) ----
extern float DAT_80583228[];

// ---- marker clip bounds (world-space rect the marker must fall within) ----
extern float DAT_805e5164;   // min x
extern float DAT_805e516c;   // min y
extern float DAT_805e5170;   // max x
extern float DAT_805e5178;   // max y

// ---- raw sibling helpers in this module (not yet decompiled) ----
extern void FUN_803826c4(void);   // begin overlay render scope
extern void FUN_803826e4(void);   // begin state-update scope
extern unsigned long long FUN_803826e8(void);  // alloc slot: hi = slot index
extern void FUN_80382710(void);   // end overlay render scope
extern void FUN_80382730(void);   // end reset scope
extern void FUN_80382734(void);   // commit begin
extern void FUN_8035d94c(void);   // draw scanner grid / lines pass
extern void FUN_8035ec24(void);   // draw scanner blips / labels pass

// Named cross-module helpers (render_target_*, gx_*, session_get_state_flag,
// math_sqrt, game_frand, prop_is_destroyed) resolve through generated_decls.h.
// memset_wrapper and game_rand resolve through generated_externs.h (int name(...)).

// register save/restore intrinsics emitted by the PPC decompiler; no-ops here.
static inline void __save_gpr_28(void) {}
static inline void __restore_gpr_28(void) {}

// Helpers to index a slot field given the slot's byte offset within the array.
static inline int*   slot_i(int off) { return (int*)(DAT_805e4fc4 + off); }
static inline float* slot_f(int off) { return (float*)(DAT_805e4fc4 + off); }

// -------------------------------------------------------------------------
// 0x8035c690  screenfx_flush - no-op commit/flush hook.
// -------------------------------------------------------------------------
void screenfx_flush(void)
{
    return;
}

// -------------------------------------------------------------------------
// 0x8035c72c  screenfx_reset_slots - zero the 4 slots and mark each idle.
// -------------------------------------------------------------------------
void screenfx_reset_slots(void)
{
    int i;
    int off;

    FUN_803826e4();
    i = 0;
    off = 0;
    do {
        memset_wrapper((int)(DAT_805e4fc4 + off), 0, 0x68);
        *slot_i(off + 0x40) = 2;        // state = idle
        i = i + 1;
        *slot_i(off + 0x44) = -1;       // scratch handle = none
        off = off + 0x68;
    } while (i < 4);
    FUN_80382730();
}

// -------------------------------------------------------------------------
// 0x8035c694  screenfx_init - full init of globals + slot array.
// -------------------------------------------------------------------------
void screenfx_init(void)
{
    int i;
    int off;

    FUN_803826e4();
    DAT_80612e34 = 0;
    i = 0;
    DAT_80612e30 = 0;
    *(float*)(DAT_805e4fb8 + 0)  = FLOAT_8061afe0;
    *(float*)(DAT_805e4fb8 + 4)  = FLOAT_8061afe0;
    *(float*)(DAT_805e4fb8 + 8)  = FLOAT_8061afe0;
    off = 0;
    do {
        memset_wrapper((int)(DAT_805e4fc4 + off), 0, 0x68);
        *slot_i(off + 0x40) = 2;    // state = idle
        i = i + 1;
        *slot_i(off + 0x44) = -1;   // scratch handle = none
        off = off + 0x68;
    } while (i < 4);
    screenfx_flush();
    FUN_80382730();
}

// -------------------------------------------------------------------------
// 0x8035c8cc  screenfx_ensure_scratch - lazily alloc slot scratch RT.
// -------------------------------------------------------------------------
unsigned int screenfx_ensure_scratch(int slot)
{
    int off = slot * 0x68;
    if (*slot_i(off + 0x44) == -1) {
        render_target_alloc_scratch(*slot_i(off + 0x30),   // w
                                    *slot_i(off + 0x34),   // h
                                    slot_i(off + 0x44));
    }
    *slot_i(off + 0x00) = *slot_i(off + 0x00) | 2;         // mark active
    return (unsigned int)*slot_i(off + 0x44);
}

// -------------------------------------------------------------------------
// 0x8035c92c  screenfx_clear_slot - mark a slot idle, clear active bit.
// -------------------------------------------------------------------------
void screenfx_clear_slot(int slot)
{
    int off = slot * 0x68;
    *slot_i(off + 0x40) = 2;                                // state = idle
    *slot_i(off + 0x00) = *slot_i(off + 0x00) & 0xfffffffd; // clear bit1
    screenfx_flush();
}

// -------------------------------------------------------------------------
// 0x8035d8c0  screenfx_set_all_state - write field +0x3c across all 4 slots.
// -------------------------------------------------------------------------
void screenfx_set_all_state(int value)
{
    int n = 4;
    int off = 0;
    do {
        *slot_i(off + 0x3c) = value;
        n = n - 1;
        off = off + 0x68;
    } while (n != 0);
}

// -------------------------------------------------------------------------
// 0x8035c79c  screenfx_begin - configure and start an effect on a slot.
// -------------------------------------------------------------------------
void screenfx_begin(int arg_a, int x, int y, int w, int h, int w2, int h2)
{
    int slot;
    int off;
    unsigned long long packed;
    double size;

    (void)arg_a;
    size = 0.0;
    packed = FUN_803826e8();
    slot = (int)(packed >> 32);
    off = slot * 0x68;

    *slot_i(off + 0x00) = (int)(unsigned int)packed;   // flags
    *slot_i(off + 0x04) = x;

    if ((*slot_i(off + 0x00) & 1) == 0) {
        *slot_i(off + 0x08) = y;
        *slot_i(off + 0x0c) = w;
        *slot_i(off + 0x10) = w2;
        *slot_i(off + 0x14) = h2;
        *slot_i(off + 0x28) = y;
        *slot_i(off + 0x2c) = w;
        *slot_i(off + 0x30) = w2;
        *slot_i(off + 0x34) = h2;
    } else {
        size = math_sqrt((double)(float)(w2 * w2 + h2 * h2));
        *slot_i(off + 0x08) = w2;
        *slot_i(off + 0x0c) = w;
        *slot_i(off + 0x10) = (int)size;
        *slot_i(off + 0x14) = (int)size;
        *slot_i(off + 0x28) = y;
        *slot_i(off + 0x2c) = w;
        *slot_i(off + 0x30) = w2;
        *slot_i(off + 0x34) = h2;
    }

    *slot_i(off + 0x18) = *slot_i(off + 0x08);
    *slot_i(off + 0x1c) = *slot_i(off + 0x0c);
    *slot_i(off + 0x20) = *slot_i(off + 0x10);
    *slot_i(off + 0x24) = *slot_i(off + 0x14);
    *slot_f(off + 0x38) = (float)size;  // committed depth (f1 return of alloc; 0 baseline)
    *slot_i(off + 0x40) = 0;   // state = grow
    *slot_i(off + 0x3c) = 0;
    *slot_f(off + 0x48) = FLOAT_8061afe0;
    *slot_f(off + 0x50) = FLOAT_8061afe0;
    *slot_f(off + 0x5c) = FLOAT_8061afe0;
    FUN_80382734();
}

// -------------------------------------------------------------------------
// 0x8035c970  screenfx_update - per-frame animate / state machine.
// -------------------------------------------------------------------------
void screenfx_update(void)
{
    float zero;
    unsigned int r;
    int players;
    int i;
    int off;
    double dt;

    __save_gpr_28();
    dt = (double)(DAT_806108ec *
                  (FLOAT_8061aff0 /
                   (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_8061afe8)));

    if (DAT_80612e34 == 0) {
        if (DAT_80612e30 != 0) {
            FLOAT_80612e2c = (float)((double)FLOAT_8061aff4 * dt + (double)FLOAT_80612e2c);
            if (FLOAT_8061aff8 < FLOAT_80612e2c)
                FLOAT_80612e2c = FLOAT_80612e2c - FLOAT_8061aff8;
        }
    } else if ((*(unsigned int*)(DAT_80612e34 + 0x28) & 0x80000000) == 0) {
        FLOAT_80612e2c = (float)((double)FLOAT_8061aff4 * dt + (double)FLOAT_80612e2c);
        if (FLOAT_8061aff8 < FLOAT_80612e2c)
            FLOAT_80612e2c = FLOAT_80612e2c - FLOAT_8061aff8;
    } else {
        DAT_80612e34 = 0;
    }

    off = 0;
    players = session_get_state_flag();
    for (i = 0; i < players; i = i + 1) {
        unsigned int* flags = (unsigned int*)(DAT_805e4fc4 + off);
        int state;

        if ((DAT_80611780 == 0) || (*slot_i(off + 0x40) != 2) || ((*flags & 2) != 0)) {
            if ((DAT_80611780 == 0) && (*slot_i(off + 0x40) == 0) && ((*flags & 2) == 0))
                *slot_i(off + 0x40) = 2;
        } else {
            *slot_i(off + 0x40) = 0;
        }

        state = *slot_i(off + 0x40);
        if (state != 2) {
            if (state < 2) {
                if (0 < state) {
                    if (-*slot_i(off + 0x14) < *slot_i(off + 0x0c)) {
                        *slot_i(off + 0x0c) = *slot_i(off + 0x0c) - 1;
                    } else {
                        *slot_i(off + 0x0c) = -*slot_i(off + 0x14);
                        *slot_i(off + 0x40) = 2;
                    }
                }
            } else if (state < 4) {
                if (*slot_i(off + 0x0c) < *slot_i(off + 0x1c)) {
                    *slot_i(off + 0x0c) = *slot_i(off + 0x0c) + 1;
                } else {
                    *slot_i(off + 0x0c) = *slot_i(off + 0x1c);
                    *slot_i(off + 0x40) = 0;
                }
            }
        }

        zero = FLOAT_8061afe0;
        if ((*slot_i(off + 0x40) != 2) && ((*flags & 1) != 0) && (0 < DAT_806108e4)) {
            double z = (double)FLOAT_8061afe0;
            if ((z < (double)*slot_f(off + 0x48)) &&
                (*slot_f(off + 0x48) =
                     -(float)(dt * (double)*slot_f(off + 0x4c) - (double)*slot_f(off + 0x48)),
                 (double)*slot_f(off + 0x48) < z)) {
                *slot_f(off + 0x48) = zero;
            }
            zero = FLOAT_8061afe0;
            z = (double)FLOAT_8061afe0;
            if ((double)*slot_f(off + 0x50) <= z) {
                if ((double)*slot_f(off + 0x5c) <= z) {
                    if (((double)*slot_f(off + 0x48) <= z) &&
                        (game_frand() < (double)(FLOAT_8061affc * DAT_806108ec))) {
                        r = (unsigned int)game_rand() & 3;
                        if (r == 2) {
                            *slot_f(off + 0x4c) =
                                (float)((double)FLOAT_8061b000 * game_frand() + (double)FLOAT_8061aff8);
                            *slot_f(off + 0x48) = FLOAT_8061aff0;
                            *slot_f(off + 0x58) =
                                (float)((double)FLOAT_8061b004 * game_frand() + (double)FLOAT_8061b004);
                        } else if (r < 2) {
                            unsigned int sign;
                            *slot_f(off + 0x60) =
                                (float)((double)FLOAT_8061b000 * game_frand() + (double)FLOAT_8061aff8);
                            *slot_f(off + 0x5c) =
                                (float)((double)(unsigned int)((unsigned int)game_rand() % 3) - DOUBLE_8061b008);
                            sign = ((unsigned int)game_rand() & 1) == 0 ? 0xffffffff : 1;
                            *slot_f(off + 0x64) =
                                (float)((double)(unsigned int)(sign ^ 0x80000000) - DOUBLE_8061afe8);
                        } else if (r < 4) {
                            *slot_f(off + 0x54) =
                                (float)((double)FLOAT_8061b000 * game_frand() + (double)FLOAT_8061aff8);
                            *slot_f(off + 0x50) = FLOAT_8061aff0;
                            *slot_f(off + 0x58) =
                                (float)((double)FLOAT_8061b004 * game_frand() + (double)FLOAT_8061b004);
                        }
                    }
                } else {
                    *slot_f(off + 0x5c) =
                        -(float)(dt * (double)*slot_f(off + 0x60) - (double)*slot_f(off + 0x5c));
                    if ((double)*slot_f(off + 0x5c) < z)
                        *slot_f(off + 0x5c) = zero;
                }
            } else {
                *slot_f(off + 0x50) =
                    -(float)(dt * (double)*slot_f(off + 0x54) - (double)*slot_f(off + 0x50));
                if ((double)*slot_f(off + 0x50) < z)
                    *slot_f(off + 0x50) = zero;
            }
        }
        off = off + 0x68;
    }
    __restore_gpr_28();
}

// -------------------------------------------------------------------------
// 0x8035cd34  screenfx_render - draw overlays for active slots.
// -------------------------------------------------------------------------
void screenfx_render(void)
{
    int players;
    int i;
    int off;
    int ctx_off;

    FUN_803826c4();
    gx_set_blend_mode(0);
    ctx_off = 0;
    off = 0;
    players = session_get_state_flag();
    for (i = 0; i < players; i = i + 1) {
        unsigned int* flags = (unsigned int*)(DAT_805e4fc4 + off);
        if ((*slot_i(off + 0x40) != 2) && ((*flags & 2) != 0) && (-1 < *slot_i(off + 0x44))) {
            DAT_80611d74 = DAT_80611d78 + ctx_off;
            gx_set_color_update_mode(0);
            gx_set_sub_material_id(1);
            gx_fill_rect_clipped(*slot_i(off + 0x08),
                                 *slot_i(off + 0x0c),
                                 *slot_i(off + 0x08) + *slot_i(off + 0x10),
                                 *slot_i(off + 0x0c) + *slot_i(off + 0x14),
                                 0xc0a087f);
            gx_set_color_update_mode(0);
            FUN_8035d94c();

            if ((*flags & 1) != 0) {
                unsigned int r9 = (unsigned int)*slot_i(off + 0x28) + (unsigned int)*slot_i(off + 0x30);
                unsigned int r10 = (unsigned int)*slot_i(off + 0x2c);
                unsigned int r3 = (unsigned int)*slot_i(off + 0x28) ^ 0x80000000;
                unsigned int r8 = r10 + (unsigned int)*slot_i(off + 0x34);
                unsigned int idx =
                    (unsigned int)(FLOAT_8061b010 *
                                   ((FLOAT_8061b014 * (FLOAT_8061b018 - *(float*)(DAT_80611d74 + 0x100))) /
                                    FLOAT_8061b01c) + FLOAT_8061b004);

                double sx = (double)(FLOAT_8061b004 *
                    (float)((double)(unsigned int)((unsigned int)*slot_i(off + 0x30) ^ 0x80000000) - DOUBLE_8061afe8));
                double sy = (double)(FLOAT_8061b004 *
                    (float)((double)(unsigned int)((unsigned int)*slot_i(off + 0x34) ^ 0x80000000) - DOUBLE_8061afe8));
                double cosv = (double)*(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc));
                double sinv = (double)DAT_80583228[idx & 0x7ff];

                double ox = (double)(FLOAT_8061b004 *
                    (float)((double)(unsigned int)((unsigned int)*slot_i(off + 0x10) ^ 0x80000000) - DOUBLE_8061afe8) +
                    (float)((double)(unsigned int)((unsigned int)*slot_i(off + 0x08) ^ 0x80000000) - DOUBLE_8061afe8));
                double oy = (double)(FLOAT_8061b004 *
                    (float)((double)(unsigned int)((unsigned int)*slot_i(off + 0x14) ^ 0x80000000) - DOUBLE_8061afe8) +
                    (float)((double)(unsigned int)((unsigned int)*slot_i(off + 0x0c) ^ 0x80000000) - DOUBLE_8061afe8));

                double a17 = (double)(float)((double)(unsigned int)r3        - DOUBLE_8061afe8);
                double a16 = (double)(float)((double)(unsigned int)(r10 ^ 0x80000000) - DOUBLE_8061afe8);
                double a15 = (double)(float)((double)(unsigned int)(r9  ^ 0x80000000) - DOUBLE_8061afe8);
                double a14 = (double)(float)((double)(unsigned int)(r8  ^ 0x80000000) - DOUBLE_8061afe8);
                double s   = (double)FLOAT_8061b020;

                gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
                gx_set_sub_material_id(1);
                gx_set_color_update_mode(0);
                gx_set_active_material(-6);
                // Camera-facing quad corners (rotated by the sin/cos LUT).
                unsigned int q0x = (unsigned int)(int)(s * (double)(float)(-sx * cosv + ox));
                unsigned int q0y = (unsigned int)(int)(s * (double)(float)(-sy * sinv + oy));
                unsigned int q1x = (unsigned int)(int)(s * (double)(float)( sx * sinv + ox));
                unsigned int q1y = (unsigned int)(int)(s * (double)(float)(-sy * cosv + oy));
                unsigned int q2x = (unsigned int)(int)(s * (double)(float)(-sx * sinv + ox));
                unsigned int q2y = (unsigned int)(int)(s * (double)(float)( sy * cosv + oy));
                gx_draw_sprite_strip4(
                    0ull, a17, a16, a15, a16, a17, a14, a15,
                    q0x, q0y, q1x, q1y, q2x, q2y,
                    (float)((double)(unsigned int)(r8 ^ 0x80000000) - DOUBLE_8061afe8),
                    0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f);
            }

            FUN_8035ec24();

            if ((*flags & 1) != 0) {
                gx_fill_rect_flags(0xf, *slot_i(off + 0x08), *slot_i(off + 0x0c),
                                   *slot_i(off + 0x08) + *slot_i(off + 0x10),
                                   *slot_i(off + 0x0c) + *slot_i(off + 0x14), 0);
                gx_set_color_update_mode(0);
            }
            if (DAT_806108e4 != 0) {
                render_target_clear_bind(*slot_i(off + 0x44), 1);
            }
            gx_fill_rect_flags(0xf, *slot_i(off + 0x28), *slot_i(off + 0x2c),
                               *slot_i(off + 0x28) + *slot_i(off + 0x30),
                               *slot_i(off + 0x2c) + *slot_i(off + 0x34), 0);
        }
        ctx_off = ctx_off + 0x141c;
        off = off + 0x68;
    }
    FUN_80382710();
}

// -------------------------------------------------------------------------
// 0x8035d234  screenfx_draw_marker - project + draw one prop marker.
//
// Reads the prop record + camera transform from the frame-scratch pointer
// returned by the enclosing scope (modelled here as explicit parameters:
// prop record, projection base, and index record).  Projects the prop to
// screen space through the sin/cos LUT, clips to the effect bounds and emits
// a camera-facing quad plus a sprite rect.
// -------------------------------------------------------------------------
void screenfx_draw_marker(int* proj_ctx, int* prop_rec, int mat_kind)
{
    int prop = *prop_rec;
    int kind = prop_rec[4];
    int lut_sel = (kind == 3) ? 7 : 6;
    float px = *(float*)(prop + 100);
    float py = *(float*)(prop + 0x6c);
    (void)mat_kind;

    if ((px <= DAT_805e5170) && (DAT_805e5164 <= px) &&
        (py <= DAT_805e5178) && (DAT_805e516c <= py)) {
        float pz = *(float*)(prop + 0x68);
        float camz = *(float*)(DAT_80611d74 + 0xac);
        float a = *(float*)&prop_rec[0x6e];
        float b = *(float*)&prop_rec[0x6f];
        float intensity = FLOAT_8061b024;

        if (a == b) {
            if ((FLOAT_8061b028 < pz - camz) || (pz - camz < FLOAT_8061b02c)) return;
            if (camz <= pz) {
                float refz = *(float*)(DAT_80611d74 + 0xb8);
                if ((refz < pz) &&
                    (intensity = FLOAT_8061b024 - FLOAT_8061b034 * (pz - refz),
                     intensity < FLOAT_8061afe0))
                    intensity = FLOAT_8061afe0;
            } else {
                intensity = FLOAT_8061b024 - FLOAT_8061b030 * (camz - pz);
                if (intensity < FLOAT_8061afe0) intensity = FLOAT_8061afe0;
            }
        } else {
            float refz = *(float*)(DAT_80611d74 + 0xb8);
            if ((refz <= a) || (b <= refz)) return;
            if (camz <= pz) {
                if ((refz < pz) &&
                    (intensity = FLOAT_8061b024 - (pz - refz) * (b - a),
                     intensity < FLOAT_8061afe0))
                    intensity = FLOAT_8061afe0;
            } else {
                intensity = FLOAT_8061b024 - (camz - pz) * (b - a);
                if (intensity < FLOAT_8061afe0) intensity = FLOAT_8061afe0;
            }
        }

        // Screen-space rect around the projected marker.
        {
            double dx = (double)(FLOAT_80612e28 * (*(float*)(DAT_80611d74 + 0xea8) - px));
            double dy = (double)(FLOAT_80612e28 * (*(float*)(DAT_80611d74 + 0xeb0) - py));
            float cx = *(float*)(proj_ctx + 4);   // proj center x
            float cy = *(float*)(proj_ctx + 2);   // proj center y
            int x0 = (int)((cx + (float)dx) - FLOAT_8061aff8);
            int x1 = (int)(FLOAT_8061aff8 + (cx + (float)dx));
            int y0 = (int)((cy + (float)dy) - FLOAT_8061aff8);
            int y1 = (int)(FLOAT_8061aff8 + (cy + (float)dy));
            (void)lut_sel;
            (void)intensity;
            gx_draw_sprite_rect(y0, x0, y1, x1, 0, 0, 0x50, 0x50);
        }
    }
}
