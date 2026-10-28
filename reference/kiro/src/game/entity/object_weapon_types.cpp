// object_weapon_types.cpp
// Per-weapon-type behaviour for the player object: the type-dispatch handlers
// (object_type_dispatch op ids) that implement individual weapons' fire/preload/
// anim-event sounds and special effects, plus the shared helpers they use --
// weapon-tuning parameter table lookups, the heat-haze render-target setup/
// release, a looping hum/overheat sound tick, and the minigun spin-up/heat
// simulation. Continuation of the object weapon module after
// object_weapon_ammo.cpp (batch_556).
//
// Address range: 0x801b6e68 - 0x801b79cc
// Batch: batch_557
//
// Non-matching translation: the 0x43300000 int->double bias idiom (Ghidra's
// int->double conversion) is reduced to ordinary int/float math, and the
// paired-single f-reg spill temporaries are dropped. Raw field-offset pointer
// arithmetic on the object / render / weapon structures is preserved (as
// elsewhere in the entity module) since those layouts are not yet typed. The
// incoming f-register values that the decompiler surfaced as `in_f31` etc. on
// the dispatch handlers are the object_type_dispatch double args; they are
// carried as explicit `double` parameters here.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h -> object_*/sound_*/render_target_*/mtx4_* helpers

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Render/compute spill-frame bracketing (paired push/pop).
extern unsigned long long FUN_803826f0(void);  // spill push: returns (secondary<<32)|object
extern void               FUN_8038273c(void);   // spill pop (matches FUN_803826f0)
extern unsigned long long FUN_803826e8(void);   // spill push variant: returns (secondary<<32)|object
extern void               FUN_80382734(void);   // spill pop (matches FUN_803826e8)

// ---- Object / render globals ----
extern int   DAT_80611d74;   // local player context base
extern int   DAT_80611748;   // camera / viewport transform block base
extern int   DAT_806108e4;   // previous-frame timestamp
extern int   DAT_806109c0;   // frame delta denominator source
extern char  DAT_805015bd;   // difficulty / rate-table selector byte
extern int   DAT_80612610;   // active-emitter / split-screen count
extern int   DAT_805c1a18[]; // per-view heat-haze render-target slot table (int, -1 = unset)

// ---- Weapon-tuning parameter tables (float, stride 0x3c bytes = per weapon type) ----
extern float DAT_8043e25c[];
extern float DAT_8043e260[];

// ---- fp bias / tuning constants ----
extern double DOUBLE_80616ba0; // int->double bias (0x43300000 magic)
extern double DOUBLE_80616ce0; // int->double bias variant
extern float  FLOAT_806108ec;  // frame-time scale
extern float  FLOAT_80611718;  // world/units scale for sound range

extern float  FLOAT_80616b98;  // 0.0f
extern float  FLOAT_80616cc0;  // default param (e260 table)
extern float  FLOAT_80616cc8;  // hum ramp rate A
extern float  FLOAT_80616ccc;  // 1.0f-ish clamp / reference
extern float  FLOAT_80616cd0;  // hum ramp denom A
extern float  FLOAT_80616cd4;  // hum target A
extern float  FLOAT_80616cd8;  // hum ramp denom B / pitch base
extern float  FLOAT_80616cdc;  // sound pitch constant
extern float  FLOAT_80616ce8;  // fade-out target volume
extern float  FLOAT_80616cec;  // 0.0f
extern float  FLOAT_80616cf0;  // spin-up impulse
extern float  FLOAT_80616cf4;  // spin decay
extern float  FLOAT_80616cf8;  // spin target
extern float  FLOAT_80616cfc;  // spin friction
extern float  FLOAT_80616d00;  // min spin
extern float  FLOAT_80616d04;  // heat ceiling
extern float  FLOAT_80616d08;  // heat ramp A
extern float  FLOAT_80616d0c;  // heat ramp B / pitch alt
extern float  FLOAT_80616d10;  // heat ramp C
extern float  FLOAT_80616d14;  // spin max
extern float  FLOAT_80616d18;  // barrel-angle wrap
extern float  FLOAT_80616d1c;  // pitch scale
extern float  FLOAT_80616d20;  // trigger ramp
extern float  FLOAT_80616d24;  // barrel spin -> rotation scale
extern float  FLOAT_80616d28;  // barrel spin -> rotation denom
extern float  FLOAT_80616d2c;  // barrel translate scale
extern float  FLOAT_80616d30;  // charge threshold
extern float  FLOAT_80616d34;  // charge add
extern float  FLOAT_80616d38;  // charge ramp A0
extern float  FLOAT_80616d3c;  // charge ramp A1 / B0
extern float  FLOAT_80616d40;  // charge ramp B1
extern float  FLOAT_80616d44;  // sound vol (case 0x11)
extern float  FLOAT_80616d48;  // sound vol
extern float  FLOAT_80616d4c;  // sound vol
extern float  FLOAT_80616d50;  // sound vol
extern float  FLOAT_80616d54;  // sound vol
} // extern "C"

// ===========================================================================
// Address: 0x801b6e68 | Name: object_weapon_anim_idle_gate | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Returns 1 when the object is NOT in any reload/fire animation frame
// (frames 9, 0xe, 0x13, 0x12, 0x21, 0x14, 0x22 all inactive) -- i.e. the weapon
// is idle and free to accept a new action. 0 otherwise.
// ===========================================================================
unsigned int object_weapon_anim_idle_gate(int object)
{
    if (object_anim_frame_active_no_pending(object, 9) != 0)    return 0;
    if (object_anim_frame_active_no_pending(object, 0xe) != 0)  return 0;
    if (object_anim_frame_active_no_pending(object, 0x13) != 0) return 0;
    if (object_anim_frame_active_no_pending(object, 0x12) != 0) return 0;
    if (object_anim_frame_active_no_pending(object, 0x21) != 0) return 0;
    if (object_anim_frame_active_no_pending(object, 0x14) != 0) return 0;
    if (object_anim_frame_active_no_pending(object, 0x22) != 0) return 0;
    return 1;
}

// ===========================================================================
// Address: 0x801b6f3c | Name: object_weapon_param_e260 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Weapon-tuning parameter lookup (table DAT_8043e260, stride 0x3c) indexed by
// the object's active weapon type (object +0x94); returns FLOAT_80616cc0 when
// no weapon type is set.
// ===========================================================================
double object_weapon_param_e260(int object)
{
    if (*(int *)(object + 0x94) == 0) {
        return (double)FLOAT_80616cc0;
    }
    return (double)*(float *)((int)&DAT_8043e260 + *(int *)(object + 0x94) * 0x3c);
}

// ===========================================================================
// Address: 0x801b6f68 | Name: object_weapon_param_e25c | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Weapon-tuning parameter lookup (table DAT_8043e25c, stride 0x3c) indexed by
// the object's active weapon type (object +0x94); returns 0.0 when no weapon
// type is set.
// ===========================================================================
double object_weapon_param_e25c(int object)
{
    if (*(int *)(object + 0x94) == 0) {
        return (double)FLOAT_80616b98;
    }
    return (double)*(float *)((int)&DAT_8043e25c + *(int *)(object + 0x94) * 0x3c);
}

// ===========================================================================
// Address: 0x801b6f94 | Name: object_weapon_heathaze_setup | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Allocates and activates the heat-haze scratch render target for the active
// weapon's distortion strip, keyed per local view (DAT_805c1a18[view]). Only
// allocates when the weapon (spill-frame secondary +0x3c == 1) requests it and
// the slot is not yet assigned; the strip height halves in the split-screen
// '\n' difficulty/mode case. Always records the strip entry for the current
// weapon effect.
// ===========================================================================
void object_weapon_heathaze_setup(int param_1, int param_2, int param_3)
{
    unsigned long long packed = FUN_803826f0();
    int object = (int)packed;
    int secondary = (int)(packed >> 0x20);

    (void)param_1;
    (void)param_2;

    int view = *(int *)(DAT_80611d74 + 4);

    if (((object != 0) && (*(int *)(secondary + 0x3c) == 1)) &&
        (DAT_805c1a18[view] == -1)) {
        gx_get_blend_mode();
        gx_get_sub_material_id();
        int stripH = 0x78;
        if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
            stripH = 0x3c;
        }
        render_target_alloc_scratch_wrapper(0xa0, (unsigned int)stripH, 3, 0, &DAT_805c1a18[view]);
        render_target_activate((unsigned int)DAT_805c1a18[view],
                               (unsigned int)(*(int *)(DAT_80611748 + 8) - 0xa0),
                               (unsigned int)(*(int *)(DAT_80611748 + 0x1c) - stripH),
                               0x140, (unsigned int)(stripH << 1), 0x8000);
    }
    if (object != 0) {
        collision_set_strip_entry((unsigned int)*(int *)(object + 4),
                                  (unsigned int)param_3,
                                  (unsigned int)DAT_805c1a18[view]);
    }
    FUN_8038273c();
}

// ===========================================================================
// Address: 0x801b70c0 | Name: object_weapon_heathaze_release | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Releases the heat-haze render-target slot for the local view and resets it to
// unset (-1).
// ===========================================================================
void object_weapon_heathaze_release(void)
{
    int view = *(int *)(DAT_80611d74 + 4);
    if (DAT_805c1a18[view] != -1) {
        render_target_clear_slot(DAT_805c1a18[view]);
        DAT_805c1a18[view] = -1;
    }
}

// ===========================================================================
// Address: 0x801b711c | Name: object_weapon_hum_sound_tick | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Per-frame update of a weapon's looping hum/spin sound: ramps the hum level
// (object +0xe0) toward a target -- faster once the spin has been sustained
// past DAT_806108e4+1 frames -- then updates the 3D looping source, and starts
// the loop the first time (handle +0x228 becomes valid). Emitter position comes
// from object_resolve_sound_position (or a fixed offset if the object has no
// bone emitter).
// ===========================================================================
void object_weapon_hum_sound_tick(int param_1, int param_2, int param_3, int param_4)
{
    unsigned long long packed = FUN_803826e8();
    int owner = (int)(packed >> 0x20);
    int prevHandle = *(int *)(param_4 + 0x228);

    (void)param_1;
    (void)param_2;

    float pos[3];
    if (*(int *)(owner + 0xec) == 0) {
        pos[0] = *(float *)(param_4 + 0x64)  + *(float *)(param_4 + 0x1a8);
        pos[1] = *(float *)(param_4 + 0x68)  + *(float *)(param_4 + 0x1ac);
        pos[2] = *(float *)(param_4 + 0x6c)  + *(float *)(param_4 + 0x1b0);
    } else {
        object_resolve_sound_position(owner, pos, 0);
    }

    float dt = FLOAT_80616ccc / (float)((int)DAT_806109c0);
    float step = (FLOAT_806108ec * dt);
    if (DAT_806108e4 + 1 < *(int *)(owner + 0xf0)) {
        scalar_step_toward_snap((double)FLOAT_80616cd4,
                                (double)(step / FLOAT_80616cd8),
                                (float *)(param_4 + 0xe0));
    } else {
        scalar_step_toward_snap((double)FLOAT_80616cc8,
                                (double)(step / FLOAT_80616cd0),
                                (float *)(param_4 + 0xe0));
    }

    sound_update_3d_source((double)(FLOAT_80611718 * *(float *)(param_4 + 0xe0)),
                           (double)FLOAT_80616cdc, (double)FLOAT_80616ccc,
                           param_4 + 0x228, (unsigned int *)pos, (float *)(intptr_t)param_3, 0);

    if ((-1 < *(int *)(param_4 + 0x228)) && (prevHandle < 0)) {
        int params[6];
        sound_init_play_params((int)params);
        // params layout: [0]=sound id, [+0x18]=flag byte, floats at +0x1c..
        params[0] = owner;
        *(float *)((int)params + 0x1c) = FLOAT_80611718;
        *(float *)((int)params + 0x20) = pos[0];
        *(float *)((int)params + 0x24) = pos[1];
        *(float *)((int)params + 0x28) = pos[2];
        *(char  *)((int)params + 0x2c) = 1;
        sound_play_request(params);
    }
    FUN_80382734();
}

// ===========================================================================
// Address: 0x801b72e4 | Name: object_weapon_minigun_dispatch | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for the spin-up (minigun / gatling) weapon type.
// Cases: 0 preload sound banks + texture set; 1 per-frame spin/heat simulation
// (barrel spin +0xdc, heat +0x108 and +0xe0, looping whir 0xc8a/0xc8b via
// sound_update_3d_source + pitch); 3 reset spin/heat; 10 fire event (charge the
// barrel toward the ceiling); 0xc rotate the muzzle bone matrix by the current
// spin and translate on wind-up; 0x10 report anim slot 6; 0x11 fire-anim sound;
// 0x17 fade out the looping whir on teardown.
// ===========================================================================
int object_weapon_minigun_dispatch(double param_1, double param_2, double param_3, double param_4,
                                   double param_5, double param_6, double param_7, double param_8,
                                   int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;
    (void)param_9; (void)param_10;

    unsigned long long packed = FUN_803826f0();
    int object = (int)packed;
    int op     = (int)(packed >> 0x20);

    float minSpin  = FLOAT_80616d00;
    float spinTgt  = FLOAT_80616cf8;
    float zero     = FLOAT_80616cec;

    switch (op) {
    case 0:
        sound_clear_bank_ref(0xc8b);
        sound_clear_bank_ref(0xc8a);
        sprite_load_texture_set();
        break;

    case 1: {
        int handleOwner = *(int *)(param_11 + 0xe8);
        int prevHandle  = *(int *)(handleOwner + 0x22c);
        float dt = FLOAT_806108ec * (FLOAT_80616ccc / (float)((int)DAT_806109c0));

        // integrate spin toward target
        *(float *)(param_11 + 0x108) =
            -(float)((double)FLOAT_80616cf0 * (double)dt - (double)*(float *)(param_11 + 0x108));
        *(float *)(param_11 + 0x108) =
            -((FLOAT_80616cf4 * dt) * (spinTgt - *(float *)(param_11 + 0x108)) -
              *(float *)(param_11 + 0x108));
        if (*(float *)(param_11 + 0x108) < zero) {
            *(float *)(param_11 + 0x108) = zero;
        }

        if ((*(int *)(object + 0x38) == 0) && (*(int *)(object + 0x64) == 0)) {
            *(float *)(object + 0xdc) = *(float *)(object + 0xdc) * FLOAT_80616cfc;
        }
        if ((*(int *)(object + 0x38) == 1) || (*(int *)(object + 0x64) != 0)) {
            bool wasLow = *(float *)(object + 0xdc) < FLOAT_80616d00;
            float spinLevel = *(float *)(param_11 + 0x108);
            if (FLOAT_80616cec < (float)((double)FLOAT_80616d04 - (double)spinLevel)) {
                if (*(int *)(object + 0x38) == 1) {
                    *(float *)(param_11 + 0x108) =
                        (float)((double)FLOAT_80616d08 *
                                (double)((FLOAT_80616d0c * dt) *
                                         (float)((double)FLOAT_80616d04 - (double)spinLevel)) +
                                (double)spinLevel);
                } else {
                    *(float *)(param_11 + 0x108) = (float)((double)FLOAT_80616d10 * (double)dt + (double)spinLevel);
                }
            }
            float clampC = FLOAT_80616ccc;
            if (*(float *)(param_11 + 0x108) < FLOAT_80616ccc) {
                clampC = *(float *)(param_11 + 0x108);
            }
            *(float *)(param_11 + 0x108) = clampC;

            float spinMax = FLOAT_80616d14;
            *(float *)(object + 0xdc) = FLOAT_80616cf4 * FLOAT_806108ec + *(float *)(object + 0xdc);
            if (*(float *)(object + 0xdc) < spinMax) {
                spinMax = *(float *)(object + 0xdc);
            }
            *(float *)(object + 0xdc) = spinMax;
            if (wasLow) {
                float minS = FLOAT_80616d00;
                if (*(float *)(object + 0xdc) < FLOAT_80616d00) {
                    minS = *(float *)(object + 0xdc);
                }
                *(float *)(object + 0xdc) = minS;
            }
        }

        float wrap = FLOAT_80616d18;
        *(float *)(object + 0xe0) = *(float *)(object + 0xdc) * FLOAT_806108ec + *(float *)(object + 0xe0);
        if (wrap < *(float *)(object + 0xe0)) {
            *(float *)(object + 0xe0) = *(float *)(object + 0xe0) - wrap;
        }

        float pos[3];
        object_resolve_sound_position(param_11, pos, 1);
        double whirRange = math_sqrt((double)(*(float *)(object + 0xdc) / FLOAT_80616d14));
        sound_update_3d_source((double)(FLOAT_80611718 * (float)(whirRange - (double)FLOAT_80616cf4)),
                               (double)FLOAT_80616cdc, (double)FLOAT_80616ccc,
                               handleOwner + 0x22c, (unsigned int *)pos, (float *)(intptr_t)0xc8b, 0);

        if ((-1 < *(int *)(handleOwner + 0x22c))) {
            sound_set_pitch((double)(FLOAT_80616cd8 + *(float *)(object + 0xdc) / FLOAT_80616d1c),
                            (unsigned int)*(int *)(handleOwner + 0x22c));
            if (*(int *)(handleOwner + 0x22c) != prevHandle) {
                int params[6];
                sound_init_play_params((int)params);
                params[0] = 0xc8a;
                *(float *)((int)params + 0x1c) = FLOAT_80611718;
                *(float *)((int)params + 0x20) = pos[0];
                *(float *)((int)params + 0x24) = pos[1];
                *(float *)((int)params + 0x28) = pos[2];
                *(char  *)((int)params + 0x2c) = 1;
                sound_play_request(params);
            }
        }

        if (*(int *)(object + 0x38) == 0) {
            float t = -(float)((double)FLOAT_80616d20 * (double)dt - (double)*(float *)(object + 0xd8));
            if (t < FLOAT_80616cec) t = FLOAT_80616cec;
            *(float *)(object + 0xd8) = t;
        } else {
            float t = (float)((double)FLOAT_80616d20 * (double)dt + (double)*(float *)(object + 0xd8));
            if (FLOAT_80616ccc < t) t = FLOAT_80616ccc;
            *(float *)(object + 0xd8) = t;
        }
        break;
    }

    case 3:
        *(float *)(object + 0xdc) = FLOAT_80616cec;
        *(float *)(object + 0xe0) = zero;
        break;

    case 10:
        *(int *)(object + 0x64) = 1;
        if ((minSpin <= *(float *)(object + 0xdc)) && (FLOAT_80616d30 < *(float *)(param_11 + 0x108))) {
            *(float *)(param_11 + 0x108) = *(float *)(param_11 + 0x108) + FLOAT_80616d34;
        }
        if ((FLOAT_80616d00 <= *(float *)(object + 0xdc)) &&
            (*(float *)(param_11 + 0x108) <= FLOAT_80616d30)) {
            float add;
            if (*(int *)(object + 0x38) == 0) {
                add = FLOAT_80616d3c * *(float *)(param_11 + 0x108) + FLOAT_80616d38;
            } else {
                add = FLOAT_80616d40 * *(float *)(param_11 + 0x108) + FLOAT_80616d3c;
            }
            *(float *)(param_11 + 0x108) = *(float *)(param_11 + 0x108) + add;
            if (FLOAT_80616ccc < *(float *)(param_11 + 0x108)) {
                *(float *)(param_11 + 0x108) = FLOAT_80616ccc;
            }
        }
        break;

    case 0xc:
        if (*(int *)(object + 0x54) != 0) {
            int model = *(int *)(*(int *)(*(int *)(object + 0x54) + 4) + 0x10);
            float *muzzleMtx = (float *)(model + 0x180);
            float savedX = *(float *)(model + 0x1b0);
            float savedY = *(float *)(model + 0x1b4);
            float savedZ = *(float *)(model + 0x1b8);
            float rotMtx[16];
            mtx4_set_rotation_zyx((double)FLOAT_80616cec, (double)FLOAT_80616cec,
                                  (double)((FLOAT_80616d24 * *(float *)(object + 0xe0)) / FLOAT_80616d28),
                                  rotMtx);
            if (object_anim_get_frame(object) == 10) {
                mtx4_copy(rotMtx, muzzleMtx);
            } else {
                mtx4_multiply(rotMtx, muzzleMtx, muzzleMtx);
            }
            *(float *)(model + 0x1b0) = savedX;
            *(float *)(model + 0x1b4) = savedY;
            *(float *)(model + 0x1b8) = savedZ;
            if (((FLOAT_80616cec < *(float *)(object + 0xd8)) &&
                 (*(int *)(object + 0x44) != 3)) && (*(int *)(object + 0x44) != 6)) {
                float *transMtx = (float *)(*(int *)(*(int *)(*(int *)(object + 0x54) + 4) + 0x10) + 0xc0);
                mtx4_set_translation(rotMtx, 0.0f, 0.0f,
                                     (float)((double)FLOAT_80616d2c * (double)*(float *)(object + 0xd8)));
                mtx4_multiply(rotMtx, transMtx, transMtx);
            }
        }
        break;

    case 0x10:
        *param_12 = 6;
        break;

    case 0x11:
        if (*(int *)(object + 0xe8) == 0x15) {
            object_play_sound_pitched((double)FLOAT_80616d44, 0xcbb, param_11, 1);
        }
        break;

    case 0x17:
        sound_register_fade_out((double)FLOAT_80616ccc, (double)FLOAT_80616ce8,
                                (int *)(param_11 + 0x100), (unsigned int *)0);
        break;
    }

    FUN_8038273c();
    return 0;
}

// ===========================================================================
// Address: 0x801b7840 | Name: object_weapon_dispatch_7840 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler for a weapon type: case 0 preloads sound banks
// (0xd3e/0xc70/0xd42) and the entity resources for asset 0x715; case 4/7 report
// ready (7 also refreshes render flags on all nodes); case 0x10 reports anim
// slot 7; case 0x11 plays the fire sound (0xc70) or, on reload frames, refreshes
// render flags. Returns a per-case status.
// ===========================================================================
unsigned int object_weapon_dispatch_7840(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    unsigned int result;
    switch (param_9) {
    case 0:
        sound_clear_bank_ref(0xd3e);
        sound_clear_bank_ref(0xc70);
        sound_clear_bank_ref(0xd42);
        entity_preload_resources(0x715);
        result = 0;
        break;
    case 4:
        result = 1;
        break;
    case 7:
        object_apply_render_flag_all_nodes();
        result = 1;
        break;
    case 0x10:
        result = 1;
        *param_12 = 7;
        break;
    case 0x11: {
        int frame = *(int *)(param_10 + 0xe8);
        if (frame == 1) {
            object_play_sound_at(0xc70, param_11, 1);
        } else if ((frame == 0x13) || (frame == 9)) {
            object_apply_render_flag_all_nodes();
            return 1;
        }
        result = 0;
        break;
    }
    default:
        result = 0;
    }
    return result;
}

// ===========================================================================
// Address: 0x801b7918 | Name: object_weapon_dispatch_7918 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: on op 0x11 (anim-event) plays the weapon's
// per-frame sounds -- fire (frame 0x15, pitch chosen by object +0x40 state) or
// spin/whine (frame 9); on op 0 clears the sound bank ref (0xc91). Always
// returns 0.
// ===========================================================================
unsigned int object_weapon_dispatch_7918(int op, int object, int soundObj)
{
    if (op == 0x11) {
        int frame = *(int *)(object + 0xe8);
        if (frame == 0x15) {
            if (*(int *)(object + 0x40) == 0) {
                object_play_sound_pitched((double)FLOAT_80616d4c, 0xcbb, soundObj, 1);
            } else {
                object_play_sound_pitched((double)FLOAT_80616d0c, 0xcbb, soundObj, 1);
            }
        } else if ((frame < 0x15) && (frame == 9)) {
            object_play_sound_pitched((double)FLOAT_80616d48, 0xc91, soundObj, 1);
        }
    } else if ((op < 0x11) && (op == 0)) {
        sound_clear_bank_ref(0xc91);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b79cc | Name: object_weapon_dispatch_79cc | Date: 2026-09-06
// ---------------------------------------------------------------------------
// object_type_dispatch handler: case 7 plays the fire sound (0xc76); case 0
// preloads sound banks (0xc77/0xc76/0xc78/0xc79) + the texture set; case 0x11
// plays per-frame anim-event sounds (0xcbb / 0xc78 / 0xc79 by frame); ops
// 0x10..0x10 report anim slot 6. Returns a per-case status.
// ===========================================================================
unsigned int object_weapon_dispatch_79cc(double param_1, double param_2, double param_3, double param_4,
                                         double param_5, double param_6, double param_7, double param_8,
                                         int param_9, int param_10, int param_11, unsigned int *param_12)
{
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;

    if (param_9 == 7) {
        object_play_sound_pitched((double)FLOAT_80616d54, 0xc76, param_11, 1);
        return 1;
    }

    if (param_9 < 7) {
        if (param_9 == 0) {
            sound_clear_bank_ref(0xc77);
            sound_clear_bank_ref(0xc76);
            sound_clear_bank_ref(0xc78);
            sound_clear_bank_ref(0xc79);
            sprite_load_texture_set();
            return 0;
        }
    } else if (param_9 == 0x11) {
        int frame = *(int *)(param_10 + 0xe8);
        if (frame == 0x15) {
            object_play_sound_at(0xcbb, param_11, 1);
        } else if (frame < 0x15) {
            if (frame == 9) {
                object_play_sound_at(0xc78, param_11, 1);
            }
        } else if (frame == 0x24) {
            object_play_sound_pitched((double)FLOAT_80616d50, 0xc79, param_11, 1);
        }
    } else if ((param_9 < 0x11) && (0xf < param_9)) {
        *param_12 = 6;
        return 1;
    }
    return 0;
}
