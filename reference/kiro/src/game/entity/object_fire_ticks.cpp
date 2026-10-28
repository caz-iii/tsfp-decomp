// object_fire_ticks.cpp
// Weapon fire-state tick handlers for the object handle FSM. These are the
// per-frame case bodies dispatched by object_weapon_state_machine_tick
// (0x801b2f80, in object_fire.cpp): idle/dual-wield swap, primary fire-mode,
// charge fire, offhand (dual-wield) fire, and spin-up. Also the small active-
// weapon accessors (channel / fire-mode / match predicates) they build on.
//
// Address range: 0x801b1bd0 - 0x801b28c8
// Batch: batch_1180 | Subsystem: game
//
// Calling convention (from the state-machine dispatch at 0x801b3c50..0x801b3d5c):
// every case handler is invoked as (r3 = object, r4 = ctx, r5 = [object+0x3c]
// = side/mode). The decompiler modelled the (object,ctx) pair as the
// __save_gpr_N spill-frame return value ((object<<32)|ctx); here it is passed
// explicitly as parameters, which is behaviourally identical and avoids the
// void-return GPR-save intrinsics (Compile-Clean rule 3).
//
// Non-matching translation: the active-player context DAT_80611d74 is indexed
// both as an int array (DAT_80611d74[k]) and as a byte-offset base
// (*(int*)((int)DAT_80611d74 + off)); the raw decompiler forms are preserved.
// The 0x43300000 int->double bias dequant of the frame delta DAT_806109c0 is
// kept verbatim (bias term cancels). All struct field access is raw pointer
// arithmetic, matching the rest of the entity module (layouts not yet typed).
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Active-player context block. Indexed as an int array in this TU (element k is
// byte offset k*4), and also used as a byte-offset base for the +0x14 etc.
// accessors. Declared int* so both forms compile.
extern int  *DAT_80611d74;

// Object/weapon descriptor tables and frame state.
extern int   DAT_80610ba0;    // weapon-slot descriptor table base (0x29c stride)
extern int   DAT_8043e238;    // weapon-group table field A (stride 0xf ints)
extern int   DAT_8043e23c;    // weapon-group table field B (stride 0xf ints)
extern int   DAT_80611d54;    // active first-person weapon context (0 = none)
extern int   DAT_80611934;    // active weapon-group id
extern int   DAT_805015c0;    // control-flags byte block (bit6 = aim-assist)
extern int   DAT_806108e4;    // frame running flag / step count
extern int   DAT_806109c0;    // frame delta-time raw (float bits, dequant base)

// Tuning constants (ROM float/double literals).
extern float  FLOAT_806108ec; // global time scale
extern float  FLOAT_80616ba8; // 1.0f
extern float  FLOAT_80616bc8; // offhand charge-rate scalar
extern double DOUBLE_80616ba0; // 0x43300000 dequant bias base

// Intra-cluster neighbours (defined in this file).
extern int  FUN_801b1bd0(void);            // object_active_weapon_channel
extern int  FUN_801b1c08(int object);      // object_weapon_slot_switch_tick
extern int  FUN_801b1cc4(void);            // object_active_weapon_mode
extern int  FUN_801b1cf8(int object);      // object_matches_active_weapon
extern int  FUN_801b1f50(void);            // object_can_fire_gate

// --- Named cross-module helpers (per-file externs; not yet in a shared header;
//     generated_decls.h is not included here because it currently pulls in
//     module-specific struct types this TU does not need). ---
extern int    object_anim_slot_find(int slotTable, int slotId, int flag);
extern int    object_slot_switch_permitted(int weapon, int slot);
extern double player_get_camera_lerp_factor(int fpCtx);
extern unsigned int input_get_buttons_pressed(int pad);
extern unsigned int input_get_buttons_held(int pad);
extern void   prop_fire_tick(int obj, int weaponRef, int a3, int a4, int a5, int *ctx);
extern void   object_destroy_entities(int object, int ctx);
extern void   object_set_handle(int object, int handle);
extern unsigned int subobj_carried_slot_flags(int *ctx);
extern void   object_anim_frame_advance(int object);
extern void   object_melee_input_tick(void);
extern int    object_melee_frame_check(void);
extern void   object_ammo_consume_frame(int arg0, int ctx, int object, int mode, int side);
extern int    match_slot_field_ptr(int slotBase, int fieldSel);
extern int    object_context_is_active(int object);
extern int    object_trigger_input_tick(void);
extern void   object_stub_noop_b09ac(void);
extern int    object_slot_field_touch(int arg0, int ctx, int side);
extern void   object_reload_anim_tick(void);
extern void   object_fire_spawn_effects(void);
extern int    object_anim_frame_reached(int object, int frameId);
extern int    object_anim_pending_reached(int object, int frameId);
extern void   object_anim_commit_default(void);
extern void   object_anim_event_reload_id(int object);
extern int    object_type_dispatch(int arg0, int type, int arg2, int arg3, int arg4);
} // extern "C"

// Frame delta-time in seconds, expressed via the 0x43300000 dequant idiom.
static float fire_dt_scaled(void)
{
    double dtRaw = (double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000)
                   - DOUBLE_80616ba0;
    return (float)(FLOAT_806108ec * (double)(FLOAT_80616ba8 / (float)dtRaw));
}

// Shared dequant of the frame delta as a raw double (bias term cancels).
static double fire_dt_raw(void)
{
    return (double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000)
           - DOUBLE_80616ba0;
}

// ---------------------------------------------------------------------------
// object_active_weapon_channel (FUN_801b1bd0)
// Resolve the active weapon's audio/anim channel id from the weapon-group
// table field B (DAT_8043e23c, stride 0xf). Returns -1 if no weapon is bound
// (context slot +0x14 is null).
// ---------------------------------------------------------------------------
int FUN_801b1bd0(void)
{
    if (*(int *)((int)DAT_80611d74 + 0x14) != 0) {
        int wpn = *(int *)(*(int *)(*(int *)((int)DAT_80611d74 + 0x14) + 0x7c) + 0xa0);
        return (&DAT_8043e23c)[wpn * 0xf];
    }
    return -1;
}

// ---------------------------------------------------------------------------
// object_weapon_slot_switch_tick (FUN_801b1c08)
// In switch-state (object field +0x3c == 2), latch the active weapon channel
// once the pending slot's switch anim (slot 0x27) resolves; otherwise query
// slot-switch permission for the requested slot. 'object' is r3 at entry.
// Returns the residual r3 (latched channel / permission result).
// ---------------------------------------------------------------------------
int FUN_801b1c08(int object)
{
    int result = 0;
    if (*(int *)(object + 0x3c) == 2) {
        if ((*(int *)((int)DAT_80611d74 + 0x514) != -1) &&
            (object_anim_slot_find(DAT_80610ba0 + *(int *)((int)DAT_80611d74 + 0x514) * 0x29c,
                                   0x27, 1) != 0)) {
            result = FUN_801b1bd0();
        }
    } else {
        result = object_slot_switch_permitted(
            *(int *)(*(int *)((int)DAT_80611d74 + 0x14) + 0x7c),
            *(int *)(object + 0x3c));
    }
    return result;
}

// ---------------------------------------------------------------------------
// object_active_weapon_mode (FUN_801b1cc4)
// Resolve the active weapon's fire-mode index from weapon-group table field A
// (DAT_8043e238, stride 0xf), clamped to {0,1} (single / dual).
// ---------------------------------------------------------------------------
int FUN_801b1cc4(void)
{
    int wpn = *(int *)(*(int *)(*(int *)((int)DAT_80611d74 + 0x14) + 0x7c) + 0x94);
    int mode = (&DAT_8043e238)[wpn * 0xf];
    int a = -mode + -1;
    return a - ((unsigned int)(a == 0) + (-mode + -2));
}

// ---------------------------------------------------------------------------
// object_matches_active_weapon (FUN_801b1cf8)
// True when the given object's type (+0x30) and mode (+0x40) both equal the
// active weapon's current channel and fire-mode.
// ---------------------------------------------------------------------------
int FUN_801b1cf8(int object)
{
    int result = 0;
    int chan = FUN_801b1c08(object);
    if ((*(int *)(object + 0x30) == chan) &&
        (*(int *)(object + 0x40) == FUN_801b1cc4())) {
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// object_can_fire_gate (FUN_801b1f50)
// Fire-permitted gate: weapon-group != 8, aim-lock cleared or aim-assist bit6
// set, context mode (+0x114) != 0x10, and no carried-subobject slot flags.
// ---------------------------------------------------------------------------
int FUN_801b1f50(void)
{
    if (((DAT_80611934 != 8) &&
         ((((*(unsigned int *)((int)DAT_80611d74 + 0xc) & 0x8000) == 0) ||
           ((DAT_805015c0 & 0x40) != 0)) &&
          (*(int *)((int)DAT_80611d74 + 0x114) != 0x10))) &&
        (subobj_carried_slot_flags(DAT_80611d74) == 0)) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// object_dualwield_swap_tick (FUN_801b1d58)
// Dual-wield swap / idle-state tick (FSM case 0). Drives the camera lerp toward
// the aim pose and reads L/R fire buttons to trigger grab/holster; the alt
// branch latches the new weapon type and destroys the old entities.
// (object = r3, ctx = r4, side = r5 = [object+0x3c].)
// ---------------------------------------------------------------------------
void FUN_801b1d58(int object, int ctxArg, int side)
{
    (void)ctxArg;
    int grp = DAT_80611d74[0x45];
    int *ctx = *(int **)(DAT_80611d74[5] + 0x7c);
    int chan = FUN_801b1c08(object);

    if (((chan < 0) || (grp == 0x80) || (grp == 0x20)) ||
        (((grp == 0x10000) || (grp == 0x40)) ||
         ((side != 1) &&
          ((DAT_80611d74[0x145] != -1) &&
           (FUN_801b1cf8((int)(DAT_80611d74 + 0x139)) == 0))))) {
        if (side == 1) {
            bool doFire = false;
            if ((((DAT_80611d54 == 0) || (grp == 0x20)) || (grp == 0x40)) ||
                ((grp == 0x80) || (grp == 0x10000))) {
                if (grp == 0x40) {
                    // Both branches enable the fire test in the original.
                    doFire = true;
                }
            } else {
                float lerp;
                lerp = (float)player_get_camera_lerp_factor(DAT_80611d54);
                *(float *)(DAT_80611d54 + 0x38c) = lerp;
                lerp = (float)player_get_camera_lerp_factor(DAT_80611d54);
                *(float *)(DAT_80611d54 + 0x390) = lerp;
                lerp = (float)player_get_camera_lerp_factor(DAT_80611d54);
                *(float *)(DAT_80611d54 + 0x394) = lerp;
            }
            if (doFire) {
                int fireObj = ctx[0x305];
                int weaponRef = *(int *)(*(int *)(fireObj + 0x7c) + 0x6c);
                unsigned int held = input_get_buttons_pressed(*DAT_80611d74);
                if (((unsigned int)DAT_80611d74[6] & held) == 0) {
                    held = input_get_buttons_pressed(*DAT_80611d74);
                    if (((unsigned int)DAT_80611d74[7] & held) != 0) {
                        prop_fire_tick(fireObj, weaponRef, 1, 0, 4, ctx);
                    }
                } else {
                    prop_fire_tick(fireObj, weaponRef, 0, 0, 4, ctx);
                }
            }
        }
    } else {
        int newChan = FUN_801b1c08(object);
        if (*(int *)(object + 0x54) != 0) {
            object_destroy_entities(object, ctxArg);
        }
        *(int *)(ctxArg + 0x30) = newChan;
        *(int *)(object + 0x30) = newChan;
        ctx[0x26] = ctx[0x25];
        object_set_handle(object, 4);
    }
}

// ---------------------------------------------------------------------------
// object_fire_mode_tick (FUN_801b1fc0)
// Primary fire-mode tick (FSM case 1/7). Handles melee + trigger input and the
// per-frame ammo consumption gated by button state, slot-field touch, and the
// object fire state. Advances the fire timer (+0x48) and runs the reload anim
// in state 7. (object = r3, ctx = r4, side = r5.)
// ---------------------------------------------------------------------------
void FUN_801b1fc0(int object, int ctxLow, int side)
{
    int arg0 = *(int *)(DAT_80611d74[5] + 0x7c);
    int base = DAT_80610ba0 + *(int *)(ctxLow + 0x30) * 0x29c;
    float dt = fire_dt_scaled();

    if (FUN_801b1f50() == 0) {
        object_anim_frame_advance(object);
    } else if (0 < DAT_806108e4) {
        object_melee_input_tick();
        if ((object_melee_frame_check() == 0) && (*(int *)(object + 0x44) == 1) &&
            ((DAT_80611d74[9] & (int)input_get_buttons_held(*DAT_80611d74)) != 0)) {
            object_ammo_consume_frame(*(int *)(DAT_80611d74[5] + 0x7c), ctxLow, object, 1, side);
        } else {
            int *pctx = DAT_80611d74;
            if (((object_melee_frame_check() == 0) ||
                 ((*(unsigned int *)match_slot_field_ptr(base, 0) & 0x10) != 0)) &&
                (pctx[0x14a] != 10)) {
                bool skip = false;
                if ((object_context_is_active(object) != 0) &&
                    ((*(int *)(object + 0xe8) == 0x19) || (*(int *)(object + 0xe8) == 0x1a))) {
                    skip = true;
                }
                if ((!skip) && (object_trigger_input_tick() == 0)) {
                    object_stub_noop_b09ac();
                }
            }
            if (*(int *)(object + 0x44) == 1) {
                int fieldIdx = *(int *)(base + 0x14);
                if (((DAT_80611d74[6] & (int)input_get_buttons_pressed(*DAT_80611d74)) == 0) &&
                    ((DAT_80611d74[7] & (int)input_get_buttons_pressed(*DAT_80611d74)) == 0)) {
                    object_anim_frame_advance(object);
                }
                if ((*(int *)(object + 0x5c) == 1) && (*(int *)(base + 0x1c) != 0)) {
                    fieldIdx = *(int *)(base + 0x1c);
                }
                if ((*(int *)(ctxLow + 0x30) != -1) &&
                    (((*(int *)(ctxLow + 0x10c) != 0) && (*(int *)(object + 0xac) != 0)) ||
                     ((*(unsigned int *)(object + 0x34) & 0x20) != 0))) {
                    if (((*(short *)(ctxLow + fieldIdx * 2 + 0x38) == 0) &&
                         ((object_slot_field_touch(arg0, ctxLow, *(int *)(object + 0x5c)) != 0) ||
                          ((object_slot_field_touch(arg0, ctxLow, 1 - *(int *)(object + 0x5c)) == 0) &&
                           ((*(unsigned int *)(object + 0x34) & 0x20) == 0)))) &&
                        ((*(int *)(object + 0x30) != 0x1e) || (*(int *)(object + 0x5c) == 0))) {
                        object_ammo_consume_frame(*(int *)(DAT_80611d74[5] + 0x7c),
                                                  ctxLow, object, 0, side);
                    }
                    *(unsigned int *)(object + 0x34) = *(unsigned int *)(object + 0x34) & 0xffffffdf;
                }
            }
        }
    }
    *(float *)(object + 0x48) = (float)((double)*(float *)(object + 0x48) + (double)dt);
    if (*(int *)(object + 0x44) == 7) {
        object_reload_anim_tick();
    }
}

// ---------------------------------------------------------------------------
// object_charge_fire_tick (FUN_801b22c0)
// Charge-fire tick (FSM case 10). Charges the meter (+0x108) while the fire
// button is held, fires (handle 1 + spawn effects) once the charge threshold
// and per-frame budget are met, and commits release via anim frame 0x2d.
// (object = r3, ctx = r4.)
// ---------------------------------------------------------------------------
void FUN_801b22c0(int object, int ctxLow)
{
    int slot = match_slot_field_ptr(DAT_80610ba0 + *(int *)(ctxLow + 0x30) * 0x29c,
                                    *(int *)(object + 0x5c));
    float dt = fire_dt_scaled();
    double dtRaw = fire_dt_raw();
    *(float *)(object + 0xbc) = FLOAT_80616ba8;

    unsigned int pressed = input_get_buttons_pressed(*DAT_80611d74);
    if (((int)DAT_80611d74[6] & (int)pressed) == 0) {
        *(int *)(object + 0xb8) = 1;
    } else if ((*(int *)(object + 0xb8) == 0) &&
               (*(float *)(ctxLow + 0x108) =
                    (float)((double)*(float *)(ctxLow + 0x108) + (double)dt),
                FLOAT_80616ba8 <= *(float *)(ctxLow + 0x108))) {
        *(float *)(ctxLow + 0x108) = FLOAT_80616ba8;
    }

    if (object_context_is_active(object) == 0) {
        if (((*(int *)(object + 0xb8) != 0) &&
             (*(int *)(object + 0xa8) = *(int *)(object + 0xa8) + DAT_806108e4,
              *(int *)(object + 0xac) == 0)) &&
            (*(float *)(slot + 0xc) * (float)dtRaw <=
             (float)((double)CONCAT44(0x43300000, (unsigned int)*(int *)(object + 0xa8) ^ 0x80000000)
                     - DOUBLE_80616ba0)) &&
            (0 < DAT_806108e4)) {
            object_set_handle(object, 1);
            object_fire_spawn_effects();
        }
    } else {
        if (object_anim_frame_reached(object, 0x2d) == 0) {
            *(int *)(object + 0xa8) = *(int *)(object + 0xa8) + DAT_806108e4;
            if (((*(int *)(object + 0xac) == 0) &&
                 (*(float *)(slot + 0xc) * (float)dtRaw <=
                  (float)((double)CONCAT44(0x43300000,
                                           (unsigned int)*(int *)(object + 0xa8) ^ 0x80000000)
                          - DOUBLE_80616ba0))) &&
                (0 < DAT_806108e4)) {
                object_set_handle(object, 1);
                object_fire_spawn_effects();
            }
        } else if ((object_anim_pending_reached(object, 0x2d) != 0) &&
                   (*(int *)(object + 0xb8) != 0)) {
            object_anim_commit_default();
            *(unsigned int *)(*(int *)(object + 0x54) + 0x28) =
                *(unsigned int *)(*(int *)(object + 0x54) + 0x28) & 0xfffffffe;
        }
    }
}

// ---------------------------------------------------------------------------
// object_dualwield_fire_tick (FUN_801b2538)
// Offhand (channel 2) fire tick (FSM case 9). Charges the offhand meter on the
// R-fire button, fires both hands (plus a third linked object in group 9) once
// the budget is met, and runs the reload anim-event path via frames 0x27-0x2a.
// (object = r3, ctx = r4, side = r5.)
// ---------------------------------------------------------------------------
void FUN_801b2538(int object, int ctxLow, int side)
{
    int slot = match_slot_field_ptr(DAT_80610ba0 + *(int *)(ctxLow + 0x30) * 0x29c,
                                    *(int *)(object + 0x5c));
    int *pctx = DAT_80611d74;
    float dt = fire_dt_scaled();
    double dtRaw = fire_dt_raw();

    if (side == 2) {
        *(float *)(object + 0xbc) = FLOAT_80616ba8;
        int *offhand = pctx + 0x139;
        unsigned int pressed = input_get_buttons_pressed(*DAT_80611d74);
        if (((int)DAT_80611d74[7] & (int)pressed) == 0) {
            *(int *)(object + 0xb8) = 1;
        } else if ((*(int *)(object + 0xb8) == 0) &&
                   (*(float *)(ctxLow + 0x108) =
                        (float)((double)FLOAT_80616bc8 * (double)dt +
                                (double)*(float *)(ctxLow + 0x108)),
                    FLOAT_80616ba8 <= *(float *)(ctxLow + 0x108))) {
            *(float *)(ctxLow + 0x108) = FLOAT_80616ba8;
        }

        if (object_context_is_active((int)offhand) == 0) {
            if (((*(int *)(object + 0xb8) != 0) &&
                 (*(int *)(object + 0xa8) = *(int *)(object + 0xa8) + DAT_806108e4,
                  *(int *)(object + 0xac) == 0)) &&
                (*(float *)(slot + 0xc) * (float)dtRaw <=
                 (float)((double)CONCAT44(0x43300000,
                                          (unsigned int)*(int *)(object + 0xa8) ^ 0x80000000)
                         - DOUBLE_80616ba0)) &&
                (0 < DAT_806108e4)) {
                object_fire_spawn_effects();
                object_set_handle(object, 1);
                object_set_handle((int)offhand, 1);
                if (pctx[0x72] == 9) {
                    object_set_handle((int)(pctx + 0x61), 1);
                }
            }
        } else {
            if ((object_anim_frame_reached((int)offhand, 0x27) == 0) &&
                (object_anim_frame_reached((int)offhand, 0x29) == 0)) {
                bool budget =
                    (*(int *)(object + 0xa8) = *(int *)(object + 0xa8) + DAT_806108e4,
                     (*(int *)(object + 0xac) == 0) &&
                     (((*(int *)(object + 0x54) != 0) &&
                       (*(float *)(slot + 0xc) * (float)dtRaw <=
                        (float)((double)CONCAT44(0x43300000,
                                                 (unsigned int)*(int *)(object + 0xa8) ^ 0x80000000)
                                - DOUBLE_80616ba0))) &&
                      (0 < DAT_806108e4)));
                if (((object_anim_frame_reached((int)offhand, 0x28) != 0) ||
                     (object_anim_frame_reached((int)offhand, 0x2a) != 0)) &&
                    budget) {
                    object_fire_spawn_effects();
                    *(unsigned int *)(*(int *)(object + 0x54) + 0x28) =
                        *(unsigned int *)(*(int *)(object + 0x54) + 0x28) & 0xfffffffe;
                    object_set_handle(object, 1);
                    object_set_handle((int)offhand, 1);
                    if (pctx[0x72] == 9) {
                        object_set_handle((int)(pctx + 0x61), 1);
                    }
                }
            } else if (((object_anim_pending_reached((int)offhand, 0x27) != 0) ||
                        (object_anim_pending_reached((int)offhand, 0x29) != 0)) &&
                       (*(int *)(object + 0xb8) != 0)) {
                object_anim_event_reload_id((int)offhand);
                object_anim_commit_default();
                object_anim_commit_default();
                object_anim_commit_default();
                int e0 = *(int *)(object + 0x54);
                if (e0 != 0) {
                    *(unsigned int *)(e0 + 0x28) = *(unsigned int *)(e0 + 0x28) | 1;
                }
                int e1 = pctx[0x76];
                if (e1 != 0) {
                    *(unsigned int *)(e1 + 0x28) = *(unsigned int *)(e1 + 0x28) & 0xfffffffe;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// object_spinup_tick (FUN_801b28c8)
// Spin-up tick (FSM case 8). Advances the spin timer (+0x48), runs melee input
// when firing is allowed, and dispatches the type-0x14 spin handler.
// (object = r3, ctx = r4.)
// ---------------------------------------------------------------------------
void FUN_801b28c8(int object, int ctxArg)
{
    *(float *)(object + 0x48) = fire_dt_scaled() + *(float *)(object + 0x48);
    if ((FUN_801b1f50() != 0) && (0 < DAT_806108e4)) {
        object_melee_input_tick();
    }
    int type;
    if (object == 0) {
        type = *(int *)(ctxArg + 0x30);
    } else {
        type = *(int *)(object + 0x30);
    }
    object_type_dispatch(0x14, type, object, ctxArg, 0);
}
