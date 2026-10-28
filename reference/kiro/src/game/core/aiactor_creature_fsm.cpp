// aiactor_creature_fsm.cpp - AI actor "creature/monster" per-frame state machines.
//
// This module holds the per-frame behavior ticks for several non-humanoid AI
// actor archetypes that share the common actor record layout used across the
// aiactor_* family (state word at +0x20 / int-index [8], pending-state at
// +0x9d0 / int-index [0x274], previous-state at +0x9d4 / [0x275], state-enter
// timestamp at +0x9d8 / [0x276], per-state timer at +0x9dc, cooldown gate at
// +0xb30, and the anim/sound plumbing shared with aiactor_script_fsm.cpp).
//
// Every tick begins with the shared prologue: retarget the visible teammate,
// commit any pending state transition (copy [0x274] -> [8], record prev/enter),
// and integrate the per-frame turn-rate accumulator (+0xa90) from the frame
// delta. The bodies then drive an archetype-specific FSM.
//
//  aiactor_melee_creature_tick     (0x802873bc): melee creature FSM. Idle/alert/
//    pain/attack states (0..4) with animation gating, per-state cooldown timers
//    seeded from the RNG, and a 3-shot projectile burst in the ranged state.
//  aiactor_creature_death_explode  (0x80287e8c): death/gib handler. On event
//    code 1 spawns an explosion at the actor's hit offset and flags the record.
//  aiactor_zombie_creature_tick    (0x80287f3c): zone-anchored creature FSM
//    (idle/approach/attack) that snaps Y to the resolved zone node and issues
//    two melee swipes each frame.
//  aiactor_morph_boss_tick         (0x802882c4): shapeshifter/morph boss FSM
//    (states 0..10): team-coded zone-sound swaps (ZPD4/ZMK1/ZPD5/ZMK2), scale
//    morph via bone matrix multiply, spark shedding, and a hand-off/teleport to
//    the paired player actor.
//  aiactor_morph_boss_damage_react (0x80289044): morph-boss damage/hit reaction
//    dispatcher: maps the incoming hit type to a pending state and triggers the
//    matching death effect / damage particle / zone sound.
//  aiactor_stealth_creature_tick   (0x80289574): stealth/cloak creature FSM
//    (idle/hidden/reveal) toggling render-visibility flags and stance.
//  aiactor_anim_play_forward       (0x80289898): thin forward to anim_play_scaled.
//  aiactor_beast_creature_tick     (0x802898d0): quadruped "beast" FSM (idle/
//    walk/turn/attack) driven by directional clip-id tables selected by facing.
//  aiactor_beast_play_locomotion   (0x8028a3cc): pick + play the beast's forward
//    or reverse locomotion clip based on the movement flag.
//  aiactor_beast_impact_react      (0x8028a4c0): beast impact reaction. On event
//    code 1 with hit-kind 0x1a, arm the stagger state (5) and its timer.
//
// Address range: 0x802873bc - 0x8028a53f
// Subsystem: game (AI actors)

#include "common.h"

extern "C" {

// --- raw actor-context accessors (still FUN_ in the IDB) -------------------
extern int  FUN_803826e8(void);   // current actor entity record (r3 = entity)
extern int  FUN_803826f0(void);   // current actor entity record (alt entry)
extern void FUN_80382734(void);   // tick epilogue A (restore fp regs)
extern void FUN_8038273c(void);   // tick epilogue B
extern int  FUN_8037caec(int classCode, int teamCode); // morph hand-off bind

// --- scalar / data globals -------------------------------------------------
extern unsigned char DAT_805015bd;   // co-op sub-mode selector ('\n' == co-op)
extern int   DAT_806109c0;           // frame delta ticks (signed, xor bias)
extern int   DAT_80611d54;           // active view/camera record base
extern int   DAT_80611d74;           // secondary player/actor record table base
extern int   DAT_80611d78;           // player record table base
extern int   DAT_80611824;           // clip-class -> table base
extern int   DAT_804d1f06;           // clip-class row index (byte)
extern int   DAT_80612610;           // active player count
extern int   DAT_80610ba0;           // character class descriptor table base
extern int   DAT_805cbbe8;           // 4-entry cached particle-emitter handle array
extern int   DAT_806127d0;           // cached particle-emitter handle (damage puff)
extern int   DAT_806127d4;           // cached particle-emitter handle (morph trail)

// morph-boss spawn vectors (rodata Vec3 triples)
extern int   DAT_803e05a0;           // morph puff A velocity.x
extern int   DAT_803e05a4;           // morph puff A velocity.y
extern int   DAT_803e05a8;           // morph puff A velocity.z
extern int   DAT_803e05ac;           // morph puff B velocity.x
extern int   DAT_803e05b0;           // morph puff B velocity.y
extern int   DAT_803e05b4;           // morph puff B velocity.z

// beast directional clip-id tables (indexed by facing*8; stride 0x20)
extern int   DAT_80473138;           // beast: attack/telegraph clip table
extern int   DAT_8047313c;           // beast: recover clip table
extern int   DAT_80473140;           // beast: idle/loop clip table
extern int   DAT_80473144;           // beast: bark clip table
extern int   DAT_80473148;           // beast: swipe clip table
extern int   DAT_8047314c;           // beast: turn-left clip table
extern int   DAT_80473150;           // beast: turn-right clip table
extern int   DAT_80473154;           // beast: walk clip table

// --- float / double constants (rodata) -------------------------------------
extern const float  FLOAT_806108ec;  // per-frame time-scale numerator
// melee-creature block (0x806193xx)
extern const float  FLOAT_8061939c;  // 1.0 (anim rate / default timer)
extern const float  FLOAT_806193a0;  // rng denominator
extern const float  FLOAT_806193a4;  // projectile accuracy base
extern const float  FLOAT_806193a8;  // rng threshold (anim variant gate)
extern const float  FLOAT_806193b8;  // 0.0
extern const float  FLOAT_806193bc;  // cooldown timer scale (pain)
extern const float  FLOAT_806193c0;  // anim rate (approach)
extern const float  FLOAT_806193d8;  // scale floor (morph shrink)
extern const float  FLOAT_806193e0;  // spring rest length
extern const float  FLOAT_806193f0;  // min distance-sq for lunge
extern const float  FLOAT_806193fc;  // cooldown timer scale (attack)
extern const float  FLOAT_8061940c;  // anim rate (turn/blend)
extern const float  FLOAT_8061941c;  // projectile spread scale
extern const float  FLOAT_80619418;  // rng threshold (idle fidget)
extern const float  FLOAT_8061942c;  // cooldown timer scale (alert)
extern const float  FLOAT_80619428;  // idle facing pitch
extern const float  FLOAT_80619430;  // cooldown timer scale (base)
extern const float  FLOAT_80619434;  // recoil bias
extern const double DOUBLE_806193b0; // rng bias
extern const double DOUBLE_806193d0; // frame-delta bias
// zombie / zone-creature block
extern const float  FLOAT_8061943c;  // zone node Y bias
extern const float  FLOAT_80619438;  // death explosion normal.y
extern const float  FLOAT_80619440;  // attack range-sq
// morph-boss block (0x806194xx and shared)
extern const float  FLOAT_80619444;  // morph puff offset.x (ZPD4)
extern const float  FLOAT_80619448;  // morph puff offset.y (ZPD4)
extern const float  FLOAT_8061944c;  // morph puff offset.z (ZPD4)
extern const float  FLOAT_80619450;  // morph puff offset.y (ZPD5)
extern const float  FLOAT_80619454;  // morph puff offset.z (ZPD5)
extern const float  FLOAT_80619458;  // morph puff drag
extern const float  FLOAT_8061945c;  // spark scale divisor
extern const float  FLOAT_80619460;  // spark base offset.x
extern const float  FLOAT_80619464;  // spark base offset.y
extern const float  FLOAT_80619468;  // spark base offset.z
extern const float  FLOAT_8061946c;  // morph charge-time floor
extern const float  FLOAT_80619470;  // per-frame scale decay
extern const float  FLOAT_80619474;  // damage-react time floor
// stealth-creature block
extern const float  FLOAT_80619398;  // stealth anim rate
// beast block (0x806194xx)
extern const float  FLOAT_80619478;  // 0.0 (beast)
extern const float  FLOAT_8061947c;  // 1.0 (beast anim rate)
extern const float  FLOAT_80619480;  // rng denominator (beast)
extern const float  FLOAT_80619484;  // aggression threshold B
extern const float  FLOAT_80619488;  // beast anim rate (idle->attack)
extern const float  FLOAT_8061948c;  // aggression threshold A
extern const float  FLOAT_80619490;  // beast anim rate (walk)
extern const float  FLOAT_80619494;  // cooldown scale (dir 2)
extern const float  FLOAT_80619498;  // cooldown scale (dir 0/1)
extern const float  FLOAT_8061949c;  // rng gate (recover)
extern const float  FLOAT_806194a0;  // cooldown scale (recover)
extern const float  FLOAT_806194b8;  // beast locomotion rate A
extern const double DOUBLE_806194a8; // frame-delta bias (beast)
extern const double DOUBLE_806194b0; // rng bias (beast)

// --- named cross-module helpers (call-site typed; still FUN_ in IDB) --------
extern unsigned int rng_counter_tick(void);            // pull 32 rng bits
extern unsigned long long anim_play_scaled(unsigned long long ctx, double speed);
extern int  anim_is_clip_active(int actor, int clipId);
extern int  anim_primary_clip_userflags(int actor, int clipId);
extern int  anim_primary_clip_time_left(double rate, int animCtx);
extern int  anim_active_slot_userflags(int animCtx);
extern int  anim_sample_keyframe(int clip, int a, int b, float* out);
extern void anim_set_clip_speed_fields(double rate, int** actorCtx);

extern int  aiactor_retarget_visible_teammate(void);
extern void aiactor_facing_update(void);
extern void aiactor_turn_toward_leader(void);
extern unsigned long long aiactor_turn_toward_entity(int actor, int target, int mode);
extern void aiactor_leader_zone_update(int actor);
extern int  aiactor_follow_zone_path(void);
extern int  aiactor_check_zone_target_players(int actor);
extern void aiactor_movement_tick(unsigned long long a, double b, double c, double d,
                                  double e, double f, double g, unsigned long long h);
extern void aiactor_melee_swipe(int actor, int side, int mode);
extern void aiactor_set_stance_flags(int actor, unsigned int flag);
extern int  aiactor_find_player_by_team(int team);

extern int* soundpad_resolve_zone_node(int zoneIdx);
extern int  soundpad_find_zone_by_id(int id);

extern void particle_cached_enable(int* pCachedRef);
extern void particle_cached_disable(int* pCachedRef);
extern void particle_cached_set_position(int* pCachedRef, float* pPos);
extern void particle_cached_set_velocity(int* pCachedRef, float* pVel);
extern void particle_cached_set_drag(double drag, int* pCachedRef);

extern int* entity_get_world_position(int entity);
extern void entity_set_render_type(int entity, void* renderFilter);
extern void entity_create_attachment(unsigned long long a, double b, double c, double d,
                                     double e, double f, double g, unsigned long long h,
                                     int entity);

extern void character_clear_damage_state(int entity);
extern void character_set_damage_bone(int entity, int bone);
extern void character_death_effect_trigger(int* charData);
extern void character_damage_spawn_effects(void);
extern void character_nametag_anim_init(void);
extern int  character_get_damage_vertex_pos(void);

extern void sound_init_play_params(int paramsPtr);
extern unsigned int sound_play_request(int* params);

extern void player_ai_projectile_spawn_simple(double accuracy, double zero, int weaponId,
                                              int posPtr, int* direction, int owner,
                                              int* actorRec, int weaponMode, int a9,
                                              int soundDef, int a11, int flags);
extern void player_ai_explosion_spawn(int fxId, int sndId, float* position, float* normal,
                                      int entity, int room, int playSound);
extern void player_ai_teleport_wrapper(int actorRec, int zone, int flags);

extern void fx_spawn_explosion_particle(float* pPosition);
extern void spark_spawn_fading(void);
extern void spark_set_last_lifetime(double lifetime);
extern void spark_set_last_scale(double scale);

extern double math_sqrt(double x);
extern void mtx4_set_scale(double sx, double sy, double sz);
extern void mtx4_multiply(float* out, float* a, float* b);

// damage-filter callbacks passed by pointer to entity_set_render_type
extern void damage_filter_invincible(int* damageCtx);
extern void damage_cap_by_max_health(int* damageCtx);

} // extern "C"

// Reconstruct the classic Ghidra "(double)CONCAT44(0x43300000, bits) - bias"
// idiom into a plain float in [0,1)-ish rng space: ((double)magic_bits) - bias.
static inline float rng_unit(unsigned int bits, double bias) {
    return (float)((double)CONCAT44(0x43300000, bits) - bias);
}

// ---------------------------------------------------------------------------
// Shared prologue: commit pending state transition + integrate turn accumulator.
// actorRecInt = actor record as int* (state fields via int index).
// ---------------------------------------------------------------------------
static void creature_state_prologue_i(int* rec) {
    if (-1 < rec[0x274]) {
        if (rec[0x274] != rec[8]) {
            rec[0x276] = (int)FLOAT_806193b8;
            rec[0x275] = rec[8];
            rec[8] = rec[0x274];
        }
        rec[0x274] = -1;
    }
    if ((rec[0xb] & 0x80) == 0) {
        rec[0x2a4] = (int)FLOAT_806193b8;
    } else {
        unsigned int biasBits = (unsigned int)DAT_806109c0 ^ 0x80000000u;
        rec[0x2a4] = (int)(FLOAT_806108ec *
                           (FLOAT_8061939c / rng_unit(biasBits, DOUBLE_806193d0)) +
                           (float)rec[0x2a4]);
    }
}

// Byte-offset variant of the prologue (record accessed as raw base address).
static void creature_state_prologue_b(int recBase) {
    if (-1 < *(int*)(recBase + 0x9d0)) {
        if (*(int*)(recBase + 0x9d0) != *(int*)(recBase + 0x20)) {
            *(float*)(recBase + 0x9d8) = FLOAT_806193b8;
            *(int*)(recBase + 0x9d4) = *(int*)(recBase + 0x20);
            *(int*)(recBase + 0x20) = *(int*)(recBase + 0x9d0);
        }
        *(int*)(recBase + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(recBase + 0x2c) & 0x80) == 0) {
        *(float*)(recBase + 0xa90) = FLOAT_806193b8;
    } else {
        unsigned int biasBits = (unsigned int)DAT_806109c0 ^ 0x80000000u;
        *(float*)(recBase + 0xa90) =
            FLOAT_806108ec *
                (FLOAT_8061939c / rng_unit(biasBits, DOUBLE_806193d0)) +
            *(float*)(recBase + 0xa90);
    }
}

// Set the pending int-indexed state transition (records prev + enter time).
static void set_pending_state_i(int* rec, int newState) {
    if (rec[8] != newState) {
        rec[0x275] = rec[8];
        rec[8] = newState;
        rec[0x276] = (int)FLOAT_806193b8;
        rec[0x274] = rec[8];
    }
}

// Set the pending byte-offset state transition.
static void set_pending_state_b(int recBase, int newState) {
    if (*(int*)(recBase + 0x20) != newState) {
        *(int*)(recBase + 0x9d4) = *(int*)(recBase + 0x20);
        *(int*)(recBase + 0x20) = newState;
        *(float*)(recBase + 0x9d8) = FLOAT_806193b8;
        *(int*)(recBase + 0x9d0) = *(int*)(recBase + 0x20);
    }
}

// ===========================================================================
// 0x802873bc  aiactor_melee_creature_tick
// Melee creature FSM: idle(0)/alert(1)/attack-approach(2)/hurt(3->ranged)/
// stagger(4). Uses the int-indexed actor record. The hurt state (iVar3<4 branch)
// runs a lunge + 3-shot projectile burst toward the leader.
// ===========================================================================
void aiactor_melee_creature_tick(void) {
    float fVar1;
    int iVar3;
    int iVar4;
    int iVar7;
    int* rec;
    int local_state_tmp;
    int local_slot[6];
    float local_f0;
    int local_ec, local_e8, local_e4;
    unsigned char local_e0;

    iVar4 = FUN_803826e8();
    rec = *(int**)(iVar4 + 0x7c);
    aiactor_retarget_visible_teammate();
    creature_state_prologue_i(rec);

    fVar1 = FLOAT_80619428;
    rec[0xc] = *(int*)(iVar4 + 0x64);
    rec[0xd] = *(int*)(iVar4 + 0x68);
    rec[0xe] = *(int*)(iVar4 + 0x6c);
    *(float*)(iVar4 + 0x68) = fVar1;
    *(unsigned int*)(iVar4 + 0x28) &= 0xffffff9f;

    iVar3 = rec[8];
    if (iVar3 == 2) {
        aiactor_turn_toward_leader();
        *(int*)(*(int*)(iVar4 + 4) + 0x30) = 1;
        iVar7 = 0;
        for (iVar3 = 0; iVar3 < 4; iVar3++) {
            local_state_tmp = *(int*)((int)&DAT_805cbbe8 + iVar7);
            particle_cached_set_drag((double)FLOAT_806193b8, &local_state_tmp);
            iVar7 += 4;
        }
        if ((float)rec[0x2cc] <= (float)rec[0x278]) {
            if (anim_is_clip_active(iVar4, 0x132) == 0 &&
                anim_is_clip_active(iVar4, 0x137) == 0 &&
                anim_is_clip_active(iVar4, 0x130) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
            } else if (anim_active_slot_userflags(*(int*)(iVar4 + 4)) != 0) {
                unsigned int rc = rng_counter_tick();
                (void)(rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0 <= FLOAT_80619418);
                anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
            }
        } else if (anim_active_slot_userflags(*(int*)(iVar4 + 4)) != 0) {
            if (anim_is_clip_active(iVar4, 0x135) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
            } else {
                unsigned int rc = rng_counter_tick();
                rec[0x278] = (int)(FLOAT_80619430 * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0) +
                                   FLOAT_80619430 + (float)rec[0x2cc]);
                set_pending_state_i(rec, 1);
            }
        }
    } else if (iVar3 < 2) {
        if (iVar3 == 0) {
            entity_set_render_type(iVar4, (void*)&damage_filter_invincible);
            anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
            unsigned int rc = rng_counter_tick();
            rec[0x278] = (int)(FLOAT_8061939c + (float)rec[0x2cc] +
                               rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0);
            set_pending_state_i(rec, 4);
            *(int*)(*(int*)(iVar4 + 4) + 0x30) = 1;
            character_nametag_anim_init();
            aiactor_turn_toward_leader();
        } else if (-1 < iVar3) {
            aiactor_turn_toward_leader();
            *(int*)(*(int*)(iVar4 + 4) + 0x30) = 2;
            if ((float)rec[0x2cc] <= (float)rec[0x278]) {
                if (anim_primary_clip_time_left((double)FLOAT_8061939c, *(int*)(iVar4 + 4)) != 0) {
                    iVar7 = 0;
                    for (iVar3 = 0; iVar3 < 4; iVar3++) {
                        local_state_tmp = *(int*)((int)&DAT_805cbbe8 + iVar7);
                        particle_cached_set_drag((double)FLOAT_8061939c, &local_state_tmp);
                        iVar7 += 4;
                    }
                }
                if (anim_is_clip_active(iVar4, 0x133) == 0 &&
                    anim_is_clip_active(iVar4, 0x12f) == 0 &&
                    anim_is_clip_active(iVar4, 0x136) == 0) {
                    anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
                } else if (anim_active_slot_userflags(*(int*)(iVar4 + 4)) != 0) {
                    unsigned int rc = rng_counter_tick();
                    (void)(FLOAT_806193a8 <= rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0);
                    anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
                    rc = rng_counter_tick();
                    rec[0x278] = (int)(FLOAT_8061942c * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0) +
                                       FLOAT_8061942c + (float)rec[0x2cc]);
                    set_pending_state_i(rec, 3);
                }
            } else if (anim_active_slot_userflags(*(int*)(iVar4 + 4)) != 0) {
                if (anim_is_clip_active(iVar4, 0x131) == 0) {
                    anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
                } else {
                    unsigned int rc = rng_counter_tick();
                    rec[0x278] = (int)(FLOAT_806193fc * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0) +
                                       FLOAT_806193fc + (float)rec[0x2cc]);
                    set_pending_state_i(rec, 4);
                }
            }
        }
    } else if (iVar3 == 4) {
        aiactor_turn_toward_leader();
        *(int*)(*(int*)(iVar4 + 4) + 0x30) = 1;
        iVar7 = 0;
        for (iVar3 = 0; iVar3 < 4; iVar3++) {
            local_state_tmp = *(int*)((int)&DAT_805cbbe8 + iVar7);
            particle_cached_set_drag((double)FLOAT_806193b8, &local_state_tmp);
            iVar7 += 4;
        }
        if (anim_is_clip_active(iVar4, 0x134) == 0) {
            anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
        }
        if ((float)rec[0x278] < (float)rec[0x2cc] &&
            anim_active_slot_userflags(*(int*)(iVar4 + 4)) != 0) {
            unsigned int rc = rng_counter_tick();
            if (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0 <= FLOAT_806193a8) {
                rc = rng_counter_tick();
                rec[0x278] = (int)(FLOAT_806193bc * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0) +
                                   FLOAT_806193bc + (float)rec[0x2cc]);
                set_pending_state_i(rec, 2);
            } else {
                rc = rng_counter_tick();
                rec[0x278] = (int)(FLOAT_80619430 * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0) +
                                   FLOAT_80619430 + (float)rec[0x2cc]);
                set_pending_state_i(rec, 1);
            }
        }
    } else if (iVar3 < 4) { // state 3: hurt / ranged lunge
        if ((float)rec[0x2cc] <= (float)rec[0x278]) {
            *(float*)(iVar4 + 0xc4) =
                *(float*)(iVar4 + 0xc4) +
                -(FLOAT_806193fc * ((float)rec[0x278] - (float)rec[0x2cc]) - FLOAT_80619434);
            *(int*)(iVar4 + 0x70) = *(int*)(iVar4 + 0xc4);
        } else {
            aiactor_turn_toward_leader();
            float dx = *(float*)(DAT_80611d74 + 0xac) - *(float*)(iVar4 + 0x68);
            float dy = *(float*)(DAT_80611d74 + 0xa8) - *(float*)(iVar4 + 0x64);
            float dz = *(float*)(DAT_80611d74 + 0xb0) - *(float*)(iVar4 + 0x6c);
            float distSq = dz * dz + dy * dy + dx * dx;
            float dirX = dx, dirY = dy, dirZ = dz;
            if (FLOAT_806193f0 < distSq) {
                float inv = 1.0f / (float)math_sqrt((double)distSq);
                float k = inv * -(FLOAT_806193a8 * distSq * inv * inv - FLOAT_806193e0);
                float scaled = FLOAT_8061939c * k * -(FLOAT_806193a8 * distSq * k * k - FLOAT_806193e0);
                dirY = dy * scaled;
                dirX = dx * scaled;
                dirZ = dz * scaled;
            }
            int* pos = entity_get_world_position(iVar4);
            sound_init_play_params((int)local_slot);
            local_e0 = 1;
            local_slot[0] = 0x561;
            local_f0 = FLOAT_806193e0;
            local_ec = pos[0];
            local_e8 = pos[1];
            local_e4 = pos[2];
            sound_play_request(local_slot);

            for (iVar3 = 0; iVar3 < 3; iVar3++) {
                unsigned int rc = rng_counter_tick();
                float ox = FLOAT_806193bc * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0 - FLOAT_806193a8);
                rc = rng_counter_tick();
                float oy = FLOAT_806193bc * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0 - FLOAT_806193a8);
                rc = rng_counter_tick();
                float oz = FLOAT_806193bc * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0 - FLOAT_806193a8);
                float shot[3];
                shot[0] = FLOAT_8061941c * ox + dirX;
                shot[1] = FLOAT_8061941c * oy + dirY;
                shot[2] = FLOAT_8061941c * oz + dirZ;
                player_ai_projectile_spawn_simple(
                    (double)FLOAT_806193a4, (double)FLOAT_806193b8, 0x2b, iVar4 + 0x64,
                    (int*)shot, iVar4, rec, 2, 0, *(int*)(iVar4 + 0x34), 0, 0x10);
            }
            unsigned int rc = rng_counter_tick();
            rec[0x278] = (int)(FLOAT_806193fc * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0) +
                               FLOAT_806193fc + (float)rec[0x2cc]);
            set_pending_state_i(rec, 4);
        }
    }

    if (rec[1] != 0) {
        int leaderRec = *(int*)(rec[1] + 0x7c);
        if (leaderRec != 0 && (*(unsigned int*)(leaderRec + 0x24) & 8) != 0) {
            int skip = 0;
            if (rec[0x2ab] == 1 && rec[0x2aa] == 0) {
                unsigned int uVar6 = (DAT_805015bd == '\n') ? (unsigned int)rec[5]
                                                            : (unsigned int)rec[7];
                if ((uVar6 & 1) == 0) {
                    skip = 1;
                }
            }
            if (!skip) {
                rec[0x2ab] = 0x10;
                rec[0x2aa] = 0;
            }
        }
    }
    FUN_80382734();
}

// ===========================================================================
// 0x80287e8c  aiactor_creature_death_explode
// Death/gib handler. event==1 -> spawn explosion at the actor's hit offset and
// set the "exploded" flag (0x100000) on the actor record.
// ===========================================================================
int aiactor_creature_death_explode(int entity, int event) {
    int ret = 0;
    float pos[3];
    float normal[3];

    normal[1] = FLOAT_80619438;
    normal[0] = FLOAT_806193b8;
    normal[2] = FLOAT_806193b8;

    if (event == 1) {
        int rec = *(int*)(entity + 0x7c);
        *(float*)(entity + 0x1cc) = FLOAT_806193b8;
        pos[0] = *(float*)(entity + 0x64) + *(float*)(entity + 0x1a8);
        pos[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
        pos[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
        player_ai_explosion_spawn(0x2d, 0x17, pos, normal, entity, entity, 1);
        ret = -1;
        *(unsigned int*)(rec + 0x2c) |= 0x100000;
    }
    return ret;
}

// ===========================================================================
// 0x80287f3c  aiactor_zombie_creature_tick
// Zone-anchored creature FSM (idle/approach/attack) that snaps Y to the resolved
// zone node height and issues two melee swipes each frame.
// ===========================================================================
void aiactor_zombie_creature_tick(void) {
    float fVar1, fVar2;
    int iVar3;
    int iVar4;
    int iVar6;
    int* piVar5;
    float local_pos[4];

    iVar4 = FUN_803826f0();
    iVar6 = *(int*)(iVar4 + 0x7c);
    aiactor_retarget_visible_teammate();
    creature_state_prologue_b(iVar6);

    piVar5 = soundpad_resolve_zone_node(*(int*)(iVar6 + 0x678));
    fVar1 = FLOAT_8061943c;
    if (*(int*)(iVar6 + 4) != 0) {
        iVar3 = *(int*)(iVar6 + 0x20);
        if (iVar3 == 2) {
            *(float*)(iVar4 + 0x68) = FLOAT_8061943c + (float)piVar5[4];
            if (anim_is_clip_active(iVar4, 0x139) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
            } else {
                fVar1 = *(float*)(iVar4 + 0x64) - *(float*)(*(int*)(iVar6 + 4) + 0x64);
                fVar2 = *(float*)(iVar4 + 0x6c) - *(float*)(*(int*)(iVar6 + 4) + 0x6c);
                if (fVar1 * fVar1 + fVar2 * fVar2 < FLOAT_80619440) {
                    anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
                    set_pending_state_b(iVar6, 3);
                }
            }
        } else if (iVar3 < 2) {
            if (iVar3 == 0) {
                if (aiactor_check_zone_target_players(iVar4) != 0) {
                    anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
                    set_pending_state_b(iVar6, 1);
                    local_pos[0] = *(float*)(iVar4 + 0x64);
                    local_pos[2] = *(float*)(iVar4 + 0x6c);
                    local_pos[1] = (FLOAT_8061943c + (float)piVar5[4]) - FLOAT_8061941c;
                    fx_spawn_explosion_particle(local_pos);
                }
            } else if (-1 < iVar3) {
                *(unsigned int*)(iVar4 + 0x28) |= 1;
                *(float*)(iVar4 + 0x68) = fVar1 + (float)piVar5[4];
                if (anim_active_slot_userflags(*(int*)(iVar4 + 4)) == 0) {
                    if (anim_is_clip_active(iVar4, 0x13a) == 0) {
                        anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
                    }
                } else {
                    anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
                    set_pending_state_b(iVar6, 2);
                }
            }
        } else if (iVar3 < 4) {
            *(float*)(iVar4 + 0x68) = FLOAT_8061943c + (float)piVar5[4];
            if (anim_is_clip_active(iVar4, 0x138) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
            } else if (anim_primary_clip_userflags(iVar4, 0x138) != 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
                set_pending_state_b(iVar6, 2);
            }
        }
        aiactor_melee_swipe(iVar4, 0, 0);
        aiactor_melee_swipe(iVar4, 1, 0);
    }
    FUN_8038273c();
}

// helper: team code test for the morph boss (ZPD4 / ZMK1 = "primary" pair)
static inline int morph_is_primary(int classPtr) {
    return classPtr != 0 &&
           (*(int*)(classPtr + 8) == 0x5a504434 || *(int*)(classPtr + 8) == 0x5a4d4b31);
}
static inline int morph_is_secondary(int classPtr) {
    return classPtr != 0 &&
           (*(int*)(classPtr + 8) == 0x5a504435 || *(int*)(classPtr + 8) == 0x5a4d4b32);
}

// resolve a zone id, falling back to the leader's current zone on failure.
static void morph_set_zone_or_fallback(int rec, int zoneId) {
    int z = soundpad_find_zone_by_id(zoneId);
    if (z < 0) {
        *(int*)(rec + 0x684) =
            *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
    } else {
        *(int*)(rec + 0x684) = soundpad_find_zone_by_id(zoneId);
    }
    *(int*)(rec + 0x9ac) = 0;
    *(int*)(rec + 0x9a8) = 0;
}

// scale the actor's bones uniformly by `s` (rebuilds each bone matrix).
static void morph_apply_bone_scale(int entity, float s) {
    mtx4_set_scale((double)s, (double)s, (double)s);
    int off = 0;
    int boneCount = *(int*)(*(int*)(*(int*)(entity + 4) + 4) + 4);
    for (int i = 0; i < boneCount; i++) {
        float* m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + off);
        mtx4_multiply(m, m, m);
        off += 0x40;
    }
}

// ===========================================================================
// 0x802882c4  aiactor_morph_boss_tick
// Shapeshifter/morph boss FSM (states 0..10). Handles team-coded zone-sound
// swaps, per-state anim, scale morphing via bone-matrix multiply, spark
// shedding, and a hand-off/teleport to the paired player actor (state 4).
// ===========================================================================
void aiactor_morph_boss_tick(void) {
    int entity;
    int iVar6;
    int iVar10;
    int rec;

    entity = FUN_803826e8();
    rec = *(int*)(entity + 0x7c);
    iVar10 = rec;
    aiactor_retarget_visible_teammate();
    creature_state_prologue_b(rec);

    int classPtr = *(int*)(rec + 0xbf0);
    int isPrimary = morph_is_primary(classPtr);
    int isSecondary = morph_is_secondary(classPtr);
    *(float*)(entity + 0x1cc) = FLOAT_8061939c;

    if (aiactor_follow_zone_path() == 0 && anim_is_clip_active(entity, 0x2a) == 0) {
        anim_play_scaled((unsigned long long)(double)FLOAT_8061940c, (double)FLOAT_8061939c);
    }
    aiactor_movement_tick(0, 0, 0, 0, 0, 0, 0, 0);

    switch (*(int*)(rec + 0x20)) {
    case 0:
        anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
        set_pending_state_b(rec, 1);
        *(float*)(rec + 0xaa0) = FLOAT_806193b8;
        *(float*)(rec + 0x9dc) = FLOAT_806193b8;
        {
            int cls = *(int*)(rec + 0xbf0);
            if (cls != 0 && *(int*)(cls + 8) == 0x5a504434) {
                morph_set_zone_or_fallback(rec, 0x49f);
                entity_create_attachment(0, 0, 0, 0, 0, 0, 0, 0, entity);
                *(unsigned int*)(entity + 0x28) |= 0x1000000;
            } else if (cls != 0 && *(int*)(cls + 8) == 0x5a504435) {
                morph_set_zone_or_fallback(rec, 0x4a2);
                entity_create_attachment(0, 0, 0, 0, 0, 0, 0, 0, entity);
            } else if (cls != 0 && *(int*)(cls + 8) == 0x5a4d4b31) {
                morph_set_zone_or_fallback(rec, 0x49f);
                entity_create_attachment(0, 0, 0, 0, 0, 0, 0, 0, entity);
                *(unsigned int*)(entity + 0x28) &= 0xffffff9e;
            } else if (cls != 0 && *(int*)(cls + 8) == 0x5a4d4b32) {
                morph_set_zone_or_fallback(rec, 0x4a2);
                entity_create_attachment(0, 0, 0, 0, 0, 0, 0, 0, entity);
                *(unsigned int*)(entity + 0x28) &= 0xffffff9e;
            }
        }
        aiactor_turn_toward_entity(entity, *(int*)(DAT_80611d78 + 0x14), 0);
        break;
    case 1:
        if ((*(unsigned int*)(entity + 0x28) & 0x1000000) == 0) {
            if (anim_is_clip_active(entity, 0x25) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
            }
        } else {
            if (anim_is_clip_active(entity, 0x112) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
            }
        }
        aiactor_facing_update();
        break;
    case 2: {
        float ox = 0, oy = 0, oz = 0;
        if (FLOAT_80619430 * *(float*)(rec + 0x9e0) <= *(float*)(rec + 0xb30)) {
            int emitter = DAT_806127d0;
            particle_cached_disable(&emitter);
        } else {
            if (isPrimary) {
                ox = FLOAT_80619444; oy = FLOAT_80619448; oz = FLOAT_8061944c;
            } else if (isSecondary) {
                ox = FLOAT_80619444; oy = FLOAT_80619450; oz = FLOAT_80619454;
            }
            float base[3];
            base[0] = ox; base[1] = oy; base[2] = oz;
            float vel[3];
            vel[0] = *(float*)(entity + 0x64) - ox;
            vel[1] = *(float*)(entity + 0x68) - oy;
            vel[2] = *(float*)(entity + 0x6c) - oz;
            int e1 = DAT_806127d0; particle_cached_set_position(&e1, base);
            int e2 = DAT_806127d0; particle_cached_set_velocity(&e2, vel);
            int e3 = DAT_806127d0; particle_cached_set_drag((double)FLOAT_80619458, &e3);
        }
        if (isPrimary) {
            int z52d = soundpad_find_zone_by_id(0x52d);
            if (*(int*)(rec + 0x684) == z52d && aiactor_follow_zone_path() != 0) {
                morph_set_zone_or_fallback(rec, 0x599);
            } else {
                int z599 = soundpad_find_zone_by_id(0x599);
                if (*(int*)(rec + 0x684) == z599 && aiactor_follow_zone_path() != 0) {
                    morph_set_zone_or_fallback(rec, 0x52d);
                }
            }
        } else if (isSecondary) {
            int z59b = soundpad_find_zone_by_id(0x59b);
            if (*(int*)(rec + 0x684) == z59b && aiactor_follow_zone_path() != 0) {
                morph_set_zone_or_fallback(rec, 0x59c);
            } else {
                int z59c = soundpad_find_zone_by_id(0x59c);
                if (*(int*)(rec + 0x684) == z59c && aiactor_follow_zone_path() != 0) {
                    morph_set_zone_or_fallback(rec, 0x59b);
                }
            }
        }
        if (anim_is_clip_active(entity, 0x36) == 0) {
            anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
        }
        aiactor_facing_update();
        break;
    }
    case 3:
        if (*(int*)(rec + 0x684) == *(int*)(rec + 0x678) &&
            *(float*)(rec + 0xb30) < FLOAT_80619430 * *(float*)(rec + 0x9e0)) {
            float dx = *(float*)(entity + 0x64) - *(float*)(DAT_80611d54 + 0x27c);
            float dy = *(float*)(entity + 0x68) - *(float*)(DAT_80611d54 + 0x280);
            float dz = *(float*)(entity + 0x6c) - *(float*)(DAT_80611d54 + 0x284);
            double d = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
            d = math_sqrt(d);
            float scale = (float)(d / (double)FLOAT_8061945c);
            for (int i = 0; i < 5; i++) {
                if (character_get_damage_vertex_pos() != 0) {
                    spark_spawn_fading();
                    spark_set_last_lifetime((double)FLOAT_8061939c);
                    spark_set_last_scale((double)scale);
                }
            }
            if (anim_is_clip_active(entity, 0x247) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
            }
        }
        aiactor_facing_update();
        break;
    case 4:
        if (*(int*)(rec + 0x684) == *(int*)(rec + 0x678)) {
            if (anim_is_clip_active(entity, 0x88e) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_8061940c, (double)FLOAT_8061939c);
            }
            int cls = *(int*)(rec + 0xbf0);
            if ((cls == 0 || *(int*)(cls + 8) != 0x5a504434) &&
                (cls == 0 || *(int*)(cls + 8) != 0x5a504435)) {
                break;
            }
            float chargeFloor = FLOAT_8061946c;
            if (chargeFloor < *(float*)(rec + 0x9dc)) {
                chargeFloor = *(float*)(rec + 0x9dc);
            }
            *(float*)(rec + 0x9dc) = chargeFloor;
            if (*(float*)(rec + 0x9dc) <= FLOAT_8061946c) {
                int paired = 0;
                int teamCode = -1;
                if (*(int*)(rec + 0xbf0) != 0 &&
                    *(int*)(*(int*)(rec + 0xbf0) + 8) == 0x5a504434) {
                    paired = aiactor_find_player_by_team(0x5a4d4b31);
                    teamCode = 0x5a4d4b31;
                    player_ai_teleport_wrapper(*(int*)(paired + 0x7c),
                                               soundpad_find_zone_by_id(0x49f), 1);
                }
                if (*(int*)(rec + 0xbf0) != 0 &&
                    *(int*)(*(int*)(rec + 0xbf0) + 8) == 0x5a504435) {
                    paired = aiactor_find_player_by_team(0x5a4d4b32);
                    teamCode = 0x5a4d4b32;
                    player_ai_teleport_wrapper(*(int*)(paired + 0x7c),
                                               soundpad_find_zone_by_id(0x4a2), 1);
                }
                int pairedRec = *(int*)(paired + 0x7c);
                *(unsigned int*)(entity + 0x28) &= 0xffffff9e;
                *(unsigned int*)(paired + 0x28) |= 0x61;
                if ((*(unsigned int*)(entity + 0x28) & 0x1000000) != 0) {
                    *(unsigned int*)(paired + 0x28) |= 0x1000000;
                }
                *(int*)(pairedRec + 0xb30) = *(int*)(rec + 0xb30);
                *(int*)(pairedRec + 0x9e0) = *(int*)(rec + 0x9e0);
                *(int*)(pairedRec + 0x9dc) = *(int*)(rec + 0x9dc);
                FUN_8037caec(*(int*)(*(int*)(rec + 0xbf0) + 8), teamCode);
                *(int*)(paired + 0x64) = *(int*)(entity + 0x64);
                *(int*)(paired + 0x68) = *(int*)(entity + 0x68);
                *(int*)(paired + 0x6c) = *(int*)(entity + 0x6c);
                if (*(int*)(pairedRec + 0x20) != 4) {
                    *(int*)(pairedRec + 0x9d0) = 4;
                }
                if (*(int*)(rec + 0x20) != 0) {
                    *(int*)(rec + 0x9d0) = 0;
                }
            }
        }
        break;
    case 5:
        if (*(int*)(rec + 0x684) == *(int*)(rec + 0x678) &&
            *(float*)(rec + 0x9dc) < FLOAT_8061941c) {
            *(unsigned int*)(entity + 0x28) |= 0x1000000;
        }
        break;
    case 6:
        if (*(int*)(rec + 0x684) == *(int*)(rec + 0x678)) {
            float t = FLOAT_8061939c +
                      (*(float*)(rec + 0x9e0) * *(float*)(rec + 0xb30)) / FLOAT_80619430;
            float capped = (t < FLOAT_806193bc) ? t : FLOAT_806193bc;
            *(float*)(rec + 0x9dc) = capped;
            if (anim_is_clip_active(entity, 0x248) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
            }
        }
        break;
    case 7:
        if (*(int*)(rec + 0x684) == *(int*)(rec + 0x678)) {
            float t = FLOAT_8061939c +
                      (*(float*)(rec + 0x9e0) * *(float*)(rec + 0xb30)) / FLOAT_80619430;
            float capped = (t < FLOAT_806193bc) ? t : FLOAT_806193bc;
            *(float*)(rec + 0xaa0) = capped;
            if (anim_is_clip_active(entity, 0x248) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_8061939c);
            }
        }
        break;
    case 8:
        if (*(int*)(rec + 0x684) == *(int*)(rec + 0x678)) {
            float floorv = FLOAT_806193d8;
            if (floorv < *(float*)(rec + 0x9dc)) {
                floorv = *(float*)(rec + 0x9dc);
            }
            *(float*)(rec + 0x9dc) = floorv;
            morph_apply_bone_scale(entity, *(float*)(rec + 0x9dc));
        }
        break;
    case 9:
        if (*(int*)(rec + 0x684) == *(int*)(rec + 0x678)) {
            float floorv = FLOAT_8061940c;
            if (floorv < *(float*)(rec + 0x9dc)) {
                floorv = *(float*)(rec + 0x9dc);
            }
            *(float*)(rec + 0x9dc) = floorv;
            *(int*)(rec + 0xb08) = *(int*)(rec + 0x9dc);
        }
        break;
    case 10:
        if (*(int*)(rec + 0x684) == *(int*)(rec + 0x678)) {
            *(float*)(entity + 0x1cc) = FLOAT_806193b8;
            float pos[3];
            pos[0] = *(float*)(entity + 0x64) + *(float*)(entity + 0x1a8);
            pos[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
            pos[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
            float normal[3];
            normal[0] = FLOAT_806193b8;
            normal[1] = FLOAT_806193b8;
            normal[2] = FLOAT_806193b8;
            player_ai_explosion_spawn(0x2d, 0x17, pos, normal, entity, entity, 1);
            *(unsigned int*)(rec + 0x2c) |= 0x100000;
            FUN_80382734();
            return;
        }
        break;
    }

    // trailing scale-morph pass (applies to non-6/7 states)
    if ((double)FLOAT_8061939c < (double)*(float*)(rec + 0x9dc)) {
        morph_apply_bone_scale(entity, *(float*)(rec + 0x9dc));
    }
    if ((double)FLOAT_8061939c < (double)*(float*)(rec + 0xaa0)) {
        float s = *(float*)(rec + 0xaa0);
        mtx4_set_scale((double)s, (double)s, (double)s);
        float* m1 = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) +
                             *(int*)(*(int*)(*(int*)(entity + 4) + 0x84) + 0x18) * 0x40);
        mtx4_multiply(m1, m1, m1);
        float* m2 = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) +
                             *(int*)(*(int*)(*(int*)(entity + 4) + 0x84) + 0x54) * 0x40);
        mtx4_multiply(m2, m2, m2);
    }
    if (*(int*)(rec + 0x20) != 6) {
        *(float*)(rec + 0x9dc) = *(float*)(rec + 0x9dc) - FLOAT_80619470;
    }
    if (*(int*)(rec + 0x20) != 7) {
        *(float*)(rec + 0xaa0) = *(float*)(rec + 0xaa0) - FLOAT_80619470;
    }
    (void)iVar6;
    (void)iVar10;
    FUN_80382734();
}

// ===========================================================================
// 0x80289044  aiactor_morph_boss_damage_react
// Damage/hit-reaction dispatcher for the morph boss. Maps the incoming hit type
// (returned in the low word of the actor-context call) to a pending state and
// triggers the matching death effect / damage particle / zone sound.
// ===========================================================================
void aiactor_morph_boss_damage_react(void) {
    int entity;
    int rec;
    int hitType;
    int curState;

    // The IDB returns {hitType:lo, entity:hi} packed in a 64-bit value; model as
    // two explicit reads from the shared accessor to keep this compile-clean.
    entity = FUN_803826f0();
    rec = *(int*)(entity + 0x7c);
    hitType = *(int*)(rec + 0x24) & 0xf; // low nibble = react selector
    curState = *(int*)(rec + 0x20);

    if (curState == 3) {
        if (*(float*)(rec + 0x9e0) <= FLOAT_80619474) {
            character_clear_damage_state(entity);
        }
    } else if (curState < 3) {
        if (1 < curState) {
            if (*(float*)(rec + 0x9e0) <= FLOAT_80619474) {
                character_death_effect_trigger((int*)(rec + 0xc68));
            }
            if (morph_is_primary(*(int*)(rec + 0xbf0))) {
                morph_set_zone_or_fallback(rec, 0x49f);
            } else {
                morph_set_zone_or_fallback(rec, 0x4a2);
            }
        }
    } else if (curState < 8) {
        if (curState < 6) {
            int emitter = DAT_806127d4;
            particle_cached_disable(&emitter);
        } else if (FLOAT_80619474 < *(float*)(rec + 0x9e0)) {
            if (curState != 10) {
                *(int*)(rec + 0x9d0) = 10;
            }
            FUN_8038273c();
            return;
        }
    }

    switch (hitType) {
    case 0:
        if (*(int*)(rec + 0x20) != 1) {
            *(int*)(rec + 0x9d0) = 1;
        }
        break;
    case 1:
        if (*(int*)(rec + 0x20) != 2) {
            *(int*)(rec + 0x9d0) = 2;
        }
        if (morph_is_primary(*(int*)(rec + 0xbf0))) {
            morph_set_zone_or_fallback(rec, 0x52d);
        } else {
            morph_set_zone_or_fallback(rec, 0x59b);
        }
        {
            int emitter = DAT_806127d0;
            particle_cached_enable(&emitter);
        }
        character_damage_spawn_effects();
        break;
    case 2:
        if (*(int*)(rec + 0x20) != 3) {
            *(int*)(rec + 0x9d0) = 3;
        }
        character_set_damage_bone(entity, 3);
        break;
    case 3:
        anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
        if (*(int*)(rec + 0x20) != 6) {
            *(int*)(rec + 0x9d0) = 6;
        }
        break;
    case 4:
        anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
        if (*(int*)(rec + 0x20) != 7) {
            *(int*)(rec + 0x9d0) = 7;
        }
        break;
    case 5: {
        float vel[3];
        vel[0] = *(float*)&DAT_803e05a0;
        vel[1] = *(float*)&DAT_803e05a4;
        vel[2] = *(float*)&DAT_803e05a8;
        float pos[3];
        pos[0] = *(float*)(entity + 0x64);
        pos[1] = *(float*)(entity + 0x68);
        pos[2] = *(float*)(entity + 0x6c);
        int e1 = DAT_806127d4; particle_cached_set_position(&e1, pos);
        int e2 = DAT_806127d4; particle_cached_set_velocity(&e2, vel);
        int e3 = DAT_806127d4;
        unsigned int rc = rng_counter_tick();
        particle_cached_set_drag(
            (double)(FLOAT_806193bc * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0) + FLOAT_806193a4),
            &e3);
        int e4 = DAT_806127d4; particle_cached_enable(&e4);
        *(float*)(rec + 0x9dc) = FLOAT_8061939c;
        if (*(int*)(rec + 0x20) != 4) {
            *(int*)(rec + 0x9d0) = 4;
        }
        break;
    }
    case 6:
        *(float*)(rec + 0x9dc) = FLOAT_8061939c;
        if (*(int*)(rec + 0x20) != 8) {
            *(int*)(rec + 0x9d0) = 8;
        }
        break;
    case 7:
        *(float*)(rec + 0x9dc) = FLOAT_8061939c;
        if (*(int*)(rec + 0x20) != 9) {
            *(int*)(rec + 0x9d0) = 9;
        }
        break;
    case 8: {
        float vel[3];
        vel[0] = *(float*)&DAT_803e05ac;
        vel[1] = *(float*)&DAT_803e05b0;
        vel[2] = *(float*)&DAT_803e05b4;
        float pos[3];
        pos[0] = *(float*)(entity + 0x64);
        pos[1] = *(float*)(entity + 0x68);
        pos[2] = *(float*)(entity + 0x6c);
        int e1 = DAT_806127d4; particle_cached_set_position(&e1, pos);
        int e2 = DAT_806127d4; particle_cached_set_velocity(&e2, vel);
        int e3 = DAT_806127d4;
        unsigned int rc = rng_counter_tick();
        particle_cached_set_drag(
            (double)(FLOAT_806193bc * (rng_unit(rc, DOUBLE_806193b0) / FLOAT_806193a0) + FLOAT_806193a4),
            &e3);
        int e4 = DAT_806127d4; particle_cached_enable(&e4);
        *(float*)(rec + 0x9dc) = FLOAT_8061939c;
        anim_play_scaled((unsigned long long)(double)FLOAT_806193c0, (double)FLOAT_8061939c);
        if (*(int*)(rec + 0x20) != 5) {
            *(int*)(rec + 0x9d0) = 5;
        }
        break;
    }
    default:
        break;
    }

    *(float*)(rec + 0x9e0) = FLOAT_806193b8;
    *(float*)(rec + 0xb30) = FLOAT_806193b8;
    FUN_8038273c();
}

// ===========================================================================
// 0x80289574  aiactor_stealth_creature_tick
// Stealth/cloak creature FSM (idle(0)/hidden(1)/reveal(2)). Toggles the actor's
// render-visibility + stance flags and samples a keyframe on reveal.
// ===========================================================================
void aiactor_stealth_creature_tick(int entity) {
    int rec = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();
    creature_state_prologue_b(rec);

    int state = *(int*)(rec + 0x20);
    if (state == 1) {
        *(unsigned int*)(entity + 0x28) |= 0x20;
        if (aiactor_check_zone_target_players(entity) != 0 ||
            (*(unsigned int*)(rec + 0xc6c) & 4) != 0) {
            *(unsigned int*)(entity + 0x28) |= 0x60;
            set_pending_state_b(rec, 2);
        }
    } else if (state < 1) {
        if (-1 < state) {
            aiactor_set_stance_flags(entity, 1);
            if ((*(unsigned int*)(rec + 0x28) & 0x20000000) == 0) {
                *(unsigned int*)(entity + 0x28) &= 0xffffffbf;
                float kf[8];
                anim_sample_keyframe(*(int*)(DAT_80611824 + DAT_804d1f06 * 4), 0, 0, kf);
                *(int*)(*(int*)(entity + 4) + 0xec) = *(int*)&kf[1];
                anim_play_scaled((unsigned long long)(double)FLOAT_806193b8, (double)FLOAT_806193b8);
                set_pending_state_b(rec, 1);
            } else {
                *(unsigned int*)(entity + 0x28) |= 0x60;
                set_pending_state_b(rec, 2);
            }
        }
    } else if (state < 3) { // state 2: reveal
        *(unsigned int*)(entity + 0x28) &= 0xffffff9f;
        *(unsigned int*)(entity + 0x28) |= 0x20;
        if (anim_is_clip_active(entity, 0x11b) == 0) {
            anim_play_scaled((unsigned long long)(double)FLOAT_80619398, (double)*(float*)(rec + 0xb18));
        } else {
            anim_set_clip_speed_fields((double)*(float*)(rec + 0xb18), (int**)(entity + 4));
        }
        if (anim_primary_clip_time_left((double)FLOAT_80619398, *(int*)(entity + 4)) != 0) {
            *(unsigned int*)(entity + 0x28) |= 0x60;
            int skip = 0;
            if (*(int*)(rec + 0xaac) == 1 && *(int*)(rec + 0xaa8) == 0) {
                unsigned int uVar3 = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x14)
                                                            : *(unsigned int*)(rec + 0x1c);
                if ((uVar3 & 1) != 0) {
                    skip = 1;
                }
            } else {
                skip = 1;
            }
            if (skip) {
                *(int*)(rec + 0xaac) = 8;
                *(int*)(rec + 0xaa8) = 0;
            }
            entity_set_render_type(entity, (void*)&damage_cap_by_max_health);
        }
    }
    aiactor_leader_zone_update(entity);
}

// ===========================================================================
// 0x80289898  aiactor_anim_play_forward
// Thin forward to anim_play_scaled (dedicated call site used by the beast FSM).
// ===========================================================================
void aiactor_anim_play_forward(unsigned long long ctx, double speed) {
    anim_play_scaled(ctx, speed);
}

// beast facing -> directional clip-table index (0 front / 2 back / 1 side).
static int beast_dir_index(int rec) {
    short facing = *(short*)(rec + 0x48);
    if (facing == 0x34) {
        return 0;
    }
    if (facing == 0x62) {
        return 2;
    }
    return 1;
}

// ===========================================================================
// 0x8028a3cc  aiactor_beast_play_locomotion
// Pick + play the beast's forward or reverse locomotion clip based on the
// movement flag (+0x24 bit 2 = moving-forward, bit 0 = moving).
// ===========================================================================
void aiactor_beast_play_locomotion(int entity) {
    int rec = *(int*)(entity + 0x7c);
    if ((*(unsigned int*)(rec + 0xc6c) & 1) != 0) {
        return;
    }
    int dir = beast_dir_index(rec);
    if ((*(unsigned int*)(rec + 0x24) & 4) == 0) {
        if ((*(unsigned int*)(rec + 0x24) & 1) != 0 &&
            anim_is_clip_active(entity, (&DAT_80473140)[dir * 8]) == 0) {
            aiactor_anim_play_forward((unsigned long long)(double)FLOAT_806194b8, (double)FLOAT_8061947c);
        }
    } else {
        if (anim_is_clip_active(entity, (&DAT_80473138)[dir * 8]) == 0) {
            aiactor_anim_play_forward((unsigned long long)(double)FLOAT_806194b8, (double)FLOAT_8061947c);
        }
    }
}

// ===========================================================================
// 0x802898d0  aiactor_beast_creature_tick
// Quadruped "beast" FSM (idle(0)/walk(1)/turn(2)/attack(3)/roar(5)/flee(6)).
// Uses directional clip-id tables (DAT_804731xx) selected by facing, with the
// int-indexed actor record and the shared cooldown/rng machinery.
// ===========================================================================
void aiactor_beast_creature_tick(void) {
    float fVar1, fVar2;
    int iVar3;
    int iVar4;
    int iVar7;
    int iVar8;
    int* rec;
    unsigned int uVar6;

    iVar3 = FUN_803826e8();
    rec = *(int**)(iVar3 + 0x7c);
    iVar4 = aiactor_retarget_visible_teammate();

    if (-1 < rec[0x274]) {
        if (rec[0x274] != rec[8]) {
            rec[0x276] = (int)FLOAT_80619478;
            rec[0x275] = rec[8];
            rec[8] = rec[0x274];
        }
        rec[0x274] = -1;
    }
    if ((rec[0xb] & 0x80) == 0) {
        rec[0x2a4] = (int)FLOAT_80619478;
    } else {
        unsigned int biasBits = (unsigned int)DAT_806109c0 ^ 0x80000000u;
        rec[0x2a4] = (int)(FLOAT_806108ec *
                           (FLOAT_8061947c / rng_unit(biasBits, DOUBLE_806194a8)) +
                           (float)rec[0x2a4]);
    }
    rec[0xb] |= 0x400;

    iVar8 = beast_dir_index((int)rec);

    if (iVar4 == 0 && rec[8] != 6) {
        rec[0x277] = rec[8];
        unsigned int rc = rng_counter_tick();
        rec[0x278] = (int)(FLOAT_8061947c + (float)rec[0x2cc] +
                           rng_unit(rc, DOUBLE_806194b0) / FLOAT_80619480);
        if (rec[8] != 6) {
            rec[0x275] = rec[8];
            rec[8] = 6;
            rec[0x276] = (int)FLOAT_80619478;
            rec[0x274] = rec[8];
        }
    }
    aiactor_leader_zone_update(iVar3);

    int leaderEnt = 0;
    if (rec == 0 || (leaderEnt = rec[0], leaderEnt == 0) || rec[2] < DAT_80612610 ||
        ((*(unsigned int*)(leaderEnt + 0x28) & 0x100) == 0 &&
         (*(unsigned int*)(leaderEnt + 0x10) & 0xffff) != 0)) {
        rec[0xc] = *(int*)(rec[1] + 0x64);
        rec[0xd] = *(int*)(rec[1] + 0x68);
        rec[0xe] = *(int*)(rec[1] + 0x6c);
    } else {
        rec[0xc] = *(int*)(leaderEnt + 0x64);
        rec[0xd] = *(int*)(rec[0] + 0x68);
        rec[0xe] = *(int*)(rec[0] + 0x6c);
    }

    fVar2 = FLOAT_80619484;
    fVar1 = FLOAT_80619478;
    if ((*(unsigned int*)(*(int*)(rec[1] + 0x7c) + 0x24) & 8) != 0) {
        int skip = 0;
        if (rec[0x2ab] == 1 && rec[0x2aa] == 0) {
            uVar6 = (DAT_805015bd == '\n') ? (unsigned int)rec[5] : (unsigned int)rec[7];
            if ((uVar6 & 1) == 0) {
                skip = 1;
            }
        }
        if (!skip) {
            rec[0x2ab] = 0x10;
            rec[0x2aa] = 0;
        }
        FUN_80382734();
        return;
    }

    iVar7 = rec[8];
    switch (iVar7) {
    case 0:
        rec[0x2cc] = (int)FLOAT_80619478;
        if ((float)rec[0x2bd] < fVar2 || (*(unsigned int*)(rec[0x2fc] + 0x58) & 0x800000) != 0) {
            if (rec[8] != 2) {
                rec[0x275] = rec[8]; rec[8] = 2;
                rec[0x276] = (int)FLOAT_80619478; rec[0x274] = rec[8];
            }
        } else if (rec[8] != 3) {
            rec[0x275] = rec[8]; rec[8] = 3;
            rec[0x276] = (int)fVar1; rec[0x274] = rec[8];
        }
        break;
    case 1:
        iVar4 = anim_is_clip_active(iVar3, (&DAT_80473138)[iVar8 * 8]);
        if (iVar4 == 0 &&
            anim_is_clip_active(iVar3, (&DAT_8047314c)[iVar8 * 8]) == 0 &&
            anim_is_clip_active(iVar3, (&DAT_80473150)[iVar8 * 8]) == 0) {
            aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619488, (double)FLOAT_8061947c);
        } else if (iVar8 == 0 && anim_active_slot_userflags(*(int*)(iVar3 + 4)) != 0) {
            uVar6 = rng_counter_tick();
            aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
        }
        if ((float)rec[0x2cc] <= (float)rec[0x278]) {
            aiactor_facing_update();
        } else if ((float)rec[0x2bd] < FLOAT_8061948c ||
                   ((uVar6 = rng_counter_tick(), uVar6 % 3 == 1) && (float)rec[0x2bd] < FLOAT_80619484) ||
                   (*(unsigned int*)(rec[0x2fc] + 0x58) & 0x800000) != 0) {
            if (rec[8] != 2) {
                rec[0x275] = rec[8]; rec[8] = 2;
                rec[0x276] = (int)FLOAT_80619478; rec[0x274] = rec[8];
            }
        } else if (rec[8] != 3) {
            rec[0x275] = rec[8]; rec[8] = 3;
            rec[0x276] = (int)FLOAT_80619478; rec[0x274] = rec[8];
        }
        break;
    case 2:
        iVar4 = anim_is_clip_active(iVar3, (&DAT_80473154)[iVar8 * 8]);
        if (iVar4 == 0) {
            aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619490, (double)FLOAT_8061947c);
        } else if (anim_active_slot_userflags(*(int*)(iVar3 + 4)) != 0) {
            unsigned int rc = rng_counter_tick();
            if (iVar8 == 2) {
                rec[0x278] = (int)(FLOAT_80619494 * (rng_unit(rc, DOUBLE_806194b0) / FLOAT_80619480) +
                                   FLOAT_80619494 + (float)rec[0x2cc]);
            } else {
                rec[0x278] = (int)(FLOAT_80619498 * (rng_unit(rc, DOUBLE_806194b0) / FLOAT_80619480) +
                                   FLOAT_80619498 + (float)rec[0x2cc]);
            }
            if (rec[8] != 1) {
                rec[0x275] = rec[8]; rec[8] = 1;
                rec[0x276] = (int)FLOAT_80619478; rec[0x274] = rec[8];
            }
        }
        aiactor_facing_update();
        break;
    case 3:
        iVar4 = anim_is_clip_active(iVar3, (&DAT_80473148)[iVar8 * 8]);
        if (iVar4 == 0 &&
            anim_is_clip_active(iVar3, (&DAT_80473140)[iVar8 * 8]) == 0 &&
            anim_is_clip_active(iVar3, (&DAT_80473144)[iVar8 * 8]) == 0 &&
            anim_is_clip_active(iVar3, (&DAT_8047313c)[iVar8 * 8]) == 0 &&
            anim_is_clip_active(iVar3, (&DAT_8047314c)[iVar8 * 8]) == 0 &&
            anim_is_clip_active(iVar3, (&DAT_80473150)[iVar8 * 8]) == 0) {
            aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619490, (double)FLOAT_8061947c);
        } else {
            iVar4 = anim_primary_clip_userflags(iVar3, (&DAT_80473148)[iVar8 * 8]);
            if (iVar4 == 0) {
                iVar4 = anim_primary_clip_userflags(iVar3, (&DAT_8047313c)[iVar8 * 8]);
                if (iVar4 != 0) {
                    unsigned int rc = rng_counter_tick();
                    if (FLOAT_8061949c < rng_unit(rc, DOUBLE_806194b0) / FLOAT_80619480) {
                        rc = rng_counter_tick();
                        rec[0x278] = (int)(FLOAT_806194a0 * (rng_unit(rc, DOUBLE_806194b0) / FLOAT_80619480) +
                                           FLOAT_806194a0 + (float)rec[0x2cc]);
                        if (rec[8] != 1) {
                            rec[0x275] = rec[8]; rec[8] = 1;
                            rec[0x276] = (int)FLOAT_80619478; rec[0x274] = rec[8];
                        }
                        break;
                    }
                }
            } else if (iVar8 == 0 &&
                       (*(unsigned int*)(DAT_80610ba0 + rec[0x81] * 0x29c + 0xc) & 1) != 0) {
                aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
            } else {
                aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
            }
        }
        if (iVar8 == 0) {
            iVar4 = anim_primary_clip_userflags(iVar3, DAT_80473140);
            if (iVar4 == 0) {
                iVar4 = anim_primary_clip_userflags(iVar3, DAT_8047314c);
                if (iVar4 != 0 || anim_primary_clip_userflags(iVar3, DAT_80473150) != 0) {
                    aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                }
            } else {
                uVar6 = rng_counter_tick();
                aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
            }
        }
        iVar4 = anim_primary_clip_userflags(iVar3, (&DAT_80473140)[iVar8 * 8]);
        if ((iVar4 != 0 || anim_primary_clip_userflags(iVar3, (&DAT_80473144)[iVar8 * 8]) != 0) &&
            ((float)rec[0x2bd] < FLOAT_8061948c || ((uVar6 = rng_counter_tick(), uVar6 % 5 == 1)))) {
            aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
        }
        aiactor_facing_update();
        break;
    case 5:
        if (anim_is_clip_active(iVar3, 0x99d) == 0) {
            anim_play_scaled((unsigned long long)(double)FLOAT_80619488, (double)FLOAT_8061947c);
        }
        if ((float)rec[0x278] < (float)rec[0x2cc]) {
            unsigned int rc = rng_counter_tick();
            rec[0x278] = (int)(FLOAT_80619498 * (rng_unit(rc, DOUBLE_806194b0) / FLOAT_80619480) +
                               FLOAT_80619498 + (float)rec[0x2cc]);
            if (rec[8] != 1) {
                rec[0x275] = rec[8]; rec[8] = 1;
                rec[0x276] = (int)FLOAT_80619478; rec[0x274] = rec[8];
            }
        }
        break;
    case 6:
        if (iVar4 != 0) {
            if (rec[0x277] != iVar7) {
                rec[0x275] = iVar7;
                rec[8] = rec[0x277];
                rec[0x276] = (int)FLOAT_80619478;
                rec[0x274] = rec[8];
            }
            break;
        }
        if ((*(unsigned int*)(rec[0x2fc] + 0x58) & 0x800000) == 0 &&
            (float)rec[0x278] < (float)rec[0x2cc]) {
            int skip = 0;
            if (rec[0x2ab] == 1 && rec[0x2aa] == 0) {
                uVar6 = (DAT_805015bd == '\n') ? (unsigned int)rec[5] : (unsigned int)rec[7];
                if ((uVar6 & 1) == 0) {
                    skip = 1;
                }
            }
            if (!skip) {
                rec[0x2ab] = 8;
                rec[0x2aa] = 0;
            }
        }
        if (anim_is_clip_active(iVar3, (&DAT_80473138)[iVar8 * 8]) == 0) {
            aiactor_anim_play_forward((unsigned long long)(double)FLOAT_80619488, (double)FLOAT_8061947c);
        }
        aiactor_facing_update();
        break;
    default:
        break;
    }
    (void)fVar1;
    FUN_80382734();
}

// ===========================================================================
// 0x8028a4c0  aiactor_beast_impact_react
// Beast impact reaction. On event==1 with hit-kind 0x1a, arm the stagger state
// (5) and seed its timer from the RNG; returns -1 when the impact is consumed.
// ===========================================================================
int aiactor_beast_impact_react(int entity, int event, int hitInfo) {
    int ret = 0;
    if (event == 1) {
        int rec = *(int*)(entity + 0x7c);
        if (*(int*)(hitInfo + 0x18) == 0x1a && *(int*)(rec + 0x20) != 5) {
            unsigned int rc = rng_counter_tick();
            *(float*)(rec + 0x9e0) =
                FLOAT_8061947c + *(float*)(rec + 0xb30) +
                rng_unit(rc, DOUBLE_806194b0) / FLOAT_80619480;
            if (*(int*)(rec + 0x20) != 5) {
                *(int*)(rec + 0x9d0) = 5;
            }
            ret = -1;
        }
        if (*(int*)(rec + 0x20) == 5) {
            ret = -1;
        }
    }
    return ret;
}
