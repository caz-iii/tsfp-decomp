// Address: 0x80028eec-0x80029318 | Name: anim_events | Date: 2026-08-05
// Animation event system: continuity checking, event queries, frame range
// tests, cutscene loading, animtags data path.

#include "common.h"
#include "animation.h"

// ============================================================================
// Address: 0x80028eec | anim_motion_continuity_check
// Detects animation motion discontinuity (clip jumps) by comparing distance
// between adjacent keyframe positions. Returns 1 if a jump is detected.
// Called by skel_pose_update / skel_pose_sample_bounds to snap interpolation.
// ============================================================================
BOOL anim_motion_continuity_check(int* pAnimCtx) {
    int iFrame;
    float afPosA[3];  // position at frame N
    float afPosB[3];  // position at frame N+1
    float fDistSq;

    if (g_AnimCurrentData == 0 || pAnimCtx == NULL) {
        return FALSE;
    }

    iFrame = (int)((float)pAnimCtx[2] * FRAMES_PER_SECOND);
    if (iFrame < 1 || *pAnimCtx - 10 < iFrame) {
        return FALSE;
    }

    // Get positions at adjacent frames
    anim_keyframe_pose_query(iFrame, 0, (int*)afPosA, NULL);
    anim_keyframe_pose_query(iFrame + 1, 0, (int*)afPosB, NULL);

    // Check squared distance between them
    fDistSq = (afPosB[0] - afPosA[0]) * (afPosB[0] - afPosA[0]) +
              (afPosB[1] - afPosA[1]) * (afPosB[1] - afPosA[1]) +
              (afPosB[2] - afPosA[2]) * (afPosB[2] - afPosA[2]);

    if (fDistSq <= DISCONTINUITY_THRESHOLD) {
        // Check one more frame ahead for two-frame discontinuities
        int iFrame2 = (int)(FRAMES_PER_SECOND *
            ((float)pAnimCtx[2] + 1.0f / (float)g_GameFrameRate));
        if (iFrame2 - iFrame == 2) {
            anim_keyframe_pose_query(iFrame + 1, 0, (int*)afPosA, NULL);
            anim_keyframe_pose_query(iFrame + 2, 0, (int*)afPosB, NULL);
            fDistSq = (afPosB[0] - afPosA[0]) * (afPosB[0] - afPosA[0]) +
                      (afPosB[1] - afPosA[1]) * (afPosB[1] - afPosA[1]) +
                      (afPosB[2] - afPosA[2]) * (afPosB[2] - afPosA[2]);
            if (fDistSq > DISCONTINUITY_THRESHOLD) {
                return TRUE;
            }
        }
        return FALSE;
    }
    return TRUE;
}

// ============================================================================
// Address: 0x8002909c | anim_stub_noop
// Empty stub — no operation. Likely a removed debug callback.
// ============================================================================
void anim_stub_noop(void) {
    return;
}

// ============================================================================
// Address: 0x800290a0 | anim_load_cutscene_asset
// Loads cutscene animation asset from assets/ts/cutscenes/ directory.
// Builds path string and triggers async animation load.
// Returns the animation index (param_10 + 5).
// ============================================================================
int anim_load_cutscene_asset(int nCutsceneId, int nAnimIndex) {
    char acPathBuf[516];

    // Build path: "assets/ts/cutscenes/<name>"
    FUN_80387fb8(acPathBuf, &g_AnimFormatTable, "assets/ts/cutscenes/", nCutsceneId);
    // Trigger async load
    FUN_80022438(acPathBuf, nAnimIndex + 5, 0);
    return nAnimIndex + 5;
}

// ============================================================================
// Address: 0x800290f8 | anim_get_animtags_path
// Returns pointer to "data/animtags.dat" path string.
// ============================================================================
const char* anim_get_animtags_path(void) {
    return "data/animtags.dat";
}

// ============================================================================
// Address: 0x80029108 | anim_get_current_frame
// Gets the current animation frame index for an entity.
// ============================================================================
int anim_get_current_frame(int nEntityHandle) {
    int iAnimState;
    int nFrame;

    iAnimState = FUN_80025868(nEntityHandle);
    FUN_80022ed8(iAnimState, &nFrame, NULL);
    return nFrame;
}

// ============================================================================
// Address: 0x80029138 | anim_event_find_nth_by_mask
// Searches animation event table for the Nth event matching a flag mask.
// Returns the event's frame number, or 0xFFFFFFFF (-1) if not found.
// ============================================================================
u32 anim_event_find_nth_by_mask(int nAnimHandle, u16 uMask, int nNth) {
    int iEventTable;
    int nEventCount;
    int i;
    int iOffset;

    iEventTable = FUN_8001d6e0(nAnimHandle);
    if (iEventTable == 0) {
        return 0xFFFFFFFF;
    }

    nEventCount = (int)*(s16*)(iEventTable + 6);
    iOffset = 0;
    for (i = 0; i < nEventCount; i++) {
        if ((uMask & *(u16*)(iEventTable + iOffset + 10)) != 0) {
            nNth--;
            if (nNth == 0) break;
        }
        iOffset += 4;
    }

    if (nNth == 0) {
        return (u32)*(u16*)(iEventTable + i * 4 + 8);
    }
    return 0xFFFFFFFF;
}

// ============================================================================
// Address: 0x800291dc | anim_entity_has_data
// Checks if an entity has valid animation data loaded.
// Returns TRUE if the entity's animation resource is resolved.
// ============================================================================
BOOL anim_entity_has_data(int nEntityHandle) {
    int* pAnimState;
    int iAnimData;

    pAnimState = (int*)FUN_80025868(nEntityHandle);
    iAnimData = FUN_8001d6e0(*pAnimState);
    return (iAnimData != 0) ? TRUE : FALSE;
}

// ============================================================================
// Address: 0x8002920c | anim_has_event_with_mask
// Checks if an animation has any event matching the given flag mask.
// Returns TRUE on first match.
// ============================================================================
BOOL anim_has_event_with_mask(int nAnimHandle, u16 uMask) {
    int iEventTable;
    int nEventCount;
    int iOffset;

    if (nAnimHandle <= 0) return FALSE;

    iEventTable = FUN_8001d6e0(nAnimHandle);
    if (iEventTable == 0) return FALSE;

    nEventCount = (int)*(s16*)(iEventTable + 6);
    iOffset = 0;
    for (int i = 0; i < nEventCount; i++) {
        if ((uMask & *(u16*)(iEventTable + iOffset + 10)) != 0) {
            return TRUE;
        }
        iOffset += 4;
    }
    return FALSE;
}

// ============================================================================
// Address: 0x80029294 | anim_frame_in_range_wrapped
// Tests if a frame index falls within a wrapped range [A, B).
// Handles wrap-around case where end < start (looping animation).
// ============================================================================
BOOL anim_frame_in_range_wrapped(int nFrame, int nRangeStart, int nRangeEnd) {
    if (nRangeEnd < nRangeStart) {
        // Wrapped range: valid if frame < end OR frame >= start
        if (nFrame < nRangeEnd || nRangeStart <= nFrame) {
            return TRUE;
        }
    } else {
        // Normal range: valid if frame < end AND frame >= start
        if (nFrame < nRangeEnd && nRangeStart <= nFrame) {
            return TRUE;
        }
    }
    return FALSE;
}

// ============================================================================
// Address: 0x800292d4 | anim_frame_in_range_inclusive
// Tests if a frame is within range, with special zero-length handling.
// If nFrame == 0 and nRangeStart == 0, returns TRUE (animation start).
// ============================================================================
BOOL anim_frame_in_range_inclusive(int nFrame, int nRangeStart, int nRangeEnd) {
    if (nFrame == 0) {
        if (nRangeStart == 0) return TRUE;
    } else if (nRangeStart < nRangeEnd) {
        if (nRangeEnd < nFrame || nFrame <= nRangeStart) {
            return TRUE;
        }
    } else {
        if (nRangeEnd < nFrame && nFrame <= nRangeStart) {
            return TRUE;
        }
    }
    return FALSE;
}

// ============================================================================
// Address: 0x8002932c | anim_event_triggered_this_frame
// Checks if a specific animation event was triggered during the current frame.
// Uses frame range tests to detect if the playhead crossed the event frame.
// ============================================================================
BOOL anim_event_triggered_this_frame(int nEntityHandle, u16 uMask, int nNth) {
    int* pAnimState;
    u32 uEventFrame;
    int nCurrentFrame;
    int nPrevFrame;

    pAnimState = (int*)FUN_80025868(nEntityHandle);
    uEventFrame = anim_event_find_nth_by_mask(*pAnimState, uMask, nNth);

    if ((int)uEventFrame < 0) {
        return FALSE;
    }

    FUN_80022ed8((int)pAnimState, &nCurrentFrame, &nPrevFrame);

    if ((float)pAnimState[0xd] <= ANIM_SPEED_THRESHOLD) {
        // Reverse playback or very slow
        if ((float)pAnimState[0xd] < ANIM_SPEED_THRESHOLD) {
            if (anim_frame_in_range_wrapped(uEventFrame, nCurrentFrame, nPrevFrame)) {
                return TRUE;
            }
        }
    } else {
        // Forward playback
        if (anim_frame_in_range_inclusive(uEventFrame, nCurrentFrame, nPrevFrame)) {
            return TRUE;
        }
    }
    return FALSE;
}

// ============================================================================
// Address: 0x800293f0 | anim_event_scan_active
// Scans all events in an entity's animation for any active event matching mask.
// Iterates event table and tests each against current frame range.
// ============================================================================
void anim_event_scan_active(int nEntityHandle, u16 uMask) {
    int* pAnimState;
    int iEventTable;
    int nCurrentFrame, nPrevFrame;
    int i;
    int iOffset;

    pAnimState = (int*)FUN_80025868(nEntityHandle);
    iEventTable = FUN_8001d6e0(*pAnimState);

    if (iEventTable == 0) return;

    FUN_80022ed8((int)pAnimState, &nCurrentFrame, &nPrevFrame);

    iOffset = 0;
    for (i = 0; i < *(s16*)(iEventTable + 6); i++) {
        if ((uMask & *(u16*)(iEventTable + iOffset + 10)) != 0) {
            u32 uFrame = (u32)*(u16*)(iEventTable + iOffset + 8);
            if (anim_frame_in_range_inclusive(uFrame, nCurrentFrame, nPrevFrame)) {
                break;  // Found active event
            }
        }
        iOffset += 4;
    }

    // Epilogue
    FUN_80382738();
}

// ============================================================================
// Address: 0x800294a8 | anim_event_has_passed
// Checks if a specific animation event frame has been passed.
// Returns signed comparison: positive if passed, 0 if not found or not yet.
// ============================================================================
int anim_event_has_passed(int nEntityHandle, u16 uMask, int nNth) {
    int* pAnimState;
    u32 uEventFrame;
    u32 uCurrentFrame;

    pAnimState = (int*)FUN_80025868(nEntityHandle);
    uEventFrame = anim_event_find_nth_by_mask(*pAnimState, uMask, nNth);

    if (uEventFrame == 0xFFFFFFFF) {
        return 0;
    }

    FUN_80022ed8((int)pAnimState, &uCurrentFrame, NULL);

    // Signed comparison: frame passed if current >= event
    return ((int)uCurrentFrame >> 31) +
           ((u32)(uEventFrame <= uCurrentFrame) - ((int)uEventFrame >> 31));
}
