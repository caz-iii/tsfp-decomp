// =============================================================================
// damage_type_handlers.cpp — Per-entity-type damage filters and modifiers
// TimeSplitters: Future Perfect (GameCube)
// Batch 194 | Date: 2026-08-11
//
// These functions are registered as damage handlers for different entity types.
// They pre-process the damage context (zero out, scale, cap) before dispatching
// to damage_apply_simple (MP) or damage_apply_full (SP).
// =============================================================================

#include "common.h"
#include "damage.h"
#include "entity.h"
#include "sound.h"

// =============================================================================
// Address: 0x80091520 | Name: damage_play_impact_sound
// Plays a positional impact sound based on entity type:
// - Type 0x30/0x32: generic hit sound (ID 0x683)
// - Type 0xA0: time-scaled dynamic sound (ID = time_factor + 0xD83)
// Called when an entity takes a hit to provide audio feedback.
// =============================================================================
void damage_play_impact_sound(int entity_ptr)
{
    unsigned short entity_type = *(unsigned short *)(entity_ptr + 0x94);
    int sound_params[7];
    int sound_params_b[7];

    if (entity_type == 0xA0) {
        // Time-scaled sound for special entities
        unsigned int game_timer = get_game_timer();
        int time_factor = (int)(g_time_scale_a *
            ((float)((double)game_timer - g_timer_offset_dbl) / g_time_divisor) +
            g_time_base_offset);

        sound_init_play_params((int)sound_params);
        // Set 3D positional flag
        sound_params[7] = 1;  // local_50 = positional
        // Position from entity
        sound_params[4] = *(int *)(entity_ptr + 0x64);   // x
        sound_params[5] = *(int *)(entity_ptr + 0x68);   // y
        sound_params[6] = *(int *)(entity_ptr + 0x6C);   // z
        sound_params[0] = time_factor + 0xD83;
        sound_play_request(sound_params);
        return;
    }

    if (entity_type != 0x30 && entity_type != 0x32) {
        return;
    }

    // Generic hit sound
    sound_init_play_params((int)sound_params_b);
    sound_params_b[7] = 1;  // positional
    sound_params_b[0] = 0x683;
    sound_params_b[4] = *(int *)(entity_ptr + 0x64);
    sound_params_b[5] = *(int *)(entity_ptr + 0x68);
    sound_params_b[6] = *(int *)(entity_ptr + 0x6C);
    sound_play_request(sound_params_b);
}

// =============================================================================
// Address: 0x80091628 | Name: damage_character_receive
// MASSIVE character damage receive handler (9 xrefs). Similar to
// damage_entity_receive but with additional logic:
// - Armor-to-stagger transition (flag 0x2 -> 0x4) with sound stop
// - Health threshold check: if health drops below max_health but is still
//   positive, triggers "wounded" state transition (stagger anim)
// - Full death sequence: clears armor/invuln flags, sets death anim category
//   based on attacker type (player type 4 = special)
// - Respawn handling via entity type flag 0x200
// =============================================================================
void damage_character_receive(void* p1, float p2, float p3, float p4,
                              float p5, float p6, float p7, void* p8,
                              int* damage_ctx)
{
    int entity = *damage_ctx;
    unsigned int entity_flags = *(unsigned int *)(entity + 0x9C);
    int weapon_type = damage_ctx[6];

    // Invulnerability check (same as damage_entity_receive)
    if ((entity_flags & 0x100000) != 0 && weapon_type != 0x17) {
        unsigned int weapon_flags = get_weapon_class_flags(weapon_type);
        if ((weapon_flags & 0x800) == 0) {
            return;
        }
    }

    // Record attacker
    *(int *)(entity + 0x210) = damage_ctx[1];

    // Self-damage position check (flag 0x4)
    if ((entity_flags & 0x4) != 0) {
        float* hit_pos = (float *)damage_ctx[10];
        if (hit_pos != NULL) {
            float ex = *(float *)(entity + 0x64) + *(float *)(entity + 0x1A8);
            float ey = *(float *)(entity + 0x68) + *(float *)(entity + 0x1AC);
            float ez = *(float *)(entity + 0x6C) + *(float *)(entity + 0x1B0);
            if (hit_pos[0] == ex && hit_pos[1] == ey && hit_pos[2] == ez) {
                return;
            }
        }
    }

    // Armor mode handling (flag 0x2 with weapon flag 0x800)
    if ((entity_flags & 0x2) != 0) {
        unsigned int weapon_flags = get_weapon_class_flags(weapon_type);
        if ((weapon_flags & 0x800) != 0) {
            if (*(float *)(entity + 0x1CC) > 0.0f) {
                // Armor intact but health > 0: armor absorbs, do nothing special
            }
            else {
                // Armor depleted
                if (*(int *)(entity + 0x168) != 0) {
                    return;  // Already in death state
                }
                // Transition: clear armor flag, set vulnerable flag
                *(unsigned int *)(*(int *)(entity + 4) + 0x30) &= ~0x2;
                *(unsigned int *)(*(int *)(entity + 4) + 0x30) |= 0x4;
                *(unsigned int *)(entity + 0x18) |= 0x2;
                // Stop any active sound
                if (*(int *)(entity + 0x228) != 0) {
                    sound_stop_handle();
                }
                *(int *)(entity + 0x228) = 0;
                *(unsigned int *)(entity + 0x28) |= 0x4000000;
                entity_register_for_update(entity, 5, FUN_death_fade_callback);
                trigger_death_effect();
                if ((*(unsigned int *)(entity + 0x28) & 0x10000) != 0) {
                    trigger_armor_break_effect();
                }
                goto post_damage;
            }
        }
    }

    // Weapon-type-specific damage application
    if (weapon_type == 0x16 && *(int *)(entity + 0x168) != 0) {
        // Fire: disable render update
        *(unsigned int *)(entity + 0x28) &= ~0x4000000;
        entity_register_for_update(entity, 5, 0);
    } else if (weapon_type == 0x17) {
        // Poison with cooldown
        if (*(int *)(entity + 0x1A4) > 0) return;
        if (*(int *)(entity + 0x160) == 0) {
            *(int *)(entity + 0x1A4) = (int)g_frame_rate >> 3;
            entity_register_for_update(entity, 3, FUN_poison_tick_callback);
            *(unsigned int *)(entity + 0x28) |= 0x4000000;
        }
        if ((*(unsigned int *)(*(int *)(entity + 4) + 0x30) & 4) == 0) {
            *(float *)(entity + 0x1CC) -= (float)damage_ctx[8];
        } else {
            *(float *)(entity + 0x1CC) -= (float)damage_ctx[8] / g_armor_divisor;
        }
    } else {
        // Standard subtraction
        *(float *)(entity + 0x1CC) -= (float)damage_ctx[8];
    }

post_damage:
    if (*(int *)(entity + 0x1A4) > 0) return;

    // Notify AI
    if (*(int *)(entity + 0x80) != 0) {
        notify_ai_damaged(entity);
    }

    // Wounded state: health below max but still alive
    float health = *(float *)(entity + 0x1CC);
    if (health < *(float *)(entity + 0x1DC) && health > 0.0f) {
        unsigned int type_flags = *(unsigned int *)(*(int *)(entity + 4) + 0x30);
        if ((type_flags & 0x2) != 0) {
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) = type_flags & ~0x2;
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) |= 0x4;
            *(unsigned int *)(entity + 0x18) |= 0x2;
            if ((*(unsigned int *)(entity + 0x28) & 0x10000) != 0) {
                trigger_armor_break_effect();
            }
            trigger_death_sequence();
            dispatch_death_awards(entity, 0, (int)(float)damage_ctx[8]);
            trigger_death_particles();
        }
    }

    // Death: health <= 0
    if (health <= 0.0f) {
        int type_data = *(int *)(entity + 4);
        unsigned int flags = *(unsigned int *)(type_data + 0x30);
        if ((flags & 0x2) != 0 || (flags & 0x4) != 0) {
            // Clear armor/vulnerable bits
            *(unsigned int *)(type_data + 0x30) &= ~0x6;
            *(unsigned int *)(entity + 0x18) |= 0x2;
            // Death anim category based on attacker
            if (*(int *)(entity + 0x20C) == 0 ||
                *(short *)(*(int *)(entity + 0x20C) + 0x94) == 4) {
                *(unsigned int *)(*(int *)(entity + 4) + 0x30) |= 0x8;
            } else {
                *(int *)(*(int *)(entity + 4) + 0x30) = 0;
            }
            if ((*(unsigned int *)(entity + 0x28) & 0x10000) != 0) {
                trigger_armor_break_effect();
            }
            trigger_death_sequence();
            dispatch_death_awards(entity, 1, (int)(float)damage_ctx[8]);
            trigger_death_particles();
            *(float *)(entity + 0x1CC) = 0.0f;
            goto final_checks;
        }
    }

    // Ragdoll / silent death / falling
    if ((entity_flags & 0x1000) != 0 && health <= 0.0f) {
        *(float *)(entity + 0x1CC) = 0.0f;
        entity_start_falling(g_gravity_default, entity);
    }
    if ((entity_flags & 0x2000) == 0 && health <= 0.0f &&
        *(int *)(entity + 0x168) == 0) {
        *(float *)(entity + 0x1CC) = 0.0f;
        entity_set_render_type(entity, 0);
    }

final_checks:
    if (*(float *)(entity + 0x1CC) <= 0.0f) {
        if (*(int *)(entity + 0x80) != 0) {
            unsigned int respawn_flags = get_entity_type_flags(*(int *)(entity + 0x80));
            if ((respawn_flags & 0x200) != 0) {
                entity_set_render_type(entity, FUN_respawn_render);
                *(float *)(entity + 0x1CC) = *(float *)(entity + 0x1DC);
                return;
            }
        }
        if (*(void **)(entity + 0x158) != NULL) {
            (*(void (**)(int, int))(entity + 0x158))(entity, 0);
        }
        entity_set_render_type(entity, 0);
    }
}

// =============================================================================
// Address: 0x80091ac8 | Name: damage_filter_melee_zero
// Damage filter for entities immune to melee. If weapon has flag 0x800
// (melee class), zeroes the damage amount before dispatching.
// Used by: 4 xrefs (entity types with melee immunity)
// =============================================================================
void damage_filter_melee_zero(int* damage_ctx)
{
    unsigned int weapon_flags = get_weapon_class_flags(damage_ctx[6]);
    if ((weapon_flags & 0x800) != 0) {
        damage_ctx[8] = 0;  // Zero damage for melee
    }

    if (g_game_mode == '\n') {
        damage_apply_simple(damage_ctx);
    } else {
        damage_apply_full();
    }
}

// =============================================================================
// Address: 0x80091b30 | Name: damage_filter_environmental_zero
// Damage filter for entities immune to environmental damage. Weapon type
// 0x1A (environmental/hazard) has its damage zeroed before dispatch.
// =============================================================================
void damage_filter_environmental_zero(int* damage_ctx)
{
    if (damage_ctx[6] == 0x1A) {
        damage_ctx[8] = 0;  // Zero environmental damage
    }

    if (g_game_mode == '\n') {
        damage_apply_simple(damage_ctx);
    } else {
        damage_apply_full();
    }
}

// =============================================================================
// Address: 0x80091b80 | Name: damage_filter_melee_env_half
// Damage filter: zeroes melee (0x800) and environmental (0x1A) damage,
// then applies 0.5x multiplier to remaining damage in MP. Also sets
// damage_ctx[0x12] = 1 (flags as reduced/modified hit for scoring).
// =============================================================================
void damage_filter_melee_env_half(int* damage_ctx)
{
    unsigned int weapon_flags = get_weapon_class_flags(damage_ctx[6]);
    if ((weapon_flags & 0x800) != 0 || damage_ctx[6] == 0x1A) {
        damage_ctx[8] = 0;
    }

    float half_mult = 0.5f;
    if (g_game_mode == '\n') {
        damage_ctx[0x12] = 1;  // Flag as modified hit
        damage_ctx[8] = (int)((float)damage_ctx[8] * half_mult);
        damage_apply_simple(damage_ctx);
    } else {
        damage_apply_full();
    }
}

// =============================================================================
// Address: 0x80091c0c | Name: damage_cap_by_max_health
// Damage cap/modifier for shielded entities. In MP mode:
// - If entity has shield flag (0x10000 on weapon data) and weapon is melee
//   (0x800): zeroes damage entirely (shields block melee)
// - If both attacker and victim have active weapon slots and not arcade mode:
//   * Weapon type 6 (explosive?): applies special multiplier
//   * Other weapons: caps damage at max_health * cap_multiplier
// =============================================================================
void damage_cap_by_max_health(int* damage_ctx)
{
    if (g_game_mode == '\n') {
        // Shield check: melee blocked by shields
        int char_data = *(int *)(*damage_ctx + 0x7C);
        unsigned int shield_flags = *(unsigned int *)(*(int *)(char_data + 0xBF0) + 0x58);
        if ((shield_flags & 0x10000) != 0) {
            unsigned int weapon_flags = get_weapon_class_flags(damage_ctx[6]);
            if ((weapon_flags & 0x800) != 0) {
                damage_ctx[8] = 0;
                damage_apply_simple(damage_ctx);
                return;
            }
        }

        // Damage cap when both attacker and victim are armed
        if (g_game_mode == '\n' && g_arcade_mode_id != 100 &&
            *damage_ctx != 0 && damage_ctx[2] != 0 &&
            *(int *)(damage_ctx[2] + 0x150) != 0) {
            if (damage_ctx[6] == 6) {
                // Explosive: special multiplier
                damage_ctx[8] = (int)((float)damage_ctx[8] * g_explosive_cap_mult);
            } else {
                // Cap at max_health * multiplier
                float raw = (float)damage_ctx[8];
                float cap = g_damage_cap_mult * *(float *)(*damage_ctx + 0x1D8);
                if (cap < raw) {
                    raw = cap;
                }
                damage_ctx[8] = (int)raw;
            }
        }

        damage_apply_simple(damage_ctx);
    } else {
        damage_apply_full();
    }
}

// =============================================================================
// Address: 0x80091d14 | Name: damage_filter_fixed_category
// Conditional damage filter: only applies damage in MP mode when the
// damage source category (damage_ctx[9]) is 0x18. Sets a fixed damage
// value (g_fixed_damage_amount). Otherwise does nothing (blocks damage).
// =============================================================================
void damage_filter_fixed_category(int* damage_ctx)
{
    if (g_game_mode == '\n') {
        if (damage_ctx[9] == 0x18) {
            damage_ctx[8] = (int)g_fixed_damage_amount;
            damage_apply_simple(damage_ctx);
        }
    } else {
        damage_apply_full();
    }
}

// =============================================================================
// Address: 0x80091d64 | Name: damage_filter_special_half
// Damage modifier: if damage source category is 0x19, triggers a special
// handler (e.g., AI script callback). Otherwise applies 0.5x multiplier.
// Then dispatches normally.
// =============================================================================
void damage_filter_special_half(int* damage_ctx)
{
    if (damage_ctx[9] == 0x19) {
        // Special category: trigger AI/script handler
        ai_special_damage_handler(*damage_ctx);
    } else {
        // Half damage for non-special sources
        damage_ctx[8] = (int)((float)damage_ctx[8] * 0.5f);
    }

    if (g_game_mode == '\n') {
        damage_apply_simple(damage_ctx);
    } else {
        damage_apply_full();
    }
}

// =============================================================================
// Address: 0x80091ddc | Name: damage_match_end_handler
// Match end/round over handler. Called from character_on_death_handler when
// the death triggers match completion. Iterates all players:
// - Checks weapon data for 'PRAP' magic (0x50415250) to determine win/lose
// - Displays appropriate string (0x15E8 = victory, 0x15E9 = defeat)
// - Updates player HUD status
// - Sets global flag 0x10000 (match over)
// - Calls match_process_end to finalize scoring
// =============================================================================
void damage_match_end_handler(void)
{
    int entity_ptr = get_current_entity_from_stack();
    int player_count = get_active_player_count();

    for (int i = 0; i < player_count; i++) {
        unsigned int* msg_string;
        if (*(int *)(*(int *)(entity_ptr + 0xBF0) + 0x28) == 0x50415250) {
            // 'PRAP' magic = this player won
            msg_string = (unsigned int *)get_string_by_id(0x15E8);
            display_player_message(i, 4, msg_string);
        } else {
            // Loser message
            msg_string = (unsigned int *)get_string_by_id(0x15E9);
            display_player_message(i, 4, msg_string);
        }

        // Update player HUD data
        if (i >= 0 && i < 4 && g_player_data_base != 0) {
            int player_slot = *(int *)(g_player_data_base + i * 0x141C + 8);
            if (player_slot != 0) {
                void* score_data = get_score_data(*(int *)(player_slot + 0x214));
                update_score_display((int)score_data);
            }
        }
    }

    // Flag match as over
    g_match_state_flags |= 0x10000;
    match_process_end(0);

    restore_stack_frame();
}

// =============================================================================
// Address: 0x80091ed0 | Name: damage_filter_unarmed_double
// Damage modifier for unarmed combat. If both attacker (damage_ctx[1]) and
// victim (damage_ctx[2]) have no active weapon slot (offset 0x150 == 0),
// zeroes damage (unarmed entities can't hurt each other without weapons).
// Otherwise applies 2x multiplier (armed vs unarmed = bonus damage).
// =============================================================================
void damage_filter_unarmed_double(int* damage_ctx)
{
    int attacker = damage_ctx[1];
    int victim = damage_ctx[2];

    if ((attacker == 0 || *(int *)(attacker + 0x150) == 0) &&
        (victim == 0 || *(int *)(victim + 0x150) == 0)) {
        // Both unarmed: no damage
        damage_ctx[8] = 0;
    } else {
        // Armed vs unarmed: double damage
        damage_ctx[8] = (int)((float)damage_ctx[8] * 2.0f);
    }

    damage_apply_simple(damage_ctx);
}

// =============================================================================
// Address: 0x80091f3c | Name: damage_filter_arcade_match_end
// Damage modifier for arcade mode 0x1A with match-end trigger:
// - If arcade mode and attacker is AI-controlled (checked via char slot):
//   applies 0.5x multiplier
// - Else if both attacker/victim unarmed: applies low multiplier (~0.25x)
// - Else: applies 2x multiplier (armed combat)
// After damage_apply_simple, if health <= 0 calls damage_match_end_handler.
// =============================================================================
void damage_filter_arcade_match_end(void* p1, void* p2, void* p3, void* p4,
                                    void* p5, void* p6, void* p7, void* p8,
                                    int* damage_ctx)
{
    if (g_arcade_mode_id == 0x1A && damage_ctx[1] != 0) {
        int attacker_char = *(int *)(damage_ctx[1] + 0x7C);
        int is_ai = check_char_slot_is_ai(*(short *)(attacker_char + 0x4A));
        if (is_ai != 0) {
            damage_ctx[8] = (int)((float)damage_ctx[8] * 0.5f);
            goto apply;
        }
    }

    if ((damage_ctx[1] == 0 || *(int *)(damage_ctx[1] + 0x150) == 0) &&
        (damage_ctx[2] == 0 || *(int *)(damage_ctx[2] + 0x150) == 0)) {
        // Both unarmed: low multiplier
        damage_ctx[8] = (int)((float)damage_ctx[8] * g_unarmed_low_mult);
    } else {
        // Armed: 2x multiplier
        damage_ctx[8] = (int)((float)damage_ctx[8] * 2.0f);
    }

apply:
    damage_apply_simple(damage_ctx);

    // Match-end check on death
    if (*(float *)(*damage_ctx + 0x1CC) <= 0.0f) {
        damage_match_end_handler();
    }
}

// =============================================================================
// Address: 0x80092020 | Name: damage_filter_invincible
// Invincible entity handler (14 xrefs). Zeroes ALL incoming damage, then
// dispatches. Used by indestructible props, story-critical entities, etc.
// =============================================================================
void damage_filter_invincible(int* damage_ctx)
{
    damage_ctx[8] = 0;  // Zero all damage

    if (g_game_mode == '\n') {
        damage_apply_simple(damage_ctx);
    } else {
        damage_apply_full();
    }
}

// =============================================================================
// Address: 0x80092064 | Name: damage_filter_fixed_low
// Fixed low-damage handler. Overrides damage to a constant value (g_armor_divisor,
// typically 4.0). Used by entities that take fixed chip damage regardless of
// weapon power.
// =============================================================================
void damage_filter_fixed_low(int* damage_ctx)
{
    damage_ctx[8] = (int)g_armor_divisor;

    if (g_game_mode == '\n') {
        damage_apply_simple(damage_ctx);
    } else {
        damage_apply_full();
    }
}

// =============================================================================
// Address: 0x800920a8 | Name: damage_filter_scaled_melee_immune
// Sets damage to a specific scaled value (g_explosive_cap_mult), then zeroes
// if weapon is melee (flag 0x800). Used by entities that take fixed ranged
// damage but are immune to melee.
// =============================================================================
void damage_filter_scaled_melee_immune(int* damage_ctx)
{
    damage_ctx[8] = (int)g_explosive_cap_mult;

    unsigned int weapon_flags = get_weapon_class_flags(damage_ctx[6]);
    if ((weapon_flags & 0x800) != 0) {
        damage_ctx[8] = 0;  // Melee immune
    }

    if (g_game_mode == '\n') {
        damage_apply_simple(damage_ctx);
    } else {
        damage_apply_full();
    }
}

// =============================================================================
// Address: 0x80092118 | Name: damage_check_bag_tag_mode
// Returns whether the current game mode is "Bag Tag" (mode 6). If so, calls
// the bag-tag specific handler and returns its result. Otherwise returns 0.
// =============================================================================
int damage_check_bag_tag_mode(void)
{
    if (g_arcade_mode_id == 6) {
        return bag_tag_handler();
    }
    return 0;
}

// =============================================================================
// Address: 0x80092150 | Name: damage_filter_vip_match_end
// VIP kill damage handler (2 xrefs). Applies damage multiplier based on
// attacker status:
// - Attacker unarmed: applies g_explosive_cap_mult (unless bag-tag mode)
// - Attacker armed: applies g_time_scale_a multiplier
// On death, checks if killed entity is VIP (via FUN_8027eb3c). If VIP:
// - Shows kill/death message (string 0x15EA/0x15EB based on attacker armed)
// - Broadcasts to all players
// - Sets match-over flag 0x10000
// - Calls match_process_end
// =============================================================================
void damage_filter_vip_match_end(void* p1, void* p2, void* p3, void* p4,
                                 void* p5, void* p6, void* p7, void* p8,
                                 int* damage_ctx)
{
    if (damage_ctx[2] == 0 || *(int *)(damage_ctx[2] + 0x150) == 0) {
        // Attacker unarmed
        int bag_tag = damage_check_bag_tag_mode();
        if (bag_tag == 0) {
            damage_ctx[8] = (int)((float)damage_ctx[8] * g_explosive_cap_mult);
        }
    } else {
        // Attacker armed
        damage_ctx[8] = (int)((float)damage_ctx[8] * g_time_scale_a);
    }

    if (g_game_mode == '\n') {
        damage_apply_simple(damage_ctx);

        if (*(float *)(*damage_ctx + 0x1CC) <= 0.0f) {
            int char_data = *(int *)(*damage_ctx + 0x7C);
            int is_vip = check_entity_is_vip(char_data);
            if (is_vip != 0) {
                char msg_buf[256];
                char* msg;
                if (damage_ctx[1] == 0 || *(int *)(damage_ctx[1] + 0x150) == 0) {
                    msg = (char *)get_string_by_id(0x15EB);  // VIP killed by world
                } else {
                    msg = (char *)get_string_by_id(0x15EA);  // VIP assassinated
                }
                format_string(msg_buf, msg, *(int *)(char_data + 0x70));

                int player_count = get_active_player_count();
                for (int i = 0; i < player_count; i++) {
                    display_player_message(i, 4, msg_buf);
                }

                g_match_state_flags |= 0x10000;
                match_process_end(0);
            }
        }
    } else {
        damage_apply_full();
    }
}
