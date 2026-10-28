// Address: 0x800d5100 - 0x800d5ce0 | Name: particle_spawn_variants | Date: 2026-08-11
// Particle spawn variant functions: different creation modes for various effect
// types (simple, explosion, column, beam, animated, timed), plus velocity tick
// and sub-count setter. All use particle_slot_alloc from particle_slot_ops.cpp.

#include "common.h"
#include "memory.h"
#include "scene.h"

// ============================================================================
// particle_spawn_simple
// Address: 0x800d5100
// Simple particle spawner: allocs slot, populates RNG positions from source,
// no AABB scene query (unlike particle_slot_spawn). 1 caller.
// ============================================================================
void particle_spawn_simple(void) {
    unsigned long long packed = FUN_803826e4();
    float* source = (float*)((packed >> 32));
    float* params = (float*)packed;

    int* slot = particle_slot_alloc();
    int* ext_data = *(int**)(slot + 0x18);

    *(int*)((char*)slot + 0x14) = 0; // sub-type = 0
    int player_count = FUN_801c1714();
    *(unsigned int*)((char*)slot + 8) = (~(unsigned int)(-(player_count == 1))) & 0x80000;

    *(float*)((char*)slot + 0xc) = 1.0f;           // opacity
    *(float*)((char*)slot + 0x10) = FLOAT_80614edc; // spread
    *(int*)((char*)slot + 4) = -1;                  // sub-type sentinel
    *(int*)((char*)slot + 0x1c) = (int)(FLOAT_80614ef4 * g_particle_intensity_max);

    // Copy origin
    ext_data[0] = *(int*)source;
    ext_data[1] = *(int*)(source + 1);
    ext_data[2] = *(int*)(source + 2);

    // Generate randomized positions
    float rng_scale = FLOAT_80614f08;
    float rng_offset = FLOAT_80614efc;
    float height_offset = FLOAT_80614ee4;
    int byte_offset = 0;

    for (int i = 0; i < *(int*)((char*)slot + 0x1c); i++) {
        float rx = FUN_801310c0();
        *(float*)((char*)ext_data + byte_offset + 0xc) = rng_scale * (params[0] + rx) - rng_offset;
        float ry = FUN_801310c0();
        *(float*)((char*)ext_data + byte_offset + 0x10) = rng_scale * (params[1] + ry) - height_offset;
        float rz = FUN_801310c0();
        *(float*)((char*)ext_data + byte_offset + 0x14) = rng_scale * (params[2] + rz) - rng_offset;
        byte_offset += 0xc;
    }

    FUN_80382730();
}

// ============================================================================
// particle_spawn_explosion
// Address: 0x800d5258
// MASSIVE dual-phase explosion particle spawner. First half of particles get
// outward velocity from center; second half get randomized height offset +
// gravity. Uses per-type config table (DAT_80406c0c, stride 0x1c).
// Computes AABB for multi-player scene query.
// 1 caller: player_ai_explosion_effect_dispatch.
// ============================================================================
void particle_spawn_explosion(int param1, int param2, float* source_pos) {
    unsigned long long packed = FUN_803826dc();
    int sub_index = (int)packed;
    int type_sub = (int)(packed >> 32);

    int* slot = particle_slot_alloc();
    int* ext = *(int**)(slot + 0x18);

    *(float*)((char*)slot + 0xc) = 1.0f; // opacity
    int player_count = FUN_801c1714();
    *(unsigned int*)((char*)slot + 8) = (~(unsigned int)(-(player_count == 1))) & 0x80000;

    int cfg_offset = sub_index * 0x1c;
    *(int*)((char*)slot + 0x14) = type_sub;

    // Store source position in slot
    *(float*)((char*)slot + 0x20) = source_pos[0];
    *(float*)((char*)slot + 0x24) = source_pos[1];
    *(float*)((char*)slot + 0x28) = source_pos[2];

    float spread = FLOAT_80614efc;
    *(float*)((char*)slot + 0x10) = spread;
    *(int*)((char*)slot + 4) = -1;
    float intensity_scale = FLOAT_80614f0c;
    *(int*)((char*)slot + 0x1c) = (int)(intensity_scale * g_particle_intensity_max);

    // Config from type table
    ext[0] = sub_index;
    ext[1] = *(int*)(&g_particle_explosion_cfg + cfg_offset); // DAT_80406c0c
    ext[2] = *(int*)(&g_particle_explosion_cfg + cfg_offset + 4); // DAT_80406c10
    float max_radius = (float)ext[2] / FLOAT_80614f10;
    int total = *(int*)((char*)slot + 0x1c);
    int half = total / 2;

    // Phase 1: outward velocity particles (first half)
    int vel_offset = 0;
    int pos_offset = 0;
    for (int i = 0; i < half; i++) {
        unsigned int rng_bit = FUN_8013109c();
        float sign = (rng_bit & 1) ? FLOAT_80614f14 : FLOAT_80614ed8;

        float rng_v = FUN_801310c0();
        *(float*)((char*)ext + vel_offset + 0x2ec) =
            (FLOAT_80614f1c * (FLOAT_80614f18 * (FLOAT_80614ee4 + rng_v) * sign)) / FLOAT_80614eec;

        *(float*)((char*)ext + vel_offset + 0x120) = 1.0f; // alive flag
        *(int*)((char*)ext + vel_offset + 0x17c) = ext[2]; // max lifetime

        // Position: random offset from source
        float rx = FUN_801310c0();
        *(float*)((char*)ext + pos_offset + 0xc) = max_radius * (FLOAT_80614ef0 * (rx - FLOAT_80614ee4)) + source_pos[0];
        float ry = FUN_801310c0();
        *(float*)((char*)ext + pos_offset + 0x10) = max_radius * (FLOAT_80614ef0 * (ry - FLOAT_80614ee4)) + source_pos[1];
        float rz = FUN_801310c0();
        *(float*)((char*)ext + pos_offset + 0x14) = max_radius * (FLOAT_80614ef0 * (rz - FLOAT_80614ee4)) + source_pos[2];

        // Velocity from position delta
        *(float*)((char*)ext + pos_offset + 0x1d8) = FLOAT_80614ee4 * (*(float*)((char*)ext + pos_offset + 0xc) - source_pos[0]);
        float ry2 = FUN_801310c0();
        *(float*)((char*)ext + pos_offset + 0x1dc) = FLOAT_80614ee4 * ry2;
        *(float*)((char*)ext + pos_offset + 0x1e0) = FLOAT_80614ee4 * (*(float*)((char*)ext + pos_offset + 0x14) - source_pos[2]);

        vel_offset += 4;
        pos_offset += 0xc;
    }

    // Phase 2: gravity particles (second half)
    for (int i = half; i < total; i++) {
        unsigned int rng_bit = FUN_8013109c();
        float sign = (rng_bit & 1) ? FLOAT_80614f14 : FLOAT_80614ed8;

        float rng_v = FUN_801310c0();
        *(float*)((char*)ext + vel_offset + 0x2ec) =
            (FLOAT_80614f1c * (FLOAT_80614f18 * (FLOAT_80614ed8 + rng_v) * sign)) / FLOAT_80614eec;

        float height_rng = FUN_801310c0();
        *(float*)((char*)ext + vel_offset + 0x120) = FLOAT_80614f20 * height_rng;
        *(float*)((char*)ext + vel_offset + 0x17c) = (float)ext[2] * FLOAT_80614ed8; // half lifetime

        float rx = FUN_801310c0();
        *(float*)((char*)ext + pos_offset + 0xc) = max_radius * (rx - FLOAT_80614ee4) + source_pos[0];
        float ry = FUN_801310c0();
        float y_offset = FLOAT_80614f24 * *(float*)((char*)ext + vel_offset + 0x120);
        *(float*)((char*)ext + pos_offset + 0x10) = max_radius * (ry - FLOAT_80614ee4) + y_offset + source_pos[1];
        float rz = FUN_801310c0();
        *(float*)((char*)ext + pos_offset + 0x14) = max_radius * (rz - FLOAT_80614ee4) + source_pos[2];

        *(float*)((char*)ext + pos_offset + 0x1d8) = *(float*)((char*)ext + pos_offset + 0xc) - source_pos[0];
        float ry3 = FUN_801310c0();
        *(float*)((char*)ext + pos_offset + 0x1dc) = ry3;
        *(float*)((char*)ext + pos_offset + 0x1e0) = *(float*)((char*)ext + pos_offset + 0x14) - source_pos[2];

        vel_offset += 4;
        pos_offset += 0xc;
    }

    // Multi-player AABB + scene query
    if ((*(unsigned int*)((char*)slot + 8) & 0x80000) != 0) {
        float bounds_offset = FLOAT_80614f2c;
        float bounds_base = FLOAT_80614f28;
        float half_ext = FLOAT_80614ee4;

        *(float*)((char*)slot + 0x80) = (bounds_base + source_pos[0]) - bounds_offset;
        *(float*)((char*)slot + 0x8c) = bounds_offset + half_ext + source_pos[0];
        *(float*)((char*)slot + 0x84) = (bounds_base + source_pos[1]) - bounds_offset;
        *(float*)((char*)slot + 0x90) = bounds_offset + half_ext + source_pos[1];
        *(float*)((char*)slot + 0x88) = (bounds_base + source_pos[2]) - bounds_offset;
        *(float*)((char*)slot + 0x94) = bounds_offset + half_ext + source_pos[2];

        int node_count = scene_query_nodes_in_frustum(0, (float*)((char*)slot + 0x80),
                                                      (int*)((char*)slot + 0x9c), 0xc);
        *(int*)((char*)slot + 0x98) = node_count;
    }

    FUN_80382728();
}

// ============================================================================
// particle_spawn_stub_a
// Address: 0x800d5670
// Empty stub / no-op placeholder.
// ============================================================================
void particle_spawn_stub_a(void) {
    return;
}

// ============================================================================
// particle_spawn_stub_b
// Address: 0x800d5674
// Empty stub / no-op placeholder.
// ============================================================================
void particle_spawn_stub_b(void) {
    return;
}

// ============================================================================
// particle_spawn_column
// Address: 0x800d5678
// Column/vertical spread particle spawner. Particles spread in Y direction
// with random velocity offsets. Computes AABB for multi-player scene query.
// Called from player_ai_impact_dispatch-related functions.
// ============================================================================
void particle_spawn_column(void) {
    unsigned long long packed = FUN_803826e4();
    float* origin = (float*)packed;

    int* slot = particle_slot_alloc();
    float* ext = *(float**)(slot + 0x18);

    int player_count = FUN_801c1714();
    *(unsigned int*)((char*)slot + 8) = (~(unsigned int)(-(player_count == 1))) & 0x80000;

    *(float*)((char*)slot + 0xc) = 1.0f;           // opacity
    *(int*)((char*)slot + 0x14) = (int)(packed >> 32); // sub-type
    *(float*)((char*)slot + 0x10) = FLOAT_80614f30; // spread
    *(int*)((char*)slot + 4) = -1;
    *(int*)((char*)slot + 0x1c) = (int)(FLOAT_80614f34 * g_particle_intensity_max);

    // Copy origin
    ext[0] = origin[0];
    ext[1] = origin[1];
    ext[2] = origin[2];

    // Generate vertical velocity offsets
    int vel_offset = 0;
    for (int i = 0; i < *(int*)((char*)slot + 0x1c); i++) {
        float rng = FUN_801310c0();
        *(float*)((char*)ext + vel_offset + 0x14) = FLOAT_80614eec * (rng - FLOAT_80614ee4);
        vel_offset += 4;
    }

    // Multi-player AABB + scene query
    if ((*(unsigned int*)((char*)slot + 8) & 0x80000) != 0) {
        float bound = FLOAT_80614f2c;
        float offset_y = FLOAT_80614ed8;
        float double_efc = FLOAT_80614efc;

        *(float*)((char*)slot + 0x80) = origin[0] - bound;
        *(float*)((char*)slot + 0x8c) = bound + origin[0];
        *(float*)((char*)slot + 0x84) = origin[1] - offset_y;
        *(float*)((char*)slot + 0x90) = double_efc + double_efc + origin[1];
        *(float*)((char*)slot + 0x88) = origin[2] - bound;
        *(float*)((char*)slot + 0x94) = bound + origin[2];

        int node_count = scene_query_nodes_in_frustum(0, (float*)((char*)slot + 0x80),
                                                      (int*)((char*)slot + 0x9c), 0xc);
        *(int*)((char*)slot + 0x98) = node_count;
    }

    FUN_80382730();
}

// ============================================================================
// particle_spawn_beam
// Address: 0x800d5808
// Beam/trail particle spawner: stores start position, direction vector, and
// speed. No AABB (single-player only flag = 0). Used for bullet trails,
// laser beams, etc. 4 callers.
// ============================================================================
void particle_spawn_beam(int p1, int p2, float* start_pos, float* direction, int player_mask) {
    unsigned long long packed = FUN_803826e8();
    float speed = extraout_f1; // second return value

    int* slot = particle_slot_alloc();
    int* ext = *(int**)(slot + 0x18);

    *(int*)((char*)slot + 8) = 0;                   // no multi-player flag
    *(float*)((char*)slot + 0xc) = 1.0f;           // opacity
    *(int*)((char*)slot + 0x14) = (int)(packed >> 32); // sub-type
    *(float*)((char*)slot + 0x10) = FLOAT_80614edc; // spread
    *(int*)((char*)slot + 4) = -1;
    *(int*)((char*)slot + 0x1c) = 0;               // no sub-particles

    // Store beam data in extended block
    ext[0] = (int)packed;          // beam type
    ext[1] = *(int*)&start_pos[0]; // start X
    ext[2] = *(int*)&start_pos[1]; // start Y
    ext[3] = *(int*)&start_pos[2]; // start Z
    ext[4] = *(int*)&direction[0]; // dir X
    ext[5] = *(int*)&direction[1]; // dir Y
    ext[6] = *(int*)&direction[2]; // dir Z
    *(float*)(ext + 7) = speed;    // speed

    *(int*)((char*)slot + 0x34) = player_mask;
    *(int*)((char*)slot + 0x98) = 0;

    FUN_80382734();
}

// ============================================================================
// particle_slot_set_sub_count
// Address: 0x800d58d4
// Sets the sub-particle count field (offset +0x1c) on a slot. 1 caller.
// ============================================================================
void particle_slot_set_sub_count(int* slot, int count) {
    *(int*)((char*)slot + 0x1c) = count;
}

// ============================================================================
// particle_velocity_tick
// Address: 0x800d58dc
// Per-frame velocity integration for beam/trail particles. Applies directional
// velocity scaled by frame time. If opacity > threshold, applies velocity with
// optional RNG jitter (when player mask is power-of-2). 1 caller.
// ============================================================================
void particle_velocity_tick(int* slot) {
    if (g_particle_enabled == 0) { // DAT_806108e4
        return;
    }

    int* ext = *(int**)((char*)slot + 0x18);
    float frame_dt = FLOAT_80611df8 * (FLOAT_80614ed8 / (float)g_frame_counter); // frame time

    float opacity = *(float*)((char*)slot + 0xc);

    if (opacity > frame_dt) {
        // Normal mode: apply velocity * speed directly
        float speed = *(float*)((char*)ext + 0x1c);
        *(float*)((char*)ext + 4) += frame_dt * (*(float*)((char*)ext + 0x10) * speed);
        *(float*)((char*)ext + 8) += frame_dt * (*(float*)((char*)ext + 0x14) * speed);
        *(float*)((char*)ext + 0xc) += frame_dt * (*(float*)((char*)ext + 0x18) * speed);
    } else {
        // Jitter mode: apply velocity with RNG-based direction randomization
        float rng = FUN_801310c0();
        unsigned int mask = *(unsigned int*)((char*)slot + 0x34);

        // Power-of-2 check: if mask has only one bit set, halve the RNG
        if ((mask & (mask - 1)) == 0) {
            rng *= FLOAT_80614ee4; // 0.5
        }

        float speed = *(float*)((char*)ext + 0x1c);
        *(float*)((char*)ext + 4) += frame_dt * (speed * (*(float*)((char*)ext + 0x10) * rng));
        *(float*)((char*)ext + 8) += frame_dt * (speed * (*(float*)((char*)ext + 0x14) * rng));
        *(float*)((char*)ext + 0xc) += frame_dt * (speed * (*(float*)((char*)ext + 0x18) * rng));
    }
}

// ============================================================================
// particle_spawn_animated
// Address: 0x800d5a24
// MASSIVE config-table-driven animated particle spawner. Reads per-type
// parameters from DAT_804070a4 (stride 0x90) including velocity curves, bounds,
// and lifetime. Computes 6-axis AABB bounds from config for visibility culling.
// 20+ callers via particle_slot_alloc chain.
// ============================================================================
void particle_spawn_animated(int p1, int p2, int flags, float* position) {
    unsigned long long packed = FUN_803826e8();
    int sub_type = (int)packed;
    int type_sub = (int)(packed >> 32);
    int cfg_offset = sub_type * 0x90;

    int* slot = particle_slot_alloc();

    *(int*)((char*)slot + 8) = flags;
    *(int*)((char*)slot + 4) = sub_type;
    *(float*)((char*)slot + 0xc) = 1.0f;
    *(int*)((char*)slot + 0x14) = type_sub;

    // Fade time = base + extra from config table
    *(float*)((char*)slot + 0x10) =
        *(float*)(&g_particle_anim_cfg + cfg_offset + 8) +  // DAT_804070ac
        *(float*)(&g_particle_anim_cfg + cfg_offset + 0x10); // DAT_804070b4

    *(int*)((char*)slot + 0x1c) = 0;
    *(float*)((char*)slot + 0x20) = position[0];
    *(float*)((char*)slot + 0x24) = position[1];
    *(float*)((char*)slot + 0x28) = position[2];
    *(int*)((char*)slot + 0x98) = 0;

    // Compute AABB bounds from config velocity/acceleration tables
    if ((*(unsigned int*)((char*)slot + 8) & 0x80000) != 0) {
        float max_t = *(float*)(&g_particle_anim_cfg + cfg_offset + 8); // DAT_804070ac
        float inv_accel = FLOAT_80614ed8 / (FLOAT_80614f40 * *(float*)(&g_particle_anim_cfg + cfg_offset + 0x3c)); // DAT_804070e0

        // Read velocity components from config
        float vel_x_lo = FLOAT_80614f28 * *(float*)(&g_particle_anim_cfg + cfg_offset + 0x50) + *(float*)(&g_particle_anim_cfg + cfg_offset + 0x44);
        float vel_x_hi = FLOAT_80614ee4 * *(float*)(&g_particle_anim_cfg + cfg_offset + 0x50) + *(float*)(&g_particle_anim_cfg + cfg_offset + 0x44);
        float vel_y_lo = FLOAT_80614f28 * *(float*)(&g_particle_anim_cfg + cfg_offset + 0x4c) + *(float*)(&g_particle_anim_cfg + cfg_offset + 0x40);
        float vel_y_hi = FLOAT_80614ee4 * *(float*)(&g_particle_anim_cfg + cfg_offset + 0x4c) + *(float*)(&g_particle_anim_cfg + cfg_offset + 0x40);
        float vel_z_lo = FLOAT_80614f28 * *(float*)(&g_particle_anim_cfg + cfg_offset + 0x54) + *(float*)(&g_particle_anim_cfg + cfg_offset + 0x48);
        float vel_z_hi = FLOAT_80614ee4 * *(float*)(&g_particle_anim_cfg + cfg_offset + 0x54) + *(float*)(&g_particle_anim_cfg + cfg_offset + 0x48);

        // Quadratic bounds: v*t + 0.5*a*t^2
        float accel_sq = max_t * FLOAT_80614ee4 * *(float*)(&g_particle_anim_cfg + cfg_offset + 0x3c) * max_t;
        float pos_x_lo = vel_x_lo * max_t + accel_sq;
        float pos_x_hi = vel_x_hi * max_t + accel_sq;
        float bound_x_lo = vel_x_lo * vel_x_lo * inv_accel;
        float bound_x_hi = vel_x_hi * vel_x_hi * inv_accel;

        // X bounds
        float min_y = (vel_y_lo < vel_y_hi) ? vel_y_lo : vel_y_hi;
        *(float*)((char*)slot + 0x68) = ((max_t * min_y <= 1.0f) ? (max_t * min_y) : 1.0f) - FLOAT_80614ed8;
        float max_y = (vel_y_lo > vel_y_hi) ? vel_y_lo : vel_y_hi;
        *(float*)((char*)slot + 0x74) = FLOAT_80614ed8 + ((max_t * max_y >= 1.0f) ? max_t * max_y : 1.0f);

        // Y bounds (from quadratic)
        float min_q = (pos_x_lo < pos_x_hi) ? pos_x_hi : pos_x_lo;
        *(float*)((char*)slot + 0x6c) = min_q - FLOAT_80614ed8;
        float max_q = (bound_x_lo > bound_x_hi) ? bound_x_lo : bound_x_hi;
        *(float*)((char*)slot + 0x78) = FLOAT_80614ed8 + max_q;

        // Z bounds
        float min_z = (vel_z_lo < vel_z_hi) ? vel_z_lo : vel_z_hi;
        *(float*)((char*)slot + 0x70) = ((max_t * min_z <= 1.0f) ? (max_t * min_z) : 1.0f) - FLOAT_80614ed8;
        float max_z = (vel_z_lo > vel_z_hi) ? vel_z_lo : vel_z_hi;
        *(float*)((char*)slot + 0x7c) = FLOAT_80614ed8 + ((max_t * max_z >= 1.0f) ? max_t * max_z : 1.0f);
    }

    FUN_80382734();
}

// ============================================================================
// particle_spawn_timed
// Address: 0x800d5ce0
// Timed particle spawner with lifetime from config table (DAT_804070a4).
// Stores position + scale = 1.0. Used for gib trails, fire effects.
// 2 callers: gib_trail_spawn, FUN_800f7de0.
// ============================================================================
void particle_spawn_timed(int p1, int p2, int flags, float* position) {
    unsigned long long packed = FUN_803826e8();
    int sub_type = (int)packed;
    int type_sub = (int)(packed >> 32);
    int cfg_offset = sub_type * 0x90;

    int* slot = particle_slot_alloc();

    *(int*)((char*)slot + 8) = flags;
    *(int*)((char*)slot + 4) = sub_type;
    *(float*)((char*)slot + 0xc) = 1.0f;
    *(int*)((char*)slot + 0x14) = type_sub;

    // Fade time from config
    *(float*)((char*)slot + 0x10) = *(float*)(&g_particle_anim_cfg + cfg_offset + 8); // DAT_804070ac

    // Lifetime from table: converted from uint to float via int-to-double trick
    unsigned int raw_lifetime = *(unsigned int*)(&g_particle_anim_cfg + cfg_offset); // DAT_804070a4
    *(int*)((char*)slot + 0x1c) = (int)((float)raw_lifetime * g_particle_intensity_max);

    // Store position
    *(float*)((char*)slot + 0x20) = position[0];
    *(float*)((char*)slot + 0x24) = position[1];
    *(float*)((char*)slot + 0x28) = position[2];
    *(float*)((char*)slot + 0x2c) = 1.0f; // scale
    *(int*)((char*)slot + 0x98) = 0;

    FUN_80382734();
}
