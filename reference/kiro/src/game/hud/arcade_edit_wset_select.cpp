// Batch: batch_916 | Module: arcade map-editor weapon-set / palette-slot select+exit handlers
// Address range: 0x80326b90 - 0x80327088
// Date: 2026-09-12
//
// The select/back/exit callback layer for the arcade map-editor weapon-set assignment screens
// (the four modal picker vtables PTR_FUN_80480e20/30/50/60/70). These sit above the batch_914/915
// colour picker and palette-grid screens: when the user picks a value on one of the wset picker
// rows this decides where the byte lands - into the active edit record (context+0x2f18) or,
// for a multi-selection edit, broadcast across every selected member in the chain (DAT_80612c14,
// linked via +0x40, selected = flag bit 2 at member+0x14).
//
//   aveditor_wset_entry_equal       0x80326b90  compare two wset-entry records for equality (0/1).
//   aveditor_apply_over_selection   0x80326c1c  run a callback over the active record or the
//                                               whole selection until it returns nonzero.
//   aveditor_any_nondefault_wset    0x80326d04  is any active/selected entry != the default wset.
//   aveditor_broadcast_wset_id      0x80326d94  write the picked wset id byte to all selected.
//   aveditor_reset_selection_wset   0x80326dc0  reset the selection's wset to the current set.
//   aveditor_confirm_wset_revert    0x80326e30  pop the "revert changes?" yes/no dialog.
//   aveditor_wset_select            0x80326e84  select handler: route the picked value by vtable.
//   aveditor_wset_clear_editing     0x80327038  clear the editing-active flag + request redraw.
//   aveditor_wset_hide              0x80327054  hide the wset screen + clear editing.
//   aveditor_wset_back              0x80327088  back/exit handler: pop or clear-mode by vtable.
//
// The screen's currently-focused item value lives at item+0x188 (the picked list index / value).
// The editor context (DAT_80612be4) carries: +0x238 a "sound played" latch (bit 0x20), +0x25c a
// state-flags word (bit 0x1000000 = input-locked, bit 0x100000 = editing-active), and +0x2f18 the
// active edit-record pointer (0 => operate on the whole selection / the live palette-slot state).
// DAT_805d674c holds the live palette-slot bytes (byte0 = wset id, byte1 = secondary id).
//
// Raw FUN_/DAT_/PTR_/FLOAT_ symbols get correctly-typed per-file externs; named cross-module
// helpers (and the batch_914 aveditor_* helpers) are re-declared with their real ABI.

#include "common.h"

extern "C" {

extern int DAT_80612be4;   // editor context base
extern int DAT_80612c14;   // active member list head (chain via +0x40)
extern int DAT_80612bec;   // HUD redraw/tick request word

// Live palette-slot bytes: byte0 = weapon-set id, byte1 = secondary id.
extern unsigned int DAT_805d674c;

// Modal picker screen vtables (compared as the screen's +4 vtable pointer).
extern void *PTR_FUN_80480e20;   // wset primary / editing-active picker
extern void *PTR_FUN_80480e30;   // simple value picker
extern void *PTR_FUN_80480e50;   // wset id picker
extern void *PTR_FUN_80480e60;   // wset secondary-id picker
extern void *PTR_FUN_80480e70;   // revert-confirm entry

// Callback trampoline used by aveditor_apply_over_selection when walking the selection.
extern unsigned char LAB_80326c04;   // (code label used as a callback address)

// -- raw helpers referenced by these handlers --
// FUN_803826e8: spills r27..r31 into a 5-word scratch buffer; used as a default-record source.
extern unsigned long long FUN_803826e8(void);
// FUN_80348238: sound/effect dispatcher, index into PTR_FUN_80481ba4 (8 = confirm/latch sound).
extern void FUN_80348238(int index);

// -- named cross-module helpers (real ABI) --
extern char aveditor_is_default_wset_entry(unsigned char *entry);   // batch_914: -1 true / -2 false, 0 = "is default"
extern void aveditor_colorpicker_back(int screen);                  // batch_914
extern void aveditor_set_current_weapon_set(unsigned int id);
extern int  aveditor_get_current_weapon_set(void);
extern int  get_string_by_id(int id);
extern void hud_create_yes_no_dialog(int screen, int text, int yes_cb, int arg);
extern void avanim_play_move_sound(int id);
extern void screen_exit_clear_mode(void);
extern void hud_screen_mark_hidden(int screen);
extern void hud_screen_pop_current(void);

// Compare two weapon-set entry records (kind byte, id byte, two ints, two floats) for equality.
// Returns 1 when identical, 0 otherwise (matches the countLeadingZeros>>5 boolean idiom).
unsigned int aveditor_wset_entry_equal(char *a, char *b)
{
    if ((a[0] == b[0]) && (a[1] == b[1]) &&
        (*(int *)(a + 4) == *(int *)(b + 4)) &&
        (*(int *)(a + 8) == *(int *)(b + 8)) &&
        (*(float *)(a + 0xc) == *(float *)(b + 0xc)) &&
        (*(float *)(a + 0x10) == *(float *)(b + 0x10))) {
        return 1;
    }
    return 0;
}

// Run a callback over the wset record(s): if the active record is not part of a multi-selection
// (its +0x14 flag bit 2 clear) apply the callback to it directly; otherwise walk the selected
// members in the chain, invoking the callback on each selected record until one returns nonzero.
// arg may be NULL, in which case a default 5-word record (r27..r31 spill via FUN_803826e8) is used.
void aveditor_apply_over_selection(int active_rec, unsigned int picked_value,
                                   unsigned int *arg, unsigned char *callback)
{
    int hi;
    unsigned long long spill;
    unsigned int local_rec[10];

    (void)picked_value;

    spill = FUN_803826e8();
    hi = (int)((unsigned long long)spill >> 0x20);
    local_rec[0] = (unsigned int)spill;
    if (arg == 0) {
        arg = local_rec;
    }

    int member = DAT_80612c14;
    if ((*(unsigned char *)(hi + 0x14) & 2) == 0) {
        ((void (*)(int, unsigned int *))callback)(hi + 0x18, arg);
    } else {
        while (member != 0) {
            if ((*(unsigned char *)(member + 0x14) & 2) != 0) {
                int r = ((int (*)(int, unsigned int *))callback)(member + 0x18, arg);
                if (r != 0) {
                    break;
                }
            }
            member = *(int *)(member + 0x40);
        }
    }
}

// Return 1 if any active/selected weapon-set entry differs from the default wset entry.
int aveditor_any_nondefault_wset(int active_rec)
{
    int member = DAT_80612c14;

    if ((*(unsigned char *)(active_rec + 0x14) & 2) == 0) {
        if (aveditor_is_default_wset_entry((unsigned char *)(active_rec + 0x18)) == '\0') {
            return 1;
        }
        return 0;
    }

    for (; member != 0; member = *(int *)(member + 0x40)) {
        if (((*(unsigned char *)(member + 0x14) & 2) != 0) &&
            (aveditor_is_default_wset_entry((unsigned char *)(member + 0x18)) == '\0')) {
            return 1;
        }
    }
    return 0;
}

// Broadcast the picked weapon-set id byte (from item+0x188) to every selected member's record.
void aveditor_broadcast_wset_id(int screen)
{
    int member = DAT_80612c14;

    while (member != 0) {
        if ((*(unsigned char *)(member + 0x14) & 2) != 0) {
            *(char *)(member + 0x18) = (char)*(int *)(screen + 0x188);
        }
        member = *(int *)(member + 0x40);
    }
}

// Reset the selection's weapon-set to the current set. When screen != 0 first seed the current set
// from the live palette-slot byte, then copy the current set id into every selected member's
// record, and finally exit the picker.
void aveditor_reset_selection_wset(int screen)
{
    int member = DAT_80612c14;

    if (screen != 0) {
        aveditor_set_current_weapon_set((unsigned int)(unsigned char)DAT_805d674c);
    }
    for (; member != 0; member = *(int *)(member + 0x40)) {
        int cur = aveditor_get_current_weapon_set();
        *(char *)(member + 0x18) = (char)cur;
    }
    if (screen != 0) {
        aveditor_colorpicker_back(screen);
    }
}

// Pop the "revert changes?" yes/no confirm dialog (string 0xb68, yes-callback 0x80326dc0 =
// aveditor_reset_selection_wset), then snapshot the picked id into the live palette-slot byte.
void aveditor_confirm_wset_revert(int screen)
{
    int text = get_string_by_id(0xb68);
    hud_create_yes_no_dialog(screen, text, (int)&aveditor_reset_selection_wset, 0);
    *(char *)&DAT_805d674c = (char)*(int *)(screen + 0x188);
}

// Select handler for the weapon-set assignment screens. Routes the currently-focused item value
// (item+0x188) to the right destination based on which modal picker vtable the screen is running,
// mirroring the batch_914/915 palette flow. No-ops while the item is unset (< 0) or input is locked
// (context+0x25c bit 0x1000000).
void aveditor_wset_select(int screen)
{
    if ((*(int *)(screen + 0x188) < 0) ||
        ((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x1000000) != 0)) {
        return;
    }

    // Primary wset-id picker: land into the active record's +0x18, else the live palette slot byte0.
    if (*(void ***)(screen + 4) == &PTR_FUN_80480e50) {
        unsigned char id = (unsigned char)*(int *)(screen + 0x188);
        if (*(int *)(DAT_80612be4 + 0x2f18) != 0) {
            *(unsigned char *)(*(int *)(DAT_80612be4 + 0x2f18) + 0x18) = id;
            id = (unsigned char)DAT_805d674c;
        }
        *(char *)&DAT_805d674c = (char)id;
        aveditor_colorpicker_back(screen);
    }

    void **vtable = *(void ***)(screen + 4);
    if (vtable == &PTR_FUN_80480e60) {
        // Secondary-id picker: land into the active record's +0x19, else palette slot byte1.
        if (*(int *)(DAT_80612be4 + 0x2f18) == 0) {
            ((unsigned char *)&DAT_805d674c)[1] = (unsigned char)*(int *)(screen + 0x188);
        } else {
            *(char *)(*(int *)(DAT_80612be4 + 0x2f18) + 0x19) = (char)*(int *)(screen + 0x188);
        }
        aveditor_colorpicker_back(screen);
    } else if (vtable == &PTR_FUN_80480e20) {
        // Editing-active picker: single-record path latches editing-active mode and plays the move
        // sound; multi-selection path commits + broadcasts, plays the confirm sound once.
        if (*(int *)(DAT_80612be4 + 0x2f18) == 0) {
            avanim_play_move_sound(0x1578);
            *(char *)&DAT_805d674c = (char)*(int *)(screen + 0x188);
            *(unsigned int *)(DAT_80612be4 + 0x25c) =
                *(unsigned int *)(DAT_80612be4 + 0x25c) | 0x100000;
        } else {
            int changed = 0;
            aveditor_apply_over_selection(*(int *)(DAT_80612be4 + 0x2f18),
                                          *(unsigned int *)(screen + 0x188), 0, &LAB_80326c04);
            // (the walk's aggregate result drives the confirm sound below)
            changed = 1;
            if ((changed != 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
                FUN_80348238(8);
                *(unsigned int *)(DAT_80612be4 + 0x238) =
                    *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
            }
            *(char *)(*(int *)(DAT_80612be4 + 0x2f18) + 0x18) = (char)*(int *)(screen + 0x188);
            aveditor_broadcast_wset_id(screen);
        }
        screen_exit_clear_mode();
    } else if (vtable == &PTR_FUN_80480e30) {
        // Simple value picker: land the value into the live palette slot byte0.
        *(char *)&DAT_805d674c = (char)*(int *)(screen + 0x188);
    } else if (vtable == &PTR_FUN_80480e70) {
        // Revert-confirm entry: pop the yes/no dialog.
        aveditor_confirm_wset_revert(screen);
    }
}

// Clear the editing-active flag (context+0x25c bit 0x100000) and request a HUD redraw.
void aveditor_wset_clear_editing(void)
{
    *(unsigned int *)(DAT_80612be4 + 0x25c) =
        *(unsigned int *)(DAT_80612be4 + 0x25c) & 0xfeffffff;
    DAT_80612bec = 0x10;
}

// Hide the weapon-set screen: clear editing-active, then mark the screen hidden.
void aveditor_wset_hide(int screen)
{
    aveditor_wset_clear_editing();
    hud_screen_mark_hidden(screen);
}

// Back/exit handler for the weapon-set screens: the modal pickers just pop the current screen;
// the editing-active picker (PTR_FUN_80480e20) hides itself and clears the screen mode.
void aveditor_wset_back(int screen)
{
    void **vtable = *(void ***)(screen + 4);

    if ((vtable == &PTR_FUN_80480e70) || (vtable == &PTR_FUN_80480e50) ||
        (vtable == &PTR_FUN_80480e60) || (vtable == &PTR_FUN_80480e30)) {
        hud_screen_pop_current();
    } else if (vtable == &PTR_FUN_80480e20) {
        aveditor_wset_hide(screen);
        screen_exit_clear_mode();
    }
}

} // extern "C"
