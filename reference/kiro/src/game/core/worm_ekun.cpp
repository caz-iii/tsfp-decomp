// worm_ekun.cpp
// CDH1 "EKUN" worm-boss set-piece: object-init dispatcher + particle-effect
// bank + the swing/fill/spark render helpers that back the object types the
// dispatcher installs. Address range 0x801e79f8 - 0x801e902c (batch_619),
// continuation of worm_column2.cpp / worm_render.cpp. subsystem:game,
// worm-effect module.
//
// Functions:
//   ekun_fill_render_flags_tick   (0x801e79f8) per-frame render-flag toggle:
//                                 raise material flag pair when the fill level
//                                 (+0x1cc) crosses a high threshold and register
//                                 the entity for update; lower it under a low
//                                 threshold. Installed as render_type for
//                                 object type 0x37f.
//   ekun_spark_spray_emitter_tick (0x801e7aac) throttled spray emitter: on the
//                                 frame budget, spawn a spark/spray burst at a
//                                 rig bone aimed toward a target point (with a
//                                 curved normalization), tinted by the owner.
//   ekun_swing_emitter_update     (0x801e7c8c) fill-driven swing emitter state
//                                 machine (states 0..3): spawn the cached spray
//                                 emitter, then ramp drag / size / spawn-radius
//                                 and integrate the swing angle (+0xd4) from the
//                                 fill value (+0x98) up and back down.
//   ekun_swing_rig_update         (0x801e7fec) rebuild the bone palette: reset
//                                 every joint to identity then rotate joint 1
//                                 about Z by the swing angle, mark the instance
//                                 matrix dirty (flag 0x200000).
//   ekun_blkh_damage_thunk        (0x801e80f4) tail-call thunk to
//                                 damage_character_receive (distinct code
//                                 address so it can be used as a render_type
//                                 for the 'BLKH' variant, type 0x37a).
//   ekun_area_damage_apply        (0x801e8114) walk the CDH1 level-object array
//                                 (stride 0x141c) and dispatch splash damage to
//                                 every object whose height is within range of
//                                 the source.
//   ekun_object_init_dispatch     (0x801e8224) the big object-def init switch,
//                                 keyed on the object type id (*(param+0x94),
//                                 0x2ed..0x38e): installs render types, update
//                                 lists, subobject descriptors, fluid/particle
//                                 emitters and flag bits for every EKUN set-piece
//                                 object variant.
//   ekun_build_smoke_overlay_emitter (0x801e8dc8) build one smoke/overlay
//                                 particle def + overlay + emitter (0x32e).
//   ekun_build_spark_emitter      (0x801e8f30) build one spark particle def +
//                                 emitter (0x34e).
//   ekun_init_particle_bank       (0x801e902c) one-time init of the whole EKUN
//                                 particle bank: clear the cached handles, build
//                                 ~8 particle defs + emitters (dust/debris/spark
//                                 /smoke/overlay groups), pre-spawn and disable
//                                 the cached emitters, and call the two builders.
//
// Non-matching: object type ids read from *(entity+0x94) are the level-def
// "class" words; the switch preserves the ROM's exact case values. The PPC
// FP-context spill frames (FUN_803826f0 open / FUN_8038273c close, __save_gpr_28
// / __restore_gpr_28) are dead ABI plumbing kept opaque. The 0x43300000
// magic-bias int<->double idiom is reduced to -(double)(int). Several shared
// helpers whose generated prototype is void / wrong-arity are routed through
// file-local typed fn-ptr aliases (__we_*).
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local, typed for use here) ---
extern int   DAT_80611d78;      // CDH1 level-object array base
extern int   DAT_80611d74;      // active-level record ptr
extern int   DAT_80612610;      // active object count
extern int   DAT_806126a0;      // arc branch-node write index
extern int   DAT_806126fc;      // arc branch-node handle array
extern int   DAT_80610d90;      // deferred budget / one-shot latch
extern int   DAT_80610d94;      // scratch render-target slot
extern unsigned int DAT_806109c0; // frame-count numerator (magic-bias src)
extern int   DAT_80611928;      // frame budget target
extern unsigned char DAT_805015bd; // level id byte
extern int   DAT_805015c0;      // level flags
extern float DAT_805c4ff4; extern float DAT_805c4ff8; extern float DAT_805c4ffc; // column ctrl block
extern int   DAT_805c5044;      // column projectile handle

// cached emitter handles (all cleared/spawned in the bank init)
extern int   DAT_80612708; extern int DAT_8061270c; extern int DAT_80612710;
extern int   DAT_80612714; extern int DAT_80612718;

// particle def / emitter descriptor blobs (rodata addresses used by ref)
extern int   DAT_805c4e4c; extern int DAT_805c4ed0;
extern int   DAT_805c50f0; extern int DAT_805c506c; extern int DAT_805c5174;
extern int   DAT_805c51f8; extern int DAT_805c527c; extern int DAT_805c5300;
extern int   DAT_805c53cc; extern int DAT_805c5404; extern int DAT_805c54d0;
extern int   DAT_805c559c; extern int DAT_805c5668; extern int DAT_805c56ec;
extern int   DAT_805c5770; extern int DAT_805c583c; extern int DAT_805c58c0;
extern int   DAT_805c598c; extern int DAT_805c5a58; extern int DAT_805c5adc;
extern int   DAT_805c5ba8; extern int DAT_805c5c74; extern int DAT_805c5d40;
extern int   DAT_805c5e0c; extern int DAT_805c5e44;
extern float DAT_80583228;      // sine/lookup table base (float[2048])

// rodata float/vector constant blocks used by the emitters and damage descs
extern int   DAT_803df87c; extern int DAT_803df880; extern int DAT_803df884;
extern int   DAT_803df888; extern int DAT_803df88c; extern int DAT_803df890;
extern int   DAT_803df894; extern int DAT_803df898; extern int DAT_803df89c;
extern int   DAT_803df8a0; extern int DAT_803df8a4; extern int DAT_803df8a8;
extern int   DAT_803df8ac; extern int DAT_803df8b0; extern int DAT_803df8b4;
extern int   DAT_803df8b8; extern int DAT_803df8bc; extern int DAT_803df8c0;
extern int   DAT_803df8c4; extern int DAT_803df8c8; extern int DAT_803df8cc;

// tuning float pool (rodata)
extern float FLOAT_80617428; extern float FLOAT_80617430; extern float FLOAT_80617440;
extern float FLOAT_80617444; extern float FLOAT_80617448; extern float FLOAT_80617450;
extern float FLOAT_80617498;
extern float FLOAT_8061745c; extern float FLOAT_80617460; extern float FLOAT_80617464;
extern float FLOAT_80617468; extern float FLOAT_8061746c; extern float FLOAT_80617470;
extern float FLOAT_80617474; extern float FLOAT_8061748c; extern float FLOAT_8061749c;
extern float FLOAT_806174a0; extern float FLOAT_806174bc; extern float FLOAT_806174c4;
extern float FLOAT_806174c8; extern float FLOAT_806174cc; extern float FLOAT_806174dc;
extern float FLOAT_806174fc; extern float FLOAT_80617500; extern float FLOAT_80617504;
extern float FLOAT_80617508; extern float FLOAT_8061750c; extern float FLOAT_80617514;
extern float FLOAT_8061754c; extern float FLOAT_80617554; extern float FLOAT_80617558;
extern float FLOAT_80617574; extern float FLOAT_8061757c; extern float FLOAT_806175a8;
extern float FLOAT_806175b4; extern float FLOAT_80617614; extern float FLOAT_8061761c;
extern float FLOAT_80617620; extern float FLOAT_80617628; extern float FLOAT_8061762c;
extern float FLOAT_80617634; extern float FLOAT_80617638; extern float FLOAT_80617640;
extern float FLOAT_80617644; extern float FLOAT_80617648; extern float FLOAT_8061749c;
extern float FLOAT_80617654; extern float FLOAT_80617658; extern float FLOAT_8061765c;
extern float FLOAT_80617660; extern float FLOAT_80617664; extern float FLOAT_80617668;
extern float FLOAT_8061766c; extern float FLOAT_80617670; extern float FLOAT_80617674;
extern float FLOAT_80617678; extern float FLOAT_8061767c; extern float FLOAT_80617680;
extern float FLOAT_80617684; extern float FLOAT_80617688; extern float FLOAT_8061768c;
extern float FLOAT_80617690; extern float FLOAT_80617694; extern float FLOAT_80617698;
extern float FLOAT_8061769c; extern float FLOAT_806176a0; extern float FLOAT_806176a4;
extern float FLOAT_806176a8; extern float FLOAT_806174dc; extern float FLOAT_80617428;
extern float FLOAT_806108ec;    // per-frame delta seconds
extern double DOUBLE_80617420;  // magic-bias subtrahend

// --- raw code symbols (file-local; not in the generated headers) ---
extern int   FUN_803826f0(void);   // FP spill frame open (returns owner entity)
extern void  FUN_8038273c(void);   // FP spill frame close
extern int   __save_gpr_28(void);  // GPR spill open (returns owner entity)
extern void  __restore_gpr_28(void);
extern void  FUN_802418b4(void);   // EKUN sub-init hook (case 0x2ee)
extern void  FUN_80369a44(unsigned long long, double, double, double, double,
                          double, double, double, int, int, int,
                          int, int, int, int, int); // subobj build variant (0x334)

} // extern "C"

// column_effect_init / column_impact_tick / column_break_arm are declared with
// C++ linkage in generated_decls.h (included via entity.h); do not re-declare.

// --- typed fn-ptr aliases for shared-header prototypes with wrong type/arity ---

// particle_def_create declared (u64,float,u32,...); the ROM form takes two
// scalars, a descriptor pointer, then the flags/ids.
static void (*const __we_def_create)(double, double, int *, int, int, int, int, int) =
    (void (*)(double, double, int *, int, int, int, int, int))&particle_def_create;

// particle_emitter_create declared (u64,double,double,u32,...); the ROM form
// takes 3 scalars, an emitter descriptor ptr, a def descriptor ptr, then flags.
static void (*const __we_emitter_create)(double, double, double, int *, int *, int, int) =
    (void (*)(double, double, double, int *, int *, int, int))&particle_emitter_create;

// particle_overlay_create declared with 8 args; the ROM form has 10.
static void (*const __we_overlay_create)(double, double, double, double, double,
                                         int *, int *, int, int, int) =
    (void (*)(double, double, double, double, double,
              int *, int *, int, int, int))&particle_overlay_create;

// particle_emitter_set_scale declared (float); the ROM form takes a float[3] ptr.
static void (*const __we_emitter_set_scale)(int *) =
    (void (*)(int *))&particle_emitter_set_scale;

// particle_emitter_set_texture declared (int); called with a descriptor ptr.
static void (*const __we_emitter_set_texture)(int *) =
    (void (*)(int *))&particle_emitter_set_texture;

// subobj_pool_alloc declared void; returns the new sub-object handle.
static int (*const __we_subobj_pool_alloc)(void) =
    (int (*)(void))&subobj_pool_alloc;

// subobj_build_descriptor_default declared 3-arg; the ROM form takes the FP
// context + 7 doubles + handle/entity + type + lod + a packed float.
static void (*const __we_subobj_build_desc)(double, double, double, double, double,
                                            double, double, double, int, int *, int,
                                            int, float) =
    (void (*)(double, double, double, double, double, double, double, double,
              int, int *, int, int, float))&subobj_build_descriptor_default;

// entity_model_cache_alloc declared (int,int); the ROM form takes the full
// spawn context (16 args) and returns the model-cache block ptr.
static int *(*const __we_model_cache_alloc)(unsigned long long, double, double,
                                            double, double, double, double, double,
                                            int *, int, int, int, int, int, int, int) =
    (int *(*)(unsigned long long, double, double, double, double, double, double,
              double, int *, int, int, int, int, int, int, int))&entity_model_cache_alloc;

// fluid_emitter_register declared void; returns the emitter handle.
static int (*const __we_fluid_emitter_register)(int, int, int, int, int) =
    (int (*)(int, int, int, int, int))&fluid_emitter_register;

// switch_slot_reset declared (int); returns the reset event/entity ptr.
static unsigned long long (*const __we_switch_slot_reset)(int) =
    (unsigned long long (*)(int))&switch_slot_reset;

// damage_dispatch_to_entity declared (int*); the ROM form is the full
// 9-arg damage dispatch (event ptr, 7 doubles, ctx block).
static void (*const __we_damage_dispatch)(unsigned long long, double, double, double,
                                          double, double, double, double, int *) =
    (void (*)(unsigned long long, double, double, double, double, double, double,
              double, int *))&damage_dispatch_to_entity;

// mtx4_set_translation declared (float*,x,y,z); this call site uses the
// 3-scalar "set current builder translation" variant.
static void (*const __we_mtx_set_translation)(double, double, double) =
    (void (*)(double, double, double))&mtx4_set_translation;

// ekun_spawn_reset_wrapper declared (8 doubles); called with an entity ptr then
// 7 doubles.
static void (*const __we_ekun_spawn_reset)(unsigned long long, double, double, double,
                                           double, double, double, double) =
    (void (*)(unsigned long long, double, double, double, double, double, double,
              double))&ekun_spawn_reset_wrapper;

// entity_set_render_type declared (int,int); the 2nd arg here is a code address.
static void (*const __we_set_render_type)(int, void *) =
    (void (*)(int, void *))&entity_set_render_type;


// ---------------------------------------------------------------------------
// 0x801e79f8  ekun_fill_render_flags_tick
// ---------------------------------------------------------------------------
void ekun_fill_render_flags_tick(int *param_1)
{
    int iVar1 = *param_1;
    if ((FLOAT_806175b4 < *(float *)(iVar1 + 0x1cc)) &&
        ((*(unsigned int *)(iVar1 + 0x28) & 0x4000000) == 0)) {
        *(unsigned int *)(iVar1 + 0x28) |= 0x4000000;
        entity_register_for_update(iVar1, 5, -0x7ff85e6c);
        *(unsigned int *)(*(int *)(iVar1 + 4) + 0x30) &= 0xfffffffd;
        *(unsigned int *)(*(int *)(iVar1 + 4) + 0x30) |= 4;
    }
    if (*(float *)(iVar1 + 0x1cc) <= FLOAT_806174dc) {
        *(unsigned int *)(*(int *)(iVar1 + 4) + 0x30) &= 0xfffffffb;
        *(unsigned int *)(*(int *)(iVar1 + 4) + 0x30) |= 8;
    }
}


// ---------------------------------------------------------------------------
// 0x801e7aac  ekun_spark_spray_emitter_tick
// ---------------------------------------------------------------------------
void ekun_spark_spray_emitter_tick(int param_1)
{
    float fVar1, fVar2;
    int *piVar3;
    float *pfVar4;
    int iVar5;
    float local_78, local_74, local_70, local_6c, local_68, local_64;
    float afStack_60[16];

    iVar5 = *(int *)(param_1 + 0x7c);
    if ((*(int *)(iVar5 + 0x144) == 0) ||
        ((unsigned int)(*(int *)(iVar5 + 0x144) +
                        (int)*(float *)(iVar5 + 0x98) * (int)DAT_806109c0) < (unsigned int)DAT_80611928)) {
        *(unsigned int *)(iVar5 + 0x144) = (unsigned int)DAT_80611928;
        entity_build_local_matrix(afStack_60, (void *)param_1, 1, 0);
        piVar3 = *(int **)(*(int *)(param_1 + 4) + 4);
        pfVar4 = (float *)piVar3[*piVar3 * -0x30 + 0x1b];
        local_78 = *pfVar4;
        local_74 = pfVar4[1];
        local_70 = pfVar4[2];
        mtx4_transform_point_alias(afStack_60, &local_78);
        local_68 = FLOAT_80617440;
        local_6c = (*(float *)(param_1 + 0x64) + *(float *)(param_1 + 0x1a8)) - local_78;
        local_64 = (*(float *)(param_1 + 0x6c) + *(float *)(param_1 + 0x1b0)) - local_70;
        fVar2 = local_64 * local_64 + local_6c * local_6c + FLOAT_80617440 * FLOAT_80617440;
        if (FLOAT_80617638 < fVar2) {
            fVar1 = 1.0f / SQRT(fVar2);
            fVar1 = fVar1 * -(FLOAT_80617430 * fVar2 * fVar1 * fVar1 - FLOAT_8061749c);
            fVar2 = FLOAT_80617428 * fVar1 * -(FLOAT_80617430 * fVar2 * fVar1 * fVar1 - FLOAT_8061749c);
            local_6c = local_6c * fVar2;
            local_68 = FLOAT_80617440 * fVar2;
            local_64 = local_64 * fVar2;
        }
        particle_emitter_spawn((unsigned int)(iVar5 + 0x11c), (unsigned int)(uintptr_t)&DAT_805c51f8,
                               0x801, &local_78, &local_6c);
        particle_emitter_set_color_keys(*(ParticleEmitter **)(iVar5 + 0x11c),
                                        (int)*(char *)(param_1 + 0x30),
                                        (int *)(param_1 + 0x38));
    }
}


// ---------------------------------------------------------------------------
// 0x801e7c8c  ekun_swing_emitter_update
// ---------------------------------------------------------------------------
void ekun_swing_emitter_update(int param_1)
{
    unsigned int uVar1;
    float fVar2, fVar3;
    bool bVar4;
    int *piVar5;
    int iVar6, iVar7;
    double dVar9, dVar10;
    int local_c8, local_c4, local_c0, local_bc, local_b8, local_b4, local_b0, local_ac, local_a8, local_a4;
    float local_a0, local_9c, local_98, local_94, local_90;
    int local_8c;
    float afStack_88[4];
    float local_78, local_74, local_70;
    double local_48;

    iVar7 = *(int *)(param_1 + 0x7c);
    local_a4 = *(int *)(iVar7 + 0x11c);
    local_48 = -(double)(int)DAT_806109c0;
    dVar9 = (double)FLOAT_80617440;
    dVar10 = (double)(FLOAT_806108ec * (FLOAT_80617428 / (float)(local_48 - DOUBLE_80617420)));
    bVar4 = particle_handle_is_valid(&local_a4);
    if (!bVar4) {
        entity_build_local_matrix(afStack_88, (void *)param_1, 1, 0);
        piVar5 = *(int **)(*(int *)(param_1 + 4) + 4);
        iVar6 = piVar5[*piVar5 * -0x30 + 0x1b];
        local_94 = *(float *)(iVar6 + 0x18);
        local_90 = *(float *)(iVar6 + 0x1c);
        local_8c = *(int *)(iVar6 + 0x20);
        mtx4_transform_point_alias(afStack_88, &local_94);
        local_a0 = -local_78;
        local_9c = -local_74;
        local_98 = -local_70;
        piVar5 = *(int **)(*(int *)(param_1 + 4) + 4);
        iVar6 = piVar5[*piVar5 * -0x30 + 0x1b];
        double dVar8 = (double)(*(float *)(iVar6 + 0x1c) - *(float *)(iVar6 + 0x34));
        particle_emitter_spawn((unsigned int)(iVar7 + 0x11c), (unsigned int)(uintptr_t)&DAT_805c527c,
                               4, &local_94, &local_a0);
        local_a8 = *(int *)(iVar7 + 0x11c);
        particle_cached_set_drag(FLOAT_80617464, (ParticleEmitter **)&local_a8);
        particle_emitter_set_color_keys(*(ParticleEmitter **)(iVar7 + 0x11c),
                                        (int)*(char *)(param_1 + 0x30),
                                        (int *)(param_1 + 0x38));
        local_ac = *(int *)(iVar7 + 0x11c);
        particle_cached_set_spawn_radius((float)((double)local_90 - dVar8),
                                         (ParticleEmitter **)&local_ac);
        local_b0 = *(int *)(iVar7 + 0x11c);
        particle_cached_set_size_range(FLOAT_80617430, FLOAT_806174fc, (ParticleEmitter **)&local_b0);
    }
    fVar3 = FLOAT_806174dc;
    fVar2 = FLOAT_8061749c;
    if ((double)FLOAT_80617440 < (double)*(float *)(iVar7 + 0x98)) {
        *(float *)(iVar7 + 0x98) = (float)((double)*(float *)(iVar7 + 0x98) - dVar10);
        uVar1 = (unsigned int)(FLOAT_80617444 * FLOAT_80617448 * fVar3 *
                               (*(float *)(iVar7 + 0x98) / fVar2) + FLOAT_80617430);
        dVar9 = (double)(float)(dVar10 * (double)(*(float *)(iVar7 + 0xd8) *
                     (FLOAT_80617654 + (&DAT_80583228)[uVar1 & 0x7ff])));
    }
    fVar3 = FLOAT_8061749c;
    fVar2 = FLOAT_80617428;
    iVar6 = *(int *)(iVar7 + 0x144);
    if (iVar6 == 2) {
        *(float *)(iVar7 + 0xd4) = (float)((double)*(float *)(iVar7 + 0xd4) + dVar9);
        local_c4 = *(int *)(iVar7 + 0x11c);
        particle_cached_set_drag(fVar2 - *(float *)(iVar7 + 0x98) / fVar3, (ParticleEmitter **)&local_c4);
        local_c8 = *(int *)(iVar7 + 0x11c);
        particle_cached_set_size_range(FLOAT_80617428,
            (float)((double)FLOAT_80617428 - (double)(*(float *)(iVar7 + 0x98) / FLOAT_8061749c)),
            (ParticleEmitter **)&local_c8);
        if (*(float *)(iVar7 + 0x98) <= FLOAT_80617440) {
            *(int *)(iVar7 + 0x144) = 3;
            *(float *)(iVar7 + 0x98) = FLOAT_80617440;
        }
    } else if (((iVar6 < 2) && (iVar6 != 0)) && (-1 < iVar6)) {
        *(float *)(iVar7 + 0xd4) = (float)((double)*(float *)(iVar7 + 0xd4) - dVar9);
        local_b4 = *(int *)(iVar7 + 0x11c);
        particle_cached_set_drag(*(float *)(iVar7 + 0x98) / fVar3, (ParticleEmitter **)&local_b4);
        local_b8 = *(int *)(iVar7 + 0x11c);
        particle_cached_set_size_range(FLOAT_80617428, *(float *)(iVar7 + 0x98) / FLOAT_8061749c,
                                       (ParticleEmitter **)&local_b8);
        if (*(float *)(iVar7 + 0x98) <= FLOAT_80617440) {
            local_bc = *(int *)(iVar7 + 0x11c);
            particle_cached_set_drag(FLOAT_80617464, (ParticleEmitter **)&local_bc);
            local_c0 = *(int *)(iVar7 + 0x11c);
            particle_cached_set_size_range(FLOAT_80617430, FLOAT_806174fc, (ParticleEmitter **)&local_c0);
            *(int *)(iVar7 + 0x144) = 0;
            *(float *)(iVar7 + 0x98) = FLOAT_80617440;
        }
    }
}


// ---------------------------------------------------------------------------
// 0x801e7fec  ekun_swing_rig_update
// ---------------------------------------------------------------------------
void ekun_swing_rig_update(void)
{
    int iVar1, iVar2;
    int *piVar3;
    float *pfVar4;
    int iVar5, iVar6, iVar7;
    float afStack_88[24];

    iVar1 = __save_gpr_28();
    iVar5 = 0;
    iVar6 = *(int *)(iVar1 + 0x7c);
    iVar7 = 0;
    while (true) {
        iVar2 = *(int *)(iVar1 + 4);
        piVar3 = *(int **)(iVar2 + 4);
        if (piVar3[1] <= iVar5) break;
        mtx4_set_identity(*(int *)(iVar2 + 0x10) + iVar7);
        iVar5 = iVar5 + 1;
        iVar7 = iVar7 + 0x40;
    }
    pfVar4 = (float *)piVar3[*piVar3 * -0x30 + 0x1b];
    double dVar10 = (double)pfVar4[0];
    double dVar9 = (double)pfVar4[1];
    double dVar8 = (double)pfVar4[2];
    mtx4_set_translate_rotz(dVar10, dVar9, dVar8, (double)*(float *)(iVar6 + 0xd4),
                            (float *)(*(int *)(iVar2 + 0x10) + 0x40));
    __we_mtx_set_translation(-dVar10, -dVar9, -dVar8);
    pfVar4 = (float *)(*(int *)(*(int *)(iVar1 + 4) + 0x10) + 0x40);
    mtx4_multiply(afStack_88, pfVar4, pfVar4);
    *(unsigned int *)(iVar1 + 0x28) |= 0x200000;
    __restore_gpr_28();
}


// ---------------------------------------------------------------------------
// 0x801e80f4  ekun_blkh_damage_thunk
// ---------------------------------------------------------------------------
void ekun_blkh_damage_thunk(void *param_1, double param_2, double param_3, double param_4,
                            double param_5, double param_6, double param_7, void *param_8,
                            int *param_9)
{
    damage_character_receive(param_1, (float)param_2, (float)param_3, (float)param_4,
                             (float)param_5, (float)param_6, (float)param_7, param_8, param_9);
}


// ---------------------------------------------------------------------------
// 0x801e8114  ekun_area_damage_apply
// ---------------------------------------------------------------------------
void ekun_area_damage_apply(unsigned long long param_1, double param_2, double param_3,
                            double param_4, double param_5, double param_6, double param_7,
                            double param_8)
{
    int iVar1, iVar2, iVar3;
    unsigned long long uVar4;
    int local_98, local_94, local_90, local_8c, local_88, local_84;
    int local_80[5];
    int local_6c, local_68;
    float local_60;
    int local_58;
    int *local_54, *local_50;

    (void)param_1;
    iVar1 = FUN_803826f0();
    double dVar5 = (double)FLOAT_80617430;
    iVar2 = 0;
    for (iVar3 = 0; iVar3 < DAT_80612610; iVar3 = iVar3 + 1) {
        if ((double)(*(float *)(DAT_80611d78 + iVar2 + 0xa8) - *(float *)(iVar1 + 0x64)) < dVar5) {
            local_8c = DAT_803df87c;
            local_88 = DAT_803df880;
            local_84 = DAT_803df884;
            local_98 = DAT_803df888;
            local_94 = DAT_803df88c;
            local_90 = DAT_803df890;
            uVar4 = __we_switch_slot_reset((int)(uintptr_t)local_80);
            local_80[0] = *(int *)(DAT_80611d78 + iVar2 + 0x14);
            local_58 = DAT_80611d78 + iVar2 + 0xa8;
            local_54 = &local_8c;
            local_50 = &local_98;
            local_60 = FLOAT_80617508;
            local_6c = 0x15;
            local_68 = 0x15;
            __we_damage_dispatch(uVar4, param_2, param_3, param_4, param_5, param_6, param_7,
                                 param_8, local_80);
        }
        iVar2 = iVar2 + 0x141c;
    }
    FUN_8038273c();
}


// ---------------------------------------------------------------------------
// 0x801e8224  ekun_object_init_dispatch
// ---------------------------------------------------------------------------
void ekun_object_init_dispatch(unsigned long long param_1, double param_2, double param_3,
                               double param_4, double param_5, double param_6, double param_7,
                               double param_8, int param_9, int param_10,
                               int param_11, int param_12, int param_13, int param_14,
                               int param_15, int param_16)
{
    float fVar1 = FLOAT_80617428;
    float fVar2 = FLOAT_80617430;
    float fVar3 = FLOAT_80617440;
    float fVar4 = FLOAT_80617468;
    float fVar5 = FLOAT_806174c4;
    float fVar6 = FLOAT_80617504;
    float fVar7 = FLOAT_80617574;
    float fVar8 = FLOAT_80617620;
    float fVar9 = FLOAT_80617660;
    float fVar10 = FLOAT_80617668;
    float fVar11 = FLOAT_8061766c;
    int iVar14;
    int uVar12;
    float *pfVar13;
    unsigned long long uVar15;
    double dVar16;
    (void)param_11;

    switch (*(unsigned short *)(param_9 + 0x94)) {
    case 0x2ed:
        iVar14 = *(int *)(param_9 + 0x7c);
        dVar16 = (double)FLOAT_80617440;
        *(int *)(iVar14 + 0xa0) = 5;
        *(float *)(iVar14 + 0x10) = fVar3;
        *(float *)(iVar14 + 0x9c) = fVar3;
        *(float *)(iVar14 + 0x18) = fVar3;
        *(float *)(iVar14 + 0x1c) = fVar3;
        render_instance_set_material((float)dVar16, *(int **)(param_9 + 4), 1);
        entity_register_for_update(param_9, 3, -0x7fe20b90);
        entity_register_for_update(param_9, 2, -0x7fe206c4);
        *(unsigned int *)(param_9 + 0x28) |= 0x4000000;
        {
            int local_18 = DAT_80612718;
            particle_cached_set_bounds((ParticleEmitter **)&local_18, *(int *)(param_9 + 0x34),
                                       (float *)0);
        }
        break;
    case 0x2ee:
        FUN_802418b4();
        *(unsigned int *)(param_9 + 0x28) |= 4;
        entity_register_for_update(param_9, 3, -0x7fdbe690);
        entity_register_for_update(param_9, 0, -0x7fe1cdd4);
        entity_register_for_update(param_9, 2, -0x7fdbe260);
        break;
    case 0x300: case 0x301: case 0x302: case 0x303: case 0x304: case 0x305:
    case 0x306: case 0x307: case 0x308: case 0x309: case 0x30a:
        arc_entity_register_node(param_9);
        break;
    case 0x30b:
        *(int *)(param_9 + 0x34) = 0xd5;
        *(unsigned int *)(param_9 + 0x28) |= 0x40000000;
        break;
    case 0x30e: case 0x30f:
        *(unsigned int *)(param_9 + 0x28) |= 0x40000000;
        *(int *)(param_9 + 0x34) = 0;
        iVar14 = DAT_806126a0 * 4;
        DAT_806126a0 = DAT_806126a0 + 1;
        *(int *)((char *)&DAT_806126fc + iVar14) = *(int *)(param_9 + 0x2c);
        break;
    case 0x310: case 0x311: case 0x312: case 0x313: case 0x314:
    case 0x315: case 0x316: case 0x317: case 0x318: case 0x319:
        arc_entity_register_branch_bone(param_9);
        break;
    case 0x31c:
        iVar14 = *(int *)(param_9 + 0x7c);
        *(float *)(iVar14 + 0x10) = FLOAT_80617440;
        *(float *)(iVar14 + 0x18) = fVar3;
        *(unsigned int *)(param_9 + 0x28) |= 0x40000000;
        *(int *)(param_9 + 0x34) = 0xd5;
        entity_register_for_update(param_9, 3, -0x7fe1c0b4);
        DAT_80610d90 = -1;
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 0;
        break;
    case 0x31d:
        iVar14 = *(int *)(param_9 + 0x7c);
        *(unsigned int *)(param_9 + 0x28) |= 0x40000000;
        *(int *)(param_9 + 0x34) = 0xd5;
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 0;
        entity_register_for_update(param_9, 3, -0x7fe1c598);
        if (iVar14 != 0) {
            *(int *)(iVar14 + 0xa0) = 1;
        }
        break;
    case 799:
        uVar12 = __we_subobj_pool_alloc();
        *(int *)(param_9 + 0x80) = uVar12;
        __we_subobj_build_desc(param_2, param_2, param_3, param_4, param_5, param_6, param_7,
                               param_8, *(int *)(param_9 + 0x80), (int *)param_9, 3, -1, 8.820692e-39f);
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        __we_set_render_type(param_9, (void *)&ekun_fragment_scatter);
        entity_register_for_update(param_9, 7, 0);
        break;
    case 800:
        iVar14 = *(int *)(param_9 + 0x7c);
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 1;
        *(float *)(param_9 + 0x1d8) = FLOAT_80617574;
        *(float *)(param_9 + 0x1cc) = FLOAT_80617574;
        *(float *)(param_9 + 0x1dc) = FLOAT_80617664;
        __we_set_render_type(param_9, (void *)&weapon_fire_smoke_dispatch);
        *(float *)(iVar14 + 0x10) = FLOAT_80617464;
        *(float *)(iVar14 + 0x9c) = FLOAT_80617440;
        *(int *)(iVar14 + 0xa0) = 0xf;
        *(int *)(iVar14 + 0xbc) = __we_fluid_emitter_register(param_9, 7, 0, 0x20, 10);
        break;
    case 0x321:
        if ((DAT_805015c0 & 0x40) != 0) {
            *(int **)(param_9 + 0x7c) =
                __we_model_cache_alloc(param_1, param_2, param_3, param_4, param_5, param_6,
                                       param_7, param_8, (int *)param_9, param_10, 0xc, param_12,
                                       param_13, param_14, param_15, param_16);
            *(unsigned char *)(param_9 + 0x32) = 2;
            memset_wrapper(*(int *)(param_9 + 0x7c), 0, 0xc);
            pfVar13 = *(float **)(param_9 + 0x7c);
            pfVar13[2] = 0.0f;
            pfVar13[0] = FLOAT_80617440;
            pfVar13[1] = *(float *)(param_9 + 0xc8);
        }
        break;
    case 0x322:
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 1;
        break;
    case 0x324:
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 1;
        break;
    case 0x325:
        entity_register_for_update(param_9, 0, -0x7fe1b14c);
        entity_register_for_update(param_9, 3, -0x7fe1b260);
        *(unsigned int *)(param_9 + 0x28) |= 4;
        break;
    case 0x329:
        render_instance_set_material(FLOAT_80617440, *(int **)(param_9 + 4), 1);
        entity_register_for_update(param_9, 2, -0x7fe20030);
        break;
    case 0x32a: case 0x32b:
        render_instance_set_material(FLOAT_80617440, *(int **)(param_9 + 4), 1);
        break;
    case 0x32c: case 0x32d:
        entity_register_for_update(param_9, 4, -0x7fe1ef90);
        // fallthrough
    case 0x331: case 0x332: case 0x333:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0xd0) = FLOAT_80617440;
        *(float *)(param_9 + 0x1cc) = FLOAT_80617428;
        uVar15 = (unsigned long long)(uintptr_t)&worm_ekun_spark_burst;
        __we_set_render_type(param_9, (void *)&worm_ekun_spark_burst);
        if (*(short *)(param_9 + 0x94) == 0x32d) {
            __we_ekun_spawn_reset(uVar15, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
            entity_register_for_update(param_9, 4, -0x7fe1cf6c);
            entity_register_for_update(param_9, 8, -0x7fe1b120);
            render_target_alloc_scratch(0x40, 0x20, &DAT_80610d94);
        }
        break;
    case 0x32e:
        *(float *)(param_9 + 0xd0) = FLOAT_80617440;
        *(float *)(param_9 + 0x1cc) = fVar1;
        entity_register_for_update(param_9, 4, -0x7fe1ef90);
        break;
    case 0x330:
        iVar14 = *(int *)(param_9 + 0x7c);
        *(int *)(iVar14 + 0xa0) = 1;
        *(unsigned int *)(param_9 + 0x28) |= 0x4000000;
        *(int *)(iVar14 + 0xbc) = 0;
        *(float *)(iVar14 + 0x10) = fVar3;
        entity_register_for_update(param_9, 3, -0x7fe1d1b0);
        entity_register_for_update(param_9, 8, -0x7fe1d3b8);
        break;
    case 0x334:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1cc) = fVar1;
        FUN_80369a44(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8, param_9,
                     -0x7fbada5c, 10, param_12, param_13, param_14, param_15, param_16);
        *(unsigned int *)(*(int *)(param_9 + 4) + 0x30) |= 1;
        break;
    case 0x33a:
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 1;
        break;
    case 0x33c:
        iVar14 = *(int *)(param_9 + 0x7c);
        *(int *)(iVar14 + 0xa0) = 4;
        *(float *)(iVar14 + 0x10) = fVar3;
        if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
            *(int *)(iVar14 + 0xbc) = 0x7ff;
        } else {
            *(int *)(iVar14 + 0xbc) = 0;
        }
        dVar16 = (double)FLOAT_80617440;
        *(int *)(iVar14 + 0xf0) = -1;
        *(float *)(iVar14 + 0x18) = FLOAT_80617440;
        *(float *)(iVar14 + 0x1c) = FLOAT_80617440;
        *(float *)(iVar14 + 0x14) = FLOAT_80617440;
        *(float *)(iVar14 + 0x50) = FLOAT_80617440;
        render_instance_set_material((float)dVar16, *(int **)(param_9 + 4), 1);
        entity_register_for_update(param_9, 3, -0x7fe1fb74);
        entity_register_for_update(param_9, 2, -0x7fe1f0c4);
        *(unsigned int *)(param_9 + 0x28) |= 0x4000000;
        break;
    case 0x33d:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar6;
        *(float *)(param_9 + 0x1cc) = fVar6;
        *(unsigned int *)(param_9 + 0x9c) |= 0x42;
        break;
    case 0x33e:
        *(unsigned int *)(param_9 + 0x28) |= 0x14;
        uVar12 = __we_subobj_pool_alloc();
        *(int *)(param_9 + 0x80) = uVar12;
        __we_subobj_build_desc(param_2, param_2, param_3, param_4, param_5, param_6, param_7,
                               param_8, *(int *)(param_9 + 0x80), (int *)param_9, 1, -1, 4.48416e-42f);
        __we_set_render_type(param_9, (void *)&muzzle_flash_init);
        *(float *)(param_9 + 0x1d8) = FLOAT_80617468;
        *(int *)(param_9 + 0x1cc) = *(int *)(param_9 + 0x1d8);
        break;
    case 0x33f: case 0x382:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar10;
        *(float *)(param_9 + 0x1cc) = fVar10;
        *(unsigned int *)(param_9 + 0x9c) |= 0x42;
        break;
    case 0x341:
        iVar14 = *(int *)(param_9 + 0x7c);
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar7;
        *(float *)(param_9 + 0x1cc) = fVar7;
        *(float *)(param_9 + 0x1dc) = FLOAT_80617664;
        __we_set_render_type(param_9, (void *)&weapon_smoke_tick);
        *(float *)(iVar14 + 0x10) = FLOAT_80617464;
        *(float *)(iVar14 + 0x9c) = FLOAT_80617440;
        *(int *)(iVar14 + 0xa0) = 0x14;
        *(int *)(iVar14 + 0xbc) = 0;
        break;
    case 0x342: case 0x359: case 0x35a: case 0x35d: case 0x35e: case 0x35f:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar1;
        *(float *)(param_9 + 0x1cc) = fVar1;
        *(unsigned int *)(param_9 + 0x9c) |= 0x1000000;
        break;
    case 0x344:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar11;
        *(float *)(param_9 + 0x1cc) = fVar11;
        *(unsigned int *)(param_9 + 0x9c) |= 0x42;
        break;
    case 0x345: case 0x346: case 0x347: case 0x348: case 0x349:
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 4;
        *(float *)(param_9 + 0x1cc) = fVar2;
        *(float *)(param_9 + 0x1d8) = fVar2;
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(unsigned int *)(param_9 + 0x9c) |= 0x9000018;
        entity_register_for_update(param_9, 4, -0x7fe1ef90);
        __we_set_render_type(param_9, (void *)&debris_break_tick_driver);
        break;
    case 0x34a:
        *(unsigned int *)(param_9 + 0x28) |= 0x70;
        uVar12 = __we_subobj_pool_alloc();
        *(int *)(param_9 + 0x80) = uVar12;
        __we_subobj_build_desc(param_2, param_2, param_3, param_4, param_5, param_6, param_7,
                               param_8, *(int *)(param_9 + 0x80), (int *)param_9, 2, -1, 2.9434e-39f);
        __we_set_render_type(param_9, (void *)&muzzle_flash_init);
        entity_register_for_update(param_9, 1, -0x7ff7fb7c);
        *(unsigned int *)(param_9 + 0x9c) |= 0x1000000;
        break;
    case 0x353:
        entity_register_for_update(param_9, 3, -0x7fe1b298);
        break;
    case 0x356:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar3;
        *(float *)(param_9 + 0x1cc) = fVar3;
        break;
    case 0x35b: case 0x35c: case 0x360:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar5;
        *(float *)(param_9 + 0x1cc) = fVar5;
        *(unsigned int *)(param_9 + 0x9c) |= 0x20;
        break;
    case 0x364:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar11;
        *(float *)(param_9 + 0x1cc) = fVar11;
        *(unsigned int *)(param_9 + 0x9c) |= 0x42;
        break;
    case 0x371:
        entity_register_for_update(param_9, 3, -0x7fe1d040);
        break;
    case 0x375:
        entity_register_for_update(param_9, 4, -0x7fe1b58c);
        entity_register_for_update(param_9, 2, -0x7fe1b52c);
        break;
    case 0x376:
        if (DAT_80611d74 == 0) {
            iVar14 = 0;
        } else {
            iVar14 = *(int *)(DAT_80611d74 + 0x1400);
        }
        if ((iVar14 == 0xe) || (iVar14 == 0xf)) {
            memset_wrapper(-0x7fa3b010, 0, 0x70);
            DAT_805c4ff4 = FLOAT_80617658;
            DAT_805c4ff8 = FLOAT_8061765c;
            DAT_805c4ffc = FLOAT_80617440;
            *(float *)(param_9 + 0xb8) = FLOAT_80617440;
            *(int *)(param_9 + 0x34) = 0xd5;
            *(int *)(param_9 + 0x38) = 0xd5;
            *(unsigned char *)(param_9 + 0x30) = 1;
            *(unsigned int *)(param_9 + 0x28) |= 0x40020000;
            *(int *)(param_9 + 0x1a0) = 0;
            particle_handle_clear((u32 *)&DAT_805c5044);
            entity_register_for_update(param_9, 3, -0x7fe19c38);
        } else {
            *(unsigned int *)(param_9 + 0x28) |= 0x40000070;
            *(int *)(param_9 + 0x38) = 0xd5;
            *(int *)(param_9 + 0x34) = 0xd5;
            *(int *)(param_9 + 0x1a0) = 0;
            column_effect_init();
            *(unsigned char *)(param_9 + 0x32) = 2;
            entity_register_for_update(param_9, 3, -0x7fe1aaf0);
            __we_set_render_type(param_9, (void *)&column_impact_tick);
        }
        break;
    case 0x37a:
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 3;
        *(float *)(param_9 + 0x1d8) = fVar8;
        *(float *)(param_9 + 0x1cc) = fVar8;
        *(float *)(param_9 + 0x1dc) = FLOAT_80617504;
        *(unsigned int *)(param_9 + 0x9c) |= 0x40;
        if (*(int *)(param_10 + 4) == 0x424c4b43) {
            *(unsigned int *)(param_9 + 0x28) &= 0xffffffef;
        } else {
            *(unsigned int *)(param_9 + 0x28) |= 0x10;
            if (*(int *)(param_10 + 4) == 0x424c4b48) {
                __we_set_render_type(param_9, (void *)&ekun_blkh_damage_thunk);
                *(float *)(param_9 + 0x1d8) = FLOAT_806174a0;
                *(float *)(param_9 + 0x1cc) = FLOAT_806174a0;
                *(float *)(param_9 + 0x1dc) = FLOAT_806174dc;
            } else {
                __we_set_render_type(param_9, (void *)&damage_character_receive);
            }
        }
        break;
    case 0x37e:
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 3;
        *(float *)(param_9 + 0x1d8) = fVar6;
        *(float *)(param_9 + 0x1cc) = fVar6;
        *(float *)(param_9 + 0x1dc) = FLOAT_8061749c;
        *(unsigned int *)(param_9 + 0x9c) |= 0x40;
        __we_set_render_type(param_9, (void *)&damage_character_receive);
        if (*(int *)(param_10 + 4) == 0x424c4b43) {
            *(unsigned int *)(param_9 + 0x28) &= 0xffffffef;
        } else {
            *(unsigned int *)(param_9 + 0x28) |= 0x10;
        }
        break;
    case 0x37f:
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 3;
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar7;
        *(float *)(param_9 + 0x1cc) = fVar7;
        __we_set_render_type(param_9, (void *)&ekun_fill_render_flags_tick);
        break;
    case 0x380:
        *(int *)(*(int *)(param_9 + 4) + 0x30) = 3;
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar4;
        *(float *)(param_9 + 0x1cc) = fVar4;
        __we_set_render_type(param_9, (void *)&column_break_arm);
        break;
    case 0x385:
        *(unsigned int *)(param_9 + 0x28) |= 0x10;
        *(float *)(param_9 + 0x1d8) = fVar9;
        *(float *)(param_9 + 0x1cc) = fVar9;
        *(float *)(param_9 + 0x1dc) = fVar3;
        break;
    case 0x387:
        iVar14 = *(int *)(param_9 + 0x7c);
        entity_register_for_update(param_9, 0, -0x7fe18554);
        *(unsigned int *)(param_9 + 0x28) |= 4;
        *(float *)(iVar14 + 0x98) = FLOAT_806174c4;
        break;
    case 0x388:
        entity_register_for_update(param_9, 0, -0x7fe183b4);
        entity_register_for_update(param_9, 3, -0x7fe18374);
        entity_register_for_update(param_9, 2, -0x7fe18014);
        *(unsigned int *)(param_9 + 0x28) |= 4;
        break;
    case 0x38e:
        entity_register_for_update(param_9, 3, -0x7fe17eec);
        break;
    default:
        break;
    }
}


// ---------------------------------------------------------------------------
// 0x801e8dc8  ekun_build_smoke_overlay_emitter
// ---------------------------------------------------------------------------
void ekun_build_smoke_overlay_emitter(void)
{
    double dVar1;
    int local_18 = DAT_803df894;
    int local_14 = DAT_803df898;
    int local_10 = DAT_803df89c;

    __we_def_create(FLOAT_80617500, FLOAT_80617428, &DAT_805c5300, 0, 1, 1, 0x32e, 0);
    particle_def_set_size_keyframes(FLOAT_8061745c, FLOAT_80617440, FLOAT_806174fc,
                                    FLOAT_806174fc, 0x3f4a5f00, 0x3f4a5f00, 0xa0f1e00);
    dVar1 = (double)FLOAT_80617440;
    particle_def_set_velocity_ranges(FLOAT_806174c8, (float)dVar1, (float)dVar1, (float)dVar1,
                                     FLOAT_80617428, FLOAT_80617430);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_80617440, FLOAT_80617468);
    particle_def_set_color_end(FLOAT_806174cc, FLOAT_80617440, FLOAT_80617428, FLOAT_80617440);
    particle_def_set_drag(FLOAT_80617468);
    __we_overlay_create(FLOAT_806174fc, FLOAT_80617574, FLOAT_80617514, FLOAT_8061754c,
                        FLOAT_8061754c, &DAT_805c53cc, &local_18, 0x290, 0x3c28ffff, 0x5028ff00);
    particle_overlay_set_fade_params(FLOAT_806174fc, 0, 0x1414501e);
    __we_emitter_create(FLOAT_80617670, FLOAT_80617674, FLOAT_80617678, &DAT_805c4e4c,
                        &DAT_805c5300, 0, 1);
    dVar1 = (double)FLOAT_80617440;
    particle_emitter_set_color_range((float)dVar1, (float)dVar1, FLOAT_8061757c, (float)dVar1);
    __we_emitter_set_texture(&DAT_805c53cc);
    (void)local_14; (void)local_10;
}


// ---------------------------------------------------------------------------
// 0x801e8f30  ekun_build_spark_emitter
// ---------------------------------------------------------------------------
void ekun_build_spark_emitter(void)
{
    __we_def_create(FLOAT_80617468, FLOAT_80617428, &DAT_805c5404, 0, 0, 0x11, 0x34e, 0);
    particle_def_set_size_keyframes(FLOAT_80617428, FLOAT_80617440, FLOAT_80617440,
                                    FLOAT_8061748c, 0x404060ff, 0x606070ff, 0x101010ff);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_806175b4, FLOAT_806175a8, FLOAT_8061761c, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_8061767c, FLOAT_80617474, FLOAT_80617680,
                                     FLOAT_80617474, FLOAT_80617428, FLOAT_80617464);
    __we_emitter_create(FLOAT_80617430, FLOAT_80617440, FLOAT_80617460, &DAT_805c527c,
                        &DAT_805c5404, 0, 0);
    particle_emitter_set_color_range(FLOAT_8061757c, FLOAT_80617440, FLOAT_80617684, FLOAT_80617440);
    particle_emitter_set_lifetime_range(FLOAT_80617504, FLOAT_80617430);
}


// ---------------------------------------------------------------------------
// 0x801e902c  ekun_init_particle_bank
// ---------------------------------------------------------------------------
void ekun_init_particle_bank(void)
{
    double dVar1;
    int local_58, local_54, local_50, local_4c;
    int local_48 = DAT_803df8c4;
    int local_44 = DAT_803df8c8;
    int local_40 = DAT_803df8cc;
    float local_3c = *(float *)&DAT_803df8b8;
    int local_38 = DAT_803df8bc;
    int local_34 = DAT_803df8c0;
    float local_30 = *(float *)&DAT_803df8ac;
    int local_2c = DAT_803df8b0;
    int local_28 = DAT_803df8b4;
    int local_24 = DAT_803df8a0;
    int local_20 = DAT_803df8a4;
    int local_1c = DAT_803df8a8;

    __save_gpr_28();
    particle_handle_clear((u32 *)&DAT_80612710);
    particle_handle_clear((u32 *)&DAT_80612718);
    particle_handle_clear((u32 *)&DAT_80612714);
    particle_handle_clear((u32 *)&DAT_8061270c);

    // group 1: light dust
    __we_def_create(FLOAT_806174bc, FLOAT_80617428, &DAT_805c54d0, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_80617428, FLOAT_80617440, FLOAT_80617440,
                                    FLOAT_8061754c, 0x404040ff, 0x101010ff, 0);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_80617460, FLOAT_80617614, FLOAT_806174bc, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_8061749c, FLOAT_80617688, FLOAT_80617504,
                                     FLOAT_806174fc, FLOAT_80617428, FLOAT_80617504);
    particle_def_set_drag(FLOAT_8061750c);

    // group 2: dark dust
    __we_def_create(FLOAT_806174bc, FLOAT_80617428, &DAT_805c559c, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_80617428, FLOAT_80617440, FLOAT_80617440,
                                    FLOAT_8061754c, 0xffffffff, 0x202020ff, 0);
    particle_def_set_gravity(FLOAT_80617508, 0xffa00000);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_80617558, FLOAT_80617614, FLOAT_806174bc, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_8061749c, FLOAT_80617688, FLOAT_80617504,
                                     FLOAT_806174fc, FLOAT_80617428, FLOAT_80617504);
    particle_def_set_drag(FLOAT_8061750c);

    __we_emitter_create(FLOAT_806174bc, FLOAT_80617440, FLOAT_80617450, &DAT_805c5668,
                        &DAT_805c54d0, 0, 6);
    particle_emitter_set_color_range(FLOAT_806174dc, FLOAT_80617440, FLOAT_8061750c, FLOAT_80617440);
    particle_emitter_set_lifetime_range(FLOAT_806174dc, FLOAT_80617440);
    __we_emitter_create(FLOAT_806174bc, FLOAT_80617440, FLOAT_80617450, &DAT_805c56ec,
                        &DAT_805c559c, 0, 6);
    particle_emitter_set_color_range(FLOAT_806174dc, FLOAT_80617440, FLOAT_8061750c, FLOAT_80617440);
    particle_emitter_set_lifetime_range(FLOAT_806174dc, FLOAT_80617440);

    // group 3: heavy smoke
    __we_def_create(FLOAT_806174bc, FLOAT_80617428, &DAT_805c5770, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_80617428, FLOAT_80617440, FLOAT_80617440,
                                    FLOAT_8061754c, 0x909090ff, 0x808080ff, 0);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_80617558, FLOAT_80617614, FLOAT_80617504, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_80617430, FLOAT_80617688, FLOAT_80617428,
                                     FLOAT_806174fc, FLOAT_80617428, FLOAT_80617504);
    particle_def_set_drag(FLOAT_806174c4);
    __we_emitter_create(FLOAT_80617430, FLOAT_80617440, FLOAT_80617428, &DAT_805c583c,
                        &DAT_805c5770, 2, 0);
    particle_emitter_set_color_range(FLOAT_80617428, FLOAT_80617440, FLOAT_8061768c, FLOAT_80617440);
    particle_emitter_set_lifetime_range(FLOAT_80617428, FLOAT_80617440);

    __we_emitter_set_scale(&local_24);
    particle_emitter_spawn((unsigned int)(uintptr_t)&DAT_80612718, (unsigned int)(uintptr_t)&DAT_805c5668,
                           0, &local_30, &local_3c);
    local_4c = DAT_80612718;
    particle_cached_disable((ParticleEmitter **)&local_4c);
    particle_emitter_spawn((unsigned int)(uintptr_t)&DAT_80612714, (unsigned int)(uintptr_t)&DAT_805c583c,
                           0, &local_30, &local_3c);
    local_50 = DAT_80612714;
    particle_cached_disable((ParticleEmitter **)&local_50);
    particle_emitter_spawn((unsigned int)(uintptr_t)&DAT_80612710, (unsigned int)(uintptr_t)&DAT_805c56ec,
                           0, &local_30, &local_3c);
    local_54 = DAT_80612710;
    particle_cached_disable((ParticleEmitter **)&local_54);

    // group 4: blue spark cloud
    __we_def_create(FLOAT_806174c4, FLOAT_80617428, &DAT_805c58c0, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_8061748c, FLOAT_80617440, FLOAT_806174fc,
                                    FLOAT_8061754c, 0x3f00ff, 0x3e00ff, 0x2f00ff);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_80617558, FLOAT_80617614, FLOAT_80617498, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_80617428, FLOAT_80617688, FLOAT_8061766c,
                                     FLOAT_806174fc, FLOAT_80617428, FLOAT_8061766c);
    particle_def_set_drag(FLOAT_80617514);
    __we_emitter_create(FLOAT_80617428, FLOAT_80617440, FLOAT_8061745c, &DAT_805c4ed0,
                        &DAT_805c58c0, 0, 0x2000);
    particle_emitter_set_color_range(FLOAT_80617690, FLOAT_80617440, FLOAT_806174c4, FLOAT_80617440);
    particle_emitter_set_lifetime_range(FLOAT_80617690, FLOAT_80617440);

    // group 5: falling dark dust
    __we_def_create(FLOAT_80617428, FLOAT_80617428, &DAT_805c598c, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_80617428, FLOAT_80617440, FLOAT_80617440,
                                    FLOAT_8061754c, 0xffffffff, 0x202020ff, 0);
    particle_def_set_gravity(FLOAT_80617508, 0xffa00000);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_80617558, FLOAT_80617614, FLOAT_80617694, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_80617554, FLOAT_80617470, FLOAT_80617430,
                                     FLOAT_806174fc, FLOAT_80617428, FLOAT_80617504);
    particle_def_set_drag(FLOAT_806174c4);
    __we_emitter_create(FLOAT_80617428, FLOAT_80617440, FLOAT_806174fc, &DAT_805c5a58,
                        &DAT_805c598c, 0, 6);
    particle_emitter_set_color_range(FLOAT_806174dc, FLOAT_80617440, FLOAT_8061750c, FLOAT_80617440);
    particle_emitter_set_lifetime_range(FLOAT_806174dc, FLOAT_80617440);
    particle_emitter_spawn((unsigned int)(uintptr_t)&DAT_80612708, (unsigned int)(uintptr_t)&DAT_805c5a58,
                           0x1000, &local_30, &local_3c);
    local_58 = DAT_80612708;
    particle_cached_disable((ParticleEmitter **)&local_58);

    // group 6: light+dark impact dust pair
    __we_def_create(FLOAT_8061749c, FLOAT_80617428, &DAT_805c5adc, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_80617428, FLOAT_80617440, FLOAT_80617440,
                                    FLOAT_8061748c, 0x404040ff, 0x101010ff, 0);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_806175b4, FLOAT_80617614, FLOAT_806175a8, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_80617428, FLOAT_80617688, FLOAT_80617468,
                                     FLOAT_806174fc, FLOAT_80617428, FLOAT_80617504);
    particle_def_set_drag(FLOAT_80617514);
    __we_def_create(FLOAT_80617628, FLOAT_80617428, &DAT_805c5ba8, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_80617428, FLOAT_80617440, FLOAT_80617440,
                                    FLOAT_806174cc, 0x101010ff, 0x404040ff, 0);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_806175b4, FLOAT_80617614, FLOAT_8061746c, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_80617428, FLOAT_80617688, FLOAT_80617698,
                                     FLOAT_806174fc, FLOAT_80617428, FLOAT_80617504);
    particle_def_set_drag(FLOAT_80617514);
    __we_emitter_create(FLOAT_80617430, FLOAT_80617440, FLOAT_80617460, &DAT_805c50f0,
                        &DAT_805c5adc, 0, 0x4000);
    particle_emitter_set_color_range(FLOAT_806174dc, FLOAT_80617440, FLOAT_80617504, FLOAT_80617440);
    particle_emitter_set_lifetime_range(FLOAT_806174dc, FLOAT_80617440);
    __we_emitter_create(FLOAT_80617504, FLOAT_80617440, FLOAT_80617460, &DAT_805c506c,
                        &DAT_805c5ba8, 0, 0x4000);
    particle_emitter_set_color_range(FLOAT_806174dc, FLOAT_80617440, FLOAT_80617620, FLOAT_80617440);
    particle_emitter_set_lifetime_range(FLOAT_806174dc, FLOAT_80617440);

    // group 7: secondary impact dust
    __we_def_create(FLOAT_80617628, FLOAT_80617428, &DAT_805c5c74, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_80617428, FLOAT_80617440, FLOAT_80617440,
                                    FLOAT_806174cc, 0x101010ff, 0x404040ff, 0);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_806175b4, FLOAT_80617614, FLOAT_8061746c, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_80617504, FLOAT_80617688, FLOAT_806174dc,
                                     FLOAT_806174fc, FLOAT_80617428, FLOAT_80617504);
    particle_def_set_drag(FLOAT_80617514);
    __we_emitter_create(FLOAT_80617504, FLOAT_80617440, FLOAT_80617460, &DAT_805c5174,
                        &DAT_805c5c74, 0, 0x4800);
    particle_emitter_set_color_range(FLOAT_8061757c, FLOAT_80617440, FLOAT_8061769c, FLOAT_80617440);

    ekun_build_spark_emitter();

    // group 8: overlay flash + smoke emitter
    __we_def_create(FLOAT_806176a0, FLOAT_80617428, &DAT_805c5d40, 0, 0, 0x11, 0x34e, 0);
    particle_def_set_size_keyframes(FLOAT_80617428, FLOAT_80617440, FLOAT_80617440,
                                    FLOAT_8061748c, 0x404060ff, 0x606070ff, 0x101010ff);
    particle_def_set_size_start_end(FLOAT_8061762c, FLOAT_80617634, FLOAT_806174fc, FLOAT_806174fc);
    particle_def_set_color_end(FLOAT_806175b4, FLOAT_806175a8, FLOAT_806176a4, FLOAT_80617440);
    particle_def_set_velocity_ranges(FLOAT_80617464, FLOAT_80617474, FLOAT_806174fc,
                                     FLOAT_80617474, FLOAT_80617428, FLOAT_80617508);
    __we_emitter_create(FLOAT_8061745c, FLOAT_80617440, FLOAT_80617678, &DAT_805c51f8,
                        &DAT_805c5d40, 0, 0x800);
    particle_emitter_set_color_range(FLOAT_80617628, FLOAT_80617440, FLOAT_806176a8, FLOAT_80617440);
    __we_overlay_create(FLOAT_8061754c, FLOAT_80617574, FLOAT_80617514, FLOAT_8061754c,
                        FLOAT_80617440, &DAT_805c5e0c, &local_48, 0x290, 0xffffffff, 0);
    __we_emitter_create(FLOAT_80617670, FLOAT_80617674, FLOAT_80617678, &DAT_805c5e44,
                        &DAT_805c5300, 0, 1);
    dVar1 = (double)FLOAT_80617440;
    particle_emitter_set_color_range((float)dVar1, (float)dVar1, FLOAT_80617428, (float)dVar1);
    __we_emitter_set_texture(&DAT_805c5e0c);

    ekun_build_smoke_overlay_emitter();
    __restore_gpr_28();

    (void)local_1c; (void)local_20; (void)local_28; (void)local_2c;
    (void)local_34; (void)local_38; (void)local_40; (void)local_44;
}
