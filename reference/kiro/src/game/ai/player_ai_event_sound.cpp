// =============================================================================
// player_ai_event_sound.cpp - AI Event Sound Dispatch & Character Defs
// Address Range: 0x800b0b9c - 0x800b18cc
// Date: 2026-08-11
// Batch: 256
// =============================================================================
//
// AI event-driven sound dispatch (explosion, footsteps, ricochet, debris, fire,
// sparks, blood, pain voice), camera shake, nametag HUD rendering, ambient
// sound sequence tick, character definition table accessors, and AI slot reset.
//
// Key subsystems:
//   - Explosion camera shake (distance-attenuated per-player)
//   - Event sound dispatch (switch on events -0x6D to -0x64)
//   - Ambient sound sequence tick (duration-based advance)
//   - Nametag HUD (GX FIFO direct vertex submission)
//   - Character def table (DAT_804045a8/bc, 0x40 stride)
//   - AI slot reset (0x22C clear preserving linked list)
//
// =============================================================================

#include "common.h"
#include "player_ai.h"
#include "sound.h"
#include "entity.h"

// External data
extern int DAT_80611c24;
extern int DAT_80611d78;
extern int DAT_80611928;
extern int DAT_80611da0;
extern int DAT_80612610;
extern int DAT_80612a9c;
extern int DAT_8060fa8c;
extern u32 DAT_805642e8;
extern u32 DAT_805642ec;
extern u32 DAT_805642f0;
extern u32* DAT_80611748;
extern volatile float DAT_cc008000;  // GX Command Processor FIFO

extern float FLOAT_80614904;
extern float FLOAT_806149e8;
extern float FLOAT_806149ec;
extern float FLOAT_806149f0;
extern float FLOAT_806149f4;
extern float FLOAT_806149f8;
extern float FLOAT_806149fc;
extern float FLOAT_80614a00;
extern float FLOAT_80614a04;
extern float FLOAT_80614a10;
extern float FLOAT_80614a14;
extern float FLOAT_80614a18;
extern float FLOAT_80614a1c;
extern float FLOAT_80614a20;
extern float FLOAT_80614a24;
extern float FLOAT_80614a28;
extern float FLOAT_80614a2c;
extern float FLOAT_80614a30;
extern float FLOAT_80614a34;
extern float FLOAT_80614a38;
extern float FLOAT_80614a3c;
extern float FLOAT_80614a40;
extern float FLOAT_80614a44;
extern float FLOAT_80614a48;
extern float FLOAT_80614a58;
extern float FLOAT_80611718;
extern double DOUBLE_80614a08;
extern double DOUBLE_80614a50;

extern u8 DAT_804045a8[];  // Character def table - name strings (0x40 stride)
extern u8 DAT_804045bc[];  // Character def table - flags (0x40 stride)
extern u8 DAT_803cc478[];  // Default particle direction
extern void* PTR_DAT_804053f8[];  // Per-type sound table pointers

// Forward declarations
extern int  FUN_803826f0(void);
extern void FUN_8038273c(void);
extern int  FUN_8013104c(void);
extern int  FUN_80026c54(int, int);
extern float FUN_80026d50(int);
extern void FUN_80144520(double, int, int, void*);
extern void FUN_802c4894(float*);
extern void FUN_8016f00c(int);
extern void FUN_8016e8ac(int);
extern void FUN_8001d4b4(void);
extern void FUN_8001d4d4(void);
extern void FUN_80172a4c(int, int, int, int, int);
extern void FUN_80172c54(void);
extern void FUN_8016f4bc(int, int, int, int, int);

extern int  sprite_measure_text_region_int(void*, void*, int*, void*, int);
extern void player_apply_camera_shake(double, double, double, int);
extern void player_ai_explosion_spawn(int, int, float*, float*, int, int, int);
extern void player_ai_play_voice_sound(int*, int);
extern void player_ai_ambient_sound_stop(int*);
extern int  player_ai_event_sound_dispatch(void);
extern int  sound_is_position_in_range(double, float*);
extern void sound_init_play_params(int*);
extern void sound_play_request(int*);
extern void sound_register_fade_out(double, double, int*, void*);
extern void sound_play_ambient_3d(double, double);
extern int* entity_get_world_position(int);
extern void memset_wrapper(int, int, int);

// =============================================================================
// Address: 0x800b0b9c | player_ai_explosion_camera_shake
// Distance-attenuated explosion camera shake for all human players within
// radius. Computes inverse-square falloff from explosion position to each
// player. Applies camera shake intensity + rumble duration/intensity.
// Rumble stored at player+0xE54 (duration) and +0xE58 (intensity).
// =============================================================================
void player_ai_explosion_camera_shake(float radius, float intensity) {
    float* explosion_pos = (float*)FUN_803826f0();
    float radius_sq = radius * radius;
    float falloff_scale = intensity / radius_sq;

    int player_offset = 0;
    for (int i = 0; i < DAT_80612610; i++) {
        int player_slot = DAT_80611d78 + player_offset;

        if (*(int*)(player_slot + 4) >= 0) {
            int entity = *(int*)(player_slot + 0x14);

            // Distance from explosion to player
            float dx = explosion_pos[0] - *(float*)(entity + 0x64);
            float dy = explosion_pos[1] - *(float*)(entity + 0x68);
            float dz = explosion_pos[2] - *(float*)(entity + 0x6C);
            float dist_sq = dx*dx + dy*dy + dz*dz;

            if (dist_sq < radius_sq) {
                // Inverse-square attenuation
                float atten = falloff_scale * (radius_sq - dist_sq);
                if (atten > FLOAT_806149e8) {
                    atten = FLOAT_806149e8;  // Clamp max
                }

                // Apply camera shake (pitch, intensity, duration)
                player_apply_camera_shake(
                    (double)(FLOAT_806149f0 * atten),
                    (double)atten,
                    (double)(FLOAT_806149f4 * atten),
                    player_slot
                );

                // Set rumble
                *(float*)(DAT_80611d78 + player_offset + 0xE54) = FLOAT_806149f8;
                float rumble = atten;
                if (rumble > FLOAT_806149fc) {
                    rumble = FLOAT_806149fc;
                }
                *(float*)(DAT_80611d78 + player_offset + 0xE58) = rumble;
            }
        }

        player_offset += 0x141C;
    }
    FUN_8038273c();
}

// =============================================================================
// Address: 0x800b0cd4 | player_ai_delayed_explosion_shake
// Timer callback for delayed explosion camera shake. Validates entity handle
// via generation check (packed handle: upper 10 bits = index, +0x2C = gen).
// If valid, dispatches explosion_camera_shake with configured radius/intensity.
// =============================================================================
void player_ai_delayed_explosion_shake(u32 entity_handle) {
    int entity = 0;

    if (entity_handle != 0) {
        entity = DAT_80611c24 + (entity_handle >> 22) * 0x2E8;
        if (*(u32*)(entity + 0x2C) != entity_handle) {
            entity = 0;
        }
    }

    if (entity != 0) {
        player_ai_explosion_camera_shake(FLOAT_80614a00, FLOAT_806149f0);
    }
}

// =============================================================================
// Address: 0x800b0d38 | player_ai_draw_nametag_hud
// Draws bordered gradient rectangle + text for AI nametag using direct GX FIFO
// writes to 0xCC008000. Submits 12 vertices as 4 quads with gradient colors
// (0x5F5F7F10 edges, 0x5F5F7F7F center). Only renders when timer
// DAT_80611da0 >= DAT_80611928. Uses sprite_measure_text_region_int for bounds.
// =============================================================================
void player_ai_draw_nametag_hud(void) {
    if (DAT_80611928 > DAT_80611da0) return;

    // Text data from last kill event stored in DAT_805642e8..F0
    float text_data[3];
    text_data[0] = *(float*)&DAT_805642e8;
    text_data[1] = *(float*)&DAT_805642ec;
    text_data[2] = *(float*)&DAT_805642f0;

    FUN_8016f00c(2);
    FUN_8016e8ac(0);

    u32 text_width, text_height;
    int result = sprite_measure_text_region_int(text_data, &text_height, (int*)&text_width, NULL, 1);

    if (result == 0) {
        // Direct GX FIFO rendering path
        FUN_8001d4b4();
        FUN_80172a4c(3, 0x82, 0, 0xC, -1);

        // Submit 12 vertices for 4 gradient quads (bordered nametag background)
        // Colors: 0x5F5F7F10 (semi-transparent edge), 0x5F5F7F7F (semi-transparent center)
        // Vertices computed from DAT_80611748 bounds and text region
        // [GX FIFO writes to DAT_cc008000 - position XYZ + color per vertex]
        // ... (12 vertices, 4 quads forming bordered gradient rectangle)

        FUN_80172c54();
        FUN_8001d4d4();
    } else {
        // Fallback: simple filled rectangle
        FUN_8016f4bc(
            DAT_80611748[0], DAT_80611748[5],
            DAT_80611748[1], DAT_80611748[6],
            0x5F5F7F10
        );
    }
}

// =============================================================================
// Address: 0x800b1114 | player_ai_event_sound_dispatch
// MASSIVE event sound dispatcher. Handles events -0x6D through -0x64:
//   -0x6D: Ricochet sound (0x64A) with reduced volume
//   -0x6C: Ricochet variant (0x649)
//   -0x6B: Special ricochet (0x1623) only if DAT_80612a9c set
//   -0x6A: Explosion sound (0x145) + nametag data store + HUD timer set
//   -0x69: Debris particles (random direction + entity offset)
//   -0x67: Fire sound (0x4F8) + delayed explosion shake via timer callback
//   -0x66: Sparks sound (0x4E8/0x4E9 random) + immediate camera shake
//   -0x65: Blood splatter particles (type 0x30/0x24)
//   -0x64: Pain voice (player_ai_play_voice_sound index 1)
//
// Entity type adjusts volume: 0x5E9 turret, 0x5E8 vehicle, 0x637 mounted.
// Position-based 3D sound with range check and velocity-scaled volume.
// =============================================================================
int player_ai_event_sound_dispatch(void) {
    // [Large function - see Ghidra decompilation at 0x800b1114]
    // Returns sound handle or -1 on failure
    // Key behaviors per event:
    //   Footstep/ricochet: 3D positioned with entity-type volume adjustment
    //   Explosion: sound + nametag HUD data + timer for camera shake
    //   Debris: random particle direction from entity position+offset
    //   Fire: sound + delayed explosion shake callback
    //   Sparks: random sound variant + immediate camera shake
    //   Blood: particle spawn via player_ai_explosion_spawn
    //   Pain: voice sound from character sound table

    FUN_803826f0();
    // ... [event switch dispatch] ...
    FUN_8038273c();
    return 0;
}

// =============================================================================
// Address: 0x800b16a8 | player_ai_ambient_sound_tick
// Per-frame ambient sound sequence ticker. Advances to next sound entry (8 bytes
// per entry: [duration_threshold, sound_id]) when duration reached. Fades old
// sound, starts new via player_ai_event_sound_dispatch. Stops when loop
// count at slot+4 reaches 0. Sequence pointer at slot+0 advances by 8 per step.
// =============================================================================
void player_ai_ambient_sound_tick(void) {
    int entity = FUN_803826f0();
    int ai_data = *(int*)(entity + 0x7C);

    if (*(int*)(ai_data + 0xE98) == 0) goto end;

    // Check if sound event is still valid
    int event_id = *(int*)(*(int*)(ai_data + 0xE98) + 8);
    if (!FUN_80026c54(entity, event_id)) goto end;

    // Check duration threshold
    while (true) {
        float elapsed = FUN_80026d50(entity);
        int* slot = *(int**)(ai_data + 0xE98);
        float threshold = *(float*)(*slot + 4);

        if (elapsed < threshold) break;

        // Time to advance - check conditions
        if ((*(u32*)(entity + 0x28) & 1) != 0 || *(s16*)(entity + 0x94) == 0x5F2) {
            int new_handle = player_ai_event_sound_dispatch();
            if (new_handle != -1) {
                // Fade out old sound
                sound_register_fade_out(
                    (double)FLOAT_806149e8,
                    (double)FLOAT_806149ec,
                    (int*)(*(int*)(ai_data + 0xE98) + 0x0C),
                    NULL
                );
                // Store new handle
                *(int*)(*(int*)(ai_data + 0xE98) + 0x0C) = new_handle;
            }
        }

        // Decrement loop count
        int remaining = *(int*)(*(int*)(ai_data + 0xE98) + 4) - 1;
        *(int*)(*(int*)(ai_data + 0xE98) + 4) = remaining;

        if (remaining == 0) {
            player_ai_ambient_sound_stop((int*)(ai_data + 0xE98));
            break;
        }

        // Advance sequence pointer by 8 bytes
        **(int**)(ai_data + 0xE98) += 8;
    }

end:
    FUN_8038273c();
}

// =============================================================================
// Address: 0x800b17a0 | player_ai_play_ambient_3d
// Plays random ambient 3D sound at AI entity position. Skips if entity type
// flags include turret/vehicle (0x20020000). Counts entries in per-type sound
// table (PTR_DAT_804053f8), picks random, plays with configured radius.
// Type flag 0x10000 selects alternate sound set.
// =============================================================================
void player_ai_play_ambient_3d(int param_1, int param_2, u32 type_flags) {
    int ai_state = FUN_803826f0();

    // Skip turret/vehicle entities
    if ((type_flags & 0x20020000) != 0) goto end;

    if (ai_state < 0x23) {
        if ((type_flags & 0x10000) == 0) {
            // Count entries in sound table for this type
            int count = 0;
            while (*(int*)(PTR_DAT_804053f8[ai_state] + count) != 0) {
                count += 4;
            }
            // Pick random and play
            FUN_8013104c();
            sound_play_ambient_3d((double)FLOAT_80614a58, (double)FLOAT_80611718);
        } else {
            // Alternate sound set
            FUN_8013104c();
            sound_play_ambient_3d((double)FLOAT_80614a58, (double)FLOAT_80611718);
        }
    }

end:
    FUN_8038273c();
}

// =============================================================================
// Address: 0x800b1860 | player_ai_char_def_is_female
// Returns bit 1 of character definition flags (gender flag). Table at
// DAT_804045bc with 0x40 stride. Used for voice bank and animation gender
// selection.
// =============================================================================
u32 player_ai_char_def_is_female(int char_index) {
    return (*(u32*)(DAT_804045bc + char_index * 0x40) >> 1) & 1;
}

// =============================================================================
// Address: 0x800b187c | player_ai_char_def_get_flags
// Returns full flags word from character definition table. Table at
// DAT_804045bc with 0x40 stride per entry. Returns 0 if index == -1.
// 21+ callers across damage/combat/voice systems.
// =============================================================================
u32 player_ai_char_def_get_flags(int char_index) {
    if (char_index != -1) {
        return *(u32*)(DAT_804045bc + char_index * 0x40);
    }
    return 0;
}

// =============================================================================
// Address: 0x800b18a4 | player_ai_char_def_get_name
// Returns name string pointer from character definition table. Table at
// DAT_804045a8 with 0x40 stride per entry. Returns 0 if index == -1.
// =============================================================================
const char* player_ai_char_def_get_name(int char_index) {
    if (char_index != -1) {
        return *(const char**)(DAT_804045a8 + char_index * 0x40);
    }
    return NULL;
}

// =============================================================================
// Address: 0x800b18cc | player_ai_slot_reset
// Clears 0x22C-byte AI slot while preserving linked list pointers at +0x10
// (prev) and +0x14 (next). Sets weapon sentinels to -1 at +0x100 and
// +0x130..+0x13C (4 weapon slots). Clears field at +0x12C. Called from
// player_ai_pool_reset and player_ai_slot_recycle.
// =============================================================================
void player_ai_slot_reset(void) {
    int slot = FUN_803826f0();

    // Preserve linked list pointers
    int next = *(int*)(slot + 0x14);
    int prev = *(int*)(slot + 0x10);

    // Clear entire slot
    memset_wrapper(slot, 0, 0x22C);

    // Restore list pointers
    *(int*)(slot + 0x14) = next;
    *(int*)(slot + 0x10) = prev;

    // Set weapon sentinels to -1 (no weapon)
    *(int*)(slot + 0x100) = -1;
    *(int*)(slot + 0x12C) = 0;
    *(int*)(slot + 0x130) = -1;
    *(int*)(slot + 0x134) = -1;
    *(int*)(slot + 0x138) = -1;
    *(int*)(slot + 0x13C) = -1;

    FUN_8038273c();
}
