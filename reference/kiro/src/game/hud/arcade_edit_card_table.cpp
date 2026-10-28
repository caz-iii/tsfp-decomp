// Batch: batch_961 | Module: arcade/challenge editor card record-table ops + card screen handlers
// Address range: 0x80339f64 - 0x8033a390
// Date: 2026-09-12
//
// The in-memory record table (DAT_80612d58) editing primitives plus the card-flow screen handlers
// for the arcade/challenge editor. The record table is a header (count at +8) followed by fixed
// 0x38-byte records starting at +0x10; each record carries a 16-bit slot index at +0x40 (i.e. +0x30
// within the 0x38 record body). These functions add/remove records, find the first free slot index,
// and drive the card save/load screens (mount reset, delete, verify-load with CRC + deserialize).
//
//   card_table_first_free_index   0x80339f64  first unused slot index (0..0x81) across the records.
//   card_table_append             0x80339ff0  append a 0x38-byte record, return its index or -1.
//   card_table_remove             0x8033a054  remove the record at an index (shift the tail down).
//   card_screen_goto_slots        0x8033a0bc  go to card screen 1 (slot list).
//   card_flow_reset               0x8033a0f0  reset card state: clear buffer, reset mount, push.
//   card_confirm_overwrite_flag   0x8033a12c  set the overwrite-confirm flag, push screen.
//   card_exit_then_load_models    0x8033a18c  run the save-exit dispatcher then ensure models load.
//   card_verify_loaded_record     0x8033a1b0  CRC-verify the loaded record; deserialize or prompt.
//   card_substate_reset           0x8033a350  reset the card sub-state (screen 1 or dismiss prompt).
//   card_delete_selected_record   0x8033a390  delete the selected slot record; handle + push screen.
//
// NON-MATCHING: card_exit_then_load_models / card_verify_loaded_record / card_delete_selected_record
// forward the 8 register pass-through slots (f1/r3..r10) to shared helpers; expressed as opaque
// forwarded registers (Compile-Clean rule 1/3). card_delete_selected_record duplicates the
// arcade_edit_card_delete body but additionally pushes the screen at the end (a separate entry). The
// CRC length in card_verify_loaded_record is read from the buffer at (DAT_805e156a - 4).
// This TU includes only common.h so the file-local FUN_ externs can carry call-site-accurate types.

#include "common.h"

extern "C" {

extern int   DAT_80612d58;   // record table base (count at +8, records at +0x10, 0x38 stride)
extern int   DAT_80612be8;   // global streamed record buffer
extern int   DAT_805e1538;   // scratch save-record (0x38 bytes)
extern int   DAT_805e156a;   // offset of the CRC field within the loaded record
extern int   DAT_80612d10;   // memcard busy/dismiss flag
extern int   DAT_80612d14;   // card op sub-state
extern int   DAT_80612d20;   // save-flow branch flag A
extern int   DAT_80612d30;   // editor-vs-challenge mode flag (bit 0)
extern int   DAT_80612d3c;   // skip-CRC flag (accept loaded record unconditionally)
extern int   DAT_80612d40;   // exit destination selector
extern int   DAT_80612d48;   // overwrite-confirm flag
extern int   DAT_80612d4c;   // next card screen id
extern int   DAT_80612d50;   // selected save slot index
extern int   DAT_80612d54;   // selected slot valid flag
extern int   DAT_805d59a0;   // loaded record roster count cache
extern int   DAT_805d59dc;   // timed-message state reset target
extern float FLOAT_80612d34; // card-flow animation timer
extern float FLOAT_8061aae4; // reset value for the card-flow timer

// named cross-module helpers (resolve through generated_decls.h)
extern void leaf_31e8(unsigned dst, unsigned src, int len);
extern int  memset_wrapper(int dst, int value, int len);
extern void hud_screen_push(void);
extern void card_mount_reset(void);
extern void memcard_check_dismiss(void);
extern int  card_delete_record(int slot, int tag);
extern unsigned crc32_buffer_matches(int buffer, int length_off, int expected);
extern int* challenge_record_find_section(int* record, int tag);
extern void challenge_record_buffer_clear(void);
extern void notif_format_event_message(unsigned long long r3, unsigned long long r4,
                                       unsigned long long r5, unsigned long long r6,
                                       unsigned long long r7, unsigned long long r8,
                                       unsigned long long r9, unsigned long long r10);
extern void notif_show_timed_message(void);
extern void notif_show_option_save_dialog(void);

// raw FUN_ helpers not yet named
extern unsigned long long arcade_edit_card_exit(void);       // save-exit dispatcher (batch_959)
extern void aveditor_ensure_models_loaded(unsigned long long f1, unsigned long long r4,
                                          unsigned long long r5, unsigned long long r6,
                                          unsigned long long r7, unsigned long long r8,
                                          unsigned long long r9, unsigned long long r10);
extern int  aveditor_deserialize_map(unsigned long long f1, unsigned long long r4,
                                     unsigned long long r5, unsigned long long r6,
                                     unsigned long long r7, unsigned long long r8,
                                     unsigned long long r9, unsigned long long r10);
extern void aveditor_view_from_camera(void);
extern void aveditor_fit_view_to_bounds(void);
extern unsigned long long FUN_80346df0(int arg);            // build a status/event id for the message
extern void FUN_8033a784(unsigned long long f1, unsigned long long r4, unsigned long long r5,
                         unsigned long long r6, unsigned long long r7, unsigned long long r8,
                         unsigned long long r9, unsigned long long r10, unsigned a9, int status,
                         int a11, int a12, unsigned a13, unsigned a14, unsigned a15,
                         unsigned a16);                     // shared card-error handler

} // extern "C"

enum {
    CARD_TABLE_MAX  = 0x82,   // max records in the table
    CARD_REC_STRIDE = 0x38,   // record byte stride
};

// -----------------------------------------------------------------------------

// Find the first unused slot index (0..0x81). Marks a 0x82-entry bitmap for every record's slot
// index (16-bit field at record+0x30, i.e. table+rec*0x38+0x40), then returns the index of the
// first unmarked entry (== count of leading used indices).
int card_table_first_free_index(void)
{
    char used[CARD_TABLE_MAX + 2];
    memset_wrapper((int)(intptr_t)used, 0, CARD_TABLE_MAX);
    int off = 0;
    for (int i = 0; i < *(int*)(DAT_80612d58 + 8); i++) {
        unsigned short idx = *(unsigned short*)(DAT_80612d58 + off + 0x40);
        off += CARD_REC_STRIDE;
        used[idx] = 1;
    }
    for (int i = 0; i < CARD_TABLE_MAX; i++) {
        if (used[i] == 0) {
            return i;
        }
    }
    return CARD_TABLE_MAX;
}

// Append a 0x38-byte record (copied from `source`) to the table if it is not full. Returns the new
// record index, or -1 when the table already holds CARD_TABLE_MAX records.
int card_table_append(unsigned source)
{
    int count = *(int*)(DAT_80612d58 + 8);
    if (count >= CARD_TABLE_MAX) {
        return -1;
    }
    *(int*)(DAT_80612d58 + 8) = count + 1;
    leaf_31e8(DAT_80612d58 + count * CARD_REC_STRIDE + 0x10, source, CARD_REC_STRIDE);
    return count;
}

// Remove the record at `index`, shifting all following records down by one and decrementing the
// count. No-op when index is out of range.
void card_table_remove(unsigned index)
{
    unsigned count = *(unsigned*)(DAT_80612d58 + 8);
    if (index < count) {
        if ((int)index < (int)(count - 1)) {
            int rec = DAT_80612d58 + index * CARD_REC_STRIDE;
            leaf_31e8(rec + 0x10, rec + 0x48, ((count - index) - 1) * CARD_REC_STRIDE);
        }
        *(int*)(DAT_80612d58 + 8) = *(int*)(DAT_80612d58 + 8) - 1;
    }
}

// Navigate to the card slot-list screen (screen id 1).
void card_screen_goto_slots(void)
{
    DAT_80612d4c = 1;
    hud_screen_push();
}

// Reset the card flow: restore the animation timer, clear the record buffer, reset the mount state,
// and push the screen.
void card_flow_reset(void)
{
    FLOAT_80612d34 = FLOAT_8061aae4;
    challenge_record_buffer_clear();
    card_mount_reset();
    hud_screen_push();
}

// Set the overwrite-confirm flag (1 when exiting to destination 1 and the record header +4 is not
// -1, i.e. a record already exists) and push the screen.
void card_confirm_overwrite_flag(void)
{
    unsigned char confirm = 0;
    if (DAT_80612d40 == 1 && *(int*)(DAT_80612d58 + 4) != -1) {
        confirm = 1;
    }
    DAT_80612d48 = confirm;
    hud_screen_push();
}

// Run the save-exit dispatcher, then ensure the editor models are loaded (its float result feeds
// the models-load call).
void card_exit_then_load_models(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                                unsigned long long r6, unsigned long long r7, unsigned long long r8,
                                unsigned long long r9, unsigned long long r10)
{
    unsigned long long result = arcade_edit_card_exit();
    aveditor_ensure_models_loaded(result, r4, r5, r6, r7, r8, r9, r10);
}

// Verify a freshly loaded record: accept it when the skip-CRC flag is set or the buffer CRC matches.
// In challenge mode a valid record shows a timed message and caches the roster count; otherwise it
// deserializes the map (fitting the view) and shows the loaded message. A failed CRC clears the
// buffer and shows the save-option dialog.
void card_verify_loaded_record(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                               unsigned long long r6, unsigned long long r7, unsigned long long r8,
                               unsigned long long r9, unsigned long long r10)
{
    bool valid = false;
    if (DAT_80612d3c != 0 ||
        crc32_buffer_matches(DAT_80612be8, DAT_805e156a - 4,
                             *(int*)(DAT_80612be8 + (DAT_805e156a - 4))) != 0) {
        valid = true;
    }
    if (valid && (DAT_80612d30 & 1) != 0) {
        DAT_80612d14 = 0;
        DAT_805d59dc = 0;
        notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
        notif_show_timed_message();
        DAT_80612d20 = 1;
        int* flags = challenge_record_find_section((int*)(intptr_t)DAT_80612be8, 0xc77668);
        DAT_805d59a0 = (unsigned)*(unsigned short*)((int)flags + 10);
        return;
    }
    if (valid &&
        aveditor_deserialize_map(r3, r4, r5, r6, r7, r8, r9, r10) != 0) {
        DAT_80612d14 = 0;
        aveditor_view_from_camera();
        aveditor_fit_view_to_bounds();
        unsigned long long status = FUN_80346df0(1);
        DAT_805d59dc = 0;
        notif_format_event_message(status, r4, r5, r6, r7, r8, r9, r10);
        notif_show_timed_message();
        DAT_80612d20 = 1;
        return;
    }
    memset_wrapper(DAT_80612be8, 0, 0x7ffc);
    notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
    notif_show_option_save_dialog();
}

// Reset the card sub-state: when the card is idle go to screen 1, otherwise dismiss the prompt.
void card_substate_reset(void)
{
    DAT_80612d14 = 0;
    if (DAT_80612d10 == 0) {
        DAT_80612d4c = 1;
    } else {
        memcard_check_dismiss();
    }
}

// Delete the selected slot's record from the card, reset the selection on success (advance to screen
// 0xd) or forward to the shared card-error handler, then push the screen.
void card_delete_selected_record(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                                 unsigned long long r6, unsigned long long r7, unsigned long long r8,
                                 unsigned long long r9, unsigned long long r10, unsigned a9)
{
    int tableBase = DAT_80612d58;
    leaf_31e8(0x805e1538, DAT_80612d58 + DAT_80612d50 * CARD_REC_STRIDE + 0x10, CARD_REC_STRIDE);
    int result = card_delete_record(DAT_80612d50, -0x7fa1eac8);
    DAT_80612d14 = 3;
    if (result == 1 || result == 0) {
        DAT_80612d54 = 0;
        DAT_80612d50 = 0;
        DAT_80612d4c = 0xd;
    } else {
        FUN_8033a784(r3, r4, r5, r6, r7, r8, r9, r10, a9, result, CARD_REC_STRIDE, tableBase,
                     0, 0, 0, 0);
    }
    hud_screen_push();
}
