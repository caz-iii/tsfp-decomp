// Address range: 0x8013aa4c - 0x8013c53f, 0x8013cb20 - 0x8013cdf3 | batch_368 | 2026-09-05
//
// AI actor combat / target-acquisition cluster. Continuation of batches 364-367.
// These run per-actor per-frame off the AI behaviour dispatcher and drive damage
// filtering, leader/stance state, nearest-target acquisition, target visibility
// gating, the massive "react to target" combat state machine, and the zone
// player-scan target check.
//
// Per-actor render object = *(actor + 0x7c); combat/target state lives there:
//   +0x00 owning entity      +0x04 current target/leader entity
//   +0x10 mode flags word A  +0x14/+0x1c per-frame input masks (coop vs vs)
//   +0x20 mount type         +0x24 stance-flag word (set by aiactor_set_stance_flags)
//   +0x28 status flags       +0x48/+0x4a weapon/character type shorts
//   +0x684 stored target zone
//   +0xaa8/+0xaac stance-request pair    +0xbf0 char def block
//   +0x9b8 current zone id
// DAT_805015bd = game mode byte (0x0a coop). DAT_80611d78 actor table (0x141c),
// DAT_80611d90 player state pool (0xea0). FUN_803826xx/__save_gpr_xx = fp/gpr
// scratch-frame helpers (actor in the high word, target/flag in the low word).
//
// Functions:
//   aiactor_apply_damage_filters      0x8013aa4c  weapon/flag -> damage callbacks
//   aiactor_update_leader_and_state   0x8013ad20  resolve leader zone + set stance
//   aiactor_acquire_nearest_target    0x8013af64  nearest player/target scan
//   aiactor_evaluate_target_los       0x8013b254  LOS/range visibility gate
//   aiactor_combat_death_behavior     0x8013b4c4  drop weapons + combat tick
//   aiactor_combat_react_to_target    0x8013b80c  MASSIVE combat reaction machine
//   aiactor_set_stance_flags          0x8013cb20  stance-flag switch (+0x24)
//   aiactor_check_zone_target_players 0x8013cbd0  scan players in zone, acquire

#include "common.h"

// Ghidra SQRT intrinsic (single-precision), self-contained Newton-Raphson.
static inline float SQRT_(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x;
    for (int i = 0; i < 6; i = i + 1) g = 0.5f * (g + x / g);
    return g;
}

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern int  player_ai_is_ranged_weapon(int weaponType);
extern int  player_ai_is_explosive_weapon(int weaponType);
extern int  player_ai_is_sniper_weapon(int weaponType);
extern int  player_ai_is_heavy_weapon(int weaponType);
extern int  player_ai_is_valid_weapon_type(int weaponType);
extern int  player_ai_is_story_mode(int weaponType);
extern u32  player_ai_check_target_team(int attacker, int target);
extern u32  player_ai_char_def_get_flags(int charIndex);
extern void player_ai_combat_behavior_tick(void);
extern void player_ai_drop_all_weapons(int* player);
extern void entity_set_render_type(int entity, int renderType);
extern void aiactor_leader_zone_update(int actor);
extern int  level_get_info_cached(int levelId);
extern void render_instance_clear_visual_state(int renderObj);
extern u32  anim_event_find_nth_by_mask(int ctrl, int mask, int n);
extern int  sound_is_playing(int handle);
extern void aiactor_turn_toward_entity(int actor, int target, int instant);
extern void character_fx_bone_particle_spawn(void);
extern void character_find_nearest_bone(void);
extern void character_on_death_handler(void);
// damage-modifier callbacks installed via entity_set_render_type (taken by address)
extern void damage_filter_invincible(int* ctx);
extern void damage_filter_vip_match_end(void);
extern void damage_filter_fixed_low(int* ctx);
extern void damage_filter_environmental_zero(int* ctx);
extern void damage_filter_melee_zero(int* ctx);
extern void damage_filter_melee_env_half(int* ctx);
extern void damage_cap_by_max_health(int* ctx);
extern void damage_filter_arcade_match_end(void);
extern void damage_filter_unarmed_double(int* ctx);
extern void damage_filter_fixed_category(int* ctx);
extern void damage_filter_special_half(int* ctx);
}

// entity_set_render_type takes an int callback slot; the callbacks are passed as
// raw code addresses. character_on_death_handler / player_ai_drop_all_weapons have
// simplified self-externs above but the ABI passes a wider register frame, so those
// two are invoked through typed function-pointer casts.
#define RENDER_CB(fp) ((int)(intptr_t)(void*)(fp))
typedef void (*on_death_fn)(u64, double, double, double, u64, u64, u64, u64);
typedef void (*drop_fn)(u64, double, double, double, u64, u64, u64, u64, int*);
#define CHAR_ON_DEATH(...) ((on_death_fn)(void*)&character_on_death_handler)(__VA_ARGS__)

// ---- raw data symbols (per-file externs, typed for this file's use) --------
extern char  DAT_805015bd;   // game mode byte (0x0a coop, 0x05, 0x02 ...)
extern char  DAT_805015be;   // sub-mode byte
extern char  DAT_805015bf;   // sub-mode byte
extern u32   DAT_805015c0;   // cheat/flag word
extern int   DAT_805015d8;   // story-state selector
extern int   DAT_80612610;   // active player/actor count
extern int   DAT_80612614;   // current level id
extern int   DAT_80611910;   // designated leader entity
extern int   DAT_80611928;   // room-change tick base
extern int   DAT_806108e4;   // room-change tick offset
extern int   DAT_806117a4;   // force-turn flag
extern int   DAT_80611d78;   // actor table base (0x141c stride, +0x14 = player0)
extern int   DAT_80611d90;   // player state pool base (0xea0 stride)
extern int   DAT_8061208c;   // cached level-info pointer
extern int   DAT_804d34dc;   // weapon-class attribute table
extern short DAT_804d1cd0;   // weapon-class index table
extern int   DAT_80472ef8;   // leader-role handler vtable base
extern u32   DAT_8040db48;   // ragdoll launch impulse scratch (x/y/z)
extern u32   DAT_8040db4c;
extern u32   DAT_8040db50;
extern u32   DAT_8040db54;
extern u32   DAT_8040db58;
extern u32   DAT_8040db5c;

extern float FLOAT_80615ca8; // 1.0
extern float FLOAT_80615cac; // 0.0
extern float FLOAT_80615cdc; // steering / recency scale
extern float FLOAT_80615cf8; // near proximity radius (crouched)
extern float FLOAT_80615d14; // 1.5 (Newton refine constant)
extern float FLOAT_80615d24; // nearest-target init distance
extern float FLOAT_80615d2c; // min stand-off distance
extern float FLOAT_80615d3c; // ragdoll launch min speed-sq
extern float FLOAT_80615d4c; // weapon spread divisor
extern float FLOAT_80615d50; // close-range multiplier
extern float FLOAT_80615d54; // vertical LOS tolerance
extern float FLOAT_80615d58; // engage-range multiplier
extern float FLOAT_80615d10; // reacquire-range multiplier
extern float FLOAT_80615d60; // pain-anim blend param

// ---- raw function symbols (per-file externs, typed to the call sites) ------
extern "C" {
extern int  FUN_80223470(int* ctx);                     // extra damage filter (0x5c3)
extern int  FUN_801fca6c(int* ctx);                     // arcade damage filter
extern int  FUN_8027ea98(void);                         // current squad leader entity
extern int  FUN_80380194(void);                         // heavy-weapon target select
extern int  FUN_8013dd58(void);                         // target still valid?
extern bool FUN_8013ef50(double range, int frame, float* targetPos); // LOS in range
extern bool FUN_8013ee44(double range, int actor, float* targetPos); // proximity test
extern int  FUN_8013e9cc(int actor, int phase, int* ctx, int flag);  // behaviour phase gate
extern int  FUN_8013ff28(int actor);                    // pinned/blocked?
extern int  FUN_80364290(int actor, int mode);          // scripted-hold check
extern u32  FUN_8027eb3c(int actor);                    // coop revive check
extern int  FUN_8002588c(int actor, int animId);        // anim playing?
extern void FUN_80023280(double a, double b);           // play pain anim
extern void FUN_8025ff4c(int a, int actor, int animId); // request combat anim
extern void FUN_80143754(int frame);                    // bark / alert callout
extern void FUN_80025cb4(int actor, int frame, int ctx);// face-target update
extern int  FUN_80025950(int actor, int a, int b, int c); // flag test
extern int  FUN_8023c32c(int tag);                      // find entity by fourcc tag
extern int  FUN_80142cdc(int actor);                    // clear combat scratch
extern int  FUN_80280594(int* frame);                   // vip death handling
extern void FUN_80234a04(int* targetObj, int* selfObj); // face-off / recoil setup
extern int  FUN_80025868(int actor);                    // active anim controller
extern void FUN_80022ed8(int ctrl, int* outFrames, int* q);
extern void FUN_8013f5c0(int actor, int a, int b);      // retrigger anim event

extern unsigned long long FUN_803826e8(void);           // fp frame enter (combat)
extern void               FUN_80382734(void);           // fp frame leave
extern unsigned long long __save_gpr_25(void);          // gpr frame enter (react)
extern void               __restore_gpr_25(void);
extern unsigned long long __save_gpr_28(void);          // gpr frame enter (los)
extern void               __restore_gpr_28(void);
}

// --- forward declarations for functions defined in this file --------------
int  aiactor_acquire_nearest_target(int actor_in, int preferred, int mode);
void aiactor_set_stance_flags(int actor, u32 flag);

static u64 aiactor_set_stance_flags_ret(int actor, u32 flag)
{
    aiactor_set_stance_flags(actor, flag);
    return (u64)(u32)actor;
}
static void player_ai_drop_all_weapons_ex(u64 a, double p2, double p3, double p4, u64 p5,
                                          u64 p6, u64 p7, u64 p8, int* rd)
{
    ((drop_fn)(void*)&player_ai_drop_all_weapons)(a, p2, p3, p4, p5, p6, p7, p8, rd);
}

// ---------------------------------------------------------------------------
// aiactor_apply_damage_filters 0x8013aa4c
// Installs the correct damage-modifier callback on the actor's combat object
// based on flag words, weapon/character type and match mode.
// ---------------------------------------------------------------------------
void aiactor_apply_damage_filters(int actor)
{
    short weap;
    int   fourcc;
    int*  rd;

    rd = *(int**)(actor + 0x7c);
    if (((rd[4] & 1U) != 0) && (rd[0x11] == 1)) {
        entity_set_render_type(actor, RENDER_CB(damage_filter_invincible));
    }
    if ((rd[5] & 0x2000U) != 0) {
        entity_set_render_type(actor, RENDER_CB(damage_filter_vip_match_end));
    }
    if ((*(u32*)(rd[0x2fc] + 0x58) & 0x2000000) != 0) {
        entity_set_render_type(actor, RENDER_CB(damage_filter_fixed_low));
    }
    if ((*(u32*)(rd[0x2fc] + 0x58) & 0x8000) != 0) {
        entity_set_render_type(actor, RENDER_CB(damage_filter_invincible));
    }
    if ((*(u32*)(rd[0x2fc] + 0x58) & 0x20000) != 0) {
        entity_set_render_type(actor, RENDER_CB(damage_filter_environmental_zero));
    }
    if ((*(u32*)(rd[0x2fc] + 0x58) & 0x10000) != 0) {
        entity_set_render_type(actor, RENDER_CB(damage_filter_melee_zero));
    }
    if (player_ai_is_ranged_weapon((int)*(short*)(rd + 0x12)) != 0) {
        entity_set_render_type(actor, RENDER_CB(damage_filter_melee_env_half));
    }
    if (rd[0x11] == 1) {
        entity_set_render_type(actor, RENDER_CB(damage_cap_by_max_health));
    }

    weap = *(short*)(rd + 0x12);
    if (weap != 0x5a) {
        if (weap < 0x5a) {
            if ((weap != 0x48) && ((0x47 < weap) || (weap != 0xc))) goto after_fourcc;
        }
        else if (weap != 0x74) goto after_fourcc;
    }
    fourcc = *(int*)(rd[0x2fc] + 0x28);
    if (fourcc == 0x50415246) {          // 'PARF'
        entity_set_render_type(actor, RENDER_CB(damage_filter_arcade_match_end));
    }
    else if (fourcc == 0x50415250) {     // 'PARP'
        entity_set_render_type(actor, RENDER_CB(damage_filter_arcade_match_end));
    }
    else if (fourcc == 0x34574159) {     // '4WAY'
        entity_set_render_type(actor, RENDER_CB(damage_filter_unarmed_double));
    }

after_fourcc:
    if ((*(u32*)(rd[0x2fc] + 0x58) & 0x40000) != 0) {
        entity_set_render_type(actor, RENDER_CB(damage_filter_fixed_category));
        *(u32*)(actor + 0x9c) = *(u32*)(actor + 0x9c) & 0xfffffffd;
    }
    if (player_ai_is_explosive_weapon((int)*(short*)(rd + 0x12)) != 0) {
        entity_set_render_type(actor, RENDER_CB(damage_filter_special_half));
    }
    if ((*(short*)(*rd + 0x94) == 0x5c3) && ((DAT_805015c0 & 0x40) != 0)) {
        entity_set_render_type(actor, RENDER_CB(FUN_80223470));
    }
    if ((((rd[5] & 0x2000U) != 0) && (*(int*)(rd[0x2fc] + 8) == 0x4143484e)) &&  // 'ACHN'
        (DAT_80612614 == 6)) {
        entity_set_render_type(actor, RENDER_CB(FUN_801fca6c));
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_update_leader_and_state 0x8013ad20
// Resolves the actor's leader/target entity, copies its zone into the actor,
// then requests the correct movement stance (4 = advance, 8 = combat).
// ---------------------------------------------------------------------------
void aiactor_update_leader_and_state(int actor)
{
    int   targetType;
    int   target;
    int   zoneStore;
    u32   mask;
    int   rd;

    rd = *(int*)(actor + 0x7c);
    target = aiactor_acquire_nearest_target(actor, *(int*)(DAT_80611d78 + 0x14), 0);
    *(int*)(rd + 4) = target;
    if (*(int*)(rd + 4) == 0) {
        *(int*)(rd + 4) = *(int*)(DAT_80611d78 + 0x14);
    }
    target = *(int*)(rd + 4);
    targetType = *(int*)(target + 0x20);
    if (targetType == 8) {
        zoneStore = *(int*)(*(int*)(target + 0x7c) + 0x678);
        if (zoneStore < 0) {
            *(int*)(rd + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        }
        else {
            *(int*)(rd + 0x684) = zoneStore;
        }
        *(int*)(rd + 0x9ac) = 0;
        *(int*)(rd + 0x9a8) = 0;
    }
    else if (targetType < 8) {
        if (targetType == 1) {
            if (*(int*)(target + 0x110) < 0) {
                *(int*)(rd + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            }
            else {
                *(int*)(rd + 0x684) = *(int*)(target + 0x110);
            }
            *(int*)(rd + 0x9ac) = 0;
            *(int*)(rd + 0x9a8) = 0;
        }
    }
    else if (targetType == 0x40) {
        zoneStore = *(int*)(*(int*)(target + 0x7c) + 0x38);
        if (zoneStore < 0) {
            *(int*)(rd + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        }
        else {
            *(int*)(rd + 0x684) = zoneStore;
        }
        *(int*)(rd + 0x9ac) = 0;
        *(int*)(rd + 0x9a8) = 0;
    }

    if (player_ai_is_sniper_weapon((int)*(short*)(rd + 0x4a)) == 0) {
        aiactor_leader_zone_update(actor);
        if (*(int*)(rd + 0xaac) == 1 && *(int*)(rd + 0xaa8) == 0) {
            mask = (DAT_805015bd == '\n') ? *(u32*)(rd + 0x14) : *(u32*)(rd + 0x1c);
            if ((mask & 1) == 0) {
                return;
            }
        }
        *(int*)(rd + 0xaac) = 8;
        *(int*)(rd + 0xaa8) = 0;
    }
    else {
        if (*(int*)(rd + 0xaac) == 1 && *(int*)(rd + 0xaa8) == 0) {
            mask = (DAT_805015bd == '\n') ? *(u32*)(rd + 0x14) : *(u32*)(rd + 0x1c);
            if ((mask & 1) == 0) {
                return;
            }
        }
        *(int*)(rd + 0xaac) = 4;
        *(int*)(rd + 0xaa8) = 0;
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_acquire_nearest_target 0x8013af64
// Picks the nearest player (and squad leader) as the actor's target, unless a
// scripted target-selector callback overrides it. Returns the chosen entity.
// ---------------------------------------------------------------------------
int aiactor_acquire_nearest_target(int actor_in, int preferred, int mode)
{
    float dx, dy, dz, distSq;
    int   handler;
    int   actor;
    int*  leader;
    int   best;
    int   idx;
    int   poolOff;
    int   stateOff;
    int   remaining;
    int*  cdef;
    float bestDist;
    unsigned long long frame;
    (void)preferred; (void)actor_in;

    frame  = __save_gpr_28();
    actor  = (int)((unsigned long long)frame >> 0x20);
    cdef   = *(int**)(actor + 0x7c);
    best   = 0;

    if (player_ai_is_heavy_weapon((int)*(short*)((int)cdef + 0x4a)) != 0) {
        FUN_80380194();
        __restore_gpr_28();
        return 0;
    }

    if (*(int*)(cdef[0x2fc] + 0x78) != 0) {
        (**(void(**)(int))(cdef[0x2fc] + 0x78))(actor);
        __restore_gpr_28();
        return 0;
    }

    if (((cdef[5] & 0x82000U) != 0) &&
        (*(int*)(*(int*)(DAT_8061208c + 0x3c) + cdef[0x27b] * 0x48 + 0x10) != 0)) {
        handler = *(int*)(*(int*)(DAT_8061208c + 0x3c) + cdef[0x27b] * 0x48 + 0x10);
        (**(void(**)(int))(&DAT_80472ef8 + handler * 4))(actor);
        __restore_gpr_28();
        return 0;
    }

    if ((DAT_805015bd == '\n') &&
        (((1 < mode - 3U && (mode != 1)) && (actor == *(int*)(DAT_80611d78 + 0x14))))) {
        leader = (int*)FUN_8027ea98();
        if (1 < DAT_80612610 || ((leader != (int*)0) && (cdef != leader))) {
            best      = -1;
            idx       = 0;
            poolOff   = 0;
            stateOff  = 0;
            remaining = DAT_80612610;
            bestDist  = FLOAT_80615d24;
            if (0 < DAT_80612610) {
                do {
                    if (*(int*)(DAT_80611d78 + stateOff + 0x114) != 0x10) {
                        int obj = *(int*)(DAT_80611d90 + poolOff);
                        dx = *(float*)(obj + 100)  - *(float*)(actor + 100);
                        dy = *(float*)(obj + 0x68) - *(float*)(actor + 0x68);
                        dz = *(float*)(obj + 0x6c) - *(float*)(actor + 0x6c);
                        distSq = dz * dz + dx * dx + dy * dy;
                        if (distSq < bestDist) {
                            best     = idx;
                            bestDist = distSq;
                        }
                    }
                    idx      = idx + 1;
                    poolOff  = poolOff + 0xea0;
                    stateOff = stateOff + 0x141c;
                    remaining = remaining + -1;
                } while (remaining != 0);
            }
            if (leader != (int*)0) {
                int lobj = *leader;
                dx = *(float*)(lobj + 100)  - *(float*)(actor + 100);
                dy = *(float*)(lobj + 0x68) - *(float*)(actor + 0x68);
                dz = *(float*)(lobj + 0x6c) - *(float*)(actor + 0x6c);
                distSq = dz * dz + dx * dx + dy * dy;
                if (distSq < bestDist) {
                    best     = leader[2];
                    bestDist = distSq;
                }
            }
            if (best == -1) {
                poolOff   = 0;
                idx       = 0;
                remaining = DAT_80612610;
                if (0 < DAT_80612610) {
                    do {
                        int obj = *(int*)(DAT_80611d90 + poolOff);
                        dx = *(float*)(obj + 100)  - *(float*)(actor + 100);
                        dy = *(float*)(obj + 0x68) - *(float*)(actor + 0x68);
                        dz = *(float*)(obj + 0x6c) - *(float*)(actor + 0x6c);
                        distSq = dz * dz + dx * dx + dy * dy;
                        if ((idx == 0) || (distSq < bestDist)) {
                            bestDist = distSq;
                        }
                        idx      = idx + 1;
                        poolOff  = poolOff + 0xea0;
                        remaining = remaining + -1;
                    } while (remaining != 0);
                }
            }
        }
    }
    __restore_gpr_28();
    return best;
}

// ---------------------------------------------------------------------------
// aiactor_evaluate_target_los 0x8013b254
// Range + LOS gate deciding whether the current target is engageable this
// frame; various mode/flag combinations short-circuit engagement.
// ---------------------------------------------------------------------------
void aiactor_evaluate_target_los(void)
{
    bool  keepEngage;
    int   valid;
    int   target;
    bool  losOk;
    int   rd;
    double hiRange;
    double curRange;
    double spread;
    int   frame;

    frame    = __save_gpr_28();
    hiRange  = (double)FLOAT_80615ca8;
    rd       = *(int*)(frame + 0x7c);
    curRange = (double)*(float*)(rd + 0x9bc);

    if ((((*(int*)(rd + 4) == 0) &&
          ((valid = FUN_8013dd58(), *(int*)(rd + 4) == 0 || (valid == 0)))) ||
         ((*(u32*)(*(int*)(rd + 0xbf0) + 0x54) & 0x200) != 0)) ||
        (((*(u32*)(*(int*)(rd + 0xbf0) + 0x58) & 1) != 0 && (valid = FUN_8013dd58(), valid != 0)))) {
        __restore_gpr_28();
        return;
    }

    valid = FUN_8013dd58();
    if (valid != 0) {
        target     = *(int*)(rd + 4);
        keepEngage = true;
        if (*(int*)(target + 0x20) == 8) {
            spread   = (double)(*(float*)(*(int*)(target + 0x7c) + 0xb28) / FLOAT_80615d4c);
            curRange = (double)(float)(curRange - spread);
            hiRange  = (double)(float)(hiRange + spread);
            if (curRange < (double)FLOAT_80615cac) {
                curRange = (double)FLOAT_80615cac;
            }
        }
        if (*(float*)(rd + 0xaf4) < FLOAT_80615d2c) {
            curRange = (double)FLOAT_80615cac;
        }
        losOk = FUN_8013ef50(curRange, frame, (float*)(target + 100));
        if (((*(u32*)(rd + 0x14) & 0x1000000) != 0) &&
            ((((*(int*)(rd + 0x20) == 2 || (*(int*)(rd + 0x20) == 5)) && (losOk)) &&
              (*(float*)(rd + 0xaf4) <
               (float)((double)FLOAT_80615d50 * hiRange) * (float)((double)FLOAT_80615d50 * hiRange))))) {
            __restore_gpr_28();
            return;
        }
        if (((*(u32*)(*(int*)(rd + 0xbf0) + 0x58) & 0x100) != 0) &&
            (FLOAT_80615d54 < ABS(*(float*)(*(int*)(rd + 4) + 0x68) - *(float*)(frame + 0x68)))) {
            keepEngage = false;
        }
        if (((keepEngage) &&
             ((*(int*)(rd + 0x94) == 0x12 ||
               (*(float*)(rd + 0xaf4) <
                (float)((double)FLOAT_80615d58 * hiRange) * (float)((double)FLOAT_80615d58 * hiRange))))) &&
            (losOk)) {
            __restore_gpr_28();
            return;
        }
    }
    if ((*(int*)(rd + 0xb60) != DAT_80611928 - DAT_806108e4) &&
        (*(float*)(rd + 0xaf4) <
         (float)((double)FLOAT_80615d10 * hiRange) * (float)((double)FLOAT_80615d10 * hiRange))) {
        FUN_8013dd58();
    }
    __restore_gpr_28();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_combat_death_behavior 0x8013b4c4
// Runs on the frame an actor dies in combat: clears visual state, evaluates the
// death animation, tunes the ragdoll launch impulse, drops all weapons and
// spawns death fx for ranged kills.
// ---------------------------------------------------------------------------
void aiactor_combat_death_behavior(u64 param_1, double param_2, double param_3, double param_4,
                                   u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    float fVar1, fVar2, fVar3;
    int*  frame;
    int   actor;
    u32   mask;
    float* pImpulse;
    u32*  pVec;
    int*  targetObj;
    int*  rd;
    int   phaseRes;
    u64   stance;
    (void)param_1;

    frame   = (int*)FUN_803826e8();
    targetObj = (int*)0;
    actor   = *frame;
    rd      = *(int**)(actor + 0x7c);
    phaseRes = FUN_8013e9cc(actor, 2, frame, 0);
    if (phaseRes == -1) {
        FUN_80382734();
        return;
    }
    FUN_80142cdc(actor);
    if (DAT_805015be == '\x12') {
        FUN_80280594(frame);
    }
    phaseRes = frame[1];
    if ((phaseRes != 0) && (*(int*)(phaseRes + 0x20) == 8)) {
        targetObj = *(int**)(phaseRes + 0x7c);
    }
    render_instance_clear_visual_state(*(int*)(actor + 4));
    FUN_80234a04(targetObj, rd);
    if ((int*)frame[0xc] != (int*)0) {
        rd[0x2d5] = *(int*)frame[0xc];
        rd[0x2d6] = *(int*)(frame[0xc] + 4);
        rd[0x2d7] = *(int*)(frame[0xc] + 8);
    }
    phaseRes = FUN_8002588c(actor, 0xaf4);
    if (phaseRes != 0) {
        int* ctrl = (int*)FUN_80025868(actor);
        u32  hit  = anim_event_find_nth_by_mask(*ctrl, 0x200, 1);
        int  frameQ[12];
        FUN_80022ed8((int)ctrl, frameQ, (int*)0);
        if (frameQ[0] < (int)hit) {
            FUN_8013f5c0(actor, 1, 0);
        }
    }
    player_ai_combat_behavior_tick();
    if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
        mask = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
        if ((mask & 1) != 0) goto set_stance;
    }
    else {
set_stance:
        rd[0x2ab] = 1;
        rd[0x2aa] = 0;
    }
    stance = aiactor_set_stance_flags_ret(actor, 8);
    phaseRes = FUN_80025950(actor, 0x10000, 0x8000, 0);
    if (phaseRes == 0) {
        player_ai_drop_all_weapons_ex(stance, param_2, param_3, param_4, param_5,
                                      param_6, param_7, param_8, rd);
    }

    fVar2 = FLOAT_80615d3c;
    if (frame[0xc] != 0) {
        *(float*)(frame[0xc] + 4) = FLOAT_80615cac;
        pImpulse = (float*)frame[0xc];
        fVar1 = *pImpulse;
        fVar3 = pImpulse[2] * pImpulse[2] + fVar1 * fVar1 + pImpulse[1] * pImpulse[1];
        if (fVar2 < fVar3) {
            fVar2 = 1.0f / SQRT_(fVar3);
            fVar2 = fVar2 * -(FLOAT_80615cdc * fVar3 * fVar2 * fVar2 - FLOAT_80615d14);
            fVar2 = FLOAT_80615ca8 * fVar2 * -(FLOAT_80615cdc * fVar3 * fVar2 * fVar2 - FLOAT_80615d14);
            *pImpulse = fVar1 * fVar2;
            *(float*)(frame[0xc] + 4) = *(float*)(frame[0xc] + 4) * fVar2;
            *(float*)(frame[0xc] + 8) = *(float*)(frame[0xc] + 8) * fVar2;
        }
        pVec = (u32*)frame[0xc];
        DAT_8040db48 = pVec[0];
        DAT_8040db4c = pVec[1];
        DAT_8040db50 = pVec[2];
    }
    pVec = (u32*)frame[10];
    if (pVec != (u32*)0) {
        DAT_8040db54 = pVec[0];
        DAT_8040db58 = pVec[1];
        DAT_8040db5c = pVec[2];
    }
    if ((player_ai_is_ranged_weapon((int)*(short*)(rd + 0x12)) != 0) && (DAT_805015bd == '\n')) {
        if (*(short*)(rd + 0x12) == 0x60) {
            character_fx_bone_particle_spawn();
        }
        else {
            character_find_nearest_bone();
        }
    }
    FUN_8013e9cc(actor, 3, frame, 0);
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_set_stance_flags 0x8013cb20
// Sets/clears the actor's stance-flag bits (+0x24) for a requested stance code.
// ---------------------------------------------------------------------------
void aiactor_set_stance_flags(int actor, u32 flag)
{
    int rd;

    rd = *(int*)(actor + 0x7c);
    switch (flag) {
    case 1:
        *(u32*)(rd + 0x24) = *(u32*)(rd + 0x24) | 1;
        *(u32*)(rd + 0x24) = *(u32*)(rd + 0x24) & 0xffffffe9;
        return;
    case 2:
        *(u32*)(rd + 0x24) = *(u32*)(rd + 0x24) | 2;
        return;
    case 4:
        *(u32*)(rd + 0x24) = *(u32*)(rd + 0x24) | 4;
        *(u32*)(rd + 0x24) = *(u32*)(rd + 0x24) & 0xffffffee;
        return;
    case 8:
        *(u32*)(rd + 0x24) = *(u32*)(rd + 0x24) & 0xffffffbf;
        *(u32*)(rd + 0x24) = *(u32*)(rd + 0x24) | 8;
        return;
    case 0x10:
        *(u32*)(rd + 0x24) = *(u32*)(rd + 0x24) | 0x10;
        *(u32*)(rd + 0x24) = *(u32*)(rd + 0x24) & 0xfffffffa;
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// aiactor_check_zone_target_players 0x8013cbd0
// Scans all active players (plus a special defend-target on level 0x11) for one
// in the actor's current zone / within alert radius; acquires it as target.
// Returns 1 if a target was found/acquired, else 0.
// ---------------------------------------------------------------------------
u32 aiactor_check_zone_target_players(int actor)
{
    float dx, dy, dz;
    u32   acquired;
    int   idx;
    int   poolOff;
    int   stateOff;
    int   remaining;
    int   defendEnt;
    int   rd;

    rd = *(int*)(actor + 0x7c);
    DAT_8061208c = level_get_info_cached(DAT_80612614);
    idx       = 0;
    poolOff   = 0;
    stateOff  = 0;
    remaining = DAT_80612610;
    if (0 < DAT_80612610) {
        do {
            if (DAT_80612614 == 100) {
                if (((*(u32*)(rd + 0x10) & 1) != 0) &&
                    (dx = *(float*)(DAT_80611d78 + stateOff + 0xa8) - *(float*)(actor + 100),
                     dy = *(float*)(DAT_80611d78 + stateOff + 0xac) - *(float*)(actor + 0x68),
                     dz = *(float*)(DAT_80611d78 + stateOff + 0xb0) - *(float*)(actor + 0x6c),
                     dz * dz + dx * dx + dy * dy < FLOAT_80615d3c)) {
                    return 1;
                }
            }
            else {
                if (((*(u32*)(rd + 0x10) & 0x1000) == 0) &&
                    (*(int*)(rd + 0x9b8) == *(int*)(DAT_80611d90 + poolOff + 0x678))) {
                    *(int*)(rd + 4) = aiactor_acquire_nearest_target(
                        actor, *(int*)(DAT_80611d90 + poolOff), 4);
                    return 1;
                }
                if ((*(u32*)(rd + 0x28) & 0x20000000) != 0) {
                    *(int*)(rd + 4) = aiactor_acquire_nearest_target(
                        actor, *(int*)(DAT_80611d90 + idx * 0xea0), 4);
                    return 1;
                }
                if (((*(u32*)(rd + 0x10) & 1) == 0) &&
                    (dx = *(float*)(DAT_80611d78 + stateOff + 0xa8) - *(float*)(actor + 100),
                     dy = *(float*)(DAT_80611d78 + stateOff + 0xac) - *(float*)(actor + 0x68),
                     dz = *(float*)(DAT_80611d78 + stateOff + 0xb0) - *(float*)(actor + 0x6c),
                     dz * dz + dx * dx + dy * dy < FLOAT_80615d3c)) {
                    return 1;
                }
            }
            idx      = idx + 1;
            poolOff  = poolOff + 0xea0;
            stateOff = stateOff + 0x141c;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    if ((DAT_80612614 == 0x11) && (defendEnt = FUN_8023c32c(0x50435a32), defendEnt != 0) &&  // 'PCZ2'
        (*(int*)(*(int*)(defendEnt + 0x7c) + 0x678) == *(int*)(rd + 0x9b8))) {
        *(int*)(rd + 4) = aiactor_acquire_nearest_target(actor, defendEnt, 4);
        acquired = 1;
    }
    else {
        acquired = 0;
    }
    return acquired;
}

// ---------------------------------------------------------------------------
// aiactor_combat_react_to_target 0x8013b80c
// The core per-frame combat reaction state machine: reacts to the current
// target entity (ctx carries the resolved target/leader), choosing bark/alert,
// flee, melee, drop-item, turn-to-face and stance transitions based on weapon
// class, char-def flags, LOS and game mode. Non-matching structural translation
// of a very large branch tree; behaviour, not byte layout, matched.
//   ctx[2] = candidate target entity   ctx[6] = char-def index
// ---------------------------------------------------------------------------
void aiactor_combat_react_to_target(u32 param_1, u32 param_2, int* ctx)
{
    int   iVar1;
    bool  bVar5;
    int   iVar2;
    u32   uVar3;
    int   actor;
    u32   uVar6;
    int   target;
    int*  rd;
    int   cdefIdx;
    double in_f2 = 0.0, in_f3 = 0.0, in_f4 = 0.0;   // incoming ps regs (death fx dir)
    u64   in_f5 = 0, in_f6 = 0, in_f7 = 0, in_f8 = 0;
    u64   fr;
    (void)param_1; (void)param_2;

    fr      = __save_gpr_25();
    actor   = (int)((unsigned long long)fr >> 0x20);
    target  = ctx[2];
    rd      = *(int**)(actor + 0x7c);
    cdefIdx = ctx[6];

    if ((target != 0) && (*(int*)(target + 0x20) != 8)) {
        target = 0;
    }
    if ((target != 0) &&
        (bVar5 = FUN_8013ef50((double)(float)rd[0x26f], actor, (float*)(target + 100)), !bVar5)) {
        rd[10] = rd[10] & 0xf7ffffff;
    }

    // Alert bark when a valid enemy target appears (coop only).
    if ((((DAT_805015bd == '\n') && (sound_is_playing(rd[0x3a4]) == 0)) && (ctx != (int*)0)) &&
        (((ctx[2] != 0 && (iVar2 = *(int*)(ctx[2] + 0x150), iVar2 != 0)) &&
          (uVar3 = player_ai_check_target_team(iVar2, (int)rd), uVar3 != 0)))) {
        if (DAT_805015bd == '\n') { uVar6 = rd[4]; uVar3 = rd[5]; }
        else                      { uVar6 = rd[6]; uVar3 = rd[7]; }
        if ((uVar3 & 3) == 0 && (uVar6 & 1) == 0) {
            FUN_80143754(actor);
        }
    }

    // Vehicle/mount target adoption.
    if (((DAT_805015bd == '\n') && (target != 0)) &&
        ((*(int*)(target + 0x20) == 0x200 &&
          ((((rd[5] & 0x800U) == 0 && (rd[0x11] != 1)) &&
            ((*(u32*)(*(int*)(target + 0x7c) + 8) & 1) != 0)))))) {
        rd[1] = target;
        if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
            uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
            if ((uVar3 & 1) == 0) goto after_mount;
        }
        rd[0x2ab] = 4;
        rd[0x2aa] = 0;
    }
after_mount:
    if (((rd != (int*)0) && (iVar2 = *rd, iVar2 != 0)) &&
        ((DAT_80612610 <= rd[2] &&
          (((*(u32*)(iVar2 + 0x28) & 0x100) != 0 || ((*(u32*)(iVar2 + 0x10) & 0xffff) == 0)))))) {
        goto face_and_finish;
    }

    // Flee reaction (scared char-def flag).
    if (((rd[0x31b] & 4U) != 0) &&
        ((rd[10] = rd[10] | 0x10000000, DAT_805015bd != '\x05' && ((rd[4] & 0x401U) == 0)))) {
        uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
        if (((((uVar3 & 0x1000) == 0) && (FUN_80364290((int)rd, 2) == 0)) &&
             ((rd[10] & 0x4000000U) == 0)) &&
            (player_ai_is_ranged_weapon((int)*(short*)((int)rd + 0x4a)) == 0)) {
            if ((((target != 0) &&
                  (player_ai_is_valid_weapon_type((int)*(short*)((int)rd + 0x4a)) == 0)) &&
                 ((*(int*)(target + 4) != 0 && ((*(u32*)(target + 0x28) & 0x80000000) == 0)))) &&
                ((*(int*)(target + 0x20) != 8 || ((*(u32*)(*(int*)(target + 0x7c) + 0x24) & 8) == 0)))) {
                rd[1] = target;
            }
            if (DAT_805015bd == '\n') {
                uVar3 = FUN_8027eb3c((int)rd);
                if (uVar3 == 0) {
                    if (((player_ai_is_valid_weapon_type((int)*(short*)((int)rd + 0x4a)) == 0) &&
                         (player_ai_is_explosive_weapon((int)*(short*)((int)rd + 0x4a)) == 0)) &&
                        (player_ai_is_sniper_weapon((int)*(short*)((int)rd + 0x4a)) == 0)) {
                        CHAR_ON_DEATH(fr, in_f2, in_f3, in_f4, in_f5, in_f6, in_f7, in_f8);
                    }
                }
                else {
                    if (FUN_8002588c(actor, 0x643) == 0) {
                        FUN_80023280((double)FLOAT_80615d60, (double)FLOAT_80615ca8);
                    }
                }
            }
            else {
                rd[0x2af] = rd[7];
                rd[0x2ae] = rd[6];
                if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
                    uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
                    if ((uVar3 & 1) == 0) goto face_and_finish;
                }
                rd[0x2ab] = 0x1000;
                rd[0x2aa] = 0;
            }
            goto face_and_finish;
        }
    }

    if (((rd[4] & 1U) != 0) && (rd[0x11] == 1)) {
        rd[10] = rd[10] | 0x8000;
        goto face_and_finish;
    }

    // Cower reaction (char-def flag 0x200).
    uVar3 = player_ai_char_def_get_flags(cdefIdx);
    if (((uVar3 & 0x200) != 0) && (DAT_805015bd != '\x05')) {
        uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
        if (((uVar3 & 0x1000) == 0) && ((rd[10] & 0x4000000U) == 0)) {
            if (((target != 0) &&
                 ((*(int*)(target + 0x20) == 8 &&
                   (*(int*)(*(int*)(target + 0x7c) + 0x84) != rd[0x21])))) &&
                (((*(u32*)(rd[0x2fc] + 0x54) & 0x800000) == 0 || (*(int*)(target + 0x150) == 0)))) {
                rd[1] = target;
            }
            rd[10] = rd[10] | 0x2000;
            if (DAT_805015bd == '\n') {
                FUN_8025ff4c(0, actor, rd[0x19e]);
            }
            if ((DAT_805015bd != '\n') ||
                ((rd[0x11] != 2 &&
                  (player_ai_is_ranged_weapon((int)*(short*)((int)rd + 0x4a)) == 0)))) {
                int f6, f7;
                if (DAT_805015bd == '\n') { f6 = rd[4]; f7 = rd[5]; }
                else                      { f6 = rd[6]; f7 = rd[7]; }
                rd[0x2af] = f7;
                rd[0x2ae] = f6;
                if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
                    uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
                    if ((uVar3 & 1) == 0) goto face_and_finish;
                }
                rd[0x2ab] = 0x1000;
                rd[0x2aa] = 0;
            }
            goto face_and_finish;
        }
    }

    if ((player_ai_is_ranged_weapon((int)*(short*)((int)rd + 0x4a)) != 0) && (DAT_805015bd == '\n')) {
        rd[10] = rd[10] | 0x8000;
        if ((rd[4] & 1U) != 0) {
            FUN_8025ff4c(0, actor, rd[0x19e]);
        }
        if ((rd[4] & 0x40000000U) != 0) {
            FUN_8025ff4c(0, actor, rd[0x19e]);
            goto face_and_finish;
        }
    }

    if ((((*(u32*)(rd[0x2fc] + 0x54) & 0x200) != 0) || ((rd[4] & 1U) != 0)) ||
        (FUN_8013ff28(actor) != 0)) {
        rd[10] = rd[10] | 0x8000;
        goto face_and_finish;
    }

    if ((((player_ai_is_story_mode((int)*(short*)(rd + 0x12)) != 0) && ((DAT_805015c0 & 0x40) != 0)) &&
         (DAT_805015d8 == 0x10)) ||
        (FUN_8013e9cc(actor, 1, ctx, 0) == -1)) {
        goto face_and_finish;
    }

    if ((target == 0) || (actor == target)) {
        uVar3 = 0;
    }
    else {
        bVar5 = FUN_8013ee44((double)FLOAT_80615cac, actor, (float*)(target + 100));
        uVar3 = countLeadingZeros((u32)bVar5);
        uVar3 = uVar3 >> 5;
    }

    if ((((*(u32*)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
           0x1c00) == 0) &&
         (((((FLOAT_80615cac < (float)rd[0x2d2] || (DAT_806117a4 == 1)) &&
             (FUN_80025cb4(actor, (int)fr, (int)ctx), DAT_805015bd != '\n')) &&
            ((DAT_805015bd != '\x05' && (DAT_805015bf != '\x02')))) && ((rd[10] & 0x10000000U) == 0)))) &&
        (((*(u32*)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
           0x10000) == 0 && (uVar3 == 0)))) {
        aiactor_turn_toward_entity(actor, target, 1);
    }

    uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
    if ((((((((uVar3 & 0x1c) != 0) || ((rd[5] & 0x9000000U) != 0)) || ((rd[7] & 0x44000U) != 0)) ||
           (((rd[4] & 1U) != 0) &&
            (player_ai_is_ranged_weapon((int)*(short*)((int)rd + 0x4a)) != 0))) &&
          ((target != 0 &&
            ((*(int*)(target + 0x20) == 8 &&
              (iVar1 = *(int*)(target + 0x7c), rd[0x21] != *(int*)(iVar1 + 0x84))))))) &&
         (DAT_805015be != '\x13')) &&
        ((DAT_805015bd != '\x02' ||
          ((rd[0x20] == 0 && ((rd[1] == 0 || (*(int*)(rd[1] + 0x20) == 8)))))))) {

        int zoneObj = *(int*)(target + 0x7c);
        if ((*(int*)(zoneObj + 8) < DAT_80612610) ||
            ((rd[1] == 0 || (FUN_8013dd58() == 0)))) {
            rd[1] = aiactor_acquire_nearest_target(actor, target, 3);
        }
        if (rd[1] == 0) {
            rd[1] = target;
        }
        {
            int tgt = rd[1];
            int zoneId;
            if ((*(int*)(tgt + 0x20) != 8) ||
                (iVar1 = *(int*)(*(int*)(tgt + 0x7c) + 0x678), iVar1 < 0)) {
                iVar1 = *(int*)(tgt + 0x110);
            }
            zoneId = iVar1;
            if (rd[0x1a1] != zoneId) {
                if (zoneId < 0) {
                    rd[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                }
                else {
                    rd[0x1a1] = zoneId;
                }
                rd[0x26b] = 0;
                rd[0x26a] = 0;
            }
            if (((DAT_805015be != '\x12') || (target != DAT_80611910)) ||
                (FLOAT_80615cdc * *(float*)(target + 0x1cc) < *(float*)(actor + 0x1cc))) {
                if ((*(u32*)(rd[0x2fc] + 0x54) & 0x4000) == 0) {
                    if (*(int*)(zoneObj + 8) < DAT_80612610) {
                        uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
                        if ((uVar3 & 0xc) == 0) {
                            if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
                                uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
                                if ((uVar3 & 1) == 0) goto near_finish;
                            }
                            rd[0x2ab] = 8;
                            rd[0x2aa] = 0;
                        }
                    }
                }
                else {
                    if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
                        uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
                        if ((uVar3 & 1) == 0) goto near_finish;
                    }
                    rd[0x2ab] = 0x200;
                    rd[0x2aa] = 0;
                }
            }
            else {
                uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
                if ((uVar3 & 0x800) == 0) {
                    if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
                        uVar3 = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
                        if ((uVar3 & 1) == 0) goto near_finish;
                    }
                    rd[0x2ab] = 0x800;
                    rd[0x2aa] = 0;
                }
            }
        }
    }
near_finish:
    if (DAT_805015bd == '\n') {
        int tgt = rd[1];
        if ((tgt == 0) || (*(int*)(tgt + 0x20) != 8)) {
            FUN_8025ff4c(0, actor, rd[0x19e]);
        }
        else {
            FUN_8025ff4c(0, actor, *(int*)(*(int*)(tgt + 0x7c) + 0x678));
        }
    }
face_and_finish:
    __restore_gpr_25();
    return;
}
