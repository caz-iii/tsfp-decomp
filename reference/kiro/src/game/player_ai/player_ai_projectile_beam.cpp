// Address: 0x800c3858-0x800c4bc8 | Name: player_ai_projectile_beam | Date: 2026-08-11
// Player AI projectile beam/continuous weapon system
// Handles: bullet tick, instant-hit raycast, beam weapon tick, beam rendering,
//          continuous beam (flamethrower/electro), beam particle trails.

#include "common.h"
#include "player_ai.h"
#include "damage.h"
#include "collision.h"
#include "weather.h"
#include "particle.h"
#include "sprite.h"

// Forward declarations
extern int player_ai_fire_raycast(int* slot, float* velocity, float* hit_pos, float* hit_normal, int* hit_info);
extern int player_ai_fire_raycast_simple(int* slot, float* velocity, float* end_pos, float* hit_pos, int* hit_info);
extern void player_ai_on_damage_received(int* slot, int hit_info);
extern void player_ai_slot_deactivate(void);
extern void player_ai_voice_pitch_update(int* slot);
extern void player_ai_projectile_play_impact_sound(void);
extern void player_ai_projectile_proximity_detonate(void);
extern void player_ai_impact_dispatch(int* slot, int type, int hit_entity, float* pos, float* normal, float* normal2, int weapon);
extern int player_ai_projectile_has_character_target(int slot_ptr);
extern void damage_resolve_and_dispatch(double param, ...);
extern void switch_slot_reset(int slot);
extern double switch_slot_load_from_def(int slot, int* src);
extern int collision_hit_is_valid_surface(void);
extern void collision_result_restore(int buffer);
extern void weather_beam_spawn(int a, int b, int* start, int* end, uint color1, uint color2, int segments, int mask);
extern int sprite_get_texture_id(int id);
extern void particle_spawn_flag(int addr, int b, int* pos, int mask);
extern int game_is_standard_arcade(void);

// ============================================================================
// Address: 0x800c3c70
// player_ai_beam_compute_segment_count
// Computes the number of beam segments based on distance factor.
// Range is [base..5] where base = floor(5.0f * distance_factor + 0.5f).
// Adds random jitter from remaining range.
// ============================================================================
int player_ai_beam_compute_segment_count(double distance_factor) {
    int base_count = (int)(5.0f * (float)distance_factor + 0.5f);
    int remaining = 5 - base_count;
    int jitter = 0;
    if (remaining != 0) {
        uint rng = random_uint();
        jitter = rng - (rng / remaining) * remaining;
    }
    return base_count + jitter;
}

// ============================================================================
// Address: 0x800c3cd8
// player_ai_projectile_is_homing_type
// Returns 1 if the projectile is type 0x2f (homing missile), sub-mode 1,
// has a valid owner, and the owner's player class is 0x11 (specific arcade mode).
// ============================================================================
int player_ai_projectile_is_homing_type(int* slot) {
    if (*slot != 0x2f) return 0;
    if (slot[0x41] == 0) return 0;
    if (slot[0x40] != 1) return 0;

    int is_arcade = game_is_standard_arcade();
    if (is_arcade != 0) return 0;

    int player_offset = g_player_data_base + *(int*)(slot[0x41] + 8) * 0x141c;
    if (*(int*)(player_offset + 0x1390) != 0x11) return 0;

    return 1;
}

// ============================================================================
// Address: 0x800c402c
// player_ai_projectile_store_impact_direction
// Normalizes the velocity vector (offsets 0x64-0x6c) and stores the unit
// direction at offset 0x180. Sets flag at 0x14c = 1 to indicate valid impact.
// ============================================================================
void player_ai_projectile_store_impact_direction(int slot_ptr) {
    float vx = *(float*)(slot_ptr + 0x64);
    float vy = *(float*)(slot_ptr + 0x68);
    float vz = *(float*)(slot_ptr + 0x6c);

    float length = sqrtf(vx * vx + vy * vy + vz * vz);
    float inv_len = 1.0f / length;

    *(float*)(slot_ptr + 0x180) = vx * inv_len;
    *(float*)(slot_ptr + 0x184) = vy * inv_len;
    *(float*)(slot_ptr + 0x188) = vz * inv_len;
    *(int*)(slot_ptr + 0x14c) = 1;
}

// ============================================================================
// Address: 0x800c4208
// player_ai_projectile_is_local_player
// Returns 1 if the projectile's owner is the local player AND the local player
// has first-person view enabled (flag bit 4 in game_state+0x21c).
// Used to choose first-person vs third-person beam origin point.
// ============================================================================
int player_ai_projectile_is_local_player(int slot_ptr) {
    int owner_entity = *(int*)(slot_ptr + 0x108);
    int player_idx = -1;

    if (owner_entity != 0) {
        int player_data = *(int*)(owner_entity + 0x150);
        if (player_data != 0) {
            int player_state = *(int*)(player_data + 8);
            if (player_state != 0) {
                player_idx = *(int*)(player_state + 0x214);
            }
        }
    }

    if (*(int*)(g_game_state + 0x214) != player_idx) return 0;
    if ((*(uint*)(g_game_state + 0x21c) & 4) == 0) return 0;

    return 1;
}

// ============================================================================
// Address: 0x800c3858
// player_ai_projectile_bullet_tick
// Per-frame tick for standard bullet/projectile types. Computes velocity from
// direction * speed * dt, performs raycast, handles collision (entity hit,
// surface hit, pass-through), dispatches damage, and deactivates on impact
// or max-distance expiry.
// ============================================================================
void player_ai_projectile_bullet_tick(int* slot) {
    float dt_scale = 1.0f / (float)g_frame_rate;
    float speed = (float)slot[0x22] * g_dt_multiplier;
    float vel_scale = (1.0f / dt_scale) * speed;

    // Compute frame velocity from direction
    float vel_x = (float)slot[0x19] * vel_scale;
    float vel_y = (float)slot[0x1a] * vel_scale;
    float vel_z = (float)slot[0x1b] * vel_scale;

    if (g_projectile_enabled == 0) return;

    // Check max distance or lifetime expiry
    float dist_traveled = (float)slot[0x26];
    if (dist_traveled >= g_projectile_max_distance ||
        (dist_traveled >= g_projectile_homing_distance &&
         slot[0x41] != 0 &&
         g_difficulty_level <= *(int*)(slot[0x41] + 8))) {
        player_ai_slot_deactivate();
        return;
    }

    // Accumulate distance squared
    slot[0x3d] = (int)(vel_scale * vel_scale + (float)slot[0x3d]);

    // Apply wind/platform velocity if on moving platform (type 8, state active)
    int platform = slot[0x43];
    if (platform != 0 && *(int*)(platform + 0x20) == 8) {
        vel_x += *(float*)(platform + 0xf8);
        vel_y += *(float*)(platform + 0xfc);
        vel_z += *(float*)(platform + 0x100);
    }

    // Type-specific pre-raycast behavior
    if (*slot == 0x15) {
        int check = FUN_8024786c();
        if (check != 0) {
            FUN_80246f44();
        }
        if (g_difficulty_mode == 1) {
            FUN_8036f4b8();
        }
    }

    // Perform raycast
    int hit_info[3];
    hit_info[0] = 0;
    hit_info[1] = 0;
    float hit_pos[3];
    float hit_normal[3];
    int hit_entity = player_ai_fire_raycast(slot, &vel_x, &hit_pos[0], &hit_normal[0], hit_info);

    if (hit_info[0] > 0) {
        hit_entity = 0; // Damage self - treat as no hit entity
    }

    // No collision - advance position
    if (hit_entity == 0 && hit_info[0] < 1) {
        slot[7] = (int)((float)slot[7] + vel_x);
        slot[8] = (int)((float)slot[8] + vel_y);
        slot[9] = (int)((float)slot[9] + vel_z);
        if (*slot == 0x15) {
            FUN_80161260();
        }
        return;
    }

    // Handle collision
    if (hit_entity == 0) {
        // Self-damage from hit_info
        hit_pos[0] = hit_normal[0]; // Copy normal data for damage
        player_ai_on_damage_received(slot, (int)hit_info);
    } else if (*(int*)(hit_entity + 0x20) == 8 && *(short*)(hit_entity + 0x94) != 0x5ea) {
        // Hit a character entity (not invulnerable type 0x5ea)
        int is_friendly = 0;
        int* char_data = *(int**)(hit_entity + 0x7c);

        if (*slot == 0x15) {
            if (slot[0x41] != 0 && char_data[0x21] == *(int*)(slot[0x41] + 0x84)) {
                is_friendly = 1;
            }
            if ((g_game_flags & 1) == 0 || (g_game_flags & 8) != 0 || !is_friendly) {
                FUN_800f3f50();
            }
        } else if (*slot == 0x16) {
            FUN_800f3de0(char_data + 0x31a);
            char_data[0x23] = 0;
            if (g_difficulty_level <= (int)char_data[2]) {
                int switch_data[23];
                switch_slot_reset((int)switch_data);
                switch_slot_load_from_def((int)switch_data, slot);
                FUN_8013b80c(*char_data, 0, switch_data);
            }
        }
    } else {
        // Hit world geometry or non-character entity
        if (*(int*)(hit_entity + 0x20) != 8) {
            int valid = collision_hit_is_valid_surface();
            if (valid != 0) {
                FUN_800e95fc(*slot, 0, slot[0x3c], hit_entity, &hit_pos[0], &hit_normal[0]);
            }
        }

        // Compute normalized velocity for damage direction
        float vel_mag_sq = (float)slot[0x19] * (float)slot[0x19] +
                           (float)slot[0x1a] * (float)slot[0x1a] +
                           (float)slot[0x1b] * (float)slot[0x1b];
        float vel_mag = sqrtf(vel_mag_sq);
        float inv_mag = 1.0f / vel_mag;
        float dir_x = (float)slot[0x19] * inv_mag;
        float dir_y = (float)slot[0x1a] * inv_mag;
        float dir_z = (float)slot[0x1b] * inv_mag;

        // Build damage packet and dispatch
        int damage_data[10];
        switch_slot_reset((int)damage_data);
        switch_slot_load_from_def((int)damage_data, slot);
        damage_data[0] = hit_entity;
        // ... additional damage fields set via stack layout ...
        damage_resolve_and_dispatch(/* params */);
    }

    if (*slot == 0x15) {
        player_ai_projectile_proximity_detonate();
    }
    player_ai_slot_deactivate();
}

// ============================================================================
// Address: 0x800c3d5c
// player_ai_projectile_instant_hit_tick
// Tick for instant-hit (hitscan) projectiles. Performs a single raycast along
// the stored direction, handles homing redirect for arcade mode, dispatches
// damage on hit, and deactivates the slot immediately after.
// ============================================================================
void player_ai_projectile_instant_hit_tick(void) {
    int* slot = (int*)projectile_slot_get_current();

    // Compute velocity from stored direction * scale factor
    float vel_x = BEAM_DIRECTION_SCALE * (float)slot[0x60];
    float vel_y = BEAM_DIRECTION_SCALE * (float)slot[0x61];
    float vel_z = BEAM_DIRECTION_SCALE * (float)slot[0x62];

    // Raycast
    int hit_info[2];
    hit_info[0] = 0;
    hit_info[1] = 0;
    float hit_pos[3];
    int hit_entity = player_ai_fire_raycast_simple(slot, &vel_x, (float*)(slot + 0x68), &hit_pos[0], hit_info);

    if (hit_info[0] > 0) {
        player_ai_on_damage_received(slot, (int)hit_info);
    }

    // Homing redirect: if arcade homing type, redirect to stored target
    if (*slot == 0x2f && hit_entity != 0 && slot[0x41] != 0 && slot[0x40] == 1) {
        int player_offset = g_player_data_base + *(int*)(slot[0x41] + 8) * 0x141c;
        int homing_target = *(int*)(player_offset + 0xe1c);
        int is_homing = player_ai_projectile_is_homing_type(slot);

        if (is_homing != 0 && homing_target != 0 &&
            hit_entity != homing_target && *(int*)(homing_target + 0x20) == 8) {
            // Redirect end position to stored homing target
            slot[0x68] = *(int*)(player_offset + 0xe20);
            slot[0x69] = *(int*)(player_offset + 0xe24);
            slot[0x6a] = *(int*)(player_offset + 0xe28);
            hit_pos[0] = *(float*)(player_offset + 0xe2c);
            hit_pos[1] = *(float*)(player_offset + 0xe30);
            hit_pos[2] = *(float*)(player_offset + 0xe34);
            hit_entity = homing_target;
        }
    }

    if (hit_entity == 0) {
        // No hit - set end position from start + velocity
        slot[0x68] = (int)((float)slot[7] + vel_x);
        slot[0x69] = (int)((float)slot[8] + vel_y);
        slot[0x6a] = (int)((float)slot[9] + vel_z);
    } else {
        // Hit entity
        if (*(int*)(hit_entity + 0x20) == 8) {
            player_ai_voice_pitch_update(slot);
            player_ai_projectile_play_impact_sound();
        }

        int is_homing = player_ai_projectile_is_homing_type(slot);
        if (is_homing == 0 || *(int*)(hit_entity + 0x20) == 8) {
            player_ai_impact_dispatch(slot, *slot, hit_entity,
                (float*)(slot + 0x68), &hit_pos[0], &hit_pos[0], slot[0x42]);
        }

        // Homing missile: notify AI tracking system
        if (*slot == 0x2f && slot[0x40] == 1 && (int*)slot[0x41] != NULL) {
            int owner = *(int*)slot[0x41];
            int has_tracker = 0;
            if (owner != 0 && *(int*)(owner + 0x150) != 0) {
                has_tracker = 1;
            }
            int tracker_idx = has_tracker ? *(int*)(*(int*)(owner + 0x150) + 4) : -1;

            if (tracker_idx >= 0) {
                int owner2 = *(int*)slot[0x41];
                int has_t2 = (owner2 != 0 && *(int*)(owner2 + 0x150) != 0) ? 1 : 0;
                int t2_idx = has_t2 ? *(int*)(*(int*)(owner2 + 0x150) + 4) : -1;
                FUN_800d3498(t2_idx, slot + 0x68);
            }
        }

        // Build and dispatch damage
        int damage_data[10];
        switch_slot_reset((int)damage_data);
        double dmg_scale = (double)switch_slot_load_from_def((int)damage_data, slot);
        damage_data[0] = hit_entity;
        // Stack layout: hit_pos_ptr, normal_ptr, dir_ptr, distance=1.0f
        damage_resolve_and_dispatch(/* dmg_scale, ... damage_data */);
        player_ai_projectile_proximity_detonate();
    }

    projectile_slot_finish_current();
}

// ============================================================================
// Address: 0x800c40ac
// player_ai_projectile_beam_tick
// Per-frame tick for beam weapons (e.g. sci-fi laser). Fires instant-hit
// raycast, computes squared travel distance, determines team color for beam,
// then spawns beam weather effect (visual line between start and end points).
// If target is a character, uses player-masked beam; otherwise full-mask beam.
// ============================================================================
void player_ai_projectile_beam_tick(void) {
    int* slot = (int*)projectile_slot_get_current();

    if (slot[0x53] == 0) {
        // No ammo / cancelled
        player_ai_slot_deactivate();
        return;
    }

    // Determine beam color by projectile type
    uint base_color = 0x143c7f; // Blue-ish (enemy)
    if (*slot == 0x2f) {
        base_color = 0x7f3c14; // Orange (player/friendly)
    }
    uint beam_color = base_color | 0x9f000000; // With alpha

    // Fire instant-hit raycast (handles damage internally)
    player_ai_projectile_instant_hit_tick();

    // Compute squared distance traveled this frame
    float dx = (float)slot[0x68] - (float)slot[7];
    float dy = (float)slot[0x69] - (float)slot[8];
    float dz = (float)slot[0x6a] - (float)slot[9];
    slot[0x3d] = (int)(dx * dx + dy * dy + dz * dz);

    // Spawn visual beam effect
    int has_char_target = player_ai_projectile_has_character_target((int)slot);
    int segments = player_ai_beam_compute_segment_count((double)(float)slot[0x7f]);

    if (has_char_target == 0) {
        // No character target - full screen mask beam
        weather_beam_spawn(0, 0, slot + 10, slot + 0x68,
            beam_color, base_color | 0x4f000000, segments, 0xffff);
    } else {
        // Character target - player-specific mask for first beam, inverse for second
        int player_bit = 1 << *(int*)(g_game_state + 0x214);
        weather_beam_spawn(0, 0, slot + 10, slot + 0x68,
            beam_color, base_color | 0x1a000000, segments, player_bit);
        weather_beam_spawn(0, 0, slot + 0xd, slot + 0x68,
            beam_color, base_color | 0x4f000000, 2, ~player_bit & 0xffff);
    }

    projectile_slot_finish_current();
}

// ============================================================================
// Address: 0x800c425c
// player_ai_projectile_beam_render
// Renders the beam weapon as a billboard quad sprite (texture 0x346).
// Computes screen-space size from depth, applies color fade based on
// projectile lifetime (opacity lerp between start and hit point).
// Draws 2 quads: one bright (near muzzle) and one faded (near impact).
// ============================================================================
void player_ai_projectile_beam_render(void) {
    int slot_ptr = projectile_render_slot_get();

    float fade_factor = 1.0f - *(float*)(slot_ptr + 0x1fc);
    int tex_id = sprite_get_texture_id(0x346);

    float lifetime = *(float*)(slot_ptr + 0x1fc);
    float size_fade = BEAM_SIZE_SCALE * fade_factor;
    float size_life = BEAM_SIZE_SCALE * lifetime;
    float glow_scale = (float)((double)*(float*)(slot_ptr + 0x200)) * fade_factor;

    // Get texture dimensions
    int tex_entry = g_texture_table + tex_id * 0x58;
    u16 tex_w = *(u16*)(tex_entry + 0x4e);
    u16 tex_h = *(u16*)(tex_entry + 0x52);

    // Compute color channels from fade/lifetime blend
    int color_combined = compute_beam_color_blend(size_fade + size_life);
    uint color_life = compute_beam_color_blend(BEAM_COLOR_BLEND_A * lifetime + size_fade);
    int color_fade = compute_beam_color_blend(BEAM_COLOR_BLEND_A * fade_factor + size_life);
    int alpha_glow = compute_beam_color_blend(BEAM_GLOW_SCALE * glow_scale);
    int alpha_core = compute_beam_color_blend(BEAM_COLOR_BLEND_A * glow_scale);

    // Choose origin point based on first-person vs third-person
    int is_local = player_ai_projectile_is_local_player(slot_ptr);
    float* origin;
    if (is_local == 0) {
        origin = (float*)(slot_ptr + 0x34); // Third-person muzzle
    } else {
        origin = (float*)(slot_ptr + 0x28); // First-person muzzle
    }

    // Draw 2 quads (near muzzle + near impact)
    for (int i = 0; i < 2; i++) {
        float size_mult = BEAM_QUAD_SIZE_FAR;
        if (i == 0) {
            size_mult = BEAM_QUAD_SIZE_NEAR;
        }

        // Transform origin to view space
        float view_pos[3];
        float depth;
        camera_transform_point(g_active_camera, origin, view_pos);
        depth = view_pos[2]; // Depth (W)

        // Frustum test
        if (depth <= 0.0f) continue;
        if (view_pos[0] < -depth || view_pos[0] > depth) continue;
        if (view_pos[1] < -depth || view_pos[1] > depth) continue;

        // Color selection
        uint quad_color;
        if (i == 0) {
            quad_color = (uint)alpha_glow << 24 | (uint)color_fade << 16 | color_life | (uint)color_combined << 8;
        } else {
            quad_color = (uint)alpha_core << 24 | 0xffffff;
        }

        // Compute projected size
        float projected_size = size_mult * *(float*)(g_game_state + 0x268) / depth;
        if (projected_size > BEAM_MAX_PROJECTED_SIZE) {
            projected_size = BEAM_MAX_PROJECTED_SIZE;
        }

        // Project to screen coordinates
        float inv_depth = 1.0f / depth;
        float screen_x = view_pos[0] * inv_depth;
        float screen_y = view_pos[1] * inv_depth;
        float screen_w = 1.0f + view_pos[2] * inv_depth;

        // Compute rotated quad corners and submit
        setup_beam_texture(tex_id);
        float half_w = projected_size * g_sin_table[/* angle */];
        float half_h = projected_size * g_cos_table[/* angle */];

        float corners[4][3];
        // ... corner computation from rotation ...
        submit_beam_quad(tex_w, tex_h, quad_color, corners);
    }

    projectile_render_slot_finish();
}

// ============================================================================
// Address: 0x800c45fc
// player_ai_projectile_continuous_tick
// Per-frame tick for continuous beam weapons (flamethrower, electrotool).
// Applies sinusoidal oscillation to beam direction, performs raycast each frame,
// handles per-frame damage dispatch with random intervals, manages ricochet
// (bounce damage to attached entity), and updates beam intensity/fade timers.
// ============================================================================
void player_ai_projectile_continuous_tick(int* slot) {
    int hit_bounce = 0;

    if (g_projectile_enabled <= 0) return;

    slot[0x28] = 0;

    // Compute oscillation parameters from lifetime
    float lifetime = (float)slot[0x26];
    float osc_freq_a = CONTINUOUS_OSC_A * lifetime;
    float osc_freq_b = CONTINUOUS_OSC_B * lifetime;
    float osc_freq_c = CONTINUOUS_OSC_C * lifetime;

    // Convert frequencies to sine table indices
    uint idx_a = (uint)(SINE_SCALE * osc_freq_a + 0.5f);
    uint idx_b = (uint)(SINE_SCALE * osc_freq_b + 0.5f);
    int idx_c = (int)(SINE_SCALE * osc_freq_c + 0.5f);

    // Compute oscillation amplitude from multiple sine harmonics
    slot[0x80] = (int)(
        CONTINUOUS_AMP_C * (0.5f + g_sin_table[(idx_c + 0x200) & 0x7ff]) +
        (CONTINUOUS_AMP_B * (0.5f + g_sin_table[idx_b & 0x7ff]) +
         (CONTINUOUS_AMP_A * (0.5f + g_sin_table[idx_a & 0x7ff]) + 0.5f))
    );

    // Clamp amplitude [0..1]
    if ((float)slot[0x80] < 0.0f) slot[0x80] = (int)0.0f;
    if (1.0f < (float)slot[0x80]) slot[0x80] = (int)1.0f;

    // Randomize yaw offset
    uint rng = random_uint();
    slot[0x77] = (int)((float)(rng - 0x80000000) / CONTINUOUS_YAW_DIVISOR);

    // Randomize sub-frame phase
    uint rng2 = random_uint();
    slot[0x7c] = rng2 % 0x7f;

    // Compute initial beam velocity from direction * short range factor
    float short_range = CONTINUOUS_SHORT_RANGE;
    float vel_x = short_range * (float)slot[0x19];
    float vel_y = short_range * (float)slot[0x1a];
    float vel_z = short_range * (float)slot[0x1b];

    // Set tentative end position
    slot[0x68] = (int)((float)slot[7] + vel_x);
    slot[0x69] = (int)((float)slot[8] + vel_y);
    slot[0x6a] = (int)((float)slot[9] + vel_z);

    // Raycast
    int hit_info[2];
    hit_info[0] = 0;
    hit_info[1] = 0;
    float hit_pos[3];
    int hit_entity = player_ai_fire_raycast_simple(slot, &vel_x, (float*)(slot + 0x68), &hit_pos[0], hit_info);
    slot[0x28] = hit_entity;

    if (hit_info[0] > 0) {
        player_ai_on_damage_received(slot, (int)hit_info);
    }

    // Compute squared distance for beam length tracking
    float dx = (float)slot[0x68] - (float)slot[7];
    float dy = (float)slot[0x69] - (float)slot[8];
    float dz = (float)slot[0x6a] - (float)slot[9];
    slot[0x3d] = (int)(dx * dx + dy * dy + dz * dz);

    if (slot[0x28] == 0) {
        // No hit - position already set above
        slot[0x68] = (int)((float)slot[7] + vel_x);
        slot[0x69] = (int)((float)slot[8] + vel_y);
        slot[0x6a] = (int)((float)slot[9] + vel_z);
    } else {
        // Hit something
        collision_result_restore((int)&hit_info);

        // Special handling for weapon entity (type 0x200) or character with shield (state 0xd)
        if (*(int*)(slot[0x28] + 0x20) != 2 && slot[0x42] != 0) {
            int weapon_ent = slot[0x42];
            int weapon_type = *(int*)(weapon_ent + 0x20);
            if (weapon_type == 0x200) {
                *(float*)(weapon_ent + 0xe4) = 0.0f; // Reset heat
            } else if (weapon_type == 8) {
                int player_data = *(int*)(weapon_ent + 0x150);
                if (player_data != 0 && *(int*)(player_data + 0x514) == 0xd) {
                    *(float*)(player_data + 0x5c4) = 0.0f; // Reset shield charge
                }
            }
        }

        // Character hit - play sound
        if (*(int*)(slot[0x28] + 0x20) == 8) {
            FUN_800f5830();
            player_ai_voice_pitch_update(slot);
        }

        // Periodic impact effect dispatch (every 4th or 20th frame based on weapon)
        uint rng3 = random_uint();
        int do_impact = 0;
        if (slot[0x42] != 0 && (rng3 & 3) == 0) do_impact = 1;
        if (slot[0x42] == 0) {
            uint rng4 = random_uint();
            if (rng4 == (rng4 / 20) * 20) do_impact = 1;
        }
        if (do_impact) {
            player_ai_impact_dispatch(slot, *slot, slot[0x28],
                (float*)(slot + 0x68), &hit_pos[0], &hit_pos[0], slot[0x42]);
        }

        // Dispatch primary damage
        int damage_data[10];
        switch_slot_reset((int)damage_data);
        double dmg = (double)switch_slot_load_from_def((int)damage_data, slot);
        damage_data[0] = slot[0x28];
        damage_resolve_and_dispatch(/* dmg, ... damage_data */);

        // Ricochet/bounce damage to attached entity (e.g. shield reflecting)
        if (slot[0x43] != 0 && /* hit type == 2 */ 1 &&
            (*(uint*)(*(int*)(slot[0x28] + 0x24) + 0x28) & 0x8000) != 0) {
            float bounce_vel[3];
            bounce_vel[0] = CONTINUOUS_BOUNCE_SCALE * (float)slot[0x19];
            bounce_vel[1] = CONTINUOUS_BOUNCE_SCALE * (float)slot[0x1a];
            bounce_vel[2] = CONTINUOUS_BOUNCE_SCALE * (float)slot[0x1b];
            float bounce_normal[3] = { -hit_pos[0], -hit_pos[1], -hit_pos[2] };

            int bounce_data[10];
            switch_slot_reset((int)bounce_data);
            switch_slot_load_from_def((int)bounce_data, slot);
            bounce_data[0] = slot[0x43];
            damage_resolve_and_dispatch(/* ... bounce_data */);
            hit_bounce = 1;
        }
    }

    // Update beam spin angle (visual rotation per frame)
    uint rng5 = random_uint();
    float dt_inv = 1.0f / (float)g_frame_rate;
    float spin_speed = -(g_dt_multiplier * dt_inv *
        (CONTINUOUS_SPIN_BASE * ((float)(rng5 - 0x80000000) / CONTINUOUS_SPIN_DIVISOR) +
         CONTINUOUS_SPIN_OFFSET) - (float)slot[0x79]);
    slot[0x79] = (int)spin_speed;
    while ((float)slot[0x79] < 0.0f) {
        slot[0x79] = (int)((float)slot[0x79] + 1.0f);
    }

    // Update per-segment fade timers (2 segments)
    for (int i = 0; i < 2; i++) {
        float fade_speed = (i == 0) ? CONTINUOUS_FADE_FAST : CONTINUOUS_FADE_SLOW;
        *(float*)((int)slot + 500 + i * 4) += g_dt_multiplier * fade_speed;
    }

    // Update intensity based on bounce state
    if (hit_bounce) {
        slot[0x7f] = (int)((float)slot[0x7f] +
            (1.0f / (float)g_frame_rate) * CONTINUOUS_SPIN_OFFSET * g_dt_multiplier);
        if (1.0f < (float)slot[0x7f]) {
            slot[0x7f] = (int)1.0f;
        }
    } else {
        slot[0x7f] = (int)(-(g_dt_multiplier * (1.0f / (float)g_frame_rate) - (float)slot[0x7f]));
        if ((float)slot[0x7f] < 0.0f) {
            slot[0x7f] = (int)0.0f;
        }
    }
}

// ============================================================================
// Address: 0x800c4bc8
// player_ai_projectile_beam_trail_render
// Renders particle trail effects along the beam path. Manages up to 5 particle
// emitters: first N are "core" particles (sparks/plasma along beam), remaining
// are "impact" particles (only active when beam hits something). Computes a
// perpendicular basis (tangent + bitangent) from the beam direction for particle
// orientation. Particle colors vary by projectile type (0x19 = blue, else = orange).
// ============================================================================
void player_ai_projectile_beam_trail_render(void) {
    int* slot = (int*)projectile_render_slot_get_alt();

    // Choose first-person vs third-person origin
    int is_local = player_ai_projectile_is_local_player((int)slot);
    int* start_pos;
    float* direction;
    if (is_local == 0) {
        start_pos = slot + 0xd;   // Third-person muzzle
        direction = (float*)(slot + 0x1f);
    } else {
        start_pos = slot + 10;    // First-person muzzle
        direction = (float*)(slot + 0x1c);
    }

    int max_impact_particles = 0;
    int max_core_particles = 0;

    if (g_projectile_enabled <= 0) {
        projectile_render_finish_alt();
        return;
    }

    // Store previous frame positions
    slot[7] = slot[0x10]; slot[8] = slot[0x11]; slot[9] = slot[0x12];
    slot[10] = slot[0x13]; slot[0xb] = slot[0x14]; slot[0xc] = slot[0x15];
    slot[0xd] = slot[0x16]; slot[0xe] = slot[0x17]; slot[0xf] = slot[0x18];

    // Compute beam direction (normalized end - start for 3P)
    slot[0x1f] = (int)((float)slot[0x68] - (float)slot[0xd]);
    slot[0x20] = (int)((float)slot[0x69] - (float)slot[0xe]);
    slot[0x21] = (int)((float)slot[0x6a] - (float)slot[0xf]);
    float len_3p = sqrtf((float)slot[0x1f] * (float)slot[0x1f] +
                         (float)slot[0x20] * (float)slot[0x20] +
                         (float)slot[0x21] * (float)slot[0x21]);
    slot[0x7b] = (int)len_3p;
    float inv_len = 1.0f / len_3p;
    slot[0x1f] = (int)((float)slot[0x1f] * inv_len);
    slot[0x20] = (int)((float)slot[0x20] * inv_len);
    slot[0x21] = (int)((float)slot[0x21] * inv_len);

    // Compute 1P direction if weapon entity exists
    if (slot[0x42] == 0 || *(int*)(slot[0x42] + 0x150) == 0) {
        slot[0x1c] = slot[0x1f];
        slot[0x1d] = slot[0x20];
        slot[0x1e] = slot[0x21];
        slot[0x7a] = slot[0x7b];
    } else {
        slot[0x1c] = (int)((float)slot[0x68] - (float)slot[10]);
        slot[0x1d] = (int)((float)slot[0x69] - (float)slot[0xb]);
        slot[0x1e] = (int)((float)slot[0x6a] - (float)slot[0xc]);
        float len_1p = sqrtf((float)slot[0x1c] * (float)slot[0x1c] +
                             (float)slot[0x1d] * (float)slot[0x1d] +
                             (float)slot[0x1e] * (float)slot[0x1e]);
        slot[0x7a] = (int)len_1p;
        float inv_1p = 1.0f / len_1p;
        slot[0x1c] = (int)((float)slot[0x1c] * inv_1p);
        slot[0x1d] = (int)((float)slot[0x1d] * inv_1p);
        slot[0x1e] = (int)((float)slot[0x1e] * inv_1p);
    }

    // Compute perpendicular basis (tangent) via cross product with world up
    float dir_y = direction[1];
    float tangent[3];
    if (fabsf(dir_y) < BEAM_UP_THRESHOLD) {
        // Use alternate up vector
        tangent[0] = dir_y * UP_ALT_Z - direction[2] * UP_ALT_Y;
        tangent[1] = direction[2] * UP_ALT_X - direction[0] * UP_ALT_Z;
        tangent[2] = direction[0] * UP_ALT_Y - dir_y * UP_ALT_X;
    } else {
        // Use primary up vector
        tangent[0] = dir_y * UP_PRI_Z - direction[2] * UP_PRI_Y;
        tangent[1] = direction[2] * UP_PRI_X - direction[0] * UP_PRI_Z;
        tangent[2] = direction[0] * UP_PRI_Y - dir_y * UP_PRI_X;
    }

    // Bitangent = tangent x direction
    float bitangent[3];
    bitangent[0] = tangent[1] * direction[2] - tangent[2] * direction[1];
    bitangent[1] = tangent[2] * direction[0] - tangent[0] * direction[2];
    bitangent[2] = tangent[0] * direction[1] - tangent[1] * direction[0];

    // Determine particle counts
    int screen_mode = FUN_801c1714();
    if (screen_mode < 3 && slot[0x40] == 1) {
        max_core_particles = 3;
    }
    max_impact_particles = max_core_particles + 2;

    // Spawn/update core particles
    for (int i = 0; i < max_core_particles; i++) {
        int particle_handle_offset = (int)slot + i * 4 + 0xac;
        if (*(int*)particle_handle_offset == 0) {
            // Spawn new particle with type-based color
            uint color;
            int size;
            if (*slot == 0x19) {
                // Blue plasma type
                size = 5;
                uint rng_g = random_uint();
                uint rng_b = random_uint();
                color = (rng_b & 0x2f) | 0xff;
            } else {
                // Orange/red type
                size = random_uint() % 3 == 0 ? 0 : 1;  // CLZ trick
                uint rng_r = random_uint() & 0x2f;
                uint rng_g = random_uint();
                color = 0xff0000 | rng_r;  // Simplified
            }
            *(int*)particle_handle_offset =
                particle_spawn_flag(particle_handle_offset, 0, start_pos, 0xffff);
            FUN_80102fec(*(int*)particle_handle_offset, size, color);
        }
        // Update particle position along beam
        int handle = *(int*)particle_handle_offset;
        if (handle != 0) {
            FUN_80103460(handle, start_pos, slot + 0x68, direction, &tangent[0], &bitangent[0]);
        }
    }

    // Spawn/update impact particles (only when beam hits something)
    for (int i = max_core_particles; i < max_impact_particles; i++) {
        int particle_handle_offset = (int)slot + i * 4 + 0xac;
        if (slot[0x28] != 0 && *(int*)particle_handle_offset == 0) {
            // Spawn impact particle
            uint color;
            int size;
            if (*slot == 0x19) {
                color = 0xff0000;
                uint rng_g = random_uint();
                uint rng_b = random_uint();
                size = 6;
            } else {
                uint rng_r = random_uint() & 0x2f;
                uint rng_g = random_uint();
                color = rng_g | 0xff0000;
                size = 2;
            }
            *(int*)particle_handle_offset =
                particle_spawn_flag(particle_handle_offset, 0, start_pos, 0xffff);
            FUN_80102fec(*(int*)particle_handle_offset, size, color);
        }
        int handle = *(int*)particle_handle_offset;
        if (handle != 0) {
            FUN_80103460(handle, start_pos, slot + 0x68, direction, &tangent[0], &bitangent[0]);
        }
    }

    projectile_render_finish_alt();
}
