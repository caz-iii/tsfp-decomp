// Address: 0x80093688 - 0x8009459c | Name: spawn_entity_lifecycle | Date: 2026-08-11
// Spawn entity runtime lifecycle: creation, destruction, position setup, tick updates,
// particle trails, lifetime management, collision activation.
// Part of the spawn/pickup system for collectible game entities.

#include "common.h"

// ============================================================================
// spawn_create_instance - 0x80093688
// MASSIVE spawner: allocates from main pool (120 slots x 0xD8) or extended pool
// (30 slots x 0xD8), spawns entity by type, sets up render/update callbacks,
// copies spawn def properties (position, angles, flags). Handles type-specific
// variants (0x20=weapon rack pickup, 0x21/0x20 with extended pool for MP items).
// 9 callers including spawn_entity_at_zone.
// ============================================================================
void spawn_create_instance(/* complex params: spawn def, position, type flags */)
{
    u64 spawn_flags = get_spawn_type_flags();  // FUN_803826b4
    int spawn_type = (int)spawn_flags;
    int evict_slot = -1;

    // Resolve model/mesh data from spawn type
    SpawnModelDef* model_def;
    if (spawn_type == 0x40) {
        model_def = spawn_get_model_def_extended(0x40, level_id, param15, param16, param17);
    } else {
        model_def = spawn_get_model_def(spawn_type, level_id);
    }

    // Determine vertex data pointer based on game mode
    bool use_extended = false;
    if (g_game_mode == 10 || (spawn_flags & 0x100000000) != 0) {
        use_extended = true;
    }
    float* vertex_data = use_extended ? (float*)(model_def + 10) : (float*)(model_def + 15);

    // Resolve spawn properties table (per-mode defaults)
    if (spawn_props == NULL) {
        spawn_props = (g_game_mode == 10) ? &s_spawn_props_story : &s_spawn_props_arcade;
    }

    // Entity type override for extended pool items (MP pickups)
    u32 entity_type_id;
    if ((spawn_flags & 0x100000000) != 0 && param15 == 0x20) {
        entity_type_id = 0x754;  // MP weapon pickup
    } else if ((spawn_flags & 0x100000000) != 0 && param15 == 0x21) {
        entity_type_id = 0x753;  // MP ammo pickup
    } else {
        entity_type_id = get_entity_type_from_model(model_def);
    }

    // Spawn the visual entity
    Entity* ent = entity_spawn_by_type(entity_type_id, position, vertex_data, 0, 0);

    // Non-weapon/ammo/special types get additional setup
    if (spawn_type != 0x80 && spawn_type != 0x20 && spawn_type != 0x400) {
        spawn_setup_render_effects();  // FUN_800e33f0
    }

    // Weapon rack special: resolve weapon entity references (story mode 6)
    if (spawn_type == 0x20 && g_game_mode == 6) {
        int weapon_id_a = (ent->param_count >= 1) ? ent->params[0] : DEFAULT_WEAPON_ID;
        spawn_resolve_weapon_entity(&weapon_id_a);
        int weapon_id_b = (ent->param_count >= 2) ? ent->params[1] : DEFAULT_WEAPON_ID;
        spawn_resolve_weapon_entity(&weapon_id_b);
    }

    // Global spawn tracking
    g_spawn_active_flag = 1;
    g_spawn_total_count++;

    // Apply spawn property overrides (scale, rotation offset, material)
    if ((spawn_props->flags & 2) != 0) {
        ent->scale_x = spawn_props->scale_x;
        ent->render_flags = spawn_props->render_mode;
        ent->scale_z = spawn_props->scale_z;
    }

    // Default health/lifetime from props
    if (ent->health == DEFAULT_HEALTH) {
        ent->health = (g_game_mode == 10) ? STORY_HEALTH_DEFAULT : ARCADE_HEALTH_DEFAULT;
    }

    // Allocate slot in main pool or extended pool
    if ((spawn_flags & 0x100000000) == 0) {
        // Main pool: 120 slots max
        if (g_spawn_main_pool_count > 0x77) {
            entity_destroy(ent);
            restore_interrupt_state();
            return;
        }
        int slot_offset = g_spawn_main_pool_count * SPAWN_SLOT_SIZE;
        g_spawn_main_pool_count++;
        ent->spawn_slot = g_spawn_main_pool_base + slot_offset;
    } else {
        // Extended pool: 30 slots, evict oldest if full
        int free_count = 0;
        int last_free = 0;
        for (int i = 0; i < 30; i++) {
            if (g_spawn_ext_pool_active[i] == 0) {
                free_count++;
                last_free = i;
            }
        }
        if (free_count == 0) {
            // Evict oldest by timestamp
            int oldest_slot = 0;
            int oldest_time = g_spawn_frame_counter;
            for (int i = 0; i < 30; i++) {
                if (g_spawn_ext_pool_timestamps[i] < oldest_time) {
                    oldest_time = g_spawn_ext_pool_timestamps[i];
                    oldest_slot = i;
                }
            }
            // Destroy existing entity in that slot
            if (g_spawn_ext_pool_entities[oldest_slot] != NULL) {
                if (g_spawn_ext_pool_entities[oldest_slot]->type == 0x400) {
                    spawn_cleanup_special_type(g_spawn_ext_pool_entities[oldest_slot]);
                }
                entity_destroy_with_event(g_spawn_ext_pool_entities[oldest_slot]);
            }
            last_free = oldest_slot;
            free_count = 1;
        }
        ent->spawn_slot = &g_spawn_ext_pool_slots[last_free];
        g_spawn_ext_pool_active[last_free] = 1;
        g_spawn_ext_pool_timestamps[last_free] = g_spawn_frame_counter;

        // Mark oldest slots for fade-out if pool nearly full
        if (free_count - 1 < 3) {
            for (int k = 0; k < free_count; k++) {
                // Find oldest active slot and mark for despawn
                int target = -1;
                int min_time = g_spawn_frame_counter;
                for (int i = 0; i < 30; i++) {
                    if (g_spawn_ext_pool_active[i] != 0 &&
                        g_spawn_ext_pool_timestamps[i] < min_time &&
                        g_spawn_ext_pool_entities[i] != NULL) {
                        min_time = g_spawn_ext_pool_timestamps[i];
                        target = i;
                    }
                }
                if (target != -1 && g_spawn_ext_pool_entities[target]->despawn_flag == 0) {
                    g_spawn_ext_pool_entities[target]->despawn_flag = 1;
                    int lifetime = (int)(DESPAWN_TIME_SCALE *
                                   (float)(g_spawn_frame_counter - min_time));
                    g_spawn_ext_pool_entities[target]->despawn_timer = lifetime;
                }
            }
        }
    }

    // Register for per-frame update callbacks
    entity_register_for_update(ent, 3, spawn_entity_tick_update);

    // Initialize spawn slot fields
    SpawnSlot* slot = (SpawnSlot*)ent->spawn_slot;
    slot->entity = ent;
    slot->state = 0;
    slot->pool_index = evict_slot;
    slot->spawn_flags_hi = (int)(spawn_flags >> 32);
    slot->spawn_type = spawn_type;
    slot->spawn_props = spawn_props;
    slot->level_id = level_id;
    slot->fade_state = 0;

    ent->entity_flags = 0x40;
    ent->render_priority = 2;

    // Type 0x800: spawn paired entity (e.g. weapon + holster)
    if (spawn_type == 0x800) {
        SpawnModelDef* paired_def = spawn_get_model_def(2, level_id);
        u32 paired_type = get_entity_type_from_model(paired_def);
        Entity* paired = entity_spawn_by_type(paired_type, position, vertex_data, 0, 0);
        slot->paired_entity = paired;
        paired->entity_flags = 0x40;
        paired->render_priority = 2;
        // Offset Y by model height
        paired->pos_y += paired_def->y_offset;
        spawn_setup_render_effects();
    }

    // Set initial material/alpha from random seed
    u32 seed = get_random_seed();
    slot->material_phase = (float)seed / RAND_MAX_F;
    float alpha = get_entity_alpha(ent);
    if (alpha != 0.0f) {
        render_instance_set_material(ent->render_instance, 1);
    }

    // Apply Y-axis rotation offset
    float rot_offset = get_entity_rotation_offset(ent);
    ent->rotation_y += rot_offset;

    // Set visibility flags
    if (spawn_props == NULL || (spawn_props->flags & 8) == 0) {
        ent->vis_flags |= 0x62;
    } else {
        ent->vis_flags &= ~1;
        ent->vis_flags |= 0x62;
    }

    // Invisible spawn flag (hidden until triggered)
    if ((spawn_flags & 0x200000000) != 0) {
        if (spawn_type == 0x800) {
            ent->vis_flags &= ~2;
            slot->paired_entity->vis_flags &= ~1;
        } else {
            ent->vis_flags &= ~3;  // fully hidden
        }
    }

    // Store spawn def reference and initial position
    slot->def_index = param_12;
    if (spawn_type == 0x20 && g_game_mode == 2) {
        slot->level_id = param_12 - 1;  // arcade mode offset
    }
    slot->initial_pos_x = ent->pos_x;
    slot->initial_pos_y = ent->pos_y;
    slot->initial_pos_z = ent->pos_z;
    slot->initial_scale_x = ent->scale_x;
    slot->initial_render_flags = ent->render_flags;
    slot->initial_scale_z = ent->scale_z;
    slot->sound_handle_a = -1;
    slot->sound_handle_b = -1;
    slot->sound_handle_ambient = -1;
    slot->category_a = param15;
    slot->category_b = param16;
    slot->category_c = param17;
    slot->category_d = param18;
    slot->respawn_count = 1;
    slot->anim_state = 3;
    slot->particle_handle = 0;
    slot->bob_phase = 0.0f;
    slot->bob_offset = 0;
    ent->spawn_ref = 0;

    restore_interrupt_state();  // FUN_80382700
}

// ============================================================================
// spawn_clear_player_slot_data - 0x80093d58
// Clears a 2-byte field at offset 0x50 in each active main pool slot for
// the given player index. Called when a player disconnects or respawns.
// 1 caller (player state reset).
// ============================================================================
void spawn_clear_player_slot_data(int player_index)
{
    int offset = 0;
    for (int i = 0; i < g_spawn_main_pool_count; i++) {
        SpawnSlot* slot = (SpawnSlot*)(g_spawn_main_pool_base + offset);
        slot->player_pickup_flags[player_index] = 0;
        offset += SPAWN_SLOT_SIZE;
    }
}

// ============================================================================
// spawn_find_entity_by_type_id - 0x80093d94
// Searches both main and extended spawn pools for a slot with type==0x200
// and matching level_id. Returns entity pointer or NULL.
// Called by trigger_tick_update (e.g. "has player picked up item X?").
// ============================================================================
Entity* spawn_find_entity_by_type_id(int target_id)
{
    // Search main pool first
    int offset = 0;
    int count = g_spawn_main_pool_count;
    for (int i = 0; i < count; i++) {
        SpawnSlot* slot = (SpawnSlot*)(g_spawn_main_pool_base + offset);
        if (slot->spawn_type == 0x200 && slot->level_id == target_id) {
            return slot->entity;
        }
        offset += SPAWN_SLOT_SIZE;
    }

    // Search extended pool
    for (int i = 0; i < 30; i++) {
        if (g_spawn_ext_pool_active[i] != 0 &&
            g_spawn_ext_pool_slots[i].spawn_type == 0x200 &&
            g_spawn_ext_pool_slots[i].level_id == target_id) {
            return g_spawn_ext_pool_slots[i].entity;
        }
    }
    return NULL;
}

// ============================================================================
// spawn_detach_linked_entity - 0x80093e3c
// For spawn types 0x21/0x20 with flag bit 0 set: detaches the linked
// sub-entity (e.g. weapon model on a rack) and clears the reference.
// Called during pickup/interaction cleanup.
// ============================================================================
int spawn_detach_linked_entity(Entity* ent)
{
    SpawnSlot* slot = (SpawnSlot*)ent->spawn_slot;
    if ((slot->spawn_flags_hi == 0x21 || slot->spawn_flags_hi == 0x20) &&
        (ent->vis_flags & 1) != 0 &&
        ent->attached_entity != NULL) {
        ent->attached_entity->link_ref = 0;
        detach_entity_from_parent();  // FUN_800b1e7c
        ent->attached_entity = NULL;
    }
    return 1;
}

// ============================================================================
// spawn_on_destroy - 0x80093eac
// Destroy handler for spawn entities. Clears extended pool active flag,
// deactivates associated sound pad entities, handles type-2 special cleanup.
// Registered as entity destroy callback.
// ============================================================================
void spawn_on_destroy(Entity* ent)
{
    if (ent == NULL) return;
    SpawnSlot* slot = (SpawnSlot*)ent->spawn_slot;
    if (slot == NULL) return;

    // Detach any linked sub-entity
    if (slot->particle_handle != 0) {
        // Clear particle entity link reference
        ((Entity*)slot->particle_handle)->link_ref = 0;
        slot->particle_handle = 0;
        detach_entity_from_parent();  // FUN_800b1e7c
    }

    // Extended pool cleanup
    if ((slot->spawn_flags_hi & 1) != 0) {
        g_spawn_ext_pool_active[slot->pool_index] = 0;

        // Deactivate sound entity (sound handle A)
        int sound_a = slot->sound_handle_a;
        bool in_range = false;
        if (sound_a == -1) {
            sound_a = slot->sound_handle_ambient;
            if (sound_a > 0x209d && sound_a < 0x20b2) {
                in_range = true;
            }
            if (in_range) {
                soundpad_deactivate_entity(sound_a);
            }
        } else {
            if (sound_a > 0x209d && sound_a < 0x20b2) {
                in_range = true;
            }
            if (in_range) {
                soundpad_deactivate_entity(sound_a);
            }
            // Type 2 (timed pickup): additional cleanup
            if (slot->spawn_type == 2) {
                spawn_type2_cleanup();  // FUN_802bc19c
            }
        }
        slot->sound_handle_ambient = -1;
        slot->sound_handle_a = -1;
    }

    // Clear entity back-reference
    slot->entity = NULL;
}

// ============================================================================
// spawn_on_spawn_position_setup - 0x80093fac
// On-spawn callback: applies initial position/rotation from spawn slot
// definition data. Handles flag-based position override and type-specific
// Y offset (type 0x10 gets extra height). Sets render flag bits.
// 2 callers (spawn tick dispatchers).
// ============================================================================
void spawn_on_spawn_position_setup(Entity* ent)
{
    SpawnSlot* slot = (SpawnSlot*)ent->spawn_slot;
    ent->vis_flags |= 99;  // 0x63: visible + shadow + collision

    if (slot == NULL) return;
    if (ent->entity_flags != 0x40) return;

    // Flag 8: override position from slot definition
    if ((slot->spawn_props_flags & 8) != 0) {
        ent->pos_x = slot->def_pos_x;
        ent->pos_y = slot->def_pos_y;
        ent->pos_z = slot->def_pos_z;
        ent->prev_pos_x = ent->velocity_x;
        ent->prev_pos_y = ent->velocity_y;
        ent->prev_pos_z = ent->velocity_z;
        ent->rotation_x = slot->def_rot_x;
        ent->rotation_y = slot->def_rot_y;
        ent->rotation_z = slot->def_rot_z;
        slot->spawn_props_flags &= ~8;  // clear flag after use
    }

    // Non-type-1: clear state counter
    if (slot->spawn_type != 1) {
        slot->state = 0;
    }

    // Types without extended pool flag: apply position from def
    if ((slot->spawn_props_flags & 1) == 0) {
        int type = slot->spawn_type;
        if (type == 1 || type == 2 || type == 8 || type == 0x10 || type == 0x40) {
            float y_pos = slot->def_pos_y;
            float z_pos = slot->def_pos_z;

            // Type 0x10: elevate slightly
            if (slot->spawn_type == 0x10) {
                y_pos += Y_OFFSET_TYPE_10;
            }

            ent->pos_x = slot->def_pos_x;
            ent->pos_y = y_pos;
            ent->pos_z = z_pos;
            ent->prev_pos_x = ent->velocity_x;
            ent->prev_pos_y = ent->velocity_y;
            ent->prev_pos_z = ent->velocity_z;
            ent->rotation_x = slot->def_rot_x;
            ent->rotation_y = slot->def_rot_y;
            ent->rotation_z = slot->def_rot_z;

            // Non-story mode: check player active state
            if (g_game_mode != 10) {
                if (g_player_active_flags[ent->player_index] != 0) {
                    spawn_apply_room_assignment();  // FUN_80098380
                }
            }
        }
    }
}

// ============================================================================
// spawn_set_collision_active - 0x80094140
// Enables full collision on a spawn entity: clears invisible flag bit 0
// and sets collision radius to maximum (0x7f all channels).
// Called by spawn_entity_tick_update when flag 4 is set.
// ============================================================================
void spawn_set_collision_active(Entity* ent)
{
    ent->vis_flags &= ~1;  // clear bit 0 (was hidden)
    collision_set_radius_and_flags(0.0f, ent->render_instance, 0x7f, 0x7f, 0x7f);
}

// ============================================================================
// spawn_lifetime_tick - 0x80094180
// Per-frame lifetime management: decrements lifetime counter, progressively
// fades collision shape opacity as lifetime expires, destroys entity when
// counter reaches zero. Type 0x400 gets special cleanup before destroy.
// 2 callers (spawn tick dispatchers for despawning items).
// ============================================================================
int spawn_lifetime_tick(Entity* ent)
{
    SpawnSlot* slot = (SpawnSlot*)ent->spawn_slot;

    if (ent->despawn_flag == 0) return 0;

    // Decrement lifetime each frame
    ent->despawn_timer -= g_frame_delta_ticks;

    // Compute fade factor: opacity decreases as timer approaches zero
    float total_lifetime = DESPAWN_TIME_SCALE * (float)g_spawn_lifetime_base;
    float remaining = (float)ent->despawn_timer;

    if (remaining <= total_lifetime) {
        // Fade collision shape opacity
        u8 opacity = (u8)(int)((FADE_CURVE_SCALE * remaining) / total_lifetime);
        collision_set_shape_params(ent->render_instance, 0xf, -1, 0xff, opacity);

        // Type 2 (timed): also fade render data
        if (slot->spawn_type == 2) {
            int render_entry = g_render_table_base + slot->render_index * 0x20;
            *(u32*)(render_entry + 4) &= ~0x200000;  // clear visible bit
        }
    }

    // Timer expired: destroy
    if (ent->despawn_timer < 1) {
        if (slot->spawn_type == 0x400) {
            spawn_cleanup_special_type(ent);  // FUN_802be43c
        }
        entity_destroy_with_event(ent);
        return 1;
    }
    return 0;
}

// ============================================================================
// spawn_particle_trail_tick - 0x800942b4
// Manages particle/smoke trail effect for spawn entities (types 0x100, 0x1000).
// Sets collision to non-interactive, manages particle lifetime via decay timer,
// spawns particle system at entity position with random velocity offset.
// Fades particle opacity over time.
// Called by spawn_entity_tick_update.
// ============================================================================
void spawn_particle_trail_tick(Entity* ent)
{
    SpawnSlot* slot = (SpawnSlot*)ent->spawn_slot;

    // Set collision to non-interactive (pickup, no physics)
    static const float collision_def[3] = { /* default collision dims */ };
    collision_set_shape_params(ent->render_instance, 0xf, -1, 2, 0);

    // Decay timer management
    if (g_frame_delta_ticks != 0) {
        slot->trail_decay -= TRAIL_DECAY_RATE / (float)g_spawn_lifetime_base;
        if (slot->trail_decay < 0.0f) {
            // Random re-seed trail phase
            u32 seed = get_random_seed();
            slot->trail_decay = TRAIL_PHASE_SCALE *
                ((float)seed / RAND_MAX_F) + TRAIL_PHASE_BASE;

            // Kill existing particle if handle valid
            if (slot->particle_effect != NULL) {
                particle_set_opacity(0.0f, slot->particle_effect);
            }
        }

        // Check if particle effect still alive
        ParticleEffect* fx = slot->particle_effect;
        if (fx != NULL) {
            if (fx->lifetime < TRAIL_MIN_LIFETIME || (slot->spawn_props_flags & 2) != 0) {
                particle_set_opacity(0.0f, fx);
                fx->active = 0;
                slot->particle_effect = NULL;
            }
        }

        // Player not active in room: kill particle
        if (g_player_active_flags[ent->player_index] == 0 ||
            (slot->spawn_props_flags & 2) != 0) {
            if (slot->particle_effect != NULL) {
                particle_set_opacity(0.0f, slot->particle_effect);
            }
        } else if (slot->particle_effect == NULL) {
            // Spawn new particle trail at entity position + offset
            float pos[3];
            pos[0] = TRAIL_OFFSET_X + ent->pos_x + ent->prev_pos_x;
            pos[1] = TRAIL_OFFSET_Y + ent->pos_y + ent->prev_pos_y;
            pos[2] = TRAIL_OFFSET_Z + ent->pos_z + ent->prev_pos_z;

            // Random velocity
            float vel[3];
            u32 r1 = get_random_seed();
            vel[0] = TRAIL_VEL_SCALE * ((float)r1 / RAND_MAX_F - 0.5f);
            u32 r2 = get_random_seed();
            vel[1] = TRAIL_VEL_SCALE * ((float)r2 / RAND_MAX_F - 0.5f);
            u32 r3 = get_random_seed();
            vel[2] = TRAIL_VEL_SCALE * ((float)r3 / RAND_MAX_F - 0.5f);

            slot->particle_effect = particle_spawn(
                slot->particle_effect_handle, 0x5f, 0x20080000,
                pos, vel, collision_def, NULL);
            particle_set_opacity(TRAIL_OPACITY, slot->particle_effect);

            // Set color from per-type table
            slot->particle_effect->color_index =
                g_spawn_trail_colors[slot->category_a];
        }
    }
}

// ============================================================================
// spawn_entity_tick_update - 0x8009459c
// Main per-frame update for all spawn entities. Dispatches to:
// - spawn_particle_trail_tick (types 0x100, 0x1000)
// - spawn_set_collision_active (flag 4)
// - Y-axis spin rotation (story mode or flag 1 + non-0x400)
// - Gravity bobbing (model flag bit 1: constant spin)
// - Sine-wave Y oscillation (model flag bit 2: bob up/down)
// - Type 0x800: paired entity sine bob
// - Material fade for visible entities
// Registered via entity_register_for_update in spawn_create_instance.
// ============================================================================
void spawn_entity_tick_update(void)
{
    Entity* ent = get_current_update_entity();  // FUN_803826f0
    SpawnSlot* slot = (SpawnSlot*)ent->spawn_slot;
    SpawnModelDef* model_def = spawn_get_model_def(slot->spawn_type, slot->level_id);

    // Particle trail for collectible types
    if (slot->spawn_type == 0x100 || slot->spawn_type == 0x1000) {
        spawn_particle_trail_tick(ent);
    }

    // Enable collision if flagged
    if ((slot->spawn_props_flags & 4) != 0) {
        spawn_set_collision_active(ent);
    }

    // Y-axis rotation (spin pickup)
    if (g_game_mode == 10 ||
        ((slot->spawn_props_flags & 1) != 0 && slot->spawn_type != 0x400)) {
        // Velocity-based Y rotation (story mode pickups rotate)
        if (ent->angular_vel_y != 0.0f) {
            for (int i = 0; i < g_frame_delta_ticks; i++) {
                ent->rotation_y += ent->angular_vel_y;
                ent->angular_vel_y = -(SPIN_DAMPING * ent->angular_vel_y
                                       - ent->angular_vel_y);
            }
            if (fabsf(ent->angular_vel_y) < SPIN_MIN_THRESHOLD) {
                ent->angular_vel_y = 0.0f;
            }
        }
    } else {
        // Constant spin (arcade mode items)
        if ((model_def->anim_flags & 2) != 0) {
            ent->rotation_y += SPIN_SPEED * g_physics_timestep;
            if (ent->rotation_y >= TWO_PI) {
                ent->rotation_y -= TWO_PI;
            }
        }

        // Paired entity spin (type 0x800)
        if (slot->spawn_type == 0x800) {
            Entity* paired = slot->paired_entity;
            paired->rotation_y += SPIN_SPEED * g_physics_timestep;
            if (paired->rotation_y >= TWO_PI) {
                paired->rotation_y -= TWO_PI;
            }
            // Sine-wave Y bob for paired entity
            float phase = SINE_FREQ * (float)g_spawn_frame_counter +
                          SINE_PHASE_SCALE * slot->bob_phase;
            int sine_index = (int)(phase + SINE_TABLE_OFFSET) & 0x7ff;
            paired->pos_y = SINE_AMPLITUDE * g_sine_table[sine_index]
                           + 0.0f + slot->def_pos_y;
        }

        // Sine-wave Y bob (model flag bit 2)
        if ((model_def->anim_flags & 4) != 0) {
            float phase = SINE_FREQ * (float)g_spawn_frame_counter +
                          SINE_PHASE_SCALE * slot->bob_phase;
            int sine_index = (int)(phase + SINE_TABLE_OFFSET) & 0x7ff;
            ent->pos_y = SINE_AMPLITUDE * g_sine_table[sine_index]
                        + slot->def_pos_y;
        }
    }

    // Material alpha fade for visible entities
    if ((ent->vis_flags & 1) != 0 && (ent->vis_flags & 0x100) == 0) {
        float alpha = get_entity_alpha(ent);
        if (alpha != 0.0f) {
            int submesh_count = get_submesh_count(ent->render_instance);
            for (int i = 0; i < submesh_count; i++) {
                set_submesh_alpha(alpha, alpha, alpha);
            }
        }
    }

    restore_paired_single_state();  // FUN_8038273c
}
