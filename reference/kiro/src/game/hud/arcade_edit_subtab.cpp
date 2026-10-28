// Batch: batch_911 | Module: arcade map-editor sub-tab dispatch + label getters + screen glue
// Address range: 0x80323bec - 0x80323ef0
// Date: 2026-09-12
//
// The editor's sub-tab layer: it decides which sub-tab (tool group / enable toggle / piece type)
// is active, shows the label string for the active sub-tab, and dispatches label/action callbacks
// through a per-sub-tab function-pointer table. Plus a few thin screen-lifecycle wrappers.
//
//   aveditor_active_subtab          0x80323bec  resolve the active sub-tab index (0/1/2, or -1 none).
//   aveditor_show_toolgroup_label   0x80323c48  show the label for the current tool-group entry.
//   aveditor_show_toggle_label      0x80323c94  show the enabled/disabled label for the toggle row.
//   aveditor_show_piecetype_label   0x80323d70  show the label for the current selected piece type.
//   aveditor_dispatch_subtab_label  0x80323db0  call the active sub-tab's label callback (def -1).
//   aveditor_dispatch_subtab_action 0x80323e00  call the active sub-tab's action callback (def 0).
//   aveditor_screen_push            0x80323e4c  push the editor HUD screen.
//   aveditor_screen_exit            0x80323e78  editor screen exit: teardown + dirty check.
//   aveditor_rebuild_if_dirty       0x80323eb0  rebuild the screen items if it has unsaved changes.
//   aveditor_set_active_context     0x80323ef0  store the active editor context pointer.
//
// The two dispatchers index a table of {action, label} callback pairs (8-byte stride) by the
// active sub-tab; base 0x80480c5c gives the action callback, 0x80480c60 the label callback. The
// table is declared here as a flat pointer array (2 slots per sub-tab) and indexed [tab*2].
//
// Named cross-module helpers are declared locally (arcade_edit_options.cpp convention) with their
// real ABI; only common.h is pulled for shared types/intrinsics. Raw FUN_/DAT_ get per-file externs.

#include "common.h"

extern "C" {

extern int   DAT_80612be4;    // editor context base
extern int   DAT_80612cb0;    // active editor context pointer (set by aveditor_set_active_context)

// -- label string tables --
extern short DAT_80477bc8;    // tool-group label id table (stride 0xc4, short)
extern int   DAT_8047b160;    // "disabled" label string id
extern int   DAT_8047b224;    // "enabled" label string id
extern int   DAT_8047c150;    // piece-type label id table (stride 0x140)

// -- sub-tab callback table: {action, label} pairs, 8-byte stride, indexed [tab*2] --
extern void *PTR_subtab_callbacks_80480c5c;   // &table[0].action

// -- raw helpers not yet named --
extern void  FUN_80338190(void);   // editor screen teardown A
extern void  FUN_80338290(void);   // editor screen teardown B

// -- named cross-module helpers (real ABI) --
extern int   get_string_by_id(int stringId);
extern void  hud_screen_push(void);
extern void  screen_exit_dirty_check(void);
extern int   hud_element_has_unsaved_changes(int element);
extern void  hud_screen_rebuild_items(void);

} // extern "C"

// forward prototype for this file's own function used before definition
int aveditor_active_subtab(void);

// -----------------------------------------------------------------------------

// Resolve the active editor sub-tab index. When no tool group is bound (context+0x2f1c == 0) there
// is no sub-tab (-1 for the label path via callers). Returns:
//   1  = the "toggle" sub-tab   (context+0x2f78 valid and view mode 1)
//  -1  = none                    (piece index < 0, or view mode != 3)
//   2  = the "piece type" sub-tab
//   0  = the default tool-group sub-tab
int aveditor_active_subtab(void)
{
    if (*(int *)(DAT_80612be4 + 0x2f1c) != 0) {
        return 0;
    }
    if ((*(int *)(DAT_80612be4 + 0x2f78) >= 0) && (*(int *)(DAT_80612be4 + 0x254) == 1)) {
        return 1;
    }
    if (*(int *)(DAT_80612be4 + 0x2f20) < 0) {
        return -1;
    }
    if (*(int *)(DAT_80612be4 + 0x254) != 3) {
        return -1;
    }
    return 2;
}

// Show the label string for the current tool-group entry (its id lives in the tool-group table
// indexed by the first int of the bound group record, context+0x2f1c).
void aveditor_show_toolgroup_label(void)
{
    int entry = **(int **)(DAT_80612be4 + 0x2f1c);
    get_string_by_id((int)*(short *)(&DAT_80477bc8 + entry * 0xc4));
}

// Show the enabled/disabled label for the toggle row (context+0x3077, stride 0xc per sub-tab
// index at +0x2f78): flag 1 -> "enabled", 0 -> "disabled".
int aveditor_show_toggle_label(void)
{
    int result = 0;
    char flag = *(char *)(DAT_80612be4 + *(int *)(DAT_80612be4 + 0x2f78) * 0xc + 0x3077);
    if (flag == 1) {
        result = get_string_by_id(DAT_8047b224);
    } else if (flag == 0) {
        result = get_string_by_id(DAT_8047b160);
    }
    return result;
}

// Show the label for the currently-selected piece type (type index at *(context+0x2f18)+0x10,
// label id in the piece-type table, stride 0x140).
void aveditor_show_piecetype_label(void)
{
    int type = *(int *)(*(int *)(DAT_80612be4 + 0x2f18) + 0x10);
    get_string_by_id(*(int *)(&DAT_8047c150 + type * 0x140));
}

// Call the active sub-tab's label callback (table slot [tab*2 + 1] via base 0x80480c60). Returns
// the callback's result, or -1 when there is no active sub-tab.
int aveditor_dispatch_subtab_label(void)
{
    int tab = aveditor_active_subtab();
    if (tab == -1) {
        return -1;
    }
    int (**table)(void) = (int (**)(void))&PTR_subtab_callbacks_80480c5c;
    return table[tab * 2 + 1]();
}

// Call the active sub-tab's action callback (table slot [tab*2] via base 0x80480c5c). Returns the
// callback's result, or 0 when there is no active sub-tab.
int aveditor_dispatch_subtab_action(void)
{
    int tab = aveditor_active_subtab();
    if (tab == -1) {
        return 0;
    }
    int (**table)(void) = (int (**)(void))&PTR_subtab_callbacks_80480c5c;
    return table[tab * 2]();
}

// Push the editor HUD screen.
void aveditor_screen_push(void)
{
    hud_screen_push();
}

// Editor screen exit: run both teardown passes then the dirty-check exit path.
void aveditor_screen_exit(void)
{
    FUN_80338190();
    FUN_80338290();
    screen_exit_dirty_check();
}

// Rebuild the screen's item list if the element has unsaved changes.
void aveditor_rebuild_if_dirty(int element)
{
    if (hud_element_has_unsaved_changes(element) != 0) {
        hud_screen_rebuild_items();
    }
}

// Store the active editor context pointer.
void aveditor_set_active_context(int context)
{
    DAT_80612cb0 = context;
}
