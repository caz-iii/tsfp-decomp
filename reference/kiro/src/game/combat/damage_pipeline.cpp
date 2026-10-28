// =============================================================================
// damage_pipeline.cpp - Damage Pipeline: Hit -> Compute -> Apply -> Death
// Full chain from weapon hit detection through health reduction to death anim.
// Covers: death anim categories, death dispatch, headshot detection, team fire,
//         feedback (screen effects/knockback), main handler, falloff, weapon
//         damage computation, resolution, and final entity dispatch.
// Batch 192 | Date: 2026-08-11
// =============================================================================

#include "common.h"
#include "entity.h"
#include "sound.h"
#include "character.h"
#include "damage.h"

// External references
extern int   g_game_flags;            // 0x805015c0
extern char  g_difficulty;            // 0x805015bd
extern char  g_game_mode;             // 0x805015be
extern int   g_match_subtype;         // 0x805015d8
extern uint  g_match_flags;           // 0x805015c4
extern int   g_damage_system_active;  // 0x806108e4
extern int   g_story_difficulty;      // 0x806117a4

extern void damage_apply_simple(int* event_data);
extern void damage_apply_full(void);
extern void damage_on_hit_event(void);
extern void damage_award_event(int* event, int char_slot, int attacker_slot);
extern void damage_set_screen_effect(double amount, int char_slot, int weapon_type);
extern void character_on_death_handler(/* variadic */);

// =============================================================================
// Address: 0x8008f050 | Name: damage_get_death_anim_category
// Maps a death animation index (0-0x13) to a category:
//   0 = normal death (ragdoll)
//   1 = standard (most animations)
//   2 = special (burn/electro, indices 0xd-0x12)
// Used by death animation dispatcher to select behavior.
// =============================================================================
int damage_get_death_anim_category(int anim_index) {
    if (anim_index > 0 && anim_index < 0x14) {
        if (anim_index < 5) {
            if (anim_index > 2) return 0;   // indices 3,4 = normal
            if (anim_index >= 0) return 1;  // indices 1,2 = standard
        } else if (anim_index < 0x13) {
            if (anim_index < 0xd) return 1; // indices 5-12 = standard
            return 2;                        // indices 13-18 = special
        }
    }
    return 1; // default = standard
}

// =============================================================================
// Address: 0x8008f62c | Name: damage_check_headshot_bounds
// AABB headshot detection. Checks if the hit position (from event_data[10])
// is within the head bounding box of the target entity's attached head entity
// (accessed via char_slot + 0xbc4).
// Returns 1 if hit is within head bounds, 0 otherwise.
// =============================================================================
int damage_check_headshot_bounds(int* event_data) {
    int head_entity = *(int*)(*(int*)(*event_data + 0x7c) + 0xbc4);
    if (head_entity == 0) return 0;

    float* hit_pos = (float*)event_data[10];
    if (hit_pos == NULL) return 0;

    // Y axis check (vertical)
    float head_y = *(float*)(head_entity + 0x68) + *(float*)(head_entity + 0x1ac);
    if (hit_pos[1] < head_y) return 0;
    if (*(float*)(head_entity + 0x1c4) + head_y < hit_pos[1]) return 0;

    // X axis check
    float head_x = *(float*)(head_entity + 0x64) + *(float*)(head_entity + 0x1a8);
    if (hit_pos[0] < head_x - *(float*)(head_entity + 0x1c0)) return 0;
    if (*(float*)(head_entity + 0x1c0) + head_x < hit_pos[0]) return 0;

    // Z axis check
    float head_z = *(float*)(head_entity + 0x6c) + *(float*)(head_entity + 0x1b0);
    if (head_z - *(float*)(head_entity + 0x1c8) > hit_pos[2]) return 0;
    if (hit_pos[2] > *(float*)(head_entity + 0x1c8) + head_z) return 0;

    return 1; // headshot confirmed
}

// =============================================================================
// Address: 0x8008f6f4 | Name: damage_team_fire_check
// Checks for friendly fire (same team, teams mode enabled).
// If team fire detected: awards team-kill events (0x1a to attacker, 0x20 to
// victim) and optionally cancels damage. Falls through to standard damage
// dispatch if not team fire or if friendly fire damage is allowed.
// =============================================================================
void damage_team_fire_check(void) {
    int* event_data = (int*)event_get_entity_pair();
    int attacker_slot = 0;
    void* attacker_raw = (void*)event_data[1];
    int victim_slot = *(int*)(*event_data + 0x7c);

    if (attacker_raw != NULL && *(int*)((char*)attacker_raw + 0x20) == 8) {
        attacker_slot = *(int*)((char*)attacker_raw + 0x7c);
    }

    // Team fire check: not self-damage, same entity as splash target,
    // teams enabled (flag 0x1), same team
    if (event_data[6] != 0x15 && g_difficulty != 0x0a &&
        attacker_slot != 0 && attacker_slot != victim_slot &&
        attacker_raw == (void*)event_data[2] &&
        (g_game_flags & 1) != 0 &&
        *(int*)(attacker_slot + 0x84) == *(int*)(victim_slot + 0x84)) {

        // Award team fire events
        award_animation_event((double)AWARD_DURATION, 0x1a, *(int*)(attacker_slot + 8));
        award_animation_event((double)AWARD_DURATION, 0x20, *(int*)(victim_slot + 8));

        // Check if friendly fire damage is disabled (flag 0x8)
        if ((g_game_flags & 8) == 0) {
            goto end; // no damage applied
        }
    }

    // Not team fire or FF damage enabled - check if damage should proceed
    if ((*(int*)(*event_data + 0x150) == 0 || g_invuln_active == 0 || g_invuln_timer != 0) &&
        (*(float*)(victim_slot + 0xad4) <= 0.0f || g_difficulty == 0x0a ||
         (event_data[0x10] & 2) != 0)) {
        // Dispatch the damage event
        dispatch_damage_event(*event_data, attacker_raw, 0x200000, 0);
    }

end:
    event_end_pair();
}

// =============================================================================
// Address: 0x8008f854 | Name: damage_apply_feedback
// MASSIVE damage feedback handler for player-controlled characters.
// Applies: hit direction indicator, screen shake, damage screen flash,
//          knockback impulse (physics push from hit direction).
// Checks and early-outs for: invulnerability (flag 0x100), god mode (0x10000),
//          vehicle occupancy (flag 0x2000), debug invulnerability.
// =============================================================================
void damage_apply_feedback(int* event_data) {
    int* player_ctrl = *(int**)(*event_data + 0x150);
    uint flags = player_ctrl[0x45];

    // Invulnerable: zero damage
    if ((flags & 0x100) != 0 || (flags & 0x10000) != 0) {
        event_data[8] = (int)0.0f;
        return;
    }

    // Vehicle occupancy check (flag 0x2000)
    if ((flags & 0x2000) != 0) {
        int vehicle = get_player_vehicle((int)player_ctrl);
        if (vehicle != 0 && *(int*)(vehicle + 0x7c) != 0 &&
            (*(unsigned short*)(*(int*)(vehicle + 0x7c) + 0x1a) & 2) != 0) {
            event_data[8] = (int)0.0f;
            return;
        }
    }

    // Hit direction indicator
    if ((float*)event_data[0xc] == NULL || event_data[5] == 0) {
        // No explicit direction - compute from attacker position
        int attacker = event_data[2];
        if (attacker != 0 && *(int*)(attacker + 0x20) == 8) {
            float* hit_pos = (float*)event_data[10];
            float dir[3];
            if (hit_pos == NULL) {
                dir[0] = (float)player_ctrl[0x2a];
                dir[1] = (float)player_ctrl[0x2b];
                dir[2] = (float)player_ctrl[0x2c];
            } else {
                dir[0] = hit_pos[0];
                dir[1] = hit_pos[1];
                dir[2] = hit_pos[2];
            }
            // Subtract attacker position to get direction
            dir[0] -= *(float*)(attacker + 0x64);
            dir[1] -= *(float*)(event_data[2] + 0x68);
            dir[2] -= *(float*)(event_data[2] + 0x6c);
            set_hit_direction_indicator(*player_ctrl, dir);
        }
    } else {
        // Explicit direction from event
        set_hit_direction_indicator(*player_ctrl, (float*)event_data[0xc]);
    }

    // Screen shake (if player index valid)
    if (player_ctrl[1] >= 0) {
        apply_screen_shake((double)(float)event_data[8], player_ctrl[1]);
    }

    // Screen flash effect
    damage_set_screen_effect((double)(float)event_data[8], (int)player_ctrl, event_data[6]);

    // Knockback impulse (if weapon has knockback and player on ground)
    if (event_data[9] != -1 && (int*)event_data[0xc] != NULL &&
        player_ctrl[0x45] == 2 &&
        (player_ctrl[3] & 0x40) == 0 && (player_ctrl[3] & 0x10) == 0) {

        int weapon_entry = event_data[9] * 0x3c;
        double knockback_force = (double)*(float*)(&WEAPON_KNOCKBACK_TABLE + weapon_entry);

        // Scale by delta time if weapon type == 1
        if (*(int*)(&WEAPON_KNOCKBACK_TYPE + weapon_entry) == 1) {
            knockback_force = (double)(float)(knockback_force *
                (double)(g_delta_time * (SWITCH_FALL_DEFAULT_LIMIT /
                (float)((double)g_frame_count_bias - DOUBLE_BIAS))));
        }

        // Copy direction and scale by force
        float force_mult = (float)((double)DAMAGE_SPLASH_LOW * knockback_force);
        player_ctrl[0x3d] = *(int*)event_data[0xc];
        player_ctrl[0x3e] = *(int*)(event_data[0xc] + 4);
        player_ctrl[0x3f] = *(int*)(event_data[0xc] + 8);
        player_ctrl[0x3d] = (int)((float)player_ctrl[0x3d] * force_mult);
        player_ctrl[0x3e] = (int)((float)player_ctrl[0x3e] * force_mult);
        player_ctrl[0x3f] = (int)((float)player_ctrl[0x3f] * force_mult);

        // Random camera punch (50% chance)
        uint rng = random_uint();
        if ((rng & 1) == 0) {
            float punch = (float)((double)(DAMAGE_KNOCKBACK_PUNCH * (float)event_data[8]) * knockback_force);
            apply_camera_punch((double)DAMAGE_SPLASH_LOW, (double)punch, (double)punch, (int)player_ctrl);
        }
    }

    // Debug invulnerability: zero damage
    if (g_invuln_active != 0) {
        event_data[8] = (int)0.0f;
    }
}

// =============================================================================
// Address: 0x8008facc | Name: damage_event_main_handler
// MASSIVE top-level damage event handler for character entities (type 8).
// Full orchestration pipeline:
// 1. Check game state (not state 8, not in specific cutscene)
// 2. Award damage events to attacker
// 3. Check team fire and apply if applicable
// 4. Validate damage amount and weapon flags
// 5. Call death animation dispatch (if not flag 0x1 = invulnerable)
// 6. Apply story mode scaling
// 7. Route to damage_apply_simple (story) or damage_apply_full (multiplayer)
//    OR invoke custom damage callback at entity+0x19c
// 8. Call damage_on_hit_event (unless invulnerable anim)
// 9. If health == 0: character_on_death_handler
// 10. If alive and bot: AI aggro dispatch
// =============================================================================
void damage_event_main_handler(/* variadic */) {
    int* event_data = (int*)event_get_entity_pair();
    int attacker_slot = 0;
    int char_slot = *(int*)(*event_data + 0x7c);

    // Skip if game state == 8 (loading/transition)
    if (g_game_state == 8) { event_end_pair(); return; }

    // Skip if in specific cutscene (state 0x1b)
    int cutscene = get_active_cutscene();
    if (cutscene != 0 && *(int*)(cutscene + 0x218) == 0x1b) { event_end_pair(); return; }

    // Identify attacker
    int attacker_entity = event_data[1];
    if (attacker_entity != 0 && *(int*)(attacker_entity + 0x20) == 8) {
        attacker_slot = *(int*)(attacker_entity + 0x7c);
    }

    // Award damage event (0x1b) to victim
    award_animation_event((double)AWARD_DURATION, 0x1b, *(int*)(char_slot + 8));

    // Award kill/assist events
    damage_award_event(event_data, char_slot, attacker_slot);

    // Particle/effect spawn for large hits
    if (0.0f < (float)event_data[0x13] && 0.0f < (float)event_data[0x14]) {
        spawn_hit_particles((double)(float)event_data[0x13],
                           (double)(float)event_data[0x14], char_slot + 0xc68);
    }

    // Team fire check (returns non-zero if damage was blocked)
    int team_blocked = damage_team_fire_check();
    if (team_blocked != 0) { event_end_pair(); return; }

    // Store attacker ID for kill attribution
    if (event_data[1] == 0) {
        *(int*)(char_slot + 0x90) = 0;
    } else {
        *(int*)(char_slot + 0x90) = *(int*)(event_data[1] + 0x2c);
    }

    // Validate: damage > 0 OR weapon != 0x19 OR has special flag
    if (0.0f < (float)event_data[8] || event_data[6] != 0x19 ||
        (*(int*)(char_slot + 0xbf0) != 0 &&
         (*(uint*)(*(int*)(char_slot + 0xbf0) + 0x58) & 0x40000) != 0)) {

        // Death animation dispatch (unless invulnerable flag 0x1)
        if ((*(uint*)(char_slot + 0x10) & 1) == 0) {
            damage_death_anim_dispatch(/* event params */);
        }

        // Check if damage is blocked by anim state
        if ((*(uint*)(char_slot + 0x24) & 8) != 0 && *(int*)(char_slot + 0x44) != 1) {
            // Blocked - only award event to attacker
            if (event_data[6] > 0 && attacker_slot != 0) {
                award_animation_event((double)AWARD_DURATION, 0xc, *(int*)(attacker_slot + 8));
            }
        } else {
            // Apply damage
            if (g_difficulty == 0x0a) {
                // Story mode: AI-vs-AI scaling + difficulty 2 scaling
                if (attacker_slot != 0 && *(int*)(event_data[1] + 0x150) == 0 &&
                    *(int*)(*event_data + 0x150) == 0 && event_data[6] != 0x15) {
                    event_data[8] = (int)((float)event_data[8] * DAMAGE_STORY_AI_MULT);
                }
                if (g_story_difficulty == 2) {
                    event_data[8] = (int)((float)event_data[8] * DAMAGE_STORY_HARD_MULT);
                }

                // Route through callback or simple handler
                if (*(void**)(*event_data + 0x19c) == NULL) {
                    damage_apply_simple(event_data);
                } else {
                    (*(void(**)(int*))(*event_data + 0x19c))(event_data);
                }
            } else {
                // Multiplayer: route through callback or full handler
                if (*(void**)(*event_data + 0x19c) == NULL) {
                    damage_apply_full();
                } else {
                    (*(void(**)(int*))(*event_data + 0x19c))(event_data);
                }
            }

            // On-hit event (unless invulnerable anim playing)
            if ((*(uint*)(char_slot + 0x10) & 1) == 0 || *(int*)(char_slot + 0x44) != 1) {
                damage_on_hit_event();
            }

            // Death check
            int entity = *event_data;
            if (0.0f == *(float*)(entity + 0x1cc)) {
                character_on_death_handler(/* params */);
            } else if (*(int*)(char_slot + 8) < g_max_bot_level) {
                // AI aggro: alert attacker
                if (event_data[1] != 0 && *(int*)(entity + 0x150) != 0) {
                    ai_aggro_on_damage(event_data);
                }
            } else {
                // High-level AI response
                ai_high_level_damage_response(entity, event_data[0x12], event_data);
            }
        }
    }

    event_end_pair();
}

// =============================================================================
// Address: 0x8008fdd0 | Name: damage_compute_falloff
// Computes distance-based damage falloff multiplier.
// Looks up falloff curve from weapon table (0x3c-byte stride, field at +0x04).
// Returns 1.0 if: weapon index == -1, weapon type == 0x17, or damage <= 0.
// Uses sqrt(damage) and power function for non-linear falloff.
// =============================================================================
float damage_compute_falloff(double damage, int weapon_index, int weapon_type) {
    float result = 1.0f;

    if (weapon_index == -1) return result;
    if (weapon_type == 0x17) return result;

    float falloff_exp = *(float*)(&WEAPON_FALLOFF_TABLE + weapon_index * 0x3c);
    if (falloff_exp == 1.0f) return result;
    if (damage <= 0.0) return result;

    float sqrt_damage = sqrt_f(damage);
    result = pow_f(falloff_exp, sqrt_damage);
    return result;
}

// =============================================================================
// Address: 0x8008fe60 | Name: damage_compute_from_weapon
// Computes base damage from weapon definition tables.
// Steps:
// 1. Early out if weapon slot == -1 or story difficulty == 1 (no damage)
// 2. Get death animation index from entity model
// 3. Map to death anim category via damage_get_death_anim_category
// 4. Check special flags (category 0 + specific entity flags = instant kill)
// 5. Apply distance falloff via damage_compute_falloff
// Story mode: AI bots can't kill flagged entities (slot +0x44 = 0)
// =============================================================================
void damage_compute_from_weapon(int entity, int weapon_slot, int weapon_type, int* event_data) {
    uint64_t event_pair = event_get_extended();
    int victim = (int)(event_pair >> 32);
    int attacker = (int)event_pair;
    int char_slot = *(int*)(victim + 0x7c);

    if (*(int*)(event_data + 0x24) == -1 || g_story_difficulty == 1) {
        event_end_extended();
        return;
    }

    // Get death animation category
    int death_anim_idx;
    if (weapon_type != -1) {
        if (event_data == NULL || (*(uint*)(event_data + 0x40) & 8) == 0) {
            int model_idx = get_model_death_anim_index();
            death_anim_idx = *(int*)(*(int*)(*(int*)(*(int*)(victim + 4) + 0x84) + 0x5c) + model_idx * 4);
        } else {
            death_anim_idx = 1; // default
        }

        int category = damage_get_death_anim_category(death_anim_idx);

        // Category 0 (normal) + entity has instant-kill flag + attacker is AI
        if (category == 0) {
            uint entity_flags = check_entity_instant_kill(char_slot);
            if (entity_flags == 0 &&
                (*(uint*)(*(int*)(char_slot + 0xbf0) + 0x58) & 0x4000000) != 0 &&
                event_data != NULL && *(int*)(*(int*)(event_data + 4) + 0x150) == 0) {
                *(int*)(event_data + 0x44) = 0; // prevent death
            }
        }
    }

    // Compute falloff and apply
    if (g_difficulty == 0x0a) {
        // Story mode: different paths for player/AI
        if (*(int*)(victim + 0x150) == 0) {
            int zone_check = check_zone_damage_mult((int*)(char_slot + 0x4c));
            damage_compute_falloff(/* params */);
        } else {
            damage_compute_falloff(/* params */);
        }
    } else {
        damage_compute_falloff(/* params */);
    }

    event_end_extended();
}

// =============================================================================
// Address: 0x800900b8 | Name: damage_resolve_and_dispatch
// Resolves final damage amount based on target entity type, then dispatches.
// Entity type 8 (character): calls damage_compute_from_weapon
// Entity type 0x20 (vehicle): extracts occupant, routes through weapon calc
// Other types: uses generic damage formula (FUN_80090be4)
// After resolution, calls damage_dispatch_to_entity.
// =============================================================================
void damage_resolve_and_dispatch(/* variadic */, int* event_data) {
    event_data[8] = (int)0.0f; // zero initial damage

    int weapon_slot = event_data[9];
    if (weapon_slot == -1) {
        // No weapon - skip computation
    } else {
        int target = *event_data;
        if (*(int*)(target + 0x20) == 8) {
            // Character target
            float damage = damage_compute_from_weapon(target, weapon_slot, event_data[6], event_data);
            event_data[8] = (int)damage;
        } else if (*(int*)(target + 0x20) == 0x20 && *(int*)(target + 0x7c) != 0) {
            // Vehicle target: get occupant entity for damage calc
            int* occupant = *(int**)(*(int*)(target + 0x7c) + 0xec);
            event_data[0x10] |= 8; // mark as vehicle hit
            float damage = damage_compute_from_weapon(*occupant, event_data[9], event_data[6], event_data);
            event_data[8] = (int)damage;
        } else {
            // Generic entity: use base formula
            float damage = compute_generic_entity_damage((double)(float)event_data[0x15], weapon_slot, event_data[6]);
            event_data[8] = (int)damage;
        }
    }

    // Dispatch to target
    damage_dispatch_to_entity(/* variadic */, event_data);
}

// =============================================================================
// Address: 0x80090180 | Name: damage_dispatch_to_entity
// Final damage dispatch. Validates target can receive damage, then applies.
// For character entities (type 8): routes to damage_event_main_handler.
// For non-characters: subtracts directly from health, with optional callback.
// Awards hit events: 0x4f (damage dealt), 0x5a (while target has armor).
// Special: entity type 0xc4 gets additional validation check.
// Explosive weapons (type 0x1a) get splash damage multiplier on props.
// =============================================================================
void damage_dispatch_to_entity(/* variadic */, int* event_data) {
    if (g_damage_system_active == 0) return;

    // Target must be damageable (flag 0x10 on entity or flag 0x2 on event)
    if ((*(uint*)(*event_data + 0x28) & 0x10) == 0 && (event_data[0x10] & 2) == 0) return;

    // Special entity type 0xc4: additional validation
    if (*(short*)(*event_data + 0x94) == 0xc4) {
        int valid = validate_entity_damage_target(event_data);
        if (valid == 0) return;
    }

    // Award hit event to attacker (0x4f = damage dealt)
    int attacker = event_data[1];
    if (attacker != 0 && *(int**)(attacker + 0x150) != NULL &&
        *(int*)(attacker + 0x20) == 8) {
        award_animation_event((double)(float)event_data[8], 0x4f, **(int**)(attacker + 0x150));

        // Award 0x5a if target has armor/shield
        if (*(int*)(*event_data + 0x80) != 0) {
            award_animation_event((double)AWARD_DURATION, 0x5a, **(int**)(event_data[1] + 0x150));
        }
    }

    // Mark entity as hit
    *(uint*)(*event_data + 0x18) |= 0x40;

    // Route by entity type
    if (*(int*)(*event_data + 0x20) == 8) {
        // Character: full damage pipeline
        damage_event_main_handler(/* params */);
        return;
    }

    // Non-character entity damage
    // Check for splash damage multiplier on props (explosive weapons)
    if ((*(uint*)(*(int*)(*event_data + 0x24) + 0x28) & 0x8000000) != 0 ||
        (game_is_standard_arcade() == 0 &&
         (*(int*)(*event_data + 0x20) == 8 || *(int*)(*event_data + 0x20) == 0x1000))) {
        int target = *event_data;
        if ((*(uint*)(target + 0x28) & 1) != 0 &&
            (*(int*)(target + 0x20) == 8 || *(int*)(target + 0x20) == 0x1000 ||
             0.0f < *(float*)(target + 0x1cc)) &&
            event_data[9] == 0x1a) {
            // Explosive splash multiplier on props
            event_data[8] = (int)((float)event_data[8] * DAMAGE_PROP_EXPLOSIVE_MULT);
        }
    }

    // Apply damage (via callback or direct)
    int target = *event_data;
    if (*(void**)(target + 0x19c) == NULL || event_data[10] == 0) {
        // Direct health subtraction
        *(float*)(target + 0x1cc) -= (float)event_data[8];
    } else {
        // Custom damage callback
        (*(void(**)(int*))(target + 0x19c))(event_data);
    }

    // Store killer attribution if target died
    if (event_data[2] != 0 && *(float*)(*event_data + 0x1cc) <= 0.0f) {
        *(int*)(*event_data + 0x214) = *(int*)(event_data[2] + 0x2c);
    }
}

// =============================================================================
// Address: 0x8008f0ac | Name: damage_death_anim_dispatch
// MASSIVE death animation dispatcher. Called when character takes lethal damage.
// Selects death animation type based on weapon and hit properties:
//   Type 0 (normal/ragdoll): standard ragdoll with optional headshot check
//   Type 1 (melee): melee kill animation
//   Type 2 (explosion): explosion ragdoll with awards
//   Type 3 (burn/electro): special death effect
//   Type 4 (gib): dismemberment
//   Type 5 (ranged headshot): sniper/precision kill
// Spawns ragdoll entity, awards kill events (0x17=kill, 0x18=melee, 0x19=explosion),
// triggers story mode callbacks, sets death animation index on event_data[0x12].
// =============================================================================
void damage_death_anim_dispatch(/* variadic */) {
    int* event_data = (int*)event_get_data_extended();
    int* attacker_slot = NULL;
    int attacker_entity = event_data[1];
    int char_slot = *(int*)(*event_data + 0x7c);

    if (attacker_entity != 0 && *(int*)(attacker_entity + 0x20) == 8) {
        attacker_slot = *(int**)(attacker_entity + 0x7c);
    }

    if (event_data[6] < 0) {
        event_end_data_extended();
        return;
    }

    int death_type = -1;
    int ragdoll_spawned = 0;

    // Default animation table pointer
    if (event_data[0xc] == 0) {
        event_data[0xc] = (int)&DEFAULT_DEATH_ANIM_TABLE;
    }

    // Determine death animation index
    if (event_data[0x11] == -1) {
        if (event_data[6] == 0x17) {
            // Random for generic
            uint rng = random_uint();
            event_data[0x11] = rng % 0x14;
        } else {
            // From entity model
            int model_idx = get_model_death_anim_index();
            event_data[0x11] = *(int*)(*(int*)(*(int*)(*(int*)(*event_data + 4) + 0x84) + 0x5c) + model_idx * 4);
        }
    }

    // Map index to death type
    switch (event_data[0x11]) {
        case 3: case 4:   death_type = 0; break; // normal ragdoll
        case 5: case 6: case 9: case 10: death_type = 1; break; // melee
        case 7: case 8:   death_type = 3; break; // burn/electro
        case 11: case 12: death_type = 2; break; // explosion
        case 13: case 14: case 15: death_type = 5; break; // ranged headshot
        case 16: case 17: case 18: death_type = 4; break; // gib
    }

    // Process by death type
    if (death_type == 2) {
        // Explosion death: story mode only, spawn ragdoll
        if (event_data[1] != 0 && g_difficulty == 0x0a) {
            ragdoll_spawned = spawn_death_ragdoll(/* params: entity, dir, hit_pos, type 1 */);
            if (ragdoll_spawned != 0) {
                activate_ragdoll(ragdoll_spawned);
                if (attacker_slot != NULL) {
                    award_animation_event((double)AWARD_DURATION, 0x19, attacker_slot[2]);
                }
                // Story mode callback
                if ((g_game_flags & 0x40) != 0 &&
                    (g_match_subtype == 0 || g_match_subtype == 1 || g_match_subtype == 2) &&
                    *(int*)(event_data[1] + 0x150) != 0) {
                    story_kill_callback(2, (int)event_data);
                }
            }
        }
    } else if (death_type < 2) {
        // Normal/melee death
        if (death_type == 0 || (death_type == 1 && event_data[1] != 0 && *(int*)(event_data[1] + 0x150) != 0)) {
            // Check headshot
            if (event_data[6] != 0x15 && (*(uint*)(*(int*)(char_slot + 0xbf0) + 0x58) & 0x40000) == 0) {
                if (death_type == 0) {
                    damage_check_headshot_bounds(event_data);
                    // If headshot confirmed and entity has head attachment
                    int head = *(int*)(*(int*)(*event_data + 0x7c) + 0xbc4);
                    if (head != 0) {
                        int head_slot = *(int*)(head + 0x7c);
                        if (head_slot != 0) *(int*)(head_slot + 0x48) = 1;
                    }
                }

                // Spawn ragdoll (type 0 = standard)
                if ((event_data[0x10] & 4) == 0 || *(float*)(*event_data + 0x1cc) < (float)event_data[8]) {
                    ragdoll_spawned = spawn_death_ragdoll(/* params */);
                }
            }

            if (ragdoll_spawned != 0) {
                activate_ragdoll(ragdoll_spawned);
                // Instant kill if head popped
                if (*(int*)(*(int*)(*event_data + 0x7c) + 0x44) == 1) {
                    *(float*)(*event_data + 0x1cc) = 0.0f;
                }
                // Awards
                if (attacker_slot != NULL) {
                    award_animation_event((double)AWARD_DURATION, 0x17, attacker_slot[2]);
                    if (attacker_slot != NULL && event_data[6] == 0) {
                        award_animation_event((double)AWARD_DURATION, 0x18, attacker_slot[2]);
                    }
                    // Player kill event
                    if ((*attacker_slot != 0) && *(int*)(*attacker_slot + 0x150) != 0 &&
                        *(int*)(*(int*)(*attacker_slot + 0x150) + 4) >= 0) {
                        trigger_kill_sfx(0x2b);
                    }
                }
                award_animation_event((double)AWARD_DURATION, 0x1e, *(int*)(char_slot + 8));
                // Story mode
                if (attacker_slot == NULL || event_data[6] != 0) {
                    if ((g_game_flags & 0x40) != 0 &&
                        (g_match_subtype == 0 || g_match_subtype == 1 || g_match_subtype == 2) &&
                        event_data[1] != 0 && *(int*)(event_data[1] + 0x150) != 0) {
                        story_kill_callback(0, (int)event_data);
                    }
                } else {
                    award_animation_event((double)AWARD_DURATION, 0x1f, *(int*)(char_slot + 8));
                    if ((g_game_flags & 0x40) != 0 &&
                        (g_match_subtype == 0 || g_match_subtype == 1 || g_match_subtype == 2) &&
                        event_data[1] != 0 && *(int*)(event_data[1] + 0x150) != 0) {
                        story_kill_callback(1, (int)event_data);
                    }
                }
            }
        }
    } else if (death_type >= 3 && death_type < 4) {
        // Burn/electro (same as explosion path but type 2)
        if (event_data[1] != 0 && g_difficulty == 0x0a) {
            ragdoll_spawned = spawn_death_ragdoll(/* type 2 */);
            if (ragdoll_spawned != 0) {
                activate_ragdoll(ragdoll_spawned);
                if (attacker_slot != NULL) {
                    award_animation_event((double)AWARD_DURATION, 0x19, attacker_slot[2]);
                }
                if ((g_game_flags & 0x40) != 0 &&
                    (g_match_subtype == 0 || g_match_subtype == 1 || g_match_subtype == 2) &&
                    *(int*)(event_data[1] + 0x150) != 0) {
                    story_kill_callback(2, (int)event_data);
                }
            }
        }
    }

    // Set death animation type on event for later use
    switch (event_data[0x11]) {
        case 0: case 1:                     event_data[0x12] = 7; break;
        case 2: case 13: case 16:           event_data[0x12] = 1; break;
        case 3: case 4:                     event_data[0x12] = 4; break;
        case 5: case 6: case 7: case 8:     event_data[0x12] = 3; break;
        case 9: case 10: case 11: case 12:  event_data[0x12] = 2; break;
        case 14: case 15:                   event_data[0x12] = 6; break;
        case 17: case 18:                   event_data[0x12] = 5; break;
        default:                            event_data[0x12] = 0; break;
    }

    // Attacker kill streak tracking
    if (attacker_slot != NULL && event_data[6] != 0 && event_data[6] != 0x15) {
        track_attacker_kill_streak((int)attacker_slot, char_slot, event_data[0x11]);
    }

    event_end_data_extended();
}
