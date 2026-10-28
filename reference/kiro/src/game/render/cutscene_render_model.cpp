// =============================================================================
// cutscene_render_model.cpp — Cutscene model loading, skinning, and visibility
// TimeSplitters: Future Perfect (GameCube)
// Batch 153 | Date: 2026-08-10
// =============================================================================

// Forward declarations
extern int DAT_80611b58;
extern int DAT_80611b5c;
extern int DAT_80611930;
extern int DAT_80611b84;
extern int DAT_805015c0;
extern int DAT_80612250;
extern int DAT_80612258;
extern int DAT_80612254;
extern int* DAT_80610a20;
extern float FLOAT_80613f00;
extern float FLOAT_80613f04;
extern float FLOAT_80613f08;
extern float FLOAT_80613f0c;
extern float FLOAT_80613f10;
extern float FLOAT_80613f14;
extern float FLOAT_80613f18;
extern float FLOAT_8060faf8;
extern int DAT_804d1cd4;
extern double DOUBLE_80613f20;
extern float DAT_8058e3cc[];

// External function declarations
void FUN_80253e7c(void*, void*, void*, void*, void*, void*, void*, void*);
void* FUN_803826f0(void);
void FUN_8038273c(void);
void* FUN_803826b8(void);
void* FUN_803826e4(void);
void FUN_80382704(void);
void FUN_80382730(void);
void FUN_80022438(int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int);
void* FUN_800229dc(int, int, int, int);
void skel_pose_update(int, int, int*, int);
void FUN_80130cd8(float*, float*);
void FUN_8012fbf4(float*, float*);
void FUN_8012fb90(float*, float*);
void FUN_8039955c(unsigned int, unsigned int);
void FUN_803cb1c8(float*, int);
void FUN_803cb248(float*, int);
int render_prop_get_value_at_index(int, int);
void gx_bind_material_fallback(void);
unsigned int FUN_80172a04(unsigned int);
void FUN_8017286c(int, int);
void* pack_get_asset_ptr(unsigned char*, void*);
int stream_find_complete_by_name(void);
void render_entity_fixup_embedded_ptrs(unsigned int, int);
void* render_entity_link_data(unsigned int, unsigned int*, unsigned int);
void* mem_alloc(void*, void*, void*, void*, void*, void*, void*, void*, int, int, void*, unsigned int, unsigned int, int, int, int);
void mem_free_top(int);
void FUN_8017bffc(void*, void*, void*, void*, void*, void*, void*, void*, int, int*, int*, unsigned int, unsigned int, int, int, int);

// =============================================================================
// Address: 0x80068f14 | render_clamp_to_byte
// Clamps a float to [0, 127] for byte packing (normals/weights).
// Called by: cutscene_entity_draw, cutscene_eval_secondary_lighting
// =============================================================================
int render_clamp_to_byte(float value)
{
    int result = (int)value;
    if (result >= 0x80) {
        result = 0x7f;
    } else if (result < 0) {
        result = 0;
    }
    return result;
}

// =============================================================================
// Address: 0x80068f48 | cutscene_set_frame_timestamps
// Stores current global frame timestamp pair into a render state struct.
// Called by: cutscene_entity_render_dispatch
// =============================================================================
void cutscene_set_frame_timestamps(int render_state)
{
    *(int*)(render_state + 4) = DAT_80611b58;
    *(int*)(render_state + 8) = DAT_80611b5c;
}

// =============================================================================
// Address: 0x80068f5c | cutscene_clear_frame_timestamps
// Clears frame timestamp pair in a render state struct to zero.
// =============================================================================
void cutscene_clear_frame_timestamps(int render_state)
{
    *(int*)(render_state + 4) = 0;
    *(int*)(render_state + 8) = 0;
}

// =============================================================================
// Address: 0x80068f6c | cutscene_render_dispatch_if_enabled
// Conditional render dispatch: if global render-disable flag is 0, calls the
// actual render submission function (FUN_80253e7c).
// Called by: FUN_801c1888
// =============================================================================
void cutscene_render_dispatch_if_enabled(
    void* p1, void* p2, void* p3, void* p4,
    void* p5, void* p6, void* p7, void* p8)
{
    if (DAT_80611930 == 0) {
        FUN_80253e7c(p1, p2, p3, p4, p5, p6, p7, p8);
    }
}

// =============================================================================
// Address: 0x80068f98 | cutscene_load_model_and_fixup
// Loads a cutscene model asset: resolves from pack or streaming cache,
// fixes up embedded relative pointers to absolute, links render data,
// allocates bone instance buffer, and processes sub-object material tables.
// Called by: cutscene_load_scene_data
// =============================================================================
void cutscene_load_model_and_fixup(void)
{
    unsigned char* name_ptr = (unsigned char*)FUN_803826f0();
    int stream_entry = stream_find_complete_by_name();

    unsigned int* asset_base;
    if (stream_entry == 0) {
        asset_base = (unsigned int*)pack_get_asset_ptr(name_ptr, (void*)0);
    } else {
        asset_base = *(unsigned int**)(stream_entry + 0x10);
    }

    unsigned int skeleton_offset = asset_base[0];
    unsigned int header_offset_flag = -(unsigned int)(asset_base[2] != 0);
    unsigned int* header = (unsigned int*)(asset_base[2] + (int)asset_base & header_offset_flag);

    // Fix up skeleton pointer
    header[9] = skeleton_offset + (int)asset_base & -(unsigned int)(skeleton_offset != 0);
    render_entity_fixup_embedded_ptrs(header[9], (int)asset_base);

    // Fix up animation data pointer
    header[10] = asset_base[1] + (int)asset_base & -(unsigned int)(asset_base[1] != 0);
    void* link_result = render_entity_link_data(header[10], asset_base, header[9]);

    // Fix up additional embedded pointers (vertices, normals, UVs, etc.)
    header[0] = header[0] + (int)asset_base & -(unsigned int)(header[0] != 0);
    header[1] = header[1] + (int)asset_base & -(unsigned int)(header[1] != 0);
    header[2] = header[2] + (int)asset_base & -(unsigned int)(header[2] != 0);
    header[4] = header[4] + (int)asset_base & -(unsigned int)(header[4] != 0);
    header[5] = header[5] + (int)asset_base & -(unsigned int)(header[5] != 0);

    // Count sub-objects (empty loop — side effect in original was cache warming)
    unsigned int sub_obj_count = header[6];
    if (0 < (int)sub_obj_count) {
        // Original loop decremented to zero (no-op in decomp)
    }

    // Fix up per-sub-object material pointers
    unsigned int strip_table = header[4];
    for (int i = 0; i < (int)header[6]; i++) {
        *(unsigned int*)(strip_table + 4) =
            *(int*)(strip_table + 4) + (int)asset_base & -(unsigned int)(*(int*)(strip_table + 4) != 0);
        *(unsigned int*)(strip_table + 0xc) =
            *(int*)(strip_table + 0xc) + (int)asset_base & -(unsigned int)(*(int*)(strip_table + 0xc) != 0);

        unsigned int mat_ptr = *(unsigned int*)(strip_table + 4);
        for (unsigned int j = 0; j < *(unsigned int*)(strip_table + 8); j++) {
            *(unsigned int*)(mat_ptr + 0xc) =
                *(int*)(mat_ptr + 0xc) + (int)asset_base & -(unsigned int)(*(int*)(mat_ptr + 0xc) != 0);
            mat_ptr += 0x14;
        }
        strip_table += 0x18;
    }

    // Allocate bone instance buffer
    unsigned int bone_buf = (unsigned int)mem_alloc(
        link_result, 0, 0, 0, 0, 0, 0, 0,
        header[7] << 6, 0, (void*)strip_table, 0, 0, 0, 0, 0);
    header[0xb] = bone_buf;

    // Walk skeleton and register bone entries (terminated by -1 sentinel)
    int offset = 0;
    unsigned int skel_data = header[9];
    while (true) {
        int* bone_entry = (int*)(skel_data + offset);
        if (*bone_entry == -1) break;
        FUN_8017bffc(0, 0, 0, 0, 0, 0, 0, 0, *bone_entry, bone_entry, bone_entry, 0, 0, 0, 0, 0);
        offset += 0x10;
    }

    FUN_8038273c();
}

// =============================================================================
// Address: 0x800691ac | cutscene_mesh_skin_init
// Initializes cutscene mesh skinning: allocates bone matrices, evaluates the
// skeleton bind pose, and transforms vertices/normals into bone-local space.
// Supports both compressed s16 and float vertex formats.
// Called by: cutscene_load_scene_data
// =============================================================================
void cutscene_mesh_skin_init(int do_skin)
{
    void* ctx = FUN_803826b8();
    int* skel_state = (int*)((unsigned long long)ctx >> 32);
    int ctx_low = (int)ctx;

    FUN_80022438(0, 0, 0, 0, 0, 0, 0, 0, ctx_low, 2, 0, 0, 0, 0, 0, 0);
    FUN_800229dc(2, 0, 0x1800, 0x82);

    skel_state[4] = 2;
    float scale = FLOAT_80613f00;
    skel_state[5] = DAT_804d1cd4;
    skel_state[6] = (int)scale;
    skel_state[0x11] = (int)scale;

    if (do_skin != 0) {
        unsigned int* mesh_def = (unsigned int*)skel_state[0];
        unsigned int flags = mesh_def[3];
        float* vert_data = (float*)mesh_def[2];
        float* pos_data = (float*)mesh_def[0];

        // Determine data format from flags
        float* float_pos = (flags & 1) ? NULL : pos_data;
        float* s16_pos = (flags & 1) ? pos_data : NULL;
        float* float_norm = (flags & 4) ? NULL : vert_data;
        float* norm_offset = (flags & 4) ? NULL : vert_data + 3;
        short* s16_norm = (flags & 4) ? (short*)((int)vert_data + 6) : NULL;
        float* s16_norm_base = (flags & 4) ? vert_data : NULL;

        // Allocate transform matrices
        int bone_matrices = (int)mem_alloc(
            0, 0, 0, 0, 0, 0, 0, 0,
            (mesh_def[7] + mesh_def[8]) * 0x40, 0, 0, 0, 0, 0, 0, 0);
        skel_state[1] = bone_matrices;
        skel_state[2] = bone_matrices + *(int*)(skel_state[0] + 0x1c) * 0x40;

        // Evaluate bind pose
        skel_pose_update(0, 0, skel_state, do_skin);

        // Transform vertices into bone-local space
        if ((*(unsigned int*)(skel_state[0] + 0xc) & 0x100) == 0) {
            int strip_offset = 0;
            for (int group = 0; group < *(int*)(skel_state[0] + 0x18); group++) {
                int* strip_entry = (int*)(*(int*)(skel_state[0] + 0x10) + strip_offset);
                flags |= 0x20 << *strip_entry;

                int mat_offset = 0;
                for (int mat = 0; mat < strip_entry[2]; mat++) {
                    int mat_data = strip_entry[1];
                    for (int vert = 0; vert < *(int*)(mat_data + mat_offset + 8); vert++) {
                        // Get bone index from weight data
                        float weight_val;
                        if (norm_offset == NULL) {
                            weight_val = (float)(int)*s16_norm;
                            s16_norm += 4;
                        } else {
                            weight_val = *norm_offset;
                            norm_offset += 4;
                        }

                        // Get bone transform matrix
                        float* bone_mtx = (float*)(skel_state[2] +
                            (strip_entry[5] + (((unsigned int)weight_val & 0xff) >> 3)) * 0x40);

                        float inv_mtx[16];
                        FUN_80130cd8(bone_mtx, inv_mtx);

                        // Read vertex position
                        float pos[3];
                        if (s16_pos == NULL) {
                            pos[0] = *float_pos;
                            pos[1] = float_pos[1];
                            pos[2] = float_pos[2];
                        } else {
                            int base_addr = skel_state[0];
                            pos[0] = FLOAT_80613f04 * (float)(int)*(short*)s16_pos
                                     + *(float*)(base_addr + 0x30);
                            pos[1] = FLOAT_80613f04 * (float)(int)*((short*)s16_pos + 1)
                                     + *(float*)(base_addr + 0x34);
                            pos[2] = FLOAT_80613f04 * (float)(int)*((short*)s16_pos + 2)
                                     + *(float*)(base_addr + 0x38);
                        }

                        // Transform position to bone-local space
                        if ((*(unsigned int*)(skel_state[0] + 0xc) & 0x100) == 0) {
                            pos[0] -= bone_mtx[0xc];
                            pos[1] -= bone_mtx[0xd];
                            pos[2] -= bone_mtx[0xe];
                            FUN_8012fbf4(inv_mtx, pos);
                        }

                        // Optional clamping (flag bit 0)
                        if ((*(unsigned int*)(skel_state[0] + 0xc) & 1) != 0) {
                            // Clamp each component to [-FLOAT_80613f0c, FLOAT_80613f08]
                            float clamp_max = FLOAT_80613f08;
                            float clamp_min = FLOAT_80613f0c;
                            pos[0] = (pos[0] > clamp_max) ? clamp_max :
                                     (pos[0] < clamp_min) ? clamp_min : pos[0];
                            pos[1] = (pos[1] > clamp_max) ? clamp_max :
                                     (pos[1] < clamp_min) ? clamp_min : pos[1];
                            pos[2] = (pos[2] > clamp_max) ? clamp_max :
                                     (pos[2] < clamp_min) ? clamp_min : pos[2];
                        }

                        // Write back transformed position
                        if (s16_pos == NULL) {
                            *float_pos = pos[0];
                            float_pos[1] = pos[1];
                            float_pos[2] = pos[2];
                            float_pos += 3;
                        } else {
                            float scale_factor = FLOAT_80613f10;
                            *(short*)s16_pos = (short)(int)(scale_factor * pos[0]);
                            *((short*)s16_pos + 1) = (short)(int)(scale_factor * pos[1]);
                            *((short*)s16_pos + 2) = (short)(int)(scale_factor * pos[2]);
                            s16_pos = (float*)((int)s16_pos + 6);
                        }

                        // Transform normals
                        if (float_norm != NULL) {
                            FUN_8012fb90(inv_mtx, float_norm);
                            float_norm += 4;
                        } else if (s16_norm_base != NULL) {
                            float norm[3];
                            norm[0] = FLOAT_80613f14 * (float)(int)*(short*)s16_norm_base;
                            norm[1] = FLOAT_80613f14 * (float)(int)*((short*)s16_norm_base + 1);
                            norm[2] = FLOAT_80613f14 * (float)(int)*((short*)((int)s16_norm_base + 4));
                            FUN_8012fb90(inv_mtx, norm);
                            float norm_scale = FLOAT_80613f18;
                            *(short*)s16_norm_base = (short)(int)(norm_scale * norm[0]);
                            *((short*)s16_norm_base + 1) = (short)(int)(norm_scale * norm[1]);
                            *((short*)((int)s16_norm_base + 4)) = (short)(int)(norm_scale * norm[2]);
                            s16_norm_base += 2;
                        }
                    }
                    mat_offset += 0x14;
                }
                strip_offset += 0x18;
            }
        }

        *(unsigned int*)(skel_state[0] + 0xc) = flags;

        // Flush vertex/normal data from cache
        unsigned int* mesh = (unsigned int*)skel_state[0];
        int last_strip = mesh[4] + (mesh[6] - 1) * 0x18;
        int last_mat = *(int*)(last_strip + 4) + (*(int*)(last_strip + 8) - 1) * 0x14;
        int total_verts = *(int*)(last_mat + 4) + *(int*)(last_mat + 8);
        FUN_8039955c(*mesh, (total_verts * 0xc + 0x1f) & 0xffffffe0);
        FUN_8039955c(*(unsigned int*)(skel_state[0] + 8), (total_verts * 0x10 + 0x1f) & 0xffffffe0);

        mem_free_top(bone_matrices);
    }

    skel_state[2] = 0;
    skel_state[1] = 0;
    FUN_80382704();
}

// =============================================================================
// Address: 0x80069760 | cutscene_mesh_is_visible
// Stub: always returns 1 (mesh is always visible once in frame range).
// =============================================================================
int cutscene_mesh_is_visible(void)
{
    return 1;
}

// =============================================================================
// Address: 0x80069768 | cutscene_frame_in_range
// Tests if the current animation frame falls within any of the key-pair
// visibility ranges for a given cutscene slot. Walks the frame-pair table.
// =============================================================================
int cutscene_frame_in_range(int anim_state, int slot_index)
{
    int slot_data = anim_state + slot_index * 0x2c;
    int key_data = *(int*)(slot_data + 0x44);
    unsigned int key_header = *(unsigned int*)(key_data + 0x10);
    int pair_count = *(int*)(key_data + 0x14);
    int visible = (pair_count == 0) ? 1 : 0;

    // Compute current frame from time delta
    int current_frame = (int)(FLOAT_8060faf8 *
        (*(float*)(anim_state + 8) - *(float*)(slot_data + 0x34)));

    // Skip past header data to frame pair array
    int* pair_ptr = (int*)(key_data +
        (((int)key_header >> 2) + (unsigned int)((int)key_header < 0 && (key_header & 3) != 0)) * 4);

    if (pair_count > 0) {
        for (int i = 0; i < pair_count; i++) {
            if ((pair_ptr[0] - 1 <= current_frame) && (current_frame <= pair_ptr[1] + 1)) {
                visible = 1;
                break;
            }
            if (current_frame < pair_ptr[1]) break;
            pair_ptr += 2;
        }
    }

    return visible;
}

// =============================================================================
// Address: 0x80069818 | cutscene_update_visibility_flag
// Updates the visibility flag (bit 0 of flags at +0x28) based on time range
// and frame range checks. Sets flag if entity is in active time window.
// =============================================================================
void cutscene_update_visibility_flag(int slot_index)
{
    void* ctx = FUN_803826f0();
    int render_obj = (int)((unsigned long long)ctx >> 32);
    int anim_state = (int)ctx;

    int slot_data = anim_state + slot_index * 0x2c;
    float current_time = *(float*)(anim_state + 8);
    float start_time = *(float*)(slot_data + 0x34);
    float end_time = *(float*)(slot_data + 0x38);

    if (current_time < start_time || end_time <= current_time) {
        // Outside time window — hide
        *(unsigned int*)(render_obj + 0x28) &= 0xfffffffe;
    } else {
        // In time window — check frame range and mesh visibility
        int in_range = cutscene_frame_in_range(anim_state, slot_index);
        if (in_range && cutscene_mesh_is_visible() != 0) {
            *(unsigned int*)(render_obj + 0x28) |= 1;
        } else {
            *(unsigned int*)(render_obj + 0x28) &= 0xfffffffe;
        }
    }

    FUN_8038273c();
}

// =============================================================================
// Address: 0x800698d8 | cutscene_eval_strip_properties
// Evaluates per-strip material properties and attached particle effects for a
// cutscene bone group. Sets up GX bone matrices, resolves material indices
// from a property table, and applies effect overrides.
// Called by: cutscene_entity_draw
// =============================================================================
void cutscene_eval_strip_properties(void)
{
    void* ctx = FUN_803826e4();
    int* render_ptr = (int*)((unsigned long long)ctx >> 32);
    int bone_group = (int)ctx;

    int mesh_base = *render_ptr;
    int prop_table = *(int*)(mesh_base + 0x24);
    int material_table = *(int*)(mesh_base + 0x28);

    if (DAT_80611b84 != 0 || (DAT_805015c0 & 0x200) == 0) {
        // Set up GX bone matrices for each bone in the group
        int mtx_index = 0;
        for (int bone = 0; bone < *(int*)(bone_group + 0x10); bone++) {
            float* bone_mtx = (float*)(render_ptr[2] +
                (bone + *(int*)(bone_group + 0x14)) * 0x40);

            if (bone == 0) {
                FUN_80130cd8(bone_mtx, DAT_8058e3cc);
                DAT_80612250 = 1;
                DAT_80612258 = 0;
            } else {
                float local_mtx[16];
                FUN_80130cd8(bone_mtx, local_mtx);
                FUN_803cb1c8(local_mtx, mtx_index);
                FUN_803cb248(local_mtx, mtx_index);
            }
            mtx_index += 3;
        }

        // Process per-strip materials
        int* strip_ptr = *(int**)(bone_group + 4);
        for (int strip = 0; strip < *(int*)(bone_group + 8); strip++) {
            int mat_idx = render_prop_get_value_at_index(*strip_ptr, material_table);
            if (mat_idx >= 0) {
                mat_idx = *(int*)(prop_table + mat_idx * 0x10);
            }

            if (mat_idx < 0 || DAT_80610a20[3] <= mat_idx || mat_idx <= DAT_80610a20[2]) {
                gx_bind_material_fallback();
            } else {
                DAT_80612254 = *DAT_80610a20 + mat_idx * 0x58;
            }

            // Apply particle effects if attached
            if (strip_ptr[4] != 0) {
                unsigned int prev_state = FUN_80172a04(0);
                FUN_8017286c(strip_ptr[3], strip_ptr[4]);
                FUN_80172a04(prev_state);
            }

            strip_ptr += 5;
        }
    }

    FUN_80382730();
}
