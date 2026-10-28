// Batch: batch_963 | Module: arcade/challenge editor card slot-list screen build + slot-name entry
// Address range: 0x8033ad9c - 0x8033b7b0
// Date: 2026-09-12
//
// The card save/load slot-list screen: build the scrollable slot list (poll the mount, add the
// header/callback items and the per-slot labels, position the frame), the slot-name text-entry
// keyboard flow (open popup, confirm/cancel handlers, new-slot creation), a per-slot timestamp
// render callback, and the slot action dispatch (write or load the selected slot). Continues the
// card save flow from batches 959-962; the record table is DAT_80612d58, the scratch record is
// DAT_805e1538, the frame element is DAT_806119e0.
//
//   card_slot_list_build       0x8033ad9c  build the scrollable slot-list screen.
//   card_slot_goto_screen1     0x8033b13c  go to card screen 1, clear the slot-valid flag.
//   card_name_confirm          0x8033b178  name-entry confirm: overwrite an existing slot or create.
//   card_name_cancel           0x8033b25c  name-entry cancel: pop the current screen.
//   card_name_confirm_thunk    0x8033b27c  thunk to card_name_confirm.
//   card_rename_popup_open     0x8033b29c  open the slot-rename keyboard popup.
//   card_new_slot_begin        0x8033b324  begin a new slot: seed the record, open the name popup.
//   card_slot_timestamp_draw   0x8033b438  per-slot timestamp render callback.
//   card_slot_action           0x8033b578  action on the selected slot: write it, or load it.
//   card_slot_item_selected    0x8033b7b0  slot-list item selected handler.
//
// NON-MATCHING: card_slot_list_build / card_name_confirm / card_slot_item_selected read their screen
// pointer from a _savegpr_-spilled register; expressed as the real `screen` parameter (Compile-Clean
// rule 3), and the _savegpr_/_restgpr_ helpers are omitted. The double->float layout math in
// card_slot_list_build / card_slot_timestamp_draw reproduces the decompiler's mixed fp arithmetic.
// The 8 register pass-through slots forwarded to notif/status helpers are opaque forwarded registers.
// This TU includes only common.h so the file-local decls can carry call-site-accurate types.

#include "common.h"

extern "C" {

extern int   DAT_80612614;   // build/mode selector (0x65 => rename-existing path)
extern int   DAT_80612d18;   // pending card result code
extern int   DAT_80612d14;   // card op sub-state (2 => write)
extern int   DAT_80612d1c;   // card sub-state / report guard
extern int   DAT_80612d20;   // save-flow branch flag A
extern int   DAT_80612d24;   // "format card" option available flag
extern int   DAT_80612d30;   // editor-vs-challenge mode flag (bit 0)
extern int   DAT_80612d3c;   // load-vs-write flag (1 => load the slot)
extern int   DAT_80612d40;   // exit destination selector (0=save, 1=load)
extern int   DAT_80612d44;   // card record kind for create
extern int   DAT_80612d4c;   // next card screen id
extern int   DAT_80612d50;   // selected save slot index
extern int   DAT_80612d54;   // selected slot valid flag
extern int   DAT_80612d58;   // record table base (count at +8, records at +0x10, 0x38 stride)
extern int   DAT_80612be8;   // global streamed record buffer
extern char  DAT_805e1538;   // scratch save-record (name text at +0)
extern short DAT_805e1568;   // scratch record: new slot index field
extern char  DAT_805e156a;   // scratch record: CRC-offset field byte
extern char  DAT_805e17d4;   // rename-source name buffer (mode 0x65)
extern int   DAT_8061122c;   // default slot-name template
extern int   DAT_80611228;   // shared format-arg block
extern int   DAT_806109c0;   // mount-poll fraction numerator (int -> double bias source)
extern int   DAT_806119e0;   // arcade-select frame element handle
extern int   DAT_806119ec;   // active root-screen index
extern int   DAT_8051a820;   // root-screen table (0x1e0 stride)
extern int   DAT_805d59a0;   // loaded record roster count cache
extern int   DAT_805d59dc;   // timed-message reset target

extern float FLOAT_806108e8; // mount-progress timer scale
extern float FLOAT_80612d34; // card-flow animation timer
extern float FLOAT_8061aae4; // layout constant (padding base)
extern float FLOAT_8061aae8; // layout constant (item padding)
extern float FLOAT_8061aaec; // layout constant (list origin)
extern float FLOAT_8061aaf4; // layout constant (item pad amount)
extern float FLOAT_8061aaf8; // layout constant (scroll offset)
extern float FLOAT_8061ab0c; // layout constant (rename-mode origin)
extern float FLOAT_8061ab10; // layout constant (timestamp scale)
extern float FLOAT_80611a5c; // frame layout constant
extern float FLOAT_80611a64; // frame layout constant
extern float FLOAT_80611a68; // frame layout constant
extern float FLOAT_80611a6c; // frame layout constant
extern float FLOAT_8061aad8; // frame layout constant
extern float FLOAT_8061aadc; // frame layout constant
extern float FLOAT_8061aae0; // frame layout constant
extern double DOUBLE_8061ab00;// int->double conversion bias

// named cross-module helpers (call-site-accurate types)
extern void leaf_31e8(unsigned dst, unsigned src, int len);
extern void leaf_92c0(int enable);
extern int  memset_wrapper(int dst, int value, int len);
extern int  get_string_by_id(int id);
extern int  card_mount_poll(void);
extern int  memcard_check_slots(int slotOut, int nameOut, int* errorCount);
extern void card_build_slot_labels(unsigned long long a1, unsigned long long a2,
                                   unsigned long long a3, unsigned long long a4,
                                   unsigned long long a5, unsigned long long a6,
                                   unsigned long long a7, unsigned long long a8);
extern int  card_format_sync(int index, int* outState);
extern int  card_table_first_free_index(void);
extern int  card_write_record(void);
extern int  arcade_edit_load_slot_record(int index);
extern int* challenge_record_find_section(int* record, int tag);
extern void card_show_status_dialog(unsigned long long r3, unsigned long long r4,
                                    unsigned long long r5, unsigned long long r6,
                                    unsigned long long r7, unsigned long long r8,
                                    unsigned long long r9, unsigned long long r10, unsigned a9,
                                    int code, unsigned a11, unsigned a12, unsigned a13,
                                    unsigned a14, unsigned a15, unsigned a16);
extern void arcade_edit_card_exit(void);
extern void card_slot_select_input(int screen);
extern void arcsel_position_frame(void);
extern void aveditor_view_from_camera(void);
extern void aveditor_fit_view_to_bounds(void);
extern int  aveditor_deserialize_map(unsigned long long f1, unsigned long long r4,
                                     unsigned long long r5, unsigned long long r6,
                                     unsigned long long r7, unsigned long long r8,
                                     unsigned long long r9, unsigned long long r10);
extern void notif_format_event_message(unsigned long long r3, unsigned long long r4,
                                       unsigned long long r5, unsigned long long r6,
                                       unsigned long long r7, unsigned long long r8,
                                       unsigned long long r9, unsigned long long r10);
extern void notif_show_timed_message(void);
extern void notif_show_yes_no_dialog(void);
extern void notif_show_yes_no_wrapper(void);
extern void notif_show_back_dialog(void);
extern int  notif_get_player_data(void);
extern int  notif_get_name_by_index(int index);
extern int  notif_get_name_by_index_safe(int index);
// returns the created popup element handle (Ghidra typed the shared decl void; the return is used)
extern int  keyboard_open_popup_text(int screen, void* buffer, int maxLen, int a4, int a5,
                                     void* confirmCb, void* cancelCb);
extern void keyboard_buffer_pool_init(void);
extern int  FUN_8038b378(unsigned* recA, unsigned* recB);   // record-name equality compare (0 == equal)

// hud element / screen helpers
extern void hud_element_init_from_layout(unsigned flags, int data, void* cancelCb, void* confirmCb);
extern void hud_set_item_padding_mode(double amount, int mode, int before, int after);
extern void hud_set_context_property(int value);
extern void hud_add_list_item(void);
extern void hud_add_padding_item(double height);
extern void hud_add_callback_item(double height);
extern unsigned long long hud_add_default_callback_item(void);
extern unsigned long long hud_element_scroll_h_overflow(double dir, double sx, double sy, int p);
extern void hud_element_apply_scroll(void);
extern void hud_element_finalize_frame(void);
extern void hud_set_focus_by_offset(int element, int offset);
extern void hud_element_apply_center_xy(int element);
extern void hud_screen_push(void);
extern void hud_screen_pop_current(void);
extern int  hud_screen_find_root(int screen);
extern int  hud_screen_all_children_closing(int screen);
extern unsigned long long hud_screen_set_handler_param_b(int screen, int handlerId, int value);
extern unsigned long long hud_item_compute_colors(unsigned a1, int item, unsigned* outFg,
                                                  unsigned* outBg);

// text render helpers
extern void text_set_font_table(int table);
extern double text_get_char_width(unsigned char* text);
extern double text_get_char_height(unsigned char* text);
extern void text_draw_char_shadowed(double x, double y, int text, unsigned fg, unsigned bg);
extern char* clock_format_timestamp_hex(unsigned long long f1, double d2, double d3, double d4,
                                        double d5, double d6, unsigned long long g7,
                                        unsigned long long g8, unsigned value);

// raw FUN_ helpers not yet named
extern void FUN_8038b510(int* dst, int* src);           // copy a slot-name string field
extern unsigned long long FUN_803484b8(int index);      // build the "loaded slot N" event id
extern unsigned long long FUN_80346df0(int arg);        // build a status/event id
extern void FUN_80387fb8(unsigned long long f1, unsigned long long r4, unsigned long long r5,
                         unsigned long long r6, unsigned long long r7, unsigned long long r8,
                         unsigned long long r9, unsigned long long r10, int buf, int str, int arg,
                         unsigned a12, unsigned a13, unsigned a14, unsigned a15,
                         unsigned a16);                  // build a formatted notification message
extern void card_name_confirm(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                              unsigned long long r6, unsigned long long r7, unsigned long long r8,
                              unsigned long long r9, unsigned long long r10);
extern void card_slot_goto_screen1(void);
extern void card_slot_action(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                             unsigned long long r6, unsigned long long r7, unsigned long long r8,
                             unsigned long long r9, unsigned long long r10, unsigned a9);

} // extern "C"

// -----------------------------------------------------------------------------

// Build the scrollable card slot-list screen. Polls the mount progress (advancing the flow timer),
// checks the card slots, then builds the list: a header item, per-slot labels (via
// card_build_slot_labels), and two name rows synced from card_format_sync/notif_get_name_by_index.
// When the mount is not ready it shows the "please wait" message (string 0x196e) instead. Finally it
// applies the scroll, finalizes the frame, sets focus and positions the arcade-select frame.
void card_slot_list_build(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                          unsigned long long r6, unsigned long long r7, unsigned long long r8,
                          unsigned long long r9, unsigned long long r10, int screen)
{
    int notReady = card_mount_poll();
    unsigned ready = countLeadingZeros((unsigned)notReady) >> 5;   // 1 when mount poll == 0
    int focusable = 0;
    int errorCount = 0;
    int slotErr = memcard_check_slots((int)(intptr_t)&DAT_80612d1c, (int)(intptr_t)&DAT_80612d18,
                                      &errorCount);
    unsigned flags = 0x2000c;
    if (DAT_80612d40 == 0 || errorCount > 0) {
        DAT_80612d1c = 1;
    }
    if (ready == 0) {
        flags = 0x2800c;
    }
    if ((DAT_80612d30 & 1) == 0) {
        flags |= 0x800;
    }

    // advance the mount-progress timer (int->double biased fraction)
    double frac = (double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_8061ab00;
    FLOAT_80612d34 = FLOAT_806108e8 * (FLOAT_8061aaec / (float)frac) + FLOAT_80612d34;

    void* confirmCb = (ready == 0) ? (void*)0 : (void*)&card_slot_select_input;
    void* cancelCb  = (ready == 0) ? (void*)0 : (void*)&arcade_edit_card_exit;
    hud_element_init_from_layout(flags, 0, cancelCb, confirmCb);

    *(int*)(screen + 0x214) = 4;
    hud_set_item_padding_mode((double)FLOAT_8061aaf4, 1, 1, 1);
    hud_set_context_property(0x20000);

    if (DAT_80612d40 == 0) {
        *(int*)(screen + 0x200) = get_string_by_id(0xe54);
    } else if (DAT_80612d40 == 1) {
        *(int*)(screen + 0x200) = get_string_by_id(0xe55);
    }

    if ((DAT_80612d30 & 1) != 0) {
        hud_add_list_item();
        hud_add_padding_item((double)FLOAT_8061aae8);
        hud_add_callback_item((double)FLOAT_8061aae8);
        hud_add_padding_item((double)FLOAT_8061aae8);
    }

    // frame Y from the layout constants
    double y = FLOAT_80611a64;
    double listY = FLOAT_80611a5c *
                   (FLOAT_8061aad8 * y +
                    (FLOAT_8061aadc * y + (FLOAT_8061aae0 * y + FLOAT_80611a6c)) - FLOAT_80611a6c) -
                   FLOAT_8061aaf8;
    hud_element_scroll_h_overflow(-listY, (double)FLOAT_8061aae4, (double)FLOAT_8061aae4, 0);

    if (ready == 0) {
        int player = notif_get_player_data();
        int str = get_string_by_id(0x196e);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, -0x7fa1e890, str, player + 0x41,
                     0, 0, 0, 0, 0);
        hud_add_list_item();
    } else if (slotErr < 1 && (DAT_80612d40 != 0 || DAT_80612d24 == 0)) {
        if (hud_screen_all_children_closing(screen) != 0 && DAT_80612d1c == 1) {
            DAT_80612d1c = 4;
        }
    } else {
        if ((DAT_80612d30 & 1) == 0) {
            int header = (DAT_80612d40 == 0) ? 0xb99 : 0xb9a;
            get_string_by_id(header);
            hud_add_list_item();
            hud_add_default_callback_item();
        }
        if (DAT_80612d40 == 0 && DAT_80612d24 != 0) {
            get_string_by_id(0xe56);
            hud_add_list_item();
            focusable = 1;
        }
        card_build_slot_labels(r3, r4, r5, r6, r7, r8, r9, r10);
        for (int i = 0; i < 2; i++) {
            int state = 0;
            card_format_sync(i, &state);
            if (state == 4) {
                notif_get_name_by_index(i);
                hud_add_list_item();
            } else {
                notif_get_name_by_index_safe(i);
                hud_add_list_item();
                focusable++;
            }
        }
    }

    hud_element_apply_scroll();
    hud_element_finalize_frame();
    if (*(int*)(screen + 0x194) == 0 && (DAT_80612d30 & 1) != 0 && focusable != 0) {
        hud_set_focus_by_offset(screen, 0);
    }
    arcsel_position_frame();
}

// Go to card screen 1 and clear the slot-valid flag.
void card_slot_goto_screen1(void)
{
    DAT_80612d4c = 1;
    DAT_80612d54 = 0;
    hud_screen_push();
}

// Name-entry confirm: if the entered name is empty just return to screen 1; otherwise look for an
// existing record with the same name (show the overwrite yes/no dialog on that slot) or, if none,
// create a new record (kind 0, screen 7).
void card_name_confirm(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                       unsigned long long r6, unsigned long long r7, unsigned long long r8,
                       unsigned long long r9, unsigned long long r10)
{
    if (DAT_805e1538 == '\0') {
        card_slot_goto_screen1();
        return;
    }
    int off = 0;
    for (int i = 0; i < *(int*)(DAT_80612d58 + 8); i++) {
        if (FUN_8038b378((unsigned*)(DAT_80612d58 + off + 0x10), (unsigned*)&DAT_805e1538) == 0) {
            DAT_80612d50 = i;
            notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
            notif_show_yes_no_wrapper();
            return;
        }
        off += 0x38;
    }
    DAT_80612d44 = 0;
    DAT_80612d4c = 7;
    hud_screen_push();
}

// Name-entry cancel: pop the current (keyboard) screen.
void card_name_cancel(void)
{
    hud_screen_pop_current();
}

// Thunk to card_name_confirm (used as the keyboard confirm callback).
void card_name_confirm_thunk(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                             unsigned long long r6, unsigned long long r7, unsigned long long r8,
                             unsigned long long r9, unsigned long long r10)
{
    card_name_confirm(r3, r4, r5, r6, r7, r8, r9, r10);
}

// Open the slot-rename keyboard popup bound to the scratch name buffer (max 0x13 chars). Toggles the
// element's +0x224 flag bit 1 and centres the frame.
void card_rename_popup_open(int screen)
{
    hud_element_init_from_layout(0, 0, 0, 0);
    int elem = keyboard_open_popup_text(screen, &DAT_805e1538, 0x13, 0, 0,
                                        (void*)&card_name_confirm_thunk, (void*)&card_name_cancel);
    *(unsigned*)(elem + 0x224) ^= 2;
    hud_element_apply_center_xy(DAT_806119e0);
    hud_element_finalize_frame();
}

// Begin a new slot: fail with the back dialog when the table is full; otherwise mark the slot valid,
// seed the scratch record name (from the rename source in mode 0x65, else the default template),
// stamp the new slot index, then open the name popup (or push straight to the keyboard-pool screen
// in challenge mode).
void card_new_slot_begin(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                         unsigned long long r6, unsigned long long r7, unsigned long long r8,
                         unsigned long long r9, unsigned long long r10, int screen)
{
    if (card_table_first_free_index() == 0x82) {
        notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
        notif_show_back_dialog();
        return;
    }
    DAT_80612d54 = 1;
    if (DAT_80612614 == 0x65 && DAT_805e17d4 != '\0') {
        FUN_8038b510((int*)&DAT_805e1538, (int*)&DAT_805e17d4);
    } else {
        DAT_805e17d4 = '\0';
        FUN_8038b510((int*)&DAT_805e1538, (int*)&DAT_8061122c);
    }
    DAT_805e1568 = (short)card_table_first_free_index();
    DAT_805e156a = 0;
    if ((DAT_80612d30 & 1) == 0) {
        keyboard_open_popup_text(screen, &DAT_805e1538, 0x13, 0, 0,
                                 (void*)&card_name_confirm, (void*)&card_slot_goto_screen1);
    } else {
        keyboard_buffer_pool_init();
        hud_screen_push();
    }
}

// Per-slot timestamp render callback. Formats the slot's timestamp (record +0x3c) as hex text, sets
// the font table, and draws it shadowed at a layout-derived position. mode 0x65 uses the alternate
// list origin.
void card_slot_timestamp_draw(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                              unsigned long long r6, unsigned long long r7, unsigned long long r8,
                              unsigned long long r9, unsigned long long r10, unsigned a9,
                              int item, int layout)
{
    float origin = (DAT_80612614 == 0x65) ? FLOAT_8061ab0c : FLOAT_8061aaec;
    unsigned fg = 0, bg = 0;
    unsigned long long acc = hud_item_compute_colors(a9, item, &fg, &bg);
    leaf_92c0(1);
    int slot = *(int*)(item + 0x14);
    char* text = clock_format_timestamp_hex(acc, r4, r5, r6, r7, r8, r9, r10,
                                            *(unsigned*)(DAT_80612d58 + slot * 0x38 + 0x3c));
    int dst = slot * 0x16 + -0x7fa1e80c;
    FUN_8038b510((int*)dst, (int*)text);
    text_set_font_table(0xf);
    double w = text_get_char_width((unsigned char*)dst);
    double h = text_get_char_height((unsigned char*)dst);
    double x = -((double)origin * (double)FLOAT_80611a68 - (double)*(float*)(layout + 8)) - w;
    double yPos = ((double)(*(float*)(layout + 4) + *(float*)(layout + 0xc)) * (double)FLOAT_8061ab10)
                  - (h * (double)FLOAT_8061ab10);
    text_draw_char_shadowed((double)(float)x, (double)(float)yPos, dst, fg, bg);
    leaf_92c0(0);
}

// Action on the selected slot. In save mode (DAT_80612d40==0): write the record (advance to screen 4
// on success, else clear the buffer and show the status dialog) or, when the load flag is set, load
// and either show a timed message (challenge mode) or deserialize the map (editor mode). In load
// mode (DAT_80612d40!=0): show the confirm yes/no dialog.
void card_slot_action(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                      unsigned long long r6, unsigned long long r7, unsigned long long r8,
                      unsigned long long r9, unsigned long long r10, unsigned a9)
{
    if (DAT_80612d40 != 0) {
        notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
        notif_show_yes_no_wrapper();
        return;
    }
    if ((unsigned)DAT_80612d50 >= *(unsigned*)(DAT_80612d58 + 8)) {
        return;
    }
    int tableBase = DAT_80612d58;
    leaf_31e8(0x805e1538, DAT_80612d58 + DAT_80612d50 * 0x38 + 0x10, 0x38);
    if (DAT_80612d3c == 0) {
        int code = card_write_record();
        DAT_80612d14 = 2;
        if (code == 0 || code == 1) {
            DAT_80612d4c = 4;
            hud_screen_push();
        } else {
            memset_wrapper(DAT_80612be8, 0, 0x7ffc);
            card_show_status_dialog(r3, r4, r5, r6, r7, r8, r9, r10, a9, code, 0x7ffc, tableBase,
                                    0, 0, 0, 0);
        }
        return;
    }
    if (arcade_edit_load_slot_record(DAT_80612d50) != 0) {
        unsigned long long msg = FUN_803484b8(DAT_80612d50);
        DAT_80612d14 = 0;
        if ((DAT_80612d30 & 1) != 0) {
            notif_format_event_message(msg, r4, r5, r6, r7, r8, r9, r10);
            notif_show_timed_message();
            DAT_805d59dc = 0;
            DAT_80612d20 = 1;
            int* flags = challenge_record_find_section((int*)(intptr_t)DAT_80612be8, 0xc77668);
            DAT_805d59a0 = (unsigned)*(unsigned short*)((int)flags + 10);
            return;
        }
        if (aveditor_deserialize_map(msg, r4, r5, r6, r7, r8, r9, r10) != 0) {
            aveditor_view_from_camera();
            aveditor_fit_view_to_bounds();
            unsigned long long status = FUN_80346df0(1);
            notif_format_event_message(status, r4, r5, r6, r7, r8, r9, r10);
            notif_show_timed_message();
            DAT_805d59dc = 0;
            DAT_80612d20 = 1;
            return;
        }
    }
    notif_format_event_message(r3, r4, r5, r6, r7, r8, r9, r10);
    notif_show_back_dialog();
}

// Slot-list item selected handler. Reads the selected slot index (screen+0x188). In load mode
// (DAT_80612d40==1) it checks the active root screen's flags: bit 0x10 => act on the slot, else
// bit 0x40 => confirm dialog. In save mode with the flag set it either acts (mode 0x65) or confirms.
void card_slot_item_selected(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                             unsigned long long r6, unsigned long long r7, unsigned long long r8,
                             unsigned long long r9, unsigned long long r10, int screen)
{
    unsigned long long acc = hud_screen_set_handler_param_b(screen, 0, 0);
    DAT_80612d50 = *(unsigned*)(screen + 0x188);
    if ((unsigned)DAT_80612d50 >= *(unsigned*)(DAT_80612d58 + 8)) {
        return;
    }
    int root = hud_screen_find_root(*(int*)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if (DAT_80612d40 == 1) {
        acc = hud_screen_set_handler_param_b(screen, 2, 0);
        if ((*(unsigned*)(root + 0xc0) & 0x10) == 0) {
            if ((*(unsigned*)(root + 0xc0) & 0x40) != 0) {
                notif_format_event_message(acc, r4, r5, r6, r7, r8, r9, r10);
                notif_show_yes_no_dialog();
            }
        } else {
            card_slot_action(acc, r4, r5, r6, r7, r8, r9, r10, (unsigned)screen);
        }
    } else if (DAT_80612d40 == 0 && (*(unsigned*)(root + 0xc0) & 0x10) != 0) {
        if (DAT_80612614 == 0x65) {
            card_slot_action(acc, r4, r5, r6, r7, r8, r9, r10, (unsigned)screen);
        } else {
            notif_format_event_message(acc, r4, r5, r6, r7, r8, r9, r10);
            notif_show_yes_no_dialog();
        }
    }
}
