// =============================================================================
// Player AI Lifecycle & Per-Frame Systems
// Address Range: 0x800b1dd4 - 0x800b24dc
// Date: 2026-08-11
// Batch: 258
//
// Manages AI slot lifecycle (recycle, deactivate), per-frame behavior dispatch,
// post-tick room synchronization, render pass, velocity normalization,
// spawn position lookup, render list building, and light probe gathering.
// =============================================================================

#include "common.h"
#include "ai/player_ai.h"
#include "entity/entity.h"
#include "render/light_probe.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define AI_POOL_SLOT_COUNT   85
#define AI_POOL_SLOT_SIZE    0x22C
#define AI_PLAYER_PARTITION  20   // First 20 slots are player partition
#define AI_BEHAVIOR_STRIDE   0x40
#define AI_MAX_LIGHT_PROBES  3

// ---------------------------------------------------------------------------
// Globals (extern)
// ---------------------------------------------------------------------------
extern u8*  g_ai_pool_base;             // DAT_80611dc0
extern s32  g_ai_initialized;           // DAT_806107b8
extern s32  g_ai_render_list_count;     // DAT_80611dac
extern s32  g_ai_render_active;         // DAT_80612250
extern s32  g_ai_render_frame_idx;      // DAT_80612258
extern s32  g_ai_timeout_threshold;     // DAT_806108e4
extern f32  g_ai_dt_scale;              // FLOAT_806108ec
extern s32* g_ai_render_list[];         // DAT_80564338

// Behavior definition table: 0x40 bytes per entry
// +0x00: partition index
// +0x04: spawn pos X (float)
// +0x08: spawn pos Y (float)
// +0x0C: spawn pos Z (float)
// +0x2C: flags
// +0x34: tick callback
// +0x38: post-tick callback
// +0x3C: render callback
extern u8 g_behavior_defs[];            // DAT_80404590

// Partition head/tail arrays
extern s32 g_partition_head[];          // DAT_80564318
extern s32 g_partition_tail[];          // DAT_80564328

// Audio listener
extern u8* g_audio_listener_ptr;        // DAT_80611d54

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
extern void ai_pool_lock(void);
extern void ai_pool_unlock(void);
extern void ai_pool_lock_render(void);
extern void ai_pool_unlock_render(void);
extern void player_ai_slot_reset(void);
extern void player_ai_slot_free(void);
extern void player_ai_slot_deactivate(void);
extern void vec3_copy(f32* dst, f32* src);
extern f32  vec3_length(f32 x2_y2_z2);
extern void light_probe_priority_insert(s32* list, s32** entry);
extern void light_probe_verify_visibility(f64 a, f64 b, void* list, f32* pos, s32 param, s32 count);

// ---------------------------------------------------------------------------
// Address: 0x800b1dd4 | Name: player_ai_slot_recycle
//
// Recycles an AI slot: frees the old contents, resets the slot data, then
// re-links at the active list head with a new behavior ID.
// Used for bot respawn into an existing slot without full deallocation.
// ---------------------------------------------------------------------------
void player_ai_slot_recycle(void)
{
    s32 behavior_id = ai_pool_lock();
    s32 partition = *(s32*)(g_behavior_defs + behavior_id * AI_BEHAVIOR_STRIDE);
    s32 tail_idx = g_partition_tail[partition];

    player_ai_slot_free();

    s32* slot = (s32*)(g_ai_pool_base + tail_idx * AI_POOL_SLOT_SIZE);

    // Link at head of partition's active list
    *(s32*)(g_ai_pool_base + g_partition_head[partition] * AI_POOL_SLOT_SIZE + 0x14) = tail_idx;
    slot[4] = g_partition_head[partition];  // next = old head
    slot[5] = -1;                          // prev = none (new head)
    g_partition_head[partition] = tail_idx;

    player_ai_slot_reset();
    *slot = behavior_id;
    g_ai_initialized = 1;

    ai_pool_unlock();
}

// ---------------------------------------------------------------------------
// Address: 0x800b1e7c | Name: player_ai_slot_deactivate
//
// Deactivates an AI slot: sets behavior to -1, frees resources, re-links
// into the inactive tail of its partition. 21+ callers (death, despawn, etc).
// ---------------------------------------------------------------------------
void player_ai_slot_deactivate(void)
{
    s32* slot = (s32*)ai_pool_lock_render();
    s32 slot_index = ((s32)slot - (s32)g_ai_pool_base) / AI_POOL_SLOT_SIZE;
    s32 partition = *(s32*)(g_behavior_defs + slot[0] * AI_BEHAVIOR_STRIDE);

    if (slot[0] != -1) {
        slot[0] = -1;
        player_ai_slot_free();

        // Re-link at tail of partition's inactive list
        *(s32*)(g_ai_pool_base + g_partition_tail[partition] * AI_POOL_SLOT_SIZE + 0x10) = slot_index;
        slot[4] = -1;                           // next = none (new tail)
        slot[5] = g_partition_tail[partition];   // prev = old tail
        g_partition_tail[partition] = slot_index;
        g_ai_initialized = 1;
    }

    ai_pool_unlock_render();
}

// ---------------------------------------------------------------------------
// Address: 0x800b1f28 | Name: player_ai_voice_pitch_update
//
// Updates AI voice sound pitch. Compares pitch on channels 8 and 9; if
// channel 9 pitch is less than channel 8, boosts channel 9 and channel 4
// volume. Skipped if slot inactive or flag 0x80000 set.
// ---------------------------------------------------------------------------
void player_ai_voice_pitch_update(s32* slot)
{
    s32 voice_data = slot[0x41];  // +0x104: voice/sound state pointer

    if (voice_data == 0 || slot[0] == -1 || (slot[3] & 0x80000) != 0) {
        return;
    }

    // Update base pitch from animation state
    sound_channel_sync_pitch(2, *(s32*)(voice_data + 8), *(s32*)(voice_data + 0x94));

    f64 pitch_ch8 = sound_get_channel_pitch(8, *(s32*)(voice_data + 8));
    f64 pitch_ch9 = sound_get_channel_pitch(9, *(s32*)(voice_data + 8));

    if (pitch_ch9 < pitch_ch8) {
        sound_set_channel_volume(1.0f, 9, *(s32*)(voice_data + 8));
        sound_set_channel_volume(1.0f, 4, *(s32*)(voice_data + 8));
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800b1fec | Name: player_ai_normalize_velocity
//
// Normalizes the AI velocity vector (slot+0x64/68/6C) with gravity applied
// on Y axis. Stores resulting magnitude at slot+0x88. Used by AI movement
// integration to get a unit direction + speed.
// ---------------------------------------------------------------------------
void player_ai_normalize_velocity(f64 gravity_dt, s32 slot_ptr)
{
    f32 speed = *(f32*)(slot_ptr + 0x88);

    f32 vx = *(f32*)(slot_ptr + 0x64) * speed;
    f32 vy = *(f32*)(slot_ptr + 0x68) * speed + (f32)(gravity_dt * g_ai_dt_scale * (1.0f / g_frame_rate));
    f32 vz = *(f32*)(slot_ptr + 0x6C) * speed;

    f32 magnitude = sqrtf(vx * vx + vy * vy + vz * vz);
    f32 inv_mag = 1.0f / magnitude;

    *(f32*)(slot_ptr + 0x64) = vx * inv_mag;
    *(f32*)(slot_ptr + 0x68) = vy * inv_mag;
    *(f32*)(slot_ptr + 0x6C) = vz * inv_mag;
    *(f32*)(slot_ptr + 0x88) = magnitude;
}

// ---------------------------------------------------------------------------
// Address: 0x800b20c8 | Name: player_ai_behavior_tick
//
// Per-frame AI behavior dispatch. Iterates all 85 slots:
// - Checks timeout counter (deactivates if expired)
// - Saves/restores entity visibility flags (0x60 mask on entity+0x28)
// - Advances per-slot timer
// - Dispatches behavior tick callback from definition table
// ---------------------------------------------------------------------------
void player_ai_behavior_tick(void)
{
    ai_pool_lock();

    for (s32 i = 0, offset = 0; i < AI_POOL_SLOT_COUNT; i++, offset += AI_POOL_SLOT_SIZE) {
        s32* slot = (s32*)(g_ai_pool_base + offset);
        s32 behavior_id = slot[0];

        if (behavior_id == -1) {
            continue;
        }

        // Timeout check: if threshold active and flag 0x20 set
        if (g_ai_timeout_threshold > 0 && (slot[3] & 0x20) != 0) {
            if (slot[0x54] == 0) {
                player_ai_slot_deactivate();
                continue;
            }
            slot[0x54] = 0;
        }

        // Save entity visibility flags before behavior tick
        u32 vis_main = 0, vis_attach1 = 0, vis_attach2 = 0;
        s32 entity_main = 0, entity_a1 = 0, entity_a2 = 0;

        s32 linked_entity = slot[0x42];  // +0x108: linked entity
        if (linked_entity != 0 && *(f32*)&slot[0x26] < 1.0f) {
            if (slot[0] != 0x17 && (slot[3] & 0x100) == 0) {
                vis_main = *(u32*)(linked_entity + 0x28) & 0x60;
                *(u32*)(linked_entity + 0x28) &= ~0x60u;
                entity_main = linked_entity;

                // Handle type 8 (character) sub-entities
                if (*(s32*)(linked_entity + 0x20) == 8) {
                    s32 ext_data = *(s32*)(linked_entity + 0x7C);
                    s32 attach1 = *(s32*)(ext_data + 0x18C);
                    if (attach1 != 0) {
                        vis_attach1 = *(u32*)(attach1 + 0x28) & 0x60;
                        *(u32*)(attach1 + 0x28) &= ~0x60u;
                        entity_a1 = attach1;
                    }
                    s32 attach2 = *(s32*)(ext_data + 0x2BC);
                    if (attach2 != 0) {
                        vis_attach2 = *(u32*)(attach2 + 0x28) & 0x60;
                        *(u32*)(attach2 + 0x28) &= ~0x60u;
                        entity_a2 = attach2;
                    }
                }
            }
        }

        // Advance per-slot timer
        *(f32*)&slot[0x26] += g_ai_dt_scale / g_frame_rate;
        *(s16*)(slot + 6) = 1;  // mark as ticked this frame

        // Dispatch behavior tick callback
        void (*tick_fn)(void) = *(void(**)(void))(g_behavior_defs + behavior_id * AI_BEHAVIOR_STRIDE + 0x34);
        if (tick_fn != NULL) {
            tick_fn();
        }

        // Restore entity visibility flags
        if (entity_main != 0) {
            *(u32*)(entity_main + 0x28) |= vis_main;
            if (*(s32*)(entity_main + 0x20) == 8) {
                s32 ext_data = *(s32*)(entity_main + 0x7C);
                if (entity_a1 != 0 && entity_a1 == *(s32*)(ext_data + 0x18C)) {
                    *(u32*)(entity_a1 + 0x28) |= vis_attach1;
                }
                if (entity_a2 != 0 && entity_a2 == *(s32*)(ext_data + 0x2BC)) {
                    *(u32*)(entity_a2 + 0x28) |= vis_attach2;
                }
            }
        }
    }

    ai_pool_unlock();
}

// ---------------------------------------------------------------------------
// Address: 0x800b22cc | Name: player_ai_post_tick_room_sync
//
// Post-behavior room synchronization. For each active slot, propagates the
// room ID from the linked entity to the slot and to sub-entities (weapon,
// attachments). Then dispatches the post-tick callback.
// ---------------------------------------------------------------------------
void player_ai_post_tick_room_sync(void)
{
    for (s32 i = 0, offset = 0; i < AI_POOL_SLOT_COUNT; i++, offset += AI_POOL_SLOT_SIZE) {
        s32* slot = (s32*)(g_ai_pool_base + offset);
        s32 behavior_id = slot[0];

        if (behavior_id == -1) {
            continue;
        }

        // Sync room ID from source entity (+0x158) to slot (+0xF0) and sub-entities
        s32 source_entity = slot[0x56];  // +0x158
        if (source_entity != 0) {
            slot[0x3C] = *(s32*)(source_entity + 0x34);  // room ID

            s32 weapon = slot[0x2A];  // +0xA8
            if (weapon != 0) {
                *(s32*)(weapon + 0x34) = slot[0x3C];
            }
            s32 attach1 = slot[99];  // +0x18C
            if (attach1 != 0) {
                *(s32*)(attach1 + 0x34) = slot[0x3C];
            }
            s32 attach2 = slot[100];  // +0x190
            if (attach2 != 0) {
                *(s32*)(attach2 + 0x34) = slot[0x3C];
            }
        }

        // Dispatch post-tick callback
        void (*post_tick_fn)(void) = *(void(**)(void))(g_behavior_defs + behavior_id * AI_BEHAVIOR_STRIDE + 0x38);
        if (post_tick_fn != NULL) {
            post_tick_fn();
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800b2398 | Name: player_ai_render_tick
//
// AI render pass. Copies listener position for audio, then dispatches render
// callback for all slots that were ticked this frame. Single caller from
// the main render pipeline.
// ---------------------------------------------------------------------------
void player_ai_render_tick(void)
{
    ai_pool_lock_render();

    // Copy audio listener position
    vec3_copy((f32*)(g_audio_listener_ptr + 0x208), &g_listener_position);

    g_ai_render_active = 1;
    g_ai_render_frame_idx = 0;

    for (s32 i = 0, offset = 0; i < AI_POOL_SLOT_COUNT; i++, offset += AI_POOL_SLOT_SIZE) {
        s32 behavior_id = *(s32*)(g_ai_pool_base + offset);

        if (behavior_id == -1) {
            continue;
        }

        // Only render slots that were ticked this frame
        s16 ticked = *(s16*)((s32*)(g_ai_pool_base + offset) + 6);
        if (ticked != 1) {
            continue;
        }

        // Dispatch render callback
        void (*render_fn)(void) = *(void(**)(void))(g_behavior_defs + behavior_id * AI_BEHAVIOR_STRIDE + 0x3C);
        if (render_fn != NULL) {
            render_fn();
        }
    }

    ai_pool_unlock_render();
}

// ---------------------------------------------------------------------------
// Address: 0x800b2440 | Name: player_ai_get_spawn_position
//
// Gets spawn/origin position (XYZ) from behavior definition table for a
// given behavior ID. Called by the light probe system to get AI entity
// color reference positions.
// ---------------------------------------------------------------------------
void player_ai_get_spawn_position(s32 behavior_id, f32* out_pos)
{
    s32 table_offset = behavior_id * AI_BEHAVIOR_STRIDE;
    out_pos[0] = *(f32*)(g_behavior_defs + table_offset + 0x04);
    out_pos[1] = *(f32*)(g_behavior_defs + table_offset + 0x08);
    out_pos[2] = *(f32*)(g_behavior_defs + table_offset + 0x0C);
}

// ---------------------------------------------------------------------------
// Address: 0x800b246c | Name: player_ai_build_render_list
//
// Builds the renderable AI slot list from the first 20 slots (player
// partition). Includes only slots whose behavior has flag bit 8 set.
// Clears the dirty flag after building.
// ---------------------------------------------------------------------------
void player_ai_build_render_list(void)
{
    g_ai_render_list_count = 0;

    for (s32 i = 0, offset = 0; i < AI_PLAYER_PARTITION; i++, offset += AI_POOL_SLOT_SIZE) {
        s32 behavior_id = *(s32*)(g_ai_pool_base + offset);

        if (behavior_id == -1) {
            continue;
        }

        // Check renderable flag (bit 3) in behavior definition flags
        u32 flags = *(u32*)(g_behavior_defs + behavior_id * AI_BEHAVIOR_STRIDE + 0x2C);
        if ((flags & 8) != 0) {
            g_ai_render_list[g_ai_render_list_count] = (s32*)(g_ai_pool_base + offset);
            g_ai_render_list_count++;
        }
    }

    g_ai_initialized = 0;  // Clear dirty flag
}

// ---------------------------------------------------------------------------
// Address: 0x800b24dc | Name: player_ai_gather_light_probes
//
// Gathers light probes from AI entity pool for lighting calculations.
// Builds render list if dirty, priority-inserts up to 3 probes by distance,
// verifies visibility via scene query, then computes attenuation-scaled
// color from behavior spawn positions. Used by the dynamic lighting system.
// ---------------------------------------------------------------------------
void player_ai_gather_light_probes(f32* camera_pos, s32 scene_ctx, s32 out_probes)
{
    s32* slot_ptr = (s32*)ai_pool_lock();

    // Rebuild render list if dirty
    if (g_ai_initialized != 0) {
        player_ai_build_render_list();
    }

    // Initialize 3 probe slots to invalid (-1)
    s32* probe_list[AI_MAX_LIGHT_PROBES];
    // ... (probe slot initialization)

    f32 max_distance_sq = MAX_PROBE_DISTANCE_SQ;  // FLOAT_80614a58

    // Find closest AI entities and insert into priority list
    bool found_any = false;
    for (s32 i = 0; i < g_ai_render_list_count; i++) {
        s32 slot_addr = (s32)g_ai_render_list[i];
        s32 slot_index = (slot_addr - (s32)g_ai_pool_base) / AI_POOL_SLOT_SIZE;

        // Compute distance squared to camera
        f32 dx = *(f32*)(slot_addr + 0x1C) - camera_pos[0];
        f32 dy = *(f32*)(slot_addr + 0x20) - camera_pos[1];
        f32 dz = *(f32*)(slot_addr + 0x24) - camera_pos[2];
        f32 dist_sq = dx * dx + dy * dy + dz * dz;

        if (dist_sq < max_distance_sq) {
            light_probe_priority_insert(probe_list, slot_index, dist_sq);
            found_any = true;
        }
    }

    // Verify visibility of selected probes
    if (found_any) {
        light_probe_verify_visibility(probe_list, camera_pos, scene_ctx, AI_MAX_LIGHT_PROBES);
    }

    // Write probe slot indices to output
    for (s32 i = 0; i < AI_MAX_LIGHT_PROBES; i++) {
        *(s16*)(slot_ptr[0] + 0x23A + i * 2) = (s16)probe_list[i]->slot_index;
    }

    // Compute attenuation-scaled color for each valid probe
    for (s32 i = 0; i < AI_MAX_LIGHT_PROBES; i++) {
        s32 probe = probe_list[i];
        if (probe->slot_index < 0) {
            continue;
        }

        f32 dist_sq = probe->distance_sq;
        f32 inv_dist = 1.0f;
        if (dist_sq > EPSILON) {
            inv_dist = fast_inv_sqrt(dist_sq);  // Newton-Raphson rsqrt
            probe->distance_sq = 1.0f / inv_dist;
        }

        // Attenuation falloff
        f32 atten = 1.0f;
        f32 dist = probe->distance_sq;
        if (dist > 1.0f) {
            atten = (MAX_PROBE_RANGE - dist) * PROBE_FALLOFF_SCALE;
        }
        atten *= PROBE_INTENSITY;

        // Scale color by spawn position from behavior table
        s32 behavior_id = *(s32*)(g_ai_pool_base + probe->slot_index * AI_POOL_SLOT_SIZE);
        probe->color_r = atten * *(f32*)(g_behavior_defs + behavior_id * AI_BEHAVIOR_STRIDE + 0x04);
        probe->color_g = atten * *(f32*)(g_behavior_defs + behavior_id * AI_BEHAVIOR_STRIDE + 0x08);
        probe->color_b = atten * *(f32*)(g_behavior_defs + behavior_id * AI_BEHAVIOR_STRIDE + 0x0C);

        // Scale existing probe direction by inv_dist
        probe->dir_x *= inv_dist;
        probe->dir_y *= inv_dist;
        probe->dir_z *= inv_dist;
    }

    // Copy probe pointers to output array
    for (s32 i = 0; i < AI_MAX_LIGHT_PROBES; i++) {
        *(s32*)(out_probes + i * 4) = (s32)probe_list[i];
    }

    ai_pool_unlock();
}
