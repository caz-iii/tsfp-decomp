// Address: 0x800d3b3c - 0x800d4aec | Name: weather_system | Date: 2026-08-11
// Weather/precipitation particle system: pool-based particle management with
// GX triangle rendering, 3-state intensity state machine, per-type linked lists.
// Pool: 100 slots x 0xD0 bytes. Renders 69 triangles per frame via GX FIFO.

#include "common.h"
#include "gx.h"
#include "memory.h"

// ============================================================================
// Globals
// ============================================================================

static int*   g_weather_pool;           // DAT_80611de8 - base of 100-slot pool
static int*   g_weather_free_head;      // DAT_80611dec - free list head
static int*   g_weather_active_heads[40]; // DAT_8056e010 - per-type active list heads
static int    g_weather_active_counts[40]; // DAT_8056e0b0 - per-type counts
static float  g_weather_intensity_max;  // FLOAT_80611e04 - difficulty-based max intensity
static float  g_weather_intensity_base; // FLOAT_80611e00 - base intensity
static float  g_weather_current_intensity; // FLOAT_80611df4
static int    g_weather_dirty;          // DAT_806107e4

extern int*   g_screen_dims;            // DAT_80611748 - screen dimensions array
extern int    g_is_story_mode;          // DAT_805015bd == '\n'
extern int    g_player_count_setting;   // DAT_80612610
extern int    g_match_mode;             // DAT_80612614
extern float  g_frame_time_scale;       // FLOAT_806108ec
extern int    g_frame_counter;          // DAT_806109c0
extern int    g_mem_mode;               // DAT_80611930

// Triangle index table for weather particle rendering (69 triangles x 3 verts)
extern unsigned char g_weather_tri_indices[]; // DAT_80406b38
// UV lookup table
extern float  g_weather_uv_table[];     // at -0x7fbf9618 relative

// ============================================================================
// weather_render_triangles
// Address: 0x800d3b3c
// Renders 69 weather triangles via GX FIFO with vertex position + color + UV.
// Uses paired-single math for vertex interpolation when intensity > threshold.
// Called from weather_render_gate.
// ============================================================================
void weather_render_triangles(float param_unused, float interp_factor) {
    int   camera_state;
    int   saved_proj;
    int   saved_mode;
    float screen_left, screen_right, screen_top, screen_bottom;
    float range_x, range_y;
    float intensity;
    float clamped_intensity;
    float alpha;
    float step_increment;
    int   tri_index;
    int   byte_offset;
    float pos[6];     // 3 vertices x 2 (x,y)
    float old_pos[6]; // previous frame positions

    camera_state = FUN_803826c0();
    saved_proj = FUN_8016e8ac(0);
    saved_mode = FUN_8016f00c(0xe);
    FUN_8016ee98();

    // Read screen bounds from global dimensions
    screen_left   = (float)((int)g_screen_dims[0]);
    screen_top    = (float)((int)g_screen_dims[5]);
    screen_right  = (float)((int)g_screen_dims[1]);
    screen_bottom = (float)((int)g_screen_dims[6]);

    range_x = screen_right - screen_left;
    range_y = screen_bottom - screen_top;

    // Intensity scaling based on camera mode
    float scale_factor = 0.5f; // FLOAT_80614eb8
    if (*(int*)((char*)camera_state + 0x1cc) == 3) {
        scale_factor = 1.0f;
    }

    float raw_intensity = *(float*)((char*)camera_state + 0x1c4);
    clamped_intensity = (raw_intensity > 1.0f) ? 1.0f : raw_intensity;
    alpha = scale_factor * clamped_intensity;
    intensity = scale_factor * raw_intensity;

    // Setup GX render state
    unsigned char prev_blend = FUN_8016e000(1);
    gx_bind_material_by_index_w(-6);
    FUN_8016e000(prev_blend);
    FUN_8001d4b4();
    FUN_80172a4c(3, 0x92, 0, 0xcf, -8);

    step_increment = 0.0f; // FLOAT_80614ebc
    tri_index = 0;
    byte_offset = 0;

    // Render 69 triangles
    for (unsigned int i = 0; i < 69; i++) {
        int mod3 = (int)i % 3;
        int idx_primary, idx_a, idx_b;

        if (mod3 == 1) {
            idx_primary = 1; idx_a = 2; idx_b = 0;
        } else if (mod3 == 0) {
            idx_primary = 0; idx_a = 1; idx_b = 2;
        } else {
            idx_primary = 2; idx_a = 0; idx_b = 1;
        }

        // Compute vertex positions from index table + UV table
        // 3 vertices per triangle, each looked up from g_weather_tri_indices
        for (int v = 0; v < 3; v++) {
            unsigned char idx = g_weather_tri_indices[byte_offset + v];
            float u = g_weather_uv_table[idx * 2];
            float v_coord = g_weather_uv_table[idx * 2 + 1];

            float px = u * range_x + screen_left;
            float py = v_coord * range_y + screen_top;

            // Clamp to screen bounds
            px = (px < screen_left) ? screen_left : (px > screen_right) ? screen_right : px;
            py = (py < screen_top) ? screen_top : (py > screen_bottom) ? screen_bottom : py;

            pos[v * 2] = px;
            pos[v * 2 + 1] = py;
            old_pos[v * 2] = px;
            old_pos[v * 2 + 1] = py;
        }

        // Interpolate primary vertex when intensity exceeds threshold
        if (step_increment < alpha) {
            float blend = 0.0f; // computed from intensity decay
            if ((intensity - step_increment) <= 0.75f) { // FLOAT_80614e78
                float t = (intensity - step_increment) / 0.75f;
                t = (t > 1.0f) ? 1.0f : (t < 0.0f) ? 0.0f : t;

                if (*(int*)((char*)camera_state + 0x1cc) == 2) {
                    blend = alpha * t; // direct alpha blend
                } else {
                    // Sin-table based wobble
                    float wobble = 2.0f * t; // FLOAT_80614e34
                    wobble = (wobble > 1.0f) ? 1.0f : wobble;
                    float sin_val = g_sin_table[(int)(1024.0f * 256.0f * (2.0f * wobble - 0.5f) + 0.5f) & 0x7ff];
                    blend = alpha * (2.0f * (0.5f * sin_val + 0.5f) + t);
                }
            }

            // Blend primary vertex toward average of other two
            float weight = 0.5f * blend;
            float* primary = &pos[idx_primary * 2];
            float* old_primary = &old_pos[idx_primary * 2];

            float new_x = weight * pos[idx_b * 2] + (1.0f - blend) * primary[0] + weight * pos[idx_a * 2];
            float new_y = weight * pos[idx_b * 2 + 1] + (1.0f - blend) * primary[1] + weight * pos[idx_a * 2 + 1];

            // Smooth old position
            old_primary[0] = (float)(interp_factor * (old_primary[0] - new_x) + old_primary[0]);
            old_primary[1] = (float)(interp_factor * (old_primary[1] - new_y) + old_primary[1]);
            primary[0] = new_x;
            primary[1] = new_y;
        }

        // Clamp final primary vertex
        pos[idx_primary * 2] = fclamp(pos[idx_primary * 2], screen_left, screen_right);
        pos[idx_primary * 2 + 1] = fclamp(pos[idx_primary * 2 + 1], screen_top, screen_bottom);
        old_pos[idx_primary * 2] = fclamp(old_pos[idx_primary * 2], screen_left, screen_right);
        old_pos[idx_primary * 2 + 1] = fclamp(old_pos[idx_primary * 2 + 1], screen_top, screen_bottom);

        // Submit 3 vertices to GX FIFO
        // Vertex 0
        GX_FIFO_F32 = pos[0];
        GX_FIFO_F32 = pos[1];
        GX_FIFO_F32 = 0.0f;
        GX_FIFO_U32 = 0x808080ff; // grey + full alpha
        float tex_v0 = FUN_8001cac0(old_pos[1], -6);
        float tex_u0 = FUN_8001ca5c(old_pos[0], -6);
        GX_FIFO_F32 = tex_u0;
        GX_FIFO_F32 = 1.0f * tex_v0 + 0.0f;

        // Vertex 1
        GX_FIFO_F32 = pos[2];
        GX_FIFO_F32 = pos[3];
        GX_FIFO_F32 = 0.0f;
        GX_FIFO_U32 = 0x808080ff;
        float tex_v1 = FUN_8001cac0(old_pos[3], -6);
        float tex_u1 = FUN_8001ca5c(old_pos[2], -6);
        GX_FIFO_F32 = tex_u1;
        GX_FIFO_F32 = 1.0f * tex_v1 + 0.0f;

        // Vertex 2
        GX_FIFO_F32 = pos[4];
        GX_FIFO_F32 = pos[5];
        GX_FIFO_F32 = 0.0f;
        GX_FIFO_U32 = 0x808080ff;
        float tex_v2 = FUN_8001cac0(old_pos[5], -6);
        float tex_u2 = FUN_8001ca5c(old_pos[4], -6);
        GX_FIFO_F32 = tex_u2;
        GX_FIFO_F32 = 1.0f * tex_v2 + 0.0f;

        step_increment += 0.0f; // per-triangle step (FLOAT_80614ebc)
        byte_offset += 3;
    }

    FUN_80172c54(); // end GX draw
    FUN_8001d4d4(); // restore state
    FUN_800d3764(g_screen_dims[0], g_screen_dims[5], g_screen_dims[1] + 1, g_screen_dims[6] + 1);
    FUN_8016e8ac(saved_proj);
    FUN_8016f00c(saved_mode);
    FUN_8016ee98();
    FUN_8038270c();
}

// ============================================================================
// weather_intensity_tick
// Address: 0x800d4390
// Per-frame intensity state machine with 3 states:
//   0 = idle (no change)
//   1 = growing (intensity increasing toward peak)
//   2 = decaying (intensity decreasing toward zero)
//   3 = forced dying (fast fade from match end)
// Uses frame time for smooth transitions. Triggered by spectator/scope state.
// ============================================================================
void weather_intensity_tick(int weather_ctx) {
    float intensity = *(float*)(weather_ctx + 0x1c4);
    float target    = *(float*)(weather_ctx + 0x1c8);
    int   state     = *(int*)(weather_ctx + 0x1cc);

    // Check if scope/spectator should trigger intensity growth
    if (state != 3 && g_spectator_data != 0 && *(int*)(g_spectator_data + 4) >= 0) {
        if (g_player_slot != 0 && (*(unsigned int*)(g_player_slot + 0x21c) & 2) != 0) {

            // Get weapon state for intensity modulation
            int weapon_state = 0;
            if (g_weapon_data != 0) {
                weapon_state = *(int*)(g_weapon_data + 0xe8c);
            }

            float scope_factor = 0.0f;
            if (weapon_state != 0) {
                short weapon_type = *(short*)(weapon_state + 0x94);
                if (weapon_type == 0x595 || weapon_type == 0x596) {
                    float ratio = 1.0f - *(float*)(weapon_state + 0x1cc) / *(float*)(weapon_state + 0x1d8);
                    scope_factor = fclamp(ratio, 0.0f, 1.0f);
                }
            }

            // Trigger growth if scope factor exceeds current target
            if ((int)(0.125f * target) < (int)(0.125f * scope_factor) && state == 0) {
                *(int*)(weather_ctx + 0x1cc) = 1;
            }
            *(float*)(weather_ctx + 0x1c8) = scope_factor;
        }
    }

    // State machine tick
    float peak_threshold = 0.4f; // FLOAT_80614e50
    float frame_dt = 1.0f / (float)g_frame_counter;
    int current_state = *(int*)(weather_ctx + 0x1cc);

    if (current_state == 2) {
        // Decaying: subtract intensity
        *(float*)(weather_ctx + 0x1c4) = -(0.75f * g_frame_time_scale * frame_dt - intensity);
        if (*(float*)(weather_ctx + 0x1c4) <= 0.0f) {
            *(float*)(weather_ctx + 0x1c4) = 0.0f;
            *(int*)(weather_ctx + 0x1cc) = 0; // back to idle
        }
    } else if (current_state == 1) {
        // Growing: add intensity
        *(float*)(weather_ctx + 0x1c4) = 0.5f * g_frame_time_scale * frame_dt + intensity;
        if (peak_threshold < *(float*)(weather_ctx + 0x1c4)) {
            *(float*)(weather_ctx + 0x1c4) = 0.6f; // FLOAT_80614ec0 overshoot peak
            *(int*)(weather_ctx + 0x1cc) = 2; // switch to decay
        }
    } else if (current_state == 3) {
        // Forced dying: faster growth (used for visual effect before match end)
        *(float*)(weather_ctx + 0x1c4) = 0.3f * g_frame_time_scale * frame_dt + intensity;
    }
}

// ============================================================================
// weather_render_gate
// Address: 0x800d45d4
// Conditional gate for weather rendering. Only renders if intensity > 0 and
// passes visibility/threshold checks. Applies sin-based wobble for state 1.
// Called from weather per-frame render dispatch.
// ============================================================================
void weather_render_gate(int weather_ctx) {
    if (*(float*)(weather_ctx + 0x1c4) <= 0.0f) {
        return;
    }

    float base_speed = FLOAT_806107d4;
    int player_count = FUN_801c1714();

    float interp = FLOAT_806107d8;
    if (player_count != 1) {
        interp = 0.5f * FLOAT_806107d8; // slower in multiplayer
    }

    float scaled_intensity = 2.0f * *(float*)(weather_ctx + 0x1c4);
    if (scaled_intensity > 1.0f) {
        scaled_intensity = 1.0f;
    }

    float wobble = 0.5f;
    if (*(int*)(weather_ctx + 0x1cc) == 1) {
        // Sin-table wobble during growth phase
        float idx_f = 1024.0f * 256.0f * (2.0f * scaled_intensity - 0.5f) + 0.5f;
        wobble = 0.15f * g_sin_table[(int)idx_f & 0x7ff] + 0.1f; // FLOAT_80614ecc/ec8
    }

    if (wobble * FLOAT_806107d0 > 0.02f) { // FLOAT_80614ed0 threshold
        weather_render_triangles(base_speed, interp);
    }
}

// ============================================================================
// weather_force_fade_out
// Address: 0x800d46d8
// Forces weather to "dying" state (mode 3). Only in story mode and specific
// match types (0xb, 0x11, 0x5, 0x1a). Called from damage_match_end_handler.
// ============================================================================
void weather_force_fade_out(int weather_ctx) {
    if (g_is_story_mode != '\n') {
        return;
    }
    if (g_match_mode != 0xb && g_match_mode != 0x11 &&
        g_match_mode != 5 && g_match_mode != 0x1a) {
        return;
    }
    if (*(int*)(weather_ctx + 0x1cc) == 3) {
        return; // already dying
    }

    // Halve intensity and set dying state
    *(float*)(weather_ctx + 0x1c4) *= 0.5f; // FLOAT_80614eb8
    *(int*)(weather_ctx + 0x1cc) = 3;
}

// ============================================================================
// weather_stub_nop
// Address: 0x800d4738
// Empty stub / no-op placeholder.
// ============================================================================
void weather_stub_nop(void) {
    return;
}

// ============================================================================
// weather_system_reset
// Address: 0x800d473c
// Full system reset: initializes 100-slot pool as doubly-linked free list,
// clears per-type active heads/counts, sets difficulty-based intensity config,
// and calls subsystem resets.
// ============================================================================
void weather_system_reset(void) {
    memset(g_weather_pool, 0, 0x5140);

    // Build doubly-linked free list across all 100 slots (0xD0 each)
    for (int i = 0; i < 100; i++) {
        int* slot = (int*)((char*)g_weather_pool + i * 0xD0);

        slot[0] = -1; // type = unassigned

        // prev pointer (offset +0x60)
        if (i == 0) {
            slot[0x60 / 4] = 0;
        } else {
            slot[0x60 / 4] = (int)((char*)g_weather_pool + (i - 1) * 0xD0);
        }

        // next pointer (offset +0x64 = 100 in decimal byte terms)
        if (i == 99) {
            slot[100 / 4] = 0;
        } else {
            slot[100 / 4] = (int)((char*)g_weather_pool + (i + 1) * 0xD0);
        }
    }

    g_weather_free_head = g_weather_pool;

    // Clear per-type active lists (40 entries each)
    for (int i = 0; i < 40; i++) {
        g_weather_active_heads[i] = 0;
        g_weather_active_counts[i] = 0;
    }

    // Difficulty-based intensity configuration
    int player_count = FUN_801c1714();

    if (g_is_story_mode == '\n') {
        if (player_count == 2) {
            g_weather_intensity_max = FLOAT_80614edc; // 2-player story
        } else if (player_count == 1) {
            g_weather_intensity_max = FLOAT_80614ed8; // 1-player story
        } else {
            g_weather_intensity_max = FLOAT_80614ee0; // 3+ player story
        }
    } else {
        if (player_count == 2) {
            g_weather_intensity_max = FLOAT_80614ee4; // 2-player arcade
        } else if (player_count == 1) {
            g_weather_intensity_max = FLOAT_80614ed8; // 1-player arcade
        } else if (player_count < 5) {
            g_weather_intensity_max = FLOAT_80614ee8; // 3-4 player arcade
        }
    }

    g_weather_intensity_base = FLOAT_80614ed8;
    if (player_count > 1) {
        g_weather_intensity_base = FLOAT_80614ee4;
    }

    g_weather_dirty = 1;

    // Subsystem resets
    FUN_800e3140();
    FUN_80102b90();
    FUN_80103830();
    FUN_80355248();
    FUN_80358c48();
}

// ============================================================================
// weather_system_alloc
// Address: 0x800d4904
// Allocates 0x5140 bytes for the weather pool and calls subsystem allocators.
// Memory comes from main heap (mode 0) or secondary (mode 1) based on g_mem_mode.
// ============================================================================
void weather_system_alloc(void) {
    int mem_flag;

    if (g_mem_mode == 0) {
        mem_flag = 0;
        g_weather_pool = (int*)mem_alloc(0x5140, 0);
    } else {
        mem_flag = 1;
        g_weather_pool = (int*)mem_alloc(0x5140, 1);
    }

    FUN_800e3104(g_weather_pool, mem_flag);
    FUN_80102b8c(); // subsystem alloc
    FUN_8010382c(); // subsystem alloc
    FUN_803551a4(); // subsystem alloc

    weather_system_reset();
    g_weather_current_intensity = FLOAT_80614ed8;
}

// ============================================================================
// weather_system_shutdown
// Address: 0x800d4968
// Shuts down the weather system by destroying all 100 slots.
// Only runs if pool is allocated.
// ============================================================================
void weather_system_shutdown(void) {
    if (g_weather_pool == 0) {
        return;
    }

    for (int i = 0; i < 100; i++) {
        int* slot = (int*)((char*)g_weather_pool + i * 0xD0);
        weather_slot_destroy(slot);
    }

    if (g_mem_mode == 0) { // DAT_80611568
        g_weather_pool = 0;
    }
}

// ============================================================================
// weather_slot_destroy
// Address: 0x800d49d8
// Destroys a single weather particle slot: clears entity reference, frees
// memory block, unlinks from per-type active list, returns to free pool.
// Called from shutdown, and 25+ other particle lifecycle functions.
// ============================================================================
void weather_slot_destroy(int* slot) {
    if (slot == NULL || slot[0] == -1) {
        return;
    }

    // Clear entity back-reference
    if ((int*)slot[5] != NULL) {
        *(int*)slot[5] = 0;
    }
    slot[5] = 0;

    // Free allocated memory block (if not externally owned)
    if (slot[6] != 0) {
        if ((slot[2] & 0x40000000) == 0) {
            block_free_simple(slot[6]);
        }
        slot[6] = 0;
    }

    // Unlink from per-type active doubly-linked list
    int* next = (int*)slot[0x19]; // offset +0x64
    int* prev = (int*)slot[0x18]; // offset +0x60

    if (next != NULL) {
        // next->prev = prev
        *(int*)((char*)next + 0x60) = (int)prev;
    }
    if (prev != NULL) {
        // prev->next = next
        *(int*)((char*)prev + 100) = (int)next;
    }

    // If this was the head, update head pointer
    if (prev == NULL) {
        g_weather_active_heads[slot[0]] = next;
    }

    // Decrement per-type count
    g_weather_active_counts[slot[0]]--;

    // Clear list pointers
    slot[0x18] = 0;
    slot[0x19] = (int)g_weather_free_head;

    // Link old free head's prev to this slot
    if ((int*)slot[0x19] != NULL) {
        *(int**)((char*)slot[0x19] + 0x60) = slot;
    }

    // Insert at free list head
    g_weather_free_head = slot;
    slot[0] = -1; // mark as unused

    g_weather_dirty = 1;
}

// ============================================================================
// weather_slot_list_traverse
// Address: 0x800d4aec
// Traverses a per-type active list to the end. Used for validation/counting.
// ============================================================================
void weather_slot_list_traverse(int type_index) {
    int* node = g_weather_active_heads[type_index];
    if (node == NULL) {
        return;
    }
    // Walk to end of list (offset +0x64 = next pointer)
    while (node != NULL) {
        node = (int*)*(int*)((char*)node + 100);
    }
}
