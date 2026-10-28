// award_scoreboard_lists.cpp
// Award-screen / scoreboard string dispatch and roster-list helpers.
//
// These functions operate on two global contexts:
//   DAT_80612db0 : award-screen context block. Holds map-name/string ids
//                  (offsets 0x90/0xa4/0xa8), and two team collections
//                  (count/array pairs at 0x94/0x98 and 0x9c/0xa0).
//   DAT_80611cd0 : base of the active player/roster array (stride 0xd8).
//   DAT_80611cd4 : number of entries in that player array.
//   DAT_805015bd : current game-mode selector byte (3 == story/co-op path,
//                  10 == a team/objective mode, else generic).
//
// Each raw symbol is declared as a per-file extern matching its call-site
// signature (Compile-Clean Standard rule 5).

#include "common.h"
#include "hud.h"

// ---- per-file externs for raw symbols referenced below ----
extern unsigned char DAT_805015bd;       // game-mode selector byte
extern int           DAT_80612db0;       // award-screen context base (as int addr)
extern int           DAT_80611cd0;       // player/roster array base (as int addr)
extern int           DAT_80611cd4;       // player/roster array count
extern int*          PTR_DAT_80477bd0;   // pointer table used for slot threshold lookup
extern int           DAT_805c7c08;       // slot descriptor table base

extern int  FUN_80350540(unsigned long long, unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long);
extern int  get_string_by_id(unsigned int id);
extern unsigned short award_pick_string_variant(int entry);
extern unsigned long long FUN_803826dc(void);  // acquire scratch cursor pair
extern void FUN_80382728(void);                // release scratch cursor
extern unsigned long long FUN_803826f0(void);  // acquire output-list cursor
extern void FUN_8038273c(void);                // release output-list cursor
extern void FUN_8038b510(int* dst, int* src);  // copy model/skeleton record

// Award map-name / caption dispatch. In story/co-op mode (3) the string comes
// from the localized dispatcher; otherwise it resolves a plain string id.
void award_string_or_map_name(unsigned long long a1, unsigned long long a2,
                              unsigned long long a3, unsigned long long a4,
                              unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8,
                              int stringId)
{
    if (DAT_805015bd == 3) {
        FUN_80350540(a1, a2, a3, a4, a5, a6, a7, a8);
    } else {
        get_string_by_id(stringId);
    }
}

// Identical dispatch shape used by a second award caption slot.
void award_string_or_caption(unsigned long long a1, unsigned long long a2,
                             unsigned long long a3, unsigned long long a4,
                             unsigned long long a5, unsigned long long a6,
                             unsigned long long a7, unsigned long long a8,
                             int stringId)
{
    if (DAT_805015bd == 3) {
        FUN_80350540(a1, a2, a3, a4, a5, a6, a7, a8);
    } else {
        get_string_by_id(stringId);
    }
}

// Mode-aware award string picker. Mode 10 returns a pointer into the per-entry
// award table; mode 3 uses the localized dispatcher; otherwise a variant id is
// chosen and resolved to a string.
int award_pick_string_by_mode(unsigned long long a1, unsigned long long a2,
                              unsigned long long a3, unsigned long long a4,
                              unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8,
                              int entry)
{
    int result;
    if (DAT_805015bd == 10) {
        result = DAT_80612db0 + (unsigned int)(*(unsigned short*)(entry + 2)) * 0x8c + 0xb8;
    } else if (DAT_805015bd < 10 && DAT_805015bd == 3) {
        result = FUN_80350540(a1, a2, a3, a4, a5, a6, a7, a8);
    } else {
        unsigned short variant = award_pick_string_variant(entry);
        result = get_string_by_id((unsigned int)variant);
    }
    return result;
}

// Award-context scalar accessors.
unsigned int award_ctx_get_field_90(void) { return *(unsigned int*)(DAT_80612db0 + 0x90); }
unsigned int award_ctx_get_field_a4(void) { return *(unsigned int*)(DAT_80612db0 + 0xa4); }
unsigned int award_ctx_get_field_a8(void) { return *(unsigned int*)(DAT_80612db0 + 0xa8); }

// Build the two per-team display lists from the award context. Each source
// collection is a (count, array) pair with 0x10-byte records; records whose
// flags (offset 8) have bit 2 set are copied into the caller's model list
// (0x20-byte stride) and their id (offset 4) into the caller's id list.
void award_build_team_lists(unsigned int /*unused1*/, unsigned int /*unused2*/,
                            int team0Models, int team1Models,
                            int team0Ids, int team1Ids)
{
    unsigned long long cursors = FUN_803826dc();
    int* team1Count = (int*)((unsigned long long)cursors >> 0x20);
    int* team0Count = (int*)cursors;

    int idx = 0;
    int off = 0;
    while (idx < *(int*)(DAT_80612db0 + 0x94)) {
        int rec = *(int*)(DAT_80612db0 + 0x98) + off;
        if ((*(unsigned int*)(rec + 8) & 4) != 0) {
            FUN_8038b510((int*)(team1Models + *team1Count * 0x20), *(int**)(rec + 0xc));
            *(unsigned int*)(team1Ids + *team1Count * 4) =
                *(unsigned int*)(*(int*)(DAT_80612db0 + 0x98) + off + 4);
            *team1Count = *team1Count + 1;
        }
        idx++;
        off += 0x10;
    }

    idx = 0;
    off = 0;
    while (idx < *(int*)(DAT_80612db0 + 0x9c)) {
        int rec = *(int*)(DAT_80612db0 + 0xa0) + off;
        if ((*(unsigned int*)(rec + 8) & 4) != 0) {
            FUN_8038b510((int*)(team0Models + *team0Count * 0x20), *(int**)(rec + 0xc));
            *(unsigned int*)(team0Ids + *team0Count * 4) =
                *(unsigned int*)(*(int*)(DAT_80612db0 + 0xa0) + off + 4);
            *team0Count = *team0Count + 1;
        }
        idx++;
        off += 0x10;
    }

    FUN_80382728();
}

// Returns 1 when the slot's tabled threshold is >= the supplied value, else 0.
// Uses countLeadingZeros on the signed difference (sign bit -> bit 31 -> >>5).
unsigned int award_slot_threshold_ge(int slot, int value)
{
    unsigned int clz = countLeadingZeros(
        ((int*)(&PTR_DAT_80477bd0))[*(int*)(DAT_805c7c08 + slot * 0x8c + 0x84) * 0x31] - value);
    return clz >> 5;
}

// Collect pointers to player records that match a team/slot filter into the
// caller-provided output list. matchTeam is the record's team field (offset
// 5 words), matchSlot its slot field (offset 3 words). includeInactive != 0
// relaxes the active-flag requirement.
void award_collect_matching_players(unsigned int /*unused1*/, unsigned int /*unused2*/,
                                    int includeInactive, int matchSlot)
{
    unsigned long long out = FUN_803826f0();
    int outOff = 0;
    int idx = 0;
    int recOff = 0;
    while (idx < DAT_80611cd4) {
        bool storyVisible = false;
        int* rec = (int*)(DAT_80611cd0 + recOff);
        if ((*(unsigned int*)rec[6] & 0x100) != 0 && DAT_805015bd == 3) {
            storyVisible = true;
        }
        if ((DAT_805015bd != 3 || storyVisible) &&
            (((*(unsigned int*)(*rec + 0x28) & 2) != 0) || includeInactive != 0) &&
            (rec[5] == (int)((unsigned long long)out >> 0x20) && rec[3] == matchSlot)) {
            if ((int)out != 0) {
                *(int**)((int)out + outOff) = rec;
            }
            outOff += 4;
        }
        idx++;
        recOff += 0xd8;
    }
    FUN_8038273c();
}

// Counts active player records matching the given team and slot in a team mode
// (mode 10) or the story-visible subset in mode 3.
int award_count_matching_players(int matchTeam, int matchSlot)
{
    int count = 0;
    int recOff = 0;
    int remaining = DAT_80611cd4;
    if (DAT_80611cd4 > 0) {
        do {
            int* rec = (int*)(DAT_80611cd0 + recOff);
            bool storyVisible = false;
            if ((*(unsigned int*)rec[6] & 0x100) != 0 && DAT_805015bd == 3) {
                storyVisible = true;
            }
            if ((DAT_805015bd == 10 || storyVisible) &&
                ((*(unsigned int*)(*rec + 0x28) & 2) == 0)) {
                if (rec[5] == matchTeam && rec[3] == matchSlot) {
                    count++;
                }
            }
            recOff += 0xd8;
            remaining--;
        } while (remaining != 0);
    }
    return count;
}

// ---------------------------------------------------------------------------
// batch_985 additions: award objective-slot processing.
// ---------------------------------------------------------------------------

extern int   DAT_80612610;   // player count for the active award/objective mode
extern int   DAT_80612614;   // active level id (for level_get_info_cached)
extern int   DAT_80611d78;   // HUD text-slot array base (stride 0x141c)
extern int   DAT_80611b8c;   // pickup descriptor table base
extern int   DAT_80611b94;   // weapon descriptor table base
extern int   DAT_80611b98;   // gadget descriptor table base
extern int   DAT_80611c14;   // pickup descriptor table count
extern int   s__d__s__s_80481ff0;  // format string "%d %s %s"

extern unsigned int FUN_8034fe04(int slot);                 // slot-eligibility test
extern void  player_ai_list_remove_by_id(int listBase, int a, int b);
extern int*  spawn_get_model_def(int a, int b);
extern void* level_get_info_cached(int levelId);  // canonical: returns level-info ptr
extern int   debug_log_formatted(unsigned long long, unsigned long long, unsigned long long,
                                 unsigned long long, unsigned long long, unsigned long long,
                                 unsigned long long, unsigned long long, int fmt,
                                 int, int, int, int, int, int, int);
extern int   hud_text_submit(unsigned int slot, int mode, unsigned int* text);

// Apply an award/objective slot removal for the current player. In story mode
// (3) the slot is marked handled and the player's AI target list is cleaned.
void award_apply_slot_removal(int playerRec, int slotRec, int outRec)
{
    int slotAi = *(int*)(slotRec + 0x7c);
    int playerAi = *(int*)(playerRec + 0x7c);
    unsigned int eligible = FUN_8034fe04(slotRec);
    if (eligible != 0) {
        if (DAT_805015bd == 3) {
            *(unsigned int*)(outRec + 0x20) = 1;
            if (slotAi == 0 || DAT_80612610 <= *(int*)(slotAi + 8)) {
                *(unsigned int*)(playerRec + 0x28) = *(unsigned int*)(playerRec + 0x28) & 0xfffffffc;
            }
            player_ai_list_remove_by_id(slotAi + 0x304,
                                        *(int*)(playerAi + 0xc), *(int*)(playerAi + 0x14));
        }
        *(unsigned char*)(outRec + 0xc) = *(unsigned char*)(outRec + 0xc) | 0x10;
    }
}

// Render an award/objective slot result line into the HUD text slots when the
// level's award flag is set. Removes the player from the AI target list and,
// for each player in the mode, formats a "count name label" line.
void award_render_slot_result(unsigned long long a1, unsigned long long a2,
                              unsigned long long a3, unsigned long long a4,
                              unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8,
                              int playerRec, int slotRec, int outRec,
                              int a12, int a13, int a14, int a15, int a16)
{
    int playerAi = *(int*)(playerRec + 0x7c);
    int* modelDef = spawn_get_model_def(*(int*)(playerAi + 0xc), *(int*)(playerAi + 0x14));
    int levelInfo = (int)level_get_info_cached(DAT_80612614);
    int slotIndex = *(int*)(outRec + 8);
    int slotTable = *(int*)(levelInfo + 0x128);
    unsigned int eligible = FUN_8034fe04(slotRec);
    if (eligible != 0) {
        if (DAT_805015bd == 3) {
            *(unsigned int*)(outRec + 0x20) = 1;
        }
        *(unsigned int*)(playerRec + 0x28) = *(unsigned int*)(playerRec + 0x28) & 0xfffffffc;
        player_ai_list_remove_by_id(*(int*)(slotRec + 0x7c) + 0x304,
                                    *(int*)(playerAi + 0xc), *(int*)(playerAi + 0x14));
        if (*(int*)(slotTable + slotIndex * 0x8c + 0x24) != 0) {
            int count = award_count_matching_players(*(int*)(playerAi + 0x14),
                                                     *(int*)(playerAi + 0xc));
            int textOff = 0;
            for (int i = 0; i < DAT_80612610; i++) {
                int labelStr = get_string_by_id(0x1607);
                int nameStr = get_string_by_id((unsigned int)*modelDef);
                unsigned int* line = (unsigned int*)debug_log_formatted(
                    a1, a2, a3, a4, a5, a6, a7, a8, s__d__s__s_80481ff0,
                    count, nameStr, labelStr, a13, a14, a15, a16);
                hud_text_submit(*(unsigned int*)(DAT_80611d78 + textOff + 4), 2, line);
                textOff += 0x141c;
            }
        }
    }
}

// Iterate the award context's objective-slot list (count at +0x80, array at
// +0x84, stride 0x28). Type-1 slots matching the resolved pickup/weapon id are
// removed; type-0xd slots whose threshold is met render a result line.
void award_process_objective_slots(unsigned long long a1, unsigned long long a2,
                                   unsigned long long a3, unsigned long long a4,
                                   unsigned long long a5, unsigned long long a6,
                                   unsigned long long a7, unsigned long long a8)
{
    int playerRec = (int)a1;  // GPR r25 save carries the player record pointer
    int playerAi = 0;
    if (playerRec != 0 && *(int*)(playerRec + 0x7c) != 0) {
        playerAi = *(int*)(playerRec + 0x7c);
    }
    if (playerAi == 0) {
        return;
    }

    int typeId = *(int*)(playerAi + 0x3c);
    int resolvedId;
    if (typeId > 7999 && typeId < 0x209e) {
        resolvedId = *(int*)(DAT_80611b94 + typeId * 0x20 + -0x3e800);
    } else if (typeId > 0x209d && typeId < 0x20b2) {
        resolvedId = *(int*)(DAT_80611b98 + typeId * 0x20 + -0x413c0);
    } else if (typeId >= 0 && typeId < DAT_80611c14) {
        resolvedId = *(int*)(DAT_80611b8c + typeId * 0x20);
    } else {
        resolvedId = -1;
    }

    int off = 0;
    for (int i = 0; i < *(int*)(DAT_80612db0 + 0x80); i++) {
        int slot = *(int*)(DAT_80612db0 + 0x84) + off + 4;
        if (*(int*)(slot + 4) == 1 && *(int*)(slot + 0x14) == resolvedId) {
            award_apply_slot_removal(playerRec, slot, (int)a1);
        } else if (*(int*)(slot + 4) == 0xd &&
                   award_slot_threshold_ge(*(int*)(slot + 8), *(int*)(playerAi + 0x14)) != 0) {
            award_render_slot_result(a1, a2, a3, a4, a5, a6, a7, a8,
                                     playerRec, slot, (int)a1, 0, 0, 0, 0, 0);
        }
        off += 0x28;
    }
}
