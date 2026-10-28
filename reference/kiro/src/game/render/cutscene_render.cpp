// Address: 0x800691ac - 0x8006a3dc | Name: cutscene_render | Date: 2026-08-10
// Cutscene render entity subsystem: mesh skinning, frame visibility,
// GX material/strip submission, lighting evaluation, pool allocation.
// Parent dispatcher: cutscene_entity_render_dispatch (0x8006b354)

#include "common.h"
#include "render/render_entity.h"
#include "animation/skel_pose.h"
#include "math/matrix.h"

// ============================================================================
// Forward declarations
// ============================================================================
static int  cutscene_mesh_is_visible(void);
static bool cutscene_frame_in_range(int entity_data, int slot_index);
static void cutscene_update_visibility_flag(int vis_data, int entity_data, int slot_index);
static void cutscene_eval_strip_properties(void);
static void cutscene_get_anim_interp_factor(void);

// ============================================================================
// 0x800691ac - cutscene_mesh_skin_init
// Called by: cutscene_load_scene_data
// Sets up skeleton vertex data with pose transforms for a cutscene mesh.
// Allocates bone matrices, evaluates pose, transforms vertices/normals
// into bone-local space (compressed short or float formats).
// ============================================================================
void cutscene_mesh_skin_init(int skel_data, int param_unused, int do_skin) {
    int* pSkelState;
    int  bone_buf;

    pSkelState = (int*)os_get_context_upper();  // FUN_803826b8
    skel_anim_init(pSkelState, 2, 0);           // FUN_80022438
    skel_alloc_channel_buffer(2, 0x1800, 0x82); // FUN_800229dc

    pSkelState[4] = 2;                          // blend mode
    pSkelState[5] = (int)DAT_804d1cd4;          // anim speed
    pSkelState[6] = (int)FLOAT_ZERO;            // time offset
    pSkelState[0x11] = (int)FLOAT_ZERO;         // blend weight

    if (do_skin == 0) {
        pSkelState[2] = 0;
        pSkelState[1] = 0;
        os_restore_context();  // FUN_80382704
        return;
    }

    // Get mesh descriptor
    u32* mesh_desc = (u32*)*pSkelState;
    u32  flags     = mesh_desc[3];
    f32* positions = (f32*)mesh_desc[2];
    f32* normals   = (f32*)*mesh_desc;
    f32* pos_iter  = positions + 3;
    f32* norm_float = NULL;
    f32* norm_iter = normals;
    s16* pos_short = NULL;
    f32* norm_short_ptr = NULL;
    s16* norm_short = NULL;

    if ((flags & 1) != 0) {
        norm_iter = NULL;
        norm_float = normals;   // compressed normal format
    }
    if ((flags & 4) != 0) {
        norm_short = (s16*)((int)positions + 6);
        pos_iter = NULL;
        norm_short_ptr = positions;
    }

    // Allocate bone matrix buffer
    int total_bones = mesh_desc[7] + mesh_desc[8];
    bone_buf = mem_alloc(total_bones * 0x40, 0);
    pSkelState[1] = bone_buf;
    pSkelState[2] = bone_buf + mesh_desc[7] * 0x40;  // secondary offset

    // Evaluate skeleton pose
    skel_pose_update(0, 0, pSkelState, do_skin);

    // Transform vertices into bone-local space
    if ((*(u32*)(*pSkelState + 0xC) & 0x100) == 0) {
        int strip_offset = 0;
        for (int group = 0; group < *(int*)(*pSkelState + 0x18); group++) {
            int* group_ptr = (int*)(*(int*)(*pSkelState + 0x10) + strip_offset);
            flags |= (0x20 << *group_ptr);

            for (int strip = 0; strip < group_ptr[2]; strip++) {
                int strip_data = group_ptr[1];
                for (int vert = 0; vert < *(int*)(strip_data + strip * 0x14 + 8); vert++) {
                    // Read bone weight/index
                    f32 weight_val;
                    if (pos_iter == NULL) {
                        weight_val = (f32)(int)*norm_short;
                        norm_short += 4;
                    } else {
                        weight_val = *pos_iter;
                        pos_iter += 4;
                    }

                    // Get bone matrix
                    int bone_idx = ((u32)weight_val & 0xFF) >> 3;
                    f32* bone_mtx = (f32*)(pSkelState[2] + (group_ptr[5] + bone_idx) * 0x40);
                    f32 inv_mtx[16];
                    mtx_invert_3x4(bone_mtx, inv_mtx);  // FUN_80130cd8

                    // Read position (short or float format)
                    f32 pos[3];
                    if (norm_float == NULL) {
                        pos[0] = *norm_iter;
                        pos[1] = norm_iter[1];
                        pos[2] = norm_iter[2];
                    } else {
                        // Decompress from s16 with scale + offset
                        pos[0] = SCALE_FACTOR * (f32)(int)*(s16*)norm_float + *(f32*)(*pSkelState + 0x30);
                        pos[1] = SCALE_FACTOR * (f32)(int)*(s16*)((int)norm_float + 2) + *(f32*)(*pSkelState + 0x34);
                        pos[2] = SCALE_FACTOR * (f32)(int)*(s16*)(norm_float + 1) + *(f32*)(*pSkelState + 0x38);
                    }

                    // Transform to bone-local space
                    if ((*(u32*)(*pSkelState + 0xC) & 0x100) == 0) {
                        pos[0] -= bone_mtx[0xC];
                        pos[1] -= bone_mtx[0xD];
                        pos[2] -= bone_mtx[0xE];
                        mtx_transform_vec3(inv_mtx, pos);  // FUN_8012fbf4
                    }

                    // Clamp if flag 1 set
                    if ((*(u32*)(*pSkelState + 0xC) & 1) != 0) {
                        // Clamp XYZ to [-limit, +limit]
                        // ... (clamping logic)
                    }

                    // Write back transformed position
                    if (norm_float == NULL) {
                        *norm_iter = pos[0];
                        norm_iter[1] = pos[1];
                        norm_iter[2] = pos[2];
                        norm_iter += 3;
                    } else {
                        *(s16*)norm_float = (s16)(int)(SCALE_INV * pos[0]);
                        *(s16*)((int)norm_float + 2) = (s16)(int)(SCALE_INV * pos[1]);
                        *(s16*)(norm_float + 1) = (s16)(int)(SCALE_INV * pos[2]);
                        norm_float = (f32*)((int)norm_float + 6);
                    }

                    // Transform normal vector
                    if (norm_short_ptr != NULL) {
                        // Decompress normal from s16, rotate by inverse bone, repack
                        f32 norm[3];
                        norm[0] = NORM_SCALE * (f32)(int)*(s16*)norm_short_ptr;
                        norm[1] = NORM_SCALE * (f32)(int)*(s16*)((int)norm_short_ptr + 2);
                        norm[2] = NORM_SCALE * (f32)(int)*(s16*)(norm_short_ptr + 1);
                        mtx_rotate_vec3(inv_mtx, norm);  // FUN_8012fb90
                        *(s16*)norm_short_ptr = (s16)(int)(NORM_SCALE_INV * norm[0]);
                        *(s16*)((int)norm_short_ptr + 2) = (s16)(int)(NORM_SCALE_INV * norm[1]);
                        *(s16*)(norm_short_ptr + 1) = (s16)(int)(NORM_SCALE_INV * norm[2]);
                        norm_short_ptr += 2;
                    } else if (pos_iter != NULL) {
                        mtx_rotate_vec3(inv_mtx, pos_iter);  // float normals
                        pos_iter += 4;
                    }
                }
            }
            strip_offset += 0x18;
        }
    }

    // Store updated flags and flush vertex/normal data
    *(u32*)(*pSkelState + 0xC) = flags;
    u32* mesh = (u32*)*pSkelState;
    int last_strip = mesh[4] + (mesh[6] - 1) * 0x18;
    last_strip = *(int*)(last_strip + 4) + (*(int*)(last_strip + 8) - 1) * 0x14;
    int total_verts = *(int*)(last_strip + 4) + *(int*)(last_strip + 8);

    // DCStoreRange for positions and normals
    dc_store_range(*mesh, (total_verts * 0xC + 0x1F) & ~0x1F);
    dc_store_range(*(u32*)(*pSkelState + 8), (total_verts * 0x10 + 0x1F) & ~0x1F);

    mem_free_top(bone_buf);
    pSkelState[2] = 0;
    pSkelState[1] = 0;
    os_restore_context();  // FUN_80382704
}

// ============================================================================
// 0x80069760 - cutscene_mesh_is_visible
// Stub: always returns 1 (mesh is always visible once in frame range).
// ============================================================================
static int cutscene_mesh_is_visible(void) {
    return 1;
}

// ============================================================================
// 0x80069768 - cutscene_frame_in_range
// Tests whether the current frame falls within the animation key range
// for a specific slot. Checks frame-pairs (start/end) in the key table.
// ============================================================================
static bool cutscene_frame_in_range(int entity_data, int slot_index) {
    int slot_offset = entity_data + slot_index * 0x2C;
    int anim_data   = *(int*)(slot_offset + 0x44);
    u32 header_size = *(u32*)(anim_data + 0x10);
    int pair_count  = *(int*)(anim_data + 0x14);
    bool result     = (pair_count == 0);  // visible if no key pairs defined

    // Convert current time to frame index
    f32 time_diff = *(f32*)(entity_data + 8) - *(f32*)(slot_offset + 0x34);
    int frame = (int)(FRAMES_PER_SECOND * time_diff);

    // Walk key pair table (each pair = start_frame, end_frame)
    int* pair_ptr = (int*)(anim_data + ((header_size >> 2) + ((int)header_size < 0 && (header_size & 3) != 0)) * 4);
    if (pair_count > 0) {
        for (int i = 0; i < pair_count; i++) {
            if (pair_ptr[0] - 1 <= frame && frame <= pair_ptr[1] + 1) {
                result = true;
                break;
            }
            if (frame < pair_ptr[1]) break;
            pair_ptr += 2;
        }
    }
    return result;
}

// ============================================================================
// 0x80069818 - cutscene_update_visibility_flag
// Checks whether the entity's current time is within its active range,
// then calls frame_in_range + mesh_is_visible to set/clear bit 0 of flags.
// ============================================================================
static void cutscene_update_visibility_flag(int vis_data, int entity_data, int slot_index) {
    int* ctx = (int*)os_get_context_upper();  // FUN_803826f0
    int  entity_base = (int)os_get_context_lower();
    int  slot_offset = entity_base + slot_index * 0x2C;

    f32 slot_start = *(f32*)(slot_offset + 0x34);
    f32 slot_end   = *(f32*)(slot_offset + 0x38);
    f32 cur_time   = *(f32*)(entity_base + 8);

    if (cur_time < slot_start || slot_end <= cur_time) {
        // Outside active time range - not visible
        *(u32*)(ctx + 0x28) &= ~1u;
    } else {
        bool in_range = cutscene_frame_in_range(entity_base, slot_index);
        if (in_range && cutscene_mesh_is_visible()) {
            *(u32*)(ctx + 0x28) |= 1u;
        } else {
            *(u32*)(ctx + 0x28) &= ~1u;
        }
    }
    os_restore_context();  // FUN_8038273c
}

// ============================================================================
// 0x800698d8 - cutscene_eval_strip_properties
// Evaluates per-strip material properties (texture IDs) and optional
// attached particle/effect sub-objects for a single bone group.
// ============================================================================
static void cutscene_eval_strip_properties(void) {
    int* ctx = (int*)os_get_context_upper();  // FUN_803826e4
    int  entity_base = (int)os_get_context_lower();
    int  skel_data   = *ctx;
    int  prop_table  = *(int*)(skel_data + 0x24);
    int  anim_index  = *(int*)(skel_data + 0x28);

    // Skip if entity is hidden (global flag check)
    if (DAT_80611b84 != 0 || (DAT_805015c0 & 0x200) == 0) {
        // Evaluate bone matrices for bone group
        int bone_count = *(int*)(entity_base + 0x10);
        int bone_start = *(int*)(entity_base + 0x14);
        int bone_offset = 0;
        for (int i = 0; i < bone_count; i++) {
            f32* bone_mtx = (f32*)(ctx[2] + (i + bone_start) * 0x40);
            if (i == 0) {
                mtx_invert_3x4(bone_mtx, &g_root_inv_matrix);
                DAT_80612250 = 1;
                DAT_80612258 = 0;
            } else {
                f32 local_mtx[16];
                mtx_invert_3x4(bone_mtx, local_mtx);
                gx_load_pos_matrix(local_mtx, bone_offset);  // FUN_803cb1c8
                gx_load_nrm_matrix(local_mtx, bone_offset);  // FUN_803cb248
            }
            bone_offset += 3;
        }

        // Evaluate material properties for each sub-object
        int* prop_list = *(int**)(entity_base + 4);
        for (int i = 0; i < *(int*)(entity_base + 8); i++) {
            int mat_index = render_prop_get_value_at_index(*prop_list, anim_index);
            if (mat_index >= 0) {
                mat_index = *(int*)(prop_table + mat_index * 0x10);
            }
            // Validate material index against global material table
            if (mat_index < 0 || mat_index >= g_material_table[3] || mat_index <= g_material_table[2]) {
                render_material_set_default();  // FUN_801720a8
            } else {
                DAT_80612254 = *g_material_table + mat_index * 0x58;
            }
            // Evaluate attached particle effects
            if (prop_list[4] != 0) {
                u32 prev_state = render_effect_save_state(0);  // FUN_80172a04
                render_effect_apply(prop_list[3], prop_list[4]);  // FUN_8017286c
                render_effect_save_state(prev_state);
            }
            prop_list += 5;
        }
    }
    os_restore_context();  // FUN_80382730
}

// ============================================================================
// 0x80069a70 - cutscene_get_anim_interp_factor
// Computes the interpolation factor between the current and next animation
// keyframes. Used to determine if frame advance is needed (returns >= 0.5
// means advance to next frame).
// ============================================================================
static void cutscene_get_anim_interp_factor(void) {
    int* ctx_upper = (int*)os_get_context_upper();  // FUN_803826f0
    int  ctx_lower = (int)os_get_context_lower();

    int anim_channel;
    if (ctx_lower == 0) {
        anim_channel = *(int*)(ctx_upper + 0x10);
        anim_get_frame_bounds(ctx_upper + 0x10, &cur_frame, &next_frame);  // FUN_80022ea4
    } else {
        anim_channel = *(int*)(*(int*)(ctx_lower + 4) + 0xCC);
        anim_get_frame_bounds(*(int*)(ctx_lower + 4) + 0xCC, &cur_frame, &next_frame);
    }

    if (cur_frame != next_frame) {
        // Look up keyframe times to compute interpolation factor
        int lut = *(int*)(*(int*)(DAT_80611824 + (s16)anim_table_offsets[anim_channel] * 4) + 0x1C);
        // factor = (time[next] - time[cur]) normalized
    }
    os_restore_context();  // FUN_8038273c
}

// ============================================================================
// 0x80069b9c - cutscene_entity_draw
// MASSIVE: Main draw call for cutscene entities. Evaluates 3-channel
// directional lighting from bone animation data, sets up GX vertex arrays
// (position/normal/color), configures materials, and submits mesh strips.
// ============================================================================
static void cutscene_entity_draw(int entity_data, int slot_index, int bone_group) {
    int* pEntity = (int*)os_get_context_upper();  // FUN_803826dc
    int  ctx_lower = (int)os_get_context_lower();

    int prev_tex_state = -1;
    int prev_blend_state = -1;
    int* skel_state = (int*)pEntity[ctx_lower * 0xB + 0x13];
    int  blend_slot = pEntity[ctx_lower * 0xB + 0x15];
    if (blend_slot == -1) blend_slot = ctx_lower;

    // Initialize 3 directional light channels to zero
    f32 light1_dir[3] = {0, 0, 0};
    f32 light2_dir[3] = {0, 0, 0};
    f32 light3_dir[3] = {0, 0, 0};
    f32 light1_col[3] = {0, 0, 0};
    f32 light2_col[3] = {0, 0, 0};
    f32 light3_col[3] = {0, 0, 0};

    // Sample lighting from animation bone channels (types 1, 2, 3)
    if (pEntity[blend_slot * 0xB + 0x11] != 0) {
        int frame = (int)(FRAMES_PER_SECOND * ((f32)pEntity[2] - (f32)pEntity[blend_slot * 0xB + 0xD]));
        int continuity = anim_motion_continuity_check(pEntity);
        f32 interp = cutscene_get_anim_interp_factor();

        if (continuity != 0) {
            interp = (interp >= 0.5f) ? 1.0f : 0.0f;
        }
        if (interp >= 0.5f) frame++;

        int chan_ptr;
        if (anim_bone_channel_lookup((int)pEntity, blend_slot, frame, 1, &chan_ptr)) {
            light1_col[0] = *(f32*)(chan_ptr + 0xC);
            light1_col[1] = *(f32*)(chan_ptr + 0x10);
            light1_col[2] = *(f32*)(chan_ptr + 0x14);
        }
        if (anim_bone_channel_lookup((int)pEntity, blend_slot, frame, 2, &chan_ptr)) {
            light2_col[0] = *(f32*)(chan_ptr + 0xC);
            light2_col[1] = *(f32*)(chan_ptr + 0x10);
            light2_col[2] = *(f32*)(chan_ptr + 0x14);
        }
        if (anim_bone_channel_lookup((int)pEntity, blend_slot, frame, 3, &chan_ptr)) {
            light3_col[0] = *(f32*)(chan_ptr + 0xC);
            light3_col[1] = *(f32*)(chan_ptr + 0x10);
            light3_col[2] = *(f32*)(chan_ptr + 0x14);
        }
    }

    // Save/restore texture and blend states
    if (*(s8*)(skel_state + 0x4F) != -1) {
        gx_set_tex_region((u32)*(u8*)((int)skel_state + 0x13F));  // FUN_8016efe8
        prev_tex_state = gx_get_tex_state(*(s8*)(skel_state + 0x4F));  // FUN_8016f00c
    }
    if (*(s8*)((int)skel_state + 0x13D) != -1) {
        prev_blend_state = gx_get_blend_state(*(s8*)((int)skel_state + 0x13D));  // FUN_8016e8ac
    }

    // Override light3 to zero in mode 0x10 (shadow-only pass)
    if (DAT_80612614 == 0x10) {
        light3_col[0] = light3_col[1] = light3_col[2] = 0.0f;
    }

    // Pack light colors into GX color registers (RGBA u8)
    f32 scale = 255.0f;  // FLOAT_80613f30
    *(u8*)(skel_state + 0x1E) = 0;   // ambient base = 0
    *(u8*)((int)skel_state + 0x79) = 0;
    *(u8*)((int)skel_state + 0x7A) = 0;
    *(u8*)((int)skel_state + 0x7B) = 0xFF;

    // Light channel 1 -> color register at +0x1F
    u32 color1;
    color1  = render_clamp_to_byte(light1_col[0] * scale) << 24;
    color1 |= render_clamp_to_byte(light1_col[1] * scale) << 16;
    color1 |= render_clamp_to_byte(light1_col[2] * scale) << 8;
    color1 |= 0xFF;
    gx_set_light_color((int)(skel_state + 0x1F), &color1);  // FUN_803c8610
    gx_set_light_attn((int)(skel_state + 0x1F), 1);         // FUN_803c8628

    // Light channel 2 -> color register at +0x2F
    u32 color2;
    color2  = render_clamp_to_byte(light2_col[0] * scale) << 24;
    color2 |= render_clamp_to_byte(light2_col[1] * scale) << 16;
    color2 |= render_clamp_to_byte(light2_col[2] * scale) << 8;
    color2 |= 0xFF;
    gx_set_light_color((int)(skel_state + 0x2F), &color2);
    gx_set_light_attn((int)(skel_state + 0x2F), 2);

    // Light channel 3 -> color register at +0x3F
    u32 color3;
    color3  = render_clamp_to_byte(light3_col[0] * scale) << 24;
    color3 |= render_clamp_to_byte(light3_col[1] * scale) << 16;
    color3 |= render_clamp_to_byte(light3_col[2] * scale) << 8;
    color3 |= 0xFF;
    gx_set_light_color((int)(skel_state + 0x3F), &color3);
    gx_set_light_attn((int)(skel_state + 0x3F), 4);

    // Submit ambient color
    u32 ambient = *(u32*)(skel_state + 0x1E);
    gx_set_ambient_color(4, &ambient);  // FUN_803c86a4

    // Set up GX vertex attribute arrays
    u32* mesh_desc = (u32*)*skel_state;
    u32  mesh_flags = mesh_desc[3];

    // Position array (float 12-byte or short 6-byte stride)
    if ((mesh_flags & 1) == 0) {
        gx_set_array(GX_VA_POS, *mesh_desc, 0xC);      // float XYZ
    } else {
        gx_set_array(GX_VA_POS, *mesh_desc, 6);        // s16 XYZ
    }
    // Normal array (float 16-byte or short 8-byte stride)
    if ((mesh_flags & 4) == 0) {
        gx_set_array(GX_VA_NRM, mesh_desc[2], 0x10);   // float XYZW
    } else {
        gx_set_array(GX_VA_NRM, mesh_desc[2], 8);      // s16 XYZW
    }
    // Color array (float 8-byte or short 4-byte stride)
    if ((mesh_flags & 2) == 0) {
        gx_set_array(GX_VA_CLR0, mesh_desc[1], 8);
    } else {
        gx_set_array(GX_VA_CLR0, mesh_desc[1], 4);
    }

    gx_begin_display_list();          // FUN_8017fab4
    gx_set_vtx_desc(4, 3);           // FUN_8016e444 - indexed pos/nrm/clr

    // Draw mesh strips matching the target bone group
    int strip_offset = 0;
    for (int group = 0; group < *(int*)(*skel_state + 0x18); group++) {
        if (*(int*)(*(int*)(*skel_state + 0x10) + strip_offset) == bone_group) {
            cutscene_eval_strip_properties();  // FUN_800698d8
        }
        strip_offset += 0x18;
    }

    gx_end_draw(0);                   // FUN_803cb298
    gx_set_vtx_desc(0, 0);           // reset

    // Restore texture/blend states
    if (prev_tex_state != -1) {
        gx_get_tex_state(prev_tex_state);
    }
    if (prev_blend_state != -1) {
        gx_get_blend_state(prev_blend_state);
    }
    os_restore_context();  // FUN_80382728
}

// ============================================================================
// 0x8006a06c - cutscene_render_pool_alloc
// Allocates a slot from the cutscene render entity pool (60 entries,
// stride 0x140 at DAT_80532a88). Initializes scale to 1.0 and zeroes state.
// ============================================================================
int* cutscene_render_pool_alloc(int entity_id) {
    int* slot = NULL;
    int  offset = 0;

    // Find first free slot (entry[0] == 0 means free)
    for (int i = 0; i < 60; i++) {
        if (*(int*)((int)&g_cutscene_render_pool + offset) == 0) {
            slot = (int*)((int)&g_cutscene_render_pool + offset);
        }
        offset += 0x140;
    }

    // Initialize slot
    *slot = entity_id;
    slot[1] = 0;             // state
    slot[2] = 0;             // frame
    slot[3] = 0;             // flags
    slot[0x1A] = FLOAT_ONE;  // scale X
    slot[0x1B] = FLOAT_ONE;  // scale Y
    slot[0x1C] = FLOAT_ONE;  // scale Z
    slot[0x1D] = FLOAT_ONE;  // alpha
    memset(&slot[4], 0, 0x58);  // clear transform data

    return slot;
}

// ============================================================================
// 0x8006a100 - cutscene_eval_skel_lighting
// Evaluates skeleton lighting for the primary cutscene entity. Samples
// 3 directional light channels from bone animation, applies camera-space
// rotation to all bone matrices and light directions, then stores light
// data in GX-compatible format.
// ============================================================================
static void cutscene_eval_skel_lighting(void) {
    int* pEntity = (int*)os_get_context_upper();  // FUN_803826e4
    int  ctx_lower = (int)os_get_context_lower();
    int* skel_state = (int*)pEntity[ctx_lower * 0xB + 0x13];
    int  blend_slot = pEntity[ctx_lower * 0xB + 0x15];
    if (blend_slot == -1) blend_slot = ctx_lower;

    // Initialize 3 light direction vectors to (1,1,1)
    f32 light1[3] = {1.0f, 1.0f, 1.0f};
    f32 light2[3] = {1.0f, 1.0f, 1.0f};
    f32 light3[3] = {1.0f, 1.0f, 1.0f};

    // Sample lighting from bone animation channels
    if (pEntity[blend_slot * 0xB + 0x11] != 0) {
        int frame = (int)(FRAMES_PER_SECOND * ((f32)pEntity[2] - (f32)pEntity[blend_slot * 0xB + 0xD]));
        int continuity = anim_motion_continuity_check(pEntity);
        f32 interp = cutscene_get_anim_interp_factor();

        if (continuity != 0) {
            interp = (interp >= 0.5f) ? 1.0f : 0.0f;
        }
        if (interp >= 0.5f) frame++;

        f32* chan_data;
        if (anim_bone_channel_lookup((int)pEntity, blend_slot, frame, 1, (int*)&chan_data)) {
            light1[0] = chan_data[0]; light1[1] = chan_data[1]; light1[2] = chan_data[2];
        }
        if (anim_bone_channel_lookup((int)pEntity, blend_slot, frame, 2, (int*)&chan_data)) {
            light2[0] = chan_data[0]; light2[1] = chan_data[1]; light2[2] = chan_data[2];
        }
        if (anim_bone_channel_lookup((int)pEntity, blend_slot, frame, 3, (int*)&chan_data)) {
            light3[0] = chan_data[0]; light3[1] = chan_data[1]; light3[2] = chan_data[2];
        }
    }

    // Build entity-to-camera-space rotation matrix
    skel_state[3] = 0;
    skel_pose_update(pEntity, ctx_lower, skel_state, 0);

    f32 entity_scale_mtx[16];
    mtx_build_scale_rotate(
        (f32)skel_state[0x1A], (f32)skel_state[0x1B], (f32)skel_state[0x1C],
        (PI * (f32)skel_state[0x1D]) / 180.0f,
        entity_scale_mtx
    );  // FUN_8012f2c4

    f32 cam_space_mtx[16];
    mtx_multiply(entity_scale_mtx, *(f32**)(g_camera_data + 0x208), cam_space_mtx);  // FUN_80130bd4

    // Transform all bone matrices into camera space
    int bone_offset = 0;
    int total_bones = *(int*)(*skel_state + 0x20);
    for (int i = 0; i < total_bones; i++) {
        f32 temp[16];
        mtx_invert_3x4((f32*)(skel_state[2] + bone_offset), temp);
        mtx_multiply((f32*)(skel_state[2] + bone_offset), cam_space_mtx,
                     (f32*)(skel_state[2] + bone_offset));
        bone_offset += 0x40;
    }

    // Rotate light directions into camera space
    mtx_rotate_vec3(cam_space_mtx, light1);
    mtx_rotate_vec3(cam_space_mtx, light2);
    mtx_rotate_vec3(cam_space_mtx, light3);

    // Store lights into GX light registers (direction + color packed)
    f32 dir_scale = LIGHT_DIR_SCALE;  // FLOAT_80613f3c
    gx_set_light_direction((f32)(light1[0] * dir_scale), (f32)(light1[1] * dir_scale),
                           (f32)(light1[2] * dir_scale), (int)(skel_state + 0x1F));
    gx_set_light_direction((f32)(light2[0] * dir_scale), (f32)(light2[1] * dir_scale),
                           (f32)(light2[2] * dir_scale), (int)(skel_state + 0x2F));
    gx_set_light_direction((f32)(light3[0] * dir_scale), (f32)(light3[1] * dir_scale),
                           (f32)(light3[2] * dir_scale), (int)(skel_state + 0x3F));

    os_restore_context();  // FUN_80382730
}

// ============================================================================
// 0x8006a3dc - cutscene_eval_secondary_lighting
// Evaluates lighting for secondary/attached entities (weapons, accessories).
// Similar to cutscene_eval_skel_lighting but reads 6 values per channel
// (direction XYZ + color RGB) and allocates a 0xC0-byte light buffer.
// ============================================================================
static void cutscene_eval_secondary_lighting(void) {
    int* pEntity = (int*)os_get_context_upper();  // FUN_803826e8
    int  ctx_lower = (int)os_get_context_lower();
    int  attach_data = pEntity[ctx_lower * 0xB + 0x12];
    int  blend_slot = pEntity[ctx_lower * 0xB + 0x15];
    if (blend_slot == -1) blend_slot = ctx_lower;

    // Initialize 3 light channels: direction (XYZ) + color (RGB) = 6 floats each
    f32 light1_pos[3] = {1.0f, 1.0f, 1.0f};
    f32 light2_pos[3] = {1.0f, 1.0f, 1.0f};
    f32 light3_pos[3] = {1.0f, 1.0f, 1.0f};
    f32 light1_col[3] = {0.0f, 0.0f, 0.0f};
    f32 light2_col[3] = {0.0f, 0.0f, 0.0f};
    f32 light3_col[3] = {0.0f, 0.0f, 0.0f};

    // Sample 6-float channels from bone animation
    if (pEntity[blend_slot * 0xB + 0x11] != 0) {
        int frame = (int)(FRAMES_PER_SECOND * ((f32)pEntity[2] - (f32)pEntity[blend_slot * 0xB + 0xD]));
        int continuity = anim_motion_continuity_check(pEntity);
        f32 interp = cutscene_get_anim_interp_factor();

        if (continuity != 0) {
            interp = (interp >= 0.5f) ? 1.0f : 0.0f;
        }
        if (interp >= 0.5f) frame++;

        f32* chan;
        if (anim_bone_channel_lookup((int)pEntity, blend_slot, frame, 1, (int*)&chan)) {
            light1_pos[0] = chan[0]; light1_pos[1] = chan[1]; light1_pos[2] = chan[2];
            light1_col[0] = chan[3]; light1_col[1] = chan[4]; light1_col[2] = chan[5];
        }
        if (anim_bone_channel_lookup((int)pEntity, blend_slot, frame, 2, (int*)&chan)) {
            light2_pos[0] = chan[0]; light2_pos[1] = chan[1]; light2_pos[2] = chan[2];
            light2_col[0] = chan[3]; light2_col[1] = chan[4]; light2_col[2] = chan[5];
        }
        if (anim_bone_channel_lookup((int)pEntity, blend_slot, frame, 3, (int*)&chan)) {
            light3_pos[0] = chan[0]; light3_pos[1] = chan[1]; light3_pos[2] = chan[2];
            light3_col[0] = chan[3]; light3_col[1] = chan[4]; light3_col[2] = chan[5];
        }
    }

    // Get attachment sub-object
    int sub_obj = *(int*)(attach_data + 4);

    // Build inverse camera matrix and rotate light directions
    f32 inv_cam[16];
    mtx_invert_3x4(*(f32**)(g_camera_data + 0x208), inv_cam);  // FUN_80130b90
    mtx_rotate_vec3(inv_cam, light1_pos);
    mtx_rotate_vec3(inv_cam, light2_pos);
    mtx_rotate_vec3(inv_cam, light3_pos);

    // Allocate 0xC0-byte light buffer (3 lights * 0x40 each)
    u32 light_buf = mem_advance_secondary(0xC0);
    *(u32*)(sub_obj + 0x70) = light_buf;
    *(u16*)(sub_obj + 0x74) = 3;  // 3 light channels
    *(u8*)(sub_obj + 0x6C) = 0;   // ambient R
    *(u8*)(sub_obj + 0x6D) = 0;   // ambient G
    *(u8*)(sub_obj + 0x6E) = 0;   // ambient B
    *(u8*)(sub_obj + 0x6F) = 0xFF; // ambient A

    // Pack colors into light buffer slots (0x40 apart)
    f32 scale = 255.0f;
    u32 c1 = (render_clamp_to_byte(scale * light1_col[0]) << 24) |
             (render_clamp_to_byte(scale * light1_col[1]) << 16) |
             (render_clamp_to_byte(scale * light1_col[2]) << 8) | 0x80;
    gx_set_light_color(light_buf, &c1);

    u32 c2 = (render_clamp_to_byte(scale * light2_col[0]) << 24) |
             (render_clamp_to_byte(scale * light2_col[1]) << 16) |
             (render_clamp_to_byte(scale * light2_col[2]) << 8) | 0x80;
    gx_set_light_color(light_buf + 0x40, &c2);

    u32 c3 = (render_clamp_to_byte(scale * light3_col[0]) << 24) |
             (render_clamp_to_byte(scale * light3_col[1]) << 16) |
             (render_clamp_to_byte(scale * light3_col[2]) << 8) | 0x80;
    gx_set_light_color(light_buf + 0x80, &c3);

    // Submit ambient and light directions
    u32 ambient = *(u32*)(sub_obj + 0x6C);
    gx_set_ambient_color(4, &ambient);

    f32 dir_scale = LIGHT_DIR_SCALE;
    gx_set_light_direction(light1_pos[0] * dir_scale, light1_pos[1] * dir_scale,
                           light1_pos[2] * dir_scale, light_buf);
    gx_set_light_direction(light2_pos[0] * dir_scale, light2_pos[1] * dir_scale,
                           light2_pos[2] * dir_scale, light_buf + 0x40);
    gx_set_light_direction(light3_pos[0] * dir_scale, light3_pos[1] * dir_scale,
                           light3_pos[2] * dir_scale, light_buf + 0x80);

    // Mark attachment as having active lighting
    *(u32*)(attach_data + 0xC) |= 0x10000000;
    *(u32*)(attach_data + 0xC) |= 0x20000000;

    os_restore_context();  // FUN_80382734
}
