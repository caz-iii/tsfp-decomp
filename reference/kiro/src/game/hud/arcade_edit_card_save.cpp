// Batch: batch_959 | Module: arcade/challenge editor memory-card save-slot flow + editor helpers
// Address range: 0x803395a4 - 0x80339aec
// Date: 2026-09-12
//
// The memory-card save/load path for the arcade challenge editor: write/create/delete a save
// record, build the 8-entry save-slot name table, queue the slot assets for streaming, and load a
// record back (validating its magic). Plus two arcade-editor helpers that spilled into this address
// range: a node-roster reference test and the arcade-select frame positioning. The card state lives
// in the DAT_80612d** block (record buffer base +0x38-stride at DAT_80612d58, selected slot
// DAT_80612d50, screen id DAT_80612d4c); the scratch record is DAT_805e1538; the streaming buffer
// is DAT_80612be8. Editor context is DAT_80612be4 (node table +0x41e4, 0x220 stride, count +0x41e0).
//
//   arcade_edit_cell_id_referenced   0x803395a4  1 when a cell id (+4) is in any node's roster list.
//   arcade_edit_card_exit            0x80339640  save-exit dispatcher: finish + navigate/push screen.
//   arcsel_position_frame            0x80339764  position the arcade-select frame (two layout modes).
//   arcade_edit_card_write           0x803397d4  write the selected slot record; handle the result.
//   arcade_edit_card_create          0x80339884  create the selected slot record; handle the result.
//   arcade_edit_card_delete          0x8033991c  delete the selected slot record; handle the result.
//   arcade_edit_confirm_yesno        0x803399b8  format an event message + show a yes/no dialog.
//   arcade_edit_build_slot_names     0x80339a1c  fill the 8-entry save-slot name table from strings.
//   arcade_edit_queue_slot_assets    0x80339aa4  queue all 8 save-slot assets for streaming.
//   arcade_edit_load_slot_record     0x80339aec  load a streamed slot record, validate its magic.
//
// NON-MATCHING: arcade_edit_cell_id_referenced returns r3 (0/1) but Ghidra typed it void; the real
// int return is restored (Compile-Clean rule 1). arcade_edit_build_slot_names is emitted with the
// _savegpr_/_restgpr_ runtime helpers (FUN_803826f0/FUN_8038273c) - pure register spill, omitted.
// The card_write/create/delete handlers carry the 8 register pass-through slots forwarded to the
// shared save-error handler FUN_8033a784; they are expressed as opaque forwarded registers.
// This TU includes only common.h (not hud.h) so helpers with wrong shared decls (card_write_record
// / card_create_record declared void though their result is used) can be declared file-locally.

#include "common.h"

extern "C" {

// editor context + card save-state blocks
extern int DAT_80612be4;    // editor context (node table +0x41e4)
extern int DAT_80612be8;    // streaming/scratch buffer (0x7ffc bytes)
extern int DAT_805e1538;    // scratch save-record (0x38 bytes)

extern int DAT_80612d10;    // memcard busy/dismiss flag
extern int DAT_80612d14;    // card op sub-state
extern int DAT_80612d20;    // save-flow branch flag A
extern int DAT_80612d28;    // save-flow branch flag B
extern int DAT_80612d2c;    // save-flow branch flag C
extern int DAT_80612d30;    // editor-vs-challenge mode flag (bit 0)
extern int DAT_80612d38;    // parent screen handle for navigation
extern int DAT_80612d40;    // exit destination selector
extern int DAT_80612d44;    // card record kind for create
extern int DAT_80612d4c;    // next card screen id
extern int DAT_80612d50;    // selected save slot index
extern int DAT_80612d54;    // selected slot valid flag
extern int DAT_80612d58;    // card record table base (0x38 stride, header at +8, records at +0x10)

extern int DAT_806119e0;    // arcade-select frame element handle
extern int PTR_menu_challenge_list_build_804764ec; // challenge-list screen build descriptor

// arcade-select frame layout constants
extern float FLOAT_80611a5c;
extern float FLOAT_80611a64;
extern float FLOAT_80611a6c;
extern float FLOAT_8061aad8;
extern float FLOAT_8061aadc;
extern float FLOAT_8061aae0;

// named cross-module helpers (declared file-locally, not via hud.h)
extern void notif_reset_player_slot(void);
extern void aveditor_enter_screen(void);
extern void hud_screen_navigate_to(int parent, int target_desc, int focus_index);
extern void hud_screen_push(void);
extern void memcard_check_dismiss(void);
extern void hud_element_set_pos_y(double y, int element);
extern void hud_element_apply_center_xy(int* element);
extern void arcsel_clamp_element_y(int elem);
extern void leaf_31e8(unsigned dst, unsigned src, int len);
extern int  memset_wrapper(int dst, int value, int len);
extern int  card_write_record(void);
extern int  card_create_record(int record, int buffer, int kind);
extern int  card_delete_record(int slot, int tag);
extern void notif_format_event_message(unsigned long long r3, unsigned long long r4,
                                       unsigned long long r5, unsigned long long r6,
                                       unsigned long long r7, unsigned long long r8,
                                       unsigned long long r9, unsigned long long r10);
extern void notif_show_yes_no_dialog(void);
extern int  get_string_by_id(int id);
extern void stream_queue_raw_asset(void);
extern int  stream_find_pending_by_name(void);

// raw FUN_ helpers not yet named
extern int  FUN_803484c4(int index);   // return the save-slot descriptor record for a slot index
extern void FUN_80339edc(void);        // finish the card save/exit bookkeeping
extern void FUN_80348ca8(void);        // apply a loaded challenge record
extern void FUN_8038b4cc(int dst, int str, int maxLen); // bounded string copy into a fixed field
extern void FUN_8033a784(unsigned long long f1, unsigned long long r4, unsigned long long r5,
                         unsigned long long r6, unsigned long long r7, unsigned long long r8,
                         unsigned long long r9, unsigned long long r10, unsigned a9, int status,
                         int a11, int a12, unsigned a13, unsigned a14, unsigned a15, unsigned a16); // shared card-error handler

} // extern "C"

// -----------------------------------------------------------------------------

// Return 1 when the cell's id (+4) is referenced by any node's roster list (node flag +0x48 == 1,
// count at +0x50, entry indices at +0x54 stride 4, resolved through the placed-cell id +0x81b4).
int arcade_edit_cell_id_referenced(int cell)
{
    int id = *(int*)(cell + 4);
    if (id == -1) {
        return 0;
    }
    for (int i = 0, off = 0; i < *(int*)(DAT_80612be4 + 0x41e0); i++, off += 0x220) {
        int node = DAT_80612be4 + off + 0x41e4;
        if (*(char*)(node + 4) != 1) {
            continue;
        }
        int count = *(int*)(node + 0x50);
        for (int j = 0, k = 0; j < count; j++, k += 4) {
            int slot = *(int*)(node + k + 0x54);
            if ((int)*(short*)(DAT_80612be4 + slot * 0xb4 + 0x81b4) == id) {
                return 1;
            }
        }
    }
    return 0;
}

// Save-exit dispatcher for the challenge editor. When the card is idle (DAT_80612d10==0): reset the
// player slot, finish the save bookkeeping, then either re-enter the editor screen (editor mode) or
// navigate to / push the challenge-list screen based on the exit-destination flags. When busy,
// dismiss the pending memcard prompt instead.
void arcade_edit_card_exit(void)
{
    if (DAT_80612d10 != 0) {
        memcard_check_dismiss();
        return;
    }
    notif_reset_player_slot();
    FUN_80339edc();
    if ((DAT_80612d30 & 1) == 0) {
        *(unsigned*)(DAT_80612be4 + 0x25c) &= 0xfffff7ff;
        aveditor_enter_screen();
        return;
    }
    // challenge mode: both DAT_80612d40 branches (0/1) share the same navigation logic
    if (DAT_80612d20 == 0 || DAT_80612d2c == 0) {
        if (DAT_80612d28 == 0 || DAT_80612d20 != 0) {
            hud_screen_navigate_to(DAT_80612d38,
                                   (int)(intptr_t)&PTR_menu_challenge_list_build_804764ec, -1);
        } else {
            hud_screen_push();
        }
    } else {
        hud_screen_push();
    }
}

// Position the arcade-select frame element: in list mode a computed Y from the layout constants,
// in grid mode centre it on X/Y and clamp the Y.
void arcsel_position_frame(void)
{
    if ((DAT_80612d30 & 1) == 0) {
        float y = FLOAT_80611a5c *
                  ((FLOAT_8061aad8 * FLOAT_80611a64 +
                    FLOAT_8061aadc * FLOAT_80611a64 +
                    FLOAT_8061aae0 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c);
        hud_element_set_pos_y((double)y, DAT_806119e0);
    } else {
        hud_element_apply_center_xy((int*)(intptr_t)DAT_806119e0);
        arcsel_clamp_element_y(DAT_806119e0);
    }
}

// Write the selected slot's record to the card. On success (result 0/1) advance to screen 4; on
// error clear the streaming buffer and forward to the shared card-error handler.
void arcade_edit_card_write(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                            unsigned long long r6, unsigned long long r7, unsigned long long r8,
                            unsigned long long r9, unsigned long long r10, unsigned a9)
{
    int tableBase = DAT_80612d58;
    leaf_31e8(0x805e1538, DAT_80612d58 + DAT_80612d50 * 0x38 + 0x10, 0x38);
    int result = card_write_record();
    if (result == 0 || result == 1) {
        DAT_80612d4c = 4;
        hud_screen_push();
    } else {
        memset_wrapper(DAT_80612be8, 0, 0x7ffc);
        FUN_8033a784(r3, r4, r5, r6, r7, r8, r9, r10, a9, result, 0x7ffc, tableBase, 0, 0, 0, 0);
    }
}

// Create the selected slot's record on the card. On success (result 0/1) advance to screen 0xc; on
// error forward to the shared card-error handler.
void arcade_edit_card_create(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                             unsigned long long r6, unsigned long long r7, unsigned long long r8,
                             unsigned long long r9, unsigned long long r10, unsigned a9)
{
    int tableBase = DAT_80612d58;
    leaf_31e8(0x805e1538, DAT_80612d58 + DAT_80612d50 * 0x38 + 0x10, 0x38);
    int kind = DAT_80612d44;
    int result = card_create_record((int)(intptr_t)&DAT_805e1538, DAT_80612be8, DAT_80612d44);
    if (result == 0 || result == 1) {
        DAT_80612d4c = 0xc;
        hud_screen_push();
    } else {
        FUN_8033a784(r3, r4, r5, r6, r7, r8, r9, r10, a9, result, kind, tableBase, 0, 0, 0, 0);
    }
}

// Delete the selected slot's record from the card. On success (result 0/1) reset the selection and
// advance to screen 0xd; on error forward to the shared card-error handler.
void arcade_edit_card_delete(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                             unsigned long long r6, unsigned long long r7, unsigned long long r8,
                             unsigned long long r9, unsigned long long r10, unsigned a9)
{
    int tableBase = DAT_80612d58;
    leaf_31e8(0x805e1538, DAT_80612d58 + DAT_80612d50 * 0x38 + 0x10, 0x38);
    DAT_80612d14 = 3;
    int result = card_delete_record(DAT_80612d50, -0x7fa1eac8);
    if (result == 1 || result == 0) {
        DAT_80612d54 = 0;
        DAT_80612d50 = 0;
        DAT_80612d4c = 0xd;
    } else {
        FUN_8033a784(r3, r4, r5, r6, r7, r8, r9, r10, a9, result, 0x38, tableBase, 0, 0, 0, 0);
    }
}

// Format an event message from the forwarded args and show a yes/no confirmation dialog.
void arcade_edit_confirm_yesno(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                               unsigned long long r6, unsigned long long r7, unsigned long long r8,
                               unsigned long long r9, unsigned long long r10)
{
    notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
    notif_show_yes_no_dialog();
}

// Fill the 8-entry save-slot name table (records at DAT_80612d58+0x10, 0x38 stride): copy each
// slot's display string (max 0x13) and null-terminate at +0x23. Header count at +8 set to 8.
// NON-MATCHING: the _savegpr_ spill is omitted (this function takes no argument).
void arcade_edit_build_slot_names(void)
{
    *(int*)(DAT_80612d58 + 8) = 8;
    for (int i = 0, off = 0; i < 8; i++, off += 0x38) {
        int rec = FUN_803484c4(i);
        int str = get_string_by_id(*(int*)(rec + 8));
        FUN_8038b4cc(DAT_80612d58 + off + 0x10, str, 0x13);
        *(unsigned char*)(DAT_80612d58 + off + 0x23) = 0;
    }
}

// Queue all 8 save-slot assets for streaming (each descriptor primes the streamer via FUN_803484c4).
void arcade_edit_queue_slot_assets(void)
{
    for (int i = 0; i < 8; i++) {
        FUN_803484c4(i);
        stream_queue_raw_asset();
    }
}

// Load a streamed save-slot record into the streaming buffer, validate its magic (0xc77667 / 0x12d).
// Returns 1 and applies the record when valid; 0 (buffer cleared) otherwise.
int arcade_edit_load_slot_record(int index)
{
    memset_wrapper(DAT_80612be8, 0, 0x7ffc);
    FUN_803484c4(index);
    int pending = stream_find_pending_by_name();
    if (pending == 0) {
        return 0;
    }
    leaf_31e8((unsigned)DAT_80612be8, *(unsigned*)(pending + 0x10), *(int*)(pending + 0x14));
    int* buf = (int*)(intptr_t)DAT_80612be8;
    if (buf[0] == 0xc77667 && buf[1] == 0x12d) {
        FUN_80348ca8();
        return 1;
    }
    buf[0] = 0;
    return 0;
}
