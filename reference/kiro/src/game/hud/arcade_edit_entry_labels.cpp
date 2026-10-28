// Batch: batch_953 | Module: arcade match-editor squad-entry queries + cell/entry label formatting
// Address range: 0x80337078 - 0x803376b8
// Date: 2026-09-12
//
// Continuation of the arcade custom-match editor. This slice covers two related things:
//   (1) queries over the cell array (DAT_80612be4 + 0xdd14, 0x8c stride, count at +0xdd10) and the
//       squad-entry table (DAT_80612be4 + 0x81a8, 0xb4 stride) - counting cells by mode, scanning
//       cells by sub-type, and flagging matching squad entries.
//   (2) label formatting for cells / squad entries in the editor UI (name + value strings via
//       get_string_by_id + the FUN_80388098 / debug_log_formatted sprintf helpers).
//   (3) the descriptor-apply tree walk that pushes a match descriptor onto an entry and its child
//       entries (match_apply_entry_tree / match_apply_active_squad).
//
//   aveditor_count_cells_of_mode      0x80337078  count cells whose mode (+0x80) equals arg.
//   aveditor_scan_cells_by_subtype    0x803370c0  scan cells by sub-type (+0x84), run squad scan.
//   match_entries_share_group         0x80337148  do two entries share a squad/team id (+0xc/+0xe).
//   match_flag_matching_entries       0x803371a0  flag every entry matching the current one.
//   match_format_entry_label          0x80337250  format a squad-entry label (kind 1/<0/1..2).
//   aveditor_format_cell_descriptor   0x803373a8  format a cell descriptor label (mode 9 / sub 0xd).
//   aveditor_format_objective_entry   0x80337474  format an objective participant label + nodes.
//   aveditor_format_zone_entry        0x80337540  format a zone-cell participant label.
//   match_apply_entry_tree            0x803375b8  apply a descriptor to an entry + its children.
//   match_apply_active_squad          0x803376b8  find the active squad entry and apply its tree.
//
// Raw FUN_/DAT_ symbols get correctly-typed per-file externs; already-named helpers are re-declared.

#include "common.h"

extern "C" {

extern int  DAT_80612be4;    // editor context base
extern int  DAT_80612d00;    // active mode / owner object (0 when no mode active)

// per-sub-type label string-id table (0xc4-byte stride)
extern short        DAT_80477bc8;
extern int          DAT_80480f1c;   // per-mode descriptor string-id table (0x4c stride)
extern int          DAT_80481424;   // entry-kind-1 string-id table (indexed by entry[2])
extern int          DAT_8048148c;   // entry/participant string-id table (indexed by entry[2] / +8)

extern const char s__s__s_80611210[];   // "%s %s"
extern const char s__s__d_80611208[];    // "%s %d" (shared)
extern int  DAT_80611218;                // format literal used by debug_log_formatted

// save/restore GPR-28 pair (temp record alloc); returns packed pair (hi = record ptr).
extern unsigned long long FUN_803826f0(void);
extern void               FUN_8038273c(void);
extern int                __save_gpr_28(void);
extern void               __restore_gpr_28(void);

extern int  get_string_by_id(int id);
extern int  FUN_80388098(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int dst, int fmtId,
                         const char *fmt, int a11, int a12, unsigned int a13, unsigned int a14,
                         unsigned int a15);
extern int  debug_log_formatted(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                                unsigned long long a4, unsigned long long a5, unsigned long long a6,
                                unsigned long long a7, unsigned long long a8, const char *fmt,
                                int a10, int a11, unsigned int a12, unsigned int a13,
                                unsigned int a14, unsigned int a15, unsigned int a16);
extern char *FUN_8038b258(char *dst, char *src);   // string copy/compare helper

// named helpers from prior batches / modules
extern void match_session_scan_local_ready_squad(void);
extern int  match_squad_entry_flag_set(int entry);
extern int  match_descriptor_apply(int *entry);
extern void aveditor_store_cell_nodes(int cell, unsigned int paletteBase, int *src); // batch_952

} // extern "C"

// forward decls
int match_entries_share_group(int a, int b);
int match_apply_entry_tree(void);

// -----------------------------------------------------------------------------

// Count editor cells whose mode field (context +0xdd94 == cell +0x80) equals `mode`.
int aveditor_count_cells_of_mode(int mode)
{
    int count = 0;
    int off = 0;
    int remaining = *(int *)(DAT_80612be4 + 0xdd10);
    if (0 < remaining) {
        do {
            if (*(int *)(DAT_80612be4 + off + 0xdd94) == mode) {
                count = count + 1;
            }
            off = off + 0x8c;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return count;
}

// Scan every cell; for those whose sub-type field (+0xdd98) matches the saved target, run the
// local-ready-squad scan. The target sub-type arrives in the saved GPR pair.
void aveditor_scan_cells_by_subtype(void)
{
    int target = __save_gpr_28();
    int off = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0xdd10); i = i + 1) {
        if (*(int *)(DAT_80612be4 + off + 0xdd98) == target) {
            match_session_scan_local_ready_squad();
        }
        off = off + 0x8c;
    }
    __restore_gpr_28();
}

// Do two squad entries share a group id: either matching squad id (+0xc) or team id (+0xe), both
// non-negative.
int match_entries_share_group(int a, int b)
{
    if ((-1 < *(short *)(a + 0xc)) && (-1 < *(short *)(b + 0xc)) &&
        (*(short *)(a + 0xc) == *(short *)(b + 0xc))) {
        return 1;
    }
    if ((-1 < *(short *)(a + 0xe)) && (-1 < *(short *)(b + 0xe)) &&
        (*(short *)(a + 0xe) == *(short *)(b + 0xe))) {
        return 1;
    }
    return 0;
}

// Flag every squad entry that matches the current one (same group, same kind (+0) and same index
// (+8)) as set. The current entry arrives in the saved GPR pair.
void match_flag_matching_entries(void)
{
    int *cur = (int *)__save_gpr_28();
    int *entry = (int *)(DAT_80612be4 + 0x81a8);
    int off = 0;
    while (entry != cur) {
        int shared = match_entries_share_group((int)cur, (int)entry);
        if ((shared != 0) && (*entry == *cur) && (entry[2] == cur[2])) {
            match_squad_entry_flag_set((int)entry);
        }
        entry = (int *)(DAT_80612be4 + off + 0x825c);
        off = off + 0xb4;
    }
    __restore_gpr_28();
}

// Format a squad-entry label into entry+0x20 based on its kind (+0): kind 1 uses the entry-kind-1
// name table, kinds 1..2 use the participant table, negative kinds fall back to the generic
// string 0xd14. Only runs when the entry flag is set and it is not kind 2.
void match_format_entry_label(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                              unsigned long long a4, unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8, int *entry, int cellBase,
                              unsigned int a11, unsigned int a12, unsigned int a13, unsigned int a14,
                              unsigned int a15, unsigned int a16)
{
    unsigned int flag = (unsigned int)match_squad_entry_flag_set((int)entry);
    if ((flag != 0) && (entry[1] != 2)) {
        int subName = get_string_by_id((int)*(short *)(&DAT_80477bc8 +
                          *(int *)(cellBase + *(short *)(entry + 3) * 0x8c + 0x84) * 0xc4));
        int kind = *entry;
        if (kind == 1) {
            int nameId = get_string_by_id((&DAT_80481424)[entry[2]]);
            FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, (int)(entry + 8), 0x3f, s__s__s_80611210,
                         subName, nameId, a14, a15, a16);
        } else if (kind < 1) {
            if (-1 < kind) {
                int nameId = get_string_by_id(0xd14);
                char *p = (char *)debug_log_formatted(f1, a2, a3, a4, a5, a6, a7, a8,
                              (const char *)&DAT_80611218, nameId, (int)a11, a12, a13, a14, a15, a16);
                p = FUN_8038b258((char *)(entry + 8), p);
                if (p == (char *)0) {
                    int id2 = get_string_by_id(0xd14);
                    FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, (int)(entry + 8), 0x3f,
                                 (const char *)&DAT_80611218, id2, (int)a13, a14, a15, a16);
                }
            }
        } else if (kind < 3) {
            int nameId = get_string_by_id((&DAT_8048148c)[entry[2]]);
            FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, (int)(entry + 8), 0x3f, s__s__s_80611210,
                         nameId, subName, a14, a15, a16);
        }
    }
}

// Format a cell descriptor label: mode-9 cells log the per-mode descriptor string; sub-type-0xd
// cells log the per-sub-type name resolved through the referenced cell's sub-type (+0xdd98).
void aveditor_format_cell_descriptor(unsigned long long f1, unsigned long long a2,
                                     unsigned long long a3, unsigned long long a4,
                                     unsigned long long a5, unsigned long long a6,
                                     unsigned long long a7, unsigned long long a8, int a9, int a10,
                                     int cell, unsigned int a12, unsigned int a13, unsigned int a14,
                                     unsigned int a15, unsigned int a16)
{
    (void)a9; (void)a10;
    unsigned long long packed = FUN_803826f0();
    int recPtr = (int)((unsigned long long)packed >> 0x20);
    int acc = recPtr;
    if (*(int *)(cell + 8) == 9) {
        int nameId = get_string_by_id(*(int *)(&DAT_80480f1c + *(int *)(cell + 4) * 0x4c));
        acc = debug_log_formatted(f1, a2, a3, a4, a5, a6, a7, a8, s__s__s_80611210, nameId,
                                  (int)packed, a12, a13, a14, a15, a16);
    }
    if (*(int *)(cell + 4) == 0xd) {
        int nameId = get_string_by_id((int)*(short *)(&DAT_80477bc8 +
                         *(int *)(DAT_80612be4 + *(short *)(cell + 0xc) * 0x8c + 0xdd98) * 0xc4));
        debug_log_formatted(f1, a2, a3, a4, a5, a6, a7, a8, s__s__s_80611210, acc, nameId,
                            a12, a13, a14, a15, a16);
    }
    FUN_8038273c();
}

// Format an objective participant label: emit "<generic 0xd13> <value>" then "<name> <participant>",
// then flush the objective cell's nodes.
void aveditor_format_objective_entry(unsigned long long f1, unsigned long long a2,
                                     unsigned long long a3, unsigned long long a4,
                                     unsigned long long a5, unsigned long long a6,
                                     unsigned long long a7, unsigned long long a8)
{
    int rec = (int)FUN_803826f0();
    int entry = DAT_80612be4 + *(int *)(DAT_80612be4 + *(int *)(rec + 0xa4) * 0x220 + 0x4270) * 0xb4
                + 0x81a8;
    int cell = DAT_80612be4 + *(short *)(entry + 0xc) * 0x8c + 0xdd14;

    const char *fmt = (const char *)get_string_by_id(0xd13);
    int acc = FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, rec + 0x20, 0x3f, fmt,
                           *(int *)(rec + 0xac), 0, 0, 0, 0);
    int nameId = get_string_by_id((&DAT_8048148c)[*(int *)(entry + 8)]);
    FUN_80388098((unsigned long long)(unsigned int)acc, a2, a3, a4, a5, a6, a7, a8, entry + 0x20,
                 0x3f, s__s__s_80611210, rec + 0x20, nameId, 0, 0, 0);
    aveditor_store_cell_nodes(cell, (unsigned int)(rec + 0x20), (int *)(entry + 0x20));
    FUN_8038273c();
}

// Format a zone-cell participant label ("<active mode name> <participant>") for a type-0xe cell
// while a mode is active.
void aveditor_format_zone_entry(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                                unsigned long long a4, unsigned long long a5, unsigned long long a6,
                                unsigned long long a7, unsigned long long a8, int cell)
{
    if ((DAT_80612d00 != 0) && (*(int *)(cell + 4) == 0xe)) {
        int nameId = get_string_by_id((&DAT_8048148c)[*(int *)(cell + 8)]);
        FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, cell + 0x20, 0x3f, s__s__s_80611210,
                     DAT_80612d00 + 0x20, nameId, 0, 0, 0);
    }
}

// Apply a match descriptor to a squad entry and, if that succeeds without short-circuit, to each of
// its two child-entry lists (+0x54 count at [0x14], +0x8c count at [0x22]). The root entry arrives
// in the saved GPR pair. Returns 1 if any apply reported a stop (goto path).
int match_apply_entry_tree(void)
{
    int *root = (int *)FUN_803826f0();
    int result = match_descriptor_apply((int *)(DAT_80612be4 + *root * 0xb4 + 0x81a8));
    if (result == 0) {
        int off = 0;
        for (int i = 0; i < root[0x14]; i = i + 1) {
            int r = match_descriptor_apply((int *)(DAT_80612be4 +
                        *(int *)((int)root + off + 0x54) * 0xb4 + 0x81a8));
            if (r != 0) { result = r; goto done; }
            off = off + 4;
        }
        int j = 0;
        off = 0;
        while (j < root[0x22]) {
            result = match_descriptor_apply((int *)(DAT_80612be4 +
                         *(int *)((int)root + off + 0x8c) * 0xb4 + 0x81a8));
            if (result != 0) break;
            j = j + 1;
            off = off + 4;
        }
    }
done:
    FUN_8038273c();
    return result;
}

// Walk the active-descriptor table (+0x41e4, 0x220 stride, count at +0x41e0); for the enabled entry
// (+4 == 1) whose owner squad (+0x8254) matches the saved target, apply its entry tree and stop.
void match_apply_active_squad(void)
{
    int target = (int)FUN_803826f0();
    int i = 0;
    int off = 0;
    while (i < *(int *)(DAT_80612be4 + 0x41e0)) {
        int *desc = (int *)(DAT_80612be4 + off + 0x41e4);
        if ((*(char *)(desc + 1) == '\x01') &&
            (*(int *)(DAT_80612be4 + *desc * 0xb4 + 0x8254) == target)) {
            if (match_apply_entry_tree() != 0) break;
        }
        i = i + 1;
        off = off + 0x220;
    }
    FUN_8038273c();
}
