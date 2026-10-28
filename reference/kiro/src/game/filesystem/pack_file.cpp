// Address: 0x8002a1a4-0x8002a7c0 | Name: pack_file | Date: 2026-08-05
// P5CK pack file system: loading, parsing, and entry lookup for the game's
// packed asset container format. Handles big-endian to little-endian conversion.
//
// Pack file format ("P5CK"):
//   Header: 4 bytes magic "P5CK", 4 bytes table_size (BE), 4 bytes version
//   Table:  N entries of 16 bytes each (4x uint32 BE), N = table_size / 16
//   Each entry contains an asset ID used for lookup.

#include "common.h"
#include "pack_file.h"
#include "mem_alloc.h"

#define PACK_MAGIC_P5CK  "P5CK"
#define PACK_MAX_SLOTS   30
#define PACK_ENTRY_SIZE  0x10
#define PACK_SLOT_STRIDE 0x1c
#define PACK_MAX_VERSION 0x80001

// Pack table globals (stride 0x1C = 28 bytes per slot):
// DAT_804e7430[slot*7+0] = DVD file handle (-1 if from memory)
// DAT_804e7438[slot*7]   = entry count (table_size / 16)
// DAT_804e743c[slot*7]   = source base pointer (for memory packs)
// DAT_804e7440[slot*7]   = table data pointer
// DAT_804e7444[slot*7]   = name string pointer
// DAT_804e7448[slot*7]   = associated resource handle
// DAT_8061187c           = total active pack slots

static inline u32 bswap32(u32 v) {
    return (v << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | (v >> 24);
}

// ============================================================================
// Address: 0x8002a1a4 | pack_alloc_slot
// Finds a free slot in the pack table or allocates a new one.
// Returns slot index.
// ============================================================================
int pack_alloc_slot(void) {
    // Search for reusable slot (handle == -1)
    for (int i = 0; i < g_PackSlotCount; i++) {
        if (g_PackHandles[i] == -1) {
            return i;
        }
    }
    // No free slot, expand
    int idx = g_PackSlotCount;
    g_PackSlotCount++;
    return idx;
}

// ============================================================================
// Address: 0x8002a1f0 | pack_store_name
// Allocates memory for and stores a pack file's name string.
// ============================================================================
void pack_store_name(const char* pName) {
    int nSlot = pack_alloc_slot();
    int nLen = strlen(pName);
    g_PackNames[nSlot] = (char*)mem_alloc(nLen + 1, 0);
    strcpy(g_PackNames[nSlot], pName);
    FUN_80382738(); // epilogue
}

// ============================================================================
// Address: 0x8002a264 | pack_parse_header_from_mem
// Parses a P5CK pack header from an in-memory buffer.
// Byte-swaps all table entries from big-endian to little-endian.
// Called when pack data is already in RAM (e.g. from resource pool).
// ============================================================================
void pack_parse_header_from_mem(u32 pBase, u32 nDataSize, int nResourceHandle) {
    char acHeader[4];
    u32 uTableSize;
    u32 uVersion;
    int nSlot;
    int nOffset;

    if (g_PackSlotCount >= PACK_MAX_SLOTS) {
        FUN_8038273c();
        return;
    }

    // Read 12-byte header from buffer
    memcpy(acHeader, (void*)pBase, 12);

    // Byte-swap header fields
    uVersion = bswap32(*(u32*)(acHeader + 8));
    uTableSize = bswap32(*(u32*)(acHeader + 4));

    nSlot = pack_alloc_slot();
    int nSlotOffset = nSlot * PACK_SLOT_STRIDE;

    // Table pointer is at end of data minus table size
    g_PackTablePtrs[nSlot] = (u32*)(pBase + nDataSize - uTableSize);
    g_PackNames[nSlot] = 0;
    g_PackHandles[nSlot] = nResourceHandle;
    g_PackDvdHandles[nSlot] = -1;
    g_PackBasePtrs[nSlot] = pBase;
    g_PackEntryCounts[nSlot] = uTableSize >> 4;  // 16 bytes per entry

    // Byte-swap all table entries (4 DWORDs each)
    nOffset = 0;
    for (int i = 0; i < g_PackEntryCounts[nSlot]; i++) {
        u32* pEntry = (u32*)((int)g_PackTablePtrs[nSlot] + nOffset);
        nOffset += PACK_ENTRY_SIZE;
        pEntry[0] = bswap32(pEntry[0]);
        pEntry[1] = bswap32(pEntry[1]);
        pEntry[2] = bswap32(pEntry[2]);
        pEntry[3] = bswap32(pEntry[3]);
    }
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002a370 | pack_load_from_dvd
// Loads a P5CK pack from a DVD file handle. Validates the "P5CK" magic,
// reads the table-of-contents, byte-swaps all entries from big-endian.
// Supports both streaming (param_11=0) and memory-based (param_11=1) modes.
// ============================================================================
void pack_load_from_dvd(int nSlotIdx, int hDvdFile, int bFromMemory,
                        int nSrcBuffer, u32 uOffset, u32 uSize) {
    char acHeader[12];
    u32 uTableSize;
    u32 uVersion;
    int nSlotOffset;

    if (g_PackSlotCount >= PACK_MAX_SLOTS) {
        FUN_80382728();
        return;
    }

    nSlotOffset = nSlotIdx * PACK_SLOT_STRIDE;

    if (bFromMemory == 0) {
        // Streaming mode: read header from DVD
        int nRead = FUN_80186f34(hDvdFile, acHeader, 12, nSrcBuffer);
        if (nRead != 12) goto error;
    } else {
        // Memory mode: seek and read
        FUN_80186fa4(hDvdFile, 0, 1);
        u32 nRead = FUN_80186f34(hDvdFile, uOffset, uSize, nSrcBuffer);
        if (nRead != uSize) goto error;
        memcpy(acHeader, (void*)uOffset, 12);
    }

    // Validate magic "P5CK"
    if (acHeader[0] != 'P' || acHeader[1] != '5' ||
        acHeader[2] != 'C' || acHeader[3] != 'K') {
        goto error;
    }

    // Byte-swap header fields
    uTableSize = bswap32(*(u32*)(acHeader + 4));
    uVersion = bswap32(*(u32*)(acHeader + 8));

    if (uVersion >= PACK_MAX_VERSION) goto error;

    // Store resource handle
    g_PackHandles[nSlotIdx] = nSrcBuffer;

    if (bFromMemory == 0) {
        // Streaming: allocate table and read from DVD
        if (g_PackTablePtrs[nSlotIdx] == NULL) {
            g_PackTablePtrs[nSlotIdx] = (u32*)mem_alloc_aligned(0x40, uTableSize);
        }
        // Seek backwards to table position and read
        int iSeekResult = FUN_80186fa4(hDvdFile, -(int)uTableSize, 0);
        if (iSeekResult < 1) goto error;
        u32 nRead = FUN_80186f34(hDvdFile, g_PackTablePtrs[nSlotIdx], uTableSize);
        if (nRead != uTableSize) goto error;

        g_PackDvdHandles[nSlotIdx] = hDvdFile;
        g_PackBasePtrs[nSlotIdx] = 0;
    } else {
        // Memory mode: table is at end of buffer
        g_PackTablePtrs[nSlotIdx] = (u32*)((uOffset + uSize) - uTableSize);
        g_PackDvdHandles[nSlotIdx] = -1;
        g_PackBasePtrs[nSlotIdx] = uOffset;
    }

    // Store entry count and byte-swap all entries
    g_PackEntryCounts[nSlotIdx] = uTableSize >> 4;

    int nByteOffset = 0;
    for (int i = 0; i < g_PackEntryCounts[nSlotIdx]; i++) {
        u32* pEntry = (u32*)((int)g_PackTablePtrs[nSlotIdx] + nByteOffset);
        nByteOffset += PACK_ENTRY_SIZE;
        pEntry[0] = bswap32(pEntry[0]);
        pEntry[1] = bswap32(pEntry[1]);
        pEntry[2] = bswap32(pEntry[2]);
        pEntry[3] = bswap32(pEntry[3]);
    }

    // Track loaded pack count for memory-based packs
    if (bFromMemory != 0 && nSrcBuffer == 1) {
        g_PackLoadedCount++;
    }

error:
    FUN_80382728(); // epilogue
}

// ============================================================================
// Address: 0x8002a5e4 | pack_find_by_name
// Searches pack table for a pack matching the given name string.
// Returns slot index, or -1 if not found.
// ============================================================================
int pack_find_by_name(const char* pName) {
    for (int i = 0; i < g_PackSlotCount; i++) {
        if (g_PackNames[i] == NULL) continue;
        if (strcmp(pName, g_PackNames[i]) == 0) {
            return i;
        }
    }
    FUN_80382734(); // epilogue
    return -1;
}

// ============================================================================
// Address: 0x8002a66c | pack_open
// Opens a pack file by path. First checks if already loaded (by name),
// otherwise opens the DVD file, allocates memory, and loads the pack.
// ============================================================================
void pack_open(const char* pPath, int nSrcBuffer, int nParam) {
    int iExisting;
    int hFile;
    u32 uFileSize;
    u32 uAllocSize;

    iExisting = pack_find_by_name(pPath);
    if (iExisting != -1) {
        FUN_80382730();
        return;  // Already loaded
    }

    // Open DVD file
    hFile = FUN_80186e94(pPath);
    if (hFile < 0) {
        FUN_80382730();
        return;
    }

    uFileSize = 0;
    uAllocSize = 0;
    if (/* has file size */ hFile >= 0) {
        uFileSize = FUN_80186fa4(hFile, 0, 0);  // Get file size
        uAllocSize = (u32)mem_alloc_aligned(0x20, uFileSize);
    }

    // Store pack name and load from DVD
    pack_store_name(pPath);
    pack_load_from_dvd(/* slot */, hFile, /* bFromMemory */ 0, nSrcBuffer,
                       uAllocSize, uFileSize);

    if (/* has file size */ uFileSize != 0) {
        FUN_80187004(hFile);  // Close DVD file after table read
    }

    FUN_80382730(); // epilogue
}

// ============================================================================
// Address: 0x8002a740 | pack_find_entry_by_id
// Searches within a specific pack slot for an entry matching the given ID.
// On hit, sets the entry as the active/current pack entry and updates
// the LRU tracking (recent packs get priority).
// Returns pointer to entry, or NULL if not found.
// ============================================================================
int* pack_find_entry_by_id(int nEntryId, int nSlot) {
    int* pTable;
    int nCount;

    // Don't search the "excluded" pack slot
    if (nSlot == g_PackExcludeSlot) {
        return NULL;
    }

    pTable = (int*)g_PackTablePtrs[nSlot];
    nCount = g_PackEntryCounts[nSlot];

    for (int i = 0; i < nCount; i++) {
        if (*pTable == nEntryId) {
            // Found - update active entry tracking
            g_PackActiveEntry = pTable;
            g_PackRecentSlot2 = g_PackRecentSlot1;
            g_PackRecentSlot1 = nSlot;
            g_PackActiveContext = &g_PackDvdHandles[nSlot];
            return pTable;
        }
        pTable += 4;  // 16 bytes per entry
    }
    return NULL;
}

// ============================================================================
// Address: 0x8002a7c0 | pack_resolve_entry
// High-level entry resolver. Searches for an entry ID across all loaded packs.
// Uses LRU order: checks recent packs first, then scans all others.
// Falls back to requesting pack load if entry not found anywhere.
// ============================================================================
void pack_resolve_entry(int nEntryId) {
    int* pResult;

    if (g_PackForceSlot == -2) {
        FUN_8038273c();
        return;  // Pack system disabled
    }

    if (g_PackForceSlot >= 0) {
        // Forced to specific pack
        pack_find_entry_by_id(nEntryId, g_PackForceSlot);
        FUN_8038273c();
        return;
    }

    // Check if already the active entry
    if (g_PackActiveEntry != NULL && nEntryId == *g_PackActiveEntry) {
        FUN_8038273c();
        return;
    }

    // Search recent packs first (LRU)
    if (g_PackRecentSlot1 >= 0 && g_PackRecentSlot1 < g_PackSlotCount) {
        pResult = pack_find_entry_by_id(nEntryId, g_PackRecentSlot1);
        if (pResult != NULL) goto done;
    }
    if (g_PackRecentSlot2 >= 0 && g_PackRecentSlot2 < g_PackSlotCount) {
        pResult = pack_find_entry_by_id(nEntryId, g_PackRecentSlot2);
        if (pResult != NULL) goto done;
    }

    // Full scan of all packs
    for (int i = 0; i < g_PackSlotCount; i++) {
        if (i == g_PackRecentSlot1 || i == g_PackRecentSlot2) continue;
        pResult = pack_find_entry_by_id(nEntryId, i);
        if (pResult != NULL) goto done;
    }

    // Not found - request dynamic load
    int nNewSlot = FUN_8002aae0();
    if (nNewSlot != -1) {
        pack_find_entry_by_id(nEntryId, nNewSlot);
    }
    FUN_8002aad8(nNewSlot);

done:
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002a900 | pack_resolve_by_path
// Resolves a pack entry by full asset path string.
// Strips directory prefix (after last '/'), hashes the filename,
// then calls pack_resolve_entry to find across all loaded packs.
// ============================================================================
void pack_resolve_by_path(const char* pPath) {
    char* pSlash;

    // Check if path contains directory separator after prefix
    pSlash = strstr((char*)pPath, &g_PackPathPrefix);
    if (pSlash != NULL) {
        pPath = (const char*)(pSlash + 6);  // Skip prefix
    }

    // Hash the filename and resolve
    FUN_8012e000((u8*)pPath);  // Compute hash (stored in global)
    pack_resolve_entry();
}

// ============================================================================
// Address: 0x8002a948 | pack_destroy_slot
// Destroys a pack slot: closes DVD handle, frees table/name/base memory,
// resets all slot fields, and invalidates LRU cache.
// ============================================================================
void pack_destroy_slot(int nSlot) {
    int nOffset = nSlot * PACK_SLOT_STRIDE;

    // Close DVD file handle if streaming
    if (g_PackDvdHandles[nSlot] != -1) {
        FUN_80187004(g_PackDvdHandles[nSlot]);
    }

    if (g_PackHandles[nSlot] == 0) {
        // Self-owned memory: free table, name, and base
        if (g_PackBasePtrs[nSlot] == 0 && g_PackTablePtrs[nSlot] != NULL) {
            mem_free_top((int)g_PackTablePtrs[nSlot]);
        }
        if (g_PackNames[nSlot] != 0) {
            mem_free_top(g_PackNames[nSlot]);
        }
        if (g_PackBasePtrs[nSlot] != 0) {
            mem_free_top(g_PackBasePtrs[nSlot]);
        }
    }

    // Reset slot
    g_PackActiveEntry = NULL;
    g_PackNames[nSlot] = 0;
    g_PackBasePtrs[nSlot] = 0;
    g_PackEntryCounts[nSlot] = 0;
    g_PackHandles[nSlot] = -1;
    g_PackTablePtrs[nSlot] = NULL;

    // Invalidate LRU
    g_PackRecentSlot1 = -1;
    g_PackRecentSlot2 = -1;

    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002aa2c | pack_find_entry_index_by_hash
// Finds entry index within a specific pack by hashing a name string.
// Returns index (0-based), or -1 if not found.
// ============================================================================
int pack_find_entry_index_by_hash(int nSlot, const char* pName) {
    int nHash = FUN_8012e000((u8*)pName);
    int nCount = g_PackEntryCounts[nSlot];
    int iOffset = 0;

    for (int i = 0; i < nCount; i++) {
        if (*(int*)((int)g_PackTablePtrs[nSlot] + iOffset) == nHash) {
            return i;
        }
        iOffset += PACK_ENTRY_SIZE;
    }
    return -1;
}

// ============================================================================
// Address: 0x8002aab0 | pack_get_entry_id_at_index
// Returns the entry ID (first DWORD) at a given index in a pack slot.
// ============================================================================
u32 pack_get_entry_id_at_index(int nSlot, int nIndex) {
    return *(u32*)((int)g_PackTablePtrs[nSlot] + nIndex * PACK_ENTRY_SIZE);
}

// ============================================================================
// Address: 0x8002aad0 | pack_set_force_slot
// Sets the pack force-slot override. When set, pack_resolve_entry will
// only search within this specific slot.
// ============================================================================
void pack_set_force_slot(int nSlot) {
    g_PackForceSlot = nSlot;
}

// ============================================================================
// Address: 0x8002aad8 | pack_set_exclude_slot
// Sets the pack exclude-slot. This slot will be skipped during searches.
// ============================================================================
void pack_set_exclude_slot(int nSlot) {
    g_PackExcludeSlot = nSlot;
}

// ============================================================================
// Address: 0x8002aae0 | pack_pop_exclude_slot
// Atomically gets and clears the exclude-slot. Returns the previous value.
// Used by pack_resolve_entry for fallback loading.
// ============================================================================
int pack_pop_exclude_slot(void) {
    int nPrev = g_PackExcludeSlot;
    g_PackExcludeSlot = -1;
    return nPrev;
}

// ============================================================================
// Address: 0x8002aaf0 | pack_get_asset_ptr
// Resolves a named asset to its memory pointer.
// Hashes the name, finds it across all packs, validates alignment,
// and returns pointer to the asset data. Optionally writes size to pSizeOut.
// Returns NULL if pack system inactive or asset not found.
// ============================================================================
void* pack_get_asset_ptr(const char* pName, u32* pSizeOut) {
    int* pEntry;

    if (g_PackSlotCount == 0) {
        return NULL;
    }

    pEntry = (int*)pack_resolve_by_path((u8*)pName);
    if (pEntry == NULL) {
        return NULL;
    }

    // Validate pack context has base data
    if (*(int*)(g_PackActiveContext + 0xc) == 0) {
        return NULL;
    }

    // Check entry has no compression (field[3] == 0) and is aligned
    if (pEntry[3] != 0) {
        return NULL;
    }
    if ((pEntry[1] & 0xF) != 0) {
        return NULL;  // Not 16-byte aligned
    }

    // Write size if requested
    if (pSizeOut != NULL) {
        *pSizeOut = (u32)pEntry[2];
    }

    // Return base + offset
    return (void*)(*(int*)(g_PackActiveContext + 0xc) + pEntry[1]);
}

// ============================================================================
// Address: 0x8002aba0 | pack_get_asset_ptr_from_slot
// Resolves asset by name within a specific pack slot (not global search).
// Used when caller knows which pack contains the asset.
// Returns pointer to asset data, or NULL if not found/invalid.
// ============================================================================
void* pack_get_asset_ptr_from_slot(const char* pName, int nSlot) {
    int nHash;
    int* pEntry;

    if (g_PackSlotCount == 0) {
        return NULL;
    }

    nHash = FUN_8012e000((u8*)pName);
    pEntry = pack_find_entry_by_id(nHash, nSlot);

    if (pEntry == NULL) {
        return NULL;
    }

    // Validate context base pointer
    if (*(int*)(g_PackActiveContext + 0xc) == 0) {
        return NULL;
    }

    // Must be uncompressed and 16-byte aligned
    if (pEntry[3] != 0) {
        return NULL;
    }
    if ((pEntry[1] & 0xF) != 0) {
        return NULL;
    }

    return (void*)(*(int*)(g_PackActiveContext + 0xc) + pEntry[1]);
}

// ============================================================================
// Address: 0x8002ac3c | dvd_get_file_size
// Opens a DVD file, queries its size, and closes it immediately.
// Returns file size, or -1 if file not found.
// ============================================================================
u32 dvd_get_file_size(const char* pPath) {
    int hFile;
    u32 uSize;

    hFile = FUN_80186e94(pPath);
    if (hFile < 0) {
        return (u32)-1;
    }

    uSize = FUN_80186fa4(hFile, 0, 0);  // Seek to end = file size
    FUN_80187004(hFile);                 // Close
    return uSize;
}

// ============================================================================
// Address: 0x8002b238 | pack_reset_slot_count
// Resets the pack slot count to zero and clears the exclude slot.
// Used during level transitions before re-opening new packs.
// ============================================================================
void pack_reset_slot_count(void) {
    g_PackSlotCount = 0;
    pack_pop_exclude_slot();
}

// ============================================================================
// Address: 0x8002b260 | pack_clear_lru_state
// Clears the LRU cache and active entry pointer.
// ============================================================================
void pack_clear_lru_state(void) {
    g_PackActiveEntry = NULL;
    g_PackRecentSlot1 = -1;
    g_PackRecentSlot2 = -1;
}

// ============================================================================
// Address: 0x8002b278 | pack_stub_noop_b278
// Empty stub.
// ============================================================================
void pack_stub_noop_b278(void) {
    return;
}

// ============================================================================
// Address: 0x8002b27c | pack_shutdown_all
// Full pack system shutdown. Closes all DVD file handles, frees all
// memory (tables, names, data), resets all state to initial.
// ============================================================================
void pack_shutdown_all(void) {
    // Close all DVD file handles
    for (int i = 0; i < g_PackSlotCount; i++) {
        if (g_PackDvdHandles[i] >= 0) {
            FUN_80187004(g_PackDvdHandles[i]);
        }
    }

    // Reset all state
    g_PackActiveEntry = NULL;
    g_PackRecentSlot1 = -1;
    g_PackRecentSlot2 = -1;
    g_PackLoadedCount = 0;
    g_PackSlotCount = 0;

    // Clear entire pack table memory
    memset_wrapper((int)&g_PackSlotTable, 0, 0x348);

    pack_stub_noop_b278();
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002b534 | pack_close_streaming_handles
// Closes DVD streaming file handles for non-persistent packs.
// Packs with handle type == 2 are kept open (shared/persistent).
// ============================================================================
void pack_close_streaming_handles(void) {
    pack_clear_lru_state();

    if (g_PackPersistFlag != 0) {
        FUN_80382734();
        return;
    }

    int iOffset = 0;
    for (int i = 0; i < g_PackSlotCount; i++) {
        // Skip persistent packs (handle type 2)
        if (g_PackHandles[i] != 2) {
            int hDvd = g_PackDvdHandles[i];
            if (hDvd >= 0) {
                FUN_80187004(hDvd);
                g_PackDvdHandles[i] = -1;
            }
        }
        iOffset += PACK_SLOT_STRIDE;
    }
    FUN_80382734(); // epilogue
}

// ============================================================================
// Address: 0x8002b314 | pack_open_level_paks
// Opens all .pak files required for the current level.
// Called during level load. Opens in order:
//   1. Level-specific path.pak (from format string)
//   2. Mode variant paks (story/arcade)
//   3. Global shared paks (gun.pak, chr.pak, overlay.pak)
//   4. Always-loaded paks (gct.pak, gcob.pak)
// Skips loading if packs are already open (g_PacksLoaded flag).
// ============================================================================
void pack_open_level_paks(void) {
    char acPathBuf[120];
    int nLevelId;

    if (g_PacksLoaded != 0) {
        FUN_8038273c();
        return;
    }

    nLevelId = g_CurrentLevel;
    if ((g_GameDebugFlags & 0x800000) != 0) {
        nLevelId = (int)g_DebugLevelOverride;
    }

    // Build and open level-specific pack
    FUN_803641b0(acPathBuf);  // Generate level pack path
    g_PackLastHandle = pack_open(acPathBuf, 0, 1);

    // Open mode-variant packs for non-arcade levels
    if (g_CurrentLevel < 0x65 || nLevelId == 100 ||
        nLevelId == 0x66 || nLevelId == 0x65) {
        asset_build_pak_path(/* variant 1 */);
        FUN_80133414(1);
        pack_open(/* path */, 0, 1);
    }

    // Build main level pak path
    asset_build_pak_path(/* variant 0 */);

    // Special handling for arcade hub (level 100)
    if (nLevelId == 100 && !game_is_in_cutscene_mode()) {
        // Load map-specific character pak
        int* pMapData = FUN_80339b90(g_ArcadeMapData, 0xc77668);
        void* pCharPak = FUN_8034902c((u32)*(u16*)((int)pMapData + 10));
        pack_open(pCharPak, 0, 1);
    }

    // Open level pak (shared or level-specific)
    if (nLevelId == 100) {
        pack_open(/* main path */, 0, 1);
    } else {
        FUN_80133414(2);
        pack_open(/* variant path */, 0, 1);
    }

    // Optional extra pak (if file exists on disc)
    asset_build_pak_path(/* variant 2 */);
    if (dvd_get_file_size(/* path */) > 0) {
        pack_open(/* path */, 0, 1);
    }

    // Shared game paks for non-arcade single-player
    if (!game_is_single_player_arcade() &&
        g_CurrentLevel != 0x67 && g_CurrentLevel != 0x6a) {
        if (g_CurrentLevel != 0x6b) {
            pack_open("pak/gun.pak", 0, 1);
            pack_open("pak/chr.pak", 0, 1);
        }
        if (g_CurrentLevel == 0x65 || g_CurrentLevel == 0x6b) {
            pack_open("pak/overlay.pak", 0, 1);
        }
    }

    // Always-loaded global paks
    g_PackLastHandle = pack_open(acPathBuf, 0, 1);
    pack_open("pak/gct.pak", 0, 1);
    pack_open("pak/gcob.pak", 0, 1);

    FUN_8038273c(); // epilogue
}
