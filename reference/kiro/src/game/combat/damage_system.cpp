// =============================================================================
// damage_system.cpp - Character Damage Application & Combat Events
// Handles: damage calculation with modifiers, screen effects, pain/death sounds,
//          hit events, kill awards, death handling with scoring
// Batch 191 | Date: 2026-08-11
// =============================================================================

#include "common.h"
#include "entity.h"
#include "sound.h"
#include "character.h"

// External references
extern float g_delta_time;                       // 0x806108ec
extern int   g_frame_counter;                    // 0x80611928
extern int   g_level_type;                       // 0x80612614
extern uint  g_game_flags;                       // 0x805015c0
extern uint  g_match_flags;                      // 0x805015c4
extern char  g_difficulty;                       // 0x805015bd
extern char  g_game_mode;                        // 0x805015be

extern void sound_init_play_params(int params);
extern void sound_play_request(int* params);
extern void sound_play_ambient_3d(double vol, double falloff);
extern void sound_get_emitter_position(int* slot, void* out);
extern float* entity_get_world_position(int entity);
extern void entity_set_render_type(int entity, int type);
extern int  score_register_death(void* killer_slot, int victim_team, int count);
extern void match_process_end(/* variadic */);
extern char* get_string_by_id(int string_id);

// =============================================================================
// Address: 0x8008dcd4 | Name: damage_apply_simple
// Simple damage application without weapon/armor modifiers.
// Subtracts damage from shield first (field +0x78), remainder from health.
// Calls damage feedback if entity has a player controller.
// Used for environmental damage, fall damage, etc.
// =============================================================================
void damage_apply_simple(int* event_data) {
    int entity = *event_data;
    int char_slot = *(int*)(entity + 0x7c);

    // Instant kill flag check (match flag 0x2000000)
    if ((g_match_flags & 0x2000000) != 0 && entity != 0 && *(int*)(entity + 0x150) != 0) {
        event_data[8] = (int)DAMAGE_INSTANT_KILL; // force max damage
    }

    // Damage feedback (screen shake, hit indicator, etc.)
    if (*(int*)(*event_data + 0x150) != 0) {
        damage_apply_feedback(event_data);
    }

    // Check for armor/powerup damage reduction
    uint powerup_flags = get_active_powerups(char_slot);
    if (powerup_flags != 0) {
        apply_powerup_damage_reduction(char_slot + 0xc68, (int)event_data);
    }

    // Apply damage: shield absorbs first, then health
    float damage = (float)event_data[8];
    float shield = *(float*)(char_slot + 0x78);
    float applied = (damage <= shield) ? damage : shield;

    // Flag 0x200000: bypass shield entirely
    if ((event_data[0xf] & 0x200000) != 0) {
        applied = DAMAGE_INSTANT_KILL;
    }

    *(float*)(char_slot + 0x78) -= applied;
    event_data[8] = (int)((float)event_data[8] - applied);

    // Store damage info for HUD
    *(float*)(char_slot + 0xb44) = 0.0f;
    *(int*)(char_slot + 0xb48) = event_data[8];

    // Subtract remaining from health
    *(float*)(*event_data + 0x1cc) -= (float)event_data[8];
    if (*(float*)(*event_data + 0x1cc) < 0.0f) {
        *(float*)(*event_data + 0x1cc) = 0.0f;
    }
}

// =============================================================================
// Address: 0x8008dde8 | Name: damage_apply_full
// MASSIVE full damage handler with all combat modifiers:
// - Weapon type flags (armor piercing, headshot multiplier, etc.)
// - Difficulty scaling (easy/normal/hard multipliers)
// - Friendly fire reduction
// - Team damage scaling
// - One-hit-kill game mode
// - Shield absorption with remainder to health
// - Attacker scoring feedback
// - Splash damage to nearby entities (weapon type 0x19)
// - Mode-specific damage rules (mode 0x0e, 0x0c, etc.)
// =============================================================================
void damage_apply_full(void) {
    int* event_data = (int*)event_get_data();
    int attacker_slot = 0;
    int char_slot = *(int*)(*event_data + 0x7c);

    // Get weapon flags for damage type
    uint weapon_flags = get_weapon_type_flags(char_slot);
    int attacker_entity = event_data[1];

    // Identify attacker character slot
    if (attacker_entity != 0 && *(int*)(attacker_entity + 0x20) == 8) {
        attacker_slot = *(int*)(attacker_entity + 0x7c);
    }

    // Armor piercing check (game flag 0x1000000)
    if (*(int*)(*event_data + 0x150) != 0 && (g_game_flags & 0x1000000) != 0) {
        uint ap_flags = get_weapon_ap_flags(event_data[6]);
        if (ap_flags == 0) {
            // Apply armor damage reduction from equipped armor table
            event_data[8] = (int)((float)event_data[8] *
                *(float*)(&ARMOR_TABLE + *(int*)(*(int*)(*(int*)(*event_data + 0x150) + 0x1398) + 0x6c) * 4));
        }
    }

    // Weapon type modifiers
    if (weapon_flags != 0) {
        uint wp_class = get_weapon_class(event_data[6]);
        if ((wp_class & 0x800) == 0) {
            // Standard weapons (type 0x1a, 0x2f = explosives)
            if (event_data[6] == 0x1a || event_data[6] == 0x2f) {
                if ((weapon_flags & 0x10) != 0) {
                    event_data[8] = (int)0.0f; // immune
                }
                if ((weapon_flags & 0x20) != 0) {
                    event_data[8] = (int)((float)event_data[8] * DAMAGE_HALF_MULT);
                }
            }
        } else {
            // Energy/special weapons
            if ((weapon_flags & 2) != 0) event_data[8] = (int)0.0f;
            if ((weapon_flags & 8) != 0) event_data[8] = (int)((float)event_data[8] * DAMAGE_HALF_MULT);
            if ((weapon_flags & 4) != 0) event_data[8] = (int)((float)event_data[8] * DAMAGE_QUARTER_MULT);
        }
    }

    // Attacker has damage boost powerup (flag 0x800 on character)
    if (attacker_slot != 0 && (*(uint*)(attacker_slot + 0xc6c) & 0x800) != 0) {
        event_data[8] = (int)((float)event_data[8] * DAMAGE_HALF_MULT);
    }

    // Damage feedback
    if (*(int*)(*event_data + 0x150) != 0) {
        damage_apply_feedback(event_data);
    }

    // Bot difficulty damage scaling (attacker is bot, victim is player)
    if (event_data[1] != 0 && *(int*)(event_data[1] + 0x20) == 8 &&
        char_slot != 0 && *(int*)(*event_data + 0x150) == 0) {
        // Scale by bot accuracy/level
        event_data[8] = (int)((float)event_data[8] *
            (DAMAGE_BOT_BASE + (float)((double)(*(uint*)(char_slot + 0xb04) ^ 0x80000000) -
            DOUBLE_BIAS) / DAMAGE_BOT_SCALE));
    }

    // Difficulty level 3 (hard) modifiers
    if (g_difficulty == 3) {
        // Friendly fire from bots deals extra damage
        if ((event_data[1] != 0 && *(int*)(event_data[1] + 0x150) == 0) ||
            (event_data[2] != 0 && *(int*)(event_data[2] + 0x150) == 0)) {
            event_data[8] = (int)((float)event_data[8] * DAMAGE_HARD_MULT);
        }
        // High-tier bots (level >= threshold) deal more
        if (*(int*)(char_slot + 0x84) == 1 && g_max_bot_level <= *(int*)(char_slot + 8)) {
            event_data[8] = (int)((float)event_data[8] * DAMAGE_ELITE_MULT);
        }
    }

    // Story mode specific victim (mode 0x12)
    if (g_game_mode == 0x12 && *event_data == g_story_player_entity) {
        event_data[8] = (int)((float)event_data[8] * DAMAGE_QUARTER_MULT);
    }

    // One-hit-kill mode (game flag 0x4): attacker is character type
    if (0.0f < (float)event_data[8] && (g_game_flags & 4) != 0) {
        if ((event_data[1] != 0 && (*(int*)(event_data[1] + 0x20) == 8 || *(int*)(event_data[1] + 0x20) == 0x200)) ||
            (event_data[2] != 0 && (*(int*)(event_data[2] + 0x20) == 8 || *(int*)(event_data[2] + 0x20) == 0x200))) {
            *(float*)(char_slot + 0x78) = 0.0f; // zero shield
            event_data[8] = *(int*)(*event_data + 0x1cc); // set damage = full health
        }
    }

    // Shield absorption
    float damage = (float)event_data[8];
    float shield = *(float*)(char_slot + 0x78);
    float applied = (damage <= shield) ? damage : shield;
    *(float*)(char_slot + 0x78) -= applied;
    event_data[8] = (int)((float)event_data[8] - applied);

    // Store weapon ID for kill feed
    *(int*)(*event_data + 0x1d4) = g_frame_counter;
    *(float*)(*event_data + 0x1d0) = *(float*)(char_slot + 0x78);

    // HUD damage indicators
    *(float*)(char_slot + 0xb44) = 0.0f;
    *(int*)(char_slot + 0xb48) = event_data[8];

    // Attacker feedback (hit marker activation)
    if (*(int*)(*event_data + 0x150) != 0 &&
        (g_game_mode != 0x0c || ((1 << *(int*)(char_slot + 8)) & g_team_mask) == 0)) {
        *(float*)(*(int*)(*event_data + 0x150) + 0x1404) = 1.0f;
    }

    // Apply to health
    *(float*)(*event_data + 0x1cc) -= (float)event_data[8];
    if (*(float*)(*event_data + 0x1cc) < 0.0f) {
        *(float*)(*event_data + 0x1cc) = 0.0f;
    }

    // Splash damage to nearby entity (weapon type 0x19, mode 0x0e)
    if (event_data[6] == 0x19) {
        int splash_target = event_data[2];
        if (splash_target != *event_data && splash_target != 0 &&
            *(int*)(splash_target + 0x20) == 8) {
            float splash_mult = (event_data[9] == 0x15) ? DAMAGE_SPLASH_HIGH : DAMAGE_SPLASH_LOW;
            *(float*)(splash_target + 0x1cc) += splash_mult * (float)event_data[8];
            // Clamp to max health
            float hp = *(float*)(splash_target + 0x1cc);
            float max_hp = *(float*)(splash_target + 0x1d8);
            if (max_hp < hp) hp = max_hp;
            *(float*)(splash_target + 0x1cc) = hp;
        }
    }

    // Mode 0x0e: same splash logic
    if (g_game_mode == 0x0e) {
        int splash_target = event_data[2];
        if (splash_target != *event_data && splash_target != 0 &&
            *(int*)(splash_target + 0x20) == 8) {
            float splash_mult = (event_data[9] == 0x15) ? DAMAGE_SPLASH_HIGH : DAMAGE_SPLASH_LOW;
            *(float*)(splash_target + 0x1cc) += splash_mult * (float)event_data[8];
            float hp = *(float*)(splash_target + 0x1cc);
            float max_hp = *(float*)(splash_target + 0x1d8);
            if (max_hp < hp) hp = max_hp;
            *(float*)(splash_target + 0x1cc) = hp;
        }
    }

    event_end_data();
}

// =============================================================================
// Address: 0x8008e2e8 | Name: damage_set_screen_effect
// Sets damage screen flash parameters (red flash intensity + duration)
// based on weapon type index and damage amount.
// param_1 (double): damage amount for scaling
// param_2: character slot pointer
// param_3: weapon type index (-1 = generic, 0x17 = special, else table lookup)
// =============================================================================
void damage_set_screen_effect(double damage_amount, int char_slot, int weapon_type) {
    if (weapon_type < 0) {
        // Generic damage: scale intensity by damage amount
        float intensity = DAMAGE_SCREEN_GENERIC_MAX;
        if (damage_amount < (double)DAMAGE_HARD_MULT) {
            intensity = (float)((double)DAMAGE_HALF_MULT * damage_amount);
        }
        *(float*)(char_slot + 0xe54) = intensity;
        *(float*)(char_slot + 0xe58) = DAMAGE_SPLASH_LOW; // duration
    } else if (weapon_type == 0x17) {
        // Special weapon: scale by damage with custom multiplier
        *(float*)(char_slot + 0xe54) = (float)((double)DAMAGE_SCREEN_SPECIAL_MULT * damage_amount);
        *(float*)(char_slot + 0xe58) = DAMAGE_SCREEN_GENERIC_MAX; // longer duration
    } else {
        // Table lookup: weapon_type * 0x40 offset into screen effect table
        *(int*)(char_slot + 0xe54) = *(int*)(&SCREEN_EFFECT_TABLE + weapon_type * 0x40);
        *(int*)(char_slot + 0xe58) = *(int*)(&SCREEN_EFFECT_TABLE + weapon_type * 0x40 + 4);
    }
}

// =============================================================================
// Address: 0x8008e360 | Name: damage_play_pain_sound
// Plays a randomized pain/hit sound from the character's sound bank.
// Selects random entry from bank (offset +0x38 = count), plays at entity pos.
// Volume scaled by global sound level, with randomized pitch.
// =============================================================================
void damage_play_pain_sound(int char_type_id, int voice_type, float* position) {
    uint64_t event_pair = event_get_data_pair();
    // Get sound bank for character
    void* sound_bank = get_character_sound_bank((int)(event_pair >> 32), (int)event_pair);
    uint bank_count = *(uint*)(sound_bank + 0x38);

    if ((int)bank_count > 0) {
        // Random index into bank
        uint rng = random_uint();
        short sound_id = *(short*)(sound_bank + (rng % bank_count) * 2);

        // Pitch variation from frame counter
        int pitch = (int)(SOUND_PITCH_SCALE *
            (float)((double)(g_frame_count_bias ^ 0x80000000) - DOUBLE_BIAS));

        int play_params[10];
        sound_init_play_params((int)play_params);
        play_params[0] = (int)sound_id;
        play_params[1] = pitch;
        *(float*)(&play_params[4]) = (float)(double)g_sound_master_volume; // volume
        *(float*)(&play_params[3]) = SOUND_PAIN_FALLOFF; // falloff
        *(unsigned char*)((char*)play_params + 0x28) = 1; // 3D positional
        // Position
        *(int*)(&play_params[5]) = *(int*)&position[0];
        *(int*)(&play_params[6]) = *(int*)&position[1];
        *(int*)(&play_params[7]) = *(int*)&position[2];

        sound_play_request(play_params);
    }
    event_end_data_pair();
}

// =============================================================================
// Address: 0x8008e460 | Name: damage_play_death_sound
// Plays a randomized death sound from the character's sound bank.
// Same structure as pain sound but uses death bank (offset +0x3c in table)
// with different volume/pitch multipliers.
// =============================================================================
void damage_play_death_sound(int char_type_id, int voice_type, float* position) {
    uint64_t event_pair = event_get_entity_pair();
    // Get sound bank for character
    void* sound_bank = get_character_sound_bank((int)(event_pair >> 32), (int)event_pair);
    uint bank_count = *(uint*)(sound_bank + 0x3c);

    if ((int)bank_count > 0) {
        uint rng = random_uint();
        short sound_id = *(short*)(sound_bank + (rng % bank_count) * 2 + 0x24);

        int pitch = (int)(SOUND_PITCH_SCALE *
            (float)((double)(g_frame_count_bias ^ 0x80000000) - DOUBLE_BIAS));

        int play_params[10];
        sound_init_play_params((int)play_params);
        play_params[0] = (int)sound_id;
        play_params[1] = pitch;
        *(float*)(&play_params[4]) = (float)(double)(SOUND_DEATH_VOL_MULT * g_sound_master_volume);
        *(float*)(&play_params[3]) = SOUND_DEATH_FALLOFF;
        *(unsigned char*)((char*)play_params + 0x28) = 1; // 3D positional
        *(int*)(&play_params[5]) = *(int*)&position[0];
        *(int*)(&play_params[6]) = *(int*)&position[1];
        *(int*)(&play_params[7]) = *(int*)&position[2];

        sound_play_request(play_params);
    }
    event_end_pair();
}

// =============================================================================
// Address: 0x8008e56c | Name: damage_can_play_death_sound
// Guard function: returns 0 if death sound should be suppressed.
// Suppressed when: flag 0x40000 set, or level==9 with 'WBRG' magic entity.
// =============================================================================
int damage_can_play_death_sound(int char_slot) {
    // Flag 0x40000: silent death
    if ((*(uint*)(char_slot + 0xc6c) & 0x40000) != 0) {
        return 0;
    }
    // Level 9 + attached entity with 'WBRG' signature (cinematic/scripted death)
    if (g_level_type == 9 && *(int*)(char_slot + 0xbf0) != 0 &&
        *(int*)(*(int*)(char_slot + 0xbf0) + 8) == 0x42524757 /* 'WBRG' */) {
        return 0;
    }
    return 1;
}

// =============================================================================
// Address: 0x8008e5c0 | Name: damage_on_hit_event
// MASSIVE on-hit event handler. Called every frame a character is taking damage.
// Handles:
// - Ambient pain sounds (3 types based on weapon: 0=normal, 1=energy, 2=custom+0x15d)
// - Death detection: when health reaches 0, plays death sound + triggers death
// - Invulnerability timer: flag 0x4 suppresses further damage for a period
// - Hit reaction dispatch (flinch animation via FUN_800af5e8)
// =============================================================================
void damage_on_hit_event(void) {
    int* event_data = (int*)event_get_data_pair();
    int* char_slot = *(int**)(*event_data + 0x7c);

    // Skip if invulnerable (flag bit 0 in slot[4])
    if ((char_slot[4] & 1) != 0) {
        event_end_data_pair();
        return;
    }

    int weapon_type = event_data[6];

    // Pain sound dispatch by weapon category
    if (weapon_type == 1) {
        // Energy weapon: play with random pitch variation
        if (event_data[10] != 0) {
            uint rng = random_uint();
            // Pitch = base + random variation
            sound_play_ambient_3d((double)SOUND_PAIN_VOLUME, (double)SOUND_PAIN_FALLOFF_ALT);
        }
    } else if (weapon_type == 0) {
        // Standard weapon: check if character has custom sound
        if (event_data[10] != 0) {
            int has_custom = check_character_custom_sound((int)*(short*)((int)char_slot + 0x4a));
            if (has_custom == 0) {
                uint rng = random_uint();
                sound_play_ambient_3d((double)SOUND_PAIN_VOLUME, (double)SOUND_PAIN_FALLOFF_ALT);
            } else {
                sound_play_ambient_3d((double)SOUND_PAIN_VOLUME, (double)SOUND_PAIN_FALLOFF_ALT);
            }
        }
    } else if (weapon_type == 2) {
        // Custom weapon sound (sound ID = rng + 0x15d)
        if (event_data[10] != NULL) {
            uint rng = random_uint();
            int pitch = (int)(1.0f + (float)((double)(rng) - DOUBLE_BIAS) / SOUND_PITCH_RANGE);

            int play_params[10];
            sound_init_play_params((int)play_params);
            play_params[0] = pitch + 0x15d;
            *(unsigned char*)((char*)play_params + 0x28) = 1;
            *(float*)(&play_params[3]) = SOUND_PAIN_VOLUME;
            *(int*)(&play_params[5]) = *(int*)event_data[10];
            *(int*)(&play_params[6]) = *((int*)event_data[10] + 1);
            *(int*)(&play_params[7]) = *((int*)event_data[10] + 2);
            sound_play_request(play_params);
        }
    }

    // Invulnerability timer check (flag 0x4 in slot[0x31b])
    if ((char_slot[0x31b] & 4) != 0 && char_slot[0x23] == 0) {
        // Not yet triggered - check if enough time elapsed
        // (frame-based timer comparison)
        if (DAMAGE_INVULN_FACTOR * (float)((double)(g_frame_count_bias ^ 0x80000000) - DOUBLE_BIAS) <
            (float)((double)((g_frame_counter - char_slot[0x22]) ^ 0x80000000) - DOUBLE_BIAS)) {
            char_slot[0x23] = 1; // mark invulnerability active
            char_slot[0x22] = g_frame_counter;
            trigger_hit_reaction(char_slot, 2); // flinch anim
        }
    } else {
        // Normal hit processing
        if (*(float*)(*event_data + 0x1cc) <= 0.0f) {
            // Character died
            char_slot[0x22] = g_frame_counter;
            char_slot[0x23] = 0;
            DAT_80611c2c = -1; // clear last hit source

            int can_play = damage_can_play_death_sound((int)char_slot);
            if (can_play != 0) {
                float* pos = entity_get_world_position(*event_data);
                damage_play_death_sound(
                    (int)*(short*)(char_slot + 0x12),
                    (int)*(char*)((int)char_slot + 0xc0d),
                    pos);
            }
        } else if (char_slot[0x23] == 0) {
            // Alive - check pain sound timer
            if (1.0f * (float)((double)(g_frame_count_bias ^ 0x80000000) - DOUBLE_BIAS) <
                (float)((double)((g_frame_counter - char_slot[0x22]) ^ 0x80000000) - DOUBLE_BIAS)) {

                char_slot[0x22] = g_frame_counter;
                int is_special = check_special_pain_case();

                if (is_special == 0) {
                    if ((char_slot[0x31b] & 0x40000) == 0) {
                        float emitter_pos[3];
                        sound_get_emitter_position(char_slot, emitter_pos);
                        damage_play_pain_sound(
                            (int)*(short*)(char_slot + 0x12),
                            (int)*(char*)((int)char_slot + 0xc0d),
                            emitter_pos);
                    }
                } else {
                    // Special case: play specific sound 0xd61
                    float emitter_pos[3];
                    sound_get_emitter_position(char_slot, emitter_pos);
                    int play_params[10];
                    sound_init_play_params((int)play_params);
                    play_params[0] = 0xd61;
                    *(unsigned char*)((char*)play_params + 0x28) = 1;
                    *(float*)(&play_params[5]) = emitter_pos[0];
                    *(float*)(&play_params[6]) = emitter_pos[1];
                    *(float*)(&play_params[7]) = emitter_pos[2];
                    sound_play_request(play_params);
                }
            }
        }
    }

    event_end_data_pair();
}

// =============================================================================
// Address: 0x8008e9a4 | Name: damage_award_event
// Awards kill/assist animation events to attacker and victim players.
// Event types: 0x0e (kill), 0x1d (death), 0x3f (special kill bonus).
// Weapon type 0 skips if no-kill event. Checks weapon flag 0x80000 for special.
// =============================================================================
void damage_award_event(int param_1, int param_2, int victim_player) {
    uint64_t event_pair = event_get_entity_pair();
    int attacker_entity = (int)(event_pair >> 32);
    int weapon_entity = (int)event_pair;

    if (victim_player != 0 && *(int*)(attacker_entity + 0x18) != 0x17) {
        if (*(int*)(attacker_entity + 0x18) == 0) {
            // Self-damage: award death event to victim, kill event to attacker
            award_animation_event((double)AWARD_DURATION, 0x0e, *(int*)(victim_player + 8));
            award_animation_event((double)AWARD_DURATION, 0x1d, *(int*)(weapon_entity + 8));
        } else {
            // Normal kill: award kill to attacker, death to victim
            award_kill_to_player(3, *(int*)(weapon_entity + 8), *(int*)(victim_player + 8));
            award_kill_to_player(2, *(int*)(victim_player + 8), *(int*)(weapon_entity + 8));

            // Special kill bonus (weapon flag check)
            if (*(int*)(attacker_entity + 0x18) != -1) {
                uint wp_flags = get_weapon_class(*(int*)(attacker_entity + 0x18));
                if ((wp_flags & 0x80000) == 0 && (*(uint*)(attacker_entity + 0x3c) & 0x100) != 0) {
                    award_animation_event((double)AWARD_DURATION, 0x3f, *(int*)(victim_player + 8));
                }
            }
        }
    }

    event_end_pair();
}

// =============================================================================
// Address: 0x8008ea74 | Name: character_on_death_init
// Death initialization wrapper. Clears character slot field +0x90,
// builds a local event struct (reusing switch_slot_reset for the clear),
// then calls character_on_death_handler for full death processing.
// =============================================================================
void character_on_death_init(/* variadic params */, int entity) {
    *(int*)(*(int*)(entity + 0x7c) + 0x90) = 0;

    int local_event[18];
    switch_slot_reset((int)local_event); // reuses the 0x5c clear utility
    local_event[0] = entity;
    local_event[1] = 0;
    // local_event[17] = 0 (local_20)

    character_on_death_handler(/* forward params */);
}

// =============================================================================
// Address: 0x8008eac8 | Name: character_on_death_handler
// MASSIVE death handler. Full death processing pipeline:
// 1. Check if already dead (flag 0x8 in slot[9]) - skip if vehicle occupant
// 2. Zero health, check for "AWAY" entity (checkpoint respawn)
// 3. Score registration: killer gets credit, victim gets death
// 4. Kill messages: "%s killed %s" using string IDs 0x15ea/0x15eb
// 5. Broadcast kill message to all players via FUN_80105d5c
// 6. Award events: distance-based (close kill 0x5b, far kill 0x5c/0x5d)
// 7. Team kill detection (same team = 0x59 award)
// 8. Vehicle occupant kill (0x57, 0x58 awards)
// 9. Killed-by-dead (0x5e), killed-self (0x60) awards
// 10. Match scoring (FUN_80108c90)
// 11. Match end check (match_process_end)
// 12. Story mode trigger (FUN_80143e10)
// 13. Respawn dispatch (player vs AI)
// 14. Increment death counter (+600)
// =============================================================================
void character_on_death_handler(/* variadic */) {
    int* event_data = (int*)event_get_data_extended();
    int* char_slot = *(int**)(*event_data + 0x7c);

    // Already dead check (flag 0x8)
    if ((char_slot[9] & 8) != 0) {
        // Vehicle occupant special case
        int player_ctrl = *(int*)(*event_data + 0x150);
        if (player_ctrl != 0 && *(int*)(player_ctrl + 0x110) == 4 &&
            (*(uint*)(player_ctrl + 0xc) & 0x800) == 0) {
            dispatch_death_respawn(/* params */);
        }
        event_end_data_extended();
        return;
    }

    // --- Normal death processing ---

    // Zero health and clear state
    char_slot[0x1e] = (int)0.0f;
    *(float*)(*event_data + 0x1cc) = 0.0f;

    // Check for scripted death callback
    uint has_callback = check_death_callback((int)char_slot);
    if (has_callback == 0) {
        // Check for "4WAY" checkpoint marker
        if (*(int*)(char_slot[0x2fc] + 0x28) == 0x34574159 /* '4WAY' */) {
            handle_checkpoint_death(/* params */);
        }
    } else {
        // Normal multiplayer death with kill message
        if (event_data[1] == 0 || *(int*)(event_data[1] + 0x150) == 0) {
            char* msg = get_string_by_id(0x15eb); // "%s died"
            format_kill_message(msg, char_slot[0x1c]);
        } else {
            char* msg = get_string_by_id(0x15ea); // "%s killed %s"
            format_kill_message(msg, char_slot[0x1c]);
        }

        // Broadcast to all players
        int player_count = get_active_player_count();
        for (int i = 0; i < player_count; i++) {
            send_kill_feed_message(i, 4, /* message buffer */);
        }

        // Set match flag for kill occurred
        g_match_flags |= 0x10000;
        match_process_end(/* params, 0 */);
    }

    // Story mode death trigger
    if (g_difficulty != 0x0a) {
        trigger_story_death_event();
    }

    // Score registration
    int attacker_entity = event_data[1];
    if (attacker_entity == 0 || *(int*)(attacker_entity + 0x20) != 8 ||
        attacker_entity == *event_data) {
        // Self-kill or environmental death
        if (g_game_mode == 0x0c) {
            handle_mode_c_death();
        }
        if (event_data[1] == 0 || *(int*)(event_data[1] + 0x20) == 8) {
            score_register_death(char_slot, char_slot[2], 1);
        } else {
            score_register_death(NULL, char_slot[2], 1);
        }
        // Story mode bot scoring
        if (g_difficulty == 0x09 && g_game_mode != 0x0a) {
            if (event_data[1] != 0) {
                int player_ctrl = *(int*)(event_data[1] + 0x150);
                if (player_ctrl != 0) {
                    score_bonus(*(int*)(player_ctrl + 4), g_frame_count_bias * 2 + 0x40);
                }
            }
        }
    } else {
        // Killed by another player/bot
        int* killer_slot = *(int**)(attacker_entity + 0x7c);
        score_register_death(killer_slot, char_slot[2], 1);

        // Award events to killer
        if (*(int**)(event_data[1] + 0x150) != NULL) {
            // Standard kill award
            if (event_data[6] == 0) {
                award_animation_event((double)AWARD_DURATION, 0x5f, **(int**)(event_data[1] + 0x150));
            }
            // Story mode scoring
            if (g_difficulty == 0x09 && g_game_mode != 0x0a) {
                score_bonus(*(int*)(*(int*)(event_data[1] + 0x150) + 4), g_frame_count_bias * 2 + 0x40);
            }
            // Special kill logic (story/coop)
            if ((g_game_flags & 0x40) == 0 || g_match_subtype != 0x13) {
                award_kill_stats(*(int*)(event_data[1] + 0x150), (int)char_slot);
            } else {
                award_coop_kill(event_data[3], (int)char_slot);
            }
            // Stealth kill
            if ((*(int**)(event_data[1] + 0x150))[3] & 0x20) {
                award_animation_event((double)AWARD_DURATION, 0x48, **(int**)(event_data[1] + 0x150));
            }

            // Distance-based awards
            int attacker_ent = event_data[1];
            if (0.0f < *(float*)(attacker_ent + 0x1cc)) {
                int victim_ent = *event_data;
                float dy = *(float*)(victim_ent + 0x68) - *(float*)(attacker_ent + 0x68);
                float dx = *(float*)(victim_ent + 0x64) - *(float*)(attacker_ent + 0x64);
                float dz = *(float*)(victim_ent + 0x6c) - *(float*)(attacker_ent + 0x6c);
                float dist_sq = dx*dx + dy*dy + dz*dz;

                if (dist_sq < KILL_CLOSE_DIST_SQ) {
                    award_animation_event((double)AWARD_DURATION, 0x5b, **(int**)(attacker_ent + 0x150));
                }
                if (KILL_FAR_DIST_SQ < dist_sq) {
                    int* killer_player = *(int**)(event_data[1] + 0x150);
                    if ((killer_player[3] & 0x20) == 0) {
                        award_animation_event((double)AWARD_DURATION, 0x5c, *killer_player);
                    } else {
                        award_animation_event((double)AWARD_DURATION, 0x5d, *killer_player);
                    }
                }
            } else {
                // Killed by dead player
                award_animation_event((double)AWARD_DURATION, 0x5e, **(int**)(attacker_ent + 0x150));
            }
        }

        // Match scoring
        handle_match_scoring(killer_slot, /* params */);

        // Self-kill while having a killer (rare: died to own explosion)
        if (event_data[6] == 0 && *(int**)(*event_data + 0x150) != NULL) {
            award_animation_event((double)AWARD_DURATION, 0x60, **(int**)(*event_data + 0x150));
        }

        // Vehicle/explosive kill awards
        if (event_data[1] != 0 && event_data[2] != 0 &&
            *(int*)(event_data[2] + 0x20) == 0x400000 &&
            *(int*)(event_data[1] + 0x20) == 8) {
            if (*(int**)(event_data[1] + 0x150) != NULL) {
                award_animation_event((double)AWARD_DURATION, 0x57, **(int**)(event_data[1] + 0x150));
                // Team kill check
                if ((g_game_flags & 1) != 0 && killer_slot[0x21] == char_slot[0x21]) {
                    award_animation_event((double)AWARD_DURATION, 0x59, **(int**)(event_data[1] + 0x150));
                }
            }
            if (*(int**)(*event_data + 0x150) != NULL) {
                award_animation_event((double)AWARD_DURATION, 0x58, **(int**)(*event_data + 0x150));
            }
        }

        // Virus mode score tracking
        if (g_game_mode == 0x09) {
            int killer_cs = *(int*)(event_data[1] + 0x7c);
            *(float*)(killer_cs + 0xa88) += 1.0f;
            if (*(float*)(killer_cs + 0xa88) < 0.0f) {
                *(float*)(killer_cs + 0xa88) = 0.0f;
            }
        }
    }

    // Respawn dispatch
    if (*(int*)(*event_data + 0x150) == 0) {
        dispatch_ai_death(/* params */);
    } else {
        dispatch_player_death(/* params */);
    }

    // Mode-specific triggers
    if (g_game_mode == 0x0a) {
        trigger_elimination_check();
    }

    // Increment death counter
    *(int*)(*event_data + 0x258) += 1;

    event_end_data_extended();
}
