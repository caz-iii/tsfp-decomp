// Batch: batch_962 | Module: arcade/challenge editor card status-dialog dispatch + card-op handlers
// Address range: 0x8033a43c - 0x8033ad38
// Date: 2026-09-12
//
// The card save/load result reporting and the remaining card-flow handlers for the arcade/challenge
// editor. The centrepiece is card_show_status_dialog (FUN_8033a784) - the shared status/error
// handler referenced by the card write/create/delete paths (batches 959/961): a switch over the
// card result code that formats the matching localized message (string ids 0x1945-0x195f) and shows
// the appropriate dialog kind (option / option-3 / retry / yes-no). Around it: begin-save, the
// save scan+dedup path, the card-op dispatch by sub-state, the slot-select input handler, and the
// screen-close error report.
//
//   card_begin_save            0x8033a43c  arm a save: create mode, copy record, screen 7, elem fx.
//   card_save_scan_dedup       0x8033a4cc  copy the table, scan the card, reuse/create a slot.
//   card_op_dispatch           0x8033a600  dispatch the card op by sub-state (write/create/delete).
//   card_create_dispatch       0x8033a684  create-path dispatch by sub-state.
//   card_goto_phase2           0x8033a6dc  advance to card screen 0xf (phase 2).
//   card_confirm_delete_dialog 0x8033a710  show the confirm-delete yes/no dialog (string 0x1967).
//   card_show_status_dialog    0x8033a784  format+show the card status/error dialog by result code.
//   card_slot_select_input     0x8033ac8c  slot-select input: pick slot 0/1, or 999 => new slot.
//   card_screen_is_final       0x8033ad24  1 when the next screen id is past the card range (>0x10).
//   card_screen_close_report   0x8033ad38  on screen close, report the pending card result once.
//
// NON-MATCHING: the card handlers forward the 8 register pass-through slots (f1/r3..r10) plus the
// four trailing GPR args to card_show_status_dialog; expressed as opaque forwarded registers
// (Compile-Clean rule 1/3). card_save_scan_dedup's allocator call is the varargs-noise form in the
// decompiler; collapsed to the real mem_alloc(size, type) (declared in generated_decls). The
// _savegpr_/_restgpr_ spill helpers around card_save_scan_dedup are pure register spill, omitted.
// This TU includes only common.h so the file-local decls can carry call-site-accurate types.

#include "common.h"

extern "C" {

extern int   DAT_80612614;   // build/mode selector (0x66 => alternate buffer size path)
extern int   DAT_80612d10;   // memcard busy/dismiss flag
extern int   DAT_80612d14;   // card op sub-state (1=create,2=write,3=delete,4=load,5+=done)
extern int   DAT_80612d18;   // pending card result code (reported on screen close)
extern int   DAT_80612d1c;   // screen-close report guard (1 once reported)
extern int   DAT_80612d3c;   // skip-CRC / accept-record flag
extern int   DAT_80612d44;   // card record kind for create
extern int   DAT_80612d48;   // overwrite-confirm flag
extern int   DAT_80612d4c;   // next card screen id
extern int   DAT_80612d50;   // selected save slot index
extern int   DAT_80612d54;   // selected slot valid flag
extern int   DAT_80612d58;   // record table base (count at +8, records at +0x10, 0x38 stride)
extern int   DAT_805e1538;   // scratch save-record (0x38 bytes)
extern int   DAT_805e1570;   // scratch message buffer
extern int   DAT_80611228;   // shared format-arg block

// named cross-module helpers (resolve through generated_decls.h)
extern void leaf_31e8(unsigned dst, unsigned src, int len);
extern int  mem_alloc(int size, int type);
extern void mem_free_top(unsigned ptr);
extern void hud_screen_push(void);
extern void hud_element_reset_transition(int element);
extern int  hud_screen_all_children_closing(int screen);
extern void hud_screen_rebuild_items(void);
extern void card_mount_reset(void);
extern int  card_scan_dir(void);
extern void memcard_check_restart(void);
extern void memcard_check_set_phase2(void);
extern void memcard_check_slots(int slotOut, int nameOut, int* errorCount);
extern int  challenge_record_buffer_size(void);
extern int  challenge_record_buffer_size_alt(void);
extern void card_flow_reset(void);
extern int  notif_get_player_data(void);
extern void notif_set_player_data(int idx);
extern void notif_format_event_message(unsigned long long r3, unsigned long long r4,
                                       unsigned long long r5, unsigned long long r6,
                                       unsigned long long r7, unsigned long long r8,
                                       unsigned long long r9, unsigned long long r10);
extern void notif_show_yes_no_dialog(void);
extern void notif_show_yes_no_localized_dialog(void);
extern void notif_show_option_dialog(void);
extern void notif_show_option_3_dialog(void);
extern void notif_show_option_retry_dialog(void);
extern int  get_string_by_id(int id);
extern void arcade_edit_build_slot_names(void);
extern void arcade_edit_card_create(unsigned long long r3, unsigned long long r4,
                                    unsigned long long r5, unsigned long long r6,
                                    unsigned long long r7, unsigned long long r8,
                                    unsigned long long r9, unsigned long long r10, unsigned a9);
extern void arcade_edit_card_write(unsigned long long r3, unsigned long long r4,
                                   unsigned long long r5, unsigned long long r6,
                                   unsigned long long r7, unsigned long long r8,
                                   unsigned long long r9, unsigned long long r10, unsigned a9);
extern void arcade_edit_card_delete(unsigned long long r3, unsigned long long r4,
                                    unsigned long long r5, unsigned long long r6,
                                    unsigned long long r7, unsigned long long r8,
                                    unsigned long long r9, unsigned long long r10, unsigned a9);

// raw FUN_ helpers not yet named (call-site-accurate types)
extern int  FUN_8038b378(unsigned* recA, unsigned* recB);           // record equality compare (0 == equal)
extern void FUN_80387fb8(unsigned long long f1, unsigned long long r4, unsigned long long r5,
                         unsigned long long r6, unsigned long long r7, unsigned long long r8,
                         unsigned long long r9, unsigned long long r10, int buf, int str, int arg,
                         unsigned a12, unsigned a13, unsigned a14, unsigned a15,
                         unsigned a16);                             // build a formatted notification message
extern void FUN_8038b4a0(int buf, int str);                        // append a string to the message buffer
extern void format_string_varargs(unsigned long long acc, unsigned long long d1, unsigned long long d2,
                                  unsigned long long d3, unsigned long long d4, unsigned long long d5,
                                  unsigned long long g6, unsigned long long g7, int* out_buf, int fmt,
                                  void* a, int b, int c, unsigned d, unsigned e, unsigned f);

} // extern "C"

// Forward decl: card_save_scan_dedup (defined below) calls the status dialog defined further down.
void card_show_status_dialog(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                             unsigned long long r6, unsigned long long r7, unsigned long long r8,
                             unsigned long long r9, unsigned long long r10, unsigned a9, int code,
                             unsigned a11, unsigned a12, unsigned a13, unsigned a14,
                             unsigned a15, unsigned a16);

// -----------------------------------------------------------------------------

// Arm a card save: mark create mode, clear the slot-valid flag, copy the selected record into the
// scratch buffer, advance to screen 7, set the element's transition flags, and push the screen.
void card_begin_save(int element)
{
    DAT_80612d44 = 1;
    DAT_80612d54 = 0;
    leaf_31e8(0x805e1538, DAT_80612d58 + DAT_80612d50 * 0x38 + 0x10, 0x38);
    DAT_80612d4c = 7;
    *(unsigned*)(element + 0x40) |= 0x20002;
    hud_element_reset_transition(element);
    hud_screen_push();
}

// Save with dedup: allocate a temp record-buffer copy, scan the card directory, then look for an
// existing record equal to the scratch record. If found, reuse that slot (create). Otherwise report
// "no space" (code 0x14). Any card-scan error is forwarded to the status dialog. NON-MATCHING: the
// allocator/free are the decompiler's varargs form, collapsed to the real 2-arg mem_alloc(size,type).
void card_save_scan_dedup(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                          unsigned long long r6, unsigned long long r7, unsigned long long r8,
                          unsigned long long r9, unsigned long long r10, unsigned a9,
                          unsigned a11, unsigned a12, unsigned a13, unsigned a14,
                          unsigned a15, unsigned a16)
{
    int size = (DAT_80612614 == 0x66) ? challenge_record_buffer_size_alt()
                                       : challenge_record_buffer_size();
    unsigned temp = (unsigned)mem_alloc(size, 0);
    leaf_31e8(temp, DAT_80612d58, size);
    int scan = card_scan_dir();
    if (scan == 1 || scan == 0) {
        int off = 0;
        for (int i = 0; i < *(int*)(DAT_80612d58 + 8); i++) {
            if (FUN_8038b378((unsigned*)(DAT_80612d58 + off + 0x10),
                             (unsigned*)&DAT_805e1538) == 0) {
                mem_free_top(temp);
                DAT_80612d44 = 1;
                DAT_80612d50 = i;
                arcade_edit_card_create(r3, r4, r5, r6, r7, r8, r9, r10, a9);
                return;
            }
            off += 0x38;
        }
        leaf_31e8(DAT_80612d58, temp, size);
        mem_free_top(temp);
        card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, a9, 0x14, size,
                                a12, a13, a14, a15, a16);
    } else {
        leaf_31e8(DAT_80612d58, temp, size);
        mem_free_top(temp);
        card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, a9, scan, size,
                                a12, a13, a14, a15, a16);
    }
}

// Dispatch the pending card op by sub-state: 1=create, 2=write, 3=delete; 4 keeps screen 1; anything
// else resets the flow. When the card is busy, restart the mount check instead.
void card_op_dispatch(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                      unsigned long long r6, unsigned long long r7, unsigned long long r8,
                      unsigned long long r9, unsigned long long r10, unsigned a9)
{
    if (DAT_80612d10 != 0) {
        memcard_check_restart();
        return;
    }
    if (DAT_80612d14 == 3) {
        arcade_edit_card_delete(r3, r4, r5, r6, r7, r8, r9, r10, a9);
        return;
    }
    if (DAT_80612d14 < 3) {
        if (DAT_80612d14 == 1) {
            arcade_edit_card_create(r3, r4, r5, r6, r7, r8, r9, r10, a9);
            return;
        }
        if (DAT_80612d14 > 0) {
            arcade_edit_card_write(r3, r4, r5, r6, r7, r8, r9, r10, a9);
            return;
        }
    } else if (DAT_80612d14 < 5) {
        DAT_80612d4c = 1;
        return;
    }
    card_flow_reset();
}

// Create-path dispatch by sub-state: 1=create (record kind 0), 2..4=stay on screen 1, else reset.
void card_create_dispatch(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                          unsigned long long r6, unsigned long long r7, unsigned long long r8,
                          unsigned long long r9, unsigned long long r10, unsigned a9)
{
    if (DAT_80612d14 == 1) {
        DAT_80612d44 = 0;
        arcade_edit_card_create(r3, r4, r5, r6, r7, r8, r9, r10, a9);
    } else if (DAT_80612d14 < 1 || DAT_80612d14 > 4) {
        card_flow_reset();
    } else {
        DAT_80612d4c = 1;
    }
}

// Advance to card screen 0xf (phase 2), triggering the mount phase-2 check if the card is busy.
void card_goto_phase2(void)
{
    if (DAT_80612d10 != 0) {
        memcard_check_set_phase2();
    }
    DAT_80612d4c = 0xf;
}

// Show the confirm-delete yes/no dialog (string 0x1967, player-data arg).
void card_confirm_delete_dialog(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                                unsigned long long r6, unsigned long long r7, unsigned long long r8,
                                unsigned long long r9, unsigned long long r10)
{
    unsigned char msg[256];
    int player = notif_get_player_data();
    int str = get_string_by_id(0x1967);
    FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, (int)(intptr_t)msg, str, player + 0x41,
                 0, 0, 0, 0, 0);
    notif_show_yes_no_dialog();
}

// Format and show the card status/error dialog for a result code. Each case selects a localized
// message (string ids 0x1945-0x195f) and a dialog kind; the busy-card cases append the "insert card"
// string (0x1983). Codes: 2/0x10 slot list, 4 slot check, 9 yes/no, 0xd/0xe/0xf/0x11..0x15 various
// card errors; default is the generic failure message (0x195f).
void card_show_status_dialog(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                             unsigned long long r6, unsigned long long r7, unsigned long long r8,
                             unsigned long long r9, unsigned long long r10, unsigned a9, int code,
                             unsigned a11, unsigned a12, unsigned a13, unsigned a14,
                             unsigned a15, unsigned a16)
{
    int player;
    int str;
    switch (code) {
    case 2:
    case 0x10:
        player = notif_get_player_data();
        str = get_string_by_id(0x195c);
        format_string_varargs(r3, r4, r5, r6, r7, r8, r9, r10, (int*)&DAT_805e1570, str,
                              &DAT_80611228, player + 0x41, 5, a14, a15, a16);
        if (DAT_80612d10 != 0) {
            FUN_8038b4a0(-0x7fa1ea90, get_string_by_id(0x1983));
        }
        notif_show_option_dialog();
        break;
    case 4: {
        int slotOut = 0, nameOut = 0, errorCount = 0;
        memcard_check_slots((int)(intptr_t)&slotOut, (int)(intptr_t)&nameOut, &errorCount);
        if (errorCount == 0) {
            str = get_string_by_id(0x1946);
            FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, 0x805e0000,
                         a12, a13, a14, a15, a16);
        } else {
            player = notif_get_player_data();
            str = get_string_by_id(0x1945);
            FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, player + 0x41,
                         a12, a13, a14, a15, a16);
        }
        if (DAT_80612d10 != 0) {
            FUN_8038b4a0(-0x7fa1ea90, get_string_by_id(0x1983));
        }
        notif_show_option_dialog();
        break;
    }
    case 9:
        notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
        notif_show_yes_no_localized_dialog();
        break;
    case 0xd:
        player = notif_get_player_data();
        str = get_string_by_id(0x194a);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, player + 0x41,
                     a12, a13, a14, a15, a16);
        if (DAT_80612d10 != 0) {
            FUN_8038b4a0(-0x7fa1ea90, get_string_by_id(0x1983));
        }
        notif_show_option_dialog();
        break;
    case 0xe:
        player = notif_get_player_data();
        str = get_string_by_id(0x1947);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, player + 0x41,
                     a12, a13, a14, a15, a16);
        notif_show_option_3_dialog();
        break;
    case 0xf:
        player = notif_get_player_data();
        str = get_string_by_id(0x1949);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, player + 0x41,
                     a12, a13, a14, a15, a16);
        notif_show_option_dialog();
        break;
    case 0x11:
        player = notif_get_player_data();
        str = get_string_by_id(0x1948);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, player + 0x41,
                     a12, a13, a14, a15, a16);
        notif_show_option_3_dialog();
        break;
    case 0x12:
        player = notif_get_player_data();
        str = get_string_by_id(0x1949);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, player + 0x41,
                     a12, a13, a14, a15, a16);
        notif_show_option_dialog();
        break;
    case 0x13:
        player = notif_get_player_data();
        str = get_string_by_id(0x1959);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, player + 0x41,
                     a12, a13, a14, a15, a16);
        if (DAT_80612d10 != 0) {
            FUN_8038b4a0(-0x7fa1ea90, get_string_by_id(0x1983));
        }
        notif_show_option_dialog();
        break;
    case 0x14:
        player = notif_get_player_data();
        str = get_string_by_id(0x1957);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, player + 0x41,
                     a12, a13, a14, a15, a16);
        notif_show_option_retry_dialog();
        break;
    case 0x15:
        player = notif_get_player_data();
        str = get_string_by_id(0x195e);
        format_string_varargs(r3, r4, r5, r6, r7, r8, r9, r10, (int*)&DAT_805e1570, str,
                              &DAT_80611228, player + 0x41, 5, a14, a15, a16);
        notif_show_option_dialog();
        break;
    default:
        player = notif_get_player_data();
        str = get_string_by_id(0x195f);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1ea90, str, player + 0x41,
                     a12, a13, a14, a15, a16);
        if (DAT_80612d10 != 0) {
            FUN_8038b4a0(-0x7fa1ea90, get_string_by_id(0x1983));
        }
        notif_show_option_dialog();
        break;
    }
}

// Slot-select input handler. The selection code lives at screen+0x188: 0 or 1 selects that save
// slot (sub-state 4, screen 1); 999 arms a brand-new slot (accept-any-record, rebuild slot names).
void card_slot_select_input(int screen)
{
    int sel = *(int*)(screen + 0x188);
    if (sel < 0 || sel > 1) {
        if (sel == 999) {
            notif_set_player_data(0xffffffff);
            DAT_80612d3c = 1;
            arcade_edit_build_slot_names();
            DAT_80612d48 = 0;
            hud_screen_push();
        }
    } else {
        DAT_80612d3c = 0;
        notif_set_player_data(*(int*)(screen + 0x188));
        DAT_80612d14 = 4;
        DAT_80612d4c = 1;
        hud_screen_push();
    }
}

// Return 1 when the next card screen id is past the card screen range (> 0x10), i.e. the final
// screen. Implemented via countLeadingZeros(0x10 - screen_id) >> 5 (sign test on the difference).
unsigned card_screen_is_final(void)
{
    return countLeadingZeros(0x10 - DAT_80612d4c) >> 5;
}

// On screen close, once all child screens are closing, report the pending card result (DAT_80612d18)
// through the status dialog exactly once (guarded by DAT_80612d1c), then rebuild the screen items.
void card_screen_close_report(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                              unsigned long long r6, unsigned long long r7, unsigned long long r8,
                              unsigned long long r9, unsigned long long r10, int screen,
                              unsigned a11, unsigned a12, unsigned a13, unsigned a14,
                              unsigned a15, unsigned a16)
{
    if (hud_screen_all_children_closing(screen) != 0 && DAT_80612d1c != 1) {
        notif_set_player_data(DAT_80612d18);
        card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, (unsigned)screen, DAT_80612d1c,
                                a11, a12, a13, a14, a15, a16);
        DAT_80612d1c = 1;
        hud_screen_rebuild_items();
    }
}
