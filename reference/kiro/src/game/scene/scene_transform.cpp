// Address: 0x80052a1c | Name: scene_get_config_value | Date: 2026-08-10
// Address: 0x80052a34 | Name: scene_update_slot_visibility | Date: 2026-08-10
// Address: 0x80052b2c | Name: scene_orbit_position_update | Date: 2026-08-10
// Address: 0x80052bc4 | Name: scene_orbit_pair_update | Date: 2026-08-10
// Address: 0x80052c5c | Name: scene_transform_bounds | Date: 2026-08-10
// Address: 0x80052e0c | Name: scene_transform_bounds_scaled | Date: 2026-08-10
// Address: 0x80053038 | Name: scene_batch_transform_verts | Date: 2026-08-10
// Address: 0x800530a4 | Name: scene_batch_transform_swap | Date: 2026-08-10
// Address: 0x80053140 | Name: scene_batch_swap_axes | Date: 2026-08-10
// Batch 114 - Scene geometry transforms: visibility, bounds rotation, vertex batch ops

#include "common.h"
#include "math3d.h"

// Static config table
extern u32 g_scene_config_table[];  // DAT_80527c08

// ============================================================================
// 0x80052a1c - scene_get_config_value
// Returns a scene configuration constant by index from a static table.
// ============================================================================
u32 scene_get_config_value(int index)
{
    return g_scene_config_table[index];
}

// ============================================================================
// 0x80052a34 - scene_update_slot_visibility
// Per-frame visibility culling for all scene slots against all viewports.
// For each slot, tests bounding rect (4 shorts: min_x, min_y, max_x, max_y)
// against viewport dimensions (DAT_806109c4 = width, DAT_806109c8 = height).
// A slot is visible if its rect is valid and within screen bounds.
// Writes per-viewport flags at viewport + slot + 0xD2C.
// Writes combined visibility to g_scene_visibility_flags + slot.
// ============================================================================
void scene_update_slot_visibility(void)
{
    for (int slot = 1; slot < g_scene_slot_count + 1; slot++) {
        u8 any_visible = 0;

        for (int vp = 0; vp < g_viewport_count; vp++) {
            int viewport = g_viewport_array + vp * 0xFFC;
            int bounds   = viewport + slot * 0xC + 0x3BC;

            u8 visible = 0;
            if ((*(u32*)(viewport + 0x21C) & 0x20) != 0) {
                s16 min_x = *(s16*)(bounds + 4);
                s16 min_y = *(s16*)(bounds + 6);
                s16 max_x = *(s16*)(bounds + 8);
                s16 max_y = *(s16*)(bounds + 10);

                if (max_x >= 0 && min_x < g_screen_width &&
                    min_x < max_x && max_y >= 0 &&
                    min_y < g_screen_height && min_y < max_y)
                {
                    visible = 1;
                }

                *(u8*)(viewport + slot + 0xD2C) = visible;
                if (visible) {
                    any_visible = 1;
                }
            }
        }

        *(u8*)(g_scene_visibility_flags + slot) = any_visible;
    }
}

// ============================================================================
// 0x80052b2c - scene_orbit_position_update
// Updates an orbiting position around a center point.
// Transforms the source vector by a matrix, transforms the dest by a different
// matrix (FUN_8012fb90 = inverse transform), then advances rotation angle.
// Angle wraps at FLOAT_80613da8 (likely 360.0 or TAU).
// Rotation speed comes from table at DAT_8047b908 indexed by mode at +0x10.
// ============================================================================
void scene_orbit_position_update(u32 param_1, u32 param_2, float* matrix)
{
    __ps_save_regs_2();
    int header = (int)(__ps_get_hi());

    // Transform source position by matrix
    mat3_transform_point(matrix, (float*)(header + 8));

    // Inverse-transform destination
    mat3_inverse_transform_point(matrix, (float*)(header + 0x14));

    // Advance rotation angle
    int speed_idx = *(int*)((int)__ps_get_lo() + 0x10);
    *(float*)(header + 0x24) += g_orbit_speed_table[speed_idx];

    // Wrap angle
    if (*(float*)(header + 0x24) >= FLOAT_360) {
        *(float*)(header + 0x24) -= FLOAT_360;
    }

    __ps_restore_regs_2();
}

// ============================================================================
// 0x80052bc4 - scene_orbit_pair_update
// Paired orbit: transforms BOTH position vectors by the same matrix
// (no inverse), then advances angle. Used for paired rotating objects.
// ============================================================================
void scene_orbit_pair_update(u32 param_1, u32 param_2, float* matrix)
{
    __ps_save_regs_2();
    int header = (int)(__ps_get_hi());

    // Transform both positions by the same matrix
    mat3_transform_point(matrix, (float*)(header + 8));
    mat3_transform_point(matrix, (float*)(header + 0x14));

    // Advance rotation angle
    int speed_idx = *(int*)((int)__ps_get_lo() + 0x10);
    *(float*)(header + 0x24) += g_orbit_speed_table[speed_idx];

    // Wrap angle
    if (*(float*)(header + 0x24) >= FLOAT_360) {
        *(float*)(header + 0x24) -= FLOAT_360;
    }

    __ps_restore_regs_2();
}

// ============================================================================
// 0x80052c5c - scene_transform_bounds
// Rotates an AABB by 0/90/180/270 degrees around Y-axis, then translates.
//
// Input: param_1 = 6 floats {min_x, min_y, min_z, max_x, max_y, max_z}
//        param_2 = transform descriptor:
//          +0x04: translate X
//          +0x08: translate Y
//          +0x0C: translate Z
//          +0x10: rotation mode (0=0deg, 1=90deg, 2=180deg, 3=270deg)
//
// The rotation swaps/negates X and Z components of min/max corners.
// Y is always just translated (no rotation around X or Z).
// ============================================================================
void scene_transform_bounds(float* bounds, int transform)
{
    float min_x = bounds[0];
    float min_z = bounds[2];
    float max_x = bounds[3];
    float max_z = bounds[5];

    float tx = *(float*)(transform + 0x04);
    float ty = *(float*)(transform + 0x08);
    float tz = *(float*)(transform + 0x0C);
    int   mode = *(int*)(transform + 0x10);

    switch (mode) {
    case 0: // 0 degrees - identity
        bounds[0] = min_x + tx;
        bounds[1] += ty;
        bounds[2] = min_z + tz;
        bounds[3] = max_x + tx;
        bounds[4] += ty;
        bounds[5] = max_z + tz;
        break;

    case 1: // 90 degrees CW
        bounds[0] = -max_z + tx;
        bounds[1] += ty;
        bounds[2] = min_x + tz;
        bounds[3] = -min_z + tx;
        bounds[4] += ty;
        bounds[5] = max_x + tz;
        break;

    case 2: // 180 degrees
        bounds[0] = -max_x + tx;
        bounds[1] += ty;
        bounds[2] = -max_z + tz;
        bounds[3] = -min_x + tx;
        bounds[4] += ty;
        bounds[5] = -min_z + tz;
        break;

    case 3: // 270 degrees CW
        bounds[0] = min_z + tx;
        bounds[1] += ty;
        bounds[2] = -max_x + tz;
        bounds[3] = max_z + tx;
        bounds[4] += ty;
        bounds[5] = -min_x + tz;
        break;
    }
}

// ============================================================================
// 0x80052e0c - scene_transform_bounds_scaled
// Extended bounds transform: applies a diagonal scale matrix first,
// then sorts min/max (in case scale flipped them), then rotates+translates.
// Scale values from FLOAT_80613dac, FLOAT_80613db0.
// ============================================================================
void scene_transform_bounds_scaled(float* bounds, int transform)
{
    float scale_mtx[16];

    // Build diagonal scale matrix
    mat3_build_scale(scale_mtx, FLOAT_SCALE_X, FLOAT_SCALE_Y, FLOAT_SCALE_Z);

    // Apply scale to both corners
    mat3_transform_point(scale_mtx, bounds);
    mat3_transform_point(scale_mtx, bounds + 3);

    // Sort min/max X (scale may have flipped)
    if (bounds[3] < bounds[0]) {
        float tmp = bounds[0];
        bounds[0] = bounds[3];
        bounds[3] = tmp;
    }

    // Then apply rotation + translation (same as scene_transform_bounds)
    scene_transform_bounds(bounds, transform);
}

// ============================================================================
// 0x80053038 - scene_batch_transform_verts
// Transforms N vertices (count at header+0x14, stride 0xC starting at +0x18)
// by a matrix, then inverse-transforms the origin back into header+8.
// ============================================================================
void scene_batch_transform_verts(void)
{
    __ps_save_regs_2();
    int  header = (int)(__ps_get_hi());
    float* matrix = (float*)__ps_get_lo();

    int vert_count = *(s16*)(header + 0x14);
    for (int i = 0; i < vert_count; i++) {
        mat3_transform_point(matrix, (float*)(header + i * 0xC + 0x18));
    }

    // Inverse-transform origin
    mat3_inverse_transform_point(matrix, (float*)(header + 8));

    __ps_restore_regs_2();
}

// ============================================================================
// 0x800530a4 - scene_batch_transform_swap
// Transforms N vertices by matrix, then swaps the position vectors at
// +0x24 and +0x3C (double-buffered old/new positions for interpolation).
// Finally inverse-transforms origin.
// ============================================================================
void scene_batch_transform_swap(void)
{
    __ps_save_regs_2();
    int  header = (int)(__ps_get_hi());
    float* matrix = (float*)__ps_get_lo();

    int vert_count = *(s16*)(header + 0x14);
    for (int i = 0; i < vert_count; i++) {
        mat3_transform_point(matrix, (float*)(header + i * 0xC + 0x18));
    }

    // Swap position double-buffer: +0x24 <-> +0x3C (3 floats each)
    float tmp[3];
    tmp[0] = *(float*)(header + 0x24);
    tmp[1] = *(float*)(header + 0x28);
    tmp[2] = *(float*)(header + 0x2C);
    *(float*)(header + 0x24) = *(float*)(header + 0x3C);
    *(float*)(header + 0x28) = *(float*)(header + 0x40);
    *(float*)(header + 0x2C) = *(float*)(header + 0x44);
    *(float*)(header + 0x3C) = tmp[0];
    *(float*)(header + 0x40) = tmp[1];
    *(float*)(header + 0x44) = tmp[2];

    // Inverse-transform origin
    mat3_inverse_transform_point(matrix, (float*)(header + 8));

    __ps_restore_regs_2();
}

// ============================================================================
// 0x80053140 - scene_batch_swap_axes
// For an array of N object pointers, swaps float pairs:
//   [i]+0x08 <-> [i]+0x14  (X axis bounds swap)
//   [i]+0x10 <-> [i]+0x1C  (Z axis bounds swap)
// Used for mirroring/flipping scene object bounds along axes.
// ============================================================================
void scene_batch_swap_axes(int* obj_array, int count)
{
    for (int i = 0; i < count; i++) {
        int obj = obj_array[i];

        // Swap X bounds if different
        if (*(float*)(obj + 0x08) != *(float*)(obj + 0x14)) {
            float tmp = *(float*)(obj + 0x08);
            *(float*)(obj + 0x08) = *(float*)(obj + 0x14);
            *(float*)(obj + 0x14) = tmp;
        }

        // Swap Z bounds if different
        if (*(float*)(obj + 0x10) != *(float*)(obj + 0x1C)) {
            float tmp = *(float*)(obj + 0x10);
            *(float*)(obj + 0x10) = *(float*)(obj + 0x1C);
            *(float*)(obj + 0x1C) = tmp;
        }
    }
}
