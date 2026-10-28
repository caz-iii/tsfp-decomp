// Address: 0x800e835c-0x800e91e8 | Batch 290 | Date: 2026-08-25
// Particle billboard/quad creation, list management, sprite resolution, lighting, impact records
// Subsystem: game/particle

#include "common.h"
#include "particle.h"
#include "collision.h"
#include "entity.h"
#include "math_util.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern u8  g_particle_bucket_table[];      // DAT_8056f008 — bucket heads, stride 0x354 per group
extern int g_particle_grid_offsets[];       // DAT_8056efd0 — circular index per row
extern int g_particle_pool_base;           // DAT_80611e40 — base of grid pool (0xB8-byte slots)
extern int g_particle_pool_stride;         // DAT_80611e44 — slots per row
extern int g_particle_special_base;        // DAT_80611e50 — special slot pool (type==1)
extern int g_particle_special_index;       // DAT_80611e60 — special slot counter
extern int g_particle_active_head;         // DAT_80611e5c — active list head

extern u8  g_impact_ring_buffer[];         // DAT_8056fa04 — 15x100-byte ring
extern int g_impact_ring_write;            // DAT_80611e3c — write cursor
extern int g_impact_ring_read;             // DAT_80611e38 — read cursor

extern int g_zone_light_r[];               // DAT_8056ffe0 — per-zone red
extern int g_zone_light_g[];               // DAT_8056ffe4 — per-zone green
extern int g_zone_light_b[];               // DAT_8056ffe8 — per-zone blue
extern int g_zone_light_count;             // DAT_80611b14

extern int g_particle_env_mode;            // DAT_80612614 — 100 = indoor

extern u8  g_sprite_type_table[];          // DAT_804045a8 — 0x40-byte entries
extern u8  g_sprite_def_table[];           // DAT_8040be38 — 0x14-byte entries

// ---------------------------------------------------------------------------
// Address: 0x800e835c | particle_list_insert
// Insert particle into priority-bucket linked list (by group index).
// Also links into secondary active list if particle passes visibility test.
// Doubly-linked via offsets 0x80/0x84. Bucket table stride 0x354 per group.
// ---------------------------------------------------------------------------
void particle_list_insert(int particle, u32 priority) {
    int group_id = *(u16*)(particle + 0x92);
    int* bucket_head = (int*)(&g_particle_bucket_table[priority * 4 + group_id * 0x354]);
    int tail = *bucket_head;

    if (tail == 0) {
        // Empty bucket — this particle becomes the head
        *bucket_head = particle;
    } else {
        // Walk to end of linked list
        int prev;
        do {
            prev = tail;
            tail = *(int*)(prev + 0x80);
        } while (*(int*)(prev + 0x80) != 0);
        // Append at tail
        *(int*)(prev + 0x80) = particle;
        *(int*)(particle + 0x84) = prev;
    }

    *(u8*)(particle + 0x99) = (u8)priority;

    // Compute fade-in alpha for non-special particles
    float fade = 0.0f;
    if (*(s16*)(particle + 0x90) == 1) {
        *(float*)(g_particle_special_base + 0xA4) = 0.0f;
    } else {
        int row = (int)priority >> 4;
        int slot_base = g_particle_pool_base + row * g_particle_pool_stride * 0xB8;
        for (int i = 0; i < g_particle_pool_stride; i++) {
            if (fade == *(float*)(slot_base + 0xB4)) {
                int wrapped = (g_particle_pool_stride + i) - g_particle_grid_offsets[row];
                wrapped = wrapped - (wrapped / g_particle_pool_stride) * g_particle_pool_stride;
                float alpha = 1.0f;
                if ((int)wrapped < 11) {
                    alpha = 0.1f * (float)wrapped;
                }
                *(float*)(slot_base + 0xA4) = alpha;
                slot_base += 0xB8;
            }
        }
    }

    // Try to add to active list (FUN_800ea460 = particle visibility test)
    int visible = particle_check_visibility(particle);
    if (visible != 0) {
        *(int*)(particle + 0x88) = g_particle_active_head;
        g_particle_active_head = particle;
        if (*(int*)(particle + 0x88) != 0) {
            *(int*)(*(int*)(particle + 0x88) + 0x8C) = particle;
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800e84c0 | particle_list_remove
// Remove particle from bucket linked list and active list.
// Unlinks from both doubly-linked chains (0x80/0x84 and 0x88/0x8c).
// ---------------------------------------------------------------------------
void particle_list_remove(int particle) {
    int next = *(int*)(particle + 0x80);
    int prev = *(int*)(particle + 0x84);

    if (next == 0 && prev == 0) {
        // Only element or not in list — clear bucket head if we're head
        if (*(u8*)(particle + 0x99) != 0) {
            int offset = (u32)*(u8*)(particle + 0x99) * 4;
            int group_id = *(u16*)(particle + 0x92);
            int* bucket_head = (int*)(&g_particle_bucket_table[offset + group_id * 0x354]);
            if (*bucket_head == particle) {
                *bucket_head = 0;
            }
        }
    } else {
        if (prev == 0) {
            // Head of list — promote next to head
            int group_id = *(u16*)(particle + 0x92);
            int offset = (u32)*(u8*)(particle + 0x99) * 4;
            *(int*)(&g_particle_bucket_table[offset + group_id * 0x354]) = next;
            *(int*)(next + 0x84) = 0;
            *(int*)(particle + 0x80) = 0;
        } else if (next == 0) {
            // Tail of list — just unlink
            *(int*)(prev + 0x80) = 0;
            *(int*)(particle + 0x84) = 0;
        } else {
            // Middle — stitch neighbors together
            *(int*)(next + 0x84) = prev;
            *(int*)(prev + 0x80) = next;
            *(int*)(particle + 0x84) = 0;
            *(int*)(particle + 0x80) = 0;
        }
    }

    // Remove from active list (0x88/0x8c)
    int active_prev = *(int*)(particle + 0x8C);
    if (active_prev == 0) {
        if (particle == g_particle_active_head) {
            g_particle_active_head = 0;
        }
    } else {
        *(int*)(active_prev + 0x88) = *(int*)(particle + 0x88);
    }
    if (*(int*)(particle + 0x88) != 0) {
        *(int*)(*(int*)(particle + 0x88) + 0x8C) = *(int*)(particle + 0x8C);
    }
    *(int*)(particle + 0x8C) = 0;
    *(int*)(particle + 0x88) = 0;
    *(u8*)(particle + 0x99) = 0;
}

// ---------------------------------------------------------------------------
// Address: 0x800e85dc | particle_slot_acquire
// Acquire a particle slot from the pool. For type==1 uses special slot
// (g_particle_special_base + index*0xC0); otherwise computes grid slot.
// Calls particle_list_remove to detach the old occupant.
// ---------------------------------------------------------------------------
int particle_slot_acquire(int type, u32 priority) {
    int slot;
    if (type == 1) {
        slot = g_particle_special_base + g_particle_special_index * 0xC0;
    } else {
        int row = (int)priority >> 4;
        slot = g_particle_pool_base + (g_particle_grid_offsets[row] + row * g_particle_pool_stride) * 0xB8;
    }
    particle_list_remove(slot);
    return slot;
}

// ---------------------------------------------------------------------------
// Address: 0x800e8658 | particle_slot_recycle
// Advance circular index for the group, then re-insert the particle.
// Used to rotate through a fixed-size pool of billboard quads.
// ---------------------------------------------------------------------------
void particle_slot_recycle(int particle) {
    if (*(s16*)(particle + 0x90) == 1) {
        g_particle_special_index = 0;
    } else {
        int row_offset = (u32)*(u8*)(particle + 0x99) >> 2 & 0x3FFFFFFC;
        int next_idx = g_particle_grid_offsets[row_offset / 4] + 1;
        g_particle_grid_offsets[row_offset / 4] = next_idx - (next_idx / g_particle_pool_stride) * g_particle_pool_stride;
    }
    particle_list_insert(particle, (u32)*(u8*)(particle + 0x99));
}

// ---------------------------------------------------------------------------
// Address: 0x800e86c0 | particle_create_billboard
// Create a billboard/quad particle. Computes right/up axes from direction
// via cross products, rotates them by angle, places 4 corner vertices,
// assigns UV coordinates, then calls particle_clip_polygon_to_plane.
// ---------------------------------------------------------------------------
void particle_create_billboard(
    u32 param_1, u32 param_2, u32 uv_index, float lifetime,
    u8 fade_mode, u32 priority, float* position, float* direction,
    int clip_planes, float* clip_origin, int clip_count,
    s16* clip_indices, int param_13, int param_14, int param_15)
{
    // Acquire slot from pool
    u64 context = particle_context_begin();
    u16* def = (u16*)(context >> 32);
    u32  variant = (u32)context;

    float* quad = (float*)particle_slot_acquire((u32)*def, priority);
    float half_size = *(float*)(def + 8);

    // Setup lifetime and fade parameters
    if (lifetime == 0.0f) {
        quad[0x27] = 0.0f;
        *(u8*)((int)quad + 0x9A) = 0xFF;
        *(u8*)((int)quad + 0x9B) = 0;
    } else {
        quad[0x27] = lifetime;
        *(u8*)((int)quad + 0x9A) = fade_mode;
        *(u8*)((int)quad + 0x9B) = (char)*(u32*)(def + 0x18);
    }

    // Copy definition fields
    *(u16*)(quad + 0x24) = *def;
    *(u16*)((int)quad + 0x92) = def[1];
    *(u16*)(quad + 0x25) = def[2];
    *(s16*)((int)quad + 0x96) = (s16)*(u32*)(def + 4);
    *(u16*)(quad + 0x2B) = def[6];
    quad[0x2D] = *(float*)(def + 0xC);
    quad[0x2A] = (float)(*(u32*)(def + 10) & 0xFFFFFF);
    *(u8*)((int)quad + 0x99) = (u8)priority;

    // Compute initial alpha from high byte
    float base_alpha = (float)((double)(*(u32*)(def + 10) >> 24) / 255.0);
    quad[0x28] = base_alpha;
    quad[0x29] = 1.0f;
    quad[0x2C] = 0.0f;

    // Compute right vector (cross product of direction x world_up)
    float right[3], up[3];
    float world_up[3] = { 0.0f, 1.0f, 0.0f };

    // If direction is near-vertical, use alternate up
    if (0.99f < fabsf(direction[1])) {
        world_up[0] = 1.0f;
        world_up[1] = 0.0f;
    }

    // right = direction x world_up (cross product + normalize)
    right[0] = direction[1] * world_up[2] - direction[2] * world_up[1];
    right[1] = direction[2] * world_up[0] - direction[0] * world_up[2];
    right[2] = direction[0] * world_up[1] - direction[1] * world_up[0];
    vec3_normalize_fast(right);

    // up = direction x right (cross product + normalize)
    up[0] = direction[1] * right[2] - direction[2] * right[1];
    up[1] = direction[2] * right[0] - direction[0] * right[2];
    up[2] = direction[0] * right[1] - direction[1] * right[0];
    vec3_normalize_fast(up);

    // Scale axes by half_size
    for (int i = 0; i < 3; i++) {
        right[i] *= half_size;
        up[i] *= half_size;
    }

    // Rotate right and up by angle from def
    float angle = (float)*(u16*)(def + 0xE) * DEG_TO_RAD_FACTOR;
    float rot_matrix[9];
    mat3_from_axis_angle(angle, rot_matrix, direction);
    mat3_transform_vec3(rot_matrix, right);
    mat3_transform_vec3(rot_matrix, up);

    // Place 4 corners: +right+up, +up-right, -right-up, -up+right
    *(u8*)((int)quad + 0x98) = 4; // vertex count
    quad[0]  = position[0] + right[0];
    quad[1]  = position[1] + right[1];
    quad[2]  = position[2] + right[2];
    quad[3]  = position[0] + up[0];
    quad[4]  = position[1] + up[1];
    quad[5]  = position[2] + up[2];
    quad[6]  = position[0] - right[0];
    quad[7]  = position[1] - right[1];
    quad[8]  = position[2] - right[2];
    quad[9]  = position[0] - up[0];
    quad[10] = position[1] - up[1];
    quad[11] = position[2] - up[2];

    // Assign UV coordinates per corner based on variant/uv_index
    // (shorts at offsets 0x60-0x6E encode texture coordinates)
    particle_assign_uv_coords(quad, variant, uv_index);

    // Clear output buffer if provided
    if (param_15 != 0) {
        for (int i = 0; i < 4; i++) {
            *(int*)(param_15 + i * 0x20) = 0;
        }
    }

    // Clip particle polygon against frustum planes
    int clipped = particle_clip_polygon_to_plane(
        quad, direction, variant, uv_index,
        clip_planes, clip_origin, clip_count, clip_indices, param_13, param_14);

    if (clipped == 0) {
        *(s16*)((int)quad + 0x96) = -1; // mark as culled
    } else {
        particle_slot_recycle((int)quad);
    }

    particle_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800e8d78 | particle_resolve_sprite_params
// Resolve sprite texture, scale, and color for a particle based on type
// tables. Uses a 0x40-byte type table and a 0x14-byte sprite def table.
// Handles indoor/outdoor variants, byte-swaps RGBA.
// ---------------------------------------------------------------------------
void particle_resolve_sprite_params(
    u32 param_1, u32 param_2, u32* out_texture,
    float* out_scale, u32* out_color, u32* out_param_a,
    u16* out_param_b, u32* out_param_c)
{
    u64 ctx = particle_sprite_context_begin();
    int type_index = (int)(ctx >> 32);
    int sub_variant = (int)ctx;

    int sprite_index = -1;
    int is_indoor = particle_is_indoor_env();

    if (is_indoor == 0) {
        // Outdoor mode
        int entry_offset = type_index * 0x40;
        *out_scale = *(float*)(&g_sprite_type_table[entry_offset + 0x08]); // +0xB0 rel
        u32 rgba = *(u32*)(&g_sprite_type_table[entry_offset + 0x04]);     // +0xAC rel
        *out_color = BSWAP32(rgba);

        int mode = *(int*)(&g_sprite_type_table[entry_offset + 0x00]);     // +0xA8 rel
        if (mode == 3) {
            u32 rng = random_bits();
            sprite_index = (rng & 1) + 5;
        } else if (mode == 1) {
            if (sub_variant == 0) sprite_index = 0;
            else if (sub_variant == 1) sprite_index = 1;
            else if (sub_variant == 2) sprite_index = 2;
            else if (sub_variant == 3) sprite_index = 4;
            else if (sub_variant == 4) sprite_index = 15;
        } else if (mode == 2) {
            u32 rng = random_bits();
            sprite_index = (rng & 1) + 16;
        } else if (mode == 4) {
            random_bits();
            sprite_index = 13;
            *out_color = 0x4F0C2F0C;
        }
    } else {
        // Indoor mode
        int entry_offset = type_index * 0x40;
        *out_scale = *(float*)(&g_sprite_type_table[entry_offset + 0x10]); // +0xB8 rel
        u32 rgba = *(u32*)(&g_sprite_type_table[entry_offset + 0x0C]);     // +0xB4 rel
        *out_color = BSWAP32(rgba);
        random_bits();
        sprite_index = 13;
    }

    if (sprite_index != -1) {
        int def_offset = sprite_index * 0x14;
        *out_texture = sprite_get_texture_id(g_sprite_def_table[sprite_index * 5]);
        *out_scale = *out_scale * *(float*)(&g_sprite_def_table[def_offset + 4]);

        if (out_param_a != NULL) {
            *out_param_a = *(u32*)(&g_sprite_def_table[def_offset + 8]);
        }
        if (out_param_b != NULL) {
            *out_param_b = (s16)*(u32*)(&g_sprite_def_table[def_offset + 12]);
        }
        if (out_param_c != NULL) {
            *out_param_c = *(u32*)(&g_sprite_def_table[def_offset + 16]);
        }
    }

    particle_sprite_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800e8f5c | particle_apply_zone_lighting
// Apply zone ambient lighting to particle color. Reads per-zone RGB, lerps
// toward average, clamps [min_brightness, 1.0], modulates RGBA channels.
// ---------------------------------------------------------------------------
void particle_apply_zone_lighting(void) {
    u64 ctx = particle_lighting_context_begin();
    int particle = (int)(ctx >> 32);
    u32 color = (u32)ctx;

    // Determine which zone to sample from
    BOOL use_attached_zone = FALSE;
    if (g_particle_env_mode == 100 && *(int*)(particle + 0x9C) != 0) {
        use_attached_zone = TRUE;
    }

    u32 zone_id;
    if (use_attached_zone) {
        zone_id = *(u32*)(*(int*)(particle + 0x9C) + 0x34);
    } else {
        zone_id = (u32)*(u8*)(particle + 0x99);
    }

    // Select min brightness based on environment
    float min_brightness = 0.4f; // outdoor
    if (g_particle_env_mode == 100) {
        min_brightness = 0.2f;   // indoor
    }

    // Compute zone light factor from averaged RGB
    if ((int)zone_id > 0 && (int)zone_id < g_zone_light_count + 1) {
        float avg = ((float)g_zone_light_r[zone_id] +
                     (float)g_zone_light_g[zone_id] +
                     (float)g_zone_light_b[zone_id]) / 3.0f;

        float factor = 0.5f * (avg - 0.5f) + 0.5f; // lerp toward 0.5
        if (factor < min_brightness) factor = min_brightness;
        if (factor > 1.0f) factor = 1.0f;

        // Modulate RGB channels by factor (keep alpha unchanged)
        u8 r = (u8)clamp_to_byte((float)(color & 0xFF) * factor);
        u8 g = (u8)clamp_to_byte((float)((color >> 8) & 0xFF) * factor);
        u8 b = (u8)clamp_to_byte((float)((color >> 16) & 0xFF) * factor);

        u32 modulated = (u32)r | ((u32)g << 8) | (color & 0xFF000000) | ((u32)b << 16);
        particle_set_vertex_color(modulated, zone_id);
    }

    particle_lighting_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800e90f8 | particle_is_valid_emitter_type
// Check if an entity type is valid for particle emission (whitelist).
// Types: 1,2,4 (char), 0x80 (weapon), 0x200 (projectile), 0x2000 (effect),
// 0x20000 (debris), 0x4000000 (dropped weapon), 0x8000000 (shell casing).
// Also requires bit 0x400 of flags (+0x28) to be clear.
// ---------------------------------------------------------------------------
int particle_is_valid_emitter_type(int entity) {
    if (entity == 0) return 0;
    if ((*(u32*)(entity + 0x28) & 0x400) != 0) return 0;

    int type = *(int*)(entity + 0x20);
    switch (type) {
        case 1:         // player character
        case 2:         // AI character
        case 3:         // ??? (returns 0 in original)
        case 4:         // NPC
        case 0x80:      // weapon
        case 0x200:     // projectile
        case 0x2000:    // effect entity
        case 0x20000:   // debris
        case 0x4000000: // dropped weapon
        case 0x8000000: // shell casing
            return 1;
        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800e919c | particle_impact_ring_alloc
// Allocate next entry in the 15-slot ring buffer for particle impact records.
// Each entry is 100 bytes. If buffer full, overwrites oldest.
// ---------------------------------------------------------------------------
void* particle_impact_ring_alloc(void) {
    // If write would hit read cursor, advance read (lose oldest)
    if ((g_impact_ring_write + 1) % 15 == g_impact_ring_read) {
        g_impact_ring_read = (g_impact_ring_read + 1) % 15;
    }
    return &g_impact_ring_buffer[g_impact_ring_write * 100];
}

// ---------------------------------------------------------------------------
// Address: 0x800e91e8 | particle_impact_record_create
// Create a particle impact record: store origin + direction, transform to
// entity-local space if attached to bone, perform short raycast to detect
// the hit surface. Used for bullet holes, sparks, blood decals.
// ---------------------------------------------------------------------------
void particle_impact_record_create(
    u32 param_1, u32 param_2, int entity,
    float* origin, float* direction, int use_bone_space)
{
    int record = particle_impact_context_begin();

    // Set initial rotation (random or fixed 45 degrees)
    if (*(int*)(record + 0x20) == 0) {
        *(float*)(record + 0x1C) = 45.0f;
    } else {
        u32 rng = random_bits();
        rng = rng % 360;
        *(float*)(record + 0x1C) = (float)rng;
    }

    // Store collision result reference
    collision_result_restore(record + 0x2C);
    *(s16*)(record + 2) = (s16)*(int*)(record + 0x34);

    // Store origin and direction
    *(float*)(record + 0x4C) = origin[0];
    *(float*)(record + 0x50) = origin[1];
    *(float*)(record + 0x54) = origin[2];
    *(float*)(record + 0x58) = direction[0];
    *(float*)(record + 0x5C) = direction[1];
    *(float*)(record + 0x60) = direction[2];

    // Set bone-space flag
    if (use_bone_space != 0) {
        *(u16*)(record + 4) |= 1;
    }

    // Transform to entity-local space if entity has bone data
    if (*(char*)(entity + 0x32) != '\0') {
        float local_matrix[16];
        entity_build_local_matrix(local_matrix, entity, 0, 0);

        // Subtract entity world position
        *(float*)(record + 0x4C) -= *(float*)(entity + 0x64);
        *(float*)(record + 0x50) -= *(float*)(entity + 0x68);
        *(float*)(record + 0x54) -= *(float*)(entity + 0x6C);

        // Rotate origin and direction into local space
        mat3_transform_point(local_matrix, (float*)(record + 0x4C));
        mat3_transform_point(local_matrix, (float*)(record + 0x58));

        // If bone matrices available, further transform by bone inverse
        int bone_matrices = *(int*)(*(int*)(record + 0x2C) + 0x10);
        int* collision_data = *(int**)(*(int*)(record + 0x2C) + 4);
        int col_index = *collision_data;
        int col_stride = *(int*)(record + 0x30);

        if (bone_matrices != 0) {
            char bone_id = *(char*)((int)collision_data + col_stride * 0xC0 + col_index * -0xC0 + 0x3D);
            if (bone_id >= 0) {
                float* bone_mat = (float*)(bone_matrices + bone_id * 0x40);
                *(float*)(record + 0x4C) -= bone_mat[12];
                *(float*)(record + 0x50) -= bone_mat[13];
                *(float*)(record + 0x54) -= bone_mat[14];
                mat3_transform_point(bone_mat, (float*)(record + 0x4C));
                mat3_transform_point(bone_mat, (float*)(record + 0x58));
            }
        }
    }

    // Perform short raycast along direction to find impact surface
    float ray_start[3], ray_dir[3];
    ray_start[0] = 0.5f * direction[0] + origin[0];
    ray_start[1] = 0.5f * direction[1] + origin[1];
    ray_start[2] = 0.5f * direction[2] + origin[2];
    ray_dir[0] = 2.0f * direction[0];
    ray_dir[1] = 2.0f * direction[1];
    ray_dir[2] = 2.0f * direction[2];

    int hit = collision_entity_raycast(
        *(int*)(record + 0x2C), ray_start, ray_dir, 0, NULL, NULL, 0);

    if (hit != 0) {
        g_impact_ring_write = (g_impact_ring_write + 1) % 15;
    }

    particle_impact_context_end();
}
