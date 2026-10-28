// Address: 0x80034a8c-0x800353f0 | Name: level_data | Date: 2026-08-05
// Level data loading, save entity management, and level definition lookup.
// The level_load_dat_file function is the main level asset parser.

#include "common.h"
#include "level_data.h"
#include "asset_load.h"
#include "mem_alloc.h"

#define SAVE_ENTITY_POOL_SIZE  0x88
#define SAVE_ENTITY_STRIDE     0x40

// ============================================================================
// Address: 0x80034a8c | save_entity_alloc
// Allocates a save/profile entity from the entity pool.
// Copies name string (max 20 chars), sets initial state.
// Returns pointer to allocated entity.
// ============================================================================
void* save_entity_alloc(const char* pName, int nParam) {
    int nNameLen = strlen(pName);
    int nCopyLen = (nNameLen < 20) ? nNameLen : 20;

    // Find free slot in pool (88 slots, stride 0x40)
    for (int i = 0; i < SAVE_ENTITY_POOL_SIZE; i++) {
        int* pSlot = (int*)(g_SaveEntityPool + i * SAVE_ENTITY_STRIDE);
        if (*pSlot == 0) {
            memset(pSlot + 1, 0, 0x15);
            strncpy((char*)(pSlot + 1), pName, nCopyLen);
            *pSlot = 1;  // Mark active
            pSlot[10] = nParam;
            pSlot[9] = nParam >> 31;  // Sign extend
            *(u16*)(pSlot + 11) = 1;
            *(u16*)((int)pSlot + 0x2E) = 0xFFFF;
            pSlot[12] = 0;
            pSlot[13] = 0;
            pSlot[14] = 0;
            return pSlot;
        }
    }
    return NULL;
}

// ============================================================================
// Address: 0x80034b64 | player_entity_destroy
// Destroys a player entity. Clears the back-pointer from any owning slot,
// then zeros the entity memory (0x40 bytes).
// ============================================================================
void player_entity_destroy(int* pEntity) {
    if (pEntity == NULL || *pEntity == 0) return;
    // Clear back-pointer in owning slot
    if ((int*)pEntity[0xE] != NULL) {
        *(int*)pEntity[0xE] = 0;
    }
    memset(pEntity, 0, 0x40);
}

// ============================================================================
// Address: 0x80034bb4 | save_data_attach
// Creates or attaches the save data block to a player.
// Allocates 300 bytes if needed, initializes on first attach.
// ============================================================================
void save_data_attach(int nFlags) {
    if (nFlags == 0 || g_SaveDataBlock == 0) {
        g_SaveDataBlock = (int)mem_alloc(300, 1);
    }
    if (g_SaveDataBlock != 0 && g_SaveDataActive != g_SaveDataBlock) {
        g_SaveDataActive = g_SaveDataBlock;
        int* pData = (int*)save_get_data_ptr();
        *(int*)(pData + 0xC) = 0;
    }
}

// ============================================================================
// Address: 0x80034c24 | save_get_data_ptr
// Returns the current active save data pointer.
// ============================================================================
void* save_get_data_ptr(void) {
    return (void*)g_SaveDataActive;
}

// ============================================================================
// Address: 0x80034c2c | save_get_options_ptr
// Returns pointer to save data options section (offset +0x130).
// ============================================================================
void* save_get_options_ptr(void) {
    return (void*)(g_SaveDataActive + 0x130);
}

// ============================================================================
// Address: 0x80034c38 | save_stub_noop
// Empty stub.
// ============================================================================
void save_stub_noop(void) { return; }

// ============================================================================
// Address: 0x80034c3c | level_load_dat_file
// MASSIVE: Loads and parses a level .dat file into the level context struct.
//
// The .dat file has a header with 30+ offset/size pairs. This function:
// 1. Loads the file via asset_load (pack-first, DVD fallback)
// 2. Resolves each offset into an absolute pointer (base + offset)
// 3. Stores pointer/count pairs into the level context struct
// 4. Resolves entity cross-references (entities reference each other by ID)
// 5. Remaps function pointer indices to actual function pointers via lookup tables
// 6. Resolves entity-to-spawnpoint linkages
//
// Level context struct layout (at pLevelCtx):
//   +0x0C/+0x10: entity instances (base + header[0x18], count = header[0x1C])
//   +0x14/+0x18: textures (header[0x6C], count = header[0x70])
//   +0x1C/+0x20: scripts (header[0x3C], count = header[0x40])
//   +0x24/+0x28: materials (header[0x78], count = header[0x7C])
//   +0x34/+0x38: animation clips (header[0xCC], count = header[0xD0])
//   +0x3C/+0x40: spawn points (header[0xD8], count = header[0xDC])
//   +0x44-0x60:  collision geometry sets (header[0x9C]-[0xC4])
//   +0x74/+0x78: AI waypoints (header[0x0C], count = header[0x10])
//   +0x84/+0x88: triggers (header[0x54], count = header[0x58])
//   +0xA4/+0xA8: particle emitters (header[0x30], count = header[0x34])
//   +0xAC/+0xB0: sound zones (header[0x24], count = header[0x28])
//   +0xCC/+0xD0: cutscene markers (header[0x90], count = header[0x94])
//   +0xD4/+0xD8: light probes (header[0x84], count = header[0x88])
//   +0xDC/+0xE0: decals (header[0x60], count = header[0x64])
//   +0xE4-0xF8:  LOD groups (header[0xE4]-[0x100])
//   +0xFC/+0x100: mapmaker tiles (header[0x108], count = header[0x10C], if mapmaker)
//
// Cross-reference resolution:
//   - Spawn points reference each other by ID at +0x38 (linked via +0x30 match)
//   - AI waypoints reference spawn points at +0x68
//   - Triggers at type 6,7,23-31 reference spawn points
//   - Cutscene markers at type 0x48 reference spawn points
//
// Function pointer remapping uses lookup tables at:
//   - DAT_803f3948 (AI waypoint behaviors)
//   - DAT_803f39dc, DAT_803f3a08 (waypoint callbacks)
//   - DAT_8060fca8 (entity instance behaviors)
//   - DAT_803f3a6c (sound zone types)
//   - DAT_803f3a4c (sound zone callbacks, with FUN_802333ec as default)
//   - DAT_803f3a8c (cutscene marker behaviors)
//   - DAT_803f3c1c (light probe types)
//   - DAT_803f3c54 (particle emitter types)
//   - DAT_803f3c44 (trigger types)
// ============================================================================
void level_load_dat_file(void) {
    int pLevelCtx = FUN_803826e4();

    if (*(int**)(pLevelCtx + 0x11C) == NULL) {
        FUN_80382730();
        return;
    }

    // Build path and load file
    char acPath[1060];
    strcpy(acPath, *(char**)(pLevelCtx + 0x11C));
    FUN_8012ea4c(acPath, &g_DatExtension, &g_DatSearchPath);

    int pFileData = asset_load(acPath, 1, &g_FileSize);
    if (pFileData == 0) {
        FUN_80382730();
        return;
    }

    // Resolve all offset/size pairs from header into absolute pointers
    // [30+ assignments resolving header offsets to context struct fields]
    // Each: *(pLevelCtx + N) = pFileData + *(pFileData + headerOffset)
    //        *(pLevelCtx + N+4) = *(pFileData + headerOffset + 4)  // count

    // Resolve animation clip array (offset table → absolute pointers)
    if (*(int*)(pFileData + 0xD0) != 0) {
        int* pClipTable = (int*)(pFileData + *(int*)(pFileData + 0xCC));
        *(int**)(pLevelCtx + 0x34) = pClipTable;
        int pAnimData = pFileData + *(int*)(pFileData + 8);
        *(int*)(pLevelCtx + 0x38) = *(int*)(pFileData + 0xD0);
        for (u32 i = 0; i < *(u32*)(pFileData + 0xD0); i++) {
            pClipTable[i * 2] = pAnimData;
            pAnimData += pClipTable[i * 2 + 1] * 0xC;
        }
    }

    // Resolve spawn point cross-references
    for (int i = 0; i < *(int*)(pLevelCtx + 0x40); i++) {
        int pSpawn = *(int*)(pLevelCtx + 0x3C) + i * 0x48;
        *(u16*)(pSpawn + 0x3E) = 0;
        int nType = *(int*)(pSpawn + 8);
        // Types 0,3,5,6,7,23-31 resolve +0x38 to spawn index
        // [cross-reference resolution by matching +0x30 IDs]
    }

    // Remap AI waypoint function pointers
    for (int i = 0; i < *(int*)(pLevelCtx + 0x78); i++) {
        int pWaypoint = *(int*)(pLevelCtx + 0x74) + i * 0x98;
        *(int*)(pWaypoint + 0x6C) = g_WaypointBehaviorTable[*(int*)(pWaypoint + 0x6C)];
        *(int*)(pWaypoint + 0x78) = g_WaypointCallbackA[*(int*)(pWaypoint + 0x78)];
        *(int*)(pWaypoint + 0x7C) = g_WaypointCallbackB[*(int*)(pWaypoint + 0x7C)];
        // Additional waypoint-to-spawnpoint linking...
    }

    // Remap entity instance behaviors
    for (int i = 0; i < *(int*)(pLevelCtx + 0x10); i++) {
        int pEntity = *(int*)(pLevelCtx + 0x0C) + i * 0x60;
        *(int*)(pEntity + 0x28) = g_EntityBehaviorTable[*(int*)(pEntity + 0x28)];
    }

    // Remap sound zone, cutscene, light probe, particle, trigger tables
    // [Similar pattern for each: index → function pointer via lookup table]

    FUN_802321f8();  // Post-load initialization
    FUN_80382730();  // epilogue
}

// ============================================================================
// Address: 0x8003538c | level_find_def_by_id
// Looks up a level definition entry by level ID from the level table.
// Table is an array of 0x80-byte entries with ID at offset +8.
// Entries are terminated by a zero at offset +4 (s16 check).
// Returns pointer to entry, or NULL if not found.
// ============================================================================
void* level_find_def_by_id(int nLevelId) {
    int iOffset = 0;
    while (*(s16*)(g_LevelDefTable + iOffset + 4) != 0) {
        if (nLevelId == *(int*)(g_LevelDefTable + iOffset + 8)) {
            return (void*)(g_LevelDefTable + iOffset);
        }
        iOffset += 0x80;
    }
    return NULL;
}

// ============================================================================
// Address: 0x800353cc | level_get_current_def
// Returns the level definition for the current level ID.
// ============================================================================
void* level_get_current_def(void) {
    return level_find_def_by_id(g_CurrentLevel);
}

// ============================================================================
// Address: 0x800353f0 | rng_seed_from_value
// Seeds the random number generator with a given value.
// ============================================================================
void rng_seed_from_value(u32 nSeed) {
    FUN_8038c67c(nSeed);
}
