// Batch: batch_967 | Module: arcade/challenge editor camera + cursor animation
// Address range: 0x8033fef8 - 0x80340c58
// Date: 2026-09-12
//
// The arcade/challenge editor's camera control and placement-cursor animation (continues batches
// 964-966). Reads the pad triggers / analog stick to pan and orbit the camera, steps the camera
// height on button presses (with an eased transition), eases the cursor spin/tilt toward its target
// (wrapping the spin and clamping the tilt), rebuilds the camera basis each frame, and resets the
// whole cursor/camera context on editor entry. State lives in the DAT_80612d68 word-indexed context
// (spin at +0x3c, tilt at +0x40, height-anim flags at +0x80/+0x98/+0x9c) and the shared camera
// globals DAT_80561c88..9c.
// Callers: aveditor_camera_update via aveditor_cursor_anim_step (from aveditor_screen_tick);
// aveditor_cursor_reset from arcade_edit_screen_enter.
//
//   aveditor_camera_pan_input   0x8033fef8  pan the camera along the view axis from the L/R triggers.
//   aveditor_camera_orbit_input 0x803401d4  orbit the camera from the analog stick.
//   aveditor_camera_height_step 0x80340568  step the camera height on button press (eased anim).
//   aveditor_cursor_spin_update 0x80340770  ease the cursor spin toward target, wrap + clamp tilt.
//   aveditor_camera_update      0x80340908  rebuild the camera basis + apply pan/orbit/height.
//   aveditor_cursor_anim_step   0x80340b98  auto-spin/squash the cursor, then run the camera update.
//   aveditor_cursor_reset       0x80340c58  reset the cursor/camera context (editor entry).
//
// NON-MATCHING: these reproduce the decompiler's structure; the reciprocal-sqrt Newton-refinement
// vector normalisations are expressed via the project math_sqrt helper, and the int->double
// magic-constant conversions are reproduced via CONCAT44. The functions are emitted with the
// _savegpr_/_restgpr_ helpers (__save_gpr_28/FUN_803826f0/FUN_8038273c) threading an fp vector pair
// through the spilled registers; expressed as plain float[3] locals seeded from those regs
// (Compile-Clean rule 3). The mtx4_* helpers are declared file-locally with call-site-accurate types
// (this TU includes only common.h). DAT_80612d68 is treated as int* / float* (word index).

#include "common.h"

extern "C" {

extern int    DAT_80612be4;   // editor context base
extern int*   DAT_80612d68;   // cursor/camera context (word-indexed)

// shared camera globals (contiguous block from c88)
extern float  DAT_80561c88;   // cursor/camera target X
extern float  DAT_80561c8c;   // cursor/camera target Y
extern float  DAT_80561c90;   // cursor/camera target Z
extern float  DAT_80561c94;   // camera eye X
extern float  DAT_80561c98;   // camera eye Y
extern float  DAT_80561c9c;   // camera eye Z

// preview spawn transform blocks
extern float  DAT_803e0afc;   // pan axis default X
extern float  DAT_803e0b00;   // pan axis default Y
extern float  DAT_803e0b04;   // pan axis default Z
extern float  DAT_803e0b20;   // orbit basis X
extern float  DAT_803e0b24;   // orbit basis Y
extern float  DAT_803e0b28;   // orbit basis Z
extern float  DAT_803e0b38;   // camera translate X
extern float  DAT_803e0b3c;   // camera translate Y
extern float  DAT_803e0b40;   // camera translate Z
extern int    DAT_803e0b44;   // camera rot block A
extern int    DAT_803e0b48;
extern int    DAT_803e0b4c;
extern int    DAT_803e0b50;   // camera rot block B
extern int    DAT_803e0b54;
extern int    DAT_803e0b58;

extern float  FLOAT_806108e8; // frame delta
extern float  FLOAT_8061ab2c; // half / normalize const
extern float  FLOAT_8061ab30; // ease divisor
extern float  FLOAT_8061ab34; // 0.0f
extern float  FLOAT_8061ab44; // spin wrap period (2*pi)
extern float  FLOAT_8061ab50; // reset spin scale
extern float  FLOAT_8061ab58; // 1.0f
extern float  FLOAT_8061ab78; // rotation numerator
extern float  FLOAT_8061ab80; // tilt min
extern float  FLOAT_8061ab84; // tilt max / 1.0f
extern float  FLOAT_8061ab9c; // length epsilon
extern float  FLOAT_8061abc8; // camera min distance
extern float  FLOAT_8061abcc; // camera max distance
extern float  FLOAT_8061abd0; // height step
extern float  FLOAT_8061abd4; // tilt clamp max
extern float  FLOAT_8061abd8; // squash rest
extern float  FLOAT_8061abdc; // reset tilt
extern float  FLOAT_80612d60; // squash target (module global)
extern float  FLOAT_80612d64; // squash rate (module global)
extern double DOUBLE_8061ab70;// int->double bias (signed)

extern float  FLOAT_8061ab3c; // rotation denominator

// named cross-module helpers (call-site-accurate types; this TU includes only common.h)
extern double math_sqrt(double x);
extern unsigned input_get_buttons_pressed(int player);
extern unsigned input_get_buttons_held(int player);
extern double input_get_analog_value(int pad, int axis);
extern void   avanim_play_move_sound(int soundId);
extern void   mtx4_set_identity(int mtx);
extern void   mtx4_set_translation(double x, double y, double z);
extern void   mtx4_transform_point(float* mtx, float* point);
extern void   mtx4_rotate_about_point(float* mtx, float* axisAngle, float* pivot);
extern void   mtx4_multiply(float* a, float* b, float* out);

// _savegpr_/_restgpr_ runtime helpers threading the camera axis vector pair
extern unsigned long long __save_gpr_28(void);
extern void   __restore_gpr_28(void);
extern unsigned long long FUN_803826f0(void);
extern void   FUN_8038273c(void);

// forward decls within this TU
void aveditor_camera_pan_input(void);
void aveditor_camera_orbit_input(void);
void aveditor_camera_height_step(void);
void aveditor_cursor_spin_update(void);
void aveditor_camera_update(void);

} // extern "C"

// reciprocal-length normalise matching the decompiler's Newton-refined rsqrt (returns scale factor).
static inline float rnorm_scale(float lenSq)
{
    float r = 1.0f / (float)math_sqrt((double)lenSq);
    r = r * -(FLOAT_8061ab2c * lenSq * r * r - FLOAT_8061ab58);
    return FLOAT_8061ab84 * r * -(FLOAT_8061ab2c * lenSq * r * r - FLOAT_8061ab58);
}

// -----------------------------------------------------------------------------

// Pan the camera along the view axis (spilled r28/r29 pair) from the L/R triggers. Advances the
// target by a normalised step when the new position stays within the allowed distance band.
void aveditor_camera_pan_input(void)
{
    unsigned long long spill = __save_gpr_28();
    float* axis = (float*)(intptr_t)(unsigned)(spill >> 0x20);   // r28: view axis
    float* target = (float*)(intptr_t)(unsigned)spill;           // r29: camera target
    unsigned pressedA = input_get_buttons_pressed(0);
    unsigned pressedB = input_get_buttons_pressed(0);
    float step[3] = { DAT_803e0afc, DAT_803e0b00, DAT_803e0b04 };

    if ((*(unsigned*)((char*)DAT_80612d68 + 0x80) & 0xc00) == 0) {
        float d = FLOAT_8061ab34;
        if ((pressedA & 4) != 0) d = FLOAT_806108e8;
        float e = FLOAT_8061ab34;
        if ((pressedB & 8) != 0) e = -FLOAT_806108e8;
        float amt = FLOAT_8061ab34 + d + e;
        unsigned ok = 0;
        if (amt != FLOAT_8061ab34) {
            step[0] = axis[0] * amt;
            step[1] = axis[1] * amt;
            step[2] = axis[2] * amt;
            float lenSq = step[2] * step[2] + step[0] * step[0] + step[1] * step[1];
            if (FLOAT_8061ab9c < lenSq) {
                float sc = rnorm_scale(lenSq);
                step[0] *= sc; step[1] *= sc; step[2] *= sc;
            }
            float dx = (step[0] + DAT_80561c88) - *(float*)((char*)DAT_80612d68 + 0x50);
            float dy = (step[1] + DAT_80561c8c) - *(float*)((char*)DAT_80612d68 + 0x54);
            float dz = (step[2] + DAT_80561c90) - *(float*)((char*)DAT_80612d68 + 0x58);
            double dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
            int inBand = (dist < (double)FLOAT_8061abc8 || (double)FLOAT_8061abcc < dist) ? 1 : 0;
            ok = countLeadingZeros((unsigned)inBand) >> 5;
        }
        if (ok != 0) {
            target[0] += step[0];
            target[1] += step[1];
            target[2] += step[2];
            float lenSq = target[2] * target[2] + target[0] * target[0] + target[1] * target[1];
            if (FLOAT_8061ab9c < lenSq) {
                float sc = rnorm_scale(lenSq);
                target[0] *= sc; target[1] *= sc; target[2] *= sc;
            }
        }
    }
    __restore_gpr_28();
}

// Orbit the camera from the analog stick (axes 1/2). Builds a tangent from the view axis (spilled
// r28) and the fixed orbit basis, scales by the stick deflection, and accumulates into the camera
// target (spilled r29), re-normalising.
void aveditor_camera_orbit_input(void)
{
    unsigned long long spill = FUN_803826f0();
    float* axis = (float*)(intptr_t)(unsigned)(spill >> 0x20);
    float* target = (float*)(intptr_t)(unsigned)spill;
    float bx = DAT_803e0b28, by = DAT_803e0b24, bz = DAT_803e0b20;
    double sx = input_get_analog_value(0, 1);
    double sy = input_get_analog_value(0, 2);

    if ((*(unsigned*)((char*)DAT_80612d68 + 0x80) & 0xc00) == 0) {
        float a0 = axis[0], a1 = axis[1], a2 = axis[2];
        float lenSq = a2 * a2 + a0 * a0 + a1 * a1;
        if (FLOAT_8061ab9c < lenSq) {
            float sc = rnorm_scale(lenSq);
            a0 *= sc; a1 *= sc; a2 *= sc;
        }
        float mv0 = (float)((double)a0 * sy);
        float mv2 = (float)((double)a2 * sy);
        float tan0 = (float)((double)(float)((double)a1 * (double)bz - (double)(float)((double)a2 * (double)by)) * sx);
        float tan1 = (float)((double)(float)((double)a2 * (double)bx - (double)(float)((double)a0 * (double)bz)) * sx);
        float tan2 = (float)((double)(float)((double)a0 * (double)by - (double)(float)((double)a1 * (double)bx)) * sx);

        float mvLen = mv2 * mv2 + mv0 * mv0 + (float)((double)a1 * sy) * (float)((double)a1 * sy);
        if (FLOAT_8061ab9c < mvLen) {
            float sc = rnorm_scale(mvLen);
            mv0 *= sc; mv2 *= sc;
        }
        float tanLen = tan2 * tan2 + tan0 * tan0 + tan1 * tan1;
        if (FLOAT_8061ab9c < tanLen) {
            float sc = rnorm_scale(tanLen);
            tan0 *= sc; tan1 *= sc;
        }
        target[0] = (target[0] + mv0 + tan0) * FLOAT_806108e8;
        target[2] = (target[2] + mv2 + tan1) * FLOAT_806108e8;
        float lenSq2 = target[2] * target[2] + target[0] * target[0] + target[1] * target[1];
        if (FLOAT_8061ab9c < lenSq2) {
            float sc = rnorm_scale(lenSq2);
            target[0] *= sc; target[1] *= sc; target[2] *= sc;
        }
    }
    FUN_8038273c();
}

// Step the camera height on the shoulder buttons (bumping the height index +0x98 within [-2,2]),
// starting an eased transition toward the new height (+0x7c) with a move sound; while a transition
// is active (+0x80 bit 0x800) it advances the height until the target is reached, then commits it.
void aveditor_camera_height_step(void)
{
    unsigned held = input_get_buttons_held(0);
    unsigned held2 = input_get_buttons_held(0);
    float step = FLOAT_8061abd0;
    bool pressed = ((held & 0x400) != 0) || ((held2 & 0x200) != 0);

    if ((*(unsigned*)((char*)DAT_80612d68 + 0x80) & 0x800) == 0) {
        if (pressed) {
            float delta = FLOAT_8061ab34;
            if ((held & 0x400) != 0 && *(int*)((char*)DAT_80612d68 + 0x98) < 2) {
                *(int*)((char*)DAT_80612d68 + 0x98) += 1;
                delta += step;
                *(int*)((char*)DAT_80612d68 + 0x9c) = 1;
            }
            step = FLOAT_8061abd0;
            if ((held2 & 0x200) != 0 && -2 < *(int*)((char*)DAT_80612d68 + 0x98)) {
                *(int*)((char*)DAT_80612d68 + 0x98) -= 1;
                delta -= step;
                *(int*)((char*)DAT_80612d68 + 0x9c) = -1;
            }
            if (delta != FLOAT_8061ab34) {
                *(float*)((char*)DAT_80612d68 + 0x7c) = DAT_80561c8c + delta;
                *(unsigned*)((char*)DAT_80612d68 + 0x80) |= 0x800;
                avanim_play_move_sound(0x157d);
            }
        }
    } else {
        int dir = *(int*)((char*)DAT_80612d68 + 0x9c);
        float prev = DAT_80561c8c - DAT_80561c98;
        DAT_80561c8c = (float)((double)CONCAT44(0x43300000, (unsigned)dir ^ 0x80000000) - DOUBLE_8061ab70) *
                       FLOAT_8061ab2c * FLOAT_806108e8 + DAT_80561c8c;
        bool done = false;
        if (dir != 0) {
            if (dir < 0) {
                if (-2 < dir) {
                    done = !(*(float*)((char*)DAT_80612d68 + 0x7c) < DAT_80561c8c);
                }
            } else if (dir < 2) {
                done = !(DAT_80561c8c < *(float*)((char*)DAT_80612d68 + 0x7c));
            }
        }
        if (done) {
            *(unsigned*)((char*)DAT_80612d68 + 0x80) &= 0xfffff7ff;
            *(int*)((char*)DAT_80612d68 + 0x9c) = 0;
            *(int*)(DAT_80612be4 + 0x264) = *(int*)((char*)DAT_80612d68 + 0x98);
        }
        DAT_80561c98 = DAT_80561c8c - prev;
    }
}

// Ease the cursor spin (+0x3c) toward a snapped target (+0x78) while a snap is active (+0x80 bit
// 0x400), advance the free spin by the frame delta and wrap it into [0, 2*pi), then clamp the tilt
// (+0x40) into [0, FLOAT_8061abd4].
void aveditor_cursor_spin_update(void)
{
    float advance = FLOAT_8061ab34;
    if ((*(unsigned*)((char*)DAT_80612d68 + 0x80) & 0x400) != 0) {
        float snapTgt = *(float*)((char*)DAT_80612d68 + 0x78);
        unsigned dir = (snapTgt <= *(float*)((char*)DAT_80612d68 + 0x3c)) ? 0xffffffff : 1u;
        bool done = false;
        float cand = (float)((double)CONCAT44(0x43300000, dir ^ 0x80000000) - DOUBLE_8061ab70) *
                     FLOAT_8061ab34 * FLOAT_8061ab2c * FLOAT_806108e8 + *(float*)((char*)DAT_80612d68 + 0x3c);
        if (dir != 0) {
            if ((int)dir < 0) {
                if (-2 < (int)dir) done = cand <= snapTgt;
            } else if ((int)dir < 2) {
                done = snapTgt <= cand;
            }
        }
        if (done) {
            *(float*)((char*)DAT_80612d68 + 0x3c) = snapTgt;
            advance = FLOAT_8061ab34;
            *(unsigned*)((char*)DAT_80612d68 + 0x80) &= 0xfffffbff;
        }
    }
    *(float*)((char*)DAT_80612d68 + 0x3c) =
        advance * FLOAT_8061ab2c * FLOAT_806108e8 + *(float*)((char*)DAT_80612d68 + 0x3c);
    *(float*)((char*)DAT_80612d68 + 0x40) =
        FLOAT_8061ab34 * FLOAT_8061ab2c * FLOAT_806108e8 + *(float*)((char*)DAT_80612d68 + 0x40);
    float wrap = FLOAT_8061ab44;
    while (wrap < *(float*)((char*)DAT_80612d68 + 0x3c)) {
        *(float*)((char*)DAT_80612d68 + 0x3c) -= wrap;
    }
    while (*(float*)((char*)DAT_80612d68 + 0x3c) < FLOAT_8061ab34) {
        *(float*)((char*)DAT_80612d68 + 0x3c) += wrap;
    }
    float tilt = *(float*)((char*)DAT_80612d68 + 0x40);
    if (FLOAT_8061abd4 < tilt) tilt = FLOAT_8061abd4;
    *(float*)((char*)DAT_80612d68 + 0x40) = tilt;
    tilt = *(float*)((char*)DAT_80612d68 + 0x40);
    if (tilt < FLOAT_8061ab34) tilt = FLOAT_8061ab34;
    *(float*)((char*)DAT_80612d68 + 0x40) = tilt;
}

// Rebuild the camera basis each frame: normalise the eye->target delta, apply the height-step,
// spin-snap and orbit inputs, then compose the translate + two rotate-about-point matrices and
// transform the camera anchor.
void aveditor_camera_update(void)
{
    __save_gpr_28();
    float dx = DAT_80561c94 - DAT_80561c88;
    float dy = DAT_80561c98 - DAT_80561c8c;
    float dz = DAT_80561c9c - DAT_80561c90;
    float lenSq = dz * dz + dx * dx + dy * dy;
    float tr[3] = { DAT_803e0b38, DAT_803e0b3c, DAT_803e0b40 };
    int rotA[3] = { DAT_803e0b44, DAT_803e0b48, DAT_803e0b4c };
    int rotB[3] = { DAT_803e0b50, DAT_803e0b54, DAT_803e0b58 };
    double savedY = (double)*(float*)((char*)DAT_80612d68 + 0x54);
    if (FLOAT_8061ab9c < lenSq) {
        float sc = rnorm_scale(lenSq);
        dx *= sc; dy *= sc; dz *= sc;
    }
    if (*(int*)(DAT_80612be4 + 0x200) != 0) {
        aveditor_camera_height_step();
    }
    aveditor_cursor_spin_update();
    aveditor_camera_pan_input();
    aveditor_camera_orbit_input();

    float m[24];
    float m2[16];
    float m3[16];
    mtx4_set_translation((double)tr[0], (double)tr[1], (double)tr[2]);
    mtx4_transform_point(m, &DAT_80561c88);
    mtx4_transform_point(m, &DAT_80561c94);
    mtx4_transform_point(m, (float*)((char*)DAT_80612d68 + 0x50));
    *(float*)((char*)DAT_80612d68 + 0x54) = (float)savedY;
    mtx4_set_identity((int)(intptr_t)m);
    mtx4_set_identity((int)(intptr_t)m2);
    mtx4_set_identity((int)(intptr_t)m3);
    mtx4_rotate_about_point(m3, (float*)rotB, &DAT_80561c94);
    mtx4_rotate_about_point(m2, (float*)rotA, &DAT_80561c94);
    mtx4_multiply(m2, m, m);
    mtx4_multiply(m3, m, m);
    mtx4_transform_point(m, &DAT_80561c88);
    __restore_gpr_28();
}

// Auto-spin/squash the cursor: advance the spin while the "auto" flag (+0x80 bit 4) is set, ease the
// squash (+0x74) back to rest when a squash is active (+0x80 bit 0x80), then run the camera update
// when the caller requests it.
void aveditor_cursor_anim_step(int runCamera)
{
    if ((*(unsigned*)((char*)DAT_80612d68 + 0x80) & 4) != 0) {
        *(float*)((char*)DAT_80612d68 + 0x3c) =
            FLOAT_80612d64 * FLOAT_80612d60 + *(float*)((char*)DAT_80612d68 + 0x3c);
    }
    float rest = FLOAT_8061abd8;
    float easeDiv = FLOAT_8061ab30;
    float squash = *(float*)((char*)DAT_80612d68 + 0x74);
    if (FLOAT_8061abd8 != squash && (*(unsigned*)((char*)DAT_80612d68 + 0x80) & 0x80) != 0) {
        if (FLOAT_8061ab84 < squash || squash < FLOAT_8061ab80) {
            *(float*)((char*)DAT_80612d68 + 0x3c) += squash / FLOAT_8061ab30;
            *(float*)((char*)DAT_80612d68 + 0x74) -= *(float*)((char*)DAT_80612d68 + 0x74) / easeDiv;
        } else {
            *(int*)((char*)DAT_80612d68 + 0x3c) = *(int*)((char*)DAT_80612d68 + 0x78);
            *(float*)((char*)DAT_80612d68 + 0x74) = rest;
        }
    }
    if (runCamera != 0) {
        aveditor_camera_update();
    }
}

// Reset the cursor/camera context on editor entry: clear the active flag, reset the squash rate,
// spin/tilt, target pad/cell/drag indices, and the animation defaults.
void aveditor_cursor_reset(void)
{
    FLOAT_80612d64 = FLOAT_8061ab50;
    FLOAT_80612d60 = FLOAT_8061ab84;
    *DAT_80612d68 = 0;
    ((float*)DAT_80612d68)[0xf] = FLOAT_8061ab34;
    DAT_80612d68[0x24] = -1;
    DAT_80612d68[0x22] = -1;
    DAT_80612d68[0x25] = -1;
    ((float*)DAT_80612d68)[0x10] = FLOAT_8061abdc;
    DAT_80612d68[0x27] = 0;
    ((float*)DAT_80612d68)[0x1d] = FLOAT_8061abd8;
    ((float*)DAT_80612d68)[0x1e] = FLOAT_8061ab34;
}
