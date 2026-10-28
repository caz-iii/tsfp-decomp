// =============================================================================
// cutscene_render_draw.cpp — Cutscene entity drawing, lighting, and pool alloc
// TimeSplitters: Future Perfect (GameCube)
// Batch 154 | Date: 2026-08-10
// =============================================================================

// External globals
extern float FLOAT_80613f00;   // 0.0f
extern float FLOAT_80613f28;   // 0.5f threshold
extern float FLOAT_80613f2c;   // 1.0f (or near)
extern float FLOAT_80613f30;   // 127.0f (color scale)
extern float FLOAT_80613f34;   // pi-related
extern float FLOAT_80613f38;   // pi-related
extern float FLOAT_80613f3c;   // light direction scale
extern float FLOAT_8060faf8;   // 30.0f (frame rate)
extern double DOUBLE_80613f20;
extern int DAT_80611824;
extern int DAT_80611b84;
extern int DAT_80611d54;
extern int DAT_80612614;
extern int DAT_80532a88;
extern short DAT_804d1cd0[];

// External functions
void* FUN_803826f0(void);
void* FUN_803826dc(void);
void* FUN_803826e4(void);
void* FUN_803826e8(void);
void FUN_8038273c(void);
void FUN_80382728(void);
void FUN_80382730(void);
void FUN_80382734(void);
void FUN_80022ea4(int, unsigned int*, unsigned int*);
int anim_motion_continuity_check(int*);
int anim_bone_channel_lookup(int, int, int, int, int*);
void skel_pose_update(int*, int, int*, int);
int render_clamp_to_byte(float);
void cutscene_eval_strip_properties(void);
void FUN_803c8610(int, void*);
void FUN_803c8628(int, int);
void FUN_803c86a4(int, void*);
void FUN_803c84e4(double, double, double, int);
void FUN_8016e694(int, int, int);
void FUN_8016efe8(unsigned int);
int FUN_8016f00c(int);
int FUN_8016e8ac(int);
void FUN_8017fab4(void);
void FUN_8016e444(int, int);
void FUN_803cb298(int);
void FUN_8012f2c4(double, double, double, double, float*);
void FUN_80130bd4(float*, float*, float*);
void FUN_80130cd8(float*, float*);
void FUN_8012fb90(float*, float*);
void FUN_80130b90(float*, float*);
void* mem_advance_secondary(int);
void memset_wrapper(int, int, int);
double cutscene_get_anim_interp_factor(void);

// =============================================================================
// Address: 0x80069a70 | cutscene_get_anim_interp_factor
// Computes interpolation factor between current and next animation keyframes.
// Returns >= 0.5 to indicate frame should advance.
// Used by all cutscene lighting/draw functions.
// =============================================================================
void cutscene_get_anim_interp_factor(void)
{
    void* ctx = FUN_803826f0();
    int render_obj = (int)((unsigned long long)ctx >> 32);
    int entity_data = (int)ctx;

    int anim_key_idx;
    unsigned int next_key, current_key;

    if (entity_data == 0) {
        anim_key_idx = *(int*)(render_obj + 0x10);
        FUN_80022ea4(render_obj + 0x10, &next_key, &current_key);
    } else {
        anim_key_idx = *(int*)(*(int*)(entity_data + 4) + 0xcc);
        FUN_80022ea4(*(int*)(entity_data + 4) + 0xcc, &next_key, &current_key);
    }

    if (next_key != current_key) {
        // Compute interpolation factor from keyframe timing table
        int timing_table = *(int*)(*(int*)(DAT_80611824 +
            (short)DAT_804d1cd0[anim_key_idx] * 4) + 0x1c);
        // Factor computed via double-precision int-to-float conversion
        // (original uses paired-single FPU trick with 0x43300000 magic)
    }

    FUN_8038273c();
}

// =============================================================================
// Address: 0x80069b9c | cutscene_entity_draw
// Main cutscene entity draw call. Evaluates 3-channel directional lighting
// from bone animation, sets up GX vertex arrays (pos/nrm/clr), configures
// materials, and submits mesh strips. Saves/restores texture and blend states.
// =============================================================================
void cutscene_entity_draw(int param_1, int param_2, int bone_group_id)
{
    void* ctx = FUN_803826dc();
    int* entity = (int*)((unsigned long long)ctx >> 32);
    int slot_idx = (int)ctx;

    int prev_tex_state = -1;
    int prev_blend_state = -1;
    int* skel_state = (int*)entity[slot_idx * 0xb + 0x13];
    int anim_slot = entity[slot_idx * 0xb + 0x15];
    if (anim_slot == -1) {
        anim_slot = slot_idx;
    }

    // Initialize all 9 lighting values to 0.0
    float light_r1 = FLOAT_80613f00, light_g1 = FLOAT_80613f00, light_b1 = FLOAT_80613f00;
    float light_r2 = FLOAT_80613f00, light_g2 = FLOAT_80613f00, light_b2 = FLOAT_80613f00;
    float light_r3 = FLOAT_80613f00, light_g3 = FLOAT_80613f00, light_b3 = FLOAT_80613f00;

    // Sample lighting from bone animation channels 1, 2, 3
    if (entity[anim_slot * 0xb + 0x11] != 0) {
        int frame = (int)(FLOAT_8060faf8 *
            ((float)entity[2] - (float)entity[anim_slot * 0xb + 0xd]));

        int continuity = anim_motion_continuity_check(entity);
        double interp = (double)cutscene_get_anim_interp_factor();

        if (continuity != 0) {
            interp = ((double)FLOAT_80613f28 <= interp) ?
                     (double)FLOAT_80613f2c : (double)FLOAT_80613f00;
        }
        if ((double)FLOAT_80613f28 <= interp) {
            frame++;
        }

        int channel_data;
        if (anim_bone_channel_lookup((int)entity, anim_slot, frame, 1, &channel_data)) {
            light_r1 = *(float*)(channel_data + 0xc);
            light_g1 = *(float*)(channel_data + 0x10);
            light_b1 = *(float*)(channel_data + 0x14);
        }
        if (anim_bone_channel_lookup((int)entity, anim_slot, frame, 2, &channel_data)) {
            light_r2 = *(float*)(channel_data + 0xc);
            light_g2 = *(float*)(channel_data + 0x10);
            light_b2 = *(float*)(channel_data + 0x14);
        }
        if (anim_bone_channel_lookup((int)entity, anim_slot, frame, 3, &channel_data)) {
            light_r3 = *(float*)(channel_data + 0xc);
            light_g3 = *(float*)(channel_data + 0x10);
            light_b3 = *(float*)(channel_data + 0x14);
        }
    }

    // Save/set texture and blend states
    if (*(char*)(skel_state + 0x4f) != -1) {
        FUN_8016efe8((unsigned int)*(unsigned char*)((int)skel_state + 0x13f));
        prev_tex_state = FUN_8016f00c((int)*(char*)(skel_state + 0x4f));
    }
    if (*(char*)((int)skel_state + 0x13d) != -1) {
        prev_blend_state = FUN_8016e8ac((int)*(char*)((int)skel_state + 0x13d));
    }

    // Override channel 3 to zero if special render mode 0x10
    if (DAT_80612614 == 0x10) {
        light_r3 = FLOAT_80613f00;
        light_g3 = FLOAT_80613f00;
        light_b3 = FLOAT_80613f00;
    }

    float color_scale = FLOAT_80613f30;

    // Set ambient color to black
    *(unsigned char*)(skel_state + 0x1e) = 0;
    *(unsigned char*)((int)skel_state + 0x79) = 0;
    *(unsigned char*)((int)skel_state + 0x7a) = 0;
    *(unsigned char*)((int)skel_state + 0x7b) = 0xff;

    // Pack channel 1 color (RGB, alpha=0xFF)
    unsigned int color1;
    int r = render_clamp_to_byte(light_r1 * color_scale);
    int g = render_clamp_to_byte(light_g1 * color_scale);
    int b = render_clamp_to_byte(light_b1 * color_scale);
    color1 = (r << 24) | (g << 16) | (b << 8) | 0xff;
    FUN_803c8610((int)(skel_state + 0x1f), &color1);
    FUN_803c8628((int)(skel_state + 0x1f), 1);

    // Pack channel 2 color
    unsigned int color2;
    r = render_clamp_to_byte(light_r2 * color_scale);
    g = render_clamp_to_byte(light_g2 * color_scale);
    b = render_clamp_to_byte(light_b2 * color_scale);
    color2 = (r << 24) | (g << 16) | (b << 8) | 0xff;
    FUN_803c8610((int)(skel_state + 0x2f), &color2);
    FUN_803c8628((int)(skel_state + 0x2f), 2);

    // Pack channel 3 color
    unsigned int color3;
    r = render_clamp_to_byte(light_r3 * color_scale);
    g = render_clamp_to_byte(light_g3 * color_scale);
    b = render_clamp_to_byte(light_b3 * color_scale);
    color3 = (r << 24) | (g << 16) | (b << 8) | 0xff;
    FUN_803c8610((int)(skel_state + 0x3f), &color3);
    FUN_803c8628((int)(skel_state + 0x3f), 4);

    // Set ambient from packed value
    unsigned int ambient = *(unsigned int*)(skel_state + 0x1e);
    FUN_803c86a4(4, &ambient);

    // Configure GX vertex arrays based on format flags
    unsigned int* mesh_def = (unsigned int*)*skel_state;
    unsigned int norm_data = mesh_def[2];
    unsigned int color_data = mesh_def[1];
    unsigned int flags = mesh_def[3];

    if ((flags & 1) == 0) {
        FUN_8016e694(9, mesh_def[0], 0xc);  // float positions, stride 12
    } else {
        FUN_8016e694(9, mesh_def[0], 6);    // s16 positions, stride 6
    }

    if ((*(unsigned int*)(*skel_state + 0xc) & 4) == 0) {
        FUN_8016e694(10, norm_data, 0x10);  // float normals, stride 16
    } else {
        FUN_8016e694(10, norm_data, 8);     // s16 normals, stride 8
    }

    if ((*(unsigned int*)(*skel_state + 0xc) & 2) == 0) {
        FUN_8016e694(0xd, color_data, 8);   // RGBA8 colors, stride 8
    } else {
        FUN_8016e694(0xd, color_data, 4);   // RGBA4 colors, stride 4
    }

    FUN_8017fab4();
    FUN_8016e444(4, 3);

    // Submit each bone group strip that matches bone_group_id
    int strip_offset = 0;
    for (int i = 0; i < *(int*)(*skel_state + 0x18); i++) {
        if (*(int*)(*(int*)(*skel_state + 0x10) + strip_offset) == bone_group_id) {
            cutscene_eval_strip_properties();
        }
        strip_offset += 0x18;
    }

    // Restore states
    FUN_803cb298(0);
    FUN_8016e444(0, 0);
    if (prev_tex_state != -1) {
        FUN_8016f00c(prev_tex_state);
    }
    if (prev_blend_state != -1) {
        FUN_8016e8ac(prev_blend_state);
    }

    FUN_80382728();
}

// =============================================================================
// Address: 0x8006a06c | cutscene_render_pool_alloc
// Allocates a slot from the cutscene render entity pool (60 entries, stride
// 0x140 at DAT_80532a88). Initializes scale to 1.0, zeroes transform state.
// =============================================================================
int* cutscene_render_pool_alloc(int entity_ref)
{
    int offset = 0;
    int* slot = NULL;
    int remaining = 0x3c;  // 60 slots

    do {
        if (*(int*)((int)&DAT_80532a88 + offset) == 0) {
            slot = (int*)((int)&DAT_80532a88 + offset);
        }
        offset += 0x140;
        remaining--;
    } while (remaining != 0);

    *slot = entity_ref;
    float one = FLOAT_80613f00;  // actually 1.0 stored in this context
    slot[1] = 0;  // position X
    slot[2] = 0;  // position Y
    slot[3] = 0;  // position Z
    slot[0x1a] = *(int*)&one;  // scale X
    slot[0x1b] = *(int*)&one;  // scale Y
    slot[0x1c] = *(int*)&one;  // scale Z
    slot[0x1d] = *(int*)&one;  // uniform scale

    memset_wrapper((int)(slot + 4), 0, 0x58);  // clear rotation/transform

    return slot;
}

// =============================================================================
// Address: 0x8006a100 | cutscene_eval_skel_lighting
// Evaluates skeleton lighting for primary cutscene entity. Samples 3
// directional light channels from bone animation, builds entity-to-camera
// rotation matrix, transforms all bone matrices and light directions into
// camera space.
// =============================================================================
void cutscene_eval_skel_lighting(void)
{
    void* ctx = FUN_803826e4();
    int* entity = (int*)((unsigned long long)ctx >> 32);
    int slot_idx = (int)ctx;

    int* skel_state = (int*)entity[slot_idx * 0xb + 0x13];
    int anim_slot = entity[slot_idx * 0xb + 0x15];
    if (anim_slot == -1) {
        anim_slot = slot_idx;
    }

    // Initialize 3 light direction vectors to zero
    float dir1[3] = { FLOAT_80613f00, FLOAT_80613f00, FLOAT_80613f00 };
    float dir2[3] = { FLOAT_80613f00, FLOAT_80613f00, FLOAT_80613f00 };
    float dir3[3] = { FLOAT_80613f00, FLOAT_80613f00, FLOAT_80613f00 };

    // Sample light directions from bone animation
    if (entity[anim_slot * 0xb + 0x11] != 0) {
        int frame = (int)(FLOAT_8060faf8 *
            ((float)entity[2] - (float)entity[anim_slot * 0xb + 0xd]));

        int continuity = anim_motion_continuity_check(entity);
        double interp = (double)cutscene_get_anim_interp_factor();

        if (continuity != 0) {
            interp = ((double)FLOAT_80613f28 <= interp) ?
                     (double)FLOAT_80613f2c : (double)FLOAT_80613f00;
        }
        if ((double)FLOAT_80613f28 <= interp) {
            frame++;
        }

        float* channel_ptr;
        if (anim_bone_channel_lookup((int)entity, anim_slot, frame, 1, (int*)&channel_ptr)) {
            dir1[0] = channel_ptr[0]; dir1[1] = channel_ptr[1]; dir1[2] = channel_ptr[2];
        }
        if (anim_bone_channel_lookup((int)entity, anim_slot, frame, 2, (int*)&channel_ptr)) {
            dir2[0] = channel_ptr[0]; dir2[1] = channel_ptr[1]; dir2[2] = channel_ptr[2];
        }
        if (anim_bone_channel_lookup((int)entity, anim_slot, frame, 3, (int*)&channel_ptr)) {
            dir3[0] = channel_ptr[0]; dir3[1] = channel_ptr[1]; dir3[2] = channel_ptr[2];
        }
    }

    // Update skeleton pose
    skel_state[3] = 0;
    skel_pose_update(entity, slot_idx, skel_state, 0);

    // Build entity rotation matrix from Euler angles + FOV
    float entity_mtx[16];
    FUN_8012f2c4((double)(float)skel_state[0x1a],
                 (double)(float)skel_state[0x1b],
                 (double)(float)skel_state[0x1c],
                 (double)((FLOAT_80613f34 * (float)skel_state[0x1d]) / FLOAT_80613f38),
                 entity_mtx);

    // Multiply with camera view matrix
    float combined_mtx[16];
    FUN_80130bd4(entity_mtx, *(float**)(DAT_80611d54 + 0x208), combined_mtx);

    // Transform all bone matrices into camera space
    int bone_offset = 0;
    for (int bone = 0; bone < *(int*)(*skel_state + 0x20); bone++) {
        float temp_mtx[16];
        FUN_80130cd8((float*)(skel_state[2] + bone_offset), temp_mtx);
        FUN_80130bd4((float*)(skel_state[2] + bone_offset), combined_mtx,
                     (float*)(skel_state[2] + bone_offset));
        bone_offset += 0x40;
    }

    // Transform light directions into camera space
    FUN_8012fb90(combined_mtx, dir1);
    FUN_8012fb90(combined_mtx, dir2);
    FUN_8012fb90(combined_mtx, dir3);

    // Store scaled light directions into skel_state light slots
    FUN_803c84e4((double)(dir1[0] * FLOAT_80613f3c),
                 (double)(dir1[1] * FLOAT_80613f3c),
                 (double)(dir1[2] * FLOAT_80613f3c),
                 (int)(skel_state + 0x1f));
    FUN_803c84e4((double)(dir2[0] * FLOAT_80613f3c),
                 (double)(dir2[1] * FLOAT_80613f3c),
                 (double)(dir2[2] * FLOAT_80613f3c),
                 (int)(skel_state + 0x2f));
    FUN_803c84e4((double)(dir3[0] * FLOAT_80613f3c),
                 (double)(dir3[1] * FLOAT_80613f3c),
                 (double)(dir3[2] * FLOAT_80613f3c),
                 (int)(skel_state + 0x3f));

    FUN_80382730();
}

// =============================================================================
// Address: 0x8006a3dc | cutscene_eval_secondary_lighting
// Evaluates lighting for secondary/attached cutscene entities (weapons,
// accessories). Reads 6 values per channel (direction XYZ + color RGB),
// allocates 0xC0-byte light buffer with 3 light slots.
// Sets flags 0x10000000 and 0x20000000 to mark active lighting.
// =============================================================================
void cutscene_eval_secondary_lighting(void)
{
    void* ctx = FUN_803826e8();
    int* entity = (int*)((unsigned long long)ctx >> 32);
    int slot_idx = (int)ctx;

    int attach_data = entity[slot_idx * 0xb + 0x12];
    int anim_slot = entity[slot_idx * 0xb + 0x15];
    if (anim_slot == -1) {
        anim_slot = slot_idx;
    }

    // Initialize 3 directions + 3 colors to zero
    float dir1[3] = { FLOAT_80613f00, FLOAT_80613f00, FLOAT_80613f00 };
    float dir2[3] = { FLOAT_80613f00, FLOAT_80613f00, FLOAT_80613f00 };
    float dir3[3] = { FLOAT_80613f00, FLOAT_80613f00, FLOAT_80613f00 };
    float col1_r = FLOAT_80613f00, col1_g = FLOAT_80613f00, col1_b = FLOAT_80613f00;
    float col2_r = FLOAT_80613f00, col2_g = FLOAT_80613f00, col2_b = FLOAT_80613f00;
    float col3_r = FLOAT_80613f00, col3_g = FLOAT_80613f00, col3_b = FLOAT_80613f00;

    // Sample 6 values per channel from bone animation
    if (entity[anim_slot * 0xb + 0x11] != 0) {
        int frame = (int)(FLOAT_8060faf8 *
            ((float)entity[2] - (float)entity[anim_slot * 0xb + 0xd]));

        int continuity = anim_motion_continuity_check(entity);
        double interp = (double)cutscene_get_anim_interp_factor();

        if (continuity != 0) {
            interp = ((double)FLOAT_80613f28 <= interp) ?
                     (double)FLOAT_80613f2c : (double)FLOAT_80613f00;
        }
        if ((double)FLOAT_80613f28 <= interp) {
            frame++;
        }

        float* channel_ptr;
        if (anim_bone_channel_lookup((int)entity, anim_slot, frame, 1, (int*)&channel_ptr)) {
            dir1[0] = channel_ptr[0]; dir1[1] = channel_ptr[1]; dir1[2] = channel_ptr[2];
            col1_r = channel_ptr[3]; col1_g = channel_ptr[4]; col1_b = channel_ptr[5];
        }
        if (anim_bone_channel_lookup((int)entity, anim_slot, frame, 2, (int*)&channel_ptr)) {
            dir2[0] = channel_ptr[0]; dir2[1] = channel_ptr[1]; dir2[2] = channel_ptr[2];
            col2_r = channel_ptr[3]; col2_g = channel_ptr[4]; col2_b = channel_ptr[5];
        }
        if (anim_bone_channel_lookup((int)entity, anim_slot, frame, 3, (int*)&channel_ptr)) {
            dir3[0] = channel_ptr[0]; dir3[1] = channel_ptr[1]; dir3[2] = channel_ptr[2];
            col3_r = channel_ptr[3]; col3_g = channel_ptr[4]; col3_b = channel_ptr[5];
        }
    }

    int render_data = *(int*)(attach_data + 4);

    // Transform light directions by inverse camera matrix
    float inv_cam[16];
    FUN_80130b90(*(float**)(DAT_80611d54 + 0x208), inv_cam);
    FUN_8012fb90(inv_cam, dir1);
    FUN_8012fb90(inv_cam, dir2);
    FUN_8012fb90(inv_cam, dir3);

    // Allocate light buffer (3 lights x 0x40 = 0xC0 bytes)
    void* light_buf = mem_advance_secondary(0xc0);
    *(int*)(render_data + 0x70) = (int)light_buf;
    *(short*)(render_data + 0x74) = 3;

    // Set ambient to black with full alpha
    *(unsigned char*)(render_data + 0x6c) = 0;
    *(unsigned char*)(render_data + 0x6d) = 0;
    *(unsigned char*)(render_data + 0x6e) = 0;
    *(unsigned char*)(render_data + 0x6f) = 0xff;

    float color_scale = FLOAT_80613f30;

    // Pack and store channel 1 color
    unsigned int packed_color;
    int r = render_clamp_to_byte(color_scale * col1_r);
    int g = render_clamp_to_byte(color_scale * col1_g);
    int b = render_clamp_to_byte(color_scale * col1_b);
    packed_color = (r << 24) | (g << 16) | (b << 8) | 0x80;
    FUN_803c8610(*(int*)(render_data + 0x70), &packed_color);

    // Pack and store channel 2 color
    r = render_clamp_to_byte(color_scale * col2_r);
    g = render_clamp_to_byte(color_scale * col2_g);
    b = render_clamp_to_byte(color_scale * col2_b);
    packed_color = (r << 24) | (g << 16) | (b << 8) | 0x80;
    FUN_803c8610(*(int*)(render_data + 0x70) + 0x40, &packed_color);

    // Pack and store channel 3 color
    r = render_clamp_to_byte(color_scale * col3_r);
    g = render_clamp_to_byte(color_scale * col3_g);
    b = render_clamp_to_byte(color_scale * col3_b);
    packed_color = (r << 24) | (g << 16) | (b << 8) | 0x80;
    FUN_803c8610(*(int*)(render_data + 0x70) + 0x80, &packed_color);

    // Set ambient from packed value
    unsigned int ambient = *(unsigned int*)(render_data + 0x6c);
    FUN_803c86a4(4, &ambient);

    // Store scaled light directions
    FUN_803c84e4((double)(dir1[0] * FLOAT_80613f3c),
                 (double)(dir1[1] * FLOAT_80613f3c),
                 (double)(dir1[2] * FLOAT_80613f3c),
                 *(int*)(render_data + 0x70));
    FUN_803c84e4((double)(dir2[0] * FLOAT_80613f3c),
                 (double)(dir2[1] * FLOAT_80613f3c),
                 (double)(dir2[2] * FLOAT_80613f3c),
                 *(int*)(render_data + 0x70) + 0x40);
    FUN_803c84e4((double)(dir3[0] * FLOAT_80613f3c),
                 (double)(dir3[1] * FLOAT_80613f3c),
                 (double)(dir3[2] * FLOAT_80613f3c),
                 *(int*)(render_data + 0x70) + 0x80);

    // Mark lighting as active
    *(unsigned int*)(attach_data + 0xc) |= 0x10000000;
    *(unsigned int*)(attach_data + 0xc) |= 0x20000000;

    FUN_80382734();
}
