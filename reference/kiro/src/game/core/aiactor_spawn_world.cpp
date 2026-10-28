// Address range: 0x8013a3e8 - 0x8013aa4b | batch_401 | 2026-09-05
//
// AI actor spawn-into-world routine + a small shared constant getter.
// Continuation of the aiactor family (batches 364-400). aiactor_spawn_into_world
// is the routine that finishes bringing a bot/story actor into the live world
// once its spawn record has been chosen: it snapshots the actor's flag state,
// (re)loads the character definition, applies damage filters, resolves the
// spawn zone/node, drops the actor onto the floor via the nav/floor query, and
// then either starts the death/behaviour phase (spawn succeeded) or destroys the
// instance (no floor found).
//
// Actor object layout used here (entity = FUN_803826e8() scratch actor,
// render_data = entity+0x7c):
//   entity+0x64/68/6c  world position (x,y,z)     entity+0x70  heading yaw
//   entity+0xc4        body yaw                    entity+0x1cc/0x1d8 splash radius
//   entity+0xec/f0/f4  previous position           entity+0x28  render flags
//   render_data+0x10/0x14   state flag pair (lo/hi)
//   render_data+0xaa8/0xaac state snapshot         render_data+0xab8/0xabc ..
//   render_data+0xac0/0xac4 masked-out flag save   render_data+0xbf0 char def ptr
//   render_data+0x48 (short) char id * 0x30 -> DAT_8048ca94/9c move-param table
//   render_data+0xb70 floor height                 render_data+0xbf0+0x54 def flags
//
// Wide float ABI: the double/long-long params are pass-through scratch regs
// threaded to the character/anim helpers that expect them.

#include "common.h"

// ---- helpers (self-externed at call-site arity; generated_decls.h is not
//      included by common.h, so every callee is declared here) --------------
extern "C" {
extern int  FUN_803826e8(void);   // fp scratch frame enter -> current actor entity
extern void FUN_80382734(void);   // fp scratch frame leave
extern void FUN_8023c8a0(int charDefField8, int entity);  // bind char def -> actor
extern void FUN_8023c990(void);                            // finalize char-def bind
extern int  FUN_8022277c(void);                            // char-def post-load hook
extern void FUN_8025c3e0(void);                            // post-spawn combat init

// named cross-module helpers (declared at the arity/return this file uses)
extern int   aiactor_acquire_nearest_target(int actor, int preferred, int mode);
extern int   aiactor_visible_list_add(int actor);          // returns actor scratch reg
extern void  aiactor_destroy_instance(unsigned long long r, double a, double b, double c,
                                       unsigned long long e, unsigned long long f,
                                       unsigned long long g, unsigned long long h, int actor);
extern void  aiactor_init_from_char_def(unsigned long long r, double a, double b, double c,
                                        unsigned long long e, unsigned long long f,
                                        unsigned long long g, unsigned long long h);
extern void  aiactor_apply_damage_filters(int actor);
extern int*  aiactor_resolve_zone_or_node(int rd);
extern int   aiactor_update_zone_anchor_position(void);
extern void  aiactor_turn_toward_leader(void);
extern void  aiactor_run_behaviour_phase(int entity, int phase, int* ctx, int arg);
extern int   aiactor_alert_group_flagA_any(u32 mask);
extern u32   spawn_check_flags_with_unlock(u32 flags, u32 unlockBits);
extern int*  switch_find_slot_by_entity(int entityId);
extern void  nav_movement_dispatch(double px, double pz, int navState, int fromState,
                                    float* target, float* outPos, int* outCol, int enableScene,
                                    unsigned int flags);
extern float floor_query_with_interact(double px, double pz, int posPtr, int floorObj,
                                        int* outInteract, int skip);
extern float damage_splash_level_gate(void);
extern double damage_get_max_radius(void);
extern void  entity_assign_room(int entity);
extern void  character_death_start(unsigned long long r, double a, double b, double c,
                                   unsigned long long e, unsigned long long f,
                                   unsigned long long g, unsigned long long h);
extern void  character_damage_effect_wrapper(void);
}

// ---- raw data symbols ------------------------------------------------------
extern char  DAT_805015dc;    // "spawn deferred" gate byte
extern int   DAT_80611d78;    // coop leader ref object
extern int   DAT_8048ca94;    // per-char move param table (x), 0x30 stride
extern int   DAT_8048ca9c;    // per-char move param table (z), 0x30 stride

extern float FLOAT_80615cac;  // 0.0
extern float FLOAT_80615cdc;  // ground/step offset constant
extern float FLOAT_80615d24;  // reacquire/best-distance init value
extern float FLOAT_80615d48;  // minimum valid floor height

// ---------------------------------------------------------------------------
// aiactor_get_step_offset (0x8013a3e8)
// Tiny leaf getter returning the shared ground/step offset constant. Used by
// physics/combat code (callers at 0x802b48xx / 0x802b90xx) that need the same
// vertical step delta the actor movement code applies.
// ---------------------------------------------------------------------------
double aiactor_get_step_offset(void)
{
    return (double)FLOAT_80615cdc;
}

// ---------------------------------------------------------------------------
// aiactor_spawn_into_world (0x8013a3f0)
// Finish spawning the current scratch actor into the live world. Guarded by the
// "spawn deferred" byte (DAT_805015dc) unless the actor has the +0xa60 override
// set. On the spawn path: snapshot flags, (re)load the char definition from
// render_data+0xbf0, acquire a coop leader target if none, run the unlock/flag
// gate (spawn_check_flags_with_unlock) - destroying the instance if it fails -
// then aiactor_init_from_char_def, resolve the switch-linked activation slot,
// scrub the transient 0x100000/0x800000/0x10000000 flag bits into their save
// slots, apply damage filters, resolve the spawn zone/node position, snap onto
// the floor (nav_movement_dispatch + floor_query_with_interact), and if the
// floor is valid start the death/behaviour phase (character_death_start +
// aiactor_run_behaviour_phase, state set to 2). If no valid floor, destroy the
// instance.
void aiactor_spawn_into_world(unsigned long long param_1, double param_2, double param_3,
                              double param_4, unsigned long long param_5, unsigned long long param_6,
                              unsigned long long param_7, unsigned long long param_8)
{
    float fVar1;
    int entity;
    u32 uVar2;
    u32 uVar3;
    u32* puVar4;
    int* piVar5;
    int rd;
    int def;
    unsigned long long uVar8;
    double dVar9;
    double dVar10;
    float local_38;
    u32 local_34;
    u32 local_30;
    float local_2c;
    u32 local_28;
    u32 local_24;

    entity = FUN_803826e8();
    rd = *(int*)(entity + 0x7c);

    if ((DAT_805015dc == 0) || (*(short*)(rd + 0xa60) != 0)) {
        *(u32*)(rd + 0xaac) = *(u32*)(rd + 0x14);
        *(u32*)(rd + 0xaa8) = *(u32*)(rd + 0x10);
        def = *(int*)(rd + 0xbf0);
        if (def != 0) {
            FUN_8023c8a0(*(int*)(def + 8), entity);
            FUN_8023c990();
            *(u32*)(rd + 0x74) = *(u32*)(*(int*)(rd + 0xbf0) + 0x7c);
            if (*(int*)(rd + 4) == 0) {
                *(int*)(rd + 4) =
                    aiactor_acquire_nearest_target(entity, *(int*)(DAT_80611d78 + 0x14), 0);
            }
            uVar8 = (unsigned long long)FUN_8022277c();
            uVar3 = spawn_check_flags_with_unlock(*(u32*)(def + 0xc), *(u32*)(def + 0x2c));
            if (uVar3 == 0) {
                uVar8 = aiactor_visible_list_add(entity);
                aiactor_destroy_instance(uVar8, param_2, param_3, param_4,
                                         param_5, param_6, param_7, param_8, entity);
                goto done;
            }
            aiactor_init_from_char_def(uVar8, param_2, param_3, param_4,
                                       param_5, param_6, param_7, param_8);
            if (((*(u32*)(*(int*)(rd + 0xbf0) + 0x54) & 8) != 0) ||
                ((*(u32*)(rd + 0x10) & 0x4000000) != 0)) {
                puVar4 = (u32*)switch_find_slot_by_entity(*(int*)(def + 0x74));
                *(u32*)(rd + 0xae8) = puVar4[2];
            }
            uVar3 = *(u32*)(rd + 0x14);
            if ((uVar3 & 0x800000) == 0) {
                if ((uVar3 & 0x100000) != 0) {
                    *(u32*)(rd + 0xac4) = uVar3 & 0xefefffff;
                    *(u32*)(rd + 0xac0) = *(u32*)(rd + 0x10);
                    *(u32*)(rd + 0x14) = *(u32*)(rd + 0x14) & ~*(u32*)(rd + 0xac4);
                    *(u32*)(rd + 0x10) = *(u32*)(rd + 0x10) & ~*(u32*)(rd + 0xac0);
                }
            }
            else {
                *(u32*)(rd + 0xac4) = uVar3 & 0xef7fffff;
                *(u32*)(rd + 0xac0) = *(u32*)(rd + 0x10);
                *(u32*)(rd + 0x14) = *(u32*)(rd + 0x14) & ~*(u32*)(rd + 0xac4);
                *(u32*)(rd + 0x10) = *(u32*)(rd + 0x10) & ~*(u32*)(rd + 0xac0);
            }
            if ((*(u32*)(rd + 0x14) & 0x10000000) == 0) {
                *(u32*)(rd + 0xabc) = *(u32*)(rd + 0x14);
                *(u32*)(rd + 0xab8) = *(u32*)(rd + 0x10);
                *(u32*)(rd + 0x14) = 0x4000000;
                *(u32*)(rd + 0x10) = 0;
                *(u32*)(rd + 0xaac) = *(u32*)(rd + 0x14);
                *(u32*)(rd + 0xaa8) = *(u32*)(rd + 0x10);
            }
            else {
                *(u32*)(rd + 0x14) = *(u32*)(rd + 0x14) & 0xefffffff;
                *(u32*)(rd + 0x10) = *(u32*)(rd + 0x10);
                *(u32*)(rd + 0xabc) = 0;
                *(u32*)(rd + 0xab8) = 0;
                *(u32*)(rd + 0xaac) = *(u32*)(rd + 0x14);
                *(u32*)(rd + 0xaa8) = *(u32*)(rd + 0x10);
            }
            if (*(int*)(rd + 0x14) == 0 && *(int*)(rd + 0x10) == 0) {
                *(u32*)(rd + 0x14) = 4;
                *(u32*)(rd + 0x10) = 0;
            }
            aiactor_apply_damage_filters(entity);
        }

        *(u32*)(rd + 0x24) = 1;
        dVar9 = (double)damage_splash_level_gate();
        *(float*)(entity + 0x1d8) = (float)dVar9;
        *(u32*)(entity + 0x1cc) = *(u32*)(entity + 0x1d8);
        dVar9 = damage_get_max_radius();
        *(float*)(rd + 0x7c) = (float)dVar9;
        fVar1 = FLOAT_80615d24;
        *(float*)(rd + 0x78) = FLOAT_80615cac;
        *(u32*)(rd + 0x9c0) = 0;
        *(u32*)(rd + 0xad0) = *(u32*)(rd + 0xb14);
        *(float*)(rd + 0xb70) = fVar1;

        piVar5 = aiactor_resolve_zone_or_node(rd);
        *(int*)(entity + 0x64) = piVar5[3];
        *(int*)(entity + 0x68) = piVar5[4];
        *(int*)(entity + 0x6c) = piVar5[5];
        *(int*)(entity + 0x70) = piVar5[6];
        *(u32*)(entity + 0xc4) = *(u32*)(entity + 0x70);
        aiactor_update_zone_anchor_position();

        local_2c = *(float*)(entity + 0x64);
        local_28 = *(u32*)(entity + 0x68);
        local_24 = *(u32*)(entity + 0x6c);
        {
            int mp = *(short*)(rd + 0x48) * 0x30;
            nav_movement_dispatch((double)*(float*)(&DAT_8048ca94 + mp),
                                  (double)*(float*)(&DAT_8048ca9c + mp),
                                  *(int*)(rd + 8), entity + 0x64, &local_2c, &local_38,
                                  (int*)0, 0, 0);
            mp = *(short*)(rd + 0x48) * 0x30;
            nav_movement_dispatch((double)*(float*)(&DAT_8048ca94 + mp),
                                  (double)*(float*)(&DAT_8048ca9c + mp),
                                  *(int*)(rd + 8), (int)&local_2c, &local_38,
                                  (float*)0, (int*)0, 0, 0);
        }
        *(float*)(entity + 0x64) = local_38;
        *(u32*)(entity + 0x68) = local_34;
        *(u32*)(entity + 0x6c) = local_30;
        {
            int mp = *(short*)(rd + 0x48) * 0x30;
            dVar10 = (double)*(float*)(&DAT_8048ca9c + mp);
            dVar9 = (double)floor_query_with_interact((double)*(float*)(&DAT_8048ca94 + mp), dVar10,
                                                      entity + 0x64, rd + 0xc00,
                                                      (int*)(rd + 0xc04), 0);
        }
        *(float*)(rd + 0xb70) = (float)dVar9;
        fVar1 = FLOAT_80615d48;
        *(float*)(rd + 0xbfc) = (float)dVar9;
        *(u32*)(rd + 0xbc0) = *(u32*)(rd + 0xb70);
        *(u32*)(entity + 0xec) = *(u32*)(entity + 0x64);
        *(u32*)(entity + 0xf0) = *(u32*)(entity + 0x68);
        *(u32*)(entity + 0xf4) = *(u32*)(entity + 0x6c);
        *(u32*)(rd + 0xaf8) = *(u32*)(entity + 0x64);
        *(u32*)(rd + 0xafc) = *(u32*)(entity + 0x68);
        *(u32*)(rd + 0xb00) = *(u32*)(entity + 0x6c);

        if (fVar1 <= *(float*)(rd + 0xb70)) {
            if (((*(u32*)(*(int*)(rd + 0xbf0) + 0x54) & 0x100) != 0) && (*(int*)(rd + 4) != 0)) {
                aiactor_turn_toward_leader();
            }
            *(u32*)(entity + 0x28) = *(u32*)(entity + 0x28) & 0xfffffffe;
            *(u32*)(rd + 0xba0) = *(u32*)(entity + 0x64);
            *(u32*)(rd + 0xba4) = *(u32*)(entity + 0x68);
            *(u32*)(rd + 0xba8) = *(u32*)(entity + 0x6c);
            if ((piVar5[1] & 0x800) == 0) {
                piVar5 = (int*)0;
            }
            *(int**)(rd + 0xacc) = piVar5;
            fVar1 = FLOAT_80615cac;
            *(u32*)(rd + 0xab4) = 0;
            *(u32*)(rd + 0xab0) = 0;
            *(u32*)(rd + 0x9d0) = 0;
            *(u32*)(rd + 0x20) = 0;
            *(u32*)(rd + 0xadc) = 0;
            *(u32*)(rd + 0xbac) = 0;
            *(float*)(rd + 0xb38) = fVar1;
            *(u32*)(rd + 0xc64) = 0;
            entity_assign_room(entity);
            uVar8 = aiactor_update_zone_anchor_position();
            if (*(char*)(entity + 0x30) == '\0') {
                *(u32*)(entity + 0x34) = 0xd5;
            }
            *(u32*)(rd + 0x28) = *(u32*)(rd + 0x28) & 0xfffffeff;
            *(u32*)(rd + 0x2c) = *(u32*)(rd + 0x2c) & 0xfffbffff;
            if ((*(u32*)(rd + 0x14) & 0x4000000) == 0) {
                uVar8 = aiactor_visible_list_add(entity);
            }
            character_death_start(uVar8, dVar10, param_3, param_4, param_5, param_6, param_7, param_8);
            if ((*(u32*)(rd + 0x28) & 0x10000000) == 0) {
                *(u32*)(rd + 0xc6c) = *(u32*)(rd + 0xc6c) & 0xfffffffb;
            }
            else {
                character_damage_effect_wrapper();
            }
            if (((*(u32*)(rd + 0x14) & 0x1000000) != 0) &&
                (aiactor_alert_group_flagA_any(*(u32*)(rd + 0xbec)) != 0)) {
                *(u32*)(rd + 0x28) = *(u32*)(rd + 0x28) | 0x400;
            }
            FUN_8025c3e0();
            aiactor_run_behaviour_phase(entity, 6, (int*)0, 0);
            *(u32*)(rd + 0xaac) = *(u32*)(rd + 0x14);
            *(u32*)(rd + 0xaa8) = *(u32*)(rd + 0x10);
            *(u32*)(rd + 0x14) = 2;
            *(u32*)(rd + 0x10) = 0;
        }
        else {
            uVar8 = aiactor_visible_list_add(entity);
            aiactor_destroy_instance(uVar8, dVar10, param_3, param_4,
                                     param_5, param_6, param_7, param_8, entity);
        }
    }
    else {
        *(u32*)(entity + 0x28) = *(u32*)(entity + 0x28) & 0xffffff9f;
    }
done:
    FUN_80382734();
    return;
}
