// Address: 0x8002ac98-0x8002b080 | Name: asset_load | Date: 2026-08-05
// Asset loading layer: sits above the pack file system and DVD I/O.
// Provides unified asset access (pack-first, DVD-fallback) plus
// game mode path construction for level-specific .pak files.

#include "common.h"
#include "pack_file.h"
#include "mem_alloc.h"

// ============================================================================
// Address: 0x8002ac98 | asset_get_size
// Gets the size of an asset by path. Tries pack system first (fast, no I/O),
// falls back to opening the DVD file and querying its size.
// Returns size in bytes, or negative on error.
// ============================================================================
int asset_get_size(const char* pPath) {
    int iResult;

    if (g_PackSlotCount == 0) {
        // No packs loaded, must use DVD
        return dvd_get_file_size(pPath);
    }

    iResult = (int)pack_resolve_by_path((u8*)pPath);
    if (iResult == 0) {
        // Not in any pack, fall back to DVD
        return dvd_get_file_size(pPath);
    }

    // Entry found in pack: size is field[2], +0x100 if compressed (field[3] != 0)
    int nSize = *(int*)(iResult + 8);
    if (*(int*)(iResult + 0xc) != 0) {
        nSize += 0x100;  // Extra space for decompression header
    }
    return nSize;
}

// ============================================================================
// Address: 0x8002ad00 | asset_get_pack_entry_size
// Gets size from the currently active pack entry (after pack_resolve_entry).
// Returns -1 if no active entry.
// ============================================================================
int asset_get_pack_entry_size(void) {
    int* pEntry = (int*)pack_resolve_entry();
    if (pEntry == NULL) {
        return -1;
    }
    int nSize = pEntry[2];
    if (pEntry[3] != 0) {
        nSize += 0x100;  // Compression header overhead
    }
    return nSize;
}

// ============================================================================
// Address: 0x8002ad4c | asset_load_from_dvd
// Loads an asset from DVD into an allocated buffer.
// Handles .gcr (graphics resource) size tracking.
// Allocates 32-byte aligned buffer, reads file, returns size via pSizeOut.
// ============================================================================
void asset_load_from_dvd(const char* pPath, u32* pSizeOut) {
    u32 uSize;
    int iOffset;
    const char* pExt;

    uSize = asset_get_size(pPath);
    if ((int)uSize <= 0) {
        if (pSizeOut != NULL) *pSizeOut = uSize;
        FUN_80382734();
        return;
    }

    // Find file extension
    iOffset = 0;
    pExt = pPath;
    while (*pExt != '.' && *pExt != '\0') {
        iOffset++;
        pExt++;
    }

    // Track .gcr (graphics resource) total size
    if (pPath[iOffset + 1] == 'g' && pPath[iOffset + 2] == 'c' && pPath[iOffset + 3] == 'r') {
        g_GcrTotalSize += uSize;
    }

    // Allocate 32-byte aligned buffer and load
    void* pBuffer = mem_alloc_aligned(0x20, uSize);
    uSize = FUN_8002b100(pPath, pBuffer, uSize);

    if (pSizeOut != NULL) {
        *pSizeOut = uSize;
    }
    FUN_80382734(); // epilogue
}

// ============================================================================
// Address: 0x8002ae2c | asset_load
// High-level asset load. Tries pack memory pointer first (zero-copy),
// falls back to DVD load if not in a pack.
// ============================================================================
void asset_load(const char* pPath, u32* pSizeOut) {
    int iPackPtr;

    iPackPtr = (int)pack_get_asset_ptr((const char*)pPath, pSizeOut);
    if (iPackPtr == 0) {
        // Not in pack, load from DVD
        asset_load_from_dvd(pPath, pSizeOut);
    }
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002ae88 | game_is_story_mode
// Returns TRUE if game is in story mode (game type == '\n' and level != 100).
// ============================================================================
BOOL game_is_story_mode(void) {
    if (g_GameMode == '\n' && g_CurrentLevel != 100) {
        return TRUE;
    }
    return FALSE;
}

// ============================================================================
// Address: 0x8002aeb8 | game_is_single_player_arcade
// Returns TRUE if game is in single-player arcade mode.
// Checks: story mode AND not mapmaker (flag 0x40) AND level != 0x38.
// Returns 0 for single-player arcade, 1 otherwise (inverted logic).
// ============================================================================
BOOL game_is_single_player_arcade(void) {
    if (g_GameMode == '\n' && g_CurrentLevel != 100 &&
        (g_GameStateFlags & 0x40) == 0 && g_CurrentLevel != 0x38) {
        return FALSE;  // IS single-player arcade (inverted return)
    }
    return TRUE;
}

// ============================================================================
// Address: 0x8002af04 | game_is_online
// Returns whether game is in online/network mode.
// Always returns 0 (offline) - network was cut from retail.
// ============================================================================
BOOL game_is_online(void) {
    return FALSE;
}

// ============================================================================
// Address: 0x8002af0c | game_get_mode_string
// Returns the game mode directory name string used in asset paths.
// "cutscene" for cutscenes, "arcade" for arcade, "story" for story,
// or mapmaker prefix for user-generated content.
// ============================================================================
const char* game_get_mode_string(int nLevelId) {
    int bIsCutscene = FUN_801c2dcc(nLevelId);

    if (bIsCutscene != 0) {
        return "cutscene";
    }

    if ((g_GameStateFlags & 0x40) != 0) {
        return g_MapmakerPrefix;  // User content directory
    }

    if (game_is_story_mode()) {
        return "story";
    }
    return "arcade";
}

// ============================================================================
// Address: 0x8002af70 | asset_build_level_suffix
// Builds an asset path suffix string based on game mode and level ID.
// Formats into a static buffer (g_LevelSuffix) using sprintf-style calls.
// Returns pointer to the static buffer.
// ============================================================================
const char* asset_build_level_suffix(int nLevelId, int nVariant) {
    g_LevelSuffix[0] = '\0';

    if (nLevelId < 100) {
        int bIsCutscene = FUN_801c2dcc(nLevelId);
        if (bIsCutscene == 0) {
            if ((g_GameStateFlags & 0x40) == 0) {
                // Normal gameplay level
                if (game_is_story_mode() == 0) {
                    sprintf(g_LevelSuffix, g_FmtArcadeLevel);  // arcade format
                } else if (nVariant == 2) {
                    sprintf(g_LevelSuffix, g_FmtStoryLevelAlt);  // story alt
                } else {
                    sprintf(g_LevelSuffix, g_FmtStoryLevel);  // story normal
                }
            } else {
                sprintf(g_LevelSuffix, g_FmtMapmaker);  // mapmaker
            }
        } else {
            sprintf(g_LevelSuffix, g_FmtCutscene);  // cutscene
        }
    }

    // Append online suffix if applicable
    if (nVariant == 1) {
        if (game_is_online()) {
            strcat(g_LevelSuffix, g_OnlineSuffix);
        } else {
            strcat(g_LevelSuffix, g_OfflineSuffix);
        }
    }

    return g_LevelSuffix;
}

// ============================================================================
// Address: 0x8002b080 | asset_build_pak_path
// Builds full .pak file path: "pak/<mode>/<prefix>_l<level><suffix>.pak"
// Combines game mode string, level ID, and suffix into output buffer.
// ============================================================================
void asset_build_pak_path(char* pOutBuf, int nLevelId, int nVariant) {
    const char* pMode;
    const char* pSuffix;

    pMode = game_get_mode_string(nLevelId);
    pSuffix = asset_build_level_suffix(nLevelId, nVariant);

    // Format: "pak/%s/%s_l_%d%s.pak"
    sprintf(pOutBuf, "pak/%s/%s_l_%d%s.pak", pMode, &g_PakPrefix, nLevelId, pSuffix);

    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002b0fc | pack_stub_noop_b0fc
// Empty stub — likely a removed debug/logging hook for asset loads.
// ============================================================================
void pack_stub_noop_b0fc(void) {
    return;
}

// ============================================================================
// Address: 0x8002b100 | asset_read_to_buffer
// Reads asset data into a pre-allocated buffer. Tries pack first (seeking
// within DVD file for streaming packs). Falls back to direct DVD read.
// Handles compressed pack entries via decompression.
// ============================================================================
u32 asset_read_to_buffer(const char* pPath, void* pBuffer, u32 nBufSize) {
    int* pEntry;

    if (g_PackSlotCount == 0) {
        // No packs, direct DVD read
        dvd_read_file_to_buffer(pPath, pBuffer, nBufSize);
        return nBufSize;
    }

    pEntry = (int*)pack_resolve_by_path((u8*)pPath);
    if (pEntry == NULL) {
        // Not in any pack, direct DVD read
        dvd_read_file_to_buffer(pPath, pBuffer, nBufSize);
    } else {
        // Found in pack — seek to offset and read
        FUN_80186fa4(*g_PackActiveContext, pEntry[1], 1);  // Seek to entry offset

        u32 uCompressedSize = (u32)pEntry[3];
        if (uCompressedSize == 0) {
            // Uncompressed: direct read
            FUN_80186f34(*g_PackActiveContext, pBuffer, nBufSize);
        } else {
            // Compressed: read compressed data, then decompress
            u32 uDecompBuf = ((int)pBuffer + nBufSize - uCompressedSize) & ~0x1F;
            FUN_80186f34(*g_PackActiveContext, (void*)uDecompBuf, uCompressedSize);
            FUN_8002cfd4(uDecompBuf, uCompressedSize, (int)pBuffer);  // Decompress
        }
    }
    FUN_80382738(); // epilogue
    return nBufSize;
}

// ============================================================================
// Address: 0x8002b1c8 | dvd_read_file_to_buffer
// Opens a DVD file by path, reads it entirely into buffer, and closes.
// Asserts on open failure.
// ============================================================================
void dvd_read_file_to_buffer(const char* pPath, void* pBuffer, u32 nSize) {
    int hFile;

    hFile = FUN_80186e94(pPath);
    if (hFile < 0) {
        halt_stub_assert();  // Fatal: file not found
    }
    FUN_80186f34(hFile, pBuffer, nSize);
    FUN_80187004(hFile);  // Close
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002b5c8 | dvd_read_chunked
// Reads a large file in chunks with an optional per-chunk callback.
// Useful for streaming large assets without requiring full-size contiguous
// buffers. Callback returns 0 to abort, non-zero to continue.
// ============================================================================
void dvd_read_chunked(int hFile, void* pBuffer, u32 nTotalSize,
                      u32 nChunkSize, void* pUserData,
                      int (*pfnCallback)(void* pBuf, u32 nRead, void* pUser)) {
    u32 nRemaining = nTotalSize;

    while (nRemaining > 0) {
        u32 nThisRead = nChunkSize;
        if ((int)nRemaining < (int)nChunkSize) {
            nThisRead = nRemaining;
        }

        u32 nActualRead = FUN_80186f34(hFile, pBuffer, nThisRead);
        if (nActualRead != nThisRead) {
            break;  // Read error
        }

        // Invoke per-chunk callback if provided
        if (pfnCallback != NULL) {
            int result = pfnCallback(pBuffer, nThisRead, pUserData);
            if (result == 0) {
                break;  // Callback aborted
            }
        }

        nRemaining -= nThisRead;
    }
    FUN_80382728(); // epilogue
}

// ============================================================================
// Address: 0x8002b688 | dvd_read_file_chunked
// Opens a DVD file, gets its size, seeks to start, reads in chunks with
// callback, then closes. Full lifecycle wrapper around dvd_read_chunked.
// ============================================================================
void dvd_read_file_chunked(const char* pPath, void* pBuffer, u32 nChunkSize,
                           void* pUserData, void* pfnCallback) {
    int hFile;
    u32 uFileSize;

    hFile = FUN_80186e94(pPath);
    if (hFile < 0) {
        FUN_80382730();
        return;
    }

    uFileSize = FUN_80186fa4(hFile, 0, 0);   // Get size
    FUN_80186fa4(hFile, 0, 1);               // Seek to start
    dvd_read_chunked(hFile, uFileSize, pBuffer, nChunkSize, pUserData, pfnCallback);
    FUN_80187004(hFile);                     // Close
    FUN_80382730(); // epilogue
}

// ============================================================================
// Address: 0x8002b730 | asset_stream_read
// Streamed asset read: checks pack first (seeks within DVD handle),
// falls back to dvd_read_file_chunked for standalone files.
// ============================================================================
void asset_stream_read(const char* pPath, void* pBuffer, u32 nChunkSize,
                       void* pUserData, void* pfnCallback) {
    int* pEntry;

    if (g_PackSlotCount == 0) {
        dvd_read_file_chunked(pPath, pBuffer, nChunkSize, pUserData, pfnCallback);
        FUN_80382730();
        return;
    }

    pEntry = (int*)pack_resolve_by_path((u8*)pPath);
    if (pEntry == NULL) {
        dvd_read_file_chunked(pPath, pBuffer, nChunkSize, pUserData, pfnCallback);
    } else {
        // Seek to entry offset within pack DVD handle and chunked read
        int iSeekResult = FUN_80186fa4(*g_PackActiveContext, pEntry[1], 1);
        if (iSeekResult == pEntry[1]) {
            dvd_read_chunked(*g_PackActiveContext, (u32)pEntry[2], pBuffer,
                             nChunkSize, pUserData, pfnCallback);
        }
    }
    FUN_80382730(); // epilogue
}

// ============================================================================
// Address: 0x8002b7f4 | asset_load_async
// Initiates async file load. If in pack, signals immediate availability.
// Otherwise dispatches to DVD async loader.
// ============================================================================
void asset_load_async(const char* pPath, void* pDest, u32 nSize) {
    int* pEntry;

    if (g_PackSlotCount == 0) {
        dvd_load_file_7198(pPath, pDest, nSize);
        return;
    }

    pEntry = (int*)pack_resolve_by_path((u8*)pPath);
    if (pEntry == NULL) {
        dvd_load_file_7198(pPath, pDest, nSize);
    } else {
        FUN_801870c4();  // Signal pack data ready (no async needed)
    }
}

// ============================================================================
// Address: 0x8002b86c | asset_load_async_from_pack
// Async load targeting a specific pack slot. Hashes name, searches the slot,
// and either signals immediate availability or dispatches DVD async load.
// ============================================================================
void asset_load_async_from_pack(const char* pPath, int nPackSlot) {
    int nHash;
    int* pEntry;

    if (g_PackSlotCount == 0) {
        dvd_load_file_7198(pPath, /* dest */, nPackSlot);
        FUN_8038273c();
        return;
    }

    nHash = FUN_8012e000((u8*)pPath);
    pEntry = pack_find_entry_by_id(nHash, nPackSlot);

    if (pEntry == NULL) {
        dvd_load_file_7198(pPath, /* dest */, nPackSlot);
    } else {
        FUN_801870c4();  // Signal ready
    }
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002b8f0 | asset_pack_entry_is_ready
// Checks if the currently resolved pack entry can be served immediately
// (i.e., the pack data is in memory, not streaming-only).
// Returns non-zero if ready.
// ============================================================================
BOOL asset_pack_entry_is_ready(void) {
    int* pEntry = (int*)pack_resolve_entry();
    if (pEntry == NULL) {
        return FALSE;
    }
    return FUN_801870c4();  // Check immediate availability
}

// ============================================================================
// Address: 0x8002b940 | asset_create_async_handle
// Creates an async DVD read handle for a pack entry.
// Resolves the path in packs, then creates a positioned read operation.
// Returns handle ID, or 0 on failure.
// ============================================================================
int asset_create_async_handle(const char* pPath) {
    int* pEntry;

    if (g_PackSlotCount == 0) return 0;

    pEntry = (int*)pack_resolve_by_path((u8*)pPath);
    if (pEntry == NULL) return 0;
    if (*g_PackActiveContext == -1) return 0;

    // Create positioned read at entry offset
    return FUN_80187590(*g_PackActiveContext, pEntry[1]);
}

// ============================================================================
// Address: 0x8002b9a0 | dvd_poll_tick
// Wrapper for the DVD subsystem polling/tick function.
// Should be called each frame to process async DVD operations.
// ============================================================================
void dvd_poll_tick(void) {
    FUN_8018758c();
}

// ============================================================================
// Address: 0x8002b9c0 | stream_alloc_page_buffer
// Allocates a page-aligned buffer for streaming operations.
// ============================================================================
void stream_alloc_page_buffer(void) {
    g_StreamPageBuffer = mem_page_alloc();
}

// ============================================================================
// Address: 0x8002b9e8 | stream_free_page_buffer
// Frees the streaming page buffer.
// ============================================================================
void stream_free_page_buffer(void) {
    mem_page_free(g_StreamPageBuffer);
}

// ============================================================================
// Address: 0x8002ba0c | stream_advance_write_offset
// Advances the streaming write offset by a 4-byte aligned amount.
// ============================================================================
void stream_advance_write_offset(int nBytes) {
    g_StreamWriteOffset += (nBytes + 3) & ~3;  // Align up to 4 bytes
}
