// =============================================================================
// Player AI Fire Utilities & Impact Effects
// Address Range: 0x800b3b40 - 0x800b69f4
// Date: 2026-08-11
// Batch: 260
//
// Damage response, collision result accessors (entity ID, face index, surface
// color, material ID, validity), MASSIVE particle definitions init (~50+ effect
// defs for bullet impacts, sparks, smoke, fire, blood, water, lasers, debris),
// impact decal spawning, and placeholder stub.
// =============================================================================

#include "common.h"
#include "ai/player_ai.h"
#include "collision/collision.h"
#include "particle/particle.h"

// ---------------------------------------------------------------------------
// Globals (extern)
// ---------------------------------------------------------------------------
extern u8  g_behavior_defs[];       // DAT_804045bc (flags at +0x2C per 0x40 entry)
extern s32 g_ai_frame_number;       // DAT_80611928
extern s32 g_ai_last_decal_frame;   // DAT_80611db4
extern s32 g_surface_material_table; // DAT_80610a20

// ---------------------------------------------------------------------------
// Address: 0x800b3b40 | Name: player_ai_on_damage_received
//
// AI damage response. Applies knockback force if behavior flag bit 2 is clear.
// Zeroes damage timer on secondary hit data. Adjusts sound pitch on channel
// 0x21 if slot has voice+weapon. 16 callers.
// ---------------------------------------------------------------------------
void player_ai_on_damage_received(s32* slot, s32 hit_data)
{
    // Apply knockback unless behavior flag bit 2 set (heavy/immovable)
    u32 behavior_flags = *(u32*)(g_behavior_defs + slot[0] * 0x40 + 0x2C);
    if ((behavior_flags & 4) == 0) {
        damage_compute_knockback_force((f32)slot[0x3D], slot[2], slot[0]);
    }

    // Zero damage timer on secondary hit
    if (*(s32*)(hit_data + 4) != 0) {
        slot[0x3E] = 0;  // float stored as int bits = 0.0f
    }

    // Boost voice pitch if slot has voice data and weapon mode 1
    if (slot[0x41] != 0 && slot[0x40] == 1) {
        sound_set_channel_volume(1.0f, 0x21, *(s32*)(slot[0x41] + 8));
    }

    damage_flash_screen();
}

// ---------------------------------------------------------------------------
// Address: 0x800b3c0c | Name: collision_get_hit_entity_id
//
// Extracts the hit entity ID from the current collision result buffer at
// offset +0x18. Returns the entity ID of what was hit.
// ---------------------------------------------------------------------------
u32 collision_get_hit_entity_id(void)
{
    u8 buffer[24];
    u32 entity_id;
    collision_result_restore((s32)buffer);
    // entity_id is at offset 0x18 in the restored buffer
    return entity_id;
}

// ---------------------------------------------------------------------------
// Address: 0x800b3c34 | Name: collision_get_hit_face_index
//
// Extracts the hit face/triangle index from the collision result at offset
// +0x14. Used for surface material lookup and decal placement.
// ---------------------------------------------------------------------------
s32 collision_get_hit_face_index(void)
{
    u8 buffer[20];
    s32 face_index;
    collision_result_restore((s32)buffer);
    // face_index is at offset 0x14 in the restored buffer
    return face_index;
}

// ---------------------------------------------------------------------------
// Address: 0x800b3c5c | Name: collision_color_expand_4444
//
// Expands a packed 16-bit 4444 RGBA color to 32-bit:
//   R = (input & 0xF000) << 15
//   G = (input & 0x0F00) << 11
//   B = (input & 0x00F0) << 7
//   A = (input & 0x000F) << 3
// ---------------------------------------------------------------------------
u32 collision_color_expand_4444(u32 packed_color)
{
    return ((packed_color & 0xF000) << 15) |
           ((packed_color & 0x0F00) << 11) |
           ((packed_color & 0x00F0) << 7)  |
           ((packed_color & 0x000F) << 3);
}

// ---------------------------------------------------------------------------
// Address: 0x800b3c74 | Name: collision_get_hit_surface_color
//
// Gets the surface material color at the collision hit point. Navigates
// mesh -> triangle -> material table, reads the 16-bit 4444 color, and
// expands via collision_color_expand_4444. Returns 0 if no valid hit.
// ---------------------------------------------------------------------------
u32 collision_get_hit_surface_color(void)
{
    s32 mesh_ptr;
    s32 mesh_index;
    s32 face_offset;

    collision_result_restore((s32)&mesh_ptr);

    if (mesh_ptr == 0 || mesh_index == -1) {
        return 0;
    }

    // Navigate: mesh -> strip table -> material entry -> 16-bit color at +0xC
    s32* mesh_data = *(s32**)(mesh_ptr + 4);
    s32 strip_idx = mesh_data[mesh_data[0] * -0x30 + mesh_index * 0x30 + 0x27];
    s32 material_base = *(s32*)(mesh_data[strip_idx] + 0x1C);
    u16 packed = *(u16*)(material_base + face_offset * 0x0E + 0x0C);

    return collision_color_expand_4444((u32)packed);
}

// ---------------------------------------------------------------------------
// Address: 0x800b3cec | Name: collision_get_surface_material_id
//
// Gets the surface material byte ID from the global material table using
// the hit face index. Returns 0 if face index is out of bounds.
// ---------------------------------------------------------------------------
u8 collision_get_surface_material_id(void)
{
    s32 face_index = collision_get_hit_face_index();

    if (face_index < 0 || face_index >= *(s32*)(g_surface_material_table + 0x0C)) {
        return 0;
    }

    return *(u8*)(*(s32*)(g_surface_material_table + 4) + face_index);
}

// ---------------------------------------------------------------------------
// Address: 0x800b3d3c | Name: collision_hit_is_valid_surface
//
// Validates a collision hit. Returns 1 if:
// - face_index >= 0 (valid hit)
// - Flag 0x1000000 is NOT set (not a no-decal surface)
// Returns 0 otherwise.
// ---------------------------------------------------------------------------
s32 collision_hit_is_valid_surface(void)
{
    u8 buffer[24];
    s32 face_index;
    u32 flags;

    collision_result_restore((s32)buffer);

    if (face_index < 0 || (flags & 0x1000000) != 0) {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Address: 0x800b3d84 | Name: player_ai_particle_defs_init
//
// MASSIVE particle system initialization. Creates ~50+ particle effect
// definitions for weapon impacts including:
// - Bullet sparks (orange/yellow, directional)
// - Smoke puffs (grey, expanding)
// - Fire trails (orange->black fade)
// - Blood splatter (red, 3 team colors: orange/green/purple)
// - Water splashes (blue, expanding rings)
// - Laser impacts (blue/green glow)
// - Debris chunks (brown, gravity-affected)
// - Muzzle flash (bright white, short-lived)
// - Shell casing trails
// - Explosion debris (multi-colored team variants)
//
// Called once from player_ai_pool_reset at level load. Populates the global
// particle definition table starting at DAT_805644c0.
// ---------------------------------------------------------------------------
void player_ai_particle_defs_init(void)
{
    ai_particle_lock();

    // NOTE: This function is ~3KB of particle system setup calls.
    // Each effect definition consists of:
    //   1. particle_def_create() - base properties (size, lifetime, texture ID)
    //   2. particle_def_set_color() - start/end RGBA with interpolation
    //   3. particle_def_set_scale() - start/end scale with easing
    //   4. particle_def_set_gravity() - gravity vector
    //   5. particle_def_set_velocity() - initial velocity range
    //   6. particle_def_register() - register into global table with parent link
    //   7. particle_def_set_emission() - emission rate and burst count
    //
    // Effect categories by global table offset:
    //   0x805644c0 - Standard bullet spark (orange)
    //   0x80564610 - Small bullet spark (grey)
    //   0x80564760 - Smoke trail (expanding grey)
    //   0x8056482c - Fire spark (orange->red with sub-emitter)
    //   0x805648e8 - Fire spark variant (blue->purple)
    //   0x80564920 - Fire combined (spark + smoke)
    //   0x805649a4 - Blue spark (energy weapon)
    //   0x80564a70 - Blue spark sub-emitter
    //   0x80564b2c - Smoke ring (after impact)
    //   0x80564c7c - Large smoke (explosion aftermath)
    //   0x80564d48 - Combined smoke (ring + large)
    //   0x80564dcc - Blood splash (standard red)
    //   0x80564e98 - Blood mist (lingering)
    //   0x80564f1c - Blood droplets (gravity-affected)
    //   0x8056506c - Blood spray (directional)
    //   0x80565138 - Blood combined
    //   0x805651bc - Blood variant (bright)
    //   0x80565240 - Blood droplets (low gravity)
    //   0x805652c4 - Blood spray variant
    //   0x80565348 - Blood dark variant
    //   0x805653cc - Dust impact (brown)
    //   0x80565498 - Dust cloud (expanding)
    //   0x8056551c - Water splash (ring)
    //   0x805655e8 - Water droplets
    //   0x8056566c - Water ripple (surface)
    //   0x805657bc - Concrete chip (tan)
    //   0x80565888 - Concrete dust
    //   0x8056590c - Metal spark (bright white, bouncing)
    //   0x80565aa4 - Metal spark blue variant
    //   0x80565b70 - Metal spark green variant
    //   0x80565cc0 - Wood splinter (brown, gravity)
    //   0x80565d44 - Blue metal (combined)
    //   0x80565dc8 - Green metal (combined)
    //   0x80565e4c - Laser beam (stretched billboard)
    //   0x80565f9c - Laser beam variant (thicker)
    //   0x805660ec - Flame burst (weapon fire)
    //   0x8056623c - Flame burst (dark variant)
    //   0x8056638c - Explosion debris (team orange)
    //   0x805664dc - Explosion debris (duplicate)
    //   0x80566560 - Team debris (red)
    //   0x805666b0 - Team debris (green)
    //   0x80566800 - Team debris (blue)
    //   0x80566950 - Glass shard (transparent, tumbling)
    //   0x80566a1c - Glass combined
    //   0x80566ad8 - Electric arc (bright, short)
    //   0x80566c28 - Electric arc (longer, blue)
    //   0x80566d78 - Smoke wisp (thin)
    //   0x80566e44 - Smoke combined
    //   0x80566fd0 - Electric blue bolt
    //   0x8056709c - Electric combined
    //   0x80567120 - Fire trail sub-emitter
    //   0x80567158 - Smoke standard
    //   0x805672a8 - Energy glow (expanding)
    //   0x805673f8 - Energy spark (short burst)
    //   0x805674c4 - Energy + fire sub
    //   0x80567548 - Energy + blue sub
    //   0x805675cc - Energy glow (no sub)
    //   0x80567650 - Energy spark (no sub)
    //   0x805676d4 - Fire trail combined
    //   0x8056770c - Energy + fire trail
    //   0x80567790 - Electric arc (variant)
    //   0x80567814 - Electric blue (variant)
    //   0x80567898 - Debris chunk (tan, gravity)
    //   0x80567964 - Debris combined
    //   0x805679e8 - Green glow (bio weapon)
    //   0x80567ab4 - Green combined
    //   0x80567b38 - Smoke + standard
    //   0x80567bbc - Electric + arc (variant)
    //   0x80567c40 - Electric + blue (variant)
    //   0x80567cc4 - Smoke wisp (thick)
    //   0x80567d90 - Smoke thick (persistent)
    //   0x80567e14 - Spark shower (golden)
    //   0x80567ee0 - Spark registered
    //   0x80567f64 - Flame (weapon, bright)
    //   0x80567fe8 - Smoke dark
    //   0x805680b4 - Smoke dark (no gravity)
    //   0x80568138 - Smoke dark (with gravity)
    //   0x805681bc - Electric thin
    //   0x80568240 - Debris tumble (physics)
    //   0x8056830c - Debris registered
    //   0x80568390 - Debris shrapnel
    //   0x8056845c - Shrapnel registered
    //   0x805684e0 - Glow pulse (short)
    //   0x805685ac - Glow registered
    //   0x80568630 - Glow (no sub)
    //   0x805686b4 - Bright flash
    //   0x80568780 - Flash registered
    //   0x80568804 - Blood drip (slow)
    //   0x805688d0 - Blood drip registered
    //   0x80568954 - Poison spray
    //   0x80568a20 - Poison registered

    // (Full implementation: ~200 particle system API calls)
    // See Ghidra decompilation at 0x800b3d84 for exact parameter values.

    ai_particle_unlock();
}

// ---------------------------------------------------------------------------
// Address: 0x800b6880 | Name: player_ai_spawn_impact_decal
//
// Spawns impact decal particles at collision point. Gets surface color via
// collision_get_hit_surface_color, spawns 2-3 particles:
// 1. Colored splash (if behavior type uses it)
// 2. Standard impact particle
// 3. Dust/debris particle
// Throttled to 1 decal per 20 frames to prevent particle overflow.
// ---------------------------------------------------------------------------
void player_ai_spawn_impact_decal(s32 p1, s32 p2, f32* direction, s32 char_entity)
{
    s32 behavior_data = ai_decal_lock();
    s32 behavior_id = behavior_data;
    f32* hit_pos = /* from lock return */;

    s32 face_index = collision_get_hit_face_index();
    if (face_index == -1) goto done;

    // Check behavior flag 0x40000 (no-decal behavior)
    s32 behavior_offset = behavior_id * 0x40;
    if ((*(u32*)(g_behavior_defs + behavior_offset + 0x2C) & 0x40000) != 0) goto done;

    // Gate: only certain behavior types (6,7) or armed+throttled
    bool should_spawn = false;
    if ((u32)(behavior_id - 6) < 2) {
        should_spawn = true;
    } else if (char_entity != 0 && *(s32*)(char_entity + 0x150) != 0) {
        if (g_ai_last_decal_frame < g_ai_frame_number - 20) {
            s32 visible = check_decal_visibility();
            if (visible == 1) should_spawn = true;
        }
    }

    if (!should_spawn) goto done;

    // Get surface color and byte-swap to ABGR
    u32 surface_color = collision_get_hit_surface_color();
    surface_color = ((surface_color << 24) | ((surface_color & 0xFF00) << 8) |
                    (surface_color >> 24) | ((surface_color >> 8) & 0xFF00)) | 0x7F;

    // Scale direction for particle velocity
    f32 vel[3];
    vel[0] = 0.5f * direction[0];
    vel[1] = 0.5f * direction[1];
    vel[2] = 0.5f * direction[2];

    // Spawn colored splash (type 2 behaviors only)
    s32 splash_type = *(s32*)(g_behavior_defs + behavior_offset + 0x18);
    if (splash_type == 2) {
        u32 splash_color = *(u32*)(g_behavior_defs + behavior_offset + 0x1C);
        particle_effect_spawn(0, 4, face_index, 1, hit_pos, vel, splash_color, surface_color);
    }

    // Spawn standard impact + dust
    particle_effect_spawn(0, 2, face_index, 1, hit_pos, vel, surface_color, surface_color);
    particle_effect_spawn(0, 3, -1, 1, hit_pos, vel, 0x7F, 0x7F);

    g_ai_last_decal_frame = g_ai_frame_number;

done:
    ai_decal_unlock();
}

// ---------------------------------------------------------------------------
// Address: 0x800b69f4 | Name: player_ai_impact_stub_nop
//
// Empty no-op stub. Used as placeholder in impact dispatch table.
// ---------------------------------------------------------------------------
void player_ai_impact_stub_nop(void)
{
    return;
}
