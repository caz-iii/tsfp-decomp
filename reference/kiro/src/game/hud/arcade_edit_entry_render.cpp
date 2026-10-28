// Batch: batch_954 | Module: arcade match-editor squad-entry rendering + context reset/lifecycle
// Address range: 0x80337758 - 0x80337e48
// Date: 2026-09-12
//
// Continuation of the arcade custom-match editor. This slice contains:
//   - the main squad-entry label renderer (match_render_entry_label) and its squad-name helper,
//     which build the multi-part "<name> <value> <detail>" strings shown per squad entry;
//   - the per-frame walk that renders every squad entry through its per-mode vtable
//     (match_render_all_entries);
//   - the full editor-context reset (match_editor_reset_context) that zeroes every table;
//   - small entry-table lifecycle helpers (create default objective, deactivate entry, find by
//     index, clear field, and a no-op vtable slot).
//
//   match_validate_squad_order        0x80337758  validate squad ordering over up to 8 rounds.
//   aveditor_format_squad_name        0x80337854  format the squad-name label for an entry.
//   match_render_entry_label          0x803378cc  main squad-entry label renderer (all variants).
//   match_render_all_entries          0x80337be8  render every squad entry via its per-mode vtable.
//   match_editor_reset_context        0x80337c9c  reset/clear the whole editor context tables.
//   aveditor_create_default_objective 0x80337dd0  create the default mode-8 objective cell.
//   match_editor_noop                 0x80337e00  empty vtable slot.
//   match_entry_clear_field_a8        0x80337e04  clear entry field +0xa8.
//   match_entry_deactivate            0x80337e10  deactivate a squad entry, decrement live count.
//   match_find_entry_by_index         0x80337e48  find a squad entry by its cell index (+0xc).
//
// Raw FUN_/DAT_ symbols get correctly-typed per-file externs; already-named helpers are re-declared.

#include "common.h"

extern "C" {

extern int  DAT_80612be4;    // editor context base
extern int  DAT_80612c14;    // render list head (linked mode list)

// per-mode tables
extern int          DAT_80480f10;   // per-mode name string-id table (int stride 0x13 words)
extern int          DAT_80480f1c;   // per-mode value-label string-id table (0x4c stride)
extern unsigned int DAT_80480f30;   // per-mode flags word (0x4c stride; bit 0x100 = "renderable")
extern int          PTR_FUN_80480f44;   // per-mode entry render vtable (0x13-word stride)
extern int          DAT_80481424;   // entry-kind-1 string-id table
extern int          DAT_8048148c;   // participant string-id table
extern unsigned int DAT_805e1470;   // per-round validity flags (0x32 dwords)

extern const char s__s__s_80611210[];       // "%s %s"
extern const char s__s__s__s_80481988[];     // "%s %s %s"
extern const char s__s__s__s__s_80481994[];  // "%s %s %s %s"
extern int  DAT_80611220;                    // format literal (numbered variant)

// save/restore GPR pair (temp record alloc); returns packed pair (hi = entry ptr).
extern unsigned long long FUN_803826dc(void);
extern void               FUN_80382728(void);

extern int  get_string_by_id(int id);
extern void memset_wrapper(int dst, int val, int len);
extern int  FUN_80388098(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int dst, int fmtId,
                         const char *fmt, int a11, int a12, void *a13, void *a14, int a15);
extern int  debug_log_formatted(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                                unsigned long long a4, unsigned long long a5, unsigned long long a6,
                                unsigned long long a7, unsigned long long a8, const char *fmt,
                                int a10, int a11, int a12, unsigned int a13, void *a14, void *a15,
                                int a16);
extern int  FUN_8038b5c8(int str);   // is-empty-string test
extern unsigned long long hud_format_value_simple(unsigned char *dst, int cap, int value,
                                                  int fmtId, unsigned int flags);

// named helpers from prior batches / modules
extern int  match_apply_active_squad(void);            // batch_953 (declared void; used as int here)
extern int  match_flag_matching_entries(void);         // batch_953
extern int  match_squad_entry_flag_set(int entry);
extern int  match_mode_type_is_eligible(int kind, int mode, int index);
extern int  aveditor_format_cell_descriptor(unsigned long long f1, unsigned long long a2,
                unsigned long long a3, unsigned long long a4, unsigned long long a5,
                unsigned long long a6, unsigned long long a7, unsigned long long a8, int *nameStr,
                unsigned char *valueStr, int entry, int a12, unsigned int a13, void *a14,
                void *a15, int a16);                    // batch_953
extern int  aveditor_format_zone_entry(unsigned long long f1, unsigned long long a2,
                unsigned long long a3, unsigned long long a4, unsigned long long a5,
                unsigned long long a6, unsigned long long a7, unsigned long long a8, int cell); // b953
extern void match_format_entry_label(unsigned long long f1, unsigned long long a2,
                unsigned long long a3, unsigned long long a4, unsigned long long a5,
                unsigned long long a6, unsigned long long a7, unsigned long long a8, int *entry,
                int cellBase, const char *fmt, int *a11, int *a12, void *a13, void *a14,
                int a15);                               // batch_953
extern int  aveditor_create_objective_cell(int type, int a2, unsigned short label, int idx); // b949

} // extern "C"

// -----------------------------------------------------------------------------

// Validate squad ordering: for each of up to 8 rounds, scan the render list for node kinds 5/6 that
// belong to the (round+1) group; if any present, apply the active squad and stop on failure;
// otherwise require the round index to be within the configured round count (+0x41dc). Returns 1 if
// the ordering is valid all the way through.
unsigned int match_validate_squad_order(void)
{
    int round = 0;
    do {
        bool haveKind5 = false;
        bool haveKind6 = false;
        for (int mode = DAT_80612c14; mode != 0; mode = *(int *)(mode + 0x40)) {
            for (int *node = *(int **)(mode + 0x3c); node != (int *)0; node = (int *)node[5]) {
                if ((*node == 5) && (!haveKind5) && (round + 1 == node[6])) {
                    haveKind5 = true;
                }
                if ((*node == 6) && (!haveKind6) && (round + 1 == node[6])) {
                    haveKind6 = true;
                }
            }
        }
        if (haveKind5 || haveKind6) {
            if (match_apply_active_squad() == 0) {
                return 0;
            }
        } else if (round < (int)(unsigned int)*(unsigned char *)(DAT_80612be4 + 0x41dc)) {
            return 0;
        }
        round = round + 1;
    } while (round < 8);
    return 1;
}

// Format the squad-name label for an entry: resolve the owning squad record (+0xdd90 -> 0xb4 table)
// and emit "<0x9cc> <squad>" into entry+0x20.
void aveditor_format_squad_name(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                                unsigned long long a4, unsigned long long a5, unsigned long long a6,
                                unsigned long long a7, unsigned long long a8, int entry)
{
    int squad = DAT_80612be4 +
        *(int *)(DAT_80612be4 + *(short *)(entry + 0xc) * 0x8c + 0xdd90) * 0xb4;
    const char *fmt = (const char *)get_string_by_id(0x9cc);
    FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, entry + 0x20, 0x3f, fmt, squad + 0x81c8, 0, 0, 0, 0);
}

// Main squad-entry label renderer. Resolves the entry's name and value strings, tests eligibility
// and matching-entry count, then emits one of several formatted variants (plain, cell-descriptor,
// numbered) depending on the entry kind (+0) and mode (+4). Zone (mode 0xe) entries get the zone
// label; flagged entries additionally get the shared entry label. Uses a temp record from the GPR
// pair for the entry pointer.
void match_render_entry_label(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                              unsigned long long a4, unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8, int a9, int a10,
                              int squadTable, int a12, unsigned int a13, void *a14, void *a15,
                              int a16)
{
    (void)a9; (void)a10; (void)a12;
    unsigned long long packed = FUN_803826dc();
    int *entry = (int *)((unsigned long long)packed >> 0x20);
    int recBase = (int)packed;

    int *nameStr = (int *)get_string_by_id((&DAT_80480f10)[entry[1] * 0x13]);
    int detailId = (*entry == 1) ? (&DAT_80481424)[entry[2]] : (&DAT_8048148c)[entry[2]];
    int *detailStr = (int *)get_string_by_id(detailId);
    char matchCount = (char)match_flag_matching_entries();
    int negIdx = -entry[2];

    if (*(short *)(entry + 3) < 0) {
        if (-1 < *(short *)((int)entry + 0xe)) {
            nameStr = (int *)(squadTable + *(short *)((int)entry + 0xe) * 0xe4);
        }
    } else {
        nameStr = (int *)(recBase + *(short *)(entry + 3) * 0x8c);
    }

    unsigned char valueStr[168];
    valueStr[0] = 0;
    int eligible = match_mode_type_is_eligible(*entry, entry[1], entry[2]);
    if (eligible != 0) {
        int valSrc = 0;
        if (-1 < *(short *)(entry + 3)) {
            valSrc = recBase + *(short *)(entry + 3) * 0x8c;
        }
        unsigned int flags = (unsigned int)(-(int)(entry[1] == 4)) & 0x60;
        int valFmt;
        int value;
        if (negIdx == -9) {
            valFmt = 0;
            value = *(int *)(valSrc + 0x20);
        } else {
            valFmt = *(int *)(&DAT_80480f1c + entry[1] * 0x4c);
            value = entry[4];
        }
        int fmtId = get_string_by_id(valFmt);
        hud_format_value_simple(valueStr, 0x80, value, fmtId, flags);
    }

    const char *usedFmt = s__s__s_80611210;
    int *nameForLabel = detailStr;
    int *detailForLabel = nameStr;

    if (*entry == 1) {
        if (FUN_8038b5c8((int)valueStr) == 0) {
            int *dstr = detailStr;
            if ('\0' < matchCount) {
                dstr = (int *)debug_log_formatted(f1, a2, a3, a4, a5, a6, a7, a8,
                            (const char *)&DAT_80611220, (int)detailStr, matchCount + 1, 0, a13,
                            a14, a15, a16);
            }
            FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, (int)(entry + 8), 0x3f, s__s__s_80611210,
                         (int)nameStr, (int)dstr, a14, a15, a16);
        } else if (entry[1] == 0xd) {
            int acc = aveditor_format_cell_descriptor(f1, a2, a3, a4, a5, a6, a7, a8, detailStr,
                          valueStr, (int)entry, a12, a13, a14, a15, a16);
            int zeroId = get_string_by_id(0);
            FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, (int)(entry + 8), 0x3f, s__s__s_80611210,
                         zeroId, acc, a14, a15, a16);
        } else {
            FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, (int)(entry + 8), 0x3f,
                         s__s__s__s_80481988, (int)nameStr, (int)detailStr, valueStr, a15, a16);
        }
        int *labelStr = (int *)get_string_by_id(0xb0c);
        detailForLabel = entry + 8;
        nameForLabel = labelStr;
        FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, (int)(entry + 0x18), 0x3f, s__s__s_80611210,
                     (int)labelStr, (int)(entry + 8), valueStr, a15, a16);
    } else {
        if (FUN_8038b5c8((int)valueStr) == 0) {
            int *dstr = nameStr;
            if ('\0' < matchCount) {
                dstr = (int *)debug_log_formatted(f1, a2, a3, a4, a5, a6, a7, a8,
                            (const char *)&DAT_80611220, (int)nameStr, matchCount + 1, 0, a13,
                            a14, a15, a16);
            }
            usedFmt = s__s__s_80611210;
            nameForLabel = dstr;
            FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, (int)(entry + 8), 0x3f, s__s__s_80611210,
                         (int)detailStr, (int)dstr, a14, a15, a16);
        } else {
            void *b64 = (void *)get_string_by_id(0xb64);
            usedFmt = s__s__s__s__s_80481994;
            nameForLabel = detailStr;
            FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, (int)(entry + 8), 0x3f,
                         s__s__s__s__s_80481994, (int)nameStr, (int)detailStr, b64, valueStr, a16);
            detailForLabel = nameStr;
        }
    }

    if (entry[1] == 0xe) {
        aveditor_format_zone_entry(f1, a2, a3, a4, a5, a6, a7, a8, (int)entry);
    }
    unsigned int flag = (unsigned int)match_squad_entry_flag_set((int)entry);
    if (flag != 0) {
        match_format_entry_label(f1, a2, a3, a4, a5, a6, a7, a8, entry, recBase, usedFmt,
                                 nameForLabel, detailForLabel, a14, a15, a16);
    }
    FUN_80382728();
}

// Render every squad entry (+0x81a8, 0xb4 stride, count at +0x81a4) whose kind is 1 or 2 through its
// per-mode render vtable (PTR_FUN_80480f44), when that slot exists and the mode's flags word has the
// "renderable" bit (0x100) set.
void match_render_all_entries(void)
{
    int off = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x81a4); i = i + 1) {
        int *entry = (int *)(DAT_80612be4 + off + 0x81a8);
        if ((*entry == 1) || (*entry == 2)) {
            void *fn = (void *)(&PTR_FUN_80480f44)[entry[1] * 0x13];
            if ((fn != (void *)0) &&
                ((*(unsigned int *)(&DAT_80480f30 + entry[1] * 0x4c) & 0x100) != 0)) {
                ((void (*)(int *, int, int))fn)(entry, DAT_80612be4 + 0xdd14,
                                                DAT_80612be4 + 0x288);
            }
        }
        off = off + 0xb4;
    }
}

// Reset the entire match-editor context: zero the scalar counters and memset every table
// (descriptors, entries, cells, node tables, per-round flags), then set the DAT_805e1470 per-round
// validity flags to 1.
void match_editor_reset_context(void)
{
    *(int *)(DAT_80612be4 + 0x280) = 0;
    *(int *)(DAT_80612be4 + 0x81a4) = 0;
    *(int *)(DAT_80612be4 + 0x41e0) = 0;
    *(int *)(DAT_80612be4 + 0x41d8) = 0;
    *(unsigned char *)(DAT_80612be4 + 0x41dc) = 0;
    *(int *)(DAT_80612be4 + 0xdd10) = 0;
    memset_wrapper(DAT_80612be4 + 0x41e4, 0, 0x3fc0);
    memset_wrapper(DAT_80612be4 + 0x288, 0, 0x2c88);
    memset_wrapper(DAT_80612be4 + 0x81a8, 0, 0x5b68);
    memset_wrapper(DAT_80612be4 + 0xdd14, 0, 14000);
    memset_wrapper(DAT_80612be4 + 0x113c4, 0, 0xc0);
    memset_wrapper(DAT_80612be4 + 0x11488, 0, 400);

    int off = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x41e0); i = i + 1) {
        *(int *)(DAT_80612be4 + off + 0x426c) = 0;
        off = off + 0x220;
    }
    int fo = 0;
    for (int i = 0x32; i != 0; i = i - 1) {
        *(unsigned int *)((int)&DAT_805e1470 + fo) = 1;
        fo = fo + 4;
    }
}

// Create the default objective cell (mode 8, sub-type 0x13) with no preset label/index.
void aveditor_create_default_objective(void)
{
    aveditor_create_objective_cell(8, 0x13, (unsigned short)0xffff, 0xffff);
}

// Empty vtable slot.
void match_editor_noop(void)
{
}

// Clear a squad entry's field +0xa8.
void match_entry_clear_field_a8(int entry)
{
    *(int *)(entry + 0xa8) = 0;
}

// Deactivate a squad entry: clear its active flag (+0x21c word), clear the owning squad's active
// marker (+0x8250), and decrement the live-entry count (+0x41d8).
void match_entry_deactivate(int *entry)
{
    int base = DAT_80612be4;
    entry[0x87] = 0;
    *(int *)(base + *entry * 0xb4 + 0x8250) = 0;
    *(int *)(DAT_80612be4 + 0x41d8) = *(int *)(DAT_80612be4 + 0x41d8) + -1;
}

// Find a squad entry by its cell index (+0xc); returns the entry pointer or 0.
int match_find_entry_by_index(int index)
{
    int off = 0;
    int remaining = *(int *)(DAT_80612be4 + 0x81a4);
    if (0 < remaining) {
        do {
            int entry = DAT_80612be4 + off + 0x81a8;
            if (*(short *)(entry + 0xc) == index) {
                return entry;
            }
            off = off + 0xb4;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return 0;
}
