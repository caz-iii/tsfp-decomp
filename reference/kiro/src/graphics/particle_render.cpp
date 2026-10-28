// Address: 0x800e5f1c - 0x800e66a0 | Particle Rendering System | Date: 2026-08-25
// Handles oriented sprite rendering, line pair rendering, emitter dispatch, and soft overlay effects.

#include "particle.h"
#include "render.h"
#include "math_util.h"
#include "sprite.h"

// ============================================================================
// Address: 0x800e5f1c | Name: particle_render_oriented_sprite
// Renders particle type 0/4: oriented sprites with angular rotation and distance-based fade.
// Each particle is rendered as a camera-facing quad with per-particle rotation computed
// from either world position, emitter-relative angle, or screen-space projection.
// Uses particle_compute_color for RGBA and particle_keyframe_lerp for size interpolation.
// ============================================================================
void particle_render_oriented_sprite(void) {
    int emitter;
    int iVar4;
    uint uVar5;
    int iVar6;
    float* particle;
    int iVar7;
    uint* puVar8;
    double dVar9, dVar10;
    float local_58, local_54, local_50;
    float fVar1, fVar2, fVar3;

    // Begin GX draw state (paired-single register save)
    uint64_t gx_state = gx_particle_begin_oriented();
    emitter = (int)(gx_state >> 32);

    puVar8 = *(uint**)(*(int*)(emitter + 0xc) + 0x4c);

    // Compute aspect ratio correction for particle size
    float aspect_x = (float)((double)g_screen_half_width_neg / (double)g_screen_aspect_x);
    float aspect_y = (float)((double)g_screen_half_height_neg / (double)g_screen_aspect_y);
    float aspect_ratio = aspect_x / aspect_y;

    // Setup render batch with scaled particle size
    iVar4 = particle_batch_setup_oriented(
        (double)(*(float*)(puVar8 + 0x1c) * aspect_ratio),
        (double)(float)puVar8[0x1d],
        (double)PARTICLE_DEFAULT_SIZE,
        (double)PARTICLE_DEFAULT_SIZE,
        0xffffffff,
        (int)(uint32_t)gx_state
    );

    if (iVar4 != 0) {
        iVar4 = *(int*)(emitter + 0x48); // ring buffer read index
        for (iVar7 = 0; iVar7 < *(int*)(emitter + 0x4c); iVar7++) {
            particle = (float*)(*(int*)(emitter + 0x40) + iVar4 * 0x34);

            if (particle[10] <= particle[9]) { // age <= lifetime
                double time = (double)(particle[10] * (1.0f / particle[9])); // normalized age
                uVar5 = particle_compute_color(emitter, (int)particle);

                // Interpolate size from keyframes
                dVar9 = particle_keyframe_lerp(time, (float*)(puVar8 + 0xe), puVar8[0xd]);

                // Compute rotation angle from start/end rotation + age
                double rot_start = (double)(particle[0xc] * (float)puVar8[0x19] + (float)puVar8[0x18]);
                double rot_end = (double)(particle[0xc] * (float)puVar8[0x1b] + (float)puVar8[0x1a]);
                dVar10 = (double)(*(float*)(emitter + 0x68) *
                    (float)(time * (double)(float)((double)rot_end - rot_start) + rot_start));

                iVar6 = gx_compute_alpha_scale((double)(float)((double)PARTICLE_HALF_SCALE * dVar9));

                // Determine particle rotation angle based on orientation mode
                double angle;
                if ((*puVar8 & 2) != 0) {
                    // Mode 2: screen-space projected angle from world offset
                    local_58 = particle[3];
                    local_54 = PARTICLE_Y_OFFSET + particle[4];
                    if (local_54 < 0.0f) {
                        local_54 = 0.0f;
                    }
                    local_50 = particle[5];
                    mat_transform_point(*(float**)(g_camera_data + 0x208), &local_58);
                    angle = (double)(PARTICLE_ROT_BIAS_2 + PARTICLE_ROT_SCALE * particle[7] +
                        (float)atan2f_fast((double)local_58, (double)local_54));
                } else if ((*puVar8 & 4) != 0) {
                    // Mode 4: emitter-relative projected angle
                    fVar1 = particle[1] - *(float*)(emitter + 0x18);
                    fVar2 = *particle - *(float*)(emitter + 0x14);
                    fVar3 = particle[2] - *(float*)(emitter + 0x1c);
                    float cam_x = fVar3 * *(float*)(g_camera_data + 0x36c) +
                                  fVar2 * *(float*)(g_camera_data + 0x364) +
                                  fVar1 * *(float*)(g_camera_data + 0x368);
                    float cam_z = fVar3 * *(float*)(g_camera_data + 0x378) +
                                  fVar2 * *(float*)(g_camera_data + 0x370) +
                                  fVar1 * *(float*)(g_camera_data + 0x374);
                    angle = (double)(PARTICLE_ROT_BIAS_1 + PARTICLE_ROT_SCALE * particle[7] +
                        (float)atan2f_fast((double)cam_x, (double)cam_z));
                } else {
                    // Mode 0: simple linear rotation from velocity
                    angle = (double)(PARTICLE_ROT_BIAS_0 + particle[8] * particle[10] + particle[7]);
                }

                // Wrap angle to [-PI, PI]
                angle = (double)fmodf_fast(angle, PARTICLE_TWO_PI);
                if ((double)PARTICLE_PI <= angle) {
                    angle = (double)(float)(angle - (double)PARTICLE_ROT_SCALE);
                } else if (angle < (double)PARTICLE_NEG_PI) {
                    angle = (double)(float)(angle + (double)PARTICLE_ROT_SCALE);
                }

                // Submit oriented particle vertex
                particle_submit_oriented_vertex(
                    (double)*particle, (double)particle[1], (double)particle[2],
                    (double)(float)((double)0.0f * dVar10),
                    angle,
                    (double)(particle[10] * (float)puVar8[7]),
                    (float)(iVar6 << 24 | (uVar5 & 0xff00) << 8 | uVar5 >> 24 | (uVar5 >> 8) & 0xff00)
                );
            }

            iVar4++;
            if (*(int*)(emitter + 0x44) <= iVar4) {
                iVar4 -= *(int*)(emitter + 0x44); // ring buffer wrap
            }
        }
        particle_batch_end();
    }
    gx_particle_end_oriented();
}

// ============================================================================
// Address: 0x800e6240 | Name: particle_render_line_pairs
// Renders particle type 2: connected line pairs (trails/beams).
// Processes particles in pairs (start/end), applying color and size interpolation.
// ============================================================================
void particle_render_line_pairs(void) {
    int emitter;
    int iVar1;
    float* pfVar2;
    float* particle;
    int iVar3;
    int iVar4;
    double dVar5, dVar6;

    uint64_t gx_state = gx_particle_begin_lines();
    emitter = (int)(gx_state >> 32);
    iVar4 = *(int*)(*(int*)(emitter + 0xc) + 0x4c); // particle def data

    // Compute aspect-corrected line width
    float aspect_x = (float)((double)g_screen_half_width_neg / (double)g_screen_aspect_x);
    float aspect_y = (float)((double)g_screen_half_height_neg / (double)g_screen_aspect_y);

    iVar1 = particle_batch_setup_lines(
        (double)(*(float*)(iVar4 + 0x70) * (aspect_x / aspect_y)),
        0xffffffff,
        (int)(uint32_t)gx_state
    );

    if (iVar1 != 0) {
        iVar1 = *(int*)(emitter + 0x48); // ring buffer read index
        // Process pairs: particle count / 2
        for (iVar3 = 0; iVar3 < *(int*)(emitter + 0x4c) >> 1; iVar3++) {
            particle = (float*)(*(int*)(emitter + 0x40) + iVar1 * 0x34);
            float age = particle[10];
            float lifetime = particle[9];
            pfVar2 = (float*)(*(int*)(emitter + 0x40) + (iVar1 + 1) * 0x34); // paired end point

            if (age <= lifetime) {
                particle_compute_color(emitter, (int)particle);
                double time = (double)(age * (1.0f / lifetime));
                // Interpolate line width from keyframes
                dVar5 = particle_keyframe_lerp(time, (float*)(iVar4 + 0x38), *(int*)(iVar4 + 0x34));
                gx_compute_alpha_scale((double)(float)((double)PARTICLE_HALF_SCALE * dVar5));

                // Submit line segment from particle to paired endpoint
                particle_submit_line_segment(
                    (double)*particle, (double)particle[1], (double)particle[2],
                    (double)*pfVar2, (double)pfVar2[1], (double)pfVar2[2]
                );
            }

            iVar1 += 2; // advance by pair
            if (*(int*)(emitter + 0x44) <= iVar1) {
                iVar1 -= *(int*)(emitter + 0x44);
            }
        }
        particle_batch_end();
    }
    gx_particle_end_lines();
}

// ============================================================================
// Address: 0x800e63ec | Name: particle_emitter_render_dispatch
// Per-emitter render dispatcher. Selects render path based on particle type:
//   type 0,4 -> oriented sprite
//   type 2   -> line pairs
//   type 3   -> billboard quads
//   type 5   -> rotated billboards
//   type 6   -> triangle fans
// Also renders soft particle overlay (lens flare/glow) if defined.
// ============================================================================
void particle_emitter_render_dispatch(int emitter) {
    uint uVar1;
    int iVar2;
    uint* puVar3;

    // Skip if: parent entity hidden, OR parent entity flagged invisible,
    // OR spectator mode doesn't match current player
    if (((*(int*)(emitter + 0x88) == 0) ||
         ((*(uint*)(*(int*)(emitter + 0x88) + 0x28) & 1) != 0)) ||
        ((*(uint*)(emitter + 4) & 0x10000000) != 0)) {
        // Also check spectator visibility
        if (((*(uint*)(emitter + 8) & 8) == 0 || (g_camera_data == 0)) ||
            (iVar2 = spectator_get_slot(), g_camera_data != iVar2)) {

            if (0 < *(int*)(emitter + 0x4c)) { // has active particles
                puVar3 = *(uint**)(*(int*)(emitter + 0xc) + 0x4c);
                uVar1 = puVar3[5]; // render type

                // Set blend mode based on type value
                if ((uVar1 == 1) || (0 < (int)uVar1) || ((int)uVar1 < 0)) {
                    render_set_blend_mode(2); // additive
                } else {
                    render_set_blend_mode(1); // alpha blend
                }

                // Bind texture
                if ((*puVar3 & 1) == 0) {
                    // Standard texture from material
                    if (*(int*)(emitter + 0x88) != 0) {
                        render_prop_get_value_at_index(
                            puVar3[4],
                            *(int*)(*(int*)(*(int*)(*(int*)(emitter + 0x88) + 4) + 4) + 0x14)
                        );
                    }
                } else {
                    // Sprite texture
                    sprite_get_texture_id(puVar3[4]);
                }

                // Dispatch by particle render type
                switch (puVar3[3]) {
                    case 0:
                    case 4:
                        particle_render_oriented_sprite();
                        break;
                    case 2:
                        particle_render_line_pairs();
                        break;
                    case 3:
                        particle_render_billboard_quads();
                        break;
                    case 5:
                        particle_render_billboard_rotated();
                        break;
                    case 6:
                        particle_render_triangle_fan();
                        break;
                }
            }

            // Render soft overlay (lens flare/glow) if defined
            if (*(int*)(*(int*)(emitter + 0xc) + 0x50) != 0) {
                particle_render_soft_overlay();
            }

            // Mark this emitter as rendered for current player
            *(uint*)(emitter + 0x80) |= 1 << *(int*)(g_camera_data + 0x214);
        }
    }
}

// ============================================================================
// Address: 0x800e659c | Name: particle_system_render_all
// Main entry point: iterates all active particle emitters and renders them.
// Handles mirror matrix switching for reflective surfaces.
// Called from particle_system_render_dispatch.
// ============================================================================
void particle_system_render_all(void) {
    bool mirror_active = false;
    int emitter = g_particle_emitter_list_head;

    // Set default camera matrix
    mat_load_camera(*(float**)(g_camera_data + 0x208), (float*)&g_identity_matrix);
    g_particle_render_pass = 1;
    g_particle_render_count = 0;

    for (; emitter != 0; emitter = *(int*)(emitter + 0xf4)) {
        render_state_push(2);

        // Check if emitter was already rendered this frame for this player
        if ((*(uint*)(emitter + 0x7c) & (1 << *(int*)(g_camera_data + 0x214))) != 0) {
            // Check mirror flag on emitter
            if ((*(uint*)(emitter + 4) & 0x10) == 0) {
                // Non-mirror: restore default matrix if we were in mirror mode
                if (mirror_active) {
                    mat_load_camera(*(float**)(g_camera_data + 0x208), (float*)&g_identity_matrix);
                    g_particle_render_pass = 1;
                    mirror_active = false;
                    g_particle_render_count = 0;
                }
            } else if (!mirror_active) {
                // Mirror: load mirror camera matrix
                mat_load_camera((float*)&g_mirror_view_matrix, (float*)&g_identity_matrix);
                g_particle_render_pass = 1;
                mirror_active = true;
                g_particle_render_count = 0;
            }

            particle_emitter_render_dispatch(emitter);
        }
    }
}

// ============================================================================
// Address: 0x800e669c | Name: particle_render_stub_nop
// Empty stub / no-op. Likely a removed or disabled render path.
// ============================================================================
void particle_render_stub_nop(void) {
    return;
}

// ============================================================================
// Address: 0x800e66a0 | Name: particle_render_soft_overlay
// Renders a soft particle overlay effect (lens flare / glow / light shaft).
// Performs occlusion query via screen-space diamond test, then renders a
// screen-aligned textured quad with distance-based fade and color accumulation
// into the per-player emitter color buffer.
// ============================================================================
void particle_render_soft_overlay(void) {
    float fVar1;
    int iVar4;
    uint uVar5;
    uint uVar7;
    int iVar8;
    float* pfVar9;
    float fVar10;
    float local_108, local_104, local_100, local_fc;
    float local_f8, local_f4, local_f0;

    uint64_t gx_state = gx_particle_begin_overlay();
    iVar8 = (int)(gx_state >> 32);
    pfVar9 = (float*)(uint32_t)gx_state; // overlay def pointer

    // Check if emitter age is within active range and within fade distance
    if ((*(float*)(iVar8 + 100) <= *(float*)(iVar8 + 0x38)) &&
        ((fVar1 = *(float*)(iVar8 + 0x38) - *(float*)(iVar8 + 100),
          fVar1 < pfVar9[3] || fVar1 < pfVar9[0xb]) ||
         ((*(uint*)(iVar8 + 4) & 0x400000) != 0))) {

        // Compute world position of overlay (emitter pos + overlay offset)
        local_f8 = *(float*)(iVar8 + 0x14) + *pfVar9;
        local_f4 = *(float*)(iVar8 + 0x18) + pfVar9[1];
        local_f0 = *(float*)(iVar8 + 0x1c) + pfVar9[2];

        // Transform to view space
        mat_world_to_view(*(float**)(g_camera_data + 0x20c), &local_f8, &local_108);
        float depth = local_fc;
        fVar1 = (float)((double)PARTICLE_FRUSTUM_SCALE * (double)depth);

        // Frustum check: within screen bounds and in front of camera
        if ((-fVar1 <= local_108 && local_108 <= fVar1) &&
            (-fVar1 <= local_104 && local_104 <= fVar1) &&
            (-(double)depth <= (double)local_100 && (double)local_100 <= (double)depth) &&
            ((double)PARTICLE_MIN_DEPTH < (double)depth)) {

            float inv_depth = 1.0f / depth;
            float screen_ndc_limit = (depth < 1.0f) ? (1.0f * depth) : 1.0f;

            // Project to NDC
            local_108 *= inv_depth;
            local_104 *= inv_depth;
            local_100 *= inv_depth;

            // Convert NDC to screen coords using viewport transform
            float vp_half_w = (float)((double)g_viewport_half_width_neg - DOUBLE_806150a8);
            float vp_offset_x = (float)((double)g_viewport_center_x - DOUBLE_806150a8);
            local_f8 = (float)((double)(g_viewport_center_y_neg) - DOUBLE_806150a8) * local_108 + vp_offset_x;

            float vp_half_h_neg = (float)(-(float)g_viewport_offset_y ^ 0x80000000);
            float vp_offset_y = (float)((double)g_viewport_center_y_pos - DOUBLE_806150a8);
            local_f4 = vp_half_h_neg * local_104 + vp_offset_y;
            local_f0 = local_100;

            // Occlusion query: draw diamond shape and test visibility
            iVar4 = particle_occlusion_begin(*(int*)(g_camera_data + 0x214), pfVar9, 0);
            if (iVar4 != 0) {
                // Submit 5-vertex diamond (center + 4 corners)
                particle_occlusion_vertex((double)local_f8, (double)local_f4, (double)local_f0);
                particle_occlusion_vertex(
                    (double)(local_f8 - PARTICLE_DIAMOND_OFFSET),
                    (double)(local_f4 - PARTICLE_DIAMOND_OFFSET),
                    (double)local_f0);
                particle_occlusion_vertex(
                    (double)(local_f8 - PARTICLE_DIAMOND_OFFSET),
                    (double)(local_f4 + PARTICLE_DIAMOND_OFFSET),
                    (double)local_f0);
                particle_occlusion_vertex(
                    (double)(local_f8 + PARTICLE_DIAMOND_OFFSET),
                    (double)(local_f4 + PARTICLE_DIAMOND_OFFSET),
                    (double)local_f0);
                particle_occlusion_vertex(
                    (double)(local_f8 + PARTICLE_DIAMOND_OFFSET),
                    (double)(local_f4 - PARTICLE_DIAMOND_OFFSET),
                    (double)local_f0);
                particle_occlusion_end();

                // Read occlusion result (visible pixel count)
                uVar5 = particle_occlusion_read_result(*(int*)(g_camera_data + 0x214), (int)pfVar9, 0);
                *(char*)(iVar8 + *(int*)(g_camera_data + 0x214) + 0x90) = (char)uVar5;

                // Glow/soft overlay rendering if visible
                if (0 < (int)uVar5) {
                    // Far glow: accumulate color into per-player emitter buffer
                    if ((*(float*)(iVar8 + 0x38) < pfVar9[0xb]) ||
                        ((*(uint*)(iVar8 + 4) & 0x400000) != 0)) {
                        float fade = PARTICLE_MIN_DEPTH;
                        if ((*(uint*)(iVar8 + 4) & 0x400000) == 0) {
                            fade = *(float*)(iVar8 + 0x38) / pfVar9[0xb];
                        }
                        double inv_fade = math_smooth_step((double)fade);
                        float depth_fade = local_100 - PARTICLE_DEPTH_BIAS;
                        float glow_color_r = pfVar9[0xc];
                        double strength = (double)(float)((double)1.0f - inv_fade);

                        if (depth_fade < PARTICLE_MIN_DEPTH) depth_fade = PARTICLE_MIN_DEPTH;
                        double depth_atten = -(double)(PARTICLE_DEPTH_SCALE * depth_fade - 1.0f);

                        // Extract RGBA channels and accumulate
                        uint color_a = pfVar9[0xd];
                        double color_scale = (double)(float)(strength *
                            (double)(float)(depth_atten * (double)(float)((double)(color_a & 0xff) - DOUBLE_806150c8)));

                        uint* player_emitter = particle_get_current_player_emitter();
                        player_emitter[7] += ((uint)glow_color_r >> 24);       // R
                        player_emitter = particle_get_current_player_emitter();
                        player_emitter[8] += ((uint)pfVar9[0xc] >> 16 & 0xff); // G
                        player_emitter = particle_get_current_player_emitter();
                        player_emitter[9] += ((uint)pfVar9[0xc] >> 8 & 0xff);  // B

                        // Accumulate weighted color channels
                        player_emitter = particle_get_current_player_emitter();
                        int accum_r = (int)((float)player_emitter[10] + color_scale);
                        player_emitter[10] = accum_r;

                        player_emitter = particle_get_current_player_emitter();
                        int accum_g = (int)((double)(float)((double)(color_a >> 24) - DOUBLE_806150c8) *
                            color_scale + (double)(float)player_emitter[11]);
                        player_emitter[11] = accum_g;

                        player_emitter = particle_get_current_player_emitter();
                        int accum_b = (int)((double)(float)((double)((color_a >> 16) & 0xff) - DOUBLE_806150c8) *
                            color_scale + (double)(float)player_emitter[12]);
                        player_emitter[12] = accum_b;

                        player_emitter = particle_get_current_player_emitter();
                        int accum_a = (int)((double)(float)((double)((color_a >> 8) & 0xff) - DOUBLE_806150c8) *
                            color_scale + (double)(float)player_emitter[13]);
                        player_emitter[13] = accum_a;
                    }
                }

                // Near glow: render screen-space textured quad
                if (0 < (int)uVar5) {
                    if ((*(float*)(iVar8 + 0x38) < pfVar9[3]) ||
                        ((*(uint*)(iVar8 + 4) & 0x400000) != 0)) {
                        float near_fade = PARTICLE_MIN_DEPTH;
                        if ((*(uint*)(iVar8 + 4) & 0x400000) == 0) {
                            near_fade = *(float*)(iVar8 + 0x38) / pfVar9[3];
                        }

                        // Compute quad size from interpolated width/height
                        double quad_w = (double)(near_fade * (pfVar9[8] - pfVar9[7]) + pfVar9[7]);
                        double quad_h = (double)((near_fade * (pfVar9[10] - pfVar9[9]) + pfVar9[9]) *
                            (float)(quad_w * (double)(aspect_ratio)));

                        // Blend overlay color based on fade
                        uVar7 = color_lerp((double)(1.0f - near_fade), (uint)pfVar9[5], (uint)pfVar9[6]);
                        // Apply alpha from occlusion result
                        float alpha_scale = (float)((float)((float)uVar5 / PARTICLE_HALF_SCALE) * screen_ndc_limit);
                        color_apply_alpha((double)alpha_scale, uVar7);

                        // Get texture and render quad
                        int tex_id = sprite_get_texture_id((int)pfVar9[4]);
                        float sx = local_f8;
                        float sy = local_f4;
                        int tex_info = g_texture_table + tex_id * 0x58;
                        uint16_t tex_w = *(uint16_t*)(tex_info + 0x4e);
                        uint16_t tex_h = *(uint16_t*)(tex_info + 0x52);

                        render_set_blend_mode(2); // additive
                        render_bind_texture(tex_id);
                        render_draw_screen_quad(
                            (double)(sx - (float)quad_w),
                            (double)(sy - (float)quad_h),
                            (double)(sx + (float)quad_w),
                            (double)(sy + (float)quad_h),
                            0, 0, (uint)tex_w << 4, (uint)tex_h << 4
                        );
                    }
                }
            }
        }
    }
    gx_particle_end_overlay();
}
