// Address: 0x800dda70 | Name: particle_system_main_tick | Date: 2026-08-12
// Address: 0x800de07c | Name: particle_sprite_billboard_render | Date: 2026-08-12
// Address: 0x800de340 | Name: particle_effect_spawn | Date: 2026-08-12
// Address: 0x800de880 | Name: particle_firework_physics_tick | Date: 2026-08-12
// Address: 0x800dec74 | Name: particle_oriented_quad_render | Date: 2026-08-12
// Address: 0x800df20c | Name: particle_animated_sprite_render | Date: 2026-08-12
// Address: 0x800df83c | Name: particle_system_render_dispatch | Date: 2026-08-12
// Address: 0x800e040c | Name: light_probe_table_rebuild | Date: 2026-08-12
// Address: 0x800e046c | Name: light_probe_gather_evaluate | Date: 2026-08-12
// Address: 0x800e0a68 | Name: particle_spawn_position_generate | Date: 2026-08-12
//
// Particle system core: main tick dispatcher, render dispatcher, effect spawning,
// physics simulation, light probe evaluation, and spawn position generation.
// This is the heart of the particle/weather engine.

#include "common.h"

// Forward declarations (previously decompiled)
extern void weather_slot_destroy(int* slot);
extern void weather_position_velocity_tick(int slot);
extern void particle_velocity_trail_tick(int slot);
extern void particle_emitter_tick_trail(void);
extern void particle_velocity_bounce_tick(int slot);
extern void particle_emitter_tick_physics(void);
extern void particle_emitter_tick_linear(void);
extern void weather_strip_physics_tick(void);
extern void particle_emitter_stub_nop(void);
extern void particle_conditional_velocity_tick(int slot);
extern void weather_sway_oscillation_tick(void);
extern void weather_fade_out_tick(int* slot);
extern void particle_radial_physics_tick(int slot);
extern void particle_emitter_tick_circular(void);
extern void weather_trail_advance_tick(int* slot);
extern void weather_snowflake_render(void);
extern void weather_billboard_render(void);
extern void weather_flare_render(void);
extern void weather_stub_nop_b(void);
extern int particle_slot_alloc(void);
extern int scene_query_nodes_in_frustum(int, int, int, int);
extern int sprite_get_texture_id(int);
extern int player_slot_get_pool_base(void);

// Global particle type list heads (40 lists at DAT_8056e010)
extern int* g_particle_type_lists[40]; // DAT_8056e010

// Frame timing
extern float g_frame_dt;       // FLOAT_80611df8
extern float g_raw_frame_dt;   // FLOAT_806108ec
extern float g_time_scale;     // FLOAT_806118ec

// ============================================================================
// particle_system_main_tick
// Address: 0x800dda70
// MASSIVE central particle system tick dispatcher. Called once per frame.
// Responsibilities:
// 1. Compute frame delta (with pause/time-scale handling)
// 2. Call subsystem updates (FUN_800e46bc, collision, scene, etc.)
// 3. Iterate all 40 particle type lists (linked via slot+0x64)
// 4. Per-slot: advance time, compute scale fade (grow/decay curves),
//    determine visibility per-player via room bitmask checks
// 5. Dispatch to appropriate per-type tick function via 27-case switch:
//    Type 3: weather_position_velocity_tick
//    Type 4-5: particle_velocity_trail_tick
//    Type 6-8: particle_emitter_tick_trail
//    Type 9: particle_firework_physics_tick (this batch)
//    Type 0xB: particle_velocity_tick
//    Type 0xC: weather_trail_advance_tick
//    Type 0xD-0xE: particle_trail_tick_simple
//    Type 0xF-0x10: particle_velocity_bounce_tick
//    Type 0x11-0x12: particle_emitter_tick_physics
//    Type 0x13-0x14: particle_emitter_tick_linear
//    Type 0x15: weather_strip_physics_tick
//    Type 0x16-0x17: particle_emitter_stub_nop
//    Type 0x18: particle_conditional_velocity_tick
//    Type 0x1B: inline RNG-based jitter update
//    Type 0x1C-0x1D,0x23-0x24,0x27: particle_trail_tick
//    Type 0x20: weather_sway_oscillation_tick
//    Type 0x21: weather_fade_out_tick
//    Type 0x22: particle_radial_physics_tick
//    Type 0x25-0x26: particle_emitter_tick_circular
// 6. Destroy slots that have expired (lifetime reached, scale=0)
// ============================================================================
void particle_system_main_tick(void)
{
    // Compute delta time with pause/time-scale
    // If game paused (flag 0x800) or cutscene flag set:
    //   g_frame_dt = raw_dt
    // Else:
    //   g_frame_dt = raw_dt * time_scale

    // Call subsystem pre-tick functions

    // Iterate 40 type lists
    for (int list_idx = 0; list_idx < 40; list_idx++) {
        // Walk linked list via slot[0x19] (next pointer at +0x64)
        // Per slot:
        //   - Advance time: slot[3] += scaled_dt
        //   - Check lifetime expiry -> destroy
        //   - Compute scale fade (grow curve via lifetime ratio)
        //   - Compute per-player visibility bitmask from room list
        //   - If visible: dispatch to type-specific tick via switch
    }
}

// ============================================================================
// particle_sprite_billboard_render
// Address: 0x800de07c
// Renders a single sprite billboard in screen space. Projects 3D position
// to clip space via camera matrix, performs frustum rejection, computes
// rotated quad corners using sin/cos of a rotation angle, and submits
// the textured quad. Used by weather_flare_render and other effects.
// Parameters include scale X/Y, rotation angle, texture ID, position, color.
// ============================================================================
void particle_sprite_billboard_render(/* scale_x, scale_y, rotation, tex_id, position, color */)
{
    // 1. Setup texture (FUN_8016e77c)
    // 2. Transform position to clip space (FUN_8012fb70 with camera matrix)
    // 3. Frustum rejection: check all clip coords within [-w*1.5, +w*1.5]
    //    Extended clip bounds (1.5x) to allow partial visibility
    // 4. Compute rotation: sin/cos from angle (lookup table)
    // 5. Perspective scale: sprite_size * viewport_scale / w
    //    Clamp to max screen size (FLOAT_80614ff4)
    // 6. Compute 4 quad corners:
    //    Rotated by sin/cos, offset in screen X/Y, at depth 1+ndc.z
    // 7. Submit quad via FUN_8017a62c with texture dims and color
}

// ============================================================================
// particle_effect_spawn
// Address: 0x800de340
// MASSIVE particle effect spawner. Creates a multi-particle effect with:
// - Slot allocation and type/lifetime setup
// - Floor height query for bounce plane (optional)
// - 8 control point basis vectors with random velocity/force
// - Per-particle (up to 32) initialization:
//   Random direction (normalized sphere), scaled by random radius
//   Random initial velocity from direction param + randomization
//   Random scale per-particle from type table range
//   Random texture selection (2 textures, random per-particle)
//   Random sub-type index (0-7) for rendering variation
//   Lifetime per-particle = 0
// - AABB from type radius, scene room query
// Uses effect type table at DAT_8040bba8 (0x44 bytes per entry).
// ============================================================================
void particle_effect_spawn(/* type, ctx, param3, param4, position, direction, color_a, color_b */)
{
    // [See full decompilation for details]
    // Key: allocates slot, queries floor, initializes 8 basis points
    // with random velocity, then per-particle random positions/velocities
    // from a separate type definition table
}

// ============================================================================
// particle_firework_physics_tick
// Address: 0x800de880
// Per-frame physics tick for firework/spark particles (type 9). Handles:
// - 8 control point basis re-normalization (FUN_8012f154)
// - Per-control-point velocity integration (force * dt)
// - Per-particle: velocity drag (exponential decay)
// - Gravity on Y velocity (from type table +0xD4)
// - Repulsion force: particles close to position get pushed away
//   (inverse-square from proximity threshold)
// - Position integration (pos += vel * dt)
// - Floor bounce (flag 0x20000) with damping on vel + force
// - AABB tracking (min/max of all particle positions)
// - Final world AABB = position + local AABB
// ============================================================================
void particle_firework_physics_tick(void)
{
    // [Complex N-body-like particle simulation]
    // Uses effect type table at DAT_8040bbb4 (0x44 bytes per entry)
}

// ============================================================================
// particle_oriented_quad_render
// Address: 0x800dec74
// Renders particles as oriented quads (triangles/quads) in world space.
// Per-particle: transforms local offset by a rotation matrix, applies
// distance-based alpha fade and scale, then submits 3 or 4 vertex quads
// to GX FIFO with color and UV. Supports an optional secondary texture
// pass with different blend mode for glow/additive effects.
// ============================================================================
void particle_oriented_quad_render(void)
{
    // 1. Get effect type entry (0x44 bytes per entry at DAT_8040bba8)
    // 2. Set blend mode from type table
    // 3. Compute distance fade: alpha = 1 - (time * fade_rate / lifetime)
    //    Clamp [0, 1], also fade by camera distance
    // 4. Per-particle:
    //    a. World position = slot_position + local_offset
    //    b. Compute rotated vertex positions using rotation matrix
    //       Supports 3-vertex (triangle) or 4-vertex (quad) from type
    //    c. Transform vertices by particle matrix (FUN_8012fa34)
    // 5. Optional secondary texture pass (type +0xB0 != -1):
    //    Same geometry, different texture, full-bright color (0x7f7f7f7f)
    // 6. Primary pass: per-particle texture from stored ID, alpha from
    //    lifetime fade, GX FIFO quad submission
}

// ============================================================================
// particle_animated_sprite_render
// Address: 0x800df20c
// Renders particles as animated sprites with texture frame selection based
// on lifetime. Supports two rendering modes:
// Mode 0 (billboard): screen-space quad via camera projection (same as
//   particle_sprite_billboard_render path)
// Mode 1+ (oriented): world-space quad using local rotation matrix with
//   sin/cos from per-particle phase angle
// Features:
// - Multi-frame texture animation (N frames from type +0xA4)
// - Cross-fade blending between frames (2 textures with alpha weights)
// - Distance-based alpha fade at end of lifetime
// - Scale growth over lifetime (from type +0xD4, +0xD8 per axis)
// ============================================================================
void particle_animated_sprite_render(void)
{
    // 1. Get type entry, compute frame index from (time/lifetime) * frame_count
    // 2. Select current and next texture IDs for cross-fade
    // 3. Compute alpha weights for cross-fade blending
    // 4. Compute end-of-life alpha fade
    // 5. If mode 0 (billboard): project + render via FUN_8017a62c
    // 6. If mode 1+ (oriented): compute sin/cos rotated quad in world space
    //    using particle basis vectors, render via FUN_80171130
}

// ============================================================================
// particle_system_render_dispatch
// Address: 0x800df83c
// MASSIVE particle system render dispatcher. Called during the render phase
// to draw all visible particles. Sets up render state, then iterates ~30
// type-specific linked lists, calling the appropriate renderer for each:
// - Blend mode 1 (normal): trails, snowflakes, oriented quads, tubes
// - Blend mode 2 (additive): flares, billboards, animated sprites
// - Per-particle visibility check via room bitmask (particle_slot_is_visible)
// - Sub-particle iteration for multi-part effects (linked at +0xCC)
// Renderers called: weather_snowflake_render, weather_billboard_render,
//   weather_flare_render, particle_trail_render, particle_trail_render_tube,
//   particle_oriented_quad_render, and ~10 other specialized renderers.
// ============================================================================
void particle_system_render_dispatch(void)
{
    // 1. Save render state
    // 2. Setup projection matrix and viewport
    // 3. Call subsystem pre-render (collision debug, etc.)
    // 4. Set blend mode additive, compute player group_mask
    // 5. For each of ~30 type lists:
    //    Walk linked list, check visibility, call renderer
    // 6. Restore render state
}

// ============================================================================
// light_probe_table_rebuild
// Address: 0x800e040c
// Rebuilds the light probe index table. Scans the 100-entry entity table
// (0xD0 bytes per entry at DAT_80611de8) for entries with type 0x19 or 0x1A
// (point lights / animated lights), builds a compact index array at
// DAT_8056e150, and stores count in DAT_80611dfc.
// Called when DAT_806107e4 flag is set (probe table dirty).
// ============================================================================
void light_probe_table_rebuild(void)
{
    int base = /* DAT_80611de8 */ 0;
    int count = 0;

    for (int i = 0; i < 100; i++) {
        int type = *(int*)(base + i * 0xD0);
        if (type > 0x18 && type < 0x1B) {
            // Store pointer to this entry in index array
            /* DAT_8056e150[count] */ count++;
        }
    }

    /* DAT_80611dfc = count */
    /* DAT_806107e4 = 0 */ // clear dirty flag
}

// ============================================================================
// light_probe_gather_evaluate
// Address: 0x800e046c
// MASSIVE light probe gathering and evaluation. Finds the 3 nearest light
// probes to a given position and computes per-probe lighting contribution.
// Algorithm:
// 1. If first frame for this entity (frame counter % 4 == entity_id % 4):
//    - Rebuild probe table if dirty
//    - For each probe in table: compute distance to position
//    - Priority-insert into 5-slot sorted list (nearest 5)
//    - For each of top 3: query floor height for shadow occlusion
//    - Verify visibility (LOS check)
//    - Store 3 probe indices in entity render data (+0x240)
// 2. On subsequent frames: re-use stored probe indices
//    - Validate: check type still 0x19-0x1A, check distance < max
//    - If invalid: clear slot
// 3. Per-probe color evaluation:
//    - Type 0x1A (animated): sum 24 keyframe contributions based on time
//      Each keyframe has attenuation based on (time - key_time) / range
//      Final intensity = sum * scale_factor, clamped to [0, 1]
//    - Type 0x19 (static): single contribution from (time / period)
//      with [0,1] clamped attenuation
//    - Extract RGB from probe color, multiply by intensity * distance_atten
//    - Store per-probe: color RGB, direction XYZ (normalized, scaled by 1/dist)
// 4. Output: 3 probe descriptors written to param_7 array
// ============================================================================
void light_probe_gather_evaluate(/* type, ctx, position, entity, param_7 */)
{
    // [See full decompilation - extremely complex lighting system]
}

// ============================================================================
// particle_spawn_position_generate
// Address: 0x800e0a68
// Generates a spawn position for a new particle based on the emitter's
// shape type. 8 shape modes supported:
// Type 0: Direct position copy (point emitter)
// Type 1: Random sphere (uniform random direction * random radius * scale)
// Type 2: Random sphere + random Y (full 3D randomization)
// Type 3: Random direction * scale per-axis (ellipsoid)
// Type 4: Same as 3 but with random Y
// Type 5: Cone (radius ramps with lifetime, XZ random, Y fixed)
// Type 6: Torus (radius + offset, clamped to ring boundary)
// Type 7: Callback (calls user function pointer)
// All modes (except 0 and 7) apply a rotation transform (FUN_800e6e24)
// and add the emitter's world position as offset.
// ============================================================================
void particle_spawn_position_generate(/* type, ctx, position */)
{
    // Get emitter shape definition from ext+0xC
    // Switch on shape type (0-7):
    //   Generate local-space position
    //   Apply rotation transform
    //   Add world position offset
}
