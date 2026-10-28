// =============================================================================
// Particle System - Core management, update, and rendering
// Address Range: 0x800cc340 - 0x800ccf84 (batch 278)
// Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "gx.h"
#include "math_util.h"
#include "particle.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

// 19-entry type-linked-list head array (each head points to first particle of that type)
static Particle* g_particle_type_heads[19];           // DAT_80569828

// Per-player 3-slot linked list heads (32 players x 3 bone attachment slots)
static Particle* g_particle_player_heads[32][3];      // DAT_80569874

// Particle pool base pointer and slot count
static Particle* g_particle_pool;                     // DAT_80611dd4
static int       g_particle_pool_size;                // DAT_80611dd0

// Circular allocation cursor
static int       g_particle_cursor;                   // DAT_80611dcc

// Global animation time (wrapping)
static float     g_particle_global_time;              // FLOAT_80611dc8

// Per-type active count (for update_all)
static int       g_particle_type_counts[8];           // DAT_805699f4

// Active particle counter (for weather)
static int       g_particle_weather_count;            // DAT_80569a08

// ---------------------------------------------------------------------------
// Address: 0x800cc498 | Name: particle_stub_nop
// Empty stub (no-op).
// ---------------------------------------------------------------------------
void particle_stub_nop(void)
{
    // intentionally empty
}

// ---------------------------------------------------------------------------
// Address: 0x800cc49c | Name: particle_unlink_from_list
// Removes a particle from its doubly-linked list. If type==5 uses per-player
// 3-slot list; otherwise uses the 19-entry type list. Sets slot type to -1.
// ---------------------------------------------------------------------------
void particle_unlink_from_list(Particle* particle)
{
    if (particle->type_id == -1) {
        return;
    }

    if (particle->prev == NULL) {
        // Head of list — update the list head pointer
        if (particle->type_id == 5) {
            g_particle_player_heads[particle->player_index][particle->slot_index] = particle->next;
        } else {
            g_particle_type_heads[particle->list_index] = particle->next;
        }
    } else {
        // Middle/tail — patch previous node's next pointer
        particle->prev->next = particle->next;
    }

    if (particle->next != NULL) {
        particle->next->prev = particle->prev;
    }

    particle->type_id = -1;
}

// ---------------------------------------------------------------------------
// Address: 0x800cc528 | Name: particle_insert_into_list
// Inserts a particle at the head of its type's linked list. Uses circular
// cursor to pick next slot, evicts existing if occupied.
// Type 5 uses per-player 3-slot list; others use 19-entry type list.
// ---------------------------------------------------------------------------
void particle_insert_into_list(u32 param_1, u32 param_2, int player_index, int slot_index)
{
    u64 type_data = get_particle_type_data(); // FUN_803826e8

    Particle* slot = &g_particle_pool[g_particle_cursor];

    // Evict whatever was in this slot
    particle_unlink_from_list(slot);

    // Store type info
    slot->list_index = (int)(type_data >> 32);  // high word
    slot->type_id = (int)type_data;             // low word
    slot->player_index = player_index;
    slot->slot_index = slot_index;

    // Insert at head of appropriate list
    if ((int)type_data == 5) {
        slot->next = g_particle_player_heads[player_index][slot_index];
        g_particle_player_heads[player_index][slot_index] = slot;
    } else {
        slot->next = g_particle_type_heads[slot->list_index];
        g_particle_type_heads[slot->list_index] = slot;
    }

    slot->prev = NULL;
    if (slot->next != NULL) {
        slot->next->prev = slot;
    }

    // Advance circular cursor
    g_particle_cursor++;
    if (g_particle_cursor >= g_particle_pool_size) {
        g_particle_cursor = 0;
    }

    commit_particle_insert(); // FUN_80382734
}

// ---------------------------------------------------------------------------
// Address: 0x800cc364 | Name: particle_destroy_all
// Destroys all active particles. If no active scene (-1), zeros all list heads
// and resets cursor/time. Otherwise iterates all slots and unlinks active ones.
// ---------------------------------------------------------------------------
void particle_destroy_all(void)
{
    int scene_id = get_active_scene_id(); // FUN_803826f0

    if (scene_id == -1) {
        // Full reset — zero all list heads
        for (int i = 0; i < g_particle_pool_size; i++) {
            Particle* slot = &g_particle_pool[i];
            slot->type_id = -1;
        }

        // Clear type heads
        for (int i = 0; i < 19; i++) {
            g_particle_type_heads[i] = NULL;
        }

        // Clear per-player heads (32 players x 3 slots)
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 3; j++) {
                g_particle_player_heads[i][j] = NULL;
            }
        }

        g_particle_cursor = 0;
        g_particle_global_time = PARTICLE_TIME_INIT; // FLOAT_80614da0
    } else {
        // Selective destroy — only particles matching this scene (or global)
        for (int i = 0; i < g_particle_pool_size; i++) {
            Particle* slot = &g_particle_pool[i];
            if (slot->type_id != -1) {
                if (scene_id < 0 || slot->player_index < 0 || scene_id == slot->player_index) {
                    particle_unlink_from_list(slot);
                }
            }
        }
    }

    commit_particle_destroy_all(); // FUN_8038273c
}

// ---------------------------------------------------------------------------
// Address: 0x800cc340 | Name: particle_system_reset
// Wrapper: calls particle_destroy_all to reset all particle state.
// ---------------------------------------------------------------------------
void particle_system_reset(void)
{
    particle_destroy_all();
}

// ---------------------------------------------------------------------------
// Address: 0x800cc618 | Name: particle_advance_global_time
// Advances global particle animation timer. Wraps at configured period.
// ---------------------------------------------------------------------------
void particle_advance_global_time(void)
{
    float dt = get_frame_delta_time(); // FUN_801310c0

    g_particle_global_time += FLOAT_806108ec * (PARTICLE_TIME_SCALE + (float)dt);

    while (g_particle_global_time >= PARTICLE_WRAP_PERIOD) {
        g_particle_global_time -= PARTICLE_WRAP_PERIOD;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800cc66c | Name: particle_update_weather
// Updates weather/environmental particles (rain, snow). Only runs if camera
// flag bit 2 is set. Uses sin table for lateral wind drift.
// ---------------------------------------------------------------------------
void particle_update_weather(void)
{
    // Save paired-single registers (GC-specific ABI)
    save_ps_registers(); // FUN_803826dc

    int prev_render_mode = set_render_mode(2);  // FUN_8016f00c
    int prev_tex_mode = set_texture_mode(2);    // FUN_8016e8ac

    // Check camera flag for weather enabled
    CameraState* cam = get_active_camera();
    if ((cam->flags & 4) == 0) {
        goto done;
    }

    float speed_scale = WEATHER_SPEED_X;  // FLOAT_80614dc0
    float drift_amp   = WEATHER_DRIFT_Y;  // FLOAT_80614dc4
    float period      = WEATHER_PERIOD;   // FLOAT_80614dc8
    float time_offset = PARTICLE_TIME_SCALE; // FLOAT_80614da4
    float base_offset = WEATHER_BASE;     // FLOAT_80614dbc

    // Generate 2 weather particles per frame
    for (u32 i = 0; i < 2; i++) {
        // Compute normalized time factor
        float screen_height = (float)g_screen_height; // DAT_806109c0
        float frame_count = (float)g_frame_counter;   // DAT_80611928
        float t_norm = frame_count * (period / screen_height);
        float amplitude = drift_amp * t_norm;

        // X position with sin-table lateral drift
        float base_x = g_weather_params[i]; // DAT_80614db4..
        u32 angle_idx = (u32)(speed_scale * (amplitude * base_x) + base_offset);

        // Y position with sin-table wobble
        float sin_val = g_sin_table[angle_idx & 0x7FF]; // DAT_80583228
        float y_pos = speed_scale * (amplitude * g_weather_params[i + 2] +
                      (time_offset * drift_amp * (float)i) * sin_val) + base_offset;
    }

done:
    set_render_mode(prev_render_mode);
    set_texture_mode(prev_tex_mode);
    restore_ps_registers(); // FUN_80382728
}

// ---------------------------------------------------------------------------
// Address: 0x800cc844 | Name: particle_update_bone_attached
// Updates per-player bone-attached particles (type-5 list). Transforms positions
// using skeleton bone matrices with interpolation toward target bone.
// Removes expired particles.
// ---------------------------------------------------------------------------
void particle_update_bone_attached(void)
{
    // Save paired-single registers
    save_ps_registers_extended(); // FUN_803826ec

    u64 type_data = get_current_type_data();
    int player_index = (int)(type_data >> 32);
    int slot_index = (int)type_data;

    Particle* particle = g_particle_player_heads[player_index][slot_index];

    // Screen height for time normalization
    float screen_h = (float)g_screen_height;
    float time_scale = FLOAT_806108ec * (WEATHER_PERIOD / screen_h);

    // Get entity data for bone matrices
    float bone_scale = particle->field_05;
    int entity_data_ptr = g_entity_render_data + (int)particle->field_03 * ENTITY_STRIDE;

    // Build rotation matrix from entity angle
    float angle = (WEATHER_DRIFT_Y * *(float*)(entity_data_ptr + 0x70)) / PARTICLE_ANIM_DIVISOR;
    float rot_matrix[16];
    build_rotation_matrix_y(angle, rot_matrix); // FUN_8012f05c

    // Transform by source bone
    float* bone_src = get_bone_matrix(entity_data_ptr, (int)particle->field_13);
    float result_src[12];
    matrix_multiply_3x4(bone_src, rot_matrix, result_src); // FUN_80130bd4

    // Build full axis rotation matrix
    float full_rot[16];
    build_rotation_matrix_xyz(
        *(float*)(entity_data_ptr + 0x64),
        *(float*)(entity_data_ptr + 0x68),
        *(float*)(entity_data_ptr + 0x6c),
        angle, full_rot); // FUN_8012f2c4

    // Transform by both source and destination bones
    float result_a[12], result_b[12];
    matrix_multiply_3x4(bone_src, full_rot, result_a);

    float* bone_dst = get_bone_matrix(entity_data_ptr, (int)particle->field_14);
    matrix_multiply_3x4(bone_dst, full_rot, result_b);

    // Walk linked list, update each particle
    while (particle != NULL) {
        Particle* next = particle->next;

        // Advance age
        particle->age += time_scale;

        // Check expiry
        if (particle->age / particle->lifetime < 0.0f ||
            particle->age / particle->lifetime >= WEATHER_PERIOD) {
            particle_unlink_from_list(particle);
            particle = next;
            continue;
        }

        g_particle_weather_count++;

        // Transform offset by source bone matrix
        float offset[3] = { particle->field_15, particle->field_17, 0.0f };
        float world_pos[3];
        transform_point_by_matrix(result_src, offset, world_pos); // FUN_8012fb90

        float base_x = world_pos[0] + offset[0];
        float base_y = world_pos[1] + offset[1];
        float base_z = world_pos[2] + offset[2];

        if ((int)particle->field_14 < 0) {
            // Single-bone mode: offset + velocity drift
            float vel_offset[3] = { particle->field_0a, particle->field_0b, particle->field_0c };
            float gravity = (float)g_particle_gravity_table[(int)bone_scale] * time_scale;
            float interp_a = WEATHER_BASE * particle->field_16 + PARTICLE_INTERP_BIAS;
            float interp_b = WEATHER_BASE * (WEATHER_PERIOD - particle->field_16) + PARTICLE_INTERP_BIAS;

            float target[3];
            transform_point_by_matrix(result_src, vel_offset, target);

            float weight = particle->field_16;
            float px = weight * (vel_offset[0] + offset[0]) + base_x;
            float py = weight * (vel_offset[1] + offset[1]) + base_y;
            float pz = weight * (vel_offset[2] + offset[2]) + base_z;

            if ((particle->flags & 1) == 0) {
                particle->pos_x = px;
                particle->pos_y = py;
                particle->pos_z = pz;
            } else {
                // Blended with previous position (smoothing)
                particle->pos_x = px * interp_a + (particle->pos_x + gravity) * interp_b;
                particle->pos_y = py * interp_a + (particle->pos_y + gravity) * interp_b;
                particle->pos_z = pz * interp_a + (particle->pos_z + gravity) * interp_b;
            }
        } else {
            // Dual-bone mode: interpolate between source and dest bone
            float dest_pos[3] = {
                result_b[9] + offset[0],
                result_b[10] + offset[1],
                result_b[11] + offset[2]
            };

            float weight = particle->field_16;
            particle->pos_x = weight * (dest_pos[0] - base_x) + base_x;
            particle->pos_y = weight * (dest_pos[1] - base_y) + base_y;
            particle->pos_z = weight * (dest_pos[2] - base_z) + base_z;
        }

        particle->field_16 += time_scale;
        particle->flags |= 1;
        particle = next;
    }

    restore_ps_registers_extended(); // FUN_80382738
}

// ---------------------------------------------------------------------------
// Address: 0x800ccc04 | Name: particle_update_all
// Main per-frame particle update. Iterates all 19 type lists: advances age,
// removes expired, applies gravity (per-type table), integrates velocity.
// Also dispatches bone-attached update for active players.
// ---------------------------------------------------------------------------
void particle_update_all(void)
{
    begin_particle_update(); // FUN_803826d8

    // Compute time scale (normalized to screen height)
    float screen_h = (float)g_screen_height;
    float time_scale = FLOAT_806108ec * (WEATHER_PERIOD / screen_h);

    if (g_particle_system_enabled == 0) { // DAT_806108e4
        goto done;
    }

    // Clear per-type active counts
    for (int i = 0; i < 8; i++) {
        g_particle_type_counts[i] = 0;
    }

    // Iterate all 19 type lists
    for (int type_idx = 0; type_idx < 19; type_idx++) {
        Particle* particle = g_particle_type_heads[type_idx];
        int gravity_offset = type_idx * GRAVITY_TABLE_STRIDE;

        while (particle != NULL) {
            Particle* next = particle->next;

            // Advance age
            particle->age += time_scale;
            float t_norm = particle->age / particle->lifetime;

            // Check expiry
            if (t_norm < 0.0f || t_norm >= WEATHER_PERIOD) {
                particle_unlink_from_list(particle);
                particle->field_06 = -NAN; // mark dead
                particle = next;
                continue;
            }

            // Count active by sub-type
            g_particle_type_counts[(int)particle->field_06]++;

            int move_type = (int)particle->field_06;

            if (move_type == 0 || (move_type >= 1 && move_type < 3)) {
                // Type 0-2: velocity + gravity integration
                int entity_idx = g_entity_render_data + (int)particle->field_03 * ENTITY_STRIDE;

                float wind_factor = -(WEATHER_BASE * t_norm - WEATHER_PERIOD);
                if (entity_idx != 0) {
                    float* entity_vel = (float*)(entity_idx + 0xF8);
                    particle->pos_x += FLOAT_806108ec * wind_factor * entity_vel[0]
                                     + particle->vel_x * time_scale;
                    particle->pos_y += FLOAT_806108ec * wind_factor * entity_vel[1]
                                     + (t_norm * g_particle_gravity_table[type_idx] * t_norm
                                        + particle->vel_y) * time_scale;
                    particle->pos_z += FLOAT_806108ec * wind_factor * entity_vel[2]
                                     + particle->vel_z * time_scale;
                }
            } else if (move_type == 4) {
                // Type 4: no gravity, pure velocity
                particle->pos_x += particle->vel_x * time_scale;
                particle->pos_y += (t_norm * g_particle_gravity_table[type_idx] * t_norm
                                    + particle->vel_y) * time_scale;
                particle->pos_z += particle->vel_z * time_scale;
            } else if (move_type == 6) {
                // Type 6: accumulator mode (velocity integrates into secondary pos)
                particle->field_15 += particle->vel_x * time_scale;
                particle->field_16 += particle->vel_y * time_scale;
                particle->field_17 += particle->vel_z * time_scale;
                particle->pos_x = particle->field_15;
                particle->pos_y = particle->field_16;
                particle->pos_z = particle->field_17;
            }

            particle->flags |= 1; // mark as updated this frame
            particle = next;
        }
    }

    // Dispatch bone-attached updates for active players
    for (int player = 0; player < g_active_player_count; player++) { // DAT_80612098
        if (g_player_entity_ptrs[player] != NULL) { // DAT_80588610
            for (int slot = 0; slot < 3; slot++) {
                int player_entity_index = *(int*)(g_player_entity_ptrs[player] + 8);
                if (g_particle_player_heads[player_entity_index][slot] != NULL) {
                    particle_update_bone_attached();
                }
            }
        }
    }

done:
    end_particle_update(); // FUN_80382724
}

// ---------------------------------------------------------------------------
// Address: 0x800ccf84 | Name: particle_render_list
// Renders a particle linked list as camera-facing billboarded quads via GX FIFO.
// Handles rotation (sin/cos table), per-particle scaling, animated texture frames,
// color/alpha, fog integration, and z-distance culling.
// ---------------------------------------------------------------------------
void particle_render_list(u32 param_1, u32 param_2, Particle* list_head, u32 render_flags)
{
    // Save paired-single registers
    save_ps_registers(); // FUN_803826dc

    // Determine if scene allows particle rendering
    bool scene_visible = false;
    if ((*(u32*)(g_scene_state + 0x21C) & 4) != 0) {
        if (g_active_camera_entity == NULL ||
            (g_active_camera_entity->type != 0x80 &&
             g_active_camera_entity->type != 0x10000 &&
             g_active_camera_entity->type != 0x100)) {
            scene_visible = true;
        }
    }

    int type_data_offset = (int)param_1 * PARTICLE_TYPE_TABLE_STRIDE; // 0xEC per type
    int prev_render_mode = -1;

    // Set render mode if type defines one
    int type_render_mode = g_particle_type_table[type_data_offset + RENDER_MODE_OFFSET];
    if (type_render_mode != -1) {
        prev_render_mode = set_render_mode(type_render_mode);
    }

    // Setup keyframe color table pointer
    int keyframe_count = g_particle_type_table[type_data_offset + KEYFRAME_COUNT_OFFSET];
    float* keyframe_table = NULL;
    if (keyframe_count > 0) {
        keyframe_table = &g_particle_type_table[type_data_offset + KEYFRAME_DATA_OFFSET];
    }

    // Setup alpha blending if type uses additive
    if ((g_particle_type_table[type_data_offset + FLAGS_OFFSET] & 1) != 0) {
        gx_set_blend_mode_additive();       // FUN_8001c868
        gx_set_alpha_compare(1.0f, 0.0f, 0.0f); // FUN_8001c844
        gx_commit_blend();                  // FUN_8001c8ec
    }

    // Walk the particle list
    for (Particle* p = list_head; p != NULL; p = p->next) {
        u32 flags = (u32)p->flags;

        // Skip if not updated this frame
        if ((flags & 1) == 0) continue;

        // Visibility/layer filtering
        bool should_render = false;
        if (flags & 8) {
            should_render = true;
        } else if ((flags & 4) != 0 && ((flags & render_flags) == 0 || !scene_visible)) {
            should_render = true;
        } else if ((flags & 0x10) != 0 && (flags & render_flags) != 0) {
            should_render = true;
        } else if (((render_flags | 2) == ((render_flags | 2) & flags)) && scene_visible) {
            should_render = true;
        }

        if (!should_render) continue;
        if (p->age < 0.0f) continue;

        // Check room visibility
        if ((flags & 2) == 0) {
            if (g_room_visibility[(int)p->room_id] == 0) continue;
        }

        // Compute normalized time
        float t_norm = p->age / p->lifetime;
        u32 color_packed = 0;

        // Interpolate keyframe colors
        if (keyframe_table != NULL) {
            int num_keys = keyframe_count - 1;
            float key_t = t_norm * (float)num_keys;
            u32 key_idx = (u32)key_t;
            float frac = key_t - (float)key_idx;
            float inv_frac = 1.0f - frac;

            float* key_a = &keyframe_table[key_idx * 4];
            float* key_b = &keyframe_table[(key_idx + 1) * 4];

            u32 r = (u32)(inv_frac * key_a[1] + frac * key_b[1]);
            u32 g = (u32)(inv_frac * key_a[2] + frac * key_b[2]);
            u32 b = (u32)(inv_frac * key_a[0] + frac * key_b[0]);
            u32 a = (u32)(inv_frac * key_a[3] + frac * key_b[3]);

            color_packed = (a << 24) | (g << 16) | b | (r << 8);
        }

        // Animated texture frame selection
        if (g_particle_type_table[type_data_offset + ANIM_FRAME_COUNT_OFFSET] > 0) {
            int num_frames = g_particle_type_table[type_data_offset + ANIM_FRAME_COUNT_OFFSET] - 1;
            int frame_idx = (int)(t_norm * (float)num_frames);
            int tex_id = sprite_get_texture_id(
                g_particle_type_table[type_data_offset + ANIM_BASE_TEX_OFFSET] + frame_idx);
            set_particle_texture(tex_id); // FUN_8016e77c
        }

        // Transform particle position to camera space
        float cam_pos[4]; // x, y, z, w (w = distance from camera)
        transform_point_to_camera(param_1, p, &cam_pos[0]); // FUN_8012fb70

        float cam_dist = cam_pos[3];

        // Distance culling
        if (cam_pos[0] < -cam_dist || cam_pos[0] > cam_dist) continue;
        if (cam_pos[1] < -cam_dist || cam_pos[1] > cam_dist) continue;
        if (cam_pos[2] < -cam_dist || cam_pos[2] > cam_dist) continue;
        if (cam_dist < PARTICLE_MIN_DRAW_DISTANCE) continue;

        // Compute screen-space size from world size + distance
        float world_size = (g_viewport_scale * (p->size_scale * t_norm + p->size_base)) / cam_dist;
        if (world_size > p->max_size) {
            world_size = p->max_size;
        }

        // Project to screen
        float proj_factor = 1.0f / cam_dist;
        cam_pos[0] *= proj_factor;
        cam_pos[1] *= proj_factor;
        cam_pos[2] = cam_pos[2] * proj_factor + 1.0f;

        // Rotation via sin/cos lookup table
        u32 angle = (u32)(PARTICLE_ROT_SCALE * p->rotation * (1.0f + t_norm) + PARTICLE_ROT_BASE);
        float sin_val = g_sin_table[angle & 0x7FF];
        float cos_val = g_sin_table[(angle + 0x200) & 0x7FF];

        float half_sin = world_size * sin_val;
        float half_cos = world_size * cos_val;

        // Screen-space quad corners
        float cx = cam_pos[0] * g_screen_proj_x + g_screen_offset_x;
        float cy = cam_pos[1] * g_screen_proj_y + g_screen_offset_y;
        float z  = p->z_bias * g_z_scale + (1.0f + cam_pos[2]);

        // Distance fog
        if (g_particle_type_table[type_data_offset + FOG_OFFSET] > 0.0f) {
            apply_particle_fog(cam_pos[2], g_particle_type_table[type_data_offset + FOG_OFFSET]);
        }

        // Submit quad if render mode allows
        if (type_render_mode != -1) {
            // Byte-swap color for GX FIFO (ARGB -> endian-swapped)
            u32 color_gx = __builtin_bswap32(color_packed);

            // Emit 4 vertices to GX CP FIFO (billboarded quad)
            GX_Begin(GX_QUADS, 0, 4); // FUN_80172818(0x98, 0, 4)

            // Vertex 0: bottom-left
            GX_FIFO_F32 = cx - half_cos;
            GX_FIFO_F32 = cy - half_sin;
            GX_FIFO_F32 = z;
            GX_FIFO_U32 = color_gx;
            GX_FIFO_F32 = 0.0f;
            GX_FIFO_F32 = 0.0f;

            // Vertex 1: bottom-right
            GX_FIFO_F32 = cx + half_sin;
            GX_FIFO_F32 = cy - half_cos;
            GX_FIFO_F32 = z;
            GX_FIFO_U32 = color_gx;
            GX_FIFO_F32 = 0.0f;
            GX_FIFO_F32 = 1.0f;

            // Vertex 2: top-right
            GX_FIFO_F32 = cx - half_sin;
            GX_FIFO_F32 = cy + half_cos;
            GX_FIFO_F32 = z;
            GX_FIFO_U32 = color_gx;
            GX_FIFO_F32 = 1.0f;
            GX_FIFO_F32 = 0.0f;

            // Vertex 3: top-left
            GX_FIFO_F32 = cx + half_cos;
            GX_FIFO_F32 = cy + half_sin;
            GX_FIFO_F32 = z;
            GX_FIFO_U32 = color_gx;
            GX_FIFO_F32 = 1.0f;
            GX_FIFO_F32 = 1.0f;

            g_vertex_count += 4;
            g_tri_count += 2;
            g_quad_count += 2;
        }
    }

    // Restore alpha blend if modified
    if ((g_particle_type_table[type_data_offset + FLAGS_OFFSET] & 1) != 0) {
        gx_restore_blend_mode(); // FUN_8001c898
        gx_commit_blend();       // FUN_8001c8ec
    }

    restore_ps_registers(); // FUN_80382728
}
