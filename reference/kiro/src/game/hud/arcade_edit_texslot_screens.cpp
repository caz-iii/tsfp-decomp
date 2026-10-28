// Batch: batch_923 | Module: arcade map-editor texset/weapon-set edit screen builders + handlers
// Address range: 0x803297d8 - 0x8032a5ec
// Date: 2026-09-12
//
// The screen builders and lifecycle handlers for the arcade map-editor texset/weapon-set editor:
// the delete B-button handler, the full slot list screen, the weapon-set config/palette editor
// screen and its two exit-mode wrappers, the texset info screen, plus the editor-exit/rebuild/closed
// handlers and a small slot-type predicate. Builds on batch_920-922 (slot record layout at ctx
// DAT_80612be4 +0x288 stride 0xe4, count +0x280; the slot ops / action-list builder).
//
//   aveditor_texslot_delete_handler   0x803297d8  B-button: register the delete handler; confirm+delete.
//   aveditor_build_texslot_list_screen 0x803298c8 build the full slot list screen (rows + sub-lists).
//   aveditor_wset_editor_exit         0x80329dc4  exit: normalize count, dirty-sound, clear scratch, pop.
//   aveditor_wset_editor_rebuild      0x80329e8c  rebuild the screen items.
//   aveditor_wset_editor_closed       0x80329eac  on all children closing, run the editor cleanup.
//   aveditor_build_wset_config_screen 0x80329ed8  build the weapon-set config/palette editor screen.
//   aveditor_build_wset_config_edit   0x8032a460  wrapper: config screen, editor-exit cb, flag 4.
//   aveditor_build_wset_config_view   0x8032a48c  wrapper: config screen, dirty-check exit, flag 5.
//   aveditor_build_texset_info_screen 0x8032a4b8  build the texset info / value-display screen.
//   aveditor_slot_is_texset           0x8032a5ec  1 if the slot type (+0x34) is 5 (texset).
//
// Editor context (DAT_80612be4): +0x238 status flags (bit 0x20 = sound latch, bit 0x40 = scroll-to
// request), +0x280 slot count, +0x288 slot array, +0x2f20 packed sub-selection. Slot: +0x34 type,
// +0x58 child count, +0x28/0x2c/0x30/0x34/0x38/0x3c/0x48/0x4c linked config values.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ get correctly-typed per-file externs; named helpers keep their real
// ABI. Paired-single FP paths reproduce the CONCAT44 u32->double (0x43300000 magic) idiom verbatim.
// countLeadingZeros / CONCAT44 come from common.h.

#include "common.h"

extern "C" {

extern int DAT_80612be4;   // editor context base
extern int DAT_806111fc;   // slot index for the delete-confirm handler
extern int DAT_806119e0;   // layout Y-position anchor
extern int *DAT_80610a20;  // sprite/weapon-def table base

// layout float / double constants
extern float FLOAT_8061a9a4, FLOAT_8061a9ac, FLOAT_8061a9b0, FLOAT_8061a9c8, FLOAT_8061a9cc;
extern float FLOAT_8061a9d0, FLOAT_8061a9d8, FLOAT_8061a9dc, FLOAT_8061a9e0, FLOAT_8061a9e4;
extern float FLOAT_8061a9ec, FLOAT_8061a9f0, FLOAT_8061aa04, FLOAT_8061aa08;
extern float FLOAT_80611a5c, FLOAT_80611a64, FLOAT_80611a6c;
extern double DOUBLE_8061a9b8, DOUBLE_8061a9f8;

// cross-module / raw helpers
extern void screen_exit_clear_mode(void);
extern void hud_scroll_to_normalized_pos(double pos, int el);
extern void hud_screen_remove_handler(int el, int slot);
extern int  get_string_by_id(int id);
extern void hud_screen_register_handler(int el, int slot, int str, int a, int b);
extern unsigned long long FUN_8032c348(void);           // delete-needs-confirm check (hi=nonzero => confirm)
extern void aveditor_delete_texslot_confirmed(unsigned long long, unsigned long long,
                                              unsigned long long, unsigned long long,
                                              unsigned long long, unsigned long long,
                                              unsigned long long, unsigned long long,
                                              int item, int a, int b, int c, int d,
                                              int e, int f, int g);   // batch_922
extern void hud_create_yes_no_dialog(int el, int str, int cb, int arg);
extern int  FUN_803826cc(void);   // get current element
extern int  FUN_803826dc(void);   // get current element (spill pair variant)
extern int  FUN_803826f0(void);   // get current element (spill pair variant)
extern void hud_element_init_from_layout(int flags, int a, int b, int cb);
extern void hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void hud_set_context_property(int prop);
extern void hud_element_scroll_h_overflow(double y, double a, double b, int c);
extern void hud_add_list_item(void);
extern void hud_set_last_child_tag(int tag);
extern void hud_add_callback_item(double h);
extern double hud_get_content_height(void);
extern unsigned int FUN_8032a600(int slot);   // slot has a live sub-list?
extern void aveditor_build_texslot_actions(void);   // batch_922
extern int  sprite_get_texture_id(int idx);
extern unsigned short cursor_get_width(int id);
extern unsigned short cursor_get_height(int id);
extern void hud_begin_sub_list(int a, int b, int c);
extern void hud_add_item_internal(double w, double h);
extern void hud_end_sub_list(void);
extern void hud_element_apply_scroll(void);
extern void hud_element_set_pos_y(double y, int anchor);
extern void hud_element_finalize_frame(void);
extern void FUN_80382718(void);   // restore fpr spill
extern void FUN_80382728(void);   // restore fpr spill
extern void FUN_8038273c(void);   // restore fpr spill
extern unsigned char aveditor_get_wset_flag(void);   // batch_919
extern int  hud_element_has_unsaved_changes(int el);
extern bool aveditor_scratch_differs(int record);     // batch_920
extern void FUN_80348238(int screenIndex);            // build/register screen
extern void aveditor_clear_scratch_and_flag(void);    // batch_920
extern void hud_screen_pop_current(void);
extern void hud_screen_rebuild_items(void);
extern int  hud_screen_all_children_closing(int el);
extern void FUN_8033f224(void);   // editor teardown / commit
extern int  aveditor_get_weapon_set(void);
extern int *menu_slot_option_table_resolve(int set);
extern void hud_begin_scroll_with_item(void);
extern void hud_item_add_slider(double w, double h, int a, int rec, int idx, int rec2, int flags);
extern void hud_end_scroll_region(void);
extern void hud_item_add_header(double w, int a);
extern void hud_event_add_pair(void);
extern void aveditor_get_palette_entry_label(int index);
extern void hud_item_set_linked_value(int *p);
extern void hud_add_padding_item(double h);
extern void hud_item_add_text_display(double w, int a);
extern void hud_item_set_value_display(int p);
extern void hud_item_add_value_selector(int a, int rec, int lo, int hi, int step, int b, int flags);
extern void screen_exit_dirty_check(int el);
extern void hud_item_add_footer(double w, int flags);
extern void hud_event_add_string(int str);

// forward declarations for intra-file callees
void aveditor_wset_editor_exit(int el);
unsigned int aveditor_slot_is_texset(int slot);

// ---- 0x803297d8 : delete B-button handler -------------------------------------------------------
void aveditor_texslot_delete_handler(unsigned long long param_1, unsigned long long param_2,
                                     unsigned long long param_3, unsigned long long param_4,
                                     unsigned long long param_5, unsigned long long param_6,
                                     unsigned long long param_7, unsigned long long param_8,
                                     int el)
{
    (void)param_1;
    int in_r8 = 0, in_r9 = 0, in_r10 = 0;
    if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x40) != 0) {
        hud_scroll_to_normalized_pos((double)FLOAT_8061a9b0, el);
        *(unsigned int *)(DAT_80612be4 + 0x238) &= 0xffffffbf;
    }
    hud_screen_remove_handler(el, 3);
    if (*(int *)(el + 0x188) >= 0 && *(int *)(el + 0x188) < *(int *)(DAT_80612be4 + 0x280)) {
        if (*(int *)(el + 0x18c) == 0) {
            int str = get_string_by_id(0xa44);
            hud_screen_register_handler(el, 3, str, 0, 0);
            if ((*(unsigned int *)(el + 0xc0) & 0x800000) != 0) {
                DAT_806111fc = *(int *)(el + 0x188);
                unsigned long long chk = FUN_8032c348();
                if ((int)((unsigned long long)chk >> 0x20) == 0) {
                    aveditor_delete_texslot_confirmed((unsigned long long)0, param_2, param_3, param_4,
                                                      param_5, param_6, param_7, param_8, el,
                                                      (int)chk, str, 0, 0, in_r8, in_r9, in_r10);
                } else {
                    int msg = get_string_by_id(0xb30);
                    hud_create_yes_no_dialog(el, msg, (int)0x80329760, 0);
                }
            }
        }
    }
}

// ---- 0x803298c8 : build the full texset/weapon-set slot list screen -----------------------------
void aveditor_build_texslot_list_screen(void)
{
    int el = FUN_803826cc();
    hud_element_init_from_layout(0x8400, 0, 0, (int)&screen_exit_clear_mode);
    *(int *)(el + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a9d0, 0, 1, 1);
    hud_set_context_property(0x28000);
    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
            ((FLOAT_8061a9dc * FLOAT_80611a64 + FLOAT_8061a9e0 * FLOAT_80611a64 +
              FLOAT_8061a9e4 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) - FLOAT_8061a9d8),
        (double)FLOAT_8061a9d0, (double)FLOAT_8061a9d0, 0);
    get_string_by_id(0xaeb);
    hud_add_list_item();
    hud_set_last_child_tag(0xb0);
    hud_add_callback_item((double)FLOAT_8061a9ac);

    double pad = (double)FLOAT_8061a9c8;
    int off = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0x280); ++i) {
        double rowH = (double)(float)(hud_get_content_height() - pad);
        int slot = DAT_80612be4 + off + 0x288;
        if (*(int *)(slot + 0x58) == 0 || FUN_8032a600(slot) == 0) {
            aveditor_build_texslot_actions();
        } else {
            int tex = sprite_get_texture_id(0x28c);
            unsigned short w = cursor_get_width(0x31);
            double cw = (double)((float)((double)CONCAT44(0x43300000, (unsigned int)(w ^ 0x80000000))
                        - DOUBLE_8061a9b8) * FLOAT_8061a9ec);
            unsigned short h = cursor_get_height(0x31);
            double chh = (double)((float)((double)CONCAT44(0x43300000, (unsigned int)(h ^ 0x80000000))
                        - DOUBLE_8061a9b8) * FLOAT_8061a9ec);
            double iconW = (double)((float)((double)CONCAT44(0x43300000,
                        (unsigned int)*(unsigned short *)(*DAT_80610a20 + tex * 0x58 + 0x4e))
                        - DOUBLE_8061a9f8) * FLOAT_8061a9f0);
            h = cursor_get_height(0x31);
            double iconH = (double)((float)((double)CONCAT44(0x43300000, (unsigned int)(h ^ 0x80000000))
                        - DOUBLE_8061a9b8) * FLOAT_8061a9ec);
            unsigned int notTex = countLeadingZeros(aveditor_slot_is_texset(slot)) >> 5;
            if (notTex == 0) {
                aveditor_build_texslot_actions();
            } else {
                hud_begin_sub_list(0, 0, 2);
                hud_add_item_internal(rowH, (double)FLOAT_8061a9cc);
                hud_set_last_child_tag(0xb1);
                hud_add_item_internal((double)FLOAT_8061a9d0, (double)FLOAT_8061a9cc);
                hud_set_last_child_tag(0xb3);
                hud_end_sub_list();
            }
            hud_begin_sub_list(0, 0, 0x10);
            if (*(int *)(slot + 0x58) < 8) {
                hud_add_item_internal(iconW, iconH);
                hud_set_last_child_tag(notTex != 0 ? 0xb5 : 0xae);
                for (int j = 0; j < *(int *)(slot + 0x58); ++j) {
                    hud_add_item_internal(cw, chh);
                    hud_set_last_child_tag(notTex != 0 ? 0xb4 : 0xad);
                    hud_add_item_internal(iconW, iconH);
                    hud_set_last_child_tag(notTex != 0 ? 0xb5 : 0xae);
                }
            } else {
                for (int j = 0; j < *(int *)(slot + 0x58); ++j) {
                    hud_add_item_internal(iconW, iconH);
                    hud_add_item_internal(cw, chh);
                    hud_set_last_child_tag(notTex != 0 ? 0xb4 : 0xae);
                }
            }
            hud_end_sub_list();
        }
        off += 0xe4;
    }
    hud_element_apply_scroll();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
            ((FLOAT_8061a9dc * FLOAT_80611a64 + FLOAT_8061a9e0 * FLOAT_80611a64 +
              FLOAT_8061a9e4 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    hud_element_finalize_frame();
    FUN_80382718();
}

// ---- 0x80329dc4 : exit the weapon-set editor ----------------------------------------------------
void aveditor_wset_editor_exit(int el)
{
    unsigned char flag = aveditor_get_wset_flag();
    int slot = DAT_80612be4 + (unsigned int)flag * 0xe4 + 0x288;
    if (*(int *)(slot + 0x58) > 1 && *(int *)(slot + 0x34) != 1 && *(int *)(slot + 0x34) != 3) {
        *(int *)(slot + 0x58) = 1;
    }
    bool play = false;
    if (hud_element_has_unsaved_changes(el) == 0) {
        if (aveditor_scratch_differs(slot)) {
            play = true;
        }
    } else {
        play = true;
    }
    if (play && (*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(0x13);
        *(unsigned int *)(DAT_80612be4 + 0x238) |= 0x20;
    }
    aveditor_clear_scratch_and_flag();
    hud_screen_pop_current();
}

// ---- 0x80329e8c : rebuild the editor screen items -----------------------------------------------
void aveditor_wset_editor_rebuild(void)
{
    hud_screen_rebuild_items();
}

// ---- 0x80329eac : when all children are closing, run the editor teardown ------------------------
void aveditor_wset_editor_closed(int el)
{
    if (hud_screen_all_children_closing(el) != 0) {
        FUN_8033f224();
    }
}

// ---- 0x80329ed8 : build the weapon-set config / palette editor screen ---------------------------
void aveditor_build_wset_config_screen(int exitCb, int param_2, unsigned int rebuildFlags)
{
    (void)param_2;
    int el = FUN_803826dc();
    unsigned char flag = aveditor_get_wset_flag();
    int slot = DAT_80612be4 + (unsigned int)flag * 0xe4 + 0x288;
    int *opts = menu_slot_option_table_resolve(aveditor_get_weapon_set());

    hud_element_init_from_layout(0x8400, 0, 0, exitCb);
    hud_set_item_padding_mode((double)FLOAT_8061a9a4, 1, 1, 1);
    hud_set_context_property(0x20000);
    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
            ((FLOAT_8061a9dc * FLOAT_80611a64 + FLOAT_8061a9e0 * FLOAT_80611a64 +
              FLOAT_8061a9e4 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) - FLOAT_8061a9d8),
        (double)FLOAT_8061a9d0, (double)FLOAT_8061a9d0, 0);

    // brightness slider
    get_string_by_id(0xd19);
    hud_begin_scroll_with_item();
    hud_item_add_slider((double)FLOAT_8061a9d0, (double)FLOAT_8061aa04, 0, slot, 0x1f, slot + 0x20, 0x2000);
    hud_set_last_child_tag(0xb7);
    hud_end_scroll_region();

    // palette selector (10 entries)
    get_string_by_id(0xd1a);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a9d0, 0);
    hud_set_last_child_tag(0xb8);
    {
        int idx = 0, o = 0;
        do {
            if (*(short *)((int)opts + o + 4) == -1) {
                get_string_by_id(0xd21);
                hud_event_add_pair();
            } else {
                aveditor_get_palette_entry_label(idx + 0x38);
                hud_event_add_pair();
            }
            ++idx; o += 8;
        } while (idx < 10);
    }
    hud_item_set_linked_value((int *)(slot + 0x28));
    hud_end_scroll_region();

    if (*(int *)(slot + 0x34) != 5) {
        // secondary palette (6)
        get_string_by_id(0xd1b);
        hud_begin_scroll_with_item();
        hud_item_add_header((double)FLOAT_8061a9d0, 0);
        hud_set_last_child_tag(0xb9);
        get_string_by_id(0x17bb);
        hud_event_add_pair();
        for (int idx = 0; idx < 6; ++idx) {
            aveditor_get_palette_entry_label(idx + 0x11);
            hud_event_add_pair();
        }
        hud_item_set_linked_value((int *)(slot + 0x2c));
        hud_add_padding_item((double)FLOAT_8061a9b0);
        get_string_by_id(0x1724);
        hud_add_list_item();
        hud_item_add_text_display((double)FLOAT_8061a9d0, 0);
        hud_set_last_child_tag(0xba);
        hud_item_set_value_display(slot + 0x30);
        hud_end_scroll_region();

        // tertiary palette (4)
        get_string_by_id(0xd16);
        hud_begin_scroll_with_item();
        hud_set_last_child_tag(0xbb);
        hud_item_add_header((double)FLOAT_8061a9d0, 0);
        get_string_by_id(0xbc4);
        hud_event_add_pair();
        for (int idx = 0; idx < 4; ++idx) {
            aveditor_get_palette_entry_label(idx + 0x1d);
            hud_event_add_pair();
        }
        hud_item_set_linked_value((int *)(slot + 0x48));
        hud_end_scroll_region();

        // type selector (6)
        get_string_by_id(0xd1c);
        hud_begin_scroll_with_item();
        hud_set_last_child_tag(0xbc);
        hud_item_add_header((double)FLOAT_8061a9d0, 0);
        for (int idx = 0; idx < 6; ++idx) {
            switch (idx) {
                case 0: get_string_by_id(0xbaf); hud_event_add_pair(); break;
                case 1: get_string_by_id(0xbb0); hud_event_add_pair(); break;
                case 2: get_string_by_id(0xba5); hud_event_add_pair(); break;
                case 3: get_string_by_id(0xba7); hud_event_add_pair(); break;
                case 4: get_string_by_id(0xba6); hud_event_add_pair(); break;
                default: break;
            }
        }
        hud_item_set_linked_value((int *)(slot + 0x34));
        hud_end_scroll_region();
    }

    // team selector (5)
    get_string_by_id(0xbb2);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a9d0, 0);
    hud_set_last_child_tag(0xbd);
    get_string_by_id(0xbb3); hud_event_add_pair();
    get_string_by_id(0xbb4); hud_event_add_pair();
    get_string_by_id(0xbb5); hud_event_add_pair();
    get_string_by_id(0xbb6); hud_event_add_pair();
    get_string_by_id(2999);  hud_event_add_pair();
    hud_item_set_linked_value((int *)(slot + 0x4c));
    hud_end_scroll_region();

    if (*(int *)(slot + 0x34) != 5) {
        get_string_by_id(0x1722);
        hud_begin_scroll_with_item();
        hud_item_add_value_selector(0, slot + 0x38, 1, 999, 1, 0, 0);
        hud_set_last_child_tag(0xbf);
        hud_end_scroll_region();
        get_string_by_id(0x1723);
        hud_begin_scroll_with_item();
        hud_item_add_value_selector(0, slot + 0x3c, 0, 600, 1, 0, 0x20);
        hud_set_last_child_tag(0xc0);
        hud_end_scroll_region();
    }
    hud_add_callback_item((double)FLOAT_8061a9ac);
    get_string_by_id(0xd18);
    hud_add_list_item();
    hud_element_apply_scroll();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
            ((FLOAT_8061a9dc * FLOAT_80611a64 + FLOAT_8061a9e0 * FLOAT_80611a64 +
              FLOAT_8061a9e4 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    hud_element_finalize_frame();
    *(unsigned int *)(el + 0x214) |= rebuildFlags;
    FUN_80382728();
}

// ---- 0x8032a460 : build the config screen with the editor-exit callback (flag 4) ----------------
void aveditor_build_wset_config_edit(int param_1)
{
    aveditor_build_wset_config_screen(param_1, (int)&aveditor_wset_editor_exit, 4);
}

// ---- 0x8032a48c : build the config screen with the standard dirty-check exit (flag 5) -----------
void aveditor_build_wset_config_view(int param_1)
{
    aveditor_build_wset_config_screen(param_1, (int)&screen_exit_dirty_check, 5);
}

// ---- 0x8032a4b8 : build the texset info / value-display screen -----------------------------------
void aveditor_build_texset_info_screen(void)
{
    int el = FUN_803826f0();
    unsigned int sub = *(unsigned int *)(DAT_80612be4 + 0x2f20);
    hud_element_init_from_layout(0x8400, 0, 0, (int)&screen_exit_dirty_check);
    *(int *)(el + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a9a4, 1, 1, 1);
    hud_set_context_property(0x20000);
    get_string_by_id(0xbc0);
    hud_begin_scroll_with_item();
    hud_item_add_footer((double)FLOAT_8061a9d0, 0x4000);
    hud_event_add_string(get_string_by_id(0xbc2));
    hud_set_last_child_tag(0xc1);
    hud_event_add_string(get_string_by_id(0xbc3));
    hud_set_last_child_tag(0xc2);
    hud_item_set_value_display(
        DAT_80612be4 + ((int)sub >> 0x10) * 0xe4 + (sub & 0xffff) * 0x10 + 0x2f8);
    hud_end_scroll_region();
    hud_add_callback_item((double)FLOAT_8061a9ac);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_set_pos_y((double)(FLOAT_8061aa08 - *(float *)(el + 100)), DAT_806119e0);
    FUN_8038273c();
}

// ---- 0x8032a5ec : 1 if the slot type (+0x34) is 5 (texset) --------------------------------------
unsigned int aveditor_slot_is_texset(int slot)
{
    return countLeadingZeros((unsigned int)(5 - *(int *)(slot + 0x34))) >> 5;
}

} // extern "C"
