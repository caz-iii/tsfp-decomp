// Address: 0x80100b48 - 0x801015c0 | Projectile Strip/Trail System | Date: 2026-08-25
// Batch 319: Bullet trail strips, hitscan (projectile + explosion), impact sound, strip lifecycle
//
// This module manages projectile visual trail strips (bullet tracers) and hitscan damage
// detection for both standard projectiles and area-of-effect explosions. Strips reuse
// the weather particle strip rendering system for GPU-efficient quad trails.

#include "common.h"
#include "projectile.h"
#include "entity.h"
#include "scene.h"
#include "sound.h"
#include "weather.h"

// ============================================================================
// Strip Lifecycle
// ============================================================================

// Address: 0x801015c0
// Deactivates a projectile strip. Sets radius based on flags and releases texture slots.
// If the strip has flags 0x40c set, it gets a large radius (fade-out visual);
// otherwise radius is set to zero (instant removal).
void projectile_strip_deactivate(u32* strip_def) {
    if ((*(u16*)(strip_def + 10) & 0x40c) == 0) {
        *strip_def = 0.0f;  // zero radius = invisible
    } else {
        *strip_def = FLOAT_80615640;  // large radius = fading out
    }

    // Release both texture slot indices (max 2 per strip)
    for (int i = 0; i < 2; i++) {
        char tex_idx = *(char*)((int)strip_def + i + 0xe);
        if (tex_idx >= 0) {
            // Free pool entry at DAT_805756f4[tex_idx * 0x13]
            (&DAT_805756f4)[tex_idx * 0x13] = 0;
            *(u8*)((int)strip_def + i + 0xe) = 0xff;  // mark unassigned
        }
    }
}

// Address: 0x8010156c
// Releases texture slots from a strip without conditional radius logic.
// Always zeroes the radius. Called by weather_strip_alloc_render_slot when
// evicting an old strip to make room for a new one.
void projectile_strip_release_textures(u32* strip_def) {
    *strip_def = 0.0f;  // zero radius

    for (int i = 0; i < 2; i++) {
        char tex_idx = *(char*)((int)strip_def + i + 0xe);
        if (tex_idx >= 0) {
            (&DAT_805756f4)[tex_idx * 0x13] = 0;
            *(u8*)((int)strip_def + i + 0xe) = 0xff;
        }
    }
}

// Address: 0x801014cc
// Destroys all projectile trail strips for the current entity list.
// Called by entity_destroy to clean up visual trails when an entity is removed.
// Also clears matching entries in the 4-slot active trail table (DAT_80575540).
void projectile_strip_destroy_all(void) {
    int entity_data = FUN_803826e0();
    int strip_list = *(int*)(entity_data + 0x1f8);
    int offset = 0;

    for (int i = 0; i < *(int*)(entity_data + 0x1c); i++) {
        u32* strip = *(u32**)(strip_list + offset);
        projectile_strip_deactivate(strip);

        // Clear active trail table entries referencing this strip
        int slot_offset = 0;
        for (int slot = 4; slot != 0; slot--) {
            if (*(int*)((int)&DAT_80575540 + slot_offset) != 0 &&
                *(int*)((int)&DAT_80575544 + slot_offset) == *(int*)(strip_list + offset)) {
                *(int*)((int)&DAT_80575540 + slot_offset) = 0;
            }
            slot_offset += 0x6c;
        }
        offset += 4;
    }
    FUN_8038272c();
}

// Address: 0x80100b48
// Removes expired projectile trail strips from the active list.
// Iterates backwards through the strip array; if a strip's lifetime float
// exceeds the threshold, memmoves subsequent entries down and decrements count.
// Called by entity_raycast_single as part of per-frame maintenance.
void projectile_strip_remove_expired(void) {
    int* list_header = (int*)FUN_803826f0();
    int count = *list_header - 1;
    int entry_offset = count * 0x34;
    float threshold = extraout_f1;  // passed via register

    for (; count >= 0; count--) {
        float lifetime = *(float*)((int)list_header + entry_offset + 0x30);
        if (threshold < lifetime) {
            // memmove entries down to remove this one
            leaf_31e8(
                (int)list_header + entry_offset + 8,
                (u32)(list_header + (count + 1) * 0xd + 2),
                ((*list_header - 1) - count) * 0x34
            );
            *list_header = *list_header - 1;
        }
        entry_offset -= 0x34;
    }
    FUN_8038273c();
}

// ============================================================================
// Strip Creation
// ============================================================================

// Address: 0x80101218
// Creates a new projectile trail/tracer strip. Allocates a weather strip slot,
// configures render flags, texture indices, velocity with wind offset, and
// optionally queries floor height for gravity-affected strips.
// Falls back to projectile_strip_deactivate if allocation fails.
void projectile_strip_create(u32 param_1, u32 param_2, int* strip_config,
                             float* velocity, float* ground_pos) {
    int context_hi = (int)((u64)FUN_803826e4() >> 32);
    u32* strip_def = (u32*)FUN_803826e4();

    // Check layer mask compatibility
    if ((strip_def[0xb] != 0) &&
        (strip_def[0xb] & *(u32*)(*(int*)(context_hi + 4) + 0x30)) == 0) {
        goto fail;
    }

    int* strip = weather_find_or_alloc_strip((int)strip_def);
    if (strip == NULL) {
        goto fail;
    }

    // Configure render flags from strip definition
    strip[2] = (u32)*(u16*)(strip_def + 10);  // base flags
    strip[3] = -1;  // texture ID (none by default)

    // Resolve texture from material table
    int material_table = *(int*)(*(int*)(*(int*)(context_hi + 4) + 4) + 0x10);
    if (material_table != 0) {
        strip[3] = *(int*)(material_table + strip_def[1] * 0x10);
    }

    // If not render mode 2, add flag 0x200 (additive blend)
    if (*(int*)(context_hi + 0x20) != 2) {
        strip[2] |= 0x200;
    }

    // Assign texture pool slots (up to 2 per strip)
    strip[0xd] = 0;
    if (*(char*)((int)strip_def + 0xe) >= 0) {
        strip[0xe] = (int)(&DAT_805756f0 + *(char*)((int)strip_def + 0xe) * 0x13);
        strip[0xd]++;
    }
    char tex_b = *(char*)((int)strip_def + 0xf);
    if (tex_b >= 0) {
        if (DAT_80611f60 != 2) {
            strip[0xd]--;  // reduce for non-mode-2
        }
        strip[0xf] = (int)(&DAT_805756f0 + tex_b * 0x13);
        strip[0xd]++;
    }

    // Set geometry data pointers from strip definition
    strip[5] = (int)(strip_def + 0x10);  // vertex data A
    strip[6] = (int)(strip_def + 0x17);  // vertex data B
    strip[7] = (int)(strip_def + 0x25);  // UV data
    strip[8] = (int)(strip_def + 0x1e);  // color data

    // Copy source position from strip_config
    strip[0x17] = *strip_config;
    strip[0x18] = strip_config[1];
    strip[4] = strip_def[2];  // strip type/class

    // Compute velocity with random spread and wind offset
    u32 rng = FUN_8013104c();
    float spread_factor = (float)((double)rng_to_float(rng) / FLOAT_80615628);
    float scale = dVar9 * spread_factor;

    strip[9]  = (int)(FLOAT_8061562c * velocity[0] * scale);
    strip[10] = (int)(velocity[1] * scale);
    strip[0xb] = (int)(FLOAT_8061562c * velocity[2] * scale);

    // Add wind offset
    float wind_scale = FLOAT_80615630;
    strip[9]  = (int)((float)strip[9]  + wind_scale * *(float*)(context_hi + 0xf8));
    strip[10] = (int)((float)strip[10] + wind_scale * *(float*)(context_hi + 0xfc));
    strip[0xb] = (int)((float)strip[0xb] + wind_scale * *(float*)(context_hi + 0x100));

    strip[0xc] = (int)(float)dVar9;  // lifetime/scale

    // Query floor height if ground_pos provided and not flagged 0x1000
    if (ground_pos != NULL && (strip[2] & 0x1000) == 0) {
        float query_pos[3];
        query_pos[0] = ground_pos[0] + velocity[0];
        query_pos[1] = ground_pos[1] + velocity[1];
        query_pos[2] = ground_pos[2] + velocity[2];
        float floor_height;
        floor_height = floor_query_with_interact(
            FLOAT_80615634, FLOAT_80615608, query_pos, &floor_height, NULL, 0);
        strip[0x10] = (int)floor_height;
        strip[2] |= 1;  // has floor data
    }

    weather_check_config_change();
    return;

fail:
    projectile_strip_deactivate(strip_def);
    FUN_80382730();
}

// ============================================================================
// Projectile Hitscan (Bullet)
// ============================================================================

// Address: 0x80100bf0
// Per-room projectile hitscan against an entity list. For each entity with valid
// radius and not flagged 0x40c0000, performs sphere intersection test, then
// scene_raycast for line-of-sight confirmation. On hit: dispatches to impact handler.
void projectile_hitscan_room_entities(u32 param_1, u32 param_2,
                                      float* ray_origin, u32 param_4, float* ray_end) {
    int entity_data = (int)((u64)FUN_803826e0() >> 32);
    int context = (int)FUN_803826e0();
    int entity_list = *(int*)(entity_data + 0x1f8);

    if (entity_list == 0) return;

    // Compute ray direction vector
    float ray_dir[3];
    ray_dir[0] = ray_end[0] - ray_origin[0];
    ray_dir[1] = ray_end[1] - ray_origin[1];
    ray_dir[2] = ray_end[2] - ray_origin[2];
    float ray_len_sq = ray_dir[0]*ray_dir[0] + ray_dir[1]*ray_dir[1] + ray_dir[2]*ray_dir[2];

    int offset = 0;
    for (int i = 0; i < *(int*)(entity_data + 0x1c); i++) {
        float* entity = *(float**)(entity_list + offset);

        // Skip entities with zero/negative radius or flagged as immune
        if (FLOAT_806155f0 > entity[0]) { offset += 4; continue; }
        if ((*(u32*)&entity[10] & 0x40c0000) != 0) { offset += 4; continue; }

        // Sphere intersection test
        float hit_point[3];
        int hit = FUN_80013dfc(ray_len_sq, ray_origin, ray_dir,
                               (int)entity, (int)hit_point, NULL);
        if (!hit) { offset += 4; continue; }

        // LOS check via scene_raycast
        float ray_to_hit[3];
        ray_to_hit[0] = hit_point[0] - ray_origin[0];
        ray_to_hit[1] = hit_point[1] - ray_origin[1];
        ray_to_hit[2] = hit_point[2] - ray_origin[2];

        *(u32*)(context + 0x28) &= ~0x20;  // clear hittable flag temporarily

        int blocked = scene_raycast(*(u32*)(context + 0x34), ray_origin, ray_to_hit,
                                    0, NULL, NULL, NULL, 0x2000070, 0, NULL);
        if (!blocked) {
            // Hit confirmed - check if type 8 for special sound
            if (*(int*)(context + 0x20) == 8) {
                FUN_80237b50(FLOAT_806155f4, 0x21, *(int*)(*(int*)(context + 0x7c) + 8));
            }
            // Dispatch impact
            FUN_801016c4(entity_data, entity, hit_point, &entity[12], ray_dir);
            FUN_8023d1a8();  // post-impact cleanup
        }

        *(u32*)(context + 0x28) |= 0x20;  // restore hittable flag
        offset += 4;
    }
    FUN_8038272c();
}

// Address: 0x80100de0
// Dispatches projectile hitscan across the player entity and all entities in the room.
// Checks flag 0x20 (hittable by bullet). Skips entities with flag 0x100 or type 8.
// Called by entity_los_check_with_scene_query.
void projectile_hitscan_all_entities(u32 param_1, u32 param_2,
                                     float* ray_origin, u32 param_4, float* ray_end) {
    int player_index = (int)((u64)FUN_803826d8() >> 32);

    // Check player entity first
    if (player_index <= DAT_80611b14) {
        int player_entity = *(int*)(DAT_80611b04 + player_index * 4);
        if ((*(u32*)(player_entity + 0x28) & 0x20) != 0) {
            projectile_hitscan_room_entities(player_entity, (int)FUN_803826d8(),
                                             ray_origin, param_4, ray_end);
        }
    }

    // Iterate room entity list
    int idx = 0;
    int offset = 0;
    while (idx < (int)(&DAT_805401dc)[player_index]) {
        int entity = *(int*)((&DAT_8053fb2c)[player_index] + offset);

        // Check parent entity is active (flag 1)
        if ((*(u32*)(*(int*)(entity + 0x24) + 0x28) & 1) != 0) {
            // Hittable (0x20), not immune (0x100), not type 8
            if (((*(u32*)(entity + 0x28) & 0x20) != 0) &&
                ((*(u32*)(entity + 0x28) & 0x100) == 0) &&
                (*(int*)(entity + 0x20) != 8)) {
                projectile_hitscan_room_entities(entity, (int)FUN_803826d8(),
                                                 ray_origin, param_4, ray_end);
            }
        }
        idx++;
        offset += 4;
    }
    FUN_80382724();
}

// ============================================================================
// Explosion Hitscan (Area Damage)
// ============================================================================

// Address: 0x80100ec8
// Per-room explosion/area damage scan. Similar to projectile hitscan but with
// Y-offset for explosion center and distance-based damage falloff.
// No LOS check required (explosions go through geometry).
void explosion_hitscan_room_entities(u64 param_1, double blast_radius,
                                     u32 param_3, u32 param_4,
                                     float* origin, float* target) {
    int context = (int)FUN_803826e0();
    int entity_list = *(int*)(context + 0x1f8);

    if (entity_list == 0) return;

    // Compute explosion center with Y offset
    float center[3];
    center[0] = origin[0];
    center[1] = origin[1] + extraout_f1;  // Y offset for explosion height
    center[2] = origin[2];

    // Direction from center to target
    float dir[3];
    dir[0] = target[0] - center[0];
    dir[1] = target[1] - origin[1];
    dir[2] = target[2] - center[2];

    // Effective radius squared (accounts for blast_radius - offset)
    float offset = blast_radius - FLOAT_80615618;
    float range_sq = offset * offset + dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];

    int offset_bytes = 0;
    for (int i = 0; i < *(int*)(context + 0x1c); i++) {
        float* entity = *(float**)(entity_list + offset_bytes);

        if (FLOAT_806155f0 > entity[0]) { offset_bytes += 4; continue; }
        if ((*(u32*)&entity[10] & 0x40c0000) != 0) { offset_bytes += 4; continue; }

        float hit_point[3];
        int hit = FUN_80013dfc(range_sq, center, dir, (int)entity, (int)hit_point, NULL);
        if (!hit) { offset_bytes += 4; continue; }

        if (DAT_806108e4 > 0) {
            // Compute actual distance for damage falloff
            float dist_sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
            FUN_80131538(dist_sq);  // apply damage falloff
            FUN_80237b50(FLOAT_806155f4, 0x21, (int)((u64)FUN_803826e0() >> 32));
            FUN_801016c4(context, entity, hit_point, &entity[12], dir);
            FUN_8023d1a8();
        }
        offset_bytes += 4;
    }
    FUN_8038272c();
}

// Address: 0x80101094
// Dispatches explosion area damage across player and all room entities.
// Checks flag 0x40 (damageable by explosion). Skips 0x100 and type 8.
// Called by nav_trace_and_slide.
void explosion_hitscan_all_entities(u64 param_1, double blast_radius,
                                    u32 param_3, u32 param_4,
                                    float* origin, float* target) {
    int player_index = (int)((u64)FUN_803826dc() >> 32);

    // Check player entity (flag 0x40 = damageable by explosion)
    if (player_index <= DAT_80611b14) {
        int player_entity = *(int*)(DAT_80611b04 + player_index * 4);
        if ((*(u32*)(player_entity + 0x28) & 0x40) != 0) {
            explosion_hitscan_room_entities(param_1, blast_radius,
                                            player_index, player_entity, origin, target);
        }
    }

    // Iterate room entity list
    int idx = 0;
    int offset = 0;
    while (idx < (int)(&DAT_805401dc)[player_index]) {
        int entity = *(int*)((&DAT_8053fb2c)[player_index] + offset);

        if ((*(u32*)(*(int*)(entity + 0x24) + 0x28) & 1) != 0) {
            if (((*(u32*)(entity + 0x28) & 0x40) != 0) &&
                ((*(u32*)(entity + 0x28) & 0x100) == 0) &&
                (*(int*)(entity + 0x20) != 8)) {
                explosion_hitscan_room_entities(param_1, blast_radius,
                                                player_index, entity, origin, target);
            }
        }
        idx++;
        offset += 4;
    }
    FUN_80382728();
}

// ============================================================================
// Impact Sound
// ============================================================================

// Address: 0x80101194
// Plays a randomized ricochet/impact sound at the given position and alerts
// nearby AI. Sound IDs are 0xd4b through 0xd4e (4 ricochet variants).
void projectile_play_impact_sound(float* position) {
    // Alert AI near impact point
    player_ai_alert_scan_simple(position, NULL);

    // Pick random ricochet variant (0-3)
    u32 random_value = FUN_8013104c();

    // Build sound parameters
    int sound_params[7];
    sound_init_play_params((int)sound_params);

    // Set 3D position and flags
    *(u8*)((int)sound_params + 0x28) = 1;  // positional flag
    *(float*)((int)sound_params + 0x2c) = FLOAT_80615624;  // volume
    *(float*)((int)sound_params + 0x1c) = position[0];
    *(float*)((int)sound_params + 0x20) = position[1];
    *(float*)((int)sound_params + 0x24) = position[2];

    sound_params[0] = (random_value & 3) + 0xd4b;  // ricochet sound ID
    sound_play_request(sound_params);
}


// ============================================================================
// Batch 320: Impact Handler, Tick Update, Debris, Fade/Portal, Renderer, Pool
// Address: 0x8010162c - 0x80102b90 | Date: 2026-08-25
// ============================================================================

// Address: 0x8010162c
// Computes parametric UV coordinates for a point on a strip quad.
// Projects position onto two edge vectors from vertex A (offsets +0x40/+0x44/+0x48)
// to vertex B (+0x5c/+0x60/+0x64) and vertex C (+0x94/+0x98/+0x9c).
// Returns normalized [0,1] parameters in out_uv[0] and out_uv[1].
void projectile_strip_compute_uv(int strip_data, float* position, float* out_uv) {
    // Vertex A at offset 0x40
    float ax = *(float*)(strip_data + 0x40);
    float ay = *(float*)(strip_data + 0x44);
    float az = *(float*)(strip_data + 0x48);

    // Edge vectors: B-A and C-A
    float edge1_x = *(float*)(strip_data + 0x5c) - ax;
    float edge1_y = *(float*)(strip_data + 0x60) - ay;
    float edge1_z = *(float*)(strip_data + 0x64) - az;

    float edge2_x = *(float*)(strip_data + 0x94) - ax;
    float edge2_y = *(float*)(strip_data + 0x98) - ay;
    float edge2_z = *(float*)(strip_data + 0x9c) - az;

    // Point relative to vertex A
    float dx = position[0] - ax;
    float dy = position[1] - ay;
    float dz = position[2] - az;

    // Project onto edge1 (normalized by edge1 length squared)
    float edge1_len_sq = edge1_x * edge1_x + edge1_y * edge1_y + edge1_z * edge1_z;
    out_uv[0] = (edge1_x * dx + edge1_y * dy + edge1_z * dz) / edge1_len_sq;

    // Project onto edge2 (normalized by edge2 length squared)
    float edge2_len_sq = edge2_x * edge2_x + edge2_y * edge2_y + edge2_z * edge2_z;
    out_uv[1] = (edge2_x * dx + edge2_y * dy + edge2_z * dz) / edge2_len_sq;
}

// Address: 0x801016c4
// Core projectile impact handler. Normalizes incoming direction, optionally
// reflects off surface normal (flag 0x100 = reflective), plays impact sound,
// and creates a tracer/debris trail strip.
void projectile_impact_handler(u32 param_1, u32 param_2, float* hit_pos,
                               float* surface_normal, float* direction) {
    int strip_data = (int)FUN_803826e8();

    // Compute UV coordinates on the impacted strip face
    float uv[2];
    projectile_strip_compute_uv(strip_data, hit_pos, uv);

    // Normalize direction vector
    float len_sq = direction[0]*direction[0] + direction[1]*direction[1] + direction[2]*direction[2];
    float inv_len = 1.0f / sqrtf(len_sq);
    float norm_dir[3];
    norm_dir[0] = direction[0] * inv_len;
    norm_dir[1] = direction[1] * inv_len;
    norm_dir[2] = direction[2] * inv_len;

    // Reflect direction off surface normal if strip is flagged reflective (0x100)
    if ((*(u16*)(strip_data + 0x28) & 0x100) != 0) {
        float dot = norm_dir[0]*surface_normal[0] +
                    norm_dir[1]*surface_normal[1] +
                    norm_dir[2]*surface_normal[2];
        dot *= 2.0f;
        // Reflection: R = -(N*2dot - D)
        norm_dir[0] = -(surface_normal[0] * dot - norm_dir[0]);
        norm_dir[1] = -(surface_normal[1] * dot - norm_dir[1]);
        norm_dir[2] = -(surface_normal[2] * dot - norm_dir[2]);
    }

    // Play ricochet/impact sound at hit position
    projectile_play_impact_sound(hit_pos);

    // Create trail strip from impact point in reflected/original direction
    projectile_strip_create((int)((u64)FUN_803826e8() >> 32), strip_data,
                            (int*)uv, norm_dir, hit_pos);
    FUN_80382734();
}

// Address: 0x801017e4
// Generic entity list iterator with callback. Filters entities by radius
// (must be positive) and flag mask (must not have 0x40c0000), then calls
// the provided function pointer for each valid entity.
void projectile_strip_iterate_entities(void) {
    u64 ctx = FUN_803826e4();
    int entity_data = (int)(ctx >> 32);
    void (*callback)(int) = (void(*)(int))(int)ctx;

    int entity_list = *(int*)(entity_data + 0x1f8);
    if (entity_list == 0) { FUN_80382730(); return; }

    int offset = 0;
    for (int i = 0; i < *(int*)(entity_data + 0x1c); i++) {
        float* entity = *(float**)(entity_list + offset);
        if (FLOAT_806155f0 <= entity[0] &&
            (*(u32*)&entity[10] & 0x40c0000) == 0 &&
            callback != NULL) {
            callback(entity_data);
        }
        offset += 4;
    }
    FUN_80382730();
}

// Address: 0x801018a4
// Main per-frame projectile strip update tick. For each strip entry (0x34 bytes):
// - Checks portal visibility via dot product against portal plane normal
// - Manages up to 4 particle slots per strip
// - Accumulates random lifetime; on threshold triggers impact or billboard spawn
// - Plays impact sound (0xd4f) and allocates render slots
// Called by player_ai_on_damage_received and player_ai_projectile_homing_tick.
void projectile_strip_tick_update(void) {
    u64 ctx = FUN_803826cc();
    int* strip_list = (int*)(ctx >> 32);
    int weapon_type = (int)ctx;
    int entry_idx = 0;
    int entry_offset = 0;
    float threshold = extraout_f1;

    do {
        if (*strip_list <= entry_idx) {
            FUN_80382718();
            return;
        }

        int* entry = (int*)((int)strip_list + entry_offset + 8);
        float* strip_def = (float*)entry[0xc];

        // Check layer mask compatibility
        bool layer_ok = false;
        if (strip_def[0xb] == 0.0f ||
            ((u32)strip_def[0xb] & *(u32*)(*(int*)(*entry + 4) + 0x30)) != 0) {
            layer_ok = true;
        }

        if (layer_ok) {
            // Portal visibility check (flag 0x20000)
            if (((u32)strip_def[10] & 0x20000) != 0) {
                int* portal = (int*)scene_portal_get_ptr((int)*(s16*)(strip_def + 3));
                // Dot product of entry direction vs portal normal
                float dot = (float)entry[6] * (float)portal[4] +
                            (float)entry[4] * (float)portal[2] +
                            (float)entry[5] * (float)portal[3];
                // Visibility: same side as current room
                bool visible = false;
                if ((FLOAT_806155f0 <= dot && *(int*)(*entry + 0x34) == *portal) ||
                    (dot < FLOAT_806155f0 && *(int*)(*entry + 0x34) != *portal)) {
                    visible = true;
                }
                if (!visible) goto next_entry;
            }

            // Find first empty particle slot (up to 4)
            int free_slot = -1;
            for (int s = 0; s < 4; s++) {
                if (*(int*)((int)strip_def + s * 4 + 0x10) == 0) break;
                free_slot = -1;  // all occupied → -1
            }

            // Accumulate random lifetime
            float rng1 = (float)FUN_8013104c_to_float();
            float rng2 = (float)FUN_8013104c_to_float();
            *strip_def += FLOAT_80615644 * (rng2 * rng1) + *strip_def;
            if (DAT_80611f60 == 0) {
                *strip_def += FUN_8013104c_to_float();
            }

            // Determine action based on lifetime and state
            bool should_impact = (*strip_def >= 1.0f || threshold >= 1.0f || free_slot == -1);
            bool has_valid_radius = (*strip_def >= FLOAT_806155f0 &&
                                    ((u32)strip_def[10] & 0x40c0000) == 0);

            // Check for special weapon types with pierce/fire flags
            bool is_special = (((u32)strip_def[10] & 0x40c0000) != 0 && weapon_type != -1 &&
                              ((*(u32*)(&DAT_804045bc + weapon_type * 0x40) & 0x600) != 0 ||
                               weapon_type == 0x15));

            bool should_fade = (!should_impact || !has_valid_radius) &&
                               ((u32)strip_def[10] & 0x20000000) == 0 && !is_special;

            bool should_spark = (DAT_80611f60 != 0) &&
                                ((u32)strip_def[10] & 0x8000000) == 0 && !is_special;

            if (should_spark) {
                // Play spark sound and allocate render slot
                projectile_strip_compute_uv((int)strip_def, (float*)(entry + 1), uv);
                if (weapon_type != 0x14) {
                    int sound_params[7];
                    sound_init_play_params((int)sound_params);
                    sound_params[0] = 0xd4f;  // spark/ricochet sound
                    // Set 3D position from entry
                    *(float*)&sound_params[5] = (float)entry[1];
                    *(float*)&sound_params[6] = (float)entry[2];
                    // ... volume and flags
                    sound_play_request(sound_params);
                }
                weather_strip_alloc_render_slot(*entry, strip_def, (int*)uv);
            }

            if (should_fade) {
                // Spawn billboard particle if valid character def
                int char_name = player_ai_char_def_get_name(weapon_type);
                if (char_name != 0 && ((u32)strip_def[10] & 0x20000000) == 0 && free_slot >= 0) {
                    float handle = particle_spawn_attached_billboard(
                        weapon_type, *entry, 0, (float*)(entry + 1), (float*)(entry + 7),
                        (int)(strip_def + 0x10), (u32)*(u16*)((int)strip_def + 0x2a));
                    strip_def[free_slot + 4] = handle;
                }
            } else if (should_impact && has_valid_radius) {
                // Trigger full impact handler
                projectile_impact_handler(*entry, strip_def, (float*)(entry + 1),
                                         (float*)(entry + 7), (float*)(entry + 4));
                FUN_8023d1a8();
            }
        }

next_entry:
        entry_idx++;
        entry_offset += 0x34;
    } while (true);
}

// Address: 0x80101cb4
// Spawns debris/spark particles at a projectile impact point. Computes the
// centroid of strip vertices, checks distance from camera against effective range,
// generates random position on strip face, computes reflected velocity with
// cross-product derived normal, and creates a trail strip or plays sound+render slot.
void projectile_impact_spawn_debris(u32 param_1, u32 param_2, float* bounds) {
    int entity_data = (int)((u64)FUN_803826e0() >> 32);
    int entity_list = *(int*)(entity_data + 0x1f8);
    if (entity_list == 0) { FUN_8038272c(); return; }

    float* camera_pos = *(float**)((int)FUN_803826e0() + 0x28);
    float range_scale = FLOAT_80615648 * extraout_f1;
    int offset = 0;

    for (int i = 0; i < *(int*)(entity_data + 0x1c); i++) {
        float* strip_def = *(float**)(entity_list + offset);
        if (FLOAT_806155f0 > *strip_def) { offset += 4; continue; }

        // Compute centroid of strip vertices
        int vert_count = (u32)*(u16*)((int)strip_def + 0x2a);
        float inv_count = 1.0f / (float)vert_count;
        float centroid[3] = {0, 0, 0};
        int vert_offset = 0;
        for (u32 v = vert_count; v != 0; v--) {
            centroid[0] += *(float*)((int)strip_def + vert_offset + 0x40);
            centroid[1] += *(float*)((int)strip_def + vert_offset + 0x44);
            centroid[2] += *(float*)((int)strip_def + vert_offset + 0x48);
            vert_offset += 0x1c;
        }
        centroid[0] *= inv_count;
        centroid[1] *= inv_count;
        centroid[2] *= inv_count;

        // Skip if bounds filter provided and centroid is outside
        if (bounds != NULL) {
            if (centroid[0] < bounds[0] || centroid[0] >= bounds[3] ||
                centroid[1] < bounds[1] || centroid[1] >= bounds[4] ||
                centroid[2] < bounds[2] || centroid[2] >= bounds[5]) {
                offset += 4;
                continue;
            }
        }

        // Distance from camera to centroid
        float dx = centroid[0] - camera_pos[0];
        float dy = centroid[1] - camera_pos[1];
        float dz = centroid[2] - camera_pos[2];
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);

        // Normalize direction to centroid
        float inv_dist = 1.0f / dist;
        float dir[3] = { dx * inv_dist, dy * inv_dist, dz * inv_dist };

        // Range check: must be within effective distance
        float effective_range = *(float*)((int)FUN_803826e0() + 0x54);
        float range_factor = range_scale * (1.0f - (dist / effective_range));
        if (range_factor <= FLOAT_806155f0) { offset += 4; continue; }

        // Random position on strip face
        float rng_u = FUN_8013104c_to_float();
        float rng_v = FUN_8013104c_to_float();

        // Compute position from strip edges
        float v1[3], v2[3];  // edge vectors from first vertex
        v1[0] = strip_def[0x17] - strip_def[0x10]; // vertex B - vertex A
        v1[1] = strip_def[0x18] - strip_def[0x11];
        v1[2] = strip_def[0x19] - strip_def[0x12];
        v2[0] = strip_def[0x1e] - strip_def[0x10]; // vertex C - vertex A
        v2[1] = strip_def[0x1f] - strip_def[0x11];
        v2[2] = strip_def[0x20] - strip_def[0x12];

        float spawn_pos[3];
        spawn_pos[0] = v2[0] * rng_v + v1[0] * rng_u + strip_def[0x10];
        spawn_pos[1] = v2[1] * rng_v + v1[1] * rng_u + strip_def[0x11];
        spawn_pos[2] = v2[2] * rng_v + v1[2] * rng_u + strip_def[0x12];

        if (((u32)strip_def[10] & 0x40c0000) == 0) {
            // Normal strip: compute cross-product normal for velocity
            float normal[3];
            normal[0] = v1[1] * v2[2] - v1[2] * v2[1];
            normal[1] = v1[2] * v2[0] - v1[0] * v2[2];
            normal[2] = v1[0] * v2[1] - v1[1] * v2[0];

            // Velocity = base_dir + abs(normal) * base_dir (biased scatter)
            float vel[3];
            vel[0] = FLOAT_80615608 * dir[0] + FLOAT_80615608 * dir[0] * fabsf(normal[0]);
            vel[1] = FLOAT_80615608 * dir[1] + FLOAT_80615608 * dir[1] * fabsf(normal[1]);
            vel[2] = FLOAT_80615608 * dir[2] + FLOAT_80615608 * dir[2] * fabsf(normal[2]);

            projectile_play_impact_sound(strip_def + 0x10);
            projectile_strip_create(entity_data, strip_def, (int*)&rng_u, vel, spawn_pos);
        } else if (DAT_80611f60 != 0 && ((u32)strip_def[10] & 0x8000000) == 0) {
            // Special flagged strip: play sound + allocate render slot only
            int sound_params[7];
            sound_init_play_params((int)sound_params);
            sound_params[0] = 0xd4f;
            // 3D position
            *(float*)&sound_params[5] = spawn_pos[0];
            *(float*)&sound_params[6] = spawn_pos[1];
            *(float*)&sound_params[7] = spawn_pos[2];
            sound_play_request(sound_params);
            weather_strip_alloc_render_slot(entity_data, strip_def, (int*)&rng_u);
        }

        offset += 4;
    }
    FUN_8038272c();
}

// Address: 0x801020cc
// Per-frame strip particle fade and portal visibility update.
// For fading strips (negative radius): accumulates fade time, reduces particle
// opacity linearly, kills particles when fully faded.
// For portal-flagged strips (0x20000): checks player proximity vs portal plane
// and sets/clears portal dirty visibility flags based on distance thresholds.
// Called by entity_update_dispatch.
void projectile_strip_fade_and_portal(void) {
    int context = FUN_803826d4();
    int strip_list = *(int*)(context + 0x1f8);

    // Compute fade rate from global timing
    float fade_rate = FLOAT_806108ec *
        (1.0f / ((float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_806155f8)));

    int offset = 0;
    for (int i = 0; i < *(int*)(context + 0x1c); i++) {
        float* strip_def = *(float**)(strip_list + offset);

        // Handle fading strips (radius < 0 = in fade-out state)
        if (*strip_def < FLOAT_806155f0) {
            float new_radius = *strip_def + FLOAT_8061564c;
            // Advance fade or clamp to zero
            if (new_radius < FLOAT_80615608 ||
                (DAT_805015bd != '\n' && (DAT_805015c0 & 0x40) == 0 &&
                 (DAT_805015c0 & 0x80) == 0)) {
                *strip_def += fade_rate;
                if (*strip_def > FLOAT_806155f0) {
                    *strip_def = FLOAT_806155f0;
                }
            }

            // Update particle opacity or kill
            float fade_t = new_radius / FLOAT_80615608;
            for (int slot = 0; slot < 4; slot++) {
                int particle = *(int*)((int)strip_def + slot * 4 + 0x10);
                if (particle != 0) {
                    if (new_radius < FLOAT_80615608) {
                        particle_set_opacity(1.0f - fade_t, particle);
                    } else {
                        particle_kill(particle);
                        *(int*)((int)strip_def + slot * 4 + 0x10) = 0;
                    }
                }
            }
        }

        // Portal visibility dirty flag management
        if ((*(u32*)(context + 0xc) & 0xffff) != 0 &&
            ((u32)strip_def[10] & 0x20000) != 0) {
            // Check each active player
            int player_offset = 0;
            for (int p = 0; p < DAT_80611d58; p++) {
                int player = DAT_80611d5c + player_offset;
                if ((*(u32*)(player + 0x21c) & 0x20) != 0) {
                    int* portal = (int*)scene_portal_get_ptr((int)*(s16*)(strip_def + 3));
                    // Distance from player to strip vertex A
                    float px = strip_def[0x10] - *(float*)(player + 0x27c);
                    float py = strip_def[0x11] - *(float*)(player + 0x280);
                    float pz = strip_def[0x12] - *(float*)(player + 0x284);

                    // Dot product with portal normal
                    float dot = pz * (float)portal[4] + px * (float)portal[2] + py * (float)portal[3];

                    // Same-side check
                    bool same_side = (FLOAT_806155f0 <= dot && *(int*)(context + 0x34) == *portal) ||
                                     (dot < FLOAT_806155f0 && *(int*)(context + 0x34) != *portal);

                    if (same_side) {
                        float flags = strip_def[10];
                        float dist_sq = px*px + py*py + pz*pz;

                        if (((u32)flags & 0x1c0000) == 0) {
                            scene_portal_mark_dirty_clear((int)*(s16*)(strip_def + 3), p);
                        } else if (((u32)flags & 0x180000) == 0) {
                            if (((u32)flags & 0x40000) != 0) {
                                scene_portal_mark_dirty_set((int)*(s16*)(strip_def + 3), p);
                            }
                        } else {
                            float threshold = FLOAT_80615654;
                            if (((u32)flags & 0x100000) != 0) {
                                threshold = FLOAT_80615650;
                            }
                            if (dist_sq <= threshold ||
                                (*(u32*)(player + 0x21c) & 0x80) != 0) {
                                scene_portal_mark_dirty_clear((int)*(s16*)(strip_def + 3), p);
                            } else {
                                scene_portal_mark_dirty_set((int)*(s16*)(strip_def + 3), p);
                            }
                        }
                    }
                }
                player_offset += 0xffc;
            }
        }
        offset += 4;
    }
    FUN_80382720();
}

// Address: 0x801023e0
// Submits a single textured quad to GX FIFO for a projectile strip overlay.
// Resolves texture from sprite system, configures GX draw command (type 0x92,
// 4 vertices), then writes positions + byte-swapped RGBA + UV for each vertex
// directly to GX hardware FIFO at 0xCC008000.
void projectile_strip_submit_textured_quad(int strip_data, int tex_entry) {
    int tex_id = sprite_get_texture_id(**(int**)(tex_entry + 0x18));
    FUN_80172a4c(2, 0x92, 0, *(u16*)(strip_data + 0x2a), tex_id);

    // Vertex 0: position + color + UV
    DAT_cc008000 = *(u32*)(strip_data + 0x40);  // pos.x
    DAT_cc008000 = *(u32*)(strip_data + 0x44);  // pos.y
    DAT_cc008000 = *(u32*)(strip_data + 0x48);  // pos.z
    u32 color0 = *(u32*)(strip_data + 0x58);
    DAT_cc008000 = BSWAP32(color0);             // RGBA byte-swapped
    DAT_cc008000 = *(u32*)(tex_entry + 0x2c);   // UV.u
    DAT_cc008000 = *(u32*)(tex_entry + 0x30);   // UV.v

    // Vertex 1
    DAT_cc008000 = *(u32*)(strip_data + 0x5c);
    DAT_cc008000 = *(u32*)(strip_data + 0x60);
    DAT_cc008000 = *(u32*)(strip_data + 0x64);
    u32 color1 = *(u32*)(strip_data + 0x74);
    DAT_cc008000 = BSWAP32(color1);
    DAT_cc008000 = *(u32*)(tex_entry + 0x34);
    DAT_cc008000 = *(u32*)(tex_entry + 0x38);

    // Vertex 2
    DAT_cc008000 = *(u32*)(strip_data + 0x94);
    DAT_cc008000 = *(u32*)(strip_data + 0x98);
    DAT_cc008000 = *(u32*)(strip_data + 0x9c);
    u32 color2 = *(u32*)(strip_data + 0xac);
    DAT_cc008000 = BSWAP32(color2);
    DAT_cc008000 = *(u32*)(tex_entry + 0x44);
    DAT_cc008000 = *(u32*)(tex_entry + 0x48);

    // Vertex 3
    DAT_cc008000 = *(u32*)(strip_data + 0x78);
    DAT_cc008000 = *(u32*)(strip_data + 0x7c);
    DAT_cc008000 = *(u32*)(strip_data + 0x80);
    u32 color3 = *(u32*)(strip_data + 0x90);
    DAT_cc008000 = BSWAP32(color3);
    DAT_cc008000 = *(u32*)(tex_entry + 0x3c);
    DAT_cc008000 = *(u32*)(tex_entry + 0x40);

    FUN_80172c54();  // end GX primitive
}

// Address: 0x80102508
// Main projectile strip GPU renderer. For each active strip with valid radius:
// - Resolves texture from material table (4-vertex quad type)
// - Checks portal visibility via dot product (for portal-flagged strips 0x20000)
// - Computes distance-based fade opacity for close/far thresholds
// - Applies global fade alpha from strip lifetime
// - Selects render path:
//   A) Direct FIFO: inline pos/color/UV write to 0xCC008000
//   B) Indexed vertex arrays (render mode 2)
//   C) TEV-blended with distance fog (portal proximity fade)
// - Submits texture overlays via projectile_strip_submit_textured_quad
// Called by render_instance_draw during the scene render pass.
void projectile_strip_render(void) {
    int context = FUN_803826d4();
    int strip_list = *(int*)(context + 0x1f8);

    if (*(int*)(context + 0x1c) > 0) {
        // Set up view-space transform for distance calculations
        FUN_80130cd8(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;
    }

    int offset = 0;
    for (int i = 0; i < *(int*)(context + 0x1c); i++) {
        float* strip_def = *(float**)(strip_list + offset);

        // Layer mask + minimum radius check
        if ((strip_def[0xb] == 0.0f ||
             ((u32)strip_def[0xb] & *(u32*)(*(int*)(context + 4) + 0x30)) != 0) &&
            *strip_def >= FLOAT_80615658) {

            // Compute alpha from lifetime
            int alpha = FUN_803825c0(FLOAT_8061565c * FLOAT_80615608 * (FLOAT_80615660 + *strip_def));
            u32 alpha_byte = alpha << 24;
            float distance_fade = FLOAT_806155f0;

            // Resolve texture for 4-vertex quads
            int tex_id = -1;
            if (*(s16*)((int)strip_def + 0x2a) == 4) {
                int mat_table = *(int*)(*(int*)(*(int*)(context + 4) + 4) + 0x10);
                if (mat_table != 0) {
                    tex_id = *(int*)(mat_table + (int)strip_def[1] * 0x10);
                }

                // Portal visibility check
                if (((u32)strip_def[10] & 0x20000) != 0) {
                    int* portal = (int*)scene_portal_get_ptr((int)*(s16*)(strip_def + 3));
                    float px = strip_def[0x10] - *(float*)(DAT_80611d54 + 0x27c);
                    float py = strip_def[0x11] - *(float*)(DAT_80611d54 + 0x280);
                    float pz = strip_def[0x12] - *(float*)(DAT_80611d54 + 0x284);
                    float dot = pz * (float)portal[4] + px * (float)portal[2] + py * (float)portal[3];

                    bool visible = (FLOAT_806155f0 <= dot && *(int*)(context + 0x34) == *portal) ||
                                   (dot < FLOAT_806155f0 && *(int*)(context + 0x34) != *portal);
                    if (!visible) goto next_strip;

                    // Distance-based fade for portal proximity
                    if (((u32)strip_def[10] & 0x180000) != 0) {
                        float dist_sq = px*px + py*py + pz*pz;
                        float inner_thresh = ((u32)strip_def[10] & 0x100000) ? FLOAT_80615620 : FLOAT_80615630;
                        if (dist_sq > inner_thresh &&
                            (*(u32*)(DAT_80611d54 + 0x21c) & 0x80) == 0) {
                            float outer_thresh = ((u32)strip_def[10] & 0x100000) ? FLOAT_80615650 : FLOAT_80615654;
                            if (dist_sq <= outer_thresh) {
                                float dist = sqrtf(dist_sq);
                                if (((u32)strip_def[10] & 0x100000) == 0) {
                                    distance_fade = (dist - FLOAT_8061566c) / FLOAT_80615664;
                                } else {
                                    distance_fade = (dist - FLOAT_80615664) / FLOAT_80615668;
                                }
                            } else {
                                distance_fade = 1.0f;
                            }
                        }
                    }
                }

                // Apply alpha from lifetime fade
                float color[4];
                if (*strip_def < FLOAT_806155f0) {
                    // Fading: override alpha channel
                    color[0] = (float)((u32)strip_def[0x16] & 0xffffff | alpha_byte);
                    color[1] = (float)((u32)strip_def[0x1d] & 0xffffff | alpha_byte);
                    color[2] = (float)((u32)strip_def[0x24] & 0xffffff | alpha_byte);
                    color[3] = (float)((u32)strip_def[0x2b] & 0xffffff | alpha_byte);
                } else {
                    color[0] = strip_def[0x16];
                    color[1] = strip_def[0x1d];
                    color[2] = strip_def[0x24];
                    color[3] = strip_def[0x2b];
                }

                // Select render path
                bool needs_tev_blend = ((u32)strip_def[10] & 0x20000) != 0 &&
                                       ((u32)strip_def[10] & 0x180000) != 0;

                if (!needs_tev_blend || FUN_803c9040(*DAT_80610a20 + tex_id * 0x58 + 0x20) == 0) {
                    if (*strip_def < FLOAT_806155f0 || *(int*)(context + 0x20) != 2) {
                        // Path A: Direct FIFO quad (most common)
                        FUN_80172d88(0x92, 0, *(u16*)((int)strip_def + 0x2a), tex_id);
                        // Write 4 vertices: pos(xyz) + color(bswap32) + UV(st)
                        // ... (GX FIFO writes for each vertex)
                        FUN_80172c54();
                    } else {
                        // Path B: Indexed vertex arrays (render mode 2)
                        FUN_801728e0(0, (u32)(strip_def + 0x10), 0x1c, *(u16*)((int)strip_def + 0x2a));
                        FUN_801728e0(2, (u32)(strip_def + 0x16), 0x1c, *(u16*)((int)strip_def + 0x2a));
                        FUN_80172a4c(5, 0x114, 0, *(u16*)((int)strip_def + 0x2a), tex_id);
                        // Write index+UV pairs
                        FUN_80172c54();
                    }
                } else {
                    // Path C: TEV-blended with distance fog
                    int prev_tev = FUN_8016e4b4(-0x7fe71c44);
                    int fog_alpha = FUN_803825c0(FLOAT_8061565c * distance_fade);
                    FUN_8016efe8(fog_alpha);
                    FUN_8016ee98();
                    FUN_8016f00c(0xf);
                    FUN_8016f00c(1);
                    // Indexed vertex arrays with TEV
                    FUN_801728e0(0, (u32)(strip_def + 0x10), 0x1c, *(u16*)((int)strip_def + 0x2a));
                    FUN_801728e0(2, (u32)(strip_def + 0x16), 0x1c, *(u16*)((int)strip_def + 0x2a));
                    FUN_80172a4c(5, 0x114, 0, *(u16*)((int)strip_def + 0x2a), tex_id);
                    // Write index+UV pairs
                    FUN_80172c54();
                    FUN_8016e4b4(prev_tev);  // restore TEV state
                }

                // Submit texture overlays (up to 2 per strip)
                if (*(s8*)((int)strip_def + 0xe) >= 0) {
                    projectile_strip_submit_textured_quad(
                        (int)strip_def,
                        (int)(&DAT_805756f0 + *(s8*)((int)strip_def + 0xe) * 0x13));
                }
                if (*(s8*)((int)strip_def + 0xf) >= 0) {
                    projectile_strip_submit_textured_quad(
                        (int)strip_def,
                        (int)(&DAT_805756f0 + *(s8*)((int)strip_def + 0xf) * 0x13));
                }
            }
        }

next_strip:
        offset += 4;
    }
    FUN_80382720();
}

// Address: 0x80102b8c
// Empty no-op stub. Used as a placeholder callback by weather_system_alloc.
void projectile_strip_stub_nop(void) {
    return;
}

// Address: 0x80102b90
// Resets the projectile strip pool. Zeroes 200 entries of 0x34 bytes each
// at DAT_80575d00 and clears the active strip count (DAT_80611f68).
// Called by weather_system_reset on level transitions.
void projectile_strip_pool_reset(void) {
    int offset = 0;
    for (int i = 200; i != 0; i--) {
        *(u32*)((int)&DAT_80575d00 + offset) = 0;
        offset += 0x34;
    }
    DAT_80611f68 = 0;
}
