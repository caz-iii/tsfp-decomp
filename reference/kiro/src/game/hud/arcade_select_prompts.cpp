// Arcade-select button-prompt predicate table + prompt scanners, plus the
// screen-index lookup helpers.
//
// Batch: batch_839  |  Address range: 0x802eedc0 - 0x802ef0d8
// Module: arcade-select-prompts (new file). Subsystem: game (menu/HUD logic).
//
// The button-prompt system uses a table of 11 records at 0x80477410, each 0xc
// bytes: { int (*predicate)(void), u32 string_id, const char* label }. The
// forward/reverse scanners walk that table and return once a predicate reports
// the prompt should be shown. The predicate cluster here (continuation of
// arcade_select_scroll.cpp's mode-query helpers) tests the active game-mode flag
// word (DAT_805cf280[DAT_806119ec] & 4), whether stats tracking is active, the
// current session state, the global mode-flags word DAT_805015c0 / sub-selector
// byte DAT_805015bd, and the player count DAT_80611934.
//
// The screen-index helpers search a second 11-entry table (0x8047740c, stride
// 0xc) of screen-context pointers and return the matching index (or -1).
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns. The prompt
// table is treated as a raw byte base and indexed by byte offset exactly as the
// original code does, so the {ptr,id,label} record stride (0xc) is preserved.

#include "common.h"

// ---- Named cross-module helpers (resolve via generated_decls.h) ----
extern int stats_is_tracking_active(void);
extern int session_get_state_flag(void);
extern int hud_screen_destroy_current(void);

// ---- Frame-guard prologue/epilogue (raw) ----
extern void FUN_803826f0(void);   // begin prompt-scan frame
extern void FUN_8038273c(void);   // end prompt-scan frame

// ---- Raw data symbols (per-file externs; typed to match the call sites) ----
extern int  DAT_805cf280;      // per-mode flag-word table (int[])
extern int  DAT_806119ec;      // active game-mode index
extern unsigned int DAT_805015c0;  // global mode-flags word
extern char DAT_805015bd;      // global mode sub-selector byte
extern int  DAT_80611934;      // active player count

// Screen-context pointer table: 11 records of 0xc bytes, record[0] = context ptr.
extern int  PTR_PTR_8047740c;
// Button-prompt predicate table: 11 records of 0xc bytes,
//   record = { int (*predicate)(void); u32 string_id; const char* label; }
// Auto-named by Ghidra after the first record's predicate target.
extern int  PTR_arcsel_mode_flag4_clear_80477410;

// ============================================================================
// 0x802eedc0 : arcsel_prompt_show_league
// Show when: mode sub-selector byte is neither 0x01 nor 0x0a, stats tracking is
// off, none of the global-flag bits (0x40 / 0x8000000 / 0x4000000) are set, and
// there are more than 7 players active.
// ============================================================================
int arcsel_prompt_show_league(void)
{
    int tracking;
    if (((DAT_805015bd != '\x01') && (DAT_805015bd != '\n')) &&
        (tracking = stats_is_tracking_active(), tracking == 0) &&
        (((DAT_805015c0 & 0x40) == 0) && ((DAT_805015c0 & 0x8000000) == 0)) &&
        (((DAT_805015c0 & 0x4000000) == 0) && (7 < DAT_80611934))) {
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x802eee40 : arcsel_prompt_flag4_tracking_state1
// Show when active-mode flag 0x4 set, tracking active, and session state == 1.
// ============================================================================
int arcsel_prompt_flag4_tracking_state1(void)
{
    int cond;
    int result;
    if ((((&DAT_805cf280)[DAT_806119ec] & 4) == 0) ||
        (cond = stats_is_tracking_active(), cond == 0) ||
        (cond = session_get_state_flag(), cond != 1)) {
        result = 0;
    }
    else {
        result = 1;
    }
    return result;
}

// ============================================================================
// 0x802eeea0 : arcsel_prompt_flag4_tracking_state_ge2
// Show when active-mode flag 0x4 set, tracking active, and session state >= 2.
// ============================================================================
int arcsel_prompt_flag4_tracking_state_ge2(void)
{
    int cond;
    int result;
    if ((((&DAT_805cf280)[DAT_806119ec] & 4) == 0) ||
        (cond = stats_is_tracking_active(), cond == 0) ||
        (cond = session_get_state_flag(), cond < 2)) {
        result = 0;
    }
    else {
        result = 1;
    }
    return result;
}

// ============================================================================
// 0x802eef00 : arcsel_prompt_flag4_and_tracking
// Show when active-mode flag 0x4 set AND stats tracking active.
// ============================================================================
int arcsel_prompt_flag4_and_tracking(void)
{
    int tracking;
    int result;
    if ((((&DAT_805cf280)[DAT_806119ec] & 4) == 0) ||
        (tracking = stats_is_tracking_active(), tracking == 0)) {
        result = 0;
    }
    else {
        result = 1;
    }
    return result;
}

// ============================================================================
// 0x802eef54 : arcsel_prompt_flag4_and_tracking_alt
// Identical predicate to arcsel_prompt_flag4_and_tracking (a distinct table slot
// with its own string id/label).
// ============================================================================
int arcsel_prompt_flag4_and_tracking_alt(void)
{
    int tracking;
    int result;
    if ((((&DAT_805cf280)[DAT_806119ec] & 4) == 0) ||
        (tracking = stats_is_tracking_active(), tracking == 0)) {
        result = 0;
    }
    else {
        result = 1;
    }
    return result;
}

// ============================================================================
// 0x802eefa8 : arcsel_screen_find_index
// Linear search of the 11-entry screen-context table for a record whose context
// pointer equals `context`. Returns the 0-based index, or -1 if not found.
// ============================================================================
int arcsel_screen_find_index(int context)
{
    int index;
    int offset;
    int remaining;

    index = 0;
    offset = 0;
    remaining = 0xb;
    do {
        if (context == *(int *)((int)&PTR_PTR_8047740c + offset)) break;
        index = index + 1;
        offset = offset + 0xc;
        remaining = remaining + -1;
    } while (remaining != 0);
    if (index < 0xb) {
        return index;
    }
    return -1;
}

// ============================================================================
// 0x802eeff4 : arcsel_screen_index_from_ctx
// Resolve the screen index for the context pointer stored at ctx[+4].
// (Original tail-calls the finder; returns its index/-1.)
// ============================================================================
int arcsel_screen_index_from_ctx(int ctx)
{
    return arcsel_screen_find_index(*(int *)(ctx + 4));
}

// ============================================================================
// 0x802ef018 : arcsel_screen_close_and_index
// Destroy the current HUD screen; if one existed and its context resolves to a
// valid table index, return 1, else 0.
// ============================================================================
int arcsel_screen_close_and_index(void)
{
    int screen;
    int result;

    screen = hud_screen_destroy_current();
    result = 0;
    if ((screen != 0) && (arcsel_screen_index_from_ctx(screen) != -1)) {
        result = 1;
    }
    return result;
}

// ============================================================================
// 0x802ef060 : arcsel_prompt_scan_forward
// Walk the prompt-predicate table front-to-back; stop at the first record whose
// predicate returns nonzero (leftmost active prompt). Returns that slot index
// (r3 carries `index` through the void frame-end epilogue); callers in the nav
// handlers compare it against the stepped-to index.
// ============================================================================
int arcsel_prompt_scan_forward(void)
{
    int result;
    int index;
    int offset;

    FUN_803826f0();
    index = 0;
    offset = 0;
    do {
        int (**pred)(void) = (int (**)(void))((int)&PTR_arcsel_mode_flag4_clear_80477410 + offset);
        if ((*pred != (int (*)(void))0) && (result = (*pred)(), result != 0)) break;
        index = index + 1;
        offset = offset + 0xc;
    } while (index < 0xb);
    FUN_8038273c();
    return index;
}

// ============================================================================
// 0x802ef0d8 : arcsel_prompt_scan_reverse
// Walk the prompt-predicate table back-to-front (starting at record 10); stop at
// the first record whose predicate returns nonzero (rightmost active prompt).
// Returns that slot index (see arcsel_prompt_scan_forward).
// ============================================================================
int arcsel_prompt_scan_reverse(void)
{
    int result;
    int index;
    int offset;

    FUN_803826f0();
    index = 10;
    offset = 0x78;
    do {
        int (**pred)(void) = (int (**)(void))((int)&PTR_arcsel_mode_flag4_clear_80477410 + offset);
        if ((*pred != (int (*)(void))0) && (result = (*pred)(), result != 0)) break;
        index = index + -1;
        offset = offset + -0xc;
    } while (-1 < index);
    FUN_8038273c();
    return index;
}
