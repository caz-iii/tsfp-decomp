// Batch: batch_957 | Module: arcade map-editor entry removal / delete-at-node
// Address range: 0x80338a30 - 0x80338ed8
// Date: 2026-09-12
//
// The "remove the placed entry under the cursor" path of the arcade match editor, sibling to the
// participant-placement layer in arcade_edit_participant_add.cpp. Given the current node (found by
// matching a cell id through the placed-cell list) it deactivates/clears the node's three entry
// fields (+0x4238 objective, +0x4270 linked, +0x41e4 roster) and deletes them, optionally
// requesting an objective-subsystem rebuild and running the screen dirty check. Editor context is
// DAT_80612be4; placed-cell list at +0x81a8 (0xb4 stride, count at +0x81a4, id at +0x81b4); node
// table stride 0x220 (count at +0x41e0).
//
//   arcade_edit_delay_280           0x80338a30  spin-delay of +0x280 iterations (timing no-op).
//   arcade_edit_delay_screen        0x80338a4c  spin-delay from three screen counters (timing no-op).
//   arcade_edit_delay_fixed         0x80338a8c  three fixed spin-delays (0x82/100/0x1e) (timing no-op).
//   arcade_edit_clear_node_field    0x80338ab4  clear/deactivate one of a node's 3 entry fields + delete.
//   arcade_edit_find_placed_by_id   0x80338b88  find a placed-cell record by id (+0xc), or 0.
//   arcade_edit_find_node_by_cell   0x80338bd4  find the node index referencing a squad cell id, or -1.
//   arcade_edit_current_cell_id     0x80338ca0  resolve the id of the cell under the cursor, or -1.
//   arcade_edit_delete_current      0x80338d14  delete the entry under the cursor from its node.
//   arcade_edit_current_entry_info  0x80338e7c  cell-record ptr (+0x18) for the current node, or 0.
//   arcade_edit_confirm_delete_text 0x80338ed8  show the delete-confirm dialog (string by sub-mode).
//
// NON-MATCHING: arcade_edit_find_node_by_cell / arcade_edit_delete_current are emitted by the
// decompiler with the compiler _savegpr_29 / _restgpr_29 runtime helpers (rendered as
// FUN_803826f0 / FUN_8038273c) folding a preserved register into a bogus "return value". Those are
// pure prologue/epilogue register spill with no semantic effect; the real values (the squad id in
// find_node_by_cell, the screen-context pointer in delete_current) are recovered from the
// disassembly and expressed as explicit parameters (Compile-Clean rules 1/3). The three delay
// helpers are countdown busy-loops with no memory effect, written as no-op stubs that preserve the
// iteration count. This TU includes only common.h (not hud.h) so match_*/aveditor_* helpers can be
// declared file-locally; several shared decls carry `void` returns their call sites contradict.

#include "common.h"

extern "C" {

// editor context block
extern int DAT_80612be4;

// named cross-module helpers (declared here, not via hud.h)
extern void match_entry_clear_field_a8(int entry);
extern void match_entry_deactivate(int* entry);
extern void match_delete_entry(void);
extern int  match_squad_entry_flag_set(int entry);
extern int  aveditor_find_cell_record_by_id(int cellId);
extern void hud_screen_push(void);
extern int  get_string_by_id(int id);
extern void hud_create_back_dialog(int unused_a, int unused_b, int user_data);
extern void screen_exit_dirty_check(void);

// raw FUN_ helper not yet named
extern void FUN_80348238(int kind);   // mark editor subsystem dirty / request rebuild

} // extern "C"

// forward prototypes for this file's helpers referenced before their definition
int  arcade_edit_current_cell_id(void);
int  arcade_edit_find_node_by_cell(int cellId);
void arcade_edit_clear_node_field(int nodeIndex, int field);
void arcade_edit_delay_fixed(void);

// -----------------------------------------------------------------------------

// Timing busy-wait: spin (+0x280) iterations. No memory effect (a hardware settle delay).
void arcade_edit_delay_280(void)
{
    int n = *(int*)(DAT_80612be4 + 0x280);
    while (n > 0) { --n; }
}

// Timing busy-wait: spin down three screen counters (+0x134, +0x12c, +0x124). No memory effect.
void arcade_edit_delay_screen(int screen)
{
    for (int n = *(int*)(screen + 0x134); n > 0; ) { --n; }
    for (int n = *(int*)(screen + 0x12c); n > 0; ) { --n; }
    for (int n = *(int*)(screen + 0x124); n > 0; ) { --n; }
}

// Timing busy-wait: three fixed spin-delays (0x82, 100, 0x1e iterations). No memory effect.
void arcade_edit_delay_fixed(void)
{
    for (int n = 0x82; n != 0; ) { --n; }
    for (int n = 100;  n != 0; ) { --n; }
    for (int n = 0x1e; n != 0; ) { --n; }
}

// Clear/deactivate one of a node's three entry fields and delete it. field: 0 = roster (+0x41e4,
// deactivated in place), 1 = objective (+0x4238), 2 = linked (+0x4270). The +0x4238/+0x4270 fields
// index a placed-cell list slot (0xb4 stride at +0x81a8) whose +0xa8 flag is cleared.
void arcade_edit_clear_node_field(int nodeIndex, int field)
{
    int slot = -1;
    int nodeBase = DAT_80612be4 + nodeIndex * 0x220;
    if (field == 1) {
        slot = *(int*)(nodeBase + 0x4238);
        match_entry_clear_field_a8(DAT_80612be4 + slot * 0xb4 + 0x81a8);
    } else if (field < 1) {
        if (field >= 0) {   // field == 0
            match_entry_deactivate((int*)(intptr_t)(nodeBase + 0x41e4));
            slot = *(int*)(nodeBase + 0x41e4);
        }
    } else if (field < 3) { // field == 2
        slot = *(int*)(nodeBase + 0x4270);
        match_entry_clear_field_a8(DAT_80612be4 + slot * 0xb4 + 0x81a8);
    }
    if (slot != -1) {
        match_delete_entry();
    }
}

// Find a placed-cell record whose id (+0xc) matches, scanning the placed-cell list (0xb4 stride,
// count at +0x81a4). Returns the record address or 0.
int arcade_edit_find_placed_by_id(int id)
{
    int count = *(int*)(DAT_80612be4 + 0x81a4);
    int off = 0;
    while (count > 0) {
        int rec = DAT_80612be4 + off + 0x81a8;
        if (*(short*)(rec + 0xc) == id) {
            return rec;
        }
        off += 0xb4;
        --count;
    }
    return 0;
}

// Find the node index that references a placed-cell of the given squad id (+0x81b4) that has its
// squad flag set, and is referenced by some node's +0x4270 or +0x4238 field. Returns index or -1.
int arcade_edit_find_node_by_cell(int squadId)
{
    for (int cellIndex = 0, off = 0;
         cellIndex < *(int*)(DAT_80612be4 + 0x81a4);
         cellIndex++, off += 0xb4) {
        if (*(short*)(DAT_80612be4 + off + 0x81b4) != squadId) {
            continue;
        }
        if (match_squad_entry_flag_set(DAT_80612be4 + off + 0x81a8) == 0) {
            continue;
        }
        int nodeOff = 0;
        for (int n = *(int*)(DAT_80612be4 + 0x41e0); n > 0; n--, nodeOff += 0x220) {
            if (cellIndex == *(int*)(DAT_80612be4 + nodeOff + 0x4270) ||
                cellIndex == *(int*)(DAT_80612be4 + nodeOff + 0x4238)) {
                return cellIndex;
            }
        }
    }
    return -1;
}

// Resolve the id of the cell currently under the cursor from the active selection state:
// the pending-cell list (+0x2f1c, id at +4), else the hovered roster entry (+0x2f78 -> +0x307a),
// else the drag/select pair (+0x2f38/+0x2f34). Returns the id (>=0) or -1.
int arcade_edit_current_cell_id(void)
{
    if (*(int*)(DAT_80612be4 + 0x2f1c) != 0) {
        int id = *(int*)(*(int*)(DAT_80612be4 + 0x2f1c) + 4);
        return (id < 0) ? -1 : id;
    }
    if (*(int*)(DAT_80612be4 + 0x2f78) >= 0) {
        int id = (int)*(short*)(DAT_80612be4 +
                                *(int*)(DAT_80612be4 + 0x2f78) * 0xc + 0x307a);
        return (id < 0) ? -1 : id;
    }
    if (*(int*)(DAT_80612be4 + 0x2f38) >= 0 && *(int*)(DAT_80612be4 + 0x2f34) >= 0) {
        return *(int*)(DAT_80612be4 + 0x2f38);
    }
    return -1;
}

// Delete the entry under the cursor from its node. Resolve the current cell id; if valid, find its
// node (else fall back to the last node), then clear the appropriate fields based on the screen
// sub-mode (+0x188): mode 2/1 clears all three + rebuild + dirty-check; mode -1/0 clears objective
// + roster + dirty-check; mode -2..-1 clears linked + pushes a screen. screen = editor screen ctx.
void arcade_edit_delete_current(int screen)
{
    int cellId = arcade_edit_current_cell_id();
    int node = -1;
    if (cellId == -1) {
        int nodeCount = *(int*)(DAT_80612be4 + 0x41e0);
        if (nodeCount > 0) {
            node = nodeCount - 1;
        }
    } else {
        node = arcade_edit_find_node_by_cell(cellId);
    }
    if (node != -1) {
        int mode = *(int*)(screen + 0x188);
        if (mode == 1) {
            arcade_edit_clear_node_field(node, 2);
            arcade_edit_clear_node_field(node, 1);
            arcade_edit_clear_node_field(node, 0);
            if ((*(unsigned*)(DAT_80612be4 + 0x238) & 0x20) == 0) {
                FUN_80348238(3);
                *(unsigned*)(DAT_80612be4 + 0x238) |= 0x20;
            }
            if (*(int*)(screen + 0x2c) != 0) {
                screen_exit_dirty_check();
            }
        } else if (mode < 1) {
            if (mode == -1) {
                arcade_edit_clear_node_field(node, 1);
                arcade_edit_clear_node_field(node, 0);
                screen_exit_dirty_check();
            } else if (mode > -2) {   // mode == 0 falls here in the original ordering
                arcade_edit_clear_node_field(node, 2);
                hud_screen_push();
            }
        } else if (mode < 3) {        // mode == 2
            arcade_edit_clear_node_field(node, 1);
            arcade_edit_clear_node_field(node, 0);
            screen_exit_dirty_check();
        }
    }
    arcade_edit_delay_fixed();
}

// Return the cell-record pointer (+0x18) for the entry at the current node's objective slot, or 0
// when there is no current cell. Used to fetch the record whose fields the UI edits.
int arcade_edit_current_entry_info(void)
{
    int cellId = arcade_edit_current_cell_id();
    if (cellId < 0) {
        return 0;
    }
    int node = arcade_edit_find_node_by_cell(cellId);
    int slot = *(int*)(DAT_80612be4 + node * 0x220 + 0x4238);
    int recId = (int)*(short*)(DAT_80612be4 + slot * 0xb4 + 0x81b4);
    int rec = aveditor_find_cell_record_by_id(recId);
    return rec + 0x18;
}

// Show the delete-confirm dialog with the string chosen by the screen sub-mode (+0x188):
// mode 1 -> 0xab8, mode 0 -> 0xab7, mode 2 -> 0xab9 (else id 0).
void arcade_edit_confirm_delete_text(int screen)
{
    int mode = *(int*)(screen + 0x188);
    int stringId = 0;
    if (mode == 1) {
        stringId = 0xab8;
    } else if (mode < 1) {
        if (mode >= 0) {   // mode == 0
            stringId = 0xab7;
        }
    } else if (mode < 3) { // mode == 2
        stringId = 0xab9;
    }
    int text = get_string_by_id(stringId);
    hud_create_back_dialog(screen, text, 0);
}
