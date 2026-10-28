// Address: 0x800eaa64-0x800eba88 | Batch 292 | Date: 2026-08-25
// Particle rendering (GX submission), blood decal system, zone lighting
// Subsystem: game/particle (graphics-adjacent)

#include "common.h"
#include "particle.h"
#include "collision.h"
#include "entity.h"
#include "math_util.h"
#include "gx.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern u8    g_particle_bucket_table[];      // DAT_8056f008 — bucket heads
extern int   g_particle_pool_base;           // DAT_80611e40
extern int   g_particle_special_bucket_idx;  // DAT_80611e4c — special group index

extern int   g_blood_decal_ring[];           // DAT_8056e418 — 50-entry ring (0x3C stride)
extern int   g_blood_decal_write;            // DAT_80611e58 — write cursor
extern int   g_blood_decal_count;            // DAT_80611e54 — active count (max 50)

extern int   g_zone_light_data_ptr;          // DAT_80611e68 — per-zone light alloc (0x28 per zone)
extern int   g_zone_light_def_ptr;           // DAT_80611e78 — zone definition table (0x1C per zone)
extern int   g_zone_light_dirty;             // DAT_80611e6c — dirty flag

extern float g_zone_light_r[];              // DAT_8056ffe0 — 213 entries
extern float g_zone_light_g[];              // DAT_8056ffe4
extern float g_zone_light_b[];              // DAT_8056ffe8

extern int   g_zone_count;                  // DAT_80611b14
extern int   g_particle_env_mode;           // DAT_80612614

extern u32   g_render_flags;                // DAT_805015c0
extern int   g_camera_data;                 // DAT_80611d54
extern int   g_camera_matrix_ptr;           // DAT_80611748

extern u32   g_weather_flags_a;             // DAT_806125fc
extern u32   g_weather_flags_b;             // DAT_80612600
extern int   g_level_mode;                  // DAT_80612614
extern int   g_game_mode;                   // DAT_805cc614

extern volatile u32 GX_FIFO;               // DAT_cc008000 — GX command FIFO

// ---------------------------------------------------------------------------
// Address: 0x800eaa64 | particle_render_quad
// Render a single particle quad to GX hardware. Computes final RGBA
// (alpha * opacity * 255), applies zone lighting if flag bit 0 set, applies
// projectile fade (cosine curve) if flag bit 2. Submits vertices with
// position (3 floats) + RGBA (byte-swapped) + UV (2 shorts scaled).
// ---------------------------------------------------------------------------
void particle_render_quad(int particle) {
    // Compute final alpha: opacity (0xA4) * base_alpha (0xA0) * 255
    u32 alpha = (u32)(*(float*)(particle + 0xA4) * 255.0f * *(float*)(particle + 0xA0));
    u32 color = *(u32*)(particle + 0xA8) | (alpha << 24);

    u16 flags = *(u16*)(particle + 0x94);

    if ((flags & 4) == 0) {
        // Non-projectile: optional grow-in fade (flag bit 1)
        if ((flags & 2) != 0) {
            float grow_factor = 2.0f * *(float*)(particle + 0xB0); // FLOAT_80615170
            if (grow_factor > 1.0f) grow_factor = 1.0f;
            // Modulate alpha by grow factor
            u32 base_alpha = alpha & 0xFF;
            color = ((u32)(grow_factor * (float)base_alpha) << 24) |
                    (*(u32*)(particle + 0xA8) & 0x00FFFFFF);
        }
    } else {
        // Projectile-attached: cosine fade curve
        float t = *(float*)(particle + 0xB0) / 0.5f; // FLOAT_80615168
        if (t < 1.0f) {
            // Cosine fade: cos(PI * t - PI) mapped to [0..255]
            float cos_val = cosf(3.14159f * t - 3.14159f); // FLOAT_8061516c
            int fade_alpha = (int)-(255.0f * cos_val - 255.0f);
            gx_set_fog_alpha(7, fade_alpha, 0, 0, 2, 0);
        }
    }

    // Apply zone lighting to modulate color
    if ((flags & 1) != 0) {
        color = particle_apply_zone_lighting();
    }

    // Only render if alpha > 0
    if ((color & 0xFF000000) != 0) {
        // Begin GX primitive: quads, vertex format 0x92, vertex_count
        u8 vertex_count = *(u8*)(particle + 0x98);
        s16 texture_id = *(s16*)(particle + 0x96);
        gx_begin_primitive(2, 0x92, 0, vertex_count, texture_id);

        // Submit each vertex
        int uv_offset = 0;
        int pos_offset = 0;
        for (int i = 0; i < vertex_count; i++) {
            u32* pos = (u32*)(particle + pos_offset);

            // Position: 3 floats
            GX_FIFO = pos[0];
            GX_FIFO = pos[1];
            GX_FIFO = pos[2];

            // Color: RGBA byte-swapped (ABGR -> RGBA for GX)
            GX_FIFO = (color << 24) | ((color & 0xFF00) << 8) |
                       ((color >> 8) & 0xFF00) | (color >> 24);

            // UV: 2 shorts at offset 0x60+, scaled by UV_SCALE
            s16 u = *(s16*)(particle + 0x60 + uv_offset);
            s16 v = *(s16*)(particle + 0x62 + uv_offset);
            GX_FIFO = *(u32*)&(float)(UV_SCALE * (float)u);
            GX_FIFO = *(u32*)&(float)(UV_SCALE * (float)v);

            uv_offset += 4;
            pos_offset += 0xC;
        }

        gx_end_primitive();
    }

    // Restore blend mode if projectile type
    if ((flags & 4) != 0) {
        gx_set_blend_mode(2);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800eac84 | particle_render_bucket
// Render all particles in a given priority bucket. Sets up perspective
// matrix, configures GX blend mode. Iterates bucket linked list: for
// non-attached uses camera inverse; for attached resolves bone matrix.
// Called from scene_render_layer_multipass.
// ---------------------------------------------------------------------------
void particle_render_bucket(void) {
    u64 ctx = particle_render_context_begin();

    // Skip if global render flag blocks particles
    if ((g_render_flags & 0x20000000) != 0) {
        particle_render_context_end();
        return;
    }

    gx_setup_particle_blend();

    int group_index = (int)(ctx >> 32);
    u32 bucket_id = (u32)ctx & 0xFFFF;
    int bucket_offset = group_index * 4 + bucket_id * 0x354;

    if (*(int*)(&g_particle_bucket_table[bucket_offset]) == 0) {
        particle_render_context_end();
        return;
    }

    // Compute perspective projection from camera FOV/aspect
    float fov_scale = *(float*)(g_camera_matrix_ptr + 0xC);
    float aspect_x = *(float*)(g_camera_matrix_ptr + 0x20);
    // ... (projection matrix setup)
    float proj_matrix[16];
    build_particle_projection(fov_scale, aspect_x, proj_matrix);
    gx_load_projection(proj_matrix);
    gx_set_blend_mode(2);

    float* last_bone_matrix = NULL;

    // Iterate all particles in this bucket
    for (int particle = *(int*)(&g_particle_bucket_table[bucket_offset]);
         particle != 0;
         particle = *(int*)(particle + 0x80))
    {
        int attached_entity = *(int*)(particle + 0x9C);

        if (attached_entity == 0) {
            // World-space particle — use camera inverse matrix
            float* cam_inv = *(float**)(g_camera_data + 0x208);
            if (last_bone_matrix != cam_inv) {
                mat4_invert_to_global(cam_inv);
                last_bone_matrix = cam_inv;
            }
            gx_set_texture((u32)*(u16*)(particle + 0xAC));
            particle_render_quad(particle);
        } else {
            // Attached particle — resolve bone matrix
            int* mesh_ptr = NULL;
            u8 sub_index = *(u8*)(particle + 0x9B);
            if (sub_index != 0) {
                mesh_ptr = *(int**)(*(int*)(attached_entity + 4) + 4);
                mesh_ptr = mesh_ptr + *mesh_ptr * -0x30 + (u32)sub_index * 0x30;
            }

            // Face-group mask check
            if (mesh_ptr != NULL) {
                if (mesh_ptr[0x14] != 0 &&
                    (*(u32*)(*(int*)(attached_entity + 4) + 0x30) & mesh_ptr[0x14]) == 0) {
                    continue; // skip — material group not visible
                }
            }

            // Get bone transform matrix
            int bone_data = *(int*)(*(int*)(attached_entity + 4) + 0x20);
            if (bone_data != 0) {
                s8 bone_id = *(s8*)(particle + 0x9A);
                int abs_bone_id = (bone_id < 0) ? -bone_id : bone_id;
                float* bone_mat = (float*)(bone_data + abs_bone_id * 0x40);

                if (last_bone_matrix != bone_mat) {
                    // Copy bone matrix to global render state
                    memcpy(&g_particle_bone_matrix, bone_mat, 64);
                    g_bone_matrix_dirty = 1;
                    g_bone_matrix_index = 0;
                    last_bone_matrix = bone_mat;
                }

                gx_set_texture((u32)*(u16*)(particle + 0xAC));
                particle_render_quad(particle);
            }
        }
    }

    // Restore default projection
    gx_load_projection(*(float**)(g_camera_data + 0x204));

    particle_render_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800eaf34 | particle_render_special_bucket
// Render all particles in the special bucket (DAT_80611e4c). Same setup as
// particle_render_bucket but for the special group. Also calls
// particle_blood_decal_render at end. Called from scene_render_pipeline.
// ---------------------------------------------------------------------------
void particle_render_special_bucket(void) {
    particle_render_special_context_begin();

    if ((g_render_flags & 0x20000000) != 0) {
        particle_render_special_context_end();
        return;
    }

    gx_setup_particle_blend();

    if (*(int*)(&g_particle_bucket_table[g_particle_special_bucket_idx * 4]) == 0) {
        particle_render_special_context_end();
        return;
    }

    // Setup projection (same as particle_render_bucket)
    float proj_matrix[16];
    build_particle_projection_from_camera(proj_matrix);
    gx_load_projection(proj_matrix);
    int prev_blend = gx_set_blend_mode(2);

    float* last_bone_matrix = NULL;

    // Iterate special bucket list
    for (int particle = *(int*)(&g_particle_bucket_table[g_particle_special_bucket_idx * 4]);
         particle != 0;
         particle = *(int*)(particle + 0x80))
    {
        int attached_entity = *(int*)(particle + 0x9C);

        if (attached_entity == 0) {
            float* cam_inv = *(float**)(g_camera_data + 0x208);
            if (last_bone_matrix != cam_inv) {
                mat4_invert_to_global(cam_inv);
                last_bone_matrix = cam_inv;
            }
            gx_set_texture((u32)*(u16*)(particle + 0xAC));
            particle_render_quad(particle);
        } else {
            int bone_data = *(int*)(*(int*)(attached_entity + 4) + 0x20);
            if (bone_data != 0) {
                s8 bone_id = *(s8*)(particle + 0x9A);
                int abs_bone_id = (bone_id < 0) ? -bone_id : bone_id;
                float* bone_mat = (float*)(bone_data + abs_bone_id * 0x40);

                if (last_bone_matrix != bone_mat) {
                    memcpy(&g_particle_bone_matrix, bone_mat, 64);
                    g_bone_matrix_dirty = 1;
                    g_bone_matrix_index = 0;
                    last_bone_matrix = bone_mat;
                }

                gx_set_texture((u32)*(u16*)(particle + 0xAC));
                particle_render_quad(particle);
            }
        }
    }

    gx_load_projection(*(float**)(g_camera_data + 0x204));
    gx_set_blend_mode(prev_blend);

    // Render blood decals on top
    particle_blood_decal_render();

    particle_render_special_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800eb1a4 | particle_blood_decal_create
// Create a blood decal attached to a character mesh bone. Raycasts the
// player/AI mesh, finds bone ID (with 2-level parent bone fallback),
// transforms hit position+direction to bone-local space, stores in 50-entry
// ring buffer (stride 0x3C at DAT_8056e418). Max 50 active decals.
// Called from player_ai_impact_dispatch.
// ---------------------------------------------------------------------------
void particle_blood_decal_create(u32 param_1, u32 param_2, float* direction) {
    u64 ctx = particle_blood_context_begin();
    int entity = (int)(ctx >> 32);
    float* hit_point = (float*)ctx;

    int* mesh_ptr = *(int**)(*(int*)(entity + 4) + 4);
    int primary = *mesh_ptr;

    // Check if entity already has max decals, or mode is at limit
    if (*(int*)(entity + 0x150) != 0) {
        particle_blood_context_end();
        return;
    }
    if (g_blood_decal_count >= 50) { // DAT_80611e54 != 0x32
        particle_blood_context_end();
        return;
    }

    // Store original hit position
    float pos[3] = { hit_point[0], hit_point[1], hit_point[2] };
    float dir[3] = { direction[0], direction[1], direction[2] };
    float neg_dir[3] = { -dir[0], -dir[1], -dir[2] };

    // Raycast to find hit point on mesh
    float ray_end[3] = { pos[0] + dir[0], pos[1] + dir[1], pos[2] + dir[2] };
    player_ai_mesh_raycast(entity, ray_end, neg_dir, NULL, 1, pos, dir, 0);

    // Resolve bone ID with 2-level parent fallback
    int active_idx = player_ai_get_active_index();
    int bone_id = (int)*(s8*)((int)mesh_ptr + active_idx * 0xC0 + primary * -0xC0 + 0x3D);

    if (bone_id < 0) {
        // Try parent bone level 1
        active_idx = player_ai_get_active_index();
        s8 parent1 = *(s8*)((int)mesh_ptr + active_idx * 0xC0 + primary * -0xC0 + 0x3E);
        if (parent1 >= 0) {
            active_idx = player_ai_get_active_index();
            bone_id = (int)*(s8*)((int)mesh_ptr + parent1 * 0xC0 + primary * -0xC0 + 0x3D);
        }
    }
    if (bone_id < 0) {
        // Try parent bone level 2
        active_idx = player_ai_get_active_index();
        s8 parent2 = *(s8*)((int)mesh_ptr + active_idx * 0xC0 + primary * -0xC0 + 0x3F);
        if (parent2 >= 0) {
            active_idx = player_ai_get_active_index();
            bone_id = (int)*(s8*)((int)mesh_ptr + parent2 * 0xC0 + primary * -0xC0 + 0x3D);
        }
    }

    // Transform to entity-local space
    pos[0] -= *(float*)(entity + 0x64);
    pos[1] -= *(float*)(entity + 0x68);
    pos[2] -= *(float*)(entity + 0x6C);

    float local_matrix[16];
    entity_build_local_matrix(local_matrix, entity, 0, 0);
    mat3_transform_point(local_matrix, pos);
    mat3_transform_point(local_matrix, dir);

    // Transform by bone inverse
    float* bone_mat = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + bone_id * 0x40);
    pos[0] -= bone_mat[12];
    pos[1] -= bone_mat[13];
    pos[2] -= bone_mat[14];
    mat3_transform_point(bone_mat, pos);
    mat3_transform_point(bone_mat, dir);

    // Store in ring buffer
    int slot_offset = g_blood_decal_write * 0x3C;
    g_blood_decal_write++;
    g_blood_decal_count++;

    *(int*)(&g_blood_decal_ring[slot_offset + 0x00]) = bone_id;
    *(int*)(&g_blood_decal_ring[slot_offset + 0x04]) = entity;
    *(int*)(&g_blood_decal_ring[slot_offset + 0x38]) = 0; // age = 0
    *(float*)(&g_blood_decal_ring[slot_offset + 0x08]) = pos[0];
    *(float*)(&g_blood_decal_ring[slot_offset + 0x0C]) = pos[1];
    *(float*)(&g_blood_decal_ring[slot_offset + 0x10]) = pos[2];
    *(float*)(&g_blood_decal_ring[slot_offset + 0x14]) = dir[0];
    *(float*)(&g_blood_decal_ring[slot_offset + 0x18]) = dir[1];
    *(float*)(&g_blood_decal_ring[slot_offset + 0x1C]) = dir[2];

    if (g_blood_decal_write > 49) {
        g_blood_decal_write = 0;
    }

    particle_blood_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800eb440 | particle_blood_decal_age
// Age blood decals in the ring buffer. Accumulates frame time per entry,
// removes expired ones (age >= max_lifetime * frame_counter). Iterates the
// circular buffer accounting for wrap-around. Called from particle_system_tick.
// ---------------------------------------------------------------------------
void particle_blood_decal_age(void) {
    particle_blood_age_context_begin();

    float max_lifetime = 0.4f; // FLOAT_80615144
    int frame_time = g_frame_time_raw; // DAT_806108e4
    u32 frame_counter = g_frame_counter; // DAT_806109c0

    int sentinel = 0;
    BOOL looping = TRUE;

    while (looping) {
        int start = g_blood_decal_write - g_blood_decal_count;
        int end = g_blood_decal_write;

        if (start < 0) {
            if (sentinel == -1) {
                looping = FALSE;
            } else {
                end = 50; // 0x32
            }
            sentinel = start + 50;
        } else {
            looping = FALSE;
            sentinel = start;
        }

        int count = end - sentinel;
        int offset = sentinel * 0x3C;

        for (int i = 0; i < count; i++) {
            // Accumulate age
            *(int*)(&g_blood_decal_ring[offset + 0x38]) += frame_time;

            // Check if expired
            float age = (float)(*(int*)(&g_blood_decal_ring[offset + 0x38]));
            float max_age = max_lifetime * (float)frame_counter;

            if (max_age <= age) {
                // Remove entry
                *(int*)(&g_blood_decal_ring[offset + 0x00]) = -1;
                *(int*)(&g_blood_decal_ring[offset + 0x04]) = 0;
                *(int*)(&g_blood_decal_ring[offset + 0x38]) = -1;
                *(float*)(&g_blood_decal_ring[offset + 0x08]) = 0.0f;
                *(float*)(&g_blood_decal_ring[offset + 0x0C]) = 0.0f;
                *(float*)(&g_blood_decal_ring[offset + 0x10]) = 0.0f;
                g_blood_decal_count--;
            }

            offset += 0x3C;
        }

        if (looping) {
            sentinel = -1;
        }
    }

    particle_blood_age_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800eb584 | particle_blood_decal_render
// Render blood decals from the ring buffer. Sets up camera inverse matrix,
// iterates buffer, removes entries whose entity has been destroyed (flag
// 0x80000000 at +0x28). Submits remaining for rendering via GX.
// ---------------------------------------------------------------------------
void particle_blood_decal_render(void) {
    particle_blood_render_context_begin();

    int sentinel = 0;
    BOOL looping = TRUE;

    if (g_blood_decal_count <= 0) {
        particle_blood_render_context_end();
        return;
    }

    // Setup camera inverse matrix for world-space rendering
    float* cam_inv = *(float**)(g_camera_data + 0x208);
    mat4_invert_to_global(cam_inv);
    g_bone_matrix_dirty = 1;
    g_bone_matrix_index = 0;

    int prev_blend = gx_set_blend_mode(2);
    gx_set_texture(2);

    while (looping) {
        int start = g_blood_decal_write - g_blood_decal_count;
        int end = g_blood_decal_write;

        if (start < 0) {
            if (sentinel == -1) {
                looping = FALSE;
            } else {
                end = 50;
            }
            sentinel = start + 50;
        } else {
            looping = FALSE;
            sentinel = start;
        }

        int count = end - sentinel;
        int offset = sentinel * 0x3C;

        for (int i = 0; i < count; i++) {
            int entity = *(int*)(&g_blood_decal_ring[offset + 0x04]);

            // Remove if entity has been destroyed
            if (entity != 0 && (*(u32*)(entity + 0x28) & 0x80000000) != 0) {
                *(int*)(&g_blood_decal_ring[offset + 0x00]) = -1;
                *(int*)(&g_blood_decal_ring[offset + 0x04]) = 0;
                *(int*)(&g_blood_decal_ring[offset + 0x38]) = -1;
                *(float*)(&g_blood_decal_ring[offset + 0x08]) = 0.0f;
                *(float*)(&g_blood_decal_ring[offset + 0x0C]) = 0.0f;
                *(float*)(&g_blood_decal_ring[offset + 0x10]) = 0.0f;
                g_blood_decal_count--;
            }

            offset += 0x3C;
        }

        if (looping) {
            sentinel = -1;
        }
    }

    gx_set_blend_mode(prev_blend);

    particle_blood_render_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800eb6d4 | particle_is_wet_blood_enabled
// Check if wet blood effect is enabled. Returns 1 if weather flag bit 5 is
// set in both g_weather_flags_a and g_weather_flags_b, AND the current level
// is not indoor (mode != 101) or game mode is 5. Used by
// particle_resolve_sprite_params and particle_spawn_attached_billboard to
// select wet vs dry blood sprites.
// ---------------------------------------------------------------------------
int particle_is_wet_blood_enabled(void) {
    u32 weather_bits = get_weather_capability_flags(); // FUN_801c0894
    if ((g_weather_flags_a & g_weather_flags_b & weather_bits & 0x20) != 0) {
        if (g_level_mode != 101 || g_game_mode == 5) {
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x800eb740 | particle_zone_light_update
// Update per-zone particle lighting data. Reads lighting state from zone
// definition, extracts packed RGB from color field, converts to float [0..1]
// range, writes to g_zone_light_r/g/b arrays. Called per-zone from the
// lighting system (FUN_8034a604, FUN_8034d090, FUN_8034d11c).
// ---------------------------------------------------------------------------
void particle_zone_light_update(void) {
    u64 ctx = particle_zone_update_context_begin();
    u8* light_data = (u8*)(ctx >> 32);
    int zone_id = (int)ctx;

    // Read lighting parameters from zone definition
    int* zone_def = (int*)(g_zone_light_def_ptr + (zone_id - 1) * 0x1C);
    int* zone_state = (int*)(g_zone_light_data_ptr + zone_id * 0x28);

    // Update zone state from light source
    light_source_evaluate(light_data, zone_def);
    *zone_state = *zone_def;

    int type = *zone_def;
    if (type == 0) {
        // Static light — use default params
        zone_def[1] = (int)1.0f;   // FLOAT_80615180
        zone_def[2] = (int)0.5f;   // FLOAT_80615184
    } else if (type == 1) {
        // Dynamic light — copy color from source
        zone_state[2] = *(int*)(light_data + 0xC);
    } else if (type == 2) {
        // Same as type 1
        zone_state[2] = *(int*)(light_data + 0xC);
    } else if (type == 3) {
        // Pulsing light — use oscillation params
        zone_state[2] = (int)0.7f;  // FLOAT_80615188
        zone_state[3] = (int)0.8f;  // FLOAT_8061518c
        zone_def[1] = (int)0.7f;
        zone_def[2] = (int)0.8f;
    }

    // Extract RGB from packed color and write to per-zone arrays
    int next_zone_offset = g_zone_light_def_ptr + zone_id * 0x1C;
    u32 packed_color = *(u32*)(next_zone_offset - 8);

    g_zone_light_r[zone_id * 3] = (float)((packed_color >> 24) & 0xFF) / 255.0f; // FLOAT_80615190
    g_zone_light_g[zone_id * 3] = (float)((packed_color >> 16) & 0xFF) / 255.0f;
    g_zone_light_b[zone_id * 3] = (float)((packed_color >> 8) & 0xFF) / 255.0f;

    particle_zone_update_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800eb898 | particle_zone_light_init
// Initialize zone lighting table. For indoor levels (mode==100): copies
// per-zone params from definition table, extracts initial RGB. For all
// levels: resets all 213 entries to default brightness.
// ---------------------------------------------------------------------------
void particle_zone_light_init(void) {
    // Indoor levels: initialize per-zone state from definitions
    if (g_particle_env_mode == 100) {
        light_system_zone_reset(); // FUN_8034a604

        for (int i = 0; i < g_zone_count; i++) {
            int* zone_state = (int*)(g_zone_light_data_ptr + (i + 1) * 0x28);

            if (g_zone_light_def_ptr == 0) {
                // No definitions — use defaults
                *zone_state = 0;
                zone_state[2] = (int)0.5f;  // FLOAT_806151a0
                zone_state[3] = (int)0.5f;  // FLOAT_80615184
                zone_state[4] = 0x0F0F0F;  // default color
                zone_state[5] = -1;
                zone_state[6] = -1;
            } else {
                // Copy from definition table
                int* zone_def = (int*)(g_zone_light_def_ptr + i * 0x1C);
                *zone_state = *zone_def;
                zone_state[2] = zone_def[1];
                zone_state[3] = zone_def[2];
                zone_state[4] = 0x0F0F0F;
                zone_state[5] = zone_def[3];
                zone_state[6] = zone_def[4];
            }

            zone_state[9] = 0x12345678; // sentinel
            zone_state[8] = 0x12345678; // sentinel
            zone_state[1] = 0;
        }
    }

    // Reset all zone light arrays to default brightness
    float default_brightness = 0.8f; // FLOAT_806151a4
    for (int i = 0; i < 213; i++) { // 0xD5
        g_zone_light_r[i] = default_brightness;
        g_zone_light_g[i] = default_brightness;
        g_zone_light_b[i] = default_brightness;
    }

    // For indoor levels: extract initial RGB values from packed colors
    if (g_particle_env_mode == 100) {
        for (int i = 0; i < g_zone_count; i++) {
            int zone_offset = g_zone_light_def_ptr + i * 0x1C;

            if (zone_offset == 0) {
                // No data — use full brightness
                g_zone_light_r[(i + 1) * 3] = 1.0f; // FLOAT_80615180
                g_zone_light_g[(i + 1) * 3] = 1.0f;
                g_zone_light_b[(i + 1) * 3] = 1.0f;
            } else {
                u32 packed = *(u32*)(zone_offset + 0x14);
                g_zone_light_r[(i + 1) * 3] = (float)((packed >> 24) & 0xFF) / 255.0f;
                g_zone_light_g[(i + 1) * 3] = (float)((packed >> 16) & 0xFF) / 255.0f;
                g_zone_light_b[(i + 1) * 3] = (float)((packed >> 8) & 0xFF) / 255.0f;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800eba88 | particle_zone_light_alloc
// Allocate zone lighting memory. Computes size (zone_count+1)*0x28,
// 16-byte aligned. Calls mem_alloc, stores pointer in DAT_80611e68.
// Calls particle_zone_light_init to initialize.
// ---------------------------------------------------------------------------
void particle_zone_light_alloc(void) {
    int size = (g_zone_count + 1) * 0x28;
    int aligned_size = (size + 0xF) & ~0xF;

    g_zone_light_data_ptr = mem_alloc(aligned_size, (g_heap_flag != 0), size);
    g_zone_light_dirty = 0;

    particle_zone_light_init();
}
