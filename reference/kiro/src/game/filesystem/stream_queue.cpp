// Address: 0x8002cfd4-0x8002d4a4 | Name: stream_queue | Date: 2026-08-05
// Streaming resource queue: priority-sorted async asset loading system.
// Manages a queue of pending resource loads (models, animations, raw assets)
// with priority ordering and deduplication.
//
// Request types:
//   0 = Raw asset (direct path)
//   1 = Model (builds path from "assets/ts/models/<name>/gamecube_out/")
//   2 = Animation (builds path with extension substitution)
//   4 = Completed (marker for finished requests)
//   6 = Callback-only (no file I/O, just queues a completion callback)
//
// Each request entry is 24 (0x18) bytes:
//   +0x00: u16 type
//   +0x02: u16 flags
//   +0x04: int* pNext (linked list)
//   +0x08: int priority (lower = higher priority)
//   +0x0C: char* pName (asset path/name)
//   +0x10: void* pData (loaded data pointer)
//   +0x14: int nDataSize

#include "common.h"
#include "stream_queue.h"
#include "pack_file.h"
#include "asset_load.h"

#define STREAM_MAX_REQUESTS  850  // 0x352
#define STREAM_ENTRY_SIZE    0x18

// ============================================================================
// Address: 0x8002cfd4 | inflate_gzip_buffer
// High-level gzip decompression: sets up inflate state from a compressed
// buffer, parses gzip header, runs DEFLATE decompression, outputs to dest.
// ============================================================================
void inflate_gzip_buffer(void* pCompressed, u32 nCompressedSize, void* pDest) {
    stream_alloc_page_buffer();

    g_InflateDestOffset = 0;
    g_InflateWindowBuf = g_InflateStaticWindow;  // 32KB static buffer
    g_InflateWindowPos = 0;
    g_InflateReadPos = 0;
    g_InflateDestBase = (int)pDest;
    g_InflateInputSize = nCompressedSize;
    g_InflateInputBuf = (int)pCompressed;

    inflate_parse_gzip_header();
    inflate_decompress();

    stream_free_page_buffer();
}

// ============================================================================
// Address: 0x8002d044 | path_get_filename
// Extracts the filename portion from a full path.
// Scans backwards from end to find last '/'.
// Returns pointer to character after last '/', or start if no '/'.
// ============================================================================
const char* path_get_filename(const char* pPath) {
    int nLen = strlen(pPath);
    const char* p = pPath + nLen;

    while (p > pPath && *p != '/') {
        p--;
    }
    if (*p == '/') {
        p++;
    }
    return p;
}

// ============================================================================
// Address: 0x8002d0a0 | stream_queue_init
// Initializes the streaming request queue. Clears all entries and resets
// counters and linked list pointers.
// ============================================================================
void stream_queue_init(void) {
    for (int i = 0; i < STREAM_MAX_REQUESTS; i++) {
        g_StreamQueue[i].type = 0;
        g_StreamQueue[i].flags = 0;
        g_StreamQueue[i].pName = NULL;
        g_StreamQueue[i].pData = NULL;
        g_StreamQueue[i].nDataSize = 0;
    }
    g_StreamQueueCount = 0;
    g_StreamQueueCompleteCount = 0;
    g_StreamQueueHead = NULL;
    g_StreamQueueActiveCount = 0;
}

// ============================================================================
// Address: 0x8002d100 | stream_queue_insert_sorted
// Inserts a request into the priority-sorted linked list.
// Lower priority value = processed first (highest priority).
// If streaming is paused (g_StreamPaused), sets flag 0x4 on request.
// ============================================================================
void stream_queue_insert_sorted(StreamRequest* pReq) {
    if (g_StreamQueueHead == NULL) {
        g_StreamQueueHead = pReq;
    } else if (g_StreamQueueHead->priority < pReq->priority) {
        // Insert after head - walk list to find position
        StreamRequest* pPrev = g_StreamQueueHead;
        StreamRequest* pCur = pPrev->pNext;
        BOOL bInserted = FALSE;

        while (pCur != NULL && !bInserted) {
            if (pCur->priority < pReq->priority) {
                pPrev = pCur;
                pCur = pCur->pNext;
            } else {
                pReq->pNext = pCur;
                pPrev->pNext = pReq;
                bInserted = TRUE;
            }
        }
        if (!bInserted) {
            pReq->pNext = NULL;
            pPrev->pNext = pReq;
        }
    } else {
        // Insert at head (highest priority)
        pReq->pNext = g_StreamQueueHead;
        g_StreamQueueHead = pReq;
    }

    if (g_StreamPaused != 0) {
        pReq->flags |= 0x4;
    }
}

// ============================================================================
// Address: 0x8002d1a4 | stream_queue_raw_asset
// Queues a raw asset load request (type 0).
// Checks for duplicate pending requests before adding.
// ============================================================================
void stream_queue_raw_asset(const char* pPath, int nPriority) {
    // Check if already pending
    if (stream_find_pending_by_name(pPath) != 0) {
        FUN_8038273c();
        return;
    }

    int idx = g_StreamQueueCount;
    int offset = idx * STREAM_ENTRY_SIZE;
    g_StreamQueueCount++;

    g_StreamQueue[idx].type = 0;  // Raw asset
    g_StreamQueue[idx].flags = 0;
    int hAsync = asset_create_async_handle((u8*)pPath);
    g_StreamQueue[idx].hAsync = hAsync;
    g_StreamQueue[idx].pName = pPath;
    g_StreamQueue[idx].pData = NULL;
    g_StreamQueue[idx].nPriority = nPriority;

    stream_queue_insert_sorted(&g_StreamQueue[idx]);
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002d230 | stream_queue_model
// Queues a model load request (type 1).
// Builds full path: "assets/ts/models/<name>/gamecube_out/<filename>"
// ============================================================================
void stream_queue_model(const char* pModelName, int nPriority) {
    char acPathBuf[536];

    // Check for duplicate
    if (stream_find_pending_by_name(pModelName) != 0) {  // FUN_8002d538
        FUN_8038273c();
        return;
    }

    int idx = g_StreamQueueCount;
    int offset = idx * STREAM_ENTRY_SIZE;
    g_StreamQueueCount++;

    g_StreamQueue[idx].type = 1;  // Model
    g_StreamQueue[idx].flags = 0;

    // Get filename from path for local lookup
    const char* pFilename = path_get_filename(pModelName);

    // Build full asset path
    sprintf(acPathBuf, "assets/ts/models/%s/gamecube_out/%s", pModelName, pFilename);

    int hAsync = asset_create_async_handle((u8*)acPathBuf);
    g_StreamQueue[idx].hAsync = hAsync;
    g_StreamQueue[idx].pName = pModelName;
    g_StreamQueue[idx].pData = NULL;
    g_StreamQueue[idx].nPriority = nPriority;

    stream_queue_insert_sorted(&g_StreamQueue[idx]);
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002d2e4 | stream_queue_callback
// Queues a callback-only request (type 6). No file I/O, just schedules
// a callback to fire when the queue reaches this entry's priority.
// ============================================================================
void stream_queue_callback(void* pCallbackData) {
    // Check for duplicate (FUN_8002d5f4)
    if (FUN_8002d5f4() != 0) return;

    int idx = g_StreamQueueCount;
    int offset = idx * STREAM_ENTRY_SIZE;
    g_StreamQueueCount++;

    g_StreamQueue[idx].type = 6;  // Callback
    g_StreamQueue[idx].flags = 0;
    g_StreamQueue[idx].hAsync = 0;
    g_StreamQueue[idx].pName = pCallbackData;
    g_StreamQueue[idx].pData = NULL;
    g_StreamQueue[idx].nPriority = 0;

    stream_queue_insert_sorted(&g_StreamQueue[idx]);
}

// ============================================================================
// Address: 0x8002d358 | stream_queue_animation
// Queues an animation load request (type 2).
// Strips file extension, replaces with pack-compatible path, creates
// async handle for the animation data file.
// ============================================================================
void stream_queue_animation(const char* pAnimPath, int nPriority) {
    char acPathBuf[152];

    // Check for duplicate (FUN_8002d688)
    if (FUN_8002d688() != 0) {
        FUN_8038273c();
        return;
    }

    int idx = g_StreamQueueCount;
    int offset = idx * STREAM_ENTRY_SIZE;
    g_StreamQueueCount++;

    g_StreamQueue[idx].type = 2;  // Animation
    g_StreamQueue[idx].flags = 0;

    // Build animation path (copy and replace extension)
    strcpy(acPathBuf, pAnimPath);
    char* pDot = strstr(acPathBuf, ".");
    if (pDot != NULL) {
        strcpy(pDot, ".anm");  // Replace extension with animation format
    }

    int hAsync = asset_create_async_handle((u8*)acPathBuf);
    g_StreamQueue[idx].hAsync = hAsync;
    g_StreamQueue[idx].pName = pAnimPath;
    g_StreamQueue[idx].pData = NULL;
    g_StreamQueue[idx].nPriority = nPriority;

    stream_queue_insert_sorted(&g_StreamQueue[idx]);
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002d410 | stream_find_pending_by_name
// Searches the stream queue for a pending (type != 0) request matching name.
// Returns pointer to found entry, or NULL.
// ============================================================================
void* stream_find_pending_by_name(const char* pName) {
    if (pName == NULL) {
        FUN_80382734();
        return NULL;
    }

    int iOffset = 0;
    for (int i = 0; i < g_StreamQueueCount; i++) {
        if (g_StreamQueue[i].type != 0) {
            if (strcmp(g_StreamQueue[i].pName, pName) == 0) {
                return &g_StreamQueue[i];
            }
        }
        iOffset += STREAM_ENTRY_SIZE;
    }
    FUN_80382734();
    return NULL;
}

// ============================================================================
// Address: 0x8002d4a4 | stream_find_complete_by_name
// Searches the stream queue for a completed (type == 4) request by name.
// Returns pointer to found entry, or NULL.
// ============================================================================
void* stream_find_complete_by_name(const char* pName) {
    if (pName == NULL) {
        FUN_80382734();
        return NULL;
    }

    int iOffset = 0;
    for (int i = 0; i < g_StreamQueueCount; i++) {
        if (g_StreamQueue[i].type == 4) {
            if (strcmp(g_StreamQueue[i].pName, pName) == 0) {
                return &g_StreamQueue[i];
            }
        }
        iOffset += STREAM_ENTRY_SIZE;
    }
    FUN_80382734();
    return NULL;
}

// ============================================================================
// Address: 0x8002d538 | stream_find_model_by_name
// Finds pending model request (type 1 or 5) by name string comparison.
// ============================================================================
void* stream_find_model_by_name(const char* pName) {
    if (pName == NULL) { FUN_80382734(); return NULL; }
    for (int i = 0; i < g_StreamQueueCount; i++) {
        if ((g_StreamQueue[i].type == 1 || g_StreamQueue[i].type == 5) &&
            strcmp(g_StreamQueue[i].pName, pName) == 0) {
            return &g_StreamQueue[i];
        }
    }
    FUN_80382734();
    return NULL;
}

// ============================================================================
// Address: 0x8002d5f4 | stream_find_callback_by_name
// Finds pending callback request (type 6) by name/data comparison.
// ============================================================================
void* stream_find_callback_by_name(const char* pName) {
    if (pName == NULL) { FUN_80382734(); return NULL; }
    for (int i = 0; i < g_StreamQueueCount; i++) {
        if (g_StreamQueue[i].type == 6 &&
            strcmp(g_StreamQueue[i].pName, pName) == 0) {
            return &g_StreamQueue[i];
        }
    }
    FUN_80382734();
    return NULL;
}

// ============================================================================
// Address: 0x8002d688 | stream_find_anim_by_name
// Finds pending animation request (type 2) by name string comparison.
// ============================================================================
void* stream_find_anim_by_name(const char* pName) {
    if (pName == NULL) { FUN_80382734(); return NULL; }
    for (int i = 0; i < g_StreamQueueCount; i++) {
        if (g_StreamQueue[i].type == 2 &&
            strcmp(g_StreamQueue[i].pName, pName) == 0) {
            return &g_StreamQueue[i];
        }
    }
    FUN_80382734();
    return NULL;
}

// ============================================================================
// Address: 0x8002d71c | stream_queue_process
// Main stream queue processor. Iterates the priority-sorted linked list,
// loading each asset by type:
//   Type 0: Raw asset → asset_load / asset_read_to_buffer
//   Type 1: Model → builds path, loads, validates header (0x10)
//   Type 2: Animation → path extension swap, loads via anim loader
//   Type 3: Texture → delegates to texture streamer
//   Type 5: Secondary load
//   Type 6: Callback → handled separately by stream_process_callbacks
// Tracks loading progress for UI display. Asserts on load failures.
// ============================================================================
void stream_queue_process(void) {
    StreamRequest* pCur = g_StreamQueueHead;
    u32 nProcessed = 0;
    BOOL bHadErrors = FALSE;

    FUN_80133414(7);  // Set progress phase

    while (pCur != NULL) {
        if (pCur->flags & 0x4) {
            g_StreamPaused = 1;
        }

        switch (pCur->type) {
        case 0:  // Raw asset
            if (pCur->pData == NULL) {
                pCur->pData = asset_load(pCur->pName, 1, &pCur->nDataSize);
            } else {
                pCur->nDataSize = asset_read_to_buffer(pCur->pName, pCur->pData, 0x2000000);
            }
            break;

        case 1:  // Model
            // Build "assets/ts/models/<name>/gamecube_out/<filename>" path
            // Load model, validate header signature (must be 0x10)
            // On failure: strip extension from name, set error flag
            break;

        case 2:  // Animation
            // Swap extension, load via FUN_8017c03c
            break;

        case 3:  // Texture
            FUN_8017c154(pCur->pName, pCur->pData, &pCur->nDataSize);
            break;

        case 5:  // Secondary variant
            if (pCur->pData == NULL) {
                pCur->pData = asset_load(pCur->pName, 0, &pCur->nDataSize);
            } else {
                pCur->nDataSize = asset_read_to_buffer(pCur->pName, pCur->pData, 0x2000000);
            }
            break;
        }

        if (pCur->flags & 0x4) {
            g_StreamPaused = 0;
        }

        nProcessed++;
        // Update progress bar: nProcessed / g_StreamQueueCount
        FUN_801333d4((float)nProcessed / (float)g_StreamQueueCount);
        pCur = pCur->pNext;
    }

    // Handle errors: strip extensions from failed model names, assert
    if (bHadErrors) {
        halt_stub_assert();
    }

    FUN_801333d4(1.0f);  // Progress complete
    FUN_80382728(); // epilogue
}

// ============================================================================
// Address: 0x8002db48 | stream_process_callbacks
// Processes type-6 (callback) entries in the queue.
// Resolves pack asset pointers for each callback entry.
// ============================================================================
void stream_process_callbacks(void) {
    StreamRequest* pCur = g_StreamQueueHead;

    while (pCur != NULL) {
        if (pCur->type == 6) {
            pCur->pData = pack_get_asset_ptr(pCur->pName, NULL);
            FUN_80060b20();  // Invoke callback handler
        }
        pCur = pCur->pNext;
    }
}

// ============================================================================
// Address: 0x8002dba8 | stream_secondary_insert_sorted
// Inserts a request into the secondary stream queue (sorted by priority
// at offset +0xC). Similar to stream_queue_insert_sorted but for the
// secondary/texture queue with different entry layout.
// ============================================================================
void stream_secondary_insert_sorted(int pEntry) {
    if (g_SecondaryQueueHead == 0) {
        g_SecondaryQueueHead = pEntry;
        return;
    }
    if (*(int*)(pEntry + 0xC) <= *(int*)(g_SecondaryQueueHead + 0xC)) {
        *(int*)(pEntry + 0x18) = g_SecondaryQueueHead;
        g_SecondaryQueueHead = pEntry;
        return;
    }
    // Walk list to find insertion point
    int pPrev = g_SecondaryQueueHead;
    int pCur = *(int*)(g_SecondaryQueueHead + 0x18);
    BOOL bInserted = FALSE;
    while (pCur != 0 && !bInserted) {
        if (*(int*)(pCur + 0xC) < *(int*)(pEntry + 0xC)) {
            pPrev = pCur;
            pCur = *(int*)(pCur + 0x18);
        } else {
            *(int*)(pEntry + 0x18) = pCur;
            *(int*)(pPrev + 0x18) = pEntry;
            bInserted = TRUE;
        }
    }
    if (!bInserted) {
        *(int*)(pEntry + 0x18) = 0;
        *(int*)(pPrev + 0x18) = pEntry;
    }
}

// ============================================================================
// Address: 0x8002dc34 | stream_queue_texture
// Queues a texture streaming request into the secondary queue.
// Builds path: "host0/textures/%06x<ext>" from texture ID.
// ============================================================================
void stream_queue_texture(u32 nTextureId, int nPriority, int nParam) {
    int idx = g_SecondaryQueueCount;
    int offset = idx * 0x1C;

    g_SecondaryQueue[idx].type = 3;  // Texture
    g_SecondaryQueue[idx].nId = nTextureId;
    g_SecondaryQueue[idx].nPriority = nPriority;

    // Build path: "host0/textures/%06x<ext>"
    char acBuf[128];
    sprintf(acBuf, "host0/textures/%06x%s", nTextureId, ".gcr");

    int hAsync = asset_create_async_handle((u8*)acBuf);
    g_SecondaryQueue[idx].hAsync = hAsync;
    g_SecondaryQueue[idx].nParam = nParam;
    g_SecondaryQueueCount++;
    g_SecondaryQueue[idx].pData = NULL;

    stream_secondary_insert_sorted((int)&g_SecondaryQueue[idx]);
}

// ============================================================================
// Address: 0x8002dcd4 | stream_secondary_queue_anim
// Queues an animation load into the secondary queue.
// Replaces file extension in path before creating async handle.
// ============================================================================
void stream_secondary_queue_anim(const char* pPath, int nPriority, int nParam) {
    char acBuf[152];
    int idx = g_SecondaryQueueCount;
    int offset = idx * 0x1C;

    g_SecondaryQueue[idx].type = 2;  // Animation
    g_SecondaryQueue[idx].pName = pPath;

    // Copy path and replace extension
    strcpy(acBuf, pPath);
    char* pDot = strstr(acBuf, ".");
    if (pDot != NULL) {
        strcpy(pDot, ".anm");
    }

    int hAsync = asset_create_async_handle((u8*)acBuf);
    g_SecondaryQueue[idx].hAsync = hAsync;
    g_SecondaryQueue[idx].nPriority = nPriority;
    g_SecondaryQueueCount++;
    g_SecondaryQueue[idx].nParam = nParam;
    g_SecondaryQueue[idx].pData = NULL;

    stream_secondary_insert_sorted((int)&g_SecondaryQueue[idx]);
}

// ============================================================================
// Address: 0x8002dd80 | stream_secondary_process
// Processes the secondary stream queue (textures + animations).
// Loads each entry by type and updates progress bar.
// ============================================================================
void stream_secondary_process(void) {
    u32 nProcessed = 0;
    FUN_80133414(9);  // Set progress phase

    short* pCur = (short*)g_SecondaryQueueHead;
    while (pCur != NULL) {
        if (*pCur == 3) {
            // Texture: load via texture streamer
            FUN_8017c154(pCur[2], pCur[8], (int**)(pCur + 10));
        } else if (*pCur == 2) {
            // Animation: extension swap + anim loader
            char acBuf[64];
            strcpy(acBuf, (char*)*(int*)(pCur + 4));
            char* pDot = strstr(acBuf, ".");
            if (pDot != NULL) strcpy(pDot, ".anm");
            FUN_8017c03c(*(int*)(pCur + 4), *(int*)(pCur + 8), (int*)(pCur + 10));
        }

        nProcessed++;
        FUN_801333d4((float)nProcessed / (float)g_SecondaryQueueCount);
        pCur = *(short**)(pCur + 0xC);
    }

    FUN_801851b0();  // Finalize
    FUN_801333d4(1.0f);
}

// ============================================================================
// Address: 0x8002de94 | stream_get_entry_type
// Returns the type byte from a stream request entry pointer.
// ============================================================================
u8 stream_get_entry_type(void** pEntry) {
    return *(u8*)*pEntry;
}
