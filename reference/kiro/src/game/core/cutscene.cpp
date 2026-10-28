// Address: 0x8003a5f0-0x8003adf8 | Name: cutscene | Date: 2026-08-05
// Cutscene/cinematic system: initialization, subtitle rendering,
// visual effects (letterbox, fade, flash), keyframe processing.
//
// Cutscene info file path: "assets/ts/cutscenes/<name>/gamecube_output/<name>_csinfo.txt"
// Subtitle table: up to 60 entries (stride 0x2C), stored at DAT_80515430
// Subtitle entry: time_start, duration, string_id, color1, color2, flags

#include "common.h"
#include "cutscene.h"

#define MAX_SUBTITLE_ENTRIES 60
#define SUBTITLE_STRIDE      0x2C

// ============================================================================
// Address: 0x8003a5f0 | cutscene_reset_playback_id
// Resets the current cutscene playback ID to -1 (none playing).
// ============================================================================
void cutscene_reset_playback_id(void) {
    g_CutscenePlaybackId = -1;
}

// ============================================================================
// Address: 0x8003a5fc | cutscene_skip_wrapper
// Wrapper that calls the cutscene skip handler.
// ============================================================================
void cutscene_skip_wrapper(int nParam) {
    FUN_80253d24(nParam);
}

// ============================================================================
// Address: 0x8003a61c | cutscene_system_init
// Initializes the cutscene system. Clears subtitle table (60 entries),
// resets all counters and state variables.
// ============================================================================
void cutscene_system_init(void) {
    memset(&g_SubtitleTable, 0, 0xA80);
    // Initialize all subtitle entries with invalid ID
    for (int i = 0; i < MAX_SUBTITLE_ENTRIES; i++) {
        g_SubtitleTable[i].nStringId = -1;
    }
    memset(&g_CutsceneState, 0, 0x28);
    g_CutsceneCounter1 = 0;
    g_CutsceneCounter2 = 0;
    g_CutsceneCounter3 = 0;
    g_CutsceneCounter4 = 0;
    g_CutsceneCounter5 = 0;
    g_SubtitleCount = 0;
    g_SubtitleDataPtr = 0;
}

// ============================================================================
// Address: 0x8003a6a8 | cutscene_build_info_path
// Builds the path to a cutscene's info file.
// Format: "assets/ts/cutscenes/<name>/gamecube_output/<name>_csinfo.txt"
// ============================================================================
void cutscene_build_info_path(char* pOutBuf) {
    const char* pName = FUN_8003af7c();  // Get cutscene name
    sprintf(pOutBuf, "%s%s%s_csinfo.txt",
            "assets/ts/cutscenes/", pName, "/gamecube_output/");
}

// ============================================================================
// Address: 0x8003a704 | cutscene_info_file_exists
// Checks if the cutscene info file exists in any loaded pack.
// ============================================================================
BOOL cutscene_info_file_exists(void) {
    char acPath[520];
    cutscene_build_info_path(acPath);
    return pack_get_asset_ptr((u8*)acPath, NULL) != 0;
}

// ============================================================================
// Address: 0x8003a734 | cutscene_is_available
// Returns TRUE if a cutscene info file is available for playback.
// ============================================================================
BOOL cutscene_is_available(void) {
    return cutscene_info_file_exists() != 0;
}

// ============================================================================
// Address: 0x8003a75c | cutscene_render_subtitles
// Renders active cutscene subtitles on screen.
// Iterates subtitle entries, checks if current playback time falls
// within entry's time range, then renders text with appropriate style.
// Supports two modes: drop-shadow text and scrolling text box.
// ============================================================================
void cutscene_render_subtitles(void) {
    int pCutscene = FUN_8006a848();
    FUN_8016f0e4(0, 0, g_ScreenWidth, g_ScreenHeight);
    FUN_8016f00c(1);
    FUN_8016e8ac(0);

    if (pCutscene == 0) return;

    text_set_font_table(0x15);
    g_TextShadowEnabled = 1;

    int nTimer = game_get_round_timer();
    if (nTimer == 0) { g_TextShadowEnabled = 0; return; }

    for (int i = 0; i < g_SubtitleCount; i++) {
        float fCurrentTime = *(float*)(pCutscene + 8);
        float fStart = g_SubtitleData[i].fStartTime;
        float fDuration = g_SubtitleData[i].fDuration;

        if (fCurrentTime >= fStart && fCurrentTime <= fStart + fDuration) {
            float fProgress = (fCurrentTime - fStart) / fDuration;
            const char* pText = get_string_by_id(g_SubtitleData[i].nStringId);

            if ((g_SubtitleData[i].flags & 1) == 0) {
                // Standard drop-shadow subtitle
                text_draw_char_drop_shadow(/* centered position */,
                    pText, g_SubtitleData[i].color1, g_SubtitleData[i].color2);
            } else {
                // Scrolling text box (for longer dialogue)
                text_draw_string_shadowed(/* scrolling box coords */);
            }
        }
    }
    g_TextShadowEnabled = 0;
}

// ============================================================================
// Address: 0x8003aa88 | cutscene_apply_visual_effects
// Applies cutscene-driven visual effects each frame:
// - Screen flash (white/color overlay)
// - Screen fade (in/out with alpha)
// - Color tint overlay
// - Letterbox bars (top/bottom black bars for widescreen)
// - Health-based vignette (red screen edge when damaged)
// Also handles non-cutscene camera effects (fade-from-black on spawn).
// ============================================================================
void cutscene_apply_visual_effects(void) {
    int pCutscene = FUN_8006a848();
    if (pCutscene == 0) return;

    // Interpolate pose parameters for current frame
    float fHeight, fWidth;
    int nFlashColor, nTintColor, nFadeAmount;
    anim_interpolate_pose(pCutscene, *(int*)(pCutscene + 0xC),
        &fHeight, &fWidth, &nFlashColor, &nTintColor, &nFadeAmount);

    // Apply screen flash
    if (fHeight > 0.0f) {
        FUN_800ce07c(/* flash params: color, intensity, region */);
    }

    // Apply fade
    if (fWidth > 0.0f) {
        FUN_800ce510(/* fade intensity */);
    }

    // Apply color tint overlay
    if (nFlashColor != 0) {
        FUN_8016f4bc(0, 0, g_ScreenWidth, g_ScreenHeight, nFlashColor);
    }

    // Apply full-screen color with alpha
    if (nTintColor != 0) {
        FUN_8016f4bc(0, 0, g_ScreenWidth, g_ScreenHeight, nTintColor | 0xFF);
    }

    // Health-based vignette / damage indicator
    if (pCutscene != 0 && *(int*)(pCutscene + 0x1C) != 0) {
        float fHealthRatio = *(float*)(pCutscene + 0x18) / *(float*)(pCutscene + 0x10);
        u8 nAlpha = (u8)(255.0f * (1.0f - min(fHealthRatio, 1.0f)));
        FUN_8016f4bc(0, 0, g_ScreenWidth, g_ScreenHeight, nAlpha);
    }

    // Widescreen letterbox bars
    if (!is_widescreen()) {
        int nBarHeight = (int)(LETTERBOX_RATIO * (float)g_ScreenHeight);
        FUN_8016f4bc(0, 0, g_ScreenWidth, nBarHeight, 0x7F);
        FUN_8016f4bc(0, g_ScreenHeight - nBarHeight, g_ScreenWidth, g_ScreenHeight, 0x7F);
    }

    // Render subtitles on top
    cutscene_render_subtitles();

    // Cutscene end: full black overlay
    if (pCutscene != 0 && *(int*)(pCutscene + 0x20) == 0 &&
        (g_GameStateFlags & 0x20000000) != 0) {
        FUN_8016f4bc(0, 0, g_ScreenWidth, g_ScreenHeight, 0xFF);
    }
}

// ============================================================================
// Address: 0x8003adb4 | cutscene_process_keyframe
// Processes a cutscene animation keyframe. Reads 32 bone transforms
// from the keyframe data and applies them.
// ============================================================================
void cutscene_process_keyframe(int pCutscene) {
    anim_keyframe_lookup(*(int*)(pCutscene + 0xC));
    for (int i = 0; i < 32; i++) {
        // Process bone data
    }
    FUN_802546b0();  // Apply transforms
}

// ============================================================================
// Address: 0x8003adf8 | cutscene_flag_is_set
// Checks if a specific flag bit is set in the cutscene's keyframe data.
// Used for triggering events at specific frames during playback.
// ============================================================================
BOOL cutscene_flag_is_set(u32 nFlagBit) {
    int pCutscene = FUN_8006a848();
    if (pCutscene == 0) return FALSE;
    int* pKeyframe = anim_keyframe_lookup(*(int*)(pCutscene + 0xC));
    int nWord = nFlagBit >> 5;
    int nBit = nFlagBit & 31;
    return (pKeyframe[nWord + 0xE] & (1 << nBit)) != 0;
}

// ============================================================================
// Batch 059: Cutscene data loading and pak management
// ============================================================================

// Cutscene table: 20 entries at stride 0x24 (DAT_80471c54)
// Each entry: level_mapping_id (at +0x18), name_ptr (at +0x14), pak_ptr (at +0x04)

// Address: 0x8003ae78 | cutscene_get_level_mapping
// Returns the cutscene index mapped to a level ID. Max 20 cutscenes.
int cutscene_get_level_mapping(int nLevelId) {
    if (nLevelId < 0 || nLevelId >= 20) return -1;
    return g_CutsceneTable[nLevelId].nMappingId;
}

// Address: 0x8003aea8 | cutscene_build_pak_path
// Builds cutscene .pak file path: "<dir>/<name>.pak"
void cutscene_build_pak_path(char* pOutBuf, int nBufSize, int nCutsceneIdx) {
    const char* pDir = g_CutsceneTable[nCutsceneIdx].pPakDir;
    const char* pName = g_CutsceneTable[nCutsceneIdx].pName;
    sprintf(pOutBuf, "%s/%s.pak", pDir, pName);
}

// Address: 0x8003af14 | cutscene_set_name_and_pak
// Directly sets the active cutscene name and pak path.
void cutscene_set_name_and_pak(const char* pName, const char* pPakPath) {
    g_ActiveCutsceneName = pName;
    g_ActiveCutscenePak = pPakPath;
}

// Address: 0x8003af20 | cutscene_resolve_from_level
// Resolves the cutscene name and pak path from the current level ID.
// Uses cutscene_skip_wrapper to find the cutscene index for this level.
void cutscene_resolve_from_level(void) {
    int nIdx = cutscene_skip_wrapper(g_CurrentLevel);
    g_ActiveCutsceneName = NULL;
    if (nIdx >= 0 && nIdx < 20) {
        g_ActiveCutsceneName = g_CutsceneTable[nIdx].pName;
        g_ActiveCutscenePak = g_CutsceneTable[nIdx].pPakDir;
    }
}

// Address: 0x8003af7c | cutscene_get_name
// Returns the active cutscene name. Lazy-resolves from level if needed.
const char* cutscene_get_name(void) {
    if (g_ActiveCutsceneName == NULL && g_CutsceneSlotId != -1) {
        cutscene_resolve_from_level();
    }
    return g_ActiveCutsceneName;
}

// Address: 0x8003afc4 | cutscene_get_pak_path
// Returns the active cutscene pak path. Lazy-resolves from level if needed.
const char* cutscene_get_pak_path(void) {
    if (g_ActiveCutscenePak == NULL && g_CutsceneSlotId != -1) {
        cutscene_resolve_from_level();
    }
    return g_ActiveCutscenePak;
}

// Address: 0x8003b00c | cutscene_parse_info_file
// Parses the cutscene _csinfo.txt file from pack.
// Reads "mesh" entries (model paths) and "pack" entries (additional paks).
// For each mesh: builds full path and queues via stream_queue_callback.
// For packs: builds pak path with extension substitution.
void cutscene_parse_info_file(void) {
    int nFileHandle = 0;
    char acPakPath[128];
    char acLineBuf[276];
    const char* pCsName = cutscene_get_name();

    memset(&g_CutsceneModelTable, 0, 0x8C0);
    nFileHandle = cutscene_info_file_exists();
    acPakPath[0] = '\0';
    int nModelIdx = 0;

    // Parse line by line
    while (FUN_8012e77c(&nFileHandle, acLineBuf) >= 0) {
        // "mesh" keyword: load model
        if (strcmp(acLineBuf, "mesh") == 0) {
            FUN_8012e77c(&nFileHandle, acLineBuf);
            // Strip extension, add gamecube_output path
            FUN_8012ea4c(acLineBuf, &g_DatExt, &g_SearchPath);
            // Build: "assets/ts/cutscenes/<name>/gamecube_output/<model>"
            sprintf(&g_CutsceneModelPaths[nModelIdx],
                    "%s%s%s/%s", "assets/ts/cutscenes/", pCsName,
                    "/gamecube_output/", acLineBuf);
            stream_queue_callback(&g_CutsceneModelPaths[nModelIdx]);
            nModelIdx += 0x50;
        }
        // "cspack" keyword: additional pack file
        if (strcmp(acLineBuf, "cspack") == 0) {
            FUN_8012e77c(&nFileHandle, acLineBuf);
            const char* pPakDir = cutscene_get_pak_path();
            sprintf(acPakPath, "%s/%s", pPakDir, acLineBuf);
            // Fix extension to .pak
            int nLen = strlen(acPakPath);
            sprintf(acPakPath + nLen - 4, ".pak");
        }
    }

    // Open additional cutscene pak if found
    g_CutsceneExtraPak = -1;
    if (acPakPath[0] != '\0') {
        g_CutsceneExtraPak = pack_open(acPakPath, 0, 0);
        if (g_CutsceneExtraPak == -1) {
            halt_stub_assert();
        }
    }
}

// Address: 0x8003b194 | cutscene_open_and_parse
// Opens the cutscene pak file and triggers info file parsing.
// If pak open fails, transitions to next level instead.
void cutscene_open_and_parse(void) {
    char acPath[264];
    cutscene_build_pak_path(acPath, 0x100, g_CutsceneSlotId);

    int hPak = pack_open(acPath, 1, 0);
    if (hPak == -1) {
        match_transition_next_level();
    } else {
        cutscene_parse_info_file();
    }
}

// Address: 0x8003b1f0 | cutscene_find_model_by_name
// Searches the cutscene model list for a model matching the given name.
// Returns index, or -1 if not found.
int cutscene_find_model_by_name(const char* pName) {
    int iOffset = 0;
    for (int i = 0; i < g_CutsceneModelCount; i++) {
        const char* pEntry = g_CutsceneModelTable + iOffset + 4;
        if (strcmp(pName, pEntry) == 0) return i;
        if (strlen(pEntry) == strlen(pName)) return i;  // Length match fallback
        iOffset += 0x114;
    }
    return -1;
}

// Address: 0x8003b288 | cutscene_get_model_data
// Finds a cutscene model by name and returns its associated data pointer.
int cutscene_get_model_data(const char* pName) {
    int nIdx = cutscene_find_model_by_name(&g_CutsceneModelList, g_CutsceneModelCount, pName);
    if (nIdx < 0) return -1;
    return g_CutsceneModelEntries[nIdx].nDataPtr;
}
