// object_fire.cpp
// Player/AI weapon handle state machine: per-frame weapon tick, fire/charge
// stages, weapon equip/spawn, reload, projectile-owned update, aim/target
// solve, and the charge-weapon (type 0x11) render tick. These are the bodies
// behind the object handle FSM whose case dispatch lives in
// object_weapon_state_machine_tick (0x801b2f80).
//
// Address range: 0x801b2a30 - 0x801b53dc
// Batch: batch_554
//
// Non-matching translation: the decompiler surfaced many paired-single / f-reg
// spill temporaries and 0x43300000 int->double bias idioms; those are reduced
// to ordinary float/int math here. Raw field-offset pointer arithmetic on the
// object/entity/slot structures is preserved (matching the rest of the entity
// module) since the struct layouts are not yet typed.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h -> named object_*/entity_*/mtx4_* helpers

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Render/AI spill-frame accessors (paired push/pop bracketing).
extern int  FUN_803826f0(void);          // spill push: returns spill-frame base
extern void FUN_8038273c(void);          // spill pop (matches FUN_803826f0)
extern int  FUN_803826dc(void);          // spill push variant: returns object base
extern unsigned long long FUN_803826e8(void); // spill push variant: (object<<32)|ctx
extern void FUN_80382728(void);          // spill pop (matches FUN_803826dc)
extern void FUN_80382734(void);          // spill pop (matches FUN_803826e8)
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);

// Object FSM neighbours (named in object_base.cpp; called here by raw address).
extern void FUN_801b1d58(int object, int ctx, int side);      // object_dualwield_swap_tick (case 0)
extern void FUN_801b1fc0(int object, int ctx, int side);      // object_fire_mode_tick      (case 1/7)
extern void FUN_801b28c8(int object, int ctx);                // object_spinup_tick         (case 8)
extern void FUN_801b2538(int object, int ctx, int side);      // object_dualwield_fire_tick (case 9)
extern void FUN_801b22c0(int object, int ctx);                // object_charge_fire_tick    (case 10)
extern int  FUN_801b1cf8(int object);                         // object_matches_active_weapon
extern int  FUN_801b1c08(int object);                         // object_weapon_slot_switch_tick
extern int  FUN_801b6afc(int slot, int aimBlock);             // aim entity (re)spawn helper

// Other subsystem helpers (raw address form).
extern int  FUN_8024077c(int a);                              // menu/pause state query
extern void FUN_8023ddfc(int a1, int a2, int a3, int a4, int a5, int *a6); // hud/aim notify
extern int  FUN_801c1714(void);                               // control style query (2 = alt)

// Arity-mismatched named helpers: the shared decls simplify these, so we call
// the true register-form via raw-address aliases (no clash with shared names).
extern int  FUN_80072d70(unsigned long long a1, double a2, double a3, unsigned long long a4,
                         unsigned long long a5, unsigned long long a6, unsigned long long a7,
                         unsigned long long a8, int typeId, void *posBlock, void *rotBlock,
                         int a12, int a13);                    // entity_spawn_by_type (raw)
extern void FUN_801a8a10(double a1, double a2, int object, int clipA, int clipB); // object_anim_start_clip (raw)
extern void FUN_80023e10(double rate, int **ctx, int mode);   // anim_set_playback_rate (raw)

// NOTE: damage_dispatch_to_entity / player_ai_projectile_update_owned / the anim
// and hud helpers are declared in the shared module headers (via entity.h ->
// generated_decls.h) with auto-simplified signatures. We call them through those
// shared declarations; the extra register-form arguments the decompiler surfaced
// (recoil scalar, DVD frame regs) are read by the callee from the object/spill
// block, so the simplified prototypes are behaviourally equivalent here.

// --- Global state blocks (declared int* / int so index + offset math works) ---
extern int  *DAT_80611d74;    // active-player context block (indexed as int array)
extern int   DAT_80610ba0;    // weapon-slot descriptor table base (0x29c stride)
extern int   DAT_806109c0;    // frame delta-time raw (float bits, 0x43300000 dequant)
extern int   DAT_806108e4;    // frame running flag / step count
extern int   DAT_80611d54;    // active first-person weapon context (0 = none)
extern int   DAT_80611748;    // nav / movement mode block
extern int   DAT_80611e2c;    // fallback particle id sentinel

extern char  DAT_805015bd;    // region/language byte ('\n' selects table A)
extern int   DAT_8043e238;    // weapon-group table field A (stride 0xf ints)
extern int   DAT_8043e22c;    // weapon-group table base (stride 0xf ints)
extern int   DAT_8043e23c;    // weapon-group table field B (stride 0xf ints)
extern int   DAT_8043e240;    // weapon-group table field C (stride 0xf ints)
extern float DAT_80431204;    // recoil-decay lookup (stride 8 bytes)
extern int   DAT_804308bc;    // reload-table A (stride 0x24)
extern int   DAT_804308c0;    // reload-table B (stride 0x24)
extern float DAT_8048ca94;    // per-weapon vertical offset table (stride 0x30)
extern int   DAT_80500000;    // sentinel default pointer
extern int   DAT_8058e3cc;    // (unused here; kept for parity)

// Muzzle/tracer spawn transform template (float block copied into locals).
extern unsigned int DAT_803cd180, DAT_803cd184, DAT_803cd188;
extern unsigned int DAT_803cd18c, DAT_803cd190, DAT_803cd194;
extern unsigned int DAT_803cd198, DAT_803cd19c, DAT_803cd1a0;
extern unsigned int DAT_803cd1a4, DAT_803cd1a8, DAT_803cd1ac;

// --- Tuning constants (ROM float/double literals) ---
extern float  FLOAT_80616b98;   // 0.0f
extern float  FLOAT_80616b9c;
extern float  FLOAT_80616ba8;   // 1.0f
extern float  FLOAT_806108ec;   // global time scale
extern float  FLOAT_80616c64, FLOAT_80616c68, FLOAT_80616c6c, FLOAT_80616c70;
extern float  FLOAT_80616c74, FLOAT_80616c78, FLOAT_80616c7c, FLOAT_80616c80;
extern float  FLOAT_80616c84, FLOAT_80616c88, FLOAT_80616c08, FLOAT_80616c18;
extern float  FLOAT_80616c20, FLOAT_80616c38, FLOAT_80616c5c, FLOAT_80616c00;
extern float  FLOAT_80616c14, FLOAT_80616be0, FLOAT_80616be4, FLOAT_80616be8;
extern float  FLOAT_80616bc8, FLOAT_80616bd8, FLOAT_80616bdc, FLOAT_80616bfc;
extern float  FLOAT_80616c8c, FLOAT_80611718, FLOAT_80616c40, FLOAT_80616bc0;
extern double DOUBLE_80616ba0;  // 0x43300000 dequant bias base
} // extern "C"

// Frame delta-time in seconds: FLOAT_806108ec * (1.0 / dt_raw). The decomp
// expresses dt_raw via the 0x43300000 int->double bias dequant of DAT_806109c0.
float object_fire_dt_scaled(void)
{
    double dtRaw = (double)(unsigned int)DAT_806109c0;   // dequant (bias cancels)
    return (float)(FLOAT_806108ec * (double)(FLOAT_80616ba8 / (float)dtRaw));
}

// ---------------------------------------------------------------------------
// object_dryfire_cycle_slot (FUN_801b2a30)
// When the object is on weapon-group 0x21 (dual/charge group), advance to the
// next slot in the group and refresh the HUD selector.
// ---------------------------------------------------------------------------
void object_dryfire_cycle_slot(int object)
{
    if (*(int *)(object + 0x94) == 0x21) {
        int slot = object_slot_cycle_next_in_group(object, 0x21, 1);
        object_set_weapon_slot(object, slot);
        hud_weapon_select_cycle_up();
    }
}

// ---------------------------------------------------------------------------
// object_charge_accumulate_tick (FUN_801b2b74)
// Charge weapon fields at object+0x544 (charge level) and +0x548 (release
// timer). While the type-0x11 threshold gate is open it snaps to the release
// profile (and dry-fires the slot when the charge bleeds to zero); otherwise it
// decays the release timer and, once expired, accumulates charge up to 1.0.
// For type 0x11 it mirrors the clamped charge level into +0x2dc for rendering.
// ---------------------------------------------------------------------------
void object_charge_accumulate_tick(int object)
{
    int gate = object_type11_threshold_gate(object);
    float dt = object_fire_dt_scaled();

    if (gate == 0) {
        float *release = (float *)(object + 0x548);
        if (FLOAT_80616b98 < *release) {
            *release = -(dt - *release);
            if (*release < FLOAT_80616b98) {
                *release = FLOAT_80616b98;
            }
        }
        float *charge = (float *)(object + 0x544);
        if (FLOAT_80616b98 == *release && *charge < FLOAT_80616ba8) {
            *charge = FLOAT_80616bc8 * dt + *charge;
            if (FLOAT_80616ba8 < *charge) {
                *charge = FLOAT_80616ba8;
            }
        }
    } else {
        *(float *)(object + 0x548) = FLOAT_80616c68;
        float *charge = (float *)(object + 0x544);
        *charge = -(FLOAT_80616c6c * dt - *charge);
        if (*charge <= FLOAT_80616b98) {
            *charge = FLOAT_80616b98;
            object_dryfire_cycle_slot(object);
        }
    }

    if (*(int *)(object + 0x204) == 0x11) {
        float lvl = *(float *)(object + 0x544);
        if (lvl < FLOAT_80616b98) {
            lvl = FLOAT_80616b98;
        }
        *(float *)(object + 0x2dc) = lvl;
    }
}

// ---------------------------------------------------------------------------
// object_charge_render_tick (FUN_801b2a7c)
// Render-time callback (installed via entity_set_render_type) for the type-0x11
// charge weapon's attached entity. Copies the owner object matrix into the spill
// frame, marks it dirty (bit 8), and when the charge is above threshold runs the
// release ramp - which can dry-fire the slot and dispatch the type-0x16 render
// handler. Finally, when the accumulated recoil (spill+0x20) is positive, routes
// it through damage_dispatch_to_entity.
// ---------------------------------------------------------------------------
void object_charge_render_tick(unsigned long long a1, double a2, double a3, unsigned long long a4,
                               unsigned long long a5, unsigned long long a6, unsigned long long a7,
                               unsigned long long a8)
{
    int *spill = (int *)FUN_803826f0();
    int owner  = *(int *)(*spill + 0x7c);
    int *ownerMtx = *(int **)(owner + 0xec);
    *spill = *ownerMtx;
    spill[0x10] = spill[0x10] | 8;

    float recoil = FLOAT_80616b98;
    if ((*(unsigned int *)(*(int *)(*(int *)(owner + 0xe8) + 4) + 0x30) & 2) != 0) {
        float level = *(float *)&ownerMtx[0x151];
        if (FLOAT_80616b98 < level) {
            float ramp = -(float)((double)FLOAT_80616c64 * (double)*(float *)&spill[8] - (double)level);
            *(float *)&ownerMtx[0x151] = ramp;
            *(float *)&spill[8] = recoil;   // recoil currently == 0.0
            if (*(float *)&ownerMtx[0x151] <= FLOAT_80616b98) {
                *(float *)&spill[8] = -*(float *)&ownerMtx[0x151];
                *(float *)&ownerMtx[0x151] = recoil;
                object_dryfire_cycle_slot((int)(intptr_t)ownerMtx);
            }
            float clamped = *(float *)&ownerMtx[0x151];
            if (clamped < FLOAT_80616b98) {
                clamped = FLOAT_80616b98;
            }
            *(float *)(owner + 0x108) = clamped;
            object_type_dispatch(0x16, *(int *)(owner + 0x30), 0, owner, 1);
        }
    }

    if (FLOAT_80616b98 < *(float *)&spill[8]) {
        // recoil magnitude is already stored at spill[8]; the callee reads it there.
        damage_dispatch_to_entity(spill);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// object_projectile_owned_tick (FUN_801b4aa0)
// Advance an owned projectile/aim helper; on a state transition play the tick
// sound (id 0xd43) at the emitter position, and when the slot's ammo count
// (+0x194 short) drops below 1 switch to the next available weapon.
// ---------------------------------------------------------------------------
void object_projectile_owned_tick(unsigned long long a1, unsigned long long a2, double a3,
                                  unsigned long long a4, unsigned long long a5, unsigned long long a6,
                                  unsigned long long a7, unsigned long long a8, int *slot)
{
    (void)a1; (void)a2; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    // The owned-projectile advance signals a fire transition through the slot
    // block; the shared decl returns void, so we re-derive the "fired this frame"
    // condition from the projectile-active flag the callee sets at slot+0x190.
    player_ai_projectile_update_owned();
    if (slot[0x190 / 4] != 0) {
        unsigned int pos[3];
        sound_get_emitter_position(slot, (float *)pos);
        int params[8];
        sound_init_play_params((int)params);
        *(char *)((int)params + 0x1c) = 1;
        params[0] = 0xd43;
        *(float *)((int)params + 0x0c) = FLOAT_80611718;
        *(unsigned int *)((int)params + 0x10) = pos[0];
        *(unsigned int *)((int)params + 0x14) = pos[1];
        *(unsigned int *)((int)params + 0x18) = pos[2];
        sound_play_request(params);
    }
    if (*(short *)((int)slot + 0x194 * 4) < 1) {
        object_switch_to_next_available(slot);
    }
}

// ---------------------------------------------------------------------------
// object_reload_play_sound (FUN_801b52c4)
// Play the generic reload sound (id 0xd0f) at the owner position, and if the
// weapon is melee (not ranged) with a valid reload-sound index, kick the scaled
// reload animation.
// ---------------------------------------------------------------------------
void object_reload_play_sound(void)
{
    int *spill = (int *)FUN_803826f0();
    int owner = *spill;
    int reloadSnd = -1;

    int params[6];
    sound_init_play_params((int)params);
    *(char *)((int)params + 0x24) = 1;
    *(float *)((int)params + 0x18) = FLOAT_80611718;
    params[1] = 0x28;
    params[0] = 0xd0f;
    *(int *)((int)params + 0x14) = *(int *)(owner + 100);
    *(int *)((int)params + 0x18) = *(int *)(owner + 0x68);
    *(int *)((int)params + 0x1c) = *(int *)(owner + 0x6c);
    sound_play_request(params);

    int *pi = spill;
    if ((pi[5] & 0x60000) == 0 &&
        player_ai_is_ranged_weapon((int)*(short *)((int)pi + 0x4a)) == 0) {
        int grp = (&DAT_8043e23c)[pi[0x25] * 0xf];
        if (grp >= 0 && grp < 0x42) {
            reloadSnd = object_resolve_reload_sound(grp);
        }
    }
    if (reloadSnd >= 0) {
        anim_play_scaled((double)FLOAT_80616c00, (double)FLOAT_80616ba8);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// object_reload_ammo_apply (FUN_801b53dc)
// Reload path: optionally accumulate per-magazine sound events (region byte
// '\n'), refill the primary/secondary ammo pools (object_ammo_reload), select
// the reload anim clip from the region-specific table, and if any ammo was
// added and the clip isn't flagged 0x400, play the reload sound and clear the
// reload accumulators.
// ---------------------------------------------------------------------------
void object_reload_ammo_apply(void)
{
    int *pi = (int *)__save_gpr_28();
    int grpIdx = pi[0x25];
    if (grpIdx != 0) {
        void *animClip = (void *)0x80500000;
        int added = 0;
        if (DAT_805015bd == '\n') {
            animClip = (void *)(&DAT_8043e22c + grpIdx * 0xf);
            int grp = (&DAT_8043e23c)[grpIdx * 0xf];
            if (grp >= 0 && grp < 0x42) {
                int desc = DAT_80610ba0 + grp * 0x29c;
                object_sound_slot_accumulate((int)pi, *(int *)(desc + 0x14), *(short *)(desc + 0x24));
                object_sound_slot_accumulate((int)pi, *(int *)(desc + 0x1c), *(short *)(desc + 0x26));
                animClip = (void *)0;   // resolved by callee via r4 side effect
            }
        }
        if (pi[0x81] != -1) {
            animClip = (void *)0;
            added = object_ammo_reload((int)pi, 0, (int)(pi + 0x75), 0);
        }
        if (pi[0x35] != -1) {
            animClip = (void *)0;
            added += object_ammo_reload((int)pi, 0, (int)(pi + 0x29), 0);
        }
        if (DAT_805015bd == '\n') {
            pi[0x2eb] = *(int *)(&DAT_804308bc + pi[0x81] * 0x24);
        } else {
            pi[0x2eb] = *(int *)(&DAT_804308c0 + pi[0x81] * 0x24);
        }
        if (added > 0) {
            int flagged = anim_clip_has_flags(*pi, animClip, 0x400, 0);
            if (flagged == 0 && (pi[7] & 0x100000) == 0) {
                object_reload_play_sound();
            }
            pi[0xb1] = 0;
            pi[0xb2] = 0;
            pi[0x66] = 0;
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// object_fire_charge_stage_tick (FUN_801b2cf8)
// Charge/hold-fire stage tick (weapon FSM case 0xc). In first-person (context
// inactive) it advances the charge timer at object+0x50 and, on reaching the
// slot threshold, either dry-fires (object_set_handle 1) when the magazine is
// empty or consumes ammo + spawns fire effects. In third-person (context
// active) it drives the same fire/ammo logic off the anim event frames
// (0x1c/0x1d/0x1e reached + pending).
// ---------------------------------------------------------------------------
void object_fire_charge_stage_tick(void)
{
    unsigned long long packed = FUN_803826e8();
    int object = (int)(packed >> 0x20);
    int ctx    = (int)packed;

    int weaponCtx = *(int *)(*(int *)((int)DAT_80611d74 + 0x14) + 0x7c);
    int base = DAT_80610ba0 + *(int *)(ctx + 0x30) * 0x29c;
    int slot = match_slot_field_ptr(base, 1);
    float dt = object_fire_dt_scaled();

    if (object_context_is_active(object) == 0) {
        float threshold = *(float *)(slot + 0x14);
        *(float *)(object + 0x50) = (float)((double)FLOAT_80616c70 * (double)dt + (double)*(float *)(object + 0x50));
        if (threshold < *(float *)(object + 0x50)) {
            *(float *)(object + 0x50) = FLOAT_80616b98;
            if (*(short *)(ctx + *(int *)(base + 0x14) * 2 + 0x38) < 1) {
                object_set_handle(object, 1);
            } else {
                object_ammo_consume_dispatch(weaponCtx, ctx, object, *(int *)(base + 0x14), 1, *(int *)(base + 0x18));
                object_fire_spawn_effects();
            }
        }
        FUN_80382734();
        return;
    }

    // Third-person: driven by animation event frames.
    if (object_anim_frame_reached(object, 0x1c) != 0) {
        if (object_anim_pending_reached(object, 0x1c) != 0) {
            object_ammo_consume_dispatch(weaponCtx, ctx, object, *(int *)(base + 0x14), 1, *(int *)(base + 0x18));
            object_fire_spawn_effects();
            object_anim_commit_default();
        }
    } else if (object_anim_frame_reached(object, 0x1d) != 0) {
        if (object_anim_pending_reached(object, 0x1d) != 0) {
            object_ammo_consume_dispatch(weaponCtx, ctx, object, *(int *)(base + 0x14), 1, *(int *)(base + 0x18));
            object_fire_spawn_effects();
            if (*(short *)(ctx + *(int *)(base + 0x14) * 2 + 0x38) < 1) {
                object_anim_commit_default();
            } else {
                object_anim_commit_default();
                object_anim_mark_frame_dirty(object);
            }
        }
    } else if (object_anim_frame_reached(object, 0x1e) != 0 &&
               object_anim_pending_reached(object, 0x1e) != 0) {
        object_set_handle(object, 1);
        object_anim_pick_event(object);
        object_anim_commit_default();
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// object_weapon_state_machine_tick (FUN_801b2f80)
// The per-frame weapon-handle FSM. Runs shared pre-step logic (idle tick,
// projectile jitter, particle disable, tracking sound follow, shell-casing
// timer, alt-fire input for the 0x80/0x10000 mode, first-person recoil/camera
// offset write into the view entity, matrix palette mirror, rumble), then
// dispatches on the handle state object+0x44 to the per-state handlers.
// ---------------------------------------------------------------------------
void object_weapon_state_machine_tick(unsigned long long param_1, unsigned long long param_2,
                                      double param_3, unsigned long long param_4,
                                      unsigned long long param_5, unsigned long long param_6,
                                      unsigned long long param_7, unsigned long long param_8)
{
    int object = FUN_803826dc();
    int ctx = *(int *)(object + 0x354);
    int *player = (int *)DAT_80611d74;
    int *view = *(int **)(player[5] + 0x7c);
    float dt = object_fire_dt_scaled();

    object_idle_tick();
    *(int **)(ctx + 0xec) = view;
    player_ai_projectile_jitter_tick((double)FLOAT_80616c40, (float *)(ctx + 0x114));

    // Disable cached particle emitters attached to the view entity when the
    // active slot has the 0x400 flag.
    if (*(int *)(object + 0x30) != -1) {
        unsigned int *slotFlags = (unsigned int *)match_slot_field_ptr(
            DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c, *(int *)(object + 0x5c));
        int ent = *(int *)(object + 0x54);
        if ((*slotFlags & 0x400) != 0 && ent != 0) {
            int stride = 0;
            for (int i = 0; i < *(int *)(ent + 0x1e8); ++i) {
                int *emitterRef = (i < *(int *)(ent + 0x1e8))
                    ? (int *)(*(int *)(ent + 0x1fc) + stride)
                    : (int *)&DAT_80611e2c;
                int tmp = *emitterRef;
                particle_cached_disable((ParticleEmitter **)&tmp);
                stride += 4;
            }
        }
    }

    // Follow a playing tracking sound to the emitter position.
    if (-1 < *(int *)(ctx + 0x100) && sound_is_playing(*(unsigned int *)(ctx + 0x100)) != 0) {
        float pos[3];
        object_resolve_sound_position(ctx, pos, 1);
        sound_get_position(*(int *)(ctx + 0x100), pos);
    }

    // Shell-casing eject timer.
    if (*(int *)(object + 0x30) != -1 && 0 < *(int *)(ctx + 0x110)) {
        int slotBase = DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c;
        int slot = match_slot_field_ptr(slotBase, *(int *)(object + 0x5c));
        *(int *)(ctx + 0x110) -= DAT_806108e4;
        if (*(int *)(ctx + 0x110) < 1 && *(int *)(slot + 0x54) != -1) {
            object_spawn_shell_casing((unsigned int)object, ctx, slotBase);
            *(int *)(ctx + 0x110) = 0;
        }
    }

    // Alt/dual-fire manual cycle in the 0x80 / 0x10000 handle modes.
    if (player[0x45] == 0x80 || player[0x45] == 0x10000) {
        int entA = player[0x306];
        int entB = player[0x305];
        int *vc = *(int **)(entB + 0x7c);
        int subA = (entA != 0) ? *(int *)(entA + 0x7c) : 0;
        (void)subA;
        int paused = FUN_8024077c(*vc);
        if (((paused == 0 && vc[4] != 1) || player[0x45] == 0x10000)) {
            if (entA == 0 || player[0x45] != 0x10000) {
                unsigned int pressed = input_get_buttons_pressed(*player);
                if ((player[6] & pressed) == 0) {
                    if ((player[7] & input_get_buttons_pressed(*player)) != 0) {
                        FUN_8023ddfc(entB, vc[0x1b], 1, 0, 4, view);
                        *(float *)&vc[0x2e] = FLOAT_80616c74 * FLOAT_806108ec + *(float *)&vc[0x2e];
                    }
                } else {
                    FUN_8023ddfc(entB, vc[0x1b], 0, 0, 4, view);
                    *(float *)&vc[0x2e] = FLOAT_80616c74 * FLOAT_806108ec + *(float *)&vc[0x2e];
                }
            }
        }
    }

    // First-person weapon position/recoil into the view entity (+0x54).
    if (DAT_80611d54 != 0 && *(int *)(object + 0x44) != 0 &&
        *(int *)(object + 0x54) != 0 && *(int *)(object + 0x30) != -1) {
        int slotBase = DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c;
        float bob = (float)((FLOAT_80616be4 * (*(float *)(DAT_80611d54 + 900) / FLOAT_80616be8)) / FLOAT_80616be8);
        int viewEnt = *(int *)(object + 0x54);

        if (object_context_is_active(object) == 0) {
            float amt = FLOAT_80616ba8;
            int typeId = (object == 0) ? *(int *)(ctx + 0x30) : *(int *)(object + 0x30);
            object_type_dispatch(0xb, typeId, object, ctx, (int)&amt);
            float side = (*(int *)(object + 0x3c) == 0) ? -*(float *)(slotBase + 0x3c) : *(float *)(slotBase + 0x3c);
            *(float *)(viewEnt + 100) = (float)player[0x5e] + side;
            *(float *)(viewEnt + 0x68) = *(float *)(slotBase + 0x40) + (float)player[0x5f];
            *(float *)(viewEnt + 0x6c) = *(float *)(slotBase + 0x44) + (float)player[0x60];
            *(float *)(viewEnt + 0xc0) = (float)((double)bob * (double)(FLOAT_80616c78 * (float)player[0x380]) +
                                                 (double)(*(float *)(slotBase + 0x48) + *(float *)(object + 0x6c)));
            float f = (FUN_801c1714() == 2) ? FLOAT_80616c5c : FLOAT_80616ba8;
            *(float *)(viewEnt + 0x70) = -(float)((double)bob * (double)((float)player[0x37f] * FLOAT_80616c78 * amt * f) -
                                                  (double)(FLOAT_80616be0 + *(float *)(slotBase + 0x4c)));
            *(float *)(viewEnt + 200) = (float)((double)(FLOAT_80616c7c + *(float *)(slotBase + 0x50)));
        } else {
            float *force = nav_get_force_b_ptr();
            bool crouch = (*(int *)(DAT_80611748 + 0x30) == 0 && *(int *)(DAT_80611748 + 0x34) == 1);
            if (object_is_active_tagged(object) == 0) {
                float *src;
                bool matchHand = ((*(int *)(object + 0x3c) == 1 && *(int *)(object + 0x30) == player[0x6d]) ||
                                  (*(int *)(object + 0x3c) == 0 && *(int *)(object + 0x30) == player[0x145]));
                if (matchHand) {
                    src = (float *)(slotBase + (crouch ? 0x1bc : 0x1a4));
                } else {
                    src = (float *)(slotBase + (crouch ? 0x1b0 : 0x198));
                }
                *(float *)(viewEnt + 100) = (float)player[0x5e] + src[0];
                *(float *)(viewEnt + 0x68) = (float)player[0x5f] + src[1];
                *(float *)(viewEnt + 0x6c) = (float)player[0x60] + src[2];
            } else {
                float bp[3];
                object_active_ctx_bone_point((unsigned int)object, bp);
                *(float *)(viewEnt + 100) = bp[0];
                *(float *)(viewEnt + 0x68) = bp[1];
                *(float *)(viewEnt + 0x6c) = bp[2];
            }
            float pv = (float)player[0x380];
            if (FLOAT_80616b98 <= pv) {
                *(float *)(viewEnt + 0x68) = -(FLOAT_80616c38 * *(float *)(slotBase + 0x1d4) * pv - *(float *)(viewEnt + 0x68));
            } else {
                *(float *)(viewEnt + 0x68) = -(FLOAT_80616c38 * *(float *)(slotBase + 0x1d0) * pv - *(float *)(viewEnt + 0x68));
            }
            float ph = (float)player[0x37f];
            if (ph <= FLOAT_80616b98) {
                *(float *)(viewEnt + 100) = FLOAT_80616c38 * *(float *)(slotBase + 0x1d8) * ph + *(float *)(viewEnt + 100);
            } else {
                *(float *)(viewEnt + 100) = FLOAT_80616c38 * *(float *)(slotBase + 0x1dc) * ph + *(float *)(viewEnt + 100);
            }
            *(float *)(viewEnt + 0xc0) = (float)((double)bob * (double)(FLOAT_80616c78 * *(float *)(slotBase + 0x1cc) * (float)player[0x380]) +
                                                 (double)*(float *)(slotBase + 0x48));
            float f = (FUN_801c1714() == 2) ? FLOAT_80616c5c : FLOAT_80616ba8;
            *(float *)(viewEnt + 0x70) = -(float)((double)bob * (double)((float)player[0x37f] * FLOAT_80616c78 * *(float *)(slotBase + 0x1c8) * f) -
                                                  (double)(FLOAT_80616be0 + *(float *)(slotBase + 0x4c)));
            *(float *)(viewEnt + 200) = (float)((double)(*(float *)(slotBase + 0x50) - FLOAT_80616c5c));
            *(float *)(viewEnt + 100) += force[0];
            *(float *)(viewEnt + 0x68) += force[1];
            *(float *)(viewEnt + 0x6c) += force[2];
        }
    }

    // Mirror the view-entity transform to the secondary entity (+0x58) and copy
    // its bone matrix palette.
    if (*(int *)(object + 0x58) != 0 && *(int *)(object + 0x54) != 0) {
        int a = *(int *)(object + 0x58);
        int b = *(int *)(object + 0x54);
        *(int *)(a + 100)  = *(int *)(b + 100);
        *(int *)(a + 0x68) = *(int *)(b + 0x68);
        *(int *)(a + 0x6c) = *(int *)(b + 0x6c);
        *(int *)(a + 0xc0) = *(int *)(b + 0xc0);
        *(int *)(a + 0x70) = *(int *)(b + 0x70);
        *(int *)(a + 200)  = *(int *)(b + 200);
        int off = 0;
        int sub = *(int *)(a + 4);
        for (int i = 0; i < *(int *)(*(int *)(sub + 4) + 4); ++i) {
            mtx4_copy((float *)(*(int *)(*(int *)(b + 4) + 0x10) + 0x40),
                      (float *)(*(int *)(sub + 0x10) + off));
            off += 0x40;
        }
    }

    // Weapon-heat / cooldown decay at object+0xbc.
    *(float *)(object + 0xbc) = -(float)((double)FLOAT_80616c80 * (double)dt - (double)*(float *)(object + 0xbc));
    if (*(float *)(object + 0xbc) < FLOAT_80616b98) {
        *(float *)(object + 0xbc) = FLOAT_80616b98;
    }

    // Auto-fire cadence + rumble while trigger held.
    if (0 < DAT_806108e4 && *(int *)(object + 0x44) == 1 && *(int *)(object + 0x30) != -1) {
        int slotBase = DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c;
        int slot = match_slot_field_ptr(slotBase, *(int *)(object + 0x5c));
        if (*(int *)(object + 0xa8) != -1) {
            *(int *)(object + 0xa8) += DAT_806108e4;
        }
        if (0 < *(int *)(ctx + 0xf8)) {
            float rate = (float)(unsigned int)*(int *)(object + 0xa8);
            if (*(float *)(slot + 0x30) * (float)(double)(unsigned int)DAT_806109c0 <= rate) {
                object_fire_state_machine(object, ctx, 0);
            }
        }
        if (*(int *)(object + 0xac) == 0) {
            float acc = (float)(unsigned int)*(int *)(object + 0xa8);
            if (*(float *)(slot + 0xc) * (float)(double)(unsigned int)DAT_806109c0 <= acc &&
                0 < DAT_806108e4 && (*(unsigned int *)(slotBase + 0xc) & 0x280) == 0) {
                object_fire_spawn_effects();
            }
        }
    }

    // Rumble scaled by heat.
    if (FLOAT_80616c08 < *(float *)(object + 0xc4) && DAT_806108e4 != 0 &&
        (*(unsigned short *)(player[0x4e6] + 2) & 0x50) == 0x50) {
        int cur = input_get_rumble_intensity(*player);
        int add = (int)(FLOAT_80616c84 * *(float *)(object + 0xc4));
        input_set_rumble_intensity(*player, add + cur);
    }

    // Heat decay by weapon recoil-decay table, and reset when no weapon.
    if (*(int *)(object + 0x30) == -1) {
        *(float *)(object + 0xc4) = FLOAT_80616b98;
    } else {
        int slot = match_slot_field_ptr(DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c, *(int *)(object + 0x5c));
        *(float *)(object + 0xc4) = *(float *)(object + 0xc4) * (&DAT_80431204)[*(int *)(slot + 100) * 2];
    }

    // Per-type animation dispatch + palette identity/reset.
    {
        int typeId = (object == 0) ? *(int *)(ctx + 0x30) : *(int *)(object + 0x30);
        object_type_dispatch(1, typeId, object, ctx, 0);
    }
    if (object_context_is_active(object) == 0 && *(int *)(object + 0x54) != 0) {
        int off = 0;
        int b = *(int *)(object + 0x54);
        for (int i = 0; i < *(int *)(*(int *)(b + 4) + 4); ++i) {
            mtx4_set_identity(*(int *)(b + 0x10) + off);
            off += 0x40;
        }
        object_stub_noop_afb90();
    } else {
        int typeId = (object == 0) ? *(int *)(ctx + 0x30) : *(int *)(object + 0x30);
        object_type_dispatch(0xc, typeId, object, ctx, 0);
    }
    *(int *)(object + 100) = 0;
    *(int *)(object + 0x68) = 0;

    // Auto weapon-switch bookkeeping (states 1/6/7 with no pending swap).
    int handle = *(int *)(object + 0x44);
    if ((handle == 1 || handle == 6 || handle == 7) &&
        object_swap_is_pending((int)player) == 0) {
        if (FUN_801b1cf8(object) == 0) {
            object_reload_anim_tick2((int)view, object, ctx);
            goto done;
        }
        int cur = view[0x25];
        int nxt = view[0x26];
        if (cur != nxt && (&DAT_8043e238)[cur * 0xf] == (&DAT_8043e238)[nxt * 0xf] &&
            (&DAT_8043e23c)[cur * 0xf] == (&DAT_8043e23c)[nxt * 0xf]) {
            view[0x26] = cur;
        }
        if (*(int *)(object + 0x30) != -1 &&
            *(int *)(object + 0x38) != (&DAT_8043e240)[view[0x25] * 0xf]) {
            unsigned int f = *(unsigned int *)(DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c + 0xc);
            if ((f & 0x1800) != 0) {
                if ((f & 0x800) == 0 && object_slot_field_touch((unsigned int)view, (unsigned int)ctx, 1) == 0) {
                    int s = object_slot_cycle_next_in_group((int)view, view[0x25], 1);
                    object_set_weapon_slot((int)view, s);
                    hud_weapon_select_cycle_up();
                } else {
                    object_fire_anim_toggle(object);
                }
            }
        }
    }
    {
        int fr = object_anim_get_frame(object);
        if (fr == 0x15 || (fr = object_anim_get_frame(object)) == 0x16) {
            object_melee_scan_targets();
        }
    }

    // Handle-state dispatch.
    switch (*(int *)(object + 0x44)) {
    case 0:
        FUN_801b1d58(object, ctx, *(int *)(object + 0x3c));
        break;
    case 1:
    case 7:
        FUN_801b1fc0(object, ctx, *(int *)(object + 0x3c));
        player = (int *)DAT_80611d74;
        if (*(int *)(object + 0x3c) == 1 &&
            (*(int *)(object + 0xe8) == 0x28 || *(int *)(object + 0xe8) == 0x2a) &&
            object_has_active_channel((int)player) != 0) {
            double interp = (double)*(float *)(object + 0xf0);
            if ((double)FLOAT_80616bc8 < interp && (player[0x21e] & 0x10) != 0) {
                object_process_pending_swap((int)player, 0);
                object_anim_set_interp((float)interp, (int)(player + 0x61));
            }
        }
        break;
    case 2: {
        double t = (double)*(float *)(object + 0x48);
        if (player[0x45] == 0x80) {
            object_set_handle(object, 3);
        } else if (object_context_is_active(object) == 0) {
            int ve = *(int *)(object + 0x54);
            if (ve != 0) {
                *(float *)(ve + 0x68) = -(float)((double)FLOAT_80616c18 * t - (double)*(float *)(ve + 0x68));
                *(float *)(ve + 0xc0) = (float)((double)FLOAT_80616b9c * t + (double)*(float *)(ve + 0xc0));
            }
            *(float *)(object + 0x48) = (float)((double)FLOAT_80616c70 * (double)dt + (double)*(float *)(object + 0x48));
            if (FLOAT_80616ba8 <= *(float *)(object + 0x48)) {
                object_set_handle(object, 3);
            }
            if (*(int *)(object + 0x44) == 2 && FUN_801b1cf8(object) != 0) {
                object_fire_anim_tick((int)view, object, ctx, *(int *)(object + 0x3c));
            }
        } else {
            if (object_anim_pending_reached(object, 0x14) != 0 || object_anim_pending_reached(object, 0x22) != 0) {
                object_set_handle(object, 3);
            }
        }
        break;
    }
    case 3: {
        int nextType = FUN_801b1c08();
        if (*(int *)(object + 0x3c) == 1 && player[0x6d] != -1) {
            int a = player[0x9b];
            if (a == 0x27 || (unsigned int)(a - 0x28) < 2 || a == 0x2a) {
                object_anim_commit_default();
            }
        }
        if (*(int *)(object + 0x30) != -1) {
            int sb = DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c;
            int f14 = *(int *)(sb + 0x14);
            if (f14 != 0) { int p = ctx + f14 * 2; *(short *)(p + 0x90) = *(short *)(p + 0x38); }
            int f1c = *(int *)(sb + 0x1c);
            if (f1c != 0) { int p = ctx + f1c * 2; *(short *)(p + 0x90) = *(short *)(p + 0x38); }
        }
        int idx = 1, o = 2;
        do {
            int cap = object_param_resolve(idx);
            int po = o + 0x61e;
            *(short *)((int)view + po) = (short)(*(short *)((int)view + po) + *(short *)(ctx + o + 0x38));
            if (cap < *(short *)((int)view + po)) {
                *(short *)((int)view + po) = (short)cap;
            }
            *(short *)(ctx + o + 0x38) = 0;
            ++idx; o += 2;
        } while (idx < 0x2c);
        *(int *)(ctx + 0x34) = 0;
        *(int *)(object + 0x38) = 0;
        if (*(int *)(ctx + 0xe8) != 0) {
            entity_destroy_with_event(*(int **)(ctx + 0xe8));
            *(int *)(ctx + 0xe8) = 0;
        }
        if (*(int *)(object + 0x54) != 0) {
            object_destroy_entities(object, ctx);
        }
        *(int *)(ctx + 0x30) = nextType;
        *(int *)(object + 0x30) = nextType;
        view[0x26] = view[0x25];
        object_set_handle(object, (nextType < 0) ? 0 : 4);
        break;
    }
    case 5: {
        *(float *)(object + 0x48) = *(float *)(object + 0x48) - FLOAT_806108ec / (float)(double)(unsigned int)DAT_806109c0;
        if (*(float *)(object + 0x48) <= FLOAT_80616b98) {
            bool doFire = true;
            if (*(int *)(ctx + 0x30) == 0x22) {
                int hi = *(int *)(DAT_80610ba0 + 0x58cc) * 2;
                if ((int)*(short *)((int)view + hi + 0x61e) + (int)*(short *)(ctx + hi + 0x38) < 1 &&
                    object_slot_primary_has_capacity((int)view, view[0x26]) == 0) {
                    object_set_handle(object, 3);
                    object_switch_to_next_available(view);
                    doFire = false;
                }
            }
            if (doFire) {
                object_fire_anim_tick((int)view, object, ctx, *(int *)(object + 0x3c));
            }
        }
        if (*(int *)(object + 0x54) != 0 && object_context_is_active(object) == 0) {
            *(float *)(*(int *)(object + 0x54) + 0x68) += FLOAT_80616c20;
            *(float *)(*(int *)(object + 0x54) + 0xc0) += FLOAT_80616b9c;
        }
        break;
    }
    case 6: {
        bool done6 = false;
        double t = (double)(float)((double)FLOAT_80616ba8 - (double)*(float *)(object + 0x48));
        if ((double)*(float *)(object + 0x48) < (double)FLOAT_80616b98) {
            t = (double)FLOAT_80616ba8;
        }
        if (player[0x45] == 0x80) {
            object_set_handle(object, 3);
        } else {
            unsigned int held = input_get_buttons_held(*player);
            bool releaseBranch = (player[6] & held) == 0 ||
                (*(int *)(object + 0x30) != 6 && (*(int *)(object + 0x30) != 0x24 || *(int *)(object + 0x38) != 1)) ||
                ((object_context_is_active(object) == 0 ||
                  ((*(int *)(object + 0xe8) != 0x13 && *(int *)(object + 0xe8) != 0x21) ||
                   *(float *)(object + 0xf0) <= FLOAT_80616c88)) &&
                 (object_context_is_active(object) != 0 || *(float *)(object + 0x48) <= FLOAT_80616c88));
            if (releaseBranch) {
                int ve = object_context_is_active(object);
                if (ve == 0 || *(int *)(object + 0x54) == 0) {
                    int v = *(int *)(object + 0x54);
                    if (v != 0) {
                        *(float *)(v + 0x68) = -(float)((double)FLOAT_80616c18 * t - (double)*(float *)(v + 0x68));
                        *(float *)(*(int *)(object + 0x54) + 0xc0) = (float)((double)FLOAT_80616b9c * t + (double)*(float *)(*(int *)(object + 0x54) + 0xc0));
                    }
                    *(float *)(object + 0x48) = (float)((double)FLOAT_80616c70 * (double)dt + (double)*(float *)(object + 0x48));
                    if (FLOAT_80616ba8 <= *(float *)(object + 0x48)) {
                        done6 = true;
                    }
                } else if (object_anim_has_pending(object)) {
                    done6 = true;
                    object_anim_pick_event(object);
                    object_anim_commit_default();
                }
                if (done6) {
                    object_set_handle(object, 1);
                    *(float *)(object + 0x48) = FLOAT_80616b98;
                    object_update_camera_offset((int)player, object);
                }
            } else {
                *(float *)(ctx + 0x108) = FLOAT_80616b98;
                *(int *)(object + 0xb8) = 0;
                *(float *)(ctx + 0x104) = FLOAT_80616b98;
                object_fire_request((unsigned int)ctx, (unsigned int)object, 0, 0, *(int *)(object + 0x3c));
                object_set_handle(object, 1);
            }
        }
        break;
    }
    case 8:
        FUN_801b28c8(object, ctx);
        break;
    case 9:
        FUN_801b2538(object, ctx, *(int *)(object + 0x3c));
        break;
    case 10:
        FUN_801b22c0();
        break;
    case 0xb: {
        int touch = object_fire_slot_touch(object, *(int *)(object + 0x3c), *(int *)(object + 0x5c));
        if (touch != 0) {
            object_fire_request((unsigned int)ctx, (unsigned int)object, *(int *)(object + 0x5c), 1, *(int *)(object + 0x3c));
        }
        break;
    }
    case 0xc:
        object_fire_charge_stage_tick();
        break;
    default:
        break;
    }
done:
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// object_weapon_equip_setup (FUN_801b42f4)
// Equip/switch to a weapon: tear down any existing muzzle entity, spawn the new
// weapon model entity (and, for type 0x11, its charge sub-entity + type-0x11
// render callback), spawn the muzzle-flash attachment, resolve the equip
// animation, reset all recoil/aim accumulators, and set the initial handle
// (1 in third-person, 5 in first-person). param_11: 0 = normal, 1 = forced.
// ---------------------------------------------------------------------------
void object_weapon_equip_setup(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int param_9, int param_10, int forced)
{
    (void)param_9; (void)param_10;
    unsigned long long packed = (unsigned long long)__save_gpr_25();
    int object = (int)(packed >> 0x20);
    int ctx    = (int)packed;
    int weaponCtx = *(int *)(*(int *)((int)DAT_80611d74 + 0x14) + 0x7c);
    int *slot = (int *)(DAT_80610ba0 + *(int *)(ctx + 0x30) * 0x29c);

    if (*(int *)(ctx + 0xe8) != 0) {
        entity_destroy_with_event(*(int **)(ctx + 0xe8));
        *(int *)(ctx + 0xe8) = 0;
    }

    if (forced == 0 && *(int *)(weaponCtx + 0x204) != (&DAT_8043e23c)[*(int *)(weaponCtx + 0x94) * 0xf]) {
        __restore_gpr_25();
        return;
    }
    if (forced != 1 && object_slot_switch_permitted(weaponCtx, 0) == 0) {
        object_set_handle(object, 0);
        __restore_gpr_25();
        return;
    }

    // Copy the two spawn transform templates into locals.
    unsigned int posA[3] = { DAT_803cd180, DAT_803cd184, DAT_803cd188 };
    unsigned int posB[3] = { DAT_803cd18c, DAT_803cd190, DAT_803cd194 };
    unsigned int rot[3]  = { DAT_803cd198, DAT_803cd19c, DAT_803cd1a0 };

    *(int *)(object + 0x54) = 0;
    *(int *)(object + 0x58) = 0;
    *(int *)(ctx + 0xe8) = 0;
    *(int *)(object + 0x38) = (&DAT_8043e240)[*(int *)(weaponCtx + 0x94) * 0xf];
    if ((slot[3] & 0x1000) != 0 && object_slot_field_touch((unsigned int)weaponCtx, (unsigned int)ctx, 1) == 0) {
        *(int *)(object + 0x38) = 0;
    }

    if (*(int *)((int)DAT_80611d74 + 4) < 0 || (slot[0] == -1 && slot[1] == -1)) {
        // No model to spawn.
        *(int *)(object + 0xe8) = 0;
        *(int *)(object + 0xf8) = 0;
        *(float *)(object + 0xec) = FLOAT_80616b98;
        int flag = ((&DAT_8043e238)[*(int *)(weaponCtx + 0x94) * 0xf] != -1 &&
                    object_slot_switch_permitted(weaponCtx, 0) != 0) ? 1 : 0;
        *(int *)(object + 0x40) = flag;
        if (object_context_is_active(object) != 0) {
            if (*(int *)(object + 0x38) == 1) object_anim_slot_resolve();
            object_anim_slot_resolve();
            *(int *)(object + 0xe8) = 0;
            object_anim_commit_default();
            *(float *)(*(int *)(*(int *)((int)DAT_80611d74 + 0xd1c) + 4) + 0x100) = FLOAT_80616ba8;
            *(float *)((int)DAT_80611d74 + 0xd70) = FLOAT_80616ba8;
        }
    } else {
        int inHand = (object_context_is_active(object) == 0) ? slot[1] : slot[0];
        int ent = FUN_80072d70(packed, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                               inHand, posB, rot, 0, 0);
        *(int *)(object + 0x54) = ent;
        particle_entity_disable_all();
        *(int *)(ent + 0x7c) = object;
        *(char *)(ent + 0x32) = 2;
        *(char *)(ent + 0x31) = 1;
        if (*(int *)((int)DAT_80611d74 + 4) < 0) {
            *(int *)(ent + 0x34) = 0xce;
        } else {
            *(int *)(ent + 0x34) = *(int *)((int)DAT_80611d74 + 4) + 0xca;
        }
        *(unsigned int *)(ent + 0x28) |= 0x40000000;
        *(int *)(object + 0xe8) = 0;
        *(int *)(object + 0xf8) = 0;
        *(float *)(object + 0xec) = FLOAT_80616b98;
        int flag = ((&DAT_8043e238)[*(int *)(weaponCtx + 0x94) * 0xf] != -1 &&
                    object_slot_switch_permitted(weaponCtx, 0) != 0) ? 1 : 0;
        *(int *)(object + 0x40) = flag;
        if (object_is_active_tagged(object) != 0) {
            *(unsigned int *)(ent + 0x28) &= 0xfffffffe;
        }
        if (object_context_is_active(object) != 0) {
            if (*(int *)(object + 0x38) == 1) object_anim_slot_resolve();
            short *clip = object_anim_slot_resolve();
            if (clip == 0) {
                object_is_active_tagged(object);
            } else {
                if (object_is_active_tagged(object) == 0) {
                    FUN_801a8a10((double)FLOAT_80616b98, (double)FLOAT_80616b98, object, (int)clip[2], (int)clip[3]);
                }
                entity_register_for_update(*(int *)(object + 0x54), 2, -0x7ffe2614);
            }
            *(int *)(object + 0xe8) = 0;
            object_anim_commit_default();
            *(float *)(*(int *)(*(int *)(object + 0x54) + 4) + 0x100) = FLOAT_80616ba8;
            *(float *)(*(int *)(*(int *)((int)DAT_80611d74 + 0xd1c) + 4) + 0x100) = FLOAT_80616ba8;
            *(float *)((int)DAT_80611d74 + 0xd70) = FLOAT_80616ba8;
        }
        if (*(int *)(object + 0x30) == 0x11) {
            int sub = FUN_80072d70(packed, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   0x6f1, posB, rot, 0, 0);
            *(int *)(object + 0x58) = sub;
            *(int *)(sub + 0x7c) = object;
            *(char *)(sub + 0x32) = 2;
            *(char *)(sub + 0x31) = 1;
            *(int *)(sub + 0x34) = (*(int *)((int)DAT_80611d74 + 4) < 0) ? 0xce : *(int *)((int)DAT_80611d74 + 4) + 0xca;
            *(unsigned int *)(sub + 0x28) |= 0x40000000;
            if (*(int *)(object + 0x38) == 0) {
                *(unsigned int *)(sub + 0x28) &= 0xfffffffe;
            }
            render_instance_set_material((double)FLOAT_80616b98, *(int **)(*(int *)(object + 0x58) + 4), 1);
            entity_register_for_update(*(int *)(object + 0x58), 2, -0x7fe4d684);
        }
    }

    // Muzzle-flash attachment entity.
    if (slot[2] != -1 && object_is_active_tagged(object) == 0) {
        int muzzle = FUN_80072d70(packed, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  slot[2], posB, rot, 0, 0);
        *(int *)(ctx + 0xe8) = muzzle;
        particle_entity_disable_all();
        unsigned int attachFlags = (forced == 0) ? 2u : 1u;
        *(int *)(muzzle + 0x7c) = ctx;
        *(int *)(muzzle + 0x8c) = *(int *)(ctx + 0x30);
        *(unsigned int *)(muzzle + 0x28) &= 0xffffff9f;
        int *parent = *(int **)(*(int *)((int)DAT_80611d74 + 0x14) + 4);
        int bone = (forced == 0) ? *(int *)(parent[0x21] + 0x2c) : *(int *)(parent[0x21] + 0x30);
        entity_setup_attachment(muzzle, parent, bone, 0xffffffff, (int)(attachFlags | 4));
        *(int *)(muzzle + 0x248) = *(int *)(*(int *)((int)DAT_80611d74 + 0x14) + 100);
        *(int *)(muzzle + 0x24c) = *(int *)(*(int *)((int)DAT_80611d74 + 0x14) + 0x68);
        *(int *)(muzzle + 0x250) = *(int *)(*(int *)((int)DAT_80611d74 + 0x14) + 0x6c);
        if (*(int *)(ctx + 0x30) == 0x11) {
            if (*(int *)(object + 0x38) == 1) {
                int e4 = *(int *)(muzzle + 4);
                *(unsigned int *)(e4 + 0x30) |= 2;
                *(unsigned int *)(muzzle + 0x28) |= 0x60;
            }
            *(unsigned int *)(muzzle + 0x28) |= 0x10;
            entity_set_render_type(muzzle, (int)(intptr_t)&object_charge_render_tick);
            *(int *)(ctx + 0x108) = *(int *)(weaponCtx + 0x544);
        }
    }

    object_set_handle(object, (object_is_active_tagged(object) == 0) ? 5 : 1);

    // Reset recoil / aim accumulators.
    *(float *)((int)DAT_80611d74 + 0xe4c) = FLOAT_80616b98;
    *(float *)(object + 0x74) = FLOAT_80616b98;
    *(float *)(object + 0x78) = FLOAT_80616b98;
    *(float *)(object + 0x7c) = FLOAT_80616b98;
    *(float *)(object + 0x6c) = FLOAT_80616b98;
    if (*(int *)(object + 0x54) != 0) {
        *(float *)(*(int *)(object + 0x54) + 0xc0) = FLOAT_80616b98;
        *(float *)(*(int *)(object + 0x54) + 0x70) = FLOAT_80616b98;
        *(float *)(*(int *)(object + 0x54) + 200) = FLOAT_80616b98;
    }
    *(float *)(object + 0x80) = FLOAT_80616b98;
    *(float *)(object + 0x84) = FLOAT_80616b98;
    *(float *)(object + 0x88) = FLOAT_80616b98;
    *(float *)(object + 0x8c) = FLOAT_80616b98;
    *(float *)(object + 0x9c) = FLOAT_80616b98;
    *(float *)(object + 0xa0) = FLOAT_80616b98;
    *(int *)(object + 0xac) = 1;
    *(float *)(object + 0xc4) = FLOAT_80616b98;
    *(float *)(ctx + 0x104) = FLOAT_80616b98;
    *(float *)(ctx + 0x108) = FLOAT_80616b98;
    *(int *)(ctx + 0x10c) = 0;
    if (*(int *)(object + 0x54) != 0) {
        int typeId = (object == 0) ? *(int *)(ctx + 0x30) : *(int *)(object + 0x30);
        if (object_type_dispatch(3, typeId, object, ctx, 0) == 0) {
            entity_register_for_update(*(int *)(object + 0x54), 8, 0);
        }
    }
    // Clear the 0x2c-entry short damage-accumulator array at ctx+0x38.
    {
        int o = 0;
        for (int i = 0x2c; i != 0; --i) {
            *(short *)(ctx + o + 0x38) = 0;
            o += 2;
        }
    }
    object_reload_and_reset_state(weaponCtx, object, ctx, 1);
    *(unsigned int *)(object + 0x34) |= 0x20;
    if (forced == 1) {
        object_update_camera_offset((int)DAT_80611d74, object);
    }
    FUN_80023e10((double)FLOAT_80616c08, (int **)(*(int *)((int)DAT_80611d74 + 0x14) + 4), 1);
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// object_aim_target_solve (FUN_801b4b44)
// Compute the aim/tracer target for an AI or sniper weapon: pick the muzzle
// origin (either from a fixed model bone for grenade/rocket types, from a
// tracked target entity's transform, or straight ahead), normalise the aim
// vector, and (re)spawn the tracer/aim-marker entity when the resolved weapon
// group changes. iVar5 == 0 selects the local player's field block.
// ---------------------------------------------------------------------------
void object_aim_target_solve(unsigned long long param_1, double param_2, double param_3,
                             double param_4, double param_5, unsigned long long param_6,
                             unsigned long long param_7, unsigned long long param_8)
{
    unsigned long long packed = (unsigned long long)__save_gpr_25();
    int owner = (int)(packed >> 0x20);
    int side  = (int)packed;
    int *pl = *(int **)(owner + 0x7c);
    float *aim = (side == 0) ? (float *)(pl + 0x29) : (float *)(pl + 0x75);
    float wantGroup = (float)(&DAT_8043e238)[side + pl[0x25] * 0xf];
    aim[0x3b] = (float)(intptr_t)pl;

    // Charge ramp for the aim marker (aim[0x41]) when not charging (pl[0x2eb]==0).
    if (pl[0x2eb] == 0) {
        float lvl = aim[0x41];
        if (FLOAT_80616b98 < lvl) {
            if ((int)aim[0x3c] < 6) {
                aim[0x41] = FLOAT_80616b98;
            } else {
                float dt = object_fire_dt_scaled();
                aim[0x41] = -(float)((double)FLOAT_80616c08 * (double)dt - (double)lvl);
                if (aim[0x41] < FLOAT_80616b98) aim[0x41] = FLOAT_80616b98;
                if (FLOAT_80616ba8 < aim[0x41]) aim[0x41] = FLOAT_80616ba8;
            }
        }
    }

    float marker = aim[0x3a];
    int weaponBase = DAT_80610ba0 + (int)aim[0xc] * 0x29c;
    bool solved = false;

    if (player_ai_is_sniper_weapon((int)*(short *)((int)(pl) + 0x48)) == 0 || stats_is_tracking_active() != 0) {
        // Grenade / rocket fixed-bone origins keyed by the raw float tag in aim[0xc].
        if (aim[0xc] == 8.54792e-44f) {           // 0x3d (grenade) tag
            float m[16];
            mtx4_set_rotation_y((double)((FLOAT_80616bdc * *(float *)(owner + 0x70)) / FLOAT_80616be0), m);
            int *sk = *(int **)(*(int *)(owner + 4) + 4);
            float *bone = (float *)sk[*sk * -0x30 + 0x22b];
            aim[0] = bone[0]; aim[1] = bone[1]; aim[2] = bone[2];
            mtx4_transform_point((float *)(*(int *)(*(int *)(owner + 4) + 0x10) +
                                 *(int *)(*(int *)(*(int *)(owner + 4) + 0x84) + 0x30) * 0x40), aim);
            mtx4_transform_point(m, aim);
            aim[1] -= FLOAT_80616c8c;
            aim[0] += *(float *)(owner + 100);
            aim[1] += *(float *)(owner + 0x68);
            aim[2] += *(float *)(owner + 0x6c);
            angles_to_dir((double)FLOAT_80616b98, (double)*(float *)(owner + 0x70), (float *)(pl + 0x2df));
            solved = true;
        } else if (aim[0xc] == 7.84727e-44f) {    // 0x38 (rocket) tag
            float m[16];
            mtx4_set_rotation_y((double)((FLOAT_80616bdc * *(float *)(owner + 0x70)) / FLOAT_80616be0), m);
            int *sk = *(int **)(*(int *)(owner + 4) + 4);
            float *bone = (float *)((int)sk[*sk * -0x30 + 0x22b] + 0x48);
            aim[0] = bone[0]; aim[1] = bone[1]; aim[2] = bone[2];
            mtx4_transform_point((float *)(*(int *)(*(int *)(owner + 4) + 0x10) +
                                 *(int *)(*(int *)(*(int *)(owner + 4) + 0x84) + 0x30) * 0x40), aim);
            mtx4_transform_point(m, aim);
            aim[0] += *(float *)(owner + 100);
            aim[1] += *(float *)(owner + 0x68);
            aim[2] += *(float *)(owner + 0x6c);
            angles_to_dir((double)FLOAT_80616b98, (double)*(float *)(owner + 0x70), (float *)(pl + 0x2df));
            solved = true;
        } else if (marker != 0.0f) {
            int mk = (int)marker;
            if ((*(unsigned int *)(mk + 0x28) & 0x100) == 0 &&
                (*(unsigned int *)(mk + 0x10) & 0xffff) != 0 &&
                *(int *)(*(int *)(mk + 4) + 0x10) != 0) {
                float rz[16], world[8];
                mtx4_set_translate_rotz((double)*(float *)(mk + 100), (double)*(float *)(mk + 0x68),
                                        (double)*(float *)(mk + 0x6c),
                                        (double)((FLOAT_80616bdc * *(float *)(mk + 0x70)) / FLOAT_80616be0), rz);
                mtx4_multiply(*(float **)(*(int *)(mk + 4) + 0x10), rz, world);
                unsigned int bref = *(unsigned int *)(weaponBase + 0x23c);
                if (bref != 0xffffffff) {
                    int *sk2 = *(int **)(*(int *)((int)aim[0x3a] + 4) + 4);
                    float *bpt = (float *)(sk2[*sk2 * -0x30 + ((int)bref >> 0x10) * 0x30 + 0x1b] + (bref & 0xffff) * 0x18);
                    mtx4_set_translation((float *)world, bpt[0], bpt[1], bpt[2]);
                    mtx4_multiply(rz, world, world);
                }
            } else {
                // No live marker transform: aim from the owner toward the tracked target.
                aim[0] = (float)pl[0x2be];
                aim[1] = (float)pl[0x2bf];
                aim[2] = (float)pl[0x2c0];
                if (pl[1] != 0) {
                    pl[0x2df] = (int)(*(float *)(pl[1] + 100) - *(float *)(owner + 100));
                    int tgt = pl[1];
                    if (*(int *)(tgt + 0x20) == 8) {
                        pl[0x2e0] = (int)(*(float *)(tgt + 0x68) -
                            (FLOAT_80616bfc * *(float *)(&DAT_8048ca94 + *(short *)((int)(pl) + 0x24) * 0x30) +
                             *(float *)(owner + 0x68)));
                    } else {
                        pl[0x2e0] = (int)(*(float *)(tgt + 0x68) - *(float *)(owner + 0x68));
                    }
                    pl[0x2e1] = (int)(*(float *)(pl[1] + 0x6c) - *(float *)(owner + 0x6c));
                    float dx = (float)pl[0x2df], dy = (float)pl[0x2e0], dz = (float)pl[0x2e1];
                    float len2 = dx * dx + dy * dy + dz * dz;
                    if (FLOAT_80616c14 < len2) {
                        float inv = 1.0f / (float)sqrt((double)len2);
                        inv = inv * -(float)((double)(FLOAT_80616bc8 * len2) * (double)(inv * inv) - (double)FLOAT_80616bd8);
                        float s = (float)((double)FLOAT_80616ba8 * (double)inv);
                        pl[0x2df] = (int)(dx * s);
                        pl[0x2e0] = (int)(dy * s);
                        pl[0x2e1] = (int)(dz * s);
                    }
                }
            }
        }
    } else {
        // Non-sniper, not tracking: aim straight from the owner.
        aim[0] = *(float *)(owner + 100);
        aim[1] = *(float *)(owner + 0x68) + FLOAT_80616ba8;
        aim[2] = *(float *)(owner + 0x6c);
        angles_to_dir((double)FLOAT_80616b98, (double)*(float *)(owner + 0x70), (float *)(pl + 0x2df));
    }
    (void)solved;

    object_idle_tick();
    if (object_slot_switch_permitted((int)pl, side) == 0) {
        wantGroup = -1.0f;   // sentinel (decomp used -NAN)
    }
    if (aim[0xc] != wantGroup) {
        if (aim[0x3a] != 0.0f) {
            entity_destroy_with_event((int *)(intptr_t)aim[0x3a]);
            aim[0x3a] = 0.0f;
        }
        FUN_801b6afc((int)(intptr_t)pl, (int)(intptr_t)aim);
        aim[0xc] = wantGroup;
        pl[0x2eb] = 0;
        float g = aim[0xc];
        if (g != -1.0f && g != 5.74532e-44f && g != 7.84727e-44f && g != 8.54792e-44f) {
            unsigned int rot2[3] = { DAT_803cd1a4, DAT_803cd1a8, DAT_803cd1ac };
            int ent = FUN_80072d70(packed, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   *(int *)(DAT_80610ba0 + (int)g * 0x29c + 8), (void *)(owner + 100), rot2, 0, 0);
            aim[0x3a] = (float)ent;
            particle_entity_disable_all();
            *(int *)(ent + 0x7c) = (int)(intptr_t)aim;
            *(float *)(ent + 0x8c) = aim[0xc];
            *(unsigned int *)(ent + 0x28) &= 0xffffff9f;
            int *parent = *(int **)(owner + 4);
            int bone = (side == 0) ? *(int *)(parent[0x21] + 0x2c) : *(int *)(parent[0x21] + 0x30);
            entity_setup_attachment(ent, parent, bone, 0xffffffff, (side == 0) ? 2 : 1);
            *(int *)(ent + 0x248) = *(int *)(*pl + 100);
            *(int *)(ent + 0x24c) = *(int *)(*pl + 0x68);
            *(int *)(ent + 0x250) = *(int *)(*pl + 0x6c);
            object_ammo_reload((int)pl, 0, (int)aim, 0);
        }
    }
    __restore_gpr_25();
}
