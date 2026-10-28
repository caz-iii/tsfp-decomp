// Player Slot System - Camera/viewport management for up to 6 player slots
// Address Range: 0x8009be4c - 0x8009c2e0
// Date: 2026-08-11
// Batch: 205

#include "common.h"

// ============================================================================
// Globals
// ============================================================================

static void* g_player_slot_pool;       // DAT_80611d5c - base of 6 x 0xFFC-byte player slots
static int   g_player_slot_count;      // DAT_80611d58 - number of allocated slots (max 6)
static int   g_active_player_count;    // DAT_80611d50 - currently active player count
static void* g_player_slot_current;    // DAT_80611d54 - current slot pointer (set on reset)
static int   g_player_system_flags;    // DAT_80611d18

static float g_camera_height_offset;   // FLOAT_80611d48
static float g_camera_fov_default;     // fRam80611d4c
static float g_camera_lerp_speed;      // FLOAT_80611d10
static float g_camera_pitch_min;       // FLOAT_80611d08
static float g_camera_pitch_max;       // FLOAT_80611d0c
static float g_camera_zoom_default;    // FLOAT_80611d64

// World-space camera defaults
static float g_world_camera_x;         // FLOAT_806104a8
static float g_world_camera_y;         // FLOAT_806104ac
static float g_world_camera_z;         // FLOAT_806104b0

// Spawn point table (populated by spawn_point_system_init)
static void* g_spawn_point_table;      // DAT_80611d28 - base of 0xC0-byte spawn entries
static int   g_spawn_point_count;      // DAT_80611d2c - number of entries

#define PLAYER_SLOT_SIZE    0xFFC
#define PLAYER_SLOT_MAX     6
#define PLAYER_POOL_SIZE    (PLAYER_SLOT_SIZE * PLAYER_SLOT_MAX)  // 0x5FF0
#define SPAWN_ENTRY_SIZE    0xC0

// ============================================================================
// Address: 0x8009be4c | player_slot_reset_interpolation
// Resets camera interpolation matrices and blend parameters for a player slot.
// If param_use_lerp is non-zero, uses camera lerp factor instead of defaults.
// ============================================================================
void player_slot_reset_interpolation(int pSlot, int use_lerp)
{
    float fDefault = 0.0f;   // FLOAT_80614678
    float fOne = 1.0f;       // FLOAT_80614680
    float fLerpInit;         // FLOAT_80614684

    // Reset interpolation weights (8 floats at +0x364)
    *(float*)(pSlot + 0x364) = fDefault;
    *(float*)(pSlot + 0x368) = fOne;
    *(float*)(pSlot + 0x36c) = fDefault;
    *(float*)(pSlot + 0x370) = fOne;
    *(float*)(pSlot + 0x374) = fDefault;
    *(float*)(pSlot + 0x378) = fDefault;
    *(float*)(pSlot + 0x37c) = fOne;
    *(float*)(pSlot + 0x380) = fOne;

    // Set blend targets (5 floats at +0x384)
    fLerpInit = 0.5f; // FLOAT_80614684
    if (use_lerp == 0) {
        *(float*)(pSlot + 0x384) = fLerpInit;
        *(float*)(pSlot + 0x388) = fLerpInit;
        *(float*)(pSlot + 0x38c) = fLerpInit;
        *(float*)(pSlot + 0x390) = fLerpInit;
        *(float*)(pSlot + 0x394) = fLerpInit;
    } else {
        *(float*)(pSlot + 0x384) = (float)player_get_camera_lerp_factor(pSlot);
        *(float*)(pSlot + 0x388) = (float)player_get_camera_lerp_factor(pSlot);
        *(float*)(pSlot + 0x38c) = (float)player_get_camera_lerp_factor(pSlot);
        *(float*)(pSlot + 0x390) = (float)player_get_camera_lerp_factor(pSlot);
        *(float*)(pSlot + 0x394) = (float)player_get_camera_lerp_factor(pSlot);
    }

    // Clear position/velocity tracking
    *(u32*)(pSlot + 0x2a8) = 0;
    *(u32*)(pSlot + 0x2ac) = 0;

    // Reset view direction vectors (identity-like)
    *(float*)(pSlot + 0x2e8) = 0.0f;
    *(float*)(pSlot + 0x2ec) = 0.0f;
    *(float*)(pSlot + 0x2f0) = 0.0f;

    // Reset orientation matrix (3x3 identity)
    *(float*)(pSlot + 0x328) = 0.0f;
    *(float*)(pSlot + 0x32c) = 0.0f;
    *(float*)(pSlot + 0x330) = 1.0f;
    *(float*)(pSlot + 0x334) = 0.0f;
    *(float*)(pSlot + 0x338) = 0.0f;
    *(float*)(pSlot + 0x33c) = 1.0f;

    *(float*)(pSlot + 0x34c) = 0.0f;
    *(float*)(pSlot + 0x350) = 0.0f;
    *(float*)(pSlot + 0x354) = 0.0f;
    *(float*)(pSlot + 0x340) = 0.0f;
    *(float*)(pSlot + 0x344) = 0.0f;
    *(float*)(pSlot + 0x348) = 1.0f;

    // Clear camera shake/offset parameters
    *(float*)(pSlot + 0x398) = 0.0f;
    *(float*)(pSlot + 0x39c) = 0.0f;
    *(float*)(pSlot + 0x3a0) = 0.0f;
    *(float*)(pSlot + 0x3a4) = 0.0f;
    *(float*)(pSlot + 0x3a8) = 0.0f;
    *(float*)(pSlot + 0x3ac) = 0.0f;
    *(float*)(pSlot + 0x3b0) = 0.0f;
    *(float*)(pSlot + 0x3b8) = 0.0f;
    *(float*)(pSlot + 0x3b4) = 0.0f;

    // Clear render flags and set default color
    *(u32*)(pSlot + 0xff8) = 0;
    *(u32*)(pSlot + 0x2c0) = 0xff;
}

// ============================================================================
// Address: 0x8009bf78 | player_slot_init
// Initializes a player slot: clears per-slot data, calls camera subsystem init,
// sets per-player occupancy flags based on slot index, resets interpolation.
// ============================================================================
void player_slot_init(int pSlot)
{
    int slot_index;
    int i;

    slot_index = (pSlot - (int)g_player_slot_pool) / PLAYER_SLOT_SIZE;

    // Clear HUD/viewport state
    *(u32*)(pSlot + 0x200) = 0;
    *(u32*)(pSlot + 0x204) = 0;
    *(u32*)(pSlot + 0x208) = 0;
    *(u32*)(pSlot + 0x20c) = 0;
    *(u32*)(pSlot + 0x210) = 0;

    // Initialize camera subsystem for this slot
    FUN_800f33d4(pSlot);

    // Clear input/event state
    *(u16*)(pSlot + 0xe08) = 0;
    *(u16*)(pSlot + 0xe0a) = 0;
    *(u32*)(pSlot + 0x26c) = 0;
    *(u32*)(pSlot + 0x270) = 0;
    *(u32*)(pSlot + 0x274) = 0;
    *(u32*)(pSlot + 0x278) = 0;

    // Set per-player viewport assignment flags (4 slots)
    for (i = 0; i < 4; i++) {
        *(char*)(pSlot + 0xdf6 + i) = (i == slot_index) ? 1 : 0;
    }

    // Set per-player screen assignment flags (6 slots)
    for (i = 0; i < 6; i++) {
        *(char*)(pSlot + 0xdfb + i) = (i == slot_index) ? 1 : 0;
    }

    *(u8*)(pSlot + 0xdfa) = 0;
    *(u8*)(pSlot + 0xe01) = 1;

    // Reset interpolation with default values (no lerp)
    player_slot_reset_interpolation(pSlot, 0);
}

// ============================================================================
// Address: 0x8009c05c | player_slot_alloc
// Allocates a player slot from the 6-slot pool. First looks for a free
// (deallocated) slot, then expands if under max. Returns slot pointer or 0.
// ============================================================================
int player_slot_alloc(int is_ai_controlled)
{
    int found = 0;
    int offset = 0;
    int remaining = g_player_slot_count;

    // Search for a previously freed slot (flag bit 31 clear = free)
    if (g_player_slot_count > 0) {
        while (remaining != 0) {
            if ((*(u32*)((int)g_player_slot_pool + offset + 0x21c) & 0x80000000) == 0) {
                found = (int)g_player_slot_pool + offset;
            }
            offset += PLAYER_SLOT_SIZE;
            remaining--;
        }
    }

    // If no free slot found, expand the pool if room available
    if (found == 0 && g_player_slot_count < PLAYER_SLOT_MAX) {
        found = (int)g_player_slot_pool + g_player_slot_count * PLAYER_SLOT_SIZE;
        g_player_slot_count++;
    }

    if (found == 0) {
        return 0;
    }

    // Initialize the slot
    player_slot_init(found);

    // Mark slot as active (bit 0x11 = allocated + valid)
    *(u32*)(found + 0x21c) = 0x00000011;

    // If AI-controlled, set additional flags
    if (is_ai_controlled != 0) {
        *(u32*)(found + 0x21c) |= 0x4002;
    }

    return found;
}

// ============================================================================
// Address: 0x8009c124 | spawn_point_find_by_entity
// Linear search of spawn point table for an entry matching the given entity ID.
// Returns pointer to the 0xC0-byte spawn point entry, or 0 if not found.
// 23 xrefs - heavily used by damage, AI, and match systems.
// ============================================================================
int spawn_point_find_by_entity(int entity_id)
{
    int offset = 0;
    int remaining = g_spawn_point_count;

    if (g_spawn_point_count > 0) {
        while (remaining != 0) {
            if (entity_id == *(int*)((int)g_spawn_point_table + offset + 0xbc)) {
                return (int)g_spawn_point_table + offset;
            }
            offset += SPAWN_ENTRY_SIZE;
            remaining--;
        }
    }
    return 0;
}

// ============================================================================
// Address: 0x8009c164 | player_slot_get_pool_base
// Returns the base pointer to the player slot pool.
// 22 xrefs - used by damage handler, rendering, and HUD systems.
// ============================================================================
void* player_slot_get_pool_base(void)
{
    return g_player_slot_pool;
}

// ============================================================================
// Address: 0x8009c16c | player_system_reset
// Resets the player system state: clears active counts, resets camera defaults,
// computes camera height from a global constant.
// ============================================================================
void player_system_reset(void)
{
    g_active_player_count = 0;
    g_player_slot_count = 0;
    g_spawn_point_count = 0; // DAT_80611d30
    g_player_slot_current = g_player_slot_pool;

    // Compute camera height offset from a scene constant
    // DAT_806109c4 is a level-specific vertical offset
    g_camera_height_offset = (float)((double)((DAT_806109c4 - 0xb8) ^ 0x80000000) - 4503601774854144.0);

    g_camera_fov_default = 60.0f;    // FLOAT_80614688
    g_world_camera_x = 0.0f;         // FLOAT_8061468c
    g_world_camera_y = 200.0f;       // FLOAT_80614690
    g_world_camera_z = -500.0f;      // FLOAT_80614694
    g_player_system_flags = 0;
}

// ============================================================================
// Address: 0x8009c1e0 | player_system_alloc
// Allocates the player slot pool (6 slots x 0xFFC = 0x5FF0 bytes).
// Initializes each slot header, then resets the system.
// Called once at game startup from level initialization.
// ============================================================================
void player_system_alloc(void)
{
    int i;
    int offset;
    int pSlot;

    g_player_slot_pool = (void*)mem_alloc(PLAYER_POOL_SIZE, 1);
    memset_wrapper(g_player_slot_pool, 0, PLAYER_POOL_SIZE);

    // Initialize slot headers
    offset = 0;
    for (i = 0; i < PLAYER_SLOT_MAX; i++) {
        pSlot = (int)g_player_slot_pool + offset;
        *(int*)(pSlot + 0x214) = i;       // slot index
        *(u32*)(pSlot + 0x21c) = 0;       // flags = inactive
        *(u32*)(pSlot + 0x2a0) = 0;       // clear position tracking
        *(u32*)(pSlot + 0x2a4) = 0;
        offset += PLAYER_SLOT_SIZE;
    }

    player_system_reset();

    // Set camera system defaults
    g_camera_lerp_speed = 0.1f;      // FLOAT_806146a0
    g_camera_pitch_min = 0.0f;       // FLOAT_80614678
    g_camera_pitch_max = 0.0f;       // FLOAT_80614678
    g_camera_zoom_default = 1.0f;    // FLOAT_80614680
}

// ============================================================================
// Address: 0x8009c278 | player_get_camera_lerp_factor
// Returns a camera interpolation factor based on player slot state.
// If slot field +0x254 == 0 AND field +0x258 == 1 (first-person + grounded),
// returns a reduced factor (smoother camera). Otherwise returns default 0.5.
// 50 xrefs - called heavily during camera interpolation.
// ============================================================================
float player_get_camera_lerp_factor(int pSlot)
{
    if (*(int*)(pSlot + 0x254) == 0 && *(int*)(pSlot + 0x258) == 1) {
        return 0.25f;   // FLOAT_806146a4 - smoother interpolation for first-person grounded
    }
    return 0.5f;        // FLOAT_80614684 - default interpolation speed
}

// ============================================================================
// Address: 0x8009c2a0 | player_set_active_count
// Sets the number of active players and clears per-slot event data.
// Called from match_spawn_players, charselect, game_phase_state_machine, etc.
// 11 xrefs.
// ============================================================================
void player_set_active_count(int count)
{
    int i;
    int offset;

    g_active_player_count = count;
    if (count == 0) {
        return;
    }

    // Clear per-slot event accumulator (field +0x26c)
    offset = 0;
    for (i = 0; i < g_player_slot_count; i++) {
        *(u32*)((int)g_player_slot_pool + offset + 0x26c) = 0;
        offset += PLAYER_SLOT_SIZE;
    }
}

// ============================================================================
// Address: 0x8009c2e0 | player_get_active_count
// Returns the current active player count.
// 13 xrefs - used by rendering, damage, and match systems.
// ============================================================================
int player_get_active_count(void)
{
    return g_active_player_count;
}
