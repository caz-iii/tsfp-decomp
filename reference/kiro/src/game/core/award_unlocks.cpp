// award_unlocks.cpp
// Award / medal unlock-tracking system. Address range 0x801c0a40 -
// 0x801c1024 (batch_586). This module decides, at the end of a match / on a
// scoring event, whether the various in-game awards (medals) have been earned
// and records the unlock into per-player stat storage.
//
// Two award-descriptor tables are indexed by award id:
//   award_descriptor_lookup   (FUN_8022d9d8): 0x38-stride records, split into
//       two regions at id 7 (ids 0..6 -> 0x80461f3c region, ids 7+ ->
//       0x80462518 region).
//   award_slot_ptr_lookup     (FUN_8022d9a4): pointer table PTR_DAT_80461ee8
//       for ids < 0x15, else &DAT_804653bc[id].
//
// The cheat flag DAT_806125f4 (UNLOCK_FEATURES cheat, armed in batch_584)
// short-circuits every earned-check to "already unlocked" (return 1) so the
// cheat instantly grants all awards. rumble_event_table_lookup (batch_583) is
// reused here as a small "condition class already satisfied" byte-table gate.
//
// Per-player award state lives in the stats block (stats_get_player_ptr /
// stats_get_combined_ptr): fields at +0xbf0..+0xc00 hold earned bitmasks and
// +0x49c holds per-award progress counters.
//
// Non-matching: the wide FP-context spill frame (FUN_803826f0 / FUN_8038273c
// and the __save_gpr_28 / __restore_gpr_28 pair Ghidra emitted for the mask
// scan) is preserved as calls so the ROM stack shape is retained. The
// feature-resolve entry (gameplay_modifier_resolve_available, batch_585) and
// the two award-getter frontier helpers are referenced via file-local externs.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {
extern int DAT_805015c0;   // global feature flags (suppress bits 0x800040)
extern int DAT_806125fc;   // resolved "available modifiers" bitmask
extern int DAT_806125f4;   // UNLOCK_FEATURES cheat flag (1 => grant all)

// Award descriptor / slot-pointer lookup tables (indexed by award id).
extern unsigned char *FUN_8022d9d8(int awardId);   // 0x38-stride descriptor ptr
extern unsigned char *FUN_8022d9a4(int awardId);   // slot pointer-table entry

// Award-record accessor: returns match-record ptr (+0x14) or NULL.
extern int *FUN_802c76b4(int param1, int param2);

// Active-player count (participant loop bound for award_set_all_players).
extern int DAT_80611014;
} // extern "C"

// Award-grant graph (defined in this file, batch_586 + batch_587).
void award_grant_dispatch(void);   // 0x801c0f6c
void award_grant_process(void);    // 0x801c1024
void award_grant_to_combined(int slotWord, int awardId, int force);  // 0x801c0f90

// stats_get_player_ptr / stats_get_combined_ptr resolve through the shared
// generated header returning void*; cast at each use site for byte arithmetic.

// Wide FP-context PPC ABI spill frame (award scan re-entry guard).
static inline unsigned long long __aw_frame_open(void)
{
    return ((unsigned long long (*)(void))0x803826f0)();
}
static inline void __aw_frame_close(void) { ((void (*)(void))0x8038273c)(); }
// Int-context variant used by award_stats_reset_all.
static inline int  __aw_frame_open_i(void)  { return ((int (*)(void))0x803826e8)(); }
static inline void __aw_frame_close_i(void) { ((void (*)(void))0x80382734)(); }
// gpr_28 save/restore pair Ghidra exposed inside the mask scan.
static inline unsigned long long __aw_save_gpr28(void)
{
    return ((unsigned long long (*)(void))0x803856f0)();  // __save_gpr_28
}

// Forward declarations (mutually-recursive award graph).
int  award_grant_generic(void);
void award_scan_all_players(void);
void award_scan_player_mask(void);

// ===========================================================================
// Address: 0x801c0a40 | Name: award_resolve_available_gated | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Resolve the available-modifier set unless the global feature flags carry the
// 0x800040 suppress bits, in which case the resolved mask is forced empty.
// ===========================================================================
void award_resolve_available_gated(void)
{
    if ((DAT_805015c0 & 0x800040) == 0) {
        gameplay_modifier_resolve_available();
    } else {
        DAT_806125fc = 0;
    }
}

// ===========================================================================
// Address: 0x801c0a88 | Name: award_stats_reset_all | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Clear all per-player award state for player 0's stats block. Seeds the
// earned/pending bitmask fields (+0xbf0, +0xbf4[3], +0xc00[3], +0xc0c[4][4])
// with the sign-extended "any-active" flag and resets the 0x30 progress
// counters (+0x49c, 0x18 stride) to 4.
// ===========================================================================
void award_stats_reset_all(void)
{
    int seed;
    unsigned char *pStats;
    int i;
    int off;
    int j;
    int koff;

    seed = __aw_frame_open_i();
    seed = -(unsigned int)(seed != 0);

    i = 0;
    off = 0;
    do {
        pStats = (unsigned char *)stats_get_player_ptr(0);
        *(int *)(pStats + off + 0xbf4) = seed;
        pStats = (unsigned char *)stats_get_player_ptr(0);
        i = i + 1;
        *(int *)(pStats + off + 0xc00) = seed;
        off = off + 4;
    } while (i < 3);

    i = 0;
    off = 0;
    do {
        j = 0;
        koff = 0;
        do {
            pStats = (unsigned char *)stats_get_player_ptr(0);
            j = j + 1;
            int fieldOff = koff + 0xc0c;
            koff = koff + 4;
            *(int *)(pStats + off + fieldOff) = seed;
        } while (j < 4);
        i = i + 1;
        off = off + 0x10;
    } while (i < 4);

    i = 0;
    off = 0;
    do {
        pStats = (unsigned char *)stats_get_player_ptr(0);
        i = i + 1;
        *(int *)(pStats + off + 0x49c) = 4;
        off = off + 0x18;
    } while (i < 0x30);

    pStats = (unsigned char *)stats_get_player_ptr(0);
    *(int *)(pStats + 0xbf0) = seed;
    __aw_frame_close_i();
}

// ===========================================================================
// Address: 0x801c0b78 | Name: award_check_map_completion | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award check keyed on a match record (param1/param2). Returns 0 if there is
// no record or the record's first word is zero; otherwise gates on award
// class 4 (rumble_event_table_lookup) - already-satisfied => 1, else runs the
// generic grant path.
// ===========================================================================
unsigned int award_check_map_completion(int param1, int param2)
{
    int *pRecord;
    unsigned int result;
    int satisfied;

    pRecord = FUN_802c76b4(param1, param2);
    if ((pRecord == (int *)0) || (*pRecord == 0)) {
        result = 0;
    } else {
        satisfied = rumble_event_table_lookup(4);
        if (satisfied == 0) {
            result = (unsigned int)award_grant_generic();
        } else {
            result = 1;
        }
    }
    return result;
}

// ===========================================================================
// Address: 0x801c0bdc | Name: award_validate_id_range | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award-id range validator. In the original both branches return 1 (the range
// check is present but the failure path was folded to the same result).
// ===========================================================================
unsigned int award_validate_id_range(int awardId)
{
    if ((awardId != 0) && (awardId < 0xb)) {
        return 1;
    }
    return 1;
}

// ===========================================================================
// Address: 0x801c0bfc | Name: award_grant_generic | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Thin wrapper that runs the shared award-grant dispatcher (FUN_801c0f6c).
// ===========================================================================
int award_grant_generic(void)
{
    award_grant_dispatch();
    return 0;
}

// ===========================================================================
// Address: 0x801c0c30 | Name: award_check_slot1 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award check for class 1. Already-satisfied (byte table) => 1. A zero
// argument grants immediately (1); otherwise runs the mask-scan grant path.
// ===========================================================================
unsigned int award_check_slot1(int param1)
{
    int satisfied;
    unsigned int result;

    satisfied = rumble_event_table_lookup(1);
    if (satisfied == 0) {
        if (param1 == 0) {
            result = 1;
        } else {
            award_grant_process();
            result = 0;
        }
    } else {
        result = 1;
    }
    return result;
}

// ===========================================================================
// Address: 0x801c0cb4 | Name: award_check_slot2 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award check for class 2. Already-satisfied => 1, else generic grant path.
// ===========================================================================
unsigned int award_check_slot2(void)
{
    int satisfied;
    unsigned int result;

    satisfied = rumble_event_table_lookup(2);
    if (satisfied == 0) {
        result = (unsigned int)award_grant_generic();
    } else {
        result = 1;
    }
    return result;
}

// ===========================================================================
// Address: 0x801c0d0c | Name: award_scan_player_mask | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Scan the combined-stats earned bitmask for a single award. The award id/bit
// arrives in the gpr_28 spill frame (upper word = word index, lower word =
// -1 sentinel meaning "scan all 4 mask words"). When active, walks the four
// 0x10-stride mask words at +0xbfc and stops at the first set bit.
// ===========================================================================
void award_scan_player_mask(void)
{
    int wordIndex;
    int signExt;
    int slot;
    int off;
    int *pStats;
    unsigned long long frame;

    frame = __aw_save_gpr28();
    wordIndex = (int)((unsigned long long)frame >> 0x20);
    signExt = wordIndex >> 0x1f;

    if ((int)frame == -1) {
        slot = 1;
        off = 0x10;
        do {
            pStats = (int *)stats_get_combined_ptr();
            int bit = (signExt * 0x20 |
                       (unsigned int)(wordIndex * 0x8000000 + signExt) >> 0x1b) - signExt;
            unsigned int maskWord =
                *(unsigned int *)((signExt >> 5) * 4 + (int)pStats + off + 0xbfc);
            if ((1U << bit & maskWord) != 0) break;
            slot = slot + 1;
            off = off + 0x10;
        } while (slot < 5);
    } else {
        stats_get_combined_ptr();
    }
    // __restore_gpr_28 tail (frame teardown) is implicit in the ROM.
}

// ===========================================================================
// Address: 0x801c0dd8 | Name: award_scan_all_players | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Iterate the award descriptor's player-count field and run the per-player
// mask scan once per participant.
// ===========================================================================
void award_scan_all_players(void)
{
    int awardId;
    int pDesc;
    int i;

    awardId = __aw_frame_open_i();
    pDesc = (int)FUN_8022d9d8(awardId);
    for (i = 0; i < *(int *)(pDesc + 4); i = i + 1) {
        award_scan_player_mask();
    }
    __aw_frame_close_i();
}

// ===========================================================================
// Address: 0x801c0e54 | Name: award_check_by_id | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award check keyed on an award id. Looks up the descriptor, then gates on
// class 3 (ids < 7) or class 2 (ids >= 7). Already-satisfied => 1, else runs
// the generic grant path.
// ===========================================================================
unsigned int award_check_by_id(int awardId)
{
    int satisfied;
    unsigned int result;

    FUN_8022d9d8(awardId);
    if (awardId < 7) {
        satisfied = rumble_event_table_lookup(3);
        if (satisfied != 0) {
            return 1;
        }
    } else {
        satisfied = rumble_event_table_lookup(2);
        if (satisfied != 0) {
            return 1;
        }
    }
    result = (unsigned int)award_grant_generic();
    return result;
}

// ===========================================================================
// Address: 0x801c0ec4 | Name: award_check_slot_by_ptr | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award check keyed on a (slotId, awardId) pair. Cheat forces 1. Gates on
// class 3 (slotId < 0x15) or class 2 (slotId >= 0x15); if the class byte-table
// gate already reports satisfied returns 1. Otherwise runs the id-keyed award
// check; on success resolves the slot pointer (FUN_8022d9a4) and grants.
// ===========================================================================
unsigned int award_check_slot_by_ptr(int slotId, int awardId)
{
    unsigned int result;
    int satisfied;

    if (DAT_806125f4 == 0) {
        if (slotId < 0x15) {
            satisfied = rumble_event_table_lookup(3);
            if (satisfied != 0) {
                return 1;
            }
        } else {
            satisfied = rumble_event_table_lookup(2);
            if (satisfied != 0) {
                return 1;
            }
        }
        satisfied = (int)award_check_by_id(awardId);
        if (satisfied == 0) {
            result = 0;
        } else {
            FUN_8022d9a4(slotId);
            award_grant_dispatch();
            result = 0;
        }
    } else {
        result = 1;
    }
    return result;
}

// ===========================================================================
// Address: 0x801c0f6c | Name: award_grant_dispatch | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Generic award-grant entry: forwards to the request-type processor.
// ===========================================================================
void award_grant_dispatch(void)
{
    award_grant_process();
}

// ===========================================================================
// Address: 0x801c0f90 | Name: award_grant_to_combined | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Grant one award into the combined-stats earned bitmask. The award word
// index / bit arrives via the FP-context spill frame. When force is set, or
// the bit is not yet present in the +0xc00 mask word, the award is recorded
// (the ROM re-reads the combined ptr to commit). slotWord/awardId carry the
// descriptor operands for the record path.
// ===========================================================================
void award_grant_to_combined(int slotWord, int awardId, int force)
{
    unsigned long long frame;
    int iCombined;

    (void)slotWord;
    (void)awardId;
    frame = __aw_frame_open();
    if ((force != 0) ||
        (iCombined = (int)stats_get_combined_ptr(),
         (1 << (int)((unsigned long long)frame >> 0x20) &
          *(unsigned int *)(iCombined + (int)frame * 4 + 0xc00)) == 0)) {
        stats_get_combined_ptr();
    }
    __aw_frame_close();
}

// ===========================================================================
// Address: 0x801c1024 | Name: award_grant_process | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award-request processor. The request descriptor arrives via the FP-context
// spill frame: request[0] = request type, request[1..2] = operands,
// request[3] = optional id-list pointer (-1 terminated). Only runs for an
// active player-0 slot and when either the type is 0 or the UNLOCK_FEATURES
// cheat is off. Dispatches by type:
//   0/1  no-op
//   2    grant explicit (slotWord, awardId) into combined mask
//   3    grant fixed slot 0xc with awardId operand
//   4    scan all players for one award
//   5    scan a single player mask
//   6    scan all players x7 (multi-award batch)
//   7    walk a -1-terminated award-id list, scanning all players per id
//   8    match operand against descriptors 7..15, scan on match
// ===========================================================================
void award_grant_process(void)
{
    int *pReq;
    BOOL active;
    int *pDesc;
    int listVal;
    int i;

    pReq = (int *)__aw_frame_open();
    active = player_slot_is_active(0);
    if (active && ((*pReq == 0) || (DAT_806125f4 == 0))) {
        switch (*pReq) {
        case 0:
            break;
        case 1:
            break;
        case 2:
            award_grant_to_combined(pReq[1], pReq[2], 0);
            break;
        case 3:
            award_grant_to_combined(0xc, pReq[2], 0);
            break;
        case 4:
            award_scan_all_players();
            break;
        case 5:
            award_scan_player_mask();
            break;
        case 6:
            i = 0;
            do {
                award_scan_all_players();
                i = i + 1;
            } while (i < 7);
            break;
        case 7:
            i = 0;
            if ((int *)pReq[3] != (int *)0) {
                listVal = *(int *)pReq[3];
                while (listVal != -1) {
                    award_scan_all_players();
                    i = i + 4;
                    listVal = *(int *)(pReq[3] + i);
                }
            }
            break;
        case 8:
            i = 0;
            do {
                pDesc = (int *)FUN_8022d9d8(i + 7);
                if (pReq[1] == *pDesc) {
                    award_scan_all_players();
                    break;
                }
                i = i + 1;
            } while (i < 9);
            break;
        }
    }
    __aw_frame_close();
}

// ===========================================================================
// Address: 0x801c11ec | Name: award_is_unlocked | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Test whether award bit `awardBit` is set in the combined-stats earned mask
// (+0xbf0). The UNLOCK_FEATURES cheat forces the result to true.
// ===========================================================================
bool award_is_unlocked(int awardBit)
{
    char result;
    int iCombined;

    if (DAT_806125f4 == 0) {
        iCombined = (int)stats_get_combined_ptr();
        result = (char)('\x01' - ((1 << awardBit & *(unsigned int *)(iCombined + 0xbf0)) == 0));
    } else {
        result = '\x01';
    }
    return (bool)result;
}

// ===========================================================================
// Address: 0x801c1244 | Name: award_set_all_players | Date: 2026-09-07
// ---------------------------------------------------------------------------
// OR award bit `awardBit` into the earned mask (+0xbf0) of every active
// player (DAT_80611014 participants).
// ===========================================================================
void award_set_all_players(int awardBit)
{
    unsigned char *pStats;
    int i;

    for (i = 0; i < DAT_80611014; i = i + 1) {
        pStats = (unsigned char *)stats_get_player_ptr(i);
        *(unsigned int *)(pStats + 0xbf0) =
            *(unsigned int *)(pStats + 0xbf0) | 1 << awardBit;
    }
}

// ===========================================================================
// Address: 0x801c12a4 | Name: award_grant_wrapper1 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Thin award-grant wrapper (dispatch entry variant).
// ===========================================================================
void award_grant_wrapper1(void)
{
    award_grant_dispatch();
}

// ===========================================================================
// Address: 0x801c12d4 | Name: award_stub_true1 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award-check stub: always reports unlocked/eligible (returns 1).
// ===========================================================================
unsigned int award_stub_true1(void)
{
    return 1;
}

// ===========================================================================
// Address: 0x801c12dc | Name: award_stub_true2 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award-check stub: always reports unlocked/eligible (returns 1).
// ===========================================================================
unsigned int award_stub_true2(void)
{
    return 1;
}

// ===========================================================================
// Address: 0x801c12e4 | Name: award_grant_wrapper2 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Thin award-grant wrapper (per-active-slot side-effect variant; called x5 for
// gameplay-modifier slot 0xc per gameplay_modifiers batch_585).
// ===========================================================================
void award_grant_wrapper2(void)
{
    award_grant_dispatch();
}

// Award-modifier descriptor table (0x1c stride); record[1] (+4) = award enable
// flag for the modifier slot. Shared with gameplay_modifiers (DAT_8044ee18).
extern "C" {
extern int DAT_8044ee1c;      // = DAT_8044ee18 + 4 (record[1] of slot 0)
extern short DAT_803f291a;    // award name string-id table (0x22 stride, halfwords)
extern unsigned char DAT_80610d00;  // scratch text buffer used as %s arg
}

// ===========================================================================
// Address: 0x801c1314 | Name: award_modifier_check_slot | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Award check for a gameplay-modifier slot. Returns 0 if the slot's award
// enable flag (descriptor record[1], 0x1c stride) is clear. Otherwise gates on
// class 5 (rumble_event_table_lookup); already-satisfied => 1, else grants.
// Called x16 by gameplay_modifier_init_all / apply_all (batch_585).
// ===========================================================================
unsigned int award_modifier_check_slot(int slot)
{
    unsigned int result;
    int satisfied;

    if (*(int *)(&DAT_8044ee1c + slot * 0x1c) == 0) {
        result = 0;
    } else {
        satisfied = rumble_event_table_lookup(5);
        if (satisfied == 0) {
            award_grant_dispatch();
            result = 0;
        } else {
            result = 1;
        }
    }
    return result;
}

// ===========================================================================
// Address: 0x801c1380 | Name: award_notify_format_string | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Build the on-screen award/medal notification string for the current award
// request. The request descriptor arrives via the FP-context spill frame:
// req[0] = request type, req[1] = award/name id, req[2] = tier/count,
// req[4] = suppress flag. Selects a template string-id per request type and
// tier, resolves the award name (uppercased), and runs the shared varargs
// formatter into the scratch buffer (DAT_80610d00). Types 0/1/7 produce no
// notification.
// ===========================================================================
void award_notify_format_string(unsigned long long param_1, double param_2, double param_3,
                                double param_4, double param_5, double param_6, double param_7,
                                unsigned long long param_8)
{
    int templateId;
    int *pReq;
    short *pName;
    unsigned int nameStr;
    unsigned char *upperName;
    int subType;
    unsigned long long acc;

    (void)param_1;
    acc = __aw_frame_open();
    pReq = (int *)(int)(acc >> 0x20);

    switch (*pReq) {
    case 0:
    case 1:
        break;
    case 2:
        if (pReq[4] == 0) {
            subType = pReq[2];
            if (subType == 1) {
                templateId = 0x1624;
            } else if ((subType < 1) || (2 < subType)) {
                templateId = 0x1623;
            } else {
                templateId = 0x1625;
            }
            nameStr = get_string_by_id((int)(short)(&DAT_803f291a)[pReq[1] * 0x22]);
            upperName = text_string_to_upper(nameStr);
            templateId = get_string_by_id(templateId);
            format_string_varargs(acc, param_2, param_3, param_4, param_5, param_6, param_7,
                                  param_8, (int *)&DAT_80610d00, templateId,
                                  (void *)&DAT_80610d00, upperName, 0, 0, 0, 0);
        }
        break;
    case 3:
        if (pReq[4] == 0) {
            subType = pReq[2];
            if (subType == 1) {
                get_string_by_id(0x1628);
            } else if ((subType < 1) || (2 < subType)) {
                get_string_by_id(0x1627);
            } else {
                get_string_by_id(0x1629);
            }
        } else {
            get_string_by_id(0x162a);
        }
        break;
    case 4: {
        int pDesc = (int)FUN_8022d9d8(pReq[1]);
        templateId = 0x1618;
        if (pReq[1] < 7) {
            templateId = 0x1614;
        }
        nameStr = get_string_by_id(*(int *)(pDesc + 0x1c));
        upperName = text_string_to_upper(nameStr);
        templateId = get_string_by_id(templateId);
        format_string_varargs(acc, param_2, param_3, param_4, param_5, param_6, param_7,
                              param_8, (int *)&DAT_80610d00, templateId,
                              (void *)&DAT_80610d00, upperName, 0, 0, 0, 0);
        break;
    }
    case 5:
        pName = (short *)FUN_8022d9a4(pReq[1]);
        nameStr = get_string_by_id((int)*pName);
        if (pReq[1] < 0x15) {
            subType = pReq[2];
            if (subType == 2) {
                templateId = 0x1612;
            } else if ((subType < 2) || (3 < subType)) {
                templateId = 0x1611;
            } else {
                templateId = 0x1613;
            }
        } else {
            subType = pReq[2];
            if (subType == 2) {
                templateId = 0x1616;
            } else if ((subType < 2) || (3 < subType)) {
                templateId = 0x1615;
            } else {
                templateId = 0x1617;
            }
        }
        upperName = text_string_to_upper(nameStr);
        templateId = get_string_by_id(templateId);
        format_string_varargs(acc, param_2, param_3, param_4, param_5, param_6, param_7,
                              param_8, (int *)&DAT_80610d00, templateId,
                              (void *)&DAT_80610d00, upperName, 0, 0, 0, 0);
        break;
    case 6:
        subType = pReq[2];
        if (subType == 2) {
            get_string_by_id(0x162c);
        } else if ((subType < 2) || (3 < subType)) {
            get_string_by_id(0x162b);
        } else {
            get_string_by_id(0x162d);
        }
        break;
    case 7:
        break;
    case 8: {
        int tierBase;
        subType = pReq[2];
        templateId = 0;
        if (subType == 2) {
            tierBase = 0x161d; // id==1
        } else if ((subType < 2) || (3 < subType)) {
            tierBase = 0x161c;
        } else {
            tierBase = 0x161e;
        }
        // Resolve within the tier group by award id (req[1]): -0/+1/+2.
        subType = pReq[1];
        if (subType == 1) {
            templateId = tierBase;
        } else if (subType < 1) {
            if (-1 < subType) {
                templateId = tierBase - 3;
            }
        } else if (subType < 3) {
            templateId = tierBase + 3;
        }
        get_string_by_id(templateId);
        break;
    }
    }
    __aw_frame_close();
}
