// Batch: batch_964 | Module: arcade/challenge editor card save/load screens + card-op state machine
// Address range: 0x8033b928 - 0x8033c518
// Date: 2026-09-12
//
// The remaining pieces of the arcade/challenge editor memory-card save/load UI (continues batches
// 959-963). This TU holds the slot-list child render/update callback, the confirm/list screen build,
// the long card-operation state machine that drives the whole save/load/delete/verify sequence, the
// "please wait / press button" status screen build, and the three flow entry points (challenge /
// save / load). The card state lives in the DAT_80612d** block: screen id DAT_80612d4c, selected
// slot DAT_80612d50, valid flag DAT_80612d54, record table base DAT_80612d58 (count at +8, records
// at +0x10 with 0x38 stride). The scratch record is DAT_805e1538; the streaming buffer is
// DAT_80612be8; the editor context is DAT_80612be4.
//
//   card_slot_screen_render    0x8033b928  slot-list child render/update: act on the focused slot or
//                                          hide the child, then probe the card status.
//   card_confirm_screen_build  0x8033b9d4  build the card confirm/list screen (save vs load layout).
//   card_flow_tick             0x8033be18  card-operation state machine (write/create/delete/load/
//                                          verify sequence, keyed on DAT_80612d4c).
//   card_wait_screen_build     0x8033c2e0  build the "please wait / press button" status screen.
//   card_flow_enter_challenge  0x8033c468  enter the card flow in challenge-save mode.
//   card_flow_enter_save       0x8033c4d8  enter the card flow in editor-save mode.
//   card_flow_enter_load       0x8033c518  enter the card flow in editor-load mode.
//
// NON-MATCHING: card_flow_tick, card_confirm_screen_build and card_flow_enter_challenge are emitted
// with the _savegpr_/_restgpr_ runtime helpers (FUN_803826dc/FUN_80382728 etc.) - pure register
// spill, omitted; the incoming screen handle they spill is expressed as the real `screen` value the
// callee needs. The card status/error handler and the 8 register pass-through slots are forwarded as
// opaque registers (Compile-Clean rule 1/3). Several card helpers whose result is used were typed
// void by Ghidra (card_probe_status/card_poll_pending/card_scan_dir/...) so they are declared
// file-locally with a real int return. This TU includes only common.h.

#include "common.h"

extern "C" {

// card save-state block
extern int   DAT_80612be4;   // editor context base
extern void* DAT_80612be8;   // streaming/scratch buffer (0x7ffc bytes)
extern char  DAT_805e1538;   // scratch save-record (0x38 bytes)
extern int   DAT_805e1564;   // scratch record: timestamp field (seconds-of-day)
extern short DAT_805e156a;   // scratch record: serialized-size / CRC-offset field
extern char  DAT_805d6340;   // default record-name source for the "current map" slot
extern int   DAT_805d59dc;   // timed-message reset target

extern int   DAT_80612614;   // build/mode selector (0x65 => rename-existing path)
extern int   DAT_806119ec;   // active root-screen index
extern int   DAT_8051a820;   // root-screen table (0x1e0 stride)
extern int   DAT_806119e0;   // arcade-select frame element handle

extern int   DAT_80612d14;   // card op sub-state
extern int   DAT_80612d20;   // save-flow branch flag A
extern int   DAT_80612d24;   // "format card" option available flag
extern int   DAT_80612d28;   // save-flow branch flag B
extern int   DAT_80612d2c;   // save-flow branch flag C
extern int   DAT_80612d30;   // editor-vs-challenge mode flag (bit 0)
extern int   DAT_80612d38;   // parent screen handle for navigation
extern int   DAT_80612bf4;   // current parent-screen handle (source for DAT_80612d38)
extern int   DAT_80612d48;   // "new slot" allowed flag
extern int   DAT_80612d3c;   // load-vs-write flag (1 => load the slot)
extern int   DAT_80612d40;   // exit destination selector (0=save, 1=load)
extern int   DAT_80612d44;   // card record kind for create
extern int   DAT_80612d4c;   // next card screen id
extern int   DAT_80612d50;   // selected save slot index
extern char  DAT_80612d54;   // selected slot valid flag
extern int   DAT_80612d58;   // record table base (count +8, records +0x10, 0x38 stride)

extern float FLOAT_8061aae8; // layout constant (item padding)
extern float FLOAT_8061aae4; // layout constant (padding base)
extern float FLOAT_8061aaf8; // layout constant (scroll offset)
extern float FLOAT_8061ab18; // horizontal-scroll origin (grid mode)
extern float FLOAT_80611a5c; // frame layout constant
extern float FLOAT_80611a64; // frame layout constant
extern float FLOAT_80611a6c; // frame layout constant
extern float FLOAT_8061aad8; // frame layout constant
extern float FLOAT_8061aadc; // frame layout constant
extern float FLOAT_8061aae0; // frame layout constant

// screen title/descriptor block + format templates (raw addresses)
extern char  DAT_805e208c;   // card confirm-screen descriptor
extern int   DAT_80611234;   // format template
extern int   DAT_80611230;   // format template
extern int   DAT_8061123c;   // format template "%s %s %s"
extern char  s__s__s_8061123c;

// named cross-module helpers (resolve via shared headers; declared here for call-site-accurate types)
extern int  hud_screen_find_root(int screen);
extern void hud_screen_mark_hidden(int screen);
extern int  hud_screen_all_children_closing(int screen);
extern void hud_screen_register_handler_global(int slot, int strId, int a3, int a4);
extern void hud_element_init_from_layout(unsigned flags, int data, int cancelCb, int confirmCb);
extern void hud_set_item_padding_mode(double amount, int mode, int before, int after);
extern int  hud_set_context_property(int value);
extern unsigned long long hud_add_list_item(void); // returns the new list-item element context
extern void hud_add_padding_item(double height);
extern void hud_add_callback_item(double height);
extern void hud_set_margins(double a, double b, int c);
extern double hud_element_scroll_horizontal(double v, int p);
extern double hud_element_scroll_h_overflow(double dir, double sx, double sy, int p);
extern void hud_element_apply_scroll(void);
extern void hud_element_finalize_frame(void);

extern int  get_string_by_id(int id);
extern int  notif_get_player_data(void);
extern char* notif_get_player_short_name(void);
extern int  notif_set_player_slot(unsigned slot);
extern int  notif_get_player_slot(void);
extern int  notif_is_visible(void);
extern void notif_format_event_message(unsigned long long r3, unsigned long long r4,
                                       unsigned long long r5, unsigned long long r6,
                                       unsigned long long r7, unsigned long long r8,
                                       unsigned long long r9, unsigned long long r10);
extern void notif_show_yes_no_wrapper(void);
extern void notif_show_back_dialog(void);
extern void notif_show_timed_message(void);

extern void input_get_confirm_button_name(void);
extern void card_confirm_overwrite_flag(void);
extern void arcsel_position_frame(void);
extern void arcade_edit_card_exit(void);
extern void card_slot_item_selected(unsigned long long r3, unsigned long long r4,
                                    unsigned long long r5, unsigned long long r6,
                                    unsigned long long r7, unsigned long long r8,
                                    unsigned long long r9, unsigned long long r10);
extern void card_show_status_dialog(unsigned long long r3, unsigned long long r4,
                                    unsigned long long r5, unsigned long long r6,
                                    unsigned long long r7, unsigned long long r8,
                                    unsigned long long r9, unsigned long long r10, int screen,
                                    int code, unsigned a11, unsigned a12, unsigned a13,
                                    unsigned a14, unsigned a15, unsigned a16);
extern int  card_create_record(void* record, void* buffer, int kind);
extern unsigned long long card_table_append(int record);
extern unsigned long long card_table_remove(int slot);
extern int  crc32_buffer(void* buf, unsigned len);
extern int  clock_seconds_of_day(void);
extern int  aveditor_serialize_map(void);
extern int  aveditor_get_record_capacity(int* record);
extern int* challenge_record_find_section(int* record, int tag);
extern void memset_wrapper(int dst, int value, int len);
extern void leaf_31e8(int dst, int src, int len);
extern void card_flow_reset(void);
extern unsigned FUN_80366414(void);   // prime the challenge-editor context

// card status helpers that Ghidra typed void but whose result is used (real int return)
extern int  card_scan_dir(void);
extern int  card_probe_status(void);
extern int  card_poll_pending(void);
extern int  card_read_dir_entry(void);
extern void card_verify_loaded_record(unsigned long long f1, unsigned long long r4,
                                      unsigned long long r5, unsigned long long r6,
                                      unsigned long long r7, unsigned long long r8,
                                      unsigned long long r9, unsigned long long r10);
extern void card_substate_reset(void);

// __save_gpr_28 / __restore_gpr_28: return the incoming screen handle (r28 spill); express directly.
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);

// raw FUN_ helpers not yet named
extern void FUN_80387fb8(unsigned long long f1, unsigned long long r4, unsigned long long r5,
                         unsigned long long r6, unsigned long long r7, unsigned long long r8,
                         unsigned long long r9, unsigned long long r10, int a9, int str, int arg,
                         int a12, int a13, int a14, int a15, int a16);
extern void debug_log_formatted(unsigned long long f1, unsigned long long r4, unsigned long long r5,
                                unsigned long long r6, unsigned long long r7, unsigned long long r8,
                                unsigned long long r9, unsigned long long r10, int fmt, int a10,
                                char* name, unsigned long long cb, int a13, int a14, int a15,
                                int a16);
extern void FUN_8038b510(int* dst, int* src);
extern int  FUN_8038b378(unsigned* recA, unsigned* recB);
extern int  DAT_805e20d0;    // save-slot name-string table (0x16 stride)

} // extern "C"

// -----------------------------------------------------------------------------

// Slot-list child render/update callback. When the child (screen+0x194) is live and its selection
// is valid, either invoke the focused slot's selected-handler (when the mount poll cleared and the
// child's mask bit 0 is unset) or mark the child hidden. Then probe the card status when idle.
void card_slot_screen_render(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                             unsigned long long r6, unsigned long long r7, unsigned long long r8,
                             unsigned long long r9, unsigned long long r10, int screen)
{
    int child = *(int*)(screen + 0x194);
    int root = hud_screen_find_root(*(int*)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    unsigned ready = countLeadingZeros((unsigned)(root - screen)) >> 5;   // 1 when root == screen
    bool handled = false;
    if (child != 0 && -1 < *(int*)(child + 0x14)) {
        if (ready != 0 && (*(unsigned*)(child + 0x28) & 1) == 0) {
            card_slot_item_selected(r3, r4, r5, r6, r7, r8, r9, r10);
            handled = true;
        }
    }
    if (!handled) {
        hud_screen_mark_hidden(screen);
    }
    if (DAT_80612d20 == 0) {
        card_probe_status();
    }
}

// Build the card confirm/list screen. Save mode (DAT_80612d3c==0) shows a player-header row; load
// mode shows a title string. In editor-vs-challenge grid mode (DAT_80612614==0x65) it adds three
// callback/padding rows and horizontal scrolling; otherwise it computes the list origin from the
// frame layout constants. Adds the "new slot" row when DAT_80612d48 is set, then the per-slot rows
// (either the "no records" message or a copy of each record's name), registers the soft-key handlers
// for the current mode, finalizes the frame and positions it.
void card_confirm_screen_build(unsigned long long r3, double r4, double r5, double r6, double r7,
                               double r8, double r9, unsigned long long r10)
{
    int screen = __save_gpr_28();
    hud_element_init_from_layout((~-(unsigned)(DAT_80612614 == 0x65) & 8) | 0x804, 0, 0,
                                 (int)(intptr_t)&card_flow_reset);
    *(int*)(screen + 0x214) = 4;
    hud_set_item_padding_mode((double)FLOAT_8061aae8, 1, 1, 1);
    int ctx = hud_set_context_property(0x20000);

    if (DAT_80612d20 == 0) {
        int player = notif_get_player_data();
        int str = get_string_by_id(0xa70);
        FUN_80387fb8((unsigned long long)ctx, (unsigned long long)r4, (unsigned long long)r5,
                     (unsigned long long)r6, (unsigned long long)r7, (unsigned long long)r8,
                     (unsigned long long)r9, r10, -0x7fa1df74, (int)(intptr_t)&DAT_80611234, str,
                     player + 0x41, 0, 0, 0, 0);
        *(int*)(screen + 0x200) = (int)(intptr_t)&DAT_805e208c;
    } else {
        int str = get_string_by_id(0xe56);
        FUN_80387fb8((unsigned long long)ctx, (unsigned long long)r4, (unsigned long long)r5,
                     (unsigned long long)r6, (unsigned long long)r7, (unsigned long long)r8,
                     (unsigned long long)r9, r10, -0x7fa1df74, (int)(intptr_t)&DAT_80611230, str,
                     0, 0, 0, 0, 0);
        *(int*)(screen + 0x200) = (int)(intptr_t)&DAT_805e208c;
    }

    if (DAT_80612614 == 0x65) {
        hud_add_list_item();
        hud_add_padding_item((double)FLOAT_8061aae8);
        hud_add_callback_item((double)FLOAT_8061aae8);
        hud_add_padding_item((double)FLOAT_8061aae8);
        hud_element_scroll_horizontal((double)FLOAT_8061ab18, 0);
    } else {
        // list-mode origin from the frame layout constants
        double y = FLOAT_80611a64;
        double listY = FLOAT_80611a5c *
                       ((FLOAT_8061aad8 * y + (FLOAT_8061aadc * y + (FLOAT_8061aae0 * y +
                        FLOAT_80611a6c))) - FLOAT_80611a6c) - FLOAT_8061aaf8;
        hud_element_scroll_h_overflow(-listY, (double)FLOAT_8061aae4, (double)FLOAT_8061aae4, 0);
    }

    bool hasNew = DAT_80612d48 != 0;   // "new slot" allowed flag
    // Per-slot rows.
    if (*(int*)(DAT_80612d58 + 8) < 1) {
        if (DAT_80612d40 == 0) {
            char* name = notif_get_player_short_name();
            int str = get_string_by_id(0x3c3);
            debug_log_formatted(0, r4, r5, r6, r7, r8, r9, r10, (int)(intptr_t)&s__s__s_8061123c,
                                str, name, 0, 0, 0, 0, 0);
            hud_add_list_item();
        }
    } else if (DAT_80612d20 == 0) {
        int idx = *(int*)(DAT_80612d58 + 8) + -1;
        int nameOff = idx * 0x16;
        int recOff = idx * 0x38;
        for (; -1 < idx; idx--) {
            hud_set_margins((double)FLOAT_8061aae8, (double)FLOAT_8061aae8, 1);
            FUN_8038b510((int*)(&DAT_805e20d0 + nameOff), (int*)(DAT_80612d58 + recOff + 0x10));
            hud_add_list_item();
            recOff -= 0x38;
            nameOff -= 0x16;
        }
    } else {
        int nameOff = 0, recOff = 0;
        for (int i = 0; i < *(int*)(DAT_80612d58 + 8); i++) {
            FUN_8038b510((int*)(&DAT_805e20d0 + nameOff), (int*)(DAT_80612d58 + recOff + 0x10));
            hud_add_list_item();
            recOff += 0x38;
            nameOff += 0x16;
        }
    }

    hud_element_apply_scroll();
    if ((DAT_80612d30 & 1) == 0) {
        int str = get_string_by_id(0xa5d);
        hud_screen_register_handler_global(1, str, 0, 0);
        if (hasNew) {
            str = get_string_by_id(0xa3c);
            hud_screen_register_handler_global(0, str, 2, 0);
        }
        if (DAT_80612d40 == 1) {
            str = get_string_by_id(0x966);
            hud_screen_register_handler_global(2, str, 2, 0);
        }
    } else {
        if (hasNew) {
            int str = get_string_by_id(0x19c7);
            hud_screen_register_handler_global(0, str, 0, 0);
        }
        int str = get_string_by_id(0xa5d);
        hud_screen_register_handler_global(1, str, 0, 0);
        str = get_string_by_id(0x966);
        hud_screen_register_handler_global(2, str, 2, 0);
    }
    hud_element_finalize_frame();
    arcsel_position_frame();
    __restore_gpr_28();
}

// Card-operation state machine, dispatched on DAT_80612d4c. Runs only while no notification is
// visible and every child screen is closing. Drives the write / create / delete / load / verify
// sequence, showing the status dialog on any error result and advancing the screen id on success.
void card_flow_tick(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                    unsigned long long r6, unsigned long long r7, unsigned long long r8,
                    unsigned long long r9, unsigned long long r10, unsigned a9, unsigned a10,
                    unsigned a11, unsigned a12, unsigned a13, unsigned a14, unsigned a15,
                    unsigned a16)
{
    int screen = __save_gpr_28();
    if (notif_is_visible()) {
        __restore_gpr_28();
        return;
    }
    if (hud_screen_all_children_closing(screen) == 0) {
        __restore_gpr_28();
        return;
    }

    switch (DAT_80612d4c) {
    case 1: {
        int dir;
        if (DAT_80612d20 == 0 && (dir = card_scan_dir()) != 1 && dir != 0) {
            unsigned slot = (DAT_80612d40 == 0) ? 1u : 2u;
            notif_set_player_slot(slot);
            card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, dir, a11, a12, a13,
                                    a14, a15, a16);
            notif_set_player_slot(0xffffffff);
        } else {
            DAT_80612d4c = 2;
        }
        break;
    }
    case 2: {
        int st = card_probe_status();
        if (st == 1) {
            if (FLOAT_8061aae8 <= *(float*)(screen + 0x1b0)) {
                card_confirm_overwrite_flag();
            }
        } else {
            card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, st, a11, a12, a13,
                                    a14, a15, a16);
        }
        break;
    }
    case 4: {
        int st = card_poll_pending();
        if (st == 1) {
            card_verify_loaded_record(r3, r4, r5, r6, r7, r8, r9, r10);
        } else if (st != 0) {
            card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, st, a11, a12, a13,
                                    a14, a15, a16);
        }
        break;
    }
    case 6: {
        int st = card_poll_pending();
        if (st == 1) {
            int off = 0;
            bool matched = false;
            for (int i = 0; i < *(int*)(DAT_80612d58 + 8); i++) {
                if (FUN_8038b378((unsigned*)(DAT_80612d58 + off + 0x10),
                                 (unsigned*)&DAT_805e1538) == 0) {
                    DAT_80612d50 = i;
                    notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
                    notif_show_yes_no_wrapper();
                    matched = true;
                    break;
                }
                off += 0x38;
            }
            if (!matched) {
                DAT_80612d44 = 0;
                DAT_80612d4c = 7;
            }
        } else if (st != 0) {
            card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, st, a11, a12, a13,
                                    a14, a15, a16);
        }
        break;
    }
    case 7: {
        DAT_805e1564 = clock_seconds_of_day();
        FUN_8038b510((int*)&DAT_805d6340, (int*)&DAT_805e1538);
        void* buf = DAT_80612be8;
        if ((DAT_80612d30 & 1) == 0) {
            DAT_805e156a = (short)aveditor_serialize_map();
        } else {
            int* sec = challenge_record_find_section((int*)DAT_80612be8, 0xc77668);
            if (sec == (int*)0) {
                notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
                notif_show_back_dialog();
                break;
            }
            DAT_805e156a = (short)(unsigned short)aveditor_get_record_capacity((int*)buf);
        }
        if (DAT_805e156a == 0) {
            arcade_edit_card_exit();
        } else {
            int crc = crc32_buffer(buf, (unsigned)(unsigned short)DAT_805e156a);
            *(int*)((char*)buf + (unsigned short)DAT_805e156a) = crc;
            DAT_80612d4c = 9;
            DAT_805e156a = (short)((unsigned short)DAT_805e156a + 4);
        }
        break;
    }
    case 9: {
        int st = card_probe_status();
        if (st == 1) {
            int kind = DAT_80612d44;
            int code = card_create_record(&DAT_805e1538, DAT_80612be8, DAT_80612d44);
            DAT_80612d14 = 1;
            if (code == 1 || code == 0) {
                DAT_80612d4c = 0xc;
            } else {
                card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, code,
                                        (unsigned)kind, a12, a13, a14, a15, a16);
            }
        } else {
            card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, st, a11, a12, a13,
                                    a14, a15, a16);
        }
        break;
    }
    case 0xc:
        if (DAT_80612d54 == '\0') {
            leaf_31e8(DAT_80612d58 + DAT_80612d50 * 0x38 + 0x10, 0x805e1538, 0x38);
        } else {
            DAT_80612d50 = (int)card_table_append(0x805e1538);
        }
        DAT_80612d4c = 0xd;
        break;
    case 0xd: {
        int st = card_poll_pending();
        if (st == 1) {
            DAT_80612d14 = 0;
            int pslot = notif_get_player_slot();
            if (pslot == 3) {
                notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
                notif_show_timed_message();
            } else {
                DAT_80612d20 = 1;
                DAT_805d59dc = 0;
                notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
                notif_show_timed_message();
            }
        } else if (st != 0) {
            if (DAT_80612d54 != '\0') {
                card_table_remove(DAT_80612d50);
            }
            card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, st, a11, a12, a13,
                                    a14, a15, a16);
        }
        break;
    }
    case 0xe:
    case 0xf: {
        int st = card_probe_status();
        if (st == 1) {
            int e = card_read_dir_entry();
            if (e == 1 || e == 0) {
                DAT_80612d4c = 0x10;
            } else {
                card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, e, a11, a12, a13,
                                        a14, a15, a16);
            }
        } else {
            card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, st, a11, a12, a13,
                                    a14, a15, a16);
        }
        break;
    }
    case 0x10: {
        int st = card_poll_pending();
        if (st == 1) {
            card_substate_reset();
        } else if (st != 0) {
            card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, screen, st, a11, a12, a13,
                                    a14, a15, a16);
        }
        break;
    }
    default:
        break;
    }
    __restore_gpr_28();
}

// Build the "please wait / press <confirm> to continue" card status screen. In editor mode
// (DAT_80612d30 bit 0 clear) it lays out the confirm-button hint row (string 0x196e) once every
// child screen is closing; in challenge mode it also adds the string-0xe0d row. Positions the frame.
void card_wait_screen_build(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                            unsigned long long r6, unsigned long long r7, unsigned long long r8,
                            unsigned long long r9, unsigned long long r10, int screen)
{
    if ((DAT_80612d30 & 1) == 0) {
        hud_element_init_from_layout(0x20800, 0, 0, 0);
        hud_set_item_padding_mode((double)FLOAT_8061aae8, 1, 1, 1);
        hud_set_context_property(0x20804);
        if (hud_screen_all_children_closing(screen) != 0) {
            input_get_confirm_button_name();
            unsigned long long item = hud_add_list_item();
            int player = notif_get_player_data();
            int str = get_string_by_id(0x196e);
            FUN_80387fb8(item, r4, r5, r6, r7, r8, r9, r10, -0x7fa1d634, str,
                         player + 0x41, 0, 0, 0, 0, 0);
            hud_add_list_item();
        }
    } else {
        hud_element_init_from_layout(0x20000, 0, 0, 0);
        input_get_confirm_button_name();
        hud_add_list_item();
        get_string_by_id(0xe0d);
        unsigned long long item = hud_add_list_item();
        int player = notif_get_player_data();
        int str = get_string_by_id(0x196e);
        FUN_80387fb8(item, r4, r5, r6, r7, r8, r9, r10, -0x7fa1d698, str,
                     player + 0x41, 0, 0, 0, 0, 0);
        hud_add_list_item();
    }
    arcsel_position_frame();
}

// Enter the card flow in challenge-save mode: mark challenge mode, prime the challenge context, and
// stash the two branch flags + the parent-screen navigation handle (spilled r28 pair) before
// resetting the flow. param_3/param_4 are the incoming branch flags.
void card_flow_enter_challenge(int a1, int a2, int a3, int a4)
{
    unsigned long long saved = (unsigned long long)(unsigned)__save_gpr_28();
    DAT_80612d30 = 1;
    FUN_80366414();                 // prime the challenge-editor context
    DAT_80612d3c = 0;
    DAT_80612d40 = 0;
    DAT_80612d20 = 0;
    DAT_80612d24 = a4;
    DAT_80612d28 = a3;
    DAT_80612d2c = (int)saved;
    DAT_80612d38 = (int)(saved >> 0x20);
    card_flow_reset();
    __restore_gpr_28();
}

// Enter the card flow in editor-save mode.
void card_flow_enter_save(void)
{
    DAT_80612d30 = 0;
    DAT_80612d24 = 1;
    DAT_80612d3c = 0;
    DAT_80612d40 = 0;
    DAT_80612d38 = DAT_80612bf4;   // parent screen handle
    card_flow_reset();
}

// Enter the card flow in editor-load mode.
void card_flow_enter_load(void)
{
    DAT_80612d30 = 0;
    DAT_80612d24 = 1;
    DAT_80612d3c = 0;
    DAT_80612d40 = 1;
    DAT_80612d38 = DAT_80612bf4;   // parent screen handle
    card_flow_reset();
}
