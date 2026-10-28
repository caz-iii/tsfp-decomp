// Batch: batch_872 | Module: arcade map-editor cell transforms + action dialogs
// Address range: 0x803072a4 - 0x80307968
// Date: 2026-09-11
//
// The map-editor "transform the current selection" operations (grab/copy, rotate,
// mirror, per-table cell re-placement) plus the copy action and the action-confirm
// yes/no dialog dispatcher. All hang off the active screen ctx DAT_80612be4 and the
// active piece returned by the hud accessor pairs (FUN_803826d8/e8/f0/d0).
//
//   grab_cell_record        0x803072a4  copy a placement record's 8 cells (0x20 bytes,
//                                       FUN_80312c1c resolves the record) into the
//                                       cursor scratch @ctx+0x2f30, mark dirty
//                                       (FUN_80348238 / ctx+0x238 bit 0x20), detach the
//                                       source cell (FUN_80314040), reset input.
//   load_selected_entry     0x80307340  mark dirty then load roster entry
//                                       roster_load_entry(FUN_80312cb0(p)); reset input.
//   rotate_selection        0x803073a0  step every grid cell's orientation byte (count
//                                       @ctx+0x386e, table @+0x3870 0xc stride) through
//                                       the rotation state machine (1..4 / 7..10 wrap;
//                                       0xb..0xe CW/CCW by dir) and re-place each cell
//                                       (FUN_80313670).
//   xform_table_cells       0x80307534  walk the placement 2D table (rows @ctx+0x280 /
//                                       +0x288 stride 0xe4, entries @+0x58 stride 0x10)
//                                       and transform cells matching the active piece
//                                       (FUN_803134f0 + slot recompute FUN_8031370c).
//   xform_mode3_cells       0x803075fc  same over the mode-3 rows (@ctx+0xdd14 stride
//                                       0x8c, entries @+0x24 stride 8; slot via
//                                       FUN_80336690).
//   xform_piece_cells       0x803076cc  same over the active piece's own cell chain
//                                       (FUN_803826f0 pair; chain @+0x3c next @+0x14).
//   mirror_selection        0x80307744  the mirror/flip op: SFX 0x1579, rotate, then
//                                       per member re-place cells (FUN_80313748 /
//                                       FUN_80313448) after running the three xform
//                                       passes.
//   action_confirm_dialog   0x80307818  action dispatcher: pick a prompt string
//                                       (0xb2e..0xb33) from which action callback the
//                                       piece pair resolves to, SFX 0x1575, open a
//                                       yes/no dialog (hud_create_yes_no_dialog).
//   copy_selection_action   0x8030790c  copy-to-clipboard: transition, aselgrid_copy_
//                                       selection, set paste-available flag (bit 4).
//   clear_selection_type    0x80307968  reset the current selection type (ctx+0x2f34 =
//                                       -1) and input repeat timers.
//
// NON-MATCHING: the hud piece accessors return a PPC {base,index} register pair as a
// u64/longlong; the high 32 bits (index/sign) and low 32 bits (base/callback ptr) are
// split verbatim. action_confirm_dialog compares the resolved callback pointer against
// the raw action-callback addresses (FUN_80307a2c/a68/aa4) and this batch's
// copy_selection_action by address identity. rotate_selection reads its rotation
// direction from the accessor pair's low word. Named cross-module helpers
// (roster_load_entry, avanim_play_move_sound, get_string_by_id, hud_create_yes_no_dialog,
// aselgrid_copy_selection, hud_input_reset_repeat_timers) resolve through the shared
// headers; every FUN_/DAT_ raw symbol gets a per-file extern below.

#include "common.h"
#include "hud.h"

// Raw per-file symbol externs use C linkage; the batch's own DEFINITIONS use C++
// linkage (below, outside this block) to match generated_decls.h.
extern "C" {

// --- PPC prologue/epilogue register-pair spill helpers ---
u64  __save_gpr_28(void);
void __restore_gpr_28(void);
u64  __save_gpr_25(void);
void __restore_gpr_25(void);

// --- hud active-piece accessors (return {base,index} register pairs) ---
u64  FUN_803826d8(void);
u64  FUN_803826f0(void);
void FUN_80382724(void);
void FUN_8038273c(void);

// --- raw FUN_ helpers referenced from this module ---
int  FUN_80312c1c(int* pOut, int a);          // resolve placement record
int  FUN_80312cb0(int a);                     // resolve roster entry index
void FUN_80348238(int mode);                  // mark ctx dirty (sound/anim)
void FUN_80314040(int listHead, int cell);    // detach one cell from a member
void FUN_80313670(short* pCell, int a, int b, int c); // re-place a cell (rotate)
void FUN_803134f0(short* px, short* py, int a, int pair); // transform a cell coord
int  FUN_8031370c(int a, int pair);           // slot recompute after transform
int  FUN_80336690(int base, int index);       // slot address recompute (mode-3)
double FUN_80313748(int a, int pair, int c);  // per-cell transform (returns float in pair)
void FUN_80313448(float* pCell, float* pXY, int a, int pair, int d); // re-place after mirror
void FUN_8031cba4(int a, int b);              // screen transition push

// --- active screen context + globals ---
extern int DAT_80612be4;   // active screen context base
extern int DAT_80612bf4;   // parent screen id (dialog parent)

} // extern "C"

// forward decls for the address-identity comparison in action_confirm_dialog.
// confirm_delete_cell / confirm_clear_selection / confirm_remove_roster are the
// batch_873 dialog-confirm callbacks (arcade_edit_actions.cpp); they resolve via
// generated_decls.h at C++ linkage.
void copy_selection_action(void);
void confirm_delete_cell(int param_1);
void confirm_clear_selection(int param_1);
void confirm_remove_roster(int param_1);

// ---------------------------------------------------------------------------
// 0x803072a4  grab_cell_record
// ---------------------------------------------------------------------------
void grab_cell_record(int param_1)
{
    u32* pSrc;
    u32* pDst;
    u32  tmp;
    int  rec;
    u32* pS;
    u32* pD;
    int  count;
    int  local_18[5];

    rec = FUN_80312c1c(local_18, param_1);
    count = 7;
    pSrc = (u32 *)(rec + -4);
    pDst = (u32 *)(DAT_80612be4 + 0x2f30);
    do {
        pD = pDst;
        pS = pSrc;
        tmp = pS[2];
        pD[1] = pS[1];
        pD[2] = tmp;
        count = count + -1;
        pSrc = pS + 2;
        pDst = pD + 2;
    } while (count != 0);
    pD[3] = pS[3];
    if ((*(u32 *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(0);
        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 0x20;
    }
    FUN_80314040(local_18[0], rec);
    hud_input_reset_repeat_timers();
}

// ---------------------------------------------------------------------------
// 0x80307340  load_selected_entry
// ---------------------------------------------------------------------------
void load_selected_entry(int param_1)
{
    int idx;

    if ((*(u32 *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(0);
        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 0x20;
    }
    idx = FUN_80312cb0(param_1);
    roster_load_entry(idx);
    hud_input_reset_repeat_timers();
}

// ---------------------------------------------------------------------------
// 0x803073a0  rotate_selection
// ---------------------------------------------------------------------------
void rotate_selection(void)
{
    unsigned char next;
    int    baseIdx;
    short* pCell;
    int    dir;
    int    off;
    int    i;
    u64    pair;

    pair = __save_gpr_28();
    baseIdx = (int)((u64)pair >> 0x20);
    dir = (int)pair;
    off = 0;
    for (i = 0; i < (int)(u32)*(unsigned short *)(DAT_80612be4 + 0x386e); i = i + 1) {
        pCell = (short *)(DAT_80612be4 + off + 0x3870);
        switch (*(unsigned char *)(pCell + 3)) {
        default:
            *(unsigned char *)(pCell + 3) = 0;
            break;
        case 1:
        case 2:
        case 3:
        case 4:
            *(char *)(pCell + 3) = *(char *)(pCell + 3) + (char)pair;
            if (4 < *(unsigned char *)(pCell + 3)) {
                *(unsigned char *)(pCell + 3) = 1;
            }
            if (*(char *)(pCell + 3) == '\0') {
                *(unsigned char *)(pCell + 3) = 4;
            }
            break;
        case 7:
        case 8:
        case 9:
        case 10:
            *(char *)(pCell + 3) = *(char *)(pCell + 3) + (char)pair;
            if (10 < *(unsigned char *)(pCell + 3)) {
                *(unsigned char *)(pCell + 3) = 7;
            }
            if (*(unsigned char *)(pCell + 3) < 7) {
                *(unsigned char *)(pCell + 3) = 10;
            }
            break;
        case 0xb:
            next = 0xe;
            if (dir == 1) { next = 0xc; }
            *(unsigned char *)(pCell + 3) = next;
            break;
        case 0xc:
            next = 0xb;
            if (dir == 1) { next = 0xd; }
            *(unsigned char *)(pCell + 3) = next;
            break;
        case 0xd:
            next = 0xc;
            if (dir == 1) { next = 0xe; }
            *(unsigned char *)(pCell + 3) = next;
            break;
        case 0xe:
            next = 0xd;
            if (dir == 1) { next = 0xb; }
            *(unsigned char *)(pCell + 3) = next;
            break;
        }
        if ((*(unsigned char *)(pCell + 3) < 0xb) || (0xe < *(unsigned char *)(pCell + 3))) {
            FUN_80313670(pCell, baseIdx, dir, 0);
        }
        else {
            FUN_80313670(pCell, baseIdx, dir, -1);
        }
        off = off + 0xc;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80307534  xform_table_cells
// ---------------------------------------------------------------------------
void xform_table_cells(void)
{
    int base;
    int slot;
    int entry;
    int row;
    int i;
    int j;
    int rowOff;
    int entOff;
    u64 pair;

    pair = __save_gpr_25();
    base = (int)((u64)pair >> 0x20);
    rowOff = 0;
    for (i = 0; i < *(int *)(DAT_80612be4 + 0x280); i = i + 1) {
        row = DAT_80612be4 + rowOff + 0x288;
        entOff = 0;
        for (j = 0; j < *(int *)(row + 0x58); j = j + 1) {
            entry = row + entOff;
            if ((*(int *)(entry + 100) == base) &&
                (FUN_803134f0((short *)(entry + 0x68), (short *)(entry + 0x6a),
                              *(int *)(base + *(int *)(base + 0xc) * 4 + 0x2c), (int)pair),
                 j == 0)) {
                slot = FUN_8031370c(*(int *)(row + 0x54), (int)pair);
                *(int *)(row + 0x54) = slot;
            }
            entOff = entOff + 0x10;
        }
        rowOff = rowOff + 0xe4;
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x803075fc  xform_mode3_cells
// ---------------------------------------------------------------------------
void xform_mode3_cells(void)
{
    int    base;
    short* pSlot;
    int    j;
    int    nEnt;
    int    i;
    int    nRows;
    int    row;
    int    entOff;
    int    rowOff;
    u64    pair;

    pair = FUN_803826d8();
    base = (int)((u64)pair >> 0x20);
    rowOff = 0;
    nRows = *(int *)(DAT_80612be4 + 0xdd10);
    for (i = 0; i < nRows; i = i + 1) {
        row = DAT_80612be4 + rowOff + 0xdd14;
        if (*(int *)(row + 0x80) == 3) {
            nEnt = *(int *)(row + 0x20);
            entOff = 0;
            for (j = 0; j < nEnt; j = j + 1) {
                if (*(int *)(row + entOff + 0x24) == base) {
                    pSlot = (short *)FUN_80336690(row, j);
                    FUN_803134f0(pSlot, pSlot + 1,
                                 *(int *)(base + *(int *)(base + 0xc) * 4 + 0x2c), (int)pair);
                }
                entOff = entOff + 8;
            }
        }
        rowOff = rowOff + 0x8c;
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x803076cc  xform_piece_cells
// ---------------------------------------------------------------------------
void xform_piece_cells(void)
{
    int base;
    int slot;
    int cell;
    u64 pair;

    pair = FUN_803826f0();
    base = (int)((u64)pair >> 0x20);
    for (cell = *(int *)(base + 0x3c); cell != 0; cell = *(int *)(cell + 0x14)) {
        FUN_803134f0((short *)(cell + 0xc), (short *)(cell + 0xe),
                     *(int *)(base + *(int *)(base + 0xc) * 4 + 0x2c), (int)pair);
        slot = FUN_8031370c(*(int *)(cell + 8), (int)pair);
        *(int *)(cell + 8) = slot;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80307744  mirror_selection
// ---------------------------------------------------------------------------
void mirror_selection(void)
{
    float  fResult;
    float* pMember;
    u64    pair;

    pair = FUN_803826f0();
    if ((int)((u64)pair >> 0x20) != 0) {
        avanim_play_move_sound(0x1579);
        rotate_selection();
        for (pMember = *(float **)(DAT_80612be4 + 0x2f7c); pMember != (float *)0x0;
             pMember = (float *)(intptr_t)pMember[0x10]) {
            fResult = (float)FUN_80313748((int)pMember[3], (int)pair, 1);
            xform_table_cells();
            xform_mode3_cells();
            xform_piece_cells();
            FUN_80313448(pMember, pMember + 1, (int)pMember[(int)pMember[3] + 0xb], (int)pair, -1);
            pMember[3] = fResult;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80307818  action_confirm_dialog
// ---------------------------------------------------------------------------
void action_confirm_dialog(u32 param_1, u32 param_2, int param_3)
{
    int   promptId;
    void  (*cb)(void);
    long long pair;

    pair = (long long)FUN_803826f0();
    cb = (void (*)(void))(intptr_t)pair;
    if (cb != (void (*)(void))0x0) {
        if (((cb == (void (*)(void))confirm_delete_cell) ||
             (cb == (void (*)(void))confirm_clear_selection)) ||
            (cb == (void (*)(void))confirm_remove_roster)) {
            if (pair < 0) {
                promptId = get_string_by_id(0xb32);
            }
            else {
                promptId = get_string_by_id(0xb31);
            }
        }
        else if (cb == copy_selection_action) {
            FUN_8031cba4(1, 0);
            promptId = get_string_by_id(0xb33);
        }
        else if (pair < 0) {
            promptId = get_string_by_id(0xb2f);
        }
        else {
            promptId = get_string_by_id(0xb2e);
        }
        avanim_play_move_sound(0x1575);
        hud_create_yes_no_dialog(DAT_80612bf4, promptId, (int)(intptr_t)cb, param_3);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8030790c  copy_selection_action
// ---------------------------------------------------------------------------
void copy_selection_action(void)
{
    FUN_8031cba4(-1, 1);
    if (*(int *)(DAT_80612be4 + 0x2f18) != 0) {
        aselgrid_copy_selection();
        if (*(int *)(DAT_80612be4 + 0x2f7c) != 0) {
            *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) | 4;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80307968  clear_selection_type
// ---------------------------------------------------------------------------
void clear_selection_type(void)
{
    *(int *)(DAT_80612be4 + 0x2f34) = -1;
    hud_input_reset_repeat_timers();
}
