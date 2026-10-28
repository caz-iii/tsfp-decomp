// object_weapon_types2.cpp
// More per-weapon-type behaviour for the player object: additional
// object_type_dispatch handlers (individual weapons' preload / fire / anim-event
// sounds, render-flag toggles, heat-haze passes and per-frame effect sims) plus
// the grenade fuse-stage timing helpers. Direct continuation of
// object_weapon_types.cpp (batch_557).
//
// Address range: 0x801b7ae0 - 0x801b8650
// Batch: batch_558
//
// Non-matching translation: the 0x43300000 int->double bias idiom (Ghidra's
// int->double conversion) is reduced to ordinary int/float math, and the
// paired-single f-reg spill temporaries are dropped. Raw field-offset pointer
// arithmetic on the object / model / handle structures is preserved (as
// elsewhere in the entity module) since those layouts are not yet typed. The
// object_type_dispatch double args the decompiler surfaced as `in_f31` etc. are
// carried as explicit `double` parameters here.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h -> object_*/sound_*/particle_*/mtx4_*/object_weapon_heathaze_* helpers

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Render/compute spill-frame bracketing (paired push/pop).
extern unsigned long long FUN_803826f0(void);  // spill push: returns (op<<32)|object
extern void               FUN_8038273c(void);   // spill pop (matches FUN_803826f0)
extern unsigned long long __save_gpr_28(void);  // GPR spill push: returns (op<<32)|object
extern void               __restore_gpr_28(void); // GPR spill pop

// Dual-barrel / rotary weapon barrel-state machine + render-target helpers.
extern void FUN_8035f6f4(int *slotPtr, int mode);  // release/reset heat-haze RT slot
extern void FUN_8035f700(void);                    // per-frame barrel-haze update
extern void FUN_80360578(float *xform, void *scratch, float *bonePt); // submit barrel warp quad
extern int  FUN_80360f70(void);                    // query barrel spin-up state
extern void FUN_80360f78(int state);               // set barrel spin-up state
extern void FUN_803610b4(void);                    // barrel wind-up tick

// ---- Fuse-stage timing tables ----
extern int DAT_80440b9c;              // fuse cumulative-time table base (int[0x2f], stride 4)
extern int PTR_caseD_13_80440b98;     // per-stage duration table base (int[], stride 4)

// ---- Particle emitter id list for grenade fuse color-keying ----
extern int DAT_804329e4;              // emitter-id list (int[], terminated by <0)
extern int DAT_80611e2c;              // fallback emitter id slot

// ---- Object / render globals ----
extern int   DAT_80610ba0;   // weapon-type descriptor table base (stride 0x29c)
extern int   DAT_80611d74;   // local player context base
extern int   DAT_80611928;   // current-frame timestamp
extern int   DAT_806109c0;   // frame delta denominator source
extern int   DAT_805c1a18[]; // per-view heat-haze / barrel-warp render-target slot table
extern float DAT_80583228[]; // sine/phase lookup table (fp), 0x7ff-masked index

// ---- fp bias / tuning constants ----
extern double DOUBLE_80616ba0; // int->double bias (0x43300000 magic)
extern double DOUBLE_80616ce0; // int->double bias variant
extern float  FLOAT_806108ec;  // frame-time scale
extern float  FLOAT_80611718;  // world/units scale for sound range
extern float  FLOAT_8060faf8;  // fuse table time base

extern float  FLOAT_80616cc8;  // ramp rate / render-flag threshold
extern float  FLOAT_80616ccc;  // 1.0f-ish clamp
extern float  FLOAT_80616cdc;  // sound pitch constant
extern float  FLOAT_80616ce8;  // ramp denom
extern float  FLOAT_80616cec;  // 0.0f
extern float  FLOAT_80616cf4;  // decay
extern float  FLOAT_80616cf8;  // spin target
extern float  FLOAT_80616d04;  // heat ceiling / fade target
extern float  FLOAT_80616d08;  // heat ramp / threshold
extern float  FLOAT_80616d24;  // rotation scale
extern float  FLOAT_80616d28;  // rotation denom
extern float  FLOAT_80616d50;  // sound vol
extern float  FLOAT_80616d58;  // sound vol / fade target
extern float  FLOAT_80616d5c;  // sound vol
extern float  FLOAT_80616d60;  // heat ramp denom
extern float  FLOAT_80616d64;  // heat overheat threshold
extern float  FLOAT_80616d68;  // sound vol
extern float  FLOAT_80616d6c;  // fuse gate A
extern float  FLOAT_80616d70;  // fuse gate B
extern float  FLOAT_80616d74;  // spin ramp
extern float  FLOAT_80616d78;  // spin impulse
extern float  FLOAT_80616d7c;  // charge gate
extern float  FLOAT_80616d80;  // sound vol
extern float  FLOAT_80616d84;  // sound vol
extern float  FLOAT_80616d88;  // wind-up add
extern float  FLOAT_80616d8c;  // muzzle warp amplitude
extern float  FLOAT_80616d90;  // muzzle warp scale
extern float  FLOAT_80616d94;  // muzzle warp denom A
extern float  FLOAT_80616d98;  // muzzle warp denom B
extern float  FLOAT_80616d9c;  // render-flag threshold A
extern float  FLOAT_80616da0;  // render-flag threshold B

// ---- batch_559: additional per-weapon-type sound/tuning constants ----
extern int    DAT_80611d54;    // global game-state / view context base (frame stamp @ +900)
extern float  FLOAT_80616cd0;  // sound pitch (loop weapon)
extern float  FLOAT_80616cf0;  // sound pitch (loop weapon variant)
extern float  FLOAT_80616d0c;  // sound pitch (single-shot alt)
extern float  FLOAT_80616d34;  // 3d-sound refresh distance gate
extern float  FLOAT_80616da4;  // fire-sound pitch
extern float  FLOAT_80616da8;  // spin step target
extern float  FLOAT_80616dac;  // spin step snap
extern float  FLOAT_80616db0;  // fire-sound pitch
extern float  FLOAT_80616db4;  // fire-sound pitch
extern float  FLOAT_80616db8;  // fire-sound pitch (shared)
extern float  FLOAT_80616dbc;  // fire-sound pitch
extern float  FLOAT_80616dc0;  // fire-sound pitch

// ---- batch_560: charge-weapon + hum-loop tuning constants ----
extern int    DAT_80610b98;    // grenade/charge emitter-id list (int[], terminated by <0)
extern double DOUBLE_80616de0; // int->double bias variant (rng jitter)
extern float  FLOAT_80616cf4;  // fire-sound pitch
extern float  FLOAT_80616cfc;  // charge decay multiplier
extern float  FLOAT_80616ce8;  // fire-sound pitch
extern float  FLOAT_80616d1c;  // emitter lifetime bias
extern float  FLOAT_80616d30;  // charge/spin decay multiplier
extern float  FLOAT_80616dc4;  // charge ramp base
extern float  FLOAT_80616dc8;  // charge ramp rate
extern float  FLOAT_80616dcc;  // charge floor
extern float  FLOAT_80616dd0;  // charge 3d-vol offset
extern float  FLOAT_80616dd4;  // charge 3d-pitch bias
extern float  FLOAT_80616dd8;  // charge detonation gate
extern float  FLOAT_80616ddc;  // rng jitter divisor
extern float  FLOAT_80616de8;  // fire-sound pitch
extern float  FLOAT_80616dec;  // fire-sound pitch

// ---- batch_561: deployable-weapon dispatch fire-sound pitches ----
extern float  FLOAT_80616df0;  // fire-sound pitch
extern float  FLOAT_80616df4;  // fire-sound pitch (alt-fire 0xca8)
extern float  FLOAT_80616df8;  // fire-sound pitch (frame1 0xca7)
extern float  FLOAT_80616dfc;  // fire-sound pitch
} // extern "C"

// rng_counter_tick is declared void in generated_decls (its 64-bit LCG counter
// return is dropped there), but these dispatch handlers read the advanced
// counter. Call it through a correctly-typed pointer to its ROM address to
// recover the u32 result without a conflicting redeclaration.
static inline unsigned int __ow_rng_tick(void)
{
    return ((unsigned int (*)(void))0x8013104c)();
}

// ===========================================================================
// Address: 0x801b7ae0 | Name: object_weapon_dispatch_7ae0 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 loads sprite set (by flag mask); op0x10
// reports anim slot 6; op0x11 plays the fire sound (0xcbb) on frame 0x15.
// ===========================================================================
unsigned int object_weapon_dispatch_7ae0(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    if (param_9 == 0x10) {
        *param_12 = 6;
        return 1;
    }
    if (param_9 < 0x10) {
        if (param_9 == 0) {
            sprite_load_by_flag_mask();
            return 0;
        }
    } else if ((param_9 < 0x12) && (*(int *)(param_10 + 0xe8) == 0x15)) {
        object_play_sound_pitched((double)FLOAT_80616d50, 0xcbb, param_11, 1);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b7b68 | Name: object_weapon_dispatch_7b68 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a pump/lever weapon: op0 preload banks;
// op4 report anim slot 0xc88; op7 fire (toggle render flags by remaining clip
// count from the type descriptor, play 0xc67); op10 auto-cycle when the clip is
// non-empty; op0x10 report slot 6; op0x11 anim-event sounds (0xcbb / 0xc89 by
// frame); op0x12/0x13 render-flag adjust on reload frames.
// ===========================================================================
void object_weapon_dispatch_7b68(int param_1, int param_2, int object, unsigned int *param_4)
{
    unsigned long long packed = FUN_803826f0();
    int handle = (int)packed;
    int op     = (int)(packed >> 0x20);

    (void)param_1;
    (void)param_2;

    switch (op) {
    case 0:
        sound_clear_bank_ref(0xc67);
        sound_clear_bank_ref(0xc88);
        sound_clear_bank_ref(0xc89);
        break;
    case 4:
        *param_4 = 0xc88;
        break;
    case 7: {
        int descBase = DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c;
        int ammoField = *(int *)(descBase + 0x14);
        if (*(short *)(object + ammoField * 2 + 0x38) == 0) {
            object_apply_render_flag(handle, 7, 0);
        }
        if (*(short *)(object + ammoField * 2 + 0x38) < 2) {
            object_apply_render_flag(handle, 8, 0);
        }
        object_play_sound_at(0xc67, object, 1);
        break;
    }
    case 10:
        if (*(int *)(handle + 0x38) == 1) {
            int ammoField = *(int *)(DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c + 0x14);
            if (0 < *(short *)(object + ammoField * 2 + 0x38)) {
                object_set_handle(handle, 0xc);
                object_anim_commit_default();
            }
        }
        break;
    case 0x10:
        *param_4 = 6;
        break;
    case 0x11: {
        int frame = *(int *)(handle + 0xe8);
        if (frame < 0x19) {
            if (frame == 0x15) {
                object_play_sound_pitched((double)FLOAT_80616d58, 0xcbb, object, 1);
            }
        } else if (frame < 0x1b) {
            object_play_sound_pitched((double)FLOAT_80616d5c, 0xc89, object, 1);
        }
        break;
    }
    case 0x12: {
        int ammoField = *(int *)(DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c + 0x14);
        if ((*(int *)(handle + 0xe8) == 1) &&
            (*(short *)(object + ammoField * 2 + 0x38) < 2)) {
            object_apply_render_flag(handle, 7, 0);
        }
        break;
    }
    case 0x13:
        if ((*(int *)(handle + 0xe8) == 9) && (FLOAT_80616cc8 < *(float *)(handle + 0xec))) {
            object_apply_render_flag(handle, 7, 1);
            object_apply_render_flag(handle, 8, 1);
        }
        break;
    }
    FUN_8038273c();
}

// ===========================================================================
// Address: 0x801b7da8 | Name: object_weapon_dispatch_7da8 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preloads the sound bank (0xd5f) and loads
// the sprite/texture set; other ops are no-ops. Returns 0.
// ===========================================================================
unsigned int object_weapon_dispatch_7da8(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    if (((param_9 != 10) && (param_9 < 10)) && (param_9 == 0)) {
        sound_clear_bank_ref(0xd5f);
        sprite_load_by_flag_mask();
        sprite_load_texture_set();
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b7e08 | Name: object_weapon_fuse_stage | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Compares an elapsed fuse time `elapsed` against the cumulative fuse-stage
// threshold for stage `stage` and returns a three-way sign result
// (-1 / 0 / +1) used to advance the grenade fuse. Stage 0 uses the base
// threshold DAT_80440b9c[0]; stages 1..0x2e use DAT_80440b9c[stage] minus the
// per-stage duration PTR_caseD_13_80440b98[stage]. Out-of-range -> 0.
// ===========================================================================
int object_weapon_fuse_stage(unsigned int stage, unsigned int elapsed)
{
    int *cumTable = &DAT_80440b9c;
    int *durTable = &PTR_caseD_13_80440b98;

    if (stage == 0) {
        int thresh = cumTable[0];
        return ((int)elapsed >> 0x1f) +
               ((unsigned int)((unsigned int)thresh <= elapsed) - (thresh >> 0x1f));
    }
    if ((0 < (int)stage) && (stage < 0x2f)) {
        int thresh = cumTable[stage] - durTable[stage];
        return ((int)elapsed >> 0x1f) +
               ((unsigned int)((unsigned int)thresh <= elapsed) - (thresh >> 0x1f));
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b7e74 | Name: object_weapon_fuse_stage_count | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Returns the number of fuse stages (0x2f) in the grenade fuse table.
// ===========================================================================
unsigned int object_weapon_fuse_stage_count(void)
{
    return 0x2f;
}

// ===========================================================================
// Address: 0x801b7e7c | Name: object_weapon_grenade_dispatch | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for the timed/grenade weapon type. op0 preload
// banks; op1 per-frame fuse charge ramp (field +0x108, gated by fuse timing);
// op4 report anim slot 0xca1; op9 set player particle emitter lifetime/rate;
// op10 fire: advance the fuse (spectator branch snaps a display timer; owner
// branch commits the throw anim, and on the final stage detonates -- plays
// 0xc6e, recolors the linked object's cached particle emitters (DAT_804329e4
// id list) and restarts them); op0x10 report slot 6; op0x11 fire sound 0xcbb on
// frame 0x15; op0x14 overheat/spin-down handling; op0x17 fade out the loop.
// ===========================================================================
void object_weapon_grenade_dispatch(double param_1, double param_2, int object_arg, unsigned int *param_4)
{
    (void)param_1;
    (void)param_2;

    unsigned long long packed = FUN_803826f0();
    int object = (int)packed;
    int op     = (int)(packed >> 0x20);
    float zero = FLOAT_80616cec;

    switch (op) {
    case 0:
        sound_clear_bank_ref(0xc6e);
        sound_clear_bank_ref(0xca1);
        sound_clear_bank_ref(0xc6e);
        break;

    case 1: {
        float dt = FLOAT_806108ec * (FLOAT_80616ccc / (float)((int)DAT_806109c0));
        if ((*(int *)(object + 0x64) == 0) && (*(int *)(object + 0x44) != 8)) {
            float fuseElapsed = (float)((int)*(unsigned int *)(object_arg + 0xf0));
            float fuseGate = FLOAT_80616d6c * (FLOAT_80616d70 / (float)((int)DAT_806109c0));
            if (fuseElapsed <= fuseGate) {
                *(float *)(object_arg + 0x108) = -(FLOAT_80616d78 * dt - *(float *)(object_arg + 0x108));
            } else {
                *(float *)(object_arg + 0x108) =
                    *(float *)(object_arg + 0x108) -
                    (FLOAT_80616cf4 * dt +
                     FLOAT_80616d74 * dt * (FLOAT_80616cf8 - *(float *)(object_arg + 0x108)));
                if (*(float *)(object_arg + 0x108) < zero) {
                    *(float *)(object_arg + 0x108) = zero;
                }
            }
        }
        break;
    }

    case 4:
        *param_4 = 0xca1;
        break;

    case 9: {
        ParticleEmitter *e = (ParticleEmitter *)particle_get_current_player_emitter();
        ((int *)e)[9] = 0x7f;
        e = (ParticleEmitter *)particle_get_current_player_emitter();
        ((int *)e)[10] = 0x1e;
        break;
    }

    case 10:
        *(int *)(object + 0x64) = 1;
        if (*(int *)(DAT_80611d74 + 4) < 0) {
            // spectator / no local view: snap the display fuse timer forward
            float elapsed = (float)((int)DAT_80611928);
            float budget  = FLOAT_80616d7c * (float)((int)DAT_806109c0);
            if (budget < elapsed - *(float *)(object + 0x358)) {
                *(float *)(object + 0x358) = elapsed;
            }
        } else {
            if (object_anim_frame_reached(object, 3) == 0) {
                float interp = *(float *)(object_arg + 0x108);
                object_anim_commit_default();
                object_anim_set_interp(interp, object);
                *(float *)(object + 0x358) = FLOAT_80616cec;
            } else {
                float fuseTime = *(float *)(object + 0xec);
                *(int *)(object_arg + 0x108) = *(int *)(object + 0xf0);
                if (object_anim_pending_reached(object, 3) == 0) {
                    // walk fuse table to find the current stage time
                    int *cumTable = &DAT_80440b9c;
                    for (int i = 0; i < 0x2f; i++) {
                        float stageTime =
                            (FLOAT_80616ccc / FLOAT_8060faf8) * (float)((int)cumTable[i]);
                        if ((double)fuseTime <= (double)stageTime) {
                            if (*(float *)(object + 0x358) != stageTime) {
                                *(float *)(object + 0x358) = stageTime;
                            }
                            break;
                        }
                    }
                } else {
                    // detonation
                    *(float *)(object_arg + 0x108) = FLOAT_80616ccc;
                    object_set_handle(object, 8);
                    object_anim_commit_default();
                    object_play_sound_at(0xc6e, object_arg, 1);
                    if (*(int *)(object + 0x54) != 0) {
                        int *idList = &DAT_804329e4;
                        for (int i = 0; idList[i] >= 0; i++) {
                            int emitterId = idList[i];
                            int *slot;
                            if (emitterId < *(int *)(*(int *)(object + 0x54) + 0x1e8)) {
                                slot = (int *)(*(int *)(*(int *)(object + 0x54) + 0x1fc) + emitterId * 4);
                            } else {
                                slot = &DAT_80611e2c;
                            }
                            int emitter = *slot;
                            particle_emitter_set_single_color_key((ParticleEmitter *)emitter,
                                                                  *(int *)(DAT_80611d74 + 4) + 0xca);
                            int flagsArg = emitter;
                            particle_emitter_set_flags((unsigned int)flagsArg, 0x10000, 0);
                            int restartArg = emitter;
                            particle_cached_restart((ParticleEmitter **)&restartArg);
                        }
                    }
                }
            }
        }
        break;

    case 0x10:
        *param_4 = 6;
        break;

    case 0x11:
        if (*(int *)(object + 0xe8) == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616d68, 0xcbb, object_arg, 1);
        }
        break;

    case 0x14:
        if (FLOAT_80616d08 < *(float *)(object + 0x48)) {
            *(float *)(object_arg + 0x108) =
                FLOAT_80616ccc - (*(float *)(object + 0x48) - FLOAT_80616d08) / FLOAT_80616d60;
        }
        if (FLOAT_80616d64 < *(float *)(object + 0x48)) {
            *(float *)(object_arg + 0x108) = FLOAT_80616cec;
            object_set_handle(object, 1);
        }
        break;

    case 0x17:
        sound_register_fade_out((double)FLOAT_80616ccc, (double)FLOAT_80616d58,
                                (int *)(object_arg + 0x100), (unsigned int *)0);
        break;
    }

    FUN_8038273c();
}

// ===========================================================================
// Address: 0x801b82f0 | Name: object_weapon_dispatch_82f0 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preloads sound banks (0xd0c/0xd0d) + entity
// resources (asset 0x74c); op4 reports the loop sound bank (0xd0c); op0x10
// reports anim slot 7. Returns a per-case status.
// ===========================================================================
unsigned int object_weapon_dispatch_82f0(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;
    (void)param_10; (void)param_11;

    if (param_9 == 4) {
        *param_12 = 0xd0c;
    } else if (param_9 < 4) {
        if (param_9 == 0) {
            sound_clear_bank_ref(0xd0c);
            sound_clear_bank_ref(0xd0d);
            entity_preload_resources(0x74c);
            return 0;
        }
    } else if (param_9 == 0x10) {
        *param_12 = 7;
        return 1;
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b8374 | Name: object_weapon_flame_dispatch | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a flame/spray weapon: op0 preload banks +
// texture set; op4 report ready; op0xd set up the heat-haze render pass; op0xe
// release it; op0x11 per-frame anim-event sounds (0xc93/0xc96/paired 0xc92+0xc93
// muzzle roar / 0xcbb by frame). Returns a per-case status.
// ===========================================================================
unsigned int object_weapon_flame_dispatch(double param_1, double param_2, double param_3, double param_4,
                                          double param_5, double param_6, double param_7, double param_8,
                                          int param_9, int param_10, int param_11, int param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    switch (param_9) {
    case 0:
        sound_clear_bank_ref(0xc92);
        sound_clear_bank_ref(0xc93);
        sound_clear_bank_ref(0xc95);
        sound_clear_bank_ref(0xc96);
        sprite_load_texture_set();
        break;
    case 4:
        return 1;
    case 0xd:
        if (*(int *)(param_10 + 0x54) != 0) {
            object_weapon_heathaze_setup(param_10, *(int *)(param_10 + 0x54), 2);
        }
        return 1;
    case 0xe:
        object_weapon_heathaze_release();
        return 1;
    case 0x11: {
        int frame = *(int *)(param_10 + 0xe8);
        if (frame == 0x13) {
            if (param_12 != 0) {
                object_play_sound_at(0xc93, param_11, 1);
            }
        } else if (frame < 0x13) {
            if (frame == 9) {
                object_play_sound_at(0xc96, param_11, 1);
            } else if (((8 < frame) && (0x11 < frame)) && (param_12 != 0)) {
                object_play_sound_vol_pitched((double)FLOAT_80616d80, (double)FLOAT_80616cc8, 0xc92, param_11, 1);
                object_play_sound_vol_pitched((double)FLOAT_80616d84, (double)FLOAT_80616cc8, 0xc93, param_11, 1);
            }
        } else if (frame == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616d58, 0xcbb, param_11, 1);
        }
        break;
    }
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b84e8 | Name: object_weapon_flame_dispatch_alt | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Sibling of object_weapon_flame_dispatch for a second flame-type variant: op0
// preload; op0xd heat-haze setup (mode 4); op0xe release; op0x11 anim-event
// sounds (same set); op0x17 fade out the loop. Returns a per-case status.
// ===========================================================================
unsigned int object_weapon_flame_dispatch_alt(int param_1, int param_2, int param_3, int param_4)
{
    switch (param_1) {
    case 0:
        sound_clear_bank_ref(0xc96);
        sound_clear_bank_ref(0xc92);
        sound_clear_bank_ref(0xc93);
        break;
    case 0xd:
        if (*(int *)(param_2 + 0x54) != 0) {
            object_weapon_heathaze_setup(param_2, *(int *)(param_2 + 0x54), 4);
        }
        return 1;
    case 0xe:
        object_weapon_heathaze_release();
        return 1;
    case 0x11: {
        int frame = *(int *)(param_2 + 0xe8);
        if (frame == 0x13) {
            if (param_4 != 0) {
                object_play_sound_at(0xc93, param_3, 1);
            }
        } else if (frame < 0x13) {
            if (frame == 9) {
                object_play_sound_at(0xc96, param_3, 1);
            } else if (((8 < frame) && (0x11 < frame)) && (param_4 != 0)) {
                object_play_sound_vol_pitched((double)FLOAT_80616d80, (double)FLOAT_80616cc8, 0xc92, param_3, 1);
                object_play_sound_vol_pitched((double)FLOAT_80616d84, (double)FLOAT_80616cc8, 0xc93, param_3, 1);
            }
        } else if (frame == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616d58, 0xcbb, param_3, 1);
        }
        break;
    }
    case 0x17:
        sound_register_fade_out((double)FLOAT_80616ccc, (double)FLOAT_80616d04,
                                (int *)(param_3 + 0x100), (unsigned int *)0);
        break;
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b8650 | Name: object_weapon_dualbarrel_dispatch | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for the rotary/dual-barrel weapon type. Cases:
// 0 preload banks + textures; 1 per-frame dual looping-sound + spin update
// (levels +0xdc/+0xe0, sounds 0xc99/0xc9a); 3 reset + open barrel-warp target;
// 4 reset + report anim slot; 5/6 barrel state transitions; 7 wind-up; 0xb
// report; 0xf submit the muzzle barrel-warp quad (jittered by the DAT_80583228
// sine table); 0x10 report anim slot 7; 0x11 anim-event driven barrel
// open/close render-flag sweeps + sounds (0xc9b/0xc9c/0xc98/0xcbb); 0x12/0x13
// reload-frame render-flag adjustments.
// ===========================================================================
void object_weapon_dualbarrel_dispatch(double param_1, double param_2, double param_3, double param_4,
                                       double param_5, double param_6, double param_7, double param_8,
                                       int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;
    (void)param_9; (void)param_10;

    unsigned long long packed = FUN_803826f0();
    int object = (int)packed;
    int op     = (int)(packed >> 0x20);
    float zero = FLOAT_80616cec;

    switch (op) {
    case 0:
        sound_clear_bank_ref(0xc97);
        sound_clear_bank_ref(0xc98);
        sound_clear_bank_ref(0xc9b);
        sound_clear_bank_ref(0xc9c);
        sound_clear_bank_ref(0xc99);
        sound_clear_bank_ref(0xc9a);
        sound_clear_bank_ref(0xc9d);
        sprite_load_texture_set();
        break;

    case 1: {
        int model = *(int *)(object + 0x54);
        if (model != 0) {
            float pos[3];
            object_resolve_sound_position(param_11, pos, 1);

            // clamp the two spin levels into [0,1]
            float lvlA = *(float *)(object + 0xe0);
            float capA = (FLOAT_80616ccc < lvlA) ? FLOAT_80616ccc : lvlA;
            float outA = FLOAT_80616cec;
            if ((FLOAT_80616cec <= capA) && (FLOAT_80616ccc < lvlA)) {
                outA = FLOAT_80616ccc;
            } else if (FLOAT_80616cec <= capA) {
                outA = lvlA;
            }
            *(float *)(object + 0xe0) = outA;

            float dt = (float)((int)DAT_806109c0);
            float lvlB = *(float *)(object + 0xdc);
            float decayB = FLOAT_80616cc8 * dt * -(FLOAT_80616ccc / (FLOAT_80616ce8 * dt));
            float capB = (FLOAT_80616ccc < lvlB) ? FLOAT_80616ccc : lvlB;
            float outB = capB;
            if ((decayB <= capB) && (FLOAT_80616ccc < lvlB)) {
                outB = FLOAT_80616ccc;
            } else if (decayB <= capB) {
                outB = lvlB;
            }
            *(float *)(object + 0xdc) = outB;

            sound_update_3d_source((double)(FLOAT_80611718 * *(float *)(object + 0xe0)),
                                   (double)FLOAT_80616cdc, (double)FLOAT_80616ccc,
                                   model + 0x22c, (unsigned int *)pos, (float *)(intptr_t)0xc9a, 0);
            sound_update_3d_source((double)(FLOAT_80611718 * *(float *)(object + 0xdc)),
                                   (double)FLOAT_80616cdc, (double)FLOAT_80616ccc,
                                   model + 0x228, (unsigned int *)pos, (float *)(intptr_t)0xc99, 0);

            *(float *)(object + 0xe0) = *(float *)(object + 0xe0) - FLOAT_80616d78;

            bool spinning = false;
            if (((*(int *)(object + 0x38) == 1) || (*(int *)(object + 0x44) == 2)) ||
                (*(int *)(object + 0x44) == 5)) {
                spinning = true;
            }
            float rampB;
            if (spinning) {
                rampB = -(FLOAT_80616ccc / (FLOAT_80616ce8 * dt));
            } else {
                rampB = FLOAT_80616ccc / (FLOAT_80616ce8 * dt);
            }
            *(float *)(object + 0xdc) = *(float *)(object + 0xdc) + rampB;
        }
        FUN_8035f700();
        break;
    }

    case 3:
        *(float *)(object + 0xdc) = FLOAT_80616cec;
        *(float *)(object + 0xe0) = zero;
        *(int *)(object + 0xa4) = -1;
        FUN_8035f6f4(&DAT_805c1a18[*(int *)(DAT_80611d74 + 4)], 1);
        FUN_80360f78(0);
        if (*(int *)(object + 0x38) == 0) {
            FUN_80360f78(1);
            object_play_sound_at(0xc98, param_11, 1);
        }
        break;

    case 4: {
        // report anim slot: 6 if secondary hand, else 7 (encoded as raw float bits in decomp)
        int slot = (*(int *)(object + 0x38) == 0) ? 7 : 6;
        *param_12 = (unsigned int)slot;
        *(float *)(object + 0xdc) = FLOAT_80616cec;
        *(float *)(object + 0xe0) = zero;
        break;
    }

    case 5:
        if (FUN_80360f70() != 0) {
            FUN_80360f78(3);
        }
        break;

    case 6:
        FUN_80360f78(0);
        break;

    case 7:
        if (*(int *)(object + 0x38) == 1) {
            *(float *)(object + 0xe0) = *(float *)(object + 0xe0) + FLOAT_80616d88;
            FUN_803610b4();
        }
        break;

    case 8:
        // countLeadingZeros(...) side-effect-free read in the decomp; no-op here
        break;

    case 0xb:
        *param_12 = (unsigned int)(int)FLOAT_80616d58;
        break;

    case 0xf:
        if (((object != 0) && (*(int *)(object + 0x54) != 0)) && (FUN_80360f70() != 0)) {
            int model = *(int *)(object + 0x54);
            unsigned int idxA = (unsigned int)(FLOAT_80616d90 * FLOAT_80616d24 * FLOAT_80616d08 *
                                               ((float)((int)(DAT_80611928 % 0x1fe)) / FLOAT_80616d94) +
                                               FLOAT_80616d04);
            unsigned int idxB = (unsigned int)(FLOAT_80616d90 * FLOAT_80616d24 * FLOAT_80616d08 *
                                               ((float)((int)(DAT_80611928 % 0x15e)) / FLOAT_80616d98) +
                                               FLOAT_80616d04);
            float warpX = FLOAT_80616d8c * DAT_80583228[idxA & 0x7ff];
            float warpY = FLOAT_80616d8c * DAT_80583228[idxB & 0x7ff];

            float bonePt[3];
            float xform[16];
            unsigned char scratch[64];
            object_bone_local_point((unsigned int)object, 0x20000, bonePt);
            mtx4_set_transform_euler((double)*(float *)(model + 0x64), (double)*(float *)(model + 0x68),
                                     (double)*(float *)(model + 0x6c),
                                     (double)((FLOAT_80616d24 * *(float *)(model + 0xc0)) / FLOAT_80616d28),
                                     (double)((FLOAT_80616d24 * *(float *)(model + 0x70)) / FLOAT_80616d28),
                                     (double)((FLOAT_80616d24 * *(float *)(model + 0xc8)) / FLOAT_80616d28),
                                     xform);
            mtx4_set_translation(xform,
                                 (float)((double)*(float *)(model + 0x64) + (double)warpX),
                                 (float)((double)*(float *)(model + 0x68) + (double)warpY),
                                 *(float *)(model + 0x6c));
            FUN_80360578(xform, scratch, bonePt);
        }
        break;

    case 0x10:
        *param_12 = 7;
        break;

    case 0x11: {
        int frame = *(int *)(object + 0xe8);
        if (frame == 0x18) {
            for (int i = 6; i < 0x14; i++) object_apply_render_flag(object, i, 1);
        } else if (frame == 0x19) {
            object_play_sound_at(0xc9b, param_11, 1);
            if (FUN_80360f70() != 0) {
                FUN_80360f78(3);
            }
        } else if (frame == 0x1a) {
            object_play_sound_at(0xc9c, param_11, 1);
        } else if (frame == 0x27) {
            for (int i = 6; i < 0x14; i++) object_apply_render_flag(object, i, 0);
            FUN_80360f78(0);
        } else if ((frame == 0x15) || (frame == 0x16)) {
            for (int i = 6; i < 0x14; i++) object_apply_render_flag(object, i, 0);
            object_play_sound_at(0xcbb, param_11, 1);
            FUN_80360f78(0);
        } else if (frame == 0x1b) {
            for (int i = 6; i < 0x14; i++) object_apply_render_flag(object, i, 1);
        } else if ((frame == 10) &&
                   ((FUN_80360f70() == 3) || (FUN_80360f70() == 0))) {
            FUN_80360f78(1);
            object_play_sound_at(0xc98, param_11, 1);
        }
        break;
    }

    case 0x12:
        if (*(int *)(object + 0xe8) == 0x1a) {
            FUN_80360f78(1);
        }
        break;

    case 0x13:
        if (*(int *)(object + 0xe8) == 0x19) {
            for (int i = 6; i < 0x14; i++) {
                object_apply_render_flag(object, i,
                    (unsigned int)((double)FLOAT_80616d9c < (double)*(float *)(object + 0xec)));
            }
        }
        if ((*(int *)(object + 0xe8) == 0x1a) && (FLOAT_80616da0 < *(float *)(object + 0xec))) {
            for (int i = 6; i < 0x14; i++) object_apply_render_flag(object, i, 0);
        }
        break;
    }

    FUN_8038273c();
}

// ===========================================================================
// batch_559: continuation of the per-weapon-type object_type_dispatch handlers
// (address range 0x801b8dbc - 0x801b9550). Same shape as above: op-switched
// dispatchers doing preload (op0), per-frame 3D loop-sound updates (op1),
// anim-slot reports (op0x10), and anim-event fire/impact sounds (op0x11/0x16/
// 0x17). Non-matching: int->double bias idiom and f-reg spill temporaries
// dropped; raw field-offset pointer arithmetic on the object/model structs
// preserved (layouts not yet typed).
// ===========================================================================

// ===========================================================================
// Address: 0x801b8dbc | Name: object_weapon_dispatch_8dbc | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op4 reports ready (1); op3 (2<op<4) clears a
// model render-flag bit (+0x28 &= ~1) when the model handle is present; op0x11
// plays the fire sound (0xcbb) on frames 0x15..0x16.
// ===========================================================================
unsigned int object_weapon_dispatch_8dbc(int param_1, int param_2, int param_3)
{
    if (param_1 == 4) {
        return 1;
    }
    if (param_1 < 4) {
        if (2 < param_1) {
            int model = *(int *)(param_2 + 0x54);
            if (model != 0) {
                *(unsigned int *)(model + 0x28) &= 0xfffffffe;
            }
            return 1;
        }
    } else if (((param_1 == 0x11) && (*(int *)(param_2 + 0xe8) < 0x17)) &&
               (0x14 < *(int *)(param_2 + 0xe8))) {
        object_play_sound_at(0xcbb, param_3, 1);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b8e50 | Name: object_weapon_scoped_dispatch | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a scoped/looping weapon: op0 preloads the
// sound banks (0xcbc/0xcbd/0xcbe/0xc6d/0xcb0) + entity resources (0x6f1) + three
// texture sets; op1 per-frame dual 3D loop-sound update (the muzzle loop 0xcb0
// tracks a spin level and the second loop 0xcbd ramps toward a target that
// depends on the trigger state, snapped via scalar_step_toward_snap); op3 zero
// the spin level; op0x10 report anim slot 6; op0x11 fire sound (0xcbb) on
// frame 0x15; op0x16 impact sounds (0xcbe on impact-count 2, else 0xcbc).
// ===========================================================================
unsigned int object_weapon_scoped_dispatch(double param_1, double param_2, double param_3, double param_4,
                                           double param_5, double param_6, double param_7, double param_8,
                                           int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    switch (param_9) {
    case 0:
        sound_clear_bank_ref(0xcbc);
        sound_clear_bank_ref(0xcbd);
        sound_clear_bank_ref(0xcbe);
        sound_clear_bank_ref(0xc6d);
        sound_clear_bank_ref(0xcb0);
        entity_preload_resources(0x6f1);
        sprite_load_texture_set();
        sprite_load_texture_set();
        sprite_load_texture_set();
        return 0;

    case 1: {
        int model = *(int *)(param_10 + 0x54);
        if (model != 0) {
            bool refresh = false;
            if ((ABS(*(float *)(DAT_80611d54 + 900) - *(float *)(model + 0xe0)) < FLOAT_80616d34) ||
                ((*(unsigned int *)(DAT_80611d74 + 0xc) & 0x20) == 0)) {
                refresh = true;
            }
            float vol = refresh ? FLOAT_80616cec : FLOAT_80616ccc;

            float pos[3];
            object_resolve_sound_position(param_11, pos, 0);
            sound_update_3d_source((double)(float)((double)FLOAT_80611718 * (double)vol),
                                   (double)FLOAT_80616cdc, (double)FLOAT_80616ccc,
                                   model + 0x22c, (unsigned int *)pos, (float *)(intptr_t)0xcb0, 0);
            *(unsigned int *)(model + 0xe0) = *(unsigned int *)(DAT_80611d54 + 900);

            float target = FLOAT_80616cec;
            float rate   = FLOAT_80616da8;
            if (*(int *)(param_10 + 0x38) == 1) {
                target = FLOAT_80616dac;
                rate   = FLOAT_80616d58;
            }
            scalar_step_toward_snap((double)target, (double)(FLOAT_806108ec * rate),
                                    (float *)(param_10 + 0xdc));
            sound_update_3d_source((double)*(float *)(param_10 + 0xdc),
                                   (double)FLOAT_80616cdc, (double)FLOAT_80616ccc,
                                   model + 0x228, (unsigned int *)pos, (float *)(intptr_t)0xcbd, 0);
        }
        return 1;
    }

    case 3:
        *(float *)(param_10 + 0xdc) = FLOAT_80616cec;
        break;

    case 0x10:
        *param_12 = 6;
        return 1;

    case 0x11:
        if (*(int *)(param_10 + 0xe8) == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616da4, 0xcbb, param_11, 1);
        }
        break;

    case 0x16:
        if (param_12 == (unsigned int *)0x2) {
            object_play_sound_at(0xcbe, param_11, 1);
        } else if (((int)(intptr_t)param_12 < 2) && (-1 < (int)(intptr_t)param_12)) {
            object_play_sound_at(0xcbc, param_11, 1);
        }
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b9080 | Name: object_weapon_dispatch_9080 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preloads sound banks (0xcb6/0x630/0x631/
// 0xcb9) + entity resources (0x753); op0x10 report anim slot 6; op0x11 fire
// sounds by anim frame (0xcbb pitched on frame 0x15, 0xcb6 on frame 9).
// ===========================================================================
unsigned int object_weapon_dispatch_9080(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    if (param_9 == 0x10) {
        *param_12 = 6;
        return 1;
    }
    if (param_9 < 0x10) {
        if (param_9 == 0) {
            sound_clear_bank_ref(0xcb6);
            sound_clear_bank_ref(0x630);
            sound_clear_bank_ref(0x631);
            entity_preload_resources(0x753);
            sound_clear_bank_ref(0xcb9);
            return 0;
        }
    } else if (param_9 < 0x12) {
        int frame = *(int *)(param_10 + 0xe8);
        if (frame == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616cd0, 0xcbb, param_11, 1);
        } else if ((frame < 0x15) && (frame == 9)) {
            object_play_sound_at(0xcb6, param_11, 1);
        }
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b9148 | Name: object_weapon_dispatch_9148 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload bank 0xcb5; op0x11 fire sounds
// (0xcbb pitched on frame 0x15, 0xcb5 pitched on frame 9); op0x17 register the
// loop fade-out.
// ===========================================================================
unsigned int object_weapon_dispatch_9148(int param_1, int param_2, int param_3)
{
    if (param_1 == 0x11) {
        int frame = *(int *)(param_2 + 0xe8);
        if (frame == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616cf0, 0xcbb, param_3, 1);
        } else if ((frame < 0x15) && (frame == 9)) {
            object_play_sound_pitched((double)FLOAT_80616db0, 0xcb5, param_3, 1);
        }
    } else if (param_1 < 0x11) {
        if (param_1 == 0) {
            sound_clear_bank_ref(0xcb5);
        }
    } else if (param_1 == 0x17) {
        sound_register_fade_out((double)FLOAT_80616ccc, (double)FLOAT_80616d58,
                                (int *)(param_3 + 0x100), (unsigned int *)0);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b91fc | Name: object_weapon_dispatch_91fc | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload entity resources (0x754) + bank
// 0xcb9; op0x10 report anim slot 6; op0x11 fire sound (0xcbb) pitched on frame
// 0x15.
// ===========================================================================
unsigned int object_weapon_dispatch_91fc(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    if (param_9 == 0x10) {
        *param_12 = 6;
        return 1;
    }
    if (param_9 < 0x10) {
        if (param_9 == 0) {
            entity_preload_resources(0x754);
            sound_clear_bank_ref(0xcb9);
            return 0;
        }
    } else if ((param_9 < 0x12) && (*(int *)(param_10 + 0xe8) == 0x15)) {
        object_play_sound_pitched((double)FLOAT_80616db4, 0xcbb, param_11, 1);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b928c | Name: object_weapon_dispatch_928c | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload banks (0xd41/0xd42); op0xb report a
// fade/vol constant; op0x10 report anim slot 7; op0x11 fire sound (0xcbb)
// pitched on frame 0x15.
// ===========================================================================
unsigned int object_weapon_dispatch_928c(int param_1, int param_2, int param_3, unsigned int *param_4)
{
    if (param_1 == 0xb) {
        *param_4 = (unsigned int)(int)FLOAT_80616d58;
        return 1;
    }
    if (param_1 < 0xb) {
        if (param_1 == 0) {
            sound_clear_bank_ref(0xd41);
            sound_clear_bank_ref(0xd42);
            return 0;
        }
    } else if (param_1 == 0x11) {
        if (*(int *)(param_2 + 0xe8) == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616db8, 0xcbb, param_3, 1);
        }
    } else if ((param_1 < 0x11) && (0xf < param_1)) {
        *param_4 = 7;
        return 1;
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b9338 | Name: object_weapon_dispatch_9338 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload banks (0xd3f/0xd40/0xd42); op0xb
// report fade/vol; op0x10 report anim slot 7; op0x11 fire sound (0xcbb)
// pitched on frame 0x15.
// ===========================================================================
unsigned int object_weapon_dispatch_9338(int param_1, int param_2, int param_3, unsigned int *param_4)
{
    if (param_1 == 0xb) {
        *param_4 = (unsigned int)(int)FLOAT_80616d58;
        return 1;
    }
    if (param_1 < 0xb) {
        if (param_1 == 0) {
            sound_clear_bank_ref(0xd3f);
            sound_clear_bank_ref(0xd40);
            sound_clear_bank_ref(0xd42);
            return 0;
        }
    } else if (param_1 == 0x11) {
        if (*(int *)(param_2 + 0xe8) == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616db8, 0xcbb, param_3, 1);
        }
    } else if ((param_1 < 0x11) && (0xf < param_1)) {
        *param_4 = 7;
        return 1;
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b93ec | Name: object_weapon_dispatch_93ec | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload banks (0xd42/0xd43); op10 (anim
// state) queries object_slot_stub and returns 2 or 0 depending on slot state
// when the object subtype (+0x30) is 0x1b; op0xb report fade/vol; op0x10 report
// anim slot 7; op0x11 fire sound (0xcbb) pitched on frame 0x15.
// ===========================================================================
unsigned char object_weapon_dispatch_93ec(int param_1, int param_2, int param_3, unsigned int *param_4)
{
    switch (param_1) {
    case 0:
        sound_clear_bank_ref(0xd42);
        sound_clear_bank_ref(0xd43);
        return 0;
    case 10:
        if (*(int *)(param_2 + 0x30) == 0x1b) {
            int slot = object_slot_stub();
            return (unsigned char)(~-(slot != 0) & 2);
        }
        break;
    case 0xb:
        *param_4 = (unsigned int)(int)FLOAT_80616d58;
        return 1;
    case 0x10:
        *param_4 = 7;
        return 1;
    case 0x11:
        if (*(int *)(param_2 + 0xe8) == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616db8, 0xcbb, param_3, 1);
        }
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b94bc | Name: object_weapon_dispatch_94bc | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload bank 0xca6 + load a texture set;
// op0x11 fire sounds by anim frame (0xcbb on frame 0x15, 0xca6 pitched on
// frame 9).
// ===========================================================================
unsigned int object_weapon_dispatch_94bc(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9, int param_10, int param_11)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    if (param_9 == 0x11) {
        int frame = *(int *)(param_10 + 0xe8);
        if (frame == 0x15) {
            object_play_sound_at(0xcbb, param_11, 1);
        } else if ((frame < 0x15) && (frame == 9)) {
            object_play_sound_pitched((double)FLOAT_80616d0c, 0xca6, param_11, 1);
        }
    } else if ((param_9 < 0x11) && (param_9 == 0)) {
        sound_clear_bank_ref(0xca6);
        sprite_load_texture_set();
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b9550 | Name: object_weapon_loop3d_dispatch | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a looping 3D-sound weapon: op0 preload banks
// (0xc84/0xc8c) + three texture sets; op1 per-frame 3D loop-sound update (muzzle
// loop 0xc8c, refresh gated by the frame-stamp delta / view flag); op0x10
// report anim slot 6; op0x11 fire sounds (0xcbb on frame 0x15, 0xc84 pitched on
// frame 0xc).
// ===========================================================================
unsigned int object_weapon_loop3d_dispatch(double param_1, double param_2, double param_3, double param_4,
                                           double param_5, double param_6, double param_7, double param_8,
                                           int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    if (param_9 == 0x10) {
        *param_12 = 6;
        return 1;
    }
    if (param_9 < 0x10) {
        if (param_9 == 1) {
            int model = *(int *)(param_10 + 0x54);
            if (model != 0) {
                bool refresh = false;
                if ((ABS(*(float *)(DAT_80611d54 + 900) - *(float *)(model + 0xe0)) < FLOAT_80616d34) ||
                    ((*(unsigned int *)(DAT_80611d74 + 0xc) & 0x20) == 0)) {
                    refresh = true;
                }
                float vol = refresh ? FLOAT_80616cec : FLOAT_80616ccc;

                float pos[3];
                object_resolve_sound_position(param_11, pos, 0);
                sound_update_3d_source((double)(float)((double)FLOAT_80611718 * (double)vol),
                                       (double)FLOAT_80616cdc, (double)FLOAT_80616ccc,
                                       model + 0x22c, (unsigned int *)pos, (float *)(intptr_t)0xc8c, 0);
                *(unsigned int *)(model + 0xe0) = *(unsigned int *)(DAT_80611d54 + 900);
            }
            return 1;
        }
        if ((param_9 < 1) && (-1 < param_9)) {
            sound_clear_bank_ref(0xc84);
            sound_clear_bank_ref(0xc8c);
            sprite_load_texture_set();
            sprite_load_texture_set();
            sprite_load_texture_set();
        }
    } else if (param_9 < 0x12) {
        int frame = *(int *)(param_10 + 0xe8);
        if (frame == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616dc0, 0xcbb, param_11, 1);
        } else if ((frame < 0x15) && (frame == 0xc)) {
            object_play_sound_pitched((double)FLOAT_80616dbc, 0xc84, param_11, 1);
        }
    }
    return 0;
}

// ===========================================================================
// batch_560: continuation of the per-weapon-type object_type_dispatch handlers
// (address range 0x801b96e0 - 0x801ba064). Adds a couple of looping-hum
// weapons (op0x15 -> object_weapon_hum_sound_tick), a rng-alternated impact
// weapon, and a LARGE charging beam weapon (object_weapon_charge_dispatch +
// its per-frame 3D charge-loop-sound helper object_weapon_charge_sound_tick).
// Same non-matching conventions as above.
// ===========================================================================

// ===========================================================================
// Address: 0x801b96e0 | Name: object_weapon_dispatch_96e0 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload banks 0xc81/0xc84 + 2 texture sets;
// op0x10 report anim slot 6; op0x11 fire sounds (0xcbb frame 0x15 / 0xc84
// frame 0xc).
// ===========================================================================
unsigned int object_weapon_dispatch_96e0(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    if (param_9 == 0x10) {
        *param_12 = 6;
        return 1;
    }
    if (param_9 < 0x10) {
        if (param_9 == 0) {
            sound_clear_bank_ref(0xc81);
            sound_clear_bank_ref(0xc84);
            sprite_load_texture_set();
            sprite_load_texture_set();
            return 0;
        }
    } else if (param_9 < 0x12) {
        int frame = *(int *)(param_10 + 0xe8);
        if (frame == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616dc0, 0xcbb, param_11, 1);
        } else if ((frame < 0x15) && (frame == 0xc)) {
            object_play_sound_pitched((double)FLOAT_80616dbc, 0xc84, param_11, 1);
        }
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b97a4 | Name: object_weapon_hum_dispatch_97a4 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a looping-hum weapon: op0 preload banks
// 0xc62/0xc63; op0x10 report anim slot 6; op0x11 fire 0xcbb frame 0x15; op0x15
// per-frame hum-sound tick (object_weapon_hum_sound_tick, loop 0xc62/0xc63).
// ===========================================================================
unsigned int object_weapon_hum_dispatch_97a4(int param_1, int param_2, int param_3, unsigned int *param_4)
{
    if (param_1 == 0x11) {
        if (*(int *)(param_2 + 0xe8) == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616d88, 0xcbb, param_3, 1);
        }
    } else if (param_1 < 0x11) {
        if (param_1 == 0) {
            sound_clear_bank_ref(0xc63);
            sound_clear_bank_ref(0xc62);
        } else if ((-1 < param_1) && (0xf < param_1)) {
            *param_4 = 6;
            return 1;
        }
    } else if (param_1 == 0x15) {
        object_weapon_hum_sound_tick(param_3, 0xc62, 0xc63, *(int *)(param_3 + 0xe8));
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b9858 | Name: object_weapon_dispatch_9858 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload banks 0xca3/0xd58/0xd59/0xd5a;
// op4 report the loop bank (0xca3); op0x11 fire sound on frame 0x15 with an
// rng-alternated impact bank (0xca4 or 0xca5).
// ===========================================================================
unsigned int object_weapon_dispatch_9858(int param_1, int param_2, int param_3, unsigned int *param_4)
{
    if (param_1 == 4) {
        *param_4 = 0xca3;
    } else if (param_1 < 4) {
        if (param_1 == 0) {
            sound_clear_bank_ref(0xca3);
            sound_clear_bank_ref(0xd58);
            sound_clear_bank_ref(0xd59);
            sound_clear_bank_ref(0xd5a);
        }
    } else if ((param_1 == 0x11) && (*(int *)(param_2 + 0xe8) == 0x15)) {
        unsigned int r = __ow_rng_tick();
        object_play_sound_at((r & 1) + 0xca4, param_3, 1);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b9904 | Name: object_weapon_hum_dispatch_9904 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a looping-hum weapon: op0 preload banks
// 0xcb2/0xca9/0xcaa; op4 ready; op0x11 anim-event fire sounds (0xcb2 on frames
// 0x13-0x14 when param_4 set, 0xcbb on frames 0x14-0x15); op0x15 per-frame hum
// tick (loop 0xca9/0xcaa).
// ===========================================================================
unsigned int object_weapon_hum_dispatch_9904(int param_1, int param_2, int param_3, int param_4)
{
    switch (param_1) {
    case 0:
        sound_clear_bank_ref(0xcb2);
        sound_clear_bank_ref(0xca9);
        sound_clear_bank_ref(0xcaa);
        return 0;
    case 4:
        return 1;
    case 0x11: {
        int frame = *(int *)(param_2 + 0xe8);
        if (frame != 0x14) {
            if (frame < 0x14) {
                if ((0x12 < frame) && (param_4 != 0)) {
                    object_play_sound_pitched((double)FLOAT_80616d0c, 0xcb2, param_3, 1);
                }
            } else if (frame < 0x16) {
                object_play_sound_pitched((double)FLOAT_80616cf4, 0xcbb, param_3, 1);
            }
        }
        break;
    }
    case 0x15:
        object_weapon_hum_sound_tick(param_3, 0xca9, 0xcaa, *(int *)(param_3 + 0xe8));
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b99e4 | Name: object_weapon_charge_sound_tick | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Per-frame 3D charge-loop sound driver for the charging beam weapon
// (object_weapon_charge_dispatch op0x13). Accumulates a charge energy level in
// field +0xe0 (decay *FLOAT_80616cfc + ramp), floors it, and drives the 3D
// loop source 0xc94 with a sqrt-scaled volume + squared pitch. During the
// charge anim frames (4..5) the ramp target is 1.0, otherwise FLOAT_80616dc4.
// ===========================================================================
void object_weapon_charge_sound_tick(int object)
{
    float rampTarget = FLOAT_80616dc4;
    int model = *(int *)(object + 0x54);
    if (model != 0) {
        float pos[4];
        object_resolve_sound_position(*(int *)(object + 0x354), pos, 0);
        float floor = FLOAT_80616dcc;
        if ((*(int *)(object + 0xe8) < 6) && (3 < *(int *)(object + 0xe8))) {
            rampTarget = FLOAT_80616ccc;
        }
        double rampRate = (double)FLOAT_80616dc8;
        *(float *)(object + 0xe0) = *(float *)(object + 0xe0) * FLOAT_80616cfc;
        *(float *)(object + 0xe0) =
            (float)(rampRate * (double)rampTarget + (double)*(float *)(object + 0xe0));
        if (*(float *)(object + 0xe0) <= floor) {
            *(float *)(object + 0xe0) = floor;
        }
        double energy = (double)*(float *)(object + 0xe0);
        double amp = math_sqrt(energy);
        sound_update_3d_source(
            (double)(FLOAT_80611718 * (float)(amp - (double)FLOAT_80616dd0)),
            (double)FLOAT_80616cdc,
            (double)(float)(energy * energy + (double)FLOAT_80616dd4),
            model + 0x22c, (unsigned int *)pos, (float *)(intptr_t)0xc94, 1);
    }
}

// ===========================================================================
// Address: 0x801b9ad0 | Name: object_weapon_charge_dispatch | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for the primary CHARGING BEAM weapon. Cases:
// 0 preload banks 0xcb3/0xc94 + texture set; 1 per-frame charge machine (while
// firing, recolor + restart the linked object's cached particle emitters via
// the DAT_80610b98 id list and ramp charge field +0x108 toward its ceiling,
// consuming ammo at charge thresholds; while not firing, disable those
// emitters and zero the charge; then advance a jittered secondary charge
// field +0xc4 [field index 0x31]); 4 reset field 0x38; 7 fire: resolve muzzle
// point (object_get_ejection_point) and emit the beam blast
// (weather_particle_spawn_type4) scaled by charge, reset frame; 0x11 fire
// sound 0xcbb on charge-stage 2; 0x13 loop start/stop 0xcb3 by charge time +
// per-frame object_weapon_charge_sound_tick.
// The register-pair spill frame (FUN_803826ec/__save_gpr_28) hands over the
// object pointer (lo) and op (hi); the object also aliases a float-view (pfVar2
// indices are *4-byte field offsets: [0x15]=+0x54 model, [0x30]=+0xc0 charge
// stage, [0x31]=+0xc4 jitter, [0x37]=+0xdc, [0x38]=+0xe0, [0x3a]=+0xe8 frame,
// [0x3b]=+0xec).
// ===========================================================================
void object_weapon_charge_dispatch(double param_1, double param_2, double param_3, double param_4,
                                   double param_5, double param_6, double param_7, double param_8,
                                   int param_9, int param_10, int object_arg)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;
    (void)param_9; (void)param_10;

    unsigned long long packed = __save_gpr_28();
    int object = (int)packed;
    int op     = (int)(packed >> 0x20);

    switch (op) {
    case 0:
        sound_clear_bank_ref(0xcb3);
        sound_clear_bank_ref(0xc94);
        sprite_load_texture_set();
        break;

    case 1: {
        int localPlayer = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
        int *idList = &DAT_80610b98;
        if ((*(float *)(object_arg + 0x104) <= FLOAT_80616cec) ||
            (*(float *)(object + 0xac) != 0.0)) {
            // not firing / released: disable the linked cached emitters + clear charge
            for (int i = 0; ; i++) {
                int model = *(int *)(object + 0x54);
                if (model == 0) break;
                int emitterId = idList[i];
                if (emitterId < 0) break;
                int *slot;
                if (emitterId < *(int *)(model + 0x1e8)) {
                    slot = (int *)(*(int *)(model + 0x1fc) + emitterId * 4);
                } else {
                    slot = &DAT_80611e2c;
                }
                int emitter = *slot;
                particle_cached_disable((ParticleEmitter **)&emitter);
            }
            *(float *)(object_arg + 0x108) = FLOAT_80616cec;
        } else {
            // firing: recolor + restart emitters, ramp charge, consume ammo at thresholds
            for (int i = 0; ; i++) {
                int model = *(int *)(object + 0x54);
                if (model == 0) break;
                int emitterId = idList[i];
                if (emitterId < 0) break;
                int *slot;
                if (emitterId < *(int *)(model + 0x1e8)) {
                    slot = (int *)(*(int *)(model + 0x1fc) + emitterId * 4);
                } else {
                    slot = &DAT_80611e2c;
                }
                int emitter = *slot;
                if (*(char *)(emitter + 0x12) == '\0') {
                    particle_emitter_set_single_color_key((ParticleEmitter *)emitter,
                                                          *(int *)(DAT_80611d74 + 4) + 0xca);
                    int flagsArg = emitter;
                    particle_emitter_set_flags((unsigned int)flagsArg, 0x10000, 0);
                    int restartArg = emitter;
                    particle_cached_restart((ParticleEmitter **)&restartArg);
                }
            }
            float charge = FLOAT_80616d60 * *(float *)(object_arg + 0x104);
            if (FLOAT_80616ccc < charge) {
                charge = FLOAT_80616ccc;
            }
            *(float *)(object_arg + 0x108) = charge;
            if (((*(float *)(object + 0xc0) == 0.0) && (FLOAT_80616dbc < *(float *)(object_arg + 0x108))) ||
                ((*(int *)(object + 0xc0) == 1) && (FLOAT_80616d30 < *(float *)(object_arg + 0x108)))) {
                int descBase = DAT_80610ba0 + *(int *)(object_arg + 0x30) * 0x29c;
                object_ammo_consume_dispatch(localPlayer, object_arg, object,
                                             *(int *)(descBase + 0x14), 1, *(int *)(descBase + 0x18));
                *(int *)(object + 0xc0) = *(int *)(object + 0xc0) + 1;
            }
        }
        // advance jittered secondary charge field +0xc4
        if (FLOAT_80616cec < *(float *)(object_arg + 0x108)) {
            unsigned int r1 = __ow_rng_tick();
            float jitter = *(float *)(object + 0xc4);
            float scaled = *(float *)(object_arg + 0x108) *
                ((float)((double)r1 - DOUBLE_80616de0) / FLOAT_80616ddc);
            if (jitter <= scaled) {
                unsigned int r2 = __ow_rng_tick();
                jitter = *(float *)(object_arg + 0x108) *
                    ((float)((double)r2 - DOUBLE_80616de0) / FLOAT_80616ddc);
            }
            *(float *)(object + 0xc4) = jitter;
        }
        break;
    }

    case 4:
        *(float *)(object + 0xe0) = FLOAT_80616cec;
        break;

    case 7: {
        int localPlayer = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
        float pt[4];
        object_get_ejection_point((unsigned int)DAT_80611d74, (unsigned int)(intptr_t)pt, 0);
        weather_particle_spawn_type4((double)*(float *)(object_arg + 0x108),
                                     *(int *)(DAT_80611d74 + 4), pt,
                                     (float *)(localPlayer + 0xb7c));
        *(int *)(object_arg + 0xf0) = 0;
        break;
    }

    case 0x11:
        if (*(int *)(object + 0xe8) == 2) {
            object_play_sound_pitched((double)FLOAT_80616dbc, 0xcbb, object_arg, 1);
        }
        break;

    case 0x13:
        if (*(int *)(object + 0xe8) == 1) {
            if (FLOAT_80616dd8 <= *(float *)(object + 0xec)) {
                if (*(float *)(object + 0xdc) != 0.0) {
                    *(float *)(object + 0xdc) = 0.0;
                    object_play_sound_at(0xcb3, object_arg, 1);
                }
            } else {
                *(int *)(object + 0xdc) = 1;
            }
        }
        object_weapon_charge_sound_tick(object);
    }

    __restore_gpr_28();
}

// ===========================================================================
// Address: 0x801b9e50 | Name: object_weapon_dispatch_9e50 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload bank 0xc7b; op3 ready; op0x10
// report anim slot 6; op0x11 fire 0xcbb on frame 0x15 (pitch depends on trigger
// state +0x40); op0x17 register loop fade-out.
// ===========================================================================
unsigned int object_weapon_dispatch_9e50(int param_1, int param_2, int param_3, unsigned int *param_4)
{
    switch (param_1) {
    case 0:
        sound_clear_bank_ref(0xc7b);
        return 0;
    case 3:
        return 1;
    case 0x10:
        *param_4 = 6;
        return 1;
    case 0x11:
        if (*(int *)(param_2 + 0xe8) == 0x15) {
            if (*(int *)(param_2 + 0x40) == 0) {
                object_play_sound_pitched((double)FLOAT_80616d88, 0xcbb, param_3, 1);
            } else {
                object_play_sound_pitched((double)FLOAT_80616dbc, 0xcbb, param_3, 1);
            }
        }
        break;
    case 0x17:
        sound_register_fade_out((double)FLOAT_80616ccc, (double)FLOAT_80616d58,
                                (int *)(param_3 + 0x100), (unsigned int *)0);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b9f1c | Name: object_weapon_spin_dispatch_9f1c | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a spin-loop weapon: op0 preload banks
// 0xc5c/0xc5d; op1 per-frame spin-level integrate (+0xdc decays *FLOAT_80616d30
// then adds FLOAT_80616d88 * +0xe0; resets +0xe0=1.0) driving loop pitch via
// sound_set_pitch; op3 reset spin levels; op9 bias the player emitter field
// [0xd]; op0x10 report anim slot 6; op0x11 fire 0xcbb frame 0x15; op0x15
// per-frame hum tick (loop 0xc5c/0xc5d).
// ===========================================================================
unsigned int object_weapon_spin_dispatch_9f1c(int param_1, int param_2, int param_3, unsigned int *param_4)
{
    float spinAdd  = FLOAT_80616d88;
    float spinFull = FLOAT_80616ccc;

    switch (param_1) {
    case 0:
        sound_clear_bank_ref(0xc5c);
        sound_clear_bank_ref(0xc5d);
        break;
    case 1: {
        *(float *)(param_2 + 0xdc) = *(float *)(param_2 + 0xdc) * FLOAT_80616d30;
        *(float *)(param_2 + 0xdc) = spinAdd * *(float *)(param_2 + 0xe0) + *(float *)(param_2 + 0xdc);
        *(float *)(param_2 + 0xe0) = spinFull;
        unsigned int handle = *(unsigned int *)(*(int *)(param_3 + 0xe8) + 0x228);
        if (-1 < (int)handle) {
            sound_set_pitch((double)*(float *)(param_2 + 0xdc), handle);
        }
        return 0;
    }
    case 3:
        *(float *)(param_2 + 0xdc) = FLOAT_80616ccc;
        *(float *)(param_2 + 0xe0) = spinFull;
        return 0;
    case 9: {
        unsigned int *e = (unsigned int *)particle_get_current_player_emitter();
        e[0xd] = (unsigned int)(int)((float)((double)(int)e[0xd] - DOUBLE_80616ce0) + FLOAT_80616d1c);
        return 1;
    }
    case 0x10:
        *param_4 = 6;
        return 1;
    case 0x11:
        if (*(int *)(param_2 + 0xe8) == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616de8, 0xcbb, param_3, 1);
        }
        break;
    case 0x15:
        object_weapon_hum_sound_tick(param_3, 0xc5d, 0xc5c, *(int *)(param_3 + 0xe8));
    }
    return 0;
}

// ===========================================================================
// Address: 0x801ba064 | Name: object_weapon_dispatch_a064 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload bank 0xcb7 + texture set; op0x10
// report anim slot 6; op0x11 fire sounds (0xcbb frame 0x15 / 0xcb7 frame 0xc);
// op0x17 register loop fade-out.
// ===========================================================================
unsigned int object_weapon_dispatch_a064(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;
    (void)param_10;

    if (param_9 == 0x11) {
        int frame = *(int *)(param_10 + 0xe8);
        if (frame == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616dec, 0xcbb, param_11, 1);
        } else if ((frame < 0x15) && (frame == 0xc)) {
            object_play_sound_pitched((double)FLOAT_80616ce8, 0xcb7, param_11, 1);
        }
    } else if (param_9 < 0x11) {
        if (param_9 == 0) {
            sound_clear_bank_ref(0xcb7);
            sprite_load_texture_set();
        } else if ((-1 < param_9) && (0xf < param_9)) {
            *param_12 = 6;
            return 1;
        }
    } else if (param_9 == 0x17) {
        sound_register_fade_out((double)FLOAT_80616ccc, (double)FLOAT_80616d58,
                                (int *)(param_11 + 0x100), (unsigned int *)0);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801ba138 | Name: object_weapon_heathaze_dispatch_a138 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a heat-haze weapon: op0 preload bank 0xc87;
// op9 bias two player emitter fields ([0xd] and [0xb]); op0xd set up the
// heat-haze render pass (mode 3); op0xe release it; op0x10 report anim slot 5;
// op0x11 fire sounds (0xcbb frame 0x15 with pitch depending on trigger state
// +0x40 / whether this is the active-ctx param block +0xa4; 0xc87 frame 9).
// ===========================================================================
unsigned int object_weapon_heathaze_dispatch_a138(int param_1, int param_2, int param_3, unsigned int *param_4)
{
    unsigned int result;
    switch (param_1) {
    case 0:
        sound_clear_bank_ref(0xc87);
    default:
        result = 0;
        break;
    case 9: {
        unsigned int *e = (unsigned int *)particle_get_current_player_emitter();
        e[0xd] = (unsigned int)(int)((float)((double)(int)e[0xd] - DOUBLE_80616ce0) + FLOAT_80616d6c);
        e = (unsigned int *)particle_get_current_player_emitter();
        e[0xb] = (unsigned int)(int)((float)((double)(int)e[0xb] - DOUBLE_80616ce0) + FLOAT_80616d1c);
        result = 1;
        break;
    }
    case 0xd:
        if (*(int *)(param_2 + 0x54) != 0) {
            object_weapon_heathaze_setup(param_2, *(int *)(param_2 + 0x54), 3);
        }
        result = 1;
        break;
    case 0xe:
        object_weapon_heathaze_release();
        result = 1;
        break;
    case 0x10:
        *param_4 = 5;
        result = 1;
        break;
    case 0x11: {
        int frame = *(int *)(param_2 + 0xe8);
        if (frame == 0x15) {
            if (*(int *)(param_2 + 0x40) == 0) {
                object_play_sound_pitched((double)FLOAT_80616cf4, 0xcbb, param_3, 1);
            } else if (*(int *)(param_3 + 0xec) + 0xa4 == param_3) {
                object_play_sound_pitched((double)FLOAT_80616d58, 0xcbb, param_3, 1);
            }
        } else if ((frame < 0x15) && (frame == 9)) {
            object_play_sound_pitched((double)FLOAT_80616dd4, 0xc87, param_3, 1);
        }
        result = 1;
    }
    }
    return result;
}

// ===========================================================================
// batch_561: last group of the per-weapon-type object_type_dispatch handlers
// (address range 0x801ba2cc - 0x801ba8cc). These are DEPLOYABLE / unfoldable
// weapons: anim events 0xb/0x19/0x1a drive object_render_queue_push toggles of
// deploy/fold render-flag groups, and set a linked-object mode word (+0x30 of
// *(*(ctx+0xe8)+4)). Same non-matching conventions as above.
// ===========================================================================

// ===========================================================================
// Address: 0x801ba2cc | Name: object_weapon_deploy_dispatch_a2cc | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a deployable weapon: op0 preload banks
// 0xc7c-0xc80 + texture set; op3 apply deploy render-flags 5/6/7 by trigger
// state +0x38 and mirror mode into the linked object; op4 ready; op0x11 anim
// events (deploy start 0xb -> queue flag7, fire sounds 0xc80/0xc7c/0xcbb/0xc7e/
// 0xc7f, unfold 0x19 -> queue flags 5/6 + linked mode 2); op0x12 fold events
// (0xb -> clear flag7, 0x1a -> clear 5/6 + linked mode 0).
// ===========================================================================
unsigned int object_weapon_deploy_dispatch_a2cc(double param_1, double param_2, double param_3, double param_4,
                                                double param_5, double param_6, double param_7, double param_8,
                                                unsigned int param_9, int param_10, int param_11)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    switch (param_9) {
    case 0:
        sound_clear_bank_ref(0xc7e);
        sound_clear_bank_ref(0xc7f);
        sound_clear_bank_ref(0xc7d);
        sound_clear_bank_ref(0xc7c);
        sound_clear_bank_ref(0xc80);
        sprite_load_texture_set();
        return 0;
    case 3:
        object_apply_render_flag(param_10, 7, 0);
        object_apply_render_flag(param_10, 5, *(int *)(param_10 + 0x38));
        object_apply_render_flag(param_10, 6, *(int *)(param_10 + 0x38));
        if ((param_11 != 0) && (*(int *)(param_11 + 0xe8) != 0)) {
            *(unsigned int *)(*(int *)(*(int *)(param_11 + 0xe8) + 4) + 0x30) =
                -(unsigned int)(*(int *)(param_10 + 0x38) != 0) & 2;
        }
        return 1;
    case 4:
        return 1;
    case 0x11:
        switch (*(int *)(param_10 + 0xe8)) {
        case 0xc:
            object_play_sound_at(0xc80, param_11, 1);
            break;
        case 0x13:
            object_play_sound_at(0xc7c, param_11, 0);
            break;
        case 0x15:
            object_play_sound_pitched((double)FLOAT_80616df0, 0xcbb, param_11, 1);
            break;
        case 0x19:
            object_play_sound_at(0xc7e, param_11, 1);
            break;
        case 0x1a:
            object_play_sound_at(0xc7f, param_11, 1);
        }
        if ((*(int *)(param_10 + 0xe8) == 0xb) && (*(int *)(param_10 + 0x38) == 1)) {
            object_render_queue_push(param_10, 7, 1);
            return 1;
        }
        if (*(int *)(param_10 + 0xe8) == 0x19) {
            object_render_queue_push(param_10, 5, 1);
            object_render_queue_push(param_10, 6, 1);
            if ((param_11 != 0) && (*(int *)(param_11 + 0xe8) != 0)) {
                *(unsigned int *)(*(int *)(*(int *)(param_11 + 0xe8) + 4) + 0x30) = 2;
            }
            return 1;
        }
        break;
    case 0x12:
        if (*(int *)(param_10 + 0xe8) == 0xb) {
            object_render_queue_push(param_10, 7, 0);
            return 1;
        }
        if (*(int *)(param_10 + 0xe8) == 0x1a) {
            object_render_queue_push(param_10, 5, 0);
            object_render_queue_push(param_10, 6, 0);
            if ((param_11 != 0) && (*(int *)(param_11 + 0xe8) != 0)) {
                *(unsigned int *)(*(int *)(*(int *)(param_11 + 0xe8) + 4) + 0x30) = 0;
            }
            return 1;
        }
    }
    return 0;
}

// ===========================================================================
// Address: 0x801ba54c | Name: object_weapon_dispatch_a54c | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: op0 preload banks 0xca7/0xca8; op0x11 fire
// sounds by anim frame (0xcbb frame 0x15 / 0xca7 frame 1 / 0xca8 frame 0x23).
// ===========================================================================
unsigned int object_weapon_dispatch_a54c(int param_1, int param_2, int param_3)
{
    if (param_1 == 0x11) {
        int frame = *(int *)(param_2 + 0xe8);
        if (frame == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616cd0, 0xcbb, param_3, 1);
        } else if (frame < 0x15) {
            if (frame == 1) {
                object_play_sound_pitched((double)FLOAT_80616df8, 0xca7, param_3, 1);
            }
        } else if (frame == 0x23) {
            object_play_sound_pitched((double)FLOAT_80616df4, 0xca8, param_3, 1);
        }
    } else if ((param_1 < 0x11) && (param_1 == 0)) {
        sound_clear_bank_ref(0xca7);
        sound_clear_bank_ref(0xca8);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801ba608 | Name: object_weapon_deploy_dispatch_a608 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a second deployable weapon: op0 preload
// banks 0xc90/0xc85/0xc86; op3 apply deploy render-flags 2/3 by trigger state
// + linked mode; op4 report readiness (CLZ of trigger +0x40); op0x11 anim
// events (0xb deploy -> queue flag3, 0xc fire 0xc86, 0x13 dry 0xc85, 0x15 fire
// 0xcbb, 0x19 unfold 0xc90 + queue flag2 + linked mode 2, 0x1a fold sound
// 0xc90); op0x12 fold (0xb -> clear flag3, 0x1a -> clear flag2 + linked mode 0).
// ===========================================================================
unsigned int object_weapon_deploy_dispatch_a608(unsigned int param_1, int param_2, int param_3, int param_4)
{
    switch (param_1) {
    case 0:
        sound_clear_bank_ref(0xc90);
        sound_clear_bank_ref(0xc85);
        sound_clear_bank_ref(0xc86);
        return 0;
    case 3:
        object_apply_render_flag(param_2, 3, 0);
        object_apply_render_flag(param_2, 2, *(int *)(param_2 + 0x38));
        if ((param_3 != 0) && (*(int *)(param_3 + 0xe8) != 0)) {
            *(unsigned int *)(*(int *)(*(int *)(param_3 + 0xe8) + 4) + 0x30) =
                -(unsigned int)(*(int *)(param_2 + 0x38) != 0) & 2;
        }
        return 1;
    case 4:
        return countLeadingZeros(*(unsigned int *)(param_2 + 0x40)) >> 5;
    case 0x11:
        switch (*(int *)(param_2 + 0xe8)) {
        case 0xb:
            if (*(int *)(param_2 + 0x38) == 1) {
                object_render_queue_push(param_2, 3, 1);
                return 1;
            }
            break;
        case 0xc:
            object_play_sound_at(0xc86, param_3, 1);
            break;
        case 0x13:
            if ((param_4 != 0) && (*(int *)(param_2 + 0x40) == 0)) {
                object_play_sound_at(0xc85, param_3, 0);
            }
            break;
        case 0x15:
            if (*(int *)(param_2 + 0x40) == 0) {
                object_play_sound_pitched((double)FLOAT_80616dfc, 0xcbb, param_3, 1);
            } else {
                object_play_sound_pitched((double)FLOAT_80616da0, 0xcbb, param_3, 1);
            }
            break;
        case 0x19:
            object_play_sound_at(0xc90, param_3, 1);
            object_render_queue_push(param_2, 2, 1);
            if ((param_3 != 0) && (*(int *)(param_3 + 0xe8) != 0)) {
                *(unsigned int *)(*(int *)(*(int *)(param_3 + 0xe8) + 4) + 0x30) = 2;
            }
            return 1;
        case 0x1a:
            object_play_sound_at(0xc90, param_3, 1);
        }
        break;
    case 0x12:
        if (*(int *)(param_2 + 0xe8) == 0xb) {
            object_render_queue_push(param_2, 3, 0);
            return 1;
        }
        if (*(int *)(param_2 + 0xe8) == 0x1a) {
            object_apply_render_flag(param_2, 2, 0);
            if ((param_3 != 0) && (*(int *)(param_3 + 0xe8) != 0)) {
                *(unsigned int *)(*(int *)(*(int *)(param_3 + 0xe8) + 4) + 0x30) = 0;
            }
            return 1;
        }
    }
    return 0;
}

// ===========================================================================
// Address: 0x801ba86c | Name: object_weapon_set_deploy_flags | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Set the three deploy render-flags (7/8/9) of an object to the same value.
// Helper for object_weapon_deploy_dispatch_a8cc.
// ===========================================================================
void object_weapon_set_deploy_flags(int object, int value)
{
    object_apply_render_flag(object, 7, value);
    object_apply_render_flag(object, 8, value);
    object_apply_render_flag(object, 9, value);
}

// ===========================================================================
// Address: 0x801ba8cc | Name: object_weapon_deploy_dispatch_a8cc | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a third deployable weapon (tripod/turret):
// op0 preload banks 0xc8f/0xc83 + entity_preload_resources(0x743) + texture;
// op3 set deploy flags 7/8/9 by trigger state; op0x11 anim event sounds (0xc83
// frame 0xc / 0xcbb frame 0x15 / 0xc8f frames 0x19-0x1a); op0x12 deactivate on
// fold-complete frames 7/0x1a; op0x13 per-frame deploy-flag toggle by anim
// frame progress vs FLOAT_80616d78/cc8; op0x17 register loop fade-out.
// ===========================================================================
unsigned int object_weapon_deploy_dispatch_a8cc(double param_1, double param_2, double param_3, double param_4,
                                                double param_5, double param_6, double param_7, double param_8,
                                                unsigned int param_9, int param_10, int param_11)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    switch (param_9) {
    case 0:
        sound_clear_bank_ref(0xc8f);
        sound_clear_bank_ref(0xc83);
        entity_preload_resources(0x743);
        sprite_load_texture_set();
        return 0;
    case 3:
        object_weapon_set_deploy_flags(param_10, *(int *)(param_10 + 0x38));
        return 1;
    case 0x11: {
        int frame = *(int *)(param_10 + 0xe8);
        if (frame == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616d58, 0xcbb, param_11, 1);
        } else if (frame < 0x15) {
            if (frame == 0xc) {
                object_play_sound_at(0xc83, param_11, 1);
            }
        } else if ((frame < 0x1b) && (0x18 < frame)) {
            object_play_sound_at(0xc8f, param_11, 1);
        }
        return 1;
    }
    case 0x12:
        if ((*(int *)(param_10 + 0xe8) == 7) || (*(int *)(param_10 + 0xe8) == 0x1a)) {
            object_set_active(param_10, 0, 0);
        }
        break;
    case 0x13: {
        int frame = *(int *)(param_10 + 0xe8);
        if ((frame != 0x19) || (*(float *)(param_10 + 0xec) <= FLOAT_80616d78)) {
            if ((frame != 0x1a) || (*(float *)(param_10 + 0xec) <= FLOAT_80616cc8)) {
                if ((frame == 7) && (FLOAT_80616d78 < *(float *)(param_10 + 0xec))) {
                    object_weapon_set_deploy_flags(param_10, 0);
                }
            } else {
                object_weapon_set_deploy_flags(param_10, 0);
            }
        } else {
            object_weapon_set_deploy_flags(param_10, 1);
        }
        break;
    }
    case 0x17:
        sound_register_fade_out((double)FLOAT_80616ccc, (double)FLOAT_80616d58,
                                (int *)(param_11 + 0x100), (unsigned int *)0);
    }
    return 0;
}
