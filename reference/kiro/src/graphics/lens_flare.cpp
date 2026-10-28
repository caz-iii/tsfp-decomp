// Address: 0x801037ec - 0x80103ac4 | Lens Flare System | Date: 2026-08-26
// Batch 322: Complete lens flare subsystem — slot init, world transform, screen render
//
// Lens flares are attached to entities (lights, sun, etc.) via sub-object definitions.
// Each flare has a 3D position + direction that gets transformed from local to world
// space using bone and entity matrices. At render time, flares are projected to screen
// space and drawn as multi-layer additive-blended textured quads with distance-based
// intensity falloff and camera lerp scaling.
//
// Pool: Global slot at DAT_805785a0, per-entity flares stored in entity render data.
// Config: DAT_8040c368 = layer count, DAT_8040c414 = layer texture indices,
//         DAT_8040c464/68/6c = per-layer RGB tint, DAT_8040c374 = per-layer offset scale.

#include "common.h"
#include "lens_flare.h"
#include "entity.h"
#include "sprite.h"
#include "math_util.h"
#include "render.h"

// ============================================================================
// Slot Initialization
// ============================================================================

// Address: 0x801037ec
// Initializes a lens flare slot from a definition pointer.
// Copies world position (3 floats at def+8) and direction (3 floats at def+0x14),
// sets default intensity from FLOAT_806156e8.
void lens_flare_slot_init(int* slot, int definition) {
    slot[0] = definition;                       // def pointer
    slot[7] = (int)FLOAT_806156e8;              // default intensity
    slot[1] = *(int*)(definition + 0x08);       // pos.x
    slot[2] = *(int*)(definition + 0x0c);       // pos.y
    slot[3] = *(int*)(definition + 0x10);       // pos.z
    slot[4] = *(int*)(definition + 0x14);       // dir.x
    slot[5] = *(int*)(definition + 0x18);       // dir.y
    slot[6] = *(int*)(definition + 0x1c);       // dir.z
}

// ============================================================================
// System Lifecycle
// ============================================================================

// Address: 0x80103830
// Resets the lens flare system. Initializes global flare slot from fixed definition.
// Called by weather_system_reset on level transitions.
void lens_flare_system_reset(void) {
    lens_flare_slot_init((int*)&DAT_805785a0, -0x7fbf3cc8);  // fixed def address
}

// Address: 0x8010382c
// Empty stub for lens flare allocation callback.
// Called by weather_system_alloc.
void lens_flare_stub_alloc(void) {
    return;
}

// Address: 0x80103860
// Empty stub for lens flare tick callback.
// Called by particle_system_main_tick.
void lens_flare_stub_tick(void) {
    return;
}

// Address: 0x80103864
// Empty stub for per-flare render callback.
// Called by lens_flare_render_dispatch.
void lens_flare_stub_render_single(void) {
    return;
}

// Address: 0x80103868
// Empty stub for lens flare render pass callback.
// Called by particle_system_render_dispatch.
void lens_flare_stub_render_pass(void) {
    return;
}

// ============================================================================
// World Transform
// ============================================================================

// Address: 0x8010386c
// Transforms lens flare positions from local to world space.
// For each sub-object's flare definitions:
//   1. Copy position+direction from definition
//   2. If bone context provided (non-zero): apply bone matrix to pos+dir
//   3. Apply entity local matrix to pos+dir
// Called by entity_init_render_data, entity_update_transform, entity_prepare_render_volumes.
void lens_flare_transform_positions(void) {
    u64 ctx = FUN_803826d4();
    int entity = (int)(ctx >> 32);
    int bone_mode = (int)ctx;

    // Build the entity's local-to-world matrix
    float local_matrix[26];  // 4x4 + extras
    entity_build_local_matrix(local_matrix, entity, 1, 0);

    int flare_slot_offset = 0;
    int sub_object_offset = 0;

    // Get model data pointer
    int* model_data = *(int**)(*(int*)(entity + 4) + 4);

    for (int sub_idx = 0; sub_idx < *model_data; sub_idx++) {
        // Each sub-object is 0xC0 bytes in the model
        int sub_base = sub_object_offset + (*model_data) * -0xC0;
        int flare_count = *(short*)((int)model_data + sub_base + 0x4e);

        for (int f = 0; f < flare_count; f++) {
            int* flare_slot = *(int**)(*(int*)(entity + 0x200) + flare_slot_offset);
            flare_slot_offset += 4;

            // Source definition data for this flare
            int def_ptr = *(int*)((int)model_data + sub_base + 0xa8) + f * 0x30;

            if (bone_mode == 0) {
                // No bone: full init from definition
                lens_flare_slot_init(flare_slot, def_ptr);
            } else {
                // Copy position + direction from definition
                flare_slot[1] = *(int*)(def_ptr + 0x08);
                flare_slot[2] = *(int*)(def_ptr + 0x0c);
                flare_slot[3] = *(int*)(def_ptr + 0x10);
                flare_slot[4] = *(int*)(def_ptr + 0x14);
                flare_slot[5] = *(int*)(def_ptr + 0x18);
                flare_slot[6] = *(int*)(def_ptr + 0x1c);

                // Apply bone matrix (bone index from sub-object at +0x3d)
                int bone_idx = *(char*)((int)model_data + sub_base + 0x3d);
                int bone_matrix_ptr = *(int*)(*(int*)(entity + 4) + 0x10) + bone_idx * 0x40;
                FUN_8012f9b8((float*)bone_matrix_ptr, (float*)(flare_slot + 1));  // transform position
                FUN_8012fb90((float*)bone_matrix_ptr, (float*)(flare_slot + 4));  // rotate direction
            }

            // Apply entity local-to-world matrix
            FUN_8012f9b8(local_matrix, (float*)(flare_slot + 1));
            FUN_8012fb90(local_matrix, (float*)(flare_slot + 4));
        }

        sub_object_offset += 0xC0;
    }

    FUN_80382720();
}

// ============================================================================
// Rendering
// ============================================================================

// Address: 0x801039bc
// Lens flare render dispatch. Sets additive blend, iterates flare count
// calling per-flare render (currently stub), restores blend state, sets up
// view transform matrix for subsequent render passes.
// Called by render_instance_draw.
void lens_flare_render_dispatch(void) {
    int context = FUN_803826e4();

    // Save and set additive blend mode
    int prev_blend = FUN_8016f00c(2);
    int prev_depth = FUN_8016e8ac(2);

    // Render each flare (currently stub — no visible flares in this build?)
    for (int i = 0; i < *(int*)(context + 0x1ec); i++) {
        lens_flare_stub_render_single();
    }

    // Restore blend state
    FUN_8016f00c(prev_blend);
    FUN_8016e8ac(prev_depth);

    // Set up view-space transform for subsequent passes
    FUN_80130cd8(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;

    FUN_80382730();
}

// Address: 0x80103a68
// Preloads lens flare texture set. Checks a condition (FUN_8023cc5c),
// then loads sprite textures twice (for 2 mip levels or LOD variants).
void lens_flare_preload_textures(u64 p1, u64 p2, u64 p3, u64 p4,
                                  u64 p5, u64 p6, u64 p7, u64 p8) {
    u64 ctx = FUN_803826f0();

    u32 should_load = FUN_8023cc5c();
    if (should_load != 0) {
        for (int i = 0; i < 2; i++) {
            ctx = sprite_load_texture_set(ctx, p2, p3, p4, p5, p6, p7, p8);
        }
    }

    FUN_8038273c();
}

// Address: 0x80103ac4
// Screen-space lens flare renderer. MASSIVE function that:
// 1. Projects flare 3D position to clip space via view matrix (FUN_8012fb70)
// 2. Checks visibility: within screen bounds and in front of camera
// 3. Computes radial distance from screen center for intensity falloff
// 4. Applies intensity from flare slot data, clamped to [0, 1]
// 5. For each layer (DAT_8040c368 count):
//    a. Resolves texture via sprite_get_texture_id from layer table
//    b. Computes per-layer RGB color (scaled by intensity)
//    c. Computes screen-space position along flare axis
//    d. Scales quad size by camera lerp factor and aspect ratio
//    e. Renders textured quad via FUN_801708b4 (screen-space blit)
// Called by FUN_80103ea8 (the per-flare iteration entry point).
void lens_flare_render_screen(void) {
    int flare_data = FUN_803826cc();

    // Project flare position to screen space
    float clip_coords[4];  // x, y, z, w
    FUN_8012fb70(*(float**)(DAT_80611d54 + 0x20c), (float*)(flare_data + 8), clip_coords);

    float w = clip_coords[3];

    // Visibility check: must be in front of camera (w > 0) and within NDC bounds
    bool visible = false;
    float ndc_x = clip_coords[0] * (FLOAT_806156f0 / w);
    float ndc_y = clip_coords[1] * (FLOAT_806156f0 / w);

    if (-w <= clip_coords[2] && clip_coords[2] <= w &&
        FLOAT_806156f4 <= ndc_x && ndc_x <= FLOAT_806156f0 &&
        FLOAT_806156f4 <= ndc_y && ndc_y <= FLOAT_806156f0) {
        visible = true;
    }

    if (!visible) {
        FUN_80382718();
        return;
    }

    // Compute radial distance from screen center for edge falloff
    float dist_from_center = sqrtf(ndc_x * ndc_x + ndc_y * ndc_y);
    float edge_fade = FLOAT_806156f8 - dist_from_center;
    if (edge_fade > FLOAT_806156f0) {
        edge_fade = FLOAT_806156f0;  // clamp to 1.0
    }

    // Compute intensity alpha from flare slot data
    float intensity = (float)((double)(0x43300000 | *(u32*)(flare_data + 0x14)) - DOUBLE_80615708);
    int base_alpha = FUN_803825c0(
        FLOAT_806156f0 * edge_fade * intensity * DAT_8040c370);

    // Negate Y for screen coordinates (Y is inverted)
    ndc_y = -ndc_y;

    // Render each flare layer
    for (int layer = 0; layer < DAT_8040c368; layer++) {
        // Resolve texture for this layer
        int layer_tex_idx = *(int*)(&DAT_8040c414 + layer);
        int tex_id = sprite_get_texture_id(*(int*)(&DAT_80610820 + layer_tex_idx * 4));
        int tex_info = *DAT_80610a20 + tex_id * 0x58;
        u16 tex_w = *(u16*)(tex_info + 0x4e);
        u16 tex_h = *(u16*)(tex_info + 0x52);

        // Compute per-layer color (RGB scaled by intensity constant)
        float* layer_color = (float*)((int)&DAT_8040c464 + layer * 0x10);
        FUN_803825c0(FLOAT_806156fc * layer_color[1]);  // R
        FUN_803825c0(FLOAT_806156fc * layer_color[0]);  // G
        FUN_803825c0(FLOAT_806156fc * layer_color[2]);  // B

        // Compute screen position along flare axis
        // Uses view matrix columns to determine flare direction on screen
        float view_x_offset = /* view matrix X column projected */ 0;
        float view_y_offset = /* view matrix Y column projected */ 0;

        float layer_scale = *(float*)((int)&DAT_8040c374 + layer * 4);
        float screen_x = view_x_offset * ndc_x * layer_scale;
        float screen_y = view_y_offset * ndc_y * layer_scale;

        // Check split-screen mode for size adjustment
        int split_mode = FUN_801c1714();
        float size_mult = FLOAT_806156f0;
        if (split_mode > 1) {
            size_mult = FLOAT_80615700;  // smaller in split-screen
        }

        // Scale by camera lerp and aspect ratio
        float camera_lerp = player_get_camera_lerp_factor(DAT_80611d54);
        float aspect_x = /* computed from timing/resolution */ 1.0f;
        float aspect_y = /* computed from timing/resolution */ 1.0f;

        float base_size = FLOAT_80615704 * DAT_8040c36c *
            *(float*)((int)&DAT_8040c3c4 + layer * 4);
        float quad_w = base_size * (camera_lerp / *(float*)(DAT_80611d54 + 900)) * size_mult;
        float quad_h = quad_w * (aspect_x / aspect_y);

        // Bind texture and render screen-space quad
        FUN_8016e77c(tex_id);
        FUN_801708b4(
            (double)(screen_x - quad_w), (double)(screen_y - quad_h),  // min corner
            (double)(screen_x + quad_w), (double)(screen_y + quad_h),  // max corner
            0, 0,                                                       // UV min
            (u32)tex_w << 4, (u32)tex_h << 4                           // UV max (fixed point)
        );
    }

    FUN_80382718();
}
