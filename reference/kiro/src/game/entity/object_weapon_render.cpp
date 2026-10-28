// object_weapon_render.cpp
// First-person weapon presentation for the local player object: weapon-select
// input handling, the per-frame recoil / weapon-bob / sway driver, muzzle
// particle emission, tracer / muzzle-flash sprite draw, and the first-person
// weapon render pass (reticle-gated, viewport save/restore). These sit
// immediately after the object weapon FSM (object_fire.cpp, batch_554) in the
// binary and drive the visible weapon each frame.
//
// Address range: 0x801b5578 - 0x801b651c
// Batch: batch_555
//
// Non-matching translation: the decompiler surfaced the usual paired-single /
// f-reg spill temporaries and the 0x43300000 int->double bias idiom (used to
// convert 32-bit ints to doubles); those are reduced to ordinary int/float
// math here. The sine/cosine lookup into DAT_80583228 (an fp table indexed by
// a 0x7ff-masked phase) is preserved as a raw table read. Raw field-offset
// pointer arithmetic on the player/object/slot structures is kept (as in the
// rest of the entity module) since those struct layouts are not yet typed.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h -> named object_*/gx_*/viewport_*/hud_* helpers

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Sound trigger: play(volume, soundId, entityCtx). Called for weapon-select clicks.
extern void FUN_80237b50(double volume, int soundId, int entityCtx);

// Render/emitter spill-frame bracketing (paired push/pop).
extern int  FUN_803826f0(void);          // spill push: returns emitter iterator base
extern void FUN_8038273c(void);          // spill pop (matches FUN_803826f0)
extern int  FUN_803826dc(void);          // spill push: returns (secondary<<32)|object packed
extern void FUN_80382728(void);          // spill pop (matches FUN_803826dc)

// AI weapon-fire tick (was FUN_802c27f0). Declared in generated_decls.h; reads
// the current actor from the PPC frame register context. Definition lives in
// aiactor_behavior_states2.cpp.

// float(double) rounding/clamp helper used by the tracer alpha ramp.
extern int  FUN_803825c0(double value);

// Fire-anim ready gate (defined in object_fire.cpp neighbourhood).
extern int  FUN_801b1cf8(int object);

// ---- Sine/phase lookup table (fp), indexed by a 0x7ff-masked phase ----
extern float DAT_80583228[];

// ---- Global player-context / tuning tables & scalars ----
extern int   DAT_80611d74[];   // local player context base (indexed as int[])
extern int   DAT_80611d54;     // active render/view context
extern int   DAT_80611748;     // camera transform block base
extern int   DAT_806108e4;     // previous-frame timestamp
extern int   DAT_806109c0;     // frame delta denominator source
extern int   DAT_80611928;     // current-frame timestamp
extern int   DAT_80611934;     // render/game state selector
extern int   DAT_80611750;     // input-suppress flag
extern int   DAT_80612610;     // emitter iteration start index
extern int   DAT_80612098;     // emitter iteration end index
extern int   DAT_805015c0;     // global mode flag bits
extern int   DAT_806125cc;     // weapon-shake enable flag
extern float FLOAT_806125c8;   // weapon-shake accumulator (mutable global)
extern int   DAT_80610a20;     // sprite/material table base (int*)
extern int   DAT_80610ba0;     // object-type descriptor table base
extern short DAT_8043e23c[];   // object-type index table (stride 0xf shorts)

// ---- fp tuning constants (immutable unless noted) ----
extern float  FLOAT_806108ec;  // frame-time scale
extern float  FLOAT_80616b98;  // 0.0f
extern float  FLOAT_80616ba8;  // 1.0f
extern double DOUBLE_80616ba0; // int->double bias (0x43300000 magic)
extern double DOUBLE_80616bd0; // int->double bias variant
extern float  FLOAT_80616bb0;
extern float  FLOAT_80616bc4;
extern float  FLOAT_80616bc8;  // 0.5f-ish phase centre
extern float  FLOAT_80616bd0;
extern float  FLOAT_80616bdc;  // TAU / period scale
extern float  FLOAT_80616be0;
extern float  FLOAT_80616be4;
extern float  FLOAT_80616c04;
extern float  FLOAT_80616c5c;
extern float  FLOAT_80616c68;
extern float  FLOAT_80616c90;  // phase->index scale (table span)
extern float  FLOAT_80616c94;
extern float  FLOAT_80616c98;
extern float  FLOAT_80616c9c;
extern float  FLOAT_80616ca0;
extern float  FLOAT_80616ca4;
extern float  FLOAT_80616ca8;
extern float  FLOAT_80616cac;
extern float  FLOAT_80616cb0;
extern float  FLOAT_80616cb4;
extern float  FLOAT_80616cb8;
extern float  FLOAT_80616cbc;
extern float  FLOAT_8060fad0;
extern int    iRam00000030;    // low-mem scratch default (object-type default id)
} // extern "C"

// ---------------------------------------------------------------------------
// Local helpers to keep the phase/sine table read readable.
// ---------------------------------------------------------------------------
static inline float weapon_sin_table(float phase) {
    int idx = (int)(FLOAT_80616c90 * phase + FLOAT_80616bc8) & 0x7ff;
    return DAT_80583228[idx];
}

// ===========================================================================
// Address: 0x801b5578 | Name: object_weapon_select_input_tick | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Reads the local player's held-button mask and cycles the equipped weapon:
// next/prev weapon group, next/prev slot within the current group (with a
// begin-switch vs cycle-up/down HUD cue depending on whether the slot changed),
// and the persistent-slot toggle. Each successful change plays a select click.
// ===========================================================================
void object_weapon_select_input_tick(void)
{
    unsigned int held;
    int newSlot;
    int prevSlot;
    int object;

    object = *(int *)(DAT_80611d74[5] + 0x7c);

    held = input_get_buttons_held(DAT_80611d74[0]);
    if ((DAT_80611d74[0xf] & held) != 0) {
        FUN_80237b50((double)FLOAT_80616ba8, 0x3e, *(int *)(object + 8));
        newSlot = object_group_cycle_next(object, 1, 1);
        object_set_weapon_slot(object, newSlot);
        hud_weapon_select_prev();
    }

    held = input_get_buttons_held(DAT_80611d74[0]);
    if ((DAT_80611d74[0xe] & held) != 0) {
        FUN_80237b50((double)FLOAT_80616ba8, 0x3e, *(int *)(object + 8));
        newSlot = object_group_cycle_prev(object, 1, 1);
        object_set_weapon_slot(object, newSlot);
        hud_weapon_select_next();
    }

    if ((DAT_80611d74[3] & 0x20U) != 0) {
        if (object_type_has_flag100000((int)(DAT_80611d74 + 0x139)) != 0) {
            goto persist_toggle;
        }
    }

    held = input_get_buttons_held(DAT_80611d74[0]);
    if ((DAT_80611d74[0x11] & held) != 0) {
        prevSlot = *(int *)(object + 0x94);
        FUN_80237b50((double)FLOAT_80616ba8, 0x3e, *(int *)(object + 8));
        newSlot = object_slot_cycle_next_in_group(object, *(int *)(object + 0x94), 1);
        object_set_weapon_slot(object, newSlot);
        if (prevSlot == *(int *)(object + 0x94)) {
            hud_weapon_select_begin_switch((int)DAT_80611d74);
        } else {
            hud_weapon_select_cycle_up();
        }
    }

    held = input_get_buttons_held(DAT_80611d74[0]);
    if ((DAT_80611d74[0x10] & held) != 0) {
        prevSlot = *(int *)(object + 0x94);
        FUN_80237b50((double)FLOAT_80616ba8, 0x3e, *(int *)(object + 8));
        newSlot = object_slot_cycle_prev_in_group(object, *(int *)(object + 0x94), 1);
        object_set_weapon_slot(object, newSlot);
        if (prevSlot == *(int *)(object + 0x94)) {
            hud_weapon_select_begin_switch((int)DAT_80611d74);
        } else {
            hud_weapon_select_cycle_down();
        }
    }

persist_toggle:
    held = input_get_buttons_held(DAT_80611d74[0]);
    if ((DAT_80611d74[0x12] & held) != 0) {
        newSlot = object_find_next_persist_slot(object, *(int *)(object + 0xa0));
        *(int *)(object + 0xa0) = newSlot;
        hud_weapon_select_stub();
    }
}

// ===========================================================================
// Address: 0x801b5764 | Name: object_weapon_recoil_render_tick | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Per-frame first-person weapon presentation driver. Advances the weapon-bob
// phase, computes recoil kick from a randomised impulse, integrates sway,
// writes the weapon transform offset (fields 0x174..0x180 of the player
// context), runs weapon-select input when allowed, and drives the weapon FSM
// (object_weapon_state_machine_tick / equip setup / anim frame driver) for the
// primary, secondary and tertiary weapon handles. Timing uses the frame
// timestamp delta (DAT_80611928 - DAT_806108e4).
// ===========================================================================
void object_weapon_recoil_render_tick(void)
{
    int   object;
    int   secondarySlot;
    float bobPhaseCur;
    float bobPhasePrev;
    float swayScaled;
    float frameDelta;
    short *animSlot;
    float recoilOut;

    object        = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    secondarySlot = *(int *)(DAT_80611d74 + 0x514);

    // Weapon-bob phase for this frame and the previous frame.
    bobPhaseCur  = weapon_sin_table((FLOAT_80616bdc * (float)((int)DAT_80611928)) / FLOAT_80616be0);
    bobPhasePrev = weapon_sin_table((FLOAT_80616bdc *
                                     (float)((int)(DAT_80611928 - DAT_806108e4))) / FLOAT_80616be0);

    object_render_queue_flush();

    // Rising edge of the bob cycle: reseed the recoil-decay coefficient.
    if ((FLOAT_80616bc8 <= bobPhaseCur) && (bobPhasePrev < FLOAT_80616bc8)) {
        float decay = FLOAT_80616c68;
        if (object_has_active_channel((int)DAT_80611d74) != 0) {
            decay = FLOAT_80616c5c;
        }
        rng_counter_tick();
        *(float *)(DAT_80611d74 + 0xe4c) =
            decay * ((float)game_frand() / FLOAT_80616bc4);
    }

    recoilOut = (bobPhaseCur - FLOAT_80616bc8) * *(float *)(DAT_80611d74 + 0xe4c);

    // Run weapon-select input unless suppressed / a menu class is active.
    if ((-1 < *(int *)(DAT_80611d74 + 4)) && (DAT_80611934 != 8)) {
        if (render_mode_class_a() == 0) {
            if ((DAT_80611750 == 0) &&
                ((*(unsigned int *)(DAT_80611d74 + 0xc) & 0x4000) == 0 &&
                 (*(unsigned int *)(DAT_80611d74 + 0x114) & 0x480e) != 0)) {
                object_weapon_select_input_tick();
            }
        } else {
            *(int *)(object + 0x94) = 0;
        }
    }

    // Integrate sway toward the target while the sway timer is positive.
    if (0 < *(int *)(DAT_80611d74 + 0x148)) {
        *(float *)(DAT_80611d74 + 0x174) =
            FLOAT_806108ec *
            ((FLOAT_80616c04 * *(float *)(DAT_80611d74 + 0x170)) /
             (float)((int)*(int *)(DAT_80611d74 + 0x148))) +
            *(float *)(DAT_80611d74 + 0x174);
    }

    // Compose the weapon transform offset from sway + bob.
    swayScaled = FLOAT_80616c94 * *(float *)(DAT_80611d74 + 0x174);
    frameDelta = FLOAT_80616ba8;
    if (FLOAT_80616ba8 < (float)((int)*(int *)(DAT_80611d74 + 0x148))) {
        frameDelta = (float)((int)*(int *)(DAT_80611d74 + 0x148));
    }
    {
        float mag = FLOAT_80616c98 * (*(float *)(DAT_80611d74 + 0x170) / frameDelta);
        *(float *)(DAT_80611d74 + 0x178) =
            mag * weapon_sin_table((FLOAT_80616bdc * swayScaled) / FLOAT_80616be0);
        *(float *)(DAT_80611d74 + 0x17c) =
            mag * weapon_sin_table((FLOAT_80616bdc * (FLOAT_80616c5c * swayScaled + FLOAT_80616be4)) /
                                   FLOAT_80616be0);
        *(float *)(DAT_80611d74 + 0x180) = FLOAT_80616b98;
    }

    // Accumulate weapon "heat"/kick energy and clamp / reset on limits.
    *(float *)(DAT_80611d74 + 0xe48) =
        FLOAT_806108ec * (FLOAT_80616ba8 / (float)((int)DAT_806109c0)) +
        *(float *)(DAT_80611d74 + 0xe48);

    if (((FLOAT_80616c98 < ABS(*(float *)(DAT_80611d74 + 0xdfc))) ||
         (FLOAT_80616c04 < ABS(*(float *)(DAT_80611d74 + 0xe00)))) ||
        (FLOAT_80616c9c < (*(float *)(DAT_80611d74 + 0x170) / frameDelta))) {
        *(float *)(DAT_80611d74 + 0xe48) = FLOAT_80616b98;
    } else if ((FLOAT_80616ca0 < *(float *)(DAT_80611d74 + 0xe48)) && (secondarySlot != -1)) {
        rng_counter_tick();
        bool primaryReady   = false;
        bool secondaryReady = false;
        if ((*(int *)(DAT_80611d74 + 0x1c8) == 0) || (*(int *)(DAT_80611d74 + 0x26c) == 10)) {
            primaryReady = true;
        }
        if ((*(int *)(DAT_80611d74 + 0x528) == 0) || (*(int *)(DAT_80611d74 + 0x5cc) == 10)) {
            secondaryReady = true;
        }
        if (((primaryReady && secondaryReady) &&
             (animSlot = object_anim_slot_resolve(), animSlot != (short *)0)) &&
            ((*(int *)(DAT_80611d74 + 0x1c8) == 0 ||
              (animSlot = object_anim_slot_resolve(), animSlot != (short *)0)))) {
            object_anim_commit_default();
            if (*(int *)(DAT_80611d74 + 0x1c8) != 0) {
                object_anim_commit_default();
            }
            *(float *)(DAT_80611d74 + 0xe48) = FLOAT_80616b98;
        }
    }

    // Global weapon-shake accumulator seed.
    if ((DAT_806125cc == 0) && (FLOAT_80616b98 < FLOAT_806125c8)) {
        FLOAT_806125c8 =
            -(FLOAT_80616c5c *
              (FLOAT_806108ec * (FLOAT_80616ba8 / (float)((int)DAT_806109c0))) - FLOAT_806125c8);
    }

    // Drive the weapon FSM ticks for the active handles.
    if (((FLOAT_806125c8 <= FLOAT_80616ca4) && ((DAT_805015c0 & 0x40000000) == 0)) &&
        (10 < (int)DAT_80611928)) {
        object_weapon_state_machine_tick(recoilOut, 0.0, 0.0, 0.0, 0.0, 0, 0, 0);
        object_weapon_state_machine_tick(recoilOut, 0.0, 0.0, 0.0, 0.0, 0, 0, 0);
        object_weapon_state_machine_tick(recoilOut, 0.0, 0.0, 0.0, 0.0, 0, 0, 0);
    }

    int animCtx = *(int *)(DAT_80611d74 + 0xd1c);
    if (((animCtx != 0) && (*(int *)(DAT_80611d74 + 0x528) == 0)) &&
        (*(int *)(DAT_80611d74 + 0x1c8) == 0)) {
        *(unsigned int *)(animCtx + 0x28) = *(unsigned int *)(animCtx + 0x28) & 0xfffffffe;
    }

    if (*(int *)(DAT_80611d74 + 0x528) == 4) {
        object_weapon_equip_setup(0, 0.0, 0.0, 0, 0, 0, 0, 0,
                                  (int)(DAT_80611d74 + 0x4e4), object + 0x1d4, 1);
    }
    if ((*(int *)(DAT_80611d74 + 0x1c8) == 4) &&
        ((*(int *)(DAT_80611d74 + 0x514) == -1 ||
          (FUN_801b1cf8((int)(DAT_80611d74 + 0x4e4)) != 0)))) {
        object_weapon_equip_setup(0, 0.0, 0.0, 0, 0, 0, 0, 0,
                                  (int)(DAT_80611d74 + 0x184), object + 0xa4, 0);
    }
    if (*(int *)(DAT_80611d74 + 0x888) == 4) {
        object_weapon_equip_setup(0, 0.0, 0.0, 0, 0, 0, 0, 0,
                                  (int)(DAT_80611d74 + 0x844), (int)(DAT_80611d74 + 0xba4), 2);
    }
    if (*(int *)(DAT_80611d74 + 0xd1c) != 0) {
        object_anim_frame_driver();
        object_anim_frame_driver();
        object_anim_frame_driver();
    }

    *(float *)(object + 0xb4c) =
        *(float *)(object + 0xb4c) + FLOAT_806108ec / (float)((int)DAT_806109c0);
}

// ===========================================================================
// Address: 0x801b5de0 | Name: object_weapon_render_pre_nop | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Empty hook (pre-render slot); retained so the object-type dispatch table has
// a valid entry. No-op in this build.
// ===========================================================================
void object_weapon_render_pre_nop(void)
{
}

// ===========================================================================
// Address: 0x801b5de4 | Name: object_weapon_muzzle_particles_emit | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Walks the active muzzle/heat-haze emitter range [DAT_80612610, DAT_80612098)
// and advances each emitter one step, bracketed by the render spill-frame
// push/pop pair. The transform/context arguments are forwarded unchanged from
// the caller's f-registers.
// ===========================================================================
void object_weapon_muzzle_particles_emit(int param_1, double param_2, double param_3,
                                         double param_4, double param_5, long long param_6,
                                         long long param_7, long long param_8)
{
    int i;

    (void)param_1; (void)param_2; (void)param_3; (void)param_4; (void)param_5;
    (void)param_6; (void)param_7; (void)param_8;
    FUN_803826f0();
    // Per-frame AI weapon-fire pass: the tick reads the current actor from the
    // PPC frame register context, so it is invoked once per bot slot.
    for (i = DAT_80612610; i < DAT_80612098; i = i + 1) {
        aiactor_weapon_fire_tick();
    }
    FUN_8038273c();
}

// ===========================================================================
// Address: 0x801b5e44 | Name: object_weapon_tracer_render | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Draws the muzzle-flash / tracer sprite for the currently-firing weapon. Reads
// the firing object (and its "other end" for beam weapons) from the render
// spill frame, projects the muzzle point through the active view matrix, and if
// on-screen emits an additive textured quad (two passes: inner + outer glow).
// The quad corners are jittered by a per-frame random phase for the flash.
// ===========================================================================
void object_weapon_tracer_render(void)
{
    unsigned int   packColor;
    unsigned short spriteW;
    unsigned short spriteH;
    int   texId;
    int   alphaStep;
    int   pass;
    int   phaseIdx;
    float glowScale;
    float lenRatio;
    float rndPhase;
    float radius;
    float *objA;   // firing object
    float *objB;   // paired object (beam far end), or 0
    unsigned long long packed;

    float worldPt[3];              // muzzle world point (in to transform)
    float proj[4];                 // projected point: proj[0..2]=xyz, proj[3]=clip bound
    float quad0[2], quad1[2], quad2[2], quad3[2];

    packed = (unsigned long long)FUN_803826dc();
    objB = (float *)(unsigned int)(packed >> 0x20);
    objA = (float *)(unsigned int)packed;

    if (((objA == (float *)0) || (objA[0x3b] == 0.0)) ||
        (pickup_fade_get_alpha((int)objA[0x3b] + 0xc68) <= (double)FLOAT_80616ba8)) {

        if (objA != (float *)0) {
            float typeArg = (objB == (float *)0) ? objA[0xc] : objB[0xc];
            object_type_dispatch(0xf, (int)typeArg, (int)objB, (int)objA, 0);
        }

        if (((objB == (float *)0) || (objB[0x15] == 0.0)) &&
            ((objB = (float *)0, objA != (float *)0 && (objA[0x3a] != 0.0)))) {
            objB = objA;
        }

        if (((objB != (float *)0) && (objA[0xc] == 2.8026e-44f)) &&
            (FLOAT_80616b98 < objA[0x41])) {

            texId    = sprite_get_texture_id(0x346);
            glowScale = FLOAT_80616c5c * objA[0x42];
            {
                int mat = DAT_80610a20 + texId * 0x58;
                spriteW = *(unsigned short *)(mat + 0x4e);
                spriteH = *(unsigned short *)(mat + 0x52);
            }
            if ((double)FLOAT_80616ba8 < (double)glowScale) {
                glowScale = FLOAT_80616ba8;
            }
            alphaStep = FUN_803825c0((double)(FLOAT_80616ca8 * glowScale));
            gx_set_blend_mode(2);
            gx_set_sub_material_id(2);

            if ((*(int *)((int)objA[0x3b] + 8) < DAT_80612610) ||
                (1 < *(int *)((int)objA[0x3b] + 0xbac))) {

                float haze = FLOAT_80616bb0 * glowScale;
                pass = 0;
                do {
                    float passScale = (pass == 0) ? FLOAT_80616cac : FLOAT_80616cb0;
                    float depthBias =
                        *(float *)(DAT_80611d54 + 0x268) * passScale + haze;

                    worldPt[0] = objB[0];
                    worldPt[1] = objB[1];
                    worldPt[2] = objB[2];
                    mtx4_transform_point4_alias(*(float **)(DAT_80611d54 + 0x20c),
                                                worldPt, proj);
                    {
                        float clipBound = proj[3];   // was local_8c
                        float negBound  = -clipBound;
                        float px = proj[0];          // was local_98
                        float py = proj[1];          // was local_94
                        float pz = proj[2];          // was local_90
                        if ((negBound <= px && px <= clipBound) &&
                            (negBound <= py && py <= clipBound) &&
                            (negBound <= pz && pz <= clipBound)) {

                            packColor = (pass == 0)
                                ? ((unsigned int)alphaStep << 0x18 | 0xf2f7f)
                                : ((unsigned int)alphaStep << 0x18 | 0xf1f7f);

                            lenRatio = depthBias / clipBound;
                            rndPhase = FLOAT_80616c5c *
                                       (float)((double)FLOAT_80616bdc * game_frand());
                            if ((double)FLOAT_80616cb4 < (double)lenRatio) {
                                lenRatio = FLOAT_80616cb4;
                            }

                            {
                                float inv = FLOAT_80616ba8 / clipBound;
                                float ndcX = px * inv;
                                float ndcY = py * inv;
                                float baseX =
                                    ((float)(-*(int *)(DAT_80611748 + 0x10))) * ndcX +
                                    ((float)(*(int *)(DAT_80611748 + 8)));
                                float baseY =
                                    ((float)(-*(int *)(DAT_80611748 + 0x24))) * ndcY +
                                    ((float)(*(int *)(DAT_80611748 + 0x1c)));

                                gx_set_active_material(texId);

                                phaseIdx = (int)(FLOAT_80616c90 * rndPhase + FLOAT_80616bc8);
                                float c = lenRatio * DAT_80583228[phaseIdx & 0x7ff];
                                float s = lenRatio *
                                          *(float *)((int)&DAT_80583228 +
                                                     ((phaseIdx + 0x200) * 4 & 0x1ffc));

                                quad0[0] = baseX + c; quad0[1] = baseY + s;
                                quad1[0] = baseX + c; quad1[1] = baseY - s;
                                quad2[0] = baseX - c; quad2[1] = baseY + s;
                                quad3[0] = baseX - c; quad3[1] = baseY - s;

                                gx_draw_quad_points((unsigned int)spriteW, (unsigned int)spriteH,
                                                    packColor, quad0, quad1, quad2, quad3);
                            }
                        }
                    }
                    pass = pass + 1;
                } while (pass < 2);
            }
        }
    }
    FUN_80382728();
}

// ===========================================================================
// Address: 0x801b625c | Name: object_weapon_tracer_render_gate | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Object-type dispatch handler (op 1): draws the weapon tracer only for the
// primary firing object that belongs to the active view context and is not the
// mirror/reflection instance.
// ===========================================================================
void object_weapon_tracer_render_gate(int param_1, int param_2)
{
    int node;

    if ((param_2 == 1) && (*(int **)(param_1 + 0x78) != (int *)0)) {
        node = **(int **)(param_1 + 0x78);
        if ((node != 0) &&
            ((((*(unsigned int *)(DAT_80611d54 + 0x21c) & 4) == 0 ||
               (node = *(int *)(node + 0x150), node == 0)) ||
              (*(int *)(node + 8) != DAT_80611d54)))) {
            object_weapon_tracer_render();
        }
    }
}

// ===========================================================================
// Address: 0x801b62cc | Name: object_weapon_first_person_flag | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Returns 1 when the active view is a normal (non-reflection, non-split-suppress)
// first-person pass with the mirror-render bit set on the render context.
// ===========================================================================
int object_weapon_first_person_flag(void)
{
    if ((DAT_805015c0 & 0x20000000) == 0) {
        if (((DAT_805015c0 & 0x40000000) == 0) &&
            ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 4) != 0)) {
            return 1;
        }
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b6308 | Name: object_weapon_first_person_render | Date: 2026-09-07
// ---------------------------------------------------------------------------
// First-person weapon render pass. Gated on the reticle being drawable and the
// weapon-shake within limits. Builds a view-proj matrix, renders the primary
// and secondary weapon models (pre/post dispatch ops 0xd/0xe), then re-renders
// the room in three blend passes with a tightened depth range so the weapon
// composites over the world, restoring the prior viewport afterward.
// ===========================================================================
void object_weapon_first_person_render(void)
{
    int   dispId;
    float viewProj[16];

    if ((((*(int *)(DAT_80611d74 + 0x14) != 0) &&
          (hud_reticle_should_draw() != 0)) &&
         (FLOAT_80616cb8 <= *(float *)(DAT_80611d54 + 900))) &&
        (FLOAT_806125c8 <= FLOAT_80616ca4)) {

        object_build_view_proj_matrix(viewProj);
        gx_set_reflect_matrix(viewProj);

        if (-1 < *(int *)(DAT_80611d74 + 4)) {
            dispId = iRam00000030;
            if (DAT_80611d74 + 0x4e4 != 0) {
                dispId = *(int *)(DAT_80611d74 + 0x514);
            }
            object_type_dispatch(0xd, dispId, (int)(DAT_80611d74 + 0x4e4), 0, 0);
            dispId = iRam00000030;
            if (DAT_80611d74 + 0x184 != 0) {
                dispId = *(int *)(DAT_80611d74 + 0x1b4);
            }
            object_type_dispatch(0xd, dispId, (int)(DAT_80611d74 + 0x184), 0, 0);
        }

        viewport_save_active();
        viewport_set_depth_params_active((double)FLOAT_80616cbc, (double)FLOAT_8060fad0,
                                         (double)FLOAT_80616b98);
        viewport_set_gx_active();
        gx_set_sub_material_id(0);
        gx_set_blend_mode(4);
        render_entity_always_visible();
        entity_render_room();
        gx_set_sub_material_id(1);
        gx_set_blend_mode(5);
        entity_render_room();
        gx_set_blend_mode(2);
        entity_render_room();
        render_entity_always_visible();

        if (-1 < *(int *)(DAT_80611d74 + 4)) {
            dispId = iRam00000030;
            if (DAT_80611d74 + 0x184 != 0) {
                dispId = *(int *)(DAT_80611d74 + 0x1b4);
            }
            object_type_dispatch(0xe, dispId, (int)(DAT_80611d74 + 0x184), 0, 0);
            dispId = iRam00000030;
            if (DAT_80611d74 + 0x4e4 != 0) {
                dispId = *(int *)(DAT_80611d74 + 0x514);
            }
            object_type_dispatch(0xe, dispId, (int)(DAT_80611d74 + 0x4e4), 0, 0);
        }

        particle_render_stub_nop();
        object_weapon_tracer_render();
        object_weapon_tracer_render();
        viewport_swap_active_saved();
        viewport_set_gx_active();
        gx_set_reflect_matrix(*(float **)(DAT_80611d54 + 0x204));
    }
}

// ===========================================================================
// Address: 0x801b6518 | Name: object_weapon_render_post_nop | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Empty hook (post-render slot); retained for the dispatch table. No-op.
// ===========================================================================
void object_weapon_render_post_nop(void)
{
}

// ===========================================================================
// Address: 0x801b651c | Name: object_weapon_type_lookup_field | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Looks up a weapon-type descriptor field: indexes the object-type index table
// (DAT_8043e23c, stride 0xf shorts) by the given type id, then reads a 32-bit
// field at +0x30 of the descriptor (stride 0x29c in DAT_80610ba0).
// ===========================================================================
int object_weapon_type_lookup_field(int param_1)
{
    return *(int *)(DAT_80610ba0 + DAT_8043e23c[param_1 * 0xf] * 0x29c + 0x30);
}
