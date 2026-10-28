// Address: 0x8032a600-0x8032a828 | Name: match_session_query | Date: 2026-09-12
// Match / arcade session state query helpers.
//
// These are small accessor/predicate functions over the shared arcade-match
// session-state global (g_MatchSession / DAT_80612be4) and the game-mode
// configuration tables (DAT_80480f10 stride 0x13 words, DAT_80480f50 stride
// 0x4c bytes). Callers span the arcade custom-match editor (aveditor_*),
// character animation preview (avanim_*), the loadout menus (menu_loadout_*)
// and level entity spawning (level_spawn_all_entities_pass), so this lives in
// the shared match/session core rather than any one screen module.
//
// g_MatchSession layout (offsets referenced here):
//   +0x280  : participant count (stride-0xe4 array follows at +0x288)
//   +0x288  : participant array, stride 0xe4
//       +0x1c (abs +0x2bc) : participant kind/state (5 == active)
//       +0x58 (abs +0x2e0) : participant score (>0 == has scored)
//   +0x81a4 : squad count (stride-0xb4 array follows at +0x81a8)
//   +0x81a8 : squad array, stride 0xb4; +0x0 == owning local-player id
//   +0xdd10 : history count (stride-0x8c array follows at +0xdd94)
//   +0xdd94 : history array, stride 0x8c; +0x0 == key

#include "common.h"

// ---- per-file externs for raw symbols referenced below --------------------
extern int  DAT_80612be4;          // g_MatchSession base pointer (int handle)
extern char DAT_805015bd;          // build/config flag byte
extern u32  DAT_80480f10;          // game-mode config table A (stride 0x13 words)
extern u32  DAT_80480f50;          // game-mode config table B (stride 0x4c bytes)
extern int  DAT_80612d00;          // cached session flags word

extern int  FUN_8033226c(int);     // session readiness probe -> nonzero if ready
extern int  FUN_8032a900(void);    // -> count of local-player-owned squads
extern int  FUN_803826e8(void);    // -> current local player id (lock)
extern void FUN_80382734(void);    // release local player id lock
extern uint match_squad_entry_flag_set(int); // 0x8032ae4c: TRUE when entry field +0xa8 == 1

// ============================================================================
// Address: 0x8032a600 | match_mode_is_teamplay_class
// Returns TRUE when the mode descriptor's class field (+0x34) is one of the
// team-based classes {1, 3, 5}.
// ============================================================================
int match_mode_is_teamplay_class(int pModeDesc) {
    int nClass = *(int *)(pModeDesc + 0x34);
    if (nClass != 3) {
        if (nClass < 3) {
            if (nClass != 1) {
                return 0;
            }
        } else if (nClass != 5) {
            return 0;
        }
    }
    return 1;
}

// ============================================================================
// Address: 0x8032a638 | match_mode_is_type5
// Returns TRUE when nMode == 5. On non-Gekko builds (DAT_805015bd != 7) the
// compiler emitted a branchless equality; the Gekko build uses countLeadingZeros.
// ============================================================================
int match_mode_is_type5(int nMode) {
    if (DAT_805015bd != '\a') {
        int iVar2 = -nMode + 5;
        return iVar2 - ((u32)(iVar2 == 0) + -nMode + 4);
    }
    return (int)(countLeadingZeros(5 - nMode) >> 5);
}

// ============================================================================
// Address: 0x8032a670 | match_session_find_active_entry
// Scans the participant array for the first entry whose kind (+0x1c) == 5 and
// returns a pointer to that entry (as an int handle), or 0 if none.
// ============================================================================
int match_session_find_active_entry(void) {
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0x280);
    if (nRemaining < 1) {
        return 0;
    }
    do {
        if (*(int *)(DAT_80612be4 + iOff + 0x2bc) == 5) {
            return DAT_80612be4 + iOff + 0x288;
        }
        iOff += 0xe4;
        nRemaining += -1;
    } while (nRemaining != 0);
    return 0;
}

// ============================================================================
// Address: 0x8032a6b4 | match_session_any_entry_has_score
// Returns TRUE if any participant has a positive score (+0x58 > 0).
// ============================================================================
int match_session_any_entry_has_score(void) {
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0x280);
    if (nRemaining < 1) {
        return 0;
    }
    do {
        if (0 < *(int *)(DAT_80612be4 + iOff + 0x2e0)) {
            return 1;
        }
        iOff += 0xe4;
        nRemaining += -1;
    } while (nRemaining != 0);
    return 0;
}

// ============================================================================
// Address: 0x8032a6f4 | match_mode_get_config
// Returns a pointer to game-mode config table A entry for nMode
// (stride 0x13 words = 0x4c bytes).
// ============================================================================
u32 *match_mode_get_config(int nMode) {
    return &DAT_80480f10 + nMode * 0x13;
}

// ============================================================================
// Address: 0x8032a708 | match_session_count_matching
// Counts history entries (stride 0x8c) whose key (+0x0) equals nKey.
// ============================================================================
int match_session_count_matching(int nKey) {
    int nCount = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0xdd10);
    int iOff = 0;
    if (0 < nRemaining) {
        do {
            u32 uMatch = countLeadingZeros(nKey - *(int *)(DAT_80612be4 + iOff + 0xdd94));
            nCount += (int)(uMatch >> 5);
            nRemaining += -1;
            iOff += 0x8c;
        } while (nRemaining != 0);
    }
    return nCount;
}

// ============================================================================
// Address: 0x8032a768 | match_session_slot_clamp
// For a valid mode config B limit (!= -1), returns the sign-corrected
// comparison of the matching-history count against the configured limit;
// returns 1 when the limit is unset (-1).
// ============================================================================
int match_session_slot_clamp(int nMode) {
    int iVar1 = 1;
    u32 *pLimit = (u32 *)(&DAT_80480f50 + nMode * 0x4c);
    if (*(int *)pLimit != -1) {
        iVar1 = match_session_count_matching(nMode);
        iVar1 = ((int)*pLimit >> 0x1f) +
                ((u32)(iVar1 + 1U <= *pLimit) - ((int)(iVar1 + 1U) >> 0x1f));
    }
    return iVar1;
}

// ============================================================================
// Address: 0x8032a7cc | match_session_is_ready
// Boolean wrapper: TRUE when the readiness probe returns nonzero.
// ============================================================================
int match_session_is_ready(void) {
    int iVar1 = FUN_8033226c(0);
    return iVar1 != 0;
}

// ============================================================================
// Address: 0x8032a7f8 | match_session_has_local_squad
// Returns TRUE when the local player owns at least one squad entry.
// ============================================================================
u32 match_session_has_local_squad(void) {
    u32 uVar1 = (u32)FUN_8032a900();
    return (-uVar1 & ~uVar1) >> 0x1f;
}

// ============================================================================
// Address: 0x8032a828 | match_session_get_flags
// Returns the cached session flags word.
// ============================================================================
int match_session_get_flags(void) {
    return DAT_80612d00;
}

// ============================================================================
// Address: 0x8032a830-0x8032aa10 | batch_925
// Session count caching + squad/history tally helpers (same module region).
// ============================================================================

// ---- additional per-file externs for batch_925 ----------------------------
extern int DAT_80612cf0;           // cached history count snapshot
extern int DAT_80612cec;           // cached (history + participant) count snapshot

// ============================================================================
// Address: 0x8032a830 | match_session_cache_history_count
// Snapshots the session history count (+0xdd10) into DAT_80612cf0.
// ============================================================================
void match_session_cache_history_count(void) {
    DAT_80612cf0 = *(int *)(DAT_80612be4 + 0xdd10);
}

// ============================================================================
// Address: 0x8032a844 | match_session_get_cached_history_count
// Returns the last snapshotted history count.
// ============================================================================
int match_session_get_cached_history_count(void) {
    return DAT_80612cf0;
}

// ============================================================================
// Address: 0x8032a84c | match_session_cache_total_count
// Snapshots history count (+0xdd10) + participant count (+0x280) into DAT_80612cec.
// ============================================================================
void match_session_cache_total_count(void) {
    DAT_80612cec = *(int *)(DAT_80612be4 + 0xdd10) + *(int *)(DAT_80612be4 + 0x280);
}

// ============================================================================
// Address: 0x8032a868 | match_session_total_index_base
// Returns the cached total count offset by nAdd (index-base helper).
// ============================================================================
int match_session_total_index_base(int nAdd) {
    return DAT_80612cec + nAdd;
}

// ============================================================================
// Address: 0x8032a874 | match_session_count_tag_bytes
// Counts entries in the stride-0x220 array (+0x41e0 count, +0x41e8 tag byte)
// whose tag byte equals nTag.
// ============================================================================
int match_session_count_tag_bytes(int nTag) {
    int nCount = 0;
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0x41e0);
    if (0 < nRemaining) {
        do {
            int iEntry = iOff + 0x41e8;
            iOff += 0x220;
            u32 uMatch = countLeadingZeros(nTag - (u32)*(byte *)(DAT_80612be4 + iEntry));
            nCount += (int)(uMatch >> 5);
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return nCount;
}

// ============================================================================
// Address: 0x8032a8b8 | match_session_count_squads_owned
// Counts squad-array entries (stride 0xb4 @ +0x81a8, count @ +0x81a4) whose
// owner id (+0x0) equals nOwner.
// ============================================================================
int match_session_count_squads_owned(int nOwner) {
    int nCount = 0;
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0x81a4);
    if (0 < nRemaining) {
        do {
            if (*(int *)(DAT_80612be4 + iOff + 0x81a8) == nOwner) {
                nCount += 1;
            }
            iOff += 0xb4;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return nCount;
}

// ============================================================================
// Address: 0x8032a900 | match_session_reset_local_squads
// Resets every squad entry owned by the current local player. Takes the local
// player id lock for the duration.
// NOTE: returns the local-player id in the return register (EAX); callers such
// as match_session_has_local_squad read it. Typed int to reflect that use.
// ============================================================================
int match_session_reset_local_squads(void) {
    int nLocalId = FUN_803826e8();
    int iOff = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int *pEntry = (int *)(DAT_80612be4 + iOff + 0x81a8);
        if (*pEntry == nLocalId) {
            match_squad_entry_flag_set((int)pEntry);
        }
        iOff += 0xb4;
    }
    FUN_80382734();
    return nLocalId;
}

// ============================================================================
// Address: 0x8032a9a8 | match_session_count_unowned_squads
// Counts squad entries with owner id 0. The ROM leaves the count in r3; the
// arcade editor tree renderer (match_editor_tree_draw) uses it as a loop bound.
// ============================================================================
int match_session_count_unowned_squads(void) {
    return match_session_count_squads_owned(0);
}

// ============================================================================
// Address: 0x8032a9cc | match_mode_default_limit
// Small lookup: mode 0 -> 30, modes 1..2 -> 50, otherwise 0. (per-mode default
// count/time limit)
// ============================================================================
int match_mode_default_limit(int nMode) {
    if (nMode == 1) {
        return 0x32;
    }
    if (nMode < 1) {
        if (-1 < nMode) {
            return 0x1e;
        }
    } else if (nMode < 3) {
        return 0x32;
    }
    return 0;
}

// ============================================================================
// Address: 0x8032aa10 | match_session_limit_reached
// Combines the per-mode default limit with the owned-squad count to produce a
// signed "limit reached" comparison.
// ============================================================================
u32 match_session_limit_reached(int nMode) {
    u32 uLimit = (u32)match_mode_default_limit(nMode);
    u32 uCount = (u32)match_session_count_squads_owned(nMode);
    return ((int)(uLimit ^ uCount) >> 1) - ((uLimit ^ uCount) & uLimit) >> 0x1f;
}

// ============================================================================
// Address: 0x8032aa64-0x8032ae60 | batch_926
// Capability/permission bitmask queries over the mode-config tables plus a
// couple of squad-scan dispatch helpers (same module region).
// ============================================================================

// ---- additional per-file externs for batch_926 ----------------------------
extern int DAT_80480f20;           // capability bitmask table A (stride 0x13 words)
extern int DAT_80480f24;           // config word (default cap A) within same row
extern int DAT_80480f28;           // capability bitmask table B (stride 0x13 words)
extern int DAT_80480f2c;           // config word (default cap B) within same row
extern int DAT_80480f40;           // per-mode callback table (stride 0x4c bytes)
extern int  FUN_8033435c(int *, int);  // capability-lock probe -> 0 if available
extern int  FUN_803826f0(void);         // -> current local player short id
extern void FUN_8038273c(void);         // release squad-scan lock

// ============================================================================
// Address: 0x8032aa64 | match_cap_a_available
// TRUE when capability bit nBit is set in table A for nMode and the capability
// lock probe reports it free.
// ============================================================================
int match_cap_a_available(int nBit, int nMode) {
    int bResult = 0;
    if (((1 << nBit & (&DAT_80480f20)[nMode * 0x13]) != 0) &&
        (FUN_8033435c((int *)0x0, nBit) == 0)) {
        bResult = 1;
    }
    return bResult;
}

// ============================================================================
// Address: 0x8032aacc | match_cap_b_available
// As match_cap_a_available but against capability table B.
// ============================================================================
int match_cap_b_available(int nBit, int nMode) {
    int bResult = 0;
    if (((1 << nBit & (&DAT_80480f28)[nMode * 0x13]) != 0) &&
        (FUN_8033435c((int *)0x0, nBit) == 0)) {
        bResult = 1;
    }
    return bResult;
}

// ============================================================================
// Address: 0x8032ab34 | match_cap_select_first_available
// For the current local player's mode, picks the capability accessor (A or B)
// and default cap word, then returns the first free capability bit (scans
// 0..0x19 when the default is taken). Runs under the local-id lock.
// ============================================================================
int match_cap_select_first_available(void) {
    u64 uPair = (u64)FUN_803826e8();
    int nMode = (int)(uPair >> 0x20);
    int nCat = (int)uPair;
    int nDefaultBit = 0;
    int nLimit = 0;
    int (*pfnProbe)(int, int) = (int (*)(int, int))0;

    if (nCat == 2) {
        nDefaultBit = *(int *)(&DAT_80480f2c + nMode * 0x4c);
        pfnProbe = match_cap_b_available;
        nLimit = 0x1a;
    } else if ((nCat < 2) && (0 < nCat)) {
        pfnProbe = match_cap_a_available;
        nDefaultBit = *(int *)(&DAT_80480f24 + nMode * 0x4c);
        nLimit = 0x1a;
    }

    int nResult = pfnProbe(nDefaultBit, nMode);
    if (nResult == 0) {
        int i = 0;
        while (i < nLimit && (pfnProbe(i, nMode) == 0)) {
            i = i + 1;
        }
    }
    FUN_80382734();
    return nResult;
}

// ============================================================================
// Address: 0x8032ac2c | match_cat_is_locked_pair
// TRUE when category is 1 or 2 and subtype is 2.
// ============================================================================
int match_cat_is_locked_pair(int nCat, int nSub) {
    if (((nCat == 1) || (nCat == 2)) && (nSub == 2)) {
        return 1;
    }
    return 0;
}

// ============================================================================
// Address: 0x8032ac54 | match_entry_is_locked_active
// TRUE when the descriptor (nCat/nSub at [0],[1]) is a locked pair and the
// referenced participant's state (+0x2bc, indexed by the short at +0xe) is one
// of {2,3,4} (active states).
// ============================================================================
int match_entry_is_locked_active(int *pDesc) {
    int bResult = 0;
    if (match_cat_is_locked_pair(pDesc[0], pDesc[1]) != 0) {
        int nState = *(int *)(DAT_80612be4 + *(short *)((int)pDesc + 0xe) * 0xe4 + 0x2bc);
        if ((nState == 2) || (nState == 4) || (nState == 3)) {
            bResult = 1;
        }
    }
    return bResult;
}

// ============================================================================
// Address: 0x8032acd0 | match_cap_apply_all
// Invokes the per-mode callback (table @ DAT_80480f40) if present, then applies
// every capability bit (0..0x19) via the A or B accessor per category.
// (Original read its arg via saved GPR r28; expressed here as pDesc parameter.)
// ============================================================================
void match_cap_apply_all(int *pDesc) {
    int (*pfnCb)(void) = *(int (**)(void))(&DAT_80480f40 + pDesc[1] * 0x4c);
    if (pfnCb != (int (*)(void))0) {
        pfnCb();
    }
    int nCat = pDesc[0];
    if (nCat == 2) {
        int i = 0;
        do {
            match_cap_b_available(i, pDesc[1]);
            i = i + 1;
        } while (i < 0x1a);
    } else if ((nCat < 2) && (0 < nCat)) {
        int i = 0;
        do {
            match_cap_a_available(i, pDesc[1]);
            i = i + 1;
        } while (i < 0x1a);
    }
}

// ============================================================================
// Address: 0x8032adac | match_cap_select_for_category
// Dispatches to match_cap_select_first_available for categories 1 and 2;
// returns -1 for any other category.
// ============================================================================
int match_cap_select_for_category(int nCat) {
    int nResult = -1;
    if (nCat == 2) {
        nResult = match_cap_select_first_available();
    } else if ((nCat < 2) && (0 < nCat)) {
        nResult = match_cap_select_first_available();
    }
    return nResult;
}

// ============================================================================
// Address: 0x8032ae04 | match_tag_entry_has_pending
// TRUE when descriptor state (+0x0) is 0 and the referenced stride-0x220 tag
// entry (indexed by [+0x29]) has a nonzero pending field (+0x50 or +0x88).
// ============================================================================
int match_tag_entry_has_pending(int *pDesc) {
    if ((pDesc[0] == 0) &&
        ((*(int *)(DAT_80612be4 + pDesc[0x29] * 0x220 + 0x41e4 + 0x50) != 0) ||
         (*(int *)(DAT_80612be4 + pDesc[0x29] * 0x220 + 0x41e4 + 0x88) != 0))) {
        return 1;
    }
    return 0;
}

// ============================================================================
// Address: 0x8032ae4c | match_squad_entry_flag_set
// TRUE when the squad entry's flag field (+0xa8) equals 1.
// ============================================================================
uint match_squad_entry_flag_set(int pEntry) {
    uint uVar1 = countLeadingZeros(1 - *(int *)(pEntry + 0xa8));
    return uVar1 >> 5;
}

// ============================================================================
// Address: 0x8032ae60 | match_session_scan_local_ready_squad
// Scans the squad array for the current local player's squad (owner short at
// +0x81b4) that is NOT flagged ready (match_squad_entry_flag_set == 0). Runs
// under the squad-scan lock.
// ============================================================================
void match_session_scan_local_ready_squad(void) {
    int nLocalId = FUN_803826f0();
    int i = 0;
    int iOff = 0;
    while ((i < *(int *)(DAT_80612be4 + 0x81a4)) &&
           ((*(short *)(DAT_80612be4 + iOff + 0x81b4) != nLocalId) ||
            (match_squad_entry_flag_set(DAT_80612be4 + iOff + 0x81a8) != 0))) {
        i = i + 1;
        iOff += 0xb4;
    }
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8032aee8-0x8032b070 | batch_927
// Descriptor predicates + a 4-entry lookup table (DAT_80481730, stride 0x20)
// with field accessors, and a flag-packing helper (same module region).
// ============================================================================

// ---- additional per-file externs for batch_927 ----------------------------
extern int DAT_804814f8;           // mode-indexed value table (stride 4)
extern int DAT_80481730;           // 4-entry descriptor table (stride 0x20)

// ============================================================================
// Address: 0x8032aee8 | match_desc_byte_equals
// TRUE when the descriptor byte at (pDesc+4) equals nValue.
// ============================================================================
uint match_desc_byte_equals(int pDesc, int nValue) {
    uint uVar1 = countLeadingZeros(nValue - (uint)*(byte *)(pDesc + 4));
    return uVar1 >> 5;
}

// ============================================================================
// Address: 0x8032aefc | match_squad_entry_flag_clear
// Logical negation of match_squad_entry_flag_set: TRUE when field +0xa8 != 1.
// ============================================================================
uint match_squad_entry_flag_clear(int pEntry) {
    uint uVar1 = match_squad_entry_flag_set(pEntry);
    uVar1 = countLeadingZeros(uVar1);
    return uVar1 >> 5;
}

// ============================================================================
// Address: 0x8032af24 | match_mode_type_is_eligible
// Eligibility predicate over (nCat, nType, nSub):
//   (nCat==1 && nType==4) -> eligible; or
//   nType in {5,6,13} AND (nCat==1 OR (nCat==2 && nSub in {3,4})) -> eligible.
// ============================================================================
int match_mode_type_is_eligible(int nCat, int nType, int nSub) {
    if ((nCat == 1) && (nType == 4)) {
        return 1;
    }
    if ((((u32)(nType - 5U) < 2) || (nType == 0xd)) &&
        ((nCat == 1) || ((nCat == 2) && ((nSub == 3) || (nSub == 4))))) {
        return 1;
    }
    return 0;
}

// ============================================================================
// Address: 0x8032af80 | match_session_mode_value
// Returns DAT_804814f8[ session mode index (+0x248) ] (stride 4).
// ============================================================================
int match_session_mode_value(void) {
    return *(int *)(&DAT_804814f8 + *(int *)(DAT_80612be4 + 0x248) * 4);
}

// ============================================================================
// Address: 0x8032afa0 | match_desc_table_find
// Linear search of the 4-entry descriptor table (DAT_80481730, stride 0x20)
// for the entry whose key (+0x0) equals nKey; returns entry ptr or NULL.
// ============================================================================
int *match_desc_table_find(int nKey) {
    int iOff = 0;
    int nRemaining = 4;
    do {
        if (nKey == *(int *)((int)&DAT_80481730 + iOff)) {
            return (int *)((int)&DAT_80481730 + iOff);
        }
        iOff += 0x20;
        nRemaining += -1;
    } while (nRemaining != 0);
    return (int *)0x0;
}

// ============================================================================
// Address: 0x8032afe0 | match_desc_field1
// Returns field [1] of the descriptor-table entry keyed by nKey.
// ============================================================================
int match_desc_field1(int nKey) {
    int *pEntry = match_desc_table_find(nKey);
    return pEntry[1];
}

// ============================================================================
// Address: 0x8032b004 | match_desc_field4
// Returns field [4] of the descriptor-table entry keyed by nKey.
// ============================================================================
int match_desc_field4(int nKey) {
    int *pEntry = match_desc_table_find(nKey);
    return pEntry[4];
}

// ============================================================================
// Address: 0x8032b028 | match_desc_field5
// Returns field [5] of the descriptor-table entry keyed by nKey.
// ============================================================================
int match_desc_field5(int nKey) {
    int *pEntry = match_desc_table_find(nKey);
    return pEntry[5];
}

// ============================================================================
// Address: 0x8032b04c | match_desc_field6
// Returns field [6] of the descriptor-table entry keyed by nKey.
// ============================================================================
int match_desc_field6(int nKey) {
    int *pEntry = match_desc_table_find(nKey);
    return pEntry[6];
}

// ============================================================================
// Address: 0x8032b070 | match_desc_pack_flags
// Packs four presence flags (+0x8, +0xc, +0x10, +0x4) into a nibble bitmask:
//   bit0 = (+0x8 != 0), bit1 = (+0xc != 0), bit2 = (+0x10 != 0), bit3 = (+0x4 != 0).
// ============================================================================
byte match_desc_pack_flags(int pDesc) {
    return (byte)(((*(int *)(pDesc + 8) != 0) ? 1U : 0U) |
                  ((*(int *)(pDesc + 0xc) != 0) ? 2U : 0U) |
                  ((*(int *)(pDesc + 0x10) != 0) ? 4U : 0U) |
                  ((*(int *)(pDesc + 4) != 0) ? 8U : 0U));
}

// ============================================================================
// batch_928  0x8032b0d8 - 0x8032b400
// Descriptor-flag pack/unpack, the "current descriptor" selection global
// (DAT_80612cc8), the 2x4 descriptor grid locator, the squad capability-pair
// scan, and the commit/add flows that sit on top of them. Continues the
// g_MatchSession (DAT_80612be4) query cluster; used by the arcade custom-match
// editor (aveditor_deserialize_map) and level entity spawning.
// ----------------------------------------------------------------------------
extern int  DAT_80612cc8;               // 0x8032b26c/74: "current descriptor" selection (int handle)
extern char DAT_804816e8;               // mode config table (byte), stride 0x44 words; +1 == DAT_804816e9
extern char DAT_804816e9;               // adjacent byte in the same 0x44-word-stride table
extern int  DAT_804816d0;               // per-mode default limit table, stride 0x44 words
extern void *PTR_LAB_8048151c;          // per-mode dispatch table of code ptrs, stride 0xd words
extern void FUN_8032bc2c(byte, byte);   // apply a bit-pair to the match capability state
extern void FUN_80348238(int);          // queue/play the given HUD sound-screen id
// match_session_count_tag_bytes (0x8032a874) and match_session_limit_reached
// (0x8032aa10) are defined earlier in this file; no extern needed.
extern int  aveditor_paste_fits_budget(int);     // budget check for a candidate paste size

// ============================================================================
// Address: 0x8032b0d8 | match_desc_unpack_flags
// Inverse of match_desc_pack_flags: expand the low nibble of nFlags back into
// the descriptor's four presence words (+0x8/+0xc/+0x10 get bits 0/1/2 as 0/1;
// +0x4 gets bit 3 kept in place, i.e. value 0 or 8).
// ============================================================================
void match_desc_unpack_flags(int pDesc, uint nFlags) {
    *(uint *)(pDesc + 8)   = nFlags & 1;
    *(uint *)(pDesc + 0xc) = (nFlags >> 1) & 1;
    *(uint *)(pDesc + 0x10) = (nFlags >> 2) & 1;
    *(uint *)(pDesc + 4)   = nFlags & 8;
}

// ============================================================================
// Address: 0x8032b0fc | match_squad_has_cap_pair
// Scan the stride-0xb4 squad array (count at +0x81a4) for any entry whose
// capability byte (+0x8258) has both bit (nCapA+2) and bit nCapB set.
// Returns 1 on the first match, else 0.
// ============================================================================
int match_squad_has_cap_pair(int nCapA, int nCapB) {
    int  nCount = *(int *)(DAT_80612be4 + 0x81a4);
    int  iOff = 0;
    if (nCount > 0) {
        do {
            uint uCap = (uint)*(byte *)(DAT_80612be4 + iOff + 0x8258);
            if (((uCap & (1U << (nCapA + 2))) != 0) && ((uCap & (1U << nCapB)) != 0)) {
                return 1;
            }
            iOff += 0xb4;
            nCount--;
        } while (nCount != 0);
    }
    return 0;
}

// ============================================================================
// Address: 0x8032b208 | match_desc_grid_locate
// Locate the descriptor whose pointer equals pDesc within the 2-row x 4-col
// grid at g_MatchSession+0x113c4 (row stride 0x60, cell stride 0x18). On a hit
// writes the column (+2) to *pCol and the row to *pRow.
// ============================================================================
void match_desc_grid_locate(int pDesc, int *pCol, int *pRow) {
    int nRow = 0;
    int iRowOff = 0;
    do {
        int nCol = 0;
        int iCellOff = 0;
        int nRemain = 4;
        do {
            if (pDesc == DAT_80612be4 + iRowOff + iCellOff + 0x113c4) {
                *pRow = nRow;
                *pCol = nCol + 2;
                return;
            }
            nCol++;
            iCellOff += 0x18;
            nRemain--;
        } while (nRemain != 0);
        nRow++;
        iRowOff += 0x60;
    } while (nRow < 2);
}

// ============================================================================
// Address: 0x8032b26c | match_current_desc_get
// Return the "current descriptor" selection handle (DAT_80612cc8).
// ============================================================================
int match_current_desc_get(void) {
    return DAT_80612cc8;
}

// ============================================================================
// Address: 0x8032b274 | match_current_desc_set
// Store the "current descriptor" selection handle.
// ============================================================================
void match_current_desc_set(int pDesc) {
    DAT_80612cc8 = pDesc;
}

// ============================================================================
// Address: 0x8032b27c | match_current_desc_clear
// Clear the current-descriptor selection (set to null).
// ============================================================================
void match_current_desc_clear(void) {
    match_current_desc_set(0);
}

// ============================================================================
// Address: 0x8032b2a0 | match_current_desc_commit
// Commit the current descriptor: locate its grid position, reset its +0x4
// word, apply the located row/col as a capability bit-pair, set busy bit 2 on
// +0x14, latch the session sound-once flag 0x20 (playing sound-screen 0x19 the
// first time), clear busy bit 2, and drop the selection.
// ============================================================================
void match_current_desc_commit(void) {
    int nCol;
    int nRow[4];

    int pDesc = match_current_desc_get();
    match_desc_grid_locate(pDesc, &nCol, nRow);
    *(int *)(pDesc + 4) = 0;
    FUN_8032bc2c((byte)(1 << nRow[0]), (byte)(1 << nCol));
    *(byte *)(pDesc + 0x14) |= 2;
    if ((*(uint *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(0x19);
        *(uint *)(DAT_80612be4 + 0x238) |= 0x20;
    }
    *(byte *)(pDesc + 0x14) &= 0xfd;
    match_current_desc_set(0);
}

// ============================================================================
// Address: 0x8032b340 | match_mode_flag_byte
// Return a per-mode config byte from the stride-0x44-word table: mode 2 reads
// DAT_804816e9[nIndex], modes 1 read DAT_804816e8[nIndex], else 0.
// ============================================================================
byte match_mode_flag_byte(int nMode, int nIndex) {
    byte bVal = 0;
    if (nMode == 2) {
        bVal = (&DAT_804816e9)[nIndex * 0x44];
    } else if ((nMode < 2) && (nMode > 0)) {
        bVal = (&DAT_804816e8)[nIndex * 0x44];
    }
    return bVal;
}

// ============================================================================
// Address: 0x8032b380 | match_try_add_under_limit
// Returns 1 if a local player is already active (FUN_803826f0 != 0). Otherwise,
// when the current mode's tag count is below its default limit
// (DAT_804816d0[mode], == DAT_804816a8+0x28 at word stride 0x44), request a slot
// add via match_session_limit_reached(mode) and return 1 if it succeeded, else 0.
// Always releases the squad-scan lock (FUN_8038273c) before returning.
// ============================================================================
int match_try_add_under_limit(void) {
    int nBusy = FUN_803826f0();
    int nResult;
    if (nBusy != 0) {
        return 1;
    }
    nResult = 0;
    {
        int nMode = *(int *)(DAT_80612be4 + 0x248);
        int nUsed = match_session_count_tag_bytes(nMode);
        if (nUsed < *(int *)(&DAT_804816d0 + nMode * 0x44)) {
            if (match_session_limit_reached(nMode) != 0) {
                nResult = 1;
            }
        }
    }
    FUN_8038273c();
    return nResult;
}

// ============================================================================
// Address: 0x8032b400 | match_mode_dispatch_budget
// Dispatch through the per-mode code-pointer table (PTR_LAB_8048151c, stride
// 0xd words) with pArg, then return the paste budget check over the result.
// ============================================================================
int match_mode_dispatch_budget(int nMode, int pArg) {
    int nSize = ((int (*)(int))(&PTR_LAB_8048151c)[nMode * 0xd])(pArg);
    return aveditor_paste_fits_budget(nSize);
}

// ============================================================================
// batch_929  0x8032b440 - 0x8032b6f8
// Per-mode budget dispatch, the "candidate slot size" family (a base size that
// depends on a single flag, plus per-mode adjustments), the add-under-limit
// wrapper, and the descriptor-editor session begin/reset entry points. Continues
// the g_MatchSession (DAT_80612be4) cluster.
// ----------------------------------------------------------------------------
extern void *PTR_FUN_80480f38;          // per-mode dispatch table A of code ptrs, stride 0x13 words
extern int  DAT_80612cfc;               // editor session: secondary flag word
extern int  DAT_80612cf8;               // editor session: primary flag word
extern int  DAT_80612cf4;               // editor session: tertiary flag word
extern int  DAT_80612d0c;               // editor session: first descriptor key/handle
extern char DAT_804816a8;               // mode config table base (byte), stride 0x44 words
extern int  FUN_803356e4(int);          // load/apply a config row into the editor screen -> descriptor id
extern void hud_screen_push(void);      // push the current HUD screen
extern void hud_draw_element_background(int, float *); // draw an element's background quad

// ============================================================================
// Address: 0x8032b440 | match_mode_dispatch_budget_checked
// Like match_mode_dispatch_budget but through config table A (PTR_FUN_80480f38,
// stride 0x13 words at the +0x28 code-ptr slot): if the slot is null return 1
// (always fits), else call it and pass the returned size to the budget check.
// ============================================================================
uint match_mode_dispatch_budget_checked(int nMode) {
    uint uResult;
    if ((&PTR_FUN_80480f38)[nMode * 0x13] == (void *)0) {
        uResult = 1;
    } else {
        int nSize = ((int (*)(void))(&PTR_FUN_80480f38)[nMode * 0x13])();
        uResult = (uint)aveditor_paste_fits_budget(nSize);
    }
    return uResult;
}

// ============================================================================
// Address: 0x8032b48c | match_slot_base_size
// Base candidate slot size: 200 bytes, plus 0x98 more when nFlag is nonzero
// (i.e. 200 when flag clear, 0x160 when set).
// ============================================================================
int match_slot_base_size(int nUnused, int nFlag) {
    (void)nUnused;
    return (-(uint)(nFlag != 0) & 0x98) + 200;
}

// ============================================================================
// Address: 0x8032b4a4 | match_slot_size_probe_a
// Compute the base slot size for its side effects (result discarded by ROM).
// ============================================================================
void match_slot_size_probe_a(int nUnused, int nFlag) {
    match_slot_base_size(nUnused, nFlag);
}

// ============================================================================
// Address: 0x8032b4c4 | match_slot_size_probe_b
// Second discarded-result wrapper over match_slot_base_size.
// ============================================================================
void match_slot_size_probe_b(int nUnused, int nFlag) {
    match_slot_base_size(nUnused, nFlag);
}

// ============================================================================
// Address: 0x8032b4e4 | match_slot_size_for_mode
// Base slot size with per-mode adjustments: modes {7, 9} add 0x40; mode 0xe adds
// 200; all other modes use the base unchanged.
// ============================================================================
int match_slot_size_for_mode(int nMode, int nFlag) {
    int nSize = match_slot_base_size(nMode, nFlag);
    if (nMode != 9) {
        if (nMode > 8) {
            if (nMode != 0xe) {
                return nSize;
            }
            return nSize + 200;
        }
        if (nMode != 7) {
            return nSize;
        }
    }
    return nSize + 0x40;
}

// ============================================================================
// Address: 0x8032b540 | match_slot_size_plus_header
// Base slot size plus a fixed 0x14-byte header.
// ============================================================================
int match_slot_size_plus_header(int nMode, int nFlag) {
    return match_slot_base_size(nMode, nFlag) + 0x14;
}

// ============================================================================
// Address: 0x8032b594 | match_desc_draw_background
// Draw the descriptor element's background quad (rect at +0xe0 of the element).
// ============================================================================
void match_desc_draw_background(int pElement) {
    hud_draw_element_background(pElement, (float *)(pElement + 0xe0));
}

// ============================================================================
// Address: 0x8032b5b8 | match_editor_begin
// Begin editing a descriptor list: store the descriptor array pointer into the
// session (DAT_80612d00 and g_MatchSession+0x11858), set the mode (+0x248),
// snapshot the first descriptor key into DAT_80612d0c, clear the flag words, and
// push the appropriate editor screen: for a type-0 head descriptor load its
// config row (FUN_803356e4 over DAT_804816a8 indexed by the +0x41e8 tag byte of
// the +0x220-stride entry) then push; for head types 1..2 push unless the first
// descriptor is null or its [1] field equals 8.
// ============================================================================
void match_editor_begin(int nUnusedA, int nUnusedB, int nMode, int *pDescArray) {
    int nHeadType;
    (void)nUnusedA;
    (void)nUnusedB;
    DAT_80612d00 = (int)pDescArray;
    *(int **)(DAT_80612be4 + 0x11858) = pDescArray;
    *(int *)(DAT_80612be4 + 0x248) = nMode;
    DAT_80612d0c = **(int **)(DAT_80612be4 + 0x11858);
    DAT_80612cfc = 0;
    DAT_80612cf8 = 0;
    DAT_80612cf4 = 0;
    nHeadType = *pDescArray;
    if (nHeadType == 0) {
        FUN_803356e4((int)(&DAT_804816a8 +
                    (uint)*(byte *)(DAT_80612be4 + pDescArray[0x29] * 0x220 + 0x41e8) * 0x44));
        hud_screen_push();
    } else if ((-1 < nHeadType) && (nHeadType < 3) &&
               ((**(int **)(DAT_80612be4 + 0x11858) == 0) ||
                ((*(int **)(DAT_80612be4 + 0x11858))[1] != 8))) {
        hud_screen_push();
    }
}

// ============================================================================
// Address: 0x8032b6a0 | match_add_slot_if_room
// Return 1 when the mode's limit-reached check passes AND a slot add succeeds
// (match_try_add_under_limit), else 0.
// ============================================================================
int match_add_slot_if_room(int nMode) {
    int nResult = 0;
    uint uLimit = match_session_limit_reached(nMode);
    if ((uLimit != 0) && (match_try_add_under_limit() != 0)) {
        nResult = 1;
    }
    return nResult;
}

// ============================================================================
// Address: 0x8032b6f8 | match_editor_reset_flags
// Reset the editor session flag words: clear DAT_80612d00/cfc/cf4, set
// DAT_80612cf8 = 1, and store the given handle into DAT_80612d0c.
// ============================================================================
void match_editor_reset_flags(int pHandle) {
    DAT_80612d00 = 0;
    DAT_80612cfc = 0;
    DAT_80612cf8 = 1;
    DAT_80612cf4 = 0;
    DAT_80612d0c = pHandle;
}

// ============================================================================
// batch_930  0x8032b718 - 0x8032c158
// The descriptor-editor "add slot" dialog flow (over-budget back-dialog, the
// core add dispatcher, the type-0/1/2 screen push), the big descriptor/history/
// squad-array delete routine and its owner-reindex pass, the capability bit-pair
// clear, the editor list-screen builder, and two small predicates. Continues the
// g_MatchSession (DAT_80612be4) cluster.
//
// NOTE: FUN_8032bc2c is DEFINED here (match_caps_clear_pair); the batch_928
// forward extern for it is left in place (identical signature, no conflict).
// ----------------------------------------------------------------------------
extern int  DAT_80481514;               // per-mode string-id table for the add dialog (stride 0x34 words)
extern int  DAT_804816b0;               // per-mode "add applied" callback table (code ptr, stride 0x44 words)
extern int  DAT_804816b4;               // per-mode add-failure string id (stride 0x44 words)
extern void *PTR_LAB_804816b8;          // per-mode add-permission code-ptr table (stride 0x11 words)
extern int  DAT_80611a5c;               // (unused fwd) — kept for symmetry, not referenced
extern int  get_string_by_id(int);      // resolve a text id -> string pointer
extern void hud_create_back_dialog(int, int, int);   // raise a back/confirm dialog
extern void hud_screen_mark_hidden(int);             // mark the given screen hidden
extern void show_blocked_dialog(int);                // raise the "action blocked" dialog
extern int  FUN_80335d68(u64, u64, u64, u64, u64, u64, u64, u64); // build the add-history text screen -> handle
extern void FUN_80387fb8(u64, u64, u64, u64, u64, u64, u64, u64, int, char *, int, int, int, int, int, int); // formatted list screen
extern void aveditor_reindex_after_delete(int);      // reindex descriptor refs after a history delete
extern void FUN_80336a28(int);          // free a history entry's owned resources
extern void leaf_31e8(uint, int, int);  // memmove helper (dst, src, len)
// FUN_8038273c is declared earlier in this file; countLeadingZeros comes from common.h.
// ---- FLOAT layout constants used by the editor list-screen builder ----------
extern float FLOAT_8061aa14, FLOAT_8061aa18, FLOAT_8061aa1c, FLOAT_8061aa20;
extern float FLOAT_8061aa24, FLOAT_8061aa28;
extern float FLOAT_80611a5c, FLOAT_80611a64, FLOAT_80611a6c;
extern int   DAT_806119e0;
extern int   FUN_8033224c(void);        // list callback
extern void  hud_element_init_from_layout(int, int, int, int (*)(void));
extern void  hud_set_item_padding_mode(double, int, int, int);
extern void  hud_set_context_property(int);
extern void  hud_element_scroll_h_overflow(double, double, double, int);
extern void  hud_add_list_item(void);
extern void  hud_set_last_child_tag(int);
extern void  hud_add_callback_item(double);
extern void  hud_element_apply_scroll(void);
extern void  hud_element_set_pos_y(double, int);
extern void  hud_element_finalize_frame(void);
// ---- forward declarations for batch_930 intra-cluster calls ----------------
void match_add_show_over_budget(u64, u64, u64, u64, u64, u64, u64, u64,
                                int, int, int, int, int, int, int, int);
void match_add_slot_core(u64, u64, u64, u64, u64, u64, u64, u64,
                         int, int, int, int, int, int, int, int);
void match_delete_entry(void);
void match_history_fixup_owner(int, int, int);

// ============================================================================
// Address: 0x8032b718 | match_add_show_over_budget
// The add-slot over-budget path: resolve the per-mode dialog string
// (DAT_80481514[mode][subMode]) — for mode 0 the sub-index is the session mode
// (+0x248), otherwise 0 — build the formatted list screen (FUN_80387fb8) with the
// per-mode limit (DAT_804816d0[subMode]), and raise a back dialog.
// ============================================================================
void match_add_show_over_budget(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5, u64 r6, u64 r7, u64 r8,
                                int nMode, int nBackScreen, int nSessionMode, int p12,
                                int p13, int p14, int p15, int p16) {
    int  nSub;
    char *pStr;
    if (nMode == 0) {
        nSub = *(int *)(DAT_80612be4 + 0x248);
    } else {
        nSub = 0;
    }
    pStr = (char *)get_string_by_id(*(int *)(&DAT_80481514 + nMode * 0x34 + nSub * 4));
    FUN_80387fb8(r1, r2, r3, r4, r5, r6, r7, r8, DAT_80612be4, pStr,
                 *(int *)(&DAT_804816d0 + nSub * 0x44), nSub * 0x44, p13, p14, p15, p16);
    hud_create_back_dialog(nBackScreen, DAT_80612be4, 0);
}

// ============================================================================
// Address: 0x8032b7b4 | match_add_slot_dispatch
// Entry to the add-slot flow: run the per-mode budget dispatch
// (match_mode_dispatch_budget) and, if it reports no room (0), show the blocked
// dialog; otherwise hand off to the core add dispatcher (match_add_slot_core).
// ============================================================================
void match_add_slot_dispatch(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5, u64 r6, u64 r7, u64 r8,
                             int nBackScreen, int nMode, int nSessionMode, int p12,
                             int p13, int p14, int p15, int p16) {
    if (match_mode_dispatch_budget(nMode, nSessionMode) == 0) {
        show_blocked_dialog(nBackScreen);
    } else {
        match_add_slot_core(r1, r2, r3, r4, r5, r6, r7, r8, nBackScreen, nMode,
                            nSessionMode, p12, p13, p14, p15, p16);
    }
}

// ============================================================================
// Address: 0x8032b824 | match_add_slot_core
// Core add dispatcher: set the session mode (+0x248), then try to reserve a slot
// (match_add_slot_if_room). On failure show the over-budget dialog. On success:
// for mode 0 build the add-history text screen (FUN_80335d68 -> DAT_80612d00),
// set the editor flags, invoke the per-mode "add applied" callback
// (DAT_804816b0[sessionMode]) and push; for modes 1..2 reset the editor flags and
// push; either way mark the back screen hidden.
// ============================================================================
void match_add_slot_core(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5, u64 r6, u64 r7, u64 r8,
                         int nBackScreen, int nMode, int nSessionMode, int p12,
                         int p13, int p14, int p15, int p16) {
    *(int *)(DAT_80612be4 + 0x248) = nSessionMode;
    if (match_add_slot_if_room(nMode) == 0) {
        match_add_show_over_budget(r1, r2, r3, r4, r5, r6, r7, r8, nMode, nBackScreen,
                                   nSessionMode, DAT_80612be4, p13, p14, p15, p16);
    } else {
        if (nMode == 0) {
            int nCbMode = *(int *)(DAT_80612be4 + 0x248);
            DAT_80612d00 = FUN_80335d68(r1, r2, r3, r4, r5, r6, r7, r8);
            DAT_80612cfc = 1;
            DAT_80612cf8 = 0;
            DAT_80612cf4 = 0;
            if (*(void **)(&DAT_804816b0 + nCbMode * 0x44) != (void *)0) {
                ((void (*)(int))*(void **)(&DAT_804816b0 + nCbMode * 0x44))(nBackScreen);
            }
            hud_screen_push();
        } else if ((-1 < nMode) && (nMode < 3)) {
            match_editor_reset_flags(nMode);
            hud_screen_push();
        }
        hud_screen_mark_hidden(nBackScreen);
    }
}

// ============================================================================
// Address: 0x8032b91c | match_editor_open_or_deny
// Open the editor for the item's mode (item+0x188 -> session +0x248): if the
// per-mode permission callback (PTR_LAB_804816b8[mode]) exists and returns 0,
// raise a back dialog with the mode's deny string (DAT_804816b4[mode]); else push
// the editor screen.
// ============================================================================
void match_editor_open_or_deny(int pItem) {
    int nMode = *(int *)(pItem + 0x188);
    *(int *)(DAT_80612be4 + 0x248) = nMode;
    if ((&PTR_LAB_804816b8)[nMode * 0x11] != (void *)0) {
        int nDenyStr = *(int *)(&DAT_804816b4 + nMode * 0x44);
        if (((int (*)(void))(&PTR_LAB_804816b8)[nMode * 0x11])() == 0) {
            int pStr = get_string_by_id(nDenyStr);
            hud_create_back_dialog(pItem, pStr, 0);
            return;
        }
    }
    hud_screen_push();
}

// ============================================================================
// Address: 0x8032b9b4 | match_build_editor_list_screen
// Build the descriptor-editor list screen: a title (string 0xae4) with the list
// callback FUN_8033224c, two tagged list items (0xae5 tag 0xcb, 0xae6 tag 0xcc),
// a callback item, a trailing action item (0xb03), then apply scroll/position and
// finalize. Sets busy bit 4 on the element's +0x214 flags.
// ============================================================================
void match_build_editor_list_screen(int pElement) {
    int nTitle = get_string_by_id(0xae4);
    hud_element_init_from_layout(0x8400, nTitle, 0, FUN_8033224c);
    hud_set_item_padding_mode((double)FLOAT_8061aa14, 1, 1, 1);
    hud_set_context_property(0x28000);
    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
                  ((FLOAT_8061aa1c * FLOAT_80611a64 +
                    FLOAT_8061aa20 * FLOAT_80611a64 +
                    FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                  FLOAT_8061aa18),
        (double)FLOAT_8061aa28, (double)FLOAT_8061aa28, 0);
    get_string_by_id(0xae5);
    hud_add_list_item();
    hud_set_last_child_tag(0xcb);
    get_string_by_id(0xae6);
    hud_add_list_item();
    hud_set_last_child_tag(0xcc);
    hud_add_callback_item((double)FLOAT_8061aa14);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_apply_scroll();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061aa1c * FLOAT_80611a64 +
                   FLOAT_8061aa20 * FLOAT_80611a64 +
                   FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    hud_element_finalize_frame();
    *(uint *)(pElement + 0x214) |= 4;
}

// ============================================================================
// Address: 0x8032bb30 | match_editor_push_screen
// Trivial wrapper: push the current HUD screen.
// ============================================================================
void match_editor_push_screen(void) {
    hud_screen_push();
}

// ============================================================================
// Address: 0x8032bb5c | match_history_fixup_owner
// After a slot removal, walk the squad array (count +0x81a4): for each type-0xf
// squad, follow its history index (+0xc) to the history entry (+0xdd14 stride
// 0x8c) and, when bDecrement, adjust the entry's owner field +0x7c relative to
// the local player id (decrement if above, set -1 if equal). When a local player
// exists and the owner became negative, run the full delete (match_delete_entry).
// Releases the local-player id lock at the end.
// ============================================================================
void match_history_fixup_owner(int p1, int p2, int bDecrement) {
    int  nLocalHi;
    int  nOff;
    int  i;
    u64  uId = (u64)(unsigned)FUN_803826e8();
    (void)p1; (void)p2;
    nLocalHi = (int)(uId >> 0x20);
    nOff = 0;
    for (i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i++) {
        int pSquad = DAT_80612be4 + nOff + 0x81a8;
        if (*(int *)(pSquad + 4) == 0xf) {
            int pHist = DAT_80612be4 + *(short *)(pSquad + 0xc) * 0x8c + 0xdd14;
            if (bDecrement != 0) {
                int nOwner = *(int *)(pHist + 0x7c);
                if (nLocalHi < nOwner) {
                    *(int *)(pHist + 0x7c) = nOwner - 1;
                } else if (nOwner == nLocalHi) {
                    *(int *)(pHist + 0x7c) = -1;
                }
            }
            if (((int)uId != 0) && (*(int *)(pHist + 0x7c) < 0)) {
                match_delete_entry();
            }
        }
        nOff += 0xb4;
    }
    FUN_80382734();
}

// ============================================================================
// Address: 0x8032bc2c | match_caps_clear_pair
// Walk the squad array (count +0x81a4, stride 0xb4): for each squad whose caps
// byte (+0xb0) has BOTH bits nMaskA and nMaskB set, clear bit nMaskB.
// ============================================================================
void match_caps_clear_pair(byte nMaskA, byte nMaskB) {
    int i = 0;
    int nOff = 0;
    while (i < *(int *)(DAT_80612be4 + 0x81a4)) {
        int pSquad = DAT_80612be4 + nOff + 0x81a8;
        byte bCaps = *(byte *)(pSquad + 0xb0);
        if (((bCaps & nMaskA) != 0) && ((bCaps & nMaskB) != 0)) {
            *(byte *)(pSquad + 0xb0) = bCaps & ~nMaskB;
        }
        i++;
        nOff += 0xb4;
    }
}

// ============================================================================
// Address: 0x8032bc84 | match_delete_entry
// Remove the current squad entry (FUN_803826f0) and cascade all references:
// depending on its kind (+0x0 = 0/1/2) compact the descriptor block array
// (+0x41e4 stride 0x220), remove the squad index from every descriptor's ref
// lists (+0x54 and +0x8c stride-4 arrays), decrement the block/list counts, drop
// the shared history entry (+0xdd14 stride 0x8c) when no other squad references
// it, clear the caps bit-pair (match_caps_clear_pair via match_history_fixup_owner),
// shift the squad array down over the removed slot, fix up every squad-index
// reference above it, decrement the squad count (+0x81a4), and release the lock.
// ============================================================================
void match_delete_entry(void) {
    int *piEntry = (int *)FUN_803826f0();
    int  nKind = *piEntry;
    int  nSquadIdx = ((int)piEntry - (DAT_80612be4 + 0x81a8)) / 0xb4;
    int  nShift;
    int  i;
    int  nOff;

    if (nKind == 0) {
        int  nBlocksAfter = *(int *)(DAT_80612be4 + 0x41e0) - (piEntry[0x29] + 1);
        uint uBlock = DAT_80612be4 + piEntry[0x29] * 0x220 + 0x41e4;
        uint uClz = countLeadingZeros(1 - (uint)*(byte *)(uBlock + 4));
        *(char *)(DAT_80612be4 + 0x41dc) = *(char *)(DAT_80612be4 + 0x41dc) - (char)(uClz >> 5);
        if (nBlocksAfter != 0) {
            leaf_31e8(uBlock, DAT_80612be4 + (piEntry[0x29] + 1) * 0x220 + 0x41e4,
                      nBlocksAfter * 0x220);
            nOff = 0;
            for (i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i++) {
                int pSq = DAT_80612be4 + nOff;
                if ((*(int *)(pSq + 0x81a8) == 0) && (piEntry[0x29] < *(int *)(pSq + 0x824c))) {
                    *(int *)(pSq + 0x824c) = *(int *)(pSq + 0x824c) - 1;
                }
                nOff += 0xb4;
            }
        }
        *(int *)(DAT_80612be4 + 0x41e0) = *(int *)(DAT_80612be4 + 0x41e0) - 1;
    } else if (nKind == 1) {
        nOff = 0;
        for (i = 0; i < *(int *)(DAT_80612be4 + 0x41e0); i++) {
            int pBlk = DAT_80612be4 + nOff + 0x41e4;
            int nCount = *(int *)(pBlk + 0x50);
            int j = 0;
            int k = 0;
            while (j < nCount) {
                if (*(int *)(pBlk + k + 0x54) == nSquadIdx) {
                    int m = j + 1;
                    int off2 = m * 4;
                    for (; m < *(int *)(pBlk + 0x50); m++) {
                        int pMove = pBlk + off2;
                        off2 += 4;
                        *(int *)(pMove + 0x50) = *(int *)(pMove + 0x54);
                        *(char *)(pBlk + m + 0x7b) = *(char *)(pBlk + m + 0x7c);
                    }
                    *(int *)(pBlk + 0x50) = *(int *)(pBlk + 0x50) - 1;
                    break;
                }
                j++;
                k += 4;
            }
            nOff += 0x220;
        }
    } else if (nKind == 2) {
        nOff = 0;
        for (i = 0; i < *(int *)(DAT_80612be4 + 0x41e0); i++) {
            int pBlk = DAT_80612be4 + nOff + 0x41e4;
            int nCount = *(int *)(pBlk + 0x88);
            int j = 0;
            int k = 0;
            while (j < nCount) {
                if (*(int *)(pBlk + k + 0x8c) == nSquadIdx) {
                    int m = j + 1;
                    int off2 = m * 4;
                    for (; m < *(int *)(pBlk + 0x88); m++) {
                        int pMove = pBlk + off2;
                        off2 += 4;
                        *(int *)(pMove + 0x88) = *(int *)(pMove + 0x8c);
                        *(int *)(pMove + 0x150) = *(int *)(pMove + 0x154);
                    }
                    *(int *)(pBlk + 0x88) = *(int *)(pBlk + 0x88) - 1;
                    break;
                }
                j++;
                k += 4;
            }
            nOff += 0x220;
        }
    }

    {
        int nHistIdx = (int)*(short *)((char *)piEntry + 0xc);
        if (-1 < nHistIdx) {
            int  bStillReferenced = 0;
            int  idx = 0;
            int  o = 0;
            int  n = *(int *)(DAT_80612be4 + 0x81a4);
            while (n > 0) {
                int *pSq = (int *)(DAT_80612be4 + o + 0x81a8);
                if ((idx != nSquadIdx) &&
                    (((*pSq == 1) || (*pSq == 2)) && (*(short *)((char *)pSq + 0xc) == nHistIdx))) {
                    bStillReferenced = 1;
                }
                idx++;
                o += 0xb4;
                n--;
            }
            if (!bStillReferenced) {
                int nHistCount = *(int *)(DAT_80612be4 + 0xdd10);
                aveditor_reindex_after_delete(nHistIdx);
                FUN_80336a28(DAT_80612be4 + *(short *)((char *)piEntry + 0xc) * 0x8c + 0xdd14);
                leaf_31e8(DAT_80612be4 + *(short *)((char *)piEntry + 0xc) * 0x8c + 0xdd14,
                          DAT_80612be4 + (*(short *)((char *)piEntry + 0xc) + 1) * 0x8c + 0xdd14,
                          (nHistCount - (nHistIdx + 1)) * 0x8c);
                nOff = 0;
                for (i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i++) {
                    int *pSq = (int *)(DAT_80612be4 + nOff + 0x81a8);
                    if (((*pSq == 1) || (*pSq == 2)) &&
                        (*(short *)((char *)piEntry + 0xc) < *(short *)((char *)pSq + 0xc))) {
                        *(short *)((char *)pSq + 0xc) = *(short *)((char *)pSq + 0xc) - 1;
                    }
                    nOff += 0xb4;
                }
                *(int *)(DAT_80612be4 + 0xdd10) = *(int *)(DAT_80612be4 + 0xdd10) - 1;
            }
        }
    }

    match_history_fixup_owner(nSquadIdx, 0, 1);
    nShift = *(int *)(DAT_80612be4 + 0x81a4) - (nSquadIdx + 1);
    if (0 < nShift) {
        leaf_31e8((uint)piEntry, DAT_80612be4 + (nSquadIdx + 1) * 0xb4 + 0x81a8, nShift * 0xb4);
        nOff = 0;
        for (i = 0; i < *(int *)(DAT_80612be4 + 0x41e0); i++) {
            int *pBlk = (int *)(DAT_80612be4 + nOff + 0x41e4);
            int t = 0;
            int j;
            for (j = 0; j < pBlk[0x14]; j++) {
                int v = *(int *)((int)pBlk + t + 0x54);
                if (nSquadIdx < v) {
                    *(int *)((int)pBlk + t + 0x54) = v - 1;
                }
                t += 4;
            }
            t = 0;
            for (j = 0; j < pBlk[0x22]; j++) {
                int v = *(int *)((int)pBlk + t + 0x8c);
                if (nSquadIdx < v) {
                    *(int *)((int)pBlk + t + 0x8c) = v - 1;
                }
                t += 4;
            }
            if (nSquadIdx < *pBlk) {
                *pBlk = *pBlk - 1;
            }
            nOff += 0x220;
        }
    }
    *(int *)(DAT_80612be4 + 0x81a4) = *(int *)(DAT_80612be4 + 0x81a4) - 1;
    match_history_fixup_owner(nSquadIdx, 1, 0);
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8032c158 | match_desc_is_type8_screen13
// Return -1 when the descriptor is a type-8 entry whose value (+0x8) is 0x13,
// else -2 (i.e. -1 - !match).
// ============================================================================
char match_desc_is_type8_screen13(int pDesc) {
    bool bMatch = false;
    if ((*(int *)(pDesc + 4) == 8) && (*(int *)(pDesc + 8) == 0x13)) {
        bMatch = true;
    }
    return (char)(-1 - !bMatch);
}

// ============================================================================
// batch_931  0x8032c184 - 0x8032c678
// Descriptor/squad cross-reference scans and validation predicates for the
// arcade editor: the "active descriptor" index (+0x284) set/clear/search, the
// two ref-list membership tests, the orphaned-squad auto-delete, a config-record
// lookup table (DAT_80481400, 3 entries stride 0xc), and the per-mode placement
// count queries. Continues the g_MatchSession (DAT_80612be4) cluster.
// ----------------------------------------------------------------------------
extern int  DAT_8051a820;               // HUD screen-descriptor table base (stride 0x1e0)
extern int  DAT_806119ec;               // current HUD screen index
extern int  DAT_80481400;               // config-record table: 3 entries, stride 0xc (key at +0, handler at +4)
extern void FUN_80337e04(int);          // pre-delete squad teardown
extern void FUN_80338a30(void);         // post-delete screen refresh
extern int  hud_screen_find_root(int);  // resolve a screen id to its root element
extern int  aveditor_count_placed_for_type(int); // count placed squads for a type key
// match_cat_is_locked_pair (0x8032ac2c), match_delete_entry (0x8032bc84) and
// match_desc_is_type8_screen13 (0x8032c158) are defined earlier in this file.
// ---- forward declaration for batch_931 intra-cluster call ------------------
int *match_config_record_find(int);      // 0x8032c638

// ============================================================================
// Address: 0x8032c184 | match_active_desc_clear
// Clear the "active descriptor" index (g_MatchSession+0x284 = -1).
// ============================================================================
void match_active_desc_clear(void) {
    *(int *)(DAT_80612be4 + 0x284) = -1;
}

// ============================================================================
// Address: 0x8032c194 | match_active_desc_find_type8
// Scan the descriptor block array (+0x41e4 stride 0x220, count +0x41e0) for a
// single-child block (+0x14 == 1) whose child squad is a type-8/screen-0x13
// descriptor (match_desc_is_type8_screen13) with its flag set, kind 2 and value
// 0x11, and record that block's key as the active descriptor (+0x284).
// ============================================================================
void match_active_desc_find_type8(void) {
    int i = 0;
    int nOff = 0;
    do {
        int *puBlk;
        if (*(int *)(DAT_80612be4 + 0x41e0) <= i) {
            return;
        }
        puBlk = (int *)(DAT_80612be4 + nOff + 0x41e4);
        if (puBlk[0x14] == 1) {
            int pEntry = DAT_80612be4 + puBlk[0x23] * 0xb4 + 0x81a8;
            char cIs = match_desc_is_type8_screen13(DAT_80612be4 + puBlk[0x15] * 0xb4 + 0x81a8);
            if ((cIs != '\0') && (match_squad_entry_flag_set(pEntry) != 0) &&
                (*(int *)(pEntry + 4) == 2) && (*(int *)(pEntry + 8) == 0x11)) {
                *(int *)(DAT_80612be4 + 0x284) = *puBlk;
                return;
            }
        }
        i++;
        nOff += 0x220;
    } while (1);
}

// ============================================================================
// Address: 0x8032c264 | match_delete_children_of_active
// Under the id lock, for the active descriptor's block (indexed via +0x284 ->
// +0x824c) delete every child squad matching the selected slot id (+0x2f10, or
// +0x2f20 when negative) provided the current screen root has flag 0x40 set:
// teardown (FUN_80337e04), match_delete_entry, refresh (FUN_80338a30).
// ============================================================================
void match_delete_children_of_active(void) {
    int nSel;
    int pBlk;
    int pRoot;
    FUN_803826e8();
    nSel = (int)*(short *)(DAT_80612be4 + 0x2f10);
    if (*(short *)(DAT_80612be4 + 0x2f10) < 0) {
        nSel = *(int *)(DAT_80612be4 + 0x2f20);
    }
    pBlk = DAT_80612be4 +
           *(int *)(DAT_80612be4 + *(int *)(DAT_80612be4 + 0x284) * 0xb4 + 0x824c) * 0x220 + 0x41e4;
    pRoot = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(uint *)(pRoot + 0xc0) & 0x40) != 0) {
        int nOff = 0;
        int i;
        for (i = 0; i < *(int *)(pBlk + 0x88); i++) {
            int pEntry = DAT_80612be4 + *(int *)(pBlk + nOff + 0x8c) * 0xb4 + 0x81a8;
            if (*(short *)(pEntry + 0xe) == nSel) {
                FUN_80337e04(pEntry);
                match_delete_entry();
                FUN_80338a30();
            }
            nOff += 4;
        }
    }
    FUN_80382734();
}

// ============================================================================
// Address: 0x8032c348 | match_find_unflagged_locked_pair
// Search the squad array (count +0x81a4) for the first locked-category pair
// (match_cat_is_locked_pair over +0x0/+0x4) whose slot id (+0xe) equals the local
// player id and whose entry flag is clear (match_squad_entry_flag_set == 0).
// (Result is consumed by the caller via the register state; no return value.)
// ============================================================================
void match_find_unflagged_locked_pair(void) {
    int nLocal = FUN_803826e8();
    int nOff = 0;
    int i;
    for (i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i++) {
        int *pEntry = (int *)(DAT_80612be4 + nOff + 0x81a8);
        if (match_cat_is_locked_pair(pEntry[0], pEntry[1]) != 0 &&
            (*(short *)((int)pEntry + 0xe) == nLocal) &&
            (match_squad_entry_flag_set((int)pEntry) == 0)) {
            break;
        }
        nOff += 0xb4;
    }
}

// ============================================================================
// Address: 0x8032c3e8 | match_block_refs_squad_88
// Membership test: does the block's +0x88 ref-list (count at +0x88, entries at
// +0x8c, stride-0xb4 squad index) contain the squad pointer pTarget?
// ============================================================================
int match_block_refs_squad_88(int pTarget, int pSquadBase, int pBlk) {
    int nCount = *(int *)(pBlk + 0x88);
    int nOff = 0;
    if (nCount > 0) {
        do {
            if (pTarget == pSquadBase + *(int *)(pBlk + nOff + 0x8c) * 0xb4) {
                return 1;
            }
            nOff += 4;
            nCount--;
        } while (nCount != 0);
    }
    return 0;
}

// ============================================================================
// Address: 0x8032c430 | match_block_refs_squad_04
// Membership test over the block's +0x04 ref-list (count at +0x4, entries at +0x8).
// ============================================================================
int match_block_refs_squad_04(int pTarget, int pSquadBase, int pBlk) {
    int nCount = *(int *)(pBlk + 4);
    int nOff = 0;
    if (nCount > 0) {
        do {
            if (pTarget == pSquadBase + *(int *)(pBlk + nOff + 8) * 0xb4) {
                return 1;
            }
            nOff += 4;
            nCount--;
        } while (nCount != 0);
    }
    return 0;
}

// ============================================================================
// Address: 0x8032c478 | match_delete_if_unreferenced
// Under the lock, take the current squad (FUN_803826f0). Unless it is a
// screen-0x13 head with an unset slot (+0xc == -1) or has any of flags 0x3c set
// (+0x2c byte), scan the block array (+0x41e4 stride 0x220): kind-2 squads test
// the +0x88 ref-list (match_block_refs_squad_88), kind-1 squads test the +0x4c
// ref-list (match_block_refs_squad_04); if none reference it there is nothing to
// do. Always releases the lock.
// ============================================================================
void match_delete_if_unreferenced(void) {
    int *piEntry = (int *)FUN_803826f0();
    if (((piEntry[2] != 0x13) || (*(short *)((char *)piEntry + 0xc) != -1)) &&
        ((*(byte *)((char *)piEntry + 0xb0) & 0x3c) == 0)) {
        int nOff = 0;
        int i;
        for (i = 0; i < *(int *)(DAT_80612be4 + 0x41e0); i++) {
            int nKind = *piEntry;
            int pBlk = DAT_80612be4 + nOff + 0x41e4;
            int bRef;
            if (nKind == 2) {
                bRef = match_block_refs_squad_88((int)piEntry, DAT_80612be4 + 0x81a8, pBlk);
                if (bRef != 0) break;
            } else if ((nKind < 2) && (nKind > 0)) {
                bRef = match_block_refs_squad_04((int)piEntry, DAT_80612be4 + 0x81a8, pBlk + 0x4c);
                if (bRef != 0) break;
            }
            nOff += 0x220;
        }
    }
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8032c594 | match_history_limit_state
// For a squad's shared history entry (+0xdd14 by index +0xc): when value (+0x8)
// is 9 return the sign of (limit +0x20 vs placed count) as -1/0/1; otherwise
// return 1 when the placed count is zero, else 0.
// ============================================================================
uint match_history_limit_state(int pSquad) {
    uint uVal;
    int pHist = DAT_80612be4 + *(short *)((char *)pSquad + 0xc) * 0x8c + 0xdd14;
    if (*(int *)(pSquad + 8) == 9) {
        uint uPlaced = (uint)aveditor_count_placed_for_type(*(int *)(pHist + 0x84));
        uVal = ((int)uPlaced >> 0x1f) +
               ((uint)(*(uint *)(pHist + 0x20) <= uPlaced) - ((int)*(uint *)(pHist + 0x20) >> 0x1f));
    } else {
        uint uPlaced = (uint)aveditor_count_placed_for_type(*(int *)(pHist + 0x84));
        uVal = (-uPlaced & ~uPlaced) >> 0x1f;
    }
    return uVal;
}

// ============================================================================
// Address: 0x8032c638 | match_config_record_find
// Linear search of the 3-entry config-record table (DAT_80481400, stride 0xc)
// for the entry whose key (+0) equals nKey; return the record pointer or null.
// ============================================================================
int *match_config_record_find(int nKey) {
    int nOff = 0;
    int i = 3;
    do {
        int *piRec = (int *)((int)&DAT_80481400 + nOff);
        if (*piRec == nKey) {
            return piRec;
        }
        nOff += 0xc;
        i--;
    } while (i != 0);
    return (int *)0;
}

// ============================================================================
// Address: 0x8032c678 | match_config_dispatch
// Look up the config record for the descriptor's key (+0x4); if none, return 1;
// otherwise call the record's handler (+0x4) with the descriptor.
// ============================================================================
int match_config_dispatch(int pDesc) {
    int *piRec = match_config_record_find(*(int *)(pDesc + 4));
    if (piRec == (int *)0) {
        return 1;
    }
    return ((int (*)(int))piRec[1])(pDesc);
}

// ============================================================================
// batch_932  0x8032c6c8 - 0x8032c9dc
// Config-record value accessor, the "clone" scratch buffer alloc/free pair and
// its active-check, plus a set of squad/history cross-reference predicates over
// g_MatchSession (DAT_80612be4). Continues the match/session query cluster;
// callers are the arcade custom-match editor delete/validate flows.
// ----------------------------------------------------------------------------
// ---- additional per-file externs for batch_932 ----------------------------
extern int  DAT_80612cd0;               // scratch clone buffer (0x40 bytes) handle
extern int  DAT_80612ccc;               // "pending selection" squad-entry pointer/handle
extern int  DAT_80612c14;               // linked list head: block records (next @ +0x40)
extern int  mem_alloc(int size, int type);   // 0x40-byte scratch allocator
extern void mem_free_top(int ptr);           // free top-of-stack allocation
extern int  FUN_8032e810(int nIndex);        // -> participant descriptor ptr for ordinal nIndex
// match_config_record_find (0x8032c638), match_cat_is_locked_pair (0x8032ac2c),
// match_delete_entry (0x8032bc84) and leaf_31e8 are defined/declared earlier.

// ============================================================================
// Address: 0x8032c6c8 | match_config_record_value
// Look up the config record for nKey; return its value field (+0x8) or 0 when
// there is no matching record.
// ============================================================================
int match_config_record_value(int nKey) {
    int *piRec = match_config_record_find(nKey);
    if (piRec == (int *)0) {
        return 0;
    }
    return piRec[2];
}

// ============================================================================
// Address: 0x8032c6fc | match_squad_reindex_after_delete
// After a squad entry is removed, walk the stride-0xb4 squad array (count at
// +0x81a4) and fix up the "linked-history index" short (+0xe) of every locked
// pair: entries pointing at the deleted index get fully removed, entries above
// it are shifted down by one. nDeletedIdx arrives via the saved r28 (return of
// __save_gpr_28 in the raw decomp); modelled here as the incoming index.
// ============================================================================
void match_squad_reindex_after_delete(int nDeletedIdx) {
    int iOff = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int *piEntry = (int *)(DAT_80612be4 + iOff + 0x81a8);
        if (match_cat_is_locked_pair(piEntry[0], piEntry[1]) != 0) {
            short sIdx = *(short *)((int)piEntry + 0xe);
            if (sIdx == nDeletedIdx) {
                match_delete_entry();
            } else if (nDeletedIdx < sIdx) {
                *(short *)((int)piEntry + 0xe) = sIdx + -1;
            }
        }
        iOff += 0xb4;
    }
}

// ============================================================================
// Address: 0x8032c798 | match_clone_buffer_alloc
// Allocate the 0x40-byte scratch "clone" buffer (DAT_80612cd0) and copy the
// caller-supplied source record into it. (The raw decomp shows a spurious
// 16-arg prototype from a register-spill misread; the function takes a single
// source pointer.)
// ============================================================================
void match_clone_buffer_alloc(uint pSource) {
    DAT_80612cd0 = mem_alloc(0x40, 0);
    leaf_31e8((uint)DAT_80612cd0, pSource, 0x40);
}

// ============================================================================
// Address: 0x8032c7dc | match_clone_buffer_free
// Free the scratch clone buffer. Returns the (now-stale) handle, matching the
// ROM which leaves it in the return register.
// ============================================================================
int match_clone_buffer_free(void) {
    mem_free_top(DAT_80612cd0);
    return DAT_80612cd0;
}

// ============================================================================
// Address: 0x8032c804 | match_pending_selection_absent
// TRUE when the "pending selection" pointer (DAT_80612ccc) does not point at
// any live squad-array entry (stride 0xb4 @ +0x81a8, count @ +0x81a4).
// ============================================================================
int match_pending_selection_absent(void) {
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0x81a4);
    if (0 < nRemaining) {
        do {
            if (DAT_80612be4 + iOff + 0x81a8 == DAT_80612ccc) {
                return 0;
            }
            iOff += 0xb4;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return 1;
}

// ============================================================================
// Address: 0x8032c850 | match_pending_selection_set
// Record the pending selection pointer, but only if none is currently set.
// (First arg is ignored by the ROM.)
// ============================================================================
void match_pending_selection_set(int nUnused, int pEntry) {
    (void)nUnused;
    if (DAT_80612ccc != 0) {
        return;
    }
    DAT_80612ccc = pEntry;
}

// ============================================================================
// Address: 0x8032c864 | match_squad_scan_ref_kinds
// For the squad at *pSquadIdx, scan every block record in the DAT_80612c14
// linked list (next @ +0x40, child list head @ +0x3c, child next @ +0x14).
// Sets pResult[0]=1 if any kind-5 child references the squad's tag (+0xac) and
// pResult[1]=1 if any kind-6 child does.
// ============================================================================
void match_squad_scan_ref_kinds(int *pSquadIdx, undefined4 *pResult) {
    int iList = DAT_80612c14;
    int pSquad = DAT_80612be4 + *pSquadIdx * 0xb4 + 0x81a8;
    pResult[0] = 0;
    pResult[1] = 0;
    for (; iList != 0; iList = *(int *)(iList + 0x40)) {
        for (int *piChild = *(int **)(iList + 0x3c); piChild != (int *)0x0;
             piChild = (int *)piChild[5]) {
            if ((*piChild == 5) && (piChild[6] == *(int *)(pSquad + 0xac))) {
                pResult[0] = 1;
            } else if ((*piChild == 6) && (piChild[6] == *(int *)(pSquad + 0xac))) {
                pResult[1] = 1;
            }
        }
    }
}

// ============================================================================
// Address: 0x8032c8f8 | match_participant_shares_tag
// For an active participant (+0x4 byte == 1), return TRUE if any *other* active
// participant (stride 0x220 @ +0x41e4, count @ +0x41e0) shares the same squad
// tag (squad +0x8254, indexed by participant field +0x0).
// ============================================================================
int match_participant_shares_tag(int *pPart) {
    if (*(char *)(pPart + 1) != '\x01') {
        return 0;
    }
    int nRemaining = *(int *)(DAT_80612be4 + 0x41e0);
    int iOff = 0;
    if (0 < nRemaining) {
        do {
            int *piOther = (int *)(DAT_80612be4 + iOff + 0x41e4);
            if (((*(char *)(piOther + 1) == '\x01') && (piOther != pPart)) &&
                (*(int *)(DAT_80612be4 + *piOther * 0xb4 + 0x8254) ==
                 *(int *)(DAT_80612be4 + *pPart * 0xb4 + 0x8254))) {
                return 1;
            }
            iOff += 0x220;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return 0;
}

// ============================================================================
// Address: 0x8032c98c | match_participant_owns_squad
// TRUE if the participant record (pPart) has, among its member-index list
// (count @ +0x50, indices @ +0x54 stride 4), a squad whose owner id
// (squad +0x81b0) equals nOwner.
// ============================================================================
int match_participant_owns_squad(int nOwner, int pPart) {
    int nRemaining = *(int *)(pPart + 0x50);
    int iOff = 0;
    if (0 < nRemaining) {
        do {
            if (*(int *)(DAT_80612be4 + *(int *)(pPart + iOff + 0x54) * 0xb4 + 0x81b0) == nOwner) {
                return 1;
            }
            iOff += 4;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return 0;
}

// ============================================================================
// Address: 0x8032c9dc | match_history_ref_scan
// Walk participants [1 .. nCount-1]; for each, look up its descriptor via
// FUN_8032e810, then scan that descriptor's member list (count @ +0x50,
// indices @ +0x54 stride 4). Break as soon as a squad entry is found whose
// history slot (+0xc -> stride-0x8c table @ +0xdd14) and stored id (+0x8) match
// the search key pair carried in pHistTarget/nId (r28/r29 in the raw decomp).
// The result is consumed only for its early-out side effect by the ROM caller.
// ============================================================================
int match_history_ref_scan(int pHistTarget, int nId, int nCount) {
    for (int i = 0; i < nCount + -1; i = i + 1) {
        int pDesc = FUN_8032e810(i + 1);
        int iOff = 0;
        int pList = DAT_80612be4 + *(int *)(pDesc + 0xa4) * 0x220 + 0x41e4;
        int nRemaining = *(int *)(pList + 0x50);
        if (0 < nRemaining) {
            do {
                int pSquad = DAT_80612be4 + *(int *)(pList + iOff + 0x54) * 0xb4 + 0x81a8;
                if ((pHistTarget == DAT_80612be4 + *(short *)(pSquad + 0xc) * 0x8c + 0xdd14) &&
                    (*(int *)(pSquad + 8) == nId)) {
                    return 1;
                }
                iOff += 4;
                nRemaining += -1;
            } while (nRemaining != 0);
        }
    }
    return 0;
}

// ============================================================================
// batch_933  0x8032caac - 0x8032cf70
// Squad "budget"/placement accounting: per-squad and per-descriptor placed-unit
// counts, the aggregate over a participant's member list, the add-eligibility
// check (total placed < 0xb), plus block/squad membership predicates and two
// index-of helpers over g_MatchSession (DAT_80612be4). Continues the
// match/session query cluster used by the arcade custom-match editor.
// ----------------------------------------------------------------------------
// All raw symbols referenced here (DAT_80612be4, DAT_80612d00,
// aveditor_count_placed_for_type) are declared earlier in this file. The four
// FUN_8032c* helpers below are defined in address order within this batch.

// ============================================================================
// Address: 0x8032caac | match_participant_scan_pair_history
// For an active participant (+0x4 byte == 1) that owns both a kind-3 and a
// kind-4 squad, walk its member list (count +0x50, indices +0x54 stride 4) and,
// for each active (state 3/4) squad, probe the shared history slot via
// match_history_ref_scan keyed by the participant's own squad tag (+0x8254);
// stop at the first hit. Result discarded by the ROM caller (side-effect scan).
// pPart arrives via saved r28 in the raw decomp.
// ============================================================================
void match_participant_scan_pair_history(int *pPart) {
    int pOwnSquad = DAT_80612be4 + *pPart * 0xb4;
    if ((*(char *)(pPart + 1) == '\x01') &&
        (match_participant_owns_squad(3, (int)pPart) != 0) &&
        (match_participant_owns_squad(4, (int)pPart) != 0)) {
        int iOff = 0;
        for (int i = 0; i < pPart[0x14]; i = i + 1) {
            int pSquad = DAT_80612be4 + *(int *)((int)pPart + iOff + 0x54) * 0xb4 + 0x81a8;
            int nState = *(int *)(pSquad + 8);
            if (((nState == 3) || (nState == 4)) &&
                (match_history_ref_scan(DAT_80612be4 + *(short *)(pSquad + 0xc) * 0x8c + 0xdd14,
                                        0, *(int *)(pOwnSquad + 0x8254)) != 0)) {
                break;
            }
            iOff += 4;
        }
    }
}

// ============================================================================
// Address: 0x8032cbb4 | match_squad_placed_count
// Placed-unit count for a squad by its state (+0x8) and shared history entry
// (+0xdd14, indexed by +0xc): state 9 -> configured limit (hist +0x20); states
// 8 -> live placed count via aveditor_count_placed_for_type(hist +0x84); else 0.
// ============================================================================
uint match_squad_placed_count(int pSquad) {
    uint uResult = 0;
    int nState = *(int *)(pSquad + 8);
    int pHist = DAT_80612be4 + *(short *)(pSquad + 0xc) * 0x8c + 0xdd14;
    if (nState == 9) {
        uResult = *(uint *)(pHist + 0x20);
    } else if ((nState < 9) && (7 < nState)) {
        uResult = aveditor_count_placed_for_type(*(int *)(pHist + 0x84));
    }
    return uResult;
}

// ============================================================================
// Address: 0x8032cc20 | match_squad_slot_weight
// "Slot weight" of a squad by state (+0x8): states {1,2} -> history budget word
// (+0xdd34); states {7} and {5,6}? -> collapses to the placed-count path via
// match_squad_placed_count for 7<state<10; state 0/other in-range -> 1; else 0.
// (Preserves the original nested comparison structure.)
// ============================================================================
uint match_squad_slot_weight(int pSquad) {
    int nState = *(int *)(pSquad + 8);
    if (nState < 5) {
        if (nState != 0) {
            if (nState < 0) {
                return 0;
            }
            if (nState < 3) {
                return *(uint *)(DAT_80612be4 + *(short *)(pSquad + 0xc) * 0x8c + 0xdd34);
            }
        }
    } else if (nState != 7) {
        if ((6 < nState) && (nState < 10)) {
            return match_squad_placed_count(pSquad);
        }
        return 0;
    }
    return 1;
}

// ============================================================================
// Address: 0x8032ccac | match_desc_slot_weight
// Descriptor-form slot weight, keyed off the descriptor kind (+0x80):
// kind 3 -> value word (+0x20); kinds {0,1} -> 1; kind 0xd -> live placed count
// via aveditor_count_placed_for_type(+0x84); else 0.
// ============================================================================
uint match_desc_slot_weight(int pDesc) {
    uint uResult = 0;
    int nKind = *(int *)(pDesc + 0x80);
    if (nKind == 3) {
        uResult = *(uint *)(pDesc + 0x20);
    } else if (nKind < 3) {
        if ((nKind < 2) && (-1 < nKind)) {
            uResult = 1;
        }
    } else if (nKind == 0xd) {
        uResult = aveditor_count_placed_for_type(*(int *)(pDesc + 0x84));
    }
    return uResult;
}

// ============================================================================
// Address: 0x8032cd1c | match_desc_sum_slot_weights
// Sum match_squad_slot_weight over the active session descriptor's member list
// (descriptor block @ +0x41e4 indexed by the saved r28 index, stride 0x220;
// count +0x50, indices +0x54). Returns the running total in the return register
// (match_squad_can_add reads it). nDescIdx arrives via saved r28 in the raw
// decomp; the ROM caller here passes the session descriptor index.
// ============================================================================
int match_desc_sum_slot_weights(int nDescIdx) {
    int nSum = 0;
    int iOff = 0;
    int pBlock = DAT_80612be4 + nDescIdx * 0x220 + 0x41e4;
    for (int i = 0; i < *(int *)(pBlock + 0x50); i = i + 1) {
        nSum += (int)match_squad_slot_weight(
            DAT_80612be4 + *(int *)(pBlock + iOff + 0x54) * 0xb4 + 0x81a8);
        iOff += 4;
    }
    return nSum;
}

// ============================================================================
// Address: 0x8032cda4 | match_block_owns_squad
// For a kind-1 block (+0x0 == 1) with flag bit 0x2 set (+0xb0), scan its second
// member list (block @ +0x4230 by nBlockIdx stride 0x220; count +0x4, indices
// +0x8) for a squad-array entry pointer equal to pSquad. Returns 1 on match.
// ============================================================================
int match_block_owns_squad(int *pSquad, int nBlockIdx) {
    int pBlock = DAT_80612be4 + nBlockIdx * 0x220 + 0x4230;
    if ((*pSquad == 1) && ((*(byte *)(pSquad + 0x2c) & 2) != 0)) {
        int nRemaining = *(int *)(pBlock + 4);
        int iOff = 0;
        if (0 < nRemaining) {
            do {
                if ((int *)(DAT_80612be4 + *(int *)(pBlock + iOff + 8) * 0xb4 + 0x81a8) == pSquad) {
                    return 1;
                }
                iOff += 4;
                nRemaining += -1;
            } while (nRemaining != 0);
        }
    }
    return 0;
}

// ============================================================================
// Address: 0x8032ce1c | match_squad_can_add
// Add-eligibility for a squad against the active session descriptor
// (DAT_80612d00). Returns 1 (allowed) when no session, when the session slot
// count (+0xac) is empty, or when nMode != 1. Otherwise: if the squad is already
// owned by the session block (match_block_owns_squad) it is blocked (1); else
// allowed only when placed(squad) + sum(session members) < 0xb.
// ============================================================================
int match_squad_can_add(int *pSquad, int nMode) {
    int iResult;
    if (DAT_80612d00 == 0) {
        iResult = 1;
    } else if ((*(int *)(DAT_80612d00 + 0xac) < 1) || (nMode != 1)) {
        iResult = 1;
    } else {
        iResult = match_block_owns_squad(pSquad, *(int *)(DAT_80612d00 + 0xa4));
        if (iResult == 0) {
            uint uPlaced = match_squad_slot_weight((int)pSquad);
            int nSum = match_desc_sum_slot_weights(*(int *)(DAT_80612d00 + 0xa4));
            iResult = (uint)((int)uPlaced + nSum < 0xb) - (((int)uPlaced + nSum) >> 0x1f);
        } else {
            iResult = 1;
        }
    }
    return iResult;
}

// ============================================================================
// Address: 0x8032cebc | match_block_lists_squad_index
// For an active block (+0x4 byte @ +0x41e4 == 1) selected by nBlockIdx, scan its
// member index list (count +0x50, indices +0x54 stride 4) for nSquadIndex.
// Returns 1 if present.
// ============================================================================
int match_block_lists_squad_index(int nSquadIndex, int nBlockIdx) {
    int pBlock = DAT_80612be4 + nBlockIdx * 0x220 + 0x41e4;
    if (*(char *)(pBlock + 4) != '\x01') {
        return 0;
    }
    int nRemaining = *(int *)(pBlock + 0x50);
    int iOff = 0;
    if (0 < nRemaining) {
        do {
            if (*(int *)(pBlock + iOff + 0x54) == nSquadIndex) {
                return 1;
            }
            iOff += 4;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return 0;
}

// ============================================================================
// Address: 0x8032cf1c | match_history_index_of
// Return the ordinal of the history entry whose address equals pHistEntry
// (stride 0x8c array @ +0xdd14, count @ +0xdd10), or -1 if not found.
// ============================================================================
int match_history_index_of(int pHistEntry) {
    int nIndex = 0;
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0xdd10);
    if (0 < nRemaining) {
        do {
            if (DAT_80612be4 + iOff + 0xdd14 == pHistEntry) {
                return nIndex;
            }
            nIndex += 1;
            iOff += 0x8c;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return -1;
}

// ============================================================================
// Address: 0x8032cf70 | match_squad_index_of
// Return the ordinal of the squad-array entry whose address equals pSquad
// (stride 0xb4 array @ +0x81a8, count @ +0x81a4), or -1. A null pSquad defaults
// to the active session descriptor (DAT_80612d00).
// ============================================================================
int match_squad_index_of(int pSquad) {
    if (pSquad == 0) {
        pSquad = DAT_80612d00;
    }
    int nIndex = 0;
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0x81a4);
    if (0 < nRemaining) {
        do {
            if (DAT_80612be4 + iOff + 0x81a8 == pSquad) {
                return nIndex;
            }
            nIndex += 1;
            iOff += 0xb4;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return -1;
}

// ============================================================================
// batch_934  0x8032cfcc - 0x8032d958
// Per-descriptor "apply/refresh" dispatch (the kind-0 vs kind-1..2 split), the
// two array-scan drivers that run it under the local-id lock, the arcade
// custom-match config info-panel renderer, plus the commit/help/icon helpers.
// Continues the match/session cluster; the renderer is the on-screen readout
// for the currently-selected match option.
// ----------------------------------------------------------------------------
// ---- additional per-file externs for batch_934 ----------------------------
extern int  DAT_80480f30;          // per-mode flag table (stride 0x4c bytes); bit0 = cell-select hook
extern int  DAT_80480f48;          // per-mode post-apply callback table (stride 0x4c bytes)
extern int  DAT_804816bc;          // per-tag commit callback table (stride 0x44 bytes)
extern int  DAT_80481424;          // string-id table indexed by owner id
extern int  DAT_804816e4;          // per-column HUD icon-id table (stride 0x20 bytes)
extern int  DAT_8061c1a8;          // scan-result seed word 0 (copied into local pair)
extern int  DAT_8061c1ac;          // scan-result seed word 1
extern int  DAT_806109c4;          // framebuffer width
extern int  DAT_806109c8;          // framebuffer height
extern float FLOAT_8061aa24, FLOAT_8061aa28, FLOAT_8061aa2c, FLOAT_8061aa30;
extern float FLOAT_8061aa34, FLOAT_8061aa38, FLOAT_8061aa3c, FLOAT_8061aa40, FLOAT_8061aa44;
extern double DOUBLE_8061aa48;
extern float FLOAT_80611a64, FLOAT_80611a68, FLOAT_80611a70;

extern int   FUN_803826f0(void);                    // -> current local player short id (declared earlier too)
extern int   aveditor_is_cell_selected(int nCell);  // -> nonzero if grid cell selected
extern u32   hud_element_set_anim_value(int elem, int prop); // -> animated color word
extern int   FUN_803825c0(double x);                // float->int quantize
extern int   get_string_by_id(int id);              // -> char* string pointer (int handle)
extern int   text_set_font_table(int table);        // set active glyph table, returns previous
extern void  text_draw_char_shadowed(double x, double y, byte *psz, u32 fg, u32 shadow);
extern void  text_draw_char(double x, double y, byte *psz, u32 color);
extern float text_get_char_height(byte *psz);
extern byte *text_check_scale(double maxWidth, byte *psz);
extern void  gx_get_scissor_rect(int *x0, int *y0, int *x1, int *y1);
extern void  gx_set_scissor_rect(int x0, int y0, int x1, int y1);
extern void  hud_icon_draw_indexed(int iconId, int x, int y, int w, int h);
extern void  aveditor_help_dialog_and_ack(int nHelpId);
extern int   mem_alloc_scratch(void);               // 0x8032d434: 0x40-byte format scratch (mem_alloc(0x40,0))
extern void  hud_format_indexed_line(int pScratch, int nMax, char *pFmt, int nArg); // 0x8032d434 formatter (FUN_80388098)
// match_squad_scan_ref_kinds/match_participant_shares_tag/
// match_participant_scan_pair_history (batch_932/933),
// match_delete_if_unreferenced/match_config_dispatch/match_cat_is_locked_pair/
// match_config_record_value and mem_alloc/mem_free_top are declared/defined earlier.

// forward decls for the mutually-referencing dispatch trio (address order)
int match_descriptor_apply(int *pDesc);
int match_squad_apply_wrapper(int *pSquad);

// ============================================================================
// Address: 0x8032cfcc | match_scan_participants_apply
// Under the local-id lock, find the current local player's participant
// (block +0x41e4 stride 0x220, count +0x41e0; owner short at +0x4) and run
// match_descriptor_apply on its first squad; stops at the first that returns
// nonzero. Always releases the lock.
// ============================================================================
void match_scan_participants_apply(void) {
    u32 uLocalId = (u32)FUN_803826f0();
    int i = 0;
    int iOff = 0;
    while (i < *(int *)(DAT_80612be4 + 0x41e0)) {
        int *piBlock = (int *)(DAT_80612be4 + iOff + 0x41e4);
        if ((*(byte *)(piBlock + 1) == uLocalId) &&
            (match_descriptor_apply((int *)(DAT_80612be4 + *piBlock * 0xb4 + 0x81a8)) != 0)) {
            break;
        }
        i = i + 1;
        iOff += 0x220;
    }
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8032d060 | match_participant_refresh
// Refresh pass for a participant block (index via saved r28 -> +0xa4). Applies
// match_descriptor_apply to every entry in its two member lists (primary count
// [0x14]/indices +0x54, secondary count [0x22]/indices +0x8c). If the block is
// active (+0x4 byte == 1) it then runs the reference/tag/pair-history scans
// (seeded from DAT_8061c1a8/ac). Returns 0 (matches the ROM's implicit result).
// ============================================================================
int match_participant_refresh(int nBlockIdx) {
    int *piBlock = (int *)(DAT_80612be4 + nBlockIdx * 0x220 + 0x41e4);
    if (piBlock[0x14] != 0) {
        int iOff = 0;
        for (int i = 0; i < piBlock[0x14]; i = i + 1) {
            match_descriptor_apply(
                (int *)(DAT_80612be4 + *(int *)((int)piBlock + iOff + 0x54) * 0xb4 + 0x81a8));
            iOff += 4;
        }
        if (piBlock[0x22] != 0) {
            iOff = 0;
            for (int i = 0; i < piBlock[0x22]; i = i + 1) {
                match_descriptor_apply(
                    (int *)(DAT_80612be4 + *(int *)((int)piBlock + iOff + 0x8c) * 0xb4 + 0x81a8));
                iOff += 4;
            }
        }
    }
    if (*(char *)(piBlock + 1) == '\x01') {
        int aScanResult[2];
        aScanResult[0] = DAT_8061c1a8;
        aScanResult[1] = DAT_8061c1ac;
        match_squad_scan_ref_kinds(piBlock, (undefined4 *)aScanResult);
        match_participant_shares_tag(piBlock);
        match_participant_scan_pair_history(piBlock);
    }
    return 0;
}

// ============================================================================
// Address: 0x8032d1d8 | match_squad_apply
// Apply pass for a squad descriptor (ptr via saved r28). Prunes it if now
// unreferenced, dispatches its config handler, optionally re-locks the paired
// cell when the mode has the cell-select flag (DAT_80480f30 bit0) and the cell
// is selected, then invokes the per-mode post-apply callback (DAT_80480f48).
// Returns 0 (implicit).
// ============================================================================
int match_squad_apply(int *pDesc) {
    int nMode = pDesc[1];
    match_delete_if_unreferenced();
    match_config_dispatch((int)pDesc);
    if (((*(uint *)(&DAT_80480f30 + nMode * 0x4c) & 1) != 0) &&
        (aveditor_is_cell_selected((int)*(short *)(pDesc + 3)) != 0)) {
        match_cat_is_locked_pair(pDesc[0], pDesc[1]);
    }
    int (**ppfnCb)(int *) = (int (**)(int *))(&DAT_80480f48 + nMode * 0x4c);
    if (*ppfnCb != (int (*)(int *))0) {
        (*ppfnCb)(pDesc);
    }
    return 0;
}

// ============================================================================
// Address: 0x8032d320 | match_descriptor_apply
// Dispatch a squad descriptor by kind (+0x0): kind 0 -> match_participant_refresh
// (via its own index), kinds {1,2} -> match_squad_apply; else 0. Returns the
// selected handler's result (both currently 0).
// ============================================================================
int match_descriptor_apply(int *pDesc) {
    int nResult = 0;
    int nKind = *pDesc;
    if ((nKind < 0) || (2 < nKind)) {
        nResult = 0;
    } else if (nKind == 0) {
        nResult = match_participant_refresh(pDesc[0x29]);
    } else if ((-1 < nKind) && (nKind < 3)) {
        nResult = match_squad_apply(pDesc);
    }
    return nResult;
}

// ============================================================================
// Address: 0x8032d38c | match_scan_squads_apply
// Under the local-id lock, walk the squad array (stride 0xb4 @ +0x81a8, count
// +0x81a4) for the current local player's squads (owner +0x0) and run
// match_squad_apply_wrapper on each; stops on the first nonzero. Releases lock.
// Returns nonzero when an applicable squad was found (loop broke early); the
// ROM leaves that result in r3 and the row-highlight draw consumes it.
// ============================================================================
int match_scan_squads_apply(void) {
    int nLocalId = FUN_803826f0();
    int nCount = *(int *)(DAT_80612be4 + 0x81a4);
    int i = 0;
    int iOff = 0;
    while (i < nCount) {
        int *piSquad = (int *)(DAT_80612be4 + iOff + 0x81a8);
        if ((*piSquad == nLocalId) && (match_squad_apply_wrapper(piSquad) != 0)) {
            break;
        }
        i = i + 1;
        iOff += 0xb4;
    }
    FUN_8038273c();
    return (i < nCount);
}

// ============================================================================
// Address: 0x8032d414 | match_squad_apply_wrapper
// Thin wrapper: run match_descriptor_apply on the squad. (ROM returns void; the
// info-panel renderer treats the same address's result as int, so return it.)
// ============================================================================
int match_squad_apply_wrapper(int *pSquad) {
    return match_descriptor_apply(pSquad);
}

// ============================================================================
// Address: 0x8032d434 | match_config_info_panel_draw
// Renders the arcade custom-match "current option" info panel: a header line,
// then either an "unset" line (when match_squad_apply_wrapper reports nothing),
// or a title plus up to two description lines selected from the option's flag
// bitmask (match_descriptor_apply result). Sets a full-screen scissor for the
// duration and restores the previous rect on exit. pEntry arrives via saved r25.
// Float geometry constants come from the FLOAT_8061aa** / FLOAT_80611a** tables.
// ============================================================================
void match_config_info_panel_draw(int *pEntry) {
    int  sx0, sy0, sx1, sy1;
    float fxBase = FLOAT_8061aa24 * FLOAT_80611a68 + FLOAT_80611a70;
    float fyBase = FLOAT_8061aa2c * FLOAT_80611a64 + *(float *)(*(int *)((int)pEntry + 0x2c) + 0xec);
    u32  uColorA = hud_element_set_anim_value((int)pEntry, 0);
    u32  uColorB = hud_element_set_anim_value((int)pEntry, 1);
    float fAnim  = *(float *)((int)pEntry + 0xf0);
    u32  uQuantA = (u32)FUN_803825c0((double)(FLOAT_8061aa30 * fAnim));
    u32  uQuantB = (u32)FUN_803825c0((double)(FLOAT_8061aa34 * fAnim));

    gx_get_scissor_rect(&sx0, &sy0, &sx1, &sy1);
    gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
    text_set_font_table(0xf);

    byte *pszHeader = (byte *)get_string_by_id(0xb38);
    text_draw_char_shadowed((double)fxBase, (double)fyBase, pszHeader, uColorA, uColorB);
    float fy = fyBase + text_get_char_height(pszHeader);

    if (match_squad_apply_wrapper(pEntry) == 0) {
        byte *pszUnset = (byte *)get_string_by_id(0xb39);
        text_draw_char_shadowed((double)fxBase, (double)fy, pszUnset,
                                (uQuantA & 0xff) | 0xa7f0a00, (uQuantB & 0xff) | 0x64646400);
    } else {
        u32  uLineColor = (uQuantA & 0xff) | 0x64646400;
        uint uFlags = (uint)match_descriptor_apply(pEntry);
        float fMaxWidth = FLOAT_8061aa38 * FLOAT_80611a68 + FLOAT_80611a70 - fxBase;
        int pScratch = mem_alloc_scratch();

        byte *pszTitle = (byte *)get_string_by_id(0xb3a);
        text_draw_char_shadowed((double)fxBase, (double)fy, pszTitle,
                                (uQuantA & 0xff) | 0x7f0a0a00, (uQuantB & 0xff) | 0x1e0a0a00);
        fy = fy + text_get_char_height(pszTitle);
        text_set_font_table(0x11);

        byte *pszLine1 = (byte *)0;
        if ((uFlags & 0x40) != 0) {
            pszLine1 = (byte *)get_string_by_id(0xb41);
        } else if ((uFlags & 0x80) != 0) {
            int nStrId = match_config_record_value(pEntry[1]);
            pszLine1 = (byte *)get_string_by_id(nStrId);
        } else if ((uFlags & 0x10) != 0) {
            pszLine1 = (byte *)get_string_by_id(0xb45);
        } else if ((uFlags & 0x20) != 0) {
            pszLine1 = (byte *)get_string_by_id(0xb46);
        }
        if ((uFlags & 1) && (uFlags & 2)) {
            pszLine1 = (byte *)get_string_by_id(0xb3b);
        } else if (uFlags & 1) {
            pszLine1 = (byte *)get_string_by_id(0xb3c);
        } else if (uFlags & 2) {
            pszLine1 = (byte *)get_string_by_id(0xb3d);
        }

        byte *pszLine2 = (byte *)0;
        if ((uFlags & 4) && (uFlags & 8)) {
            pszLine2 = (byte *)get_string_by_id(0xb3e);
        } else if (uFlags & 4) {
            pszLine2 = (byte *)get_string_by_id(0xb3f);
        } else if (uFlags & 8) {
            pszLine2 = (byte *)get_string_by_id(0xb40);
        } else if (uFlags & 0x100) {
            pszLine2 = (byte *)get_string_by_id(0xb4c);
        } else if ((uFlags & 0x200) && (uFlags & 0x400)) {
            pszLine2 = (byte *)get_string_by_id(0xb4d);
        } else if (uFlags & 0x200) {
            pszLine2 = (byte *)get_string_by_id(0xb4e);
        } else if (uFlags & 0x400) {
            pszLine2 = (byte *)get_string_by_id(0xb4f);
        } else if (uFlags & 0x800) {
            pszLine2 = (byte *)get_string_by_id(0xb50);
        } else if (uFlags & 0x1000) {
            int nOwner = *(int *)(DAT_80612be4 +
                          *(int *)(DAT_80612be4 +
                                   *(short *)(pEntry + 3) * 0x8c + 0xdd90) * 0xb4 + 0x81b0);
            int nStrId = get_string_by_id((&DAT_80481424)[nOwner]);
            char *pFmt = (char *)get_string_by_id(0xb47);
            hud_format_indexed_line(pScratch, 0x3f, pFmt, nStrId);
            pszLine2 = (byte *)pScratch;
        }

        if (pszLine1 != (byte *)0) {
            byte *pszDraw = text_check_scale((double)fMaxWidth, pszLine1);
            text_draw_char((double)fxBase, (double)fy, pszDraw, uLineColor);
            fy = fy + text_get_char_height(pszDraw);
        }
        if (pszLine2 != (byte *)0) {
            byte *pszDraw = text_check_scale((double)fMaxWidth, pszLine2);
            text_draw_char((double)fxBase, (double)fy, pszDraw, uLineColor);
        }
        mem_free_top(pScratch);
    }

    gx_set_scissor_rect(sx0, sy0, sx1, sy1);
}

// ============================================================================
// Address: 0x8032d88c | match_commit_selected
// Commit the current selection: for the active descriptor (DAT_80612be4 +
// 0x11858) with a valid tag index (+0xa4 >= 0), invoke its per-tag commit
// callback (DAT_804816bc, stride 0x44, indexed by tag byte +0x41e8), then set
// it as the pending selection.
// ============================================================================
void match_commit_selected(int nUnused) {
    int pDesc = *(int *)(DAT_80612be4 + 0x11858);
    if (-1 < *(int *)(pDesc + 0xa4)) {
        byte nTag = *(byte *)(DAT_80612be4 + *(int *)(pDesc + 0xa4) * 0x220 + 0x41e8);
        int (**ppfnCb)(int) = (int (**)(int))(&DAT_804816bc + nTag * 0x44);
        if (*ppfnCb != (int (*)(int))0) {
            (*ppfnCb)(pDesc);
        }
    }
    match_pending_selection_set(nUnused, pDesc);
}

// ============================================================================
// Address: 0x8032d914 | match_help_gate
// If the "help pending" flag (DAT_80612be4 + 0x11854) is set, show the help
// dialog + ack for nHelpId and return FALSE (gate closed); otherwise TRUE.
// ============================================================================
bool match_help_gate(int nHelpId) {
    bool bPending = *(char *)(DAT_80612be4 + 0x11854) == '\x01';
    if (bPending) {
        aveditor_help_dialog_and_ack(nHelpId);
    }
    return !bPending;
}

// ============================================================================
// Address: 0x8032d958 | match_column_icon_draw
// Draw the per-column status icon (id table DAT_804816e4 stride 0x20 by nColumn)
// at the entry's screen position (+0x4 x, +0x8 y), clipped to the visible band
// [FLOAT_8061aa28 .. height - DOUBLE_8061aa48].
// ============================================================================
void match_column_icon_draw(int nUnused0, int nUnused1, int nColumn, int pEntry) {
    (void)nUnused0;
    (void)nUnused1;
    float fY = FLOAT_8061aa40 * FLOAT_80611a64 + *(float *)(pEntry + 4);
    if (FLOAT_8061aa28 <= fY) {
        double dBand = (double)DAT_806109c8 - DOUBLE_8061aa48;
        if (fY <= (float)dBand) {
            hud_icon_draw_indexed(
                *(int *)(&DAT_804816e4 + nColumn * 0x20),
                (int)-(FLOAT_8061aa2c * FLOAT_80611a68 - *(float *)(pEntry + 8)),
                (int)fY,
                (int)(FLOAT_8061aa44 * FLOAT_80611a68),
                (int)(FLOAT_8061aa24 * FLOAT_80611a64));
        }
    }
}

// ============================================================================
// batch_935  0x8032da30 - 0x8032e78c
// Arcade custom-match editor row/column rendering: the animated "twinkle"
// cursor sprite, the softkey/help-line setup, two big yes/no confirm-dialog
// builders (grid vs list variants), the per-option row renderer with hit-test +
// hover callbacks, the rect-inflate helper, the column marker/line renderer, its
// zero-arg wrapper, a vertical clip-line helper, and a max-score reducer. Heavy
// HUD/GX rendering but functionally the match-editor UI; kept in this cluster.
// ----------------------------------------------------------------------------
// ---- additional per-file externs for batch_935 ----------------------------
extern int   DAT_80611928;         // frame counter (mod 0x78 drives cursor twinkle phase)
extern int   DAT_80612ce4;         // editor "rows visible" flag
extern int   DAT_80612ce8;         // editor "cursor active" flag
extern int   DAT_80612ce0;         // current column-style index (stride 0x18 into DAT_805d6778)
extern int   DAT_8060f948;         // language/build variant selector (1 == variant A)
extern int  *DAT_80610a20;         // sprite material table base pointer
extern int   DAT_805d6778;         // per-column-style descriptor table (stride 0x18)
extern float DAT_80583228;         // sine/twinkle lookup table (0x800 entries)
extern int   DAT_80481510;         // per-kind title string-id table (stride 0x34)
extern int   DAT_80481520;         // per-kind color word A table (stride 0x34)
extern int   DAT_80481524;         // per-kind color word B table (stride 0x34)
extern int   DAT_80611200;         // "%s ... %s" format string ("x of y")
extern char  s__s___s____804817c0; // "%s : %s" style format (variant A)
extern char  s__s___s___804817cc;  // "%s : %s" style format (variant B)
extern float FLOAT_8061aa14, FLOAT_8061aa50, FLOAT_8061aa54, FLOAT_8061aa58, FLOAT_8061aa5c;
extern float FLOAT_8061aa60, FLOAT_8061aa64, FLOAT_8061aa68, FLOAT_8061aa6c, FLOAT_8061aa74;
extern float FLOAT_8061aa78, FLOAT_8061aa7c, FLOAT_8061aa80, FLOAT_8061aa84, FLOAT_8061aa88;
extern float FLOAT_8061aa8c, FLOAT_8061aa90, FLOAT_8061aa94, FLOAT_8061aa70;

extern u32   color_scale_lo(double scale, u32 color);   // scale low byte / alpha of color
extern u32   color_scale_rgb(double scale, u32 color);  // scale rgb of color
extern int   sprite_get_texture_id(int index);
extern void  gx_set_active_material(int index);
extern void  cursor_draw_stretched(double x1, double y1, double x2, double y2);
extern void  hud_screen_register_handler(int screen, int slotId, int strPtr, int a, int b);
extern void  memset_wrapper(int dst, int val, int len);
extern void  hud_create_yes_no_dialog(int screen, int ctx, int confirmData, int cancelData);
extern int   text_apply_width_color(void);
extern void  text_measure_string(int pOutW, float *pOutH, byte *psz, int flags);
extern void  text_set_color_rgb(double r, double g, double b);
extern int   FUN_8038b510(int pDst, int pSrc);          // string copy/append into buffer
extern int   FUN_8038b5c8(int pStr);                    // string non-empty / needs-suffix probe
extern int   FUN_803826c8(void);                        // -> (kind<<32 | style) pair for current row
extern void  FUN_80382714(void);                        // release current-row lock
extern int   aveditor_clip_segment_y(float *pY0, float *pY1);
extern void  aveditor_draw_viewport_tri(float *pRect, u32 colA, u32 colB);
extern void  aveditor_draw_viewport_xline(double x, double bias, float *pRect, u32 color);
extern void  gx_draw_line_f(double x0, double y0, double x1, double y1, u32 color);
extern void  gx_draw_line_i(int x0, int y0, int x1, int y1, u32 color);
extern void  gx_draw_line_counted(int x0, int y0, int x1, int y1, u32 color);
extern void  gx_draw_sprite_quad(double x0, double y0, double x1, double y1,
                                 u32 u0, u32 v0, u32 u1, u32 v1);
extern int   hud_cursor_hit_test_rect(float *pRect);
extern void  aveditor_update_back_softkey(int screen, int enable);
// match_desc_grid_locate (void), match_current_desc_set (void), match_desc_field4 (int),
// hud_screen_mark_hidden (void) and FUN_80387fb8 are all declared/defined earlier in this file.
// Clean dialog format wrappers (model the register-spill printf-ish calls at
// 0x8032dc44 / 0x8032dd50; the ROM uses variadic string builders whose raw
// decomp prototypes are register-spill garbage):
extern void  match_dialog_format_titled(int pCtx, char *pFmt, int nSep, int nTitle);
extern int   hud_screen_register_handler2(int screen, int slotId, int strPtr, int a, int b); // returns accumulator handle
extern int   match_dialog_format_str(int pAcc, char *pFmt, int nSep, int pArg);   // "<a> : <b>"
extern int   match_dialog_format_join(int pAcc, int pFmt, int pPart, int nTail);  // "<part> (<tail>)"

// ============================================================================
// Address: 0x8032da30 | match_cursor_twinkle_draw
// Draws the animated selection-cursor sprite at (x,y) when within the visible
// vertical band. Phase comes from the frame counter (DAT_80611928 % 0x78) into
// the sine table (DAT_80583228); the resulting scale sets a pulsing colour, then
// cursor_draw_stretched paints a FLOAT_8061aa60 x FLOAT_8061aa64 quad. param_3
// is the element alpha.
// ============================================================================
void match_cursor_twinkle_draw(double x, double y, double alpha) {
    float fLimit = (float)((double)DAT_806109c8 - DOUBLE_8061aa48);
    if (((double)FLOAT_8061aa28 <= y) && (y <= (double)fLimit)) {
        float fPhase = (float)((double)(DAT_80611928 % 0x78) - (double)DOUBLE_8061aa48) /
                       FLOAT_8061aa50;
        int nIndex = (int)(FLOAT_8061aa54 *
                           (FLOAT_8061aa14 * FLOAT_8061aa58 * fPhase * fPhase - FLOAT_8061aa5c) +
                           FLOAT_8061aa40) & 0x7ff;
        double dPulse = (double)(FLOAT_8061aa40 * (&DAT_80583228)[nIndex] + FLOAT_8061aa40);

        int nTex = sprite_get_texture_id(0);
        gx_set_active_material(nTex);

        dPulse = (double)(FLOAT_8061aa68 * (float)(dPulse * dPulse));
        u32 uR = (u32)FUN_803825c0((double)(float)((double)FLOAT_8061aa6c + dPulse));
        u32 uG = (u32)FUN_803825c0((double)(float)((double)FLOAT_8061aa30 + dPulse));
        u32 uA = (u32)FUN_803825c0((double)(float)((double)FLOAT_8061aa30 * alpha));
        color_scale_lo(alpha, (uA & 0xff) | ((uR & 0xff) << 8) | (uG << 0x18) | ((uR & 0xff) << 0x10));
        cursor_draw_stretched(x, y, (double)(float)((double)FLOAT_8061aa60 + x),
                              (double)(float)((double)FLOAT_8061aa64 + y));
    }
}

// ============================================================================
// Address: 0x8032dbd8 | match_row_setup_softkey
// Row softkey/help-line setup: when the editor is in "help pending" mode
// (DAT_80612be4 + 0x11854) enable the back softkey; otherwise register the
// default action string (0x19c7) on the screen.
// ============================================================================
void match_row_setup_softkey(int nScreen) {
    if (*(char *)(DAT_80612be4 + 0x11854) == '\x01') {
        aveditor_update_back_softkey(nScreen, 1);
    } else {
        int nStr = get_string_by_id(0x19c7);
        hud_screen_register_handler(nScreen, 0, nStr, 0, 0);
    }
}

// ============================================================================
// Address: 0x8032dc44 | match_confirm_dialog_grid
// Builds the "clear this option?" yes/no dialog for a grid-cell descriptor:
// registers the prompt string (0xb27), and when the screen has the confirm
// flag (0x40) locates the grid cell, formats "<field4-title> : <0xa44>" with the
// language-appropriate format string, and shows the yes/no dialog.
// ============================================================================
void match_confirm_dialog_grid(int nScreen, int nIdx) {
    int nPrompt = get_string_by_id(0xb27);
    hud_screen_register_handler(nScreen, 2, nPrompt, 0, 0);
    if ((*(uint *)(nScreen + 0xc0) & 0x40) != 0) {
        int aCell[3];
        int nFlag;
        match_desc_grid_locate(nIdx, aCell, &nFlag);
        match_current_desc_set(nIdx);
        int nTitle = get_string_by_id(match_desc_field4(aCell[0]));
        int nSep = get_string_by_id(0xa44);
        char *pFmt = (DAT_8060f948 == 1) ? &s__s___s____804817c0 : &s__s___s___804817cc;
        match_dialog_format_titled((int)DAT_80612be4, pFmt, nSep, nTitle);
        hud_create_yes_no_dialog(nScreen, (int)DAT_80612be4, -0x7fcd4d60, -0x7fcd4d84);
    }
}

// ============================================================================
// Address: 0x8032dd50 | match_confirm_dialog_list
// List-variant of the confirm dialog (descriptor ptr via saved r28). For a
// non-locked descriptor (kind 0 or mode!=8, and DAT_80480f30 bit4 clear),
// registers the per-kind title (DAT_80481510), then formats a two-part string:
// "<name> : <sep>" then " (<tag-name>)" using the per-tag label (DAT_804816e4),
// choosing the shorter/suffix form via FUN_8038b5c8, and shows the dialog.
// ============================================================================
void match_confirm_dialog_list(int nScreen) {
    int *piDesc = (int *)FUN_803826e8();
    if ((piDesc != (int *)0) &&
        ((*piDesc == 0) || (piDesc[1] != 8)) &&
        ((*(uint *)(&DAT_80480f30 + piDesc[1] * 0x4c) >> 4 & 1) != 1)) {
        int nTitle = get_string_by_id(*(int *)(&DAT_80481510 + *piDesc * 0x34));
        int pAcc = hud_screen_register_handler2(nScreen, 2, nTitle, 0, 0);
        if ((*(uint *)(nScreen + 0xc0) & 0x40) != 0) {
            byte nTag = *(byte *)(DAT_80612be4 + piDesc[0x29] * 0x88 + 0x107a);
            int nSep = get_string_by_id(0xa44);
            char *pFmt = (DAT_8060f948 == 1) ? &s__s___s____804817c0 : &s__s___s___804817cc;
            int pPart = match_dialog_format_str(pAcc, pFmt, nSep, (int)(piDesc + 8));
            int nTagName = get_string_by_id(*(int *)(&DAT_804816e4 + (uint)nTag * 0x44));
            int pFull = match_dialog_format_join(pAcc, (int)&DAT_80611200, pPart, nTagName);
            int nTagName2 = get_string_by_id(*(int *)(&DAT_804816e4 + (uint)nTag * 0x44));
            if (FUN_8038b5c8(nTagName2) != 0) {
                pPart = pFull;
            }
            FUN_8038b510((int)DAT_80612be4, pPart);
            *(int *)((int)DAT_80612be4 + 0x4616 * 4) = (int)piDesc;
            hud_create_yes_no_dialog(nScreen, (int)DAT_80612be4, -0x7fcd2774, 0);
        }
    }
}

// ============================================================================
// Address: 0x8032dee0 | match_option_row_draw
// Renders one editor option row: measures/clips the label, draws its background
// tri and separator xline, paints the twinkle cursor when the row is active and
// hovered, draws the column status icon, draws the label text, and on hover runs
// the info-panel + softkey setup + the row's help-gated action/hover callbacks.
// pRow comes via saved r28-region; the many float in_fN are inbound geometry.
// ============================================================================
float match_option_row_draw(int nUnused0, double yRow, double xRow, int nParam4,
                           int nParam5, int nParam6, int *pRow, byte *pszLabel,
                           int nHasCursor, int nParam10, int nActive, int nHelpId,
                           void *pfnAction, void *pfnHover) {
    (void)nUnused0; (void)nParam4; (void)nParam5; (void)nParam10;
    // FUN_803826c8 returns a (elemPtr<<32 | kind) pair; split it:
    long long llPair = (long long)FUN_803826c8();
    int pElem = (int)(unsigned long long)((unsigned long long)llPair >> 32);
    int nKind = (int)llPair;

    double dExtra = (double)FLOAT_8061aa70;   // inbound f-reg default when band-edge
    byte abLabel[136];
    memset_wrapper((int)abLabel, 0, 0x40);

    double dLabelX = (double)(float)(yRow + (double)FLOAT_8061aa2c);
    float fColW = (nHasCursor != 0) ? FLOAT_8061aa74 : FLOAT_8061aa6c;
    double dMaxWidth = -(double)(float)((double)FLOAT_8061aa14 * (double)fColW - dExtra);

    text_set_font_table(3);
    text_apply_width_color();
    byte *pszScaled = (byte *)text_check_scale(dMaxWidth, pszLabel);
    FUN_8038b510((int)abLabel, (int)pszScaled);
    float fMeasuredH;
    int aMeasureW;
    text_measure_string((int)&aMeasureW, &fMeasuredH, abLabel, 0);
    double dRowH = (double)(FLOAT_8061aa78 * FLOAT_80611a64 + fMeasuredH);

    if (DAT_80612ce4 != 0) {
        u32 uColLabel = color_scale_lo((double)*(float *)(pElem + 0xf0), 0x6464647f);
        u32 uColA = color_scale_lo((double)*(float *)(pElem + 0xf0),
                                   *(uint *)(&DAT_80481520 + nKind * 0x34));
        u32 uColB = color_scale_lo((double)*(float *)(pElem + 0xf0),
                                   *(uint *)(&DAT_80481524 + nKind * 0x34));
        u32 uColLine = color_scale_lo((double)*(float *)(pElem + 0xf0), 0x7f7f7f7f);

        float aRect[4];
        aRect[0] = (float)dLabelX;                       // local_e0
        aRect[1] = (float)xRow;                          // local_dc
        aRect[2] = (float)(dLabelX + dExtra);            // local_d8
        aRect[3] = (float)(xRow + dRowH);                // local_d4

        bool bHover = false;
        double dTriX = (nHasCursor != 0) ? (double)(FLOAT_8061aa7c * FLOAT_80611a68)
                                         : (double)FLOAT_8061aa28;
        if ((DAT_80612ce8 != 0) && (hud_cursor_hit_test_rect(aRect) != 0) &&
            (pfnAction != (void *)0)) {
            bHover = true;
        }
        uColB = color_scale_rgb((double)(bHover ? FLOAT_8061aa7c : FLOAT_8061aa80), uColB);
        uColA = color_scale_rgb((double)(bHover ? FLOAT_8061aa84 : FLOAT_8061aa7c), uColA);
        aveditor_draw_viewport_tri(aRect, uColA, uColB);
        aveditor_draw_viewport_xline(dTriX, (double)FLOAT_8061aa2c, aRect, uColLine);

        if ((nActive != 0) && (pRow != (int *)0) && (match_squad_apply_wrapper(pRow) != 0)) {
            match_cursor_twinkle_draw((double)(aRect[2] - FLOAT_8061aa88),
                                      (double)(aRect[1] - FLOAT_8061aa8c),
                                      (double)*(float *)(pElem + 0xf0));
        }
        if ((pRow != (int *)0) && (2 < nKind) && (nKind < 7)) {
            match_column_icon_draw(pElem, (int)pRow, nKind, (int)aRect);
        }
        text_draw_char((double)(float)(dLabelX + (double)fColW),
                       (double)(float)(xRow + (double)((float)(dRowH - (double)fMeasuredH) *
                                                       FLOAT_8061aa40)),
                       abLabel, uColLabel);
        double dWhite = (double)FLOAT_8061aa90;
        text_set_color_rgb(dWhite, dWhite, dWhite);
        if (bHover) {
            if ((nActive != 0) && (pRow != (int *)0)) {
                match_config_info_panel_draw(pRow);
            }
            match_row_setup_softkey(pElem);
            if (((*(uint *)(pElem + 0xc0) & 0x10) != 0) && (pfnAction != (void *)0) &&
                match_help_gate(nHelpId)) {
                ((void (*)(int, int, int, int *))pfnAction)(pElem, nKind, nParam6, pRow);
            }
            if (pfnHover != (void *)0) {
                ((void (*)(int, int *))pfnHover)(pElem, pRow);
            }
        }
    }
    double dWhite2 = (double)FLOAT_8061aa90;
    text_set_color_rgb(dWhite2, dWhite2, dWhite2);
    FUN_80382714();
    return (float)dRowH;   // ROM leaves the row-advance height in f1
}

// ============================================================================
// Address: 0x8032e2a8 | match_rect_inflate
// Build an inflated rect from a point (+0x4 x, +0x8 y) by +/- fRadius on both
// axes: out[0..3] = { x-r, y-r, x+r, y+r }.
// ============================================================================
void match_rect_inflate(double fRadius, float *pOut, int pPoint) {
    float fx = *(float *)(pPoint + 4);
    float fy = *(float *)(pPoint + 8);
    pOut[0] = (float)((double)fx - fRadius);
    pOut[1] = (float)((double)fy - fRadius);
    pOut[2] = (float)((double)fx + fRadius);
    pOut[3] = (float)((double)fy + fRadius);
}

// ============================================================================
// Address: 0x8032e2d4 | match_column_marker_draw
// Draws a column node marker (kind-dependent quad + connector lines) for the
// editor tree, with hit-test toggle/callback on hover. The raw decomp models the
// row's kind as a float bit-pattern tag; here it is the real integer kind
// (0=leaf, 1=branch, 2=mid). Runs under the local-id lock. The paired-single
// struct copies are the SDK's vec4 stores; expressed as plain rect writes.
// ============================================================================
void match_column_marker_draw(int nUnused0, double yNode, double xNode, int nParam4,
                              int nColor, int pToggle, void *pfnCb, u32 uLineColor) {
    (void)nUnused0; (void)nParam4; (void)nColor;
    long long llPair = (long long)FUN_803826e8();
    int pElem = (int)(unsigned long long)((unsigned long long)llPair >> 32);
    int nKind = (int)llPair;

    float fLimit = (float)((double)DAT_806109c8 - DOUBLE_8061aa48);
    if ((DAT_80612ce4 != 0) && ((double)FLOAT_8061aa28 <= yNode) && (yNode <= (double)fLimit)) {
        float fNodeX = (float)nKind;              // (float)(int)pair lo, retained as tag
        int   nTag = nKind;

        if (-1 < nKind) {
            gx_draw_line_f(xNode, yNode, xNode, yNode, uLineColor);
        }

        float aQuad[4];
        int nTexKind = -1;
        double dRadius = (double)FLOAT_8061aa28;
        u32 uAlpha = (u32)FUN_803825c0((double)(FLOAT_8061aa30 * *(float *)(pElem + 0xf0))) & 0xff;

        if (nTag == 0) {
            dRadius = (double)FLOAT_8061aa24;
            nTexKind = 0;
        } else if (nTag < 0) {
            /* no marker */
        } else if (nTag < 1) {
            dRadius = (double)FLOAT_8061aa14;
            nTexKind = 3;
        } else if (nTag < 3) {
            nTexKind = 0;
            dRadius = (double)FLOAT_8061aa24;
        }
        (void)fNodeX;

        float aInner[4];
        match_rect_inflate(dRadius, aQuad, (int)aInner);
        match_rect_inflate((double)(float)((double)FLOAT_8061aa14 * dRadius), aInner, (int)aQuad);

        if ((nTag != 0) && (DAT_80612ce8 != 0) &&
            (hud_cursor_hit_test_rect(aInner) != 0)) {
            if ((*(uint *)(pElem + 0xc0) & 0x10) != 0) {
                if (pfnCb == (void *)0) {
                    *(char *)(pToggle + 0x18) = '\x01' - *(char *)(pToggle + 0x18);
                } else {
                    ((void (*)(int))pfnCb)(pToggle);
                }
                hud_screen_mark_hidden(pElem);
            }
            color_scale_rgb((double)FLOAT_8061aa94, uLineColor);
        }

        int nTex = sprite_get_texture_id(nTexKind);
        gx_set_active_material(nTex);
        int pMat = *DAT_80610a20 + nTex * 0x58;
        gx_draw_sprite_quad((double)aQuad[0], (double)aQuad[1], (double)aQuad[2], (double)aQuad[3],
                            0, 0, (uint)*(ushort *)(pMat + 0x4e) << 4,
                            (uint)*(ushort *)(pMat + 0x52) << 4);

        if (nTag == 2) {
            int nY = (int)yNode;
            gx_draw_line_i((int)(FLOAT_8061aa14 + aQuad[0]), nY, (int)aQuad[2], nY, uAlpha);
        } else if (nTag == 1) {
            int nY = (int)yNode;
            gx_draw_line_counted((int)(FLOAT_8061aa14 + aQuad[0]), nY, (int)aQuad[2], nY, uAlpha);
            int nX = (int)xNode;
            gx_draw_line_counted(nX, (int)(FLOAT_8061aa2c + aQuad[1]), nX,
                                 (int)(aQuad[3] - FLOAT_8061aa90), uAlpha);
        }
    }
    FUN_80382734();
}

// ============================================================================
// Address: 0x8032e6d4 | match_column_marker_draw_default
// Convenience wrapper: draw a column marker at the origin/default geometry with
// no color, toggle target, or callback.
// ============================================================================
void match_column_marker_draw_default(int pToggle) {
    double dZero = (double)FLOAT_8061aa28;
    match_column_marker_draw((int)dZero, dZero, dZero, pToggle, -1, 0, (void *)0, 0);
}

// ============================================================================
// Address: 0x8032e710 | match_draw_clip_vline
// Draw a vertical line at x from y0..y1, clipped to the visible band via
// aveditor_clip_segment_y; skips when the editor rows are hidden.
// ============================================================================
void match_draw_clip_vline(double x, double y0, double y1, u32 color) {
    if (DAT_80612ce4 != 0) {
        float aY[4];
        float fY1;
        aY[0] = (float)y0;
        fY1 = (float)y1;
        if (aveditor_clip_segment_y(aY, &fY1) != 0) {
            gx_draw_line_f(x, (double)aY[0], x, (double)fY1, color);
        }
    }
}

// ============================================================================
// Address: 0x8032e78c | match_participants_max_score
// Reduce over active participants (block +0x41e4 stride 0x220, count +0x41e0):
// return the maximum squad score (squad +0x8254, indexed by participant +0x0)
// among active (+0x4 byte == 1) entries; 0 if none.
// ============================================================================
int match_participants_max_score(void) {
    int nMax = 0;
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0x41e0);
    if (0 < nRemaining) {
        do {
            int *piPart = (int *)(DAT_80612be4 + iOff + 0x41e4);
            if (*(char *)(piPart + 1) == '\x01') {
                int nScore = *(int *)(DAT_80612be4 + *piPart * 0xb4 + 0x8254);
                if (nMax < nScore) {
                    nMax = nScore;
                }
            }
            iOff += 0x220;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return nMax;
}

// ---- additional per-file externs for batch_936 ----------------------------
// DAT_80612be4 (g_MatchSession), DAT_80612c14 (block-record list head),
// DOUBLE_8061aa48, FLOAT_8061aa90, FUN_803826f0, FUN_8038273c, FUN_8032e810
// and match_delete_entry / match_participants_max_score are all declared or
// defined earlier in this file.
extern int  FUN_80337474(int, int, int, int, int, int, int, int); // rank-cell reflow/advance accumulator
// ============================================================================
// Address: 0x8032e7ec | match_participant_count_plus_one
// One past the current maximum squad score (the value a freshly-scoring
// participant would take). Thin wrapper over match_participants_max_score.
// ============================================================================
int match_participant_count_plus_one(void) {
    return match_participants_max_score() + 1;
}
// ============================================================================
// Address: 0x8032e810 | match_squad_find_by_score
// Linear scan of the squad array (base +0x81a8, stride 0xb4, count +0x81a4)
// for the first entry whose score/rank field (+0xac) equals nScore. Returns
// the entry pointer, or 0 when none match.
// ============================================================================
int match_squad_find_by_score(int nScore) {
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0x81a4);
    if (0 < nRemaining) {
        do {
            int pEntry = DAT_80612be4 + iOff + 0x81a8;
            if (*(int *)(pEntry + 0xac) == nScore) {
                return pEntry;
            }
            iOff += 0xb4;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return 0;
}
// ============================================================================
// Address: 0x8032e85c | match_squad_team_block_by_score
// Resolve the squad whose rank == nScore (match_squad_find_by_score) and map
// its team index (+0xa4) to the corresponding team block (base +0x41e4,
// stride 0x220). Returns the team-block pointer.
// ============================================================================
int match_squad_team_block_by_score(int nScore) {
    int pEntry = match_squad_find_by_score(nScore);
    return DAT_80612be4 + *(int *)(pEntry + 0xa4) * 0x220 + 0x41e4;
}
// ============================================================================
// Address: 0x8032e890 | match_rank_widgets_swap
// Walk every block record (DAT_80612c14, next @ +0x40) and, within each, every
// child widget (head @ +0x3c, next @ +0x14 words == [5]). For rank widgets
// (type 5 or 6) whose rank value ([6]) is one of the swapped pair, exchange
// nRankA <-> nRankA+nDelta. Used when two adjacent ranks trade places.
// ============================================================================
void match_rank_widgets_swap(int nRankA, int nDelta) {
    for (int iBlock = DAT_80612c14; iBlock != 0; iBlock = *(int *)(iBlock + 0x40)) {
        for (int *piWidget = *(int **)(iBlock + 0x3c); piWidget != (int *)0x0;
             piWidget = (int *)piWidget[5]) {
            if ((*piWidget == 5) || (*piWidget == 6)) {
                if (piWidget[6] == nRankA + nDelta) {
                    piWidget[6] = nRankA;
                } else if (piWidget[6] == nRankA) {
                    piWidget[6] = nRankA + nDelta;
                }
            }
        }
    }
}
// ============================================================================
// Address: 0x8032e8f4 | match_nearest_free_score
// Among the team blocks (base +0x41e4, stride 0x220, count +0x41dc byte),
// return the score value (index+1) that (a) is not currently assigned to any
// squad (match_squad_find_by_score == 0) and (b) is closest to nTarget. The
// distance compare uses the 0x43300000 magic-constant int->double trick.
// nTarget arrives via the saved r28 in the ROM; modelled as the parameter.
// ============================================================================
int match_nearest_free_score(int nTarget) {
    byte nCount = *(byte *)(DAT_80612be4 + 0x41dc);
    uint uBest = (uint)nCount;
    for (int i = 0; i < (int)(uint)nCount; i = i + 1) {
        int pEntry = match_squad_find_by_score(i + 1);
        uint uCand = (i + 1) - nTarget;
        if ((pEntry == 0) &&
            (ABS((float)((double)CONCAT44(0x43300000, uCand ^ 0x80000000) - DOUBLE_8061aa48)) <
             ABS((float)((double)CONCAT44(0x43300000, uBest ^ 0x80000000) - DOUBLE_8061aa48)))) {
            uBest = uCand;
        }
    }
    return (int)uBest;
}
// ============================================================================
// Address: 0x8032e9b0 | match_ranks_shift_for_drag
// Reorder squad ranks when the anchor entry is dragged by nDelta. pDragOp
// carries the clamp bounds (+0x14 == limit, +0x18 == anchor entry ptr). For
// every other scored squad, nudge its rank by one toward the vacated slot
// (respecting the direction/limit), then reflow the rank widgets
// (match_rank_widgets_swap) and advance the cell (FUN_80337474). The raw
// decomp's 8-arg prototype is a register-spill misread of that varargs call.
// ============================================================================
void match_ranks_shift_for_drag(int pDragOp, int nDelta) {
    int pAnchor = *(int *)(pDragOp + 0x18);
    int iOff = 0;
    int nAnchorScore = *(int *)(pAnchor + 0xac);
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int pEntry = DAT_80612be4 + iOff + 0x81a8;
        if (pEntry != pAnchor) {
            int nScore = *(int *)(pEntry + 0xac);
            if (0 < nScore) {
                if (nDelta != 0) {
                    if (nDelta < 0) {
                        if ((-2 < nDelta) && (nScore <= nAnchorScore) &&
                            (*(int *)(pDragOp + 0x14) <= nScore)) {
                            *(int *)(pEntry + 0xac) = nScore + -1;
                        }
                    } else if ((nDelta < 2) && (nAnchorScore <= nScore) &&
                               (nScore <= *(int *)(pDragOp + 0x14))) {
                        *(int *)(pEntry + 0xac) = nScore + 1;
                    }
                }
                match_rank_widgets_swap(*(int *)(pEntry + 0xac), nDelta);
                FUN_80337474(*(int *)(pEntry + 0xac), 0, 0, 0, 0, 0, 0, 0);
            }
        }
        iOff += 0xb4;
    }
}
// ============================================================================
// Address: 0x8032eaa4 | match_ranks_shift_teamplay
// Teamplay variant of the drag reorder. Walks the team blocks (+0x41e4 stride
// 0x220, count +0x41e0). For each active (+0x4 byte == 1) block whose squad
// rank matches the anchor's, compute the nearest free score
// (match_nearest_free_score) and either nudge that squad one step and reflow
// (small delta, within FLOAT_8061aa90) or defer to the per-squad shifter
// (match_ranks_shift_for_drag). pDragOp arrives via saved r25.
// ============================================================================
void match_ranks_shift_teamplay(int pDragOp) {
    int pAnchor = *(int *)(pDragOp + 0x18);
    int iOff = 0;
    int nAnchorScore = *(int *)(pAnchor + 0xac);
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x41e0); i = i + 1) {
        int *piBlock = (int *)(DAT_80612be4 + iOff + 0x41e4);
        int pSquad = DAT_80612be4 + *piBlock * 0xb4 + 0x81a8;
        if ((*(char *)(piBlock + 1) == '\x01') && (*(int *)(pSquad + 0xac) == nAnchorScore) &&
            (pSquad != pAnchor)) {
            int nFree = match_nearest_free_score(nAnchorScore);
            uint uDiff = nFree - *(int *)(pSquad + 0xac);
            int nStep = (0 < (int)uDiff) ? 1 : -1;
            double dDist = ABS((double)(float)((double)CONCAT44(0x43300000, uDiff ^ 0x80000000) -
                                                DOUBLE_8061aa48));
            if (dDist <= (double)FLOAT_8061aa90) {
                *(int *)(pSquad + 0xac) = *(int *)(pSquad + 0xac) + nStep;
                FUN_80337474((int)dDist, 0, 0, 0, 0, 0, 0, 0);
                FUN_80337474((int)dDist, 0, 0, 0, 0, 0, 0, 0);
                match_rank_widgets_swap(nAnchorScore, nStep);
            } else {
                match_ranks_shift_for_drag(pDragOp, nStep);
            }
        }
        iOff += 0x220;
    }
}
// ============================================================================
// Address: 0x8032ebbc | match_rank_widgets_decrement_after
// After a squad is deleted, walk every rank widget (types 5/6 across the block
// records) and: clear (-1) any widget pointing at the deleted squad's rank
// (+0xac), and decrement any widget whose rank is above it.
// ============================================================================
void match_rank_widgets_decrement_after(int pDeleted) {
    for (int iBlock = DAT_80612c14; iBlock != 0; iBlock = *(int *)(iBlock + 0x40)) {
        for (int *piWidget = *(int **)(iBlock + 0x3c); piWidget != (int *)0x0;
             piWidget = (int *)piWidget[5]) {
            if ((*piWidget == 5) || (*piWidget == 6)) {
                int nRank = piWidget[6];
                if (*(int *)(pDeleted + 0xac) < nRank) {
                    piWidget[6] = nRank + -1;
                } else if (nRank == *(int *)(pDeleted + 0xac)) {
                    piWidget[6] = -1;
                }
            }
        }
    }
}
// ============================================================================
// Address: 0x8032ec24 | match_delete_current_entry
// Thin wrapper that deletes the current squad entry (match_delete_entry).
// ============================================================================
void match_delete_current_entry(void) {
    match_delete_entry();
}
// ============================================================================
// Address: 0x8032ec68 | match_delete_current_and_reflow
// Delete the current squad (FUN_803826f0) and close the rank gap it leaves:
// clear/decrement its rank widgets (match_rank_widgets_decrement_after), run
// the entry delete, then decrement the +0xac score of every squad ranked
// below it, advancing the affected cell each time (FUN_80337474). The raw
// 8-arg prototype is a register-spill misread of that varargs call.
// ============================================================================
void match_delete_current_and_reflow(void) {
    int pCurrent = FUN_803826f0();
    match_rank_widgets_decrement_after(pCurrent);
    int nAcc = 0;
    match_delete_current_entry();
    int iOff = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int pEntry = DAT_80612be4 + iOff + 0x81a8;
        if ((pEntry != pCurrent) && (*(int *)(pCurrent + 0xac) < *(int *)(pEntry + 0xac))) {
            *(int *)(pEntry + 0xac) = *(int *)(pEntry + 0xac) + -1;
            nAcc = FUN_80337474(nAcc, 0, 0, 0, 0, 0, 0, 0);
        }
        iOff += 0xb4;
    }
    FUN_8038273c();
}

// ---- additional per-file externs for batch_937 ----------------------------
// Animated squad-rank "ladder"/scoreboard renderer for the arcade editor.
// Scoreboard animation context (0x110 bytes, built by match_scoreboard_context_build):
//   entries: array of 0x20-byte slots (count = g_MatchSession+0x41dc byte)
//     +0x04 f : current Y (animated)      +0x08 f : (scratch / prev target)
//     +0x10 i : source rank value         +0x14 i : cached rank (last frame)
//     +0x18 p : squad-entry pointer       +0x1c i : animating flag
//   +0x100 i : "active" flag
//   +0x104 p : per-entry animate fn  (match_scoreboard_entry_animate)
//   +0x108 p : reflow fn             (match_ranks_shift_teamplay)
//   +0x10c p : per-entry draw fn     (LAB_8032eec0 -> match_scoreboard_layout_targets tail)
extern int   DAT_80612cd4;         // scoreboard animation context handle (0x110 bytes) or 0
extern float FLOAT_8061aa98;       // scoreboard slide-speed scale
extern float FLOAT_8061aa9c;       // scoreboard row pitch
extern float FLOAT_8061aaa0;       // ladder column x-fraction of framebuffer width
extern int   DAT_806108e4;         // frame delta-time numerator (ms)
extern int   DAT_806109c0;         // frame delta-time denominator (target fps base)
extern int   DAT_80612cd8;         // per-column-style connector Y cache (stride 4, index DAT_80612ce0)
extern char  DAT_805d67a8;         // scoreboard row style descriptor (passed as &DAT_805d67a8)
extern int   DAT_80481670;         // per-mode row color word (unselected) table (stride 0x24)
extern int   DAT_80481678;         // per-mode row color word (selected) table (stride 0x24)
extern int   FUN_803826d4(void);   // -> current editor screen handle (no lock)
extern void  FUN_80382720(void);   // release editor screen handle
extern void  LAB_8032eec0(int pEntry, char *pStyle); // scoreboard row commit (tail of layout)
// match_squad_find_by_score, match_ranks_shift_teamplay, match_participants_max_score,
// match_option_row_draw, match_confirm_dialog_list, match_column_marker_draw,
// match_draw_clip_vline, match_editor_begin, DAT_80612be4, DAT_80612d00, DAT_80612ce0,
// DAT_80612ce4, mem_alloc, mem_free_top, FUN_803826e8, FUN_80382734 are earlier.
// ============================================================================
// Address: 0x8032ecfc | match_selected_score_if_unique
// Count how many squads share the currently-selected squad's score
// (DAT_80612d00+0xac) across the squad array (+0x8254, stride 0xb4). Returns
// that score if exactly one squad holds it, else 0 (branchless in the ROM).
// ============================================================================
uint match_selected_score_if_unique(void) {
    uint uCount = 0;
    int iOff = 0;
    int nRemaining = *(int *)(DAT_80612be4 + 0x81a4);
    if (0 < nRemaining) {
        do {
            if (*(uint *)(DAT_80612be4 + iOff + 0x8254) == *(uint *)(DAT_80612d00 + 0xac)) {
                uCount = uCount + 1;
            }
            iOff += 0xb4;
            nRemaining += -1;
        } while (nRemaining != 0);
    }
    return *(uint *)(DAT_80612d00 + 0xac) & -(uint)(1 < uCount) - ((int)uCount >> 0x1f);
}
// ============================================================================
// Address: 0x8032ed60 | match_scoreboard_entry_animate
// Per-entry animation tick. Refresh the cached rank (+0x14) from the squad
// (+0x18 -> +0xac). When the entry is still animating (+0x1c), advance its
// current Y (+0x4) toward the target by a frame-rate-scaled step derived from
// the source rank (+0x10); clear the animating flag once within one step.
// ============================================================================
void match_scoreboard_entry_animate(int pEntry) {
    *(int *)(pEntry + 0x14) = *(int *)(*(int *)(pEntry + 0x18) + 0xac);
    if (*(int *)(pEntry + 0x1c) != 0) {
        float fStep = FLOAT_8061aa98 *
                      (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) -
                              DOUBLE_8061aa48) *
                      (FLOAT_8061aa90 /
                       (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                               DOUBLE_8061aa48));
        uint uDone = countLeadingZeros(
            (uint)(ABS(*(float *)(pEntry + 4) - *(float *)(pEntry + 8)) < fStep));
        *(float *)(pEntry + 4) =
            (float)((double)CONCAT44(0x43300000, *(uint *)(pEntry + 0x10) ^ 0x80000000) -
                    DOUBLE_8061aa48) * fStep + *(float *)(pEntry + 4);
        *(uint *)(pEntry + 0x1c) = uDone >> 5;
    }
}
// ============================================================================
// Address: 0x8032ee2c | match_scoreboard_layout_targets
// Compute the target Y (+0x3, i.e. entry[3]) and initial current Y (entry[0])
// for every scoreboard entry (count = g_MatchSession+0x41dc byte, stride 0x20)
// from its squad's rank (+0x18 -> +0xac). Mirror the current Y into pMirror and
// entry[1], then clear the ctx "active" flag (+0x100).
// ============================================================================
void match_scoreboard_layout_targets(double yBase, double yPitch, int pEntries, int pMirror) {
    double dHalf = DOUBLE_8061aa48;
    double dRowPitch = (double)FLOAT_8061aa9c;
    int iMirror = 0;
    int iOff = 0;
    for (int i = 0; i < (int)(uint)*(byte *)(DAT_80612be4 + 0x41dc); i = i + 1) {
        float *pfEntry = (float *)(pEntries + iOff);
        iOff += 0x20;
        pfEntry[3] = (float)(dRowPitch + yPitch);
        pfEntry[0] = (float)((double)(float)((double)CONCAT44(0x43300000,
                                    *(int *)((int)pfEntry[6] + 0xac) - 1U ^ 0x80000000) - dHalf) *
                             (double)pfEntry[3] + yBase);
        *(float *)(pMirror + iMirror) = pfEntry[0];
        iMirror += 4;
        pfEntry[1] = pfEntry[0];
    }
    *(int *)(pEntries + 0x100) = 0;
}
// ============================================================================
// Address: 0x8032ef14 | match_scoreboard_find_entry_by_squad
// Scan the scoreboard entries (count +0x41dc byte, stride 0x20) for the one
// whose squad pointer (+0x18) equals pSquad. Returns the entry pointer or 0.
// ============================================================================
int match_scoreboard_find_entry_by_squad(int pSquad, int pEntries) {
    int iOff = 0;
    uint uRemaining = (uint)*(byte *)(DAT_80612be4 + 0x41dc);
    while (true) {
        if (uRemaining == 0) {
            return 0;
        }
        if (*(int *)(pEntries + iOff + 0x18) == pSquad) {
            break;
        }
        iOff += 0x20;
        uRemaining -= 1;
    }
    return pEntries + iOff;
}
// ============================================================================
// Address: 0x8032ef54 | match_scoreboard_context_build
// Allocate the 0x110-byte scoreboard animation context under the local-id lock
// (FUN_803826e8), populate one 0x20-byte entry per team slot (count +0x41dc):
// bind the squad pointer (match_squad_find_by_score(rank+1)), seed cached rank
// (+0x14) and clear the animating flag (+0x1c). Then set the "active" flag and
// the three callbacks and release the lock. Returns the context pointer. The
// raw 16-arg prototype is a register-spill misread of the mem_alloc varargs.
// ============================================================================
int match_scoreboard_context_build(void) {
    int nLockId = FUN_803826e8();
    int pCtx = mem_alloc(0x110, 0);
    (void)nLockId;
    int iOff = 0;
    for (int i = 0; i < (int)(uint)*(byte *)(DAT_80612be4 + 0x41dc); i = i + 1) {
        int pEntry = pCtx + iOff;
        int pSquad = match_squad_find_by_score(i + 1);
        *(int *)(pEntry + 0x18) = pSquad;
        iOff += 0x20;
        *(int *)(pEntry + 0x14) = *(int *)(*(int *)(pEntry + 0x18) + 0xac);
        *(int *)(pEntry + 0x1c) = 0;
    }
    *(int *)(pCtx + 0x100) = 1;
    *(void **)(pCtx + 0x104) = (void *)match_scoreboard_entry_animate;
    *(void **)(pCtx + 0x108) = (void *)match_ranks_shift_teamplay;
    *(void **)(pCtx + 0x10c) = (void *)&LAB_8032eec0;
    FUN_80382734();
    return pCtx;
}
// ============================================================================
// Address: 0x8032f008 | match_scoreboard_find_entry_or_last
// Like match_scoreboard_find_entry_by_squad, but returns the last-scanned entry
// pointer (0 when the list is empty) rather than 0 on a miss. Used by the tick
// to locate the entry for the currently-selected squad.
// ============================================================================
int match_scoreboard_find_entry_or_last(int pSquad, int pEntries) {
    uint uRemaining = (uint)*(byte *)(DAT_80612be4 + 0x41dc);
    int iOff = 0;
    int pEntry = 0;
    for (; ; iOff += 0x20) {
        pEntry = 0;
        if (uRemaining == 0) {
            break;
        }
        pEntry = pEntries + iOff;
        if (*(int *)(pEntry + 0x18) == pSquad) {
            break;
        }
        uRemaining -= 1;
    }
    return pEntry;
}
// ============================================================================
// Address: 0x8032f04c | match_scoreboard_update
// Per-frame tick for the animated scoreboard. Lazily builds the context
// (match_scoreboard_context_build) when a selection becomes active and none
// exists, frees it when the selection deactivates (state 5), then: re-lays out
// targets, runs the reflow for the selected squad, and per entry applies the
// draw / commit / animate callbacks, promoting a just-vacated rank to 1 when the
// selected score is no longer unique. pTick (r28) carries the selection record.
// ============================================================================
void match_scoreboard_update(double yBase, double yPitch, int pTick) {
    uint uUnique = match_selected_score_if_unique();
    if ((DAT_80612cd4 == 0) && (pTick != 0) && (*(int *)(pTick + 0x1ac) != 5)) {
        DAT_80612cd4 = match_scoreboard_context_build();
    } else if ((*(int *)(pTick + 0x1ac) == 5) && (DAT_80612cd4 != 0)) {
        mem_free_top(DAT_80612cd4);
        DAT_80612cd4 = 0;
    }
    if (DAT_80612cd4 != 0) {
        if ((*(int *)(DAT_80612cd4 + 0x100) != 0) && (pTick != 0)) {
            match_scoreboard_layout_targets(yBase, yPitch, DAT_80612cd4, (int)&DAT_805d67a8);
        }
        if (uUnique != 0) {
            match_scoreboard_find_entry_or_last(DAT_80612d00, DAT_80612cd4);
            ((void (*)(void))*(void **)(DAT_80612cd4 + 0x108))();
        }
        int i = 0;
        int iOff = 0;
        while (true) {
            if ((int)(uint)*(byte *)(DAT_80612be4 + 0x41dc) <= i) {
                break;
            }
            int pEntry = DAT_80612cd4 + iOff;
            int nRank = *(int *)(*(int *)(pEntry + 0x18) + 0xac);
            if (nRank != *(int *)(pEntry + 0x14)) {
                if ((uUnique == 0) && (nRank == 0)) {
                    *(int *)(*(int *)(pEntry + 0x18) + 0xac) = 1;
                    ((void (*)(int))*(void **)(DAT_80612cd4 + 0x108))(pEntry);
                }
                ((void (*)(int, char *))*(void **)(DAT_80612cd4 + 0x10c))(pEntry, &DAT_805d67a8);
            }
            ((void (*)(int))*(void **)(DAT_80612cd4 + 0x104))(pEntry);
            i = i + 1;
            iOff = iOff + 0x20;
        }
    }
}
// ============================================================================
// Address: 0x8032f1f4 | match_ladder_draw
// Draw the animated squad-rank ladder. Tick the scoreboard
// (match_scoreboard_update), then for each rank 1..max draw its option row
// (match_option_row_draw) at the animated Y sourced from the scoreboard entry
// (match_scoreboard_find_entry_by_squad), selecting the selected/unselected row
// color table (DAT_80481678 / DAT_80481670) by whether the squad is the current
// selection (DAT_80612d00). nShowConnectors is stashed in DAT_80612ce4.
// ============================================================================
double match_ladder_draw(int nUnused1, int nUnused2, float *pGeom, int nShowConnectors) {
    (void)nUnused1; (void)nUnused2;
    int nScreen = FUN_803826d4();
    int nMax = match_participants_max_score();
    double yStart = (double)(pGeom[1] + FLOAT_80611a64);
    double yRow = (double)(pGeom[1] + FLOAT_80611a64);
    double dRowStep = (double)(FLOAT_8061aa14 * FLOAT_80611a64);
    double xCol = (double)(float)((double)FLOAT_8061aaa0 * (double)FLOAT_80611a68 + (double)*pGeom);
    match_scoreboard_update(yRow, dRowStep, DAT_80612cd4);
    DAT_80612ce4 = nShowConnectors;
    for (int i = 0; i < nMax; i = i + 1) {
        double yBase = yRow;
        int pSquad = match_squad_find_by_score(i + 1);
        bool bSelected = (DAT_80612d00 == pSquad);
        int nMode = *(int *)(DAT_80612be4 + 0x248);
        int nColor = bSelected ? *(int *)(&DAT_80481678 + nMode * 0x24)
                               : *(int *)(&DAT_80481670 + nMode * 0x24);
        if (DAT_80612cd4 != 0) {
            int pEntry = match_scoreboard_find_entry_by_squad(pSquad, DAT_80612cd4);
            yRow = (double)*(float *)(pEntry + 4);
        }
        int nHasCursor = bSelected ? 2 : 0;
        float fRowH = match_option_row_draw((int)FLOAT_8061aa28, xCol, yRow, nScreen, nHasCursor,
                                            nMode, (int *)0, (byte *)(pSquad + 0x20), 1, 0, 1,
                                            nColor, (void *)0, (void *)match_confirm_dialog_list);
        yRow = (double)(float)(yBase + (double)(float)((double)fRowH + dRowStep));
    }
    FUN_80382720();
    return (double)(float)(yRow - yStart);   // ROM returns accumulated ladder height in f1
}
// ============================================================================
// Address: 0x8032f39c | match_ladder_draw_active
// Wrapper that draws the ladder with connectors enabled (nShowConnectors = 1).
// ============================================================================
void match_ladder_draw_active(int nUnused1, int nUnused2, float *pGeom) {
    match_ladder_draw(nUnused1, nUnused2, pGeom, 1);
}
// ============================================================================
// Address: 0x8032f3c0 | match_ladder_column_draw
// Draw one ladder column entry: pick the row field (+0x18 for kind 1/3, else
// +0x8), draw its option row (match_option_row_draw with the editor-begin
// action), then draw the connector vline from the cached style Y
// (DAT_80612cd8[DAT_80612ce0]) to the row bottom and the column node marker
// (match_column_marker_draw). Returns the row height (f1).
// ============================================================================
double match_ladder_column_draw(double xCol, double yRow, double dRowStep, int nScreen,
                                int nMode, int *pRow, uint uLineColor, int nKind, int nColor) {
    bool bBranch = (nKind == 1) || (nKind == 3);
    int *pField = bBranch ? (pRow + 0x18) : (pRow + 8);
    double xEntry = (double)(float)(xCol + dRowStep);
    double dRowH = (double)match_option_row_draw((int)FLOAT_8061aa28, xEntry, yRow, nScreen, 1,
                                                 nMode, pRow, (byte *)pField, 1, 0, 1, nColor,
                                                 (void *)match_editor_begin,
                                                 (void *)match_confirm_dialog_list);
    double yBottom = (double)(float)((double)FLOAT_8061aa40 * dRowH + yRow);
    match_draw_clip_vline(xCol, (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4), yBottom,
                          uLineColor);
    int iStyle = DAT_80612ce0 * 4;
    *(float *)(&DAT_80612cd8 + iStyle) = (float)yBottom;
    match_column_marker_draw((int)xCol, (double)*(float *)(&DAT_80612cd8 + iStyle), xEntry, nScreen,
                             0, 0, (void *)0, uLineColor);
    return dRowH;
}

// ---- additional per-file externs for batch_938 ----------------------------
// Arcade custom-match editor tree/column renderer. match_editor_tree_draw is
// the central walker; it dispatches on the editor view mode (g_MatchSession
// word +0x91: 0 = full descriptor tree, 1 = filter kind-1, 2 = filter kind-2)
// and lays out nested rows/columns with connector lines.
extern int   DAT_80481508;         // per-kind add-slot string-id table (base; stride 0x34 words)
extern int   DAT_80481670;         // per-mode row color: default        (stride 0x24)
extern int   DAT_80481674;         // per-mode row color: child          (stride 0x24)
extern int   DAT_8048167c;         // per-mode row color: alt child       (stride 0x24)
extern int   DAT_80481680;         // per-mode row color: capability row  (stride 0x24)
extern int   DAT_80481740;         // per-capability-slot label string-id table (stride 0x20)
extern int   DAT_8048174c;         // per-capability-slot flag word within same 0x20 row
extern float DAT_804817d8;         // top-level ladder geometry rect (float[4])
extern float FLOAT_8061aaa8;       // scroll overflow x-fraction
extern void *PTR_FUN_804817e8;     // editor vtable A: descriptor-tree screen
extern void *PTR_FUN_804817f8;     // editor vtable B: rank-ladder screen
extern void  LAB_8032b1f4(int pDesc); // descriptor-select action (mid-function label used as cb)
extern void  match_add_slot_dispatch(u64, u64, u64, u64, u64, u64, u64, u64,
                                     int, int, int, int, int, int, int, int); // add-slot action cb
extern void  FUN_8033277c(int, int, int, int); // capability-row toggle action cb
extern void  FUN_8038270c(void);   // release editor tree lock (FUN_8032f960 tail)
extern int   FUN_803826c0(void);   // -> (screenElem<<32 | ?) pair; hi = current screen element
extern void  hud_scroll_to_normalized_pos(double t, int pElem); // scroll list to normalized pos
extern int   hud_style_push_element(int mode);   // push element style, returns previous
extern int   hud_style_apply_variant(int variant); // apply style variant, returns previous
extern void  hud_add_spacer_item(double h, int a, void *pfnDraw); // add a custom-draw list item
extern void  hud_screen_clear_all_handlers(int screen);
extern void  screen_exit_clear_mode(void);       // list-item init callback
// match_session_count_unowned_squads, match_squad_entry_flag_clear,
// match_desc_byte_equals, match_tag_entry_has_pending, match_squad_has_cap_pair,
// match_add_slot_dispatch, match_pending_selection_absent, match_clone_buffer_alloc,
// match_delete_entry, match_squad_team_block_by_score, match_ladder_column_draw,
// match_ladder_draw, match_ladder_draw_active, match_option_row_draw,
// match_column_marker_draw, match_column_marker_draw_default, match_draw_clip_vline,
// match_confirm_dialog_list, match_confirm_dialog_grid, match_editor_begin,
// FUN_803825c0, FUN_80348238, hud_cursor_hit_test_rect, get_string_by_id,
// hud_screen_mark_hidden, hud_screen_register_handler, hud_element_init_from_layout,
// hud_element_scroll_h_overflow, hud_set_item_padding_mode, hud_element_apply_scroll,
// hud_element_finalize_frame, FUN_803826f0, FUN_8038273c and the DAT_80612c**/
// DAT_805d6778/FLOAT_806** symbols are declared earlier in this file.
// ============================================================================
// Address: 0x8032f520 | match_ladder_column_draw_active
// Column-entry draw variant with the cursor highlight forced on (nHasCursor=2),
// used for the currently-selected squad. Same body as match_ladder_column_draw
// but always takes the +0x8 row field. Returns the row height (f1).
// ============================================================================
double match_ladder_column_draw_active(double xCol, double yRow, double dRowStep, int nScreen,
                                       int nMode, int *pRow, uint uLineColor, int nColor) {
    double xEntry = (double)(float)(xCol + dRowStep);
    double dRowH = (double)match_option_row_draw((int)FLOAT_8061aa28, xEntry, yRow, nScreen, 2,
                                                 nMode, pRow, (byte *)(pRow + 8), 1, 0, 1, nColor,
                                                 (void *)match_editor_begin,
                                                 (void *)match_confirm_dialog_list);
    double yBottom = (double)(float)((double)FLOAT_8061aa40 * dRowH + yRow);
    match_draw_clip_vline(xCol, (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4), yBottom,
                          uLineColor);
    int iStyle = DAT_80612ce0 * 4;
    *(float *)(&DAT_80612cd8 + iStyle) = (float)yBottom;
    match_column_marker_draw((int)xCol, (double)*(float *)(&DAT_80612cd8 + iStyle), xEntry, nScreen,
                             0, 0, (void *)0, uLineColor);
    return dRowH;
}
// ============================================================================
// Address: 0x8032f654 | match_ladder_extra_row_draw
// Draw a plain labeled ladder row (no action/hover callbacks) plus its
// connector vline and node marker. Used for the "unlimited ammo"/"one-shot
// kills" style extra option rows. Returns the row height (f1).
// ============================================================================
double match_ladder_extra_row_draw(double xCol, double yRow, double dRowStep, int nScreen,
                                   int nMode, byte *pszLabel, uint uLineColor, int nColor) {
    double xEntry = (double)(float)(xCol + dRowStep);
    double dRowH = (double)match_option_row_draw((int)FLOAT_8061aa28, xEntry, yRow, nScreen, 1,
                                                 nMode, (int *)0, pszLabel, 1, 0, 1, nColor,
                                                 (void *)0, (void *)0);
    double yBottom = (double)(float)((double)FLOAT_8061aa40 * dRowH + yRow);
    match_draw_clip_vline(xCol, (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4), yBottom,
                          uLineColor);
    int iStyle = DAT_80612ce0 * 4;
    *(float *)(&DAT_80612cd8 + iStyle) = (float)yBottom;
    match_column_marker_draw((int)xCol, (double)*(float *)(&DAT_80612cd8 + iStyle), xEntry, nScreen,
                             0, 0, (void *)0, uLineColor);
    return dRowH;
}
// ============================================================================
// Address: 0x8032f77c | match_desc_block_for_kind
// Resolve the descriptor/team block for a tree node by its kind: kind 1 maps
// through the score (match_squad_team_block_by_score(nOrdinal+1)); kind 0 maps
// directly to the team block (base +0x41e4, stride 0x220); anything else 0.
// ============================================================================
int match_desc_block_for_kind(int nTeamIdx, int nOrdinal, int nKind) {
    int pBlock = 0;
    if (nKind == 1) {
        pBlock = match_squad_team_block_by_score(nOrdinal + 1);
    } else if ((nKind < 1) && (-1 < nKind)) {
        pBlock = DAT_80612be4 + nTeamIdx * 0x220 + 0x41e4;
    }
    return pBlock;
}
// ============================================================================
// Address: 0x8032f7d8 | match_add_slot_row_draw
// Draw the "add slot" option row for the current mode (+0x244): resolve the
// per-slot label (DAT_80481508 table) and per-slot color, draw the row with the
// add-slot action (match_add_slot_dispatch), then place its node marker. nSlot
// arrives via saved r28. The 6-arg proto captures the inbound geometry regs.
// ============================================================================
void match_add_slot_row_draw(int nScreen, double yRow, double xRow, int nSlot, int nUnused,
                             uint uLineColor) {
    (void)nUnused;
    int nMode = *(int *)(DAT_80612be4 + 0x244);
    byte *pszLabel = (byte *)get_string_by_id(*(int *)(&DAT_80481508 + nMode * 0x34 + nSlot * 4));
    double xEntry = (double)(float)(0.0 + yRow);
    double dRowH = (double)match_option_row_draw(
        (int)FLOAT_8061aa28, xEntry, xRow, nScreen, *(int *)(DAT_80612be4 + 0x244), nSlot, (int *)0,
        pszLabel, 0, 0, 1, *(int *)(nSlot * 0x24 + (int)&DAT_80481680 + nMode * 4),
        (void *)match_add_slot_dispatch, (void *)match_confirm_dialog_list);
    int iStyle = DAT_80612ce0 * 4;
    *(float *)(&DAT_80612cd8 + iStyle) =
        (float)((double)FLOAT_8061aa40 *
                    (double)(float)((double)CONCAT44(0x43300000, (int)dRowH ^ 0x80000000) -
                                    DOUBLE_8061aa48) +
                xRow);
    match_column_marker_draw(nScreen, (double)*(float *)(&DAT_80612cd8 + iStyle), xEntry, nScreen,
                             0, 0, (void *)0, uLineColor);
}
// ============================================================================
// Address: 0x8032f960 | match_editor_tree_draw
// Central renderer for the arcade custom-match editor descriptor tree. Under the
// editor lock (FUN_803826c0 / FUN_8038270c) it resets the per-level connector
// caches (DAT_805d6778 / DAT_80612ce0), sets hover state from the cursor hit
// test, draws the add-slot row (match_add_slot_row_draw), then dispatches on the
// editor view mode word (g_MatchSession[0x91]):
//   0 = full tree: walk every unowned squad block, draw its row + nested
//       child/alt-child columns (match_ladder_column_draw / _active) and, for
//       the 4 capability categories, their capability rows + squad columns and
//       the two optional "extra" rows (match_ladder_extra_row_draw at ids 0xaee
//       / 0xaef);
//   1 = only kind-1 squads (reverse order);
//   2 = only kind-2 squads (reverse order).
// Finally, if a pending selection exists (DAT_80612ccc) it clones+deletes that
// entry and plays the confirm sound (FUN_80348238(0x15)).
// The many float in_fN / spurious spill args are inbound geometry / a
// register-spill misread of the match_clone_buffer_alloc varargs.
// ============================================================================
double match_editor_tree_draw(int nUnused1, int nUnused2, float *pGeom, int *pShowConnectors,
                              int *pMode, int nArgA, int nArgB, int nArgC) {
    (void)nUnused1; (void)nUnused2; (void)nArgA; (void)nArgB; (void)nArgC;
    long long llPair = (long long)FUN_803826c0();
    int pScreen = (int)(unsigned long long)((unsigned long long)llPair >> 32);

    double dScaleX = (double)FLOAT_80611a68;
    double dScaleY = (double)FLOAT_80611a64;
    double xCol = (double)(float)((double)FLOAT_8061aaa0 * dScaleX + (double)*pGeom);
    double yRow = (double)(float)((double)pGeom[1] + dScaleY);
    u32 uAlpha = FUN_803825c0((double)(FLOAT_8061aa30 * *(float *)(pScreen + 0xf0)));
    double dChildX = (double)(float)((double)FLOAT_8061aa8c * dScaleX);
    double dChildIndent = (double)(float)((double)FLOAT_8061aa88 * dScaleX);
    double dRowStep = (double)(float)((double)FLOAT_8061aa14 * dScaleY);
    uint uLineColor = (uAlpha & 0xff) | 0x64646400;

    DAT_80612ce8 = 0;
    if (match_pending_selection_absent() != 0) {
        DAT_80612ccc = 0;
    }

    // reset the 2-level connector style caches (stride 0x18) to -1
    int iReset = 0;
    for (int r = 2; r != 0; r = r + -1) {
        *(int *)((int)&DAT_805d6778 + iReset) = -1;
        iReset += 0x18;
    }
    DAT_80612ce0 = 0;
    DAT_80612ce4 = (int)pShowConnectors;
    if ((pShowConnectors != (int *)0) &&
        (hud_cursor_hit_test_rect((float *)(pScreen + 0x78)) != 0)) {
        DAT_80612ce8 = 1;
    }

    // header add-slot row
    match_add_slot_row_draw(pScreen, dChildX, yRow, (int)pMode, 0, uLineColor);
    double yCursor = (double)(float)(yRow + (double)(float)(0.0 + dRowStep));
    int nView = *(int *)(DAT_80612be4 + 0x91 * 4);

    if (nView == 0) {
        double xIndent = (double)(float)(xCol + dChildX);
        int iBlock = 0;
        int nDrawn = 0;
        for (int i = 0; i < match_session_count_unowned_squads(); i = i + 1) {
            int *piBlock = (int *)(DAT_80612be4 + iBlock + 0x41e4);
            uint uOk = match_squad_entry_flag_clear(DAT_80612be4 + *piBlock * 0x2d * 4 + 0x206a * 4);
            if ((uOk != 0) && (match_desc_byte_equals((int)piBlock, (int)pMode) != 0)) {
                int pDesc = match_desc_block_for_kind(i, nDrawn, (uint)*(byte *)((int)piBlock + 4));
                int pSquad = DAT_80612be4 + *(int *)pDesc * 0x2d * 4 + 0x206a * 4;
                nDrawn = nDrawn + 1;
                if ((pSquad == DAT_80612d00) && (*(int *)(pScreen + 0x1ac) == 2)) {
                    hud_scroll_to_normalized_pos(
                        (double)(ABS((float)(yCursor - (double)pGeom[1])) /
                                 ABS((float)((double)pGeom[3] - (double)pGeom[1]))),
                        pScreen);
                }
                double yEntry = (double)(float)(xIndent + dChildX);
                float fRowH = match_option_row_draw((int)FLOAT_8061aa28, yEntry, yCursor, pScreen,
                                                    0, (int)pMode, (int *)pSquad,
                                                    (byte *)(pSquad + 8), 1, 0, 1,
                                                    *(int *)(&DAT_80481670 + (int)pMode * 0x24),
                                                    (void *)match_editor_begin,
                                                    (void *)match_confirm_dialog_list);
                double yBottom = (double)(float)((double)FLOAT_8061aa40 * (double)fRowH + yCursor);
                match_draw_clip_vline(xCol, (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4),
                                      yBottom, uLineColor);
                yCursor = (double)(float)(yCursor + (double)(float)((double)fRowH + dRowStep));
                *(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4) = (float)yBottom;

                if (match_tag_entry_has_pending((int *)pSquad) == 0) {
                    match_column_marker_draw((int)xCol,
                                             (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4),
                                             yEntry, pScreen, 0, 0, (void *)0, uLineColor);
                } else {
                    match_column_marker_draw((int)xCol,
                                             (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4),
                                             yBottom, pScreen, 0, 0, (void *)0, uLineColor);
                    int nMarkKind = (*(char *)(pSquad + 0x18) != '\0') ? 2 : 1;
                    match_column_marker_draw((int)xIndent,
                                             (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4),
                                             yEntry, pScreen, nMarkKind, pSquad, (void *)0,
                                             uLineColor);
                    if (*(char *)(pSquad + 0x18) != '\0') {
                        DAT_80612ce0 = DAT_80612ce0 + 1;
                        *(int *)(&DAT_80612cd8 + DAT_80612ce0 * 4) = (&DAT_80612cd4)[DAT_80612ce0];
                        int iChild = 0;
                        for (int c = 0; c < piBlock[0x14]; c = c + 1) {
                            int nSel = (int)(uint)*(byte *)((int)piBlock + c + 0x7c);
                            int nChildColor = *(int *)(&DAT_80481674 + (int)pMode * 0x24);
                            int pChild = DAT_80612be4 +
                                         *(int *)((int)piBlock + iChild + 0x54) * 0x2d * 4 +
                                         0x206a * 4;
                            double h = match_ladder_column_draw(xIndent, yCursor, dChildIndent,
                                                                pScreen, (int)pMode, (int *)pChild,
                                                                uLineColor, nSel, nChildColor);
                            iChild = iChild + 4;
                            yCursor = (double)(float)(yCursor + (double)(float)(h + dRowStep));
                        }
                        int iAlt = 0;
                        for (int a = 0; a < piBlock[0x22]; a = a + 1) {
                            int nAltColor = *(int *)(&DAT_80481678 + (int)pMode * 0x24);
                            int pAlt = DAT_80612be4 +
                                       *(int *)((int)piBlock + iAlt + 0x8c) * 0x2d * 4 + 0x206a * 4;
                            double h = match_ladder_column_draw_active(xIndent, yCursor,
                                                                       dChildIndent, pScreen,
                                                                       (int)pMode, (int *)pAlt,
                                                                       uLineColor, nAltColor);
                            iAlt = iAlt + 4;
                            yCursor = (double)(float)(yCursor + (double)(float)(h + dRowStep));
                        }
                        match_column_marker_draw_default(pScreen);
                        DAT_80612ce0 = DAT_80612ce0 + -1;
                    }
                }
            }
            iBlock = iBlock + 0x220;
        }

        // 4 capability categories
        int iCat = (int)pMode * 0x60;
        int iCatColor = 0;
        int iCatOff = 0;
        for (int cat = 0; cat < 4; cat = cat + 1) {
            int *pCap = (int *)((int)DAT_80612be4 + iCat + iCatOff + 0x113c4 * 4);
            if (*(int *)((int)DAT_80612be4 + iCat + iCatOff + 0x113c8 * 4) != 0) {
                int nEnabled = 0;
                if ((match_squad_has_cap_pair(cat, (int)pMode) != 0) ||
                    (*(int *)((int)DAT_80612be4 + iCat + iCatOff + 0x113cc * 4) != 0) ||
                    (*(int *)((int)DAT_80612be4 + iCat + iCatOff + 0x113d0 * 4) != 0)) {
                    nEnabled = 1;
                }
                byte *pszCat = (byte *)get_string_by_id(*(int *)((int)&DAT_80481740 + iCatColor));
                double yCat = (double)(float)(yCursor + dChildX);
                double h = (double)match_option_row_draw(
                    (int)FLOAT_8061aa28, yCat, yCursor, pScreen,
                    *(int *)((int)&DAT_8048174c + iCatColor), (int)pMode, pCap, pszCat, 1, 0, 0,
                    *(int *)(&DAT_80481680 + (int)pMode * 0x24), (void *)FUN_8033277c,
                    (void *)match_confirm_dialog_grid);
                double yBottom = (double)(float)((double)FLOAT_8061aa40 * h + yCursor);
                match_draw_clip_vline(xCol, (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4),
                                      yBottom, uLineColor);
                *(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4) = (float)yBottom;
                match_column_marker_draw((int)(float)(yCursor - dChildX),
                                         (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4), yCat,
                                         pScreen, 0, 0, (void *)0, uLineColor);
                yCursor = (double)(float)(yCursor + (double)(float)(h + dRowStep));

                if (nEnabled != 0) {
                    match_column_marker_draw((int)yCursor,
                                             (double)*(float *)(&DAT_80612cd8 + DAT_80612ce0 * 4),
                                             yCat, pScreen, 1, (int)pCap, (void *)&LAB_8032b1f4,
                                             uLineColor);
                    if (*pCap != 0) {
                        DAT_80612ce0 = DAT_80612ce0 + 1;
                        *(int *)(&DAT_80612cd8 + DAT_80612ce0 * 4) = (&DAT_80612cd4)[DAT_80612ce0];
                        int iSq = 0;
                        for (int s = 0; s < *(int *)(DAT_80612be4 + 0x2069 * 4); s = s + 1) {
                            int *piSq = (int *)((int)DAT_80612be4 + iSq + 0x81a8);
                            if ((((uint)*(byte *)((int)piSq + 0xb0) &
                                  (1 << *(int *)((int)&DAT_80481730 + iCatColor))) != 0) &&
                                (((uint)*(byte *)((int)piSq + 0xb0) & (1 << (int)pMode)) != 0)) {
                                int nColColor = *(int *)(&DAT_8048167c + (int)pMode * 0x24);
                                int nSel = (piSq[7] != 0);
                                double h2 = match_ladder_column_draw(yCursor, yCursor, dChildIndent,
                                                                     pScreen, (int)pMode, piSq,
                                                                     uLineColor, nSel, nColColor);
                                yCursor = (double)(float)(yCursor + (double)(float)(h2 + dRowStep));
                            }
                            iSq = iSq + 0xb4;
                        }
                        if (*(int *)((int)DAT_80612be4 + iCat + iCatOff + 0x113cc * 4) != 0) {
                            byte *psz = (byte *)get_string_by_id(0xaee);
                            double h2 = match_ladder_extra_row_draw(yCursor, yCursor, dChildIndent,
                                                                    pScreen, (int)pMode, psz,
                                                                    uLineColor, 0x10d);
                            yCursor = (double)(float)(yCursor + (double)(float)(h2 + dRowStep));
                        }
                        if (*(int *)((int)DAT_80612be4 + iCat + iCatOff + 0x113d0 * 4) != 0) {
                            byte *psz = (byte *)get_string_by_id(0xaef);
                            double h2 = match_ladder_extra_row_draw(yCursor, yCursor, dChildIndent,
                                                                    pScreen, (int)pMode, psz,
                                                                    uLineColor, 0x10e);
                            yCursor = (double)(float)(yCursor + (double)(float)(h2 + dRowStep));
                        }
                        match_column_marker_draw_default(pScreen);
                        DAT_80612ce0 = DAT_80612ce0 + -1;
                    }
                }
            }
            iCatOff = iCatOff + 0x18;
            iCatColor = iCatColor + 0x20;
        }
    } else if (nView == 1) {
        int nCount = *(int *)(DAT_80612be4 + 0x2069 * 4);
        int iOff = (nCount + -1) * 0xb4;
        for (int idx = nCount + -1; -1 < idx; idx = idx + -1) {
            int *piSq = (int *)((int)DAT_80612be4 + iOff + 0x81a8);
            if ((*piSq == 1) && (match_squad_entry_flag_clear((int)piSq) != 0)) {
                int nColor = *(int *)(&DAT_80481674 + (int)pMode * 0x24);
                double h = match_ladder_column_draw(xCol, yCursor, dChildIndent, pScreen,
                                                    (int)pMode, piSq, uLineColor, -1, nColor);
                yCursor = (double)(float)(yCursor + (double)(float)(h + dRowStep));
            }
            iOff = iOff + -0xb4;
        }
    } else if (nView == 2) {
        int nCount = *(int *)(DAT_80612be4 + 0x2069 * 4);
        int iOff = (nCount + -1) * 0xb4;
        for (int idx = nCount + -1; -1 < idx; idx = idx + -1) {
            int *piSq = (int *)((int)DAT_80612be4 + iOff + 0x81a8);
            if ((*piSq == 2) && (match_squad_entry_flag_clear((int)piSq) != 0)) {
                int nColor = *(int *)(&DAT_80481678 + (int)pMode * 0x24);
                double h = match_ladder_column_draw_active(xCol, yCursor, dChildIndent, pScreen,
                                                           (int)pMode, piSq, uLineColor, nColor);
                yCursor = (double)(float)(yCursor + (double)(float)(h + dRowStep));
            }
            iOff = iOff + -0xb4;
        }
    }

    match_column_marker_draw_default(pScreen);
    if (DAT_80612ccc != 0) {
        match_clone_buffer_alloc((uint)(DAT_80612ccc + 0x20));
        match_delete_entry();
        DAT_80612ccc = 0;
        hud_screen_mark_hidden(pScreen);
        if ((*(uint *)(DAT_80612be4 + 0x8e * 4) & 0x20) == 0) {
            FUN_80348238(0x15);
            *(uint *)(DAT_80612be4 + 0x8e * 4) = *(uint *)(DAT_80612be4 + 0x8e * 4) | 0x20;
        }
    }
    FUN_8038270c();
    return (double)(float)(yCursor - (double)pGeom[1]);   // ROM returns measured content height in f1
}
// ============================================================================
// Address: 0x803303a4 | match_mode_slots_all_positive
// Predicate: TRUE when both slot-count words of the mode's DAT_80481508 row
// (offsets +0 and +4) are >= 1. Used to decide whether to draw per-slot rows.
// ============================================================================
int match_mode_slots_all_positive(int nMode) {
    int iOff = 0;
    int n = 2;
    do {
        if (*(int *)(&DAT_80481508 + nMode * 0x34 + iOff) < 1) {
            return 0;
        }
        iOff = iOff + 4;
        n = n + -1;
    } while (n != 0);
    return 1;
}
// ============================================================================
// Address: 0x803303e8 | match_editor_content_draw
// Draw the editor content: if the current mode's slots aren't all populated
// (match_mode_slots_all_positive == 0) draw a single tree pass; otherwise draw
// one pass per slot (0..1), advancing the geometry rect (pGeom[1]/[3]) by each
// pass's height. nScreen arrives via saved r28.
// ============================================================================
void match_editor_content_draw(int nUnused1, int nScreen, float *pGeom, int nUnusedB, int nUnusedC,
                               int nArgA, int nArgB, int nArgC) {
    (void)nUnused1; (void)nUnusedB; (void)nUnusedC;
    if (match_mode_slots_all_positive(*(int *)(DAT_80612be4 + 0x244)) == 0) {
        match_editor_tree_draw(nScreen, nScreen, pGeom, (int *)1, (int *)0, nArgA, nArgB, nArgC);
    } else {
        int nSlot = 0;
        do {
            double h = 0.0;
            match_editor_tree_draw(nScreen, nScreen, pGeom, (int *)1, (int *)nSlot, nArgA, nArgB,
                                   nArgC);
            nSlot = nSlot + 1;
            pGeom[1] = (float)((double)pGeom[1] + h);
            pGeom[3] = (float)((double)pGeom[3] + h);
        } while (nSlot < 2);
    }
}
// ============================================================================
// Address: 0x80330490 | match_editor_setup_softkeys
// Register the editor screen's two softkeys: slot 1 = "Back" (0x19c8), slot 0 =
// "Accept" (0x19c7), the latter enabled/greyed by the sign bit of the screen's
// element flags (+0x2c -> +0x188).
// ============================================================================
void match_editor_setup_softkeys(int nScreen) {
    hud_screen_clear_all_handlers(nScreen);
    int nBack = get_string_by_id(0x19c8);
    hud_screen_register_handler(nScreen, 1, nBack, 0, 0);
    uint uFlags = *(uint *)(*(int *)(nScreen + 0x2c) + 0x188);
    int nAccept = get_string_by_id(0x19c7);
    hud_screen_register_handler(nScreen, 0, nAccept, uFlags >> 0x1f, 0);
}
// ============================================================================
// Address: 0x80330510 | match_editor_scroll_frame
// Build the editor's scrollable content frame. Under the local-id lock, measure
// the content by drawing it once (match_editor_tree_draw for the descriptor
// vtable A, or match_ladder_draw for the ladder vtable B), scale by
// FLOAT_80611a5c, then create the scroll list element with the appropriate
// custom draw callback (match_editor_content_draw or match_ladder_draw_active).
// ============================================================================
void match_editor_scroll_frame(void) {
    int pScreen = FUN_803826f0();
    int nStyle = hud_style_push_element(1);
    int nVariant = hud_style_apply_variant(0);
    double dHeight = (double)match_editor_tree_draw(pScreen, 0, (float *)&DAT_804817d8, (int *)0,
                                                    (int *)0, 0, 0, 0);
    if (*(void **)(pScreen + 4) == &PTR_FUN_804817e8) {
        if (match_mode_slots_all_positive(*(int *)(DAT_80612be4 + 0x244)) != 0) {
            double dExtra = (double)match_editor_tree_draw(pScreen, 0, (float *)&DAT_804817d8,
                                                           (int *)0, (int *)1, 0, 0, 0);
            dHeight = (double)(float)(dHeight + dExtra);
        }
    } else if (*(void **)(pScreen + 4) == &PTR_FUN_804817f8) {
        dHeight = (double)match_ladder_draw(pScreen, 0, (float *)&DAT_804817d8, 0);
    }
    dHeight = (double)(float)(dHeight * (double)FLOAT_80611a5c);
    hud_element_init_from_layout(0, 0, 0, (int (*)(void))screen_exit_clear_mode);
    hud_element_scroll_h_overflow((double)FLOAT_8061aaa8, (double)FLOAT_8061aa2c,
                                  (double)FLOAT_8061aa2c, 0);
    hud_set_item_padding_mode((double)FLOAT_8061aa28, 0, 1, 1);
    if (*(void **)(pScreen + 4) == &PTR_FUN_804817e8) {
        hud_add_spacer_item(dHeight, 0, (void *)match_editor_content_draw);
    } else if (*(void **)(pScreen + 4) == &PTR_FUN_804817f8) {
        hud_add_spacer_item(dHeight, 0, (void *)match_ladder_draw_active);
    }
    hud_element_apply_scroll();
    hud_element_finalize_frame();
    hud_style_push_element(nStyle);
    hud_style_apply_variant(nVariant);
    FUN_8038273c();
}
// ============================================================================
// Address: 0x803306bc | match_editor_set_mode_and_hide
// Commit the chosen mode (screen element +0x188) into g_MatchSession +0x244 and
// hide the mode-select screen.
// ============================================================================
void match_editor_set_mode_and_hide(int nScreen) {
    *(int *)(DAT_80612be4 + 0x244) = *(int *)(nScreen + 0x188);
    hud_screen_mark_hidden(nScreen);
}

// ---- additional per-file externs for batch_939 ----------------------------
// Editor row-highlight helpers, the per-squad UI-child dispatcher, the add-slot
// action, and the mode-select screen builder.
extern int   DAT_804815bc;         // default menu-row color word A (unselected rows)
extern int   DAT_804815c0;         // default menu-row color word B (unselected rows)
extern int   DAT_804814f4;         // mode-select entry string-id table (stride 0x34 words)
extern float FLOAT_8061aaac;       // row highlight scale: not-hovered
extern int   FUN_803826e4(void);   // -> current screen element handle (no lock)
extern void  FUN_80382730(void);   // release that screen element handle
extern void  gx_draw_gradient_rect(double x0, double y0, double x1, double y1);
extern int   hud_find_child_by_id(int pScreen, int nId);   // -> child element for tag id, or 0
extern void  LAB_80335c28(int pCtx, int pSquad); // per-squad edit-apply callback (mid-fn label)
extern int   hud_element_has_unsaved_changes(int pElem);
extern void  hud_screen_rebuild_items(void);
extern void  hud_item_add_text_display(double pad, int a);
extern void  hud_item_set_value_display(int pValue);
extern void  hud_add_padding_item(double pad);
extern int   hud_element_create(int pParent, void *pVTable, int nKind); // -> new child element
extern void  hud_element_set_pos_xy(double x, double y, int pElem);
extern void  hud_element_link_and_show(int pParent, int pChild);
// match_kind color tables DAT_80481520/24, match_scan_squads_apply,
// match_cursor_twinkle_draw, match_add_slot_if_room, match_add_show_over_budget,
// match_editor_reset_flags, color_scale_rgb/_lo, hud_* list builders,
// PTR_FUN_804817e8, DAT_80612be4/d00, FUN_803826e8/f0, FUN_80382734/8273c,
// FLOAT_8061aa** and DAT_806119e0 are declared earlier in this file.
// ============================================================================
// Address: 0x803306e8 | match_kind_row_colors
// Fetch the per-kind row color pair (A -> DAT_80481520, B -> DAT_80481524,
// stride 0x34) into pOut[0..1].
// ============================================================================
void match_kind_row_colors(int nKind, int *pOut) {
    pOut[0] = *(int *)(&DAT_80481520 + nKind * 0x34);
    pOut[1] = *(int *)(&DAT_80481524 + nKind * 0x34);
}
// ============================================================================
// Address: 0x8033070c | match_menu_row_highlight_draw
// Draw a two-color gradient highlight rect for a menu row, brightening it on
// cursor hover (FLOAT_8061aa3c vs FLOAT_8061aaac) and modulating by the screen
// element's fade (+0xf0). When bActive and the row is the applied one
// (+0x1b4 == FLOAT_8061aa90) and a squad applies (match_scan_squads_apply),
// overlay the twinkle cursor. Runs under the screen-element handle.
// ============================================================================
void match_menu_row_highlight_draw(int nUnused1, int nUnused2, float *pRect, uint *pColors,
                                   int bActive) {
    (void)nUnused1; (void)nUnused2;
    int pElem = FUN_803826e4();
    float fScale = (hud_cursor_hit_test_rect(pRect) != 0) ? FLOAT_8061aa3c : FLOAT_8061aaac;
    double dScale = (double)fScale;
    uint uColA = color_scale_rgb(dScale, pColors[0]);
    uint uColB = color_scale_rgb(dScale, pColors[1]);
    color_scale_lo((double)*(float *)(pElem + 0xf0), uColA);
    color_scale_lo((double)*(float *)(pElem + 0xf0), uColB);
    gx_draw_gradient_rect((double)pRect[0], (double)pRect[1], (double)pRect[2], (double)pRect[3]);
    if ((bActive != 0) && (FLOAT_8061aa90 == *(float *)(pElem + 0x1b4)) &&
        (match_scan_squads_apply() != 0)) {
        match_cursor_twinkle_draw((double)(pRect[2] - FLOAT_8061aa88),
                                  (double)(pRect[1] - FLOAT_8061aa8c), (double)FLOAT_8061aa90);
    }
    FUN_80382730();
}
// ============================================================================
// Address: 0x8033083c | match_squad_row_highlight_draw
// Squad-row highlight: resolve the row's kind (current squad +0x14 via the
// local-id lock) to its color pair (match_kind_row_colors), then draw the active
// highlight (match_menu_row_highlight_draw with bActive=1).
// ============================================================================
void match_squad_row_highlight_draw(int nUnused1, int nUnused2, float *pRect) {
    (void)nUnused1; (void)nUnused2;
    long long llPair = (long long)FUN_803826f0();
    int pSquad = (int)(unsigned long long)((unsigned long long)llPair >> 32);
    int nSquadLo = (int)llPair;
    uint aColors[6];
    match_kind_row_colors(*(int *)(nSquadLo + 0x14), (int *)aColors);
    match_menu_row_highlight_draw(pSquad, nSquadLo, pRect, aColors, 1);
    FUN_8038273c();
}
// ============================================================================
// Address: 0x80330898 | match_menu_row_highlight_default
// Draw a menu-row highlight with the default (unselected) color pair
// (DAT_804815bc / DAT_804815c0) and bActive=0.
// ============================================================================
void match_menu_row_highlight_default(int nParam1, int nParam2, float *pRect) {
    uint aColors[2];
    aColors[0] = (uint)DAT_804815bc;
    aColors[1] = (uint)DAT_804815c0;
    match_menu_row_highlight_draw(nParam1, nParam2, pRect, aColors, 0);
}
// ============================================================================
// Address: 0x803308d8 | match_mode_select_build
// Build the arcade custom-match mode-select screen: create the list element,
// add the 3 mode entries (DAT_804814f4 strings, tags 0xc3..0xc5) plus the "back"
// entry (0xae4, tag 0xc6), position the frame, then create and link the child
// scroll-frame element bound to the descriptor-tree vtable
// (PTR_FUN_804817e8 / match_editor_scroll_frame). pScreen via saved r25.
// ============================================================================
void match_mode_select_build(int pScreen) {
    hud_element_init_from_layout(0x408410, 0, 0, (int (*)(void))screen_exit_clear_mode);
    hud_set_item_padding_mode((double)FLOAT_8061aa28, 0, 1, 1);
    hud_set_context_property(0x30000);
    int iTable = 0;
    for (int i = 0; i < 3; i = i + 1) {
        get_string_by_id(*(int *)((int)&DAT_804814f4 + iTable));
        hud_add_list_item();
        hud_set_last_child_tag(i + 0xc3);
        hud_add_padding_item((double)FLOAT_8061aa3c);
        iTable = iTable + 0x34;
    }
    get_string_by_id(0xae4);
    hud_add_list_item();
    hud_set_last_child_tag(0xc6);
    hud_add_padding_item((double)FLOAT_8061aa3c);
    hud_element_finalize_frame();
    double yFrame = (double)(FLOAT_80611a5c *
                             ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                               FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c));
    hud_element_set_pos_y(yFrame, DAT_806119e0);
    *(int *)(pScreen + 0x214) = 2;
    int pChild = hud_element_create(pScreen, &PTR_FUN_804817e8, 2);
    hud_element_set_pos_xy((double)FLOAT_8061aa38, (double)(float)(yFrame + (double)FLOAT_8061aa2c),
                           pChild);
    hud_element_link_and_show(pScreen, pChild);
}
// ============================================================================
// Address: 0x80330a80 | match_foreach_squad_child
// Under the local-id lock, iterate the squad array (count +0x81a4, stride 0xb4).
// For each squad, look up its UI child element by tag (index+2); when the child
// exists and either it carries a bound squad (+200) or a local id is active,
// invoke pfnCallback(pTarget, pSquad) where pTarget is the child's bound squad
// (local player) or the current selection (DAT_80612d00).
// ============================================================================
void match_foreach_squad_child(int nUnused1, int nLocalActive, void *pfnCallback) {
    (void)nUnused1;
    long long llPair = (long long)FUN_803826e8();
    int pScreen = (int)(unsigned long long)((unsigned long long)llPair >> 32);
    int nLocalId = (int)llPair;
    (void)nLocalActive;
    int iOff = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int pChild = hud_find_child_by_id(pScreen, i + 2);
        if ((pChild != 0) && ((*(int *)(pChild + 200) != 0) || (nLocalId != 0))) {
            int pTarget = DAT_80612d00;
            if (nLocalId != 0) {
                pTarget = *(int *)(pChild + 200);
            }
            ((void (*)(int, int))pfnCallback)(pTarget, DAT_80612be4 + iOff + 0x81a8);
        }
        iOff = iOff + 0xb4;
    }
    FUN_80382734();
}
// ============================================================================
// Address: 0x80330b34 | match_apply_all_squad_edits
// Run match_foreach_squad_child with the per-squad edit-apply callback
// (LAB_80335c28) for the local player (nLocalActive = 1).
// ============================================================================
void match_apply_all_squad_edits(int nScreen) {
    match_foreach_squad_child(nScreen, 1, (void *)&LAB_80335c28);
}
// ============================================================================
// Address: 0x80330b60 | match_commit_if_unsaved
// If the editor element has unsaved changes, apply all squad edits and rebuild
// the screen list items.
// ============================================================================
void match_commit_if_unsaved(int pElem) {
    if (hud_element_has_unsaved_changes(pElem) != 0) {
        match_apply_all_squad_edits(pElem);
        hud_screen_rebuild_items();
    }
}
// ============================================================================
// Address: 0x80330bc8 | match_add_slot_action
// Add-slot softkey action: try to add a squad slot (match_add_slot_if_room(1)).
// On success set the "dirty" flag (+0x11484), reset the editor flags, and push
// the editor screen; on failure raise the over-budget dialog. The raw 16-arg
// prototype is a register-spill misread of the dialog varargs.
// ============================================================================
void match_add_slot_action(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5, u64 r6, u64 r7, u64 r8) {
    if (match_add_slot_if_room(1) == 0) {
        match_add_show_over_budget(r1, r2, r3, r4, r5, r6, r7, r8, 1, 0, 0, 0, 0, 0, 0, 0);
    } else {
        *(int *)(DAT_80612be4 + 0x11484) = 1;
        match_editor_reset_flags(1);
        hud_screen_push();
    }
}
// ============================================================================
// Address: 0x80330c38 | match_add_value_list_item
// Add a list item with a text display and a value display, tagging it with the
// given child tag.
// ============================================================================
void match_add_value_list_item(int nUnused1, int nValue, int nTag) {
    (void)nUnused1;
    hud_add_list_item();
    hud_item_add_text_display((double)FLOAT_8061aa28, 0);
    hud_set_last_child_tag(nTag);
    hud_item_set_value_display(nValue);
}

// ===========================================================================
// batch_940 additions: arcade custom-match "add squad" / squad-list editor
// screen builders and apply/confirm handlers (0x80330ca0 - 0x803315c4).
// New per-file externs for raw symbols first referenced by this batch.
// ===========================================================================
extern int  DAT_80612d04;                 // active descriptor index (session scratch)
extern int  DAT_804816ac;                 // per-tab config: allow-clear flag (&DAT_804816a8+4, stride 0x44)
extern int  DAT_804816d4;                 // per-tab config: confirm-dialog title id (stride 0x44)
extern int  DAT_804816d8;                 // per-tab config: confirm-dialog body string id (stride 0x44)
extern int  FUN_803826d8(void);           // lock local player id, return screen frame ptr
extern void FUN_80382724(void);           // release the FUN_803826d8 lock
extern int  FUN_8033245c(int, int, int);  // per-row apply predicate (add-squad list)
extern int  FUN_80332494(int, int);       // per-row apply predicate (squad list)
extern int  FUN_80332020(u64, u64, u64, u64, u64, u64, u64, u64); // commit selected squad edits -> 0 on success
extern int  FUN_8033210c(int, int);       // classify unsaved edit -> confirm-dialog kind
extern void FUN_80348238(int);            // raise an unsaved-changes prompt (kind 0x17)
extern void FUN_80335c6c(int, int);       // per-squad edit-apply callback
extern int  FUN_80332560(int);            // squad-entry visibility predicate -> bool
extern int  LAB_80330ba8;                 // add-squad per-row value-change trampoline
extern int  LAB_80331548;                 // squad-list per-row value-change trampoline
// hud helpers first used by this batch (others already declared above)
extern void hud_item_add_header(double pad, int nFlags);
extern void hud_screen_register_handler_global(int nEvent, int nStrId, int a, int b);
extern void hud_begin_scroll_with_item(void);
extern void hud_end_scroll_region(void);
extern void hud_event_add_string(int pStr);
extern int  hud_get_last_created_item(void);
extern void hud_item_set_locked(int pItem, int nLocked);
extern void hud_item_on_value_change(void);
// forward decl: defined later in this file (batch_940)
int match_add_squad_rows(int pScreen, int pfnPredicate, void *pfnRow);
// ===========================================================================
// Address: 0x80330ca0 | match_add_value_row
// Add a labeled option row carrying a header, two string columns and a value
// display, bound to the given tag. When nWrapScroll is set the row is emitted
// inside its own scroll region (begin-scroll / end-scroll); otherwise it is a
// plain list item. The leading text column is taken from r4 (extraout_r4 in the
// raw decomp, the caller's first display string).
// ===========================================================================
void match_add_value_row(int pSession, int pStrCol1, int pStrCol2, int pValue,
                         int nWrapScroll, int nTag) {
    (void)pSession;
    int pLeadStr = FUN_803826d8();      // r4 leading column string (locked fetch)
    if (nWrapScroll == 0) {
        hud_add_list_item();
    } else {
        hud_begin_scroll_with_item();
    }
    hud_item_add_header((double)FLOAT_8061aa28, 0x900);
    hud_set_last_child_tag(nTag);
    hud_event_add_string(pLeadStr);
    hud_event_add_string(pStrCol2);
    hud_item_set_value_display(pValue);
    if (nWrapScroll != 0) {
        hud_end_scroll_region();
    }
    FUN_80382724();
}
// ===========================================================================
// Address: 0x80330d50 | match_build_add_squad_screen
// Build the arcade "add squad" editor screen for the active descriptor
// (DAT_80612d04). Emits the descriptor title/help header via the formatted
// list-screen helper (FUN_80387fb8), positions the scroll frame, then adds the
// callback rows: two slot-budget value rows (fields +8/+0xc of the mode block),
// one add-value row (+0x10), and the accept/back rows. The accept and back
// rows are locked based on how many budget fields are non-zero (uCount).
// pScreen arrives in the prologue-saved r28.
// ===========================================================================
void match_build_add_squad_screen(int pScreen) {
    int pSession = DAT_80612be4;
    int nTab = *(int *)(DAT_80612be4 + 0x248);
    int nDesc = DAT_80612d04 - 2;
    *(int *)(DAT_80612be4 + 0x11484) = 0;
    int pModeBlock = pSession + nTab * 0x60 + nDesc * 0x18 + 0x113c4;

    int pTitle = get_string_by_id(match_desc_field1(DAT_80612d04));
    char *pHelp = (char *)get_string_by_id(0xaed);
    FUN_80387fb8(0, 0, 0, 0, 0, 0, 0, 0, DAT_80612be4, pHelp, pTitle, pSession, 0, 0, 0, 0);

    int pName = get_string_by_id(match_desc_field4(DAT_80612d04));
    hud_element_init_from_layout(0x8400, pName, 0, (int (*)(void))FUN_8033224c);
    hud_set_item_padding_mode((double)FLOAT_8061aa14, 1, 1, 1);
    hud_set_context_property(0x28000);
    double xOverflow = -(double)(FLOAT_80611a5c *
                                 ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                                   FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                                 FLOAT_8061aa18);
    hud_element_scroll_h_overflow(xOverflow, (double)FLOAT_8061aa28, (double)FLOAT_8061aa28, 0);

    get_string_by_id(0xae9);
    hud_add_list_item();
    hud_set_last_child_tag(0x10c);
    hud_add_callback_item((double)FLOAT_8061aa14);

    int nBase = match_add_squad_rows(pScreen, (int)FUN_8033245c, (void *)&LAB_80330ba8);
    int pOpt = get_string_by_id(0xaee);
    match_add_value_list_item(pOpt, pModeBlock + 8, 0x10d);
    pOpt = get_string_by_id(0xaef);
    match_add_value_list_item(pOpt, pModeBlock + 0xc, 0x10e);

    hud_add_callback_item((double)FLOAT_8061aa14);
    int pStrA = get_string_by_id(0xaf0);
    int pStrB = get_string_by_id(0xaf1);
    match_add_value_row(DAT_80612be4, pStrB, pStrA, pModeBlock + 0x10, 0, 0x10f);

    unsigned int uCount = (unsigned int)nBase + (unsigned int)(*(int *)(pModeBlock + 8) != 0) +
                          (unsigned int)(*(int *)(pModeBlock + 0xc) != 0);
    int pItem = hud_get_last_created_item();
    hud_item_set_locked(pItem, (int)((unsigned int)(uCount < 2) - ((int)uCount >> 0x1f)));

    get_string_by_id(0xb03);
    hud_add_list_item();
    pItem = hud_get_last_created_item();
    hud_item_set_locked(pItem, (int)(countLeadingZeros(uCount) >> 5));

    hud_element_apply_scroll();
    double yFrame = (double)(FLOAT_80611a5c *
                             ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                               FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c));
    hud_element_set_pos_y(yFrame, DAT_806119e0);
    hud_element_finalize_frame();
    *(unsigned int *)(pScreen + 0x214) = *(unsigned int *)(pScreen + 0x214) | 4;
}
// ===========================================================================
// Address: 0x80330fd4 | match_reset_team_and_apply
// Clear the current team block's edit-scratch field (+0x4234, indexed by the
// selection's team index +0xa4) then re-run the per-squad edit-apply callback
// (FUN_80335c6c) over every squad child.
// ===========================================================================
void match_reset_team_and_apply(int pScreen) {
    *(int *)(DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220 + 0x4234) = 0;
    match_foreach_squad_child(pScreen, 0, (void *)FUN_80335c6c);
}
// ===========================================================================
// Address: 0x8033101c | match_try_commit_edits
// Attempt to commit the currently-selected squad edits (FUN_80332020). On
// success (returns 0) reset the team scratch, re-apply, and raise the three
// editor dirty flags. Returns TRUE when the commit succeeded (caller pushes).
// ===========================================================================
bool match_try_commit_edits(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8,
                            int pScreen) {
    int nResult = FUN_80332020(p1, p2, p3, p4, p5, p6, p7, p8);
    if (nResult == 0) {
        match_reset_team_and_apply(pScreen);
        DAT_80612d0c = 1;
        DAT_80612cf8 = 1;
        DAT_80612cf4 = 1;
    }
    return nResult == 0;
}
// ===========================================================================
// Address: 0x80331078 | match_confirm_and_commit
// Editor accept handler: if the element has unsaved changes and the "prompted"
// flag (+0x238 bit 0x20) is not yet set, raise the unsaved-changes prompt and
// set the flag. Then try to commit; on success push the next screen.
// ===========================================================================
void match_confirm_and_commit(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8,
                              int pScreen) {
    int nDirty = hud_element_has_unsaved_changes(pScreen);
    if ((nDirty != 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0x17);  // raw reused r3 as p1; result unused by the commit path
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    bool bCommitted = match_try_commit_edits(p1, p2, p3, p4, p5, p6, p7, p8, pScreen);
    if (bCommitted) {
        hud_screen_push();
    }
}
// ===========================================================================
// Address: 0x803310f8 | match_back_reset_and_push
// Editor back handler: raise the unsaved-changes prompt (once), reset the team
// scratch and re-apply, and when not in the "committing" mode (DAT_80612cfc==0)
// rebuild the tab's scroll frame before pushing the screen.
// ===========================================================================
void match_back_reset_and_push(int pScreen) {
    int nTab = *(int *)(DAT_80612be4 + 0x248);
    int nDirty = hud_element_has_unsaved_changes(pScreen);
    if ((nDirty != 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0x17);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    match_reset_team_and_apply(pScreen);
    if (DAT_80612cfc == 0) {
        FUN_803356e4((int)(&DAT_804816a8 + nTab * 0x44));
    }
    hud_screen_push();
}
// ===========================================================================
// Address: 0x803311a8 | match_back_reset_and_push_alt
// Variant of match_back_reset_and_push used as the squad-list screen's back
// callback: identical reset/re-apply, but the scroll-frame rebuild and the push
// are on the same DAT_80612cfc==0 branch (else path just pushes).
// ===========================================================================
void match_back_reset_and_push_alt(int pScreen) {
    int nDirty = hud_element_has_unsaved_changes(pScreen);
    if ((nDirty != 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0x17);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    match_reset_team_and_apply(pScreen);
    if (DAT_80612cfc == 0) {
        FUN_803356e4((int)(&DAT_804816a8 + *(int *)(DAT_80612be4 + 0x248) * 0x44));
        hud_screen_push();
    } else {
        hud_screen_push();
    }
}
// ===========================================================================
// Address: 0x80331258 | match_squad_row_action
// Per-row value-change action for a squad entry. When the row's element carries
// the "toggle" style (flag +0xc0 bit 0x800000), has a valid squad index
// (+0x188 >= 0) and clearing is allowed on this tab (DAT_804816ac != 0), flip
// the squad's enabled word (+0x1c of its 0xb4-stride block) and re-apply. Then,
// if there are unsaved changes, classify the edit (FUN_8033210c) and raise the
// appropriate confirm dialog: kind 1 uses the tab's title/body ids, kind 2 uses
// the shared "cannot" string 0xb5f, kind 0 just re-applies. Rebuilds items.
// ===========================================================================
void match_squad_row_action(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8,
                            int pElem) {
    int pSession = DAT_80612be4;
    if (((*(unsigned int *)(pElem + 0xc0) & 0x800000) != 0) && (-1 < *(int *)(pElem + 0x188)) &&
        (*(int *)(&DAT_804816ac + *(int *)(DAT_80612be4 + 0x248) * 0x44) != 0)) {
        int pSquad = DAT_80612be4 + (*(int *)(pElem + 0x188) + -2) * 0xb4 + 0x81a8;
        *(int *)(pSquad + 0x1c) = 1 - *(int *)(pSquad + 0x1c);
        match_reset_team_and_apply(pElem);
        hud_screen_rebuild_items();  // raw reused r3 as p1; result unused downstream
    }
    int nDirty = hud_element_has_unsaved_changes(pElem);
    if (nDirty != 0) {
        int nKind = FUN_8033210c(pElem, 1);
        if (nKind == 1) {
            int nCfg = *(int *)(DAT_80612be4 + 0x248) * 0x44;
            char *pBody = (char *)get_string_by_id(*(int *)(&DAT_804816d8 + nCfg));
            FUN_80387fb8(p1, p2, p3, p4, p5, p6, p7, p8, DAT_80612be4,
                         pBody, *(int *)(&DAT_804816d4 + nCfg), pSession, 0, 0, 0, 0);
            hud_create_back_dialog(pElem, DAT_80612be4, 0);
        } else if (nKind < 1) {
            if (-1 < nKind) {
                match_reset_team_and_apply(pElem);
            }
        } else if (nKind < 3) {
            char *pBody = (char *)get_string_by_id(0xb5f);
            FUN_80387fb8(p1, p2, p3, p4, p5, p6, p7, p8, DAT_80612be4,
                         pBody, 10, pSession, 0, 0, 0, 0);
            hud_create_back_dialog(pElem, DAT_80612be4, 0);
        }
        hud_screen_rebuild_items();
    }
}
// ===========================================================================
// Address: 0x803313bc | match_add_squad_rows
// Walk the squad array (count +0x81a4, stride 0xb4) and, for each active entry
// (state == 1) that passes the flag-clear check, the caller's per-row predicate
// (pfnPredicate, arrives in r4) and the visibility test (FUN_80332560), emit a
// text-display list row bound to tag 0xd9 and register its value-change handler
// (pfnRow). Rows whose "over budget" bit (+0xb0 bit 2) is set with a zero
// predicate result also nudge match_squad_can_add. Returns the number of rows.
// ===========================================================================
int match_add_squad_rows(int pScreen, int pfnPredicate, void *pfnRow) {
    (void)pScreen;
    FUN_803826d8();
    int nCount = 0;
    unsigned char cClearArg =
        ((unsigned char *)&DAT_804816e8)[*(int *)(DAT_80612be4 + 0x248) * 0x44];
    int iOff = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int *pEntry = (int *)(DAT_80612be4 + iOff + 0x81a8);
        if ((*pEntry == 1) && (match_squad_entry_flag_clear((int)pEntry) != 0) &&
            (((int (*)(int, int, int))pfnPredicate)(*pEntry, pEntry[1], cClearArg) != 0) &&
            (FUN_80332560((int)pEntry) != 0)) {
            int nRes = ((int (*)(int *, int))pfnRow)(pEntry, i);
            if (((*(unsigned char *)((char *)pEntry + 0xb0) & 2) != 0) && (nRes == 0)) {
                match_squad_can_add(pEntry, 1);
            }
            hud_add_list_item();
            hud_item_add_text_display((double)FLOAT_8061aa28, 0);
            hud_set_last_child_tag(0xd9);
            hud_get_last_created_item();
            hud_item_on_value_change();
            nCount = nCount + 1;
        }
        iOff = iOff + 0xb4;
    }
    FUN_80382724();
    return nCount;
}
// ===========================================================================
// Address: 0x803315c4 | match_build_squad_list_screen
// Build the arcade squad-list editor screen (title 0xd0d, back callback
// match_back_reset_and_push_alt). Adds the callback rows, then the squad rows
// via match_add_squad_rows (predicate FUN_80332494, row handler LAB_80331548),
// a team-name header row (tag 0xda, value display = team block +0x4230) locked
// by the row count, and an accept row (tag 0xdb, 0xb02/0xb03 depending on the
// committing mode) locked when there are zero rows. Positions the frame, and
// when clearing is allowed on this tab registers the global "clear" handler.
// pScreen arrives in the prologue-saved r28.
// ===========================================================================
void match_build_squad_list_screen(int pScreen) {
    int nTab = *(int *)(DAT_80612be4 + 0x248);
    int pTeam = DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220;

    int pTitle = get_string_by_id(0xd0d);
    hud_element_init_from_layout(0x8400, pTitle, 0, (int (*)(void))match_back_reset_and_push_alt);
    hud_set_item_padding_mode((double)FLOAT_8061aa14, 1, 1, 1);
    hud_set_context_property(0x28000);
    double xOverflow = -(double)(FLOAT_80611a5c *
                                 ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                                   FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                                 FLOAT_8061aa18);
    hud_element_scroll_h_overflow(xOverflow, (double)FLOAT_8061aa28, (double)FLOAT_8061aa28, 0);

    get_string_by_id(0xae9);
    hud_add_list_item();
    hud_set_last_child_tag(0xd8);
    hud_add_callback_item((double)FLOAT_8061aa14);

    unsigned int uRows = (unsigned int)match_add_squad_rows(pScreen, (int)FUN_80332494,
                                                            (void *)&LAB_80331548);

    hud_add_callback_item((double)FLOAT_8061aa14);
    hud_add_list_item();
    int pHdr = hud_get_last_created_item();
    double height = (double)FLOAT_8061aa28;
    *(int *)(pHdr + 0x1c) = 5;
    *(int *)(pHdr + 0xbc) = 5;
    hud_item_add_header(height, 0);
    hud_set_last_child_tag(0xda);
    int pStr = get_string_by_id(0xd10);
    hud_event_add_string(pStr);
    pStr = get_string_by_id(0xd11);
    hud_event_add_string(pStr);
    hud_item_set_value_display(pTeam + 0x4230);
    int pItem = hud_get_last_created_item();
    hud_item_set_locked(pItem, (int)((unsigned int)(uRows < 2) - ((int)uRows >> 0x1f)));

    int nAcceptStr = 0xb03;
    if (DAT_80612cfc != 0) {
        nAcceptStr = 0xb02;
    }
    get_string_by_id(nAcceptStr);
    hud_add_list_item();
    hud_set_last_child_tag(0xdb);
    pItem = hud_get_last_created_item();
    hud_item_set_locked(pItem, (int)(countLeadingZeros(uRows) >> 5));

    hud_element_apply_scroll();
    double yFrame = (double)(FLOAT_80611a5c *
                             ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                               FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c));
    hud_element_set_pos_y(yFrame, DAT_806119e0);
    if (*(int *)(&DAT_804816ac + nTab * 0x44) != 0) {
        int pClear = get_string_by_id(0xb0b);
        hud_screen_register_handler_global(3, pClear, 0, 0);
    }
    hud_element_finalize_frame();
    *(unsigned int *)(pScreen + 0x214) = *(unsigned int *)(pScreen + 0x214) | 4;
}

// ===========================================================================
// batch_941 additions: team-block variant of the arcade squad-list editor
// (0x80331834 - 0x80331fc4). These mirror the batch_940 session-wide handlers
// but operate on the per-team slot arrays at g_MatchSession + team*0x220 +
// 0x41e4 (index list at +0x41e4+0x8c, count at +0x41e4+0x88).
// New per-file externs first referenced by this batch.
// ===========================================================================
extern int  DAT_804816dc;                 // per-team-config confirm-dialog title id (stride 0x44)
extern int  DAT_804816e0;                 // per-team-config confirm-dialog body string id (stride 0x44)
extern int  FUN_80335d2c(int, int);       // bind a team-squad row's value display
extern void hud_lock_child_by_id(int pScreen, int nId, int nLocked); // lock/unlock a child element
// ===========================================================================
// Address: 0x80331834 | match_squad_slot_unlocked
// Predicate: TRUE when the squad's mode-config B word (DAT_80480f30, indexed by
// the squad's mode id +4, stride 0x4c) does NOT have the "locked" bit (bit 4)
// set. Returns (bit4 == 0).
// ===========================================================================
unsigned int match_squad_slot_unlocked(int pSquad) {
    return (*(unsigned int *)(&DAT_80480f30 + *(int *)(pSquad + 4) * 0x4c) >> 4 & 1) ^ 1;
}
// ===========================================================================
// Address: 0x80331858 | match_team_list_compact
// Compact the locked team's squad-index list (FUN_803826e4 -> pTeam; list at
// +0x8c, count at +0x88): keep only entries whose squad is still unlocked
// (match_squad_slot_unlocked == 0 means locked-out, so we keep those where it
// returns 0 -- matching the raw), rewriting the array in place and updating the
// count. Releases the lock (FUN_80382730) on exit.
// ===========================================================================
void match_team_list_compact(void) {
    int pTeam = FUN_803826e4();
    int nKept = 0;
    int iDst = 0;
    int iSrc = 0;
    for (int i = 0; i < *(int *)(pTeam + 0x88); i = i + 1) {
        unsigned int uLocked =
            match_squad_slot_unlocked(DAT_80612be4 + *(int *)(pTeam + iSrc + 0x8c) * 0xb4 + 0x81a8);
        if (uLocked == 0) {
            *(int *)(pTeam + iDst + 0x8c) = *(int *)(pTeam + iSrc + 0x8c);
            nKept = nKept + 1;
            iDst = iDst + 4;
        }
        iSrc = iSrc + 4;
    }
    *(int *)(pTeam + 0x88) = nKept;
    FUN_80382730();
}
// ===========================================================================
// Address: 0x803318f0 | match_team_reset_and_apply
// Team-block variant of match_reset_team_and_apply: fetch the screen
// (FUN_803826f0), compact the team squad list, then re-run the per-squad
// edit-apply callback (FUN_80335c6c) for every bound squad child (id + 1).
// ===========================================================================
void match_team_reset_and_apply(void) {
    int pScreen = FUN_803826f0();
    match_team_list_compact();
    int iOff = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int pChild = hud_find_child_by_id(pScreen, i + 1);
        if ((pChild != 0) && (*(int *)(pChild + 200) != 0)) {
            FUN_80335c6c(DAT_80612d00, DAT_80612be4 + iOff + 0x81a8);
        }
        iOff = iOff + 0xb4;
    }
    FUN_8038273c();
}
// ===========================================================================
// Address: 0x8033199c | match_team_confirm_and_commit
// Team-block accept handler: commit the selected edits (FUN_80332020); on
// success raise the unsaved-changes prompt once, re-apply the team list, set the
// editor dirty flags (mode 2), and push the screen.
// ===========================================================================
void match_team_confirm_and_commit(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8,
                                   int pScreen) {
    int nResult = FUN_80332020(p1, p2, p3, p4, p5, p6, p7, p8);
    if (nResult == 0) {
        int nDirty = hud_element_has_unsaved_changes(pScreen);
        if ((nDirty != 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
            FUN_80348238(0x17);
            *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
        }
        match_team_reset_and_apply();
        DAT_80612d0c = 2;
        DAT_80612cf8 = 1;
        DAT_80612cf4 = 1;
        hud_screen_push();
    }
}
// ===========================================================================
// Address: 0x80331a3c | match_team_back_reset_and_push
// Team-block back handler: prompt once, re-apply the team list, rebuild the
// tab's scroll frame, push the screen (unconditional rebuild variant).
// ===========================================================================
void match_team_back_reset_and_push(int pScreen) {
    int nTab = *(int *)(DAT_80612be4 + 0x248);
    int nDirty = hud_element_has_unsaved_changes(pScreen);
    if ((nDirty != 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0x17);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    match_team_reset_and_apply();
    FUN_803356e4((int)(&DAT_804816a8 + nTab * 0x44));
    hud_screen_push();
}
// ===========================================================================
// Address: 0x80331ad8 | match_team_back_reset_and_push_alt
// Team-block back handler variant: prompt once, re-apply, and when not
// committing (DAT_80612cfc==0) rebuild the tab scroll frame before pushing;
// otherwise just push.
// ===========================================================================
void match_team_back_reset_and_push_alt(int pScreen) {
    int nDirty = hud_element_has_unsaved_changes(pScreen);
    if ((nDirty != 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0x17);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    match_team_reset_and_apply();
    if (DAT_80612cfc == 0) {
        FUN_803356e4((int)(&DAT_804816a8 + *(int *)(DAT_80612be4 + 0x248) * 0x44));
        hud_screen_push();
    } else {
        hud_screen_push();
    }
}
// ===========================================================================
// Address: 0x80331b88 | match_team_first_free_slot
// Walk the current team's squad-index list (+0x41e4 +0x8c, count +0x88) and
// return the index of the first slot whose squad is unlocked
// (match_squad_slot_unlocked != 0 stops the scan). Registers are saved/restored
// around the walk in the raw. Returns the leading locked-slot count.
// ===========================================================================
int match_team_first_free_slot(void) {
    int nIdx = 0;
    int iOff = 0;
    int pTeam = DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220 + 0x41e4;
    while (nIdx < *(int *)(pTeam + 0x88)) {
        unsigned int uLocked =
            match_squad_slot_unlocked(DAT_80612be4 + *(int *)(pTeam + iOff + 0x8c) * 0xb4 + 0x81a8);
        if (uLocked == 0) {
            break;
        }
        nIdx = nIdx + 1;
        iOff = iOff + 4;
    }
    return nIdx;
}
// ===========================================================================
// Address: 0x80331c24 | match_team_row_action
// Team-block per-row value-change / confirm handler: count the bound squad
// children, and on unsaved changes classify the edit (FUN_8033210c kind 2). A
// kind-1 result raises the team confirm dialog using the per-team-config strings
// (&DAT_804816e0 / &DAT_804816dc indexed by the team id byte +0x41e8); a kind-0
// result just re-applies. Finally lock child 0 when there are no bound squads
// and no free slot (match_team_first_free_slot == 0).
// ===========================================================================
void match_team_row_action(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8) {
    int pScreen = FUN_803826f0();
    int nBound = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int pChild = hud_find_child_by_id(pScreen, i + 1);
        if ((pChild != 0) && (*(int *)(pChild + 200) != 0)) {
            nBound = nBound + 1;
        }
    }
    int nDirty = hud_element_has_unsaved_changes(pScreen);
    if (nDirty != 0) {
        int nKind = FUN_8033210c(pScreen, 2);
        if (nKind == 1) {
            int nCfg = (unsigned int)*(unsigned char *)
                       (DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220 + 0x41e8) * 0x44;
            char *pBody = (char *)get_string_by_id(*(int *)(&DAT_804816e0 + nCfg));
            FUN_80387fb8(p1, p2, p3, p4, p5, p6, p7, p8, DAT_80612be4,
                         pBody, *(int *)(&DAT_804816dc + nCfg), 0, 0, 0, 0, 0);
            hud_create_back_dialog(pScreen, DAT_80612be4, 0);
        } else if ((nKind < 1) && (-1 < nKind)) {
            match_team_reset_and_apply();
        }
        hud_screen_rebuild_items();
    }
    int nLock = 0;
    if ((nBound == 0) && (match_team_first_free_slot() == 0)) {
        nLock = 1;
    }
    hud_lock_child_by_id(pScreen, 0, nLock);
    FUN_8038273c();
}
// ===========================================================================
// Address: 0x80331d68 | match_build_team_squad_screen
// Build the team-block squad-list editor screen (title 0xd0e, back callback
// match_team_back_reset_and_push_alt). Walk the squad array for team squads
// (state == 2) that pass the flag-clear + FUN_80332494 predicate and whose
// membership bit (byte +0xb0, bit = current tab +0x248) is set; emit a
// text-display row (tag 0xdd) bound via FUN_80335d2c. Adds the accept row
// (0xb02/0xb03), positions the frame, marks it built. pScreen in r28.
// ===========================================================================
void match_build_team_squad_screen(int pScreen) {
    int pTitle = get_string_by_id(0xd0e);
    hud_element_init_from_layout(0x8400, pTitle, 0, (int (*)(void))match_team_back_reset_and_push_alt);
    hud_set_item_padding_mode((double)FLOAT_8061aa14, 1, 1, 1);
    hud_set_context_property(0x28000);
    double xOverflow = -(double)(FLOAT_80611a5c *
                                 ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                                   FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                                 FLOAT_8061aa18);
    hud_element_scroll_h_overflow(xOverflow, (double)FLOAT_8061aa28, (double)FLOAT_8061aa28, 0);

    get_string_by_id(0xaea);
    hud_add_list_item();
    hud_set_last_child_tag(0xdc);
    hud_add_callback_item((double)FLOAT_8061aa14);

    int iOff = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int *pEntry = (int *)(DAT_80612be4 + iOff + 0x81a8);
        if ((*pEntry == 2) && (match_squad_entry_flag_clear((int)pEntry) != 0)) {
            char cKeep = (char)FUN_80332494(*pEntry, pEntry[1]);
            if ((cKeep != '\0') &&
                (((unsigned int)*(unsigned char *)((char *)pEntry + 0xb0) &
                  (1 << *(int *)(DAT_80612be4 + 0x248))) != 0)) {
                hud_add_list_item();
                hud_item_add_text_display((double)FLOAT_8061aa28, 0);
                hud_set_last_child_tag(0xdd);
                FUN_80335d2c(DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220 + 0x41e4, i);
                hud_get_last_created_item();
                hud_item_on_value_change();
            }
        }
        iOff = iOff + 0xb4;
    }

    hud_add_callback_item((double)FLOAT_8061aa14);
    int nAcceptStr = 0xb03;
    if (DAT_80612cfc != 0) {
        nAcceptStr = 0xb02;
    }
    get_string_by_id(nAcceptStr);
    hud_add_list_item();
    hud_set_last_child_tag(0xde);
    hud_element_apply_scroll();
    double yFrame = (double)(FLOAT_80611a5c *
                             ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                               FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c));
    hud_element_set_pos_y(yFrame, DAT_806119e0);
    hud_element_finalize_frame();
    *(unsigned int *)(pScreen + 0x214) = *(unsigned int *)(pScreen + 0x214) | 4;
}
// ===========================================================================
// Address: 0x80331fc4 | match_team_slot_budget_sign
// Compare a team-block's live count against its configured budget: when the
// requested slot (nSlot) resolves to the "count" form use +0x88 vs the config
// body id (&DAT_804816dc[+8]); otherwise use +0x50 vs the config title id. The
// mode id byte +4 selects the config row (stride 0x44). Returns the sign bit of
// the signed budget comparison (branchless, as emitted).
// ===========================================================================
unsigned int match_team_slot_budget_sign(int pTeam, int nSlot) {
    unsigned int uSel = countLeadingZeros((unsigned int)(1 - nSlot));
    int iCfg = (unsigned int)*(unsigned char *)(pTeam + 4) * 0x44;
    unsigned int uCount;
    unsigned int uLimit;
    if (uSel >> 5 == 0) {
        uCount = *(unsigned int *)(pTeam + 0x88);
        uLimit = *(unsigned int *)(&DAT_804816dc + iCfg);
    } else {
        uCount = *(unsigned int *)(pTeam + 0x50);
        uLimit = *(unsigned int *)(&DAT_804816d4 + iCfg);
    }
    return (unsigned int)(((int)(uLimit ^ uCount) >> 1) - ((uLimit ^ uCount) & uLimit)) >> 0x1f;
}

// ===========================================================================
// batch_942 additions: the definitions of the match-editor commit gate,
// edit classifier, screen callbacks and mode-config screen builder
// (0x80332020 - 0x80332584). Several of these were forward-declared as externs
// by batch_940/941; the definitions below share those FUN_ symbol names so the
// earlier call sites resolve unchanged (the Ghidra IDB carries the readable
// names).
// New per-file externs first referenced by this batch.
// ===========================================================================
extern int  DAT_80612d08;                 // active mode id for the mode-config screen
extern int  DAT_80480f3c;                 // mode-config table: validator callback ptr (stride 0x4c)
extern int  DAT_80480f34;                 // mode-config table: UI child tag (stride 0x4c)
extern void hud_screen_pop_current(void); // pop the current HUD screen
extern void FUN_80333288(int pScreen);    // enter the mode-config sub-screen
extern void FUN_8038b510(int *pSession, int *pStr); // raise a simple info dialog
// match_mode_flag_byte is defined earlier in this file; FUN_8033224c has an
// existing `int (void)` extern above (used as a list callback) so its definition
// below returns int to match.
// ===========================================================================
// Address: 0x80332020 | FUN_80332020 (match_check_commit_budget)
// Commit-gate for the current team's squad edits. With no active selection
// (DAT_80612d00==0) returns 0 (nothing to block). Otherwise, if the team is
// over its slot budget (match_team_slot_budget_sign != 0) returns 0; if it is
// within budget and a screen is present, raise the over-count confirm dialog
// (title/body from the per-mode config row) and return 1; return 1 when a
// screen is present in-budget even without the dialog. Callers commit only when
// this returns 0.
// ===========================================================================
int FUN_80332020(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8) {
    long long llScreen = (long long)FUN_803826f0();
    int pScreen = (int)llScreen;
    int nScreenHi = (int)((unsigned long long)llScreen >> 0x20);
    int nRet = 0;
    if (DAT_80612d00 != 0) {
        int pTeam = DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220 + 0x41e4;
        unsigned int uOver = match_team_slot_budget_sign(pTeam, pScreen);
        if (uOver == 0) {
            if (nScreenHi != 0) {
                unsigned int uSel = countLeadingZeros((unsigned int)(1 - pScreen));
                int iCfg = (unsigned int)*(unsigned char *)(pTeam + 4) * 0x44;
                int nTitle;
                int nBodyId;
                if (uSel >> 5 == 0) {
                    nTitle = *(int *)(&DAT_804816dc + iCfg);
                    nBodyId = *(int *)(&DAT_804816e0 + iCfg);
                } else {
                    nTitle = *(int *)(&DAT_804816d4 + iCfg);
                    nBodyId = *(int *)(&DAT_804816d8 + iCfg);
                }
                char *pBody = (char *)get_string_by_id(nBodyId);
                FUN_80387fb8(p1, p2, p3, p4, p5, p6, p7, p8, DAT_80612be4, pBody, nTitle,
                             0, 0, 0, 0, 0);
                hud_create_back_dialog(nScreenHi, DAT_80612be4, 0);
            }
            nRet = 1;
        } else {
            nRet = 0;
        }
    }
    FUN_8038273c();
    return nRet;
}
// ===========================================================================
// Address: 0x8033210c | FUN_8033210c (match_classify_squad_edit)
// Classify a pending squad edit for the confirm-dialog logic. Walks the squad's
// membership list (+0x174 linked via +4), counting bound children (+200) of the
// matched partner squad (state +0xb4 == 4, tag +0xc4 > 0, key == nKey), tracking
// the last such node. Returns 1 when the bound count is under the per-tab budget
// (or no partner), 2 when match_squad_can_add fails but a live partner exists,
// else 0. Nudges hud_item_on_value_change in the 1 and 2 cases.
// ===========================================================================
int FUN_8033210c(int pSquad, int nKey) {
    int nBound = 0;
    unsigned int uSel = countLeadingZeros((unsigned int)(1 - nKey));
    int *pKeyEntry =
        (int *)(DAT_80612be4 + (*(int *)(pSquad + 0x188) - ((uSel >> 5) + 1)) * 0xb4 + 0x81a8);
    int pLast = 0;
    for (int pNode = *(int *)(pSquad + 0x174); pNode != 0; pNode = *(int *)(pNode + 4)) {
        int pKeep = pLast;
        if ((pKeyEntry != (int *)0x0) && (*(int *)(pNode + 0xb4) == 4) &&
            (pKeep = pNode, pNode != *(int *)(pSquad + 0x194)) &&
            (pKeep = pLast, 0 < *(int *)(pNode + 0xc4) && (*pKeyEntry == nKey))) {
            nBound = nBound + (int)(unsigned int)(*(int *)(pNode + 200) != 0);
        }
        pLast = pKeep;
    }
    int nRet;
    if ((nBound < *(int *)(&DAT_804816d4 + *(int *)(DAT_80612be4 + 0x248) * 0x44)) || (pLast == 0) ||
        (*(int *)(pLast + 200) != 1)) {
        int nCanAdd = match_squad_can_add(pKeyEntry, nKey);
        if ((nCanAdd == 0) && (pLast != 0) && (*(int *)(pLast + 200) == 1)) {
            hud_item_on_value_change();
            nRet = 2;
        } else {
            nRet = 0;
        }
    } else {
        hud_item_on_value_change();
        nRet = 1;
    }
    return nRet;
}
// ===========================================================================
// Address: 0x8033224c | FUN_8033224c (match_editor_screen_pop)
// Editor screen exit callback: pop the current HUD screen. Declared int(void)
// upstream (used as a list callback); returns 0.
// ===========================================================================
int FUN_8033224c(void) {
    hud_screen_pop_current();
    return 0;
}
// ===========================================================================
// Address: 0x8033226c | FUN_8033226c (match_collect_active_squads)
// Session readiness / collector: count squads whose state (+0x81ac, stride 0xb4)
// is one of {4,5,7}. When pOut is null just return the count; otherwise also
// write each matching squad's ordinal index into the pOut int array.
// ===========================================================================
int FUN_8033226c(int pOut) {
    int nCount = 0;
    int nWrite = 0;
    if (pOut == 0) {
        int iOff = 0;
        int nRemain = *(int *)(DAT_80612be4 + 0x81a4);
        if (0 < nRemain) {
            do {
                int nState = *(int *)(DAT_80612be4 + iOff + 0x81ac);
                if ((nState == 5) || (nState == 4) || (nState == 7)) {
                    nCount = nCount + 1;
                }
                iOff = iOff + 0xb4;
                nRemain = nRemain + -1;
            } while (nRemain != 0);
        }
    } else {
        int iOff = 0;
        for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
            int nState = *(int *)(DAT_80612be4 + iOff + 0x81ac);
            if ((nState == 5) || (nState == 4) || (nState == 7)) {
                *(int *)(pOut + nWrite) = i;
                nCount = nCount + 1;
                nWrite = nWrite + 4;
            }
            iOff = iOff + 0xb4;
        }
    }
    return nCount;
}
// ===========================================================================
// Address: 0x8033232c | FUN_8033232c (match_raise_mode_help)
// Raise the mode help/info dialog for the current screen (FUN_803826f0). When
// the element's flags word (+0x20 == word index 8) lacks bit 0x80, show the
// simple info dialog (FUN_8038b510) with the element's help string (+0x44);
// otherwise build the formatted list-screen help (FUN_80387fb8, body 0xb4b).
// ===========================================================================
void FUN_8033232c(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8) {
    int *pElem = (int *)FUN_803826f0();
    int pSession = DAT_80612be4;
    if ((pElem[8] & 0x80U) == 0) {
        int *pStr = (int *)get_string_by_id(pElem[0x11]);
        FUN_8038b510((int *)pSession, pStr);
    } else {
        int pTitle = get_string_by_id(*pElem);
        char *pBody = (char *)get_string_by_id(0xb4b);
        FUN_80387fb8(p1, p2, p3, p4, p5, p6, p7, p8, pSession, pBody, pElem[0x10], pTitle,
                     0, 0, 0, 0);
    }
    FUN_8038273c();
}
// ===========================================================================
// Address: 0x803323b4 | FUN_803323b4 (match_mode_row_select)
// Mode-select row action: record the chosen mode id (+0x188) into DAT_80612d08,
// run the mode's validator callback (mode-config table +0x3c, stride 0x4c) if
// present; on validator failure raise the mode-help dialog. Otherwise, when the
// mode's config-A word bit 0x40 is clear just push the screen, else enter the
// mode-config sub-screen (FUN_80333288).
// ===========================================================================
void FUN_803323b4(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8, int pElem) {
    DAT_80612d08 = *(int *)(pElem + 0x188);
    int iCfg = DAT_80612d08 * 0x4c;
    int (*pfnValidate)(void) = *(int (**)(void))(&DAT_80480f3c + iCfg);
    if ((pfnValidate != (int (*)(void))0x0) && (pfnValidate() == 0)) {
        FUN_8033232c(p1, p2, p3, p4, p5, p6, p7, p8);
        hud_create_back_dialog(pElem, 0, 0);
        return;
    }
    if ((*(unsigned int *)(&DAT_80480f30 + iCfg) & 0x40) == 0) {
        hud_screen_push();
    } else {
        FUN_80333288(pElem);
    }
}
// ===========================================================================
// Address: 0x8033245c | FUN_8033245c (match_mode_flag_matches)
// Predicate over the mode-config B word (DAT_80480f30, stride 0x4c): TRUE when
// the caller's flag mask (nMask) intersects the config flags, or nMask is 0.
// Returns the sign-extended boolean (-1 - !match, i.e. 0 or -1 in a char).
// ===========================================================================
int FUN_8033245c(int nUnused, int nModeId, unsigned int nMask) {
    (void)nUnused;
    bool bMatch = false;
    if (((nMask & *(unsigned int *)(&DAT_80480f30 + nModeId * 0x4c)) != 0) || (nMask == 0)) {
        bMatch = true;
    }
    return -1 - (int)(!bMatch);
}
// ===========================================================================
// Address: 0x80332494 | FUN_80332494 (match_row_visible_for_scope)
// Squad-list row visibility predicate. With no team selection (DAT_80612d00==0)
// test the mode membership flag for the current tab; with a team selection use
// the team's membership byte (+0x41e8) but hide the row when the mode-config B
// word has bit 0x10 (team-locked) set. Delegates the flag test to
// match_mode_flag_matches.
// ===========================================================================
int FUN_80332494(int pSquad, int nModeId) {
    int nResult;
    if (DAT_80612d00 == 0) {
        unsigned char bFlag = (unsigned char)match_mode_flag_byte(pSquad, *(int *)(DAT_80612be4 + 0x248));
        nResult = FUN_8033245c(pSquad, nModeId, (unsigned int)bFlag);
    } else {
        unsigned char bFlag = (unsigned char)match_mode_flag_byte(
            pSquad,
            (unsigned int)*(unsigned char *)(DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220 + 0x41e8));
        if ((*(unsigned int *)(&DAT_80480f30 + nModeId * 0x4c) & 0x10) == 0) {
            nResult = FUN_8033245c(pSquad, nModeId, (unsigned int)bFlag);
        } else {
            nResult = 0;
        }
    }
    return nResult;
}
// ===========================================================================
// Address: 0x80332560 | FUN_80332560 (match_squad_visible_on_tab)
// Predicate: TRUE when the squad entry's membership byte (+0xb0) has the bit for
// the current tab (+0x248) set.
// ===========================================================================
int FUN_80332560(int pEntry) {
    return (int)('\x01' - (int)(((unsigned int)*(unsigned char *)(pEntry + 0xb0) &
                                 (1 << *(int *)(DAT_80612be4 + 0x248))) == 0));
}
// ===========================================================================
// Address: 0x80332584 | match_build_mode_config_screen
// Build the arcade "apply/confirm mode config" screen. Title 0xb04 when the
// pending mode is the add form (DAT_80612d0c==1) else 0xb05. Iterate the 0x10
// mode-config rows (stride 0x4c); for each row enabled for the pending mode
// (config-A word +0x10/+0x18 non-zero by mode) that passes the row predicate
// (match_row_visible_for_scope), add a labeled list item tagged by the row's UI
// tag (+0x34). Positions and finalizes the frame. pScreen in r28.
// ===========================================================================
void match_build_mode_config_screen(int pScreen) {
    int nTitleId = 0xb05;
    if (DAT_80612d0c == 1) {
        nTitleId = 0xb04;
    }
    int pTitle = get_string_by_id(nTitleId);
    hud_element_init_from_layout(0x8400, pTitle, 0, (int (*)(void))FUN_8033224c);
    hud_set_item_padding_mode((double)FLOAT_8061aa14, 1, 1, 1);
    hud_set_context_property(0x28000);
    int nMode = 0;
    int iCfg = 0;
    do {
        if ((((DAT_80612d0c == 1) && (*(int *)((int)&DAT_80480f20 + iCfg) != 0)) ||
             ((DAT_80612d0c == 2) && (*(int *)((int)&DAT_80480f28 + iCfg) != 0))) &&
            ((char)FUN_80332494(DAT_80612d0c, nMode) != '\0')) {
            get_string_by_id(*(int *)((int)&DAT_80480f10 + iCfg));
            hud_add_list_item();
            hud_set_last_child_tag(*(int *)((int)&DAT_80480f34 + iCfg));
        }
        nMode = nMode + 1;
        iCfg = iCfg + 0x4c;
    } while (nMode < 0x10);
    double yFrame = (double)(FLOAT_80611a5c *
                             ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                               FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c));
    hud_element_set_pos_y(yFrame, DAT_806119e0);
    hud_element_finalize_frame();
    *(unsigned int *)(pScreen + 0x214) = *(unsigned int *)(pScreen + 0x214) | 4;
}

// ===========================================================================
// batch_943 additions: arcade custom-match descriptor/tab selection actions,
// the descriptor-slot list screen, and character/weapon availability predicates
// (0x803326f8 - 0x80332c04). Config blocks live in g_MatchSession at
// +0x113c4 (per-tab 0x60 stride, 4 sub-blocks of 0x18 each). New per-file
// externs first referenced by this batch.
// ===========================================================================
extern int  DAT_80481738;                 // descriptor-slot label table (populated rows), stride 8 ints
extern int  DAT_8048173c;                 // descriptor-slot label table (empty rows), stride 0x20
extern unsigned int DAT_80477c80;         // character/weapon availability table, stride 0x31 bytes
extern short DAT_80477bca;                // character level-tag table, stride 0x62 shorts
extern int  FUN_80338498(void);           // -> current pick/preview validity handle
extern int  roster_slot_to_layout(int nSlot); // arcade_select_roster.cpp: map roster slot -> layout id
// ===========================================================================
// Address: 0x803326f8 | match_open_descriptor_slot
// Record the chosen descriptor slot (element +0x188) into the active-descriptor
// index DAT_80612d04 and push the descriptor screen.
// ===========================================================================
void match_open_descriptor_slot(int pElem) {
    DAT_80612d04 = *(int *)(pElem + 0x188);
    hud_screen_push();
}
// ===========================================================================
// Address: 0x8033272c | match_block_slot_index
// Return the index (0..3) of the config block whose address matches pBlock
// within the current tab's 4-slot block array (g_MatchSession + tab*0x60 +
// 0x113c4, stride 0x18), or -1 if not found.
// ===========================================================================
int match_block_slot_index(int pBlock) {
    int nIdx = 0;
    int iOff = 0;
    int nRemain = 4;
    do {
        if (DAT_80612be4 + *(int *)(DAT_80612be4 + 0x248) * 0x60 + iOff + 0x113c4 == pBlock) {
            return nIdx;
        }
        nIdx = nIdx + 1;
        iOff = iOff + 0x18;
        nRemain = nRemain + -1;
    } while (nRemain != 0);
    return -1;
}
// ===========================================================================
// Address: 0x8033277c | match_select_tab_and_open
// Tab-select action: set the active tab (+0x248 = nTab), resolve the target
// block's slot index (match_block_slot_index), store it (+2) into the owning
// element's descriptor id (+0x2c -> +0x188) and open the descriptor screen.
// ===========================================================================
void match_select_tab_and_open(int pElem, int nUnused, int nTab, int pBlock) {
    (void)nUnused;
    *(int *)(DAT_80612be4 + 0x248) = nTab;
    int nIdx = match_block_slot_index(pBlock);
    *(int *)(*(int *)(pElem + 0x2c) + 0x188) = nIdx + 2;
    match_open_descriptor_slot(*(int *)(pElem + 0x2c));
}
// ===========================================================================
// Address: 0x803327c8 | match_enable_descriptor_slot
// Mark the descriptor block for the element's slot (mode id +0x188, minus 2) as
// active: set its two head words to 1 and OR the busy bit into +0x14; raise the
// one-shot prompt (kind 0x18) if not yet prompted; clear the busy bit; then open
// the descriptor screen.
// ===========================================================================
void match_enable_descriptor_slot(int pElem) {
    unsigned int *pBlock = (unsigned int *)
        (DAT_80612be4 + *(int *)(DAT_80612be4 + 0x248) * 0x60 +
         (*(int *)(pElem + 0x188) + -2) * 0x18 + 0x113c4);
    pBlock[1] = 1;
    *pBlock = 1;
    *(unsigned char *)(pBlock + 5) = *(unsigned char *)(pBlock + 5) | 1;
    if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(0x18);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    *(unsigned char *)(pBlock + 5) = *(unsigned char *)(pBlock + 5) & 0xfe;
    match_open_descriptor_slot(pElem);
}
// ===========================================================================
// Address: 0x80332874 | match_find_block_by_flag
// Scan the 2x4 grid of config blocks (2 tabs of 0x60, 4 sub-blocks of 0x18)
// for the first whose flag byte (+0x14) intersects nMask. Writes the tab index
// to *pRow and the sub-slot index to *pCol, and returns the matching block ptr
// (0 if none in the 2x4 window).
// ===========================================================================
int match_find_block_by_flag(unsigned char nMask, int *pRow, int *pCol) {
    int pFound = 0;
    int nTab = 0;
    int iRowOff = 0;
    do {
        int nCol = 0;
        int iColOff = 0;
        int nRemain = 4;
        do {
            int pBlock = DAT_80612be4 + iRowOff + iColOff + 0x113c4;
            if ((nMask & *(unsigned char *)(pBlock + 0x14)) != 0) {
                *pCol = nCol;
                *pRow = nTab;
                pFound = pBlock;
                break;
            }
            nCol = nCol + 1;
            iColOff = iColOff + 0x18;
            nRemain = nRemain + -1;
        } while (nRemain != 0);
        nTab = nTab + 1;
        iRowOff = iRowOff + 0x60;
        if (1 < nTab) {
            return pFound;
        }
    } while (true);
}
// ===========================================================================
// Address: 0x803328e4 | match_build_descriptor_slot_screen
// Build the descriptor-slot list screen (title = current session mode value).
// For each of the 4 descriptor slots of the current tab, pick the label from the
// populated-row table (&DAT_80481738, stride 8) when the slot's block ptr
// (+0x113c8) is set, else from the empty-row table (&DAT_8048173c, stride 0x20),
// and tag the row 0xd7 (populated) / 0xd6 (empty). Adds the accept row (0xb03),
// positions and finalizes. pScreen in r28.
// ===========================================================================
void match_build_descriptor_slot_screen(int pScreen) {
    int nMode = match_session_mode_value();
    int pTitle = get_string_by_id(nMode);
    hud_element_init_from_layout(0x8400, pTitle, 0, (int (*)(void))FUN_8033224c);
    hud_set_item_padding_mode((double)FLOAT_8061aa14, 1, 1, 1);
    hud_set_context_property(0x28000);
    int nSlot = 2;
    do {
        int nIdx = nSlot + -2;
        int pBlock = *(int *)(DAT_80612be4 + *(int *)(DAT_80612be4 + 0x248) * 0x60 +
                              nIdx * 0x18 + 0x113c8);
        int nLabel;
        if (pBlock == 0) {
            nLabel = *(int *)(&DAT_8048173c + nIdx * 0x20);
        } else {
            nLabel = (&DAT_80481738)[nIdx * 8];
        }
        get_string_by_id(nLabel);
        hud_add_list_item();
        if (pBlock == 0) {
            hud_set_last_child_tag(0xd6);
        } else {
            hud_set_last_child_tag(0xd7);
        }
        nSlot = nSlot + 1;
    } while (nSlot < 6);
    hud_add_callback_item((double)FLOAT_8061aa14);
    get_string_by_id(0xb03);
    hud_add_list_item();
    double yFrame = (double)(FLOAT_80611a5c *
                             ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                               FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c));
    hud_element_set_pos_y(yFrame, DAT_806119e0);
    hud_element_finalize_frame();
    *(unsigned int *)(pScreen + 0x214) = *(unsigned int *)(pScreen + 0x214) | 4;
}
// ===========================================================================
// Address: 0x80332a84 | match_pickable_availability
// Character/weapon availability predicate over the pick table (&DAT_80477c80,
// stride 0x31 bytes). When the pending mode is add (DAT_80612d08==0) test bit
// 0x10 (or 0x20 when the current selection is a team leader, +0xb0 bit 2); when
// it is apply (==1) test bit 0x40 (or 0x80 for a leader). Any other mode returns
// false.
// ===========================================================================
bool match_pickable_availability(int nPick) {
    if (DAT_80612d08 == 0) {
        bool bLeader = false;
        if ((DAT_80612d00 != 0) && ((*(unsigned char *)(DAT_80612d00 + 0xb0) & 2) != 0)) {
            bLeader = true;
        }
        unsigned int uMask = 0x10;
        if (bLeader) {
            uMask = 0x20;
        }
        return (bool)('\x01' - (int)(((&DAT_80477c80)[nPick * 0x31] & uMask) == 0));
    }
    if (DAT_80612d08 == 1) {
        bool bLeader = false;
        if ((DAT_80612d00 != 0) && ((*(unsigned char *)(DAT_80612d00 + 0xb0) & 2) != 0)) {
            bLeader = true;
        }
        unsigned int uMask = 0x40;
        if (bLeader) {
            uMask = 0x80;
        }
        return (bool)('\x01' - (int)(((&DAT_80477c80)[nPick * 0x31] & uMask) == 0));
    }
    return false;
}
// ===========================================================================
// Address: 0x80332b34 | match_pick_valid_for_level
// Validity predicate for a character/weapon pick (nPick). The pick's level-tag
// short (&DAT_80477bca, stride 0x62) must equal the session's current level id
// (+0x20c), and the pick id must be in one of the allowed ranges (0x35..0x37,
// 0x45..0x46). Returns 1 when valid, else 0.
// ===========================================================================
int match_pick_valid_for_level(int nPick) {
    if ((int)(short)(&DAT_80477bca)[nPick * 0x62] != *(int *)(DAT_80612be4 + 0x20c)) {
        return 0;
    }
    if ((nPick != 0x46) && (((0x45 < nPick || (0x37 < nPick)) || (nPick < 0x35)))) {
        return 0;
    }
    return 1;
}
// ===========================================================================
// Address: 0x80332b90 | match_current_pick_valid
// When the session is in the pick screen (+0x1185c == 8) and the currently
// highlighted pick (**+0x2f1c) is valid for the level, return 1 if its slot
// (+4) is empty (-1), else the CLZ-boolean of the current pick/preview handle
// (FUN_80338498). Returns 0 outside the pick screen or on an invalid pick.
// ===========================================================================
unsigned int match_current_pick_valid(void) {
    unsigned int uResult;
    if ((*(int *)(DAT_80612be4 + 0x1185c) == 8) &&
        (match_pick_valid_for_level(**(int **)(DAT_80612be4 + 0x2f1c)) != 0)) {
        if (*(int *)(*(int *)(DAT_80612be4 + 0x2f1c) + 4) == -1) {
            uResult = 1;
        } else {
            uResult = countLeadingZeros((unsigned int)FUN_80338498()) >> 5;
        }
    } else {
        uResult = 0;
    }
    return uResult;
}
// ===========================================================================
// Address: 0x80332c04 | match_roster_pick_valid
// Roster variant of match_current_pick_valid: in the pick screen with a valid
// roster slot (+0x2f78 >= 0) and no active pick node (+0x2f1c == 0), map the
// slot's layout id (roster_slot_to_layout of byte +0x3077) and validate it. If
// the slot's stored tag short (+0x307a) is negative return 1, else the
// CLZ-boolean of FUN_80338498. Returns 0 otherwise.
// ===========================================================================
unsigned int match_roster_pick_valid(void) {
    if ((*(int *)(DAT_80612be4 + 0x1185c) == 8) && (-1 < *(int *)(DAT_80612be4 + 0x2f78)) &&
        (*(int *)(DAT_80612be4 + 0x2f1c) == 0)) {
        int nLayout = roster_slot_to_layout(
            (unsigned int)*(unsigned char *)(DAT_80612be4 + *(int *)(DAT_80612be4 + 0x2f78) * 0xc + 0x3077));
        if (match_pick_valid_for_level(nLayout) != 0) {
            if (*(short *)(DAT_80612be4 + *(int *)(DAT_80612be4 + 0x2f78) * 0xc + 0x307a) < 0) {
                return 1;
            }
            return countLeadingZeros((unsigned int)FUN_80338498()) >> 5;
        }
    }
    return 0;
}

// ===========================================================================
// batch_944: arcade custom-match editor action dispatcher + screen-state
// transitions. These sit on top of the mode-config screen (DAT_80612d08 active
// mode, DAT_80612d0c edit kind) and the g_MatchSession block/history arrays.
//   +0x11858 : pointer to the currently-selected element block
//   +0x1185c : current editor screen-state id (2=add, 5=apply, 6=review, 8=pick)
//   +0xdd10  : history-entry count; +0xdd14 stride-0x8c history array
//   +0xdd90  : per-history committed flag (i*0x8c + 0xdd90)
// Address range: 0x80332ca0 - 0x80332fa0
// ---- per-file externs new to this batch -----------------------------------
extern int  DAT_80612bf4;                     // editor: current target mode id (add-flow arg)
extern void screen_exit_clear_flag(void);     // 0x8032...: pop screen + clear the busy flag
extern int  FUN_80336138(int, int, unsigned short, unsigned short); // build apply-selection block -> block ptr
extern int  FUN_80335f5c(int, int, unsigned short, unsigned short); // build add-selection block -> block ptr
extern int  FUN_80336e6c(int, int, int, int, int, int, int, int);   // append history entry -> history index

// forward declaration: dispatcher is defined at the end of this batch block
void match_dispatch_editor_action(void *pPreCb, void *pValueCb, int nElement, int nValue);

// ===========================================================================
// Address: 0x80332ca0 | match_resolve_selected_descriptor
// Resolve the display cap for a selected block's category (block[0]) via
// match_cap_select_for_category and stash it back into block[2].
// ===========================================================================
void match_resolve_selected_descriptor(int *pBlock) {
    int nCap = match_cap_select_for_category(*pBlock);
    pBlock[2] = nCap;
}

// ===========================================================================
// Address: 0x80332cd8 | match_apply_pending_selection
// Build the pending selection block for the active mode (DAT_80612d08) using
// the appropriate constructor for the current edit kind (DAT_80612d0c: 2 ==
// apply table &DAT_80480f2c, 0<kind<2 == add table &DAT_80480f24), store the
// resulting block ptr into g_MatchSession+0x11858, resolve its display cap, and
// (when outside the pick screen and the cached selection is uncommitted) run
// the follow-up bind FUN_80335c6c.
// ===========================================================================
void match_apply_pending_selection(unsigned short nParamA, unsigned short nParamB) {
    if (DAT_80612d0c == 2) {
        int nBlock = FUN_80336138(DAT_80612d08,
                                  *(int *)(&DAT_80480f2c + DAT_80612d08 * 0x4c),
                                  nParamA, nParamB);
        *(int *)(DAT_80612be4 + 0x11858) = nBlock;
    } else if ((DAT_80612d0c < 2) && (0 < DAT_80612d0c)) {
        int nBlock = FUN_80335f5c(DAT_80612d08,
                                  *(int *)(&DAT_80480f24 + DAT_80612d08 * 0x4c),
                                  nParamA, nParamB);
        *(int *)(DAT_80612be4 + 0x11858) = nBlock;
    }
    match_resolve_selected_descriptor(*(int **)(DAT_80612be4 + 0x11858));
    if (((DAT_80612d00 != 0) && (*(int *)DAT_80612d00 == 0)) &&
        (*(int *)(DAT_80612be4 + 0x1185c) != 8)) {
        FUN_80335c6c(DAT_80612d00, *(int *)(DAT_80612be4 + 0x11858));
    }
}

// ===========================================================================
// Address: 0x80332dc0 | match_history_append_committed
// Append a new editor-history entry (FUN_80336e6c) and mark that entry's
// committed flag (i*0x8c + 0xdd90) set. Forwards the full 8-arg tail so the
// history constructor can capture the source callback and preview handle.
// ===========================================================================
void match_history_append_committed(int a1, int a2, int a3, int a4,
                                    int a5, int a6, int a7, int a8) {
    int nIndex = FUN_80336e6c(a1, a2, a3, a4, a5, a6, a7, a8);
    *(int *)(DAT_80612be4 + nIndex * 0x8c + 0xdd90) = 1;
}

// ===========================================================================
// Address: 0x80332e00 | match_commit_selection_action
// Editor "commit" action: dispatch match_history_append_committed as the
// value-callback with the current target mode (DAT_80612bf4).
// ===========================================================================
void match_commit_selection_action(int nElement) {
    match_dispatch_editor_action(0, (void *)match_history_append_committed,
                                 DAT_80612bf4, nElement);
}

// ===========================================================================
// Address: 0x80332e38 | match_set_screen_state_add
// Pre-callback: enter the "add" editor screen-state (+0x1185c = 2) and pop the
// current screen / clear the busy flag.
// ===========================================================================
void match_set_screen_state_add(void) {
    *(int *)(DAT_80612be4 + 0x1185c) = 2;
    screen_exit_clear_flag();
}

// ===========================================================================
// Address: 0x80332e68 | match_open_add_screen_action
// Editor action opening the "add" screen: run match_set_screen_state_add as the
// pre-callback, with no value-callback (index -1).
// ===========================================================================
void match_open_add_screen_action(int nElement) {
    match_dispatch_editor_action((void *)match_set_screen_state_add, 0, nElement, -1);
}

// ===========================================================================
// Address: 0x80332ea0 | match_set_screen_state_apply
// Pre-callback: enter the "apply" editor screen-state (+0x1185c = 5) and pop the
// current screen / clear the busy flag.
// ===========================================================================
void match_set_screen_state_apply(void) {
    *(int *)(DAT_80612be4 + 0x1185c) = 5;
    screen_exit_clear_flag();
}

// ===========================================================================
// Address: 0x80332ed0 | match_open_apply_screen_action
// Editor action opening the "apply" screen: run match_set_screen_state_apply as
// the pre-callback, no value-callback.
// ===========================================================================
void match_open_apply_screen_action(int nElement) {
    match_dispatch_editor_action((void *)match_set_screen_state_apply, 0, nElement, -1);
}

// ===========================================================================
// Address: 0x80332f08 | match_set_screen_state_review
// Enter the "review" editor screen-state (+0x1185c = 6), then walk the block
// linked list (head DAT_80612c14, next @ +0x40) and, for every block referenced
// by the selected element's history slot (element short +0xc -> history row at
// i*0x8c + 0xdd14, entries at +0x20 stride 8, count +0x70), set the block's
// "referenced" bit (+0x14 |= 2). Finishes by popping the screen / clearing the
// busy flag.
// ===========================================================================
void match_set_screen_state_review(void) {
    int nBlock = DAT_80612c14;
    int iBase = DAT_80612be4;
    short nHist = *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc);
    *(int *)(DAT_80612be4 + 0x1185c) = 6;
    iBase = iBase + nHist * 0x8c + 0xdd14;
    for (; nBlock != 0; nBlock = *(int *)(nBlock + 0x40)) {
        int nOff = 0;
        for (int i = 0; i < *(int *)(iBase + 0x70); i = i + 1) {
            if (nBlock == *(int *)(iBase + nOff + 0x20)) {
                *(unsigned char *)(nBlock + 0x14) = *(unsigned char *)(nBlock + 0x14) | 2;
            }
            nOff = nOff + 8;
        }
    }
    screen_exit_clear_flag();
}

// ===========================================================================
// Address: 0x80332fa0 | match_dispatch_editor_action
// Budget-checked editor action dispatcher. Reads a packed pair of callbacks
// from the current screen element (FUN_803826e8 hi = pre-callback pcVar1, lo =
// value-callback), then gates on match_mode_dispatch_budget_checked(active
// mode). If over budget it raises the blocked dialog; otherwise it runs the
// pre-callback with nElement (when both non-null) and, when a value-callback is
// present and 0 <= nValue < 0x50, invokes it with nValue. Releases the element
// lock (FUN_80382734) on exit.
//   pPreCb  : optional pre-callback (unused directly; packed value comes from
//             the element handle in the original)
//   pValueCb: optional value callback (same note)
// ===========================================================================
void match_dispatch_editor_action(void *pPreCb, void *pValueCb, int nElement, int nValue) {
    (void)pPreCb;
    (void)pValueCb;
    // FUN_803826e8 returns a packed 64-bit callback pair in the original (hi =
    // pre-callback, lo = value-callback). This file declares it as int (see the
    // head extern), so we follow the established u64-cast idiom used elsewhere.
    u64 uPair = (u64)FUN_803826e8();
    void (*pcPre)(int) = (void (*)(int))(int)(uPair >> 0x20);
    void (*pcValue)(int) = (void (*)(int))(int)uPair;
    unsigned int uBudget = match_mode_dispatch_budget_checked(DAT_80612d08);
    if (uBudget == 0) {
        show_blocked_dialog(nElement);
    } else {
        if ((pcPre != 0) && (nElement != 0)) {
            (*pcPre)(nElement);
        }
        if ((pcValue != 0) && (-1 < nValue) && (nValue < 0x50)) {
            (*pcValue)(nValue);
        }
    }
    FUN_80382734();
}

// ===========================================================================
// batch_945: arcade custom-match pick-slot apply/confirm action pairs. Each
// "confirm" action feeds a slot-apply pre-callback into match_dispatch_editor_action
// (batch_944); the pre-callback validates the selected element's chosen slot
// (element +0x188, -1 == none), applies it through match_apply_pending_selection
// and pushes the resulting screen. Some variants gate on a per-mode slot budget
// (match_session_slot_clamp) or a leader descriptor-weight cap.
// Address range: 0x80333050 - 0x80333304
// ---- per-file externs new to this batch -----------------------------------
extern void FUN_8033232c(u64, u64, u64, u64, u64, u64, u64, u64); // raise mode help/info dialog into DAT_80612be4

// forward declarations: apply pre-callbacks are defined before their confirm
// wrappers, but the budgeted/weighted variants reference peers defined later.
void match_apply_current_slot_cb(int nElement);

// ===========================================================================
// Address: 0x80333050 | match_apply_add_slot_cb
// Slot-apply pre-callback (add flavour): when the element's chosen slot
// (+0x188) is valid, append a history entry (FUN_80336e6c) as the "A" selection
// param, apply the pending selection with a null "B" (0xffff), and push the
// screen. 8-arg tail is forwarded to the history constructor.
// ===========================================================================
void match_apply_add_slot_cb(int a1, int a2, int a3, int a4,
                             int a5, int a6, int a7, int a8, int pElement) {
    if (-1 < *(int *)(pElement + 0x188)) {
        unsigned short nSel = (unsigned short)FUN_80336e6c(a1, a2, a3, a4, a5, a6, a7, a8);
        match_apply_pending_selection(nSel, 0xffff);
        hud_screen_push();
    }
}

// ===========================================================================
// Address: 0x803330a4 | match_confirm_add_slot_action
// Confirm action: dispatch match_apply_add_slot_cb as the pre-callback.
// ===========================================================================
void match_confirm_add_slot_action(int nElement) {
    match_dispatch_editor_action((void *)match_apply_add_slot_cb, 0, nElement, -1);
}

// ===========================================================================
// Address: 0x803330dc | match_apply_swap_slot_cb
// Slot-apply pre-callback (swap flavour): when the chosen slot (+0x188) is
// valid, clamp it against the participant count (+0x280; if the slot exceeds it
// shift right 0x10 to recover the packed index), then apply the pending
// selection with a null "A" (0xffff) and the slot as the "B" short, and push.
// ===========================================================================
void match_apply_swap_slot_cb(int pElement) {
    int nSlot = *(int *)(pElement + 0x188);
    if (-1 < nSlot) {
        if (*(int *)(DAT_80612be4 + 0x280) < nSlot) {
            *(int *)(pElement + 0x188) = nSlot >> 0x10;
        }
        match_apply_pending_selection(0xffff, (unsigned short)(short)*(int *)(pElement + 0x188));
        hud_screen_push();
    }
}

// ===========================================================================
// Address: 0x80333144 | match_confirm_swap_slot_action
// Confirm action: dispatch match_apply_swap_slot_cb as the pre-callback.
// ===========================================================================
void match_confirm_swap_slot_action(int nElement) {
    match_dispatch_editor_action((void *)match_apply_swap_slot_cb, 0, nElement, -1);
}

// ===========================================================================
// Address: 0x8033317c | match_apply_slot_cb
// Slot-apply pre-callback (direct flavour): when the chosen slot (+0x188) is
// valid, apply the pending selection using the slot as the "A" short and push.
// ===========================================================================
void match_apply_slot_cb(int pElement) {
    if (-1 < *(int *)(pElement + 0x188)) {
        match_apply_pending_selection((unsigned short)(short)*(int *)(pElement + 0x188), 0xffff);
        hud_screen_push();
    }
}

// ===========================================================================
// Address: 0x803331c8 | match_confirm_slot_action
// Confirm action: dispatch match_apply_slot_cb as the pre-callback.
// ===========================================================================
void match_confirm_slot_action(int nElement) {
    match_dispatch_editor_action((void *)match_apply_slot_cb, 0, nElement, -1);
}

// ===========================================================================
// Address: 0x80333200 | match_apply_budgeted_slot_cb
// Slot-apply pre-callback with a per-mode budget gate: if the active mode's slot
// budget is exhausted (match_session_slot_clamp(DAT_80612d08) == 0) raise the
// mode help dialog (FUN_8033232c, built into DAT_80612be4) behind a back dialog;
// otherwise append a history entry and apply the selection, then push.
// ===========================================================================
void match_apply_budgeted_slot_cb(u64 a1, u64 a2, u64 a3, u64 a4,
                                  u64 a5, u64 a6, u64 a7, u64 a8, int pElement) {
    int nBudget = match_session_slot_clamp(DAT_80612d08);
    if (nBudget == 0) {
        FUN_8033232c(a1, a2, a3, a4, a5, a6, a7, a8);
        hud_create_back_dialog(pElement, DAT_80612be4, 0);
    } else {
        unsigned short nSel = (unsigned short)FUN_80336e6c((int)a1, (int)a2, (int)a3, (int)a4,
                                                           (int)a5, (int)a6, (int)a7, (int)a8);
        match_apply_pending_selection(nSel, 0xffff);
        hud_screen_push();
    }
}

// ===========================================================================
// Address: 0x80333288 | match_confirm_budgeted_slot_action
// Confirm action: dispatch match_apply_budgeted_slot_cb as the pre-callback.
// ===========================================================================
void match_confirm_budgeted_slot_action(int nElement) {
    match_dispatch_editor_action((void *)match_apply_budgeted_slot_cb, 0, nElement, -1);
}

// ===========================================================================
// Address: 0x803332c0 | match_apply_current_slot_cb
// Slot-apply pre-callback (unconditional): apply the pending selection using the
// element's chosen slot (+0x188) as the "A" short and push the screen.
// ===========================================================================
void match_apply_current_slot_cb(int pElement) {
    match_apply_pending_selection((unsigned short)(short)*(int *)(pElement + 0x188), 0xffff);
    hud_screen_push();
}

// ===========================================================================
// Address: 0x80333304 | match_confirm_weighted_slot_action
// Confirm action with a leader descriptor-weight cap. When the current selection
// is a team leader (DAT_80612d00 +0xb0 bit 2) with a valid descriptor (+0xa4),
// sum this slot's descriptor weight (match_desc_slot_weight of the element's
// history row) with the existing total (match_desc_sum_slot_weights); if the
// combined weight would exceed the cap (>= 0xb) the confirm is refused. On
// refusal it shows the "weight exceeded" list-screen message (text 0xb5f) behind
// a back dialog; otherwise it dispatches match_apply_current_slot_cb.
// ===========================================================================
void match_confirm_weighted_slot_action(u64 a1, u64 a2, u64 a3, u64 a4,
                                        u64 a5, u64 a6, u64 a7, u64 a8, int pElement,
                                        int a10, int a11, int a12, int a13,
                                        int a14, int a15, int a16) {
    int bAllowed = 1;
    if (((DAT_80612d00 != 0) && ((*(unsigned char *)(DAT_80612d00 + 0xb0) & 2) != 0)) &&
        (-1 < *(int *)(DAT_80612d00 + 0xa4))) {
        unsigned int uWeight =
            match_desc_slot_weight(DAT_80612be4 + *(int *)(pElement + 0x188) * 0x8c + 0xdd14);
        int nTotal = match_desc_sum_slot_weights(0);
        bAllowed = (int)((unsigned int)(nTotal + uWeight < 0xb) - ((int)(nTotal + uWeight) >> 0x1f));
    }
    if (bAllowed == 0) {
        char *pMsg = (char *)get_string_by_id(0xb5f);
        FUN_80387fb8(a1, a2, a3, a4, a5, a6, a7, a8, DAT_80612be4, pMsg,
                     10, a12, a13, a14, a15, a16);
        hud_create_back_dialog(pElement, DAT_80612be4, 0);
    } else {
        match_dispatch_editor_action((void *)match_apply_current_slot_cb, 0, pElement, -1);
    }
}

// ===========================================================================
// batch_946: arcade custom-match editor list-screen builder, per-frame preview
// tick/close hooks, and selection-descriptor predicates. Centred on the current
// editor screen element and the active mode (DAT_80612d08) over g_MatchSession
// (DAT_80612be4). The screen builder walks the mode-config tables (DAT_80480f14
// title / DAT_80480f18 subtitle / DAT_80480f30 flags at stride 0x4c) and the
// per-mode code-ptr tables (PTR_aveditor_build_weapon_list_a_804816c0 stride
// 0x11, PTR_FUN_80480f44 stride 0x13) to populate the list.
// Address range: 0x803333f4 - 0x80333c40
// ---- per-file externs new to this batch -----------------------------------
extern float FLOAT_8061aab0;                 // list padding step for squad-slot rows
extern int   DAT_80480f14;                   // mode-config table: title string id (stride 0x4c)
extern int   DAT_80480f18;                   // mode-config table: subtitle string id (stride 0x4c)
extern short DAT_80477bc8;                    // pick table: title-string short (stride 0xc4)
extern void *PTR_aveditor_build_weapon_list_a_804816c0; // per-mode weapon-list builder table (stride 0x11 words)
extern void *PTR_FUN_80480f44;               // per-mode apply/rebuild callback table (stride 0x13 words)
extern void FUN_8033f224(void);              // rebuild the entity preview after a close
extern void FUN_80337be8(void);              // flush pending editor edits before enter
extern int  FUN_803392d4(int);               // -> nonzero when the selected block has unsaved edits
extern void FUN_80339370(void);              // finalize the editor screen enter
extern void FUN_80339250(int);               // re-run the per-block apply pass
extern void aveditor_build_texslot_actions(void); // append a texture-slot action row
extern int  LAB_80333458(int pSquad);        // inline squad-visibility predicate (kind==1 && (1<<squad[8])&1)
extern int  hud_element_is_closing(int element);        // -> nonzero when the screen element is closing
extern void aveditor_update_entity_preview(int type);   // refresh the live entity preview
extern int  hud_screen_all_children_closing(int screen);// -> nonzero when every child element is closing

// forward declaration: the descriptor-id mapper is used by the screen enter fn
unsigned int match_descriptor_id_for_kind(int *pSel, int pRow);

// ===========================================================================
// Address: 0x803333f4 | match_apply_swap_history_cb
// Slot-apply pre-callback that also records the chosen slot into the new history
// entry's committed field. Append a history entry (FUN_80336e6c), copy the
// element's chosen slot (+0x188) into that entry's committed flag (i*0x8c +
// 0xdd90), then apply the selection using the history index as the A short and
// push the screen.
// ===========================================================================
void match_apply_swap_history_cb(int a1, int a2, int a3, int a4,
                                 int a5, int a6, int a7, int a8, int pElement) {
    int nIndex = FUN_80336e6c(a1, a2, a3, a4, a5, a6, a7, a8);
    *(int *)(DAT_80612be4 + nIndex * 0x8c + 0xdd90) = *(int *)(pElement + 0x188);
    match_apply_pending_selection((unsigned short)(short)nIndex, 0xffff);
    hud_screen_push();
}

// ===========================================================================
// Address: 0x80333488 | match_build_squad_slot_rows
// Walk the squad array (count +0x81a4, stride 0xb4 from +0x81a8) and, for each
// squad the supplied predicate (pPredicate, passed in r4 in the raw) accepts and
// whose mode-config flags (DAT_80480f30 indexed by squad+4) intersect nMask, add
// a labeled list item plus a padding row. pRowAction is the per-row action bound
// by hud_add_list_item's context (passed through by the caller).
// ===========================================================================
void match_build_squad_slot_rows(void *pRowAction, int (*pPredicate)(int), unsigned int nMask) {
    (void)pRowAction;
    FUN_803826e4();
    int nOff = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int pSquad = DAT_80612be4 + nOff + 0x81a8;
        int bKeep = (*pPredicate)(pSquad);
        if ((bKeep != 0) &&
            ((*(unsigned int *)(&DAT_80480f30 + *(int *)(pSquad + 4) * 0x4c) & nMask) != 0)) {
            hud_add_list_item();
            hud_add_padding_item((double)FLOAT_8061aab0);
        }
        nOff = nOff + 0xb4;
    }
    FUN_80382730();
}

// ===========================================================================
// Address: 0x80333550 | match_build_editor_list_screen
// Build the arcade custom-match editor list screen for the active mode
// (DAT_80612d08). Sets up the scroll frame (title from DAT_80480f14), then
// branches on the mode: unlocked modes (config flag bit 1 clear) enumerate the
// history entries relevant to the mode (texslot actions for mode 2, closed
// squads for mode 0xc, the squad-slot predicate walk for mode 0xf, else the
// per-mode subtitle plus matching history rows); locked modes (bit 1 set) build
// the fixed weapon/loadout rows for modes 0/1 via the per-tab builder table and
// mode 0xb's fixed subtitle. Finishes by applying the scroll frame and marking
// the screen built (+0x214 |= 4).
// ===========================================================================
void match_build_editor_list_screen(void) {
    int pElem = FUN_803826f0();
    int nTitle = get_string_by_id(*(int *)(&DAT_80480f14 + DAT_80612d08 * 0x4c));
    hud_element_init_from_layout(0xa400, nTitle, 0, 0);
    hud_set_item_padding_mode((double)FLOAT_8061aa28, 1, 1, 1);
    hud_set_context_property(0x28000);
    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
                  ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                    FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                  FLOAT_8061aa18),
        (double)FLOAT_8061aa28, (double)FLOAT_8061aa28, 0);
    if ((*(unsigned int *)(&DAT_80480f30 + DAT_80612d08 * 0x4c) & 1) == 0) {
        if (DAT_80612d08 == 2) {
            *(int *)(pElem + 0x214) = 1;
            int n = *(int *)(DAT_80612be4 + 0x280);
            while (n = n - 1, -1 < n) {
                aveditor_build_texslot_actions();
            }
        } else if (DAT_80612d08 == 0xc) {
            int n = *(int *)(DAT_80612be4 + 0xdd10) - 1;
            int nOff = n * 0x8c;
            for (; -1 < n; n = n - 1) {
                int nState = *(int *)(DAT_80612be4 + nOff + 0xdd94);
                if (((nState == 5) || (nState == 4)) || (nState == 7)) {
                    hud_add_list_item();
                }
                nOff = nOff - 0x8c;
            }
        } else if (DAT_80612d08 == 0xf) {
            match_build_squad_slot_rows((void *)match_apply_swap_history_cb,
                                        LAB_80333458, 0x20);
        } else {
            get_string_by_id(*(int *)(&DAT_80480f18 + DAT_80612d08 * 0x4c));
            hud_add_list_item();
            hud_add_callback_item((double)FLOAT_8061aa14);
            int n = *(int *)(DAT_80612be4 + 0xdd10) - 1;
            int nOff = n * 0x8c;
            for (; -1 < n; n = n - 1) {
                if (*(int *)(DAT_80612be4 + nOff + 0xdd94) == DAT_80612d08) {
                    hud_add_list_item();
                }
                nOff = nOff - 0x8c;
            }
        }
    } else {
        int nTab = *(int *)(DAT_80612be4 + 0x248);
        if (DAT_80612d08 == 0) {
            get_string_by_id(0xb21);
            hud_add_list_item();
            hud_add_callback_item((double)FLOAT_8061aa14);
            ((void (*)(int, void *))(&PTR_aveditor_build_weapon_list_a_804816c0)[nTab * 0x11])(
                DAT_80612d08, (void *)match_confirm_add_slot_action);
        } else if (DAT_80612d08 == 1) {
            get_string_by_id(0xb22);
            hud_add_list_item();
            hud_add_callback_item((double)FLOAT_8061aa14);
            ((void (*)(int, void *))(&PTR_aveditor_build_weapon_list_a_804816c0)[nTab * 0x11])(
                DAT_80612d08, (void *)match_confirm_add_slot_action);
        } else if (DAT_80612d08 == 0xb) {
            get_string_by_id(0xb23);
            hud_add_list_item();
            hud_add_callback_item((double)FLOAT_8061aa14);
            get_string_by_id(*(int *)(&DAT_80480f18 + DAT_80612d08 * 0x4c));
            hud_add_list_item();
        }
    }
    hud_element_apply_scroll();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061aa1c * FLOAT_80611a64 + FLOAT_8061aa20 * FLOAT_80611a64 +
                   FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    hud_element_finalize_frame();
    *(unsigned int *)(pElem + 0x214) = *(unsigned int *)(pElem + 0x214) | 4;
}

// ===========================================================================
// Address: 0x8033392c | match_editor_preview_tick
// Per-frame preview hook: while the screen element (pElement) is not closing,
// the active mode allows a live preview (DAT_80480f30 bit 1 set) and a slot is
// selected (+0x188 >= 0), refresh the entity preview.
// ===========================================================================
void match_editor_preview_tick(int nType, double p2, double p3, double p4,
                               int a5, int a6, int a7, int a8, int pElement) {
    (void)p2; (void)p3; (void)p4; (void)a5; (void)a6; (void)a7; (void)a8;
    int bClosing = hud_element_is_closing(pElement);
    if (((bClosing == 0) && ((*(unsigned int *)(&DAT_80480f30 + DAT_80612d08 * 0x4c) & 1) != 0)) &&
        (-1 < *(int *)(pElement + 0x188))) {
        aveditor_update_entity_preview(nType);
    }
}

// ===========================================================================
// Address: 0x80333990 | match_editor_screen_closing
// Screen close hook: when all child elements are closing and the active mode
// supports the live preview (DAT_80480f30 bit 1), tear the preview back down.
// ===========================================================================
void match_editor_screen_closing(int pScreen) {
    int bAllClosing = hud_screen_all_children_closing(pScreen);
    if ((bAllClosing != 0) && ((*(unsigned int *)(&DAT_80480f30 + DAT_80612d08 * 0x4c) & 1) != 0)) {
        FUN_8033f224();
    }
}

// ===========================================================================
// Address: 0x803339dc | match_descriptor_id_for_kind
// Map a selection descriptor (pSel) plus its config row (pRow) to a descriptor
// id. For apply-kind selections (pSel[0]==2): sub-kind 0xe loads the row via
// FUN_803356e4, otherwise the descriptor id lives at pRow+0x20. For add-kind
// (0<pSel[0]<2) the id lives at pRow+0x1c. Any other kind returns 0.
// ===========================================================================
unsigned int match_descriptor_id_for_kind(int *pSel, int pRow) {
    unsigned int uResult = 0;
    int nKind = *pSel;
    if (nKind == 2) {
        if (pSel[1] == 0xe) {
            uResult = (unsigned int)FUN_803356e4(pRow);
        } else {
            uResult = *(unsigned int *)(pRow + 0x20);
        }
    } else if ((nKind < 2) && (0 < nKind)) {
        uResult = *(unsigned int *)(pRow + 0x1c);
    }
    return uResult;
}

// ===========================================================================
// Address: 0x80333a44 | match_editor_screen_enter
// Editor screen enter/refresh. When an edit is in progress (DAT_80612cfc or
// DAT_80612cf4 set), resolve the selected block's descriptor id for the current
// leader's team tag (match_descriptor_id_for_kind over DAT_804816a8 indexed by
// the +0x41e8 tag byte); otherwise clear the "preview dirty" flag (+0x238 bit
// 0x10000). Flush pending edits (FUN_80337be8), and if the screen has unsaved
// changes (hud_element_has_unsaved_changes or FUN_803392d4) and the "warned"
// flag (+0x238 bit 0x20) is clear, raise the unsaved-changes prompt (0x16) and
// set it. Finalize (FUN_80339370) and push.
// ===========================================================================
void match_editor_screen_enter(void) {
    int pElem = FUN_803826f0();
    int *pBlock = *(int **)(DAT_80612be4 + 0x11858);
    if ((DAT_80612cfc == 0) && (DAT_80612cf4 == 0)) {
        if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x10000) != 0) {
            *(unsigned int *)(DAT_80612be4 + 0x238) =
                *(unsigned int *)(DAT_80612be4 + 0x238) & 0xfffeffff;
        }
    } else {
        match_descriptor_id_for_kind(
            pBlock,
            (int)(&DAT_804816a8 +
                  (unsigned int)*(unsigned char *)(DAT_80612be4 +
                        *(int *)(DAT_80612d00 + 0xa4) * 0x220 + 0x41e8) * 0x44));
    }
    FUN_80337be8();
    int bUnsaved = hud_element_has_unsaved_changes(pElem);
    if (((bUnsaved != 0) || (FUN_803392d4((int)pBlock) != 0)) &&
        ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0x16);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    FUN_80339370();
    hud_screen_push();
    FUN_8038273c();
}

// ===========================================================================
// Address: 0x80333b50 | match_editor_screen_rebuild
// Rebuild the currently-selected block's screen. When the block's mode-config
// flags (DAT_80480f30 indexed by block+4) lack bit 2, run the per-mode apply
// callback (PTR_FUN_80480f44[mode]) with the block, history array (+0xdd14) and
// participant array (+0x288). When the "suppress reapply" flag (+0x238 bit
// 0x800) is clear, re-run FUN_80339250 over the block. Then rebuild the list.
// ===========================================================================
void match_editor_screen_rebuild(void) {
    unsigned int pBlock = *(unsigned int *)(DAT_80612be4 + 0x11858);
    if ((*(unsigned int *)(&DAT_80480f30 + *(int *)(pBlock + 4) * 0x4c) & 2) == 0) {
        ((void (*)(unsigned int, int, int))(&PTR_FUN_80480f44)[*(int *)(pBlock + 4) * 0x13])(
            pBlock, DAT_80612be4 + 0xdd14, DAT_80612be4 + 0x288);
    }
    if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x800) == 0) {
        FUN_80339250(pBlock);
    }
    hud_screen_rebuild_items();
}

// ===========================================================================
// Address: 0x80333be4 | match_selected_title_string
// Resolve the list-screen title text id for the current selection into *pOut.
// For selection kind 0xd (character pick) the title comes from the pick table
// (&DAT_80477bc8, stride 0xc4) indexed by the selection's history-row descriptor
// (+0xdd98). Kind 3 uses the fixed title 0xb7c. Other kinds leave *pOut alone.
// ===========================================================================
void match_selected_title_string(int nUnused, int *pOut) {
    (void)nUnused;
    int nKind = *(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4);
    if (nKind == 0xd) {
        *pOut = (int)*(short *)(&DAT_80477bc8 +
            *(int *)(DAT_80612be4 +
                     *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd98) * 0xc4);
        return;
    }
    if (0xc < nKind) {
        return;
    }
    if (nKind != 3) {
        return;
    }
    *pOut = 0xb7c;
}

// ===========================================================================
// Address: 0x80333c40 | match_selection_slot_available
// Predicate: is the current selection's slot available/selectable? When the
// selection's history-row short (+0xc) is negative it is an inline
// participant-index selection (+0xe): available when that participant slot has
// no score (+0x2e0 == 0). Otherwise it is a cell selection: available when the
// row's committed flag (+0xdd90) is 0, or the cell is not already selected
// (aveditor_is_cell_selected) while the flag is 1.
// ===========================================================================
unsigned int match_selection_slot_available(void) {
    unsigned int uResult;
    int nRow = (int)*(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc);
    if (nRow < 0) {
        int nPart = (int)*(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xe);
        uResult = 0;
        if ((-1 < nPart) && (*(int *)(DAT_80612be4 + nPart * 0xe4 + 0x2e0) == 0)) {
            uResult = 1;
        }
    } else {
        int bSelected = aveditor_is_cell_selected(nRow);
        uResult = 0;
        int nFlag = *(int *)(DAT_80612be4 +
            *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd90);
        if ((nFlag == 0) || ((bSelected == 0) && (nFlag == 1))) {
            uResult = 1;
        }
    }
    return uResult;
}

// ===========================================================================
// batch_947: arcade custom-match editor palette/cell item rendering and cell
// selection action handlers. The renderers draw the currently-selected cell's
// two-line label and the palette icons; the action handlers commit a cell/entry
// selection into g_MatchSession (DAT_80612be4) and advance the editor screen
// state (+0x1185c). The selected element block is at +0x11858; the history row
// short lives at block+0xc (>=0 => cell, <0 => inline participant at block+0xe).
// Address range: 0x80333cec - 0x80334330
// ---- per-file externs new to this batch -----------------------------------
extern float FLOAT_8061aab4;                 // selection row height (available)
extern float FLOAT_8061aab8;                 // selection row height (unavailable)
extern float FLOAT_8061aabc;                 // palette icon max aspect width factor
extern unsigned int DAT_80477bcc;            // pick table: palette icon color word (stride 0xc4)
extern void hud_item_compute_colors(int a, int b, unsigned int *pFg, unsigned int *pBg);
extern int  hud_element_get_dimension(int element, unsigned int flags);
extern void text_apply_fit_color(void);      // apply the fit/measure color for the next draw
extern void text_apply_alpha(double maxWidth, unsigned char *psz, int flags);
extern int  aveditor_palette_item_texture_id(int type);
extern int  hud_item_is_focused(int item);
extern void aveditor_draw_palette_item(int mode, float *pRect, double alpha, int material,
                                       unsigned int iconColor, unsigned int countColor, int count);
extern int  aveditor_get_slot_texset(int slot);
extern unsigned int cursor_get_texture_id(int index);
extern void aveditor_set_tool_params_by_cat(void);
extern void grab_cell_record(int nCell);
extern void load_selected_entry(int nCell);

// forward declarations for peer helpers referenced before their definitions
unsigned int match_selection_kind_le13(void);
int match_selection_kind3_desc(void);

// ===========================================================================
// Address: 0x80333cec | match_selection_row_height
// List-row height callback: return the "available" row height (FLOAT_8061aab4)
// when the current selection slot is available, else the shorter unavailable
// height (FLOAT_8061aab8).
// ===========================================================================
double match_selection_row_height(void) {
    float fHeight = FLOAT_8061aab8;
    if (match_selection_slot_available() != 0) {
        fHeight = FLOAT_8061aab4;
    }
    return (double)fHeight;
}

// ===========================================================================
// Address: 0x80333d20 | match_draw_selection_labels
// Draw the two-line label for the current selection cell. Computes the title
// (local_3c) and subtitle (local_40) string ids from the selection state:
// inline participant selections use 0xb53/0xb29; cell selections use the
// select-state (0xb2c/0xb2b) and committed flag (0xb2a/0xb29). Delegates to
// match_selected_title_string for kind-specific title overrides, then renders
// the title (fit color) and, when present, the subtitle (alpha) via the text
// stack. pRect is the item rectangle {x, y, w, h}.
// ===========================================================================
void match_draw_selection_labels(int pItem, int nState, float *pRect) {
    unsigned int uFg;
    unsigned int uBg;
    int nTitle = 0;
    int nSub = 0;
    double dY = (double)pRect[1];
    double dX = (double)(FLOAT_8061aa14 * FLOAT_80611a68 + *pRect);
    double dW = (double)(-(FLOAT_8061aa2c * FLOAT_80611a68 - pRect[2]) - *pRect);
    hud_item_compute_colors(pItem, nState, &uBg, &uFg);
    int nRow = (int)*(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc);
    if (nRow < 0) {
        if ((-1 < *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xe)) &&
            (nTitle = 0xb53,
             *(int *)(DAT_80612be4 +
                      *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xe) * 0xe4 + 0x2e0) == 0)) {
            nSub = 0xb29;
        }
    } else {
        int pRowRec = DAT_80612be4 + nRow * 0x8c;
        int bSelected = aveditor_is_cell_selected(nRow);
        nTitle = 0xb2c - (unsigned int)(bSelected == 0);
        int nFlag = *(int *)(pRowRec + 0xdd90);
        if (nFlag == 1) {
            nSub = 0xb2a;
            if (bSelected != 0) {
                nSub = 0;
            }
        } else if ((nFlag < 1) && (-1 < nFlag)) {
            nSub = 0xb29;
        }
    }
    match_selected_title_string(nTitle, &nSub);
    int nFont = hud_element_get_dimension(pItem, 0);
    text_set_font_table(nFont);
    get_string_by_id(nTitle);
    text_apply_fit_color();
    unsigned char *pTitle = (unsigned char *)get_string_by_id(nTitle);
    unsigned char *pFit = text_check_scale(dW, pTitle);
    text_draw_char_shadowed(dX, dY, pFit, uBg, uFg);
    double dH = text_get_char_height(pFit);
    dY = (double)(float)(dY + dH);
    nFont = hud_element_get_dimension(pItem, 0x100);
    text_set_font_table(nFont);
    if (nSub != 0) {
        unsigned char *pSub = (unsigned char *)get_string_by_id(nSub);
        text_apply_alpha(dW, pSub, 0);
        unsigned char *pSub2 = (unsigned char *)get_string_by_id(nSub);
        text_draw_char_shadowed(dX, dY, pSub2, 0x7f503c7f, 0x14140a32);
    }
    double dGray = (double)FLOAT_8061aa90;
    text_set_color_rgb(dGray, dGray, dGray);
}

// ===========================================================================
// Address: 0x80333f34 | match_selection_kind3_desc
// For a kind-3 selection return the fixed descriptor id 0x45; otherwise (no
// selection block, or a non-kind-3 selection) return -1.
// ===========================================================================
int match_selection_kind3_desc(void) {
    if (*(int *)(DAT_80612be4 + 0x11858) == 0) {
        return -1;
    }
    if (*(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4) != 3) {
        return -1;
    }
    return 0x45;
}

// ===========================================================================
// Address: 0x80333f68 | match_draw_palette_item
// Draw a palette entry for the selection's type. Resolves the type from the
// selection's history-row descriptor (+0xdd98; when negative falls back to
// match_selection_kind3_desc), squeezes the item rectangle toward its centre if
// wider than the max aspect, then draws the icon with its placed count
// (aveditor_count_placed_for_type) and per-type color (&DAT_80477bcc). pItem is
// the focused-item handle passed in r4.
// ===========================================================================
void match_draw_palette_item(int nUnusedA, int nUnusedB, unsigned int *pRect, int pItem) {
    (void)nUnusedA; (void)nUnusedB;
    FUN_803826e8();
    int nType =
        *(int *)(DAT_80612be4 +
                 *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd98);
    if (nType < 0) {
        nType = match_selection_kind3_desc();
    }
    unsigned int local_rect[4];
    local_rect[0] = pRect[0];
    local_rect[2] = pRect[2];
    float fY0 = (float)pRect[1];
    float fY1 = (float)pRect[3];
    if (FLOAT_8061aabc * FLOAT_80611a64 < fY1 - fY0) {
        fY1 = FLOAT_8061aa40 * (fY1 - fY0) + fY0;
        float fPad = FLOAT_8061aa40 * FLOAT_8061aabc * FLOAT_80611a64;
        fY0 = fY1 - fPad;
        fY1 = fY1 + fPad;
    }
    *(float *)&local_rect[1] = fY0;
    *(float *)&local_rect[3] = fY1;
    unsigned int uCount = aveditor_count_placed_for_type(nType);
    int nTex = aveditor_palette_item_texture_id(nType);
    int nFocused = hud_item_is_focused(pItem);
    aveditor_draw_palette_item(nFocused, (float *)local_rect, 1.0, nTex,
                               *(unsigned int *)(&DAT_80477bcc + nType * 0xc4),
                               0xa0a0a20, uCount);
    FUN_80382734();
}

// ===========================================================================
// Address: 0x8033407c | match_draw_cursor_palette_item
// Palette draw variant for the cursor/participant slot: uses the participant's
// texture set (aveditor_get_slot_texset of the selection's inline index +0xe)
// and the cursor icon (cursor_get_texture_id 0x42), squeezing the rectangle the
// same way and drawing with a zero count. pItem is the focused-item handle (r4).
// ===========================================================================
void match_draw_cursor_palette_item(int nUnusedA, int nUnusedB, unsigned int *pRect, int pItem) {
    (void)nUnusedA; (void)nUnusedB;
    unsigned int local_rect[4];
    local_rect[0] = pRect[0];
    local_rect[2] = pRect[2];
    float fY0 = (float)pRect[1];
    float fY1 = (float)pRect[3];
    if (FLOAT_8061aabc * FLOAT_80611a64 < fY1 - fY0) {
        fY1 = FLOAT_8061aa40 * (fY1 - fY0) + fY0;
        float fPad = FLOAT_8061aa40 * FLOAT_8061aabc * FLOAT_80611a64;
        fY0 = fY1 - fPad;
        fY1 = fY1 + fPad;
    }
    *(float *)&local_rect[1] = fY0;
    *(float *)&local_rect[3] = fY1;
    unsigned int uTexset =
        aveditor_get_slot_texset((int)*(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xe));
    int nIcon = cursor_get_texture_id(0x42);
    int nFocused = hud_item_is_focused(pItem);
    aveditor_draw_palette_item(nFocused, (float *)local_rect, 1.0, nIcon, uTexset, 0xa0a0a20, 0);
}

// ===========================================================================
// Address: 0x80334150 | match_selection_kind_le13
// Predicate: TRUE (1) when the selected block's kind (+4) is <= 0xd, via the
// CLZ-boolean of (0xd - kind).
// ===========================================================================
unsigned int match_selection_kind_le13(void) {
    unsigned int uCLZ = countLeadingZeros(0xd - *(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4));
    return uCLZ >> 5;
}

// ===========================================================================
// Address: 0x80334170 | match_cell_select_action
// Cell "select" action. Resolves the selection's history row index. If the cell
// is not already selected, or its kind is <= 0xd (match_selection_kind_le13),
// stage a fresh tool selection: copy the row's payload (+0x84) into the tool
// param (+0x2f34), clear +0x2f3c, record the row index (+0x2f38), apply the
// per-category tool params, and set screen-state 0. Otherwise grab the existing
// cell record and set screen-state 1. Pops the screen on exit.
// ===========================================================================
void match_cell_select_action(void) {
    FUN_803826f0();
    int pRow = DAT_80612be4 + *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14;
    int nCell = (pRow - (DAT_80612be4 + 0xdd14)) / 0x8c;
    int bSelected = aveditor_is_cell_selected(nCell);
    if ((bSelected == 0) || (match_selection_kind_le13() != 0)) {
        *(int *)(DAT_80612be4 + 0x2f34) = *(int *)(pRow + 0x84);
        *(int *)(DAT_80612be4 + 0x2f3c) = 0;
        *(int *)(DAT_80612be4 + 0x2f38) = nCell;
        aveditor_set_tool_params_by_cat();
        *(int *)(DAT_80612be4 + 0x1185c) = 0;
    } else {
        grab_cell_record(nCell);
        *(int *)(DAT_80612be4 + 0x1185c) = 1;
    }
    screen_exit_clear_flag();
    FUN_8038273c();
}

// ===========================================================================
// Address: 0x80334244 | match_cell_select_apply_action
// Apply variant of the cell-select action. If the cell is not already selected,
// stage the apply tool (fixed param 0x46), record the row index and set
// screen-state 3; otherwise load the existing selected entry and set
// screen-state 4. Pops the screen on exit.
// ===========================================================================
void match_cell_select_apply_action(void) {
    int nCell = (*(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c) / 0x8c;
    int bSelected = aveditor_is_cell_selected(nCell);
    if (bSelected == 0) {
        *(int *)(DAT_80612be4 + 0x2f34) = 0x46;
        *(int *)(DAT_80612be4 + 0x2f3c) = 0;
        *(int *)(DAT_80612be4 + 0x2f38) = nCell;
        aveditor_set_tool_params_by_cat();
        *(int *)(DAT_80612be4 + 0x1185c) = 3;
    } else {
        load_selected_entry(nCell);
        *(int *)(DAT_80612be4 + 0x1185c) = 4;
    }
    screen_exit_clear_flag();
}

// ===========================================================================
// Address: 0x80334300 | match_set_screen_state_confirm
// Enter the "confirm" editor screen-state (+0x1185c = 7) and pop the screen.
// ===========================================================================
void match_set_screen_state_confirm(void) {
    *(int *)(DAT_80612be4 + 0x1185c) = 7;
    screen_exit_clear_flag();
}

// ===========================================================================
// Address: 0x80334330 | match_palette_screen_closing
// Palette screen close hook: when all child elements are closing, tear the
// entity preview back down (FUN_8033f224).
// ===========================================================================
void match_palette_screen_closing(int pScreen) {
    int bAllClosing = hud_screen_all_children_closing(pScreen);
    if (bAllClosing != 0) {
        FUN_8033f224();
    }
}
