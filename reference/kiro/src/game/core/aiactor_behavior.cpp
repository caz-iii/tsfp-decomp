// Address range: 0x8025d7a0 - 0x802603a3 | batch_739 | 2026-09-10
//
// AI actor per-behaviour-state tick cluster. Each of the FSM-state ticks below
// is dispatched once per frame from the AI behaviour driver (FUN_80281da4 /
// FUN_80282570) keyed on the actor's current behaviour-state field
// (actorState = *(entity + 0x7c); state id at actorState + 0x20). They share a
// common prologue that resolves the scratch frame (FUN_803826f0 / _e8 / _c8
// return the entity handle in the frame), applies any queued state swap
// (+0x9d0 pending / +0x9d4 previous / +0x9d8 blend-timer), and advances the
// locomotion timer (+0xa90) while flag +0x2c bit0x80 is set.
//
// Per-actor state block (actorState = entity + 0x7c) fields touched here:
//   +0x04 current target/leader entity      +0x08 owner short id
//   +0x14/+0x1c per-frame input masks (coop DAT_805015bd=='\n' vs vs)
//   +0x20 behaviour-state id                +0x24 stance-flag word
//   +0x28 status flags word                 +0x2c capability flags word
//   +0x44 mount/vehicle type                +0x48/+0x4a weapon/char type shorts
//   +0x30..+0x38 nav target xyz             +0x684 stored target zone id
//   +0x9d0/+0x9d4/+0x9d8 pending-state swap trio
//   +0xa90 locomotion timer                 +0xaa8/+0xaac stance-request pair
//   +0xab8/+0xabc queued-action pair        +0xae8 mounted-target entity
//   +0xb30/+0xb34/+0xb4c behaviour timers   +0xbf0 char-def block  +0xbec alert key
//
// FUN_8025ff4c = aiactor_alert_broadcast_group (mask-indexed zone-alert table
// DAT_80587e10 stride 0x60, up to 0x14 buckets). Already referenced by many
// named callers (switch_object_set_state, worm_event_execute,
// aiactor_combat_react_to_target) so kept as extern where cross-file.
//
// Functions:
//   aiactor_behavior_follow_tick   0x8025d7a0  escort/follow state machine
//   aiactor_behavior_guard_tick    0x8025e210  guard/hold + floor-query stance
//   aiactor_behavior_patrol_tick   0x8025e994  patrol/idle w/ rng idle anims
//   aiactor_request_take_cover     0x8025ee20  bool: queue take-cover action
//   aiactor_behavior_combat_tick   0x8025ef34  8-state approach/strafe/retreat FSM
//   aiactor_behavior_fire_gate     0x8025f5f8  stance-4 gate into fire helper
//   aiactor_commit_pending_action  0x8025f674  commit queued action pair
//   aiactor_behavior_engage_tick   0x8025f89c  engage/attack: LOS + vehicle + fire
//   aiactor_play_attack_anim       0x8025fc38  select & play attack/melee anim
//   aiactor_alert_broadcast_group  0x8025ff4c  broadcast alert to zone group

#include "common.h"

// ---- forward decls: mount seat-command engine (defined below in batch_746) --
// Placed here so earlier-batch callers (which formerly used the raw FUN_ names)
// resolve to these definitions.
void   aiactor_mount_command_clip_tick(void);
void   aiactor_mount_command_advance(void);
void   aiactor_mount_command_nav_tick(void);
void   aiactor_mount_command_play_clip(int frame, int cmdRec);
int    aiactor_mount_command_clip_active(void);
int    aiactor_mount_attack_clip_active(void);
int    aiactor_mount_turn_clip_active(void);
double aiactor_mount_clip_aim_offset(int frame);
void   aiactor_mount_steer_to_path_node(void);
double aiactor_mount_command_reach_dist(int opcode, int actorState);

// ---- forward decls: mount command-support helpers (defined below, batch_747)
int    aiactor_mount_command_precondition(int entity, int opcode);
void   aiactor_mount_command_complete(void);
void   aiactor_mount_scan_alert_target(void);
int    aiactor_mount_next_command_opcode(int entity, int cmdRec);
int    aiactor_mount_next_command_record(int entity, int cmdRec);
void   aiactor_mount_retarget_nearest_command(void);

// ---- scratch-frame prologue/epilogue helpers (fp/gpr save-restore) ---------
extern "C" {
extern int      __save_gpr_28(void);
extern void     __restore_gpr_28(void);
extern int      __save_gpr_25(void);
extern void     __restore_gpr_25(void);
extern int      FUN_803826f0(void);      // resolve entity handle (frame variant a)
extern int      FUN_803826e8(void);      // resolve entity handle (frame variant b)
extern longlong FUN_803826c8(void);      // resolve entity + aux (packed)
extern void     FUN_8038273c(void);      // epilogue a
extern void     FUN_80382734(void);      // epilogue b
extern void     FUN_80382714(void);      // epilogue c

// ---- named cross-module AI helpers -----------------------------------------
extern int  aiactor_retarget_visible_teammate(void);
extern int  aiactor_resolve_target_zone_id(int frame);
extern int  aiactor_follow_zone_path(void);
extern void aiactor_turn_toward_leader(void);
extern void aiactor_turn_toward_angle(double angle, int entity, int instant);
extern void aiactor_facing_update(void);
extern void aiactor_leader_zone_update(int entity);
extern void aiactor_set_stance_flags(int entity, int flags);
extern int  aiactor_acquire_nearest_target(int entity, int actorTable, int flag);
extern int  aiactor_evaluate_target_los(void);
extern int  aiactor_alert_group_flagA_any(unsigned int alertKey);
extern void aiactor_alert_group_remove(int entity, unsigned int mask);
extern int  aiactor_relation_entry(int a, int b);
extern int  aiactor_aim_alignment_test(double tol, int entity, float* targetPos);
extern int  player_ai_is_ranged_weapon(int weaponType);
extern int  player_ai_is_valid_weapon_type(int weaponType);
extern int  entity_handle_resolve(unsigned int handle);
extern float floor_query_with_interact(double x, double z, int entity, int a, int* out, int b);
extern int  soundpad_find_zone_by_id(int zoneId);
extern void switch_object_toggle_by_mask(void);

// ---- anim helpers ----------------------------------------------------------
extern int  anim_is_clip_active(int entity, int clipId);
extern int  anim_play_scaled(double startPhase, double rate);
extern void anim_set_playback_rate(double rate, int* ctrl, int slot);
extern int  anim_primary_clip_userflags(int entity, int clipId);
extern int  anim_active_slot_userflags(int ctrl);
extern int  anim_clip_has_flags(int entity, int clipId, int a, int mask);
extern void anim_sample_keyframe(int desc, int a, int b, float* out);
extern unsigned int rng_counter_tick(void);

// ---- raw FUN_ helpers referenced at these call sites -----------------------
extern void FUN_80285528(int frame);
extern void FUN_8028a3cc(int frame);
extern void FUN_80280c8c(int frame);
extern void FUN_80260dd0(int a, int b);
extern int  FUN_802a6690(void);
extern int  FUN_80281ce8(void);
extern int  prop_hinge_player_push_tick(void);

// ---- forward decls for the intra-file dispatch chain -----------------------
int  aiactor_request_take_cover(int actorState);
void aiactor_commit_pending_action(int entity);
void aiactor_behavior_fire_gate(int entity);
void aiactor_behavior_engage_tick(void);
void aiactor_play_attack_anim(int entity);
void aiactor_alert_broadcast_group(unsigned int param_1, int entity, int zoneId);

// ---- raw data symbols ------------------------------------------------------
extern char  DAT_805015bd;               // game mode byte (0x0a = coop)
extern unsigned int DAT_805015c0;
extern int   DAT_80611d78;               // actor table base
extern int   DAT_80611d90;               // player state pool base
extern int   DAT_806120f8;               // local player short id
extern int   DAT_80610ba0;               // anim state table base
extern int   DAT_80611928;               // global frame counter
extern int   DAT_80612610;               // proximity threshold
extern int   DAT_80611824;               // anim descriptor table base
extern int   DAT_804d1f06;
extern int   DAT_806109c0;               // frame-delta raw (int)
extern int   DAT_804308b8;               // per-zone type table
extern int   DAT_804d1cd0;               // anim track index table (shorts)
extern int   DAT_804d34dc;               // anim track flag table
extern int   DAT_80472550;               // idle-anim clip id table (per stance)
extern int   DAT_80587e10;               // zone-alert bucket table (stride 0x60)
extern int   DAT_80587e14;
extern int   DAT_80587e18;
extern float DAT_80587e1c;
extern unsigned char DAT_8048ca94[];     // per-weapon spawn-probe X table (stride 0x30)
extern unsigned char DAT_8048ca9c[];     // per-weapon spawn-probe Z table (stride 0x30)

extern float FLOAT_806108ec;             // locomotion timer gain
extern float FLOAT_80618f98;             // 0.0f
extern float FLOAT_80618fa8;
extern float FLOAT_80618fac;
extern float FLOAT_80618fb8;             // 1.0f
extern float FLOAT_80618fd0;
extern float FLOAT_80618fd4;
extern float FLOAT_80618fd8;
extern float FLOAT_80618fe0;
extern float FLOAT_80618ff0;
extern float FLOAT_80618ff8;
extern float FLOAT_80618ffc;
extern float FLOAT_80619000;
extern float FLOAT_80619004;
extern float FLOAT_80619008;
extern float FLOAT_8061900c;
extern float FLOAT_80619010;
extern float FLOAT_80619014;
extern float FLOAT_80619018;
extern float FLOAT_8061901c;
extern float FLOAT_80619020;
extern float FLOAT_80619024;
extern float FLOAT_80619028;
extern float FLOAT_8061902c;
extern float FLOAT_80619030;
extern float FLOAT_80619034;
extern double DOUBLE_80618fc8;           // frame-delta bias (double)
extern double DOUBLE_80618fe8;           // rng->float bias (double)
}

// Advance the shared locomotion timer at actorState+0xa90 while capability
// bit 0x80 is set (delta-scaled), else reset to 0.
static inline void advance_loco_timer(int actorState) {
    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        double frameDelta =
            (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8;
        *(float*)(actorState + 0xa90) =
            FLOAT_806108ec * (FLOAT_80618fb8 / (float)frameDelta) +
            *(float*)(actorState + 0xa90);
    }
}

// Apply any queued behaviour-state swap (pending id at +0x9d0). On a real swap
// it records previous state at +0x9d4, resets the blend timer +0x9d8, then
// clears the pending slot.
static inline void apply_pending_state_swap(int actorState) {
    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d4) = *(unsigned int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = *(unsigned int*)(actorState + 0x9d0);
        }
        *(unsigned int*)(actorState + 0x9d0) = 0xffffffff;
    }
}

// ---------------------------------------------------------------------------
// aiactor_behavior_follow_tick 0x8025d7a0
// Escort/follow behaviour: retarget teammate, resolve target zone, then per
// mount/state drive follow-zone-path, turn-toward-leader and locomotion anims.
// ---------------------------------------------------------------------------
void aiactor_behavior_follow_tick(void)
{
    float fVar1;
    int iVar2;
    int iVar3;
    int iVar4;
    unsigned int uVar5;
    int iVar6;
    int (*pcVar7)(int);
    int actorState;
    double dVar9;

    iVar2 = __save_gpr_28();
    actorState = *(int*)(iVar2 + 0x7c);
    aiactor_retarget_visible_teammate();
    apply_pending_state_swap(actorState);
    advance_loco_timer(actorState);

    iVar3 = aiactor_resolve_target_zone_id(iVar2);
    if (*(int*)(actorState + 0x44) == 1) {
        iVar4 = aiactor_retarget_visible_teammate();
        if (iVar4 == 0) {
            *(float*)(actorState + 0xb34) = FLOAT_80618f98;
        } else if (*(float*)(actorState + 0xb34) < FLOAT_80618ffc) {
            iVar4 = 0;
        }
    } else {
        iVar4 = aiactor_retarget_visible_teammate();
    }

    if (*(int*)(actorState + 4) == 0) {
        if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
            uVar5 = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                           : *(unsigned int*)(actorState + 0x1c);
            if ((uVar5 & 1) == 0) { __restore_gpr_28(); return; }
        }
        *(unsigned int*)(actorState + 0xaac) = 0x10;
        *(unsigned int*)(actorState + 0xaa8) = 0;
        __restore_gpr_28();
        return;
    }

    iVar6 = player_ai_is_ranged_weapon((int)*(short*)(actorState + 0x4a));
    if ((iVar6 != 0) && ((*(unsigned int*)(*(int*)(actorState + 0xbf0) + 0x58) & 0x800000) != 0)) {
        if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
            uVar5 = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                           : *(unsigned int*)(actorState + 0x1c);
            if ((uVar5 & 1) == 0) { __restore_gpr_28(); return; }
        }
        *(unsigned int*)(actorState + 0xaac) = 4;
        *(unsigned int*)(actorState + 0xaa8) = 0;
        __restore_gpr_28();
        return;
    }

    if (iVar3 != *(int*)(actorState + 0x684)) {
        if (iVar3 < 0) {
            *(unsigned int*)(actorState + 0x684) =
                *(unsigned int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        } else {
            *(int*)(actorState + 0x684) = iVar3;
        }
        *(unsigned int*)(actorState + 0x9ac) = 0;
        *(unsigned int*)(actorState + 0x9a8) = 0;
    }

    if (((*(int*)(actorState + 8) == DAT_806120f8) &&
         (pcVar7 = *(int (**)(int))(*(int*)(actorState + 0xbf0) + 0x78), pcVar7 != (int (*)(int))0)) &&
        (iVar3 = (*pcVar7)(iVar2), iVar3 != 0)) {
        *(int*)(actorState + 4) = iVar3;
    }

    iVar3 = *(int*)(actorState + 0x20);
    if (iVar3 == 1) {
        if (iVar4 == 0) {
            iVar3 = aiactor_follow_zone_path();
            if (iVar3 != 0) {
                fVar1 = FLOAT_80619004;
                if ((*(unsigned int*)(actorState + 0x2c) & 0x100) != 0) {
                    fVar1 = FLOAT_80619000;
                }
                dVar9 = (double)fVar1;
                iVar3 = player_ai_is_valid_weapon_type((int)*(short*)(actorState + 0x4a));
                if ((iVar3 != 0) && (*(float*)(actorState + 0xaf4) < FLOAT_80618fa8)) {
                    if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                          (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                                    : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
                        *(unsigned int*)(actorState + 0xaac) = 4;
                        *(unsigned int*)(actorState + 0xaa8) = 0;
                    }
                }
                if (*(float*)(actorState + 0x9b0) <= (float)(dVar9 * dVar9)) {
                    if (*(int*)(actorState + 0x44) == 1) {
                        FUN_80285528(iVar2);
                    }
                    if ((*(unsigned int*)(DAT_80610ba0 + *(int*)(actorState + 0x204) * 0x29c + 0xc) & 1) == 0) {
                        if (anim_is_clip_active(iVar2, 0x5f9) == 0) {
                            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                        }
                    } else {
                        if (anim_is_clip_active(iVar2, 0x60b) == 0) {
                            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                        }
                    }
                    aiactor_turn_toward_leader();
                    goto follow_post;
                }
                *(unsigned int*)(actorState + 0x30) = *(unsigned int*)(*(int*)(actorState + 0x67c) + 0xc);
                *(unsigned int*)(actorState + 0x34) = *(unsigned int*)(*(int*)(actorState + 0x67c) + 0x10);
                *(unsigned int*)(actorState + 0x38) = *(unsigned int*)(*(int*)(actorState + 0x67c) + 0x14);
            }
        } else {
            *(unsigned int*)(actorState + 0x9d4) = 1;
            *(unsigned int*)(actorState + 0x20) = 2;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
        if (*(int*)(actorState + 0x44) == 1) {
            FUN_80285528(iVar2);
        } else {
            iVar3 = player_ai_is_ranged_weapon((int)*(short*)(actorState + 0x48));
            if (iVar3 == 0) {
                if (*(short*)(actorState + 0x48) == 0x51) {
                    if (anim_is_clip_active(iVar2, 0x6fe) == 0) {
                        anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    }
                } else if ((*(unsigned int*)(DAT_80610ba0 + *(int*)(actorState + 0x204) * 0x29c + 0xc) & 1) == 0) {
                    if (anim_is_clip_active(iVar2, 0xad3) == 0) {
                        anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    }
                } else {
                    if (anim_is_clip_active(iVar2, 0xad4) == 0) {
                        anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    }
                }
            } else {
                FUN_8028a3cc(iVar2);
            }
        }
        aiactor_facing_update();
    } else if (iVar3 < 1) {
        if (-1 < iVar3) {
            aiactor_follow_zone_path();
            if ((*(unsigned int*)(actorState + 0x28) & 0x10000) == 0) {
                if (*(int*)(actorState + 0x44) == 1) {
                    FUN_80285528(iVar2);
                } else {
                    iVar3 = player_ai_is_ranged_weapon((int)*(short*)(actorState + 0x48));
                    if (iVar3 == 0) {
                        if (*(short*)(actorState + 0x48) == 0x51) {
                            if (anim_is_clip_active(iVar2, 0x6fe) == 0) {
                                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                            }
                        } else if ((*(unsigned int*)(DAT_80610ba0 + *(int*)(actorState + 0x204) * 0x29c + 0xc) & 1) == 0) {
                            if (anim_is_clip_active(iVar2, 0xad3) == 0) {
                                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                            }
                        } else {
                            if (anim_is_clip_active(iVar2, 0xad4) == 0) {
                                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                            }
                        }
                    } else {
                        FUN_8028a3cc(iVar2);
                    }
                }
                aiactor_facing_update();
            } else {
                aiactor_turn_toward_leader();
            }
            if (*(int*)(actorState + 0x20) != 1) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 1;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        }
    } else if (iVar3 < 3) {
        if (*(int*)(actorState + 0x44) == 1) {
            FUN_80285528(iVar2);
        } else {
            iVar3 = player_ai_is_ranged_weapon((int)*(short*)(actorState + 0x48));
            if (iVar3 == 0) {
                if (*(short*)(actorState + 0x48) == 0x51) {
                    if (anim_is_clip_active(iVar2, 0x6fe) == 0) {
                        anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    }
                } else if ((*(unsigned int*)(DAT_80610ba0 + *(int*)(actorState + 0x204) * 0x29c + 0xc) & 1) == 0) {
                    if (anim_is_clip_active(iVar2, 0xad3) == 0) {
                        anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    }
                } else {
                    if (anim_is_clip_active(iVar2, 0xad4) == 0) {
                        anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    }
                }
            } else {
                FUN_8028a3cc(iVar2);
            }
        }
        aiactor_facing_update();
        iVar2 = player_ai_is_valid_weapon_type((int)*(short*)(actorState + 0x48));
        if (((((iVar2 != 0) ||
               (iVar2 = player_ai_is_ranged_weapon((int)*(short*)(actorState + 0x48)), iVar2 != 0)) ||
              (*(short*)(actorState + 0x48) == 0x51)) ||
             ((*(int*)(actorState + 0x204) == -1 || (*(float*)(actorState + 0xaf4) <= FLOAT_80619008)))) ||
            (*(int*)(&DAT_804308b8 + *(int*)(actorState + 0x204) * 0x24) != 0)) {
            if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                            : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
                *(unsigned int*)(actorState + 0xaac) = 4;
                *(unsigned int*)(actorState + 0xaa8) = 0;
            }
        } else {
            if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                            : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
                *(unsigned int*)(actorState + 0xaac) = 0x2000000;
                *(unsigned int*)(actorState + 0xaa8) = 0;
            }
        }
    }

follow_post:
    if ((FLOAT_80618fac < *(float*)(actorState + 0xb30)) &&
        ((*(unsigned int*)(actorState + 0x28) & 1) != 0)) {
        *(unsigned int*)(actorState + 0xabc) = 4;
        *(unsigned int*)(actorState + 0xab8) = 0;
        if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
              (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                        : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
            *(unsigned int*)(actorState + 0xaac) = 0x20;
            *(unsigned int*)(actorState + 0xaa8) = 0;
        }
    }
    __restore_gpr_28();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_guard_tick 0x8025e210
// Guard/hold behaviour: acquire target, then per state (0 hold, 1 advance,
// 2/3 combat) drive floor-query stance, leader-zone follow and cover picks.
// ---------------------------------------------------------------------------
void aiactor_behavior_guard_tick(void)
{
    float fVar1;
    int iVar2;
    int iVar3;
    int iVar4;
    unsigned int uVar5;
    unsigned int uVar6;
    int iVar7;
    bool bVar8;
    int (*pcVar9)(int);
    int actorState;
    double dVar11;

    iVar3 = FUN_803826f0();
    actorState = *(int*)(iVar3 + 0x7c);
    iVar4 = aiactor_retarget_visible_teammate();
    apply_pending_state_swap(actorState);
    advance_loco_timer(actorState);

    if (iVar4 != 0) {
        aiactor_alert_broadcast_group(0, iVar3,
            *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 0x678));
    }
    if (*(int*)(actorState + 4) == 0) {
        uVar5 = aiactor_acquire_nearest_target(iVar3, *(int*)(DAT_80611d78 + 0x14), 0);
        *(unsigned int*)(actorState + 4) = uVar5;
    }
    iVar2 = *(int*)(actorState + 0x20);
    if (iVar2 == 2) { FUN_8038273c(); return; }

    if (iVar2 < 2) {
        if (iVar2 == 0) {
            *(float*)(actorState + 0xb34) = FLOAT_80618f98;
            if ((*(unsigned int*)(actorState + 0xab4) & 0x100000) != 0) {
                iVar4 = *(short*)(actorState + 0x48) * 0x30;
                dVar11 = (double)floor_query_with_interact(
                    (double)*(float*)(&DAT_8048ca94[iVar4]),
                    (double)*(float*)(&DAT_8048ca9c[iVar4]), iVar3 + 100, 0, (int*)0, 0);
                *(float*)(actorState + 0xb70) = (float)dVar11;
                *(float*)(actorState + 0xbfc) = (float)dVar11;
                *(float*)(iVar3 + 0x68) = (float)dVar11;
            }
            *(unsigned int*)(actorState + 0x30) = *(unsigned int*)(iVar3 + 100);
            *(unsigned int*)(actorState + 0x34) = *(unsigned int*)(iVar3 + 0x68);
            *(unsigned int*)(actorState + 0x38) = *(unsigned int*)(iVar3 + 0x6c);
            aiactor_set_stance_flags(iVar3, 4);
            *(unsigned int*)(actorState + 0xae8) = 0;
            if (((*(unsigned int*)(*(int*)(actorState + 0xbf0) + 0x54) & 0x40) != 0) &&
                (iVar3 = entity_handle_resolve(*(unsigned int*)(*(int*)(actorState + 0xbf0) + 0x74)),
                 FLOAT_80618f98 < *(float*)(iVar3 + 0x1cc))) {
                *(int*)(actorState + 0xae8) = iVar3;
            }
            if (*(int*)(actorState + 0xae8) == 0) {
                if (*(int*)(actorState + 0x20) != 3) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(unsigned int*)(actorState + 0x20) = 3;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
                }
            } else if (*(int*)(actorState + 0x20) != 1) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 1;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        } else if (-1 < iVar2) {
            *(unsigned int*)(actorState + 4) = *(unsigned int*)(actorState + 0xae8);
            FUN_80280c8c(iVar3);
            if ((*(unsigned int*)(&DAT_804d34dc +
                    (short)(&DAT_804d1cd0)[*(int*)(*(int*)(iVar3 + 4) + 0xcc)] * 8) & 0x100) != 0) {
                *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffbffff;
                if (((*(int**)(iVar3 + 4))[0x32] & 1U) != 0) {
                    anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar3 + 4), 2);
                }
            }
            aiactor_turn_toward_leader();
            if (iVar4 != 0) {
                *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
            }
            fVar1 = FLOAT_80618f98;
            if ((*(float*)(*(int*)(actorState + 4) + 0x1cc) <= FLOAT_80618f98) &&
                (*(int*)(actorState + 0x20) != 0)) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 0;
                *(float*)(actorState + 0x9d8) = fVar1;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        }
    } else if (iVar2 < 4) {
        uVar6 = *(unsigned int*)(actorState + 0x28);
        if (((((uVar6 & 0x1000) == 0) || ((uVar6 & 0x10000) != 0)) || (iVar4 != 0)) ||
            (((uVar6 & 0x40000000) != 0 ||
              (iVar2 = aiactor_relation_entry(
                   *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 8),
                   *(int*)(actorState + 8)),
               DAT_80611928 - *(int*)(iVar2 + 8) <= (0x10 - *(int*)(actorState + 0xb04)) * 0x3c)))) {
            if ((*(unsigned int*)(*(int*)(actorState + 0xbf0) + 4) & 0x1000000) != 0) {
                iVar2 = aiactor_alert_group_flagA_any(*(unsigned int*)(actorState + 0xbec));
                if ((iVar4 != 0) || (iVar2 != 0)) {
                    *(float*)(actorState + 0xb34) = FLOAT_80618f98;
                }
                if (FLOAT_80618ff8 < *(float*)(actorState + 0xb34)) {
                    *(unsigned int*)(actorState + 0xac8) = 0xffffffff;
                    *(unsigned int*)(actorState + 0xaa0) = 0;
                    *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffffbff;
                    *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffeffff;
                    if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                          (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                                    : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
                        *(unsigned int*)(actorState + 0xaac) = 0x1000000;
                        *(unsigned int*)(actorState + 0xaa8) = 0;
                    }
                    FUN_8038273c();
                    return;
                }
            }
            FUN_80280c8c(iVar3);
            if ((*(unsigned int*)(actorState + 0x28) & 0x8000000) == 0) {
                if ((*(unsigned int*)(&DAT_804d34dc +
                        (short)(&DAT_804d1cd0)[*(int*)(*(int*)(iVar3 + 4) + 0xcc)] * 8) & 0x100) != 0) {
                    *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffbffff;
                    if (((*(int**)(iVar3 + 4))[0x32] & 1U) != 0) {
                        anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar3 + 4), 2);
                    }
                }
                aiactor_turn_toward_leader();
                if (iVar4 != 0) {
                    *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
                }
            } else {
                iVar2 = *(int*)(actorState + 4);
                bVar8 = false;
                if (FLOAT_80618fe0 <= *(float*)(*(int*)(iVar2 + 0x7c) + 0xb4c)) {
                    if (FLOAT_80618fd0 <= *(float*)(*(int*)(iVar2 + 0x7c) + 0xb44)) {
                        iVar7 = DAT_80611d90 + DAT_806120f8 * 0xea0;
                        if (FLOAT_80618fe0 <= *(float*)(iVar7 + 0xb4c)) {
                            if (*(float*)(iVar7 + 0xb44) < FLOAT_80618fd0) {
                                bVar8 = true;
                            }
                        } else {
                            bVar8 = true;
                        }
                    } else {
                        bVar8 = true;
                    }
                } else {
                    bVar8 = true;
                }
                if (((iVar2 == 0) || (iVar4 == 0)) ||
                    ((!bVar8 &&
                      (bVar8 = aiactor_aim_alignment_test(
                           (double)*(float*)(actorState + 0x9bc), iVar3, (float*)(iVar2 + 100)),
                       !bVar8)))) {
                    if (*(int*)(actorState + 0xacc) == 0) {
                        aiactor_turn_toward_angle(
                            (double)*(float*)(*(int*)(actorState + 0x67c) + 0x18), iVar3, 0);
                    } else {
                        aiactor_turn_toward_angle(
                            (double)*(float*)(*(int*)(actorState + 0xacc) + 0x18), iVar3, 0);
                    }
                    dVar11 = (double)FLOAT_8061900c;
                    *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 0x40000;
                    anim_set_playback_rate(dVar11, *(int**)(iVar3 + 4), 2);
                } else {
                    if ((*(unsigned int*)(&DAT_804d34dc +
                            (short)(&DAT_804d1cd0)[*(int*)(*(int*)(iVar3 + 4) + 0xcc)] * 8) & 0x100) != 0) {
                        *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffbffff;
                        if (((*(int**)(iVar3 + 4))[0x32] & 1U) != 0) {
                            anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar3 + 4), 2);
                        }
                    }
                    aiactor_turn_toward_leader();
                    *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
                }
            }
            if (((*(int*)(actorState + 8) == DAT_806120f8) &&
                 (pcVar9 = *(int (**)(int))(*(int*)(actorState + 0xbf0) + 0x78),
                  pcVar9 != (int (*)(int))0)) &&
                (iVar3 = (*pcVar9)(iVar3), iVar3 != 0)) {
                *(int*)(actorState + 4) = iVar3;
            }
        } else {
            if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                            : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
                *(unsigned int*)(actorState + 0xaac) = 4;
                *(unsigned int*)(actorState + 0xaa8) = 0;
            }
        }
    }
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_patrol_tick 0x8025e994
// Patrol/idle behaviour: acquire target, follow leader zone path, then play a
// randomly-chosen idle/patrol locomotion clip (per stance flag + track table).
// ---------------------------------------------------------------------------
void aiactor_behavior_patrol_tick(void)
{
    int iVar1;
    int iVar2;
    int iVar3;
    unsigned int uVar4;
    int iVar6;
    int* piVar7;

    iVar1 = FUN_803826e8();
    piVar7 = *(int**)(iVar1 + 0x7c);
    iVar2 = aiactor_retarget_visible_teammate();
    apply_pending_state_swap((int)piVar7);
    advance_loco_timer((int)piVar7);

    iVar6 = 0;
    if (piVar7[1] == 0) {
        iVar1 = aiactor_acquire_nearest_target(iVar1, *(int*)(DAT_80611d78 + 0x14), 0);
        piVar7[1] = iVar1;
        FUN_80382734();
        return;
    }
    iVar3 = prop_hinge_player_push_tick();
    if ((iVar3 != 0) || (iVar3 = FUN_802a6690(), iVar3 != 0)) { FUN_80382734(); return; }
    if ((float)piVar7[0x2bd] < FLOAT_80619010) {
        if (!(piVar7[0x2ab] == 1 && piVar7[0x2aa] == 0 &&
              (((DAT_805015bd == '\n') ? (unsigned int)piVar7[5]
                                        : (unsigned int)piVar7[7]) & 1) == 0)) {
            piVar7[0x2ab] = 4;
            piVar7[0x2aa] = 0;
        }
        FUN_80382734();
        return;
    }

    if (iVar2 == 0) {
        aiactor_resolve_target_zone_id(iVar1);
        aiactor_follow_zone_path();
    } else {
        bool useZonePath = false;
        if (piVar7 != (int*)0) {
            iVar3 = *piVar7;
            if (iVar3 != 0) {
                if (DAT_80612610 <= piVar7[2]) {
                    if (((*(unsigned int*)(iVar3 + 0x28) & 0x100) != 0) ||
                        ((*(unsigned int*)(iVar3 + 0x10) & 0xffff) == 0)) {
                        useZonePath = true;
                    }
                }
            }
        }
        if (useZonePath) {
            aiactor_resolve_target_zone_id(iVar1);
            aiactor_follow_zone_path();
        } else {
            aiactor_leader_zone_update(iVar1);
            piVar7[0xc] = *(int*)(piVar7[1] + 100);
            piVar7[0xd] = *(int*)(piVar7[1] + 0x68);
            piVar7[0xe] = *(int*)(piVar7[1] + 0x6c);
        }
    }

    if ((*(unsigned int*)(DAT_80610ba0 + piVar7[0x81] * 0x29c + 0xc) & 1) == 0) {
        iVar3 = piVar7[0x25];
        if (((iVar3 == 0xe) || (iVar3 == 0x35)) || (iVar3 == 0xf)) {
            iVar6 = 2;
        }
    } else {
        iVar6 = 1;
    }

    iVar3 = anim_is_clip_active(iVar1, (&DAT_80472550)[iVar6]);
    if ((iVar3 == 0) &&
        (iVar3 = anim_is_clip_active(iVar1, *(int*)(iVar6 * 4 + -0x7fb8daa0)), iVar3 == 0)) {
        anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80619014);
        iVar6 = (&DAT_80472550)[iVar6];
    } else {
        iVar3 = anim_primary_clip_userflags(iVar1, (&DAT_80472550)[iVar6]);
        if (iVar3 == 0) {
            iVar6 = *(int*)(iVar6 * 4 + -0x7fb8daa0);
            iVar3 = anim_primary_clip_userflags(iVar1, iVar6);
            if (iVar3 == 0) goto patrol_flags;
        } else {
            iVar6 = (&DAT_80472550)[iVar6];
        }
        {
            unsigned int r = rng_counter_tick();
            (void)r;
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
        }
    }
patrol_flags:
    if (iVar2 != 0) {
        piVar7[10] = piVar7[10] | 2;
    }
    iVar2 = anim_clip_has_flags(iVar1, iVar6, 0, 8);
    if (iVar2 == 0) {
        piVar7[0xb] = piVar7[0xb] | 0x400;
    } else {
        piVar7[0xb] = piVar7[0xb] & 0xfffffbff;
    }
    if (FLOAT_80618fac < (float)piVar7[0x2cc]) {
        if ((piVar7[10] & 1U) != 0) {
            if (!(piVar7[0x2ab] == 1 && piVar7[0x2aa] == 0 &&
                  (((DAT_805015bd == '\n') ? (unsigned int)piVar7[5]
                                            : (unsigned int)piVar7[7]) & 1) == 0)) {
                piVar7[0x2ab] = 8;
                piVar7[0x2aa] = 0;
            }
        }
    }
    if (((piVar7[2] == DAT_806120f8) && (*(int (**)(int))(piVar7[0x2fc] + 0x78) != (int (*)(int))0)) &&
        (iVar1 = (**(int (**)(int))(piVar7[0x2fc] + 0x78))(iVar1), iVar1 != 0)) {
        piVar7[1] = iVar1;
    }
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_request_take_cover 0x8025ee20
// Returns 1 if a take-cover action was queued/allowed this frame, 0 to abort.
// Gated by mode, active target, cover-cooldown and teammate-block checks.
// ---------------------------------------------------------------------------
int aiactor_request_take_cover(int actorState)
{
    int iVar1;
    unsigned int uVar2;

    if (DAT_805015bd == '\n') {
        return 0;
    }
    iVar1 = FUN_80281ce8();
    if ((((iVar1 != 0) && (*(int*)(actorState + 4) != 0)) &&
         (*(float*)(actorState + 0xaf4) < FLOAT_80619018)) &&
        ((iVar1 = aiactor_retarget_visible_teammate(), iVar1 != 0 &&
          (*(float*)(actorState + 0xa90) < FLOAT_80618fb8)))) {
        return 0;
    }
    if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
        uVar2 = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                       : *(unsigned int*)(actorState + 0x1c);
        if ((uVar2 & 1) == 0) {
            return 1;
        }
    }
    *(unsigned int*)(actorState + 0xaac) = 0x40000;
    *(unsigned int*)(actorState + 0xaa8) = 0;
    return 1;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_combat_tick 0x8025ef34
// The main combat behaviour FSM (8 states via +0x20): approach, strafe,
// retreat, melee, reposition. Computes distance to target, drives stance
// flags and transitions between range bands.
// ---------------------------------------------------------------------------
void aiactor_behavior_combat_tick(void)
{
    float fVar1;
    int iVar3;
    int iVar4;
    int iVar5;
    unsigned int uVar6;
    unsigned int uVar7;
    int actorState;
    double dVar9;
    double dVar10;
    double dVar11;
    float fVar2;

    iVar3 = FUN_803826f0();
    actorState = *(int*)(iVar3 + 0x7c);
    iVar4 = aiactor_retarget_visible_teammate();
    apply_pending_state_swap(actorState);
    advance_loco_timer(actorState);

    anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar3 + 4), 2);
    fVar1 = FLOAT_80619024;
    fVar2 = FLOAT_80619020;
    if (*(int*)(actorState + 0x94) != 0x14) {
        fVar1 = FLOAT_80619020;
        fVar2 = FLOAT_8061901c;
    }
    dVar11 = (double)fVar2;
    dVar9 = (double)fVar1;
    iVar5 = *(int*)(actorState + 4);
    if ((iVar5 == 0) || (*(float*)(iVar5 + 0x1cc) <= FLOAT_80618f98)) {
        uVar6 = aiactor_acquire_nearest_target(iVar3, *(int*)(DAT_80611d78 + 0x14), 0);
        *(unsigned int*)(actorState + 4) = uVar6;
        if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
              (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                        : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
            *(unsigned int*)(actorState + 0xaac) = 0x10;
            *(unsigned int*)(actorState + 0xaa8) = 0;
        }
        FUN_8038273c();
        return;
    }

    fVar1 = *(float*)(iVar3 + 100) - *(float*)(iVar5 + 100);
    fVar2 = *(float*)(iVar3 + 0x6c) - *(float*)(iVar5 + 0x6c);
    dVar10 = (double)(fVar1 * fVar1 + fVar2 * fVar2);
    if (iVar4 != 0) {
        if (*(int*)(actorState + 0x44) == 0) {
            *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
        } else if ((*(int*)(actorState + 0x44) == 1) &&
                   (FLOAT_80618fd4 < *(float*)(actorState + 0xb4c))) {
            *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
        }
    }
    aiactor_leader_zone_update(iVar3);
    iVar5 = *(int*)(actorState + 0x20);
    switch (iVar5) {
    case 0:
        if (iVar5 != 1) {
            *(int*)(actorState + 0x9d4) = iVar5;
            *(unsigned int*)(actorState + 0x20) = 1;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
        break;
    case 1:
        if (dVar11 <= dVar10) {
            *(unsigned int*)(actorState + 0xb28) = *(unsigned int*)(actorState + 0xb14);
            aiactor_set_stance_flags(iVar3, 1);
            *(unsigned int*)(actorState + 0x30) = *(unsigned int*)(*(int*)(actorState + 4) + 100);
            *(unsigned int*)(actorState + 0x34) = *(unsigned int*)(*(int*)(actorState + 4) + 0x68);
            *(unsigned int*)(actorState + 0x38) = *(unsigned int*)(*(int*)(actorState + 4) + 0x6c);
        } else if (iVar5 != 2) {
            *(int*)(actorState + 0x9d4) = iVar5;
            *(unsigned int*)(actorState + 0x20) = 2;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
        break;
    case 2:
        *(unsigned int*)(actorState + 0xad0) = *(unsigned int*)(actorState + 0xb18);
        *(unsigned int*)(actorState + 0x30) = *(unsigned int*)(*(int*)(actorState + 4) + 100);
        *(unsigned int*)(actorState + 0x34) = *(unsigned int*)(*(int*)(actorState + 4) + 0x68);
        *(unsigned int*)(actorState + 0x38) = *(unsigned int*)(*(int*)(actorState + 4) + 0x6c);
        if (dVar9 <= dVar10) {
            if (dVar11 <= dVar10) {
                if (*(int*)(actorState + 0x20) != 1) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(unsigned int*)(actorState + 0x20) = 1;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
                }
            } else {
                aiactor_set_stance_flags(iVar3, 1);
                if ((FLOAT_80619028 < *(float*)(actorState + 0xb30)) ||
                    (FLOAT_80619028 < *(float*)(actorState + 0xb4c))) {
                    *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
                    *(float*)(actorState + 0xb30) = FLOAT_80618f98;
                }
            }
        } else {
            if (*(int*)(actorState + 0x20) != 3) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 3;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
        }
        break;
    case 3:
        aiactor_set_stance_flags(iVar3, 4);
        if (ABS(*(float*)(actorState + 0xae0)) < FLOAT_80618ff8) {
            *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
            if (*(int*)(actorState + 0x20) != 5) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 5;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
        }
        break;
    case 4:
        if ((*(unsigned int*)(actorState + 0x24) & 2) == 0) {
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
        }
        aiactor_set_stance_flags(iVar3, 2);
        if (FLOAT_8061902c < *(float*)(actorState + 0xb30)) {
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x24) = *(unsigned int*)(actorState + 0x24) & 0xfffffffd;
            if (*(int*)(actorState + 0x20) != 1) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 1;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        }
        break;
    case 5:
        if (dVar10 <= (double)(float)((double)FLOAT_80618fb8 + dVar11)) {
            aiactor_set_stance_flags(iVar3, 4);
        } else if (iVar5 != 7) {
            *(int*)(actorState + 0x9d4) = iVar5;
            *(unsigned int*)(actorState + 0x20) = 7;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
        if (FLOAT_80618fe0 < *(float*)(actorState + 0xb30)) {
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
            if (*(int*)(actorState + 0x20) != 3) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 3;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        }
        break;
    case 6:
        aiactor_set_stance_flags(iVar3, 4);
        if ((FLOAT_80618fb8 < *(float*)(actorState + 0xb30)) && (iVar4 == 0)) {
            if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                            : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
                *(unsigned int*)(actorState + 0xaac) = 0x10;
                *(unsigned int*)(actorState + 0xaa8) = 0;
            }
            break;
        }
        if ((iVar4 != 0) && (*(int*)(actorState + 0x20) != 7)) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = 7;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
        break;
    case 7:
        if (dVar10 <= dVar11) {
            if (iVar5 != 2) {
                *(int*)(actorState + 0x9d4) = iVar5;
                *(unsigned int*)(actorState + 0x20) = 2;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        } else if (iVar5 != 1) {
            *(int*)(actorState + 0x9d4) = iVar5;
            *(unsigned int*)(actorState + 0x20) = 1;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
        break;
    default:
        break;
    }

    if ((((*(int*)(actorState + 700) != 0) || (*(int*)(actorState + 0x18c) != 0)) ||
         (iVar3 = aiactor_request_take_cover(actorState), iVar3 == 0)) &&
        ((iVar4 == 0 && (*(int*)(actorState + 0x20) != 6)))) {
        *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
        *(unsigned int*)(actorState + 0x20) = 6;
        *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
        *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        *(float*)(actorState + 0xb30) = FLOAT_80618f98;
    }
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_fire_gate 0x8025f5f8
// Set stance 4, then gate into aiactor_commit_pending_action either when the
// actor has an active target (mode flags +0x10 low word) or its alert group
// flag-A is set with capability bit 0x400.
// ---------------------------------------------------------------------------
void aiactor_behavior_fire_gate(int entity)
{
    int actorState;

    actorState = *(int*)(entity + 0x7c);
    aiactor_set_stance_flags(entity, 4);
    if ((*(unsigned int*)(entity + 0x10) & 0xffff) == 0) {
        if (((*(unsigned int*)(*(int*)(actorState + 0xbf0) + 0x54) & 0x400) != 0) &&
            (aiactor_alert_group_flagA_any(*(unsigned int*)(actorState + 0xbec)) != 0)) {
            aiactor_commit_pending_action(entity);
        }
    } else {
        aiactor_commit_pending_action(entity);
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_commit_pending_action 0x8025f674
// Resolve/refresh the current target, then promote the queued-action pair
// (+0xabc/+0xab8) into the live stance-request pair (+0xaac/+0xaa8), honouring
// the "busy" guard (aac==1 && aa8==0) and per-frame input-mask gate. If the
// actor is flagged uncontrollable (+0x2c bit 4) it bails immediately.
// ---------------------------------------------------------------------------
void aiactor_commit_pending_action(int entity)
{
    unsigned int uVar1;
    unsigned int uVar2;
    int (*pcVar3)(void);
    int actorState;

    actorState = *(int*)(entity + 0x7c);
    if ((*(unsigned int*)(actorState + 0x2c) & 4) != 0) {
        return;
    }
    if ((*(unsigned int*)(actorState + 0x28) & 0x80) == 0) {
        pcVar3 = *(int (**)(void))(*(int*)(actorState + 0xbf0) + 0x78);
        if (pcVar3 != (int (*)(void))0) {
            uVar2 = (*pcVar3)();
            *(unsigned int*)(actorState + 4) = uVar2;
        }
        if (*(int*)(actorState + 4) == 0) {
            uVar2 = aiactor_acquire_nearest_target(entity, *(int*)(DAT_80611d78 + 0x14), 0);
            *(unsigned int*)(actorState + 4) = uVar2;
        }
        if ((*(unsigned int*)(actorState + 0xabc) & 0x1160000) == 0 &&
            (*(unsigned int*)(actorState + 0xab8) & 1) == 0) {
            if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
                uVar1 = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                               : *(unsigned int*)(actorState + 0x1c);
                if ((uVar1 & 1) == 0) {
                    return;
                }
            }
            *(unsigned int*)(actorState + 0xaac) = 0x8000000;
            *(unsigned int*)(actorState + 0xaa8) = 0;
            return;
        }
        if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
              (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                        : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
            *(unsigned int*)(actorState + 0xaac) = *(unsigned int*)(actorState + 0xabc);
            *(unsigned int*)(actorState + 0xaa8) = *(unsigned int*)(actorState + 0xab8);
        }
        *(unsigned int*)(actorState + 0xabc) = 0;
        *(unsigned int*)(actorState + 0xab8) = 0;
        return;
    }
    if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
          (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                    : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
        *(unsigned int*)(actorState + 0xaac) = *(unsigned int*)(actorState + 0xabc);
        *(unsigned int*)(actorState + 0xaa8) = *(unsigned int*)(actorState + 0xab8);
    }
    *(unsigned int*)(actorState + 0xabc) = 0;
    *(unsigned int*)(actorState + 0xab8) = 0;
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_engage_tick 0x8025f89c
// Engage/attack behaviour: evaluate target LOS, resolve target via char-def
// callback, set stance-4, then either fire the alert-broadcast payload
// (aiactor_alert_broadcast_group) or play the attack anim, keyed on LOS result,
// vehicle-mount class (VICE/VICO) and the queued-action pair.
// ---------------------------------------------------------------------------
void aiactor_behavior_engage_tick(void)
{
    int iVar1;
    int iVar2;
    unsigned int uVar3;
    int iVar4;
    unsigned int uVar5;
    int (*pcVar6)(int);
    int actorState;

    iVar1 = FUN_803826f0();
    actorState = *(int*)(iVar1 + 0x7c);
    iVar2 = aiactor_evaluate_target_los();
    if (((iVar2 == 0) && ((*(unsigned int*)(*(int*)(actorState + 0xbf0) + 0x54) & 0x400) != 0)) &&
        (*(int*)(actorState + 8) == DAT_806120f8)) {
        iVar2 = aiactor_alert_group_flagA_any(*(unsigned int*)(actorState + 0xbec));
    }
    aiactor_set_stance_flags(iVar1, 4);
    pcVar6 = *(int (**)(int))(*(int*)(actorState + 0xbf0) + 0x78);
    if (pcVar6 != (int (*)(int))0) {
        uVar3 = (*pcVar6)(iVar1);
        *(unsigned int*)(actorState + 4) = uVar3;
        iVar2 = 2;
    }
    *(unsigned int*)(actorState + 0x30) = *(unsigned int*)(iVar1 + 100);
    *(unsigned int*)(actorState + 0x34) = *(unsigned int*)(iVar1 + 0x68);
    *(unsigned int*)(actorState + 0x38) = *(unsigned int*)(iVar1 + 0x6c);

    if ((*(unsigned int*)(actorState + 0x28) & 0x80) != 0) {
        iVar4 = aiactor_retarget_visible_teammate();
        if (iVar4 != 0) {
            if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                            : *(unsigned int*)(actorState + 0x1c)) & 1) != 0)) {
                *(unsigned int*)(actorState + 0xaac) = *(unsigned int*)(actorState + 0xabc);
                *(unsigned int*)(actorState + 0xaa8) = *(unsigned int*)(actorState + 0xab8);
            }
            *(unsigned int*)(actorState + 0xabc) = 0;
            *(unsigned int*)(actorState + 0xab8) = 0;
            aiactor_alert_broadcast_group(0, iVar1,
                *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 0x678));
            FUN_8038273c();
            return;
        }
    }

    if (iVar2 != 0) {
        if (*(short*)(*(int*)(actorState + 0xbf0) + 0x4c) == -1) {
            if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                            : *(unsigned int*)(actorState + 0x1c)) & 1) != 0)) {
                *(unsigned int*)(actorState + 0xaac) = *(unsigned int*)(actorState + 0xabc);
                *(unsigned int*)(actorState + 0xaa8) = *(unsigned int*)(actorState + 0xab8);
            }
            *(unsigned int*)(actorState + 0xabc) = 0;
            *(unsigned int*)(actorState + 0xab8) = 0;
        } else if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                     (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                               : *(unsigned int*)(actorState + 0x1c)) & 1) != 0)) {
            *(unsigned int*)(actorState + 0xaac) = 0;
            *(unsigned int*)(actorState + 0xaa8) = 0x800;
        }
        if (iVar2 == 1) {
            aiactor_alert_broadcast_group(0, iVar1,
                *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 0x678));
        } else {
            aiactor_alert_broadcast_group(0, iVar1, *(int*)(actorState + 0x678));
        }
        FUN_8038273c();
        return;
    }

    if ((*(unsigned int*)(actorState + 0xab8) & 0x4000001) == 0) {
        iVar2 = *(int*)(*(int*)(actorState + 0xbf0) + 8);
        if (iVar2 != 0x45434956) {          // 'VICE'
            if (iVar2 != 0x4f434956) {      // 'VICO'
                aiactor_play_attack_anim(iVar1);
                FUN_8038273c();
                return;
            }
        }
    }
    if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
          (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                    : *(unsigned int*)(actorState + 0x1c)) & 1) != 0)) {
        *(unsigned int*)(actorState + 0xaac) = *(unsigned int*)(actorState + 0xabc);
        *(unsigned int*)(actorState + 0xaa8) = *(unsigned int*)(actorState + 0xab8);
    }
    *(unsigned int*)(actorState + 0xabc) = 0;
    *(unsigned int*)(actorState + 0xab8) = 0;
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_play_attack_anim 0x8025fc38
// Select and play the attack / melee reaction clip for this actor based on
// game mode (coop vs vs), mount type (+0x44), and the char-def attack clip
// shorts (+0x48/+0x4a). Falls back to a randomly-timed generic swing.
// ---------------------------------------------------------------------------
void aiactor_play_attack_anim(int entity)
{
    unsigned int uVar1;
    int iVar2;
    int iVar3;
    double dVar4;
    float fStack_38;
    unsigned int local_34;
    unsigned int local_18;
    unsigned int uStack_14;
    int actorState;

    actorState = *(int*)(entity + 0x7c);
    if (DAT_805015bd == '\n') {
        if (*(int*)(actorState + 0x44) == 1) {
            if (((*(unsigned int*)(actorState + 0x10) & 1) == 0) &&
                ((*(unsigned int*)(actorState + 0xab8) & 1) == 0)) {
                iVar3 = anim_is_clip_active(entity, 0x112);
                if (iVar3 == 0) {
                    uStack_14 = rng_counter_tick();
                    local_18 = 0x43300000;
                    (void)local_18;
                    anim_play_scaled((double)FLOAT_80618f98,
                        (double)(FLOAT_8061900c *
                                 ((float)((double)CONCAT44(0x43300000, uStack_14) - DOUBLE_80618fe8) /
                                  FLOAT_80618fd8) + FLOAT_80618fb8));
                }
            } else {
                iVar3 = anim_is_clip_active(entity, 0x11b);
                if (iVar3 == 0) {
                    local_34 = 0;
                    anim_sample_keyframe(*(int*)(DAT_80611824 + DAT_804d1f06 * 4), 0, 0, &fStack_38);
                    dVar4 = (double)FLOAT_80618f98;
                    *(unsigned int*)(*(int*)(entity + 4) + 0xec) = local_34;
                    anim_play_scaled(dVar4, dVar4);
                }
            }
        } else {
            iVar2 = (int)*(short*)(*(int*)(actorState + 0xbf0) + 0x48);
            if (iVar2 == -1) {
                iVar3 = anim_is_clip_active(entity, 0x25);
                if (iVar3 == 0) {
                    uStack_14 = rng_counter_tick();
                    local_18 = 0x43300000;
                    (void)local_18;
                    anim_play_scaled((double)FLOAT_80618f98,
                        (double)(FLOAT_8061900c *
                                 ((float)((double)CONCAT44(0x43300000, uStack_14) - DOUBLE_80618fe8) /
                                  FLOAT_80618fd8) + FLOAT_80618fb8));
                }
            } else {
                iVar2 = anim_is_clip_active(entity, iVar2);
                if (iVar2 == 0) {
                    iVar3 = anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
                    if (iVar3 == 0) {
                        anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
                    }
                } else {
                    iVar2 = anim_is_clip_active(entity, (int)*(short*)(*(int*)(actorState + 0xbf0) + 0x48));
                    if (((iVar2 != 0) ||
                         (iVar2 = anim_is_clip_active(entity, (int)*(short*)(*(int*)(actorState + 0xbf0) + 0x4a)),
                          iVar2 != 0)) &&
                        (iVar2 = anim_active_slot_userflags(*(int*)(entity + 4)), iVar2 != 0)) {
                        if (*(short*)(*(int*)(actorState + 0xbf0) + 0x4a) == -1) {
                            anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
                        } else {
                            uVar1 = rng_counter_tick();
                            if ((uVar1 & 1) == 0) {
                                anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
                            } else {
                                anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
                            }
                        }
                    }
                    *(float*)(*(int*)(entity + 4) + 0x100) = FLOAT_80618fb8;
                }
            }
        }
    } else {
        iVar3 = anim_is_clip_active(entity, 0x25);
        if ((iVar3 != 0) && (iVar3 = anim_active_slot_userflags(*(int*)(entity + 4)), iVar3 != 0)) {
            uStack_14 = rng_counter_tick();
            local_18 = 0x43300000;
            (void)local_18;
            anim_play_scaled((double)FLOAT_80618f98,
                (double)(FLOAT_8061900c *
                         ((float)((double)CONCAT44(0x43300000, uStack_14) - DOUBLE_80618fe8) /
                          FLOAT_80618fd8) + FLOAT_80619030));
        }
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_alert_broadcast_group 0x8025ff4c
// Broadcast an alert/aggro to every AI actor registered in the mask-selected
// zone-alert buckets (table DAT_80587e10, 0x14 buckets stride 0x60). For each
// member: set/clear the "engaged" flag +0x400, hand off the target/zone, and
// queue the appropriate stance request (fire/retreat/cover) unless it is
// already busy or gated out. param_3 = target zone id.
// ---------------------------------------------------------------------------
void aiactor_alert_broadcast_group(unsigned int param_1, int entity, int zoneId)
{
    float fVar1;
    int iVar2;
    int iVar3;
    unsigned int uVar4;
    int iVar5;
    int iVar6;
    int* piVar7;
    int iVar8;
    int* piVar9;
    int iVar10;
    int iVar11;
    unsigned int uVar12;
    int iVar13;
    long long lVar14;
    unsigned long long uVar15;

    (void)param_1;
    lVar14 = FUN_803826c8();
    iVar2 = (int)((unsigned long long)lVar14 >> 0x20);
    iVar6 = (int)lVar14;
    iVar13 = -1;
    uVar12 = 0xffffffff;
    piVar9 = (int*)0;
    iVar8 = -1;
    if ((DAT_805015bd == '\n') && ((DAT_805015c0 & 0x40000000) == 0)) {
        if (iVar2 == 1) {
            iVar10 = *(int*)(iVar6 + 0x10);
            uVar12 = *(unsigned int*)(iVar6 + 4);
            if ((iVar10 == 0) || (*(int*)(iVar10 + 0x20) != 8)) {
                if (iVar10 != 0) {
                    iVar8 = *(int*)(iVar10 + 0x110);
                }
            } else {
                iVar8 = *(int*)(*(int*)(iVar10 + 0x7c) + 0x678);
            }
        } else if (lVar14 < 0x100000000LL) {
            if (-1 < lVar14) {
                piVar9 = *(int**)(iVar6 + 0x7c);
                if ((*(unsigned int*)(piVar9[0x2fc] + 0x54) & 1) != 0) { FUN_80382714(); return; }
                uVar12 = piVar9[0x2fb];
                iVar13 = piVar9[2];
                switch_object_toggle_by_mask();
                iVar8 = zoneId;
            }
        } else if (lVar14 < 0x300000000LL) {
            uVar12 = *(unsigned int*)(iVar6 + 0xc);
            iVar8 = soundpad_find_zone_by_id(*(int*)(iVar6 + 0x10));
            switch_object_toggle_by_mask();
        }
        iVar10 = 0;
        iVar6 = 0;
        do {
            fVar1 = FLOAT_80619034;
            if ((uVar12 & 1 << iVar10) != 0) {
                iVar5 = 0;
                *(unsigned int*)((int)&DAT_80587e14 + iVar6) = 1;
                *(unsigned int*)((int)&DAT_80587e18 + iVar6) = 1;
                *(float*)((int)&DAT_80587e1c + iVar6) = fVar1;
                for (iVar11 = 0; iVar11 < *(int*)((int)&DAT_80587e10 + iVar6); iVar11 = iVar11 + 1) {
                    piVar7 = *(int**)(*(int*)((int)&DAT_80587e10 + iVar6 + iVar5 + 0x10) + 0x7c);
                    if (piVar7[2] != iVar13) {
                        if (((piVar7[5] & 0x1000000U) == 0) || ((piVar7[10] & 0x400U) != 0)) {
                            if (((piVar7[5] & 0x1000000U) == 0) ||
                                (((piVar7[10] & 0x400U) == 0 ||
                                  ((*(unsigned int*)(piVar7[0x2fc] + 0x54) & 8) != 0)))) {
                                if (((piVar7[4] & 1U) != 0) &&
                                    (iVar3 = player_ai_is_ranged_weapon((int)*(short*)((int)piVar7 + 0x4a)),
                                     iVar3 != 0)) {
                                    piVar7[10] = piVar7[10] | 0x400;
                                }
                            } else {
                                uVar15 = (unsigned long long)(long long)aiactor_retarget_visible_teammate();
                                if ((int)((unsigned long long)uVar15 >> 0x20) == 0) {
                                    iVar3 = anim_clip_has_flags(*piVar7, (int)uVar15, 0, 0x20);
                                    if (iVar3 == 0) {
                                        if (!(piVar7[0x2ab] == 1 && piVar7[0x2aa] == 0 &&
                                              (((DAT_805015bd == '\n') ? (unsigned int)piVar7[5]
                                                                        : (unsigned int)piVar7[7]) & 1) == 0)) {
                                            piVar7[0x2ab] = 8;
                                            piVar7[0x2aa] = 0;
                                        }
                                    }
                                } else if ((*(unsigned int*)(piVar7[0x2fc] + 0x54) & 0x4000) == 0) {
                                    iVar3 = anim_clip_has_flags(*piVar7, (int)uVar15, 0, 0x20);
                                    if (iVar3 == 0) {
                                        if (!(piVar7[0x2ab] == 1 && piVar7[0x2aa] == 0 &&
                                              (((DAT_805015bd == '\n') ? (unsigned int)piVar7[5]
                                                                        : (unsigned int)piVar7[7]) & 1) == 0)) {
                                            piVar7[0x2ab] = 4;
                                            piVar7[0x2aa] = 0;
                                        }
                                    }
                                } else {
                                    if (!(piVar7[0x2ab] == 1 && piVar7[0x2aa] == 0 &&
                                          (((DAT_805015bd == '\n') ? (unsigned int)piVar7[5]
                                                                    : (unsigned int)piVar7[7]) & 1) == 0)) {
                                        piVar7[0x2ab] = 0x200;
                                        piVar7[0x2aa] = 0;
                                    }
                                }
                            }
                        } else {
                            if (iVar2 == 0) {
                                FUN_80260dd0(*piVar9, 0);
                                piVar7[0x2b2] = iVar8;
                            } else {
                                iVar3 = piVar7[1];
                                if (*(int*)(iVar3 + 0x20) == 8) {
                                    piVar7[0x2b2] = *(int*)(*(int*)(iVar3 + 0x7c) + 0x678);
                                } else {
                                    piVar7[0x2b2] = *(int*)(iVar3 + 0x110);
                                }
                            }
                            piVar7[10] = piVar7[10] | 0x400;
                            if ((piVar7[0x2fb] & ~uVar12) != 0) {
                                aiactor_alert_group_remove(*piVar7, piVar7[0x2fb] & ~uVar12);
                                piVar7[0x2fb] = piVar7[0x2fb] & uVar12;
                            }
                        }
                    }
                    iVar5 = iVar5 + 4;
                }
            }
            iVar10 = iVar10 + 1;
            iVar6 = iVar6 + 0x60;
        } while (iVar10 < 0x14);
    }
    FUN_80382714();
    return;
}

// ===========================================================================
// batch_740 | 2026-09-10 | 0x802603a4 - 0x80262d17
// More AI actor behaviour-state ticks + dispatch/event/trigger helpers.
//   aiactor_player_los_sweep     0x802603a4  per-frame local-player LOS/visibility scan
//   aiactor_play_alert_voice     0x80260dd0  play alert voice line once (from broadcast)
//   aiactor_behavior_investigate_tick 0x80260e24  investigate/approach state tick
//   aiactor_behavior_alert_sound 0x80261270  state6 alert sfx / state2 blink stop
//   aiactor_play_idle_bark_sound 0x8026136c  random idle anim + positional bark
//   aiactor_behavior_creature_tick 0x80261470  7-state creature/monkey FSM (flee/melee/throw)
//   aiactor_behavior_search_tick 0x80262564  search/return-to-zone state tick
//   aiactor_spawn_trigger_eval   0x802628cc  spawn/alert trigger-descriptor condition eval
//   aiactor_set_alert_zone_slot  0x80262c34  set alert-zone slot + arm pending state 1
//   aiactor_behavior_event_handler 0x80262c9c  behaviour event handler (0x3ea/1/0x3eb)
// ===========================================================================
extern "C" {
extern void     FUN_803826d8(void);      // frame prologue variant d (no return used)
extern void     FUN_80382724(void);      // epilogue d
extern long long FUN_8027ea98(void);     // resolve co-op partner (packed hi=entity)
extern int  scene_raycast(int room, float* from, float* to, int a, float* b, float* c,
                          int* d, int mask, int flags, int* e);
extern void aiactor_set_relation_visible(int viewer, int targetTeam, int visible);
extern int  aiactor_check_zone_target_players(int frame);
extern void aiactor_turn_toward_position(int entity, int* pos, int instant);
extern void aiactor_melee_swipe(int entity, int a, int b);
extern void aiactor_compute_flee_target(int entity, float* out);
extern void entity_build_local_matrix(float* outMtx, int entity, int a, int b);
extern void mtx4_transform_point_alias(float* mtx, float* pt);
extern int  anim_clip_flags_active_slot(int entity, int a, int mask, int flags);
extern void switch_use_handler(void);
extern void sound_init_play_params(int params);
extern int  sound_play_request(int* params);
extern int  pickup_blink_start(double a, double b, int rec);
extern void pickup_blink_stop(int rec);
extern int  object_melee_thrown_fire(int a, double b, int entity, int c, int d, int e);
extern void object_sound_slot_accumulate(int* slot, int soundId, int amount);
extern int  player_ai_drop_all_weapons(int a, double b, double c, double d,
                                       int e, int f, int g, int h, int* player);
extern void player_ai_play_voice_sound(int* actorState, int lineId);
extern int  statlvl_class_is_state5(int classId);
extern int  entity_tag_count(int tag);
extern int  floor_height_from_position(double x, double z);
extern int  soundpad_find_nearest_entity(double a, float* pos);

extern int   DAT_806120fc;               // active player-slot span
extern int   DAT_80612098;               // spawnvis entity count
extern int   DAT_80588610;               // spawnvis entity table base
extern int   DAT_80612614;               // difficulty/health-pct global
extern int   DAT_8043e23c;               // weapon->sound-class table
extern int   DAT_806120dc;               // voice-line gate a
extern int   DAT_806120e8;               // voice-line gate b (one-shot)
extern int   DAT_8061208c;               // level-def / trigger descriptor table base

extern float FLOAT_80618fdc;
extern float FLOAT_80618ff4;
extern float FLOAT_8061903c;
extern float FLOAT_80619038;
extern float FLOAT_80619040;
extern float FLOAT_80619044;
extern float FLOAT_80619048;
extern float FLOAT_8061904c;
extern float FLOAT_80619050;
extern float FLOAT_80619054;
}

// ---------------------------------------------------------------------------
// aiactor_player_los_sweep 0x802603a4
// Per-frame local-player target-visibility sweep (called from aiactor_tick_update,
// round-robin over active player slots via DAT_806120f8). Resolves the current
// player's aim/target point, picks a candidate target (co-op partner, team, or
// the nearest bot from the spawnvis table DAT_80588610), scene_raycasts from the
// player eye to the candidate and records mutual visibility via
// aiactor_set_relation_visible. Advances DAT_806120f8 to the next active slot.
// ---------------------------------------------------------------------------
void aiactor_player_los_sweep(void)
{
    bool bVar1;
    int iVar2;
    int iVar3;
    int iVar4;
    int iVar5;
    unsigned int uVar6;
    int iVar7;
    unsigned int uVar8;
    int iVar9;
    int iVar10;
    unsigned int uVar11;
    int iVar12;
    int* piVar13;
    long long uVar14;
    float local_90;
    float local_8c;
    float local_88;
    float local_84;
    float local_80;
    float local_7c;
    float local_78;
    float local_74;
    float local_70;
    float afStack_6c[27];

    FUN_803826d8();
    iVar9 = 0xfffffff;
    iVar10 = 0;
    bVar1 = false;
    piVar13 = (int*)(DAT_80611d90 + DAT_806120f8 * 0xea0);
    iVar7 = -1;
    if ((DAT_805015c0 & 0x200) != 0) goto sweep_done;
    if (*piVar13 == 0) {
        piVar13 = (int*)0;
    }
    iVar2 = DAT_806120f8;
    if (((((piVar13 != (int*)0) && (piVar13[1] != 0)) && (*(int*)(piVar13[1] + 0x20) == 8)) ||
         (((piVar13 != (int*)0 && (piVar13[1] != 0)) && ((piVar13[0xb] & 0x200U) != 0)))) ||
        ((piVar13 != (int*)0 && ((piVar13[5] & 0x1082000U) != 0)))) {
        iVar7 = 0;
        if ((DAT_805015bd == '\n') && (DAT_80612610 == 1)) {
            uVar14 = FUN_8027ea98();
            iVar4 = (int)((unsigned long long)uVar14 >> 0x20);
            iVar2 = (int)uVar14;
            if ((iVar4 != 0) && (iVar2 = DAT_806120f8, *(int*)(iVar4 + 8) != DAT_806120f8)) {
                iVar2 = aiactor_relation_entry(*(int*)(iVar4 + 8), DAT_806120f8);
                iVar12 = *(int*)(iVar2 + 4);
                iVar2 = DAT_806120f8;
                iVar3 = aiactor_relation_entry(0, DAT_806120f8);
                if (iVar12 < *(int*)(iVar3 + 4)) {
                    iVar7 = *(int*)(iVar4 + 8);
                }
            }
        } else if (((*(int*)(DAT_80611d90 + 0x84) == *(int*)(DAT_80611d90 + 0xf24)) &&
                    (DAT_806120f8 != 1)) &&
                   (*(int*)(DAT_80611d90 + 0xeb4) != 0 || *(int*)(DAT_80611d90 + 0xeb0) != 0)) {
            iVar2 = aiactor_relation_entry(1, DAT_806120f8);
            iVar3 = *(int*)(iVar2 + 4);
            iVar2 = DAT_806120f8;
            iVar4 = aiactor_relation_entry(0, DAT_806120f8);
            if (iVar3 < *(int*)(iVar4 + 4)) {
                iVar7 = 1;
            }
        }
    }
    if (((piVar13 == (int*)0) || (iVar2 = piVar13[1], iVar2 == 0)) || (*(int*)(iVar2 + 0x20) != 8)) {
        if ((piVar13 != (int*)0) && (piVar13[1] != 0)) {
            if ((piVar13[0xb] & 0x200U) == 0) {
                iVar7 = -1;
            } else {
                bVar1 = true;
                piVar13[0xb] = piVar13[0xb] & 0xfffffdff;
            }
        }
    } else {
        iVar4 = *(int*)(*(int*)(iVar2 + 0x7c) + 8);
        if ((DAT_80612610 <= iVar4) && (iVar2 != *piVar13)) {
            iVar7 = iVar4;
        }
    }
    if (iVar7 != -1) {
        if ((DAT_805015bd == '\n') && (DAT_80612610 == 1)) {
            iVar2 = (int)FUN_8027ea98();
            if (iVar2 == 0) {
                iVar4 = DAT_80612098 << 1;
            } else {
                iVar4 = DAT_80612098 * 3;
            }
        } else {
            iVar4 = DAT_80612098 << 1;
        }
        iVar2 = DAT_806120f8;
        iVar3 = aiactor_relation_entry(iVar7, DAT_806120f8);
        if (DAT_80611928 - *(int*)(iVar3 + 4) < iVar4) {
            iVar3 = 0;
            for (iVar4 = 0; iVar4 < DAT_80612098; iVar4 = iVar4 + 1) {
                iVar12 = *(int*)((int)&DAT_80588610 + iVar3);
                iVar2 = DAT_806120f8;
                if ((*(int*)(iVar12 + 8) != DAT_806120f8) &&
                    (iVar5 = aiactor_relation_entry(*(int*)(iVar12 + 8), DAT_806120f8),
                     *(int*)(iVar5 + 4) < iVar9)) {
                    iVar2 = DAT_806120f8;
                    iVar7 = aiactor_relation_entry(*(int*)(iVar12 + 8), DAT_806120f8);
                    iVar9 = *(int*)(iVar7 + 4);
                    iVar7 = *(int*)(iVar12 + 8);
                }
                iVar3 = iVar3 + 4;
            }
        }
    }
    if ((piVar13 != (int*)0) && (*piVar13 != 0)) {
        iVar9 = anim_clip_flags_active_slot(*piVar13, iVar2, 0x1800, 0x82);
        if ((iVar9 == 0) || ((piVar13[5] & 0x60000U) != 0)) {
            iVar9 = *piVar13;
            if (*(char*)(iVar9 + 0x32) == '\x02') {
                local_78 = (float)piVar13[0x2ed];
                local_74 = (float)piVar13[0x2ee];
                local_70 = (float)piVar13[0x2ef];
            } else {
                if ((*(unsigned int*)(iVar9 + 0x28) & 0x200000) == 0) {
                    iVar2 = 0;
                } else {
                    iVar2 = *(int*)(*(int*)(*(int*)(iVar9 + 4) + 0x84) + 0x18);
                }
                entity_build_local_matrix(afStack_6c, iVar9, 1, 0);
                iVar9 = *(int*)(*(int*)(*piVar13 + 4) + 0x10) + iVar2 * 0x40;
                local_78 = *(float*)(iVar9 + 0x30);
                local_74 = *(float*)(iVar9 + 0x34);
                local_70 = *(float*)(iVar9 + 0x38);
                mtx4_transform_point_alias(afStack_6c, &local_78);
            }
        } else {
            iVar9 = *piVar13;
            local_78 = *(float*)(iVar9 + 100);
            local_74 = (float)piVar13[0x2ee];
            if (ABS(local_74 - *(float*)(iVar9 + 0x68)) < FLOAT_80619038) {
                local_74 = *(float*)(iVar9 + 0x68) + *(float*)(iVar9 + 0x1ac);
            }
            local_70 = *(float*)(*piVar13 + 0x6c);
        }
    }
    iVar9 = DAT_80611d90;
    if ((iVar7 == -1) || (iVar7 == DAT_806120f8)) {
        if ((piVar13 != (int*)0) && (((piVar13[1] != 0 && (iVar7 != DAT_806120f8)) && (!bVar1)))) {
            uVar11 = *(unsigned int*)(*piVar13 + 0x28);
            *(unsigned int*)(*piVar13 + 0x28) = uVar11 & 0xffffff9f;
            local_84 = (float)piVar13[0x2e2];
            local_90 = local_84 - local_78;
            local_80 = (float)piVar13[0x2e3];
            local_8c = local_80 - local_74;
            local_7c = (float)piVar13[0x2e4];
            local_88 = local_7c - local_70;
            uVar8 = piVar13[5];
            if ((uVar8 & 0x8000) == 0) {
                if (DAT_805015bd != '\n') {
                    uVar8 = piVar13[7];
                }
                if ((uVar8 & 0x100) != 0) {
                    iVar7 = scene_raycast(*(int*)(*piVar13 + 0x34), &local_78, &local_90, 0,
                                          (float*)0, (float*)0, (int*)0, 0x2410270, 1, (int*)0);
                } else {
                    iVar7 = scene_raycast(*(int*)(*piVar13 + 0x34), &local_78, &local_90, 0,
                                          (float*)0, (float*)0, (int*)0, 0x2410070, 1, (int*)0);
                }
            } else {
                iVar7 = scene_raycast(*(int*)(*piVar13 + 0x34), &local_78, &local_90, 0,
                                      (float*)0, (float*)0, (int*)0, 0x2410270, 1, (int*)0);
            }
            piVar13[0xb] = piVar13[0xb] | 0x200;
            if (iVar7 == piVar13[1]) {
                piVar13[0x2b7] = 1;
            } else if ((iVar7 == 0) || (*(int*)(iVar7 + 0x20) != 8)) {
                piVar13[0x2b7] = 0;
            } else {
                aiactor_set_relation_visible(DAT_806120f8, *(int*)(*(int*)(iVar7 + 0x7c) + 8), 1);
                piVar13[0x2b7] = 0;
            }
            *(unsigned int*)(*piVar13 + 0x28) = uVar11;
        }
    } else {
        iVar4 = iVar7 * 0xea0;
        uVar8 = *(unsigned int*)(*piVar13 + 0x28);
        iVar2 = *(int*)(DAT_80611d90 + iVar4);
        *(unsigned int*)(*piVar13 + 0x28) = uVar8 & 0xffffff9f;
        iVar9 = anim_clip_flags_active_slot(iVar2, iVar9, 0x1800, 0x82);
        if ((iVar9 == 0) || ((*(unsigned int*)(DAT_80611d90 + iVar4 + 0x14) & 0x60000) != 0)) {
            if (*(char*)(iVar2 + 0x32) == '\x02') {
                iVar9 = *(int*)(iVar2 + 0x7c);
                local_84 = *(float*)(iVar9 + 0xbb4);
                local_80 = *(float*)(iVar9 + 3000);
                local_7c = *(float*)(iVar9 + 0xbbc);
            } else {
                if ((*(unsigned int*)(iVar2 + 0x28) & 0x200000) == 0) {
                    iVar9 = 0;
                } else {
                    iVar9 = *(int*)(*(int*)(*(int*)(iVar2 + 4) + 0x84) + 0x18);
                }
                entity_build_local_matrix(afStack_6c, iVar2, 1, 0);
                iVar9 = *(int*)(*(int*)(iVar2 + 4) + 0x10) + iVar9 * 0x40;
                local_84 = *(float*)(iVar9 + 0x30);
                local_80 = *(float*)(iVar9 + 0x34);
                local_7c = *(float*)(iVar9 + 0x38);
                mtx4_transform_point_alias(afStack_6c, &local_84);
            }
        } else {
            local_84 = *(float*)(iVar2 + 100);
            local_80 = *(float*)(*(int*)(iVar2 + 0x7c) + 3000);
            local_7c = *(float*)(iVar2 + 0x6c);
        }
        local_90 = local_84 - local_78;
        local_8c = local_80 - local_74;
        local_88 = local_7c - local_70;
        if (*(int*)(*piVar13 + 0x34) != 0) {
            uVar11 = 1;
            bool useFlag41 = false;
            if (piVar13[2] < DAT_80612610) {
                iVar10 = DAT_80611d90 + iVar4;
                if (DAT_80612610 <= *(int*)(iVar10 + 8)) {
                    uVar6 = (DAT_805015bd == '\n') ? *(unsigned int*)(iVar10 + 0x14)
                                                   : *(unsigned int*)(iVar10 + 0x1c);
                    if (((uVar6 & 0x2000000) != 0) ||
                        ((iVar10 = player_ai_is_valid_weapon_type(
                              (int)*(short*)(DAT_80611d90 + iVar4 + 0x48)),
                          iVar10 != 0 &&
                          ((iVar10 = *(int*)(DAT_80611d90 + iVar4 + 0x94), iVar10 == 0 ||
                            (iVar10 == 0x36)))))) {
                        useFlag41 = true;
                    }
                }
            } else {
                uVar6 = (DAT_805015bd == '\n') ? (unsigned int)piVar13[5]
                                               : (unsigned int)piVar13[7];
                if (((uVar6 & 0x2000000) == 0) &&
                    ((iVar10 = player_ai_is_valid_weapon_type((int)*(short*)(piVar13 + 0x12)),
                      iVar10 == 0 || ((piVar13[0x25] != 0 && (piVar13[0x25] != 0x36)))))) {
                    // fall through: re-evaluate first-branch predicate
                    iVar10 = DAT_80611d90 + iVar4;
                    if (DAT_80612610 <= *(int*)(iVar10 + 8)) {
                        uVar6 = (DAT_805015bd == '\n') ? *(unsigned int*)(iVar10 + 0x14)
                                                       : *(unsigned int*)(iVar10 + 0x1c);
                        if (((uVar6 & 0x2000000) != 0) ||
                            ((iVar10 = player_ai_is_valid_weapon_type(
                                  (int)*(short*)(DAT_80611d90 + iVar4 + 0x48)),
                              iVar10 != 0 &&
                              ((iVar10 = *(int*)(DAT_80611d90 + iVar4 + 0x94), iVar10 == 0 ||
                                (iVar10 == 0x36)))))) {
                            useFlag41 = true;
                        }
                    }
                } else {
                    useFlag41 = true;
                }
            }
            if (useFlag41) { uVar11 = 0x41; }
            iVar10 = scene_raycast(*(int*)(*piVar13 + 0x34), &local_78, &local_90, 0,
                                   (float*)0, (float*)0, (int*)0, 0x2410270, uVar11, (int*)0);
        }
        if (iVar10 == iVar2) {
            aiactor_set_relation_visible(DAT_806120f8, iVar7, 1);
            if (iVar2 == piVar13[1]) {
                piVar13[0x2b7] = 1;
            }
        } else if ((iVar10 == 0) || (*(int*)(iVar10 + 0x20) != 8)) {
            aiactor_set_relation_visible(DAT_806120f8, iVar7, 0);
            if (iVar2 == piVar13[1]) {
                piVar13[0x2b7] = 0;
            }
        } else {
            aiactor_set_relation_visible(DAT_806120f8, *(int*)(*(int*)(iVar10 + 0x7c) + 8), 1);
            aiactor_set_relation_visible(DAT_806120f8, iVar7, 0);
            if (iVar2 == piVar13[1]) {
                piVar13[0x2b7] = 0;
            }
        }
        *(unsigned int*)(*piVar13 + 0x28) = uVar8;
    }
    iVar7 = DAT_806120f8;
    do {
        do {
            DAT_806120f8 = DAT_806120f8 + 1;
            if (DAT_80612610 + DAT_806120fc <= DAT_806120f8) {
                DAT_806120f8 = DAT_80612610;
            }
            if (iVar7 == DAT_806120f8) goto sweep_done;
            iVar10 = DAT_80611d90 + DAT_806120f8 * 0xea0;
        } while ((*(unsigned int*)(iVar10 + 0x14) & 0x4000000) != 0);
        if (DAT_805015bd == '\n') {
            iVar9 = *(int*)(iVar10 + 0x10);
            iVar10 = *(int*)(iVar10 + 0x14);
        } else {
            iVar9 = *(int*)(iVar10 + 0x18);
            iVar10 = *(int*)(iVar10 + 0x1c);
        }
    } while (iVar10 == 0 && iVar9 == 0);
sweep_done:
    FUN_80382724();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_play_alert_voice 0x80260dd0
// One-shot: when param_2==1 and the voice gates permit (DAT_806120dc==0 &&
// DAT_806120e8!=0), play the actor's alert voice line and clear the gate.
// Called from aiactor_alert_broadcast_group.
// ---------------------------------------------------------------------------
void aiactor_play_alert_voice(int entity, int mode)
{
    if (((mode == 1) && (DAT_806120dc == 0)) && (DAT_806120e8 != 0)) {
        player_ai_play_voice_sound(*(int**)(entity + 0x7c), 1);
        DAT_806120e8 = 0;
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_investigate_tick 0x80260e24
// Investigate/approach behaviour-state tick. State 2 follows the zone path to a
// point of interest (+0xae8) and plays walk/run anims by distance; state 0 picks
// walk vs alert-walk by capability bit 8; state 1 checks zone target players;
// states <4 run the "use switch" reach animation. Dispatched from FUN_80282570.
// ---------------------------------------------------------------------------
void aiactor_behavior_investigate_tick(void)
{
    float fVar1;
    float fVar2;
    float fVar3;
    int iVar4;
    unsigned int uVar5;
    int iVar6;
    int actorState;
    double dVar8;

    iVar4 = FUN_803826f0();
    actorState = *(int*)(iVar4 + 0x7c);
    aiactor_retarget_visible_teammate();
    apply_pending_state_swap(actorState);
    advance_loco_timer(actorState);

    dVar8 = (double)FLOAT_8061900c;
    *(float*)(actorState + 0xad0) = FLOAT_80618fe0 * *(float*)(actorState + 0xb18);
    anim_set_playback_rate(dVar8, *(int**)(iVar4 + 4), 1);
    fVar3 = FLOAT_80618fdc;
    iVar6 = *(int*)(actorState + 0x20);
    if (iVar6 == 2) {
        iVar6 = aiactor_follow_zone_path();
        fVar3 = FLOAT_80619028;
        if (iVar6 == 0) {
            if (anim_is_clip_active(iVar4, 0x2a) == 0) {
                anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
            }
        } else {
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x30) = *(unsigned int*)(*(int*)(actorState + 0xae8) + 100);
            *(unsigned int*)(actorState + 0x34) = *(unsigned int*)(*(int*)(actorState + 0xae8) + 0x68);
            *(unsigned int*)(actorState + 0x38) = *(unsigned int*)(*(int*)(actorState + 0xae8) + 0x6c);
            fVar1 = *(float*)(iVar4 + 0x6c) - *(float*)(actorState + 0x38);
            fVar2 = *(float*)(iVar4 + 100) - *(float*)(actorState + 0x30);
            if (fVar3 <= fVar2 * fVar2 + fVar1 * fVar1) {
                if (anim_is_clip_active(iVar4, 0x2a) == 0) {
                    anim_play_scaled((double)FLOAT_80619038, (double)FLOAT_80618fb8);
                }
            } else {
                if (anim_is_clip_active(iVar4, 0x25) == 0) {
                    anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
                }
                if (*(int*)(actorState + 0x20) != 3) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(unsigned int*)(actorState + 0x20) = 3;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
                }
            }
        }
    } else if (iVar6 < 2) {
        if (iVar6 == 0) {
            if ((*(unsigned int*)(*(int*)(actorState + 0xbf0) + 0x54) & 8) == 0) {
                *(unsigned int*)(actorState + 0x9d4) = 0;
                *(unsigned int*)(actorState + 0x20) = 1;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            } else {
                *(unsigned int*)(actorState + 0x9d4) = 0;
                *(unsigned int*)(actorState + 0x20) = 2;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
            if (anim_is_clip_active(iVar4, 0x2a) == 0) {
                anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
            }
        } else if (((-1 < iVar6) && (iVar6 = aiactor_check_zone_target_players(iVar4), iVar6 != 0)) &&
                   (*(int*)(actorState + 0x20) != 2)) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = 2;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
    } else if (iVar6 == 4) {
        *(unsigned int*)(actorState + 0x30) = *(unsigned int*)(*(int*)(actorState + 4) + 100);
        *(unsigned int*)(actorState + 0x34) = *(unsigned int*)(*(int*)(actorState + 4) + 0x68);
        *(unsigned int*)(actorState + 0x38) = *(unsigned int*)(*(int*)(actorState + 4) + 0x6c);
        if (ABS(*(float*)(actorState + 0xae0)) < fVar3) {
            if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                            : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
                *(unsigned int*)(actorState + 0xaac) = 8;
                *(unsigned int*)(actorState + 0xaa8) = 0;
            }
        }
    } else if (iVar6 < 4) {
        *(unsigned int*)(actorState + 0x30) = *(unsigned int*)(*(int*)(actorState + 0xae8) + 100);
        *(unsigned int*)(actorState + 0x34) = *(unsigned int*)(*(int*)(actorState + 0xae8) + 0x68);
        *(unsigned int*)(actorState + 0x38) = *(unsigned int*)(*(int*)(actorState + 0xae8) + 0x6c);
        iVar6 = anim_is_clip_active(iVar4, 0x77);
        if (iVar6 == 0) {
            anim_play_scaled((double)FLOAT_8061903c, (double)FLOAT_80618fb8);
        } else {
            iVar6 = anim_active_slot_userflags(*(int*)(iVar4 + 4));
            if (iVar6 != 0) {
                switch_use_handler();
                if (*(int*)(actorState + 0x20) != 4) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(unsigned int*)(actorState + 0x20) = 4;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
                }
            }
        }
    }
    aiactor_leader_zone_update(iVar4);
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_alert_sound 0x80261270
// Small state handler: on entering state 6, arm the alert-blink effect
// (+0xc68 block) and fire a randomly-chosen alert SFX at the actor position;
// on state 2, stop the blink effect.
// ---------------------------------------------------------------------------
void aiactor_behavior_alert_sound(void)
{
    int iVar2;
    int iVar3;
    int actorState;
    long long uVar5;
    int local_58[7];
    unsigned int local_3c;
    unsigned int local_38;
    unsigned int local_34;
    unsigned char local_30;
    unsigned int local_28;
    unsigned int uStack_24;
    long long local_20;

    uVar5 = (long long)FUN_803826f0();
    iVar2 = (int)((unsigned long long)uVar5 >> 0x20);
    iVar3 = (int)uVar5;
    actorState = *(int*)(iVar2 + 0x7c);
    if (iVar3 == 6) {
        *(float*)(actorState + 0xc90) = FLOAT_80619040;
        *(unsigned int*)(actorState + 0xc6c) = *(unsigned int*)(actorState + 0xc6c) | 0x10;
        *(unsigned int*)(actorState + 0xc88) = 1;
        *(float*)(actorState + 0xc8c) = FLOAT_80618f98;
        uStack_24 = rng_counter_tick();
        local_28 = 0x43300000;
        (void)local_28;
        iVar3 = (int)(FLOAT_80618fdc *
                      ((float)((double)CONCAT44(0x43300000, uStack_24) - DOUBLE_80618fe8) /
                       FLOAT_80618fd8) + FLOAT_80618fd0);
        local_20 = (long long)iVar3;
        (void)local_20;
        sound_init_play_params((int)local_58);
        local_30 = 1;
        (void)local_30;
        local_3c = *(unsigned int*)(iVar2 + 100);
        (void)local_3c;
        local_38 = *(unsigned int*)(iVar2 + 0x68);
        (void)local_38;
        local_34 = *(unsigned int*)(iVar2 + 0x6c);
        (void)local_34;
        local_58[0] = iVar3 + 0x13f0;
        sound_play_request(local_58);
    } else if ((iVar3 < 6) && (iVar3 == 2)) {
        pickup_blink_stop(actorState + 0xc68);
    }
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_play_idle_bark_sound 0x8026136c
// Play a random idle animation variant and fire a randomly-selected positional
// bark SFX at the actor's position. Helper for aiactor_behavior_creature_tick.
// ---------------------------------------------------------------------------
void aiactor_play_idle_bark_sound(int* actorPtr)
{
    int iVar1;
    int iVar2;
    int local_48[7];
    unsigned int local_2c;
    unsigned int local_28;
    unsigned int local_24;
    unsigned char local_20;
    unsigned int local_18;
    unsigned int uStack_14;
    long long local_10;

    uStack_14 = rng_counter_tick();
    local_18 = 0x43300000;
    (void)local_18;
    if ((float)((double)CONCAT44(0x43300000, uStack_14) - DOUBLE_80618fe8) / FLOAT_80618fd8 <=
        FLOAT_80618fd0) {
        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
    } else {
        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
    }
    iVar2 = *actorPtr;
    uStack_14 = rng_counter_tick();
    local_18 = 0x43300000;
    (void)local_18;
    iVar1 = (int)(FLOAT_80618fdc *
                  ((float)((double)CONCAT44(0x43300000, uStack_14) - DOUBLE_80618fe8) /
                   FLOAT_80618fd8) + FLOAT_80618fd0);
    local_10 = (long long)iVar1;
    (void)local_10;
    sound_init_play_params((int)local_48);
    local_20 = 1;
    (void)local_20;
    local_2c = *(unsigned int*)(iVar2 + 100);
    (void)local_2c;
    local_28 = *(unsigned int*)(iVar2 + 0x68);
    (void)local_28;
    local_24 = *(unsigned int*)(iVar2 + 0x6c);
    (void)local_24;
    local_48[0] = iVar1 + 0x13f0;
    sound_play_request(local_48);
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_creature_tick 0x80261470
// 7-state creature/critter behaviour FSM (state field = actorState[8]):
//   0 idle/acquire   1 alert   2 wander   3 flee   4 forage/peck
//   5 melee-swipe    6 attack-throw
// Picks state transitions from rng + distance + clip userflags, plays the
// per-state animation set (clips 0x57b-0x591), computes flee targets, does
// melee swipes and thrown-attack fire. Dispatched from FUN_80281da4.
// ---------------------------------------------------------------------------
void aiactor_behavior_creature_tick(int param_9)
{
    float fVar1;
    float fVar2;
    float fVar3;
    int iVar4;
    unsigned int uVar5;
    int* piVar6;
    double dVar7;
    int uVar8;
    double dVar9;
    int local_48[6];
    float local_30;
    unsigned int local_2c;
    unsigned int local_28;
    unsigned int local_24;
    unsigned char local_20;
    double local_18;
    double local_10;

    piVar6 = *(int**)(param_9 + 0x7c);
    aiactor_retarget_visible_teammate();
    if (-1 < piVar6[0x274]) {
        if (piVar6[0x274] != piVar6[8]) {
            piVar6[0x276] = (int)FLOAT_80618f98;
            piVar6[0x275] = piVar6[8];
            piVar6[8] = piVar6[0x274];
        }
        piVar6[0x274] = -1;
    }
    if ((piVar6[0xb] & 0x80U) == 0) {
        piVar6[0x2a4] = (int)FLOAT_80618f98;
    } else {
        local_18 = (double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000);
        double gain = (double)FLOAT_806108ec;
        piVar6[0x2a4] =
            (int)(float)(gain * (double)(FLOAT_80618fb8 / (float)(local_18 - DOUBLE_80618fc8)) +
                         (double)(float)piVar6[0x2a4]);
    }
    dVar7 = (double)FLOAT_80618fa8;
    piVar6[0xc] = *(int*)(param_9 + 100);
    dVar9 = (double)FLOAT_80619038;
    piVar6[0xd] = *(int*)(param_9 + 0x68);
    piVar6[0xe] = *(int*)(param_9 + 0x6c);
    piVar6[10] = piVar6[10] | 0x4000;
    uVar8 = pickup_blink_start(dVar7, dVar9, (int)(piVar6 + 0x31a));
    switch (piVar6[8]) {
    case 0:
        if (DAT_80612614 == 100) {
            player_ai_drop_all_weapons(uVar8, dVar9, (double)FLOAT_80618f98, (double)FLOAT_80618f98,
                                       0, 0, 0, 0, piVar6);
            piVar6[0x25] = 0x40;
            object_sound_slot_accumulate(
                piVar6, *(int*)(DAT_80610ba0 + (&DAT_8043e23c)[piVar6[0x25] * 0xf] * 0x29c + 0x14),
                10000);
        }
        aiactor_turn_toward_leader();
        iVar4 = aiactor_acquire_nearest_target(param_9, *(int*)(DAT_80611d78 + 0x14), 0);
        piVar6[1] = iVar4;
        if (piVar6[1] != 0) {
            if (((float)piVar6[0x2bd] < FLOAT_80619044) ||
                (((*(unsigned int*)(piVar6[0x2fc] + 0x54) & 0x80000) != 0 &&
                  ((float)piVar6[0x2bd] < FLOAT_80619048)))) {
                uVar5 = rng_counter_tick();
                local_18 = (double)CONCAT44(0x43300000, uVar5);
                piVar6[0x278] =
                    (int)(FLOAT_80619038 * ((float)(local_18 - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                          FLOAT_80619038 + (float)piVar6[0x2cc]);
                if (piVar6[8] != 1) {
                    piVar6[0x275] = piVar6[8];
                    piVar6[8] = 1;
                    piVar6[0x276] = (int)FLOAT_80618f98;
                    piVar6[0x274] = piVar6[8];
                }
                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80619014);
            } else {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                aiactor_compute_flee_target(param_9, (float*)(piVar6 + 0x29d));
                if (piVar6[8] != 3) {
                    piVar6[0x275] = piVar6[8];
                    piVar6[8] = 3;
                    piVar6[0x276] = (int)FLOAT_80618f98;
                    piVar6[0x274] = piVar6[8];
                }
            }
        }
        break;
    case 1:
        aiactor_turn_toward_leader();
        iVar4 = anim_is_clip_active(param_9, 0x581);
        if ((iVar4 == 0) && (iVar4 = anim_is_clip_active(param_9, 0x582), iVar4 == 0)) {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
        } else {
            iVar4 = anim_primary_clip_userflags(param_9, 0x581);
            if ((iVar4 != 0) || (iVar4 = anim_primary_clip_userflags(param_9, 0x582), iVar4 != 0)) {
                if (((float)piVar6[0x278] < (float)piVar6[0x2cc]) ||
                    (FLOAT_80619044 < (float)piVar6[0x2bd])) {
                    uVar5 = rng_counter_tick();
                    local_18 = (double)CONCAT44(0x43300000, uVar5);
                    iVar4 = (int)(FLOAT_80618fdc * ((float)(local_18 - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                                  FLOAT_80618fd0);
                    local_10 = (double)(long long)iVar4;
                    (void)local_10;
                    if ((float)piVar6[0x2bd] < FLOAT_80619010) {
                        iVar4 = 6;
                    }
                    if (((*(unsigned int*)(piVar6[0x2fc] + 0x54) & 0x80000) != 0) && (3 < iVar4)) {
                        iVar4 = 3;
                    }
                    if (iVar4 == 4) {
                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                        aiactor_compute_flee_target(param_9, (float*)(piVar6 + 0x29d));
                        if (piVar6[8] != 3) {
                            piVar6[0x275] = piVar6[8];
                            piVar6[8] = 3;
                            piVar6[0x276] = (int)FLOAT_80618f98;
                            piVar6[0x274] = piVar6[8];
                        }
                    } else if (iVar4 < 4) {
                        if (iVar4 < 3) {
                            if (-1 < iVar4) {
                                uVar5 = rng_counter_tick();
                                local_10 = (double)CONCAT44(0x43300000, uVar5);
                                if ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8 <= FLOAT_80618fd0) {
                                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                                } else {
                                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                                }
                                if (piVar6[8] != 4) {
                                    piVar6[0x275] = piVar6[8];
                                    piVar6[8] = 4;
                                    piVar6[0x276] = (int)FLOAT_80618f98;
                                    piVar6[0x274] = piVar6[8];
                                }
                                uVar5 = rng_counter_tick();
                                local_10 = (double)CONCAT44(0x43300000, uVar5);
                                piVar6[0x2cc] = (int)((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8);
                            }
                        } else {
                            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                            if (piVar6[8] != 6) {
                                piVar6[0x275] = piVar6[8];
                                piVar6[8] = 6;
                                piVar6[0x276] = (int)FLOAT_80618f98;
                                piVar6[0x274] = piVar6[8];
                            }
                        }
                    } else if (iVar4 == 6) {
                        aiactor_play_idle_bark_sound(piVar6);
                        if (piVar6[8] != 5) {
                            piVar6[0x275] = piVar6[8];
                            piVar6[8] = 5;
                            piVar6[0x276] = (int)FLOAT_80618f98;
                            piVar6[0x274] = piVar6[8];
                        }
                    } else if (iVar4 < 6) {
                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                        if (piVar6[8] != 2) {
                            piVar6[0x275] = piVar6[8];
                            piVar6[8] = 2;
                            piVar6[0x276] = (int)FLOAT_80618f98;
                            piVar6[0x274] = piVar6[8];
                        }
                    }
                } else {
                    iVar4 = anim_is_clip_active(param_9, 0x582);
                    if (iVar4 == 0) {
                        uVar5 = rng_counter_tick();
                        local_10 = (double)CONCAT44(0x43300000, uVar5);
                        if ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8 <= FLOAT_80618fac) {
                            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                            return;
                        }
                    }
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                }
            }
        }
        break;
    case 2:
        aiactor_turn_toward_leader();
        iVar4 = anim_is_clip_active(param_9, 0x586);
        if ((((iVar4 == 0) && (iVar4 = anim_is_clip_active(param_9, 0x58b), iVar4 == 0)) &&
             (iVar4 = anim_is_clip_active(param_9, 0x57f), iVar4 == 0)) &&
            (((iVar4 = anim_is_clip_active(param_9, 0x57e), iVar4 == 0 &&
               (iVar4 = anim_is_clip_active(param_9, 0x589), iVar4 == 0)) &&
              (iVar4 = anim_is_clip_active(param_9, 0x580), iVar4 == 0)))) {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
        } else {
            iVar4 = anim_primary_clip_userflags(param_9, 0x586);
            if (iVar4 == 0) {
                iVar4 = anim_primary_clip_userflags(param_9, 0x580);
                if (iVar4 == 0) {
                    iVar4 = anim_primary_clip_userflags(param_9, 0x589);
                    if (iVar4 == 0) {
                        iVar4 = anim_primary_clip_userflags(param_9, 0x57e);
                        if ((iVar4 != 0) ||
                            (iVar4 = anim_primary_clip_userflags(param_9, 0x57f), iVar4 != 0)) {
                            uVar5 = rng_counter_tick();
                            local_10 = (double)CONCAT44(0x43300000, uVar5);
                            iVar4 = (int)(FLOAT_8061904c * ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                                          FLOAT_80618fd0);
                            local_18 = (double)(long long)iVar4;
                            (void)local_18;
                            if ((float)piVar6[0x2bd] < FLOAT_80619044) {
                                iVar4 = 0;
                            }
                            if (iVar4 == 1) {
                                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                            } else {
                                if (iVar4 < 1) {
                                    if (-1 < iVar4) {
                                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                                        return;
                                    }
                                } else if (iVar4 < 5) {
                                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                                    return;
                                }
                                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                            }
                        }
                    } else {
                        if (piVar6[8] != 3) {
                            piVar6[0x275] = piVar6[8];
                            piVar6[8] = 3;
                            piVar6[0x276] = (int)FLOAT_80618f98;
                            piVar6[0x274] = piVar6[8];
                        }
                        aiactor_compute_flee_target(param_9, (float*)(piVar6 + 0x29d));
                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                    }
                } else {
                    uVar5 = rng_counter_tick();
                    local_10 = (double)CONCAT44(0x43300000, uVar5);
                    piVar6[0x278] =
                        (int)(FLOAT_80619038 * ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                              FLOAT_80619038 + (float)piVar6[0x2cc]);
                    if (piVar6[8] != 1) {
                        piVar6[0x275] = piVar6[8];
                        piVar6[8] = 1;
                        piVar6[0x276] = (int)FLOAT_80618f98;
                        piVar6[0x274] = piVar6[8];
                    }
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                }
            } else {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
            }
        }
        break;
    case 3:
        if (FLOAT_80619048 <= (float)piVar6[0x2bd]) {
            iVar4 = anim_active_slot_userflags(*(int*)(param_9 + 4));
            if ((iVar4 != 0) ||
                (fVar1 = (float)piVar6[0x29d] - *(float*)(param_9 + 100),
                 fVar2 = (float)piVar6[0x29e] - *(float*)(param_9 + 0x68),
                 fVar3 = (float)piVar6[0x29f] - *(float*)(param_9 + 0x6c),
                 fVar3 * fVar3 + fVar1 * fVar1 + fVar2 * fVar2 < FLOAT_80619050)) {
                if ((float)piVar6[0x2bd] <= FLOAT_8061901c) {
                    uVar5 = rng_counter_tick();
                    local_10 = (double)CONCAT44(0x43300000, uVar5);
                    if ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8 <= FLOAT_80619054)
                        goto creature_c3_anim;
                }
                aiactor_compute_flee_target(param_9, (float*)(piVar6 + 0x29d));
            }
        } else {
            piVar6[0x29d] = *(int*)(piVar6[1] + 100);
            piVar6[0x29e] = *(int*)(piVar6[1] + 0x68);
            piVar6[0x29f] = *(int*)(piVar6[1] + 0x6c);
        }
creature_c3_anim:
        aiactor_turn_toward_position(param_9, piVar6 + 0x29d, 0);
        iVar4 = anim_is_clip_active(param_9, 0x58a);
        if ((((iVar4 == 0) && (iVar4 = anim_is_clip_active(param_9, 0x589), iVar4 == 0)) &&
             (iVar4 = anim_is_clip_active(param_9, 0x587), iVar4 == 0)) &&
            ((iVar4 = anim_is_clip_active(param_9, 0x58b), iVar4 == 0 &&
              (iVar4 = anim_is_clip_active(param_9, 0x588), iVar4 == 0)))) {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
        } else {
            iVar4 = anim_primary_clip_userflags(param_9, 0x589);
            if ((iVar4 == 0) && (iVar4 = anim_primary_clip_userflags(param_9, 0x58a), iVar4 == 0)) {
                iVar4 = anim_primary_clip_userflags(param_9, 0x588);
                if (iVar4 == 0) {
                    iVar4 = anim_primary_clip_userflags(param_9, 0x58b);
                    if (iVar4 == 0) {
                        iVar4 = anim_primary_clip_userflags(param_9, 0x587);
                        if (iVar4 != 0) {
                            fVar1 = (float)piVar6[0x2bd];
                            if (FLOAT_80619010 <= fVar1) {
                                if ((fVar1 < FLOAT_80619044) ||
                                    (((*(unsigned int*)(piVar6[0x2fc] + 0x54) & 0x80000) != 0 &&
                                      (fVar1 < FLOAT_80619048)))) {
                                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                                }
                            } else {
                                aiactor_play_idle_bark_sound(piVar6);
                                if (piVar6[8] != 5) {
                                    piVar6[0x275] = piVar6[8];
                                    piVar6[8] = 5;
                                    piVar6[0x276] = (int)FLOAT_80618f98;
                                    piVar6[0x274] = piVar6[8];
                                }
                            }
                        }
                    } else {
                        if (piVar6[8] != 2) {
                            piVar6[0x275] = piVar6[8];
                            piVar6[8] = 2;
                            piVar6[0x276] = (int)FLOAT_80618f98;
                            piVar6[0x274] = piVar6[8];
                        }
                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                    }
                } else {
                    uVar5 = rng_counter_tick();
                    local_10 = (double)CONCAT44(0x43300000, uVar5);
                    piVar6[0x278] =
                        (int)(FLOAT_80619038 * ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                              FLOAT_80619038 + (float)piVar6[0x2cc]);
                    if (piVar6[8] != 1) {
                        piVar6[0x275] = piVar6[8];
                        piVar6[8] = 1;
                        piVar6[0x276] = (int)FLOAT_80618f98;
                        piVar6[0x274] = piVar6[8];
                    }
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                }
            } else {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
            }
        }
        break;
    case 4:
        aiactor_turn_toward_leader();
        iVar4 = anim_is_clip_active(param_9, 0x58e);
        if (((iVar4 == 0) && (iVar4 = anim_is_clip_active(param_9, 0x58c), iVar4 == 0)) &&
            ((iVar4 = anim_is_clip_active(param_9, 0x58d), iVar4 == 0 &&
              (((iVar4 = anim_is_clip_active(param_9, 0x591), iVar4 == 0 &&
                 (iVar4 = anim_is_clip_active(param_9, 0x58f), iVar4 == 0)) &&
                (iVar4 = anim_is_clip_active(param_9, 0x590), iVar4 == 0)))))) {
            uVar5 = rng_counter_tick();
            local_10 = (double)CONCAT44(0x43300000, uVar5);
            if ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8 <= FLOAT_80618fd0) {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
            } else {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
            }
        } else {
            iVar4 = anim_primary_clip_userflags(param_9, 0x58e);
            if (iVar4 == 0) {
                iVar4 = anim_primary_clip_userflags(param_9, 0x591);
                if (iVar4 == 0) {
                    iVar4 = anim_primary_clip_userflags(param_9, 0x58d);
                    if ((iVar4 == 0) && (iVar4 = anim_primary_clip_userflags(param_9, 0x590), iVar4 == 0)) {
                        iVar4 = anim_is_clip_active(param_9, 0x58d);
                        if (((iVar4 == 0) && (iVar4 = anim_is_clip_active(param_9, 0x590), iVar4 == 0)) &&
                            (FLOAT_80618fb8 < (float)piVar6[0x2cc])) {
                            iVar4 = anim_is_clip_active(param_9, 0x58c);
                            if ((iVar4 == 0) && (iVar4 = anim_is_clip_active(param_9, 0x58e), iVar4 == 0)) {
                                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                            } else {
                                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                            }
                        }
                    } else {
                        if (piVar6[8] != 6) {
                            piVar6[0x275] = piVar6[8];
                            piVar6[8] = 6;
                            piVar6[0x276] = (int)FLOAT_80618f98;
                            piVar6[0x274] = piVar6[8];
                        }
                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                    }
                } else {
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                }
            } else {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
            }
        }
        break;
    case 5:
        iVar4 = anim_is_clip_active(param_9, 0x583);
        if ((iVar4 == 0) && (iVar4 = anim_is_clip_active(param_9, 0x584), iVar4 == 0)) {
            uVar5 = rng_counter_tick();
            local_10 = (double)CONCAT44(0x43300000, uVar5);
            piVar6[0x278] =
                (int)(FLOAT_80619038 * ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                      FLOAT_80619038 + (float)piVar6[0x2cc]);
            if (piVar6[8] != 1) {
                piVar6[0x275] = piVar6[8];
                piVar6[8] = 1;
                piVar6[0x276] = (int)FLOAT_80618f98;
                piVar6[0x274] = piVar6[8];
            }
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80619014);
        } else {
            iVar4 = anim_is_clip_active(param_9, 0x583);
            if ((iVar4 != 0) || (iVar4 = anim_is_clip_active(param_9, 0x584), iVar4 != 0)) {
                iVar4 = anim_active_slot_userflags(*(int*)(param_9 + 4));
                if (iVar4 == 0) {
                    aiactor_melee_swipe(param_9, 1, 0);
                } else {
                    uVar5 = rng_counter_tick();
                    local_10 = (double)CONCAT44(0x43300000, uVar5);
                    piVar6[0x278] =
                        (int)(FLOAT_80619038 * ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                              FLOAT_80619038 + (float)piVar6[0x2cc]);
                    if (piVar6[8] != 1) {
                        piVar6[0x275] = piVar6[8];
                        piVar6[8] = 1;
                        piVar6[0x276] = (int)FLOAT_80618f98;
                        piVar6[0x274] = piVar6[8];
                    }
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                }
            }
        }
        break;
    case 6:
        aiactor_turn_toward_leader();
        iVar4 = anim_is_clip_active(param_9, 0x585);
        if (((iVar4 == 0) && (iVar4 = anim_is_clip_active(param_9, 0x57c), iVar4 == 0)) &&
            (iVar4 = anim_is_clip_active(param_9, 0x57b), iVar4 == 0)) {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
        } else {
            iVar4 = anim_primary_clip_userflags(param_9, 0x585);
            if (iVar4 == 0) {
                iVar4 = anim_primary_clip_userflags(param_9, 0x57c);
                if (iVar4 == 0) {
                    iVar4 = anim_primary_clip_userflags(param_9, 0x57b);
                    if (iVar4 != 0) {
                        uVar5 = rng_counter_tick();
                        local_10 = (double)CONCAT44(0x43300000, uVar5);
                        piVar6[0x278] =
                            (int)(FLOAT_80619038 * ((float)(local_10 - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                                  FLOAT_80619038 + (float)piVar6[0x2cc]);
                        if (piVar6[8] != 1) {
                            piVar6[0x275] = piVar6[8];
                            piVar6[8] = 1;
                            piVar6[0x276] = (int)FLOAT_80618f98;
                            piVar6[0x274] = piVar6[8];
                        }
                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                    }
                } else {
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80619014);
                }
            } else {
                dVar7 = (double)FLOAT_80618f98;
                dVar9 = (double)FLOAT_80619014;
                piVar6[0xb] = piVar6[0xb] | 0x400;
                piVar6[10] = piVar6[10] | 2;
                uVar8 = anim_play_scaled(dVar7, dVar9);
                object_melee_thrown_fire(uVar8, dVar9, param_9, 0, 0, 0);
                sound_init_play_params((int)local_48);
                local_20 = 1;
                (void)local_20;
                local_48[0] = 0xd30;
                local_30 = FLOAT_80618ff4;
                (void)local_30;
                local_2c = *(unsigned int*)(param_9 + 100);
                (void)local_2c;
                local_28 = *(unsigned int*)(param_9 + 0x68);
                (void)local_28;
                local_24 = *(unsigned int*)(param_9 + 0x6c);
                (void)local_24;
                sound_play_request(local_48);
            }
        }
        break;
    default:
        break;
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_search_tick 0x80262564
// Search / return-to-zone behaviour-state tick. State 1 follows the zone path
// (walk vs mounted anim), state 0 resolves the target sound-zone and re-enters
// state 1, state 2 plays the search idle and retargets; falls to queue an
// investigate action (0x4000000) if the entity has no active target.
// ---------------------------------------------------------------------------
void aiactor_behavior_search_tick(int entity)
{
    float fVar1;
    int iVar2;
    unsigned int uVar3;
    int actorState;

    actorState = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();
    apply_pending_state_swap(actorState);
    advance_loco_timer(actorState);

    iVar2 = *(int*)(actorState + 0x20);
    if (iVar2 == 1) {
        iVar2 = aiactor_follow_zone_path();
        if ((iVar2 != 0) && (*(int*)(actorState + 0x20) != 2)) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = 2;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
        if (*(int*)(actorState + 0x44) == 1) {
            if (anim_is_clip_active(entity, 0x113) == 0) {
                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
            }
        } else {
            if (anim_is_clip_active(entity, 0x26) == 0) {
                anim_play_scaled((double)FLOAT_8061903c, (double)FLOAT_80618fb8);
            }
        }
    } else if (iVar2 < 1) {
        if (-1 < iVar2) {
            iVar2 = soundpad_find_zone_by_id(*(int*)(*(int*)(actorState + 0xbf0) + 0x10));
            if (iVar2 < 0) {
                *(unsigned int*)(actorState + 0x684) =
                    *(unsigned int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                iVar2 = soundpad_find_zone_by_id(*(int*)(*(int*)(actorState + 0xbf0) + 0x10));
                *(int*)(actorState + 0x684) = iVar2;
            }
            *(unsigned int*)(actorState + 0x9ac) = 0;
            *(unsigned int*)(actorState + 0x9a8) = 0;
            if (*(int*)(actorState + 0x20) != 1) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 1;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        }
    } else if (iVar2 < 3) {
        if (anim_is_clip_active(entity, 0x25) == 0) {
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
        }
        iVar2 = aiactor_retarget_visible_teammate();
        if (iVar2 != 0) {
            if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
                uVar3 = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                               : *(unsigned int*)(actorState + 0x1c);
                if ((uVar3 & 1) == 0) {
                    return;
                }
            }
            *(unsigned int*)(actorState + 0xaac) = 8;
            *(unsigned int*)(actorState + 0xaa8) = 0;
            return;
        }
    }
    if ((*(unsigned int*)(entity + 0x10) & 0xffff) == 0) {
        if (*(int*)(actorState + 0xabc) == 0 && *(int*)(actorState + 0xab8) == 0) {
            *(unsigned int*)(actorState + 0xabc) = 8;
            *(unsigned int*)(actorState + 0xab8) = 0;
        }
        if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
            uVar3 = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                           : *(unsigned int*)(actorState + 0x1c);
            if ((uVar3 & 1) == 0) {
                return;
            }
        }
        *(unsigned int*)(actorState + 0xaac) = 0x4000000;
        *(unsigned int*)(actorState + 0xaa8) = 0;
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_spawn_trigger_eval 0x802628cc
// Evaluate an alert/spawn trigger descriptor (record at
// DAT_8061208c[+0x3c] + actorState[+0x9ec]*0x48). Tests the descriptor's
// condition flags (+0x34): timer expiry, tag count, class/alert state, LOS,
// team relation, and player-proximity/zone checks. Bails (no fire) when any
// gating condition holds. Dispatched from FUN_8026367c / FUN_80279374.
// ---------------------------------------------------------------------------
void aiactor_spawn_trigger_eval(void)
{
    float fVar1;
    float fVar2;
    float fVar3;
    int iVar4;
    int iVar5;
    unsigned int uVar6;
    char* pcVar7;
    int iVar8;
    int iVar9;
    int actorState;
    double dVar11;

    iVar4 = FUN_803826e8();
    actorState = *(int*)(iVar4 + 0x7c);
    iVar9 = *(int*)(DAT_8061208c + 0x3c) + *(int*)(actorState + 0x9ec) * 0x48;
    if ((((*(unsigned int*)(iVar9 + 0x34) & 0x8000) != 0) &&
         (*(float*)(iVar9 + 0x24) <= *(float*)(actorState + 0xb30))) ||
        (((*(unsigned int*)(iVar9 + 0x34) & 0x2000) != 0 &&
          (iVar5 = entity_tag_count(*(int*)(iVar9 + 0x20)), iVar5 == 0)))) {
        FUN_80382734();
        return;
    }
    if ((*(unsigned int*)(iVar9 + 0x34) & 0x800) == 0) {
        if ((*(unsigned int*)(iVar9 + 0x34) & 0x1000) != 0) {
            uVar6 = aiactor_alert_group_flagA_any(*(unsigned int*)(iVar9 + 0x2c));
            if (uVar6 != 0) { FUN_80382734(); return; }
        }
    } else {
        uVar6 = statlvl_class_is_state5(*(int*)(iVar9 + 0x28));
        if (uVar6 != 0) { FUN_80382734(); return; }
    }
    uVar6 = *(unsigned int*)(iVar9 + 0x34);
    if ((uVar6 & 0x20) == 0) {
        if ((uVar6 & 0x40) == 0) {
            if (((uVar6 & 0x80) != 0) && (iVar5 = aiactor_evaluate_target_los(), iVar5 != 0)) {
                FUN_80382734();
                return;
            }
        } else {
            iVar5 = aiactor_evaluate_target_los();
            if (iVar5 == 1) { FUN_80382734(); return; }
        }
    } else {
        iVar8 = 0;
        for (iVar5 = 0; iVar5 < DAT_80612610; iVar5 = iVar5 + 1) {
            pcVar7 = (char*)aiactor_relation_entry(
                *(int*)(actorState + 8),
                *(int*)(*(int*)(*(int*)(DAT_80611d78 + iVar8 + 0x14) + 0x7c) + 8));
            if (*pcVar7 != '\0') { FUN_80382734(); return; }
            iVar8 = iVar8 + 0x141c;
        }
    }
    uVar6 = *(unsigned int*)(iVar9 + 0x34);
    if ((uVar6 & 0x400) == 0) {
        if ((uVar6 & 0x4000) == 0) {
            if ((uVar6 & 0x200) == 0) {
                if ((uVar6 & 0x100) != 0) {
                    dVar11 = (double)FLOAT_80618f98;
                    iVar4 = 0;
                    iVar8 = 0;
                    while ((iVar4 < DAT_80612610 &&
                            ((iVar5 = *(int*)(DAT_80611d78 + iVar8 + 0x14),
                              iVar9 = *(int*)(iVar5 + 0x7c),
                              (double)*(float*)(iVar5 + 0x1cc) <= dVar11 ||
                              (iVar5 = soundpad_find_zone_by_id(*(int*)(iVar9 + 0x28)),
                               *(int*)(iVar9 + 0x678) != iVar5))))) {
                        iVar4 = iVar4 + 1;
                        iVar8 = iVar8 + 0x141c;
                    }
                }
            } else {
                iVar4 = (int)FUN_8027ea98();
                if (iVar4 != 0) {
                    soundpad_find_zone_by_id(*(int*)(iVar9 + 0x28));
                }
            }
        }
    } else {
        iVar5 = 0;
        int rem = DAT_80612610;
        if (0 < DAT_80612610) {
            do {
                iVar8 = *(int*)(DAT_80611d78 + iVar5 + 0x14);
                if ((FLOAT_80618f98 < *(float*)(iVar8 + 0x1cc)) &&
                    (fVar1 = *(float*)(iVar4 + 100) - *(float*)(iVar8 + 100),
                     fVar2 = *(float*)(iVar4 + 0x68) - *(float*)(iVar8 + 0x68),
                     fVar3 = *(float*)(iVar4 + 0x6c) - *(float*)(iVar8 + 0x6c),
                     fVar3 * fVar3 + fVar1 * fVar1 + fVar2 * fVar2 <
                     (float)((double)CONCAT44(0x43300000,
                                 *(int*)(iVar9 + 0x28) * *(int*)(iVar9 + 0x28) ^ 0x80000000) -
                             DOUBLE_80618fc8))) break;
                iVar5 = iVar5 + 0x141c;
                rem = rem + -1;
            } while (rem != 0);
        }
    }
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_set_alert_zone_slot 0x80262c34
// Set the actor's alert-trigger descriptor slot index (+0x9ec) from param_2 and
// arm a pending transition to behaviour-state 1, but only if the per-frame input
// mask permits (bit 0x80000). Called from worm_event_execute.
// ---------------------------------------------------------------------------
void aiactor_set_alert_zone_slot(int entity, unsigned int slot)
{
    unsigned int uVar1;
    int actorState;

    actorState = *(int*)(entity + 0x7c);
    uVar1 = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                   : *(unsigned int*)(actorState + 0x1c);
    if ((uVar1 & 0x80000) == 0) {
        return;
    }
    *(unsigned int*)(actorState + 0x9ec) = slot;
    if (*(int*)(actorState + 0x20) == 1) {
        return;
    }
    *(unsigned int*)(actorState + 0x9d0) = 1;
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_event_handler 0x80262c9c
// Behaviour event handler (event id param_2). Handles:
//   0x3ea  -> set can't-see flag (unless char-def bit 0x400), return -2
//   0x001  -> spawn/floor-place: floor-query the spawn point, snap to floor,
//             resolve nearest sound-zone entity, advance trigger slot, arm state1
//   0x3eb  -> force behaviour-state 100, stash param_3 into +0xaa0, return -2
// Returns 0 to pass the event on, 0xfffffffe (-2) to consume it.
// ---------------------------------------------------------------------------
unsigned int aiactor_behavior_event_handler(int entity, int eventId, unsigned int* param_3)
{
    unsigned int uVar1;
    int iVar2;
    int actorState;
    double dVar4;

    uVar1 = 0;
    if (eventId == 0x3ea) {
        if ((*(unsigned int*)(*(int*)(*(int*)(entity + 0x7c) + 0xbf0) + 0x58) & 0x400) == 0) {
            *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 1;
        }
        uVar1 = 0xfffffffe;
    } else if (eventId < 0x3ea) {
        if ((eventId == 1) &&
            (actorState = *(int*)(entity + 0x7c),
             (*(unsigned int*)(*(int*)(DAT_8061208c + 0x3c) + *(int*)(actorState + 0x9ec) * 0x48 + 0x34) &
              0x100000) != 0)) {
            iVar2 = *(short*)(actorState + 0x48) * 0x30;
            dVar4 = (double)floor_query_with_interact(
                (double)*(float*)(&DAT_8048ca94[iVar2]),
                (double)*(float*)(&DAT_8048ca9c[iVar2]), entity + 100, 0, (int*)0, 0);
            *(float*)(actorState + 0xbfc) = (float)dVar4;
            *(float*)(entity + 0x68) = (float)dVar4;
            uVar1 = floor_height_from_position((double)FLOAT_80619000, (double)FLOAT_80619000);
            *(unsigned int*)(entity + 0x34) = uVar1;
            iVar2 = soundpad_find_nearest_entity((double)FLOAT_80618f98, (float*)(entity + 100));
            *(int*)(actorState + 0x678) = iVar2;
            *(int*)(actorState + 0x9ec) = *(int*)(actorState + 0x9ec) + 1;
            if (*(int*)(actorState + 0x20) != 1) {
                *(unsigned int*)(actorState + 0x9d0) = 1;
            }
            uVar1 = 0xfffffffe;
        }
    } else if (eventId < 0x3ec) {
        actorState = *(int*)(entity + 0x7c);
        if (*(int*)(actorState + 0x20) != 100) {
            *(unsigned int*)(actorState + 0x9d0) = 100;
        }
        uVar1 = 0xfffffffe;
        *(unsigned int*)(actorState + 0xaa0) = *param_3;
    }
    return uVar1;
}

// ===========================================================================
// batch_741 | 2026-09-10 | 0x80262de8 - 0x80267383
// Scripted-actor / creature / mounted-weapon behaviour driver cluster.
//   aiactor_zone_crowd_cost        0x80262de8  crowd/avoidance cost over zone nodes
//   aiactor_locomotion_anim_tick   0x8026314c  walk/run/idle+alert locomotion anim driver
//   aiactor_scripted_update        0x8026367c  MASSIVE scripted-AI opcode/state driver
//   aiactor_play_proximity_bark    0x802649d0  proximity-gated bark SFX + HUD text
//   aiactor_is_grabbed_entity      0x80264b78  membership test in grabbed-entity list
//   aiactor_mounted_weapon_tick    0x80264bb8  AI operating a mounted/turret weapon (aim FSM)
//   aiactor_path_script_tick       0x80266564  scripted path-node follow FSM
//   aiactor_death_anim_tick        0x80266ba4  play death clip then destroy instance
//   aiactor_behavior_reaction_tick 0x80266cfc  play char-def reaction clip + commit action
//   aiactor_behavior_vehicle_ride_tick 0x80266ea8  MSUB/ARCC/2RTU vehicle-ride follow FSM
// ===========================================================================
extern "C" {
extern void     FUN_803826e4(void);      // frame prologue variant e
extern int      FUN_803826e4_ret(void);  // frame prologue variant e (returns entity handle)
extern long long FUN_803826e8_pair(void);// frame prologue variant b, packed (hi=entity, lo=state)
extern void     FUN_80382730(void);      // epilogue e
extern int      FUN_802b4844(int base, int variant);         // build locomotion anim clip id
extern double   FUN_803909ec(double x);  // trig helper (sin-ish)
extern double   FUN_80390324(double x);  // trig helper (angle wrap/normalize)

// ---- vector/trig math helpers (defined in other modules) -------------------
extern void   dir_to_angles(void* vec, void* outYaw, float* outPitch);
extern void   angles_to_dir(double yaw, double pitch, float* outVec);
extern double math_sqrt(double x);
extern double prop_hinge_wrap_angle_to_range(double a, float* pivot, float* targetPos);

// ---- scripted-driver helpers ----------------------------------------------
extern void  aiactor_turn_toward_entity(int entity, int target, int instant);
extern void  prop_hinge_apply_spring(double k, int entity, int pivot);
extern void  entity_set_render_type(int entity, int fn);
extern int   aiactor_zone_crowd_cost_ret(void);   // aiactor_zone_crowd_cost variant returning best zone
extern int   FUN_8027ea98_ret(void);              // resolve co-op partner (low = state ptr)
extern long long anim_play_scaled_pair(double startPhase, double rate); // packed (hi=played-clip-id)
extern int   aiactor_spawn_trigger_eval_ret(void);// spawn_trigger_eval variant returning 0/1/2 result
extern float FLOAT_80619068;

extern int  aiactor_movement_tick(long long a, double b, double c, double d,
                                  double e, double f, double g, long long h);
extern int  aiactor_facing_dot_test(double tol, int entity, float* targetPos);
extern void aiactor_play_turn_animation(int entity);
extern int  aiactor_destroy_instance(double a, double b, double c, double d,
                                     double e, double f, double g, long long h, int entity);
extern void aiactor_clear_bark_state(int entity);
extern int  aiactor_find_player_by_team(int teamTag);
extern int* soundpad_resolve_zone_node(int zoneId);
extern int  soundpad_get_entity_data(int zoneId, int* outList);
extern int  soundpad_get_entity_id_from_zone(int zoneId);
extern int  soundpad_find_trigger_by_id(int id);
extern int  sound_resolve_sample_id(int desc);
extern int  game_get_round_timer(void);
extern int  hud_create_localized_text(double a, double b, double c, double d, double e,
                                      long long f, long long g, long long h,
                                      int player, int kind, int desc, int arg,
                                      int a2, int a3, int a4, int a5);
extern unsigned char* weapon_get_def_from_entity(void);
extern void weapon_compute_fire_origin(int weaponEnt, int a, int b, float* c, float* out);
extern void prop_get_world_pivot(int entity, float* out);
extern void prop_compute_eye_point(void);
extern int  damage_dispatch_to_entity(double a, double b, double c, double d, double e,
                                      double f, double g, long long h, int* desc);
extern void damage_filter_invincible(void);
extern int  switch_slot_reset(int rec);
extern void player_ai_teleport_wrapper(int actorState, int zone, int flag);
extern double anim_get_clip_end_time_of(int entity);
extern int  anim_primary_clip_time_left(double phase, int ctrl);
extern int  anim_start_clip(double a, double b);
extern int  anim_event_scan_active(void);
extern void mtx4_set_rotation_y(double angle, float* out);
extern void mtx4_transform_vector(float* mtx, float* vec);

extern int   DAT_805887d0;               // grabbed-entity id table
extern int   DAT_806120ec;               // grabbed-entity count
extern int   DAT_80583228;               // sin/cos LUT base (int-addressed uses)
extern float FLOAT_80583228_arr[];       // sin/cos LUT (float-indexed alias, same base)
extern int   prop_hinge_angle_in_range(double angle, int weaponEnt);
extern int   DAT_80472ef8;               // target-resolve callback table
extern int   DAT_804d34d8;               // anim track flag table (alt base)
extern int   DAT_80611b8c;               // trigger/zone state table base
extern int   DAT_80611c24;               // entity handle pool base
extern float DAT_803e0510;
extern float DAT_803e0514;
extern float DAT_803e0518;
extern float DAT_804726b8[];             // per-seat ride offset vectors (stride 3 floats)
extern float DAT_804726d0;
extern float DAT_804726d4;
extern float DAT_804726d8;

extern float FLOAT_80618f9c;
extern float FLOAT_80618fa0;
extern float FLOAT_80618fa4;
extern float FLOAT_80618fc0;
extern float FLOAT_80618fc4;
extern float FLOAT_80619058;
extern float FLOAT_8061905c;
extern float FLOAT_80619060;
extern float FLOAT_80619064;
extern float FLOAT_8061906c;
extern float FLOAT_80619070;
extern float FLOAT_80619074;
extern float FLOAT_80619078;
extern float FLOAT_8061907c;
extern float FLOAT_80619080;
extern float FLOAT_80619084;
extern float FLOAT_80619088;
extern float FLOAT_8061908c;
extern float FLOAT_80619090;
extern float FLOAT_80619094;
extern float FLOAT_80619098;
extern float FLOAT_8061909c;
extern float FLOAT_806190a0;
extern float FLOAT_806190a4;
extern float FLOAT_806190a8;
extern float FLOAT_806190ac;
extern float FLOAT_806190b0;
}

// ---------------------------------------------------------------------------
// aiactor_zone_crowd_cost 0x80262de8
// Compute a crowd/avoidance cost for the actor's current target over the set of
// adjacent sound-zone nodes (soundpad_get_entity_data on the +0x14..+0x20 zone
// list). For each candidate zone node it evaluates the distance from the actor
// and its target, penalised by other active bots (spawnvis table DAT_80588610)
// already heading to the same node, and keeps the worst (max) cost. Called from
// the scripted driver; result is used to bias zone selection. Frame-scratch
// float spills preserved from the register-save prologue.
// ---------------------------------------------------------------------------
void aiactor_zone_crowd_cost(void)
{
    int iVar1;
    int iVar2;
    int* piVar3;
    int iVar6;
    int iVar7;
    int iVar8;
    int iVar9;
    int iVar10;
    int* piVar11;
    double dVar12;
    double dVar13;
    double dVar14;
    double dVar16;
    double dVar17;
    double dVar18;
    long long uVar19;
    int local_c8;
    int local_c4[23];

    uVar19 = FUN_803826c8();
    iVar1 = (int)((unsigned long long)uVar19 >> 0x20);
    iVar7 = (int)uVar19;
    piVar11 = *(int**)(iVar1 + 0x7c);
    if (piVar11[1] != 0) {
        iVar8 = 0;
        local_c4[0] = *(int*)(iVar7 + 0x14);
        dVar18 = (double)FLOAT_80619058;
        local_c4[1] = *(int*)(iVar7 + 0x18);
        local_c4[2] = *(int*)(iVar7 + 0x1c);
        local_c4[3] = *(int*)(iVar7 + 0x20);
        iVar7 = 0;
        do {
            local_c8 = 0;
            iVar2 = soundpad_get_entity_data(*(int*)((int)local_c4 + iVar7), &local_c8);
            iVar6 = 0;
            for (iVar10 = 0; iVar10 < iVar2; iVar10 = iVar10 + 1) {
                dVar16 = (double)FLOAT_8061905c;
                if (piVar11[0x19e] != *(int*)(local_c8 + iVar6)) {
                    piVar3 = soundpad_resolve_zone_node(*(int*)(local_c8 + iVar6));
                    dVar12 = (double)(*(float*)(piVar11[1] + 0x6c) - (float)piVar3[5]);
                    piVar3 = soundpad_resolve_zone_node(*(int*)(local_c8 + iVar6));
                    dVar13 = (double)(float)((double)(*(float*)(piVar11[1] + 0x6c) - (float)piVar3[5]) * dVar12);
                    piVar3 = soundpad_resolve_zone_node(*(int*)(local_c8 + iVar6));
                    dVar12 = (double)(*(float*)(piVar11[1] + 100) - (float)piVar3[3]);
                    piVar3 = soundpad_resolve_zone_node(*(int*)(local_c8 + iVar6));
                    dVar17 = (double)(float)((double)(*(float*)(piVar11[1] + 100) - (float)piVar3[3]) * dVar12 + dVar13);
                    piVar3 = soundpad_resolve_zone_node(*(int*)(local_c8 + iVar6));
                    dVar12 = (double)(*(float*)(iVar1 + 0x6c) - (float)piVar3[5]);
                    piVar3 = soundpad_resolve_zone_node(*(int*)(local_c8 + iVar6));
                    dVar13 = (double)(float)((double)(*(float*)(iVar1 + 0x6c) - (float)piVar3[5]) * dVar12);
                    piVar3 = soundpad_resolve_zone_node(*(int*)(local_c8 + iVar6));
                    dVar12 = (double)(*(float*)(iVar1 + 100) - (float)piVar3[3]);
                    piVar3 = soundpad_resolve_zone_node(*(int*)(local_c8 + iVar6));
                    iVar9 = 0;
                    dVar12 = (double)(float)((double)(*(float*)(iVar1 + 100) - (float)piVar3[3]) * dVar12 + dVar13);
                    for (int j = 0; j < DAT_80612098; j = j + 1) {
                        piVar3 = *(int**)((int)&DAT_80588610 + iVar9);
                        if ((((piVar11 != piVar3) && (piVar11[1] != *piVar3)) && ((piVar3[9] & 8U) == 0)) &&
                            (-1 < piVar3[0x1a1])) {
                            int* piVar4;
                            // squared distance between the other bot's target node
                            // (piVar3[0x1a1]) and this candidate node (iVar6+local_c8),
                            // in the Z then X axes, accumulated into dVar13.
                            piVar4 = soundpad_resolve_zone_node(piVar3[0x1a1]);
                            dVar13 = (double)(float)piVar4[5];
                            piVar4 = soundpad_resolve_zone_node(*(int*)(iVar6 + local_c8));
                            dVar14 = (double)(float)((double)(float)piVar4[5] - dVar13);
                            piVar4 = soundpad_resolve_zone_node(piVar3[0x1a1]);
                            dVar13 = (double)(float)piVar4[5];
                            piVar4 = soundpad_resolve_zone_node(*(int*)(iVar6 + local_c8));
                            dVar13 = (double)(float)((double)(float)((double)(float)piVar4[5] - dVar13) * dVar14);
                            piVar4 = soundpad_resolve_zone_node(piVar3[0x1a1]);
                            dVar14 = (double)(float)piVar4[3];
                            piVar4 = soundpad_resolve_zone_node(*(int*)(iVar6 + local_c8));
                            dVar14 = (double)(float)((double)(float)piVar4[3] - dVar14);
                            piVar4 = soundpad_resolve_zone_node(piVar3[0x1a1]);
                            dVar13 = (double)(float)((double)(float)((double)(float)piVar4[3] - dVar14) * dVar14 + dVar13);
                            if (dVar13 < dVar16) {
                                dVar16 = dVar13;
                            }
                        }
                        iVar9 = iVar9 + 4;
                    }
                    if ((double)FLOAT_8061905c == dVar16) {
                        dVar16 = (double)FLOAT_80618f98;
                    }
                    if ((double)FLOAT_80619060 < dVar12) {
                        dVar12 = (double)(float)(dVar12 * dVar12);
                    }
                    if (dVar17 < (double)FLOAT_80619008) {
                        dVar17 = (double)(float)(dVar17 * dVar17);
                    }
                    dVar16 = (double)(float)(dVar16 + -(double)(float)((double)FLOAT_80618fd0 * dVar12 - dVar17));
                    if (dVar18 < dVar16) {
                        dVar18 = dVar16;
                    }
                }
                iVar6 = iVar6 + 4;
            }
            iVar8 = iVar8 + 1;
        } while ((-1 < *(int*)((int)local_c4 + iVar7 + 4)) && (iVar7 = iVar7 + 4, iVar8 < 4));
    }
    FUN_80382714();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_locomotion_anim_tick 0x8026314c
// Walk/run/idle+alert locomotion animation driver. Picks a clip set index
// (0 = normal, 1 = alert/coop-walk when anim flag bit0 set, 2 = special stance
// 0xe/0x35/0xf), advances the loco timer at +0xa00, and plays the appropriate
// move / idle-turn clip built via FUN_802b4844(setIndex, variant). Variant 0 is
// the locomotion move clip, variant 5 the idle/turn clip. Handles the periodic
// idle-fidget schedule (+0xaa0 count, +0xa80 next-time, +0xb34 timer).
// param_3 = current trigger/zone descriptor record.
// ---------------------------------------------------------------------------
void aiactor_locomotion_anim_tick(unsigned int param_1, unsigned int param_2, int param_3)
{
    float fVar1;
    bool bVar2;
    int iVar3;
    unsigned int uVar4;
    int iVar5;
    int iVar6;
    int actorState;
    double dVar8;
    long long uVar9;

    (void)param_1;
    (void)param_2;
    uVar9 = FUN_803826e8_pair();
    fVar1 = FLOAT_80618fb8;
    iVar5 = (int)((unsigned long long)uVar9 >> 0x20);
    actorState = *(int*)(iVar5 + 0x7c);
    iVar6 = 0;
    if ((*(unsigned int*)(DAT_80610ba0 + *(int*)(actorState + 0x204) * 0x29c + 0xc) & 1) == 0) {
        iVar3 = *(int*)(actorState + 0x94);
        if (((iVar3 == 0xe) || (iVar3 == 0x35)) || (iVar3 == 0xf)) {
            iVar6 = 2;
        }
    } else {
        iVar6 = 1;
    }

    if (((int)uVar9 == 0) && ((*(unsigned int*)(param_3 + 0x34) & 0x80000) == 0)) {
        *(float*)(actorState + 0xa00) =
            FLOAT_806108ec *
            (FLOAT_80618fb8 /
             (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8)) +
            *(float*)(actorState + 0xa00);
        bVar2 = (*(float*)(actorState + 0xa00) < fVar1);
    } else {
        bVar2 = true;
        *(float*)(actorState + 0xa00) = FLOAT_80618f98;
    }

    *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffffffd;
    iVar3 = FUN_802b4844(iVar6, 5);
    iVar3 = anim_is_clip_active(iVar5, iVar3);
    if (iVar3 != 0) {
        if (bVar2) {
            *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
        }
        if ((*(unsigned int*)(param_3 + 0x34) & 0x80000) != 0) {
            *(unsigned int*)(actorState + 0x2c) = *(unsigned int*)(actorState + 0x2c) | 0x400;
            *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
        }
    }

    iVar3 = FUN_802b4844(iVar6, 0);
    iVar3 = anim_is_clip_active(iVar5, iVar3);
    if (iVar3 == 0) {
        iVar3 = FUN_802b4844(iVar6, 5);
        iVar3 = anim_is_clip_active(iVar5, iVar3);
        if (iVar3 == 0) {
            FUN_802b4844(iVar6, 0);
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
            FUN_80382734();
            return;
        }
    }

    if (((iVar6 == 1) && (0 < *(int*)(actorState + 0xaa0))) &&
        ((*(float*)(actorState + 0xa80) < *(float*)(actorState + 0xb34) && bVar2))) {
        iVar3 = FUN_802b4844(1, 5);
        iVar3 = anim_is_clip_active(iVar5, iVar3);
        if (iVar3 != 0) {
            FUN_802b4844(1, 5);
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
            *(float*)(actorState + 0xb34) = FLOAT_80618f98;
            uVar4 = rng_counter_tick();
            *(float*)(actorState + 0xa80) =
                FLOAT_80618fac *
                ((float)((double)CONCAT44(0x43300000, uVar4) - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                FLOAT_80618fe0;
            *(int*)(actorState + 0xaa0) = *(int*)(actorState + 0xaa0) + -1;
            FUN_80382734();
            return;
        }
    }

    iVar3 = anim_active_slot_userflags(*(int*)(iVar5 + 4));
    if (iVar3 != 0) {
        iVar3 = FUN_802b4844(iVar6, 5);
        iVar3 = anim_is_clip_active(iVar5, iVar3);
        if (((iVar3 != 0) || (!bVar2)) ||
            (dVar8 = anim_get_clip_end_time_of(iVar5),
             (float)((double)FLOAT_80619040 * dVar8) <= *(float*)(actorState + 0x9d8))) {
            iVar3 = FUN_802b4844(iVar6, 5);
            iVar3 = anim_is_clip_active(iVar5, iVar3);
            if (((iVar3 == 0) || (*(int*)(actorState + 0xaa0) < 1)) || (!bVar2)) {
                iVar3 = FUN_802b4844(iVar6, 0);
                iVar3 = anim_is_clip_active(iVar5, iVar3);
                if (iVar3 == 0) {
                    FUN_802b4844(iVar6, 0);
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                    *(float*)(actorState + 0xb30) = FLOAT_80618f98;
                } else {
                    iVar3 = FUN_802b4844(iVar6, 0);
                    iVar5 = anim_is_clip_active(iVar5, iVar3);
                    if (iVar5 != 0) {
                        fVar1 = FLOAT_80618fb8;
                        if (iVar6 == 0) {
                            fVar1 = FLOAT_80618fd0;
                        }
                        if (*(float*)(actorState + 0xb30) < fVar1) {
                            FUN_802b4844(iVar6, 0);
                            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                            FUN_80382734();
                            return;
                        }
                    }
                    if (bVar2) {
                        FUN_802b4844(iVar6, 5);
                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                        if (iVar6 == 0) {
                            uVar4 = 0;
                        } else {
                            uVar4 = (iVar6 == 1) ? 8 : 2;
                        }
                        *(unsigned int*)(actorState + 0xaa0) = uVar4;
                        if (iVar6 == 1) {
                            *(float*)(actorState + 0xb34) = FLOAT_80618f98;
                            uVar4 = rng_counter_tick();
                            *(float*)(actorState + 0xa80) =
                                FLOAT_80618fac *
                                ((float)((double)CONCAT44(0x43300000, uVar4) - DOUBLE_80618fe8) /
                                 FLOAT_80618fd8) +
                                FLOAT_80618fe0;
                        }
                    }
                }
            } else {
                FUN_802b4844(iVar6, 5);
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                *(float*)(actorState + 0xb34) = FLOAT_80618f98;
                uVar4 = rng_counter_tick();
                *(float*)(actorState + 0xa80) =
                    FLOAT_80618fac *
                    ((float)((double)CONCAT44(0x43300000, uVar4) - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                    FLOAT_80618fe0;
                *(int*)(actorState + 0xaa0) = *(int*)(actorState + 0xaa0) + -1;
            }
        } else {
            FUN_802b4844(iVar6, 5);
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
            *(float*)(actorState + 0xb34) = FLOAT_80618f98;
            uVar4 = rng_counter_tick();
            *(float*)(actorState + 0xa80) =
                FLOAT_80618fac *
                ((float)((double)CONCAT44(0x43300000, uVar4) - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                FLOAT_80618fe0;
            if (iVar6 == 0) {
                uVar4 = 0;
            } else {
                uVar4 = (iVar6 == 1) ? 8 : 2;
            }
            *(unsigned int*)(actorState + 0xaa0) = uVar4;
        }
    }
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_play_proximity_bark 0x802649d0
// Play a positional "bark" SFX (and optional localized HUD subtitle) for this
// actor, gated by proximity: if a radius (radius > 0) is given, at least one
// player must be within radius^2 of the actor or the bark is skipped. When a
// valid sample id resolves (sound_resolve_sample_id) it stops any prior bark
// (+0xe90) and fires a new positional one-shot, recording the handle at +0xe90.
// param_11 = show-subtitle flag; radius arrives in the incoming FP arg.
// ---------------------------------------------------------------------------
void aiactor_play_proximity_bark(double radius, int param_11,
                                 double param2, double param3, double param4, double param5,
                                 long long param6, long long param7, long long param8,
                                 int param13, int param14, int param15, int param16)
{
    float fVar1;
    int actor;
    int iVar2;
    int iVar4;
    int iVar6;
    int iVar3;
    int iVar4b;
    unsigned int uVar5;
    int actorState;
    double dVar8;
    long long uVar9;
    int found;
    int local_58[4];
    unsigned int local_48;
    float local_40;
    unsigned int local_3c;
    unsigned int local_38;
    unsigned int local_34;
    unsigned char local_30;
    float local_2c;

    uVar9 = FUN_803826e8_pair();
    actor = (int)((unsigned long long)uVar9 >> 0x20);
    actorState = *(int*)(actor + 0x7c);
    iVar2 = sound_resolve_sample_id((int)uVar9);
    dVar8 = radius;

    found = 1;
    if ((double)FLOAT_80618f98 < dVar8) {
        double r2 = (double)(float)(dVar8 * dVar8);
        found = 0;
        iVar3 = 0;
        iVar4 = DAT_80612610;
        if (0 < DAT_80612610) {
            do {
                iVar6 = *(int*)(DAT_80611d78 + iVar3 + 0x14);
                double dx = (double)(*(float*)(actor + 100) - *(float*)(iVar6 + 100));
                double dy = (double)(*(float*)(actor + 0x68) - *(float*)(iVar6 + 0x68));
                fVar1 = *(float*)(actor + 0x6c) - *(float*)(iVar6 + 0x6c);
                double dz2 = (double)(fVar1 * fVar1);
                if ((double)(float)(dz2 + (double)((float)(dx * dx) + (float)(dy * dy))) < r2) {
                    found = 1;
                    break;
                }
                iVar3 = iVar3 + 0x141c;
                iVar4 = iVar4 + -1;
            } while (iVar4 != 0);
        }
        if (found == 0) { FUN_80382734(); return; }
    }

    if ((param_11 != 0) && (iVar4b = game_get_round_timer(), iVar4b != 0)) {
        for (iVar4b = 0; iVar4b < DAT_80612610; iVar4b = iVar4b + 1) {
            hud_create_localized_text(
                (double)FLOAT_80618f98, param2, param3, param4, param5, param6, param7, param8,
                iVar4b, 9, (int)uVar9, found, param13, param14, param15, param16);
        }
    }

    if (-1 < iVar2) {
        if (-1 < *(int*)(actorState + 0xe90)) {
            aiactor_clear_bark_state(actor);
        }
        sound_init_play_params((int)local_58);
        local_48 = local_48 | 0xa0;
        (void)local_48;
        local_30 = 1;
        (void)local_30;
        local_2c = (float)dVar8;
        (void)local_2c;
        local_40 = FLOAT_80619040;
        (void)local_40;
        local_3c = *(unsigned int*)(actor + 100);
        (void)local_3c;
        local_38 = *(unsigned int*)(actor + 0x68);
        (void)local_38;
        local_34 = *(unsigned int*)(actor + 0x6c);
        (void)local_34;
        local_58[0] = iVar2;
        uVar5 = sound_play_request(local_58);
        *(unsigned int*)(actorState + 0xe90) = uVar5;
    }
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_is_grabbed_entity 0x80264b78
// Return 1 if entity id is present in the grabbed-entity id table
// (DAT_805887d0, DAT_806120ec entries), else 0.
// ---------------------------------------------------------------------------
unsigned int aiactor_is_grabbed_entity(int entityId)
{
    int iVar1;
    int iVar2;

    iVar1 = 0;
    iVar2 = DAT_806120ec;
    if (0 < DAT_806120ec) {
        do {
            if (*(int*)((int)&DAT_805887d0 + iVar1) == entityId) {
                return 1;
            }
            iVar1 = iVar1 + 4;
            iVar2 = iVar2 + -1;
        } while (iVar2 != 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// aiactor_mounted_weapon_tick 0x80264bb8
// AI actor operating a mounted / turret weapon (entity handle at +0xc14). Runs
// a 6-state aim/track FSM (+0x20): 0/-1 seek+claim a firing zone-node, 1 align
// to the mount, 2 track+follow, 3 acquire aim solution, 4 wait, 5 idle-sweep.
// While states 2..5 hold a target it computes the aim direction (+0x9f0 vec)
// with lead + spread from the sin/cos LUT (DAT_80583228), clamps pitch/yaw to
// the mount limits (weapon-def rows at iVar14+0x34..+0x40), and writes the mount
// yaw (+0xc4) and stance/turn animation. Register-save FP spill locals from the
// prologue are dead and omitted. Called from the AI behaviour driver.
// ---------------------------------------------------------------------------
void aiactor_mounted_weapon_tick(unsigned int param_1, unsigned int param_2,
                                 double param_3, double param_4, double param_5,
                                 double param_6, double param_7, double param_8)
{
    float fVar1;
    float fVar2;
    float fVar3;
    float fVar4;
    float fVar5;
    int iVar6;
    int iVar7;
    unsigned int uVar8;
    int iVar9;
    unsigned int uVar10;
    unsigned char* puVar11;
    bool bVar12;
    int (*pcVar13)(int);
    int iVar14;
    int iVar15;
    long long uVar16;
    double dVar17;
    double dVar18;
    double dVar19;
    double dVar20;
    double dVar21;
    double dVar22;
    double dVar23;
    double dVar24;
    float local_148;
    float local_144;
    float local_140;
    float local_13c;
    float local_138;
    float local_134;
    float local_130;
    float local_12c;
    float local_128;
    float local_124;
    float local_120;
    unsigned int local_11c;
    unsigned int local_118;
    unsigned int local_114;
    float local_110;
    float local_10c;
    float local_108;
    float local_104;
    float local_100;
    float local_fc;
    float afStack_f8[16];

    (void)param_1;
    (void)param_2;
    (void)param_4;
    (void)param_6;
    (void)param_7;
    (void)param_8;
    iVar6 = FUN_803826e8();
    iVar15 = *(int*)(iVar6 + 0x7c);
    iVar7 = aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(iVar15 + 0x9d0)) {
        if (*(int*)(iVar15 + 0x9d0) != *(int*)(iVar15 + 0x20)) {
            *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(iVar15 + 0x9d4) = *(unsigned int*)(iVar15 + 0x20);
            *(unsigned int*)(iVar15 + 0x20) = *(unsigned int*)(iVar15 + 0x9d0);
        }
        *(unsigned int*)(iVar15 + 0x9d0) = 0xffffffff;
    }

    if ((*(unsigned int*)(iVar15 + 0x2c) & 0x80) == 0) {
        *(float*)(iVar15 + 0xa90) = FLOAT_80618f98;
    } else {
        double lb = (double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000);
        *(float*)(iVar15 + 0xa90) =
            (float)((double)FLOAT_806108ec *
                        (double)(FLOAT_80618fb8 / (float)(lb - DOUBLE_80618fc8)) +
                    (double)*(float*)(iVar15 + 0xa90));
    }

    iVar14 = 0;
    {
        double lb = (double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000);
        dVar23 = (double)(float)((double)FLOAT_806108ec *
                                 (double)(FLOAT_80618fb8 / (float)(lb - DOUBLE_80618fc8)));
    }
    *(unsigned int*)(iVar15 + 0x28) = *(unsigned int*)(iVar15 + 0x28) & 0xfffffffd;
    pcVar13 = *(int (**)(int))(*(int*)(iVar15 + 0xbf0) + 0x78);
    if ((pcVar13 != (int (*)(int))0) &&
        ((*(int*)(iVar15 + 8) == DAT_806120f8) || (*(int*)(iVar15 + 4) == 0))) {
        uVar8 = (*pcVar13)(iVar6);
        *(unsigned int*)(iVar15 + 4) = uVar8;
    }

    if ((*(int*)(iVar15 + 0xc14) != 0) &&
        (iVar9 = aiactor_is_grabbed_entity(*(int*)(iVar15 + 0xc14)), iVar9 == 0)) {
        if (!(*(int*)(iVar15 + 0xaac) == 1 && *(int*)(iVar15 + 0xaa8) == 0 &&
              (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar15 + 0x14)
                                        : *(unsigned int*)(iVar15 + 0x1c)) & 1) == 0)) {
            *(unsigned int*)(iVar15 + 0xaac) = 4;
            *(unsigned int*)(iVar15 + 0xaa8) = 0;
        }
        FUN_80382734();
        return;
    }

    if ((*(unsigned int*)(iVar15 + 0xab4) & 0x80000) == 0) {
        if ((*(unsigned int*)(iVar15 + 0xab4) & 0x100000) != 0) {
            iVar7 = 1;
        }
    } else {
        iVar7 = entity_handle_resolve(
            *(unsigned int*)(*(int*)(DAT_8061208c + 0x3c) +
                             (*(int*)(iVar15 + 0x9ec) + -1) * 0x48 + 0x14));
        if (*(int*)(iVar15 + 4) != iVar7) {
            if (!(*(int*)(iVar15 + 0xaac) == 1 && *(int*)(iVar15 + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar15 + 0x14)
                                            : *(unsigned int*)(iVar15 + 0x1c)) & 1) != 0)) {
                *(unsigned int*)(iVar15 + 0xaac) = 0x80000;
                *(unsigned int*)(iVar15 + 0xaa8) = 0;
            }
            *(unsigned int*)(iVar15 + 4) = 0;
            FUN_80382734();
            return;
        }
        iVar7 = 1;
    }
    if (*(int*)(iVar15 + 4) == 0) {
        iVar7 = 0;
    }

    dVar18 = (double)FLOAT_80618f98;
    if ((*(int*)(iVar15 + 0x20) < 6) && (1 < *(int*)(iVar15 + 0x20))) {
        double dVar20t;
        iVar14 = *(int*)(*(int*)(iVar15 + 0xc14) + 0x7c);
        puVar11 = weapon_get_def_from_entity();
        prop_get_world_pivot(*(int*)(iVar15 + 0xc14), &local_110);
        prop_compute_eye_point();
        *(unsigned int*)(iVar6 + 100) = local_11c;
        *(unsigned int*)(iVar6 + 0x68) = local_118;
        *(unsigned int*)(iVar6 + 0x6c) = local_114;
        if ((*(unsigned int*)(iVar14 + 8) & 0x4000) != 0) {
            *(float*)(iVar6 + 0x68) = *(float*)(iVar6 + 0x68) - FLOAT_80618fb8;
        }
        if (iVar7 != 0) {
            int aimIdx = (int)((double)FLOAT_8061906c *
                                   (double)((FLOAT_80619070 * *(float*)(iVar14 + 200)) / FLOAT_80619074) +
                               (double)FLOAT_80618fd0);
            bVar12 = aiactor_aim_alignment_test(
                (double)*(float*)((int)&DAT_80583228 + ((aimIdx + 0x200) * 4 & 0x1ffc)),
                iVar6, (float*)(*(int*)(iVar15 + 4) + 100));
            if (((bVar12) ||
                 (iVar9 = *(int*)(*(int*)(iVar15 + 4) + 0x7c),
                  *(float*)(iVar9 + 0xb4c) < FLOAT_80618ff4)) ||
                (*(float*)(iVar9 + 0xb44) < FLOAT_80618ff4)) {
                iVar7 = 1;
            } else {
                iVar7 = 0;
            }
        }

        if (*(int*)(puVar11 + 0x38) < 0) {
            iVar9 = anim_is_clip_active(iVar6, 0x25);
            if (iVar9 == 0) {
                anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
            }
        } else {
            iVar9 = anim_is_clip_active(iVar6, *(int*)(puVar11 + 0x38));
            if (iVar9 == 0) {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
            }
            *(float*)(*(int*)(iVar6 + 4) + 0x100) = FLOAT_80618f98;
        }

        if (iVar7 == 0) {
            *(float*)(iVar15 + 0xa00) =
                -(float)((double)FLOAT_8061907c * dVar23 - (double)*(float*)(iVar15 + 0xa00));
            if (*(float*)(iVar15 + 0xa00) < FLOAT_80618f98) {
                *(float*)(iVar15 + 0xa00) = FLOAT_80618f98;
            }
        } else {
            *(float*)(iVar15 + 0xa00) =
                (float)((double)FLOAT_80619078 * dVar23 + (double)*(float*)(iVar15 + 0xa00));
            if (FLOAT_80618fb8 < *(float*)(iVar15 + 0xa00)) {
                *(float*)(iVar15 + 0xa00) = FLOAT_80618fb8;
            }
        }

        if ((iVar7 != 0) ||
            (((FLOAT_80618f98 < *(float*)(iVar15 + 0xa00) && (*(int*)(iVar15 + 0x20) != 5)) &&
              (*(int*)(iVar15 + 4) != 0)))) {
            dVar20t = (double)(float)((*(int*)(iVar15 + 0xa00) >> 1) + 0x1fc00000);
            dir_to_angles((void*)(iVar15 + 0x9f0), &local_140, &local_144);
            iVar9 = *(int*)(iVar15 + 4);
            local_104 = (*(float*)(iVar9 + 100) + *(float*)(iVar9 + 0x1a8)) - local_110;
            local_100 = (*(float*)(iVar9 + 0x68) + *(float*)(iVar9 + 0x1ac)) - local_10c;
            local_fc = (*(float*)(iVar9 + 0x6c) + *(float*)(iVar9 + 0x1b0)) - local_108;
            dVar17 = math_sqrt((double)(local_104 * local_104 + local_fc * local_fc));
            fVar1 = (float)((double)local_fc * (double)local_fc) +
                    (float)((double)local_104 * (double)local_104) +
                    (float)((double)local_100 * (double)local_100);
            if (FLOAT_80619080 < fVar1) {
                fVar2 = 1.0f / (float)math_sqrt((double)fVar1);
                fVar2 = fVar2 * -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040);
                dVar21 = (double)(FLOAT_80618fb8 * fVar2 *
                                  -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040));
                local_104 = (float)((double)local_104 * dVar21);
                local_100 = (float)((double)local_100 * dVar21);
                local_fc = (float)((double)local_fc * dVar21);
            }

            if (*(int*)(iVar15 + 0x20) == 3) {
                local_134 = DAT_803e0510;
                local_130 = DAT_803e0514;
                local_12c = DAT_803e0518;
                dVar19 = prop_hinge_wrap_angle_to_range(
                    (double)FLOAT_80618f98, &local_110, (float*)(*(int*)(iVar15 + 4) + 100));
                fVar1 = (float)((double)((float)((double)local_134 * (double)local_134) +
                                         (float)((double)local_130 * (double)local_130)) +
                                (double)(float)((double)local_12c * (double)local_12c));
                if (FLOAT_80619080 < fVar1) {
                    fVar2 = 1.0f / (float)math_sqrt((double)fVar1);
                    fVar2 = fVar2 * -(float)((double)(FLOAT_80618fd0 * fVar1) * (double)(fVar2 * fVar2) -
                                             (double)FLOAT_80619040);
                    dVar21 = (double)(float)((double)FLOAT_80618fb8 * (double)fVar2);
                    local_134 = (float)((double)local_134 * dVar21);
                    local_130 = (float)((double)local_130 * dVar21);
                    local_12c = (float)((double)local_12c * dVar21);
                }
                if (dVar17 < (double)FLOAT_80619084) {
                    if (*(int*)(iVar15 + 0x20) != 4) {
                        *(int*)(iVar15 + 0x9d4) = *(int*)(iVar15 + 0x20);
                        *(unsigned int*)(iVar15 + 0x20) = 4;
                        *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
                    }
                    goto state_dispatch;
                }
                mtx4_set_rotation_y(
                    (double)((float)((double)FLOAT_80619070 * dVar19) / FLOAT_80619074), afStack_f8);
                mtx4_transform_vector(afStack_f8, &local_134);
                fVar1 = FLOAT_80619088;
                if (0 < *(int*)(iVar15 + 0x9dc)) {
                    double lb = (double)(CONCAT44(0x43300000, 200 - *(int*)(iVar15 + 0x9dc)) ^ 0x80000000);
                    int idx = (int)(FLOAT_8061906c *
                                        ((FLOAT_8061908c * (float)(lb - DOUBLE_80618fc8)) / FLOAT_80619090) +
                                    FLOAT_80618fd0);
                    fVar1 = FLOAT_80619088 *
                            *(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc));
                }
                iVar9 = *(int*)(iVar15 + 4);
                uVar10 = (unsigned int)(FLOAT_8061906c *
                                            ((FLOAT_80619070 * *(float*)(iVar9 + 0x70)) / FLOAT_80619074) +
                                        FLOAT_80618fd0);
                fVar2 = FLOAT_80583228_arr[uVar10 & 0x7ff];
                fVar3 = *(float*)((int)&DAT_80583228 + ((uVar10 + 0x200) * 4 & 0x1ffc));
                fVar5 = fVar3 * fVar3 + fVar2 * fVar2 + FLOAT_80618f98;
                fVar4 = FLOAT_80618f98;
                if (FLOAT_80619080 < fVar5) {
                    fVar4 = 1.0f / (float)math_sqrt((double)fVar5);
                    fVar4 = fVar4 * -(FLOAT_80618fd0 * fVar5 * fVar4 * fVar4 - FLOAT_80619040);
                    fVar5 = FLOAT_80618fb8 * fVar4 *
                            -(FLOAT_80618fd0 * fVar5 * fVar4 * fVar4 - FLOAT_80619040);
                    fVar2 = fVar2 * fVar5;
                    fVar4 = FLOAT_80618f98 * fVar5;
                    fVar3 = fVar3 * fVar5;
                }
                local_128 = (local_134 * fVar1 + FLOAT_80619094 * fVar2 + *(float*)(iVar9 + 100)) - local_110;
                local_124 = (local_130 * fVar1 + FLOAT_80619094 * fVar4 + *(float*)(iVar9 + 0x68)) - local_10c;
                local_120 = (local_12c * fVar1 + FLOAT_80619094 * fVar3 + *(float*)(iVar9 + 0x6c)) - local_108;
                dVar17 = math_sqrt((double)(local_104 * local_104 + local_fc * local_fc));
                fVar1 = local_120 * local_120 + local_128 * local_128 + local_124 * local_124;
                if (FLOAT_80619080 < fVar1) {
                    fVar2 = 1.0f / (float)math_sqrt((double)fVar1);
                    fVar2 = fVar2 * -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040);
                    fVar1 = FLOAT_80618fb8 * fVar2 *
                            -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040);
                    local_128 = local_128 * fVar1;
                    local_124 = local_124 * fVar1;
                    local_120 = local_120 * fVar1;
                }
                if (*(int*)(iVar15 + 0x9dc) == 0) {
                    dir_to_angles(&local_128, 0, &local_148);
                    if (ABS(local_148 - local_144) < FLOAT_80618fb8) {
                        *(float*)(iVar15 + 0xb30) = FLOAT_80618f98;
                        *(unsigned int*)(iVar15 + 0x9dc) = 200;
                    }
                } else {
                    *(int*)(iVar15 + 0x9dc) = *(int*)(iVar15 + 0x9dc) + -1;
                    if ((*(int*)(iVar15 + 0x9dc) == 0) && (*(int*)(iVar15 + 0x20) != 4)) {
                        *(int*)(iVar15 + 0x9d4) = *(int*)(iVar15 + 0x20);
                        *(unsigned int*)(iVar15 + 0x20) = 4;
                        *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
                    }
                }
                local_104 = local_128;
                local_100 = local_124;
                local_fc = local_120;
            }

            dir_to_angles(&local_104, &local_138, &local_13c);
            {
                double dd22 = (double)(local_138 - (local_140 + FLOAT_80618f9c));
                double dd21 = ABS((double)(local_138 - local_140));
                dVar18 = (double)(local_138 - (local_140 - FLOAT_80618f9c));
                double dd19 = ABS(dd22);
                if (dd19 <= dd21) {
                    if (dd19 < ABS(dVar18)) {
                        dVar18 = dd22;
                    }
                } else if (dd21 < ABS(dVar18)) {
                    dVar18 = (double)(local_138 - local_140);
                }
            }
            dVar19 = FUN_803909ec((double)((FLOAT_80619070 * local_138) / FLOAT_80619074));
            dVar21 = (double)(float)dVar19;
            dVar18 = FUN_803909ec((double)((float)((double)FLOAT_80619070 * dVar18) / FLOAT_80619074));
            dVar22 = (double)(float)((double)(float)((double)FLOAT_80619098 * dVar23) * dVar20t);
            {
                float base = local_140;
                float cur = local_138;
                fVar1 = (float)((double)cur - (double)(float)((double)base + (double)FLOAT_80618f9c));
                fVar3 = ABS(cur - base);
                fVar4 = ABS(fVar1);
                fVar2 = (float)((double)cur - (double)(float)((double)base - (double)FLOAT_80618f9c));
                if (fVar4 <= fVar3) {
                    if (fVar4 < ABS(fVar2)) { fVar2 = fVar1; }
                } else if (fVar3 < ABS(fVar2)) {
                    fVar2 = cur - base;
                }
                dVar24 = (double)(float)((double)((float)(dVar22 *
                                            (double)(float)(((int)ABS(FLOAT_80618fe0 *
                                                (float)(dVar17 * (double)(float)((double)(float)dVar18 - dVar21))) >> 1) +
                                                0x1fc00000)) *
                                            (FLOAT_80619040 -
                                             ABS(*(float*)(iVar15 + 0x9f8) * local_fc +
                                                 *(float*)(iVar15 + 0x9f0) * local_104 +
                                                 *(float*)(iVar15 + 0x9f4) * local_100))) *
                                        (double)(float)((unsigned int)fVar2 & 0x80000000 | 0x3f800000) +
                                        (double)base);
            }
            dVar18 = FUN_80390324((double)((float)(dVar24 - (double)FLOAT_80618fa4) / FLOAT_80618f9c));
            {
                double dd17 = (double)FLOAT_80618f9c;
                float base = local_144;
                float cur = local_13c;
                fVar1 = (float)((double)cur - (double)(float)((double)base + dd17));
                local_140 = -(float)(dd17 * (double)(float)dVar18 - dVar24);
                fVar3 = ABS(cur - base);
                fVar4 = ABS(fVar1);
                fVar2 = (float)((double)cur - (double)(float)((double)base - dd17));
                if (fVar4 <= fVar3) {
                    if (fVar4 < ABS(fVar2)) { fVar2 = fVar1; }
                } else if (fVar3 < ABS(fVar2)) {
                    fVar2 = cur - base;
                }
                double dd17b = (double)(float)(dVar22 * (double)(float)((unsigned int)fVar2 & 0x80000000 | 0x3f800000) +
                                               (double)base);
                dVar18 = FUN_80390324((double)((float)(dd17b - (double)FLOAT_80618fa4) / FLOAT_80618f9c));
                local_144 = -(float)((double)FLOAT_80618f9c * (double)(float)dVar18 - dd17b);
            }

            fVar1 = *(float*)(iVar14 + 0x34);
            if ((FLOAT_80618f98 != fVar1) || (FLOAT_80618f98 != *(float*)(iVar14 + 0x38))) {
                fVar3 = fVar1;
                if (fVar1 < local_140) { fVar3 = local_140; }
                fVar2 = *(float*)(iVar14 + 0x38);
                if ((fVar3 <= *(float*)(iVar14 + 0x38)) && (fVar2 = fVar1, fVar1 < local_140)) {
                    fVar2 = local_140;
                }
                local_140 = fVar2;
            }

            if ((FLOAT_80618f98 != *(float*)(iVar14 + 0x3c)) ||
                (FLOAT_80618f98 != *(float*)(iVar14 + 0x40))) {
                double d18 = (double)(*(float*)(*(int*)(*(int*)(iVar15 + 0xc14) + 0x7c) + 0x40) +
                                      *(float*)(*(int*)(iVar15 + 0xc14) + 0x70));
                dVar18 = FUN_80390324((double)((float)(d18 - (double)FLOAT_80618fa4) / FLOAT_80618f9c));
                d18 = -(double)(float)((double)FLOAT_80618f9c * (double)(float)dVar18 - d18);
                double d17 = (double)(*(float*)(*(int*)(*(int*)(iVar15 + 0xc14) + 0x7c) + 0x3c) +
                                      *(float*)(*(int*)(iVar15 + 0xc14) + 0x70));
                dVar17 = FUN_80390324((double)(float)((double)(float)(d17 - (double)FLOAT_80618fa4) /
                                                      (double)FLOAT_80618f9c));
                d17 = -(double)(float)((double)FLOAT_80618f9c * (double)(float)dVar17 - d17);
                if ((double)FLOAT_80618fa0 <= ABS((double)(float)(d18 - d17))) {
                    if (d17 <= d18) {
                        double d21 = (double)local_144;
                        double d19 = d17;
                        if (d17 < d21) { d19 = d21; }
                        double dset = d18;
                        if ((d19 <= d18) && (dset = d17, d17 < d21)) { dset = d21; }
                        local_144 = (float)dset;
                    } else {
                        double d19 = (double)local_144;
                        if ((d18 < d19) && (d19 < d17)) {
                            if ((double)(float)(ABS((double)(float)(d18 - d17)) * (double)FLOAT_80618fd0 + d18) <= d19) {
                                local_144 = (float)d17;
                            } else {
                                local_144 = (float)d18;
                            }
                        }
                    }
                }
            }

            angles_to_dir((double)local_140, (double)local_144, (float*)(iVar15 + 0x9f0));
            fVar1 = *(float*)(iVar15 + 0x9f0);
            fVar2 = *(float*)(iVar15 + 0x9f8) * *(float*)(iVar15 + 0x9f8) +
                    fVar1 * fVar1 + *(float*)(iVar15 + 0x9f4) * *(float*)(iVar15 + 0x9f4);
            if (FLOAT_80619080 < fVar2) {
                fVar3 = 1.0f / (float)math_sqrt((double)fVar2);
                fVar3 = fVar3 * -(FLOAT_80618fd0 * fVar2 * fVar3 * fVar3 - FLOAT_80619040);
                fVar2 = FLOAT_80618fb8 * fVar3 * -(FLOAT_80618fd0 * fVar2 * fVar3 * fVar3 - FLOAT_80619040);
                *(float*)(iVar15 + 0x9f0) = fVar1 * fVar2;
                *(float*)(iVar15 + 0x9f4) = *(float*)(iVar15 + 0x9f4) * fVar2;
                *(float*)(iVar15 + 0x9f8) = *(float*)(iVar15 + 0x9f8) * fVar2;
            }

            fVar1 = *(float*)(iVar6 + 0x70);
            fVar2 = local_144 - (fVar1 + FLOAT_80618f9c);
            fVar4 = ABS(local_144 - fVar1);
            fVar3 = local_144 - (fVar1 - FLOAT_80618f9c);
            fVar5 = ABS(fVar2);
            if (fVar5 <= fVar4) {
                if (fVar5 < ABS(fVar3)) { fVar3 = fVar2; }
            } else if (fVar4 < ABS(fVar3)) {
                fVar3 = local_144 - fVar1;
            }
            *(float*)(iVar15 + 0xae0) = fVar3;

            {
                double p4 = (double)*(float*)(iVar6 + 0xc4);
                double d17 = (double)local_144;
                double p6 = (double)(float)(d17 - (double)(float)(p4 + (double)FLOAT_80618f9c));
                double d18b = ABS((double)(float)(d17 - p4));
                double p3 = (double)(float)(dVar20t * (double)(float)(dVar23 * (double)*(float*)(iVar14 + 0x5c)));
                double p5 = (double)(float)(d17 - (double)(float)(p4 - (double)FLOAT_80618f9c));
                double p7 = p5;
                if (ABS(p6) <= d18b) {
                    if (ABS(p6) < ABS(p5)) { p7 = p6; }
                } else if (d18b < ABS(p5)) {
                    p7 = (double)(float)(d17 - p4);
                }
                double d19 = (double)(float)(p7 * p3 + p4);
                dVar17 = FUN_80390324((double)((float)(d19 - (double)FLOAT_80618f98) / FLOAT_80618f9c));
                *(float*)(iVar6 + 0xc4) = -(float)((double)FLOAT_80618f9c * (double)(float)dVar17 - d19);
            }
        }
    }

state_dispatch:
    fVar1 = FLOAT_8061909c;
    iVar9 = *(int*)(iVar15 + 0x20);
    if (iVar9 == 3) {
        if (iVar7 == 0) {
            *(unsigned int*)(iVar15 + 0x9d4) = 3;
            *(unsigned int*)(iVar15 + 0x20) = 5;
            *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
        } else {
            dVar23 = prop_hinge_wrap_angle_to_range(
                (double)FLOAT_80618f98, &local_110, (float*)(*(int*)(iVar15 + 4) + 100));
            iVar7 = prop_hinge_angle_in_range(dVar23, *(int*)(iVar15 + 0xc14));
            if (iVar7 == 0) {
                if (!(*(int*)(iVar15 + 0xaac) == 1 && *(int*)(iVar15 + 0xaa8) == 0 &&
                      (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar15 + 0x14)
                                                : *(unsigned int*)(iVar15 + 0x1c)) & 1) == 0)) {
                    *(unsigned int*)(iVar15 + 0xaac) = 4;
                    *(unsigned int*)(iVar15 + 0xaa8) = 0;
                }
            } else if ((0 < *(int*)(iVar15 + 0x9dc)) && (*(float*)(iVar14 + 0x1c4) <= FLOAT_80618f98)) {
                *(unsigned int*)(iVar14 + 0xb0) = *(unsigned int*)(iVar6 + 0x34);
                *(unsigned int*)(iVar15 + 0x28) = *(unsigned int*)(iVar15 + 0x28) | 2;
            }
        }
    } else if (iVar9 < 3) {
        if (iVar9 == 1) {
            if ((*(int*)(iVar15 + 4) == 0) ||
                (bVar12 = aiactor_aim_alignment_test(
                     (double)*(float*)(iVar15 + 0x9bc), iVar6, (float*)(*(int*)(iVar15 + 4) + 100)),
                 !bVar12)) {
                *(unsigned int*)(iVar15 + 0x28) = *(unsigned int*)(iVar15 + 0x28) | 0x40000;
            } else if (iVar7 != 0) {
                *(unsigned int*)(iVar15 + 0x28) = *(unsigned int*)(iVar15 + 0x28) | 2;
                *(unsigned int*)(iVar15 + 0x28) = *(unsigned int*)(iVar15 + 0x28) & 0xfffbffff;
            }
            aiactor_set_stance_flags(iVar6, 1);
            aiactor_play_turn_animation(iVar6);
            soundpad_find_zone_by_id(**(int**)(iVar15 + 0x9c0));
            aiactor_follow_zone_path();
            aiactor_facing_update();
            uVar16 = 0;
            aiactor_movement_tick(uVar16, dVar18, param_3, param_4, param_5, param_6, param_7, param_8);
            if (*(int*)(iVar15 + 0xc14) != 0) {
                if (*(int*)(iVar15 + 0x20) != 2) {
                    *(int*)(iVar15 + 0x9d4) = *(int*)(iVar15 + 0x20);
                    *(unsigned int*)(iVar15 + 0x20) = 2;
                    *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
                }
                *(unsigned int*)(iVar15 + 0x28) = *(unsigned int*)(iVar15 + 0x28) & 0xfffffffd;
                if (iVar7 != 0) {
                    *(float*)(iVar15 + 0xa00) = FLOAT_80618fb8;
                }
            }
        } else if (iVar9 < 1) {
            if (-1 < iVar9) {
                if (*(int*)(iVar15 + 0x9c0) == 0) {
                    if (((*(int*)(iVar15 + 0xc14) == 0) &&
                         ((*(unsigned int*)(*(int*)(iVar15 + 0xbf0) + 4) & 0x100) == 0)) &&
                        ((*(unsigned int*)(iVar15 + 0xab4) & 0x80000) == 0)) {
                        *(unsigned int*)(iVar15 + 0x9c0) = 0;
                        if (!(*(int*)(iVar15 + 0xaac) == 1 && *(int*)(iVar15 + 0xaa8) == 0 &&
                              (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar15 + 0x14)
                                                        : *(unsigned int*)(iVar15 + 0x1c)) & 1) == 0)) {
                            *(unsigned int*)(iVar15 + 0xaac) = 4;
                            *(unsigned int*)(iVar15 + 0xaa8) = 0;
                        }
                        FUN_80382734();
                        return;
                    }
                    iVar7 = *(int*)(iVar15 + 0x678);
                    iVar14 = DAT_80611b8c + iVar7 * 0x20;
                    uVar10 = *(unsigned int*)(iVar14 + 4);
                    if ((uVar10 & 0x40000000) != 0) {
                        *(unsigned int*)(iVar15 + 0x9c0) = 0;
                        if (!(*(int*)(iVar15 + 0xaac) == 1 && *(int*)(iVar15 + 0xaa8) == 0 &&
                              (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar15 + 0x14)
                                                        : *(unsigned int*)(iVar15 + 0x1c)) & 1) == 0)) {
                            *(unsigned int*)(iVar15 + 0xaac) = 4;
                            *(unsigned int*)(iVar15 + 0xaa8) = 0;
                        }
                        FUN_80382734();
                        return;
                    }
                    *(unsigned int*)(iVar14 + 4) = uVar10 | 0x40000000;
                    iVar7 = soundpad_get_entity_id_from_zone(iVar7);
                    iVar7 = soundpad_find_trigger_by_id(iVar7);
                    *(int*)(iVar15 + 0x9c0) = iVar7;
                    *(float*)(iVar15 + 0xa00) = FLOAT_80618fb8;
                    if (*(int*)(iVar15 + 0xc14) == 0) {
                        if (*(int*)(iVar15 + 0x20) != 1) {
                            *(int*)(iVar15 + 0x9d4) = *(int*)(iVar15 + 0x20);
                            *(unsigned int*)(iVar15 + 0x20) = 1;
                            *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                            *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
                        }
                    } else if (*(int*)(iVar15 + 0x20) != 2) {
                        *(int*)(iVar15 + 0x9d4) = *(int*)(iVar15 + 0x20);
                        *(unsigned int*)(iVar15 + 0x20) = 2;
                        *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
                    }
                } else {
                    iVar7 = soundpad_find_zone_by_id((int)*(short*)(*(int*)(iVar15 + 0x9c0) + 8));
                    if ((*(unsigned int*)(DAT_80611b8c + iVar7 * 0x20 + 4) & 0x40000000) != 0) {
                        *(unsigned int*)(iVar15 + 0x9c0) = 0;
                        if (!(*(int*)(iVar15 + 0xaac) == 1 && *(int*)(iVar15 + 0xaa8) == 0 &&
                              (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar15 + 0x14)
                                                        : *(unsigned int*)(iVar15 + 0x1c)) & 1) == 0)) {
                            *(unsigned int*)(iVar15 + 0xaac) = 4;
                            *(unsigned int*)(iVar15 + 0xaa8) = 0;
                        }
                        FUN_80382734();
                        return;
                    }
                    if (*(int*)(iVar15 + 0x20) != 1) {
                        *(int*)(iVar15 + 0x9d4) = *(int*)(iVar15 + 0x20);
                        *(unsigned int*)(iVar15 + 0x20) = 1;
                        *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
                    }
                    iVar7 = soundpad_find_zone_by_id((int)*(short*)(*(int*)(iVar15 + 0x9c0) + 8));
                    iVar7 = DAT_80611b8c + iVar7 * 0x20;
                    *(unsigned int*)(iVar7 + 4) = *(unsigned int*)(iVar7 + 4) | 0x40000000;
                }
                if ((*(unsigned int*)(iVar15 + 0xab4) & 0x80000) != 0) {
                    uVar8 = floor_height_from_position((double)FLOAT_80619000, (double)FLOAT_80619000);
                    *(unsigned int*)(iVar6 + 0x34) = uVar8;
                }
            }
        } else if (iVar7 == 0) {
            if (DAT_806120f8 == *(int*)(iVar15 + 8)) {
                int off = 0;
                for (iVar14 = 0; iVar14 < DAT_80612610; iVar14 = iVar14 + 1) {
                    iVar9 = *(int*)(DAT_80611d78 + off + 0x14);
                    fVar2 = *(float*)(iVar6 + 100) - *(float*)(iVar9 + 100);
                    fVar3 = *(float*)(iVar6 + 0x68) - *(float*)(iVar9 + 0x68);
                    fVar4 = *(float*)(iVar6 + 0x6c) - *(float*)(iVar9 + 0x6c);
                    if (fVar4 * fVar4 + fVar2 * fVar2 + fVar3 * fVar3 < fVar1) {
                        *(int*)(iVar15 + 4) = iVar9;
                    }
                    off = off + 0x141c;
                }
            }
            if ((*(int*)(iVar15 + 4) == 0) || (*(float*)(*(int*)(iVar15 + 4) + 0x1cc) <= FLOAT_80618f98)) {
                uVar8 = aiactor_acquire_nearest_target(iVar6, *(int*)(DAT_80611d78 + 0x14), 0);
                *(unsigned int*)(iVar15 + 4) = uVar8;
            }
        } else {
            *(unsigned int*)(iVar15 + 0x9dc) = 0;
            weapon_compute_fire_origin(*(int*)(iVar15 + 0xc14), 0, 0, (float*)0, (float*)(iVar15 + 0x9f0));
            if (*(int*)(iVar15 + 0x20) != 3) {
                *(int*)(iVar15 + 0x9d4) = *(int*)(iVar15 + 0x20);
                *(unsigned int*)(iVar15 + 0x20) = 3;
                *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
            }
        }
    } else if (iVar9 == 5) {
        if ((*(unsigned int*)(iVar15 + 0xab4) & 0x80000) == 0) {
            if (iVar7 == 0) {
                if (*(float*)(iVar14 + 0x1c4) <= FLOAT_80618f98) {
                    double lb = (double)(CONCAT44(0x43300000, DAT_80611928) ^ 0x80000000);
                    double dVar22a = (double)*(float*)(iVar14 + 0x4c);
                    uVar10 = (unsigned int)(FLOAT_8061906c * FLOAT_806190a0 * (float)(lb - DOUBLE_80618fc8) +
                                            FLOAT_80618fd0);
                    double d19 = (double)(FLOAT_80618fc4 * FLOAT_80583228_arr[uVar10 & 0x7ff]);
                    double d21 = (double)(float)(d19 - (double)(float)(dVar22a + (double)FLOAT_80618f9c));
                    double d18 = (double)(float)(d19 - (double)(float)(dVar22a - (double)FLOAT_80618f9c));
                    double d17 = ABS(d21);
                    double d20 = ABS((double)(float)(d19 - dVar22a));
                    if (d17 <= d20) {
                        if (d17 < ABS(d18)) { d18 = d21; }
                    } else if (d20 < ABS(d18)) {
                        d18 = (double)(float)(d19 - dVar22a);
                    }
                    dVar24 = (double)(float)((double)FLOAT_80619098 * dVar23);
                    d17 = (double)(float)(d18 * dVar24 + dVar22a);
                    d18 = FUN_80390324((double)((float)(d17 - (double)FLOAT_80618fa4) / FLOAT_80618f9c));
                    *(float*)(iVar14 + 0x4c) = -(float)((double)FLOAT_80618f9c * (double)(float)d18 - d17);

                    double d21b = (double)*(float*)(iVar14 + 0x50);
                    lb = (double)(CONCAT44(0x43300000, DAT_80611928) ^ 0x80000000);
                    uVar10 = (unsigned int)(FLOAT_8061906c * FLOAT_806190a4 * (float)(lb - DOUBLE_80618fc8) +
                                            FLOAT_80618fd0);
                    double d17b = (double)(FLOAT_80618fa8 * FLOAT_80583228_arr[uVar10 & 0x7ff]);
                    double d20b = (double)(float)(d17b - (double)(float)(d21b + (double)FLOAT_80618f9c));
                    double d23b = (double)(float)(d17b - (double)(float)(d21b - (double)FLOAT_80618f9c));
                    d18 = ABS(d20b);
                    double d19b = ABS((double)(float)(d17b - d21b));
                    if (d18 <= d19b) {
                        if (d18 < ABS(d23b)) { d23b = d20b; }
                    } else if (d19b < ABS(d23b)) {
                        d23b = (double)(float)(d17b - d21b);
                    }
                    d18 = (double)(float)(d23b * dVar24 + d21b);
                    d23b = FUN_80390324((double)((float)(d18 - (double)FLOAT_80618f98) / FLOAT_80618f9c));
                    *(float*)(iVar14 + 0x50) = -(float)((double)FLOAT_80618f9c * (double)(float)d23b - d18);
                    *(unsigned int*)(iVar14 + 0xb0) = *(unsigned int*)(iVar6 + 0x34);
                    *(unsigned int*)(iVar15 + 0x28) = *(unsigned int*)(iVar15 + 0x28) | 2;
                }
                if ((FLOAT_806190a8 < *(float*)(iVar15 + 0x9d8)) && (*(int*)(iVar15 + 0x20) != 2)) {
                    *(int*)(iVar15 + 0x9d4) = *(int*)(iVar15 + 0x20);
                    *(unsigned int*)(iVar15 + 0x20) = 2;
                    *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
                }
            } else {
                *(unsigned int*)(iVar15 + 0x9d4) = 5;
                *(unsigned int*)(iVar15 + 0x20) = 3;
                *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
            }
        } else {
            if (!(*(int*)(iVar15 + 0xaac) == 1 && *(int*)(iVar15 + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar15 + 0x14)
                                            : *(unsigned int*)(iVar15 + 0x1c)) & 1) == 0)) {
                *(unsigned int*)(iVar15 + 0xaac) = 0x80000;
                *(unsigned int*)(iVar15 + 0xaa8) = 0;
            }
        }
    } else if (iVar9 < 5) {
        if (iVar7 == 0) {
            if (iVar9 != 5) {
                *(int*)(iVar15 + 0x9d4) = iVar9;
                *(unsigned int*)(iVar15 + 0x20) = 5;
                *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(iVar15 + 0x9d0) = *(unsigned int*)(iVar15 + 0x20);
            }
            *(float*)(iVar15 + 0x9d8) = FLOAT_80618f98;
        } else {
            dVar23 = prop_hinge_wrap_angle_to_range(
                (double)FLOAT_80618f98, &local_110, (float*)(*(int*)(iVar15 + 4) + 100));
            iVar7 = prop_hinge_angle_in_range(dVar23, *(int*)(iVar15 + 0xc14));
            if (iVar7 == 0) {
                if (!(*(int*)(iVar15 + 0xaac) == 1 && *(int*)(iVar15 + 0xaa8) == 0 &&
                      (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar15 + 0x14)
                                                : *(unsigned int*)(iVar15 + 0x1c)) & 1) == 0)) {
                    *(unsigned int*)(iVar15 + 0xaac) = 4;
                    *(unsigned int*)(iVar15 + 0xaa8) = 0;
                }
            } else if (*(float*)(iVar14 + 0x1c4) <= FLOAT_80618f98) {
                *(unsigned int*)(iVar14 + 0xb0) = *(unsigned int*)(iVar6 + 0x34);
                *(unsigned int*)(iVar15 + 0x28) = *(unsigned int*)(iVar15 + 0x28) | 2;
            }
        }
    }
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_path_script_tick 0x80266564
// Scripted path-node follow FSM (state field actorState[8]). Walks a compiled
// command list (level-def table DAT_8061208c[+0x34] indexed by char-def +0x68,
// current step +0x2a8). Opcodes: 1 = goto sound-zone (enter state 3 follow),
// 8 = enter wander state 2, 0xb = destroy self, 0x12 = enter forage state 4.
// State 2 plays the step's clip and advances on completion; state 4 handles the
// scripted drop/stance action; state 3 follows the zone path then aims/fires.
// A pending grab (+0x24 handle when char-def bit 0x20000) diverts to a struggle
// action. Advances the fidget timer +0x2a4.
// ---------------------------------------------------------------------------
void aiactor_path_script_tick(unsigned int param_1, double param_2, double param_3, double param_4,
                              long long param_5, long long param_6, long long param_7, long long param_8)
{
    float fVar1;
    int iVar2;
    int iVar3;
    int iVar4;
    bool bVar5;
    unsigned int uVar6;
    unsigned int uVar7;
    int iVar8;
    int* piVar9;
    double dVar10;

    (void)param_1;
    iVar2 = __save_gpr_28();
    piVar9 = *(int**)(iVar2 + 0x7c);
    iVar3 = aiactor_retarget_visible_teammate();
    dVar10 = (double)FLOAT_80618f98;

    if (-1 < piVar9[0x274]) {
        if (piVar9[0x274] != piVar9[8]) {
            piVar9[0x276] = (int)FLOAT_80618f98;
            piVar9[0x275] = piVar9[8];
            piVar9[8] = piVar9[0x274];
        }
        piVar9[0x274] = -1;
    }

    if ((piVar9[0xb] & 0x80U) == 0) {
        piVar9[0x2a4] = (int)FLOAT_80618f98;
    } else {
        dVar10 = (double)(float)((double)FLOAT_80618fb8 /
                                 (double)(float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                                                 DOUBLE_80618fc8));
        piVar9[0x2a4] = (int)(float)((double)FLOAT_806108ec * dVar10 + (double)(float)piVar9[0x2a4]);
    }

    iVar8 = *(int*)(*(int*)(DAT_8061208c + 0x34) + *(int*)(piVar9[0x2fc] + 0x68) * 8);
    uVar7 = *(unsigned int*)(iVar8 + piVar9[0x2a8] * 0xc);

    if ((*(unsigned int*)(piVar9[0x2fc] + 0x54) & 0x20000) != 0) {
        uVar6 = piVar9[0x24];
        if (uVar6 == 0) {
            iVar4 = 0;
        } else {
            iVar4 = DAT_80611c24 + (uVar6 >> 0x16) * 0x2e8;
            if (*(unsigned int*)(iVar4 + 0x2c) != uVar6) {
                iVar4 = 0;
                piVar9[0x24] = 0;
            }
        }
        if (iVar4 != 0) {
            piVar9[1] = iVar4;
            if (!(piVar9[0x2ab] == 1 && piVar9[0x2aa] == 0 &&
                  (((DAT_805015bd == '\n') ? (unsigned int)piVar9[5]
                                            : (unsigned int)piVar9[7]) & 1) == 0)) {
                piVar9[0x2ab] = 8;
                piVar9[0x2aa] = 0;
            }
            __restore_gpr_28();
            return;
        }
    }

    iVar4 = piVar9[8];
    if (iVar4 == 2) {
        iVar3 = anim_clip_flags_active_slot(iVar2, 2, 0, 8);
        if (iVar3 == 0) {
            iVar3 = anim_is_clip_active(iVar2, *(int*)(iVar8 + piVar9[0x2a8] * 0xc + 8));
            if (iVar3 == 0) {
                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
            } else {
                iVar2 = anim_active_slot_userflags(*(int*)(iVar2 + 4));
                if (iVar2 != 0) {
                    piVar9[0x2a8] = piVar9[0x2a8] + 1;
                    if (piVar9[8] != 1) {
                        piVar9[0x275] = piVar9[8];
                        piVar9[8] = 1;
                        piVar9[0x276] = (int)FLOAT_80618f98;
                        piVar9[0x274] = piVar9[8];
                    }
                }
            }
        } else {
            piVar9[0x2a8] = piVar9[0x2a8] + 1;
            if (piVar9[8] != 1) {
                piVar9[0x275] = piVar9[8];
                piVar9[8] = 1;
                piVar9[0x276] = (int)FLOAT_80618f98;
                piVar9[0x274] = piVar9[8];
            }
        }
    } else if (iVar4 < 2) {
        if (iVar4 == 0) {
            piVar9[0x2a8] = 0;
            if (piVar9[8] != 1) {
                piVar9[0x275] = piVar9[8];
                piVar9[8] = 1;
                piVar9[0x276] = (int)FLOAT_80618f98;
                piVar9[0x274] = piVar9[8];
            }
        } else if (-1 < iVar4) {
            switch (uVar7) {
            case 1:
                iVar2 = soundpad_find_zone_by_id(*(int*)(iVar8 + piVar9[0x2a8] * 0xc + 8));
                if (iVar2 < 0) {
                    piVar9[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    iVar2 = soundpad_find_zone_by_id(*(int*)(iVar8 + piVar9[0x2a8] * 0xc + 8));
                    piVar9[0x1a1] = iVar2;
                }
                piVar9[0x26b] = 0;
                piVar9[0x26a] = 0;
                piVar9[0x2a8] = piVar9[0x2a8] + 1;
                if (piVar9[8] != 3) {
                    piVar9[0x275] = piVar9[8];
                    piVar9[8] = 3;
                    piVar9[0x276] = (int)FLOAT_80618f98;
                    piVar9[0x274] = piVar9[8];
                }
                break;
            case 8:
                if (iVar4 != 2) {
                    piVar9[0x275] = iVar4;
                    piVar9[8] = 2;
                    piVar9[0x276] = (int)FLOAT_80618f98;
                    piVar9[0x274] = piVar9[8];
                }
                break;
            case 0xb:
                aiactor_destroy_instance(dVar10, param_2, param_3, param_4,
                                         param_5, param_6, param_7, param_8, iVar2);
                break;
            case 0x12:
                if (iVar4 != 4) {
                    piVar9[0x275] = iVar4;
                    piVar9[8] = 4;
                    piVar9[0x276] = (int)FLOAT_80618f98;
                    piVar9[0x274] = piVar9[8];
                }
                break;
            default:
                break;
            }
        }
    } else if (iVar4 == 4) {
        if ((piVar9[0x2b1] & 1U) != 0) {
            player_ai_drop_all_weapons(dVar10, param_2, param_3, param_4,
                                       (int)param_5, (int)param_6, (int)param_7, (int)param_8, piVar9);
            aiactor_set_stance_flags(iVar2, 8);
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        }
        if (!(piVar9[0x2ab] == 1 && piVar9[0x2aa] == 0 &&
              (((DAT_805015bd == '\n') ? (unsigned int)piVar9[5]
                                        : (unsigned int)piVar9[7]) & 1) == 0)) {
            piVar9[0x2ab] = piVar9[0x2b1];
            piVar9[0x2aa] = piVar9[0x2b0];
        }
    } else if (iVar4 < 4) {
        iVar8 = aiactor_follow_zone_path();
        if (iVar8 == 0) {
            aiactor_set_stance_flags(iVar2, 1);
            aiactor_play_turn_animation(iVar2);
            aiactor_facing_update();
            if ((piVar9[1] == 0) ||
                (bVar5 = aiactor_aim_alignment_test(
                     (double)(float)piVar9[0x26f], iVar2, (float*)(piVar9[1] + 100)),
                 !bVar5)) {
                piVar9[10] = piVar9[10] | 0x40000;
                piVar9[10] = piVar9[10] & 0xfffffffd;
            } else if (iVar3 != 0) {
                piVar9[10] = piVar9[10] | 2;
                piVar9[10] = piVar9[10] & 0xfffbffff;
            }
            if ((piVar9[10] & 1U) != 0) {
                if (DAT_805015bd == '\n') {
                    iVar3 = piVar9[4];
                    iVar2 = piVar9[5];
                } else {
                    iVar3 = piVar9[6];
                    iVar2 = piVar9[7];
                }
                piVar9[0x2af] = iVar2;
                piVar9[0x2ae] = iVar3;
                if (!(piVar9[0x2ab] == 1 && piVar9[0x2aa] == 0 &&
                      (((DAT_805015bd == '\n') ? (unsigned int)piVar9[5]
                                                : (unsigned int)piVar9[7]) & 1) == 0)) {
                    piVar9[0x2ab] = 0x20;
                    piVar9[0x2aa] = 0;
                }
            }
        } else if (piVar9[8] != 1) {
            piVar9[0x275] = piVar9[8];
            piVar9[8] = 1;
            piVar9[0x276] = (int)FLOAT_80618f98;
            piVar9[0x274] = piVar9[8];
        }
    }
    __restore_gpr_28();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_death_anim_tick 0x80266ba4
// Death behaviour tick: apply the pending state swap + loco timer, then, if the
// actor is not already flagged fully-dead (+0x2c bit31) and has a death clip id
// (+0xc64), play it once (stance 8) and destroy the instance when the clip's
// userflags fire. Character 0x24 gets a fixed death-Y (+0x68). Any actor with no
// death clip is destroyed immediately. param_9 = entity. Extra FP args are the
// destroy-callback pass-through.
// ---------------------------------------------------------------------------
void aiactor_death_anim_tick(unsigned int param_1, double param_2, double param_3, double param_4,
                             long long param_5, long long param_6, long long param_7,
                             long long param_8, int param_9)
{
    int iVar1;
    int actorState;
    double dVar3;

    (void)param_1;
    actorState = *(int*)(param_9 + 0x7c);
    dVar3 = (double)aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d4) = *(unsigned int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = *(unsigned int*)(actorState + 0x9d0);
        }
        *(unsigned int*)(actorState + 0x9d0) = 0xffffffff;
    }

    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        dVar3 = (double)(float)((double)FLOAT_80618fb8 /
                                (double)(float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                                                DOUBLE_80618fc8));
        *(float*)(actorState + 0xa90) =
            (float)((double)FLOAT_806108ec * dVar3 + (double)*(float*)(actorState + 0xa90));
    }

    if (((*(unsigned int*)(actorState + 0x2c) & 0x80000000) == 0) &&
        (*(int*)(actorState + 0xc64) != 0)) {
        if (*(short*)(actorState + 0x48) == 0x24) {
            *(float*)(param_9 + 0x68) = FLOAT_806190ac;
        }
        iVar1 = anim_is_clip_active(param_9, *(int*)(actorState + 0xc64));
        if (iVar1 == 0) {
            aiactor_set_stance_flags(param_9, 8);
            anim_start_clip((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        } else {
            iVar1 = anim_primary_clip_userflags(param_9, *(int*)(actorState + 0xc64));
            if (iVar1 != 0) {
                aiactor_destroy_instance(dVar3, param_2, param_3, param_4,
                                         param_5, param_6, param_7, param_8, param_9);
            }
        }
    } else {
        aiactor_destroy_instance(dVar3, param_2, param_3, param_4,
                                 param_5, param_6, param_7, param_8, param_9);
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_reaction_tick 0x80266cfc
// Play the char-def reaction/flinch clip (id at char-def +0x4c) once, and when
// it has (nearly) finished (anim_primary_clip_time_left), commit the queued
// action pair (+0xabc/+0xab8) into the live stance-request pair unless busy.
// Applies the standard pending-state-swap + loco-timer prologue. param_1 = entity.
// ---------------------------------------------------------------------------
void aiactor_behavior_reaction_tick(int entity)
{
    int iVar1;
    int actorState;

    actorState = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d4) = *(unsigned int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = *(unsigned int*)(actorState + 0x9d0);
        }
        *(unsigned int*)(actorState + 0x9d0) = 0xffffffff;
    }

    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        *(float*)(actorState + 0xa90) =
            FLOAT_806108ec *
            (FLOAT_80618fb8 /
             (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8)) +
            *(float*)(actorState + 0xa90);
    }

    iVar1 = anim_is_clip_active(entity, (int)*(short*)(*(int*)(actorState + 0xbf0) + 0x4c));
    if (iVar1 == 0) {
        anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fb8);
        return;
    }
    iVar1 = anim_is_clip_active(entity, (int)*(short*)(*(int*)(actorState + 0xbf0) + 0x4c));
    if (iVar1 == 0) {
        return;
    }
    iVar1 = anim_primary_clip_time_left((double)FLOAT_80618ff0, *(int*)(entity + 4));
    if (iVar1 == 0) {
        return;
    }
    if (!(*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0 &&
          (((DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                    : *(unsigned int*)(actorState + 0x1c)) & 1) == 0)) {
        *(unsigned int*)(actorState + 0xaac) = *(unsigned int*)(actorState + 0xabc);
        *(unsigned int*)(actorState + 0xaa8) = *(unsigned int*)(actorState + 0xab8);
    }
    *(unsigned int*)(actorState + 0xabc) = 0;
    *(unsigned int*)(actorState + 0xab8) = 0;
    return;
}

// ---------------------------------------------------------------------------
// aiactor_behavior_vehicle_ride_tick 0x80266ea8
// Behaviour for an AI actor riding a scripted vehicle. Resolves the three named
// vehicle entities by tag: 'MSUB' (0x5355424d, ride base), 'ARCC' (0x43435241,
// controller, its state block piVar7), and '2RTU' (0x54555232 - selects seat 0
// or 1 via uVar6). States: 0/1 mount + snap to seat offset (DAT_804726b8[]),
// 2 ride idle/hold anim following the base, 3 transitional, <4 dismount at the
// controller's exit point (DAT_804726d0) with clip 0xb26, 4 fall-off ragdoll
// (clip 0xb29, gravity on +0x68) then destroy when below floor.
// ---------------------------------------------------------------------------
void aiactor_behavior_vehicle_ride_tick(void)
{
    int iVar2;
    int iVar3;
    int iVar4;
    int iVar5;
    unsigned int uVar6;
    int* piVar7;
    int actorState;
    double dVar9;

    iVar2 = FUN_803826e4_ret();
    actorState = *(int*)(iVar2 + 0x7c);
    iVar3 = aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d4) = *(unsigned int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = *(unsigned int*)(actorState + 0x9d0);
        }
        *(unsigned int*)(actorState + 0x9d0) = 0xffffffff;
    }

    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        *(float*)(actorState + 0xa90) =
            FLOAT_806108ec *
            (FLOAT_80618fb8 /
             (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8)) +
            *(float*)(actorState + 0xa90);
    }

    iVar4 = entity_handle_resolve(0x5355424d);          // 'MSUB'
    iVar5 = entity_handle_resolve(0x43435241);          // 'ARCC'
    piVar7 = *(int**)(iVar5 + 0x7c);
    iVar5 = entity_handle_resolve(0x54555232);          // '2RTU'
    uVar6 = (unsigned int)(iVar2 == iVar5);
    iVar5 = *(int*)(actorState + 0x20);

    if (iVar5 == 2) {
        if (*(char*)((int)piVar7 + uVar6 + 0x34) == '\0') {
            iVar5 = anim_is_clip_active(iVar2, 0x25);
            if (iVar5 == 0) {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618f98);
            } else {
                *(float*)(*(int*)(iVar2 + 4) + 0x100) = FLOAT_80618f98;
            }
            if ((*(unsigned int*)(&DAT_804d34dc +
                    (short)(&DAT_804d1cd0)[*(int*)(*(int*)(iVar2 + 4) + 0xcc)] * 8) & 0x100) != 0) {
                *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffbffff;
                if (((*(int**)(iVar2 + 4))[0x32] & 1U) != 0) {
                    anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar2 + 4), 2);
                }
            }
        } else {
            iVar5 = anim_is_clip_active(iVar2, 0xb28);
            if (iVar5 == 0) {
                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
            }
            *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffdffff;
            *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffbffff;
            anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar2 + 4), 1);
        }
        iVar5 = uVar6 * 0xc;
        *(float*)(iVar2 + 100) = *(float*)(iVar4 + 100) + DAT_804726b8[iVar5 / 4 + 0];
        *(float*)(iVar2 + 0x68) = *(float*)(iVar4 + 0x68) + DAT_804726b8[iVar5 / 4 + 1];
        *(float*)(iVar2 + 0x6c) = *(float*)(iVar4 + 0x6c) + DAT_804726b8[iVar5 / 4 + 2];
        aiactor_turn_toward_leader();
        if (iVar3 != 0) {
            *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 2;
        }
        if (((*piVar7 == 3) && (piVar7[0xe] == iVar2)) && (*(int*)(actorState + 0x20) != 3)) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = 3;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
    } else if (iVar5 < 2) {
        if (iVar5 == 0) {
            int off = uVar6 * 0xc;
            *(float*)(iVar2 + 100) = *(float*)(iVar4 + 100) + DAT_804726b8[off / 4 + 0];
            *(float*)(iVar2 + 0x68) = *(float*)(iVar4 + 0x68) + DAT_804726b8[off / 4 + 1];
            *(float*)(iVar2 + 0x6c) = *(float*)(iVar4 + 0x6c) + DAT_804726b8[off / 4 + 2];
            if (*(int*)(actorState + 0x20) != 2) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 2;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        } else if (-1 < iVar5) {
            *(float*)(*(int*)(iVar2 + 4) + 0x100) = FLOAT_80618fd0;
            iVar3 = anim_active_slot_userflags(*(int*)(iVar2 + 4));
            if ((iVar3 != 0) && (*(int*)(actorState + 0x20) != 2)) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 2;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
            int off = uVar6 * 0xc;
            *(float*)(iVar2 + 100) = *(float*)(iVar4 + 100) + DAT_804726b8[off / 4 + 0];
            *(float*)(iVar2 + 0x68) = *(float*)(iVar4 + 0x68) + DAT_804726b8[off / 4 + 1];
            *(float*)(iVar2 + 0x6c) = *(float*)(iVar4 + 0x6c) + DAT_804726b8[off / 4 + 2];
        }
    } else if (iVar5 == 4) {
        iVar3 = anim_is_clip_active(iVar2, 0xb29);
        if (iVar3 == 0) {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        }
        *(float*)(iVar2 + 0x68) =
            -(float)((double)FLOAT_80619000 * (double)FLOAT_806108ec - (double)*(float*)(iVar2 + 0x68));
        if ((double)*(float*)(iVar2 + 0x68) < (double)FLOAT_806190b0) {
            aiactor_destroy_instance((double)*(float*)(iVar2 + 0x68),
                                     (double)FLOAT_806108ec, (double)FLOAT_80619000,
                                     0, 0, 0, 0, 0, iVar2);
        }
    } else if (iVar5 < 4) {
        iVar3 = anim_is_clip_active(iVar2, 0xb26);
        if (iVar3 == 0) {
            anim_play_scaled((double)FLOAT_80618fac, (double)FLOAT_80618fd4);
        }
        *(float*)(iVar2 + 100) = (float)piVar7[4] + DAT_804726d0;
        *(float*)(iVar2 + 0x68) = (float)piVar7[5] + DAT_804726d4;
        *(float*)(iVar2 + 0x6c) = (float)piVar7[6] + DAT_804726d8;
        iVar3 = piVar7[0x10];
        *(int*)(iVar2 + 0xc4) = iVar3;
        *(int*)(iVar2 + 0x70) = iVar3;
        if (piVar7[0xe] == 0) {
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
            if (*(int*)(actorState + 0x20) != 4) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 4;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        }
    }
    (void)dVar9;
    FUN_80382730();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_scripted_update 0x8026367c
// The main scripted-AI behaviour driver: interprets the per-actor script command
// list (trigger descriptor at DAT_8061208c[+0x3c] + step +0x9ec*0x48, cmd fields
// via puVar9). Prologue applies the pending state swap + loco timer and refreshes
// the actor's nav target from its current sound-zone node. Detects/broadcasts
// alerts (aiactor_alert_broadcast_group) when LOS + relation gating passes, then
// dispatches on behaviour-state +0x20:
//   state 4  : run the current spawn/anim script step (opcode via puVar9[0xf]),
//              face target, play the step clip, and on completion advance +0x9ec.
//   state 1  : execute the script opcode in puVar9[2] (0 goto-zone, 3 idle-turn,
//              5 wait-for-target, 6 jump, 0xc destroy, 0xd/e set-clip, 0x11-0x13
//              capability toggles, 0x15 teleport, 0x17 class-cond branch,
//              0x1b trigger-cond branch, 0x1c queue-action, 0x1d rng-branch,
//              0x27/0x28 alert flags, 0x2b blink, 0x2c scripted damage, ...).
//   state 2  : approach target with locomotion anim + movement tick.
//   state 3  : follow zone path playing the walk/step clip.
//   state 0/-1: script entry: reset, optional invincible render, enter state 1.
//   state 100: stunned/holding clip; exit to state 1 when the clip clears.
//   state <6 : self-destruct.
// Register-save FP spill locals from the prologue are dead and omitted.
// ---------------------------------------------------------------------------
void aiactor_scripted_update(unsigned int param_1, double param_2, double param_3, double param_4,
                             double param_5, double param_6, double param_7, long long param_8)
{
    float fVar1;
    int iVar2;
    int iVar3;
    int* piVar4;
    int iVar5;
    unsigned int uVar6;
    unsigned int uVar7;
    bool bVar8;
    unsigned int* puVar9;
    int iVar10;
    int iVar11;
    double dVar12;
    long long uVar13;
    int local_98;
    int local_94;
    int local_90;
    unsigned int local_80;
    float local_78;
    unsigned int local_74;
    unsigned int local_70;
    unsigned int local_68;
    float local_64;
    unsigned int local_50;
    unsigned int uStack_34;

    (void)param_1;
    iVar2 = FUN_803826e4_ret();
    iVar10 = *(int*)(iVar2 + 0x7c);
    iVar3 = aiactor_retarget_visible_teammate();
    dVar12 = (double)FLOAT_80618f98;

    if (-1 < *(int*)(iVar10 + 0x9d0)) {
        if (*(int*)(iVar10 + 0x9d0) != *(int*)(iVar10 + 0x20)) {
            *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(iVar10 + 0x9d4) = *(unsigned int*)(iVar10 + 0x20);
            *(unsigned int*)(iVar10 + 0x20) = *(unsigned int*)(iVar10 + 0x9d0);
        }
        *(unsigned int*)(iVar10 + 0x9d0) = 0xffffffff;
    }
    if ((*(unsigned int*)(iVar10 + 0x2c) & 0x80) == 0) {
        *(float*)(iVar10 + 0xa90) = FLOAT_80618f98;
    } else {
        uStack_34 = DAT_806109c0 ^ 0x80000000;
        dVar12 = (double)(float)((double)FLOAT_80618fb8 /
                                 (double)(float)((double)CONCAT44(0x43300000, uStack_34) - DOUBLE_80618fc8));
        *(float*)(iVar10 + 0xa90) = (float)((double)FLOAT_806108ec * dVar12 + (double)*(float*)(iVar10 + 0xa90));
    }

    iVar11 = -1;
    puVar9 = (unsigned int*)(*(int*)(DAT_8061208c + 0x3c) + *(int*)(iVar10 + 0x9ec) * 0x48);
    piVar4 = soundpad_resolve_zone_node(*(int*)(iVar10 + 0x678));
    *(int*)(iVar10 + 0x30) = piVar4[3];
    piVar4 = soundpad_resolve_zone_node(*(int*)(iVar10 + 0x678));
    *(int*)(iVar10 + 0x34) = piVar4[4];
    piVar4 = soundpad_resolve_zone_node(*(int*)(iVar10 + 0x678));
    *(int*)(iVar10 + 0x38) = piVar4[5];

    if ((*(unsigned int*)(iVar10 + 0x2c) & 0x8000) != 0) {
        if (*(int*)(iVar10 + 4) == 0) {
            *(unsigned int*)(iVar10 + 4) = *(unsigned int*)(DAT_80611d78 + 0x14);
            iVar11 = *(int*)(iVar10 + 4);
            {
                float dx = *(float*)(iVar2 + 100) - *(float*)(iVar11 + 100);
                float dy = *(float*)(iVar2 + 0x68) - *(float*)(iVar11 + 0x68);
                float dz = *(float*)(iVar2 + 0x6c) - *(float*)(iVar11 + 0x6c);
                *(float*)(iVar10 + 0xaf4) = (float)((double)(dz * dz) + (double)(dx * dx + dy * dy));
            }
            iVar5 = aiactor_evaluate_target_los();
            *(float*)(iVar10 + 0xaf4) = FLOAT_80618f98;
            *(unsigned int*)(iVar10 + 4) = 0;
        } else {
            iVar5 = aiactor_evaluate_target_los();
        }
        iVar11 = 0;
        if (iVar5 != 0) {
            if (!(*(int*)(iVar10 + 0xaac) == 1 && *(int*)(iVar10 + 0xaa8) == 0 &&
                  (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar10 + 0x14)
                                            : *(unsigned int*)(iVar10 + 0x1c)) & 1) == 0)) {
                *(unsigned int*)(iVar10 + 0xaac) = 4;
                *(unsigned int*)(iVar10 + 0xaa8) = 0;
            }
            FUN_80382730();
            return;
        }
    }

    iVar5 = aiactor_alert_group_flagA_any(*(unsigned int*)(iVar10 + 0xbec));
    if ((iVar5 == 0) && ((iVar5 = *(int*)(iVar10 + 4), iVar5 == 0 || (*(int*)(iVar5 + 0x20) == 8)))) {
        if (iVar5 == 0) {
            *(unsigned int*)(iVar10 + 4) = *(unsigned int*)(DAT_80611d78 + 0x14);
            iVar5 = *(int*)(iVar10 + 4);
            {
                float dx = *(float*)(iVar2 + 100) - *(float*)(iVar5 + 100);
                float dy = *(float*)(iVar2 + 0x68) - *(float*)(iVar5 + 0x68);
                float dz = *(float*)(iVar2 + 0x6c) - *(float*)(iVar5 + 0x6c);
                *(float*)(iVar10 + 0xaf4) = (float)((double)(dz * dz) + (double)(float)((double)(dx * dx) + (double)(dy * dy)));
            }
            iVar5 = *(int*)(*(int*)(iVar10 + 4) + 0x7c);
            if (iVar11 == -1) {
                iVar11 = aiactor_evaluate_target_los();
            }
            *(float*)(iVar10 + 0xaf4) = FLOAT_80618f98;
            *(unsigned int*)(iVar10 + 4) = 0;
        } else {
            iVar5 = *(int*)(iVar5 + 0x7c);
            if (iVar11 == -1) {
                iVar11 = aiactor_evaluate_target_los();
            }
        }
        if (iVar11 == 1) {
            uStack_34 = DAT_806109c0 ^ 0x80000000;
            if ((double)*(float*)(iVar5 + 0xb4c) <
                (double)(float)((double)FLOAT_80619064 *
                                (double)(float)((double)FLOAT_80618fb8 /
                                                (double)(float)((double)CONCAT44(0x43300000, uStack_34) -
                                                                DOUBLE_80618fc8)))) {
                aiactor_alert_broadcast_group(0, iVar2, *(int*)(iVar5 + 0x678));
            }
        }
        if (iVar11 == 2) {
            aiactor_alert_broadcast_group(0, iVar2, *(int*)(iVar5 + 0x678));
        }
    }

    iVar11 = *(int*)(iVar10 + 0x20);
    if (iVar11 == 4) {
        iVar11 = aiactor_spawn_trigger_eval_ret();
        if ((puVar9[0xd] & 0x20000) == 0) {
            if ((puVar9[0xd] & 2) != 0) {
                aiactor_turn_toward_entity(iVar2, *(int*)(DAT_80611d78 + 0x14), 0);
            }
        } else if (*(int*)(iVar10 + 0xacc) == 0) {
            aiactor_turn_toward_angle((double)*(float*)(*(int*)(iVar10 + 0x67c) + 0x18), iVar2, 0);
        } else {
            aiactor_turn_toward_angle((double)*(float*)(*(int*)(iVar10 + 0xacc) + 0x18), iVar2, 0);
        }
        if ((puVar9[0xd] & 0x40000) != 0) {
            prop_hinge_apply_spring((double)FLOAT_80618fa0, iVar2, *(int*)(iVar10 + 0x67c));
        }
        *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffdffff;
        *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffbffff;
        anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar2 + 4), 1);
        iVar5 = *(int*)(iVar2 + 4);
        if (((*(unsigned int*)(&DAT_804d34d8 + (short)(&DAT_804d1cd0)[*(int*)(iVar5 + 0xcc)] * 8) & 0x100000) != 0) ||
            ((FLOAT_80618f98 < *(float*)(iVar5 + 400) &&
              ((*(unsigned int*)(&DAT_804d34d8 + (short)(&DAT_804d1cd0)[*(int*)(iVar5 + 0x124)] * 8) & 0x100000) != 0)))) {
            if ((puVar9[0xd] & 0x80000) == 0) {
                if ((iVar3 == 0) ||
                    (bVar8 = aiactor_facing_dot_test((double)FLOAT_80619054, iVar2, (float*)(*(int*)(iVar10 + 4) + 100)),
                     !bVar8)) {
                    *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) | 2;
                    *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) | 0x400;
                } else {
                    *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) | 2;
                    *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xfffffbff;
                }
            } else {
                *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) | 2;
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) | 0x400;
            }
        } else if ((puVar9[0xd] & 0x80000) == 0) {
            *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffffffd;
            *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xfffffbff;
        } else {
            *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) | 2;
            if ((iVar3 == 0) ||
                (bVar8 = aiactor_facing_dot_test((double)FLOAT_80619054, iVar2, (float*)(*(int*)(iVar10 + 4) + 100)),
                 !bVar8)) {
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) | 0x400;
            }
        }
        if ((*(unsigned int*)(iVar10 + 0x28) & 2) != 0) {
            if (puVar9[4] != 0) {
                uVar7 = (*(int (**)(int))(&DAT_80472ef8 + puVar9[4] * 4))(iVar2);
                *(unsigned int*)(iVar10 + 4) = uVar7;
            }
            if (*(int*)(iVar10 + 4) == 0) {
                *(int*)(iVar10 + 4) = iVar2;
            }
        }
        iVar3 = anim_is_clip_active(iVar2, (int)*(short*)(puVar9 + 0xf));
        if ((iVar3 == 0) || ((double)*(float*)(iVar10 + 0xb30) < (double)FLOAT_80618f98)) {
            fVar1 = FLOAT_80618ff0;
            if ((puVar9[0xd] & 4) != 0) {
                fVar1 = FLOAT_80618f98;
            }
            if (((puVar9[0xd] & 0x2000000) == 0) ||
                (iVar3 = anim_event_scan_active(), iVar3 != 0)) {
                uVar13 = anim_play_scaled((double)fVar1, (double)FLOAT_80618fb8);
                if (((int)((unsigned long long)uVar13 >> 0x20) == 0) &&
                    (iVar3 = anim_clip_has_flags(iVar2, (int)uVar13, 0, 8), iVar3 == 0)) {
                    anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    if (*(int*)(iVar10 + 0x20) != 100) {
                        *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                        *(unsigned int*)(iVar10 + 0x20) = 100;
                        *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                    }
                    *(int*)(iVar10 + 0xaa0) = (int)*(short*)(puVar9 + 0xf);
                }
                *(unsigned int*)(iVar2 + 0x28) = *(unsigned int*)(iVar2 + 0x28) | 1;
                *(float*)(iVar10 + 0xb30) = FLOAT_80618f98;
            }
        } else if (iVar11 == 0) {
            iVar3 = anim_primary_clip_userflags(iVar2, (int)*(short*)(puVar9 + 0xf));
            if ((iVar3 != 0) &&
                ((*(unsigned int*)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[*(int*)(*(int*)(iVar2 + 4) + 0xcc)] * 8) & 1) == 0)) {
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                if (*(int*)(iVar10 + 0x20) != 1) {
                    *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                    *(unsigned int*)(iVar10 + 0x20) = 1;
                    *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                }
                *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xffffbfff;
            }
        } else {
            if (iVar11 == 1) {
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
            } else {
                *(unsigned int*)(iVar10 + 0x9dc) = *(unsigned int*)(iVar10 + 0x9ec);
                *(unsigned int*)(iVar10 + 0x9ec) = puVar9[0xe];
            }
            if (*(int*)(iVar10 + 0x20) != 1) {
                *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                *(unsigned int*)(iVar10 + 0x20) = 1;
                *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
            }
            *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xffffbfff;
        }
        aiactor_movement_tick(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    } else if (iVar11 < 4) {
        if (iVar11 == 1) {
            switch (puVar9[2]) {
            case 0:
                *(float*)(iVar10 + 0xb30) = FLOAT_80618f98;
                if ((puVar9[0xd] & 0x10000) == 0) {
                    iVar2 = soundpad_find_zone_by_id(puVar9[5]);
                    if (iVar2 < 0) {
                        *(unsigned int*)(iVar10 + 0x684) =
                            *(unsigned int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    } else {
                        iVar2 = soundpad_find_zone_by_id(puVar9[5]);
                        *(int*)(iVar10 + 0x684) = iVar2;
                    }
                    *(unsigned int*)(iVar10 + 0x9ac) = 0;
                    *(unsigned int*)(iVar10 + 0x9a8) = 0;
                    if (*(int*)(iVar10 + 0x20) != 3) {
                        *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                        *(unsigned int*)(iVar10 + 0x20) = 3;
                        *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                    }
                } else {
                    iVar3 = aiactor_zone_crowd_cost_ret();
                    if (iVar3 < 0) {
                        iVar2 = anim_is_clip_active(iVar2, 0x25);
                        if (iVar2 == 0) {
                            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                        }
                    } else {
                        *(int*)(iVar10 + 0x684) = iVar3;
                        *(unsigned int*)(iVar10 + 0x9ac) = 0;
                        *(unsigned int*)(iVar10 + 0x9a8) = 0;
                        if (*(int*)(iVar10 + 0x20) != 3) {
                            *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                            *(unsigned int*)(iVar10 + 0x20) = 3;
                            *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                            *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                        }
                    }
                }
                break;
            case 3:
                if (iVar3 == 0) {
                    *(float*)(iVar10 + 0xa00) = FLOAT_80618ff8;
                } else {
                    *(float*)(iVar10 + 0xa00) = FLOAT_80618f98;
                }
                *(float*)(iVar10 + 0xb30) = FLOAT_80618f98;
                *(unsigned int*)(iVar10 + 4) = 0;
                if (*(int*)(iVar10 + 0x20) != 2) {
                    *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                    *(unsigned int*)(iVar10 + 0x20) = 2;
                    *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                }
                break;
            case 5:
                *(float*)(iVar10 + 0xb30) = FLOAT_80618fc0;
                if (*(int*)(iVar10 + 0x20) != 4) {
                    *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                    *(unsigned int*)(iVar10 + 0x20) = 4;
                    *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                }
                *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) | 0x4000;
                if (puVar9[4] != 0) {
                    uVar7 = (*(int (**)(int))(&DAT_80472ef8 + puVar9[4] * 4))(iVar2);
                    *(unsigned int*)(iVar10 + 4) = uVar7;
                }
                break;
            case 6:
                *(unsigned int*)(iVar10 + 0x9ec) = puVar9[0xe];
                break;
            case 0xc:
                aiactor_destroy_instance(dVar12, param_2, param_3, param_4,
                                         param_5, param_6, param_7, param_8, iVar2);
                break;
            case 0xd:
            case 0xe:
                *(int*)(iVar10 + 0x9e0) = (int)*(short*)(puVar9 + 0xf);
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            case 0xf:
            case 0x10:
                *(unsigned int*)(iVar10 + 0x9e0) = 0xffffffff;
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            case 0x11:
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) | 0x1000;
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xffffbfff;
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xdfffffff;
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            case 0x12:
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) | 0x4000;
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) | 0x20000000;
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xffffefff;
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            case 0x13:
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xffffbfff;
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xffffefff;
                if ((*(unsigned int*)(*(int*)(iVar10 + 0xbf0) + 0x58) & 0x20000000) == 0) {
                    *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xdfffffff;
                }
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            case 0x15: {
                iVar3 = soundpad_find_zone_by_id(puVar9[5]);
                player_ai_teleport_wrapper(iVar10, iVar3, (unsigned int)puVar9[0xd] >> 0x11 & 1);
                if (iVar3 < 0) {
                    *(unsigned int*)(iVar10 + 0x684) =
                        *(unsigned int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    *(int*)(iVar10 + 0x684) = iVar3;
                }
                *(unsigned int*)(iVar10 + 0x9ac) = 0;
                *(unsigned int*)(iVar10 + 0x9a8) = 0;
                if (*(short*)(puVar9 + 0xf) != -1) {
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618f98);
                    *(float*)(iVar2 + 0x70) = FLOAT_80618f98;
                    *(float*)(iVar2 + 0xc4) = FLOAT_80618f98;
                }
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            }
            case 0x16:
                *(unsigned int*)(iVar10 + 0x9ec) = *(unsigned int*)(iVar10 + 0x9dc);
                break;
            case 0x17:
                uVar6 = statlvl_class_is_state5(puVar9[5]);
                if (uVar6 == 0) {
                    *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                } else {
                    *(unsigned int*)(iVar10 + 0x9ec) = puVar9[0xe];
                }
                break;
            case 0x1b:
                iVar2 = aiactor_spawn_trigger_eval_ret();
                if (iVar2 == 0) {
                    *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                } else {
                    if (iVar2 == 1) {
                        *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                    } else {
                        *(unsigned int*)(iVar10 + 0x9dc) = *(unsigned int*)(iVar10 + 0x9ec);
                        *(unsigned int*)(iVar10 + 0x9ec) = puVar9[0xe];
                    }
                    if (*(int*)(iVar10 + 0x20) != 1) {
                        *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                        *(unsigned int*)(iVar10 + 0x20) = 1;
                        *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                    }
                }
                break;
            case 0x1c: {
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                *(unsigned int*)(iVar10 + 0xabc) = *(unsigned int*)(iVar10 + 0x14);
                *(unsigned int*)(iVar10 + 0xab8) = *(unsigned int*)(iVar10 + 0x10);
                iVar2 = entity_handle_resolve(puVar9[5]);
                *(int*)(iVar10 + 4) = iVar2;
                uVar7 = *puVar9;
                if (!(*(int*)(iVar10 + 0xaac) == 1 && *(int*)(iVar10 + 0xaa8) == 0 &&
                      (((DAT_805015bd == '\n') ? *(unsigned int*)(iVar10 + 0x14)
                                                : *(unsigned int*)(iVar10 + 0x1c)) & 1) == 0)) {
                    *(unsigned int*)(iVar10 + 0xaac) = puVar9[1];
                    *(unsigned int*)(iVar10 + 0xaa8) = uVar7;
                }
                break;
            }
            case 0x1d: {
                uStack_34 = rng_counter_tick();
                int rr = (int)(FLOAT_80618fd0 +
                               (float)((double)CONCAT44(0x43300000, uStack_34) - DOUBLE_80618fe8) /
                               FLOAT_80618fd8);
                if (rr == 0) {
                    *(unsigned int*)(iVar10 + 0x9ec) = puVar9[0xe];
                } else {
                    *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                }
                break;
            }
            case 0x27:
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) | 0x8000;
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            case 0x28:
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xffff7fff;
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            case 0x2b:
                pickup_blink_start((double)FLOAT_80618fa8, (double)FLOAT_80618fd0, iVar10 + 0xc68);
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            case 0x2c: {
                switch_slot_reset((int)&local_98);
                local_50 = 0;
                (void)local_50;
                local_74 = 7;
                local_70 = 0;
                local_68 = 0;
                local_64 = FLOAT_80618f98;
                (void)local_64; (void)local_70; (void)local_68; (void)local_74;
                if (DAT_80612614 == 0x1a) {
                    iVar2 = aiactor_find_player_by_team(0x5054435a);
                    if (iVar2 != 0) {
                        local_80 = 0x1e;
                        (void)local_80;
                        local_78 = FLOAT_80619068;
                        (void)local_78;
                        local_98 = iVar2;
                        local_94 = iVar2;
                        local_90 = iVar2;
                        damage_dispatch_to_entity(dVar12, param_2, param_3, param_4,
                                                  param_5, param_6, param_7, param_8, &local_98);
                    }
                } else if (((DAT_80612614 < 0x1a) && (DAT_80612614 == 6)) &&
                           (piVar4 = (int*)(int)FUN_8027ea98_ret(), piVar4 != (int*)0)) {
                    local_98 = *piVar4;
                    local_80 = 4;
                    (void)local_80;
                    local_78 = FLOAT_80619068;
                    (void)local_78;
                    local_94 = local_98;
                    local_90 = local_98;
                    damage_dispatch_to_entity(dVar12, param_2, param_3, param_4,
                                              param_5, param_6, param_7, param_8, &local_98);
                }
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            }
            case 0x2e:
            case 0x2f:
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                break;
            default:
                break;
            }
        } else if (iVar11 < 1) {
            if (-1 < iVar11) {
                *(unsigned int*)(iVar10 + 4) = 0;
                *(unsigned int*)(iVar10 + 0x9e0) = 0xffffffff;
                if (*(int*)(*(int*)(iVar10 + 0xbf0) + 8) == 0x44423031) {   // '10BD'
                    entity_set_render_type(iVar2, (int)(long)(void*)&damage_filter_invincible);
                }
                if (puVar9[2] != 5) {
                    *(unsigned int*)(iVar2 + 0x28) = *(unsigned int*)(iVar2 + 0x28) | 1;
                }
                if (*(int*)(iVar10 + 0x20) != 1) {
                    *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                    *(unsigned int*)(iVar10 + 0x20) = 1;
                    *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                }
            }
        } else if (iVar11 < 3) {
            iVar11 = aiactor_spawn_trigger_eval_ret();
            if (iVar11 == 0) {
                aiactor_locomotion_anim_tick(iVar2, iVar3, (int)puVar9);
                iVar11 = *(int*)(iVar10 + 4);
                if (((iVar11 != 0) && (*(float*)(iVar11 + 0x1cc) <= FLOAT_80618f98)) ||
                    ((iVar11 == 0 || (*(int*)(iVar10 + 8) == DAT_806120f8)))) {
                    if (puVar9[4] != 0) {
                        uVar7 = (*(int (**)(int))(&DAT_80472ef8 + puVar9[4] * 4))(iVar2);
                        *(unsigned int*)(iVar10 + 4) = uVar7;
                    }
                    if ((*(int*)(iVar10 + 4) == 0) ||
                        ((iVar11 = aiactor_retarget_visible_teammate(), iVar11 == 0 &&
                          ((puVar9[0xd] & 0x400000) != 0)))) {
                        *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                        if (*(int*)(iVar10 + 0x20) != 1) {
                            *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                            *(unsigned int*)(iVar10 + 0x20) = 1;
                            *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                            *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                        }
                        *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffffffd;
                        *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xfffffbff;
                        FUN_80382730();
                        return;
                    }
                }
                if ((puVar9[0xd] & 0x40000) != 0) {
                    prop_hinge_apply_spring((double)FLOAT_80618fa0, iVar2, *(int*)(iVar10 + 0x67c));
                }
                if (((iVar3 == 0) && ((puVar9[0xd] & 0x80000) == 0)) &&
                    (iVar3 = entity_handle_resolve(0x54524350), *(int*)(iVar10 + 4) != iVar3)) {
                    *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffdffff;
                    *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffbffff;
                    anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar2 + 4), 1);
                } else {
                    aiactor_turn_toward_leader();
                    *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffdffff;
                    *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffbffff;
                    anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar2 + 4), 1);
                }
                aiactor_movement_tick(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
            } else {
                if (iVar11 == 1) {
                    *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                } else {
                    *(unsigned int*)(iVar10 + 0x9dc) = *(unsigned int*)(iVar10 + 0x9ec);
                    *(unsigned int*)(iVar10 + 0x9ec) = puVar9[0xe];
                }
                if (*(int*)(iVar10 + 0x20) != 1) {
                    *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                    *(unsigned int*)(iVar10 + 0x20) = 1;
                    *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                }
                *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffffffd;
                *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xfffffbff;
            }
        } else {
            iVar3 = aiactor_spawn_trigger_eval_ret();
            fVar1 = FLOAT_80618ff0;
            if ((puVar9[0xd] & 4) != 0) {
                fVar1 = FLOAT_80618f98;
            }
            *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffffffd;
            *(unsigned int*)(iVar10 + 0x2c) = *(unsigned int*)(iVar10 + 0x2c) & 0xfffffbff;
            iVar11 = aiactor_follow_zone_path();
            if (iVar11 == 0) {
                if (iVar3 == 0) {
                    if (*(int*)(iVar10 + 0x9e0) == -1) {
                        iVar3 = anim_is_clip_active(iVar2, 0x2a);
                        if (iVar3 == 0) {
                            anim_play_scaled((double)fVar1, (double)FLOAT_80618fb8);
                        }
                    } else {
                        iVar3 = anim_is_clip_active(iVar2, *(int*)(iVar10 + 0x9e0));
                        if (iVar3 == 0) {
                            uVar13 = anim_play_scaled((double)fVar1, (double)FLOAT_80618fb8);
                            if (((int)((unsigned long long)uVar13 >> 0x20) == 0) &&
                                (iVar3 = anim_clip_has_flags(iVar2, (int)uVar13, 0, 8), iVar3 == 0)) {
                                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                                if (*(int*)(iVar10 + 0x20) != 100) {
                                    *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                                    *(unsigned int*)(iVar10 + 0x20) = 100;
                                    *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                                    *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                                }
                                *(unsigned int*)(iVar10 + 0xaa0) = *(unsigned int*)(iVar10 + 0x9e0);
                            }
                        }
                    }
                    *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffdffff;
                    *(unsigned int*)(iVar10 + 0x28) = *(unsigned int*)(iVar10 + 0x28) & 0xfffbffff;
                    anim_set_playback_rate((double)FLOAT_8061900c, *(int**)(iVar2 + 4), 1);
                    aiactor_facing_update();
                    aiactor_movement_tick(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
                } else {
                    if (iVar3 == 1) {
                        *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                    } else {
                        *(unsigned int*)(iVar10 + 0x9dc) = *(unsigned int*)(iVar10 + 0x9ec);
                        *(unsigned int*)(iVar10 + 0x9ec) = puVar9[0xe];
                    }
                    if (*(int*)(iVar10 + 0x20) != 1) {
                        *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                        *(unsigned int*)(iVar10 + 0x20) = 1;
                        *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                    }
                }
            } else {
                *(int*)(iVar10 + 0x9ec) = *(int*)(iVar10 + 0x9ec) + 1;
                if (*(int*)(iVar10 + 0x20) != 1) {
                    *(int*)(iVar10 + 0x9d4) = *(int*)(iVar10 + 0x20);
                    *(unsigned int*)(iVar10 + 0x20) = 1;
                    *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
                }
            }
        }
    } else if (iVar11 == 100) {
        if (*(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[*(int*)(iVar10 + 0xaa0)] * 4) != 0) {
            *(unsigned int*)(iVar10 + 0x9d4) = 100;
            *(unsigned int*)(iVar10 + 0x20) = 1;
            *(float*)(iVar10 + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(iVar10 + 0x9d0) = *(unsigned int*)(iVar10 + 0x20);
        }
    } else if ((iVar11 < 100) && (iVar11 < 6)) {
        aiactor_destroy_instance(dVar12, param_2, param_3, param_4, param_5, param_6, param_7, param_8, iVar2);
    }
    FUN_80382730();
    return;
}

// ===========================================================================
// Address range: 0x80267384 - 0x80269147 | batch_742 | 2026-09-09
//
// Continuation of the AI actor per-behaviour-state tick cluster. These ticks
// cover: a bone-lean matrix pass, prop/vehicle-ride attachment, scripted-death
// and conveyor-belt (GHZ) ride states, and the two proximity/melee scan passes
// that walk the spawn-visible actor table (DAT_80588610, iterated from
// DAT_80612610 to DAT_80612098). They share the same prologue as the rest of
// the file: resolve the scratch frame (FUN_803826xx), retarget a visible
// teammate, apply the pending-state swap trio (+0x9d0/+0x9d4/+0x9d8) and
// advance the loco timer (+0xa90). Register-pair spawn-frame helpers
// (FUN_803826d4/e8/f0 open, FUN_80382720/34/3c close) are opaque calls that
// return the entity/frame handle; the wide double/u64 param tails on the
// switch-driven ticks mirror the PPC FP-arg ABI and are carried verbatim.
//
// Functions:
//   aiactor_behavior_bone_lean_tick     0x80267384  ZYX bone-matrix lean pass
//   aiactor_behavior_prop_ride_tick     0x8026749c  ride attached prop/vehicle
//   aiactor_behavior_scripted_death_tick 0x80267990 scripted-death clip 0xbf3
//   aiactor_behavior_belt_ride_tick     0x80267b38  GHZ conveyor-belt ride FSM
//   aiactor_melee_proximity_scan        0x80268018  scan actors -> melee cue
//   aiactor_melee_proximity_stub        0x80268168  (empty tail stub)
//   aiactor_melee_proximity_scan_armed  0x8026816c  scan (weapon-type gated)
//   aiactor_melee_resolve_target        0x80268350  pick melee target + bark
//   aiactor_predict_intercept_point     0x80268494  ballistic intercept solve
//   aiactor_behavior_mounted_gun_tick   0x80268698  mounted-gun aim/fire FSM
// ---------------------------------------------------------------------------
extern "C" {
// ---- new named cross-module helpers ---------------------------------------
extern void mtx4_rotate_about_point(float* outMtx, float* axisAngle, float* pivot);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void model_pose_eval(void);
extern void entity_assign_room(int entity);
extern void entity_register_for_update(int entity, int phase, int key);
extern void aiactor_update_aim_height(void);
extern void anim_reset_blend_and_clip(int ctrl);
extern void anim_set_blend_weight(double weight, int ctrl);
extern int  anim_secondary_clip_finished(int ctrl);
extern void ghz_belt_flow_solve(int posVec, float* outVel, void* outFlagA,
                                float* outNormal, unsigned int* outClip, float* outSpeed);
extern double FUN_80390b44(double base, double exp);   // pow-like smoothing
extern int  FUN_8027eb3c(int actorRec);                // actor-eligible test
extern int  subobj_get_entity(unsigned int handle);
extern int  aiactor_run_behaviour_phase(int entity, int phaseId, int* frame, int flag);
extern int  sound_is_playing(unsigned int soundHandle);
extern void aiactor_bark_request_secondary(int entity);
extern double math_asin(double x);
extern int  soundpad_query_adjacent_zones_simple(int zoneId, int* outList, int max,
                                                 int mask, int flag);
extern int  projectile_spawn(double a, double b, double c, double d,
                             unsigned long long e, unsigned long long f,
                             unsigned long long g, unsigned long long h);
// first FP arg (f1) carries the retargeted-teammate handle as a double
extern void FUN_80284ba4(double a, double b, double c, double d,
                         unsigned long long e, unsigned long long f,
                         unsigned long long g, unsigned long long h, int entity);
extern int  player_ai_is_explosive_weapon(int weaponType);
extern int  player_ai_is_sniper_weapon(int weaponType);
// ---- register-pair spawn-frame helpers used by this batch ------------------
extern int             FUN_803826d4(void);   // resolve entity handle (frame var d)
extern void            FUN_80382720(void);    // epilogue d
// ---- intra-file callees (defined below) ------------------------------------
void aiactor_predict_intercept_point(int entity, int frame, float* outDistSq,
                                     float* outYaw, float* outPos);
void aiactor_melee_proximity_stub(void);
void aiactor_melee_resolve_target(void);
// ---- new raw data symbols --------------------------------------------------
extern int   DAT_805c4ff0;               // attached prop/vehicle transform base
extern float DAT_805c4ff4;               // prop-attach local offset x
extern float DAT_805c4ff8;               // prop-attach local offset y
extern float DAT_805c4ffc;               // prop-attach local offset z
extern float DAT_805c5050;               // prop-ride recoil smoothing accum x
extern float DAT_805c5054;               // prop-ride recoil smoothing accum y
extern float DAT_805c5058;               // prop-ride recoil clamp state x
extern float DAT_805c505c;               // prop-ride recoil clamp state y
extern short DAT_804d34c6;               // ride-attach enable/index
extern float DAT_803e051c;               // bone-lean rot A vec (x)
extern float DAT_803e0520;               // bone-lean rot A vec (y)
extern float DAT_803e0524;               // bone-lean rot A vec (z)
extern float DAT_803e0528;               // prop-ride pos offset (x)
extern float DAT_803e052c;               // prop-ride pos offset (y)
extern float DAT_803e0530;               // prop-ride pos offset (z)
extern float DAT_803e0534;               // prop-ride pos offset alt (x)
extern float DAT_803e0538;               // prop-ride pos offset alt (y)
extern float DAT_803e053c;               // prop-ride pos offset alt (z)
extern float FLOAT_806190c0;
extern float FLOAT_806190c4;
extern float FLOAT_806190c8;
extern float FLOAT_806190cc;
extern float FLOAT_806190d0;
extern float FLOAT_806190d4;
extern float FLOAT_806190d8;
extern float FLOAT_806190dc;
extern double DOUBLE_806190b8;           // prop-ride smoothing exponent
}

// 0x80267384  aiactor_behavior_bone_lean_tick
// Builds a ZYX rotation matrix about a pivot (two mtx4_rotate_about_point +
// mtx4_multiply), re-evaluates the model pose, then post-multiplies every bone
// whose local-height field (bone[0xd]) is below the head bone height into the
// lean matrix. Used to tilt the actor's spine during a lean/peek pose.
void aiactor_behavior_bone_lean_tick(void)
{
    int   frame;
    float* bone;
    int   boneIdx;
    int   boneOff;
    float rotA[4];
    float headHeight;
    float pivot[3];
    float rotZ[16];
    float rotY[16];

    frame = FUN_803826f0();
    rotA[0] = DAT_803e051c;
    rotA[1] = DAT_803e0520;
    rotA[2] = DAT_803e0524;
    pivot[0] = FLOAT_80618f98;
    // head-bone height threshold: model->skel->rootBone[0].height (+0x24 chain)
    headHeight = *(float*)(*(int*)(*(int*)(frame + 0x24) + 0x20) + 8);
    pivot[1] = FLOAT_80618f98;
    mtx4_rotate_about_point(rotZ, rotA, pivot);
    rotA[2] = FLOAT_80618f98;
    rotA[0] = FLOAT_80618fb8;
    mtx4_rotate_about_point(rotY, rotA, pivot);
    mtx4_multiply(rotZ, rotY, rotZ);
    model_pose_eval();

    boneOff = 0;
    for (boneIdx = 0; boneIdx < *(int*)(*(int*)(*(int*)(frame + 4) + 4) + 4); boneIdx = boneIdx + 1) {
        bone = (float*)(*(int*)(*(int*)(frame + 4) + 0x10) + boneOff);
        if (bone[0xd] < headHeight) {
            mtx4_multiply(bone, rotZ, bone);
        }
        boneOff = boneOff + 0x40;
    }
    FUN_8038273c();
    return;
}

// 0x8026749c  aiactor_behavior_prop_ride_tick
// Ride state: while attached to a prop/vehicle (DAT_805c4ff0 transform), snap
// the actor's world position to the prop mount point (+0x274 local offset),
// re-room it, and drive a camera-recoil smoothing pair (DAT_805c5050..5c) from
// the prop's angular velocity using a pow-shaped decay. state 1 = riding;
// state in [0,0) with ride enabled (DAT_804d34c6 >= 0) = mount transition.
void aiactor_behavior_prop_ride_tick(int entity)
{
    unsigned int propAngle;
    float velX, velY, thresholdHi, thresholdLo;
    double decay;
    int actorState;
    int state;
    double frameDelta;
    double gainPow;
    float ofsA[3];
    float ofsB[3];

    actorState = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();
    apply_pending_state_swap(actorState);
    advance_loco_timer(actorState);

    state = *(int*)(actorState + 0x20);
    if (state == 1) {
        if (DAT_805c4ff0 != 0) {
            ofsA[0] = DAT_803e0534;
            ofsA[1] = DAT_803e0538;
            ofsA[2] = DAT_803e053c;
            *(float*)(entity + 100) = DAT_805c4ff4;
            *(float*)(entity + 0x68) = DAT_805c4ff8;
            *(float*)(entity + 0x6c) = DAT_805c4ffc;
            mtx4_transform_vector((float*)(DAT_805c4ff0 + 0x274), ofsA);
            *(float*)(entity + 100)  = *(float*)(entity + 100)  + ofsA[0];
            *(float*)(entity + 0x68) = *(float*)(entity + 0x68) + ofsA[1];
            *(float*)(entity + 0x6c) = *(float*)(entity + 0x6c) + ofsA[2];
            *(int*)(entity + 0x38) = 0;
            *(int*)(entity + 0x34) = 0;
            *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xbffdffff;
            entity_assign_room(entity);
            if (*(int*)(entity + 0x34) == 0) {
                *(int*)(entity + 0x38) = 0xd5;
                *(int*)(entity + 0x34) = 0xd5;
            }
            thresholdHi = FLOAT_80619074;
            thresholdLo = FLOAT_80619070;
            propAngle = *(unsigned int*)(DAT_805c4ff0 + 0x70);
            *(unsigned int*)(entity + 0xc4) = propAngle;
            *(unsigned int*)(entity + 0x70) = propAngle;
            *(unsigned int*)(entity + 0xc0) = *(unsigned int*)(DAT_805c4ff0 + 0xc0);
            *(unsigned int*)(entity + 200)  = *(unsigned int*)(DAT_805c4ff0 + 200);
            frameDelta = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8;
            velX = FLOAT_80618fb8 / (float)frameDelta;
            DAT_805c5050 = -(velX * (float)((double)FLOAT_806108ec *
                            (double)(FLOAT_80618ff8 *
                                (DAT_805c5058 + (thresholdLo * -*(float*)(entity + 0xc0)) / thresholdHi))) -
                            DAT_805c5050);
            DAT_805c5054 = -(velX * (float)((double)FLOAT_806108ec *
                            (double)(FLOAT_80618ff8 *
                                (DAT_805c505c + (thresholdLo * -*(float*)(entity + 200)) / thresholdHi))) -
                            DAT_805c5054);
            decay = FUN_80390b44(DOUBLE_806190b8, (double)FLOAT_806108ec);
            DAT_805c5050 = DAT_805c5050 * (float)decay;
            decay = FUN_80390b44(DOUBLE_806190b8, (double)FLOAT_806108ec);
            DAT_805c5054 = DAT_805c5054 * (float)decay;
            frameDelta = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8;
            velY = FLOAT_80618fb8 / (float)frameDelta;
            velX = velY * DAT_805c5050 * FLOAT_806108ec + DAT_805c5058;
            gainPow = velX;
            if (FLOAT_806190c4 < velX) { gainPow = FLOAT_806190c4; }
            DAT_805c5058 = FLOAT_806190c0;
            if ((FLOAT_806190c0 <= (float)gainPow) && (DAT_805c5058 = velX, FLOAT_806190c4 < velX)) {
                DAT_805c5058 = FLOAT_806190c4;
            }
            velY = velY * DAT_805c5054 * FLOAT_806108ec + DAT_805c505c;
            velX = velY;
            if (FLOAT_806190c4 < velY) { velX = FLOAT_806190c4; }
            DAT_805c505c = FLOAT_806190c0;
            if ((FLOAT_806190c0 <= velX) && (DAT_805c505c = velY, FLOAT_806190c4 < velY)) {
                DAT_805c505c = FLOAT_806190c4;
            }
            entity_register_for_update(entity, 2, -0x7fd98c7c);
        }
    } else if (((state < 1) && (-1 < state)) && (DAT_805c4ff0 != 0) && (-1 < (int)DAT_804d34c6)) {
        ofsB[0] = DAT_803e0528;
        ofsB[1] = DAT_803e052c;
        ofsB[2] = DAT_803e0530;
        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        *(float*)(entity + 100) = DAT_805c4ff4;
        *(float*)(entity + 0x68) = DAT_805c4ff8;
        *(float*)(entity + 0x6c) = DAT_805c4ffc;
        mtx4_transform_vector((float*)(DAT_805c4ff0 + 0x274), ofsB);
        *(float*)(entity + 100)  = *(float*)(entity + 100)  + ofsB[0];
        *(float*)(entity + 0x68) = *(float*)(entity + 0x68) + ofsB[1];
        *(float*)(entity + 0x6c) = *(float*)(entity + 0x6c) + ofsB[2];
        entity_assign_room(entity);
        propAngle = *(unsigned int*)(DAT_805c4ff0 + 0x70);
        *(unsigned int*)(entity + 0xc4) = propAngle;
        *(unsigned int*)(entity + 0x70) = propAngle;
        *(unsigned int*)(entity + 0xc0) = *(unsigned int*)(DAT_805c4ff0 + 0xc0);
        *(unsigned int*)(entity + 200)  = *(unsigned int*)(DAT_805c4ff0 + 200);
        DAT_805c5058 = FLOAT_80618f98;
        DAT_805c505c = FLOAT_80618f98;
        if (*(int*)(actorState + 0x20) != 1) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = 1;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
        entity_register_for_update(entity, 2, -0x7fd98c7c);
        if (*(int*)(actorState + 0x20) != 1) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = 1;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
    }
    aiactor_update_aim_height();
    return;
}

// 0x80267990  aiactor_behavior_scripted_death_tick
// Scripted-death / one-shot clip state (clip 0xbf3). state 1: wait for the
// death clip's user-flag (clip id at actorState+0xc64) then destroy the actor.
// state [0,0): start clip 0xbf3 if not active, and once the actor's death timer
// (+0x1cc) reaches 0 latch state 1, then set playback rate on the anim ctrl.
void aiactor_behavior_scripted_death_tick(unsigned long long param_1, double param_2,
                                          double param_3, double param_4,
                                          unsigned long long param_5, unsigned long long param_6,
                                          unsigned long long param_7, unsigned long long param_8,
                                          int entity)
{
    int actorState;
    int state;
    int flags;
    double dt;

    double handle;
    actorState = *(int*)(entity + 0x7c);
    handle = (double)aiactor_retarget_visible_teammate();
    apply_pending_state_swap(actorState);
    advance_loco_timer(actorState);

    state = *(int*)(actorState + 0x20);
    if (state == 1) {
        flags = anim_primary_clip_userflags(entity, *(int*)(actorState + 0xc64));
        if (flags != 0) {
            aiactor_destroy_instance(handle, param_2, param_3, param_4,
                                     param_5, param_6, param_7, param_8, entity);
        }
    } else if ((state < 1) && (-1 < state)) {
        flags = anim_is_clip_active(entity, 0xbf3);
        if (flags == 0) {
            anim_start_clip((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        }
        if ((double)*(float*)(entity + 0x1cc) <= (double)FLOAT_80618f98) {
            anim_start_clip((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
            if (*(int*)(actorState + 0x20) != 1) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 1;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        }
        dt = (double)FLOAT_8061900c;
        *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffdffff;
        *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) & 0xfffbffff;
        anim_set_playback_rate(dt, *(int**)(entity + 4), 1);
    }
    return;
}

// 0x80267b38  aiactor_behavior_belt_ride_tick
// GHZ conveyor-belt ride FSM. state 1: solve belt flow (ghz_belt_flow_solve)
// and, when moving, offset the actor along the belt and choose a ride clip
// (0x20c walk-on, 0x20f/0x210 slide) whose speed threshold comes from the anim
// descriptor table (DAT_80611824 indexed by DAT_804d1cd0[clip]); latch state 2
// on a valid ride clip. state [0,0): play the death/fall clip (state marker 6).
// state [1,3): play stand clip 0x211 and probe the floor with the per-weapon
// spawn tables (DAT_8048ca94/9c stride 0x30 by weapon short +0x48) to set the
// landing height (+0xb70/+0xbfc). DAT_805015bd=='\n' picks the coop input mask.
void aiactor_behavior_belt_ride_tick(void)
{
    float clipThreshold;
    bool clipReady;
    int active;
    int frame;
    unsigned int inputMask;
    int actorState;
    double dt;
    float beltVel[1];      // local_48 speed accumulator sink
    unsigned int beltClip; // local_44 selected ride clip id
    float beltNormal[3];   // afStack_40
    unsigned int beltFlagA;// local_34
    float beltRot;         // local_30
    unsigned int beltFlagB;// local_2c
    float beltOfs[3];      // local_28 / local_24 / local_20
    float speedOut;        // local_48 (belt speed)

    frame = __save_gpr_28();
    actorState = *(int*)(frame + 0x7c);
    aiactor_retarget_visible_teammate();
    apply_pending_state_swap(actorState);
    advance_loco_timer(actorState);

    int state = *(int*)(actorState + 0x20);
    if (state != 1) {
        if (state < 1) {
            if (-1 < state) {
                dt = (double)FLOAT_80618f98;
                *(int*)(frame + 0x34) = 6;
                anim_play_scaled(dt, (double)FLOAT_80618fb8);
                anim_reset_blend_and_clip(*(int*)(frame + 4));
                anim_set_blend_weight((double)FLOAT_80618fb8, *(int*)(frame + 4));
                *(unsigned int*)(frame + 100)  = *(unsigned int*)(frame + 0xa8);
                *(unsigned int*)(frame + 0x68) = *(unsigned int*)(frame + 0xac);
                *(unsigned int*)(frame + 0x6c) = *(unsigned int*)(frame + 0xb0);
                *(float*)(frame + 0x1cc) = FLOAT_80618fa0;
                *(unsigned int*)(actorState + 0x2c) = *(unsigned int*)(actorState + 0x2c) | 0x40000000;
                if (*(int*)(actorState + 0x20) != 1) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(unsigned int*)(actorState + 0x20) = 1;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
                }
            }
        } else if (state < 3) {
            active = anim_is_clip_active(frame, 0x211);
            if (active == 0) {
                double frameDelta = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8;
                anim_play_scaled((double)(float)((double)FLOAT_80618fb8 / frameDelta),
                                 (double)FLOAT_80618fb8);
                *(unsigned int*)(actorState + 0x2c) = *(unsigned int*)(actorState + 0x2c) & 0xbfffffff;
                *(unsigned int*)(frame + 0x1cc) = *(unsigned int*)(frame + 0x1d8);
                int probe = *(short*)(actorState + 0x48) * 0x30;
                dt = floor_query_with_interact((double)*(float*)(&DAT_8048ca94[probe]),
                                               (double)*(float*)(&DAT_8048ca9c[probe]),
                                               frame + 100, 0, (int*)0, 0);
                *(float*)(actorState + 0xb70) = (float)dt;
                *(float*)(actorState + 0xbfc) = (float)dt;
                *(float*)(frame + 0x68) = (float)dt;
            } else {
                active = anim_secondary_clip_finished(*(int*)(frame + 4));
                if (active != 0) {
                    if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
                        if (DAT_805015bd == '\n') {
                            inputMask = *(unsigned int*)(actorState + 0x14);
                        } else {
                            inputMask = *(unsigned int*)(actorState + 0x1c);
                        }
                        if ((inputMask & 1) == 0) goto belt_done;
                    }
                    *(unsigned int*)(actorState + 0xaac) = 4;
                    *(unsigned int*)(actorState + 0xaa8) = 0;
                }
            }
        }
        goto belt_done;
    }

    clipReady = false;
    ghz_belt_flow_solve(frame + 100, beltOfs, &beltFlagA, beltNormal, &beltClip, &speedOut);
    if (FLOAT_8061905c != speedOut) {
        *(unsigned char*)(frame + 0x32) = 2;
        *(unsigned int*)(frame + 0xc0) = beltFlagA;
        *(float*)(frame + 0x70) = FLOAT_80619074 + beltRot;
        *(float*)(frame + 0xc4) = FLOAT_80619074 + beltRot;
        *(unsigned int*)(frame + 200) = beltFlagB;
        *(float*)(frame + 100)  = *(float*)(frame + 100)  + beltOfs[0];
        *(float*)(frame + 0x68) = *(float*)(frame + 0x68) + beltOfs[1];
        *(float*)(frame + 0x6c) = *(float*)(frame + 0x6c) + beltOfs[2];
        if (beltClip != 0x20c) {
            bool isSlide = (beltClip == 0x210) || (beltClip == 0x20f);
            bool takeClip = false;
            if (isSlide) {
                int desc = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[beltClip] * 4);
                clipThreshold = FLOAT_80618f98;
                if (desc != 0) clipThreshold = *(float*)(desc + 0x18);
                if (speedOut < clipThreshold * FLOAT_80619038) takeClip = true;
            } else {
                int desc = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[beltClip] * 4);
                clipThreshold = FLOAT_80618f98;
                if (desc != 0) clipThreshold = *(float*)(desc + 0x18);
                if (clipThreshold * FLOAT_80618fd0 <= speedOut) {
                    if ((beltClip == 0x210) || (beltClip == 0x20f)) {
                        int desc2 = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[beltClip] * 4);
                        float t2 = FLOAT_80618f98;
                        if (desc2 != 0) t2 = *(float*)(desc2 + 0x18);
                        if (speedOut < t2 * FLOAT_80619038) takeClip = true;
                    }
                } else {
                    active = anim_is_clip_active(frame, beltClip);
                    if (active == 0) takeClip = true;
                }
            }
            clipReady = takeClip;
        }
    }
    active = anim_is_clip_active(frame, 0x20c);
    if (active == 0) {
        *(float*)(frame + 0xc0) = FLOAT_80618f98;
        *(float*)(frame + 200)  = FLOAT_80618f98;
    }
    active = anim_is_clip_active(frame, 0x210);
    if (((active == 0) && (active = anim_is_clip_active(frame, 0x20f), active == 0)) ||
        (active = anim_secondary_clip_finished(*(int*)(frame + 4)), active == 0)) {
        int fin = anim_secondary_clip_finished(*(int*)(frame + 4));
        if ((fin != 0) || (clipReady)) {
            double frameDelta = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8;
            anim_play_scaled((double)(float)((double)FLOAT_80618fb8 / frameDelta),
                             (double)FLOAT_80618fb8);
            anim_reset_blend_and_clip(*(int*)(frame + 4));
            anim_set_blend_weight((double)FLOAT_80618fb8, *(int*)(frame + 4));
        }
    } else if (*(int*)(actorState + 0x20) != 2) {
        *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
        *(unsigned int*)(actorState + 0x20) = 2;
        *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
        *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
    }
belt_done:
    __restore_gpr_28();
    return;
}

// 0x80268018  aiactor_melee_proximity_scan
// Per-frame scan of the spawn-visible actor table (DAT_80588610, iterated
// [DAT_80612610, DAT_80612098)). For each candidate actor: reject own team
// entity, apply the flag/short filters (state flags, weapon short != 0x51,
// FUN_8027eb3c eligibility), require squared distance <= FLOAT_8061901c and a
// facing dot-test pass, then fire the melee cue (empty tail stub here).
void aiactor_melee_proximity_scan(void)
{
    float dx, dy, dz;
    unsigned int flagsHi;
    int self;
    int selfEnt;
    bool facing;
    unsigned int flagsLo;
    int candEnt;
    int idx;
    int* rec;
    int i4;

    self = FUN_803826d4();
    i4 = DAT_80612610 << 2;
    for (idx = DAT_80612610; idx < DAT_80612098; idx = idx + 1) {
        rec = *(int**)((int)&DAT_80588610 + i4);
        candEnt = *rec;
        selfEnt = subobj_get_entity(*(unsigned int*)(self + 0x1e4));
        if (DAT_805015bd == '\n') {
            flagsLo = rec[4];
            flagsHi = rec[5];
        } else {
            flagsLo = rec[6];
            flagsHi = rec[7];
        }
        if (((flagsHi & 0xc078b403) == 0 && (flagsLo & 0x27fe7ff) == 0) &&
            ((rec[9] & 8U) == 0) && (*(short*)(rec + 0x12) != 0x51)) {
            flagsHi = (unsigned int)FUN_8027eb3c((int)rec);
            if (flagsHi == 0) {
                dx = *(float*)(candEnt + 100)  - *(float*)(selfEnt + 100);
                dy = *(float*)(candEnt + 0x68) - *(float*)(selfEnt + 0x68);
                dz = *(float*)(candEnt + 0x6c) - *(float*)(selfEnt + 0x6c);
                if (dz * dz + dx * dx + dy * dy <= FLOAT_8061901c) {
                    facing = aiactor_facing_dot_test((double)FLOAT_80619054, candEnt,
                                                     (float*)(selfEnt + 100));
                    if (facing) {
                        aiactor_melee_proximity_stub();
                    }
                }
            }
        }
        i4 = i4 + 4;
    }
    FUN_80382720();
    return;
}

// 0x80268168  aiactor_melee_proximity_stub
// 4-byte tail stub (blr). The melee cue for the un-armed scan resolves to a
// no-op in this build.
void aiactor_melee_proximity_stub(void)
{
    return;
}

// 0x8026816c  aiactor_melee_proximity_scan_armed
// Weapon-type-gated variant of the proximity scan. Reads self position from the
// scratch frame (+0x1c/+0x20/+0x24), gated by level-actor-loaded fields
// (+0x104/+0x108). Adds weapon-class filters (valid/ranged/explosive/sniper)
// and accepts a candidate on a facing pass OR (aux flag set AND within the
// nearer distance FLOAT_80619010). Fires aiactor_melee_resolve_target.
void aiactor_melee_proximity_scan_armed(void)
{
    float dx, dy, dz;
    unsigned int flagsHi;
    int self;
    int result;
    bool facing;
    unsigned int flagsLo;
    int candEnt;
    int idx;
    int* rec;
    int i4;
    double distSq;
    int auxFlag;

    self = FUN_803826d4();
    auxFlag = self;   // low half of the register pair (facing-relaxed flag)
    if (((*(int*)(self + 0x104) == 0) || (*(int*)(*(int*)(self + 0x104) + 8) < DAT_80612610)) &&
        ((*(int*)(self + 0x108) == 0) || (*(int*)(*(int*)(self + 0x108) + 0x150) != 0))) {
        i4 = DAT_80612610 << 2;
        for (idx = DAT_80612610; idx < DAT_80612098; idx = idx + 1) {
            rec = *(int**)((int)&DAT_80588610 + i4);
            candEnt = *rec;
            if (DAT_805015bd == '\n') {
                flagsLo = rec[4];
                flagsHi = rec[5];
            } else {
                flagsLo = rec[6];
                flagsHi = rec[7];
            }
            if ((((flagsHi & 0xc078b403) == 0 && (flagsLo & 0x27fe7ff) == 0) &&
                 ((rec[9] & 8U) == 0) && (*(short*)(rec + 0x12) != 0x51)) &&
                ((flagsHi = (unsigned int)FUN_8027eb3c((int)rec), flagsHi == 0) &&
                 (result = player_ai_is_valid_weapon_type((int)*(short*)(rec + 0x12)), result == 0))) {
                if ((((result = player_ai_is_ranged_weapon((int)*(short*)(rec + 0x12)), result == 0) &&
                      ((result = player_ai_is_explosive_weapon((int)*(short*)(rec + 0x12)), result == 0) &&
                       (result = player_ai_is_sniper_weapon((int)*(short*)(rec + 0x12)), result == 0))) &&
                     (dx = *(float*)(candEnt + 100)  - *(float*)(self + 0x1c),
                      dy = *(float*)(candEnt + 0x68) - *(float*)(self + 0x20),
                      dz = *(float*)(candEnt + 0x6c) - *(float*)(self + 0x24),
                      distSq = (double)(dz * dz + dx * dx + dy * dy),
                      distSq <= (double)FLOAT_8061901c)) &&
                    ((facing = aiactor_facing_dot_test((double)FLOAT_80619054, candEnt,
                                                       (float*)(self + 0x1c)), facing) ||
                     ((auxFlag != 0) && (distSq <= (double)FLOAT_80619010)))) {
                    aiactor_melee_resolve_target();
                }
            }
            i4 = i4 + 4;
        }
    }
    FUN_80382720();
    return;
}

// 0x80268350  aiactor_melee_resolve_target
// Runs the melee behaviour phase (0x3ec); if it doesn't take over, predicts the
// intercept point (aiactor_predict_intercept_point) and, when the target is out
// of the melee radius (dist > (state==0xe ? FLOAT_8061904c : FLOAT_80618ff8)^2),
// records the pursue target at +0xb60/+0xb64; otherwise queues a melee stance
// request (+0xaec target, +0xaac/+0xaa8 request pair) gated on the input bit.
// Finally requests a secondary bark if no melee sound is playing.
void aiactor_melee_resolve_target(void)
{
    int actor;
    int taken;
    unsigned int inputMask;
    int* target;
    int actorState;
    double radius;
    float distSq[8];

    actor = FUN_803826f0();
    actorState = *(int*)(actor + 0x7c);
    // target record returned in the low half of the frame pair
    target = (int*)actor;
    taken = aiactor_run_behaviour_phase(actor, 0x3ec, target, 0);
    if (taken != 0) goto resolve_done;

    radius = (double)FLOAT_80618ff8;
    aiactor_predict_intercept_point(actor, (int)target, distSq, (float*)0, (float*)0);
    if (*target == 0xe) {
        radius = (double)FLOAT_8061904c;
    }
    if ((float)(radius * radius) <= distSq[0]) {
        *(int*)(actorState + 0xb60) = DAT_80611928;
        *(int*)(actorState + 0xb64) = *target;
    } else {
        *(int**)(actorState + 0xaec) = target;
        if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
            if (DAT_805015bd == '\n') {
                inputMask = *(unsigned int*)(actorState + 0x14);
            } else {
                inputMask = *(unsigned int*)(actorState + 0x1c);
            }
            if ((inputMask & 1) == 0) goto resolve_bark;
        }
        *(unsigned int*)(actorState + 0xaac) = 0x80000000;
        *(unsigned int*)(actorState + 0xaa8) = 0;
    }
resolve_bark:
    taken = sound_is_playing(*(unsigned int*)(actorState + 0xe90));
    if (taken == 0) {
        aiactor_bark_request_secondary(actor);
    }
resolve_done:
    FUN_8038273c();
    return;
}

// 0x80268494  aiactor_predict_intercept_point
// Ballistic lead solve. Given the target record, computes a quadratic-style
// intercept time from the relative height and the gravity/step constants, then
// projects the target's future position. Optionally returns squared ground
// distance (outDistSq), wrapped bearing to the point (outYaw) and the world
// point (outPos). Used by the melee resolver to decide reach/facing.
void aiactor_predict_intercept_point(int entity, int frame, float* outDistSq,
                                     float* outYaw, float* outPos)
{
    float dxp, dzp;
    int recType;
    int* rec;
    double t0, disc, tHit, root;
    double relY, gravity;
    float future[3];

    (void)entity;
    rec = (int*)FUN_803826e8();
    recType = frame;   // high half of the returned frame pair (env record ptr)
    if ((rec != (int*)0) && ((outDistSq != (float*)0) || (outYaw != (float*)0))) {
        gravity = (double)FLOAT_806190c8;
        if (*rec == 0xc) {
            gravity = (double)(FLOAT_80618ff8 * (float)rec[0x7f] + FLOAT_806190c8);
        }
        double frameDelta = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8;
        t0 = (double)((-(float)rec[0x1a] * (float)frameDelta) / (float)gravity);
        disc = (double)((FLOAT_806190cc *
                ((float)(t0 * (double)((float)rec[0x1a] * (float)frameDelta) +
                         (double)((float)gravity * (float)(t0 * t0) * FLOAT_80618fd0)) +
                 ((float)rec[8] - *(float*)(recType + 0x68)))) / (float)gravity);
        relY = disc;
        if ((double)FLOAT_8061905c < disc) { relY = (double)FLOAT_8061905c; }
        if ((double)FLOAT_80618f98 <= relY) {
            if ((double)FLOAT_8061905c < disc) { disc = (double)FLOAT_8061905c; }
        } else {
            disc = (double)FLOAT_80618f98;
        }
        root = math_sqrt(disc);
        double frameDelta2 = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8;
        tHit = (double)((float)(t0 + root) * (float)frameDelta2);
        future[0] = (float)rec[0x19] * (float)tHit + (float)rec[7];
        future[1] = FLOAT_80618f98 * (float)tHit + (float)rec[8];
        future[2] = (float)rec[0x1b] * (float)tHit + (float)rec[9];
        if (outDistSq != (float*)0) {
            dxp = *(float*)(recType + 100) - future[0];
            dzp = *(float*)(recType + 0x6c) - future[2];
            *outDistSq = dxp * dxp + dzp * dzp;
        }
        if (outYaw != (float*)0) {
            double yaw = prop_hinge_wrap_angle_to_range((double)*(float*)(recType + 0x70),
                                                        (float*)(recType + 100), future);
            *outYaw = (float)yaw;
        }
        if (outPos != (float*)0) {
            outPos[0] = future[0];
            outPos[1] = future[1];
            outPos[2] = future[2];
        }
    }
    FUN_80382734();
    return;
}

// 0x80268698  aiactor_behavior_mounted_gun_tick
// Mounted-gun / turret behaviour FSM (states 0,1,2,5,6,7). state 0/1: play the
// mount idle/aim clip 0x640, and on the aim clip's user-flag run the zone lead
// solve (soundpad_query_adjacent_zones_simple over up to 0x14 adjacent zones),
// picking the best-aligned target zone via three asin cone tests; latch state 2
// once a firing bearing is within the cone (FLOAT_806190d4/d8/dc). state 2:
// fire-clip user-flag gate -> dismount/death (states 5/6). state 6: on clip
// 0x63f spawn the projectile then latch state 7. state 7: hand off to
// FUN_80284ba4 (post-fire cleanup).
void aiactor_behavior_mounted_gun_tick(unsigned long long param_1, double param_2,
                                       double param_3, double param_4,
                                       unsigned long long param_5, unsigned long long param_6,
                                       unsigned long long param_7, unsigned long long param_8)
{
    float fVar1, fVar2;
    bool foundA, foundB, foundC, moving;
    int active;
    int* zone;
    int self;
    unsigned int fireFlags;
    int zoneCount, zoneIdx, i4, bestZone;
    double dVar14, dVar15, dVar16, dVar18, dVar19, dVar20;
    double dVar21, dVar22, dVar23, dVar24, dVar25, dVar26, dVar27, dVar28, dVar29;
    double v1x, v1y, v1z;
    int actorState;
    float bearing[3];
    float bearingYaw;
    int zoneList[21];
    double dt;

    self = FUN_803826d4();
    actorState = *(int*)(self + 0x7c);
    dVar16 = (double)aiactor_retarget_visible_teammate();
    apply_pending_state_swap(actorState);
    advance_loco_timer(actorState);

    switch (*(int*)(actorState + 0x20)) {
    case 0: {
        double frameDelta = (double)CONCAT44(0x43300000, *(unsigned int*)(actorState + 8) ^ 0x80000000) - DOUBLE_80618fc8;
        anim_play_scaled((double)FLOAT_80618ff0,
                         (double)(FLOAT_806190a4 * (float)frameDelta + FLOAT_80619030));
        *(float*)(actorState + 0xb30) = FLOAT_80618f98;
        if (*(int*)(actorState + 0x20) != 1) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(unsigned int*)(actorState + 0x20) = 1;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
        }
        break;
    }
    case 1:
        active = anim_is_clip_active(self, 0x640);
        if (active == 0) {
            double frameDelta = (double)CONCAT44(0x43300000, *(unsigned int*)(actorState + 8) ^ 0x80000000) - DOUBLE_80618fc8;
            anim_play_scaled((double)FLOAT_80618ff0,
                             (double)(FLOAT_806190a4 * (float)frameDelta + FLOAT_80619030));
        } else {
            active = anim_primary_clip_userflags(self, 0x640);
            if (active != 0) {
                bestZone = -1;
                foundB = false;
                moving = false;
                foundC = false;
                aiactor_leader_zone_update(self);
                aiactor_predict_intercept_point(self, *(int*)(actorState + 0xaec),
                                                &bearing[1], &bearingYaw, bearing);
                int tgt = *(int*)(actorState + 0xaec);
                dVar24 = (double)*(float*)(tgt + 0x68);
                dVar16 = (double)*(float*)(tgt + 100);
                dVar26 = (double)*(float*)(tgt + 0x6c);
                fVar1 = (float)(dVar26 * dVar26 +
                        (double)(float)(dVar16 * dVar16 + (double)(float)(dVar24 * dVar24)));
                if (FLOAT_80619080 < fVar1) {
                    fVar2 = 1.0f / (float)math_sqrt((double)fVar1);
                    fVar2 = fVar2 * -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040);
                    dVar14 = (double)(FLOAT_80618fb8 * fVar2 *
                             -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040));
                    dVar16 = (double)(float)(dVar16 * dVar14);
                    dVar24 = (double)(float)(dVar24 * dVar14);
                    dVar26 = (double)(float)(dVar26 * dVar14);
                }
                dVar29 = -dVar26;
                dVar28 = (double)FLOAT_80618f98;
                fVar1 = (float)(dVar16 * dVar16 + (double)(float)(dVar29 * dVar29 + dVar28));
                dVar14 = dVar16;
                if (FLOAT_80619080 < fVar1) {
                    fVar2 = 1.0f / (float)math_sqrt((double)fVar1);
                    fVar2 = fVar2 * -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040);
                    dVar14 = (double)(FLOAT_80618fb8 * fVar2 *
                             -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040));
                    dVar29 = (double)(float)(dVar29 * dVar14);
                    dVar28 = (double)(float)(dVar28 * dVar14);
                    dVar14 = (double)(float)(dVar16 * dVar14);
                }
                v1x = (double)(*(float*)(self + 0x68) - *(float*)(tgt + 0x20));
                v1y = (double)(*(float*)(self + 100)  - *(float*)(tgt + 0x1c));
                v1z = (double)(*(float*)(self + 0x6c) - *(float*)(tgt + 0x24));
                fVar1 = (float)(v1z * v1z + (double)(float)(v1y * v1y + (double)(float)(v1x * v1x)));
                if (FLOAT_80619080 < fVar1) {
                    fVar2 = 1.0f / (float)math_sqrt((double)fVar1);
                    fVar2 = fVar2 * -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040);
                    dVar15 = (double)(FLOAT_80618fb8 * fVar2 *
                             -(FLOAT_80618fd0 * fVar1 * fVar2 * fVar2 - FLOAT_80619040));
                    v1y = (double)(float)(v1y * dVar15);
                    v1x = (double)(float)(v1x * dVar15);
                    v1z = (double)(float)(v1z * dVar15);
                }
                dVar20 = (double)(float)(dVar14 * v1z +
                         (double)(float)(dVar29 * v1y + (double)(float)(dVar28 * v1x)));
                zoneCount = soundpad_query_adjacent_zones_simple(*(int*)(actorState + 0x678),
                                                                 zoneList, 0x14, 0x1c0020, 0);
                dVar18 = (double)FLOAT_80618fc0;
                dVar19 = (double)FLOAT_80619080;
                dVar27 = (double)(float)(dVar18 * dVar16);
                double halfPi = (double)FLOAT_80618fb8;
                dVar15 = (double)(float)(dVar18 * dVar24);
                double coneLimit = (double)FLOAT_806190d0;
                dVar26 = (double)(float)(dVar18 * dVar26);
                zoneIdx = 0;
                i4 = 0;
                while ((zoneIdx < zoneCount) && (!foundB)) {
                    zone = soundpad_resolve_zone_node(*(int*)((int)zoneList + i4));
                    dVar22 = (double)((float)zone[4] - *(float*)(self + 0x68));
                    dVar23 = (double)((float)zone[3] - *(float*)(self + 100));
                    dVar21 = (double)((float)zone[5] - *(float*)(self + 0x6c));
                    dVar25 = (double)(float)(dVar21 * dVar21 +
                             (double)(float)(dVar23 * dVar23 + (double)(float)(dVar22 * dVar22)));
                    if (dVar19 < dVar25) {
                        double inv = 1.0 / (double)(float)math_sqrt(dVar25);
                        fVar1 = (float)(inv * -(double)((float)((double)FLOAT_80618fd0 * dVar25) *
                                (float)(inv * inv) - FLOAT_80619040));
                        dVar25 = (double)(FLOAT_80618fb8 * fVar1 *
                                 -((float)((double)FLOAT_80618fd0 * dVar25) * fVar1 * fVar1 - FLOAT_80619040));
                        dVar23 = (double)(float)(dVar23 * dVar25);
                        dVar22 = (double)(float)(dVar22 * dVar25);
                        dVar21 = (double)(float)(dVar21 * dVar25);
                    }
                    unsigned int sgn = (unsigned int)(float)dVar20 & 0x80000000 | 0x3f800000;
                    double sgnF = (double)(float)sgn;
                    double dot = (double)(float)((double)(float)(dVar14 * sgnF) * dVar21 +
                            (double)(float)((double)(float)(dVar29 * sgnF) * dVar23 +
                            (double)(float)((double)(float)(dVar28 * sgnF) * dVar22)));
                    double clamped = dot;
                    if (halfPi < dot) { clamped = halfPi; }
                    if (dVar18 <= clamped) {
                        if ((double)FLOAT_80618fb8 < dot) { dot = (double)FLOAT_80618fb8; }
                    } else {
                        dot = (double)FLOAT_80618fc0;
                    }
                    double ang = math_asin(dot);
                    if (coneLimit <= (ang < 0 ? -ang : ang)) {
                        if (!moving) {
                            double dot2 = (double)(float)(dVar26 * dVar21 +
                                    (double)(float)(dVar27 * dVar23 + (double)(float)(dVar15 * dVar22)));
                            double c2 = dot2;
                            if ((double)FLOAT_80618fb8 < dot2) { c2 = (double)FLOAT_80618fb8; }
                            if ((double)FLOAT_80618fc0 <= c2) {
                                if ((double)FLOAT_80618fb8 < dot2) { dot2 = (double)FLOAT_80618fb8; }
                            } else {
                                dot2 = (double)FLOAT_80618fc0;
                            }
                            double ang2 = math_asin(dot2);
                            if ((double)FLOAT_806190d0 <= (ang2 < 0 ? -ang2 : ang2)) {
                                if (!foundC) {
                                    double lim = (double)FLOAT_80618fc0;
                                    unsigned int sgn3 = (unsigned int)(float)dVar20 & 0x80000000 | 0x3f800000;
                                    double sgnF3 = (double)(float)sgn3;
                                    double dot3 = (double)(float)(
                                        (double)(float)(dVar14 * (double)(float)(lim * sgnF3)) * dVar21 +
                                        (double)(float)((double)(float)(dVar29 * (double)(float)(lim * sgnF3)) * dVar23 +
                                        (double)(float)((double)(float)(dVar28 * (double)(float)(lim * sgnF3)) * dVar22)));
                                    double c3 = dot3;
                                    if ((double)FLOAT_80618fb8 < dot3) { c3 = (double)FLOAT_80618fb8; }
                                    if (lim <= c3) {
                                        if ((double)FLOAT_80618fb8 < dot3) { dot3 = (double)FLOAT_80618fb8; }
                                    } else {
                                        dot3 = (double)FLOAT_80618fc0;
                                    }
                                    double ang3 = math_asin(dot3);
                                    if ((ang3 < 0 ? -ang3 : ang3) < (double)FLOAT_806190d0) {
                                        bestZone = *(int*)((int)zoneList + i4);
                                        foundC = true;
                                    }
                                }
                            } else {
                                bestZone = *(int*)((int)zoneList + i4);
                                moving = true;
                            }
                        }
                    } else {
                        bestZone = *(int*)((int)zoneList + i4);
                        foundB = true;
                    }
                    zoneIdx = zoneIdx + 1;
                    i4 = i4 + 4;
                }
                if (bestZone < 0) {
                    int tgt2 = *(int*)(actorState + 0xaec);
                    bearing[0] = *(float*)(self + 100)  - *(float*)(tgt2 + 0x1c);
                    bearingYaw = *(float*)(self + 0x68) - *(float*)(tgt2 + 0x20);
                    // note: third component packed after bearingYaw in the frame
                    dir_to_angles(&bearing[0], 0, &bearing[1]);
                } else {
                    zone = soundpad_resolve_zone_node(bestZone);
                    double yaw = prop_hinge_wrap_angle_to_range((double)*(float*)(self + 0x70),
                                                                (float*)(self + 100), (float*)(zone + 3));
                    bearing[1] = (float)yaw;
                }
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                if (*(int*)(actorState + 0x20) != 2) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(unsigned int*)(actorState + 0x20) = 2;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
                }
            }
        }
        break;
    case 2:
        active = anim_primary_clip_userflags(self, 0x63a);
        if (((active == 0) && (active = anim_primary_clip_userflags(self, 0x63b), active == 0)) &&
            ((active = anim_primary_clip_userflags(self, 0x63d), active == 0) &&
             (active = anim_primary_clip_userflags(self, 0x63c), active == 0))) {
            active = anim_primary_clip_userflags(self, 0x63e);
            if (active != 0) {
                dVar16 = (double)FLOAT_80618f98;
                if (dVar16 < (double)*(float*)(self + 0x1cc)) {
                    if ((*(int*)(actorState + 4) == 0) ||
                        (!aiactor_facing_dot_test(dVar16, self, (float*)(*(int*)(actorState + 4) + 100)))) {
                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                    } else {
                        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                    }
                    if (*(int*)(actorState + 0x20) != 5) {
                        *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                        *(unsigned int*)(actorState + 0x20) = 5;
                        *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
                    }
                } else {
                    anim_play_scaled(dVar16, (double)FLOAT_80618fb8);
                    if (*(int*)(actorState + 0x20) != 6) {
                        *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                        *(unsigned int*)(actorState + 0x20) = 6;
                        *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                        *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
                    }
                }
            }
        } else {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        }
        break;
    case 5:
        active = anim_active_slot_userflags(*(int*)(self + 4));
        if (active != 0) {
            if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
                if (DAT_805015bd == '\n') {
                    fireFlags = *(unsigned int*)(actorState + 0x14);
                } else {
                    fireFlags = *(unsigned int*)(actorState + 0x1c);
                }
                if ((fireFlags & 1) == 0) break;
            }
            *(unsigned int*)(actorState + 0xaac) = 4;
            *(unsigned int*)(actorState + 0xaa8) = 0;
        }
        break;
    case 6:
        active = anim_primary_clip_userflags(self, 0x63f);
        if (active != 0) {
            projectile_spawn(dVar16, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
            if (*(int*)(actorState + 0x20) != 7) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(unsigned int*)(actorState + 0x20) = 7;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(unsigned int*)(actorState + 0x9d0) = *(unsigned int*)(actorState + 0x20);
            }
        }
        break;
    case 7:
        FUN_80284ba4(dVar16, param_2, param_3, param_4, param_5, param_6, param_7, param_8, self);
    }
    FUN_80382720();
    return;
}


// ============================================================================
// Address range: 0x80269148 - 0x8026a41f | batch_743 | 2026-09-09
//
// Continuation of the AI-actor per-behaviour-state tick cluster (batches
// 739-742), covering the mounted-weapon / vehicle-ride sub-behaviours: the
// mounted-gun combat FSM, its fire routine (aim solve + projectile spawn), the
// turret/mount pose+aim matrix build, the yaw slew helpers, and the mount
// death/kill-credit dispatch.
//
// Shared prologue/epilogue (compiler float-GPR save/restore helpers) resolve
// the actor scratch frame and return the entity handle in a register pair:
//   FUN_803826f0 -> entity handle (hi word of the returned pair is the entity,
//   lo word the current behaviour-state id when read via the *_pair variant).
//   FUN_8038273c is the matching epilogue.
//
// Actor state block (actorState = *(entity + 0x7c)) fields touched here:
//   +0x20 behaviour-state id                +0x28 status flags word
//   +0x2c capability flags (bit0x80 = locomotion-timer active)
//   +0x9d0/+0x9d4/+0x9d8 pending-state swap trio
//   +0x9dc mount-yaw accumulator (fire)     +0x9e0 mount-yaw target accumulator
//   +0xa80 mount-pitch accumulator          +0xa90 locomotion timer
//   +0xaa8/+0xaac stance-request pair        +0xab8/+0xabc queued-action pair
//   +0xbf0 char-def block (+0x68 seat/anim index; -2 == no mount)
//   +0xbb4/+0xbb8/+0xbbc partner world xyz  +0xaa0 seat-anim slot index
//   +0x684 stored target zone id
//
// Entity fields: +0x04 anim controller, +0x64/+0x68/+0x6c world xyz,
//   +0x70 facing yaw, +0x108 previous facing, +0xf8/+0x100 velocity xz,
//   +0x1a8/+0x1ac/+0x1b0 muzzle offset, +0x274 orientation matrix row.
//
// Functions:
//   aiactor_behavior_reaction_gate     0x80269148  reaction-anim gate (ret -1 = handled)
//   aiactor_behavior_vehicle_state_tick 0x80269204 vehicle-ride FSM + movement dispatch
//   aiactor_behavior_ride_idle_tick    0x802693fc  ride idle: retarget + yaw accum + idle anim
//   aiactor_ride_path_arrival_tick     0x802694f8  ride zone-path arrival check
//   aiactor_mounted_gun_combat_tick    0x802696a4  mounted-gun combat anim/sound state dispatch
//   aiactor_mounted_gun_fire           0x80269a74  mounted-gun fire: aim solve + projectile spawn
//   aiactor_mount_pose_aim_update      0x8026a050  mount pose eval + barrel aim clamp + matrix
//   aiactor_mount_yaw_step             0x8026a234  angular step-toward for mount yaw
//   aiactor_mount_target_yaw_delta     0x8026a2f8  compute yaw delta to target (atan2 vs euler)
//   aiactor_mount_death_dispatch       0x8026a388  mount death/alert kill-credit dispatch
// ============================================================================

extern "C" {
// --- new per-file externs for batch_743 ---
// (helpers already covered by generated_decls.h / generated_externs.h are NOT
//  re-declared here; call sites match the generated signatures.)
extern int   DAT_804d3118;               // active seat-anim descriptor index
extern float FLOAT_806190e0;             // mount yaw slew gain
extern float FLOAT_806118ec;             // frame-scaled slew constant
extern float DAT_803e0540;               // muzzle-forward basis x
extern float DAT_803e0544;               // muzzle-forward basis y
extern float DAT_803e0548;               // muzzle-forward basis z
// FUN_802711e8 defined below in batch_746 (aiactor_mount_command_advance)
// aiactor_mount_next_command_opcode defined below in batch_747 (aiactor_mount_next_command_opcode)
extern double math_atan2(double y, double x);
extern double anim_primary_clip_phase(int ctrl);
extern double anim_sample_curve_value(double v, int desc);
extern int   session_get_state_flag(void);
extern int   scene_ray_portal_intersect(double az, double ay, int* fromXyz, float* dir,
                                        int room, int a, int b, float* c, float* d);
extern void  mtx4_transform_point(float* mtx, float* pt);
extern void  mtx4_to_euler(int mtx, int a, float* outEuler, float* b);
extern void  worm_stat_on_kill(int* killerState, int* victimState);
extern void  aiactor_alert_group_arm(int alertKey);
extern void  particle_spawn_directed(int a, int type, unsigned int flags, float* pos, int* d);
extern void  player_ai_spawn_trail_particle(int a, int b, int c, float* pos, float* dir, int d);
extern void  player_ai_projectile_spawn_simple(double speed, double grav, int type, float* pos,
                                               int* dir, int entity, int* actorState, int a,
                                               int b, int room, int projId, int c);
extern void  FUN_8026a5c8(void);                 // mount pose commit helper
extern void  FUN_8026a568(int entity);           // mount state-7 handler
extern void  FUN_8026a6dc(long long a, double b, double c, double d, double e,
                          long long f, long long g, long long h, int entity, int i,
                          int j, int k, int l, int m, int n, int o);
}

// 0x80269148 : reaction-anim gate. Returns -1 when the reaction/lean state is
// still "owned" by this handler (so the caller should not re-dispatch), 0
// otherwise. Keyed on the passed sub-state and the actor's current clip id.
int aiactor_behavior_reaction_gate(int entity, int subState) {
    int result = 0;
    if (subState == 3) {
        return result;
    }
    if (subState < 3) {
        if (subState == 1) {
            result = -1;
        } else if (subState > 0 && *(int*)(*(int*)(entity + 4) + 0xcc) == 0x63e) {
            result = -1;
        }
    } else if (subState < 5 && *(int*)(*(int*)(entity + 4) + 0xcc) == 0x641) {
        double phase = anim_primary_clip_phase(*(int*)(entity + 4));
        if (phase < (double)FLOAT_80618fd0) {
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
            result = -1;
        }
    }
    return result;
}

// 0x80269204 : vehicle-ride behaviour-state FSM. Applies the queued state swap,
// advances the locomotion timer, then dispatches by state id (1 = dismount
// countdown, 0/negative = play ride idle, 2 = re-arm stance request) before
// forwarding to the shared movement tick.
void aiactor_behavior_vehicle_state_tick(long long frame, double p2, double p3, double p4,
                                         double p5, double p6, double p7, long long p8,
                                         int entity) {
    int actorState = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = *(int*)(actorState + 0x9d0);
        }
        *(int*)(actorState + 0x9d0) = -1;
    }

    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        float delta = FLOAT_80618fb8 /
            (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8);
        *(float*)(actorState + 0xa90) = (float)((double)FLOAT_806108ec * (double)delta
                                                + (double)*(float*)(actorState + 0xa90));
    }

    int state = *(int*)(actorState + 0x20);
    if (state == 1) {
        float countdown = *(float*)(entity + 0x2dc);
        if ((double)countdown <= (double)FLOAT_80618f98) {
            *(int*)(actorState + 0x9d4) = 1;
            *(int*)(actorState + 0x20) = 2;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
        }
        aiactor_movement_tick((long long)(double)countdown, p2, p3, p4, p5, p6, p7, p8);
        return;
    }
    if (state < 1) {
        if (state < 0) {
            return;
        }
        long long anim = anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
        aiactor_movement_tick(anim, (double)FLOAT_80618fb8, p3, p4, p5, p6, p7, p8);
        return;
    }
    if (state > 2) {
        return;
    }
    if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
        unsigned int mask;
        if (DAT_805015bd == '\n') {
            mask = *(unsigned int*)(actorState + 0x14);
        } else {
            mask = *(unsigned int*)(actorState + 0x1c);
        }
        if ((mask & 1) == 0) {
            aiactor_movement_tick(frame, p2, p3, p4, p5, p6, p7, p8);
            return;
        }
    }
    *(int*)(actorState + 0xaac) = *(int*)(actorState + 0xabc);
    *(int*)(actorState + 0xaa8) = *(int*)(actorState + 0xab8);
    aiactor_movement_tick(frame, p2, p3, p4, p5, p6, p7, p8);
}

// 0x802693fc : lightweight ride-idle tick. Applies queued swap, advances the
// locomotion timer, and starts the ride idle clip when nothing is playing.
void aiactor_behavior_ride_idle_tick(int entity) {
    int actorState = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = *(int*)(actorState + 0x9d0);
        }
        *(int*)(actorState + 0x9d0) = -1;
    }

    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        float delta = FLOAT_80618fb8 /
            (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8);
        *(float*)(actorState + 0xa90) = FLOAT_806108ec * delta + *(float*)(actorState + 0xa90);
    }

    if (anim_is_clip_active(entity, 0x25) == 0 && *(short*)(entity + 0x94) != 0x5ea) {
        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
    }
}

// 0x802694f8 : ride zone-path arrival check. When the mount seat anim is in
// state 1, samples the start/end keyframes of the seat clip and compares the
// actor's distance to the stored target zone; on arrival it latches the target
// zone as current and advances the path, otherwise continues following it.
void aiactor_ride_path_arrival_tick(void) {
    long long framePair = (long long)__save_gpr_28();
    int entity = (int)((unsigned long long)framePair >> 0x20);
    int frame = (int)framePair;
    int actorState = *(int*)(entity + 0x7c);

    if (*(int*)(*(int*)(actorState + 0xbf0) + 0x68) != -2) {
        int seatState = *(int*)(frame + *(int*)(actorState + 0xaa0) * 0xc);
        if (seatState == 1) {
            float startKf[2];
            float startKf3;
            float endKf[2];
            float endKf3;
            anim_sample_keyframe(*(int*)(DAT_80611824 + DAT_804d3118 * 4), 0, 0, startKf);
            int desc = *(int*)(DAT_80611824 + DAT_804d3118 * 4);
            anim_sample_keyframe(desc, 0, *(int*)(desc + 0x10) - 1, endKf);
            (void)startKf; (void)endKf;
            int zoneRec = DAT_80611b8c + *(int*)(actorState + 0x684) * 0x20;
            float dx = *(float*)(entity + 100) - *(float*)(zoneRec + 0xc);
            float dz = *(float*)(entity + 0x6c) - *(float*)(zoneRec + 0x14);
            float startY = ((float*)startKf)[2];
            float endY = ((float*)endKf)[2];
            if ((startY - endY) * (startY - endY) <= dx * dx + dz * dz) {
                aiactor_follow_zone_path();
            } else {
                *(int*)(actorState + 0x678) = *(int*)(actorState + 0x684);
                aiactor_mount_command_advance();
            }
        } else if (seatState < 1 && -1 < seatState) {
            aiactor_mount_command_advance();
        } else {
            if (anim_is_clip_active(entity, 0xa25) == 0) {
                *(int*)(actorState + 0x30) = *(int*)(entity + 100);
                *(int*)(actorState + 0x34) = *(int*)(entity + 0x68);
                *(int*)(actorState + 0x38) = *(int*)(entity + 0x6c);
            } else {
                int zoneRec = DAT_80611b8c + *(int*)(actorState + 0x684) * 0x20;
                *(int*)(actorState + 0x30) = *(int*)(zoneRec + 0xc);
                *(int*)(actorState + 0x34) = *(int*)(zoneRec + 0x10);
                *(int*)(actorState + 0x38) = *(int*)(zoneRec + 0x14);
            }
        }
    }
    __restore_gpr_28();
}

// 0x802696a4 : mounted-gun combat FSM. Reads the seat-anim sub-state, and per
// state drives the idle/aim/fire clips, plays reload/cock one-shot sounds keyed
// on the primary clip's user flags, and forwards to the fire routine when the
// sub-state resolves to the fire branch.
void aiactor_mounted_gun_combat_tick(long long p1, long long p2, double p3, double p4, double p5,
                                     long long p6, long long p7, long long p8, int p9, int p10,
                                     int p11, int p12, int p13, int p14, int p15, int p16) {
    long long framePair = FUN_803826f0();
    int entity = (int)((unsigned long long)framePair >> 0x20);
    int frame = (int)framePair;
    int actorState = *(int*)(entity + 0x7c);

    int state;
    if (*(int*)(*(int*)(actorState + 0xbf0) + 0x68) == -2) {
        state = 4;
    } else {
        state = *(int*)(frame + *(int*)(actorState + 0xaa0) * 0xc);
    }

    if (state == 4) {
        if (anim_is_clip_active(entity, 0xa2a) == 0) {
            anim_play_scaled((double)FLOAT_8061903c, (double)FLOAT_80618fb8);
        }
    } else if (state < 4) {
        if (state == 1) {
            if (anim_is_clip_active(entity, 0xa25) == 0) {
                if (anim_is_clip_active(entity, 0xa23) == 0) {
                    anim_play_scaled((double)FLOAT_8061903c, (double)FLOAT_80618fb8);
                }
            } else if (anim_active_slot_userflags(*(int*)(entity + 4)) != 0) {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
            }
        }
    } else if (state < 8) {
        if (anim_is_clip_active(entity, 0xa24) == 0 &&
            anim_is_clip_active(entity, 0xa25) == 0 &&
            anim_is_clip_active(entity, 0xa26) == 0 &&
            anim_is_clip_active(entity, 0xa27) == 0 &&
            anim_is_clip_active(entity, 0xa28) == 0) {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        } else if (anim_primary_clip_userflags(entity, 0xa24) == 0) {
            if (anim_primary_clip_userflags(entity, 0xa27) != 0 ||
                anim_primary_clip_userflags(entity, 0xa26) != 0 ||
                anim_primary_clip_userflags(entity, 0xa28) != 0) {
                int subState = state;
                if (anim_is_clip_active(entity, 0xa24) == 0) {
                    subState = aiactor_mount_next_command_opcode(entity, frame);
                    aiactor_mount_command_advance();
                }
                if (subState == 5) {
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                } else if (subState < 5) {
                    if (subState == 1) {
                        long long anim = anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                        FUN_8026a6dc(anim, (double)FLOAT_80618fb8, p3, p4, p5, p6, p7, p8,
                                     entity, 0, p11, p12, p13, p14, p15, p16);
                    }
                } else if (subState == 7) {
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                } else if (subState < 7) {
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                }
            } else if (state == 6) {
                int soundParams[7];
                unsigned int soundNamePad;
                unsigned int sx, sy, sz;
                unsigned char soundFlag;
                float soundVol;
                sound_init_play_params((int)soundParams);
                soundFlag = 1;
                soundParams[0] = 0x35b;
                soundVol = FLOAT_806190a8;
                sx = *(unsigned int*)(entity + 100);
                sy = *(unsigned int*)(entity + 0x68);
                sz = *(unsigned int*)(entity + 0x6c);
                (void)soundNamePad; (void)soundFlag; (void)soundVol; (void)sx; (void)sy; (void)sz;
                sound_play_request(soundParams);
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
            } else if (state < 6) {
                if (state > 4) {
                    int soundParams[7];
                    unsigned char soundFlag;
                    float soundVol;
                    unsigned int sx, sy, sz;
                    sound_init_play_params((int)soundParams);
                    soundFlag = 1;
                    soundParams[0] = 0x35c;
                    soundVol = FLOAT_806190a8;
                    sx = *(unsigned int*)(entity + 100);
                    sy = *(unsigned int*)(entity + 0x68);
                    sz = *(unsigned int*)(entity + 0x6c);
                    (void)soundFlag; (void)soundVol; (void)sx; (void)sy; (void)sz;
                    sound_play_request(soundParams);
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                }
            } else if (state < 8) {
                int soundParams[7];
                unsigned char soundFlag;
                float soundVol;
                unsigned int sx, sy, sz;
                sound_init_play_params((int)soundParams);
                soundFlag = 1;
                soundParams[0] = 0x35b;
                soundVol = FLOAT_806190a8;
                sx = *(unsigned int*)(entity + 100);
                sy = *(unsigned int*)(entity + 0x68);
                sz = *(unsigned int*)(entity + 0x6c);
                (void)soundFlag; (void)soundVol; (void)sx; (void)sy; (void)sz;
                sound_play_request(soundParams);
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
            }
        }
    }
    FUN_8038273c();
}

// 0x80269a74 : mounted-gun fire. On an active fire clip whose fire event has
// passed, computes the muzzle world position and firing direction (either the
// straight-ahead barrel forward, or a lead solution toward the partner target
// with a distance-scaled reach), plays the fire sound, spawns the tracer trail
// and the projectile.
void aiactor_mounted_gun_fire(void) {
    int entity = FUN_803826f0();
    int* actorState = *(int**)(entity + 0x7c);

    int fireActive = anim_is_clip_active(entity, 0xa29);
    if (fireActive == 0) {
        fireActive = anim_is_clip_active(entity, 0xa2b);
    }
    if (fireActive != 0 &&
        (anim_event_scan_active() != 0 || anim_event_scan_active() != 0)) {

        float dir[3];   // local_b8/b4/b0
        float muzzle[3]; // local_ac/a8/a4
        if (((*(unsigned int*)(entity + 0x10) | *(unsigned int*)(entity + 0xc)) & 0xffff) == 0) {
            float mtx[22];
            mtx4_set_rotation_y((double)((FLOAT_80619070 *
                                          ((float)actorState[0x278] + *(float*)(entity + 0x70)))
                                         / FLOAT_80619074), mtx);
            dir[0] = FLOAT_80618f98;
            dir[1] = FLOAT_80618f98;
            dir[2] = FLOAT_80618fb8;
            mtx4_transform_vector(mtx, dir);
            float lenSq = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
            if (FLOAT_80619080 < lenSq) {
                float rs = 1.0f / (float)math_sqrt((double)lenSq);
                rs = rs * -(FLOAT_80618fd0 * lenSq * rs * rs - FLOAT_80619040);
                float scale = FLOAT_80618fb8 * rs * -(FLOAT_80618fd0 * lenSq * rs * rs - FLOAT_80619040);
                dir[0] = dir[0] * scale;
                dir[1] = dir[1] * scale;
                dir[2] = dir[2] * scale;
            }
            muzzle[0] = FLOAT_80618fac * dir[0] + *(float*)(entity + 100);
            muzzle[1] = FLOAT_80618fac * dir[1] + *(float*)(entity + 0x68) + FLOAT_80618fb8;
            muzzle[2] = FLOAT_80618fac * dir[2] + *(float*)(entity + 0x6c);
        } else {
            float fwd[3];   // local_c4/c0/bc
            fwd[0] = DAT_803e0540;
            fwd[1] = DAT_803e0544;
            fwd[2] = DAT_803e0548;
            int boneIndex;
            if (anim_event_scan_active() == 0) {
                boneIndex = 4;
                int* ctrl = *(int**)(*(int*)(entity + 4) + 4);
                float* anchor = (float*)ctrl[*ctrl * -0x30 + 0xab];
                muzzle[0] = anchor[0];
                muzzle[1] = anchor[1];
                muzzle[2] = anchor[2];
            } else {
                boneIndex = 3;
                int* ctrl = *(int**)(*(int*)(entity + 4) + 4);
                float* anchor = (float*)ctrl[*ctrl * -0x30 + 0xdb];
                muzzle[0] = anchor[0];
                muzzle[1] = anchor[1];
                muzzle[2] = anchor[2];
            }
            mtx4_transform_point((float*)(*(int*)(*(int*)(entity + 4) + 0x10) + boneIndex * 0x40), muzzle);
            mtx4_transform_vector((float*)(entity + 0x274), muzzle);
            muzzle[0] = muzzle[0] + *(float*)(entity + 100);
            muzzle[1] = muzzle[1] + *(float*)(entity + 0x68);
            muzzle[2] = muzzle[2] + *(float*)(entity + 0x6c);
            mtx4_transform_vector((float*)(*(int*)(*(int*)(entity + 4) + 0x10) + boneIndex * 0x40), fwd);
            mtx4_transform_vector((float*)(entity + 0x274), fwd);
            float fwdLenSq = fwd[2] * fwd[2] + fwd[0] * fwd[0] + fwd[1] * fwd[1];
            if (FLOAT_80619080 < fwdLenSq) {
                float rs = 1.0f / (float)math_sqrt((double)fwdLenSq);
                rs = rs * -(FLOAT_80618fd0 * fwdLenSq * rs * rs - FLOAT_80619040);
                float scale = FLOAT_80618fb8 * rs * -(FLOAT_80618fd0 * fwdLenSq * rs * rs - FLOAT_80619040);
                fwd[0] = fwd[0] * scale;
                fwd[1] = fwd[1] * scale;
                fwd[2] = fwd[2] * scale;
            }
            int partnerState = *(int*)(actorState[1] + 0x7c);
            float px = *(float*)(entity + 100) - *(float*)(partnerState + 0xbb4);
            float py = *(float*)(entity + 0x68) - *(float*)(partnerState + 3000);
            float pz = *(float*)(entity + 0x6c) - *(float*)(partnerState + 0xbbc);
            double dist = math_sqrt((double)(pz * pz + px * px + py * py));
            float clamp = (float)(dist - (double)FLOAT_80618ff4);
            if (FLOAT_80618ff8 < clamp) {
                clamp = FLOAT_80618ff8;
            }
            double reach;
            if (FLOAT_80618fd0 <= clamp) {
                reach = (double)(float)(dist - (double)FLOAT_80618ff4);
                if ((double)FLOAT_80618ff8 < reach) {
                    reach = (double)FLOAT_80618ff8;
                }
            } else {
                reach = (double)FLOAT_80618fd0;
            }
            float lead = (float)(dist + reach);
            fwd[0] = fwd[0] * lead + *(float*)(entity + 100);
            dir[0] = fwd[0] - muzzle[0];
            fwd[1] = fwd[1] * lead + *(float*)(entity + 0x68) + FLOAT_80618fb8;
            dir[1] = fwd[1] - muzzle[1];
            fwd[2] = fwd[2] * lead + *(float*)(entity + 0x6c);
            dir[2] = fwd[2] - muzzle[2];
            float dirLenSq = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
            if (FLOAT_80619080 < dirLenSq) {
                float rs = 1.0f / (float)math_sqrt((double)dirLenSq);
                rs = rs * -(FLOAT_80618fd0 * dirLenSq * rs * rs - FLOAT_80619040);
                float scale = FLOAT_80618fb8 * rs * -(FLOAT_80618fd0 * dirLenSq * rs * rs - FLOAT_80619040);
                dir[0] = dir[0] * scale;
                dir[1] = dir[1] * scale;
                dir[2] = dir[2] * scale;
            }
            muzzle[0] = FLOAT_80618fe0 * dir[0] + muzzle[0];
            muzzle[1] = FLOAT_80618fe0 * dir[1] + muzzle[1];
            muzzle[2] = FLOAT_80618fe0 * dir[2] + muzzle[2];
        }

        float toMuzzle[3];  // local_d0/cc/c8
        toMuzzle[0] = muzzle[0] - *(float*)(entity + 100);
        toMuzzle[1] = (float)((double)muzzle[1] - (double)*(float*)(entity + 0x68));
        toMuzzle[2] = (float)((double)muzzle[2] - (double)*(float*)(entity + 0x6c));
        int hitRoom = scene_ray_portal_intersect((double)muzzle[2], (double)muzzle[1],
                                                 (int*)(entity + 100), toMuzzle,
                                                 *(int*)(entity + 0x34), 0, 0, (float*)0, (float*)0);

        int soundParams[7];
        unsigned char soundFlag;
        float sfxMuzzle[3];
        sound_init_play_params((int)soundParams);
        soundFlag = 1;
        soundParams[0] = 0xcab;
        sfxMuzzle[0] = muzzle[0];
        sfxMuzzle[1] = muzzle[1];
        sfxMuzzle[2] = muzzle[2];
        (void)soundFlag; (void)sfxMuzzle;
        sound_play_request(soundParams);

        player_ai_spawn_trail_particle(0, 0, -1, muzzle, dir, -1);
        player_ai_projectile_spawn_simple((double)FLOAT_80619088, (double)FLOAT_80618f98, 4,
                                          muzzle, (int*)dir, entity, actorState, 2, 0,
                                          hitRoom, 0x184, 2);
    }
    FUN_8038273c();
}

// 0x8026a050 : mount pose + aim matrix build. Evaluates the model pose, and for
// the firing state (8) computes the barrel yaw/pitch from the sampled aim
// curves and the mount velocity, clamping the accumulators (+0x9dc yaw, +0xa80
// pitch) via the angle-wrap helper. Builds the seat rotation matrix and applies
// it to the four mount bone matrices before committing the pose.
void aiactor_mount_pose_aim_update(void) {
    int entity = FUN_803826f0();

    float yaw;
    if (*(int*)(entity + 0x20) == 8) {
        yaw = *(float*)(*(int*)(entity + 0x7c) + 0x9e0);
    } else {
        yaw = *(float*)(*(int*)(entity + 0x7c) + 0x20);
    }
    double yawIn = (double)yaw;
    model_pose_eval();

    if (*(int*)(entity + 0x20) == 8) {
        int actorState = *(int*)(entity + 0x7c);
        int desc = *(int*)(DAT_80611824 + *(int*)(*(int*)(entity + 4) + 0xd0) * 4);
        double aimA = anim_sample_curve_value((double)*(float*)(*(int*)(entity + 4) + 0xd4), desc);
        double aimB = anim_sample_curve_value((double)*(float*)(*(int*)(entity + 4) + 0xd8), desc);
        double aimYaw = (double)((FLOAT_80619070 *
                                  ((float)(aimA - aimB) +
                                   (*(float*)(entity + 0x70) - *(float*)(entity + 0x108))))
                                 / FLOAT_80619074);
        double speed = math_sqrt((double)(*(float*)(entity + 0xf8) * *(float*)(entity + 0xf8) +
                                          *(float*)(entity + 0x100) * *(float*)(entity + 0x100)));
        speed = -speed;
        if ((double)FLOAT_806190d0 < ABS(aimYaw)) {
            aimYaw = (double)FLOAT_80618f98;
        }
        double scaledYaw = (double)(float)((double)FLOAT_80618fd0 * aimYaw);
        double targetYaw = (double)(float)(scaledYaw + (double)(float)((double)*(float*)(actorState + 0x9dc) + speed));
        double wrappedYaw = FUN_80390324((double)((float)(targetYaw - (double)FLOAT_80618f98) / FLOAT_80618fb8));
        *(float*)(actorState + 0x9dc) = -(float)((double)FLOAT_80618fb8 * (double)(float)wrappedYaw - targetYaw);
        double targetPitch = (double)(float)((double)(float)((double)*(float*)(actorState + 0xa80) + speed) - scaledYaw);
        double wrappedPitch = FUN_80390324((double)(float)((double)(float)(targetPitch - (double)FLOAT_80618f98) / (double)FLOAT_80618fb8));
        *(float*)(actorState + 0xa80) = -(float)((double)FLOAT_80618fb8 * (double)(float)wrappedPitch - targetPitch);
    }

    float mtx[24];
    mtx4_set_rotation_y((double)((float)((double)FLOAT_80619070 * yawIn) / FLOAT_80619074), mtx);
    int boneMtxBase = *(int*)(*(int*)(entity + 4) + 0x10);
    for (int i = 1, off = 0x40; i < 5; ++i, off += 0x40) {
        float* boneMtx = (float*)(boneMtxBase + off);
        mtx4_multiply(boneMtx, mtx, boneMtx);
    }
    FUN_8026a5c8();
    FUN_8038273c();
}

// 0x8026a234 : mount-yaw slew. Steps the stored yaw accumulator (+0x9e0)
// toward the requested angle by a frame-scaled maximum delta, wrapping the
// error into [-180, 180] first.
void aiactor_mount_yaw_step(double targetAngle, int entity) {
    int actorState = *(int*)(entity + 0x7c);
    float maxStep = FLOAT_806118ec * FLOAT_806190e0 *
        (FLOAT_80618fb8 /
         (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8)) *
        FLOAT_806108ec;

    float err = (float)(targetAngle - (double)*(float*)(actorState + 0x9e0));
    while (err <= FLOAT_80618fa4) {
        err = err + FLOAT_80618f9c;
    }
    while (FLOAT_80619074 < err) {
        err = err - FLOAT_80618f9c;
    }

    if (-maxStep <= err) {
        if (err <= maxStep) {
            *(float*)(actorState + 0x9e0) = (float)targetAngle;
        } else {
            *(float*)(actorState + 0x9e0) = *(float*)(actorState + 0x9e0) + maxStep;
        }
    } else {
        *(float*)(actorState + 0x9e0) = *(float*)(actorState + 0x9e0) - maxStep;
    }
}

// 0x8026a2f8 : compute the yaw error (in the game's 0..65536 angle units)
// between the mount's current seat matrix orientation and the direction to the
// current target, minus the actor's facing yaw.
double aiactor_mount_target_yaw_delta(int entity) {
    float euler[4];
    mtx4_to_euler(*(int*)(*(int*)(entity + 4) + 0x10), 0, euler, (float*)0);
    int target = *(int*)(*(int*)(entity + 0x7c) + 4);
    double toTarget = (double)math_atan2((double)(*(float*)(target + 100) - *(float*)(entity + 100)),
                                         (double)(*(float*)(target + 0x6c) - *(float*)(entity + 0x6c)));
    return (double)((FLOAT_80619074 * ((float)toTarget - euler[0])) / FLOAT_80619070
                    - *(float*)(entity + 0x70));
}

// 0x8026a388 : mount death / alert dispatch. On the death sub-state (2) it
// credits the kill (resolving the killer from the passed damage record), clears
// the active flag, sets the ragdoll stance, plays the death sound and spawns the
// death particle. Sub-state 1 arms the zone alert group when the damage source
// is valid; sub-state 7 forwards to the mount state-7 handler.
void aiactor_mount_death_dispatch(unsigned int p1, unsigned int p2, int* damageRec) {
    long long framePair = FUN_803826f0();
    int entity = (int)((unsigned long long)framePair >> 0x20);
    int subState = (int)framePair;
    int* actorState = *(int**)(entity + 0x7c);

    if (subState == 2) {
        int* killerState = (int*)0;
        if (damageRec != (int*)0 && damageRec[1] != 0 && *(int*)(damageRec[1] + 0x20) == 8) {
            killerState = *(int**)(damageRec[1] + 0x7c);
        }
        worm_stat_on_kill(killerState, actorState);
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xffffffbf;
        aiactor_set_stance_flags(entity, 8);
        anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        if (actorState[8] != 5) {
            actorState[0x274] = 5;
        }
        int soundParams[7];
        unsigned char soundFlag;
        float soundVol;
        unsigned int sx, sy, sz;
        sound_init_play_params((int)soundParams);
        soundFlag = 1;
        soundParams[0] = 0x35d;
        soundVol = FLOAT_80619008;
        sx = *(unsigned int*)(entity + 0x1a8);
        sy = *(unsigned int*)(entity + 0x1ac);
        sz = *(unsigned int*)(entity + 0x1b0);
        (void)soundFlag; (void)soundVol; (void)sx; (void)sy; (void)sz;
        sound_play_request(soundParams);
        float burstPos[3];
        burstPos[0] = *(float*)(entity + 100) + *(float*)(entity + 0x1a8);
        burstPos[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
        burstPos[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
        int coop = session_get_state_flag();
        particle_spawn_directed(0, 0x15, ~-(unsigned int)(coop == 1) & 0x80000, burstPos, (int*)0);
    } else if (subState < 2) {
        if (0 < subState && actorState[8] == 1) {
            if (damageRec != (int*)0 && *damageRec != 0 && *(int*)(*damageRec + 0x150) != 0) {
                aiactor_alert_group_arm(actorState[0x2fb]);
            }
            if (actorState[8] != 4) {
                actorState[0x274] = 4;
            }
            actorState[0x2cc] = (int)FLOAT_80618f98;
        }
    } else if (subState == 7) {
        FUN_8026a568(entity);
    }
    FUN_8038273c();
}

// ============================================================================
// Address range: 0x8026a568 - 0x8026c143 | batch_744 | 2026-09-09
//
// Continuation of the AI-actor mount/turret cluster (batches 739-743). This
// group is the mounted-gun / turret-creature / kamikaze main-tick drivers plus
// their small state-request helpers and the collision-face/pose commit used by
// the mount pose builder.
//
// Two distinct actor kinds share the +0x7c state block here:
//   * MOUNTED GUN / vehicle rider - behaviour-state id at (actorState + 0x20),
//     pending-swap trio +0x9d0/+0x9d4/+0x9d8, yaw/pitch accumulators
//     +0x9dc/+0x9e0/+0xa80, cooldown timers +0xb30/+0xb34.
//   * TURRET CREATURE - identical layout but the driver at 0x8026b28c walks the
//     state via the word-indexed alias (piVar7[8] == +0x20, piVar7[0x274] ==
//     +0x9d0, piVar7[0x278] == +0x9e0, etc.); charge/scale ramp on +0x9e0.
//
// Functions:
//   aiactor_mount_request_dismount     0x8026a568  state3 -> queue pending state 1
//   aiactor_mount_request_reload       0x8026a58c  in-range states -> queue pending 3
//   aiactor_mount_build_collision_faces 0x8026a5c8 build 2 collision-face matrices from yaw/pitch
//   aiactor_mount_state_bark           0x8026a6dc  per-state proximity bark w/ cooldown gate
//   aiactor_mount_main_tick            0x8026a784  mounted-gun 7-state FSM main tick
//   aiactor_mount_spawn_tick           0x8026b0c8  mount state0 spawn/idle setup
//   aiactor_mount_request_combat       0x8026b23c  queue pending state 4 (combat)
//   aiactor_turret_creature_tick       0x8026b28c  turret-creature FSM (fire/charge/melee)
//   aiactor_kamikaze_tick              0x8026ba94  self-destruct charge + explosion tick
//   aiactor_mount_event_handler        0x8026bd98  mount damage/hit-reaction event dispatch
// ============================================================================

extern "C" {
// --- new per-file externs for batch_744 (not already declared above) ---
extern float FLOAT_80618fbc;
extern float FLOAT_806190e4;
extern float FLOAT_806190e8;
extern float FLOAT_806190ec;
extern float FLOAT_806190f0;
extern float DAT_803e054c;
extern float DAT_803e0550;
extern float DAT_803e0554;
extern unsigned char DAT_8048caa0[];             // per-weapon spawn-probe table (stride 0x30)
extern int   aiactor_target_scan_all(int frame);
extern int   collision_alloc_face_table(int* ctrl);
extern int   mtx4_pool_alloc(void);
extern int   anim_slot_is_primary_clip(int entity, int clipId);
extern int   game_frand(void);
extern double math_poly_trig(double x);
extern void  mtx4_set_translation(double x, double y, double z);
extern void  collision_set_face_entry(int ctrl, int index, int mtx);
extern void  mtx4_set_scale(double x, double y, double z);
extern void  mtx4_inverse_transform_point(float* mtx, float* pt);
extern void  sound_update_3d_source(double a, double b, double c, int handleSlot, int posXyz,
                                    int soundId, int flag);
extern void  sound_set_pitch(double pitch, unsigned int handle);
extern int   player_ai_explosion_spawn(int a, int type, float* pos, float* dir, int owner,
                                       int source, int flag);
extern void  FUN_80358fc4(int a, int b, int c, float* pos, float* dir);   // decal/impact spawn
extern void  zcsc_smd_handle_remap(int entity, int rec);
extern int   worm_map100_arcade_hook(int entity);
extern void  aiactor_play_proximity_bark(double vol, double p2, double p3, double p4, double p5,
                                         long long p6, long long p7, long long p8, int entity,
                                         int lineId, int p11, int p12, int p13, int p14,
                                         int p15, int p16);
}

// 0x8026a568 : queue a dismount when the mount is in ride state 3.
void aiactor_mount_request_dismount(int entity) {
    if (*(int*)(*(int*)(entity + 0x7c) + 0x20) == 3) {
        *(int*)(*(int*)(entity + 0x7c) + 0x9d0) = 1;
    }
}

// 0x8026a58c : queue a reload/return (pending state 3) for the in-range states
// (0..2, 4). No-op for state 3 (already reloading) or out-of-band states.
void aiactor_mount_request_reload(int entity) {
    int state = *(int*)(*(int*)(entity + 0x7c) + 0x20);
    if (state == 3) {
        return;
    }
    if (state < 3) {
        if (state < 0) {
            return;
        }
    } else if (4 < state) {
        return;
    }
    *(int*)(*(int*)(entity + 0x7c) + 0x9d0) = 3;
}

// 0x8026a5c8 : build the mount's two dynamic collision-face matrices from the
// current yaw (+0x9dc / +0x24) and pitch (+0xa80 / +0x48) accumulators and bind
// them into the collision object (faces 0 and 2), flagging both as dynamic.
void aiactor_mount_build_collision_faces(void) {
    int entity = FUN_803826f0();
    float yaw, pitch;
    if (*(int*)(entity + 0x20) == 8) {
        yaw = *(float*)(*(int*)(entity + 0x7c) + 0x9dc);
        pitch = *(float*)(*(int*)(entity + 0x7c) + 0xa80);
    } else {
        yaw = *(float*)(*(int*)(entity + 0x7c) + 0x24);
        pitch = *(float*)(*(int*)(entity + 0x7c) + 0x48);
    }
    double dyaw = (double)yaw;
    double dpitch = (double)pitch;

    if ((*(int**)(entity + 4))[0x7a] != 0 ||
        collision_alloc_face_table(*(int**)(entity + 4)) != 0) {
        int mtxA = mtx4_pool_alloc();
        int mtxB = mtx4_pool_alloc();
        double zero = (double)FLOAT_80618f98;
        int col = *(int*)(*(int*)(entity + 4) + 0x1e8);
        *(unsigned int*)(col + 0x1c) = *(unsigned int*)(col + 0x1c) | 0x8000;
        mtx4_set_translation(zero, dpitch, zero);
        collision_set_face_entry(*(int*)(entity + 4), 0, mtxA);
        zero = (double)FLOAT_80618f98;
        col = *(int*)(*(int*)(entity + 4) + 0x1e8);
        *(unsigned int*)(col + 0x7c) = *(unsigned int*)(col + 0x7c) | 0x8000;
        mtx4_set_translation(zero, dyaw, zero);
        collision_set_face_entry(*(int*)(entity + 4), 2, mtxB);
    }
    FUN_8038273c();
}

// 0x8026a6dc : per-state proximity bark. Picks a bark line id from the current
// mount state and, once the +0xb34 cooldown timer has elapsed, plays it and
// resets the timer.
void aiactor_mount_state_bark(long long p1, double p2, double p3, double p4, double p5,
                              long long p6, long long p7, long long p8, int entity,
                              int p10, int p11, int p12, int p13, int p14, int p15, int p16) {
    int actorState = *(int*)(entity + 0x7c);
    int state = *(int*)(actorState + 0x20);
    if (state != 3) {
        float threshold = FLOAT_80618ff8;
        int lineId;
        if (state < 3) {
            if (state == 1) {
                lineId = 0x105b;
                threshold = FLOAT_80619034;
            } else {
                if (state < 1) {
                    return;
                }
                lineId = 0x105d;
            }
        } else {
            if (6 < state) {
                return;
            }
            if (4 < state) {
                return;
            }
            lineId = 0x105e;
        }
        if (threshold <= *(float*)(actorState + 0xb34)) {
            aiactor_play_proximity_bark((double)FLOAT_806190a8, p2, p3, p4, p5, p6, p7, p8,
                                        entity, lineId, 0, p12, p13, p14, p15, p16);
            *(float*)(actorState + 0xb34) = FLOAT_80618f98;
        }
    }
}

// 0x8026a784 : mounted-gun 7-state FSM main tick. Applies the queued state swap,
// advances the locomotion timer, (re)acquires a target, runs the mounted-gun
// fire aim solve, then dispatches per state: 0 spawn/register, 1 combat scan +
// LOS transition, 2 aim/fire, 3 reload, 4 return-to-path, 5 spawn dismount
// projectile, 6 destroy. Finishes with a 3D engine-sound update.
void aiactor_mount_main_tick(long long p1, double p2, double p3, double p4, double p5,
                             double p6, double p7, double p8, int p9, int p10, float* p11,
                             int p12, int p13, int p14, int p15, int p16) {
    int entity = __save_gpr_28();
    int actorState = *(int*)(entity + 0x7c);
    int los = aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = *(int*)(actorState + 0x9d0);
        }
        *(int*)(actorState + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        float dt = FLOAT_80618fb8 /
            (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8);
        *(float*)(actorState + 0xa90) = (float)((double)FLOAT_806108ec * (double)dt
                                                + (double)*(float*)(actorState + 0xa90));
    }

    int mountedType = 0;
    double dv = (double)FLOAT_80618fb8;
    if (*(int*)(actorState + 8) == DAT_806120f8 || *(int*)(actorState + 4) == 0) {
        int (*scan)(int) = *(int(**)(int))(*(int*)(actorState + 0xbf0) + 0x78);
        if (scan == (int(*)(int))0) {
            *(int*)(actorState + 4) = aiactor_target_scan_all(entity);
        } else {
            *(int*)(actorState + 4) = scan(entity);
        }
    }
    if (*(int*)(actorState + 4) == 0) {
        __restore_gpr_28();
        return;
    }
    int seat = *(int*)(*(int*)(actorState + 0xbf0) + 0x68);
    if (seat != -2) {
        mountedType = *(int*)(*(int*)(DAT_8061208c + 0x34) + seat * 8);
    }
    aiactor_mounted_gun_fire();

    // aim-basis build when a linked aim node (+0xc00) is present
    int aimNode = *(int*)(actorState + 0xc00);
    if (aimNode != 0) {
        float d0 = *(float*)(aimNode + 0x40) - *(float*)(aimNode + 0x58);
        float d2 = *(float*)(aimNode + 0x40) - *(float*)(aimNode + 0x28);
        if ((double)FLOAT_80618f98 == (double)d0 && (double)FLOAT_80618f98 == (double)d2) {
            *(unsigned char*)(entity + 0x32) = 1;
        } else {
            float ax = *(float*)(aimNode + 0x3c) - *(float*)(aimNode + 0x54);
            float ay = *(float*)(aimNode + 0x3c) - *(float*)(aimNode + 0x24);
            float az = *(float*)(aimNode + 0x44) - *(float*)(aimNode + 0x5c);
            float aw = *(float*)(aimNode + 0x44) - *(float*)(aimNode + 0x2c);
            // cross products against the fixed basis DAT_803e054c/50/54
            float cx = (float)((double)d0 * (double)aw - (double)az * (double)d2);
            float cy = (float)((double)az * (double)ay - (double)ax * (double)aw);
            float cz = (float)((double)ax * (double)d2 - (double)d0 * (double)ay);
            float b0 = (float)((double)cx * (double)DAT_803e0550 - (double)cy * (double)DAT_803e054c);
            float b1 = (float)((double)cy * (double)DAT_803e0554 - (double)cz * (double)DAT_803e0550);
            float b2 = (float)((double)cz * (double)DAT_803e054c - (double)cx * (double)DAT_803e0554);
            float basis[3];
            basis[0] = (float)((double)b2 * (double)cz - (double)b0 * (double)cy);
            basis[1] = (float)((double)b0 * (double)cx - (double)b1 * (double)cz);
            basis[2] = (float)((double)b1 * (double)cy - (double)b2 * (double)cx);
            float angs[3];
            dir_to_angles(basis, 0, angs);
            float lenSq = (float)((double)cz * (double)cz + (double)cx * (double)cx + (double)cy * (double)cy);
            if (FLOAT_80619080 < lenSq) {
                float rs = 1.0f / (float)math_sqrt((double)lenSq);
                rs = rs * -(FLOAT_80618fd0 * lenSq * rs * rs - FLOAT_80619040);
                float scale = (float)((double)FLOAT_80618fb8 * (double)(rs * -(FLOAT_80618fd0 * lenSq * rs * rs - FLOAT_80619040)));
                cx = cx * scale; cy = cy * scale; cz = cz * scale;
            }
            double s1 = math_poly_trig((double)((FLOAT_80619070 * (*(float*)(entity + 0x70) + angs[0])) / FLOAT_80619074 - FLOAT_806190e4));
            double s2 = math_poly_trig((double)((FLOAT_80619070 * (*(float*)(entity + 0x70) + angs[0])) / FLOAT_80619074));
            double cySq = (double)(float)((double)cy * (double)cy);
            double m1 = math_sqrt((double)(float)((double)cx * (double)cx + cySq));
            double m2 = math_sqrt((double)(float)((double)cz * (double)cz + cySq));
            double a1 = (double)math_asin((double)(float)((double)cy / m1));
            double e1 = (double)((float)((double)FLOAT_80619074 * a1) / FLOAT_80619070);
            double a2 = (double)math_asin((double)(float)((double)cy / m2));
            double e2 = (double)((float)((double)FLOAT_80619074 * a2) / FLOAT_80619070);
            *(float*)(entity + 200) = (float)(e1 * s1 + (double)(float)(e2 * s2));
            *(float*)(entity + 0xc0) = (float)(e2 * s1 + (double)(float)(e1 * s2));
            *(unsigned char*)(entity + 0x32) = 2;
        }
    }

    switch (*(int*)(actorState + 0x20)) {
    case 0:
        *(unsigned int*)(actorState + 0x28) = *(unsigned int*)(actorState + 0x28) | 0x6000000;
        entity_register_for_update(entity, 2, -0x7fd95fb0);
        *(float*)(actorState + 0xb30) = FLOAT_80618f98;
        *(float*)(actorState + 0x9e0) = FLOAT_80618f98;
        aiactor_mount_yaw_step((double)FLOAT_80618f98, entity);
        *(float*)(actorState + 0xb2c) = *(float*)(actorState + 0xb2c) * FLOAT_80619038;
        if (*(int*)(actorState + 0x20) != 1) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = 1;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
        }
        *(float*)(DAT_8048ca9c + *(short*)(actorState + 0x48) * 0x30) = FLOAT_80619054;
        *(float*)(DAT_8048caa0 + *(short*)(actorState + 0x48) * 0x30) = FLOAT_806190e8;
        break;
    case 1:
        aiactor_mounted_gun_combat_tick(dv, p2, p3, p4, p5, p6, p7, p8, entity, mountedType,
                                        (int)(long long)p11, p12, p13, p14, p15, p16);
        aiactor_ride_path_arrival_tick();
        aiactor_mount_yaw_step((double)FLOAT_80618f98, entity);
        los = aiactor_evaluate_target_los();
        if (los == 1) {
            if (*(int*)(actorState + 0x20) != 2) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 2;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
            *(float*)(actorState + 0xb34) = FLOAT_80619088;
            if (*(int*)(*(int*)(actorState + 4) + 0x150) != 0) {
                aiactor_alert_group_arm(*(int*)(actorState + 0xbec));
            }
        } else if (los < 1 && -1 < los) {
            if (aiactor_alert_group_flagA_any(*(unsigned int*)(actorState + 0xbec)) != 0) {
                if (*(int*)(actorState + 0x20) != 4) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(int*)(actorState + 0x20) = 4;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                }
                *(float*)(actorState + 0xb30) = FLOAT_80618f98;
                *(float*)(actorState + 0xb34) = FLOAT_80619088;
            }
        } else {
            if (*(int*)(actorState + 0x20) != 4) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 4;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
            *(float*)(actorState + 0xb34) = FLOAT_80619088;
            if (*(int*)(*(int*)(actorState + 4) + 0x150) != 0) {
                aiactor_alert_group_arm(*(int*)(actorState + 0xbec));
            }
        }
        aiactor_facing_update();
        break;
    case 2: {
        if (anim_is_clip_active(entity, 0xa29) == 0) {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        }
        double yawDelta = aiactor_mount_target_yaw_delta(entity);
        aiactor_mount_yaw_step(yawDelta, entity);
        bool retreat = true;
        if (los != 0) {
            int target = *(int*)(actorState + 4);
            float dx = *(float*)(entity + 100) - *(float*)(target + 100);
            float dy = *(float*)(entity + 0x68) - *(float*)(target + 0x68);
            float dz = *(float*)(entity + 0x6c) - *(float*)(target + 0x6c);
            float distSq = (float)((double)(dz * dz) + (double)(dx * dx) + (double)(dy * dy));
            if ((double)FLOAT_80619048 > (double)distSq) {
                retreat = false;
            }
        }
        if (retreat) {
            if (*(int*)(actorState + 0x20) != 4) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 4;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
            *(float*)(actorState + 0xb34) = FLOAT_80619088;
        }
        aiactor_mount_state_bark((long long)yawDelta, p2, p3, p4, p5, p6, p7, p8, entity, 0,
                                 (int)(long long)p11, p12, p13, p14, p15, p16);
        break;
    }
    case 3:
        dv = (double)FLOAT_80618fc0;
        if (anim_is_clip_active(entity, 0xa2a) == 0) {
            anim_play_scaled((double)FLOAT_8061903c, (double)FLOAT_80618fb8);
        }
        break;
    case 4: {
        if (anim_is_clip_active(entity, 0xa2b) == 0 && anim_is_clip_active(entity, 0xa2b) == 0) {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        }
        double yawDelta = aiactor_mount_target_yaw_delta(entity);
        aiactor_mount_yaw_step(yawDelta, entity);
        int leaderZone = *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 0x678);
        if (*(int*)(actorState + 0x684) != leaderZone) {
            if (leaderZone < 0) {
                *(int*)(actorState + 0x684) =
                    *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                *(int*)(actorState + 0x684) = leaderZone;
            }
            *(int*)(actorState + 0x9ac) = 0;
            *(int*)(actorState + 0x9a8) = 0;
        }
        aiactor_follow_zone_path();
        if (anim_active_slot_userflags(*(int*)(entity + 4)) != 0) {
            if (los == 0) {
                if (anim_is_clip_active(entity, 0xa22) == 0) {
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                }
            } else {
                *(float*)(actorState + 0xb30) = FLOAT_80618f98;
                if (anim_is_clip_active(entity, 0xa2b) == 0) {
                    anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                }
            }
        }
        bool doneReturn = true;
        if (los != 0) {
            int target = *(int*)(actorState + 4);
            float dx = *(float*)(entity + 100) - *(float*)(target + 100);
            float dy = *(float*)(entity + 0x68) - *(float*)(target + 0x68);
            float dz = *(float*)(entity + 0x6c) - *(float*)(target + 0x6c);
            float distSq = (float)((double)(dz * dz) + (double)(dx * dx) + (double)(dy * dy));
            if (FLOAT_80619048 > distSq) {
                if (*(int*)(actorState + 0x20) != 2) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(int*)(actorState + 0x20) = 2;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                }
                *(float*)(actorState + 0xb34) = FLOAT_80619088;
                doneReturn = false;
            }
        }
        if (doneReturn && los == 0 && FLOAT_806190dc < *(float*)(actorState + 0xb30)) {
            aiactor_mount_request_dismount(entity);
        }
        aiactor_facing_update();
        aiactor_mount_state_bark((long long)yawDelta, p2, p3, p4, p5, p6, p7, p8, entity, 0,
                                 (int)(long long)p11, p12, p13, p14, p15, p16);
        break;
    }
    case 5:
        dv = (double)FLOAT_80618fc0;
        if (anim_active_slot_userflags(*(int*)(entity + 4)) != 0) {
            int proj = projectile_spawn(dv, p2, p3, p4, p5, p6, p7, p8);
            entity_register_for_update(proj, 2, -0x7fd95fb0);
            int projState = *(int*)(proj + 0x7c);
            *(int*)(projState + 0x20) = *(int*)(actorState + 0x9e0);
            *(int*)(projState + 0x24) = *(int*)(actorState + 0x9dc);
            *(int*)(projState + 0x48) = *(int*)(actorState + 0xa80);
            if (*(int*)(actorState + 0x20) != 6) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 6;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
            __restore_gpr_28();
            return;
        }
        break;
    case 6: {
        long long h = (long long)worm_map100_arcade_hook(entity);
        aiactor_destroy_instance(h, p2, p3, p4, p5, p6, p7, p8, entity);
        __restore_gpr_28();
        return;
    }
    }
    sound_update_3d_source(dv, (double)FLOAT_80618fbc, (double)FLOAT_80618fb8,
                           entity + 0x22c, entity + 100, 0x35a, 0);
    __restore_gpr_28();
}

// 0x8026b0c8 : mount state-0 spawn/idle setup. Applies queued swap + locomotion
// timer, then for state 0 samples the idle keyframe of the char-def's default
// clip, starts it if idle, and sets the passive stance flags.
void aiactor_mount_spawn_tick(void) {
    int entity = FUN_803826f0();
    int actorState = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = *(int*)(actorState + 0x9d0);
        }
        *(int*)(actorState + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        *(float*)(actorState + 0xa90) = FLOAT_806108ec *
            (FLOAT_80618fb8 / (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8))
            + *(float*)(actorState + 0xa90);
    }

    if (*(int*)(actorState + 0x20) == 0) {
        int track = -1;
        if (*(int*)(actorState + 0xbf0) != 0) {
            int t = (int)*(short*)(*(int*)(actorState + 0xbf0) + 0x94);
            if (t != -1) {
                track = t;
            }
        }
        float kf[4];
        anim_sample_keyframe(*(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[track] * 4), 0, 0, kf);
        *(int*)(*(int*)(entity + 4) + 0xec) = *(int*)&kf[1];
        if (anim_is_clip_active(entity, track) == 0) {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618f98);
        }
        aiactor_set_stance_flags(entity, 1);
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xffffffbf;
    }
    FUN_8038273c();
}

// 0x8026b23c : queue a transition into combat (pending state 4) unless the
// mount is disabled (flag +0x10 bit0x10000) or already in state 4; seeds the
// yaw accumulator to 1.0.
void aiactor_mount_request_combat(int entity) {
    int actorState = *(int*)(entity + 0x7c);
    if ((*(unsigned int*)(actorState + 0x10) & 0x10000) != 0) {
        return;
    }
    if (*(int*)(actorState + 0x20) != 4) {
        *(int*)(actorState + 0x9d0) = 4;
        *(float*)(actorState + 0x9e0) = FLOAT_80618fb8;
    }
}

// 0x8026b28c : turret-creature FSM main tick. Same state block accessed via the
// word-indexed alias. States: 2 = fire (spawn projectile 0x30 toward target /
// wind up), 0 = acquire target + blink + charge windup, 1 = approach/leader
// follow + range check, 4 = spin/scale ramp + engine sound + burst explosion,
// 3 = melee swipe.
void aiactor_turret_creature_tick(void) {
    int entity = __save_gpr_28();
    int* actorState = *(int**)(entity + 0x7c);
    int active = aiactor_retarget_visible_teammate();

    if (-1 < actorState[0x274]) {
        if (actorState[0x274] != actorState[8]) {
            actorState[0x276] = (int)FLOAT_80618f98;
            actorState[0x275] = actorState[8];
            actorState[8] = actorState[0x274];
        }
        actorState[0x274] = -1;
    }
    if ((actorState[0xb] & 0x80U) == 0) {
        actorState[0x2a4] = (int)FLOAT_80618f98;
    } else {
        actorState[0x2a4] = (int)(FLOAT_806108ec *
            (FLOAT_80618fb8 / (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8))
            + (float)actorState[0x2a4]);
    }
    if (actorState[1] == 0) {
        actorState[1] = aiactor_acquire_nearest_target(entity, *(int*)(DAT_80611d78 + 0x14), 0);
    }
    if (actorState[1] == 0) {
        actorState[1] = *(int*)(DAT_80611d78 + 0x14);
    }

    int state = actorState[8];
    if (state == 2) {
        if (anim_is_clip_active(entity, 0x76c) == 0) {
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
        } else {
            unsigned int prim = anim_slot_is_primary_clip(entity, 0x76c);
            if (prim == 0 || anim_event_scan_active() == 0) {
                if (anim_slot_is_primary_clip(entity, 0x76c) != 0 &&
                    anim_primary_clip_time_left((double)FLOAT_80618ff0, *(int*)(entity + 4)) != 0) {
                    aiactor_set_stance_flags(entity, 1);
                    anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    if (actorState[8] != 1) {
                        actorState[0x275] = actorState[8];
                        actorState[8] = 1;
                        actorState[0x276] = (int)FLOAT_80618f98;
                        actorState[0x274] = actorState[8];
                    }
                    unsigned int r = rng_counter_tick();
                    actorState[0x278] = (int)(FLOAT_80618fdc *
                        ((float)((double)(long long)(unsigned int)r - DOUBLE_80618fe8) / FLOAT_80618fd8)
                        + FLOAT_80618fdc + (float)actorState[0x2cc]);
                }
            } else {
                float px = *(float*)(entity + 100);
                float py = *(float*)(entity + 0x68) + FLOAT_80618fb8;
                float pz = *(float*)(entity + 0x6c);
                int target = actorState[1];
                float dir[3];
                float pos[3];
                pos[0] = px; pos[1] = py; pos[2] = pz;
                dir[0] = *(float*)(target + 100) - px;
                dir[1] = *(float*)(target + 0x68) - py;
                dir[2] = *(float*)(target + 0x6c) - pz;
                int soundParams[7];
                unsigned char soundFlag;
                float soundVol, spx, spy, spz;
                sound_init_play_params((int)soundParams);
                soundFlag = 1;
                soundParams[0] = 0xd30;
                soundVol = FLOAT_80618ff4;
                spx = px; spy = py; spz = pz;
                (void)soundFlag; (void)soundVol; (void)spx; (void)spy; (void)spz;
                sound_play_request(soundParams);
                player_ai_projectile_spawn_simple((double)FLOAT_80618fa8, (double)FLOAT_80618f98,
                                                  0x30, pos, (int*)dir, 0, (int*)actorState, 2, 0,
                                                  *(int*)(entity + 0x34), 0, 2);
            }
        }
        aiactor_turn_toward_leader();
    } else if (state < 2) {
        if (state == 0) {
            actorState[1] = aiactor_acquire_nearest_target(entity, *(int*)(DAT_80611d78 + 0x14), 0);
            aiactor_set_stance_flags(entity, 1);
            actorState[10] = actorState[10] | 0x4000000;
            pickup_blink_start((double)FLOAT_80618fd0, (double)FLOAT_80618fa8, (int)(actorState + 0x31a));
            if ((*(unsigned int*)(actorState[0x2fc] + 0x54) & 0x100000) == 0) {
                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                if (actorState[8] != 1) {
                    actorState[0x275] = actorState[8];
                    actorState[8] = 1;
                    actorState[0x276] = (int)FLOAT_80618f98;
                    actorState[0x274] = actorState[8];
                }
                unsigned int r = rng_counter_tick();
                actorState[0x278] = (int)(FLOAT_80618fdc *
                    ((float)((double)(long long)(unsigned int)r - DOUBLE_80618fe8) / FLOAT_80618fd8)
                    + FLOAT_80618fdc + (float)actorState[0x2cc]);
            } else {
                aiactor_mount_request_combat(entity);
            }
        } else if (-1 < state) {
            if (anim_is_clip_active(entity, 0x76b) == 0) {
                aiactor_set_stance_flags(entity, 1);
                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
            }
            int t = actorState[1] != 0 ? *(int*)actorState[1] : 0;
            if (((actorState == (int*)0 || (t = *actorState) == 0 || actorState[2] < DAT_80612610 ||
                  ((*(unsigned int*)(t + 0x28) & 0x100) == 0 && (*(unsigned int*)(t + 0x10) & 0xffff) != 0)))
                && active != 0) {
                aiactor_leader_zone_update(entity);
                actorState[0xc] = *(int*)(actorState[1] + 100);
                actorState[0xd] = *(int*)(actorState[1] + 0x68);
                actorState[0xe] = *(int*)(actorState[1] + 0x6c);
            } else {
                int lz = *(int*)(*(int*)(actorState[1] + 0x7c) + 0x678);
                if (lz < 0) {
                    actorState[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    actorState[0x1a1] = lz;
                }
                actorState[0x26b] = 0;
                actorState[0x26a] = 0;
                aiactor_follow_zone_path();
            }
            if (active != 0) {
                if ((float)actorState[0x2bd] < FLOAT_80619050) {
                    if (*(int*)(actorState[1] + 0x20) == 8 &&
                        *(int*)(actorState[1] + 0x7c) != 0 &&
                        *(int*)(*(int*)(actorState[1] + 0x7c) + 8) < DAT_80612610) {
                        aiactor_mount_request_combat(entity);
                    } else if (actorState[8] != 3) {
                        actorState[0x275] = actorState[8];
                        actorState[8] = 3;
                        actorState[0x276] = (int)FLOAT_80618f98;
                        actorState[0x274] = actorState[8];
                    }
                    __restore_gpr_28();
                    return;
                }
                if ((float)actorState[0x278] < (float)actorState[0x2cc]) {
                    anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    if (actorState[8] != 2) {
                        actorState[0x275] = actorState[8];
                        actorState[8] = 2;
                        actorState[0x276] = (int)FLOAT_80618f98;
                        actorState[0x274] = actorState[8];
                    }
                    aiactor_set_stance_flags(entity, 4);
                }
            }
            aiactor_facing_update();
        }
    } else if (state == 4) {
        float scaleMtx[16];
        mtx4_set_scale((double)FLOAT_80618fb8, (double)(float)actorState[0x278], (double)(float)actorState[0x278]);
        int off = 0;
        for (int i = 0; i < *(int*)(*(int*)(*(int*)(entity + 4) + 4) + 4); ++i) {
            float* boneMtx = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + off);
            mtx4_multiply(scaleMtx, boneMtx, boneMtx);
            off += 0x40;
        }
        actorState[0x278] = (int)(FLOAT_806190ec * FLOAT_806108ec * FLOAT_806118ec + (float)actorState[0x278]);
        float scaleVal = (float)actorState[0x278];
        double pitch = (double)(float)((double)FLOAT_80618fd0 + (double)(float)((double)scaleVal / (double)FLOAT_80619064));
        sound_update_3d_source((double)(float)(pitch - (double)FLOAT_80619038), (double)FLOAT_806190f0,
                               (double)FLOAT_80618fb8, entity + 0x22c, entity + 100, 0x630, 0);
        if (-1 < *(int*)(entity + 0x22c)) {
            sound_set_pitch(pitch, *(unsigned int*)(entity + 0x22c));
        }
        if (anim_is_clip_active(entity, 0x76d) == 0) {
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
        } else {
            double ph;
            if (anim_slot_is_primary_clip(entity, 0x76d) != 0 &&
                (ph = anim_primary_clip_phase(*(int*)(entity + 4)), (double)FLOAT_80618fd0 < ph)) {
                float pos[3];
                pos[0] = *(float*)(entity + 100) + *(float*)(entity + 0x1a8);
                pos[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
                pos[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
                float dir[3];
                dir[0] = FLOAT_80618f98; dir[1] = FLOAT_80618fb8; dir[2] = FLOAT_80618f98;
                long long h = (long long)player_ai_explosion_spawn(0x2c, 0x17, pos, dir, entity, entity, 1);
                FUN_80284ba4(h, (double)pos[2], (double)pos[2], (double)scaleVal, (double)FLOAT_80618fd0,
                             0, 0, 0, entity);
                __restore_gpr_28();
                return;
            }
        }
        aiactor_turn_toward_leader();
    } else if (state < 4) {
        if (anim_is_clip_active(entity, 0x76f) == 0) {
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
        }
        if ((float)actorState[0x2bd] <= FLOAT_80619050) {
            aiactor_melee_swipe(entity, 0, 0);
            aiactor_turn_toward_leader();
        } else {
            if (actorState[8] != 1) {
                actorState[0x275] = actorState[8];
                actorState[8] = 1;
                actorState[0x276] = (int)FLOAT_80618f98;
                actorState[0x274] = actorState[8];
            }
            unsigned int r = rng_counter_tick();
            actorState[0x278] = (int)(FLOAT_80618fdc *
                ((float)((double)(long long)(unsigned int)r - DOUBLE_80618fe8) / FLOAT_80618fd8)
                + FLOAT_80618fdc + (float)actorState[0x2cc]);
        }
    }
    __restore_gpr_28();
}

// 0x8026ba94 : self-destruct / kamikaze charge tick. State 0 arms (stance 4,
// random windup on +0x9e0). State 1 waits for the windup to elapse, then trips
// to state 2. State 2 detonates on the active anim event. States >=2 also spawn
// the explosion effect once the fire-anim user flag fires.
void aiactor_kamikaze_tick(int entity) {
    int actorState = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = *(int*)(actorState + 0x9d0);
        }
        *(int*)(actorState + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        *(float*)(actorState + 0xa90) = FLOAT_806108ec *
            (FLOAT_80618fb8 / (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8))
            + *(float*)(actorState + 0xa90);
    }

    int state = *(int*)(actorState + 0x20);
    if (state == 2) {
        if (anim_active_slot_userflags(*(int*)(entity + 4)) != 0) {
            if (*(int*)(actorState + 0x20) != 1) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 1;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
            unsigned int r = rng_counter_tick();
            *(float*)(actorState + 0x9e0) = FLOAT_80619040 *
                ((float)((double)(long long)(unsigned int)r - DOUBLE_80618fe8) / FLOAT_80618fd8)
                + FLOAT_80619040 + *(float*)(actorState + 0xb30);
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
        }
    } else if (state < 2) {
        if (state == 0) {
            aiactor_set_stance_flags(entity, 4);
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
            if (*(int*)(actorState + 0x20) != 1) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 1;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
            unsigned int r = rng_counter_tick();
            *(float*)(actorState + 0x9e0) = FLOAT_80619040 *
                ((float)((double)(long long)(unsigned int)r - DOUBLE_80618fe8) / FLOAT_80618fd8)
                + FLOAT_80619040 + *(float*)(actorState + 0xb30);
        } else if (-1 < state) {
            if (*(float*)(actorState + 0xb30) <= *(float*)(actorState + 0x9e0)) {
                if (anim_is_clip_active(entity, 0x765) == 0 &&
                    anim_active_slot_userflags(*(int*)(entity + 4)) != 0) {
                    anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                }
            } else {
                if (state != 2) {
                    *(int*)(actorState + 0x9d4) = state;
                    *(int*)(actorState + 0x20) = 2;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                }
                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
            }
        }
    } else if (state < 4 && anim_active_slot_userflags(*(int*)(entity + 4)) != 0) {
        float pos[3];
        pos[0] = *(float*)(entity + 100) + *(float*)(entity + 0x1a8);
        pos[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
        pos[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
        float dir[3];
        dir[0] = FLOAT_80618f98; dir[1] = FLOAT_80618fb8; dir[2] = FLOAT_80618f98;
        long long h = (long long)player_ai_explosion_spawn(0x2c, 0x17, pos, dir, entity, entity, 1);
        FUN_80284ba4(h, (double)pos[2], (double)pos[2], 0, 0, 0, 0, 0, entity);
    }
}

// 0x8026bd98 : mount damage / event dispatch. Event id 4 spawns the death
// explosion and latches the disabled flag. Event id 1 (a hit) with a valid
// damage record plays the hit-reaction (dodge left/right/back keyed on the hit
// bone Z vs the mesh extent), spawns an impact decal, and randomly plays an
// impact voice. Event 0x3ef on level 0x16 remaps a monitor handle.
void aiactor_mount_event_handler(unsigned int p1, unsigned int p2, int damageRec) {
    long long framePair = __save_gpr_28();
    int entity = (int)((unsigned long long)framePair >> 0x20);
    int event = (int)framePair;
    int actorState = *(int*)(entity + 0x7c);

    if (event == 4) {
        float pos[3];
        pos[0] = *(float*)(entity + 100) + *(float*)(entity + 0x1a8);
        pos[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
        pos[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
        float dir[3];
        dir[0] = FLOAT_80618f98; dir[1] = FLOAT_80618fb8; dir[2] = FLOAT_80618f98;
        player_ai_explosion_spawn(0x2c, 0x17, pos, dir, entity, entity, 1);
        *(unsigned int*)(actorState + 0x2c) = *(unsigned int*)(actorState + 0x2c) | 0x100000;
    } else if (event < 4) {
        if (event == 1 && damageRec != 0) {
            if (*(int*)(damageRec + 0x24) == 0x19) {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                if (*(int*)(actorState + 0x20) != 3) {
                    *(int*)(actorState + 0x9d0) = 3;
                }
            } else if (*(int*)(damageRec + 0x18) != 0x15) {
                int ctrl = *(int*)(entity + 4);
                float local[16];
                entity_build_local_matrix(local, entity, 1, 0);
                float* hitPos = *(float**)(damageRec + 0x28);
                float hp[3];
                hp[0] = hitPos[0]; hp[1] = hitPos[1]; hp[2] = hitPos[2];
                mtx4_inverse_transform_point(local, hp);
                if (anim_is_clip_active(entity, 0x763) == 0 &&
                    anim_is_clip_active(entity, 0x762) == 0 &&
                    anim_is_clip_active(entity, 0x764) == 0) {
                    int boneTbl = *(int*)(ctrl + 0x84);
                    int mtxBase = *(int*)(ctrl + 0x10);
                    int offA = *(int*)(boneTbl + 0x54) * 0x40;
                    int offB = *(int*)(boneTbl + 0x4c) * 0x40;
                    float span = FLOAT_8061903c *
                        ABS(*(float*)(mtxBase + 0x38 + offA) - *(float*)(mtxBase + 0x38 + offB));
                    if (span <= ABS(hp[2] - *(float*)(mtxBase + offB + 0x38))) {
                        if (span <= ABS(hp[2] - *(float*)(mtxBase + offA + 0x38))) {
                            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                        } else {
                            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                        }
                    } else {
                        anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
                    }
                }
                float jitter[3];
                double r1 = game_frand();
                jitter[1] = FLOAT_80618fb8;
                jitter[0] = FLOAT_80618fe0 * (float)((double)FLOAT_80618fd4 * r1 - (double)FLOAT_80618fb8);
                double r2 = game_frand();
                jitter[2] = FLOAT_80618fe0 * (float)((double)FLOAT_80618fd4 * r2 - (double)FLOAT_80618fb8);
                float lenSq = jitter[2] * jitter[2] + jitter[0] * jitter[0] + jitter[1] * jitter[1];
                if (FLOAT_80619080 < lenSq) {
                    float rs = 1.0f / (float)math_sqrt((double)lenSq);
                    rs = rs * -(FLOAT_80618fd0 * lenSq * rs * rs - FLOAT_80619040);
                    float scale = (float)((double)FLOAT_80618fb8 *
                        (double)(rs * -(FLOAT_80618fd0 * lenSq * rs * rs - FLOAT_80619040)));
                    jitter[0] = jitter[0] * scale;
                    jitter[1] = jitter[1] * scale;
                    jitter[2] = jitter[2] * scale;
                }
                unsigned int rr = rng_counter_tick();
                if (FLOAT_80618ffc <
                    (float)((double)(long long)(unsigned int)rr - DOUBLE_80618fe8) / FLOAT_80618fd8) {
                    unsigned int* sp = *(unsigned int**)(damageRec + 0x28);
                    unsigned int sndSel = rng_counter_tick();
                    int soundParams[7];
                    unsigned char soundFlag;
                    unsigned int s0, s1, s2v;
                    sound_init_play_params((int)soundParams);
                    soundFlag = 1;
                    s0 = sp[0]; s1 = sp[1]; s2v = sp[2];
                    soundParams[0] = (int)((sndSel & 3) + 0x63d);
                    (void)soundFlag; (void)s0; (void)s1; (void)s2v;
                    sound_play_request(soundParams);
                }
                FUN_80358fc4(0, 0, 0, *(float**)(damageRec + 0x28), jitter);
            }
        }
    } else if (event == 0x3ef && DAT_80612614 == 0x16) {
        zcsc_smd_handle_remap(entity, damageRec);
    }
    __restore_gpr_28();
}

// ============================================================================
// Address range: 0x8026c144 - 0x802706df | batch_745 | 2026-09-09
//
// Mounted/creature ("beast" seat) AI behaviour cluster - continuation of the
// AI-behaviour tick family above. These ticks drive an actor that is riding a
// mount or is itself a rideable creature: they share the same behaviour-driver
// prologue (resolve scratch frame FUN_803826f0, apply pending-state swap
// +0x9d0/+0x9d4/+0x9d8, advance loco timer +0xa90) but select animation clips
// from per-mount clip-descriptor tables in the 0x80472xxx block, indexed by a
// "mount variant" selector bVar3 (0 = base rig, 1 = alt rig) computed from the
// actor's attachment slot +0x204 against the anim-state table DAT_80610ba0.
//
// Clip-id descriptor tables (each entry is an int clip id; [variant] selects
// base vs alt rig): DAT_80472790 idle, _7b0 mount, _820/_830/_840 ride,
// _850/_860/_870 walk, _880..._900 combat, _910..._930 turn, _940/_950/_960
// react, _970 dismount. Command tables DAT_80472718/_71c/_720 (clip/threshold/
// flag triples) and DAT_80472760/_764/_770 (fallback seat-command record).
//
// Functions:
//   aiactor_mount_clip_gate_tick        0x8026c144  2-state clip-0x643 gate
//   aiactor_mount_follow_tick           0x8026c33c  3-state escort + take-cover
//   aiactor_mount_patrol_zone_tick      0x8026c808  named-zone patrol FSM
//   aiactor_mount_behavior_main_tick    0x8026ce28  9-state main mount FSM
//   aiactor_clamp_value_to_range        0x8026ffc0  clamp value into [lo,hi] arc
//   aiactor_wrap_angle_delta            0x80270018  wrap (b-a) into signed range
//   aiactor_mount_retarget_zone_event   0x80270054  state1 -> retarget + state6
//   aiactor_mount_movement_command_tick 0x802700ec  seat movement-command driver
//   aiactor_mount_los_engage_gate       0x80270488  target LOS -> engage/charge
//   aiactor_mount_proximity_react       0x802706e0  proximity reaction gate
// ----------------------------------------------------------------------------

// ---- additional named helpers referenced by this batch ---------------------
extern "C" {
extern int  aiactor_nav_advance_path(int entity, int* zoneIdInOut);
extern void aiactor_handle_blocked_path(int entity);
extern int  aiactor_alert_group_flagB_any(unsigned int alertKey);
extern int  anim_slot_is_secondary_clip(int entity, int clipId);
extern int  anim_event_find_nth_by_mask(int clipId, int mask, int nth);
extern void anim_compute_root_velocity(double scaledDt, double curSpeed, double distDelta,
                                       double invDt, int frame, int mode);
extern void worm_set_pending_state_gated(int actorState, int r4, int flagsA, int flagsB);

// ---- raw FUN_ helpers referenced at these call sites -----------------------
extern double FUN_80390324(double x);           // fractional/mod helper
// aiactor_mount_retarget_nearest_command / aiactor_mount_scan_alert_target defined below in batch_747
// (aiactor_mount_retarget_nearest_command / aiactor_mount_scan_alert_target)
// NOTE: FUN_802722fc/_713b4/_70870/_71cc8/_71e20/_720f8 are defined below in
// batch_746 as the aiactor_mount_command_* engine; batch_745 call sites were
// updated to the descriptive names.

// ---- mount clip-descriptor / command tables (int clip ids, [variant]) ------
extern int DAT_80472718;  extern int DAT_8047271c;  extern int DAT_80472720;
extern int DAT_80472760;  extern int DAT_80472764;  extern int DAT_80472770;
extern int DAT_80472790;  extern int DAT_804727b0;
extern int DAT_80472820;  extern int DAT_80472830;  extern int DAT_80472840;
extern int DAT_80472850;  extern int DAT_80472860;  extern int DAT_80472870;
extern int DAT_80472880;  extern int DAT_80472890;  extern int DAT_804728a0;
extern int DAT_804728b0;  extern int DAT_804728c0;  extern int DAT_804728d0;
extern int DAT_804728e0;  extern int DAT_804728f0;  extern int DAT_80472900;
extern int DAT_80472910;  extern int DAT_80472920;  extern int DAT_80472930;
extern int DAT_80472940;  extern int DAT_80472950;  extern int DAT_80472960;
extern int DAT_80472970;

// ---- tuning constants (mount clip-blend rates, ranges, angle limits) -------
extern float  FLOAT_806190f4;
extern float  FLOAT_806190f8;   // 0.0 blend/reset
extern float  FLOAT_806190fc;   // 1.0 playback rate
extern double DOUBLE_80619150;  // 4503599627370496.0 (2^52 int->float bias)
extern float  FLOAT_80619100;
extern float  FLOAT_80619104;
extern float  FLOAT_80619108;
extern float  FLOAT_8061910c;
extern float  FLOAT_80619110;
extern float  FLOAT_80619114;
extern float  FLOAT_80619118;
extern float  FLOAT_8061911c;
extern float  FLOAT_80619120;
extern float  FLOAT_80619124;
extern float  FLOAT_80619128;
extern float  FLOAT_8061912c;
extern float  FLOAT_80619130;
extern float  FLOAT_80619134;
extern float  FLOAT_80619138;
extern float  FLOAT_8061913c;
extern float  FLOAT_80619140;
extern float  FLOAT_80619144;
extern float  FLOAT_80619148;   // wrap range lower bound
extern float  FLOAT_8061914c;   // wrap range upper bound
extern double DOUBLE_80619158;
extern float  FLOAT_80619160;   // wrap step (full turn)
}

// ---- intra-batch forward declarations (mutually recursive helpers) ---------
void   aiactor_mount_retarget_zone_event(int entity, int otherEntity);
double aiactor_clamp_value_to_range(double v, double lo, double hi);
float  aiactor_wrap_angle_delta(double a, double b);
void   aiactor_mount_movement_command_tick(void);
int    aiactor_mount_los_engage_gate(void);
void   aiactor_mount_proximity_react(void);

// ----------------------------------------------------------------------------
// aiactor_mount_clip_gate_tick 0x8026c144
// Minimal 2-state gate used by a mounted actor: state 1 plays gate clip 0x643
// and, on its userflag, commits the queued action pair (+0xabc/+0xab8) into the
// live stance-request pair (+0xaac/+0xaa8) once the coop/vs input bit allows;
// state <= 0 blends into state 1 via anim_play_scaled. Shares the standard
// pending-state-swap + loco-timer prologue.
// ----------------------------------------------------------------------------
void aiactor_mount_clip_gate_tick(int entity)
{
    int actorState = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = *(int*)(actorState + 0x9d0);
        }
        *(int*)(actorState + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        *(float*)(actorState + 0xa90) =
            FLOAT_806108ec *
            (FLOAT_80618fb8 /
             (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8)) +
            *(float*)(actorState + 0xa90);
    }

    int state = *(int*)(actorState + 0x20);
    if (state == 1) {
        if (anim_primary_clip_userflags(entity, 0x643) == 0) {
            if ((anim_is_clip_active(entity, 0x643) == 0) && (*(int*)(actorState + 0x20) != 0)) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 0;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
        } else {
            if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
                unsigned int inMask = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                                             : *(unsigned int*)(actorState + 0x1c);
                if ((inMask & 1) == 0) {
                    return;
                }
            }
            *(int*)(actorState + 0xaac) = *(int*)(actorState + 0xabc);
            *(int*)(actorState + 0xaa8) = *(int*)(actorState + 0xab8);
        }
    } else if (((state < 1) && (-1 < state)) &&
               (anim_play_scaled((double)FLOAT_8061903c, (double)FLOAT_80618fb8) == 1) &&
               (*(int*)(actorState + 0x20) != 1)) {
        *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
        *(int*)(actorState + 0x20) = 1;
        *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
        *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
    }
}

// ----------------------------------------------------------------------------
// aiactor_mount_follow_tick 0x8026c33c
// 3-state escort/follow tick for a mounted actor. Mirrors the leader's nav
// target (+0x30..+0x38 <- leader +0x64/+0x68/+0x6c), tracks the leader's stored
// zone (+0x684) and clears the nav-progress pair (+0x9a8/+0x9ac) when it moves.
//   state 1: play locomotion clip 0x6fd/0x6fe (or blend from 0x6ffe) and, on a
//            low behaviour timer +0xaf4, drop to state 2 (halt).
//   state 0..: re-acquire nearest target then pick state 1 (move) or 2 (hold).
//   state 2: play halt clip 0x62b/0x62c and rise to state 1 when +0xaf4 grows.
// Then aiactor_leader_zone_update + a take-cover queue (action 0x10) gated by
// alert-bit +0x28.1 / timer +0xb30 and the coop input bit.
// ----------------------------------------------------------------------------
void aiactor_mount_follow_tick(void)
{
    int frame = FUN_803826f0();
    int actorState = *(int*)(frame + 0x7c);
    int noTeammate = aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = *(int*)(actorState + 0x9d0);
        }
        *(int*)(actorState + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        *(float*)(actorState + 0xa90) =
            FLOAT_806108ec *
            (FLOAT_80618fb8 /
             (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8)) +
            *(float*)(actorState + 0xa90);
    }

    int leader = *(int*)(actorState + 4);
    *(int*)(actorState + 0x30) = *(int*)(leader + 100);
    *(int*)(actorState + 0x34) = *(int*)(leader + 0x68);
    *(int*)(actorState + 0x38) = *(int*)(leader + 0x6c);

    int leaderZone = *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 0x678);
    if (leaderZone != *(int*)(actorState + 0x684)) {
        if (leaderZone < 0) {
            *(int*)(actorState + 0x684) =
                *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        } else {
            *(int*)(actorState + 0x684) = leaderZone;
        }
        *(int*)(actorState + 0x9ac) = 0;
        *(int*)(actorState + 0x9a8) = 0;
    }

    int state = *(int*)(actorState + 0x20);
    if (state == 1) {
        if ((anim_is_clip_active(frame, 0x6fe) == 0) &&
            (anim_is_clip_active(frame, 0x6fd) == 0)) {
            anim_play_scaled((double)FLOAT_8061903c, (double)FLOAT_80618fb8);
        } else if (anim_primary_clip_userflags(frame, 0x6fd) == 0) {
            if (anim_active_slot_userflags(*(int*)(frame + 4)) != 0) {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                *(unsigned int*)(actorState + 0x2c) |= 0x400;
            }
        } else {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        }
        if ((*(float*)(actorState + 0xaf4) < FLOAT_80619008) && (*(int*)(actorState + 0x20) != 2)) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = 2;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
        }
        aiactor_facing_update();
    } else if (state < 1) {
        if (-1 < state) {
            *(int*)(actorState + 4) =
                aiactor_acquire_nearest_target(frame, *(int*)(DAT_80611d78 + 0x14), 0);
            if (FLOAT_80619008 <= *(float*)(actorState + 0xaf4)) {
                if (*(int*)(actorState + 0x20) != 1) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(int*)(actorState + 0x20) = 1;
                    *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                    *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                }
            } else if (*(int*)(actorState + 0x20) != 2) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 2;
                *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
        }
    } else if (state < 3) {
        if ((anim_is_clip_active(frame, 0x62b) == 0) &&
            (anim_is_clip_active(frame, 0x62c) == 0)) {
            anim_play_scaled((double)FLOAT_8061903c, (double)FLOAT_80618fb8);
        } else if (anim_primary_clip_userflags(frame, 0x62c) == 0) {
            if (anim_active_slot_userflags(*(int*)(frame + 4)) != 0) {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
                *(unsigned int*)(actorState + 0x2c) |= 0x400;
            }
        } else {
            anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
        }
        if ((FLOAT_806190f4 < *(float*)(actorState + 0xaf4)) && (*(int*)(actorState + 0x20) != 1)) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = 1;
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
        }
        aiactor_turn_toward_leader();
    }

    aiactor_leader_zone_update(frame);
    if ((*(unsigned int*)(actorState + 0x28) & 1) == 0) {
        if (noTeammate == 0) {
            if (FLOAT_80618fb8 < *(float*)(actorState + 0xb30)) {
                if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
                    unsigned int inMask = (DAT_805015bd == '\n')
                                              ? *(unsigned int*)(actorState + 0x14)
                                              : *(unsigned int*)(actorState + 0x1c);
                    if ((inMask & 1) == 0) { FUN_8038273c(); return; }
                }
                *(int*)(actorState + 0xaac) = 0x10;
                *(int*)(actorState + 0xaa8) = 0;
            }
        } else {
            *(float*)(actorState + 0xb30) = FLOAT_80618f98;
        }
    } else {
        if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
            unsigned int inMask = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                                         : *(unsigned int*)(actorState + 0x1c);
            if ((inMask & 1) == 0) { FUN_8038273c(); return; }
        }
        *(int*)(actorState + 0xaac) = 0x10;
        *(int*)(actorState + 0xaa8) = 0;
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// aiactor_mount_patrol_zone_tick 0x8026c808
// Named-zone patrol/scripted-move FSM for a mounted actor. The seat command
// slot (+0x08) selects a named destination zone via soundpad_find_zone_by_id
// (0x49c/0x3eb/0x49f/0x47e/0x47d/0x3e9) and a matching locomotion clip id.
//   state 2: choose destination + clip by command, then aiactor_follow_zone_path
//            -> movement tick / facing, or request state 1 on arrival.
//   state 0/1: play mount clip 0x16c, teleport to command zone if drifted, then
//            turn toward the command/leader facing.
//   state 4: cooldown/idle via anim descriptor triple DAT_80472718/_71c/_720
//            indexed by frame +0x1a0, drop to state 2 when timer/flags satisfied.
//   state 3..: follow_zone_path with per-command clip, else rng-pick a new idle
//            sub-index (state 4) and blend.
// ----------------------------------------------------------------------------
void aiactor_mount_patrol_zone_tick(long long param_1, double param_2, double param_3,
                                    double param_4, double param_5, double param_6,
                                    double param_7, long long param_8)
{
    int frame = FUN_803826f0();
    int actorState = *(int*)(frame + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_80618f98;
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = *(int*)(actorState + 0x9d0);
        }
        *(int*)(actorState + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_80618f98;
    } else {
        *(float*)(actorState + 0xa90) =
            (float)((double)FLOAT_806108ec *
                        (double)(float)((double)FLOAT_80618fb8 /
                                        (double)(float)((double)(int)(DAT_806109c0 ^ 0x80000000) -
                                                        DOUBLE_80618fc8)) +
                    (double)*(float*)(actorState + 0xa90));
    }

    int state = *(int*)(actorState + 0x20);
    if (state == 2) {
        int cmd = *(int*)(actorState + 8);
        int clip = 0x2a;
        int zone;
        if (cmd == 3) {
            zone = soundpad_find_zone_by_id(0x49c);
            if (zone < 0)
                *(int*)(actorState + 0x684) =
                    *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int*)(actorState + 0x684) = zone;
            clip = 0x113;
            *(int*)(actorState + 0x9ac) = 0;
            *(int*)(actorState + 0x9a8) = 0;
        } else if (cmd < 3) {
            if (cmd == 1) {
                zone = soundpad_find_zone_by_id(0x3eb);
                if (zone < 0)
                    *(int*)(actorState + 0x684) =
                        *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int*)(actorState + 0x684) = zone;
                clip = 0x171;
                *(int*)(actorState + 0x9ac) = 0;
                *(int*)(actorState + 0x9a8) = 0;
            } else if (0 < cmd) {
                zone = soundpad_find_zone_by_id(0x49f);
                if (zone < 0)
                    *(int*)(actorState + 0x684) =
                        *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int*)(actorState + 0x684) = zone;
                clip = 0x2a;
                *(int*)(actorState + 0x9ac) = 0;
                *(int*)(actorState + 0x9a8) = 0;
            }
        } else if (cmd == 5) {
            zone = soundpad_find_zone_by_id(0x47e);
            if (zone < 0)
                *(int*)(actorState + 0x684) =
                    *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int*)(actorState + 0x684) = zone;
            clip = 0x2a;
            *(int*)(actorState + 0x9ac) = 0;
            *(int*)(actorState + 0x9a8) = 0;
        } else if (cmd < 5) {
            zone = soundpad_find_zone_by_id(0x47d);
            if (zone < 0)
                *(int*)(actorState + 0x684) =
                    *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int*)(actorState + 0x684) = zone;
            clip = 0x6bf;
            *(int*)(actorState + 0x9ac) = 0;
            *(int*)(actorState + 0x9a8) = 0;
        }

        if (aiactor_follow_zone_path() == 0) {
            long long moveArg = param_1;
            if (anim_is_clip_active(frame, clip) == 0) {
                param_2 = (double)FLOAT_80618fb8;
                moveArg = (long long)anim_play_scaled((double)FLOAT_80618ff0, param_2);
            }
            aiactor_movement_tick(moveArg, param_2, param_3, param_4, param_5, param_6,
                                  param_7, param_8);
            aiactor_facing_update();
        } else if (*(int*)(actorState + 0x20) != 1) {
            *(int*)(actorState + 0x9d0) = 1;
        }
    } else if (state < 2) {
        if (state == 0) {
            *(int*)(actorState + 0x9d0) = 1;
        } else if (-1 < state) {
            if (anim_is_clip_active(frame, 0x16c) == 0) {
                anim_play_scaled((double)FLOAT_80618f98, (double)FLOAT_80618fb8);
            }
            int cmd = *(int*)(actorState + 8);
            int zone = -1;
            if (cmd == 3)          zone = soundpad_find_zone_by_id(0x49c);
            else if (cmd < 3) {
                if (cmd == 1)      zone = soundpad_find_zone_by_id(0x3eb);
                else if (0 < cmd)  zone = soundpad_find_zone_by_id(0x49f);
            } else if (cmd == 5)   zone = soundpad_find_zone_by_id(0x47e);
            else if (cmd < 5)      zone = soundpad_find_zone_by_id(0x47d);

            if (*(int*)(actorState + 0x678) != zone) {
                player_ai_teleport_wrapper(actorState, zone, 1);
            }
            int homeZone = soundpad_find_zone_by_id(0x3e9);
            if (homeZone < 0)
                *(int*)(actorState + 0x684) =
                    *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int*)(actorState + 0x684) = homeZone;
            *(int*)(actorState + 0x9ac) = 0;
            *(int*)(actorState + 0x9a8) = 0;
            if (*(int*)(actorState + 0xacc) == 0) {
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(actorState + 0x67c) + 0x18),
                                          frame, 0);
            } else {
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(actorState + 0xacc) + 0x18),
                                          frame, 0);
            }
        }
    } else if (state == 4) {
        int idx = *(int*)(frame + 0x1a0) * 0xc;
        if ((&DAT_80472720)[idx / 4] == 0) {
            if ((&DAT_8047271c)[idx / 4] <= *(int*)(actorState + 0x9d8)) {
                *(int*)(actorState + 0x9d0) = 2;
                FUN_8038273c();
                return;
            }
        } else if (anim_active_slot_userflags(*(int*)(frame + 4)) != 0) {
            if (*(int*)(actorState + 0x20) != 2) {
                *(int*)(actorState + 0x9d0) = 2;
            }
            FUN_8038273c();
            return;
        }
        if (anim_is_clip_active(frame, (&DAT_80472718)[*(int*)(frame + 0x1a0) * 3]) == 0) {
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
        }
    } else if (state < 4) {
        int clip = 0x2a;
        long long moveArg = param_1;
        if (aiactor_follow_zone_path() == 0) {
            int cmd = *(int*)(actorState + 8);
            if (cmd == 3)          clip = 0x113;
            else if (cmd < 3) {
                if (cmd == 1)      clip = 0x171;
                else if (0 < cmd)  clip = 0x2a;
            } else if (cmd == 5)   clip = 0x2a;
            else if (cmd < 5)      clip = 0x6bf;
        } else {
            unsigned int r = rng_counter_tick();
            param_3 = (double)(float)((double)(int)r - DOUBLE_80618fe8);
            float pick = FLOAT_80618fdc * (float)(param_3 / (double)FLOAT_80618fd8) + FLOAT_80618fd0;
            if (*(int*)(actorState + 0x20) != 4) {
                *(int*)(actorState + 0x9d0) = 4;
            }
            *(int*)(frame + 0x1a0) = (int)pick;
            param_2 = (double)FLOAT_80618fb8;
            moveArg = (long long)anim_play_scaled((double)FLOAT_80618ff0, param_2);
        }
        if (anim_is_clip_active(frame, clip) == 0) {
            param_2 = (double)FLOAT_80618fb8;
            moveArg = (long long)anim_play_scaled((double)FLOAT_80618ff0, param_2);
        }
        aiactor_movement_tick(moveArg, param_2, param_3, param_4, param_5, param_6,
                              param_7, param_8);
        aiactor_facing_update();
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// mount_clip_variant (helper for 0x8026ce28)
// The mount's clip tables are laid out as [base, alt] pairs; this selects index
// 0 (alt rig) when the actor has a live attachment slot (+0x204) whose anim
// state (DAT_80610ba0 record, stride 0x29c, flag bit0 at +0xc) is set, else 1
// (base rig). Extracted from the ~30 inlined copies in the original.
// ----------------------------------------------------------------------------
static int mount_clip_variant(int actorState)
{
    int slot = *(int*)(actorState + 0x204);
    if ((slot == -1) ||
        ((*(unsigned int*)(DAT_80610ba0 + slot * 0x29c + 0xc) & 1) != 0)) {
        return 1;
    }
    return 0;
}

// case-7 (pursue/circle) and case-8 (turn-in-place) bodies of the main mount
// FSM, split out to keep aiactor_mount_behavior_main_tick readable. Both operate
// on the resolved frame + actorState and match the original switch arms.
static void aiactor_mount_pursue_state(int frame, int actorState)
{
    int bVar3 = mount_clip_variant(actorState);
    int active = anim_is_clip_active(frame, (&DAT_80472910)[bVar3]);
    if (active == 0) {
        int* t2[5] = { &DAT_80472920, &DAT_80472930, &DAT_804728b0, &DAT_804728a0, &DAT_80472890 };
        for (int i = 0; i < 5; ++i) {
            bVar3 = mount_clip_variant(actorState);
            if (anim_is_clip_active(frame, t2[i][bVar3]) != 0) { active = 1; break; }
        }
    }
    if (active == 0) {
        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
    } else {
        bVar3 = mount_clip_variant(actorState);
        int contact = anim_primary_clip_userflags(frame, (&DAT_804728b0)[bVar3]);
        if (contact == 0) {
            // approach/steer toward the target zone
            int z = soundpad_find_nearest_entity((double)FLOAT_806190f8, (float*)(frame + 100));
            if (z != *(int*)(actorState + 0x678)) {
                *(int*)(actorState + 0x678) = z;
                *(int*)(actorState + 0x9ac) = 0;
                *(int*)(actorState + 0x9a8) = 0;
            }
            int next = aiactor_nav_advance_path(frame, (int*)(actorState + 0x678));
            int* zn;
            if (-1 < next) {
                zn = soundpad_resolve_zone_node(next);
                double yaw = prop_hinge_wrap_angle_to_range((double)*(float*)(frame + 0x70),
                                                            (float*)(frame + 100), (float*)(zn + 3));
                if ((double)FLOAT_80619124 < ABS(yaw)) {
                    anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                    return;
                }
            }
            if (next == -1) {
                zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x678));
                double yaw = prop_hinge_wrap_angle_to_range((double)*(float*)(frame + 0x70),
                                                            (float*)(frame + 100), (float*)(zn + 3));
                if ((double)FLOAT_80619124 < ABS(yaw)) {
                    anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                    return;
                }
            }
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        } else {
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        }
    }

    aiactor_mount_command_nav_tick();
    bVar3 = mount_clip_variant(actorState);
    if ((anim_is_clip_active(frame, (&DAT_80472890)[bVar3]) != 0) ||
        (bVar3 = mount_clip_variant(actorState),
         anim_is_clip_active(frame, (&DAT_804728a0)[bVar3]) != 0)) {
        soundpad_get_entity_id_from_zone(*(int*)(actorState + 0x684));
        aiactor_mount_steer_to_path_node();
    }
    bVar3 = mount_clip_variant(actorState);
    if ((anim_is_clip_active(frame, (&DAT_80472910)[bVar3]) != 0) ||
        (bVar3 = mount_clip_variant(actorState),
         anim_is_clip_active(frame, (&DAT_80472920)[bVar3]) != 0)) {
        int* zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x684));
        if (ABS((float)zn[4] - *(float*)(frame + 0x68)) < FLOAT_80619110) {
            float dz = *(float*)(frame + 0x6c) - (float)zn[5];
            float dx = *(float*)(frame + 100) - (float)zn[3];
            if (((float)(dz * dz + dx * dx) < FLOAT_80619138) &&
                (anim_event_scan_active() != 0)) {
                *(unsigned int*)(actorState + 0x28) &= 0xfffffbff;
                aiactor_mount_movement_command_tick();
            }
        }
    }
}

static void aiactor_mount_turn_in_place_state(int frame, int actorState, int curCmd)
{
    int bVar3 = mount_clip_variant(actorState);
    int active = anim_is_clip_active(frame, (&DAT_80472970)[bVar3]);
    if (active == 0) {
        bVar3 = mount_clip_variant(actorState);
        active = anim_is_clip_active(frame, (&DAT_804727b0)[bVar3]);
        if ((active == 0) && (aiactor_mount_turn_clip_active() == 0)) {
            anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
            return;
        }
    }
    bVar3 = mount_clip_variant(actorState);
    if ((anim_primary_clip_userflags(frame, (&DAT_80472970)[bVar3]) == 0) &&
        (bVar3 = mount_clip_variant(actorState),
         anim_primary_clip_userflags(frame, (&DAT_804727b0)[bVar3]) == 0)) {
        if (aiactor_mount_turn_clip_active() != 0) {
            if (anim_active_slot_userflags(*(int*)(frame + 4)) == 0) {
                double add = aiactor_mount_clip_aim_offset(frame);
                double delta = aiactor_clamp_value_to_range(
                    (double)(*(float*)(actorState + 0x9e0) -
                             (float)((double)*(float*)(frame + 0x70) + add)),
                    (double)FLOAT_80619148, (double)FLOAT_8061914c);
                if ((double)FLOAT_80619100 < ABS(delta)) {
                    double endT = anim_get_clip_end_time_of(frame);
                    unsigned int steps =
                        (unsigned int)((float)((double)(int)(DAT_806109c0 ^ 0x80000000) -
                                               DOUBLE_80619150) *
                                       (float)(endT - (double)*(float*)(*(int*)(frame + 4) + 0xd4)));
                    if (0 < (int)steps) {
                        float rate = (float)(delta /
                                             (double)(float)((double)(int)(steps ^ 0x80000000) -
                                                             DOUBLE_80619150));
                        if (FLOAT_80619110 < rate) rate = FLOAT_80619110;
                        *(float*)(frame + 0xc4) = *(float*)(frame + 0xc4) + rate;
                    }
                }
            } else {
                anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
            }
        }
        return;
    }
    double heading = (double)aiactor_wrap_angle_delta((double)*(float*)(actorState + 0x9e0),
                                                      (double)*(float*)(frame + 0x70));
    if (ABS(heading) <= (double)FLOAT_80619140) {
        if (ABS(heading) <= (double)FLOAT_80619144) {
            (void)curCmd; // both branches request state 1 in the original
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
            if (*(int*)(actorState + 0x20) != 1) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 1;
                *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
        } else {
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        }
    } else {
        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
    }
}

// ----------------------------------------------------------------------------
// aiactor_mount_behavior_main_tick 0x8026ce28
// Primary 9-state (0..8) behaviour FSM for a rideable creature/mount. Resolves
// the active seat-command record (level-def table DAT_8061208c[+0x34] indexed by
// char-def +0x68, or the fallback record DAT_80472760/_764/_770 when the seat is
// unassigned -2), then dispatches on +0x20:
//   0 idle/dispatch   1 spawn/board   2 approach-target   3 attack-windup
//   4 attack-loop     5 charge/melee  6 recoil/return     7 pursue/circle
//   8 turn-in-place
// Each state selects clips from the DAT_80472790..0x970 tables via
// mount_clip_variant, drives locomotion / root-velocity blending, and issues
// alert broadcasts (aiactor_alert_broadcast_group) and pending-state requests
// (worm_set_pending_state_gated). Uses the seat-command sub-driver
// aiactor_mount_movement_command_tick and proximity gate helpers below.
// ----------------------------------------------------------------------------
void aiactor_mount_behavior_main_tick(long long param_1, double param_2, double param_3,
                                      double param_4, double param_5, double param_6,
                                      long long param_7, long long param_8)
{
    int frame = __save_gpr_25();
    int actorState = *(int*)(frame + 0x7c);
    int noTeammate = aiactor_retarget_visible_teammate();
    int bVar3;

    if (-1 < *(int*)(actorState + 0x9d0)) {
        if (*(int*)(actorState + 0x9d0) != *(int*)(actorState + 0x20)) {
            *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = *(int*)(actorState + 0x9d0);
        }
        *(int*)(actorState + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(actorState + 0x2c) & 0x80) == 0) {
        *(float*)(actorState + 0xa90) = FLOAT_806190f8;
    } else {
        *(float*)(actorState + 0xa90) =
            (float)((double)FLOAT_806108ec *
                        (double)(float)((double)FLOAT_806190fc /
                                        (double)(float)((double)(int)(DAT_806109c0 ^ 0x80000000) -
                                                        DOUBLE_80619150)) +
                    (double)*(float*)(actorState + 0xa90));
    }
    *(unsigned int*)(actorState + 0x28) |= 0x4000;

    // Resolve the active seat-command record.
    int* cmdRec;
    int seat = *(int*)(*(int*)(actorState + 0xbf0) + 0x68);
    if (seat == -2) {
        DAT_80472764 = *(int*)(*(int*)(actorState + 0xbf0) + 0x10);
        cmdRec = &DAT_80472760;
        DAT_80472770 = DAT_80472764;
    } else {
        cmdRec = *(int**)(*(int*)(DAT_8061208c + 0x34) + seat * 8);
    }

    if ((*(int*)(actorState + 4) == 0) || (*(int*)(actorState + 8) == DAT_806120f8)) {
        *(int*)(actorState + 4) =
            aiactor_acquire_nearest_target(frame, *(int*)(DAT_80611d78 + 0x14), 0);
    }

    int curCmd = cmdRec[*(int*)(actorState + 0xaa0) * 3];

    // Root-velocity blending when the current clip is active and not root-locked.
    bVar3 = mount_clip_variant(actorState);
    if ((anim_is_clip_active(frame, (&DAT_80472790)[bVar3]) != 0) &&
        ((*(unsigned int*)(*(int*)(frame + 4) + 0x120) & 0x20) == 0)) {
        int* zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x684));
        unsigned int nth = 0;
        float dx = *(float*)(frame + 100) - (float)zn[3];
        float dz = *(float*)(frame + 0x6c) - (float)zn[5];
        double distSq = (double)(dx * dx + dz * dz);
        int mode = aiactor_mount_next_command_opcode(frame, (int)cmdRec);
        if (mode == 2) {
            bVar3 = mount_clip_variant(actorState);
            nth = (unsigned int)anim_event_find_nth_by_mask((&DAT_80472790)[bVar3], 2, 1);
        }
        double reach = (double)aiactor_mount_command_reach_dist(mode, actorState);
        double dist = math_sqrt(distSq);
        double distDelta = (double)(float)(dist - reach);
        double invDt = (double)(FLOAT_806190fc /
                                (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80619150));
        double scaledDt = (double)(float)((double)(float)((double)(int)(nth ^ 0x80000000) -
                                                          DOUBLE_80619150) *
                                          (double)(FLOAT_80619100 /
                                                   (float)((double)(int)(DAT_806109c0 ^ 0x80000000) -
                                                           DOUBLE_80619150)));
        anim_compute_root_velocity(scaledDt, (double)*(float*)(actorState + 0xb08), distDelta,
                                   invDt, frame, 2);
    }

    switch (*(int*)(actorState + 0x20)) {
    case 0: {
        if ((*(unsigned int*)(actorState + 0xab4) & 0x200) == 0) {
            int cdef = *(int*)(actorState + 0xbf0);
            if ((*(unsigned int*)(cdef + 0x54) & 0x40000) == 0) {
                if ((*(unsigned int*)(cdef + 0x54) & 0x20) == 0) {
                    int z = soundpad_find_zone_by_id(cmdRec[*(int*)(actorState + 0xaa0) * 3 + 1]);
                    if (z < 0)
                        *(int*)(actorState + 0x684) =
                            *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else
                        *(int*)(actorState + 0x684) = z;
                    *(int*)(actorState + 0x9ac) = 0;
                    *(int*)(actorState + 0x9a8) = 0;
                    *(float*)(actorState + 0xb30) = FLOAT_80619108;
                    if (*(int*)(actorState + 0x20) != 1) {
                        *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                        *(int*)(actorState + 0x20) = 1;
                        *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
                        *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                    }
                } else {
                    if (*(int*)(cdef + 0x50) < 0) {
                        if (*(int*)(actorState + 4) == 0)
                            *(int*)(actorState + 0xac8) = *(int*)(DAT_80588610 + 0x678);
                        else
                            *(int*)(actorState + 0xac8) =
                                *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 0x678);
                    } else {
                        *(int*)(actorState + 0xac8) = soundpad_find_zone_by_id(*(int*)(cdef + 0x50));
                    }
                    *(float*)(actorState + 0xb30) = FLOAT_80619108;
                    if (*(int*)(*(int*)(actorState + 0xbf0) + 0x68) == -2) {
                        int z = soundpad_find_zone_by_id(cmdRec[*(int*)(actorState + 0xaa0) * 3 + 1]);
                        if (z < 0)
                            *(int*)(actorState + 0x684) =
                                *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                        else
                            *(int*)(actorState + 0x684) = z;
                        *(int*)(actorState + 0x9ac) = 0;
                        *(int*)(actorState + 0x9a8) = 0;
                        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                        if (*(int*)(actorState + 0x20) != 4) {
                            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                            *(int*)(actorState + 0x20) = 4;
                            *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
                            *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                        }
                    } else {
                        if (*(int*)(actorState + 0xac8) < 0)
                            *(int*)(actorState + 0x684) =
                                *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                        else
                            *(int*)(actorState + 0x684) = *(int*)(actorState + 0xac8);
                        *(int*)(actorState + 0x9ac) = 0;
                        *(int*)(actorState + 0x9a8) = 0;
                        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                        if (*(int*)(actorState + 0x20) != 5) {
                            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                            *(int*)(actorState + 0x20) = 5;
                            *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
                            *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                        }
                    }
                }
            } else {
                if (*(int*)(cdef + 0x50) < 0) {
                    if (*(int*)(actorState + 4) == 0)
                        *(int*)(actorState + 0xac8) = *(int*)(DAT_80588610 + 0x678);
                    else
                        *(int*)(actorState + 0xac8) =
                            *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 0x678);
                } else {
                    *(int*)(actorState + 0xac8) = soundpad_find_zone_by_id(*(int*)(cdef + 0x50));
                }
                *(float*)(actorState + 0xb30) = FLOAT_80619108;
                if (*(int*)(actorState + 0xac8) < 0)
                    *(int*)(actorState + 0x684) =
                        *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int*)(actorState + 0x684) = *(int*)(actorState + 0xac8);
                *(int*)(actorState + 0x9ac) = 0;
                *(int*)(actorState + 0x9a8) = 0;
                anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                if (*(int*)(actorState + 0x20) != 6) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(int*)(actorState + 0x20) = 6;
                    *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
                    *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                }
            }
        } else {
            anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
            aiactor_mount_retarget_nearest_command();
        }
        break;
    }
    case 1: {
        aiactor_mount_scan_alert_target();
        aiactor_mount_command_clip_tick();
        aiactor_mount_command_nav_tick();
        if ((*(unsigned int*)(actorState + 0x28) & 0x400) != 0) {
            if (*(int*)(actorState + 0xac8) < 0) {
                *(int*)(actorState + 0xac8) = *(int*)(DAT_80588610 + 0x678);
                if (*(int*)(actorState + 0xac8) < 0)
                    *(int*)(actorState + 0x684) =
                        *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int*)(actorState + 0x684) = *(int*)(actorState + 0xac8);
                *(int*)(actorState + 0x9ac) = 0;
                *(int*)(actorState + 0x9a8) = 0;
            } else {
                *(int*)(actorState + 0x684) = *(int*)(actorState + 0xac8);
                *(int*)(actorState + 0x9ac) = 0;
                *(int*)(actorState + 0x9a8) = 0;
            }
            *(float*)(actorState + 0xb30) = FLOAT_806190f8;
            if (noTeammate == 0) {
                anim_play_scaled((double)FLOAT_8061910c, (double)FLOAT_806190fc);
                if (*(int*)(actorState + 0x20) != 5) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(int*)(actorState + 0x20) = 5;
                    *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
                    *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                }
            } else if (*(int*)(actorState + 0x20) != 3) {
                *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                *(int*)(actorState + 0x20) = 3;
                *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
                *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
            }
        }
        aiactor_mount_proximity_react();
        if (curCmd == 0xb) {
            aiactor_destroy_instance((double)param_1, param_2, param_3, param_4, param_5, param_6,
                                     param_7, param_8, frame);
        }
        break;
    }
    case 2: {
        bVar3 = mount_clip_variant(actorState);
        int done = anim_primary_clip_userflags(frame, (&DAT_80472850)[bVar3]);
        if (done == 0) {
            bVar3 = mount_clip_variant(actorState);
            done = anim_primary_clip_userflags(frame, (&DAT_80472940)[bVar3]);
        }
        if (done != 0) {
            int tgt = *(int*)(actorState + 4);
            if (*(int*)(tgt + 0x20) == 8)
                *(int*)(actorState + 0xac8) = *(int*)(*(int*)(tgt + 0x7c) + 0x678);
            else
                *(int*)(actorState + 0xac8) = *(int*)(tgt + 0x110);
            if (noTeammate == 0) {
                if (*(int*)(actorState + 0xac8) < 0)
                    *(int*)(actorState + 0x684) =
                        *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int*)(actorState + 0x684) = *(int*)(actorState + 0xac8);
                *(int*)(actorState + 0x9ac) = 0;
                *(int*)(actorState + 0x9a8) = 0;
                *(float*)(actorState + 0xb30) = FLOAT_806190f8;
                anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                if (*(int*)(actorState + 0x20) != 5) {
                    *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                    *(int*)(actorState + 0x20) = 5;
                    *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
                    *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                }
            } else {
                unsigned int f54 = *(unsigned int*)(*(int*)(actorState + 0xbf0) + 0x54);
                if ((f54 & 8) == 0) {
                    if ((f54 & 0x4000) == 0) {
                        *(int*)(actorState + 0xad0) = *(int*)(actorState + 0xb14);
                        aiactor_alert_broadcast_group(0, frame, *(int*)(actorState + 0xac8));
                        worm_set_pending_state_gated(actorState, 0, 0, 4);
                    } else {
                        aiactor_alert_broadcast_group(0, frame, *(int*)(actorState + 0xac8));
                        worm_set_pending_state_gated(actorState, 0, 0, 0x200);
                    }
                } else {
                    aiactor_alert_broadcast_group(0, frame, *(int*)(actorState + 0xac8));
                    worm_set_pending_state_gated(actorState, 0, 0x4000000, 0);
                }
            }
        }
        if (noTeammate != 0) {
            aiactor_turn_toward_leader();
        }
        break;
    }
    case 3: {
        bVar3 = mount_clip_variant(actorState);
        int active = anim_is_clip_active(frame, (&DAT_80472850)[bVar3]);
        if (active == 0) {
            bVar3 = mount_clip_variant(actorState);
            active = anim_is_clip_active(frame, (&DAT_80472940)[bVar3]);
            if (active == 0) {
                bVar3 = mount_clip_variant(actorState);
                active = anim_is_clip_active(frame, (&DAT_80472950)[bVar3]);
                if (active == 0) {
                    bVar3 = mount_clip_variant(actorState);
                    active = anim_is_clip_active(frame, (&DAT_80472960)[bVar3]);
                    if (active == 0) {
                        anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
                        goto after_windup_play;
                    }
                }
                anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
            }
        }
    after_windup_play:
        bVar3 = mount_clip_variant(actorState);
        if (anim_slot_is_primary_clip(frame, (&DAT_80472850)[bVar3]) == 0) {
            bVar3 = mount_clip_variant(actorState);
            if (anim_slot_is_primary_clip(frame, (&DAT_80472940)[bVar3]) == 0)
                break;
        }
        if (*(int*)(actorState + 0x20) != 2) {
            *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
            *(int*)(actorState + 0x20) = 2;
            *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
            *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
        }
        break;
    }
    case 4: {
        bVar3 = mount_clip_variant(actorState);
        int active = anim_is_clip_active(frame, (&DAT_80472950)[bVar3]);
        if (active == 0) {
            bVar3 = mount_clip_variant(actorState);
            active = anim_is_clip_active(frame, (&DAT_80472940)[bVar3]);
            if (active == 0) {
                bVar3 = mount_clip_variant(actorState);
                active = anim_is_clip_active(frame, (&DAT_80472960)[bVar3]);
                if (active == 0) {
                    anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
                }
            }
        }
        bVar3 = mount_clip_variant(actorState);
        unsigned int prim = (unsigned int)anim_slot_is_primary_clip(frame, (&DAT_80472950)[bVar3]);
        if ((prim == 0) || (*(float*)(actorState + 0xb34) <= FLOAT_80619118)) {
            goto attack_loop_react;
        } else {
            bVar3 = mount_clip_variant(actorState);
            if (anim_is_clip_active(frame, (&DAT_80472960)[bVar3]) != 0)
                goto attack_loop_react;
            anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
        }
        goto attack_loop_tail;
    attack_loop_react:
        bVar3 = mount_clip_variant(actorState);
        if (anim_primary_clip_userflags(frame, (&DAT_80472960)[bVar3]) != 0) {
            unsigned int r = rng_counter_tick();
            *(float*)(actorState + 0xb34) =
                (float)((double)(int)r - DOUBLE_80619158) / FLOAT_8061911c;
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        }
    attack_loop_tail:
        if (((FLOAT_80619120 < *(float*)(actorState + 0xb30)) &&
             ((*(unsigned int*)(*(int*)(actorState + 0xbf0) + 0x54) & 0x20) == 0)) &&
            ((*(unsigned int*)(actorState + 0x28) & 0x10000) == 0)) {
            anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
            aiactor_mount_retarget_nearest_command();
        }
        aiactor_mount_proximity_react();
        break;
    }
    case 5: {
        aiactor_mount_command_nav_tick();
        // Charge/melee clip set: any of these active means we're mid-attack.
        int* chargeTables[8] = {
            &DAT_80472880, &DAT_804728e0, &DAT_804728f0, &DAT_80472900,
            &DAT_804728d0, &DAT_804728c0, &DAT_80472860, &DAT_80472870
        };
        int hit = 0;
        for (int i = 0; i < 8; ++i) {
            bVar3 = mount_clip_variant(actorState);
            if (anim_is_clip_active(frame, chargeTables[i][bVar3]) != 0) { hit = 1; break; }
        }
        if (hit == 0) {
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
            goto charge_settle;
        }
        // Windup / commit selection among the charge clip set.
        bVar3 = mount_clip_variant(actorState);
        if (anim_primary_clip_userflags(frame, (&DAT_80472880)[bVar3]) != 0) {
            int* zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x684));
            double yaw = prop_hinge_wrap_angle_to_range((double)*(float*)(frame + 0x70),
                                                        (float*)(frame + 100), (float*)(zn + 3));
            (void)yaw;
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        } else {
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        }
    charge_settle:
        // Melee-contact test: within height + planar radius -> deal hit (aiactor_mount_retarget_nearest_command).
        {
            int* zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x684));
            int close = 0;
            if (FLOAT_80619110 <= ABS((float)zn[4] - *(float*)(frame + 0x68))) {
                close = 0;
            } else {
                float dz = *(float*)(frame + 0x6c) - (float)zn[5];
                float dx = *(float*)(frame + 100) - (float)zn[3];
                if ((float)(dz * dz + dx * dx) < FLOAT_80619130) close = 1;
            }
            if (close && (anim_event_scan_active() != 0)) {
                bVar3 = mount_clip_variant(actorState);
                double phase = (anim_is_clip_active(frame, (&DAT_804728f0)[bVar3]) != 0)
                                   ? (double)FLOAT_80619104
                                   : (double)FLOAT_806190f8;
                anim_play_scaled(phase, (double)FLOAT_806190fc);
                aiactor_mount_retarget_nearest_command();
            }
        }
        aiactor_mount_proximity_react();
        break;
    }
    case 6: {
        aiactor_mount_command_nav_tick();
        bVar3 = mount_clip_variant(actorState);
        int active = anim_is_clip_active(frame, (&DAT_80472830)[bVar3]);
        if (active == 0) {
            bVar3 = mount_clip_variant(actorState);
            active = anim_is_clip_active(frame, (&DAT_80472820)[bVar3]);
            if (active == 0) {
                bVar3 = mount_clip_variant(actorState);
                active = anim_is_clip_active(frame, (&DAT_80472840)[bVar3]);
            }
        }
        if (active == 0) {
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        } else {
            bVar3 = mount_clip_variant(actorState);
            if (anim_primary_clip_userflags(frame, (&DAT_80472820)[bVar3]) == 0) {
                bVar3 = mount_clip_variant(actorState);
                if (anim_primary_clip_userflags(frame, (&DAT_80472840)[bVar3]) != 0) {
                    anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                    *(int*)(actorState + 0xac8) =
                        *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 0x678);
                    if (*(int*)(actorState + 0xac8) < 0)
                        *(int*)(actorState + 0x684) =
                            *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else
                        *(int*)(actorState + 0x684) = *(int*)(actorState + 0xac8);
                    *(int*)(actorState + 0x9ac) = 0;
                    *(int*)(actorState + 0x9a8) = 0;
                    *(float*)(actorState + 0xb30) = FLOAT_806190f8;
                    if (*(int*)(actorState + 0x20) != 5) {
                        *(int*)(actorState + 0x9d4) = *(int*)(actorState + 0x20);
                        *(int*)(actorState + 0x20) = 5;
                        *(float*)(actorState + 0x9d8) = FLOAT_806190f8;
                        *(int*)(actorState + 0x9d0) = *(int*)(actorState + 0x20);
                    }
                }
            } else {
                anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
            }
        }
        bVar3 = mount_clip_variant(actorState);
        if (anim_is_clip_active(frame, (&DAT_80472830)[bVar3]) != 0) {
            bVar3 = mount_clip_variant(actorState);
            if (anim_slot_is_secondary_clip(frame, (&DAT_80472840)[bVar3]) == 0) {
                int* zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x684));
                int reach = 0;
                if (FLOAT_80619110 <= ABS((float)zn[4] - *(float*)(frame + 0x68))) {
                    reach = ((*(unsigned int*)(actorState + 0x28) & 1) == 0);
                } else {
                    float dz = *(float*)(frame + 0x6c) - (float)zn[5];
                    float dx = *(float*)(frame + 100) - (float)zn[3];
                    if (FLOAT_80619114 <= (float)(dz * dz + dx * dx))
                        reach = ((*(unsigned int*)(actorState + 0x28) & 1) == 0);
                    else
                        reach = 1;
                }
                if (reach) {
                    if (*(int*)(actorState + 4) == 0)
                        aiactor_mount_retarget_nearest_command();
                    else
                        anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
                }
            }
        }
        aiactor_mount_proximity_react();
        break;
    }
    case 7: {
        aiactor_mount_pursue_state(frame, actorState);
        aiactor_mount_proximity_react();
        break;
    }
    case 8: {
        aiactor_mount_turn_in_place_state(frame, actorState, curCmd);
        aiactor_mount_proximity_react();
        break;
    }
    }

    __restore_gpr_25();
}

// ----------------------------------------------------------------------------
// aiactor_clamp_value_to_range 0x8026ffc0
// Maps v into the arc [lo,hi]: computes the fractional position of v within the
// span (hi-lo) via FUN_80390324 (frac/floor) and returns the residual offset
// from lo. Used by the turn-in-place state to fold a heading error into range.
// ----------------------------------------------------------------------------
double aiactor_clamp_value_to_range(double v, double lo, double hi)
{
    double span = (double)(float)(hi - lo);
    double frac = FUN_80390324((double)(float)((double)(float)(v - lo) / span));
    return -(double)(float)(span * (double)(float)frac - v);
}

// ----------------------------------------------------------------------------
// aiactor_wrap_angle_delta 0x80270018
// Returns (b - a) wrapped into the signed range (FLOAT_80619148, FLOAT_8061914c]
// by repeatedly adding/subtracting a full turn FLOAT_80619160. (The original
// leaves the result in the return register; callers consume it as the heading
// error.)
// ----------------------------------------------------------------------------
float aiactor_wrap_angle_delta(double a, double b)
{
    float d = (float)(b - a);
    for (; d <= FLOAT_80619148; d += FLOAT_80619160) { }
    for (; FLOAT_8061914c < d; d -= FLOAT_80619160) { }
    return d;
}

// ----------------------------------------------------------------------------
// aiactor_mount_retarget_zone_event 0x80270054
// Event helper: when the mount is in state 1 (boarding/spawn), latch a new
// escort zone (+0x684/+0xac8) from the other actor's stored zone, request
// pending state 6 (return/recoil), and broadcast an alert to that zone group.
// ----------------------------------------------------------------------------
void aiactor_mount_retarget_zone_event(int entity, int otherEntity)
{
    int actorState = *(int*)(entity + 0x7c);
    if (*(int*)(actorState + 0x20) == 1) {
        *(int*)(actorState + 0xac8) = *(int*)(*(int*)(otherEntity + 0x7c) + 0x678);
        if (*(int*)(actorState + 0xac8) < 0)
            *(int*)(actorState + 0x684) =
                *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int*)(actorState + 0x684) = *(int*)(actorState + 0xac8);
        *(int*)(actorState + 0x9ac) = 0;
        *(int*)(actorState + 0x9a8) = 0;
        *(float*)(actorState + 0xb30) = FLOAT_806190f8;
        if (*(int*)(actorState + 0x20) != 6) {
            *(int*)(actorState + 0x9d0) = 6;
        }
        aiactor_alert_broadcast_group(0, entity, *(int*)(actorState + 0xac8));
    }
}

// ----------------------------------------------------------------------------
// aiactor_mount_movement_command_tick 0x802700ec
// Seat movement-command sub-driver. Reads the current command opcode from the
// seat record (cmdRec[slot*3]) and:
//   opcodes 0/2/3/0xc/0xd/0x10/0x12: queue stance action 8 (idle/hold), gated
//     by the coop/vs input bit.
//   opcode 1: advance the nav path to the command's target zone; if blocked ->
//     aiactor_handle_blocked_path; else solve the heading to the next zone
//     (dir_to_angles + wrap) and pick turn-in-place (state 8) when the error is
//     large or walk (state 1) otherwise.
//   default: face the stored zone's heading (state 8).
// ----------------------------------------------------------------------------
void aiactor_mount_movement_command_tick(void)
{
    long long packed = FUN_803826c8();
    int frame = (int)(packed >> 0x20);
    int cmdRec = (int)packed;
    int actorState = *(int*)(frame + 0x7c);
    int opcode = *(int*)(cmdRec + *(int*)(actorState + 0xaa0) * 0xc);

    switch (opcode) {
    case 0: case 2: case 3: case 0xc: case 0xd: case 0x10: case 0x12:
        if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
            unsigned int inMask = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                                         : *(unsigned int*)(actorState + 0x1c);
            if ((inMask & 1) == 0) break;
        }
        *(int*)(actorState + 0xaac) = 8;
        *(int*)(actorState + 0xaa8) = 0;
        break;
    case 1: {
        *(int*)(actorState + 0x678) = *(int*)(actorState + 0x684);
        int z = soundpad_find_zone_by_id(*(int*)(cmdRec + *(int*)(actorState + 0xaa0) * 0xc + 8));
        if (z < 0)
            *(int*)(actorState + 0x684) =
                *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int*)(actorState + 0x684) = z;
        *(int*)(actorState + 0x9ac) = 0;
        *(int*)(actorState + 0x9a8) = 0;
        int next = aiactor_nav_advance_path(frame, (int*)(actorState + 0x678));
        if (next < 0) {
            aiactor_handle_blocked_path(frame);
        } else {
            int* zA = soundpad_resolve_zone_node(*(int*)(actorState + 0x678));
            int* zB = soundpad_resolve_zone_node(next);
            float dir[3];
            dir[0] = (float)((double)(float)zB[3] - (double)(float)zA[3]);
            zA = soundpad_resolve_zone_node(*(int*)(actorState + 0x678));
            zB = soundpad_resolve_zone_node(next);
            dir[1] = (float)((double)(float)zB[4] - (double)(float)zA[4]);
            zA = soundpad_resolve_zone_node(*(int*)(actorState + 0x678));
            zB = soundpad_resolve_zone_node(next);
            dir[2] = (float)((double)(float)zB[5] - (double)(float)zA[5]);
            float outYaw;
            dir_to_angles(dir, 0, &outYaw);
            float err = outYaw - *(float*)(frame + 0x70);
            for (; err <= FLOAT_80619148; err += FLOAT_80619160) { }
            for (; FLOAT_8061914c < err; err -= FLOAT_80619160) { }
            if (FLOAT_80619144 <= ABS(err)) {
                *(float*)(actorState + 0x9e0) = outYaw;
                anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                if (*(int*)(actorState + 0x20) != 8) {
                    *(int*)(actorState + 0x9d0) = 8;
                }
            } else {
                anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
                if (*(int*)(actorState + 0x20) != 1) {
                    *(int*)(actorState + 0x9d0) = 1;
                }
            }
        }
        break;
    }
    default: {
        int* zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x684));
        *(int*)(actorState + 0x9e0) = zn[6];
        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        if (*(int*)(actorState + 0x20) != 8) {
            *(int*)(actorState + 0x9d0) = 8;
        }
        break;
    }
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// aiactor_mount_los_engage_gate 0x80270488
// Target line-of-sight gate. Evaluates LOS to the current target; clears the
// "lost target" flag (+0x28 bit31) if the alert group still sees it. On LOS
// result: 0 = keep, 1 = request engage (state 3), 2/3 = hold if flagged. Then,
// in state 5, either queue action 8 or drop to engage; otherwise, if the return
// timer +0xb30 elapsed and the seat isn't locked (+0x54 bit0x20), latch the
// leader's zone and request charge (state 5).
// ----------------------------------------------------------------------------
int aiactor_mount_los_engage_gate(void)
{
    int frame = __save_gpr_28();
    int actorState = *(int*)(frame + 0x7c);
    int los = aiactor_evaluate_target_los();

    if (((*(unsigned int*)(actorState + 0x28) & 0x80000000) != 0) &&
        (aiactor_alert_group_flagB_any(*(unsigned int*)(actorState + 0xbec)) != 0)) {
        *(unsigned int*)(actorState + 0x28) &= 0x7fffffff;
    }

    if (los != 2) {
        if (los < 2) {
            if (los == 0) { __restore_gpr_28(); return los; }
            if (-1 < los) {
                if (((*(unsigned int*)(actorState + 0x28) & 0x80000000) == 0) &&
                    (*(float*)(actorState + 0xb30) = FLOAT_806190f8,
                     *(int*)(actorState + 0x20) != 3)) {
                    *(int*)(actorState + 0x9d0) = 3;
                }
                __restore_gpr_28();
                return los;
            }
        } else if ((los < 4) && ((*(unsigned int*)(actorState + 0x28) & 0x80000000) != 0)) {
            __restore_gpr_28();
            return los;
        }
    }

    if (*(int*)(actorState + 0x20) == 5) {
        if (*(int*)(actorState + 0xadc) == 0) {
            if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
                unsigned int inMask = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                                             : *(unsigned int*)(actorState + 0x1c);
                if ((inMask & 1) == 0) { __restore_gpr_28(); return los; }
            }
            *(int*)(actorState + 0xaac) = 8;
            *(int*)(actorState + 0xaa8) = 0;
        } else {
            *(int*)(actorState + 0x9d0) = 3;
        }
    } else if ((FLOAT_806190fc < *(float*)(actorState + 0xb30)) &&
               ((*(unsigned int*)(*(int*)(actorState + 0xbf0) + 0x54) & 0x20) == 0)) {
        *(float*)(actorState + 0xb30) = FLOAT_806190f8;
        *(int*)(actorState + 0xac8) = *(int*)(*(int*)(*(int*)(actorState + 4) + 0x7c) + 0x678);
        if (*(int*)(actorState + 0xac8) < 0)
            *(int*)(actorState + 0x684) =
                *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int*)(actorState + 0x684) = *(int*)(actorState + 0xac8);
        *(int*)(actorState + 0x9ac) = 0;
        *(int*)(actorState + 0x9a8) = 0;
        anim_play_scaled((double)FLOAT_8061910c, (double)FLOAT_806190fc);
        if (*(int*)(actorState + 0x20) != 5) {
            *(int*)(actorState + 0x9d0) = 5;
        }
    }
    __restore_gpr_28();
    return los;
}

// ----------------------------------------------------------------------------
// aiactor_mount_proximity_react 0x802706e0
// Per-frame reaction gate invoked from the mount FSM states. Runs the LOS gate;
// if that reports "no engage" (0) and a local player other than the seat owner
// is present, tests relation + facing (aiactor_facing_dot_test). Depending on
// that player's relation-flag bits it either fires the retarget-zone event
// (aiactor_mount_retarget_zone_event) or queues stance action 8.
// ----------------------------------------------------------------------------
void aiactor_mount_proximity_react(void)
{
    int frame = FUN_803826f0();
    int actorState = *(int*)(frame + 0x7c);
    int los = aiactor_mount_los_engage_gate();
    if (los == 0) {
        if (DAT_806120f8 != *(int*)(actorState + 8)) {
            int* pstate = (int*)(DAT_80611d90 + DAT_806120f8 * 0xea0);
            char* rel = (char*)aiactor_relation_entry(*(int*)(actorState + 8), pstate[2]);
            if ((*rel != '\0') &&
                (aiactor_facing_dot_test((double)*(float*)(actorState + 0x9bc), frame,
                                         (float*)(*pstate + 100)) != 0)) {
                unsigned int flags = (DAT_805015bd == '\n') ? (unsigned int)pstate[5]
                                                            : (unsigned int)pstate[7];
                if ((flags & 0x7120c) == 0) {
                    flags = (DAT_805015bd == '\n') ? (unsigned int)pstate[5]
                                                   : (unsigned int)pstate[7];
                    if ((flags & 0x400001) != 0) {
                        aiactor_mount_retarget_zone_event(frame, *pstate);
                    }
                } else {
                    if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
                        unsigned int inMask = (DAT_805015bd == '\n')
                                                  ? *(unsigned int*)(actorState + 0x14)
                                                  : *(unsigned int*)(actorState + 0x1c);
                        if ((inMask & 1) == 0) { FUN_8038273c(); return; }
                    }
                    *(int*)(actorState + 0xaac) = 8;
                    *(int*)(actorState + 0xaa8) = 0;
                }
            }
        }
    }
    FUN_8038273c();
}

// ============================================================================
// Address range: 0x80270870 - 0x802724c3 | batch_746 | 2026-09-09
//
// Mounted/creature seat-command EXECUTION ENGINE. The mount FSM (batch_745)
// stores a per-actor "command program counter" at actorState+0xaa0 that indexes
// a seat-command list (cmdRec, resolved from the level-def table). Each command
// is a 3-int record {opcode, zoneA, zoneB} at cmdRec[pc*3]. These functions
// interpret that command stream: play the matching clips, advance the PC
// (handling jump/loop opcodes), follow the nav path to the command's target
// zone, and report clip/arrival state back to the FSM.
//
// Opcode meanings (from the dispatch arms): 1 = idle-at-zone, 2/3/0x10 = move-
// with-attack, 4 = look-at, 5..9/0x11 = special poses/turns, 0xb = end, 0xc =
// reset-pc, 0xd = loop-back (pc -= rec[1]), 0x12 = advance.
// Clip-descriptor tables are the same DAT_80472790.. block used by batch_745,
// indexed by mount_clip_variant (see helper above); the per-opcode clip sub-
// tables live at DAT_804727a0/_7b0/_7c0/_7d0/_7e0/_7f0/_800/_810 and command
// clip-index list DAT_80472b34.
//
// Functions:
//   aiactor_mount_command_clip_tick    0x80270870  per-opcode clip driver
//   aiactor_mount_command_advance      0x802711e8  PC advance + jump/loop
//   aiactor_mount_command_nav_tick     0x802713b4  nav path follow + arrival
//   aiactor_mount_command_play_clip    0x80271968  play command clip (or rng)
//   aiactor_mount_command_clip_active  0x80271a80  any command clip active?
//   aiactor_mount_attack_clip_active   0x80271b70  attack primary clip active?
//   aiactor_mount_turn_clip_active     0x80271cc8  turn/transition clip active?
//   aiactor_mount_clip_aim_offset      0x80271e20  per-clip aim-offset angle
//   aiactor_mount_steer_to_path_node   0x802720f8  steer yaw toward next node
//   aiactor_mount_command_reach_dist   0x802722fc  clip root-motion stride dist
// ----------------------------------------------------------------------------

extern "C" {
// ---- additional frame prologue + external sub-drivers ----------------------
// FUN_803826e4 is declared void / FUN_803826e4_ret (int) earlier; the packed
// pair is fetched via FUN_803826e8_pair (established convention in this file).
// aiactor_mount_command_precondition / aiactor_mount_command_complete / aiactor_mount_next_command_record defined below in batch_747
// (aiactor_mount_command_precondition / _complete / _next_command_record)
extern void aiactor_check_door_between_zones(int frame);

// ---- per-opcode mount clip sub-tables (int clip ids, [variant]) ------------
extern int DAT_804727a0;  extern int DAT_804727c0;  extern int DAT_804727d0;
extern int DAT_804727e0;  extern int DAT_804727f0;  extern int DAT_80472800;
extern int DAT_80472810;  extern int DAT_80472b34;

// ---- command-stride/arrival tuning constants -------------------------------
extern float  FLOAT_80619164;
extern float  FLOAT_80619168;
extern float  FLOAT_8061916c;
extern float  FLOAT_80619170;
extern float  FLOAT_80619174;
extern float  FLOAT_80619178;
extern float  FLOAT_8061917c;
extern float  FLOAT_80619180;
extern float  FLOAT_80619188;
}

// ---- forward declarations for the batch_746 command engine (mutual calls) --
void   aiactor_mount_command_advance(void);
void   aiactor_mount_command_nav_tick(void);
void   aiactor_mount_command_play_clip(int frame, int cmdRec);
int    aiactor_mount_command_clip_active(void);
int    aiactor_mount_attack_clip_active(void);
int    aiactor_mount_turn_clip_active(void);
double aiactor_mount_clip_aim_offset(int frame);
void   aiactor_mount_steer_to_path_node(void);
double aiactor_mount_command_reach_dist(int opcode, int actorState);

// ----------------------------------------------------------------------------
// aiactor_mount_command_clip_tick 0x80270870
// Per-opcode clip driver. Reads the current command opcode (cmdRec[pc*3]); if
// the actor is mid-dismount (+0x28 bit0x100000) it just plays the dismount clip
// DAT_80472970. Otherwise dispatches: opcode 1 idle clips, 2/3/0x10 attack via
// precondition test (aiactor_mount_command_precondition) + completion (aiactor_mount_command_complete) or steer, 4
// look-at + turn toward door zone, 5..9/0x11 special-pose set with per-opcode
// sub-handling, 0xc resets the PC to 1.
// ----------------------------------------------------------------------------
void aiactor_mount_command_clip_tick(void)
{
    long long packed = (long long)FUN_803826e8();
    int frame = (int)((unsigned long long)packed >> 0x20);
    int cmdRec = (int)packed;
    int actorState = *(int*)(frame + 0x7c);
    int opcode = *(int*)(cmdRec + *(int*)(actorState + 0xaa0) * 0xc);

    if ((*(unsigned int*)(actorState + 0x28) & 0x100000) == 0) {
        switch (opcode) {
        case 1: {
            int bVar1 = mount_clip_variant(actorState);
            if (anim_is_clip_active(frame, (&DAT_804727a0)[bVar1]) == 0) {
                bVar1 = mount_clip_variant(actorState);
                if (anim_is_clip_active(frame, (&DAT_80472790)[bVar1]) == 0) {
                    anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                }
            } else if (anim_active_slot_userflags(*(int*)(frame + 4)) != 0) {
                anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
            }
            break;
        }
        case 2: case 3: case 0x10: {
            if ((aiactor_mount_attack_clip_active() == 0) &&
                (aiactor_mount_command_precondition(frame, opcode) != 0)) {
                aiactor_mount_command_complete();
            } else if (aiactor_mount_attack_clip_active() != 0) {
                aiactor_mount_next_command_record(frame, cmdRec);
                if (opcode == 0x10) {
                    soundpad_get_entity_id_from_zone(*(int*)(actorState + 0x9dc));
                }
                aiactor_mount_steer_to_path_node();
            }
            break;
        }
        case 4: {
            if (aiactor_mount_command_clip_active() == 0) {
                aiactor_mount_command_play_clip(frame, cmdRec);
            } else if (anim_active_slot_userflags(*(int*)(frame + 4)) != 0) {
                aiactor_mount_command_play_clip(frame, cmdRec);
            }
            aiactor_turn_toward_angle(
                (double)*(float*)(DAT_80611b8c + *(int*)(actorState + 0x678) * 0x20 + 0x18),
                frame, 0);
            break;
        }
        case 5: case 6: case 7: case 8: case 9: case 0x11: {
            int bVar1 = mount_clip_variant(actorState);
            int a = anim_is_clip_active(frame, (&DAT_804727b0)[bVar1]);
            if (a == 0) {
                bVar1 = mount_clip_variant(actorState);
                a = anim_is_clip_active(frame, (&DAT_804727a0)[bVar1]);
                if ((a == 0) && (aiactor_mount_command_clip_active() == 0) &&
                    (aiactor_mount_turn_clip_active() == 0)) {
                    bVar1 = mount_clip_variant(actorState);
                    if (anim_is_clip_active(frame, (&DAT_80472790)[bVar1]) == 0) {
                        aiactor_mount_command_play_clip(frame, cmdRec);
                    } else if (aiactor_mount_command_precondition(frame, opcode) != 0) {
                        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                    }
                    break;
                }
            }
            bVar1 = mount_clip_variant(actorState);
            a = anim_is_clip_active(frame, (&DAT_804727b0)[bVar1]);
            if ((a == 0) || (anim_active_slot_userflags(*(int*)(frame + 4)) == 0)) {
                int b = aiactor_mount_command_clip_active();
                if ((b == 0) || (anim_active_slot_userflags(*(int*)(frame + 4)) == 0)) {
                    if (aiactor_mount_turn_clip_active() != 0) {
                        int* rec = (int*)aiactor_mount_next_command_record(frame, cmdRec);
                        if (*rec == 1) {
                            if (anim_active_slot_userflags(*(int*)(frame + 4)) == 0) {
                                aiactor_mount_steer_to_path_node();
                            } else {
                                anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                            }
                        } else if (anim_active_slot_userflags(*(int*)(frame + 4)) != 0) {
                            aiactor_mount_command_advance();
                            aiactor_mount_command_play_clip(frame, cmdRec);
                        }
                    }
                } else {
                    switch (opcode) {
                    case 5: case 6: case 7:
                        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                        break;
                    case 8:
                        if (aiactor_mount_next_command_opcode(frame, cmdRec) == 1) {
                            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                        } else {
                            aiactor_mount_command_advance();
                            aiactor_mount_command_play_clip(frame, cmdRec);
                        }
                        break;
                    case 0x11: {
                        int* zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x9dc));
                        double yaw = prop_hinge_wrap_angle_to_range(
                            (double)*(float*)(frame + 0x70), (float*)(frame + 100),
                            (float*)(zn + 3));
                        (void)yaw;
                        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
                        break;
                    }
                    }
                }
            } else {
                aiactor_mount_command_play_clip(frame, cmdRec);
            }
            break;
        }
        case 0xc:
            *(int*)(actorState + 0xaa0) = 1;
            break;
        }
    } else {
        int bVar1 = mount_clip_variant(actorState);
        if (anim_is_clip_active(frame, (&DAT_80472970)[bVar1]) == 0) {
            anim_play_scaled((double)FLOAT_80619104, (double)FLOAT_806190fc);
        }
    }
    FUN_80382734();
}

// ----------------------------------------------------------------------------
// aiactor_mount_command_advance 0x802711e8
// Advance the seat-command program counter (+0xaa0) and handle control-flow
// opcodes: 0xd loops back (pc -= rec[1]), 0xb..0xc wrap to 1, 0x12 steps
// forward. Then resolve the new command's target zone: opcode 1 -> zone id at
// rec[2]; opcodes 0x10/0x11 -> nav path to rec[2]'s zone (blocked -> handler).
// Falls through to latch the command's primary zone rec[1].
// ----------------------------------------------------------------------------
void aiactor_mount_command_advance(void)
{
    long long packed = (long long)FUN_803826f0();
    int frame = (int)((unsigned long long)packed >> 0x20);
    int cmdRec = (int)packed;
    int actorState = *(int*)(frame + 0x7c);

    *(int*)(actorState + 0xaa0) = *(int*)(actorState + 0xaa0) + 1;
    int pc = *(int*)(actorState + 0xaa0);
    int opcode = *(int*)(cmdRec + pc * 0xc);
    if (opcode == 0xd) {
        *(int*)(actorState + 0xaa0) = pc - *(int*)(cmdRec + pc * 0xc + 4);
    } else if (opcode < 0xd) {
        if (0xb < opcode) {
            *(int*)(actorState + 0xaa0) = 1;
        }
    } else if (opcode == 0x12) {
        *(int*)(actorState + 0xaa0) = pc + 1;
    }

    int off = *(int*)(actorState + 0xaa0) * 0xc;
    opcode = *(int*)(cmdRec + off);
    if (opcode < 0x10) {
        if (opcode == 1) {
            int z = soundpad_find_zone_by_id(*(int*)(cmdRec + off + 8));
            if (z < 0)
                *(int*)(actorState + 0x684) =
                    *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int*)(actorState + 0x684) =
                    soundpad_find_zone_by_id(*(int*)(cmdRec + *(int*)(actorState + 0xaa0) * 0xc + 8));
            *(int*)(actorState + 0x9ac) = 0;
            *(int*)(actorState + 0x9a8) = 0;
            FUN_8038273c();
            return;
        }
    } else if (opcode < 0x12) {
        int rec = aiactor_mount_next_command_record(frame, cmdRec);
        int local_358[3];
        local_358[0] = *(int*)(actorState + 0x678);
        local_358[1] = soundpad_find_zone_by_id(*(int*)(rec + 8));
        local_358[2] = 0;
        int next = aiactor_nav_advance_path(0, local_358);
        if (next < 0) {
            aiactor_handle_blocked_path(frame);
            FUN_8038273c();
            return;
        }
        *(int*)(actorState + 0x9dc) = next;
    }

    int z = soundpad_find_zone_by_id(*(int*)(cmdRec + *(int*)(actorState + 0xaa0) * 0xc + 4));
    if (z < 0)
        *(int*)(actorState + 0x684) =
            *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
    else
        *(int*)(actorState + 0x684) =
            soundpad_find_zone_by_id(*(int*)(cmdRec + *(int*)(actorState + 0xaa0) * 0xc + 4));
    *(int*)(actorState + 0x9ac) = 0;
    *(int*)(actorState + 0x9a8) = 0;
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// aiactor_mount_command_nav_tick 0x802713b4
// Nav-path follow for the active seat command. If an attack/idle clip is still
// playing, waits (advances PC on its userflag). Otherwise advances the nav path
// (aiactor_nav_advance_path): -2 = target-blocked (broadcast alert + handler),
// -1 = arrived-at-final (face zone anchor). For a live next node it runs an
// arrival-radius test per command sub-state (aiactor_mount_next_command_opcode) using the target
// zone-node position, advances the command PC on arrival, records the next node
// (+0x680), checks for doors, and updates the nav target (+0x30..+0x38) toward
// either the live target actor or the node, decrementing the progress counter
// when inside the (state-scaled) arrival radius.
// ----------------------------------------------------------------------------
void aiactor_mount_command_nav_tick(void)
{
    long long packed = (long long)__save_gpr_28();
    int frame = (int)((unsigned long long)packed >> 0x20);
    int cmdRec = (int)packed;
    int actorState = *(int*)(frame + 0x7c);

    if (aiactor_mount_attack_clip_active() != 0) {
        if (anim_active_slot_userflags(*(int*)(frame + 4)) != 0) {
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
            aiactor_mount_command_advance();
        }
        __restore_gpr_28();
        return;
    }
    int bVar4 = mount_clip_variant(actorState);
    if (anim_is_clip_active(frame, (&DAT_804727a0)[bVar4]) != 0) {
        if (anim_active_slot_userflags(*(int*)(frame + 4)) != 0) {
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
            aiactor_mount_command_advance();
        }
        __restore_gpr_28();
        return;
    }

    int next = aiactor_nav_advance_path(frame, (int*)(actorState + 0x678));
    if (next == -2) {
        aiactor_alert_broadcast_group(0, frame, *(int*)(actorState + 0x678));
        aiactor_handle_blocked_path(frame);
        __restore_gpr_28();
        return;
    }

    if ((7 < *(int*)(actorState + 0x20)) || (*(int*)(actorState + 0x20) < 5)) {
        int opcode = *(int*)(cmdRec + *(int*)(actorState + 0xaa0) * 0xc);
        if (opcode == 0) {
            aiactor_mount_command_advance();
        } else if (opcode == 1) {
            unsigned int sub = (unsigned int)aiactor_mount_next_command_opcode(frame, cmdRec);
            float radius;
            switch (sub) {
            case 1:  radius = FLOAT_80619168; goto arrival_test;
            case 2: case 3: case 0x10: radius = FLOAT_80619138; goto arrival_test;
            case 5: case 6: case 7: case 8: case 9: case 0x11:
                radius = FLOAT_8061916c;
            arrival_test: {
                int* zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x684));
                if (ABS((float)zn[4] - *(float*)(frame + 0x68)) < FLOAT_80619110) {
                    float dz = *(float*)(frame + 0x6c) - (float)zn[5];
                    float dx = *(float*)(frame + 100) - (float)zn[3];
                    if ((float)(dz * dz + dx * dx) < radius) {
                        *(int*)(actorState + 0x678) = *(int*)(actorState + 0x684);
                        aiactor_mount_command_advance();
                        __restore_gpr_28();
                        return;
                    }
                }
                break;
            }
            default: break;
            }
        }
    }

    if (next == -1) {
        *(float*)(actorState + 0xad0) = FLOAT_80619170;
        *(int*)(actorState + 0x30) = *(int*)(*(int*)(actorState + 0x67c) + 0xc);
        *(int*)(actorState + 0x34) = *(int*)(*(int*)(actorState + 0x67c) + 0x10);
        *(int*)(actorState + 0x38) = *(int*)(*(int*)(actorState + 0x67c) + 0x14);
        __restore_gpr_28();
        return;
    }

    *(int*)(actorState + 0x680) = next;
    aiactor_check_door_between_zones(frame);
    if (((*(int*)(actorState + 0x20) == 3) || (*(int*)(actorState + 0x20) == 2)) &&
        (*(int*)(actorState + 4) != 0)) {
        *(int*)(actorState + 0x30) = *(int*)(*(int*)(actorState + 4) + 100);
        *(int*)(actorState + 0x34) = *(int*)(*(int*)(actorState + 4) + 0x68);
        *(int*)(actorState + 0x38) = *(int*)(*(int*)(actorState + 4) + 0x6c);
    } else {
        int base = DAT_80611b8c + next * 0x20;
        *(int*)(actorState + 0x30) = *(int*)(base + 0xc);
        *(int*)(actorState + 0x34) = *(int*)(base + 0x10);
        *(int*)(actorState + 0x38) = *(int*)(base + 0x14);
    }

    float dy = *(float*)(frame + 0x68) - *(float*)(actorState + 0x34);
    float dx = *(float*)(frame + 100) - *(float*)(actorState + 0x30);
    float dz = *(float*)(frame + 0x6c) - *(float*)(actorState + 0x38);
    double distSq = (double)(dz * dz + dx * dx + dy * dy);
    float r = FLOAT_80619178;
    if ((*(unsigned int*)(actorState + 0x2c) & 0x100) != 0) {
        r = FLOAT_80619174;
    }
    double radSq = (double)(r * r);
    if ((*(int*)(actorState + 0x680) == *(int*)(actorState + 0x684)) &&
        (*(int*)(actorState + 0x20) == 1)) {
        int sub = aiactor_mount_next_command_opcode(frame, cmdRec);
        if (sub < 8) {
            if (sub != 4) {
                if (3 < sub) radSq = (double)FLOAT_80619180;
                else if (1 < sub) radSq = (double)FLOAT_8061917c;
            }
        } else if (sub == 0x11) {
            radSq = (double)FLOAT_80619180;
        } else if ((sub < 0x11) && (0xf < sub)) {
            radSq = (double)FLOAT_8061917c;
        }
    }
    if (distSq < radSq) {
        *(int*)(actorState + 0x678) = next;
        *(int*)(actorState + 0x9a8) = *(int*)(actorState + 0x9a8) + -1;
    }
    __restore_gpr_28();
}

// ----------------------------------------------------------------------------
// aiactor_mount_command_play_clip 0x80271968
// Plays the current command's animation. For opcodes 8/9 it plays the explicit
// clip id at cmdRec[pc*3+2]; otherwise it advances the rng and plays the
// blend-reset clip. (The original folds both plays through anim_play_scaled.)
// ----------------------------------------------------------------------------
void aiactor_mount_command_play_clip(int frame, int cmdRec)
{
    int opcode = *(int*)(cmdRec + *(int*)(*(int*)(frame + 0x7c) + 0xaa0) * 0xc);
    if (((opcode != 8) && (opcode != 9)) ||
        (anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc) == 0)) {
        rng_counter_tick();
        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
    }
}

// ----------------------------------------------------------------------------
// aiactor_mount_command_clip_active 0x80271a80
// Query: is any of the current command's animation clips currently playing? For
// opcodes 8/9 it checks the explicit clip id; otherwise it scans up to 0x18
// clip-index entries (DAT_80472b34 list) against the mount clip table.
// ----------------------------------------------------------------------------
int aiactor_mount_command_clip_active(void)
{
    long long packed = FUN_803826e8_pair();
    int frame = (int)((unsigned long long)packed >> 0x20);
    int cmdRec = (int)packed;
    int actorState = *(int*)(frame + 0x7c);
    int off = *(int*)(actorState + 0xaa0) * 0xc;
    int opcode = *(int*)(cmdRec + off);
    int result = 0;
    if ((opcode == 8) || (opcode == 9)) {
        result = anim_is_clip_active(frame, *(int*)(cmdRec + off + 8));
    } else {
        int i = 0;
        int j = 0;
        do {
            int bVar1 = mount_clip_variant(actorState);
            result = anim_is_clip_active(
                frame, (&DAT_80472790)[(&DAT_80472b34)[j / 4] * 4 + (unsigned int)bVar1]);
            if (result != 0) break;
            i = i + 1;
            j = j + 4;
        } while (i < 0x18);
    }
    FUN_80382730();
    return result;
}

// ----------------------------------------------------------------------------
// aiactor_mount_attack_clip_active 0x80271b70
// Query: is the mount currently playing a committed "attack" primary clip? Tests
// the primary-clip slot against the attack sub-tables DAT_804727d0/_7e0/_7c0.
// ----------------------------------------------------------------------------
int aiactor_mount_attack_clip_active(void)
{
    int frame = __save_gpr_28();
    int actorState = *(int*)(frame + 0x7c);
    int result = 1;
    int bVar2 = mount_clip_variant(actorState);
    if (anim_slot_is_primary_clip(frame, (&DAT_804727d0)[bVar2]) == 0) {
        bVar2 = mount_clip_variant(actorState);
        if (anim_slot_is_primary_clip(frame, (&DAT_804727e0)[bVar2]) == 0) {
            result = 0;
        }
    }
    if (result != 0) {
        bVar2 = mount_clip_variant(actorState);
        result = anim_slot_is_primary_clip(frame, (&DAT_804727c0)[bVar2]);
    }
    __restore_gpr_28();
    return result;
}

// ----------------------------------------------------------------------------
// aiactor_mount_turn_clip_active 0x80271cc8
// Query: is the mount currently playing a turn/transition clip? Tests the active
// clip against the turn sub-tables DAT_80472810/_800/_7f0.
// ----------------------------------------------------------------------------
int aiactor_mount_turn_clip_active(void)
{
    int frame = __save_gpr_28();
    int actorState = *(int*)(frame + 0x7c);
    int result = 1;
    int bVar2 = mount_clip_variant(actorState);
    if (anim_is_clip_active(frame, (&DAT_80472810)[bVar2]) == 0) {
        bVar2 = mount_clip_variant(actorState);
        if (anim_is_clip_active(frame, (&DAT_80472800)[bVar2]) == 0) {
            result = 0;
        }
    }
    if (result != 0) {
        bVar2 = mount_clip_variant(actorState);
        result = anim_is_clip_active(frame, (&DAT_804727f0)[bVar2]);
    }
    __restore_gpr_28();
    return result;
}

// ----------------------------------------------------------------------------
// aiactor_mount_clip_aim_offset 0x80271e20
// Returns a heading/aim offset angle (radians) selected by which mount clip is
// currently active - each move/turn/attack clip has a canonical facing bias so
// the steering code can aim the body correctly mid-clip. Falls back to 0 when
// no tracked clip is active.
// ----------------------------------------------------------------------------
double aiactor_mount_clip_aim_offset(int frame)
{
    int actorState = *(int*)(frame + 0x7c);
    int bVar1 = mount_clip_variant(actorState);
    if (anim_is_clip_active(frame, (&DAT_804727e0)[bVar1]) != 0) return (double)FLOAT_80619188;
    bVar1 = mount_clip_variant(actorState);
    if (anim_is_clip_active(frame, (&DAT_804727d0)[bVar1]) != 0) return (double)FLOAT_80619124;
    bVar1 = mount_clip_variant(actorState);
    if (anim_is_clip_active(frame, (&DAT_804727c0)[bVar1]) != 0) return (double)FLOAT_8061914c;
    bVar1 = mount_clip_variant(actorState);
    if (anim_is_clip_active(frame, (&DAT_80472810)[bVar1]) != 0) return (double)FLOAT_80619188;
    bVar1 = mount_clip_variant(actorState);
    if (anim_is_clip_active(frame, (&DAT_80472800)[bVar1]) != 0) return (double)FLOAT_80619124;
    bVar1 = mount_clip_variant(actorState);
    if (anim_is_clip_active(frame, (&DAT_804727f0)[bVar1]) != 0) return (double)FLOAT_8061914c;
    bVar1 = mount_clip_variant(actorState);
    if (anim_is_clip_active(frame, (&DAT_80472890)[bVar1]) != 0) return (double)FLOAT_8061914c;
    return (double)FLOAT_806190f8;
}

// ----------------------------------------------------------------------------
// aiactor_mount_steer_to_path_node 0x802720f8
// Steers the mount's yaw toward the next nav node. Resolves the current/target
// zone (re-anchoring +0x684 if the caller passed a different owner), advances
// the nav path, then computes the desired heading = actor yaw + clip aim offset,
// wraps it into range (FUN_80390324), and applies a capped incremental yaw rate
// to +0xc4 scaled by the current clip's remaining time.
// ----------------------------------------------------------------------------
void aiactor_mount_steer_to_path_node(void)
{
    long long packed = (long long)__save_gpr_28();
    int frame = (int)((unsigned long long)packed >> 0x20);
    int owner = (int)packed;
    int actorState = *(int*)(frame + 0x7c);

    int next;
    int ent = soundpad_get_entity_id_from_zone(*(int*)(actorState + 0x684));
    if (owner == ent) {
        next = aiactor_nav_advance_path(frame, (int*)(actorState + 0x678));
    } else {
        int prevZone = *(int*)(actorState + 0x684);
        int z = soundpad_find_zone_by_id(owner);
        if (z < 0)
            *(int*)(actorState + 0x684) =
                *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int*)(actorState + 0x684) = soundpad_find_zone_by_id(owner);
        *(int*)(actorState + 0x9ac) = 0;
        *(int*)(actorState + 0x9a8) = 0;
        next = aiactor_nav_advance_path(frame, (int*)(actorState + 0x678));
        if (next < 0) { __restore_gpr_28(); return; }
        if (prevZone < 0)
            *(int*)(actorState + 0x684) =
                *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int*)(actorState + 0x684) = prevZone;
        *(int*)(actorState + 0x9ac) = 0;
        *(int*)(actorState + 0x9a8) = 0;
    }

    if (-1 < next) {
        double aim = aiactor_mount_clip_aim_offset(frame);
        double desired = (double)(float)((double)*(float*)(frame + 0x70) + aim);
        double frac = FUN_80390324((double)((float)(desired - (double)FLOAT_80619148) /
                                            FLOAT_80619160));
        frac = (double)(float)frac;
        int* zn = soundpad_resolve_zone_node(next);
        double yaw = prop_hinge_wrap_angle_to_range(
            -(double)(float)((double)FLOAT_80619160 * frac - desired),
            (float*)(frame + 100), (float*)(zn + 3));
        if ((double)FLOAT_80619100 < ABS(yaw)) {
            double endT = anim_get_clip_end_time_of(frame);
            int steps = (int)((float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80619150) *
                              (float)(endT - (double)*(float*)(*(int*)(frame + 4) + 0xd4)));
            if (0 < steps) {
                float rate = (float)(yaw / (double)(float)((double)(int)(steps ^ 0x80000000) -
                                                           DOUBLE_80619150));
                if (FLOAT_80619110 < rate) rate = FLOAT_80619110;
                *(float*)(frame + 0xc4) = *(float*)(frame + 0xc4) + rate;
            }
        }
    }
    __restore_gpr_28();
}

// ----------------------------------------------------------------------------
// aiactor_mount_command_reach_dist 0x802722fc
// Returns the mount's stride/reach distance for a command opcode. For move
// opcodes (4-7, 0x10-0x11) it samples the first and last root keyframes of the
// command's locomotion clip (anim_sample_keyframe via the DAT_80611824 clip
// descriptor table indexed by DAT_804d1cd0[clip]) and returns the Z travel
// distance; other opcodes return the fixed reach constant FLOAT_80619108, or
// FLOAT_806190f8 for opcodes >= 0x12.
// ----------------------------------------------------------------------------
double aiactor_mount_command_reach_dist(int opcode, int actorState)
{
    float reach = FLOAT_80619108;
    if (opcode != 0x10) {
        if (opcode < 0x10) {
            if (opcode < 4) {
                if (1 < opcode) return (double)reach;   // opcodes 2,3
            } else if (opcode < 8) {
                goto sample_clip;                        // opcodes 4-7
            }
            reach = FLOAT_806190f8;                      // opcodes 0,1,8..0xf
            return (double)reach;
        } else if (opcode < 0x12) {
            goto sample_clip;                            // opcode 0x11
        }
        reach = FLOAT_806190f8;                          // opcodes >= 0x12
        return (double)reach;
    }
sample_clip: {
    float startKf[3];
    float endKf[3];
    int bVar1 = mount_clip_variant(actorState);
    int descA = *(int*)(DAT_80611824 +
                        (short)(&DAT_804d1cd0)[(&DAT_804727b0)[bVar1]] * 4);
    anim_sample_keyframe(descA, 0, 0, startKf);
    bVar1 = mount_clip_variant(actorState);
    int descB = *(int*)(DAT_80611824 +
                        (short)(&DAT_804d1cd0)[(&DAT_804727b0)[bVar1]] * 4);
    anim_sample_keyframe(descB, 0, *(int*)(descA + 0x10) - 1, endKf);
    return (double)(endKf[2] - startKf[2]);
}
}

// ============================================================================
// Address range: 0x802724c4 - 0x80272c43 | batch_747 | 2026-09-09
//
// Mount seat-command SUPPORT helpers - the remaining callees of the command
// engine (batch_746). These evaluate command preconditions/completion, peek the
// next command record, scan the alert group for a nearer target, and retarget
// the mount to the nearest command-referenced zone entity.
//
// Functions:
//   aiactor_mount_command_precondition     0x802724c4  gate a command on clip+event
//   aiactor_mount_command_complete         0x802725c0  finish a command (facing)
//   aiactor_mount_scan_alert_target        0x80272838  nearest alert-group target
//   aiactor_mount_next_command_opcode      0x802729b0  peek next command opcode
//   aiactor_mount_next_command_record      0x802729d4  ptr to next command record
//   aiactor_mount_retarget_nearest_command 0x80272a30  retarget to nearest cmd zone
// ----------------------------------------------------------------------------

extern "C" {
extern long long FUN_803826d8_pair(void);      // frame prologue variant d, packed (hi=entity, lo=cmdRec)
extern int  soundpad_resolve_entity_to_zone(int entityZoneId); // -> zone rec ptr
extern int  DAT_806120e4;                 // active alert-group bitfield source
extern int  DAT_805885b8;                 // alert-group table base (stride 0xb*4)
extern int  DAT_805885bc;                 // alert-group member ptr table
extern float FLOAT_8061918c;              // large initial nearest-dist sentinel
}

// ----------------------------------------------------------------------------
// aiactor_mount_command_precondition 0x802724c4
// Returns nonzero if the mount may execute the current command now: the actor's
// active clip must be the command's base clip (DAT_80472790[variant]); for
// opcode 2 it is always eligible, for opcode 0x10 it also requires a positive
// wrapped facing angle toward the command's target zone-node. When eligible it
// returns the active anim-event scan result.
// ----------------------------------------------------------------------------
int aiactor_mount_command_precondition(int entity, int opcode)
{
    int actorState = *(int*)(entity + 0x7c);
    int bVar2 = mount_clip_variant(actorState);
    if (*(int*)(*(int*)(entity + 4) + 0xcc) != (&DAT_80472790)[bVar2]) {
        return 0;
    }
    bool eligible = false;
    if (opcode == 2) {
        eligible = true;
    } else if (opcode == 0x10) {
        int* zn = soundpad_resolve_zone_node(*(int*)(actorState + 0x9dc));
        double yaw = prop_hinge_wrap_angle_to_range((double)*(float*)(entity + 0x70),
                                                    (float*)(entity + 100), (float*)(zn + 3));
        if ((double)FLOAT_806190f8 < yaw) {
            eligible = true;
        }
    }
    (void)eligible; // both branches return the same anim-event query
    return anim_event_scan_active();
}

// ----------------------------------------------------------------------------
// aiactor_mount_command_complete 0x802725c0
// Command completion handler dispatched on the command opcode (packed low dword
// from the frame prologue): opcodes 2/3 replay the reset clip; opcode 0x10 does
// a facing test toward the target zone-node and, once within tolerance, advances
// the command PC (aiactor_mount_command_advance); other opcodes are no-ops.
// ----------------------------------------------------------------------------
void aiactor_mount_command_complete(void)
{
    long long packed = (long long)FUN_803826f0();
    int frame = (int)((unsigned long long)packed >> 0x20);
    int opcode = (int)packed;
    if (opcode == 3) {
        anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
    } else if (opcode < 3) {
        if (1 < opcode) {
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        }
    } else if (opcode == 0x10) {
        int* zn = soundpad_resolve_zone_node(*(int*)(*(int*)(frame + 0x7c) + 0x9dc));
        double yaw = prop_hinge_wrap_angle_to_range((double)*(float*)(frame + 0x70),
                                                    (float*)(frame + 100), (float*)(zn + 3));
        if ((double)FLOAT_80619164 <= ABS(yaw)) {
            anim_play_scaled((double)FLOAT_806190f8, (double)FLOAT_806190fc);
        } else {
            aiactor_mount_command_advance();
        }
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// aiactor_mount_scan_alert_target 0x80272838
// Scans the actor's alert group for the nearest member that references this
// mount and latches that member's target zone into +0xac8, flagging +0x28 bit
// 0x400 (new-target). Walks the alert-group table DAT_805885b8 (selected by the
// active group index countLeadingZeros(DAT_806120e4)>>5) and its per-member
// bucket lists in DAT_80587e10 (0x14 buckets, stride 0x60).
// ----------------------------------------------------------------------------
void aiactor_mount_scan_alert_target(void)
{
    int entity = __save_gpr_28();
    int actorState = *(int*)(entity + 0x7c);
    unsigned int grp = countLeadingZeros(DAT_806120e4) >> 5;
    int best = -1;
    float bestDist = FLOAT_8061918c;

    int rowOff = 0;
    int count = (&DAT_805885b8)[grp * 0xb];
    for (int i = 0; i < count; i++) {
        int member = *(int*)((int)(&DAT_805885b8 + grp * 0xb) + rowOff + 4);
        unsigned int mask = *(unsigned int*)(member + 0x114);
        if ((-1 < (int)mask) && ((int)mask < 0x100000)) {
            int bit = 0;
            int bucketOff = 0;
            do {
                if ((mask & (1 << bit)) != 0) {
                    int n = *(int*)((int)&DAT_80587e10 + bucketOff);
                    int slotOff = 0;
                    while (0 < n) {
                        if (entity == *(int*)((int)&DAT_80587e10 + bucketOff + slotOff + 0x10)) {
                            float dx = *(float*)(member + 100) - *(float*)(entity + 100);
                            float dy = *(float*)(member + 0x68) - *(float*)(entity + 0x68);
                            float dz = *(float*)(member + 0x6c) - *(float*)(entity + 0x6c);
                            float d = dz * dz + dx * dx + dy * dy;
                            if (d < bestDist) { best = i; bestDist = d; }
                        }
                        slotOff += 4;
                        n--;
                    }
                }
                bit++;
                bucketOff += 0x60;
            } while (bit < 0x14);
        }
        rowOff += 4;
    }

    if ((-1 < best) &&
        (*(int*)(*(int*)(&DAT_805885bc + grp * 0x2c + best * 4) + 0x110) != -1)) {
        *(int*)(actorState + 0xac8) =
            *(int*)(*(int*)(&DAT_805885bc + grp * 0x2c + best * 4) + 0x110);
        *(unsigned int*)(actorState + 0x28) |= 0x400;
    }
    __restore_gpr_28();
}

// ----------------------------------------------------------------------------
// aiactor_mount_next_command_opcode 0x802729b0
// Returns the opcode of the NEXT command record (dereferences the record ptr
// from aiactor_mount_next_command_record).
// ----------------------------------------------------------------------------
int aiactor_mount_next_command_opcode(int entity, int cmdRec)
{
    int* rec = (int*)aiactor_mount_next_command_record(entity, cmdRec);
    return *rec;
}

// ----------------------------------------------------------------------------
// aiactor_mount_next_command_record 0x802729d4
// Returns a pointer to the next command record, honoring control-flow opcodes at
// the current PC: 0xd loops back (pc+1 - rec[+0x10]), 0xb..0xc wrap forward one,
// otherwise advance to pc+1.
// ----------------------------------------------------------------------------
int aiactor_mount_next_command_record(int entity, int cmdRec)
{
    int pc = *(int*)(*(int*)(entity + 0x7c) + 0xaa0);
    int cur = cmdRec + pc * 0xc;
    int opcode = *(int*)(cur + 0xc);
    if (opcode == 0xd) {
        return cmdRec + ((pc + 1) - *(int*)(cur + 0x10)) * 0xc;
    }
    if ((opcode < 0xd) && (0xb < opcode)) {
        return cmdRec + 0xc;
    }
    return cmdRec + (pc + 1) * 0xc;
}

// ----------------------------------------------------------------------------
// aiactor_mount_retarget_nearest_command 0x80272a30
// Scans the mount's seat-command list for the nearest command that references a
// different zone entity (skipping control/idle opcodes 0/2/3/0xc/0xd/0x10/0x12).
// If found, latches that entity's zone into +0x684, sets the command PC (+0xaa0)
// to that command, and requests pursue state 7. If none is found, queues stance
// action 8 (gated by the coop/vs input bit). For an unassigned seat (-2) it uses
// the char-def's single default target.
// ----------------------------------------------------------------------------
void aiactor_mount_retarget_nearest_command(void)
{
    long long packed = FUN_803826d8_pair();
    int frame = (int)((unsigned long long)packed >> 0x20);
    int cmdRec = (int)packed;
    int actorState = *(int*)(frame + 0x7c);

    int bestEnt = 0;
    int bestPc = *(int*)(actorState + 0xaa0);
    int seat = *(int*)(*(int*)(actorState + 0xbf0) + 0x68);
    if (seat == -2) {
        bestEnt = *(int*)(*(int*)(actorState + 0xbf0) + 0x10);
        bestPc = 1;
    } else {
        double bestDist = (double)FLOAT_8061918c;
        int off = 0;
        int n = *(int*)(*(int*)(DAT_8061208c + 0x34) + seat * 8 + 4);
        for (int i = 0; i < n; i++) {
            int* rec = (int*)(cmdRec + off);
            int ent = rec[1];
            if (ent != bestEnt) {
                switch (rec[0]) {
                case 0: case 2: case 3: case 0xc: case 0xd: case 0x10: case 0x12:
                    break;
                default: {
                    int zn = soundpad_resolve_entity_to_zone(ent);
                    if (zn != 0) {
                        float dx = *(float*)(frame + 100) - *(float*)(zn + 0xc);
                        float dy = *(float*)(frame + 0x68) - *(float*)(zn + 0x10);
                        float dz = *(float*)(frame + 0x6c) - *(float*)(zn + 0x14);
                        double d = (double)(dz * dz + dx * dx + dy * dy);
                        if (d < bestDist || bestEnt == 0) {
                            bestEnt = ent;
                            bestPc = i;
                            bestDist = d;
                        }
                    }
                    break;
                }
                }
            }
            off += 0xc;
        }
    }

    if (bestEnt == 0) {
        if (*(int*)(actorState + 0xaac) == 1 && *(int*)(actorState + 0xaa8) == 0) {
            unsigned int inMask = (DAT_805015bd == '\n') ? *(unsigned int*)(actorState + 0x14)
                                                         : *(unsigned int*)(actorState + 0x1c);
            if ((inMask & 1) == 0) { FUN_80382724(); return; }
        }
        *(int*)(actorState + 0xaac) = 8;
        *(int*)(actorState + 0xaa8) = 0;
    } else {
        int z = soundpad_find_zone_by_id(bestEnt);
        if (z < 0)
            *(int*)(actorState + 0x684) =
                *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int*)(actorState + 0x684) = soundpad_find_zone_by_id(bestEnt);
        *(int*)(actorState + 0x9ac) = 0;
        *(int*)(actorState + 0x9a8) = 0;
        *(int*)(actorState + 0xaa0) = bestPc;
        if (*(int*)(actorState + 0x20) != 7) {
            *(int*)(actorState + 0x9d0) = 7;
        }
    }
    FUN_80382724();
}
