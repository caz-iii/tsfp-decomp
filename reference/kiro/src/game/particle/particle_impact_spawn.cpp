// Address: 0x800e9450-0x800ea48c | Batch 291 | Date: 2026-08-25
// Particle impact spawning, kill/lifecycle, per-frame tick, adjacency propagation
// Subsystem: game/particle

#include "common.h"
#include "particle.h"
#include "collision.h"
#include "entity.h"
#include "math_util.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern int   g_particle_pool_base;           // DAT_80611e40 — grid pool base (0xB8-byte slots)
extern int   g_particle_pool_total;          // DAT_806107f8 — total pool slot count
extern int   g_particle_active_head;         // DAT_80611e5c — active list head
extern int   g_particle_special_base;        // DAT_80611e50 — special slot (type==1)

extern u8    g_impact_ring_buffer[];         // DAT_8056fa04 — 15x100-byte ring
extern int   g_impact_ring_write;            // DAT_80611e3c — write cursor
extern int   g_impact_ring_read;             // DAT_80611e38 — read cursor
extern int   g_impact_ring_entity_refs[];    // DAT_8056fa30 — entity ref per slot (stride 100)

extern int   g_particle_env_mode;            // DAT_80612614 — 100 = indoor
extern float g_frame_dt_scaled;              // FLOAT_806108ec — delta time * speed
extern int   g_frame_counter;                // DAT_806109c0 — frame counter for dt calc

extern u8    g_sprite_type_table[];          // DAT_804045a8
extern u8    g_sprite_def_table[];           // DAT_8040be38

// ---------------------------------------------------------------------------
// Address: 0x800e9450 | particle_spawn_impact_single
// Spawn a single particle impact decal on a known entity. Validates emitter
// type, allocates ring buffer slot, resolves sprite texture/color, creates
// impact record with bone-space transform.
// ---------------------------------------------------------------------------
void particle_spawn_impact_single(
    int type_index, int sub_variant, u32 param_3,
    int entity, float* position, float* normal)
{
    int ctx = particle_impact_inner_begin();

    int valid = particle_is_valid_emitter_type(entity);
    if (valid != 0) {
        u16* record = (u16*)particle_impact_ring_alloc();

        int mode = *(int*)(&g_sprite_type_table[type_index * 0x40]);
        record[2] = 0;
        u32 use_bone = countLeadingZeros(1 - mode) >> 5; // 1 if mode==1, 0 otherwise
        *record = 0;

        // Resolve sprite texture, scale, color
        particle_resolve_sprite_params(
            type_index, sub_variant,
            (u32*)(record + 4),      // out_texture
            (float*)(record + 8),    // out_scale
            (u32*)(record + 10),     // out_color
            (u32*)(record + 0x10),   // out_param_a
            record + 6,             // out_param_b
            (u32*)(record + 0xC));   // out_param_c

        particle_impact_record_create(record, param_3, entity, position, normal, use_bone);
    }

    particle_impact_inner_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800e9518 | particle_spawn_impact_from_ray
// Spawn a particle impact from a raycast. Performs scene_raycast to find
// the hit entity and surface normal, validates entity type, allocates ring
// slot, stores params, creates impact record.
// ---------------------------------------------------------------------------
void particle_spawn_impact_from_ray(
    u32 param_1, u32 param_2, u32 param_3, u16 param_4,
    u32 param_5, u32 ray_origin, u32 ray_dir, float* ray_end)
{
    u32 ctx = particle_impact_inner_begin();

    float hit_pos[3];
    float hit_normal[3];

    int hit_entity = scene_raycast(
        ray_origin, ray_dir, ray_end, 0,
        hit_pos, hit_normal, NULL, 0x2010078, 0, NULL);

    int valid = particle_is_valid_emitter_type(hit_entity);
    if (valid != 0) {
        u8* record = particle_impact_ring_alloc();

        *(u32*)(record + 8)  = ctx;       // texture ID
        *(u32*)(record + 0x14) = param_3;  // sub-type
        *(u16*)(record + 4)  = param_4;    // flags
        *(float*)(record + 0x10) = 0.0f;   // lifetime start
        *(u32*)(record + 0x20) = param_5;  // rotation mode
        *(float*)(record + 0x18) = 0.0f;   // fade delay

        particle_impact_record_create(record, ray_origin, hit_entity, hit_pos, hit_normal, 0);
    }

    particle_impact_inner_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800e95fc | particle_spawn_impact_spread
// Spawn blood/impact particles along 6 cardinal directions. If character is
// "female" type (different splatter), casts 6 rays in +X,-X,+Y,-Y,+Z,-Z,
// deduplicates hits, calls particle_spawn_impact_single for each unique hit.
// For non-female, just calls particle_spawn_impact_single directly.
// ---------------------------------------------------------------------------
void particle_spawn_impact_spread(
    u32 param_1, u32 param_2, u32 param_3,
    int entity, float* position, float* direction)
{
    u64 ctx = particle_spread_context_begin();
    int type_index = (int)(ctx >> 32);
    u32 sub_variant = (u32)ctx;

    if (*(int*)(&g_sprite_type_table[type_index * 0x40]) == 0) {
        particle_spread_context_end();
        return;
    }

    u32 is_female = player_ai_char_def_is_female(type_index);

    if (is_female == 0) {
        // Single impact for non-female characters
        particle_spawn_impact_single(type_index, sub_variant, param_3, entity, position, direction);
    } else {
        // Multi-directional blood spread
        int hit_count = 0;

        // Offset position along direction slightly
        float offset_pos[3];
        float offset_factor = 0.1f; // FLOAT_8061515c
        if (entity != 0) {
            offset_pos[0] = offset_factor * direction[0] + position[0];
            offset_pos[1] = offset_factor * direction[1] + position[1];
            offset_pos[2] = offset_factor * direction[2] + position[2];
        }

        // 6 cardinal ray directions
        static const float ray_dirs[6][3] = {
            { -1.0f,  0.0f,  0.0f },  // -X
            {  0.0f, -1.0f,  0.0f },  // -Y (FLOAT_80615160)
            {  0.0f,  0.0f, -1.0f },  // -Z
            {  0.0f,  0.0f,  1.0f },  // +Z (FLOAT_80615160)
            {  0.0f,  0.0f, -1.0f },  // -Z alt
            {  0.0f,  0.0f,  1.0f },  // +Z alt (FLOAT_80615160)
        };

        // Collision result buffer for deduplication (up to 6 results x 0x20 bytes)
        int collision_results[6 * 8];

        for (int ray = 0; ray < 6; ray++) {
            float hit_pos[3];
            float hit_normal[3];

            int hit_entity = scene_raycast(
                param_3, offset_pos, (float*)&ray_dirs[ray], 0,
                hit_pos, hit_normal, NULL, 0x2010078, 0, NULL);

            if (hit_entity != 0) {
                // Check for duplicate hit (compare collision result)
                BOOL is_unique = TRUE;
                collision_result_restore((int)&collision_results[hit_count * 8]);

                for (int j = 0; j < hit_count; j++) {
                    if (collision_results[hit_count * 8 + 0] == collision_results[j * 8 + 0] &&
                        collision_results[hit_count * 8 + 1] == collision_results[j * 8 + 1] &&
                        collision_results[hit_count * 8 + 2] == collision_results[j * 8 + 2] &&
                        collision_results[hit_count * 8 + 3] == collision_results[j * 8 + 3] &&
                        collision_results[hit_count * 8 + 4] == collision_results[j * 8 + 4]) {
                        is_unique = FALSE;
                        break;
                    }
                }

                if (is_unique) {
                    hit_count++;
                    particle_spawn_impact_single(
                        type_index, sub_variant, param_3,
                        hit_entity, hit_pos, hit_normal);
                }
            }
        }
    }

    particle_spread_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800e9cec | particle_spawn_attached_billboard
// Spawn a billboard particle attached to an entity. Resolves sprite, creates
// billboard via particle_create_billboard, then transforms all vertex
// positions into bone-local space using entity_build_local_matrix + bone
// inverse. Used for persistent attached effects (blood, burn marks).
// ---------------------------------------------------------------------------
void particle_spawn_attached_billboard(
    u32 param_1, u32 param_2, u8 fade_mode,
    float* position, float* direction, int clip_data, int clip_count)
{
    u64 ctx = particle_attached_context_begin();
    float emitter_ref = (float)ctx;

    u32 bucket = particle_pool_get_bucket((int)emitter_ref);

    // Pick random sprite index (8-11 range)
    u32 rng = random_bits();
    int sprite_idx = (rng & 3) + 8;
    u32 texture_id = sprite_get_texture_id(g_sprite_def_table[sprite_idx * 5]);

    // Pick UV quadrant
    rng = random_bits();
    u32 uv_quadrant = rng & 3;
    BOOL use_alt = (uv_quadrant == 0 || uv_quadrant == 2);
    u32 uv_index;
    if (use_alt) {
        rng = random_bits();
        uv_index = (rng & 1) * 2 + 1;
    } else {
        rng = random_bits();
        uv_index = (rng & 1) << 1;
    }

    // Copy world-up vectors for billboard orientation
    float world_axes[6]; // 3x vec2 from DAT_803cc868
    memcpy(world_axes, &g_world_axis_data, sizeof(world_axes));

    float sprite_scale = *(float*)(&g_sprite_def_table[sprite_idx * 0x14 + 4]);
    u32 color = 0x7FFFFFFF; // white with max alpha

    // Random rotation angle
    rng = random_bits();
    float angle = (float)(rng % 360);

    // Clamp clip_count to 12
    if (clip_count > 12) clip_count = 12;

    // Transform clip vertex positions relative to camera
    float clip_vertices[12 * 3];
    int* camera_pos_ptr = *(int**)(*(int*)((int)emitter_ref + 4) + 4);
    for (int i = 0; i < clip_count; i++) {
        float* src = (float*)(clip_data + i * 0x1C);
        clip_vertices[i * 3 + 0] = src[0] - *(float*)(camera_pos_ptr + 0x2C/4);
        clip_vertices[i * 3 + 1] = src[1] - *(float*)(camera_pos_ptr + 0x30/4);
        clip_vertices[i * 3 + 2] = src[2] - *(float*)(camera_pos_ptr + 0x34/4);
    }

    // Transform position relative to camera
    float local_pos[3];
    local_pos[0] = position[0] - *(float*)(camera_pos_ptr + 0x2C/4);
    local_pos[1] = position[1] - *(float*)(camera_pos_ptr + 0x30/4);
    local_pos[2] = position[2] - *(float*)(camera_pos_ptr + 0x34/4);

    // Check indoor mode for alternate sprite
    int is_indoor = particle_is_indoor_env();
    if (is_indoor != 0) {
        particle_resolve_sprite_params(
            (int)(ctx >> 32), 0,
            &texture_id, &sprite_scale, &color, NULL, NULL, NULL);
    }

    // Build particle def struct
    ParticleSpawnDef def;
    def.type = 0;
    def.flags = 1;
    def.variant = 0;
    def.texture_id = texture_id;
    def.uv_page = 1;
    def.angle = angle;
    def.scale = sprite_scale;
    def.color = color;
    def.lifetime = 0.0f;
    def.extra = 0;

    // Create the billboard
    int particle = particle_create_billboard(
        &def, uv_quadrant, uv_index, emitter_ref, fade_mode, bucket,
        local_pos, direction, (int)clip_vertices,
        NULL, 0, (s16*)&world_axes, clip_count, 0, 0);

    // If particle created and has attached entity, transform to bone-local space
    if (particle != 0 && *(int*)(particle + 0x9C) != 0) {
        float inv_matrix[16];
        float bone_matrix[16];
        float combined[16];

        entity_build_local_matrix(bone_matrix, *(int*)(particle + 0x9C), 1, 0);

        char bone_id = *(char*)(particle + 0x9A);
        if (bone_id < 0) {
            // No bone — just invert the entity matrix
            mat4_invert_affine(bone_matrix, inv_matrix);
        } else {
            // Combine bone matrix with entity matrix, then invert
            int bone_data = *(int*)(*(int*)(*(int*)(particle + 0x9C) + 4) + 0x10);
            float* bone_mat = (float*)(bone_data + bone_id * 0x40);
            mat4_multiply(bone_mat, bone_matrix, combined);
            mat4_invert_affine(combined, inv_matrix);
        }

        // Transform each vertex position by the inverse matrix
        int vertex_count = *(u8*)(particle + 0x98);
        for (int i = 0; i < vertex_count; i++) {
            mat4_transform_point(inv_matrix, (float*)(particle + i * 0xC));
        }
    }

    particle_attached_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800ea044 | particle_kill
// Kill/invalidate a particle: marks group type as -1 (0xFFFF), removes from
// bucket + active lists. Simple teardown wrapper.
// ---------------------------------------------------------------------------
void particle_kill(int particle) {
    if (particle != 0) {
        *(u16*)(particle + 0x96) = 0xFFFF; // mark as invalid
        particle_list_remove(particle);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800ea074 | particle_kill_by_entity
// Remove all particles attached to a specific entity. Iterates entire grid
// pool (stride 0xB8), kills any particle whose entity ref at +0x9C matches.
// Also clears matching impact ring buffer entries. 21+ callers.
// ---------------------------------------------------------------------------
void particle_kill_by_entity(void) {
    u64 ctx = particle_lighting_context_begin();
    int target_entity = (int)(ctx >> 32);

    // Iterate grid pool
    int offset = 0;
    for (int i = 0; i < g_particle_pool_total; i++) {
        if (*(int*)(g_particle_pool_base + offset + 0x9C) == target_entity) {
            *(u16*)(g_particle_pool_base + offset + 0x96) = 0xFFFF;
            particle_list_remove(g_particle_pool_base + offset);
        }
        offset += 0xB8;
    }

    // Clear matching impact ring entries
    int ring_offset = 0;
    for (int i = 0; i < 15; i++) {
        int* entity_ref = (int*)(&g_impact_ring_entity_refs[ring_offset]);
        if (entity_ref != NULL && *entity_ref == target_entity) {
            *(int*)(&g_impact_ring_entity_refs[ring_offset]) = 0;
        }
        ring_offset += 100;
    }

    particle_lighting_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800ea130 | particle_set_opacity
// Set particle opacity/alpha value at offset 0xA0. Simple float setter.
// ---------------------------------------------------------------------------
void particle_set_opacity(float opacity, int particle) {
    *(float*)(particle + 0xA0) = opacity;
}

// ---------------------------------------------------------------------------
// Address: 0x800ea138 | particle_test_adjacent_face
// Test if an adjacent triangle face should also receive a decal. Checks:
// (1) face-group mask matches current material, (2) face normal faces toward
// impact direction (dot product >= threshold), (3) not already in hit list
// (dedup). If all pass, projects impact position onto adjacent face plane
// and stores hit record with position + normal.
// ---------------------------------------------------------------------------
void particle_test_adjacent_face(
    u32 param_1, u32 param_2, int face_index,
    float* impact_pos, float* impact_dir,
    int hit_list, int* hit_count)
{
    u64 ctx = particle_spread_context_begin();
    int mesh_data = (int)(ctx >> 32);
    int sub_index = (int)ctx;

    int* mesh_ptr = *(int**)(mesh_data + 4);
    int primary = *mesh_ptr;

    // Check face group mask
    int mask_entry = mesh_ptr[primary * -0x30 + sub_index * 0x30 + 0x14];
    if (mask_entry != 0 && (*(u32*)(mesh_data + 0x30) & mask_entry) == 0) {
        particle_spread_context_end();
        return;
    }

    // Get face data
    int face_table_offset = face_index * 0xE;
    int face_data_base = *(int*)(mesh_ptr[primary * -0x30 + sub_index * 0x30 + 0x27] + 0x1C);
    int face_entry = face_data_base + face_table_offset;

    u32 strip_index = *(u8*)(face_entry + 4) & 3;

    // Decode face normal from signed bytes
    float nx = (float)((int)*(s8*)(face_entry + 1)) / 127.0f;
    float ny = (float)((int)*(s8*)(face_entry + 2)) / 127.0f;
    float nz = (float)((int)*(s8*)(face_entry + 3)) / 127.0f;

    // Dot product with impact direction — must face toward impact
    float dot = nx * impact_dir[0] + ny * impact_dir[1] + nz * impact_dir[2];
    if (dot < -0.3f) { // FLOAT_80615164 threshold
        particle_spread_context_end();
        return;
    }

    // Dedup check — don't hit the same face twice
    int existing = *hit_count;
    BOOL is_unique = TRUE;
    for (int i = 0; i < existing; i++) {
        int* entry = (int*)(hit_list + i * 0x38);
        if (entry[0] == mesh_data && sub_index == entry[1] && face_index == entry[4]) {
            is_unique = FALSE;
            break;
        }
    }

    if (!is_unique) {
        particle_spread_context_end();
        return;
    }

    // Environment check for indoor mode
    u16 face_uv_index = *(u16*)(face_entry + 8);
    int vertex_table_base = *(int*)(mesh_ptr[primary * -0x30 + sub_index * 0x30 + 0x27] + 0x2C);

    int strip_data = mesh_ptr[primary * -0x30 + sub_index * 0x30 + strip_index + 0x15]
                     + (u32)*(u16*)(face_entry + 6) * 8;

    if (g_particle_env_mode == 100 &&
        mesh_ptr[primary * -0x30 + sub_index * 0x30 + strip_index + 0x15] == 0) {
        particle_spread_context_end();
        return;
    }

    // Store hit record
    int record_offset = existing * 0x38;
    *(int*)(hit_list + record_offset) = mesh_data;
    *(int*)(hit_list + record_offset + 4) = sub_index;
    *(int*)(hit_list + record_offset + 0x10) = face_index;

    // Get vertex position from mesh
    int vert_index = (int)*(s16*)(vertex_table_base + (u32)face_uv_index * 2);
    float vx, vy, vz;

    if ((mesh_ptr[3] & 0x10) == 0) {
        // Float vertex format (12 bytes per vertex)
        float* vert = (float*)(mesh_ptr[primary * -0x30 + sub_index * 0x30 + strip_index * 5 + 1]
                       + ((u32)*(u16*)(strip_data + 6) + vert_index) * 0xC);
        vx = vert[0];
        vy = vert[1];
        vz = vert[2];
    } else {
        // Short vertex format (6 bytes per vertex) with bias
        s16* vert = (s16*)(mesh_ptr[primary * -0x30 + sub_index * 0x30 + strip_index * 5 + 1]
                    + ((u32)*(u16*)(strip_data + 6) + vert_index) * 6);
        float scale = 1.0f / 256.0f; // FLOAT_806150f8
        vx = scale * (float)vert[0] + (float)mesh_ptr[0xB];
        vy = scale * (float)vert[1] + (float)mesh_ptr[0xC];
        vz = scale * (float)vert[2] + (float)mesh_ptr[0xD];
    }

    // Project impact position onto the face plane
    float plane_dist = (impact_pos[2] - vz) * nz +
                       (impact_pos[0] - vx) * nx +
                       (impact_pos[1] - vy) * ny;

    *(float*)(hit_list + record_offset + 0x20) = -(plane_dist * nx - impact_pos[0]);
    *(float*)(hit_list + record_offset + 0x24) = -(plane_dist * ny - impact_pos[1]);
    *(float*)(hit_list + record_offset + 0x28) = -(plane_dist * nz - impact_pos[2]);
    *(float*)(hit_list + record_offset + 0x2C) = nx;
    *(float*)(hit_list + record_offset + 0x30) = ny;
    *(float*)(hit_list + record_offset + 0x34) = nz;

    *hit_count = existing + 1;

    particle_spread_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800ea460 | particle_check_visibility
// Check if particle should be in the active render list. Returns 1 if
// lifetime (offset 0xB4) > 0 OR if force-visible flags (bits 1|2 of offset
// 0x94) are set. Called by particle_list_insert to gate active list entry.
// ---------------------------------------------------------------------------
int particle_check_visibility(int particle) {
    if (*(float*)(particle + 0xB4) > 0.0f) {
        return 1;
    }
    if ((*(u16*)(particle + 0x94) & 6) != 0) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x800ea48c | particle_system_tick
// MASSIVE per-frame particle system update. Three phases:
// (1) Age all active particles: accumulate time, compute alpha fade from
//     lifetime ratio, kill expired particles. Projectile-attached particles
//     use projectile_compute_lifetime_params for two-phase fade.
// (2) Fade special slot opacity (e.g. screen flash).
// (3) Process impact ring buffer: for each pending impact record, create
//     billboard decal via particle_create_billboard, test up to 4 clipped
//     polygon outputs for adjacent face propagation via
//     particle_test_adjacent_face, also walk edge neighbor table for
//     additional adjacency.
// ---------------------------------------------------------------------------
void particle_system_tick(void) {
    particle_tick_context_begin();

    int process_all = 1;

    // ---- Phase 1: Age active particles ----
    int current = g_particle_active_head;
    float dt_factor = g_frame_dt_scaled / (float)g_frame_counter; // scaled dt

    while (current != 0) {
        int next = *(int*)(current + 0x88);

        // Accumulate age
        *(float*)(current + 0xB0) += dt_factor;

        if ((*(u16*)(current + 0x94) & 4) == 0) {
            // Normal lifetime fade
            float lifetime = *(float*)(current + 0xB4);
            if (lifetime > 0.0f) {
                if (*(float*)(current + 0xB0) <= lifetime) {
                    // Compute alpha: 1.0 - (age / lifetime)
                    *(float*)(current + 0xA4) =
                        1.0f - (*(float*)(current + 0xB0) / lifetime);
                } else {
                    particle_kill(current);
                }
            }
        } else {
            // Projectile-attached: two-phase lifetime
            float fade_start, fade_duration;
            projectile_compute_lifetime_params(
                *(int*)(current + 0x9C), &fade_duration, &fade_start);

            float age = *(float*)(current + 0xB0);
            if (age > fade_start) {
                if (age <= fade_start + fade_duration) {
                    *(float*)(current + 0xA4) =
                        1.0f - (age - fade_start) / fade_duration;
                } else {
                    particle_kill(current);
                }
            }
        }

        current = next;
    }

    // Flush any deferred particle operations
    particle_flush_deferred();

    // ---- Phase 2: Special slot fade ----
    int special = g_particle_special_base;
    if (*(s16*)(special + 0x96) >= 0) {
        // Fade opacity down over time (15x frame rate multiplier)
        *(float*)(special + 0xA0) -= g_frame_dt_scaled / (float)(g_frame_counter * 15);
        if (*(float*)(special + 0xA0) <= 0.0f) {
            particle_kill(special);
        }
    }

    // ---- Phase 3: Process impact ring buffer ----
    // Check ring buffer occupancy — throttle if > 7 pending
    if (g_impact_ring_read != g_impact_ring_write) {
        int pending = 0;
        int cursor = g_impact_ring_read;
        while (cursor != g_impact_ring_write) {
            cursor = (cursor + 1 > 14) ? cursor - 14 : cursor + 1;
            pending++;
        }
        if (pending > 7) {
            process_all = 0; // process one-at-a-time to spread load
        }
    }

    do {
        if (g_impact_ring_read == g_impact_ring_write) break;

        // Read next impact record from ring
        s16* record = (s16*)(&g_impact_ring_buffer[g_impact_ring_read * 100]);

        int mesh_data = *(int*)(record + 0x16);
        int sub_index = *(int*)(record + 0x18);
        int face_index = *(int*)(record + 0x1E);

        // Advance read cursor
        g_impact_ring_read = (g_impact_ring_read + 1) % 15;

        if (mesh_data == 0) continue;

        // Determine UV quadrant from randomness
        int hit_list_count = 0;
        u32 uv_quadrant, uv_index;

        if (*(int*)(record + 0x10) == 0) {
            if (*(int*)(record + 0x14) == 0) {
                uv_quadrant = 0;
                uv_index = 1;
            } else {
                uv_quadrant = 2;
                uv_index = 1;
            }
        } else {
            u32 rng = random_bits();
            uv_quadrant = rng & 3;
            BOOL alt = (uv_quadrant == 0 || uv_quadrant == 2);
            if (alt) {
                rng = random_bits();
                uv_index = (rng & 1) * 2 + 1;
            } else {
                rng = random_bits();
                uv_index = (rng & 1) << 1;
            }
        }

        record[0x16] = 0;
        record[0x17] = 0;

        // Extract position and normal from record
        float pos[3], normal[3];
        pos[0] = *(float*)(record + 0x26);
        pos[1] = *(float*)(record + 0x28);
        pos[2] = *(float*)(record + 0x2A);
        normal[0] = *(float*)(record + 0x2C);
        normal[1] = *(float*)(record + 0x2E);
        normal[2] = *(float*)(record + 0x30);

        // Build hit list starting with primary hit
        int hit_list[12 * 14]; // 12 max hits x 0x38 bytes
        float hit_data[12 * 14];

        hit_list[0] = mesh_data;
        hit_list[1] = sub_index;
        hit_list[4] = face_index;
        hit_data[0] = pos[0];
        hit_data[1] = pos[1];
        hit_data[2] = pos[2];
        hit_data[3] = normal[0];
        hit_data[4] = normal[1];
        hit_data[5] = normal[2];
        hit_list_count = 1;

        // Process each hit in the list (may grow via adjacency)
        for (int h = 0; h < hit_list_count; h++) {
            int* hit_entry = &hit_list[h * 14];
            float* hit_mesh = (float*)hit_entry[0];
            int hit_sub = hit_entry[1];
            int* mesh_ptr = *(int**)((int)hit_mesh + 4);
            int primary = *mesh_ptr;

            u32 bucket = particle_pool_get_bucket((int)hit_mesh);

            // Get face data for this hit
            int face_table = *(int*)(mesh_ptr[primary * -0x30 + hit_sub * 0x30 + 0x27] + 0x1C);
            char* face_entry = (char*)(face_table + hit_entry[4] * 0xE);
            u32 strip_idx = (u8)face_entry[4] & 3;

            int strip_data = mesh_ptr[primary * -0x30 + hit_sub * 0x30 + strip_idx + 0x15]
                           + (u32)*(u16*)(face_entry + 6) * 8;

            // Determine vertex format
            int is_short_format = mesh_ptr[3] & 0x10;
            int vertex_base;
            if (is_short_format == 0) {
                vertex_base = mesh_ptr[primary * -0x30 + hit_sub * 0x30 + strip_idx * 5 + 1]
                            + (u32)*(u16*)(strip_data + 6) * 0xC;
            } else {
                vertex_base = mesh_ptr[primary * -0x30 + hit_sub * 0x30 + strip_idx * 5 + 1]
                            + (u32)*(u16*)(strip_data + 6) * 6;
            }

            // Check if particle has bone attachment
            BOOL has_bone = FALSE;
            u8 bone_id = 0xFF;
            float lifetime_val = hit_data[h * 14 + 4]; // from hit record
            if (lifetime_val != 0.0f) {
                char bone_byte = *(char*)((int)mesh_ptr + hit_sub * 0xC0 + primary * -0xC0 + 0x3D);
                if (bone_byte >= 0) {
                    has_bone = TRUE;
                    bone_id = (u8)bone_byte;
                }
            }

            float* hit_pos = &hit_data[h * 14];
            float* hit_normal = &hit_data[h * 14 + 3];

            // Create billboard decal
            int particle = particle_create_billboard(
                record, uv_quadrant, uv_index, hit_mesh,
                has_bone ? bone_id : 0xFF, bucket,
                hit_pos, hit_normal, vertex_base,
                (float*)(mesh_ptr + 0xB), is_short_format,
                (s16*)(*(int*)(mesh_ptr[primary * -0x30 + hit_sub * 0x30 + 0x27] + 0x2C)
                       + (u32)*(u16*)(face_entry + 8) * 2),
                (int)*face_entry,
                0, 0); // adjacency output buffers

            if (particle == 0) continue;

            // Store entity ref if impact type 1
            if (*record == 1) {
                *(int*)(particle + 0xB8) = *(int*)(record + 0x12);
                *(int*)(particle + 0xBC) = *(int*)(record + 0xE);
            }

            // Test clipped polygon outputs for adjacent face propagation
            // (up to 4 clip plane outputs can trigger new faces)
            u32 clip_results[4 * 8];  // 4 outputs x 0x20 bytes
            int adj_results[4 * 8];

            for (int c = 0; c < 4; c++) {
                if (adj_results[c * 8] != 0 && hit_list_count < 12) {
                    particle_test_adjacent_face(
                        adj_results[c * 8], adj_results[c * 8 + 1],
                        adj_results[c * 8 + 4],
                        hit_pos, hit_normal,
                        (int)hit_list, &hit_list_count);
                }
            }

            // Walk edge neighbor table for additional adjacency
            int edge_table = *(int*)(mesh_ptr[primary * -0x30 + hit_sub * 0x30 + 0x27] + 0x34)
                           + (u32)*(u16*)(face_entry + 10) * 2;
            int num_edges = (int)(u8)face_entry[5];

            for (int e = 0; e < num_edges && hit_list_count < 12; e++) {
                u32 packed = *(u16*)(edge_table + e * 2);
                u32 neighbor_a = packed >> 4;
                u32 neighbor_b = (neighbor_a + 1) % (int)*face_entry;

                // Check if edge connects to clipped vertex pairs
                for (int cv = 0; cv < 8 && hit_list_count < 12; cv++) {
                    u32 clip_a = clip_results[cv * 2];
                    u32 clip_b = (clip_a + 1) % (int)*face_entry;
                    if ((clip_a == neighbor_a && clip_b == neighbor_b) ||
                        (clip_a == neighbor_b && clip_b == neighbor_a)) {
                        particle_test_adjacent_face(
                            hit_mesh, hit_sub,
                            packed & 0xFFF,
                            hit_pos, hit_normal,
                            (int)hit_list, &hit_list_count);
                    }
                }
            }
        }
    } while (process_all == 0);

    particle_tick_context_end();
}
