// Camera Rendering & Effects - Projection, shake, roll, render dispatch
// Address Range: 0x8009fd34 - 0x800a09d0
// Date: 2026-08-11
// Batch: 208

#include "common.h"

// ============================================================================
// Player slot camera effect fields:
//   +0x398: shake amplitude (time remaining)
//   +0x39c: shake frequency
//   +0x3a0: shake intensity (multiplier for up-vector oscillation)
//   +0x3a4: roll amplitude
//   +0x3a8: roll decay rate
//   +0x3ac: roll frequency (current, decays per frame)
//   +0x3b0: roll direction decay rate
//   +0x3b8: per-slot shake blend factor (1.0 = full, decays)
// ============================================================================

// Global camera oscillation accumulators
static float g_cam_oscillation_x;   // FLOAT_806104a8
static float g_cam_oscillation_y;   // FLOAT_806104ac
static float g_cam_oscillation_z;   // FLOAT_806104b0

// Global shake position offsets
static float g_shake_offset_x;      // DAT_80561c70
static float g_shake_offset_y;      // DAT_80561c74
static float g_shake_offset_z;      // DAT_80561c78
static float g_shake_offset_3;      // DAT_80561c7c
static float g_shake_offset_4;      // DAT_80561c80
static float g_shake_offset_5;      // DAT_80561c84

// Sin table at DAT_80583228 (2048 entries, 11-bit index)

// ============================================================================
// Address: 0x8009fd34 | camera_build_projection
// Builds the perspective projection matrix for the active camera.
// Computes effective FOV from aspect ratio, applies widescreen cap if flagged,
// calls the matrix builder, and stores tan(half-FOV) for frustum culling.
// 3 xrefs - called from camera_per_frame_update and pause camera.
// ============================================================================
void camera_build_projection(float near_plane, float far_plane, float aspect_scale, float fov)
{
    // Compute effective vertical FOV from aspect ratio
    // aspect_scale = (screen_height / display_height) / (render_height / render_display_height)
    float effective_fov = fov * aspect_scale;

    // Cap FOV for widescreen mode (flag 0x40000000 in DAT_805015c0)
    if ((DAT_805015c0 & 0x40000000) != 0 && effective_fov > FOV_MAX_WIDESCREEN) {
        effective_fov = FOV_MAX_WIDESCREEN;  // FLOAT_80614738
    }

    // Build perspective matrix: FUN_8012f90c(fov, half_fov_scaled, near, far, out_matrix)
    float half_fov_scaled = (PI * fov) / 360.0f;  // FLOAT_806146d4 / FLOAT_806146e8
    FUN_8012f90c(effective_fov, half_fov_scaled, near_plane, far_plane,
                 *(float**)(DAT_80611d54 + 0x204));

    // Store tan(half_fov) for frustum width calculation
    float tan_half = tan((PI * fov * 0.5f) / 360.0f);  // FUN_803909ec
    *(float*)(DAT_80611d54 + 0x37c) = tan_half;
    *(float*)(DAT_80611d54 + 0x380) = tan_half * effective_fov;
}

// ============================================================================
// Address: 0x8009fe40 | camera_set_shake
// Directly sets camera shake parameters on a player slot.
// amplitude = time duration, frequency = oscillation speed, intensity = magnitude.
// 1 xref.
// ============================================================================
void camera_set_shake(float amplitude, float frequency, float intensity, int pSlot)
{
    if (pSlot == 0) return;
    *(float*)(pSlot + 0x398) = amplitude;
    *(float*)(pSlot + 0x39c) = frequency;
    *(float*)(pSlot + 0x3a0) = intensity;
}

// ============================================================================
// Address: 0x8009fe58 | camera_set_shake_priority
// Sets camera shake only if the new shake is stronger than the current one.
// Checks: amplitude > 0 AND (no current shake OR frequency >= current).
// 1 xref.
// ============================================================================
void camera_set_shake_priority(float amplitude, float frequency, float intensity, int pSlot)
{
    if (pSlot == 0) return;
    if (amplitude <= 0.0f) return;

    // Skip if current shake is active and has higher frequency
    if (*(float*)(pSlot + 0x398) > 0.0f && frequency <= *(float*)(pSlot + 0x39c)) {
        return;
    }

    *(float*)(pSlot + 0x398) = amplitude;
    *(float*)(pSlot + 0x39c) = frequency;
    *(float*)(pSlot + 0x3a0) = intensity;
}

// ============================================================================
// Address: 0x8009fe98 | camera_set_roll_params
// Sets camera roll effect parameters (for explosions, impacts, etc.)
// amplitude = initial roll angle, decay = per-frame multiplier,
// frequency = oscillation rate, direction = sign of roll.
// 1 xref.
// ============================================================================
void camera_set_roll_params(float amplitude, float decay, float frequency, float direction,
                            int pSlot)
{
    *(float*)(pSlot + 0x3a4) = amplitude;
    *(float*)(pSlot + 0x3a8) = decay;
    *(float*)(pSlot + 0x3ac) = frequency;
    *(float*)(pSlot + 0x3b0) = direction;
}

// ============================================================================
// Address: 0x8009feac | camera_shake_reset_all
// Resets all camera shake/oscillation state: global offsets, per-slot blend.
// Called at level start and camera mode transitions.
// 2 xrefs.
// ============================================================================
void camera_shake_reset_all(void)
{
    // Clear global oscillation accumulators
    g_shake_offset_x = 0.0f;
    g_shake_offset_y = 0.0f;
    g_shake_offset_z = 0.0f;
    g_shake_offset_3 = 0.0f;
    g_shake_offset_4 = 0.0f;
    g_shake_offset_5 = 0.0f;

    // Reset per-slot shake blend factor to 1.0 (full effect)
    int offset = 0;
    for (int i = 0; i < g_player_slot_count; i++) {
        *(float*)(g_player_slot_pool + offset + 0x3b8) = 1.0f;
        offset += PLAYER_SLOT_SIZE;
    }
}

// ============================================================================
// Address: 0x8009ff0c | camera_shake_tick
// MASSIVE per-frame camera shake and roll application.
// If flag 0x80 is set (shake disabled), clears all shake state and returns.
// Otherwise:
// 1. Decrements shake timer (+0x398) by frame time
// 2. Advances oscillation phase accumulators (3 independent frequencies)
// 3. Computes sin-table-based oscillation for up-vector perturbation
// 4. Applies roll rotation to camera direction vector (yaw-based rotation)
// 5. Decays roll amplitude and frequency per frame (exponential decay)
// 6. Computes position offset from shake (direction + sin oscillation)
// 7. Performs raycast collision check for shake offset
// 8. If no hit: applies full offset via portal traversal
// 9. If hit: clips offset to collision point minus safety margin
// 1 xref - called from camera_per_frame_update.
// ============================================================================
void camera_shake_tick(float* direction)
{
    if ((*(u32*)(DAT_80611d54 + 0x21c) & 0x80) != 0) {
        // Shake disabled - clear all state
        *(float*)(DAT_80611d54 + 0x3b8) = 0.0f;
        *(float*)(DAT_80611d54 + 0x3b0) = 0.0f;
        *(float*)(DAT_80611d54 + 0x3ac) = 0.0f;
        *(float*)(DAT_80611d54 + 0x3a8) = 0.0f;
        *(float*)(DAT_80611d54 + 0x3a4) = 0.0f;
        *(float*)(DAT_80611d54 + 0x3a0) = 0.0f;
        *(float*)(DAT_80611d54 + 0x39c) = 0.0f;
        *(float*)(DAT_80611d54 + 0x398) = 0.0f;
        return;
    }

    // Decrement shake timer
    if (*(float*)(DAT_80611d54 + 0x398) > 0.0f) {
        *(float*)(DAT_80611d54 + 0x398) -= FLOAT_806108ec / fps_to_time_factor;

        // Advance oscillation phases (3 independent sin waves)
        g_cam_oscillation_x += (FLOAT_8061473c * FLOAT_806108ec) / fps_to_time_factor;
        g_cam_oscillation_y += (FLOAT_80614740 * FLOAT_806108ec) / fps_to_time_factor;
        g_cam_oscillation_z += (FLOAT_80614744 * FLOAT_806108ec) / fps_to_time_factor;

        if (*(float*)(DAT_80611d54 + 0x398) < 0.0f) {
            *(float*)(DAT_80611d54 + 0x398) = 0.0f;
        }
    }

    float amplitude = *(float*)(DAT_80611d54 + 0x398);
    float frequency = *(float*)(DAT_80611d54 + 0x39c);
    float blend = *(float*)(DAT_80611d54 + 0x3b8);

    // Compute sin-table oscillation (11-bit indices into 2048-entry table)
    u32 idx_y = (u32)(FLOAT_80614748 * (PI * g_cam_oscillation_y / 360.0f) + 0.5f);
    u32 idx_x = (u32)(FLOAT_80614748 * (PI * g_cam_oscillation_x / 360.0f) + 0.5f);
    u32 idx_z = (u32)(FLOAT_80614748 * (PI * g_cam_oscillation_z / 360.0f) + 0.5f);

    float sin_y = sin_table[idx_y & 0x7ff];
    float sin_x = sin_table[idx_x & 0x7ff];
    float sin_z = sin_table[idx_z & 0x7ff];

    // Apply shake to up-vector (+0x364..0x36c)
    float shake_scale = *(float*)(DAT_80611d54 + 0x3a0) * amplitude;
    *(float*)(DAT_80611d54 + 0x364) += shake_scale * sin_x;
    *(float*)(DAT_80611d54 + 0x368) += shake_scale * sin_y;
    *(float*)(DAT_80611d54 + 0x36c) += shake_scale * sin_z;

    // Apply roll: rotate direction around forward axis
    float roll_amp = -*(float*)(DAT_80611d54 + 0x3a4);
    // ... rotation matrix application via yaw decomposition ...

    // Compute position offset
    float offset_x = frequency * amplitude * sin_x + roll_amp * direction[1] + g_shake_offset_x * blend;
    float offset_y = frequency * amplitude * sin_z + amplitude * sin_z + g_shake_offset_y * blend;
    float offset_z = amplitude * sin_x + roll_amp * direction[2] + g_shake_offset_z * blend;

    // Decay roll amplitude per frame
    for (int i = 0; i < DAT_806108e4; i++) {
        *(float*)(DAT_80611d54 + 0x3a4) *= *(float*)(DAT_80611d54 + 0x3a8);
        *(float*)(DAT_80611d54 + 0x3ac) *= *(float*)(DAT_80611d54 + 0x3b0);
        if (*(float*)(DAT_80611d54 + 0x3a4) < EPSILON) {
            *(float*)(DAT_80611d54 + 0x3a4) = 0.0f;
        }
        if (*(float*)(DAT_80611d54 + 0x3ac) < EPSILON) {
            *(float*)(DAT_80611d54 + 0x3ac) = 0.0f;
        }
    }

    // Collision test: raycast in offset direction
    // If hit, clip to collision point - safety margin
    // If no hit, apply full offset via portal traversal
    // Updates position (+0x27c..0x284) and room (+0x2a0)
}

// ============================================================================
// Address: 0x800a0548 | camera_render_dispatch
// Per-frame camera render entry point. 3 xrefs.
// 1. Sets entity visibility flags (bit 0x40000000) on tracked entities
// 2. Configures viewport from slot parameters (+0x224..0x23c)
// 3. Calls scene rendering pipeline (frustum, portals, entities)
// 4. Calls HUD overlay rendering
// 5. Clears visibility flags on tracked entities
// 6. If split-screen (flag 0x8): renders player-specific HUD elements
// Only executes if slot is active (flag 0x1 set) and not in standard arcade
// mode with fewer than 7 players.
// ============================================================================
void camera_render_dispatch(void)
{
    // Skip if slot inactive or conditions not met
    if (DAT_80611d54 == 0 || (*(u32*)(DAT_80611d54 + 0x21c) & 0x1) == 0) return;

    int is_arcade = game_is_standard_arcade();
    if (is_arcade != 0 && DAT_80612028 <= 6) return;

    // Mark tracked entities as always-visible (for rendering)
    if ((*(u32*)(DAT_80611d54 + 0x21c) & 0x2) == 0) {
        render_entity_always_visible();
    }

    // For each tracked entity (slots 0 and 1):
    //   Set visibility bit 0x40000000 on entity + attachments
    //   Call bone update (FUN_800baa7c)
    //   For vehicle entities (type 8): also mark sub-entities

    // Configure viewport and render
    FUN_8001c6c8(DAT_80611d54 + 0x224);  // set viewport params
    FUN_8001c8ec();                        // apply viewport
    FUN_8016e8ac(1);                       // begin scene render pass
    FUN_8016f0e4(/* viewport params */);   // render scene (portals + entities)
    FUN_8016e8ac(4);                       // begin HUD pass
    FUN_80171f9c(*(float**)(DAT_80611d54 + 0x204));  // get view matrix
    FUN_801c2584(/* render HUD overlay */);

    // Clear visibility flags
    // ... (mirror of set loop above, clearing bit 0x40000000) ...

    // Split-screen HUD rendering
    if ((*(u32*)(DAT_80611d54 + 0x21c) & 0x8) != 0) {
        // Call custom HUD callback if registered
        if (*(void**)(DAT_80611d54 + 0xff8) != NULL) {
            (*(void(*)(void))(DAT_80611d54 + 0xff8))();
        }
        // Render player-specific HUD elements
    }

    // Restore entity visibility
    if ((*(u32*)(DAT_80611d54 + 0x21c) & 0x2) == 0) {
        render_entity_always_visible();
    }
}

// ============================================================================
// Address: 0x800a0914 | camera_hud_render
// Renders HUD for the active camera when NOT in split-screen mode (flag 0x8 clear).
// Configures viewport and calls the HUD rendering pipeline.
// 2 xrefs.
// ============================================================================
void camera_hud_render(void)
{
    if (DAT_80611d54 == 0) return;
    if ((*(u32*)(DAT_80611d54 + 0x21c) & 0x1) == 0) return;
    if ((*(u32*)(DAT_80611d54 + 0x21c) & 0x8) != 0) return;  // split-screen handled elsewhere

    FUN_8001c6c8(DAT_80611d54 + 0x224);
    FUN_8001c8ec();
    FUN_8016f0e4(/* viewport */);
    FUN_801c2934(/* HUD render */);
}

// ============================================================================
// Address: 0x800a0978 | camera_post_render
// Post-render pass: renders scoreboard for split-screen modes on scoring levels,
// then finalizes HUD rendering.
// 2 xrefs.
// ============================================================================
void camera_post_render(void)
{
    if (DAT_80611d54 != 0 && (*(u32*)(DAT_80611d54 + 0x21c) & 0x2) != 0) {
        int supports_scoring = game_level_supports_scoring();
        if (supports_scoring != 0) {
            FUN_802f0e8c();  // render scoreboard background
            FUN_802f135c();  // render scoreboard text
        }
    }
    if (DAT_80611d54 != 0) {
        FUN_801c29cc();  // finalize HUD
    }
}

// ============================================================================
// Address: 0x800a09d0 | camera_orbit_init
// Initializes camera orbit mode from a waypoint path definition.
// Loads the first waypoint position and direction from the path node's
// waypoint table, resolves the room via floor query.
// 2 xrefs.
// ============================================================================
void camera_orbit_init(int pSlot)
{
    // Get waypoint table from current path node (+0x26c -> +0xa8)
    int waypoint_table = *(int*)(*(int*)(pSlot + 0x26c) + 0xa8);

    // Determine which player slot to use for room resolution
    if ((DAT_805015c0 & 0x40000000) == 0) {
        *(int*)(pSlot + 0xfcc) = *(int*)(DAT_80611d54 + 0x214);
    } else {
        *(int*)(pSlot + 0xfcc) = *(int*)(*(int*)(DAT_80611d78 + 8) + 0x214);
    }

    // Load first waypoint (0x14-byte entries, indexed by +0xfb8)
    int wp_offset = *(int*)(pSlot + 0xfb8) * 0x14;
    *(int*)(pSlot + 0xfbc) = *(int*)(waypoint_table + wp_offset);      // waypoint data ptr
    *(int*)(pSlot + 0xfc0) = *(int*)(waypoint_table + wp_offset + 4);  // waypoint flags
    *(int*)(pSlot + 0xfc4) = 1;                                         // state = active
    *(float*)(pSlot + 0xfc8) = 0.0f;                                    // interpolation t

    // Set position from waypoint (vec3 at waypoint_data[0..8])
    int wp_data = *(int*)(pSlot + 0xfbc);
    *(float*)(pSlot + 0x27c) = *(float*)(wp_data + 0x0);
    *(float*)(pSlot + 0x280) = *(float*)(wp_data + 0x4);
    *(float*)(pSlot + 0x284) = *(float*)(wp_data + 0x8);

    // Copy to previous position
    *(float*)(pSlot + 0x288) = *(float*)(pSlot + 0x27c);
    *(float*)(pSlot + 0x28c) = *(float*)(pSlot + 0x280);
    *(float*)(pSlot + 0x290) = *(float*)(pSlot + 0x284);

    // Resolve room from floor query (unless level type 0x65)
    if (DAT_80612614 != 0x65) {
        int room = floor_height_from_position(1.0f, 0.5f);
        if (room != 0) {
            *(int*)(pSlot + 0x2a4) = room;
            *(int*)(pSlot + 0x2a0) = room;
        }
    }

    // Set direction from waypoint (vec3 at waypoint_data[0xc..0x14])
    *(float*)(pSlot + 0x334) = *(float*)(wp_data + 0x0c);
    *(float*)(pSlot + 0x338) = *(float*)(wp_data + 0x10);
    *(float*)(pSlot + 0x33c) = *(float*)(wp_data + 0x14);
}
