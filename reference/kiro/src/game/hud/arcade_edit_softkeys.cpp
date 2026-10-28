// arcade_edit_softkeys.cpp
// Arcade map-editor on-screen soft-key (button prompt) management. Registers /
// removes the A/B/confirm handlers on the editor screen root and keeps them in
// sync with the editor context flags.
//
// The screen root is resolved from the screen descriptor table DAT_8051a820
// indexed by the active screen slot DAT_806119ec (0x1e0 stride) via
// hud_screen_find_root, and compared against the parent handle DAT_80612bf4 to
// decide whether to operate on the "current" screen or an explicit one. The
// editor context DAT_80612be4+0x25c holds the mode/state bitmask (mask
// 0x4044088 => a modal/blocking sub-state is active); DAT_80612ca8 is the
// confirm-prompt-active flag.
//
// Address: 0x80320fd0..0x80321454 | Batch: batch_905 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern int DAT_80612be4;      // editor screen context base
extern int DAT_80612bec;      // soft-key layout mode
extern int DAT_80612bf4;      // parent/root screen handle
extern int DAT_806119ec;      // active screen table index
extern int DAT_8051a820;      // screen descriptor table base

extern int  get_string_by_id(int stringId);

// HUD screen soft-key handler primitives
extern void hud_screen_register_handler(int screen, int slot, int str, int kind, int a);
extern void hud_screen_register_handler_current(int slot, int str, int kind, int a);
extern void hud_screen_set_handler_param_b(int screen, int slot, int value);
extern void hud_screen_set_handler_b_current(int slot, int value);
extern void hud_screen_remove_handler(int screen, int slot);
extern void hud_screen_rebuild_items(void);
extern int  hud_screen_find_root(int descriptor);

// hud active-piece / handler-slot accessor (returns a PPC {base,index} pair);
// FUN_8038273c is its no-op epilogue counterpart.
extern "C" unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);

// confirm-prompt-active getter (0x803214c0: returns DAT_80612ca8)
extern int aveditor_get_confirm_prompt(void);

// forward decls within this TU
extern "C" void aveditor_set_handler_b(unsigned int slot, unsigned int a2, unsigned int value);

// ---------------------------------------------------------------------------
// 0x80320fd0  aveditor_options_screen_open_flag
// Mark the options screen as open (ctx+0x11854 = 1). Counterpart to
// aveditor_options_screen_close which clears it.
// ---------------------------------------------------------------------------
void aveditor_options_screen_open_flag(void)
{
    *(unsigned char *)(DAT_80612be4 + 0x11854) = 1;
}

// ---------------------------------------------------------------------------
// 0x80320fe4  aveditor_register_softkeys
// Register the standard editor soft-keys on an explicit screen: slot 1 = accept
// (string 0xa4d), slot 0 = back (string 0xa4f, kind 2); enable the param-b flag
// on slots 2 and 3. Sets the soft-key layout mode DAT_80612bec = 0x10.
// ---------------------------------------------------------------------------
void aveditor_register_softkeys(int screen)
{
    DAT_80612bec = 0x10;
    hud_screen_register_handler(screen, 1, get_string_by_id(0xa4d), 0, 0);
    hud_screen_register_handler(screen, 0, get_string_by_id(0xa4f), 2, 0);
    hud_screen_set_handler_param_b(screen, 2, 1);
    hud_screen_set_handler_param_b(screen, 3, 1);
}

// ---------------------------------------------------------------------------
// 0x80321074  aveditor_register_softkeys_current
// Same accept/back registration as aveditor_register_softkeys but on the current
// screen.
// ---------------------------------------------------------------------------
void aveditor_register_softkeys_current(void)
{
    DAT_80612bec = 0x10;
    hud_screen_register_handler_current(1, get_string_by_id(0xa4d), 0, 0);
    hud_screen_register_handler_current(0, get_string_by_id(0xa4f), 2, 0);
}

// ---------------------------------------------------------------------------
// 0x803210d0  aveditor_set_handler_b
// Resolve the target handler slot via the hud accessor pair (base = high word,
// slot = low word). If there is no base (index 0) or the editor is on its own
// root with no modal state, set it on the current screen; otherwise set it on the
// explicit base screen.
// ---------------------------------------------------------------------------
extern "C" void aveditor_set_handler_b(unsigned int slot, unsigned int a2, unsigned int value)
{
    (void)a2;
    unsigned long long pair = FUN_803826f0();
    int base = (int)(pair >> 0x20);
    unsigned int lowSlot = (unsigned int)pair;

    if ((base == 0) ||
        (((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x4044088u) == 0) &&
         (hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)) == DAT_80612bf4))) {
        hud_screen_set_handler_b_current((int)lowSlot, (int)value);
    } else {
        hud_screen_set_handler_param_b(base, (int)lowSlot, (int)value);
    }
    (void)slot;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8032116c  aveditor_update_back_softkey
// If the screen has no active sub-element (+0x194 == 0): when enable is set,
// (re)apply the handler via aveditor_set_handler_b. Otherwise, if the active
// sub-element's type (deref +0x194, field +0x18) is a valid id (0..0x10f), clear
// the back handler param (slot 0 = 0).
// ---------------------------------------------------------------------------
void aveditor_update_back_softkey(int screen, int enable)
{
    if ((screen == 0) || (*(int *)(screen + 0x194) == 0)) {
        if (enable != 0) {
            aveditor_set_handler_b((unsigned int)screen, 0, 0);
        }
    } else {
        int type = *(int *)(*(int *)(screen + 0x194) + 0x18);
        if ((-1 < type) && (type < 0x110)) {
            hud_screen_set_handler_param_b(screen, 0, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x803211d4  aveditor_remove_softkeys
// Remove the back(0)/accept(1) handlers from a screen and rebuild the item list.
// ---------------------------------------------------------------------------
void aveditor_remove_softkeys(int screen)
{
    hud_screen_remove_handler(screen, 0);
    hud_screen_remove_handler(screen, 1);
    hud_screen_rebuild_items();
}

// ---------------------------------------------------------------------------
// 0x80321218  aveditor_refresh_softkeys
// If a modal sub-state is active (ctx+0x25c & 0x4044088) or the editor is not on
// its own root, remove the soft-keys from the resolved screen root.
// ---------------------------------------------------------------------------
void aveditor_refresh_softkeys(void)
{
    if (((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x4044088u) != 0) ||
        (hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)) != DAT_80612bf4)) {
        int root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        aveditor_remove_softkeys(root);
    }
}

// ---------------------------------------------------------------------------
// 0x80321294  aveditor_setup_softkeys
// Build the editor soft-keys for the current screen root: on the own-root, no
// modal path register the current-screen accept/back handlers; on the explicit /
// modal path (with a non-empty root at +4) register the full handler set and
// prime the back soft-key.
// ---------------------------------------------------------------------------
void aveditor_setup_softkeys(void)
{
    int root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));

    if (((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x4044088u) == 0) &&
        (hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)) == DAT_80612bf4)) {
        aveditor_register_softkeys_current();
    }

    if ((((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x4044088u) != 0) ||
         (hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)) != DAT_80612bf4)) &&
        (*(int *)(root + 4) != 0)) {
        aveditor_register_softkeys(root);
        aveditor_update_back_softkey(root, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x80321370  aveditor_register_confirm_softkey
// When no confirm prompt is active and the editor is not in the "no-confirm"
// sub-state (ctx+0x238 bit 2 clear), register the confirm soft-key (string 0xa4e)
// on the current screen (own-root path) or the explicit resolved root.
// ---------------------------------------------------------------------------
void aveditor_register_confirm_softkey(void)
{
    int root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));

    if ((aveditor_get_confirm_prompt() == 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 2u) == 0)) {
        if ((root == 0) ||
            (((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x4044088u) == 0) &&
             (hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)) == DAT_80612bf4))) {
            hud_screen_register_handler_current(0, get_string_by_id(0xa4e), 0, 0);
        } else {
            hud_screen_register_handler(root, 0, get_string_by_id(0xa4e), 0, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80321454  aveditor_is_confirm_prompt_active
// Predicate: true when a confirm prompt is active (DAT_80612ca8 != 0) and the
// given screen's mode (+0x1ac) == 4.
// ---------------------------------------------------------------------------
unsigned int aveditor_is_confirm_prompt_active(int screen)
{
    if (screen == 0)
        return 0;

    if ((aveditor_get_confirm_prompt() != 0) && (*(int *)(screen + 0x1ac) == 4))
        return 1;

    return 0;
}
