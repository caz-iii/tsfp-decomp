// Address: 0x800ad944-0x800ada54 | Name: player_ai_attachments | Date: 2026-08-11
// AI bot attachment slots, death gib spawning, and killer entity handle resolution.
// Part of the AI character management subsystem.

#include "common.h"
#include "player.h"
#include "entity.h"

// Globals
static u32 g_killerEntityHandle; // 0x80611d9c

extern int  player_ai_get_voice_sound_id(int charType);
extern void entity_spawn_with_activation(int type, int modelId, f32* pos, f32* rot, int a, int b);
extern int  entity_spawn_by_type(int type, f32* pos, f32* rot, int a, int b);
extern void entity_destroy_with_event(void* entity);
extern void mesh_collision_transform_wrapper(int entity, int* meshData, f32* boneMatrix);

// ============================================================================
// Address: 0x800ad944
// player_ai_attach_slot1_stub - Attachment slot 1 (unused, returns -1)
// ============================================================================
int player_ai_attach_slot1_stub(void) {
    return -1;
}

// ============================================================================
// Address: 0x800ad94c
// player_ai_attach_slot2_stub - Attachment slot 2 (unused, returns -1)
// ============================================================================
int player_ai_attach_slot2_stub(void) {
    return -1;
}

// ============================================================================
// Address: 0x800ad954
// player_ai_attach_slot3_stub - Attachment slot 3 (unused, returns -1)
// ============================================================================
int player_ai_attach_slot3_stub(void) {
    return -1;
}

// ============================================================================
// Address: 0x800ad95c
// player_ai_attach_slot4_stub - Attachment slot 4 (unused, returns -1)
// ============================================================================
int player_ai_attach_slot4_stub(void) {
    return -1;
}

// ============================================================================
// Address: 0x800ad964
// player_ai_attach_nop - Empty no-op attachment callback
// ============================================================================
void player_ai_attach_nop(void) {
    return;
}

// ============================================================================
// Address: 0x800ad968
// player_ai_get_bag_entity_type - Get bag/flag entity type for capture modes
// Currently stubbed to return -1 (no bag entity).
// ============================================================================
int player_ai_get_bag_entity_type(void) {
    return -1;
}

// ============================================================================
// Address: 0x800ad970
// player_ai_get_attachment_type - Attachment entity type dispatcher
// 6 slots: slot 0 = voice sound ID, slots 1-4 = stub (-1), slot 5 = bag entity.
// Returns entity type ID for the given attachment slot, or -1 if none.
// ============================================================================
int player_ai_get_attachment_type(int charIdx, int slotIdx, int attachSlot) {
    switch (attachSlot) {
        case 0: return player_ai_get_voice_sound_id(slotIdx);
        case 1: return player_ai_attach_slot1_stub();
        case 2: return player_ai_attach_slot2_stub();
        case 3: return player_ai_attach_slot3_stub();
        case 4: return player_ai_attach_slot4_stub();
        case 5: return player_ai_get_bag_entity_type();
        default: return -1;
    }
}

// ============================================================================
// Address: 0x800ada0c
// player_ai_store_killer_handle - Store killer entity handle from death event
// Reads generation-checked handle at offset +0x2C from event data.
// Called from damage_death_anim_dispatch.
// ============================================================================
void player_ai_store_killer_handle(int eventData) {
    g_killerEntityHandle = *(u32*)(eventData + 0x2C);
}

// ============================================================================
// Address: 0x800ada18
// player_ai_resolve_killer_entity - Resolve stored handle to entity pointer
// High 10 bits = pool index. Validates handle matches the entity slot.
// Clears stored handle on stale/invalid references.
// ============================================================================
int player_ai_resolve_killer_entity(void) {
    if (g_killerEntityHandle == 0) {
        return 0;
    }

    // Extract pool index from high bits and compute entity address
    int entityAddr = g_entityPoolBase + (g_killerEntityHandle >> 22) * 0x2E8;

    // Generation check: stored handle must match current entity handle
    if (*(u32*)(entityAddr + 0x2C) == g_killerEntityHandle) {
        return entityAddr;
    }

    // Stale handle: entity was recycled
    g_killerEntityHandle = 0;
    return 0;
}

// ============================================================================
// Address: 0x800ada54
// player_ai_spawn_death_gibs - Death dismemberment gib spawner
// Spawns base gib entity, then iterates 6 body part slots (head, torso, arms,
// legs, accessory). For each slot with a valid attachment type and not already
// dismembered, creates a temporary entity positioned via bone-transform.
// Marks dismembered parts in per-character bitmask.
// Called from player_ai_spawn_bot and combat death system.
// ============================================================================
void player_ai_spawn_death_gibs(int charIdx, int slotIdx, int forceAll) {
    // Get character data
    int charTableIdx = charIdx;
    int modelIdx = *(int*)(g_characterTableBase + charIdx * 0x30);

    // Default gib spawn position/rotation
    f32 spawnPos[3];
    f32 spawnRot[3];
    spawnPos[0] = g_gibDefaultPos[0];
    spawnPos[1] = g_gibDefaultPos[1];
    spawnPos[2] = g_gibDefaultPos[2];
    spawnRot[0] = g_gibDefaultRot[0];
    spawnRot[1] = g_gibDefaultRot[1];
    spawnRot[2] = g_gibDefaultRot[2];

    // Spawn base gib entity (type 1 = generic gib container)
    int* baseEntity = (int*)entity_spawn_with_activation(
        1, modelIdx, spawnPos, spawnRot, 0, 0
    );

    // Iterate 6 attachment slots (body parts)
    for (int slot = 0; slot < 6; slot++) {
        int attachType = player_ai_get_attachment_type(charTableIdx, slotIdx, slot);

        // Skip if no attachment for this slot
        if (attachType == -1) continue;

        // Skip if already dismembered (unless forceAll)
        if (forceAll == 0 && ((1 << slot) & g_dismemberBitmask[slotIdx]) != 0) {
            continue;
        }

        // Spawn body part entity at base entity position
        int* partEntity = (int*)entity_spawn_by_type(
            attachType, (f32*)(baseEntity + 0x19), spawnRot, 0, 0
        );

        // Get mesh data for bone transform
        int renderData = partEntity[1];
        int* meshData = *(int**)(renderData + 4) + (*(*(int**)(renderData + 4))) * -0x30;

        // Transform to bone position based on slot
        // Slot -> bone matrix offset: 0=0x60, 1=0x68, 2=0x6C, 3=0x70, 4=0x74, 5=0x64
        int boneTableBase = *(int*)(baseEntity[1] + 0x84);
        f32* boneMatrix;
        switch (slot) {
            case 0: boneMatrix = *(f32**)(boneTableBase + 0x60); break;
            case 1: boneMatrix = *(f32**)(boneTableBase + 0x68); break;
            case 2: boneMatrix = *(f32**)(boneTableBase + 0x6C); break;
            case 3: boneMatrix = *(f32**)(boneTableBase + 0x70); break;
            case 4: boneMatrix = *(f32**)(boneTableBase + 0x74); break;
            case 5: boneMatrix = *(f32**)(boneTableBase + 0x64); break;
        }
        mesh_collision_transform_wrapper(renderData, meshData, boneMatrix);

        // Destroy temp entity (gib physics takes over)
        entity_destroy_with_event(partEntity);

        // Mark this slot as dismembered
        g_dismemberBitmask[slotIdx] |= (1 << slot);
    }

    // Destroy base container entity
    entity_destroy_with_event(baseEntity);
}
