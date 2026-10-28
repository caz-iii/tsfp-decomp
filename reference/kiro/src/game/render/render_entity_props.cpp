// =============================================================================
// render_entity_props.cpp - Render entity property channels and multi-pass submission
// Part of tsfp-decomp (TimeSplitters: Future Perfect decompilation)
// Batch: 152 | Date: 2026-08-10
// =============================================================================

#include "common.h"

// =============================================================================
// Address: 0x80068928 | Name: render_entity_submit_multipass | Batch: 152
// Date: 2026-08-10
// Description: 3-pass render entity submission:
//   Pass 1: mode 4 (cull front faces), submit via FUN_801839a8
//   Pass 2: mode 5 (cull back faces), submit via FUN_801839a8
//   Pass 3: mode 2 (overlay/blend), submit via FUN_801839a8
//   Used for translucent/outline effects requiring multiple draw calls.
// =============================================================================

void render_entity_submit_multipass(u64 render_context, double param) {
    FUN_8016e8ac(4);          // set cull mode: front
    FUN_8016f00c(0);          // set face winding: CW
    u64 ctx = FUN_801839a8(render_context, param);

    FUN_8016f00c(1);          // set face winding: CCW
    FUN_8016e8ac(5);          // set cull mode: back
    ctx = FUN_801839a8(ctx, param);

    FUN_8016e8ac(2);          // set cull mode: overlay/none
    FUN_801839a8(ctx, param);
}

// =============================================================================
// Address: 0x800689b4 | Name: render_entity_submit_multipass_indexed | Batch: 152
// Date: 2026-08-10
// Description: Indexed variant of multipass render submission. First pass uses
//   FUN_80183984 (indexed draw), subsequent passes use FUN_801839a8 (standard).
//   Wrapped in perf counters.
// =============================================================================

void render_entity_submit_multipass_indexed(u64 render_context, double param) {
    FUN_803826ec();  // perf begin

    FUN_8016e8ac(4);          // cull front
    FUN_8016f00c(0);          // winding CW
    u64 ctx = FUN_80183984(render_context, param);  // indexed draw

    FUN_8016f00c(1);          // winding CCW
    FUN_8016e8ac(5);          // cull back
    ctx = FUN_801839a8(ctx, param);

    FUN_8016e8ac(2);          // overlay
    FUN_801839a8(ctx, param);

    FUN_80382738();  // perf end
}

// =============================================================================
// Address: 0x80068a50 | Name: render_prop_build_transform_matrix | Batch: 152
// Date: 2026-08-10
// Description: Builds 4x4 transform matrix from 5 animated property channels:
//   [0] = rotation X (radians, wrapped to [-pi, pi])
//   [1] = rotation Y (radians, wrapped to [-pi, pi])
//   [2] = scale X (clamped to [min_scale, max_scale])
//   [3] = scale Y (clamped to [min_scale, max_scale])
//   [4] = roll/Z rotation (degrees, converted to radians)
//   Applies rotations via matrix multiply chain. Handles billboard-style
//   rotation and non-uniform scaling.
// =============================================================================

void render_prop_build_transform_matrix(float* out_matrix, int* channel_ptrs) {
    float rot_matrix[12];

    FUN_8012ef38((int)out_matrix);  // identity 4x4

    // Evaluate channel 0: rotation X
    double rot_x = 0.0;
    if (channel_ptrs[0] != 0) {
        rot_x = (double)FUN_80133d80();  // evaluate animation curve
    }
    double wrapped_x = FUN_80390324((double)((float)(rot_x - 0.0) / 1.0));
    rot_x = -(1.0 * (float)wrapped_x - rot_x);

    // Evaluate channel 1: rotation Y
    double rot_y = 0.0;
    if (channel_ptrs[1] != 0) {
        rot_y = (double)FUN_80133d80();
    }
    double wrapped_y = FUN_80390324((double)((float)(rot_y - 0.0) / 1.0));
    rot_y = -(1.0 * (float)wrapped_y - rot_y);

    // Evaluate channel 2: scale X (clamped)
    double scale_x = 1.0;
    if (channel_ptrs[2] != 0) {
        scale_x = (double)FUN_80133d80();
    }
    scale_x = clamp(scale_x, MIN_SCALE, MAX_SCALE);

    // Evaluate channel 3: scale Y (clamped)
    double scale_y = 1.0;
    if (channel_ptrs[3] != 0) {
        scale_y = (double)FUN_80133d80();
    }
    scale_y = clamp(scale_y, MIN_SCALE, MAX_SCALE);

    // Evaluate channel 4: roll (degrees -> radians)
    double roll = 0.0;
    if (channel_ptrs[4] != 0) {
        roll = (double)FUN_80133d80();
    }
    double wrapped_roll = FUN_80390324((double)((float)(roll - 0.0) / 360.0));

    // Set translation from rotation values
    out_matrix[12] = (float)rot_x;
    out_matrix[13] = (float)rot_y;

    // Build and apply rotation matrices
    FUN_8012ef38((int)rot_matrix);
    // ... scale + rotation composition via FUN_80130bd4, FUN_8012f0d8, FUN_8012f62c
    FUN_8012f62c(scale_x, scale_y, 1.0);
    FUN_80130bd4(rot_matrix, out_matrix, out_matrix);
}

// =============================================================================
// Address: 0x80068cf0 | Name: render_prop_evaluate_channel | Batch: 152
// Date: 2026-08-10
// Description: Evaluates a render property channel (A=0, B=1, C=2, D=3), builds
//   transform matrix if not cached this frame. Handles:
//   - Per-frame caching via frame counter comparison
//   - Channel dispatch to get_value / get_value_b / get_value_c / get_value_d
//   - Mirror flag (swaps matrix columns for X-flip)
//   - Billboard rotation (Y-axis rotate toward camera via DAT_80611b48)
//   Stores computed matrix at entity[5], updates frame stamp at entity[4].
// =============================================================================

void render_prop_evaluate_channel(u32 unused1, u32 unused2, int channel_index) {
    // Entity from perf context
    int* entity = /* from context */;

    // Skip if already evaluated this frame
    if (DAT_80612024 == entity[4]) return;

    int* channel_data = NULL;
    int value_index = -1;

    switch (channel_index) {
        case 0: value_index = render_prop_get_value(entity);   channel_data = (int*)entity[8];  break;
        case 1: value_index = render_prop_get_value_b(entity); channel_data = (int*)entity[9];  break;
        case 2: value_index = render_prop_get_value_c(entity); channel_data = (int*)entity[10]; break;
        case 3: value_index = render_prop_get_value_d(entity); channel_data = (int*)entity[11]; break;
    }

    if (value_index < 0 || channel_data == NULL) return;

    float* matrix = (float*)mem_advance_secondary(0x40);

    if ((*(u8*)((int)entity + 0x1a) & 1) == 0) {
        render_prop_build_transform_matrix(matrix, channel_data);
    } else {
        memset_wrapper((int)matrix, 0, 0x40);  // zeroed for disabled channel
    }

    // Mirror flag: swap X/Y columns
    if ((*(u32*)(/* bone data */ + value_index * 0x10 + 0xc) & 0x1000000) != 0) {
        float tmp;
        tmp = matrix[1]; matrix[1] = matrix[4]; matrix[4] = tmp;
        tmp = matrix[0]; matrix[0] = matrix[5]; matrix[5] = tmp;
        tmp = matrix[12]; matrix[12] = matrix[13]; matrix[13] = tmp;
    }

    entity[4] = DAT_80612024;  // stamp current frame
    entity[5] = (int)matrix;   // cache matrix pointer

    // Billboard: rotate toward camera if flag set
    if (DAT_80611b48 != 0) {
        double angle = FUN_80390ae4(/* computed angle */);
        FUN_8012f534(0.0, angle, 0.0);  // apply Y rotation
    }
}

// =============================================================================
// Address: 0x80068eb8 | Name: render_entity_always_visible | Batch: 152
// Date: 2026-08-10
// Description: Default visibility callback. Always returns 1 (visible).
//   Used as fallback for entities that should never be culled.
// =============================================================================

int render_entity_always_visible(void) {
    return 1;
}
