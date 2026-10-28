// Address: 0x8004ee70 | Name: sprite_measure_text_width | Date: 2026-08-10
// Address: 0x8004ee98 | Name: world_to_screen_project | Date: 2026-08-10
// Batch 111 - Text measurement wrapper + world-to-screen projection

#include "common.h"
#include "sprite_system.h"
#include "camera.h"

// ============================================================================
// 0x8004ee70 - sprite_measure_text_width
// Convenience wrapper for sprite_measure_text_region_int.
// Returns only the width (height output is NULL).
// ============================================================================
void sprite_measure_text_width(u32 font_id, u32 text_ptr, int* out_width)
{
    sprite_measure_text_region_int(font_id, text_ptr, out_width, NULL, 0);
}

// ============================================================================
// 0x8004ee98 - world_to_screen_project
// Projects a 3D world position to 2D screen coordinates through the active camera.
// Uses the camera's view-projection matrix at g_active_camera + 0x20C.
// Clamps output to screen bounds when clamp_mode == 0.
//
// Output (via paired-single register return):
//   pfVar1 -> {screen_x, screen_y, depth}
//   pfVar2 -> {clamped_x, clamped_y, scale, inv_depth} (if non-null)
// ============================================================================
void world_to_screen_project(u32 world_pos, u32 flags, int clamp_mode)
{
    float* out_primary;
    float* out_secondary;
    float local_58[3];  // world position copy
    float local_4c[3];  // view-space coords
    float local_3c[2];  // final screen coords
    float depth_scale;

    // Get output register pair (paired-single return convention)
    u64 reg_pair = __ps_get_output_pair();
    out_primary  = (float*)(reg_pair >> 32);
    out_secondary = (float*)(u32)reg_pair;

    if (g_active_camera == NULL) goto done;
    float* view_matrix = *(float**)(g_active_camera + 0x20C);
    if (view_matrix == NULL) goto done;

    // Copy world position
    local_58[0] = out_primary[0];
    local_58[1] = out_primary[1];
    local_58[2] = out_primary[2];

    // Transform world -> view space
    camera_world_to_view(view_matrix, local_58, local_4c);

    // Check if behind camera (depth > threshold)
    if (FLOAT_NEAR_ZERO >= local_4c[6]) goto done;

    // Perspective divide
    depth_scale = (float)(FLOAT_ONE / local_4c[6]);
    local_4c[0] *= depth_scale;
    local_4c[1] *= depth_scale;

    float abs_depth = (float)((double)local_4c[5] * fabs((double)depth_scale));

    // Clamp to screen bounds if not in free mode
    if ((fabs(local_4c[0]) > FLOAT_ONE || fabs(local_4c[1]) > FLOAT_ONE) && clamp_mode == 0)
    {
        if (local_4c[0] > FLOAT_ONE)
            local_4c[0] = FLOAT_ONE;
        else if (local_4c[0] < FLOAT_NEG_ONE)
            local_4c[0] = FLOAT_NEG_ONE;

        if (local_4c[1] > FLOAT_ONE)
            local_4c[1] = FLOAT_ONE;
        else if (local_4c[1] < FLOAT_NEG_ONE)
            local_4c[1] = FLOAT_NEG_ONE;
    }

    // Convert normalized device coords to screen pixels
    local_4c[5] = local_4c[5] * depth_scale + FLOAT_ONE;
    screen_ndc_to_pixels(&local_3c[0], &local_4c[0]);

    // Write secondary output if buffer provided
    if (out_secondary != NULL)
    {
        out_secondary[0] = local_3c[0];
        out_secondary[1] = local_3c[1];
        out_secondary[2] = abs_depth;
        out_secondary[3] = depth_scale;
    }

done:
    __ps_restore_output_pair();
}
