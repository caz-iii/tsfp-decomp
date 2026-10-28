// arcade_edit_entryscreen.cpp
// Arcade map-editor entry / options screen: the screen shown when entering the
// editor. Handles the enter/back callbacks, the fade-in accumulator, the
// screen-transition dispatcher (memory-card check -> enter editor -> open the
// weapon-set select popup), the arcade-match launch, plus the editor options
// list builder and its screen init/render/close callbacks.
//
// The active editor screen context is DAT_80612be4; DAT_80612be8 points at the
// active level command-stream (tag 0xc77667 means "already in a saved map"),
// DAT_80612c90 is the "transition pending" latch and FLOAT_80612c94 is the
// fade-in alpha accumulator.
//
// Address: 0x803208e4..0x80320f98 | Batch: batch_904 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern int  DAT_80612be4;      // editor screen context base
extern int *DAT_80612be8;      // active level command-stream base (first word = tag)
extern int  DAT_80612bf4;      // parent screen handle
extern int  DAT_806119e0;      // center-xy layout anchor
extern int  DAT_806119ec;      // active screen table index
extern int  DAT_8051a820;      // screen descriptor table base
extern int  DAT_80612c90;      // transition-pending latch
extern int  DAT_805d59dc;      // arcade-launch scratch flag
extern int  DAT_80480350;      // memcard-check callback target A
extern int  DAT_80480360;      // back-navigation screen descriptor
extern int  DAT_80480370;      // memcard-check callback target B

extern float FLOAT_80612c94;   // fade-in alpha accumulator
extern float FLOAT_8061a850;   // fade rate numerator
extern float FLOAT_806108e8;   // fade rate scale
extern float FLOAT_8061a860;   // fade-in complete threshold
extern float FLOAT_8061a834;   // fade reset value
extern float FLOAT_8061a870;   // list item padding / default scalar
extern float FLOAT_8061a874;   // list scroll amount
extern float FLOAT_8061a878;   // ticker layout size
extern float FLOAT_8061a880;   // options screen y-offset
extern float FLOAT_8061a884;   // logo x scale
extern float FLOAT_8061a888;   // logo y scale
extern int   DAT_806109c0;     // frame-count raw (int->double convert source)
extern double DOUBLE_8061a868; // frame-count bias

// ---- helpers defined elsewhere (raw, per-file externs) ----
extern int  hud_element_is_closing(int element);
extern void hud_screen_push(void);
extern void hud_screen_pop_current(void);
extern int  hud_screen_find_root(int descriptor);
extern void hud_screen_navigate_to(int from, void *screenDesc, int arg);
extern void memcard_check_enter_active(int parent, void *cbA, void *cbB);
extern void memset_wrapper(int dst, int value, int len);
extern int  get_string_by_id(int stringId);

extern void aveditor_enter_screen(void);
extern void aveditor_ensure_models_loaded(unsigned int ctx, unsigned long long a2,
                                          unsigned long long a3, unsigned long long a4,
                                          unsigned long long a5, unsigned long long a6,
                                          unsigned long long a7, unsigned long long a8);
extern void aveditor_open_weaponset_select(void);

// arcade match launch (0x80031a40) - variadic register/stack passthrough
extern void match_start_cutscene_transition(unsigned long long a1, unsigned long long a2,
                                            unsigned long long a3, unsigned long long a4,
                                            unsigned long long a5, unsigned long long a6,
                                            unsigned long long a7, unsigned long long a8,
                                            unsigned int scratch, unsigned int z1,
                                            unsigned int z2, unsigned int r6,
                                            unsigned int r7, unsigned int r8,
                                            unsigned int r9, unsigned int r10);

// HUD list-builder primitives
extern void hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void hud_element_scroll_vertical(double amount, int a);
extern void hud_set_context_property(int prop);
extern void hud_add_list_item(void);
extern void hud_item_add_spacer(double h, int flags);
extern void hud_event_add_string(int str);
extern void hud_element_apply_scroll(void);
extern void hud_element_init_from_layout(int layoutId, int a, int b, void *cb);
extern void hud_element_clear_dirty_flag(int element);
extern void hud_clear_both_callbacks(void);
extern void hud_element_apply_center_xy(int anchor);
extern void ticker_build_layout(double w, double h, int color, int a, int b, int c);
extern void ticker_render_dispatch(double x, double y, double s1, double s2, double s3,
                                   int color, int element, int a, int b);
extern void arcsel_draw_center_logo(double x, double y, double scale);

// referenced but decompiled in a later batch (raw externs)
extern void FUN_80321218(void);            // refresh/re-root the editor screen
extern void FUN_80321a38(void);            // pop-current wrapper (options screen close cb)
extern void FUN_80345da8(int mode);        // set editor sub-state

// forward decls within this TU
extern "C" void aveditor_entry_transition(unsigned long long a1, unsigned long long a2,
                                          unsigned long long a3, unsigned long long a4,
                                          unsigned long long a5, unsigned long long a6,
                                          unsigned long long a7, unsigned long long a8);
extern "C" void aveditor_build_options_list(void);

// ---------------------------------------------------------------------------
// 0x803208e4  aveditor_entry_confirm
// "confirm" callback on the editor entry screen. If the screen is not already
// closing: latch the pending transition, push the screen, clear the
// "weaponset-forced" flag (ctx+0x238 bit 0x8000). If the level stream is a saved
// map (tag 0xc77667) clear the extra bit 0x400 and enter the editor directly,
// otherwise force the weapon-set select popup (ctx+0x25c bit 0x40000).
// ---------------------------------------------------------------------------
void aveditor_entry_confirm(unsigned long long param_1, unsigned long long param_2,
                            unsigned long long param_3, unsigned long long param_4,
                            unsigned long long param_5, unsigned long long param_6,
                            unsigned long long param_7, unsigned long long param_8,
                            int element)
{
    if (hud_element_is_closing(element) != 0)
        return;

    DAT_80612c90 = 1;
    hud_screen_push();
    *(unsigned int *)(DAT_80612be4 + 0x238) &= 0xffff7fffu;

    if (*DAT_80612be8 == 0xc77667) {
        *(unsigned int *)(DAT_80612be4 + 0x238) &= 0xfffffbffu;
        aveditor_enter_screen();
        aveditor_ensure_models_loaded((unsigned int)DAT_80612be4, param_2, param_3, param_4,
                                      param_5, param_6, param_7, param_8);
    } else {
        *(unsigned int *)(DAT_80612be4 + 0x25c) |= 0x40000u;
        aveditor_open_weaponset_select();
    }
}

// ---------------------------------------------------------------------------
// 0x80320980  aveditor_entry_fade_tick
// Per-frame update: accumulate the fade-in alpha (rate = FLOAT_8061a850 scaled by
// FLOAT_806108e8 and normalised by the frame delta derived from DAT_806109c0).
// When the screen is not closing and the alpha has passed the completion
// threshold, kick off the transition dispatcher.
// ---------------------------------------------------------------------------
void aveditor_entry_fade_tick(int element)
{
    double rateNum = (double)FLOAT_8061a850;
    double rateScale = (double)FLOAT_806108e8;
    double frameDelta =
        (double)(float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_8061a868);

    FLOAT_80612c94 = (float)(rateScale * (double)(float)(rateNum / (double)(float)frameDelta) +
                             (double)FLOAT_80612c94);

    if ((hud_element_is_closing(element) == 0) &&
        ((double)FLOAT_8061a860 < (double)FLOAT_80612c94)) {
        aveditor_entry_transition((unsigned long long)FLOAT_80612c94, 0, 0, 0, 0, 0, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x803209f4  aveditor_entry_back
// Back/cancel callback: reset the fade accumulator to its base value and navigate
// back to the previous screen descriptor.
// ---------------------------------------------------------------------------
void aveditor_entry_back(int element)
{
    if (hud_element_is_closing(element) != 0)
        return;

    FLOAT_80612c94 = FLOAT_8061a834;
    hud_screen_navigate_to(*(int *)(element + 0x2c), &DAT_80480360, -1);
}

// ---------------------------------------------------------------------------
// 0x80320a44  aveditor_entry_transition
// Screen-transition dispatcher. If no transition is pending (DAT_80612c90==0)
// set the two entry bits (ctx+0x238 bits 0x8000|0x400) and run the memory-card
// check. Otherwise: for a saved map (tag 0xc77667) enter the editor directly,
// else set bit 0x400 + force the weapon-set select popup (ctx+0x25c bit 0x40000).
// ---------------------------------------------------------------------------
extern "C" void aveditor_entry_transition(unsigned long long param_1, unsigned long long param_2,
                                          unsigned long long param_3, unsigned long long param_4,
                                          unsigned long long param_5, unsigned long long param_6,
                                          unsigned long long param_7, unsigned long long param_8)
{
    (void)param_1;
    if (DAT_80612c90 == 0) {
        *(unsigned int *)(DAT_80612be4 + 0x238) |= 0x8000u;
        *(unsigned int *)(DAT_80612be4 + 0x238) |= 0x400u;
        memcard_check_enter_active(DAT_80612bf4, &DAT_80480350, &DAT_80480370);
    } else if (*DAT_80612be8 == 0xc77667) {
        aveditor_enter_screen();
        aveditor_ensure_models_loaded((unsigned int)DAT_80612be4, param_2, param_3, param_4,
                                      param_5, param_6, param_7, param_8);
    } else {
        *(unsigned int *)(DAT_80612be4 + 0x238) |= 0x400u;
        *(unsigned int *)(DAT_80612be4 + 0x25c) |= 0x40000u;
        aveditor_open_weaponset_select();
    }
}

// ---------------------------------------------------------------------------
// 0x80320af0  aveditor_start_arcade_match
// Zero a 0x98-byte match-parameter block on the stack, tag it (byte at +0x4c =
// 0x65 == arcade/editor test), start the match cutscene transition, then clear
// the transition latch and the arcade-launch scratch flag.
// ---------------------------------------------------------------------------
void aveditor_start_arcade_match(unsigned long long param_1, unsigned long long param_2,
                                 unsigned long long param_3, unsigned long long param_4,
                                 unsigned long long param_5, unsigned long long param_6,
                                 unsigned long long param_7, unsigned long long param_8)
{
    unsigned char matchParams[0x98];

    memset_wrapper((int)matchParams, 0, 0x98);
    matchParams[0x4c] = 0x65;

    match_start_cutscene_transition(param_1, param_2, param_3, param_4, param_5, param_6,
                                    param_7, param_8, (unsigned int)matchParams, 0, 0x98,
                                    0, 0, 0, 0, 0);
    DAT_80612c90 = 0;
    DAT_805d59dc = 0;
}

// ---------------------------------------------------------------------------
// 0x80320b40  aveditor_build_options_list
// Populate the editor options list: item padding/scroll setup, a "context" flag,
// then a run of list items each pairing a display string (0x18f8..0x1910, 0x1768,
// 0x1785) with its help/value string (0xcd5..0xce2). Applies the scroll extents
// at the end.
// ---------------------------------------------------------------------------
extern "C" void aveditor_build_options_list(void)
{
    int s;

    hud_set_item_padding_mode((double)FLOAT_8061a870, 1, 1, 1);
    hud_element_scroll_vertical((double)FLOAT_8061a874, 0);
    hud_set_context_property(0x100);

    get_string_by_id(0x18fa); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xcd6); hud_event_add_string(s);

    get_string_by_id(0x18f8); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xcd8); hud_event_add_string(s);

    get_string_by_id(0x18fb); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xcd7); hud_event_add_string(s);

    get_string_by_id(0x18f9); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xcd5); hud_event_add_string(s);

    get_string_by_id(0x1768); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xce1); hud_event_add_string(s);

    get_string_by_id(0x1910); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xcdf); hud_event_add_string(s);

    get_string_by_id(0x190f); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xce0); hud_event_add_string(s);

    get_string_by_id(0x1904); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xcda); hud_event_add_string(s);

    get_string_by_id(0x1905); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xcd9); hud_event_add_string(s);

    get_string_by_id(0x190e); hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xce2); hud_event_add_string(s);

    hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0xcdd); hud_event_add_string(s);

    hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a870, 0);
    s = get_string_by_id(0x1785); hud_event_add_string(s);

    hud_element_apply_scroll();
}

// ---------------------------------------------------------------------------
// 0x80320e20  aveditor_options_screen_init
// Build the options screen element from a ticker layout, install the pop-current
// close callback, clear the dirty flag / callbacks, build the options list,
// centre it and nudge it up by FLOAT_8061a880. Sets element flag bit 0x10.
// ---------------------------------------------------------------------------
void aveditor_options_screen_init(int element)
{
    ticker_build_layout((double)FLOAT_8061a878, (double)FLOAT_8061a878, -0x7fa29ac0, 1, 2, 0);
    hud_element_init_from_layout(0, 0, 0, (void *)FUN_80321a38);
    hud_element_clear_dirty_flag(element);
    hud_clear_both_callbacks();
    aveditor_build_options_list();
    hud_element_apply_center_xy(DAT_806119e0);

    float yOff = FLOAT_8061a880;
    *(unsigned int *)(element + 0x214) |= 0x10u;
    *(float *)(element + 0x60) -= yOff;
}

// ---------------------------------------------------------------------------
// 0x80320ec0  aveditor_screen_invoke_cb
// If the element's low-word flag (+0xc0 & 0xffff) is set, invoke the callback
// stored at element+0x1bc.
// ---------------------------------------------------------------------------
void aveditor_screen_invoke_cb(int element)
{
    if ((*(unsigned int *)(element + 0xc0) & 0xffff) != 0) {
        (**(void (**)(void))(element + 0x1bc))();
    }
}

// ---------------------------------------------------------------------------
// 0x80320ef4  aveditor_options_screen_render
// Render the options screen: draw the centre logo at the element's transformed
// position (a4/ac scaled by FLOAT_8061a884, a8/b0 by FLOAT_8061a888, scale from
// +0xf0), then dispatch the ticker render at the same position.
// ---------------------------------------------------------------------------
void aveditor_options_screen_render(int element)
{
    double x = (double)(FLOAT_8061a884 * *(float *)(element + 0xac) + *(float *)(element + 0xa4));
    double y = (double)(FLOAT_8061a888 * *(float *)(element + 0xb0) + *(float *)(element + 0xa8));

    arcsel_draw_center_logo(x, y, (double)*(float *)(element + 0xf0));

    double s = (double)FLOAT_8061a870;
    ticker_render_dispatch(x, y, s, s, s, -0x7fa29ac0, element, 0, 1);
}

// ---------------------------------------------------------------------------
// 0x80320f98  aveditor_options_screen_close
// Clear the "options open" byte in the editor context (+0x11854), refresh/re-root
// the editor screen, and set the editor sub-state to 7.
// ---------------------------------------------------------------------------
void aveditor_options_screen_close(void)
{
    *(unsigned char *)(DAT_80612be4 + 0x11854) = 0;
    FUN_80321218();
    FUN_80345da8(7);
}
