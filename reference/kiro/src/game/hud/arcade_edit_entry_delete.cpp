// Batch: batch_955 | Module: arcade match-editor entry deletion + index compaction
// Address range: 0x80337e94 - 0x80338498
// Date: 2026-09-12
//
// Continuation of the arcade custom-match editor. When cells/entries are removed the editor must
// (a) delete the flagged squad entries, (b) renumber every reference to cell indices at or above the
// removed range, and (c) drop descriptors that become empty. This slice implements that machinery:
//
//   match_shift_node_indices        0x80337e94  shift render-list node cell indices (+4) down.
//   match_shift_group_indices       0x80337ed4  shift the group index table (+0x3070) down.
//   match_delete_entries_in_range   0x80337f18  delete flagged entries in an index range + renumber.
//   match_find_flagged_unselected   0x80337fb8  find the first flagged, unselected entry of a kind.
//   aveditor_next_used_id           0x80338060  find the next in-use cell/roster id from a start.
//   match_delete_flagged_unselected 0x803380f0  delete all flagged, unselected entries.
//   match_delete_flagged_desc_child 0x80338190  delete a flagged descriptor's active child entry.
//   match_deactivate_flagged_descs  0x80338290  deactivate + delete flagged empty descriptors.
//   match_compact_entries           0x80338378  top-level compaction: find gaps, delete, renumber.
//   match_find_flagged_by_id        0x80338498  find a flagged entry by cell id (+0x81b4).
//
// Raw FUN_/DAT_ symbols get correctly-typed per-file externs; already-named helpers are re-declared.

#include "common.h"

extern "C" {

extern int  DAT_80612be4;    // editor context base
extern int  DAT_80612c14;    // render list head (linked mode list)

// save/restore GPR pairs used to pass packed (lo,hi) index arguments.
extern unsigned long long FUN_803826e4(void);
extern void               FUN_80382730(void);
extern unsigned long long FUN_803826f0(void);
extern void               FUN_8038273c(void);
extern int                __save_gpr_28(void);
extern void               __restore_gpr_28(void);

// named helpers from prior batches / modules
extern int  match_find_entry_by_index(int index);     // batch_954
extern void match_entry_clear_field_a8(int entry);    // batch_954
extern void match_entry_deactivate(int *entry);       // batch_954
extern int  match_squad_entry_flag_set(int entry);
extern void match_delete_entry(void);
extern int  aveditor_is_cell_selected(int index);
extern int  aveditor_find_roster_slot(int index);
extern int  aveditor_find_cell_by_id(int *unused, int index);
extern char match_desc_is_type8_screen13(int entry);
extern void match_active_desc_clear(void);
extern void FUN_80338a8c(void);   // post-delete UI refresh
extern char FUN_80338630(int descIndex);   // descriptor-is-flagged test

} // extern "C"

// forward decls
void match_shift_node_indices(int threshold, int count);
void match_shift_group_indices(int threshold, short count);
void match_delete_entries_in_range(int oldIndex, int newIndex, int count);
int  match_find_flagged_unselected(void);
int  aveditor_next_used_id(int start, int mode);
void match_delete_flagged_unselected(void);
void match_delete_flagged_desc_child(void);
void match_deactivate_flagged_descs(void);

// -----------------------------------------------------------------------------

// Shift every render-list node's cell index (+4) down by `count` when it is >= `threshold`.
void match_shift_node_indices(int threshold, int count)
{
    for (int mode = DAT_80612c14; mode != 0; mode = *(int *)(mode + 0x40)) {
        for (int node = *(int *)(mode + 0x3c); node != 0; node = *(int *)(node + 0x14)) {
            if (threshold <= *(int *)(node + 4)) {
                *(int *)(node + 4) = *(int *)(node + 4) - count;
            }
        }
    }
}

// Shift the group index table (+0x3070, 0xc stride, count at +0x306e, index field +10) down by
// `count` for entries whose index is >= `threshold`.
void match_shift_group_indices(int threshold, short count)
{
    int off = 0;
    for (int i = 0; i < (int)(unsigned int)*(unsigned short *)(DAT_80612be4 + 0x306e); i = i + 1) {
        int entry = DAT_80612be4 + off + 0x3070;
        short idx = *(short *)(entry + 10);
        if (threshold <= idx) {
            *(short *)(entry + 10) = idx - count;
        }
        off = off + 0xc;
    }
}

// Delete up to `count` flagged squad entries starting at cell index `oldIndex`, then renumber the
// render-list and group-index tables down by `count`. The old/new index pair arrives packed in the
// GPR pair (hi = oldIndex, lo = newIndex-scan-end).
void match_delete_entries_in_range(int oldIndex, int newIndex, int count)
{
    (void)oldIndex; (void)newIndex;
    unsigned long long packed = FUN_803826e4();
    int scanEnd = (int)packed;
    int base = (int)((unsigned long long)packed >> 0x20);
    int deleted = 0;
    for (int i = base; i < scanEnd; i = i + 1) {
        int entry = match_find_entry_by_index(base);
        if (match_squad_entry_flag_set(entry) != 0) {
            match_entry_clear_field_a8(entry);
            match_delete_entry();
            deleted = deleted + 1;
            i = i - 1;
            if (deleted == count) break;
        }
    }
    match_shift_node_indices(scanEnd, count);
    match_shift_group_indices(scanEnd, (short)count);
    FUN_80382730();
}

// Find the first flagged, unselected squad entry of the requested kind (packed lo of the GPR pair)
// starting at the packed hi index. Returns the found index or -1 (walk falls through).
int match_find_flagged_unselected(void)
{
    unsigned long long packed = __save_gpr_28();
    int kind = (int)packed;
    int i = (int)((unsigned long long)packed >> 0x20);
    int off = i * 0xb4;
    int result = -1;
    for (; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int *entry = (int *)(DAT_80612be4 + off + 0x81a8);
        if ((match_squad_entry_flag_set((int)entry) != 0) && (*entry != 0) &&
            (aveditor_is_cell_selected((int)*(short *)(entry + 3)) == 0) && (entry[1] == kind)) {
            result = i;
            break;
        }
        off = off + 0xb4;
    }
    __restore_gpr_28();
    return result;
}

// Find the next in-use cell/roster id at or after `start`: for mode 0x46 use the roster-slot lookup,
// otherwise the cell-by-id lookup. Returns the first in-use id, or the total cell count if none.
int aveditor_next_used_id(int start, int mode)
{
    do {
        if (*(int *)(DAT_80612be4 + 0xdd10) <= start) {
            return *(int *)(DAT_80612be4 + 0xdd10);
        }
        if (mode == 0x46) {
            if (aveditor_find_roster_slot(start) != -1) {
                return start;
            }
        } else {
            if (aveditor_find_cell_by_id((int *)0, start) != 0) {
                return start;
            }
        }
        start = start + 1;
    } while (true);
}

// Delete every flagged, unselected squad entry, refreshing the UI after each removal.
void match_delete_flagged_unselected(void)
{
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int *entry = (int *)(DAT_80612be4 + i * 0xb4 + 0x81a8);
        if ((match_squad_entry_flag_set((int)entry) != 0) && (*entry != 0) &&
            (aveditor_is_cell_selected((int)*(short *)(entry + 3)) == 0)) {
            match_entry_clear_field_a8((int)entry);
            match_delete_entry();
            i = i - 1;
            FUN_80338a8c();
        }
    }
}

// For each flagged descriptor, delete its active child entry (the "then" branch child when +0x4234
// > 0, else the first list child) unless that child is a type-8/screen-13 descriptor.
void match_delete_flagged_desc_child(void)
{
    __save_gpr_28();
    int off = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x41e0); i = i + 1) {
        int base = DAT_80612be4;
        char flagged = FUN_80338630(i);
        if ((flagged != '\0') &&
            (match_squad_entry_flag_set(base + *(int *)(base + off + 0x41e4) * 0xb4 + 0x81a8) != 0)) {
            int child = 0;
            int desc = DAT_80612be4 + off;
            if (*(int *)(desc + 0x4234) < 1) {
                if (0 < *(int *)(desc + 0x426c)) {
                    child = DAT_80612be4 + *(int *)(desc + 0x4270) * 0xb4 + 0x81a8;
                }
            } else {
                child = DAT_80612be4 + *(int *)(desc + 0x4238) * 0xb4 + 0x81a8;
            }
            if ((child != 0) && (match_desc_is_type8_screen13(child) == '\0')) {
                match_entry_clear_field_a8(child);
                match_delete_entry();
            }
        }
        off = off + 0x220;
    }
    __restore_gpr_28();
}

// Deactivate + delete flagged descriptors that have become empty (no "else" child [0x22]==0, and
// either no "then" child [0x14]==0 or that child is a type-8/screen-13 terminator). Clears the
// active descriptor if the removed one was active.
void match_deactivate_flagged_descs(void)
{
    __save_gpr_28();
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x41e0); i = i + 1) {
        int *desc = (int *)(DAT_80612be4 + i * 0x220 + 0x41e4);
        int thenChild = DAT_80612be4 + desc[i + 0x15] * 0xb4 + 0x81a8;
        if ((match_squad_entry_flag_set(DAT_80612be4 + *desc * 0xb4 + 0x81a8) != 0) &&
            (desc[0x22] == 0) &&
            ((desc[0x14] == 0) || (match_desc_is_type8_screen13(thenChild) != '\0'))) {
            if (*desc == *(int *)(DAT_80612be4 + 0x284)) {
                match_active_desc_clear();
            }
            match_entry_deactivate(desc);
            match_delete_entry();
            i = i - 1;
        }
    }
    __restore_gpr_28();
}

// Top-level entry compaction. Repeatedly locate the lowest pair of flagged unselected entries,
// compute the next used id gap, delete the flagged entries in that range and renumber; when no more
// flagged entries remain, run the descriptor cleanup passes.
void match_compact_entries(void)
{
    FUN_803826f0();
    bool done = false;
    while (!done) {
        int a = match_find_flagged_unselected();
        int b = match_find_flagged_unselected();
        if ((a == -1) && (b == -1)) {
            done = true;
        } else {
            int pick;
            if (((a != -1) || (b == -1)) && ((a == -1) || (b != -1))) {
                if ((a == -1) || (b == -1)) break;
                pick = (b < a) ? b : a;
            } else {
                pick = (a != -1) ? a : b;
            }
            int used0 = aveditor_next_used_id(pick + 1, 0);
            int used46 = aveditor_next_used_id(pick + 1, 0x46);
            int newIdx = (used46 < used0) ? used46 : used0;
            match_delete_entries_in_range(pick, newIdx, used0 - pick);
        }
    }
    if (done) {
        match_delete_flagged_unselected();
        match_delete_flagged_desc_child();
        match_deactivate_flagged_descs();
    }
    FUN_8038273c();
}

// Find a flagged squad entry by cell id (+0x81b4 short); the target id arrives in the GPR pair.
// Walks the entry table; returns nothing (side-effect walk in the original).
void match_find_flagged_by_id(void)
{
    int target = (int)FUN_803826f0();
    if (-1 < target) {
        int off = 0;
        int i = 0;
        while (i < *(int *)(DAT_80612be4 + 0x81a4)) {
            if ((*(short *)(DAT_80612be4 + off + 0x81b4) == target) &&
                (match_squad_entry_flag_set(DAT_80612be4 + off + 0x81a8) != 0)) {
                break;
            }
            i = i + 1;
            off = off + 0xb4;
        }
    }
    FUN_8038273c();
}
