// Address: 0x800d5dc0 - 0x800d8358 | Name: particle_trail_system | Date: 2026-08-11
// Particle trail/ribbon system: directed spawners, trail position circular buffers,
// GX FIFO quad-strip renderers with cross-product basis vectors, oriented billboards.
// Heavy use of sin/cos table for tube cross-sections.

#include "common.h"
#include "memory.h"
#include "scene.h"
#include "gx.h"

// ============================================================================
// particle_spawn_directed
// Address: 0x800d5dc0
// Config-table-driven directed particle spawner with explicit direction vector.
// Per-particle velocity from config table + RNG jitter. Computes AABB bounds
// from velocity curves. 9 callers: death_effects, ai_impact, explosions.
// ============================================================================
void particle_spawn_directed(int p1, int p2, int flags, float* position, float* direction);

// ============================================================================
// particle_spawn_directed_full
// Address: 0x800d6300
// Extended directed spawner with 3 optional override vectors:
//   param_5 = velocity base override (or NULL for config defaults)
//   param_6 = velocity range override (or NULL)
//   param_7 = direction override (or NULL)
// Stores extra state at ext+0xfa4 (sub-config) and ext+0xc80 (scale).
// Same AABB logic as particle_spawn_directed.
// ============================================================================
void particle_spawn_directed_full(int p1, int p2, int flags, float* position,
                                   float* vel_base, float* vel_range, float* direction);

// ============================================================================
// particle_spawn_trail
// Address: 0x800d67b0
// Trail particle spawner: allocates slot, initializes per-particle position
// history (current + previous at stride 0x20), velocity from config table.
// Uses circular buffer with max entries from DAT_804070a4. AABB bounds.
// ============================================================================
void particle_spawn_trail(int p1, int p2, int flags, float* position, float* direction);

// ============================================================================
// particle_trail_render
// Address: 0x800d6c34
// MASSIVE GX FIFO trail renderer. Reads circular buffer of positions from
// ext_data, computes cross-product tangent/normal/binormal per segment,
// renders textured quad strips with sin-table-based tube cross-sections.
// Alpha fades from head to tail. Uses sprite_get_texture_id for texturing.
// 1 caller (particle render dispatch).
// ============================================================================
void particle_trail_render(void);

// ============================================================================
// particle_spawn_billboard
// Address: 0x800d72f4
// Simple billboard particle: position only, no sub-particles. Stores opacity
// scale in ext_data[0] and ext_data[0xab]. Flag 0x40000000 checked via CLZ.
// 8 callers.
// ============================================================================
void particle_spawn_billboard(int p1, int p2, unsigned int flags, float* position, int player_mask);

// ============================================================================
// particle_trail_tick
// Address: 0x800d73cc
// Per-frame trail update: advances circular buffer write index, stores new
// position if moved, computes tangent from consecutive positions using cross
// product with up vector (DAT_803cc790-803cc798). Normalizes via Newton-Raphson
// inverse sqrt. For types 0x1c/0x1d/0x27, uses absolute values for basis.
// Removes expired entries based on elapsed time / max lifetime.
// Calls weather_slot_destroy when trail expires.
// ============================================================================
void particle_trail_tick(int* slot);

// ============================================================================
// particle_trail_render_tube
// Address: 0x800d782c
// MASSIVE tubular trail renderer. 2-pass rendering:
//   Pass 1 (x3 layers): ring cross-sections using sin/cos from g_sin_table,
//     quad strips with per-vertex alpha from DAT_8040bd40 width table.
//     Color palette selection by ext_data[0xac] (3 types).
//   Pass 2: end-cap billboard using camera matrix from g_player_slot+0x364.
// Uses sprite textures 0x33A and 0x290.
// 1 caller (particle render dispatch).
// ============================================================================
void particle_trail_render_tube(void);

// ============================================================================
// particle_spawn_oriented
// Address: 0x800d7dc4
// Oriented billboard particle spawner. Computes basis vectors from direction:
//   - If direction Y is small: use simplified cross with up=(0,1,0)
//   - Otherwise: full cross product + Newton-Raphson normalize
// Stores tangent at ext[0x48-0x4a], binormal at ext[0x4b-0x4d].
// ============================================================================
void particle_spawn_oriented(void);

// ============================================================================
// particle_trail_tick_simple
// Address: 0x800d80ac
// Simplified trail tick: positions stored linearly at ext+0x10 (stride 0xc),
// timestamps at ext+0xdc (stride 4), speed from ext+0x138.
// Same circular buffer / expire logic as particle_trail_tick but simpler layout.
// Calls weather_slot_destroy when expired.
// ============================================================================
void particle_trail_tick_simple(int* slot);

// ============================================================================
// particle_spawn_flag
// Address: 0x800d8358
// Minimal particle spawner: flags from packed param, position, player mask.
// No sub-particles, no AABB. 1 caller.
// ============================================================================
void particle_spawn_flag(int p1, int p2, float* position, int player_mask);
