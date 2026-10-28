// arcade_edit_screennav.cpp
// Arcade map-editor screen navigation / help-prompt helpers: the confirm-prompt
// flag accessors, the context-help back-dialog opener, the options-screen toggle,
// the modal/root state predicate, and the screen-root traversal helpers used to
// locate the active screen node.
//
// The editor screen root is resolved from the descriptor table DAT_8051a820
// indexed by DAT_806119ec (0x1e0 stride) via hud_screen_find_root, and compared
// with the parent handle DAT_80612bf4. DAT_80612ca8 is the confirm-prompt-active
// flag; the editor context bitmask lives at DAT_80612be4+0x25c (mask 0x4044088 =>
// modal sub-state) and +0x238 (bit 2 => no-confirm sub-state).
//
// Address: 0x803214b8..0x80321788 | Batch: batch_906 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern int DAT_80612be4;      // editor screen context base
extern int DAT_80612bf4;      // parent/root screen handle
extern int DAT_806119ec;      // active screen table index
extern int DAT_8051a820;      // screen descriptor table base
extern int DAT_80612ca8;      // confirm-prompt-active flag
extern int DAT_804803a0;      // help-string id table (index * 4)
extern int DAT_80477c84;      // tool descriptor table (stride 0xc4, +0 = help id)

extern int  get_string_by_id(int stringId);
extern int  hud_screen_find_root(int descriptor);
extern void hud_create_back_dialog(int screen, int str, int a);
extern void hud_input_reset_repeat_timers(void);

// cross-module helpers (named elsewhere)
extern void aveditor_options_screen_close(void);
extern void aveditor_options_screen_open_flag(void);
extern void aveditor_update_back_softkey(int screen, int enable);

// referenced but decompiled in a later batch (raw externs)
extern int  FUN_80323db0(void);   // current tool/palette index
extern int  FUN_80323e00(void);   // options-toggle predicate
extern void FUN_80345da8(int mode);
extern void FUN_80345c2c(int mode);

// forward decls within this TU
extern "C" void aveditor_set_confirm_prompt(int value);
extern "C" int  aveditor_get_confirm_prompt(void);
extern "C" void aveditor_open_help_dialog(int helpIndex);
extern "C" int  aveditor_find_active_screen_node(void);

// ---------------------------------------------------------------------------
// 0x803214b8  aveditor_set_confirm_prompt
// Set the confirm-prompt-active flag.
// ---------------------------------------------------------------------------
extern "C" void aveditor_set_confirm_prompt(int value)
{
    DAT_80612ca8 = value;
}

// ---------------------------------------------------------------------------
// 0x803214c0  aveditor_get_confirm_prompt
// Return the confirm-prompt-active flag.
// ---------------------------------------------------------------------------
extern "C" int aveditor_get_confirm_prompt(void)
{
    return DAT_80612ca8;
}

// ---------------------------------------------------------------------------
// 0x803214c8  aveditor_open_help_dialog
// If no confirm prompt is active, mark it active and open a "back" help dialog on
// the screen root with the help string resolved from the id table
// (DAT_804803a0[helpIndex]).
// ---------------------------------------------------------------------------
extern "C" void aveditor_open_help_dialog(int helpIndex)
{
    int root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));

    if (aveditor_get_confirm_prompt() == 0) {
        aveditor_set_confirm_prompt(1);
        int str = get_string_by_id(*(int *)(&DAT_804803a0 + helpIndex * 4));
        hud_create_back_dialog(root, str, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x80321550  aveditor_toggle_options_screen
// When no confirm prompt is active and the no-confirm sub-state is clear
// (ctx+0x238 bit 2), toggle the options screen: if already open (ctx+0x11854==1)
// close it, else set the open flag. Resets the input repeat timers.
// ---------------------------------------------------------------------------
void aveditor_toggle_options_screen(void)
{
    if ((aveditor_get_confirm_prompt() == 0) &&
        ((*(unsigned int *)(DAT_80612be4 + 0x238) & 2u) == 0)) {
        if (*(char *)(DAT_80612be4 + 0x11854) == 1) {
            aveditor_options_screen_close();
        } else {
            aveditor_options_screen_open_flag();
        }
        hud_input_reset_repeat_timers();
    }
}

// ---------------------------------------------------------------------------
// 0x803215b0  aveditor_is_modal_or_offroot
// Predicate: true (1) when a modal sub-state is active (ctx+0x25c & 0x4044088) or
// the editor is not on its own screen root; false (0) otherwise.
// ---------------------------------------------------------------------------
int aveditor_is_modal_or_offroot(void)
{
    int flag = 0;
    if (((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x4044088u) != 0) ||
        (hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)) != DAT_80612bf4)) {
        flag = 1;
    }
    return flag;
}

// ---------------------------------------------------------------------------
// 0x80321624  aveditor_show_tool_help
// Look up the current tool index (0..0x4f), fetch its help-string id from the
// tool descriptor table (DAT_80477c84, stride 0xc4), and if valid (0..0x10f)
// open the context help dialog.
// ---------------------------------------------------------------------------
void aveditor_show_tool_help(void)
{
    int tool = FUN_80323db0();
    if ((-1 < tool) && (tool < 0x50)) {
        int helpId = *(int *)(&DAT_80477c84 + tool * 0xc4);
        if ((-1 < helpId) && (helpId < 0x110)) {
            aveditor_open_help_dialog(helpId);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8032167c  aveditor_toggle_substate_7
// Depending on the options-toggle predicate FUN_80323e00: if 0 set editor
// sub-state 7 (FUN_80345da8), else prime the back soft-key and clear sub-state 7
// (FUN_80345c2c).
// ---------------------------------------------------------------------------
void aveditor_toggle_substate_7(void)
{
    if (FUN_80323e00() == 0) {
        FUN_80345da8(7);
    } else {
        aveditor_update_back_softkey(0, 1);
        FUN_80345c2c(7);
    }
}

// ---------------------------------------------------------------------------
// 0x803216c4  aveditor_find_active_screen_node
// Walk the screen root's sibling chain (+0x2c) starting from the editor root,
// returning the first node with a non-zero active sub-element (+0x194). Stops and
// returns the current node when the chain ends or wraps back to the root.
// ---------------------------------------------------------------------------
extern "C" int aveditor_find_active_screen_node(void)
{
    int node = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));

    while (true) {
        int next = *(int *)(node + 0x2c);
        if (next == 0) {
            return node;
        }
        if (*(int *)(node + 0x194) != 0) {
            break;
        }
        node = next;
        if (next == *(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)) {
            return node;
        }
    }
    return node;
}

// ---------------------------------------------------------------------------
// 0x80321744  aveditor_show_active_node_help
// Find the active screen node; if it has an active sub-element (+0x194) whose type
// id (deref +0x194, field +0x18) is valid (0..0x10f), open the context help dialog.
// ---------------------------------------------------------------------------
void aveditor_show_active_node_help(void)
{
    int node = aveditor_find_active_screen_node();
    if (*(int *)(node + 0x194) != 0) {
        int helpId = *(int *)(*(int *)(node + 0x194) + 0x18);
        if ((-1 < helpId) && (helpId < 0x110)) {
            aveditor_open_help_dialog(helpId);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80321788  aveditor_get_screen_root
// Return the editor screen root (hud_screen_find_root of the active descriptor).
// ---------------------------------------------------------------------------
int aveditor_get_screen_root(void)
{
    return hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
}
