// =============================================================================
// Particle System - Spawning, render dispatch, and utilities
// Address Range: 0x800cd590 - 0x800cdb08 (batch 279)
// Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "gx.h"
#include "particle.h"

// ---------------------------------------------------------------------------
// Globals (continued from particle_system.cpp)
// ---------------------------------------------------------------------------

// Emitter definition table (0x7A bytes per entry)
static ParticleEmitterDef g_emitter_defs[MAX_EMITTER_DEFS]; // DAT_80569a10

// Pool size configuration
static int g_particle_pool_base_size;     // DAT_80611de0
static int g_particle_pool_extended_size; // DAT_80611de4

// Split-screen flag (affects pool sizing)
extern int g_is_splitscreen; // DAT_80612244

// ---------------------------------------------------------------------------
// Address: 0x800cdb08 | Name: particle_get_emitter_def
// Returns pointer to emitter definition at index (0x7A bytes per entry).
// The emitter def table stores per-effect configs (velocity, colors, lifetimes).
// ---------------------------------------------------------------------------
ParticleEmitterDef* particle_get_emitter_def(int index)
{
    return &g_emitter_defs[index]; // DAT_80569a10 + index * 0x7A
}

// ---------------------------------------------------------------------------
// Address: 0x800cdae4 | Name: particle_set_pool_sizes
// Sets particle pool sizes. Base=0x38 (56), extended=0x40 (64).
// If split-screen flag is set, base also becomes 0x40.
// ---------------------------------------------------------------------------
void particle_set_pool_sizes(void)
{
    g_particle_pool_extended_size = 0x40;
    g_particle_pool_base_size = 0x38;

    if (g_is_splitscreen != 0) {
        g_particle_pool_base_size = 0x40;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800cda88 | Name: particle_random_blood_type
// Returns a random blood particle sub-type:
//   RNG % 15 == 3  -> type 3 (spray variant, ~7% chance)
//   RNG % 15 < 3   -> type 0 (standard splat, ~20% chance)
//   RNG % 15 > 3   -> type 2 (medium splat, ~73% chance)
// Called from player_ai_projectile_character_impact.
// ---------------------------------------------------------------------------
int particle_random_blood_type(void)
{
    u32 rng = get_random_u32(); // FUN_8013109c

    if (rng % 15 == 3) {
        return 3;
    } else if (rng % 15 < 3) {
        return 0;
    } else {
        return 2;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800cd6e0 | Name: particle_set_keyframe_color
// Reads a color keyframe from the per-type color table and scales by factor.
// Table layout: type * 0xEC + keyframe_index * 0x10 -> 4 floats (RGBA).
// Writes scaled RGBA to particle fields at +0x38..+0x44.
// ---------------------------------------------------------------------------
void particle_set_keyframe_color(float scale_factor, Particle* particle, int keyframe_index)
{
    // Compute offset into the particle type color table
    int type_offset = particle->list_index * PARTICLE_TYPE_TABLE_STRIDE; // 0xEC per type
    int color_offset = type_offset + keyframe_index * 0x10;

    // Read RGBA from table and scale
    float* color_table = (float*)&g_particle_type_color_table; // ~0x80405870
    particle->color_r = color_table[color_offset + 0] * scale_factor;
    particle->color_g = color_table[color_offset + 1] * scale_factor;
    particle->color_b = color_table[color_offset + 2] * scale_factor;
    particle->color_a = color_table[color_offset + 3] * scale_factor;
}

// ---------------------------------------------------------------------------
// Address: 0x800cd730 | Name: particle_spawn_global
// Spawns a global particle (not attached to any entity, player=-1, slot=-1).
// Sets position from pos_ptr, velocity from vel_ptr, age/lifetime from params.
// Computes rotation speed from frame delta and sets initial keyframe color.
// ---------------------------------------------------------------------------
void particle_spawn_global(/* register params */ float initial_age, float lifetime,
                           float* pos_ptr, float* vel_ptr)
{
    // Get type data from register context
    u64 type_data = get_particle_spawn_context(); // FUN_803826ec
    int type_index = (int)(type_data >> 32);
    int sub_type = (int)type_data;

    // Allocate a slot and insert into type list (global: player=-1, slot=-1)
    Particle* p = (Particle*)particle_insert_into_list(type_index, 0, -1, -1);

    // Initialize flags: mark as needing render (flag 8 = always visible)
    p->flags = 8;
    p->room_id = sub_type;

    // Set age and lifetime
    p->age = initial_age;
    p->lifetime = lifetime;

    // Set position
    p->pos_x = pos_ptr[0];
    p->pos_y = pos_ptr[1];
    p->pos_z = pos_ptr[2];

    // Set velocity
    p->vel_x = vel_ptr[0];
    p->vel_y = vel_ptr[1];
    p->vel_z = vel_ptr[2];

    // Compute rotation speed from frame time
    float dt = get_frame_delta_time(); // FUN_801310c0
    p->rotation = (PARTICLE_ROT_SCALE_A * PARTICLE_ROT_PERIOD * (dt - PARTICLE_ROT_BASE_B)) /
                  PARTICLE_ROT_DIVISOR;

    // Set initial keyframe color (index 0)
    particle_set_keyframe_color(/* scale from extra register */, p, 0);

    restore_spawn_context(); // FUN_80382738
}

// ---------------------------------------------------------------------------
// Address: 0x800cd838 | Name: particle_spawn_random_variant
// Picks a random variant index from the type's variant table, then delegates
// to particle_spawn_global with that variant. If no variants (count <= 0),
// uses the default sub-type at +0x34.
// ---------------------------------------------------------------------------
void particle_spawn_random_variant(/* register params */ float initial_age, float lifetime,
                                   float* pos_ptr, float* vel_ptr)
{
    begin_spawn_section(); // FUN_80382658

    // Get type data
    u64 type_data = get_particle_spawn_context(); // FUN_803826ec
    int* type_ptr = (int*)type_data;

    // Default sub-type
    int sub_type = type_ptr[0x34 / 4];

    // If variant count > 0, pick random variant
    int variant_count = *(s8*)(type_ptr + 0x30);
    if (variant_count > 0) {
        u32 rng = get_random_u32(); // FUN_8013109c
        int variant_idx = rng % variant_count;
        sub_type = *(int*)((u8*)type_ptr + 0x38 + variant_idx * 4);
    }

    // Delegate to the core spawn function
    particle_spawn_global(initial_age, lifetime, pos_ptr, vel_ptr);

    end_spawn_section();   // FUN_803826a4
    restore_spawn_context(); // FUN_80382738
}

// ---------------------------------------------------------------------------
// Address: 0x800cd8e4 | Name: particle_spawn_entity_attached
// Spawns a particle attached to an entity (type_id=4 in linked list,
// linked to entity's player index). Computes room visibility flag from
// the entity's scene node data.
// ---------------------------------------------------------------------------
void particle_spawn_entity_attached(/* register params */ float initial_age, float lifetime,
                                    float* pos_ptr, float* vel_ptr)
{
    // Get type data
    u64 type_data = get_particle_spawn_context(); // FUN_803826ec
    int* entity_data = (int*)type_data;
    int player_index = entity_data[2]; // player index from entity

    // Allocate slot (type 4 = entity-attached, slot=-1)
    Particle* p = (Particle*)particle_insert_into_list(
        (int)(type_data >> 32), 4, player_index, -1);

    // Base flags: mark for render
    p->flags = 4;

    // Compute room visibility bit from entity's scene node
    int entity_ptr = entity_data[0];
    int scene_node_ptr = *(int*)(entity_ptr + 0x150);
    if (scene_node_ptr != 0) {
        int room_data = *(int*)(scene_node_ptr + 8);
        if (room_data != 0) {
            // Set the per-player room visibility flag
            p->flags |= (1 << (*(int*)(room_data + 0x214) + 16));
        }
    }

    // Set room ID from entity data
    p->room_id = *(int*)(entity_ptr + 0x34);

    // Set age and lifetime
    p->age = initial_age;
    p->lifetime = lifetime;

    // Set position
    p->pos_x = pos_ptr[0];
    p->pos_y = pos_ptr[1];
    p->pos_z = pos_ptr[2];

    // Set velocity
    p->vel_x = vel_ptr[0];
    p->vel_y = vel_ptr[1];
    p->vel_z = vel_ptr[2];

    // Compute rotation speed
    float dt = get_frame_delta_time(); // FUN_801310c0
    p->rotation = (PARTICLE_ROT_SCALE_A * PARTICLE_ROT_PERIOD * (dt - PARTICLE_ROT_BASE_B)) /
                  PARTICLE_ROT_DIVISOR;

    // Set initial keyframe color
    particle_set_keyframe_color(/* scale */, p, 0);

    restore_spawn_context(); // FUN_80382738
}

// ---------------------------------------------------------------------------
// Address: 0x800cda2c | Name: particle_detach_from_entity
// Iterates all particle pool slots. For particles with type_id==4 (entity-attached)
// and matching player_index, converts to type 3 (free-floating) and clears link.
// Called when an entity is destroyed to release its particles.
// ---------------------------------------------------------------------------
void particle_detach_from_entity(int entity_player_index)
{
    for (int i = 0; i < g_particle_pool_size; i++) {
        Particle* slot = &g_particle_pool[i];

        if (slot->type_id == 4 && slot->player_index == entity_player_index) {
            // Convert to free-floating: type 3, unlinked
            slot->type_id = 3;
            slot->player_index = -1;
            slot->flags = 8; // always visible (no room check)
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800cd5d8 | Name: particle_render_dispatch
// Main particle render dispatch. Sets up GX state for particles, iterates all
// 19 type-linked-lists calling particle_render_list for each non-empty head,
// then iterates per-player bone-attached lists. Tears down state after.
// ---------------------------------------------------------------------------
void particle_render_dispatch(void)
{
    // Save paired-single registers + get render context
    save_ps_registers(); // FUN_803826dc
    u64 context = /* register context */;
    int view_index = (int)(context >> 32);
    u32 view_mask = 1 << ((int)context + 16);

    // Setup GX state for particle rendering
    gx_begin_particle_pass();   // FUN_8017c658
    gx_set_particle_material(); // FUN_8017fab4

    // Render all 19 type lists
    for (int type_idx = 0; type_idx < 19; type_idx++) {
        Particle* head = g_particle_type_heads[type_idx];
        if (head != NULL) {
            particle_render_list(view_index, type_idx, head, view_mask);
        }
    }

    // Render per-player bone-attached lists
    for (int player = 0; player < g_active_player_count; player++) {
        if (g_player_entity_ptrs[player] != NULL) {
            int entity_index = *(int*)(g_player_entity_ptrs[player] + 8);
            for (int slot = 0; slot < 3; slot++) {
                Particle* head = g_particle_player_heads[entity_index][slot];
                if (head != NULL) {
                    particle_render_list(view_index, (int)head->field_05, head, view_mask);
                }
            }
        }
    }

    // Teardown GX particle state
    gx_end_particle_pass(); // FUN_8017c670

    restore_ps_registers(); // FUN_80382728
}

// ---------------------------------------------------------------------------
// Address: 0x800cd590 | Name: particle_render_all_wrapper
// Sets texture mode to 2 (particle blend mode), calls particle_render_dispatch,
// then restores previous texture mode.
// ---------------------------------------------------------------------------
void particle_render_all_wrapper(void)
{
    int prev_tex_mode = set_texture_mode(2); // FUN_8016e8ac
    particle_render_dispatch();
    set_texture_mode(prev_tex_mode);
}
