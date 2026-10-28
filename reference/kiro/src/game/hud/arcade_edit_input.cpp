// arcade_edit_input.cpp
// Arcade map-editor screen input dispatch: classifies the current screen input
// state and routes to the appropriate help / toggle / close action, plus the
// per-frame screen update and the pop-current close callback.
//
// The two 2-entry dispatch tables live back-to-back at 0x80480390:
//   PTR_help[]   @0x80480390 = { aveditor_show_tool_help, aveditor_show_active_node_help }
//   PTR_toggle[] @0x80480394 = { aveditor_toggle_substate_7, aveditor_get_screen_root }
// each indexed by aveditor_is_modal_or_offroot() (0 => own-root, 1 => modal/offroot).
//
// The editor screen root is resolved from DAT_8051a820[DAT_806119ec*0x1e0] via
// hud_screen_find_root; the editor context is DAT_80612be4 (+0x238 state bits,
// +0x11854 options-open byte).
//
// Address: 0x803217c0..0x80321a38 | Batch: batch_907 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern int DAT_80612be4;      // editor screen context base
extern int DAT_806119ec;      // active screen table index
extern int DAT_8051a820;      // screen descriptor table base

extern int  hud_screen_find_root(int descriptor);
extern void hud_screen_pop_current(void);

// cross-module helpers (named elsewhere)
extern "C" int  aveditor_is_modal_or_offroot(void);
extern "C" int  aveditor_is_confirm_prompt_active(int screen);
extern "C" int  aveditor_get_confirm_prompt(void);
extern "C" void aveditor_set_confirm_prompt(int value);
extern "C" void aveditor_open_help_dialog(int helpIndex);
extern "C" void aveditor_update_back_softkey(int screen, int enable);
extern "C" void aveditor_options_screen_close(void);
extern "C" void aveditor_setup_softkeys(void);

// action dispatch block at 0x80480390: 4 consecutive function pointers.
//   [0] aveditor_show_tool_help      [1] aveditor_toggle_substate_7
//   [2] aveditor_show_active_node_help  [3] aveditor_get_screen_root
// The help dispatcher indexes [state*2] (0 or 2); the toggle dispatcher indexes
// this same block offset by one slot, i.e. [state*2 + 1] (1 or 3). Modelled as a
// single 4-entry table so the 8-byte-per-state interleave is exact.
extern void (*PTR_aveditor_dispatch_80480390[4])(void);

// forward decls within this TU
extern "C" int aveditor_classify_input_state(void);
extern "C" void aveditor_process_input_state(void);
extern "C" void aveditor_input_help_dispatch(void);
extern "C" void aveditor_input_toggle_dispatch(void);

// ---------------------------------------------------------------------------
// 0x803217c0  aveditor_input_toggle_dispatch
// Dispatch through PTR_toggle[] by the modal/offroot state: own-root runs
// aveditor_toggle_substate_7, modal/offroot runs aveditor_get_screen_root (no-op
// in this context). Index step is 2 words (one function pointer per entry).
// ---------------------------------------------------------------------------
extern "C" void aveditor_input_toggle_dispatch(void)
{
    unsigned int idx = (unsigned int)(unsigned char)aveditor_is_modal_or_offroot();
    PTR_aveditor_dispatch_80480390[idx * 2 + 1]();
}

// ---------------------------------------------------------------------------
// 0x803217fc  aveditor_input_help_dispatch
// Dispatch through PTR_help[] by the modal/offroot state: own-root runs
// aveditor_show_tool_help, modal/offroot runs aveditor_show_active_node_help.
// ---------------------------------------------------------------------------
extern "C" void aveditor_input_help_dispatch(void)
{
    unsigned int idx = (unsigned int)(unsigned char)aveditor_is_modal_or_offroot();
    PTR_aveditor_dispatch_80480390[idx * 2]();
}

// ---------------------------------------------------------------------------
// 0x80321834  aveditor_classify_input_state
// Classify the current editor screen input state:
//   2  = the active screen has a confirm prompt open
//  -2  = a confirm prompt is globally active
//   0  = the root has the options bit (+0xc0 & 0x10) set
//   1  = the root has the highlight bit (+0xc0 & 0x20) set
//  -1  = neither bit set (idle)
// ---------------------------------------------------------------------------
extern "C" int aveditor_classify_input_state(void)
{
    int root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if (aveditor_is_confirm_prompt_active(root) != 0)
        return 2;

    if (aveditor_get_confirm_prompt() != 0)
        return -2;

    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(unsigned int *)(root + 0xc0) & 0x10) != 0)
        return 0;

    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(unsigned int *)(root + 0xc0) & 0x20) != 0)
        return 1;

    return -1;
}

// ---------------------------------------------------------------------------
// 0x803218e4  aveditor_help_dialog_and_ack
// Open the context help dialog and acknowledge (always returns 1).
// ---------------------------------------------------------------------------
int aveditor_help_dialog_and_ack(int helpIndex)
{
    aveditor_open_help_dialog(helpIndex);
    return 1;
}

// ---------------------------------------------------------------------------
// 0x80321908  aveditor_prime_and_help
// Prime the back soft-key, then if the screen root has the options bit set open
// the context help dialog.
// ---------------------------------------------------------------------------
void aveditor_prime_and_help(int helpIndex)
{
    aveditor_update_back_softkey(0, 1);
    int root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(unsigned int *)(root + 0xc0) & 0x10) != 0) {
        aveditor_open_help_dialog(helpIndex);
    }
}

// ---------------------------------------------------------------------------
// 0x8032196c  aveditor_field4_is_zero
// Return 1 when the object's +0x4 field is zero, else 0 (clz(x) >> 5 idiom).
// ---------------------------------------------------------------------------
unsigned int aveditor_field4_is_zero(int obj)
{
    return countLeadingZeros(*(unsigned int *)(obj + 4)) >> 5;
}

// ---------------------------------------------------------------------------
// 0x8032197c  aveditor_can_open_options
// Return 1 when the options screen may be opened: the "options disabled" bit
// (ctx+0x238 & 0x200) is clear and the options screen is not already open
// (ctx+0x11854 != 1); else 0.
// ---------------------------------------------------------------------------
int aveditor_can_open_options(void)
{
    if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x200) != 0)
        return 0;
    if (*(char *)(DAT_80612be4 + 0x11854) == 1)
        return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// 0x803219a8  aveditor_process_input_state
// Run the action selected by aveditor_classify_input_state:
//   0  -> help dispatch
//  -1  -> (>-3, !=-2) toggle dispatch
//  -2  -> (skipped)
//   2  -> clear the confirm prompt
//  <2  -> close the options screen
// ---------------------------------------------------------------------------
extern "C" void aveditor_process_input_state(void)
{
    int state = aveditor_classify_input_state();
    if (state == 0) {
        aveditor_input_help_dispatch();
    } else if (state < 0) {
        if ((state != -2) && (-3 < state)) {
            aveditor_input_toggle_dispatch();
        }
    } else if (state == 2) {
        aveditor_set_confirm_prompt(0);
    } else if (state < 2) {
        aveditor_options_screen_close();
    }
}

// ---------------------------------------------------------------------------
// 0x80321a14  aveditor_screen_update
// Per-frame editor screen update: rebuild the soft-keys then process the input
// state.
// ---------------------------------------------------------------------------
void aveditor_screen_update(void)
{
    aveditor_setup_softkeys();
    aveditor_process_input_state();
}

// ---------------------------------------------------------------------------
// 0x80321a38  aveditor_screen_pop
// Pop-current close callback (installed as the options screen close cb).
// ---------------------------------------------------------------------------
void aveditor_screen_pop(void)
{
    hud_screen_pop_current();
}
