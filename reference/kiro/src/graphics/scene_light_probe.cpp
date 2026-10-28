// Address: 0x8005c5b4 - 0x8005cafc | Name: scene_light_probe | Date: 2026-08-10
// Scene light probe system: manages per-entity lighting environment probes.
// Probes track entity bone positions and test against portal clip planes to
// determine visibility/lighting contribution. Also handles ambient light
// direction+color extraction from packed level data.
//
// Pool: up to 0x145 (325) probes, stride 0x24 (36 bytes) per slot.
// Table base: DAT_8052f940, count: DAT_80611b38, range: DAT_80611b3c..DAT_80611b38

#include "common.h"
#include "scene.h"
#include "level.h"
#include "math_util.h"

// ============================================================================
// Constants
// ============================================================================

#define LIGHT_PROBE_MAX_SLOTS   0x145   // 325
#define LIGHT_PROBE_SLOT_SIZE   0x24    // 36 bytes per probe entry
#define LIGHT_PROBE_TABLE_SIZE  0x2DB4  // total table size in bytes

#define LIGHT_PROBE_FLAG_HAS_NORMAL   0x02
#define LIGHT_PROBE_FLAG_HAS_MATRIX   0x04
#define LIGHT_PROBE_FLAG_MOVED        0x08

// ============================================================================
// Globals (extern)
// ============================================================================

extern int   g_light_probe_count;           // DAT_80611b38
extern int   g_light_probe_range_start;     // DAT_80611b3c
extern int   g_scene_context_ptr;           // DAT_80611d54
extern float g_light_probe_table[];         // DAT_8052f940 (stride 0x24)
extern int   g_portal_plane_counts[];       // DAT_8052d948
extern float g_portal_planes[];             // DAT_8052d968

// ============================================================================
// 0x8005c5b4 | light_probe_test_aabb_against_planes
// Tests an AABB (defined by min/max corners) against portal clip plane groups.
// Returns 1 if fully outside all groups (not visible), -1 if inside any group.
// Each plane group has up to 5 planes (stride 0x10 per plane, 0x88 per group).
// ============================================================================

int light_probe_test_aabb_against_planes(float *aabb_min, float *aabb_max)
{
    int viewport_index = *(int *)(g_scene_context_ptr + 0x214);
    int num_groups = g_portal_plane_counts[viewport_index];
    int group_offset = 0;

    for (int group = 0; group < num_groups; group++) {
        float *planes = &g_portal_planes[viewport_index * (0x550 / 4) + (group_offset / 4)];
        int all_outside = 0;

        for (int plane_idx = 0; plane_idx < 5; plane_idx++) {
            float *p = planes + plane_idx * 4;

            // Select AABB corner: use max if plane normal >= 0, else min
            float x = (p[0] >= 0.0f) ? aabb_min[0] : aabb_max[0];
            float y = (p[1] >= 0.0f) ? aabb_min[1] : aabb_max[1];
            float z = (p[2] >= 0.0f) ? aabb_min[2] : aabb_max[2];

            float dist = p[0] * x + p[1] * y + p[2] * z + p[3];
            if (dist < 0.0f) {
                all_outside = 1;
                break;
            }
        }

        if (!all_outside) {
            return -1;  // Inside at least one plane group (visible)
        }

        group_offset += 0x88;
    }

    return 1;  // Outside all plane groups (not visible)
}

// ============================================================================
// 0x8005c6a0 | light_probe_clip_against_portals
// Clips light probe polygon vertices against portal clip planes.
// Uses scene_portal_test_clip_planes for the actual polygon/plane intersection.
// Called from scene_portal_visibility_traverse.
// ============================================================================

void light_probe_clip_against_portals(void)
{
    // Acquire iteration context (paired-single register save/restore)
    int ctx = FUN_803826e0();  // get_ps_context
    int poly_data = ctx;       // polygon data at ctx
    short num_verts = *(short *)(poly_data + 0x14);

    int viewport_index = *(int *)(g_scene_context_ptr + 0x214);
    int num_groups = g_portal_plane_counts[viewport_index];
    int group_offset = 0;

    for (int group = 0; group < num_groups; group++) {
        int *clip_planes = (int *)(&g_portal_planes[viewport_index * (0x550 / 4)]
                                   + (group_offset / 4));

        int result = scene_portal_test_clip_planes(
            (float *)(poly_data + 0x18),    // vertices
            (int)num_verts,                  // vertex count
            (int)ctx,                        // context/output
            clip_planes                      // plane set
        );

        if (result != 0) {
            break;  // Fully clipped
        }

        group_offset += 0x88;
    }

    FUN_8038272c();  // release_ps_context
}

// ============================================================================
// 0x8005c74c | light_probe_stub_return_zero
// Unused stub function - always returns 0.
// ============================================================================

int light_probe_stub_return_zero(void)
{
    return 0;
}

// ============================================================================
// 0x8005c754 | light_probe_encode_direction
// Normalizes a direction vector and packs into 3 signed bytes (range -127..127).
// Written to probe entry at offsets +0x11, +0x12, +0x13.
// param_1: probe entry pointer
// param_2: transform matrix (3x4 or 4x4)
// param_3: source direction vector (3 floats)
// ============================================================================

void light_probe_encode_direction(int probe_entry, float *matrix, float *direction)
{
    float dir[3];
    dir[0] = direction[0];
    dir[1] = direction[1];
    dir[2] = direction[2];

    // Transform direction by matrix (rotation only, no translation)
    vec3_transform_normal(matrix, dir);  // FUN_8012fbf4

    float scale = 127.0f;  // FLOAT_80613e38
    *(char *)(probe_entry + 0x11) = (char)(int)(scale * dir[0]);
    *(char *)(probe_entry + 0x12) = (char)(int)(scale * dir[1]);
    *(char *)(probe_entry + 0x13) = (char)(int)(scale * dir[2]);
}

// ============================================================================
// 0x8005c7ec | light_probe_update_positions
// Per-frame update: recalculates world-space positions for all active probes
// from their attached entity bone transforms. Sets MOVED flag if position changed.
// Called from scene_reset_visibility_state and scene rendering pipeline.
// ============================================================================

void light_probe_update_positions(void)
{
    // Save/restore paired-single registers
    FUN_803826e4();  // save_ps_registers

    int range_start = g_light_probe_range_start;
    int slot_offset = range_start * LIGHT_PROBE_SLOT_SIZE;

    for (int i = range_start; i < g_light_probe_count; i++) {
        float *position = (float *)((int)&g_light_probe_table + slot_offset);
        int *entity_ptr_addr = (int *)((int)&g_light_probe_table + slot_offset + 0x1C);

        if (*entity_ptr_addr != 0) {
            int entity = **entity_ptr_addr;
            float old_x = position[0];
            float old_y = position[1];
            float old_z = position[2];

            // Read entity world position (offset 0x4C..0x54 in entity)
            int entity_data = *(int *)((int)&g_light_probe_table + slot_offset + 0x14);
            position[0] = *(float *)(entity_data + 0x4C);
            position[1] = *(float *)(entity_data + 0x50);
            position[2] = *(float *)(entity_data + 0x54);

            // If entity has valid bone data (bone count != 0)
            if (*(char *)(entity + 0x32) != 0) {
                byte flags = *((byte *)&g_light_probe_table + slot_offset + 0x10);
                float bone_matrix[16];

                // Get bone world matrix
                FUN_80075ef0(bone_matrix, entity, 1, 0);  // entity_get_bone_matrix

                int matrix_array = *(int *)(*entity_ptr_addr + 0x10);
                if (matrix_array == 0) {
                    // Use bone matrix directly
                    vec3_extract_translation(bone_matrix, position);  // FUN_8012fa34
                    if ((flags & LIGHT_PROBE_FLAG_HAS_NORMAL) != 0) {
                        light_probe_encode_direction(
                            (int)position, bone_matrix,
                            (float *)(entity_data + 0x88));
                    }
                } else {
                    // Concatenate with sub-bone matrix
                    int bone_index = *(int *)((int)&g_light_probe_table + slot_offset + 0x20);
                    float result_matrix[24];
                    mat4_multiply(
                        (float *)(matrix_array + bone_index * 0x40),
                        bone_matrix, result_matrix);  // FUN_80130bd4
                    vec3_extract_translation(result_matrix, position);
                    if ((flags & LIGHT_PROBE_FLAG_HAS_NORMAL) != 0) {
                        light_probe_encode_direction(
                            (int)position, result_matrix,
                            (float *)(entity_data + 0x88));
                    }
                }
            }

            // Set or clear MOVED flag based on position change
            byte *flags_ptr = (byte *)&g_light_probe_table + slot_offset + 0x10;
            if (old_x == position[0] && old_y == position[1] && old_z == position[2]) {
                *flags_ptr &= ~LIGHT_PROBE_FLAG_MOVED;
            } else {
                *flags_ptr |= LIGHT_PROBE_FLAG_MOVED;
            }
        }

        slot_offset += LIGHT_PROBE_SLOT_SIZE;
    }

    FUN_80382730();  // restore_ps_registers
}

// ============================================================================
// 0x8005c994 | light_probe_pool_clear
// Zeros the entire probe table and resets count to 0.
// Table is 0x2DB4 bytes (325 slots * 36 bytes).
// ============================================================================

void light_probe_pool_clear(void)
{
    g_light_probe_count = 0;
    memset(&g_light_probe_table, 0, LIGHT_PROBE_TABLE_SIZE);
}

// ============================================================================
// 0x8005c9cc | light_probe_pool_reset
// Wrapper that calls light_probe_pool_clear.
// ============================================================================

void light_probe_pool_reset(void)
{
    light_probe_pool_clear();
}

// ============================================================================
// 0x8005c9ec | light_probe_validate_slots
// Iterates all active probe slots. If a slot has NULL entity pointer AND
// bone_index == -1, resets bone_index to 0 (default bone).
// Called during scene initialization to fix up stale slots.
// ============================================================================

void light_probe_validate_slots(void)
{
    if (g_light_probe_count < 1) {
        return;
    }

    int slot_offset = 0;
    int remaining = g_light_probe_count;

    do {
        int *entity_ptr = (int *)((int)&g_light_probe_table + slot_offset + 0x1C);
        int *bone_index = (int *)((int)&g_light_probe_table + slot_offset + 0x20);

        if (*entity_ptr == 0 && *bone_index == -1) {
            *bone_index = 0;
        }

        slot_offset += LIGHT_PROBE_SLOT_SIZE;
        remaining--;
    } while (remaining != 0);
}

// ============================================================================
// 0x8005ca38 | light_probe_set_primary_ambient
// Extracts primary ambient light direction and color from entity light data.
// Color is packed as RGBA in a single 32-bit int at entity_data+0x94.
// Direction is negated from entity_data+0x88..0x90.
// Written to level_def at offsets +0x2C..+0x40 (direction + color).
// ============================================================================

void light_probe_set_primary_ambient(int entity_data)
{
    int level_def = level_get_current_def();
    if (level_def == 0) return;

    unsigned int packed_color = *(unsigned int *)(entity_data + 0x94);
    float inv_255 = 1.0f / 255.0f;  // FLOAT_80613e3c (approx via DOUBLE_80613e40)

    // Unpack RGBA bytes to normalized floats (R at bits 31-24, G at 23-16, B at 15-8)
    *(float *)(level_def + 0x38) = (float)(packed_color >> 24) * inv_255;
    *(float *)(level_def + 0x3C) = (float)((packed_color >> 16) & 0xFF) * inv_255;
    *(float *)(level_def + 0x40) = (float)((packed_color >> 8) & 0xFF) * inv_255;

    // Direction (negated)
    *(float *)(level_def + 0x2C) = -*(float *)(entity_data + 0x88);
    *(float *)(level_def + 0x30) = -*(float *)(entity_data + 0x8C);
    *(float *)(level_def + 0x34) = -*(float *)(entity_data + 0x90);
}

// ============================================================================
// 0x8005cafc | light_probe_set_secondary_ambient
// Extracts secondary ambient light direction and color (same pattern as primary).
// Written to level_def at offsets +0x44..+0x58 (direction + color).
// ============================================================================

void light_probe_set_secondary_ambient(int entity_data)
{
    int level_def = level_get_current_def();
    if (level_def == 0) return;

    unsigned int packed_color = *(unsigned int *)(entity_data + 0x94);
    float inv_255 = 1.0f / 255.0f;

    // Unpack RGBA bytes to normalized floats
    *(float *)(level_def + 0x50) = (float)(packed_color >> 24) * inv_255;
    *(float *)(level_def + 0x54) = (float)((packed_color >> 16) & 0xFF) * inv_255;
    *(float *)(level_def + 0x58) = (float)((packed_color >> 8) & 0xFF) * inv_255;

    // Direction (negated)
    *(float *)(level_def + 0x44) = -*(float *)(entity_data + 0x88);
    *(float *)(level_def + 0x48) = -*(float *)(entity_data + 0x8C);
    *(float *)(level_def + 0x4C) = -*(float *)(entity_data + 0x90);
}

// ============================================================================
// 0x8005cbc0 | light_probe_register_entities
// Master probe registration: iterates all scene entities, allocates probe slots
// for each light-emitting sub-entity. Stores radius^2, flags, entity pointer,
// bone index. Entities with flag 0x04 → primary ambient, flag 0x80 → secondary.
// Pool max: 0x145 (325) slots. Called once per scene load from FUN_80061598.
// ============================================================================

void light_probe_register_entities(void)
{
    int ctx = FUN_803826e0();  // get_scene_context
    int search_start = 0;

    int *list_end = *(int **)(ctx + 4);
    int list_count = *list_end;  // negative offset count

    for (int *node = list_end + list_count * -0x30; node < list_end; node += 0x30) {
        int sub_count = *(char *)((int)node + 0x42);
        int sub_offset = 0;

        for (int sub = 0; sub < sub_count; sub++) {
            int entity_data = node[0x18] + sub_offset;

            // Copy previous frame's light byte
            *(char *)(entity_data + 0xAD) = *(char *)(entity_data + 0xAC);

            int prev_count = g_light_probe_count;

            if ((*(unsigned int *)(entity_data + 4) & 4) != 0) {
                // Flag 0x04: primary ambient light source
                light_probe_set_primary_ambient(entity_data);
            }
            else if ((*(unsigned int *)(entity_data + 4) & 0x80) != 0) {
                // Flag 0x80: secondary ambient light source
                light_probe_set_secondary_ambient(entity_data);
            }
            else {
                // Standard probe: find free slot
                int slot_offset = search_start * LIGHT_PROBE_SLOT_SIZE;
                while (*(int *)((int)&g_light_probe_table + slot_offset + 0x1C) != 0 ||
                       *(int *)((int)&g_light_probe_table + slot_offset + 0x20) == -1) {
                    search_start++;
                    slot_offset += LIGHT_PROBE_SLOT_SIZE;
                }

                if (g_light_probe_count <= search_start) {
                    search_start = g_light_probe_count;
                }

                if (search_start < LIGHT_PROBE_MAX_SLOTS) {
                    int s = search_start;
                    int off = s * LIGHT_PROBE_SLOT_SIZE;

                    // Store radius squared
                    float radius = *(float *)(entity_data + 0xA8);
                    *(float *)((int)&g_light_probe_table + off + 0x0C) = radius * radius;

                    // Store matrix pointer, entity data, entity context, bone index
                    *(int *)((int)&g_light_probe_table + off + 0x18) = node[0x14];
                    *(int *)((int)&g_light_probe_table + off + 0x14) = entity_data;
                    *(int *)((int)&g_light_probe_table + off + 0x1C) = ctx;
                    *(int *)((int)&g_light_probe_table + off + 0x20) = 0; // bone index

                    // Flags: bit 0 = (entity_flags >> 4) & 1
                    byte *flags = (byte *)((int)&g_light_probe_table + off + 0x10);
                    *flags = (byte)((*(unsigned int *)(entity_data + 4) >> 4) & 1);

                    // Encode light direction if entity has normal data
                    if (*(short *)(entity_data + 0x0E) != 0) {
                        float scale = 127.0f;
                        *(char *)((int)&g_light_probe_table + off + 0x11) =
                            (char)(int)(scale * *(float *)(entity_data + 0x88));
                        *(char *)((int)&g_light_probe_table + off + 0x12) =
                            (char)(int)(scale * *(float *)(entity_data + 0x8C));
                        *(char *)((int)&g_light_probe_table + off + 0x13) =
                            (char)(int)(scale * *(float *)(entity_data + 0x90));
                        *flags |= LIGHT_PROBE_FLAG_HAS_NORMAL;
                    }

                    // Flag 0x04 if node has matrix
                    if (node[0x14] != 0) {
                        *flags |= LIGHT_PROBE_FLAG_HAS_MATRIX;
                    }

                    if (prev_count <= search_start) {
                        g_light_probe_count++;
                    }
                }
            }

            sub_offset += 0xB4;
        }
    }

    FUN_8038272c();  // release_scene_context
}

// ============================================================================
// 0x8005cd90 | light_probe_unregister_by_entity
// Removes all probe slots belonging to a given entity context pointer.
// Sets entity_ptr=0, data_ptr=0, bone_index=-1 for matching slots.
// Iterates backwards from count-1 to 0.
// ============================================================================

void light_probe_unregister_by_entity(int entity_ctx)
{
    int offset = (g_light_probe_count - 1) * LIGHT_PROBE_SLOT_SIZE;
    int remaining = g_light_probe_count;

    if (g_light_probe_count - 1 < 0) return;

    do {
        if (*(int *)((int)&g_light_probe_table + offset + 0x1C) == entity_ctx) {
            *(int *)((int)&g_light_probe_table + offset + 0x14) = 0;  // data_ptr
            *(int *)((int)&g_light_probe_table + offset + 0x1C) = 0;  // entity_ptr
            *(int *)((int)&g_light_probe_table + offset + 0x20) = -1; // bone_index
        }
        offset -= LIGHT_PROBE_SLOT_SIZE;
        remaining--;
    } while (remaining != 0);
}

// ============================================================================
// 0x8005cde4 | light_probe_priority_insert
// Inserts a probe candidate into a sorted priority queue (max 5 entries).
// Queue is sorted by distance (field +0x2C). Shifts lower-priority entries
// down to make room. If all 5 slots are closer, the new entry replaces the
// head and the displaced entry goes to the output pointer.
// param_1: queue array base (5 entries, each a pointer at offsets 0x10..0x00)
// param_2: pointer to candidate probe entry pointer
// ============================================================================

void light_probe_priority_insert(int queue_base, int *candidate_ptr)
{
    int head_entry = *(int *)(queue_base + 0x10);

    // If head is valid and closer than candidate, skip
    if (*(int *)(head_entry + 4) >= 0 &&
        *(float *)(head_entry + 0x2C) <= *(float *)(*candidate_ptr + 0x2C)) {
        return;
    }

    // Shift entries down (from slot 3 to slot 0)
    for (int i = 3, off = 0x0C; i >= 0; i--, off -= 4) {
        int *slot = (int *)(queue_base + off);
        int entry = *slot;

        if (*(int *)(entry + 4) >= 0 &&
            *(float *)(entry + 0x2C) <= *(float *)(*candidate_ptr + 0x2C)) {
            // Found insertion point: link displaced entry
            slot[1] = *candidate_ptr;
            *candidate_ptr = head_entry;
            return;
        }
        slot[1] = entry;

        if (i == 0) {
            *slot = *candidate_ptr;
            *candidate_ptr = head_entry;
        }
    }
}

// ============================================================================
// 0x8005ce78 | light_probe_verify_visibility
// Verifies probe candidates via ray-portal traversal. For each of the 5
// candidate slots, traces a ray from the probe position to the query point.
// If the ray doesn't reach the same portal node, the probe is invalidated.
// Keeps up to param_6 valid probes, swaps valid ones to the front.
// ============================================================================

void light_probe_verify_visibility(
    void *ps_ctx, float *query_pos, int src_node, int max_valid)
{
    int ctx = FUN_803826dc();  // get_ps_context (returns ptr pair)
    int *slots = (int *)(ctx >> 32);     // high word = slot array
    float *pos = (float *)(int)ctx;      // low word = position

    int valid_count = 0;
    int valid_offset = 0;

    if (src_node < 1 || src_node > DAT_80611b14) {
        // Invalid source node: invalidate all 5 slots
        for (int i = 0; i < 5; i++) {
            int *slot = (int *)(slots + i);
            *(int *)(*slot + 4) = -1;
        }
    } else {
        // Verify each slot via portal ray traversal
        for (int i = 0; i < 5; i++) {
            int entry = slots[i];
            if (*(int *)(entry + 4) < 0) continue;

            // Direction = probe_pos - query_pos
            float dir[3];
            dir[0] = *(float *)(entry + 0x20) - pos[0];
            dir[1] = *(float *)(entry + 0x24) - pos[1];
            dir[2] = *(float *)(entry + 0x28) - pos[2];

            int dest_node = scene_ray_traverse_portals(
                pos, src_node, dir, 0);

            if (*(int *)(entry) == dest_node) {
                // Visible: swap to front of valid list
                int *valid_slot = (int *)(slots + valid_offset / 4);
                int temp = *valid_slot;
                valid_count++;
                *valid_slot = entry;
                valid_offset += 4;
                slots[i] = temp;
                if (valid_count >= max_valid) break;
            } else {
                *(int *)(entry + 4) = -1;  // Mark invalid
            }
        }
    }

    FUN_80382728();  // release_ps_context
}

// ============================================================================
// 0x8005cf94 | light_probe_lerp_color
// Linearly interpolates between two packed RGBA colors.
// param_1: interpolation factor (0.0 = color_a, 1.0 = color_b)
// param_2: output float[3] (RGB)
// param_3: packed color A (RGBA in bytes 31-24, 23-16, 15-8, 7-0)
// param_4: packed color B
// ============================================================================

void light_probe_lerp_color(float t, float *out_rgb, unsigned int color_a, unsigned int color_b)
{
    float inv_t = 1.0f - t;

    float r_a = (float)(color_a >> 24);
    float g_a = (float)((color_a >> 16) & 0xFF);
    float b_a = (float)((color_a >> 8) & 0xFF);

    float r_b = (float)(color_b >> 24);
    float g_b = (float)((color_b >> 16) & 0xFF);
    float b_b = (float)((color_b >> 8) & 0xFF);

    out_rgb[0] = r_a * t + r_b * inv_t;
    out_rgb[1] = g_a * t + g_b * inv_t;
    out_rgb[2] = b_a * t + b_b * inv_t;
}

// ============================================================================
// 0x8005d058 | light_probe_gather_and_evaluate
// MASSIVE main per-frame light gathering function. For a given world position:
// 1. Finds nearest probes within range (up to 325 candidates)
// 2. Inserts into priority queue (5 slots, sorted by distance)
// 3. Verifies visibility via portal ray traversal (keeps up to 3)
// 4. Evaluates falloff (distance / dot product with probe normal)
// 5. Computes final RGB contribution per probe (handles time-of-day lerp)
// 6. Outputs up to (3 - param_8) light descriptors to param_7 array
// Called from FUN_8005e500 (entity lighting update).
// ============================================================================

void light_probe_gather_and_evaluate(
    void *ps_ctx, float *query_pos, int src_node,
    int param_7_output, int existing_count)
{
    // [MASSIVE function - 0x6EC bytes]
    // Key algorithm:
    // - Spatial query via FUN_801340a0 (sorted by distance)
    // - For each candidate probe within radius:
    //   * Compute direction vector and distance
    //   * sqrt distance via FUN_80131538
    //   * Normalize direction
    //   * Dot product with probe normal for backface rejection
    //   * Insert into priority queue via light_probe_priority_insert
    // - Verify top candidates via light_probe_verify_visibility (max 3)
    // - For each verified probe:
    //   * Compute falloff: linear ramp between inner/outer radius
    //   * Scale by intensity byte (entity_data+0xAD → float)
    //   * If entity has time-of-day flag: lerp colors via light_probe_lerp_color
    //   * Else: unpack single RGBA color, scale by falloff
    //   * Store result pointer to output array
    //
    // Uses DAT_803cc1fc as template for light descriptor (7 words)
    // Priority queue: 5 entries at local_80[0..4]
    // Output: up to 3 light descriptors at param_7[existing_count..]
    //
    // See raw decompilation for full implementation details.
    // PC port note: replace FUN_80131538 with sqrtf(), simplify register saves.
}

// ============================================================================
// 0x8005d744 | light_probe_compute_entity_color
// Computes ambient color for an entity. Recursive (calls itself for parent).
// Reads entity type ID (offset +0x34 in entity data):
//   - Types 0xCA-0xCD: vehicle entities → reads from vehicle table
//   - Type 0xD5: special white entity
//   - Others: reads from global color table DAT_8056ffe0 (stride 12, 3 floats)
// Applies visibility blend (flag 0x100000 + 0x1000000) and minimum threshold.
// Stores result as 3 bytes at entity offsets +0x78, +0x79, +0x7A (scaled by 255).
// Sets "color computed" flag (bit 24 of entity+0x0C).
// ============================================================================

void light_probe_compute_entity_color(void)
{
    // Save/restore paired-single registers
    FUN_803826f0();

    int *entity_ctx = /* from register context */;
    float *out_rgb = /* from register context */;
    int entity_data = *entity_ctx;
    int type_id = *(int *)(entity_data + 0x34);

    // Read previous frame's color (bytes → float via /255)
    float prev_r = (float)*(unsigned char *)((int)entity_ctx + 0x78) / 255.0f;
    float prev_g = (float)*(unsigned char *)((int)entity_ctx + 0x79) / 255.0f;
    float prev_b = (float)*(unsigned char *)((int)entity_ctx + 0x7A) / 255.0f;

    if ((*(unsigned int *)(entity_data + 0x0C) & 0x1000000) == 0) {
        // Color not yet computed this frame
        int parent = 0;

        if (type_id >= 0xCA && type_id <= 0xCD) {
            // Vehicle: get parent from vehicle table
            parent = *(int *)(*(int *)(DAT_80611d78 + (type_id - 0xCA) * 0x141C + 0x14) + 4);
        } else {
            // Standard entity: check parent pointer
            if (*(int **)(entity_data + 0x78) != NULL) {
                parent = *(int *)(**(int **)(entity_data + 0x78) + 4);
            }
        }

        if (parent != 0) {
            // Recursive: compute parent color first
            light_probe_compute_entity_color(/* parent context */);
        } else {
            // Leaf: read from global color table or special cases
            if (type_id == 0xD5) {
                out_rgb[0] = out_rgb[1] = out_rgb[2] = 1.0f;  // White
            } else if (DAT_80612614 == 100) {
                // Read from level color table
                out_rgb[0] = g_entity_color_table[type_id * 3 + 0];
                out_rgb[1] = g_entity_color_table[type_id * 3 + 1];
                out_rgb[2] = g_entity_color_table[type_id * 3 + 2];
            } else {
                out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.5f;  // Default gray
            }

            // Minimum threshold clamp
            if (out_rgb[0] < 0.25f) {
                out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.1f;
            }

            // Lighting disabled check
            if (DAT_8060fad8 == 0) {
                out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.25f;
            }

            // Visibility blend with probe influence
            if ((*(unsigned int *)(entity_data + 0x28) & 0x100000) != 0 &&
                (*(unsigned int *)(entity_data + 0x10) & 0x1000000) != 0) {
                float blend = 0.7f;  // FLOAT_80613e64
                out_rgb[0] = blend * (out_rgb[0] - prev_r) + prev_r;
                out_rgb[1] = blend * (out_rgb[1] - prev_g) + prev_g;
                out_rgb[2] = blend * (out_rgb[2] - prev_b) + prev_b;
            }
        }

        // Store computed color as bytes and set flag
        *(unsigned int *)(entity_data + 0x0C) |= 0x1000000;
        *(char *)((int)entity_ctx + 0x78) = (char)(int)(255.0f * out_rgb[0]);
        *(char *)((int)entity_ctx + 0x79) = (char)(int)(255.0f * out_rgb[1]);
        *(char *)((int)entity_ctx + 0x7A) = (char)(int)(255.0f * out_rgb[2]);
    } else {
        // Already computed: return cached values
        out_rgb[0] = prev_r;
        out_rgb[1] = prev_g;
        out_rgb[2] = prev_b;
    }

    FUN_8038273c();  // restore_ps_registers
}

// ============================================================================
// 0x8005d98c | light_probe_compute_final_color
// Extends entity color computation: after computing base color via
// light_probe_compute_entity_color, adds vehicle headlight contribution
// and self-illumination blend. For vehicle entities (0xCA-0xCD):
// - Queries headlight state via FUN_801af334 (slots 0 and 1)
// - Gets headlight source position via FUN_801af364 → FUN_800b2440
// - Blends headlight color (stronger if entity IS the headlight source)
// Also applies self-illumination overlay from entity byte +0x7F.
// ============================================================================

void light_probe_compute_final_color(void)
{
    FUN_803826e4();  // save_ps_registers

    int *entity_ctx = /* from register context */;
    float *out_rgb = /* from register context */;

    int entity_data = *entity_ctx;
    int type_id = *(int *)(entity_data + 0x34);

    // Determine if this is a vehicle entity
    int is_vehicle = 0;
    int vehicle_ctx = 0;
    if (type_id > 0xC9 && type_id < 0xCE) {
        is_vehicle = 1;
        vehicle_ctx = DAT_80611d78 + (type_id - 0xCA) * 0x141C;
    }

    // Get parent entity for color computation
    int *parent;
    if (is_vehicle) {
        parent = *(int **)(*(int *)(vehicle_ctx + 0x14) + 4);
    } else {
        int *parent_ptr = *(int **)(entity_data + 0x78);
        parent = entity_ctx;
        if (parent_ptr != NULL) {
            parent = *(int **)(*parent_ptr + 4);
        }
    }

    // Compute base entity color (recursive)
    light_probe_compute_entity_color();

    // Add probe contribution from parent's bytes (+0x7C, +0x7D, +0x7E)
    float probe_add_r = (float)*(unsigned char *)((int)parent + 0x7C) / 255.0f;
    float probe_add_g = (float)*(unsigned char *)((int)parent + 0x7D) / 255.0f;
    float probe_add_b = (float)*(unsigned char *)((int)parent + 0x7E) / 255.0f;
    out_rgb[0] = fmin(out_rgb[0] + probe_add_r, 1.0f);
    out_rgb[1] = fmin(out_rgb[1] + probe_add_g, 1.0f);
    out_rgb[2] = fmin(out_rgb[2] + probe_add_b, 1.0f);

    // Vehicle headlight contribution
    if (is_vehicle) {
        int headlight_a = FUN_801af334(vehicle_ctx, 0);
        int headlight_b = FUN_801af334(vehicle_ctx, 1);
        int headlight_id = -1;

        if (headlight_a != 0 || headlight_b != 0) {
            headlight_id = FUN_801af364(vehicle_ctx);
        }

        if (headlight_id != -1) {
            float light_color[3];
            FUN_800b2440(headlight_id, light_color);

            float strength;
            // Stronger blend if this entity IS the headlight source
            if ((headlight_a != 0 && *(int **)(*(int *)(vehicle_ctx + 0x1D8) + 4) == entity_ctx) ||
                (headlight_b != 0 && *(int **)(*(int *)(vehicle_ctx + 0x538) + 4) == entity_ctx)) {
                strength = 0.5f;  // FLOAT_80613e58
            } else {
                strength = 0.7f;  // FLOAT_80613e64
            }

            out_rgb[0] = fmin(out_rgb[0] + strength * light_color[0], 1.0f);
            out_rgb[1] = fmin(out_rgb[1] + strength * light_color[1], 1.0f);
            out_rgb[2] = fmin(out_rgb[2] + strength * light_color[2], 1.0f);
        }
    }

    // Self-illumination overlay (byte at entity_ctx+0x7F)
    unsigned int self_illum = *(unsigned char *)((int)entity_ctx + 0x7F);
    if (self_illum != 0) {
        float illum_scale = (float)self_illum / 255.0f;  // 0..1
        float blend_factor = 1.0f - illum_scale / 255.0f;
        float illum_color_r = (float)*(unsigned char *)((int)entity_ctx + 499) / 255.0f;
        float illum_color_g = (float)*(unsigned char *)(entity_ctx + 0x7D) / 255.0f;
        float illum_color_b = (float)*(unsigned char *)((int)entity_ctx + 0x1F5) / 255.0f;

        out_rgb[0] = out_rgb[0] * blend_factor + illum_color_r * illum_scale;
        out_rgb[1] = out_rgb[1] * blend_factor + illum_color_g * illum_scale;
        out_rgb[2] = out_rgb[2] * blend_factor + illum_color_b * illum_scale;
    }

    FUN_80382730();  // restore_ps_registers
}

// ============================================================================
// 0x8005dcc8 | light_probe_gather_with_vehicle
// Vehicle-specific light gathering variant. Detects vehicle context
// (entity type 0xCA-0xCD), computes vehicle headlight offset position,
// then delegates to FUN_800b24dc for the main light gather.
// If a valid vehicle headlight is found, stores its descriptor at
// param_7+8 pointing to global DAT_8053282c.
// ============================================================================

void light_probe_gather_with_vehicle(
    void *ps_ctx, float *query_pos, int param_6, int param_7)
{
    FUN_803826d8();  // save_ps_context

    int *entity_ctx = /* from register context */;
    int entity_data = *entity_ctx;
    int type_id = *(int *)(entity_data + 0x34);

    DAT_80532830 = -1;  // Reset vehicle light state

    if (type_id > 0xC9 && type_id < 0xCE) {
        // Vehicle entity: check headlights
        int vehicle_ctx = DAT_80611d78 + (type_id - 0xCA) * 0x141C;
        float *light_offset = NULL;

        int headlight_a = FUN_801af334(vehicle_ctx, 0);
        int headlight_b = FUN_801af334(vehicle_ctx, 1);

        if (headlight_a != 0) {
            light_offset = (float *)(vehicle_ctx + 0x184);
        } else if (headlight_b != 0) {
            light_offset = (float *)(vehicle_ctx + 0x4E4);
        }

        if (light_offset != NULL) {
            int headlight_id = FUN_801af364(vehicle_ctx);
            if (headlight_id != -1) {
                DAT_80532830 = 0;
                DAT_80532858 = 1.0f;

                // Get headlight color
                FUN_800b2440(headlight_id, &DAT_80532834);

                // Compute world position = offset + vehicle transform * scale
                int vehicle_data = *(int *)(vehicle_ctx + 8);
                float scale = 0.8f;  // FLOAT_80613e6c
                DAT_8053284c = scale * *(float *)(vehicle_data + 0x364) + light_offset[0];
                DAT_80532850 = scale * *(float *)(vehicle_data + 0x368) + light_offset[1];
                DAT_80532854 = scale * *(float *)(vehicle_data + 0x36C) + light_offset[2];

                // Direction = light_pos - query_pos
                DAT_80532840 = DAT_8053284c - query_pos[0];
                DAT_80532844 = DAT_80532850 - query_pos[1];
                DAT_80532848 = DAT_80532854 - query_pos[2];
            }
        }
    }

    // Delegate to main entity light gather
    FUN_800b24dc(entity_ctx, query_pos, param_6, param_7);

    // If vehicle light was found, attach descriptor
    if (DAT_80532830 >= 0) {
        *(int *)(param_7 + 8) = (int)&DAT_8053282c;
    }

    FUN_80382724();  // restore_ps_context
}

// ============================================================================
// 0x8005de48 | light_probe_setup_directional_lights
// Sets up 3 directional light sources from level_def ambient data.
// Converts spherical coordinates (angles at level_def+0x30..+0x44) to
// cartesian direction vectors using sin/cos lookup table (DAT_80583228,
// 2048 entries, period = 2*PI mapped to 0x800 entries).
// Each light: direction = -magnitude * (cos_elev*sin_azim, sin_elev, cos_elev*cos_azim)
// Normalizes each direction (Newton-Raphson inverse sqrt, 2 iterations).
// Translates directions to world position (adds entity position).
// Stores into local descriptor array and passes to renderer (FUN_80182740).
// ============================================================================

void light_probe_setup_directional_lights(void)
{
    FUN_803826f0();  // save_ps_registers (9 pairs)

    int entity_ctx = /* from register r4 */;
    int level_def = *(int *)(entity_ctx + 0x254);

    // Pointers to 3 output direction vectors
    float *dir_a = &local_118;  // primary
    float *dir_b = &local_124;  // secondary
    float *dir_c = &local_130;  // tertiary

    // Sin/cos table at DAT_80583228 (2048 entries, stride 4)
    // Index = angle * (2048 / 2PI) + 512 (quarter-turn offset for cos)

    // For each of 3 lights (data at level_def + {0x30,0x3C,0x48} / {0x34,0x40,0x44}):
    for (int light = 0; light < 3; light++) {
        int base = level_def + 0x0C + light * 0x0C;
        float magnitude = -*(float *)(level_def + 0x48 + light * 4);

        // Azimuth and elevation angles
        float azim = *(float *)(base + 0x24);
        float elev = *(float *)(base + 0x30);

        // Convert to table indices
        unsigned int azim_idx = (unsigned int)(azim * 325.9493f + 512.0f);  // FLOAT_80613e70/6c
        unsigned int elev_idx = (unsigned int)(elev * 325.9493f + 512.0f);

        // Lookup sin/cos (table wraps at 0x7FF, cos = sin + 0x200)
        float sin_azim = g_sincos_table[azim_idx & 0x7FF];
        float cos_azim = g_sincos_table[(azim_idx + 0x200) & 0x7FF];
        float sin_elev = g_sincos_table[elev_idx & 0x7FF];
        float cos_elev = g_sincos_table[(elev_idx + 0x200) & 0x7FF];

        // Direction = magnitude * (cos_elev * sin_azim, sin_elev, cos_elev * cos_azim)
        float *dir = (light == 0) ? dir_a : (light == 1) ? dir_b : dir_c;
        dir[0] = magnitude * cos_azim;           // X
        dir[1] = magnitude * sin_elev;           // Y
        dir[2] = magnitude * sin_azim * cos_elev; // Z
    }

    // Normalize each direction (Newton-Raphson rsqrt, 2 iterations)
    for (int i = 0; i < 3; i++) {
        float *dir = (i == 0) ? dir_a : (i == 1) ? dir_b : dir_c;
        float len_sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
        float inv_len = fast_rsqrt_nr2(len_sq);  // 2-iteration Newton-Raphson
        float scale = 0.5f * inv_len;  // final normalized magnitude
        dir[0] *= scale;
        dir[1] *= scale;
        dir[2] *= scale;
    }

    // Get entity world position
    float entity_pos[3];
    FUN_80075e88(entity_pos, entity_ctx);

    // Translate directions to world positions
    dir_a[0] += entity_pos[0]; dir_a[1] += entity_pos[1]; dir_a[2] += entity_pos[2];
    dir_b[0] += entity_pos[0]; dir_b[1] += entity_pos[1]; dir_b[2] += entity_pos[2];
    dir_c[0] += entity_pos[0]; dir_c[1] += entity_pos[1]; dir_c[2] += entity_pos[2];

    // Pack into descriptor and submit to renderer
    // local_170/16c/168 = level_def offsets for color data
    FUN_80182740();  // submit_directional_lights

    FUN_8038273c();  // restore_ps_registers
}

// ============================================================================
// 0x8005e288 | light_probe_extract_directional_colors
// Extracts color + direction for up to 4 directional lights from level_def.
// Uses sin/cos lookup table (DAT_80583228) to convert spherical angles to
// cartesian directions. Only writes lights with intensity >= 0.25 (FLOAT_80613e5c).
// Light data in level_def at floats [0..11] = 4 lights * 3 (intensity,G,B).
// Angles at floats [12..17] = 6 values (azim0,elev0,azim1,elev1,azim2,elev2).
// param_3: array of 4 color output pointers
// param_4: array of 4 direction output pointers (only 3 have dirs, 4th is ambient)
// ============================================================================

void light_probe_extract_directional_colors(
    void *entity_ctx, int entity_data, int *color_ptrs, int *dir_ptrs)
{
    FUN_803826f0();  // save_ps_registers

    float *level_def = *(float **)(entity_data + 0x254);  // extraout_r4

    // Convert 6 angles to sin/cos indices
    unsigned int idx[6];
    for (int i = 0; i < 6; i++) {
        idx[i] = (unsigned int)(325.9493f * level_def[12 + i] + 512.0f);
    }

    // Lookup sin/cos for each angle pair
    float sin_a[3], cos_a[3], sin_e[3], cos_e[3];
    for (int i = 0; i < 3; i++) {
        sin_a[i] = g_sincos_table[idx[i*2] & 0x7FF];
        cos_a[i] = g_sincos_table[(idx[i*2] + 0x200) & 0x7FF];
        sin_e[i] = g_sincos_table[idx[i*2+1] & 0x7FF];
        cos_e[i] = g_sincos_table[(idx[i*2+1] + 0x200) & 0x7FF];
    }

    // Light 0 (direction + color) - only if intensity >= 0.25
    if (level_def[3] >= 0.25f) {
        *(float *)color_ptrs[0] = level_def[3];
        *(float *)(color_ptrs[0] + 4) = level_def[4];
        *(float *)(color_ptrs[0] + 8) = level_def[5];
        *(float *)dir_ptrs[0] = -cos_a[0];
        *(float *)(dir_ptrs[0] + 4) = -sin_e[0];
        *(float *)(dir_ptrs[0] + 8) = -sin_a[0] * cos_e[0];
    }

    // Light 1
    if (level_def[6] >= 0.25f) {
        *(float *)color_ptrs[1] = level_def[6];
        *(float *)(color_ptrs[1] + 4) = level_def[7];
        *(float *)(color_ptrs[1] + 8) = level_def[8];
        *(float *)dir_ptrs[1] = -cos_a[1];
        *(float *)(dir_ptrs[1] + 4) = -sin_e[1];
        *(float *)(dir_ptrs[1] + 8) = -sin_a[1] * cos_e[1];
    }

    // Light 2
    if (level_def[9] >= 0.25f) {
        *(float *)color_ptrs[2] = level_def[9];
        *(float *)(color_ptrs[2] + 4) = level_def[10];
        *(float *)(color_ptrs[2] + 8) = level_def[11];
        *(float *)dir_ptrs[2] = -cos_a[2];
        *(float *)(dir_ptrs[2] + 4) = -sin_e[2];
        *(float *)(dir_ptrs[2] + 8) = -sin_a[2] * cos_e[2];
    }

    // Light 3 (ambient only - no direction)
    if (level_def[0] >= 0.25f) {
        *(float *)color_ptrs[3] = level_def[0];
        *(float *)(color_ptrs[3] + 4) = level_def[1];
        *(float *)(color_ptrs[3] + 8) = level_def[2];
    }

    FUN_8038273c();  // restore_ps_registers
}

// ============================================================================
// 0x8005e500 | light_probe_entity_lighting_update
// MASSIVE per-entity lighting orchestrator (non-skinned path, 0x898 bytes).
// Called when entity doesn't have level_def (no directional light source).
// 1. Copies parent probe indices if entity has parent
// 2. Computes world position (entity pos + offset, or vehicle position)
// 3. Gathers probes via light_probe_gather_and_evaluate (3 probes)
// 4. Optionally gathers vehicle light + FUN_800e046c (total 9 candidates)
// 5. Computes final ambient via light_probe_compute_final_color
// 6. Sorts candidates by contribution (R + 0.59*G + 0.3*B) / distance
// 7. Stability: swaps light slots to avoid flickering when same probes persist
// 8. Smooth lerp positions + colors (factor 0.7) with Newton-Raphson normalize
// 9. Submits 3 directional lights + ambient color to renderer (FUN_80182740)
// If entity HAS level_def: delegates to light_probe_setup_directional_lights.
// ============================================================================

void light_probe_entity_lighting_update(void)
{
    // [MASSIVE function - see raw decompilation for full implementation]
    // Key data flow:
    // - Entity context at piVar5 (stride 0x1F8 from DAT_80611b34)
    // - Per-entity light state at offsets:
    //   +0x34..+0x3B: 2 probe slot IDs (piVar5[0xD], piVar5[0xE])
    //   +0x3C..+0x5F: 2 light positions (3 floats each, 2 slots * 0xC)
    //   +0x54..+0x77: 2 light colors (3 floats each, 2 slots * 0xC)
    // - Template from DAT_803cc214 (9 words: 3 color + 3 direction + 3 ambient)
    // - Contribution formula: (R + 0.59*G + 0.3*B) / distance
    //   where 0.59 = FLOAT_80613e78, 0.3 = FLOAT_80613e7c
    //
    // PC port: replace Newton-Raphson rsqrt with 1/sqrtf(), simplify ps saves.
}

// ============================================================================
// 0x8005eda4 | light_probe_entity_lighting_update_skinned
// MASSIVE skinned entity lighting variant (0xFE4 bytes).
// Extended version with:
// - Material flag checking (0x200 = two-sided lighting, 0x400 = no ambient)
// - Variable number of reserved probe slots (iVar24 = 0, 1, or 2)
// - Level ambient direction from level_def +0x2C/+0x44 (primary/secondary)
// - Distance-weighted point light accumulation (up to 10 lights from piVar10)
// - Dynamic time-of-day: atan2-based sun angle → sin table → ambient modulation
// - Minimum intensity threshold (FLOAT_80613e88) to cull weak lights
// - Final output: 3 directional lights + ambient stored to entity render state
// - Allocates 0xC0 bytes from secondary pool for light descriptors
// If entity HAS level_def: delegates to light_probe_setup_directional_lights.
// ============================================================================

void light_probe_entity_lighting_update_skinned(void)
{
    // [MASSIVE function - see raw decompilation for full implementation]
    // Key differences from non-skinned:
    // - Checks material flags at piVar10 (word 2): 0x200, 0x400
    // - bVar7 = has primary ambient direction, bVar8 = has secondary
    // - Distance computation to up to 10 point lights (stride 0x18)
    // - Weighted average: sum(1 - dist/total) * intensity_byte
    // - Smoothed accumulation at entity_data+0x180
    // - Time-of-day via FUN_80390ae4 (atan2) → sin table modulation
    // - Calls mem_advance_secondary(0xC0) for light descriptor allocation
    // - Calls FUN_801825d0 to build each light descriptor (up to 3)
    // - Stores ambient as 3 bytes at piVar11+0x6C..0x6E + alpha 0xFF
}
