// =============================================================================
// Player AI Pool Management
// Address Range: 0x800b1938 - 0x800b1c38
// Date: 2026-08-11
// Batch: 257
//
// Manages the 85-slot AI player pool used for bots in arcade/multiplayer.
// Each slot is 0x22C bytes. The pool is partitioned into 4 groups (teams).
// Provides allocation, reset, teardown, particle/sound helpers, and score access.
// =============================================================================

#include "common.h"
#include "ai/player_ai.h"
#include "entity/entity.h"
#include "particle/particle.h"
#include "sound/sound.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define AI_POOL_SLOT_COUNT  85
#define AI_POOL_SLOT_SIZE   0x22C
#define AI_POOL_TOTAL_SIZE  (AI_POOL_SLOT_COUNT * AI_POOL_SLOT_SIZE) // 0xB8A0
#define AI_PARTITION_COUNT  4

// ---------------------------------------------------------------------------
// Globals (BSS)
// ---------------------------------------------------------------------------
static u8* g_ai_pool_base;          // DAT_80611dc0
static s32 g_ai_active_count;       // DAT_80611db4
static s32 g_ai_total_spawned;      // DAT_80611dbc
static s32 g_ai_total_freed;        // DAT_80611db8
static s32 g_ai_total_allocated;    // DAT_80611db0
static s32 g_ai_initialized;        // DAT_806107b8
static s32 g_ai_frame_counter;      // DAT_80611dc4

// Per-partition indices (4 partitions / teams)
static s32 g_partition_start[AI_PARTITION_COUNT];    // DAT_805642f8
static s32 g_partition_end[AI_PARTITION_COUNT];      // DAT_80564308
static s32 g_partition_head[AI_PARTITION_COUNT];     // DAT_80564318
static s32 g_partition_tail[AI_PARTITION_COUNT];     // DAT_80564328

// Partition sizes loaded from ROM data
extern s32 g_partition_sizes[AI_PARTITION_COUNT];    // DAT_80405484

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
extern void player_ai_slot_reset(void);
extern void player_ai_particle_defs_init(void);
extern void player_ai_get_set_raycast_mode(s32 mode);
extern void ai_pool_lock(void);        // FUN_803826ec
extern void ai_pool_unlock(void);      // FUN_80382738
extern s32  ai_pool_get_slot(void);    // FUN_803826e8
extern void ai_pool_dec_count(void);   // FUN_80382734

// ---------------------------------------------------------------------------
// Address: 0x800b1938 | Name: player_ai_pool_reset
//
// Full pool reset: links all 85 slots into a doubly-linked free list,
// resets per-slot state, initializes 4 team partitions from ROM data,
// and sets up behavior/particle subsystems. Called at level load.
// ---------------------------------------------------------------------------
void player_ai_pool_reset(void)
{
    s32 i;
    s32 offset;
    s32 partition_start_idx;

    ai_pool_lock();

    // Build doubly-linked free list across all slots
    i = 0;
    offset = 0;
    do {
        *(s32*)(g_ai_pool_base + offset + 0x10) = i + 1;  // next
        *(s32*)(g_ai_pool_base + offset + 0x14) = i - 1;  // prev
        player_ai_slot_reset();
        *(s32*)(g_ai_pool_base + offset) = -1;             // entity ID = none
        i++;
        offset += AI_POOL_SLOT_SIZE;
    } while (i < AI_POOL_SLOT_COUNT);

    // Initialize 4 team partitions from ROM-defined sizes
    partition_start_idx = 0;
    for (i = 0; i < AI_PARTITION_COUNT; i++) {
        g_partition_start[i] = partition_start_idx;
        partition_start_idx += g_partition_sizes[i];
        g_partition_end[i] = partition_start_idx - 1;

        // Head/tail point to partition boundaries initially
        g_partition_head[i] = g_partition_start[i];
        g_partition_tail[i] = g_partition_end[i];

        // Terminate linked list at partition boundaries
        *(s32*)(g_ai_pool_base + g_partition_head[i] * AI_POOL_SLOT_SIZE + 0x14) = -1;
        *(s32*)(g_ai_pool_base + g_partition_tail[i] * AI_POOL_SLOT_SIZE + 0x10) = -1;
    }

    g_ai_initialized = 1;
    g_ai_active_count = 0;
    g_ai_total_spawned = 0;
    g_ai_total_freed = 0;
    g_ai_total_allocated = 0;

    player_ai_particle_defs_init();
    g_ai_frame_counter = 0;
    player_ai_get_set_raycast_mode(0);

    ai_pool_unlock();
}

// ---------------------------------------------------------------------------
// Address: 0x800b1a6c | Name: player_ai_pool_alloc
//
// Allocates the AI pool buffer (0xB8A0 bytes = 85 * 0x22C) from the game
// heap, then performs a full reset. Called once during system init.
// ---------------------------------------------------------------------------
void player_ai_pool_alloc(void)
{
    g_ai_pool_base = (u8*)mem_alloc(AI_POOL_TOTAL_SIZE, 1);
    player_ai_pool_reset();
}

// ---------------------------------------------------------------------------
// Address: 0x800b1aa0 | Name: player_ai_pool_stub
//
// Empty no-op stub. Registered as a placeholder callback.
// ---------------------------------------------------------------------------
void player_ai_pool_stub(void)
{
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x800b1aa4 | Name: player_ai_particle_stop_fade
//
// Fades out a particle effect attached to an AI slot. Sets opacity to fade
// value, clears the active flag, and nulls the pointer.
// Used when an AI behavior ends gracefully (7 call sites in combat code).
//
// param_1: AI slot base pointer
// param_2: particle slot index (0-14)
// ---------------------------------------------------------------------------
void player_ai_particle_stop_fade(u8* slot_base, s32 particle_idx)
{
    s32 offset = (s32)slot_base + particle_idx * 4;
    void* particle = *(void**)(offset + 0xAC);

    if (particle != NULL) {
        particle_slot_set_opacity(PARTICLE_FADE_OPACITY, particle);
        *(s32*)((s32)particle + 0x14) = 0;  // clear active flag
        *(void**)(offset + 0xAC) = NULL;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800b1af4 | Name: player_ai_particle_destroy
//
// Immediately destroys a particle effect attached to an AI slot.
// Used during AI slot destruction (hard cleanup).
//
// param_1: AI slot base pointer
// param_2: particle slot index (0-14)
// ---------------------------------------------------------------------------
void player_ai_particle_destroy(u8* slot_base, s32 particle_idx)
{
    s32 offset = (s32)slot_base + particle_idx * 4;
    void* particle = *(void**)(offset + 0xAC);

    if (particle != NULL) {
        weather_slot_destroy(particle);
        *(void**)(offset + 0xAC) = NULL;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800b1b38 | Name: player_ai_sound_stop_and_clear
//
// Stops a looping sound and releases the sound handle at the given slot index.
// Sound handles are stored at slot+0xE8 (2 slots per AI entity).
//
// param_1: AI slot base pointer
// param_2: sound slot index (0-1)
// ---------------------------------------------------------------------------
void player_ai_sound_stop_and_clear(u8* slot_base, s32 sound_idx)
{
    s32 handle = *(s32*)((s32)slot_base + sound_idx * 4 + 0xE8);
    sound_stop(handle);
    sound_handle_release((u32*)((s32)slot_base + sound_idx * 4 + 0xE8));
}

// ---------------------------------------------------------------------------
// Address: 0x800b1b8c | Name: player_ai_sound_release_handle
//
// Releases (destroys) a sound handle at the given slot index without stopping.
// Used during AI slot teardown after the sound has already been stopped.
//
// param_1: AI slot base pointer
// param_2: sound slot index (0-1)
// ---------------------------------------------------------------------------
void player_ai_sound_release_handle(u8* slot_base, s32 sound_idx)
{
    s32 handle = *(s32*)((s32)slot_base + sound_idx * 4 + 0xE8);
    sound_handle_destroy(handle);
}

// ---------------------------------------------------------------------------
// Address: 0x800b1bc0 | Name: player_ai_get_team_score
//
// Returns the team score for an AI slot. Checks:
// 1. If slot has a linked player entity (+0x104), reads score at entity+0xE8C
// 2. Else if slot has an item entity (+0x108) of type 0x200, reads score
//    from item's extended data (+0x7C->+0xD4)
// 3. Returns 0 if neither path resolves.
// ---------------------------------------------------------------------------
s32 player_ai_get_team_score(u8* slot)
{
    s32 player_entity = *(s32*)(slot + 0x104);
    if (player_entity != 0) {
        return *(s32*)(player_entity + 0xE8C);
    }

    s32 item_entity = *(s32*)(slot + 0x108);
    if (item_entity != 0 && *(s32*)(item_entity + 0x20) == 0x200) {
        return *(s32*)(*(s32*)(item_entity + 0x7C) + 0xD4);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x800b1c00 | Name: player_ai_set_team_score
//
// Sets the team score for an AI slot. Same entity resolution as get_team_score.
// ---------------------------------------------------------------------------
void player_ai_set_team_score(u8* slot, s32 score)
{
    s32 player_entity = *(s32*)(slot + 0x104);
    if (player_entity != 0) {
        *(s32*)(player_entity + 0xE8C) = score;
        return;
    }

    s32 item_entity = *(s32*)(slot + 0x108);
    if (item_entity == 0) {
        return;
    }
    if (*(s32*)(item_entity + 0x20) != 0x200) {
        return;
    }
    *(s32*)(*(s32*)(item_entity + 0x7C) + 0xD4) = score;
}

// ---------------------------------------------------------------------------
// Address: 0x800b1c38 | Name: player_ai_slot_free
//
// Frees an active AI slot. Full teardown:
// - Calls destruction callback if set (+0x154)
// - Clears weapon link (+0x204)
// - Clears team score self-reference
// - Destroys linked entities: weapon (+0xA8), attachment1 (+0x190), attachment2 (+0x18C)
// - Destroys all 15 particle effects
// - Releases both sound handles
// - Unlinks from the doubly-linked active list
// - Updates partition head/tail pointers
// ---------------------------------------------------------------------------
void player_ai_slot_free(void)
{
    s32 slot_index = ai_pool_get_slot();
    u8* slot = g_ai_pool_base + slot_index * AI_POOL_SLOT_SIZE;

    s32 next = *(s32*)(slot + 0x10);
    s32 prev = *(s32*)(slot + 0x14);

    // Clear weapon link
    if (*(void**)(slot + 0x204) != NULL) {
        **(s32**)(slot + 0x204) = 0;
        *(s32*)(slot + 0x204) = 0;
    }

    // Call destruction callback
    if (*(void**)(slot + 0x154) != NULL) {
        ((void(*)(u8*))(*(void**)(slot + 0x154)))(slot);
        *(s32*)(slot + 0x154) = 0;
    }

    // Clear team score self-reference
    s32 team_score = player_ai_get_team_score(slot);
    if (team_score == (s32)slot) {
        player_ai_set_team_score(slot, 0);
    }

    // Destroy linked weapon entity
    s32 weapon_entity = *(s32*)(slot + 0xA8);
    if (weapon_entity != 0) {
        if (*(s32*)(weapon_entity + 0x20) == 0x40) {
            *(s32*)(*(s32*)(weapon_entity + 0x7C) + 0xBC) = 0;
        }
        entity_destroy_with_event(*(void**)(slot + 0xA8));
        *(s32*)(slot + 0xA8) = 0;
    }

    // Destroy attachment entities
    if (*(void**)(slot + 0x190) != NULL) {
        entity_destroy_with_event(*(void**)(slot + 0x190));
        *(s32*)(slot + 0x190) = 0;
    }
    if (*(void**)(slot + 0x18C) != NULL) {
        entity_destroy_with_event(*(void**)(slot + 0x18C));
        *(s32*)(slot + 0x18C) = 0;
    }

    // Destroy all 15 particle effects
    for (s32 i = 0; i < 15; i++) {
        player_ai_particle_destroy(slot, i);
    }

    // Release both sound handles
    for (s32 i = 0; i < 2; i++) {
        player_ai_sound_release_handle(slot, i);
    }

    // Unlink from doubly-linked active list
    if (next < 0) {
        g_partition_tail[slot_index] = prev;
    } else {
        *(s32*)(g_ai_pool_base + next * AI_POOL_SLOT_SIZE + 0x14) = prev;
        *(s32*)(slot + 0x14) = -1;
    }

    if (prev < 0) {
        g_partition_head[slot_index] = next;
    } else {
        *(s32*)(g_ai_pool_base + prev * AI_POOL_SLOT_SIZE + 0x10) = next;
        *(s32*)(slot + 0x10) = -1;
    }

    ai_pool_dec_count();
}
