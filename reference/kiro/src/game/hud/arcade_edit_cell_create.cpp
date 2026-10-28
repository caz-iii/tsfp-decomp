// Batch: batch_949 | Module: arcade match-editor cell/participant creation + lookup
// Address range: 0x803357ec - 0x80336378
// Date: 2026-09-12
//
// The placement side of the arcade custom-match editor: creating new editor "cells" (participants,
// objective markers, trigger zones), registering placed cells into the per-mode lists, and the
// lookup helpers that map a mode/type to a compatible cell and its display index.
//
//   match_editor_softkey_add_object 0x803357ec  softkey handler: label the pending object, chain on.
//   match_editor_build_count_screen 0x80335894  build the participant count / score sub-screen.
//   aveditor_register_placed_cell   0x80335c6c   register a placed cell into the mode's cell lists.
//   aveditor_cell_list_contains     0x80335d2c   does the participant list contain a given index?
//   aveditor_create_participant     0x80335d68   allocate + init a new participant/player entry.
//   aveditor_create_objective_cell  0x80335f5c   allocate + init a new type-1 (objective) cell.
//   aveditor_create_zone_cell       0x80336138   allocate + init a new type-2 (trigger/zone) cell.
//   aveditor_find_compatible_cell   0x80336284   find a mode cell record compatible with `type`.
//   aveditor_cell_display_index     0x80336338   compute a cell's packed display index.
//   aveditor_lookup_cell_display    0x80336378   find compatible cell for `type`, return its index.
//
// Cell records are 0xb4 bytes at DAT_80612be4 + <idx>*0xb4 + 0x81a8; the mode record table starts
// at DAT_80612be4 + 0xdd14 (stride 0x8c). The participant/list bookkeeping lives in the 0x220-byte
// slot object at DAT_80612be4 + <slot>*0x220 + 0x41e4. Object-type dispatch runs through the
// per-type vtable table PTR_FUN_80480f44 (stride 0x13 pointers).
//
// PPC ABI note: the create/softkey handlers receive the caller's r3..r10 (float/vararg spill) which
// the decompiler exposes as param_1..param_8 + in_rN. These are threaded verbatim into the shared
// HUD label formatters (FUN_80387fb8/FUN_80388098/FUN_80387540) and modelled here as opaque
// register-width words (unsigned long long / int) so the call sites keep their arity.
//
// Raw FUN_/DAT_/FLOAT_/PTR_ symbols get correctly-typed per-file externs; already-named
// cross-module helpers are re-declared with their real ABI.

#include "common.h"

extern "C" {

// -- editor / match globals --
extern int  DAT_80612be4;   // editor context base
extern int  DAT_80612d00;   // active match-grid slot object (0 if none)
extern int  DAT_80612d04;   // secondary local-player index (bit into cell owner mask)
extern int  DAT_806119e0;   // shared position/anim descriptor arg

// -- geometry float constants --
extern float FLOAT_8061aa14;  // callback-item size
extern float FLOAT_8061aa18;  // scroll overflow offset
extern float FLOAT_8061aa1c;  // layout coefficient
extern float FLOAT_8061aa20;  // layout coefficient
extern float FLOAT_8061aa24;  // layout coefficient
extern float FLOAT_8061aa28;  // common item size (0 baseline)
extern float FLOAT_8061aa2c;  // sub-frame Y offset
extern float FLOAT_8061aad0;  // slider size
extern float FLOAT_80611a5c;  // screen scroll coefficient
extern float FLOAT_80611a64;  // screen layout coefficient
extern float FLOAT_80611a6c;  // screen top Y

// -- per-type dispatch / config tables --
extern void        *PTR_FUN_80480f44;  // per-object-type init vtable (stride 0x13 ptrs)
extern unsigned int DAT_80480f30;       // per-mode config flag words (stride 0x4c)
extern void        *PTR_FUN_804818b8;   // "participant" object vtable marker
extern void        *PTR_match_editor_scroll_frame_804817f8; // child scroll-frame descriptor

// -- runtime save/restore helpers --
extern unsigned long long __save_gpr_28(void);
extern void __restore_gpr_28(void);

// -- raw helpers not individually named --
extern long long FUN_803826e4(void);   // fetch current placement descriptor (packed type|pos)
extern void  FUN_80382730(void);        // finalize placement (variant D)
extern void  FUN_80348238(int promptId);// queue a modal prompt
extern int   FUN_803369b0(int cellRec);  // per-cell display multiplier
extern void  memset_wrapper(int dst, int val, int len);
// shared HUD label formatters (variadic register spill threaded through)
extern unsigned long long FUN_80387fb8(unsigned long long a1, unsigned long long a2,
    unsigned long long a3, unsigned long long a4, unsigned long long a5, unsigned long long a6,
    unsigned long long a7, unsigned long long a8, int target, char *label, int value, int slot,
    int r7, int r8, int r9, int r10);
extern void FUN_80387540(unsigned long long a1, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, int cellRec);
extern void FUN_80388098(unsigned long long a1, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, int target, int steps, char *label, int value, int ctx,
    int r8, int r9, int r10);

// -- already-named cross-module helpers (real ABI) --
extern int   get_string_by_id(int stringId);
extern int   match_participants_max_score(void);
extern int   match_participant_count_plus_one(void);
extern unsigned char *aveditor_palette_item_texture_root(void);
extern void  match_editor_enter_or_pop(void);   // used as a close callback fn ptr

extern void  hud_element_init_from_layout(int layoutId, int a, int b, void *closeCb);
extern void  hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void  hud_set_context_property(int prop);
extern void  hud_element_scroll_h_overflow(double amount, double a, double b, int c);
extern void  hud_begin_scroll_with_item(void);
extern void  hud_add_list_item(void);
extern void  hud_end_scroll_region(void);
extern void  hud_item_add_value_selector(int a, int target, int lo, int hi, int step,
                                         int label, int flags);
extern void  hud_item_add_slider(double size, double range, int a, int target, int steps,
                                 int label, int flags);
extern void  hud_set_last_child_tag(int tag);
extern void  hud_add_callback_item(double size);
extern void  hud_element_apply_scroll(void);
extern void  hud_element_set_pos_y(double y, int descriptor);
extern void  hud_element_finalize_frame(void);
extern int   hud_element_create(int parent, void *descriptor, int kind);
extern void  hud_element_set_pos_xy(double x, double y, int element);
extern void  hud_element_link_and_show(int child, int parent);

} // extern "C"

// -----------------------------------------------------------------------------

// Softkey handler for the "add object" prompt: when a slot is active and its object is the
// participant vtable, label the pending object (if it has an active edit at +0xa0) and chain into
// the follow-up handler for the slot's active mode cell record.
void match_editor_softkey_add_object(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8, int item)
{
    int slot = DAT_80612d00;
    if ((DAT_80612d00 != 0) && (*(void ***)(item + 4) == (void **)&PTR_FUN_804818b8)) {
        int slotBase = DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220;
        if (*(int *)(DAT_80612d00 + 0xa0) != 0) {
            char *label = (char *)get_string_by_id(0xd13);
            param_1 = FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                   param_8, DAT_80612d00 + 0x20, label,
                                   *(int *)(slot + 0xac), DAT_80612d00, 0, 0, 0, 0);
        }
        FUN_80387540(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     DAT_80612be4 + *(int *)(slotBase + 0x4270) * 0xb4 + 0x81a8);
    }
}

// Build the participant count / score sub-screen. Two flavours keyed on the object vtable at
// item+4: the participant flavour (PTR_FUN_804818b8) shows a score-limit value selector plus a set
// of action rows and attaches a child scroll frame; the other flavour shows a target slider.
void match_editor_build_count_screen(int item)
{
    void **vtbl = *(void ***)(item + 4);
    int title = get_string_by_id(0xb06);
    hud_element_init_from_layout(
        (int)(-(unsigned int)(vtbl == (void **)&PTR_FUN_804818b8) & 0x10) | 0x8400,
        title, 0, (void *)match_editor_enter_or_pop);
    hud_set_item_padding_mode((double)FLOAT_8061aa28, 1, 1, 1);
    hud_set_context_property(0x20000);
    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
                  ((FLOAT_8061aa1c * FLOAT_80611a64 +
                    FLOAT_8061aa20 * FLOAT_80611a64 +
                    FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                  FLOAT_8061aa18),
        (double)FLOAT_8061aa28, (double)FLOAT_8061aa28, 0);

    if (*(void ***)(item + 4) == (void **)&PTR_FUN_804818b8) {
        get_string_by_id(0xadc);
        hud_begin_scroll_with_item();
        hud_add_list_item();
        hud_end_scroll_region();

        get_string_by_id(0xade);
        hud_begin_scroll_with_item();
        int maxScore = match_participants_max_score();
        hud_item_add_value_selector(0, DAT_80612d00 + 0xac, 1, maxScore, 1, 0, 0);
        hud_set_last_child_tag(0x109);
        hud_end_scroll_region();

        get_string_by_id(0xb08);
        hud_add_list_item();
        hud_set_last_child_tag(0x10a);
        get_string_by_id(0xb09);
        hud_add_list_item();
        hud_set_last_child_tag(0x108);
        get_string_by_id(0xb0a);
        hud_add_list_item();
        hud_set_last_child_tag(0x10b);
    } else {
        get_string_by_id(0xb07);
        hud_begin_scroll_with_item();
        hud_item_add_slider((double)FLOAT_8061aa28, (double)FLOAT_8061aad0, 0,
                            DAT_80612d00 + 0x20, 0x3f, DAT_80612d00 + 0xa0, 0);
        hud_set_last_child_tag(0x106);
        hud_end_scroll_region();

        get_string_by_id(0xb08);
        hud_add_list_item();
        hud_set_last_child_tag(0x107);
        get_string_by_id(0xb09);
        hud_add_list_item();
        hud_set_last_child_tag(0x108);
    }

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

    if (*(void ***)(item + 4) == (void **)&PTR_FUN_804818b8) {
        *(int *)(item + 0x214) = 2;
        int child = hud_element_create(item, &PTR_match_editor_scroll_frame_804817f8, 2);
        hud_element_set_pos_xy(
            (double)(FLOAT_8061aa24 + *(float *)(item + 0x4c)),
            (double)(FLOAT_80611a5c *
                     ((FLOAT_8061aa1c * FLOAT_80611a64 +
                       FLOAT_8061aa20 * FLOAT_80611a64 +
                       FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) +
                     FLOAT_8061aa2c),
            child);
        hud_element_link_and_show(child, item);
    }
    *(unsigned int *)(item + 0x214) = *(unsigned int *)(item + 0x214) | 4;
}

// Register a placed cell (`cell`, a 0xb4-byte record) into the mode lists of match slot `slot`.
// Type 2 cells append to the "zone" list at +0x88/+0x8c; type 1 cells append to the "objective"
// list at +0x50/+0x54, recording an owner/team byte at +0x7c.
void aveditor_register_placed_cell(int slot, int *cell)
{
    int type = *cell;
    int cellIndex = ((int)cell - (DAT_80612be4 + 0x81a8)) / 0xb4;
    int lists = DAT_80612be4 + *(int *)(slot + 0xa4) * 0x220 + 0x41e4;

    if (type == 2) {
        int e = lists + *(int *)(lists + 0x88) * 4;
        *(int *)(e + 0x8c) = cellIndex;
        *(int *)(e + 0x154) = 0;
        *(int *)(lists + 0x88) = *(int *)(lists + 0x88) + 1;
        return;
    }
    if (1 < type) {
        return;
    }
    if (type < 1) {
        return;
    }

    int slotEntry = lists + *(int *)(lists + 0x50);
    *(int *)(lists + *(int *)(lists + 0x50) * 4 + 0x54) = cellIndex;
    *(char *)(slotEntry + 0x7c) = (char)(1 - (int)(cell[7] == 0));
    if (*(int *)(lists + 0x4c) == 1) {
        unsigned char owner = (cell[7] != 0) ? 3 : 2;
        *(unsigned char *)(slotEntry + 0x7c) = owner;
    }
    *(int *)(lists + 0x50) = *(int *)(lists + 0x50) + 1;
}

// Does the zone-list at `lists` (+0x88 count, +0x8c entries) already contain cell index `index`?
unsigned int aveditor_cell_list_contains(int lists, int index)
{
    int count = *(int *)(lists + 0x88);
    int off = 0;
    if (0 < count) {
        do {
            if (index == *(int *)(lists + off + 0x8c)) {
                return 1;
            }
            off = off + 4;
            count = count + -1;
        } while (count != 0);
    }
    return 0;
}

// Allocate + initialise a new participant/player entry: zero a new cell (0xb4) and a new slot list
// object (0x220), wire the new list index, bump the participant/team counters, format the label via
// the shared formatter, and set the owner mask + defaults. iVar6 carries the participant number.
void aveditor_create_participant(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8)
{
    unsigned long long r3 = __save_gpr_28();
    int labelId = 0;
    int number = 0;
    int *lists = (int *)(DAT_80612be4 + *(int *)(DAT_80612be4 + 0x41e0) * 0x220 + 0x41e4);
    int *cell  = (int *)(DAT_80612be4 + *(int *)(DAT_80612be4 + 0x81a4) * 0xb4 + 0x81a8);
    int ctx = DAT_80612be4;

    memset_wrapper((int)cell, 0, 0xb4);
    *cell = 0;
    memset_wrapper((int)lists, 0, 0x220);
    lists[0x87] = 0;
    *lists = *(int *)(DAT_80612be4 + 0x81a4);
    *(char *)((int)lists + 4) = (char)*(int *)(DAT_80612be4 + 0x248);
    cell[0x29] = *(int *)(DAT_80612be4 + 0x41e0);
    *(int *)(DAT_80612be4 + 0x81a4) = *(int *)(DAT_80612be4 + 0x81a4) + 1;
    *(int *)(DAT_80612be4 + 0x41e0) = *(int *)(DAT_80612be4 + 0x41e0) + 1;
    *(int *)(DAT_80612be4 + 0x11488) = *(int *)(DAT_80612be4 + 0x11488) + 1;

    int kind = *(int *)(DAT_80612be4 + 0x248);
    if (kind == 1) {
        labelId = 0xd13;
        *(int *)(DAT_80612be4 + 0x1148c) = *(int *)(DAT_80612be4 + 0x1148c) + 1;
        *(char *)(DAT_80612be4 + 0x41dc) = *(char *)(DAT_80612be4 + 0x41dc) + 1;
        number = match_participant_count_plus_one();
    } else if ((kind < 1) && (-1 < kind)) {
        labelId = 0xd12;
        number = (*(int *)(DAT_80612be4 + 0x11488) - *(int *)(DAT_80612be4 + 0x41d8)) -
                 *(int *)(DAT_80612be4 + 0x1148c);
    }

    char *label = (char *)get_string_by_id(labelId);
    FUN_80388098(r3, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 (int)(cell + 8), 0x3f, label, number, ctx, 0, 0, 0);
    *(unsigned char *)(cell + 6) = 1;
    cell[0x28] = 1;
    *(unsigned short *)(cell + 3) = 0xffff;
    *(unsigned short *)((int)cell + 0xe) = 0xffff;
    cell[0x2b] = (*(char *)((int)lists + 4) == 1) ? number : -1;
    *(char *)(cell + 0x2c) = (char)(1 << *(int *)(DAT_80612be4 + 0x248));

    if ((*(int *)(DAT_80612be4 + 0x254) == 4) &&
        ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0x14);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    __restore_gpr_28();
}

// Allocate + init a new type-1 (objective) cell from the current placement descriptor. Sets the
// type-specific default limit (+0x10), applies the owner mask (with an optional secondary local
// player), runs the per-type init vtable, then finalises.
void aveditor_create_objective_cell(unsigned int param_1, unsigned int param_2,
                                    unsigned short param_3, unsigned short param_4)
{
    long long desc = FUN_803826e4();
    int base = DAT_80612be4;
    int typeHi = (int)((unsigned long long)desc >> 0x20);
    int idx = *(int *)(DAT_80612be4 + 0x81a4);
    *(int *)(DAT_80612be4 + 0x81a4) = idx + 1;
    unsigned int *cell = (unsigned int *)(base + idx * 0xb4 + 0x81a8);

    memset_wrapper((int)cell, 0, 0xb4);
    *cell = 1;
    *(long long *)(cell + 1) = desc;
    cell[0x28] = 1;
    *(unsigned short *)(cell + 3) = param_3;
    *(unsigned short *)((int)cell + 0xe) = param_4;
    cell[4] = 0;
    cell[0x29] = 0xffffffff;
    cell[0x2b] = 0xffffffff;
    *(char *)(cell + 0x2c) = (char)(1 << *(int *)(DAT_80612be4 + 0x248));

    if (*(int *)(DAT_80612be4 + 0x11484) != 0) {
        *(unsigned char *)(cell + 0x2c) =
            *(unsigned char *)(cell + 0x2c) | (unsigned char)(1 << DAT_80612d04);
        *(int *)(DAT_80612be4 + 0x11484) = 0;
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x10000;
    }

    if (typeHi == 6) {
        cell[4] = 10;
    } else if (desc < 0x600000000LL) {
        if (typeHi == 4) {
            cell[4] = 0x3c;
        } else if (3 < typeHi) {
            cell[4] = 10;
        }
    } else if (typeHi == 0xd) {
        int rec = DAT_80612be4 + *(short *)(cell + 3) * 0x8c;
        unsigned char *pal = aveditor_palette_item_texture_root();
        *(int *)(rec + 0xdd98) = **(int **)(pal + 4);
    }

    (*(void (*)(unsigned int *, int, int))(&PTR_FUN_80480f44)[typeHi * 0x13])(
        cell, DAT_80612be4 + 0xdd14, DAT_80612be4 + 0x288);

    if ((*(int *)(DAT_80612be4 + 0x254) == 4) &&
        ((*(unsigned int *)(&DAT_80480f30 + typeHi * 0x4c) & 0x20) != 0) &&
        ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0x14);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    FUN_80382730();
}

// Allocate + init a new type-2 (trigger/zone) cell from the current placement descriptor. Same
// shape as the objective cell but with type-2 defaults and no secondary-owner handling.
void aveditor_create_zone_cell(unsigned int param_1, unsigned int param_2,
                               unsigned short param_3, unsigned short param_4)
{
    long long desc = FUN_803826e4();
    int base = DAT_80612be4;
    int typeHi = (int)((unsigned long long)desc >> 0x20);
    int idx = *(int *)(DAT_80612be4 + 0x81a4);
    *(int *)(DAT_80612be4 + 0x81a4) = idx + 1;
    unsigned int *cell = (unsigned int *)(base + idx * 0xb4 + 0x81a8);

    memset_wrapper((int)cell, 0, 0xb4);
    *cell = 2;
    *(long long *)(cell + 1) = desc;
    cell[0x28] = 1;
    *(unsigned short *)(cell + 3) = param_3;
    *(unsigned short *)((int)cell + 0xe) = param_4;
    cell[4] = 0;
    cell[0x29] = 0xffffffff;
    cell[0x2b] = 0xffffffff;
    *(char *)(cell + 0x2c) = (char)(1 << *(int *)(DAT_80612be4 + 0x248));

    if (typeHi == 6) {
        cell[4] = 1;
    } else if ((desc < 0x600000000LL) && (4 < typeHi)) {
        cell[4] = 1;
    }

    (*(void (*)(unsigned int *, int, int))(&PTR_FUN_80480f44)[typeHi * 0x13])(
        cell, DAT_80612be4 + 0xdd14, DAT_80612be4 + 0x288);

    if ((*(int *)(DAT_80612be4 + 0x254) == 4) &&
        ((*(unsigned int *)(&DAT_80480f30 + typeHi * 0x4c) & 0x20) != 0) &&
        ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0x14);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    FUN_80382730();
}

// Scan the mode cell records from `firstMode` down to 0 for one compatible with `type`. Modes
// 9/7/0xe are mutually compatible; otherwise the types must be identical. Returns the first record
// (+0x74 selectable) that matches, or 0.
int aveditor_find_compatible_cell(int firstMode, int type)
{
    int i = firstMode + 1;
    int off = firstMode * 0x8c;
    if (-1 < firstMode) {
        do {
            int rec = DAT_80612be4 + off + 0xdd14;
            unsigned int match;
            int recType = *(int *)(rec + 0x80);
            if ((type == 9) || (type == 7) || (type == 0xe)) {
                match = ((recType == 9) || (recType == 7) || (recType == 0xe)) ? 1u : 0u;
            } else {
                match = countLeadingZeros((unsigned int)(type - recType)) >> 5;
            }
            if ((-1 < *(short *)(rec + 0x74)) && (match != 0)) {
                return rec;
            }
            off = off + -0x8c;
            i = i + -1;
        } while (i != 0);
    }
    return 0;
}

// Compute a cell's packed display index: base index (+0x74) plus a per-cell multiplier (+0x76)
// scaled by FUN_803369b0.
int aveditor_cell_display_index(int cellRec)
{
    int mul = FUN_803369b0(cellRec);
    return (int)(short)(*(short *)(cellRec + 0x74) +
                        (unsigned short)*(unsigned char *)(cellRec + 0x76) * (short)mul);
}

// Find the cell compatible with `type` in the current mode set and return its display index, or 0.
int aveditor_lookup_cell_display(int type)
{
    int result = 0;
    if (*(int *)(DAT_80612be4 + 0xdd10) != 0) {
        int rec = aveditor_find_compatible_cell(*(int *)(DAT_80612be4 + 0xdd10) + -1, type);
        if (rec == 0) {
            result = 0;
        } else {
            result = aveditor_cell_display_index(rec);
        }
    }
    return result;
}
