// =============================================================================
// Character Damage Effect System
// Address: 0x800f3e60 - 0x800f5830 | Batch 305 | Date: 2026-08-25
// =============================================================================
// Manages all visual and audio feedback for character damage: blood splatter
// particles, damage trail billboard rendering along bone splines, 3D positioned
// sound, bone-based hit detection, and damage application dispatch. The system
// supports multiple rendering modes (level 0x65 = special arena, mode 0x0c =
// melee) with different particle configs and scoring behavior.
// =============================================================================

#include "common.h"
#include "entity.h"
#include "particle.h"
#include "math_util.h"
#include "gx.h"
#include "sound.h"
#include "sprite.h"
#include "collision.h"

// --- Data references ---

// Bone chain table: per-damage-bone entries with segment indices
// 0x8040c110[bone_index * 0x5c] = segment count + per-segment bone refs
static const u8* g_bone_chain_table = (u8*)0x8040c110;

// Damage trail sprite IDs
#define SPRITE_DAMAGE_TRAIL_A   0x11D
#define SPRITE_DAMAGE_TRAIL_B   0x290

// Damage sound ID
#define SOUND_DAMAGE_LOOP       0x55E

// Camera data pointer
extern int g_camera_data; // 0x80611d54

// =============================================================================
// Address: 0x800f3e60 | Name: character_damage_effect_wrapper
// Simple wrapper that calls character_damage_spawn_effects.
// Used as a callback entry point from the event/timer system.
// =============================================================================
void character_damage_effect_wrapper(void) {
    character_damage_spawn_effects();
}

// =============================================================================
// Address: 0x800f3e84 | Name: character_damage_apply
// Applies damage to a character entity. If game mode is melee (0x0c), calls
// the effect spawner directly. Otherwise builds a damage dispatch struct with
// type 0x15, time-scaled force, and dispatches via damage_dispatch_to_entity.
// =============================================================================
void character_damage_apply(u32* char_data, int damage_amount) {
    if (g_game_mode_byte == 0x0c) {
        // Melee mode: direct effect spawn
        character_damage_effect_wrapper();
    } else {
        // Build damage dispatch struct
        float dt = CONST_FRAME_TIME *
                   (CONST_EFFECT_INTERVAL / (float)g_target_framerate);

        DamageInfo info;
        info.entity = *(int*)*char_data;
        info.type_a = damage_amount;
        info.type_b = damage_amount;
        info.force = CONST_DAMAGE_FORCE * dt;
        info.damage_type_a = 0x15;
        info.damage_type_b = 0x15;

        switch_slot_reset(&info);
        damage_dispatch_to_entity(&info);
    }
}

// =============================================================================
// Address: 0x800f3f50 | Name: character_damage_spawn_effects
// MASSIVE: Spawns damage visual effects (blood, sparks) on a character.
//
// Flow:
// 1. Check invulnerability (entity flag bit 1 at +0x9c, via FUN_80364290)
// 2. Check game state (mode 0x0c = melee, level 0x65 = special arena)
// 3. If conditions met:
//    a. Set opacity + render state, enable "has damage" flag (bit 2)
//    b. For melee mode: set damage render filter, update team scoring,
//       play feedback sound, manage respawn state
//    c. Release existing particles (character_release_particles)
//    d. Spawn 2-3 directed particle effects at entity world position:
//       - Effect 0x2f: main blood burst (size 0x10)
//       - Effect 0x30: secondary splatter (size 4)
//       - Effect 0x31: tertiary/ground (size 3 or 0xc0 for melee)
//    e. Level 0x65 uses flags 0x12000000/0x10000000 (no gravity)
//       Other levels use flags 0x12020000/0x10020000 (gravity-affected)
// =============================================================================
void character_damage_spawn_effects(void) {
    u32* char_data; // from register context
    int* char_internal = (int*)*char_data;
    int entity = *char_internal;

    // Check invulnerability
    if ((*(u32*)(entity + 0x9c) & 2) != 0) {
        int invuln = check_entity_invulnerability(char_internal, 2);
        if (invuln != 0) goto done;
    }
    if (g_game_mode_byte == 0x0c && g_current_level_id != 0x65) goto done;
    if ((char_data[1] & 4) != 0) goto done;

    // Set damage state
    char_data[0x2d] = (u32)CONST_DAMAGE_OPACITY; // 0x806153c8
    char_data[0x2e] = /* timestamp */;
    char_data[1] |= 4; // "has active damage" flag

    u32 prev_team_flags = g_team_damage_flags;

    if (g_game_mode_byte == 0x0c) {
        // Melee mode: special handling
        g_team_damage_flags |= (1 << char_internal[2]); // per-player bit

        // Set damage render filter
        entity_set_render_type(entity, DAMAGE_FILTER_MELEE_ZERO);
        char_internal[0x296] = g_melee_render_param;

        // Score/respawn management
        if (g_active_player_count <= char_internal[2]) {
            u32 player_flags;
            if (g_game_mode_byte_2 == 0x0a) {
                player_flags = char_internal[5];
            } else {
                player_flags = char_internal[7];
            }
            if ((player_flags & 0x401) == 0) {
                if (char_internal[0x2ab] == 1 && char_internal[0x2aa] == 0) {
                    if (g_game_mode_byte_2 == 0x0a) {
                        player_flags = char_internal[5];
                    } else {
                        player_flags = char_internal[7];
                    }
                    if ((player_flags & 1) != 0) {
                        char_internal[0x2ab] = 0x2000;
                        char_internal[0x2aa] = 0;
                    }
                } else {
                    char_internal[0x2ab] = 0x2000;
                    char_internal[0x2aa] = 0;
                }
                play_damage_feedback_sound(CONST_DAMAGE_SOUND_PITCH, CONST_EFFECT_INTERVAL);
            }
        }

        g_damage_opacity_timer = 0.0f;
        if (g_team_damage_flags == 0) {
            g_team_damage_flags = prev_team_flags;
        }
    }

    // Release existing particle effects
    character_release_particles();

    // Compute scale factor for particles
    float scale = CONST_EFFECT_INTERVAL; // default
    if (*(int*)(entity + 0x150) != 0) {
        scale = CONST_DAMAGE_SCALE_LARGE; // weapon equipped = larger effect
    }

    // Direction vector for particle emission
    float dir[3] = { g_damage_dir_table[0], g_damage_dir_table[1], g_damage_dir_table[2] };
    float* pos = (float*)(entity + 0x64); // entity world position

    if (g_current_level_id == 0x65) {
        // Special arena: no-gravity particles
        spawn_particle_effect(scale * CONST_PARTICLE_SCALE_A, 1.0f, 1.0f,
                             &char_data[0x2f], 0x10, pos, dir, NULL,
                             0x12000000, g_character_fx_callback_handle, entity);
        float secondary_scale = scale * CONST_PARTICLE_SCALE_B;
        spawn_particle_effect(secondary_scale, 1.0f, 1.0f,
                             &char_data[0x30], 4, pos, dir, NULL,
                             0x10000000, g_character_fx_callback_handle, entity);
        spawn_particle_effect(secondary_scale, 1.0f, 1.0f,
                             &char_data[0x31], 3, pos, dir, NULL,
                             0x10000000, g_character_fx_callback_handle, entity);
    } else {
        if (g_game_mode_byte == 0x0c) {
            // Melee mode: clear first two particle slots
            particle_handle_clear(&char_data[0x2f]);
            particle_handle_clear(&char_data[0x30]);
        } else {
            // Normal mode: gravity-affected particles
            spawn_particle_effect(scale * CONST_PARTICLE_SCALE_A,
                                 CONST_GRAVITY_OPACITY, CONST_GRAVITY_OPACITY,
                                 &char_data[0x2f], 0x10, pos, dir, NULL,
                                 0x12020000, g_character_fx_callback_handle, entity);
            spawn_particle_effect(scale * CONST_PARTICLE_SCALE_B,
                                 CONST_GRAVITY_OPACITY_2, CONST_GRAVITY_OPACITY_2,
                                 &char_data[0x30], 4, pos, dir, NULL,
                                 0x10020000, g_character_fx_callback_handle, entity);
        }
        // Tertiary effect (both melee and normal)
        u32 tertiary_size = (g_game_mode_byte == 0x0c) ? 0xc0 : 3;
        spawn_particle_effect(scale * CONST_PARTICLE_SCALE_B,
                             CONST_GRAVITY_OPACITY, CONST_GRAVITY_OPACITY,
                             &char_data[0x31], tertiary_size, pos, dir, NULL,
                             0x10020000, g_character_fx_callback_handle, entity);
    }

done:
    return;
}

// =============================================================================
// Address: 0x800f42e4 | Name: character_damage_find_hit_vertex
// Finds a mesh vertex near a damage hit point for blood decal placement.
// Randomly samples up to 10 triangles from the character mesh, transforms
// vertices from bone-local to world space, and validates face data. Applies
// camera direction bias to offset the result toward the viewer.
//
// Returns world-space position in the output float[3] parameter.
// Falls back to entity center + offset if no valid vertex is found.
// =============================================================================
void character_damage_find_hit_vertex(/* params from register context */) {
    int char_internal; // from calling convention
    float* out_pos;    // output position

    // Initialize to zero
    out_pos[0] = 0.0f;
    out_pos[1] = 0.0f;
    out_pos[2] = 0.0f;

    int attached_entity = *(int*)(char_internal + 0x88);

    // Check if entity has a damage target type (0x1000)
    if (attached_entity != 0 && *(int*)(attached_entity + 0x20) == 0x1000) {
        int sub_data = *(int*)(attached_entity + 0x7c);
        float t = (*(float*)(sub_data + 0x10) - *(float*)(sub_data + 0x18)) /
                  *(float*)(sub_data + 0x1c);
        if (t >= 0.0f) {
            *(float*)(char_internal + 0x74) = CONST_EFFECT_INTERVAL - t;
        }
    }

    if (attached_entity == 0) goto done;
    if (*(int*)(attached_entity + 4) == 0) goto done;
    if ((*(u32*)(attached_entity + 0x28) & 0x80000000) != 0) goto done;

    // Get mesh data
    int* mesh_data = *(int**)(*(int*)(attached_entity + 4) + 4);
    int mesh_count = *mesh_data;
    bool found = false;

    // Build local-to-world matrix
    float local_matrix[16];
    entity_build_local_matrix(local_matrix, attached_entity, 1, 0);

    // Start with entity center + bounding offset
    out_pos[0] = *(float*)(attached_entity + 0x64) + *(float*)(attached_entity + 0x1a8);
    out_pos[1] = *(float*)(attached_entity + 0x68) + *(float*)(attached_entity + 0x1ac);
    out_pos[2] = *(float*)(attached_entity + 0x6c) + *(float*)(attached_entity + 0x1b0);

    // Sample up to 10 random triangles
    for (int attempt = 0; attempt < 10 && !found; attempt++) {
        // Pick random submesh (half the mesh count)
        u32 half_count = mesh_count / 2;
        if (half_count < 1) half_count = 1;
        u32 rand_idx = random_u32() % half_count;

        // Check if face has valid vertex data
        if (*(char*)(mesh_data + mesh_count * -0x30 + rand_idx * 0x30 + 0xf) == 0) {
            // Count valid faces in this submesh
            u32 face_count = 0;
            int face_ptr = mesh_data[mesh_count * -0x30 + rand_idx * 0x30 + 0x15];
            while (face_ptr != 0 && *(char*)(face_ptr + 3) >= 0) {
                face_count++;
                face_ptr += 8;
            }

            if (face_count != 0) {
                // Pick random face
                u32 face_rand = random_u32() % face_count;
                int face = mesh_data[mesh_count * -0x30 + rand_idx * 0x30 + 0x15] + face_rand * 8;

                if (face != 0 && *(char*)(face + 2) != 0) {
                    // Pick random vertex within face
                    u32 vert_rand = random_u32() % *(u8*)(face + 2);
                    int vert_data_ptr = *(int*)(*(int*)(attached_entity + 4) + 4);

                    int vert_index = (u32)*(u16*)(face + 6) + vert_rand;

                    float pos[4];
                    if ((*(u32*)(vert_data_ptr + 0xc) & 0x10) == 0) {
                        // Float format (0xC bytes per vertex)
                        float* vert = (float*)(mesh_data[mesh_count * -0x30 + rand_idx * 0x30 + 1]
                                     + vert_index * 0xc);
                        pos[0] = vert[0];
                        pos[1] = vert[1];
                        pos[2] = vert[2];
                        pos[3] = vert[3];
                    } else {
                        // Short format (0x6 bytes per vertex, needs scale + offset)
                        short* vert = (short*)(mesh_data[mesh_count * -0x30 + rand_idx * 0x30 + 1]
                                     + vert_index * 6);
                        pos[0] = CONST_VERT_SCALE * (float)vert[0] + *(float*)(vert_data_ptr + 0x2c);
                        pos[1] = CONST_VERT_SCALE * (float)vert[1] + *(float*)(vert_data_ptr + 0x30);
                        pos[2] = CONST_VERT_SCALE * (float)vert[2] + *(float*)(vert_data_ptr + 0x34);
                        pos[3] = (float)((int)vert[3] | 0x3f800000); // normalize W
                    }

                    // Transform by bone matrix then local matrix
                    int bone_idx = *(char*)(mesh_data + rand_idx * 0xc0 + mesh_count * -0xc0 + 0x3d);
                    float* bone_mtx = (float*)(*(int*)(*(int*)(attached_entity + 4) + 0x10)
                                     + bone_idx * 0x40);
                    math_transform_point(bone_mtx, pos);
                    math_transform_point(local_matrix, pos);
                    found = true;
                }

                // Apply camera direction bias
                out_pos[0] = pos[0];
                out_pos[1] = pos[1];
                out_pos[2] = pos[2];
                out_pos[0] += CONST_CAMERA_BIAS * *(float*)(g_camera_data + 0x334);
                out_pos[1] += CONST_CAMERA_BIAS * *(float*)(g_camera_data + 0x338);
                out_pos[2] += CONST_CAMERA_BIAS * *(float*)(g_camera_data + 0x33c);
            }
        }
    }

done:
    return;
}

// =============================================================================
// Address: 0x800f465c | Name: character_set_damage_bone
// Sets the damage bone index on a character and enables the damage trail
// rendering flag (0x80000 at +0xc6c). The bone index at +0xd38 determines
// which bone the damage trail effect renders along.
// =============================================================================
void character_set_damage_bone(int entity, int bone_index) {
    if (entity == 0) return;

    int char_data = *(int*)(entity + 0x7c);
    *(u32*)(char_data + 0xc6c) |= 0x80000; // enable damage trail flag
    *(int*)(char_data + 0xd38) = bone_index;
}

// =============================================================================
// Address: 0x800f467c | Name: character_clear_damage_state
// Clears the active damage state. Stops the 3D sound handle, resets it to -1
// (invalid), and clears the damage trail flag (0x80000). Called when the
// damage trail effect should stop rendering.
// =============================================================================
void character_clear_damage_state(int entity) {
    if (entity == 0) return;

    int char_data = *(int*)(entity + 0x7c);
    if ((*(u32*)(char_data + 0xc6c) & 0x80000) == 0) return;

    // Stop 3D sound if playing
    if (*(int*)(entity + 0x22c) >= 0) {
        sound_stop_handle();
        *(int*)(entity + 0x22c) = -1;
    }

    // Clear damage trail flag
    *(u32*)(char_data + 0xc6c) &= ~0x80000;
}

// =============================================================================
// Address: 0x800f46e4 | Name: character_render_damage_trail
// MASSIVE: Renders blood/damage trail billboards along a bone spline using
// GX FIFO quad submission.
//
// Algorithm:
// 1. Check damage trail flag (0x80000) and entity validity
// 2. Compute animation frame position via Catmull-Rom spline (FUN_801317b8)
//    using 3 bone positions (left foot, right foot bone indices from skeleton)
// 3. For each of 2 iterations (both legs):
//    a. Extract 3 bone world positions from bone matrix array
//    b. Transform to world space via entity local matrix
//    c. Interpolate position on spline at current + next frame
//    d. Compute billboard tangent (direction between frames)
//    e. Compute billboard normal via cross product with up vector
//    f. Compute billboard binormal via second cross product
//    g. Normalize all vectors using fast inverse sqrt
//    h. Set GX blend mode, depth write off
//    i. Submit two textured quads per segment:
//       - Sprite 0x11D: main trail texture
//       - Sprite 0x290: secondary overlay (offset along camera vectors)
//    j. Each quad: 4 vertices with position, color (0x50647f3f), and UV
// 4. Uses 3D sound position from entity for positional audio
// =============================================================================
void character_render_damage_trail(void) {
    u32* char_data; // from register context

    // Early out checks
    if ((char_data[1] & 0x80000) == 0) return;
    if (char_data[0x34] == 3 || char_data[0x34] == 0) return;

    int entity = *(int*)*char_data;
    if (entity == 0) return;
    if ((*(u32*)(entity + 0x10) & 0xFFFF) == 0) return;

    // Compute animation time
    float anim_time = get_current_anim_time();
    float dt_scaled = CONST_ANIM_SPEED *
                      (CONST_EFFECT_INTERVAL / (float)g_target_framerate) * anim_time;
    u32 frame = (u32)dt_scaled;
    float frac = dt_scaled - (float)frame;
    int frame_signed = (frame & 1) ^ -(int)(frame >> 31) + (frame >> 31);

    // Update sound position if playing
    if (*(int*)(entity + 0x22c) < 0) {
        sound_get_position(*(int*)(entity + 0x22c), (float*)(entity + 0x64));
    }

    // Build world transform matrix
    float world_matrix[16];
    math_build_scale_matrix(
        *(float*)(entity + 0x64), *(float*)(entity + 0x68), *(float*)(entity + 0x6c),
        (CONST_SCALE_FACTOR * *(float*)(entity + 0x70)) / CONST_SCALE_DIVISOR,
        world_matrix);

    // Render 2 iterations (both leg trails)
    for (int leg = 0; leg < 2; leg++) {
        // Get bone indices for this leg from skeleton data
        int skel_data = *(int*)(*(int*)(entity + 4) + 0x84);
        int bone_indices[3];
        if (leg == 0) {
            bone_indices[0] = *(int*)(skel_data + 0x28);
            bone_indices[1] = *(int*)(skel_data + 0x38);
            bone_indices[2] = *(int*)(skel_data + 0x30);
        } else {
            bone_indices[0] = *(int*)(skel_data + 0x24);
            bone_indices[1] = *(int*)(skel_data + 0x34);
            bone_indices[2] = *(int*)(skel_data + 0x2c);
        }

        // Extract bone world positions
        float bone_pos[3][3];
        int bone_mtx_base = *(int*)(*(int*)(entity + 4) + 0x10);
        for (int b = 0; b < 3; b++) {
            int mtx_offset = bone_indices[b] * 0x40;
            bone_pos[b][0] = *(float*)(bone_mtx_base + mtx_offset + 0x30);
            bone_pos[b][1] = *(float*)(bone_mtx_base + mtx_offset + 0x34);
            bone_pos[b][2] = *(float*)(bone_mtx_base + mtx_offset + 0x38);
            math_transform_point(world_matrix, bone_pos[b]);
        }

        // Compute spline positions at current and next frame
        float pos_curr[3], pos_next[3], spline_tangent[3];
        int prev_frame = frame_signed - 1;
        if (prev_frame < 0) prev_frame = 0;
        int next_frame = frame_signed + 1;
        if (next_frame > 2) next_frame = 2;

        catmull_rom_interpolate(frac, pos_curr,
                               bone_pos[prev_frame], bone_pos[frame_signed],
                               bone_pos[next_frame], spline_tangent);

        // Next frame position
        float frac_next = frac + (1.0f / (float)g_target_framerate);
        int frame_next = frame_signed;
        if (frac_next > 1.0f) {
            frac_next -= 1.0f;
            frame_next++;
        }
        int prev_next = frame_next - 1;
        if (prev_next < 0) prev_next = 0;
        int next_next = frame_next + 1;
        if (next_next > 2) next_next = 2;

        catmull_rom_interpolate(frac_next, pos_next,
                               bone_pos[prev_next], bone_pos[frame_next],
                               bone_pos[next_next], spline_tangent);

        // Compute billboard orientation
        float dir[3] = {
            pos_next[0] - pos_curr[0],
            pos_next[1] - pos_curr[1],
            pos_next[2] - pos_curr[2]
        };
        float dir_len_sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
        if (dir_len_sq > CONST_EPSILON) {
            float inv_len = fast_inv_sqrt(dir_len_sq);
            dir[0] *= inv_len; dir[1] *= inv_len; dir[2] *= inv_len;
        }

        // Cross product with up vector for billboard tangent
        float up[3] = { g_up_vector[0], g_up_vector[1], g_up_vector[2] };
        float tangent[3] = {
            dir[1] * up[2] - dir[2] * up[1],
            dir[2] * up[0] - dir[0] * up[2],
            dir[0] * up[1] - dir[1] * up[0]
        };
        normalize_vector(tangent);

        // Binormal = dir x tangent
        float binormal[3] = {
            dir[1] * tangent[2] - dir[2] * tangent[1],
            dir[2] * tangent[0] - dir[0] * tangent[2],
            dir[0] * tangent[1] - dir[1] * tangent[0]
        };

        // Set GX state
        gx_set_blend_mode(2);
        gx_set_vertex_format(0);
        gx_set_depth_write(2);

        // --- First texture pass (sprite 0x11D) ---
        int tex_a = sprite_get_texture_id(SPRITE_DAMAGE_TRAIL_A);
        gx_set_texture(2, 0x92, 0, 4, tex_a);

        float half_width = CONST_TRAIL_WIDTH * -CONST_TRAIL_SCALE;
        float v0[3] = {
            pos_curr[0] + tangent[0] * half_width + binormal[0] * half_width,
            pos_curr[1] + tangent[1] * half_width + binormal[1] * half_width,
            pos_curr[2] + tangent[2] * half_width + binormal[2] * half_width
        };

        // Submit 4 vertices as GX_QUADS
        GX_BEGIN(GX_QUADS, 4);
        // ... vertex submission with pos, color 0x50647f3f, UV pairs ...
        GX_END();

        // Apply camera offset for tangent position
        spline_tangent[0] += CONST_CAMERA_OFFSET * *(float*)(g_camera_data + 0x334);
        spline_tangent[1] += CONST_CAMERA_OFFSET * *(float*)(g_camera_data + 0x338);
        spline_tangent[2] += CONST_CAMERA_OFFSET * *(float*)(g_camera_data + 0x33c);

        // --- Second texture pass (sprite 0x290) ---
        int tex_b = sprite_get_texture_id(SPRITE_DAMAGE_TRAIL_B);
        gx_set_texture(2, 0x92, 0, 4, tex_b);

        // Submit second quad with camera-relative offset
        // ... similar vertex submission ...
        GX_END();
    }
}

// =============================================================================
// Address: 0x800f5000 | Name: character_damage_sound_and_particles
// MASSIVE: Per-frame damage effect update with 3D positioned sound and
// vertex-based particle spawning along a bone chain.
//
// Algorithm:
// 1. Check damage trail flag (0x80000) and entity validity
// 2. Update 3D sound source at entity position (sound ID 0x55E)
// 3. Compute distance from camera, derive attenuation factor
// 4. Build world transform matrix from entity
// 5. Get head bone position (bone at skeleton offset +0x18)
// 6. Iterate 6 bone chain segments (from bone_chain_table):
//    a. For each step along the chain's time range:
//       - Sample random vertex from the current bone's submesh
//       - Transform vertex to world space (bone + entity matrices)
//       - Spawn positional particle at that vertex
//       - Apply distance-based scale via FUN_80102f74/80102f9c
// =============================================================================
void character_damage_sound_and_particles(void) {
    u32* char_data; // from register context

    if ((char_data[1] & 0x80000) == 0) return;

    int entity = *(int*)*char_data;
    if (entity == 0) return;
    if (*(int*)(entity + 4) == 0) return;

    // Update 3D sound
    sound_update_3d_source(CONST_EFFECT_INTERVAL, CONST_SOUND_RANGE, CONST_EFFECT_INTERVAL,
                          (int*)(entity + 0x22c), (float*)(entity + 0x64),
                          SOUND_DAMAGE_LOOP, 0);

    if ((*(u32*)(entity + 0x10) & 0xFFFF) == 0) return;

    // Distance attenuation from camera
    float dx = *(float*)(entity + 0x64) - *(float*)(g_camera_data + 0x27c);
    float dy = *(float*)(entity + 0x68) - *(float*)(g_camera_data + 0x280);
    float dz = *(float*)(entity + 0x6c) - *(float*)(g_camera_data + 0x284);
    int char_sub = *(int*)(entity + 0x7c);
    float dist_sq = dx*dx + dy*dy + dz*dz;
    float dist = sqrt(dist_sq);
    dist = sqrt(dist); // double sqrt for softer falloff
    float attenuation = dist / CONST_ATTENUATION_RANGE;

    // Build world transform
    float world_matrix[16];
    math_build_scale_matrix(
        *(float*)(entity + 0x64), *(float*)(entity + 0x68), *(float*)(entity + 0x6c),
        (CONST_SCALE_FACTOR * *(float*)(entity + 0x70)) / CONST_SCALE_DIVISOR,
        world_matrix);

    // Advance damage timer
    *(float*)(char_sub + 0xd3c) += CONST_DAMAGE_DECAY_RATE *
        (1.0f / (float)g_target_framerate) * (float)g_frame_delta;
    if (*(float*)(char_sub + 0xd3c) > 1.0f) {
        *(float*)(char_sub + 0xd3c) = 1.0f;
    }

    // Get head bone position
    int bone_mtx_base = *(int*)(*(int*)(entity + 4) + 0x10);
    int head_bone = *(int*)(*(int*)(*(int*)(entity + 4) + 0x84) + 0x18);
    float head_pos[3];
    head_pos[0] = *(float*)(bone_mtx_base + head_bone * 0x40 + 0x30);
    head_pos[1] = *(float*)(bone_mtx_base + head_bone * 0x40 + 0x34);
    head_pos[2] = *(float*)(bone_mtx_base + head_bone * 0x40 + 0x38);
    math_transform_point(world_matrix, head_pos);

    // Iterate 6 bone chain segments
    for (int seg = 0; seg < 6; seg++) {
        float spawn_pos[3] = { head_pos[0], head_pos[1], head_pos[2] };
        u32 step = 0;

        // Get segment bone count from chain table
        int bone_chain_entry = *(int*)(char_sub + 0xd38); // damage bone index
        int segment_count = g_bone_chain_table[bone_chain_entry * 0x5c];
        float time_limit = *(float*)(char_sub + 0xd3c) * (float)segment_count;

        while ((float)step < time_limit) {
            // Compute bone index for this segment step
            int bone_idx_offset = (seg < 3) ? (step / 2) * 2 + 2 : (step / 2) * 2 + 1;
            u32 sub_bone = g_bone_chain_table[bone_chain_entry * 0x5c + bone_idx_offset * 4];

            // Sample random vertex from submesh
            // ... vertex lookup with random face/vert selection ...
            // ... transform by bone matrix + world matrix ...

            // Spawn particle at vertex if not first step
            if (step > 0) {
                particle_emitter_start();
                particle_set_distance_scale(CONST_PARTICLE_DIST_SCALE);
                particle_set_attenuation(attenuation);
            }

            step++;
        }
    }
}

// =============================================================================
// Address: 0x800f54f0 | Name: character_get_damage_vertex_pos
// Gets a vertex position from the character mesh at the damage bone.
// Uses random sampling: picks a random face from the bone's submesh vertex
// group, then a random vertex within that face. Transforms from bone-local
// space to world space. Supports both float (12 bytes/vert) and compressed
// short (6 bytes/vert) vertex formats.
//
// Returns world-space position in the output float[3].
// =============================================================================
void character_get_damage_vertex_pos(/* params from register context */) {
    u32* char_data;
    float* out_pos;

    if ((char_data[1] & 0x80000) == 0) return;

    int entity = *(int*)*char_data;
    if (entity == 0) return;
    if (*(int*)(entity + 4) == 0) return;
    if ((*(u32*)(entity + 0x10) & 0xFFFF) == 0) return;

    int char_sub = *(int*)(entity + 0x7c);

    // Build world transform
    float world_matrix[16];
    math_build_scale_matrix(
        *(float*)(entity + 0x64), *(float*)(entity + 0x68), *(float*)(entity + 0x6c),
        (CONST_SCALE_FACTOR * *(float*)(entity + 0x70)) / CONST_SCALE_DIVISOR,
        world_matrix);

    out_pos[0] = 0.0f;
    out_pos[1] = 0.0f;
    out_pos[2] = 0.0f;

    // Compute vertex index from damage state
    int bone_chain_entry = *(int*)(char_sub + 0xd38);
    int segment_count = g_bone_chain_table[bone_chain_entry * 0x5c];
    int time_steps = (int)(*(float*)(char_sub + 0xd3c) * (float)segment_count);

    // Random offset within time range
    u32 rand_val = random_u32();
    int offset = (int)((float)(time_steps - 1) * ((float)rand_val / CONST_RNG_DIVISOR)
                + CONST_HALF);
    int sub_bone = g_bone_chain_table[bone_chain_entry * 0x5c + (offset + 1) * 4];

    // Sample vertex from submesh
    int* mesh_data = *(int**)(*(int*)(entity + 4) + 4);
    int mesh_count = *mesh_data;

    // Count valid faces
    u32 face_count = 0;
    int face_ptr = mesh_data[mesh_count * -0x30 + sub_bone * 0x30 + 0x15];
    while (face_ptr != 0 && *(char*)(face_ptr + 3) >= 0) {
        face_count++;
        face_ptr += 8;
    }

    if (face_count == 0) return;

    // Pick random face and vertex
    u32 face_rand = random_u32();
    int face_idx = (int)((float)(face_count) * ((float)face_rand / CONST_RNG_DIVISOR)
                  + CONST_HALF);
    int face = mesh_data[mesh_count * -0x30 + sub_bone * 0x30 + 0x15] + face_idx * 8;

    if (face == 0 || *(char*)(face + 2) == 0) return;

    u32 vert_rand = random_u32() % *(u8*)(face + 2);
    int vert_data_ptr = *(int*)(*(int*)(entity + 4) + 4);
    int vert_index = (u32)*(u16*)(face + 6) + vert_rand;

    float pos[4];
    if ((*(u32*)(vert_data_ptr + 0xc) & 0x10) == 0) {
        float* vert = (float*)(mesh_data[mesh_count * -0x30 + sub_bone * 0x30 + 1]
                     + vert_index * 0xc);
        pos[0] = vert[0]; pos[1] = vert[1]; pos[2] = vert[2]; pos[3] = vert[3];
    } else {
        short* vert = (short*)(mesh_data[mesh_count * -0x30 + sub_bone * 0x30 + 1]
                     + vert_index * 6);
        pos[0] = CONST_VERT_SCALE * (float)vert[0] + *(float*)(vert_data_ptr + 0x2c);
        pos[1] = CONST_VERT_SCALE * (float)vert[1] + *(float*)(vert_data_ptr + 0x30);
        pos[2] = CONST_VERT_SCALE * (float)vert[2] + *(float*)(vert_data_ptr + 0x34);
        pos[3] = (float)((int)vert[3] | 0x3f800000);
    }

    // Transform by bone matrix then world matrix
    int bone_idx = *(char*)(mesh_data + sub_bone * 0xc0 + mesh_count * -0xc0 + 0x3d);
    float* bone_mtx = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + bone_idx * 0x40);
    math_transform_point(bone_mtx, pos);
    math_transform_point(world_matrix, pos);

    out_pos[0] = pos[0];
    out_pos[1] = pos[1];
    out_pos[2] = pos[2];
}

// =============================================================================
// Address: 0x800f5830 | Name: character_find_nearest_bone
// Finds the nearest bone to a world-space hit position for damage effect
// attachment. If no hit position provided (NULL), uses the default bone from
// skeleton data (+0x14). Otherwise transforms hit point to entity-local space,
// iterates all bones computing squared distance, and selects the closest.
// Only updates if new distance is less than or equal to the stored maximum.
// Stores: bone index at +0xd38, max distance at +0xd3c.
// =============================================================================
void character_find_nearest_bone(int entity, float* hit_pos) {
    int char_sub = *(int*)(entity + 0x7c);

    // Only update if new hit is at least as close
    if ((*(u32*)(char_sub + 0xc6c) & 0x400000) != 0) {
        if (*(float*)(char_sub + 0xd3c) > /* current_distance */) {
            return; // existing hit is closer
        }
    }

    // Mark as having a damage bone assigned
    *(u32*)(char_sub + 0xc6c) |= 0x400000;
    *(float*)(char_sub + 0xd3c) = /* distance */;

    if (hit_pos == NULL) {
        // Use default bone from skeleton
        int skel = *(int*)(*(int*)(entity + 4) + 0x84);
        *(int*)(char_sub + 0xd38) = *(int*)(skel + 0x14);
    } else {
        // Transform hit point to local space
        float local_matrix[16];
        entity_build_local_matrix(local_matrix, entity, 0, 0);

        float local_hit[3];
        local_hit[0] = hit_pos[0] - *(float*)(entity + 0x64);
        local_hit[1] = hit_pos[1] - *(float*)(entity + 0x68);
        local_hit[2] = hit_pos[2] - *(float*)(entity + 0x6c);

        // Invert matrix and transform
        math_invert_matrix(local_matrix, local_matrix);
        math_transform_point(local_matrix, local_hit);

        // Find nearest bone
        float best_dist = CONST_MAX_BONE_DIST; // 0x80615430
        int bone_count = *(int*)(*(int*)(*(int*)(entity + 4) + 4) + 4);
        int bone_mtx_base = *(int*)(*(int*)(entity + 4) + 0x10);

        for (int i = 0; i < bone_count; i++) {
            float dx = local_hit[0] - *(float*)(bone_mtx_base + i * 0x40 + 0x30);
            float dy = local_hit[1] - *(float*)(bone_mtx_base + i * 0x40 + 0x34);
            float dz = local_hit[2] - *(float*)(bone_mtx_base + i * 0x40 + 0x38);
            float dist_sq = dx*dx + dy*dy + dz*dz;

            if (dist_sq < best_dist) {
                *(int*)(char_sub + 0xd38) = i;
                best_dist = dist_sq;
            }
        }
    }
}
