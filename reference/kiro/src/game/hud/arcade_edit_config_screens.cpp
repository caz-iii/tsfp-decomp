// Batch: batch_912 | Module: arcade map-editor config/options screen builders + state glue
// Address range: 0x80323ef8 - 0x80324cdc
// Date: 2026-09-12
//
// Continuation of the arcade map-editor HUD. This file holds three big options-screen builders
// (each assembles a scroll list of labeled value-selectors and toggles from string-table ids),
// the small screen-lifecycle glue that pushes/exits/refreshes those screens, a power-of-two ->
// log2 helper, and the routine that commits the edited toggle flags back into the grid record.
//
//   aveditor_get_active_context     0x80323ef8  return the active editor context pointer.
//   aveditor_build_map_config       0x80323f00  build the map settings screen (author + limits).
//   aveditor_config_push_or_exit    0x80324430  push the screen for a saved-map ctx, else dirty-exit.
//   aveditor_apply_config_substate  0x8032447c  copy the 7-pair edit block, set sub-state 0x35/0x36.
//   aveditor_set_config_toggles     0x80324570  derive two toggle flags from the editor sub-state.
//   aveditor_build_bag_config       0x803245a8  build the "bag"/spawn settings screen.
//   aveditor_config_rebuild_dirty   0x80324a3c  rebuild the screen items if it is dirty.
//   aveditor_build_mode_config      0x80324a7c  build the mode-specific settings screen.
//   aveditor_pow2_to_log2           0x80324ca0  x==(1<<n) -> n+1, non-power-of-two -> 0.
//   aveditor_commit_grid_toggles    0x80324cdc  commit edited toggle flags into the grid cell.
//
// The three builders share the same shell: FUN_803826f0 fetches the screen element, the layout is
// initialised via hud_element_init_from_layout, item padding/context flags are set, then a run of
// get_string_by_id + hud_begin_scroll_with_item / hud_event_add_pair / hud_item_set_linked_value
// pairs builds each row; finally the element is positioned and finalised. The context base lives at
// DAT_80612be4 and the bound tool-group record pointer at context+0x2f1c.
//
// Raw FUN_/DAT_/FLOAT_ symbols get correctly-typed per-file externs; named cross-module HUD helpers
// resolve through common.h's generated decls but are re-declared here with their real ABI so the
// arg/return types match the call sites exactly (the same address is used with differing arity in
// other files, so we never rely on the shared header for these raw prototypes).

#include "common.h"

extern "C" {

extern int   DAT_80612be4;    // editor context base
extern int   DAT_80612cb0;    // active editor context pointer
extern int   DAT_806119e0;    // shared position/anim descriptor arg

// -- layout / geometry float constants --
extern float FLOAT_8061a894;  // item padding value
extern float FLOAT_8061a8c0;  // common header/text display size
extern float FLOAT_8061a8d4;  // callback-item size
extern float FLOAT_8061a8dc;  // scroll overflow base offset
extern float FLOAT_8061a8e0;  // scroll overflow term 0
extern float FLOAT_8061a8e4;  // scroll overflow term 1
extern float FLOAT_8061a8e8;  // scroll overflow term 2
extern float FLOAT_8061a920;  // screen top Y
extern float FLOAT_80611a5c;  // scroll scale
extern float FLOAT_80611a64;  // scroll unit
extern float FLOAT_80611a6c;  // scroll bias

// -- raw helpers not yet individually named --
extern int   FUN_803826d8(void);   // fetch editor screen element (variant A)
extern int   FUN_803826f0(void);    // fetch editor screen element (variant B)
extern void  FUN_80382724(void);    // finalize screen (variant A)
extern void  FUN_8038273c(void);    // finalize screen (variant B)
extern int   FUN_80338498(void);    // saved-map: needs-load query
extern void  FUN_803387f0(double, unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long);   // saved-map load kick
extern int   FUN_80338528(void);    // author-string present query
extern char  FUN_80338630(int slot);   // saved-map has-preview flag
extern int   FUN_80338668(int slot);   // saved-map author/name accessor
extern int   FUN_80338bd4(void);    // resolve saved-map list slot index
extern int   FUN_80338ca0(void);    // resolve saved-map selection (-1 none)
extern void  FUN_80338d14(void);    // saved-map preview exit callback
extern int   FUN_80338e7c(void);    // default map-config record pointer

// -- named cross-module helpers (real ABI) --
extern int   get_string_by_id(int stringId);
extern void  hud_screen_push(void);
extern void  screen_exit_dirty_check(void);
extern int   hud_element_has_unsaved_changes(int element);
extern void  hud_screen_rebuild_items(void);
extern void  aveditor_refresh_all(void);
extern void  aveditor_set_active_context(int context);
extern void  aveditor_screen_exit(void);

extern void  hud_element_init_from_layout(int layoutId, int a, int b, void *closeCb);
extern void  hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void  hud_set_context_property(int prop);
extern void  hud_element_scroll_h_overflow(double amount, double a, double b, int c);
extern void  hud_begin_scroll_with_item(void);
extern void  hud_begin_scroll_region(void);
extern void  hud_item_add_header(double size, int flags);
extern void  hud_item_add_footer(double size, int flags);
extern void  hud_set_last_child_tag(int tag);
extern void  hud_event_add_pair(void);
extern void  hud_event_add_string(int stringId);
extern void  hud_item_set_linked_value(int *value);
extern void  hud_end_scroll_region(void);
extern void  hud_item_add_value_selector(int a, int *value, int b, int step, int c, int d, int e);
extern void  hud_add_list_item(void);
extern void  hud_item_add_text_display(double size, int flags);
extern void  hud_item_set_value_display(int value);
extern void  hud_add_callback_item(double size);
extern void  hud_item_add_labeled_pair(int a, int b, int labelOff, int labelOn);
extern int   hud_get_last_created_item(void);
extern void  hud_item_set_toggle(int value, int item);
extern void  hud_element_apply_scroll(void);
extern void  hud_element_set_pos_y(double y, int descriptor);
extern void  hud_element_finalize_frame(void);

} // extern "C"

// forward prototype: used by aveditor_build_bag_config before its definition below
void aveditor_set_config_toggles(int flagsRecord);

// The scroll-overflow amount is the same expression in every builder; factor it out so the three
// builders read identically to the original per-call inline computation.
static double aveditor_scroll_overflow_amount(void)
{
    return -(double)(FLOAT_80611a5c *
                     ((FLOAT_8061a8e0 * FLOAT_80611a64 +
                       FLOAT_8061a8e4 * FLOAT_80611a64 +
                       FLOAT_8061a8e8 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                     FLOAT_8061a8dc);
}

// -----------------------------------------------------------------------------

// Return the active editor context pointer (companion getter to aveditor_set_active_context).
int aveditor_get_active_context(void)
{
    return DAT_80612cb0;
}

// Build the map settings screen: author/name header, then the map limit value-selectors. For a
// saved-map context (record tag 0x43 at *(context+0x2f1c)) it binds the saved record and kicks a
// load if needed; otherwise it uses the default config record. The list rows are all string-table
// driven (ids 0xaba..0xb03) and linked to the bound record's fields.
void aveditor_build_map_config(void)
{
    int   screen = FUN_803826d8();
    int   slot   = -1;
    int  *record = *(int **)(DAT_80612be4 + 0x2f1c);
    int   authorSlot = 0;
    bool  isSaved = false;
    int  *linked;

    if ((record != (int *)0) && (*record == 0x43)) {
        isSaved = true;
    }
    if (isSaved) {
        linked = record + 6;
    } else {
        linked = (int *)FUN_80338e7c();
    }
    if ((record != (int *)0) && (*record == 0x43)) {
        aveditor_set_active_context((int)record);
    }
    if (((record != (int *)0) && (*record == 0x43)) && (FUN_80338498() == 0)) {
        FUN_803387f0((double)0, 0, 0, 0, 0, 0, 0, 0);
    }

    slot = FUN_80338ca0();
    if (slot == -1) {
        get_string_by_id(0xaba);
    } else {
        slot = FUN_80338bd4();
        if (-1 < slot) {
            authorSlot = DAT_80612be4 + slot * 0x220 + 0x41e4;
        }
        if ((authorSlot == 0) || (*(int *)(authorSlot + 0x88) < 1)) {
            get_string_by_id(0xaba);
        }
    }

    authorSlot = FUN_80338668(slot);
    if ((authorSlot == 0) || (FUN_80338528() == 0)) {
        FUN_80338528();
    }

    void *closeCb;
    if ((FUN_80338630(slot) == 0) || (*(int *)(DAT_80612be4 + slot * 0x220 + 0x4234) != 1)) {
        closeCb = (void *)aveditor_screen_exit;
    } else {
        closeCb = (void *)FUN_80338d14;
    }

    hud_element_init_from_layout(0xa400, 0, 0, closeCb);
    *(int *)(screen + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a894, 1, 1, 1);
    hud_set_context_property(0x20000);
    hud_element_scroll_h_overflow(aveditor_scroll_overflow_amount(),
                                  (double)FLOAT_8061a8c0, (double)FLOAT_8061a8c0, 0);

    // author line
    get_string_by_id(0xac3);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0x800);
    hud_set_last_child_tag(0x86);
    get_string_by_id(0xac4);
    hud_event_add_pair();
    get_string_by_id(0xac5);
    hud_event_add_pair();
    hud_item_set_linked_value(linked);
    hud_end_scroll_region();

    // second header line
    get_string_by_id(0xac0);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0x800);
    hud_set_last_child_tag(0x85);
    get_string_by_id(0xac1);
    hud_event_add_pair();
    get_string_by_id(0xac2);
    hud_event_add_pair();
    hud_item_set_linked_value(linked + 1);
    hud_end_scroll_region();

    // width selector
    (void)countLeadingZeros(linked[2]);
    get_string_by_id(0xabe);
    hud_begin_scroll_with_item();
    hud_item_add_value_selector(0, linked + 4, 0, 0x3c, 1, 0, 0x20);
    hud_set_last_child_tag(0x83);
    get_string_by_id(0xabc);
    hud_add_list_item();
    hud_set_last_child_tag(0x84);
    hud_item_add_text_display((double)FLOAT_8061a8c0, 0);
    hud_item_set_value_display((int)(linked + 2));
    hud_end_scroll_region();

    // height selector
    (void)countLeadingZeros(linked[3]);
    get_string_by_id(0xabf);
    hud_begin_scroll_with_item();
    hud_item_add_value_selector(0, linked + 5, 0, 0x3c, 1, 0, 0x20);
    hud_set_last_child_tag(0x83);
    get_string_by_id(0xabd);
    hud_add_list_item();
    hud_set_last_child_tag(0x84);
    hud_item_add_text_display((double)FLOAT_8061a8c0, 0);
    hud_item_set_value_display((int)(linked + 3));
    hud_end_scroll_region();

    // action rows
    get_string_by_id(0xabb);
    hud_begin_scroll_region();
    hud_add_list_item();
    hud_set_last_child_tag(0x80);
    hud_end_scroll_region();

    get_string_by_id(0xac6);
    hud_add_list_item();
    hud_set_last_child_tag(0x81);
    FUN_80338668(slot);
    get_string_by_id(0x9c8);
    hud_add_list_item();
    hud_set_last_child_tag(0x82);
    hud_add_callback_item((double)FLOAT_8061a8d4);
    get_string_by_id(0xb03);
    hud_add_list_item();

    hud_element_apply_scroll();
    hud_element_set_pos_y((double)(FLOAT_8061a920 - *(float *)(screen + 100)), DAT_806119e0);
    hud_element_finalize_frame();
    FUN_80382724();
}

// Config screen push glue: for a saved-map context (tag 0x43) push the HUD screen; otherwise run
// the dirty-check exit path.
void aveditor_config_push_or_exit(void)
{
    if (**(int **)(DAT_80612be4 + 0x2f1c) == 0x43) {
        hud_screen_push();
    } else {
        screen_exit_dirty_check();
    }
}

// Apply the edited config sub-state: snapshot the 7-and-a-bit int-pair edit block (context-4 ..
// into a stack scratch), set the "dirty" bit 0x8 in context+0x2c from context+0x34, advance the
// editor sub-state to 0x35/0x36 (unless already 0x37) based on context+0x30, refresh on the
// transition into 0x36, then rebuild the screen items if dirty.
void aveditor_apply_config_substate(int element)
{
    int context = *(int *)(DAT_80612be4 + 0x2f1c);
    int stateScratch[17];
    unsigned int *src = (unsigned int *)(context + -4);
    unsigned int *dst = (unsigned int *)&stateScratch[0];
    int count = 7;
    unsigned int *pSrc, *pDst;
    do {
        pDst = dst;
        pSrc = src;
        unsigned int v2 = pSrc[2];
        pDst[1] = pSrc[1];
        pDst[2] = v2;
        count = count + -1;
        src = pSrc + 2;
        dst = pDst + 2;
    } while (count != 0);
    pDst[3] = pSrc[3];

    if (*(int *)(context + 0x34) == 0) {
        *(unsigned int *)(context + 0x2c) = *(unsigned int *)(context + 0x2c) & 0xfffffff7;
    } else {
        *(unsigned int *)(context + 0x2c) = *(unsigned int *)(context + 0x2c) | 8;
    }

    if (**(int **)(DAT_80612be4 + 0x2f1c) != 0x37) {
        int newState = 0x36;
        if (*(int *)(context + 0x30) == 0) {
            newState = 0x35;
        }
        **(int **)(DAT_80612be4 + 0x2f1c) = newState;
    }

    if ((**(int **)(DAT_80612be4 + 0x2f1c) != stateScratch[0]) &&
        (**(int **)(DAT_80612be4 + 0x2f1c) == 0x36)) {
        aveditor_refresh_all();
    }

    if (hud_element_has_unsaved_changes(element) != 0) {
        hud_screen_rebuild_items();
    }
}

// Derive the two config toggle flags for the record at flagsRecord: bit 3 of +0x14 -> +0x1c, and
// (editor sub-state == 0x36) -> +0x18, then clear bit 3 of +0x14.
void aveditor_set_config_toggles(int flagsRecord)
{
    *(unsigned int *)(flagsRecord + 0x1c) = *(unsigned int *)(flagsRecord + 0x14) >> 3 & 1;
    unsigned int clz = countLeadingZeros(0x36 - **(int **)(DAT_80612be4 + 0x2f1c));
    *(unsigned int *)(flagsRecord + 0x18) = clz >> 5;
    *(unsigned int *)(flagsRecord + 0x14) = *(unsigned int *)(flagsRecord + 0x14) & 0xfffffff7;
}

// Build the "bag"/spawn settings screen: seeds the toggle flags from the sub-state, then lays out
// the spawn-set headers (string ids 0x98d..0x995 / 0x6d3..0x6d8 / weather ids 0xac7..0x170e), the
// extra footer block when not in the 0x37 sub-state, and two enable toggles.
void aveditor_build_bag_config(void)
{
    int screen  = FUN_803826f0();
    int context = *(int *)(DAT_80612be4 + 0x2f1c);
    int s0, s1;

    aveditor_set_config_toggles(context + 0x18);
    hud_element_init_from_layout(0x8400, 0, 0, (void *)screen_exit_dirty_check);
    *(int *)(screen + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a894, 1, 1, 1);
    hud_set_context_property(0x20000);
    hud_element_scroll_h_overflow(aveditor_scroll_overflow_amount(),
                                  (double)FLOAT_8061a8c0, (double)FLOAT_8061a8c0, 0);

    get_string_by_id(0xaa3);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x8a);
    get_string_by_id(0x98d);
    hud_event_add_pair();
    get_string_by_id(0x98e);
    hud_event_add_pair();
    hud_item_set_linked_value((int *)(context + 0x2c));
    hud_end_scroll_region();

    get_string_by_id(0x991);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x8b);
    get_string_by_id(0x993);
    hud_event_add_pair();
    get_string_by_id(0x6d3);
    hud_event_add_pair();
    get_string_by_id(0x6d4);
    hud_event_add_pair();
    get_string_by_id(0x6d5);
    hud_event_add_pair();
    get_string_by_id(0x6d6);
    hud_event_add_pair();
    hud_item_set_linked_value((int *)(context + 0x24));
    hud_end_scroll_region();

    get_string_by_id(0x992);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x8c);
    get_string_by_id(0x993);
    hud_event_add_pair();
    get_string_by_id(0x6d7);
    hud_event_add_pair();
    get_string_by_id(0x6d8);
    hud_event_add_pair();
    hud_item_set_linked_value((int *)(context + 0x28));
    hud_end_scroll_region();

    if (**(int **)(DAT_80612be4 + 0x2f1c) != 0x37) {
        get_string_by_id(0xab3);
        hud_begin_scroll_with_item();
        hud_item_add_header((double)FLOAT_8061a8c0, 0);
        hud_set_last_child_tag(0x8d);
        get_string_by_id(0xac7);
        hud_event_add_pair();
        get_string_by_id(0xac8);
        hud_event_add_pair();
        get_string_by_id(0xac9);
        hud_event_add_pair();
        get_string_by_id(0xaca);
        hud_event_add_pair();
        get_string_by_id(0x1701);
        hud_event_add_pair();
        get_string_by_id(0x1709);
        hud_event_add_pair();
        get_string_by_id(0x170e);
        hud_event_add_pair();
        get_string_by_id(0x170d);
        hud_event_add_pair();
        hud_item_set_linked_value((int *)(context + 0x20));
        hud_end_scroll_region();

        get_string_by_id(0xb28);
        hud_begin_scroll_with_item();
        hud_item_add_footer((double)FLOAT_8061a8c0, 0);
        s0 = get_string_by_id(0x994);
        hud_event_add_string(s0);
        hud_set_last_child_tag(0x8e);
        s0 = get_string_by_id(0x995);
        hud_event_add_string(s0);
        hud_set_last_child_tag(0x8f);
        hud_item_set_value_display(context + 0x30);
        hud_end_scroll_region();
    }

    get_string_by_id(0x98f);
    hud_add_list_item();
    hud_item_add_text_display((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x90);
    hud_item_set_value_display(context + 0x34);

    get_string_by_id(0xaaf);
    hud_begin_scroll_with_item();
    s0 = get_string_by_id(0x96e);
    s1 = get_string_by_id(0x96d);
    hud_item_add_labeled_pair(0, 0, s1, s0);
    hud_set_last_child_tag(0x91);
    s0 = hud_get_last_created_item();
    hud_item_set_toggle(context + 0x18, s0);
    hud_end_scroll_region();

    get_string_by_id(0xab0);
    hud_begin_scroll_with_item();
    s0 = get_string_by_id(0xab2);
    s1 = get_string_by_id(0xab1);
    hud_item_add_labeled_pair(0, 0, s1, s0);
    hud_set_last_child_tag(0x92);
    s0 = hud_get_last_created_item();
    hud_item_set_toggle(context + 0x1c, s0);
    hud_end_scroll_region();

    hud_add_callback_item((double)FLOAT_8061a8d4);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_apply_scroll();
    hud_element_set_pos_y((double)(FLOAT_8061a920 - *(float *)(screen + 100)), DAT_806119e0);
    hud_element_finalize_frame();
    FUN_8038273c();
}

// Rebuild the config screen items if the element is dirty.
void aveditor_config_rebuild_dirty(int element)
{
    if (hud_element_has_unsaved_changes(element) != 0) {
        hud_screen_rebuild_items();
    }
}

// Build the mode-specific settings screen: one enable toggle, then a mode header whose value list
// is chosen by the editor sub-state (0x32/0x33/0x34 select different string-id runs), then the
// standard callback/exit row.
void aveditor_build_mode_config(void)
{
    int screen  = FUN_803826f0();
    int context = *(int *)(DAT_80612be4 + 0x2f1c);
    int s0, s1;

    hud_element_init_from_layout(0x8400, 0, 0, (void *)screen_exit_dirty_check);
    *(int *)(screen + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a894, 1, 1, 1);
    hud_set_context_property(0x20000);

    get_string_by_id(0xab0);
    hud_begin_scroll_with_item();
    s0 = get_string_by_id(0xab2);
    s1 = get_string_by_id(0xab1);
    hud_item_add_labeled_pair(0, 0, s1, s0);
    hud_set_last_child_tag(0x93);
    s0 = hud_get_last_created_item();
    hud_item_set_toggle(context + 0x18, s0);
    hud_end_scroll_region();

    get_string_by_id(0xab3);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x94);
    int state = **(int **)(DAT_80612be4 + 0x2f1c);
    if (state == 0x32) {
        get_string_by_id(0xac7);
        hud_event_add_pair();
        get_string_by_id(0xac8);
        hud_event_add_pair();
        get_string_by_id(0xac9);
        hud_event_add_pair();
    } else if (state == 0x33) {
        get_string_by_id(0xaca);
        hud_event_add_pair();
        get_string_by_id(0x1701);
        hud_event_add_pair();
        get_string_by_id(0xacb);
        hud_event_add_pair();
        get_string_by_id(0x1703);
        hud_event_add_pair();
        get_string_by_id(0x1709);
        hud_event_add_pair();
    } else if (state == 0x34) {
        get_string_by_id(0xace);
        hud_event_add_pair();
    }
    hud_item_set_linked_value((int *)(context + 0x1c));
    hud_end_scroll_region();

    hud_add_callback_item((double)FLOAT_8061a8d4);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_set_pos_y((double)(FLOAT_8061a920 - *(float *)(screen + 100)), DAT_806119e0);
    FUN_8038273c();
}

// Map an exact power-of-two to its (log2 + 1); returns 0 when the input is 0 or not a power of two.
int aveditor_pow2_to_log2(int x)
{
    int n = 0;
    if (x == 0) {
        return 0;
    }
    while (true) {
        if (x < (1 << n)) {
            return 0;
        }
        if (x == (1 << n)) {
            break;
        }
        n = n + 1;
    }
    return n + 1;
}

// Commit the edited toggle flags back into the current grid cell record (context+0x2f78 selects the
// 0xc-byte cell at +0x3070). Only writes when the element is dirty. The bit at cell+8 is the signed
// sign-bit of the level index (context+0x2f54); cell+9 ORs the three normalized enable flags.
void aveditor_commit_grid_toggles(int element)
{
    int base = DAT_80612be4;
    int cell = DAT_80612be4 + *(int *)(DAT_80612be4 + 0x2f78) * 0xc + 0x3070;

    if (hud_element_has_unsaved_changes(element) != 0) {
        int level = *(int *)(base + 0x2f54);
        *(unsigned char *)(cell + 8) =
            (unsigned char)(1 << (level + -1)) &
            (unsigned char)((char)((unsigned char)((unsigned int)-level >> 0x18) &
                                   ~(unsigned char)((unsigned int)level >> 0x18)) >> 7);
        *(unsigned int *)(base + 0x2f58) = -(unsigned int)(*(int *)(base + 0x2f58) != 0) & 8;
        *(unsigned int *)(base + 0x2f5c) = -(unsigned int)(*(int *)(base + 0x2f5c) != 0) & 4;
        *(unsigned char *)(cell + 9) =
            (unsigned char)*(unsigned int *)(base + 0x2f5c) |
            (unsigned char)*(unsigned int *)(base + 0x2f50) |
            (unsigned char)*(unsigned int *)(base + 0x2f58);
    }
}
