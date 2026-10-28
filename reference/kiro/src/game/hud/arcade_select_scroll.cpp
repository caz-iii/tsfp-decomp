// Arcade-select menu scroll math + game-mode query predicates.
//
// Batch: batch_838  |  Address range: 0x802ee9a8 - 0x802eed30
// Module: arcade-select-scroll (new file). Subsystem: game (menu/HUD logic).
//
// This block contains the horizontal scroll-offset math for the arcade-select
// menu strip (how far the currently-selected column has slid, driven by the
// live cursor position at DAT_80611748+0x20 and the column count), the
// per-element target-position updater that eases each row toward its resting X,
// and a cluster of small game-mode / handicap query predicates used by the
// options and HUD-prompt screens (DAT_805cf270 mode table, the DAT_805cf280[]
// per-mode flag words indexed by DAT_806119ec, and the DAT_805015c0 global
// mode-flags word).
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns. Incoming PPC
// FP register pairs are modeled as explicit double return values; the
// CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE constant idiom is the standard
// PPC signed-int-to-double conversion and is preserved verbatim.

#include "common.h"

// ---- Named cross-module helpers (resolve via generated_decls.h) ----
extern int    session_get_state_flag(void);
extern double arcsel_menu_x_offset(int column);
extern unsigned int arcsel_menu_column_count(int column);
extern void   hud_element_apply_center_x(int element);
extern void   hud_flush_layout_if_active(int element);
extern int    stats_is_tracking_active(void);
extern unsigned int game_is_handicap_enabled(void);

// ---- Raw data symbols (per-file externs; typed to match the call sites) ----
extern float  FLOAT_8061a418;
extern float  FLOAT_8061a41c;
extern float  FLOAT_8061a428;
extern float  FLOAT_8061a438;
extern float  FLOAT_8061a43c;
extern float  FLOAT_8061a440;
extern float  FLOAT_8061a444;
extern float  FLOAT_80611a5c;
extern double DOUBLE_8061a430;

extern int    DAT_80611748;    // live cursor/selection state block
extern int    DAT_80611d74;    // arcade-select screen context (block at +4)
extern int    DAT_80612bc8;    // "menu populated" gate flag
extern int    DAT_805cf270;    // per-mode config value table (int[])
extern int    DAT_805cf280;    // per-mode flag-word table (int[])
extern int    DAT_806119ec;    // active game-mode index
extern unsigned int DAT_805015c0;  // global mode-flags word
extern char   DAT_805015bd;    // global mode sub-selector byte

// ============================================================================
// 0x802ee9a8 : arcsel_menu_scroll_offset
// Signed cursor position (DAT_80611748+0x20) scaled to a horizontal pixel
// offset. Uses a different scale factor depending on menu phase and whether the
// current column has any entries.
// ============================================================================
double arcsel_menu_scroll_offset(int column)
{
    int  state;
    unsigned int columnCount;
    double result;

    state = session_get_state_flag();
    if ((state < 2) || (DAT_80612bc8 == 0)) {
        result = (double)(FLOAT_8061a43c *
                 (float)((double)CONCAT44(0x43300000, *(unsigned int *)(DAT_80611748 + 0x20) ^ 0x80000000)
                        - DOUBLE_8061a430));
    }
    else {
        columnCount = arcsel_menu_column_count(column);
        if (columnCount == 0) {
            result = (double)(FLOAT_8061a438 *
                     (float)((double)CONCAT44(0x43300000, *(unsigned int *)(DAT_80611748 + 0x20) ^ 0x80000000)
                            - DOUBLE_8061a430));
        }
        else {
            result = (double)(FLOAT_8061a428 *
                     (float)((double)CONCAT44(0x43300000, *(unsigned int *)(DAT_80611748 + 0x20) ^ 0x80000000)
                            - DOUBLE_8061a430));
        }
    }
    return result;
}

// ============================================================================
// 0x802eea84 : arcsel_element_update_target_x
// Eases element[+0x60] (current X) toward a target derived from the base menu
// x-offset and the live scroll offset. element[+0x40] bit 0x40000000 disables
// the base-offset term; element[+0x64] holds the previous frame's delta.
// ============================================================================
void arcsel_element_update_target_x(int element)
{
    float  baseOffset;
    double baseD;
    double scrollD;

    baseOffset = FLOAT_8061a418;
    if ((*(unsigned int *)(element + 0x40) & 0x40000000) == 0) {
        baseD = arcsel_menu_x_offset(*(int *)(DAT_80611d74 + 4));
        baseOffset = (float)((double)FLOAT_80611a5c * baseD);
    }
    baseD = (double)baseOffset;
    scrollD = arcsel_menu_scroll_offset(*(int *)(DAT_80611d74 + 4));
    scrollD = -(double)(float)((double)FLOAT_80611a5c * scrollD - (double)FLOAT_8061a440);
    hud_flush_layout_if_active(element);
    *(float *)(element + 0x60) =
        (float)((double)FLOAT_8061a444 *
                    ((double)((float)(scrollD - baseD) - *(float *)(element + 0x64)) )
               + baseD);
}

// ============================================================================
// 0x802eeb2c : arcsel_element_tick_scroll
// Per-frame element updater: refresh target X, apply center-X layout, then nudge
// element[+0x5c] left during the two "outro" menu phases (states 3 and 4) for
// specific element[+0x38] sub-types.
// ============================================================================
void arcsel_element_tick_scroll(int element)
{
    int state;

    arcsel_element_update_target_x(element);
    hud_element_apply_center_x(element);
    state = session_get_state_flag();
    if (state == 3) {
        if (*(int *)(element + 0x38) == 2) {
            *(float *)(element + 0x5c) = *(float *)(element + 0x5c) - FLOAT_8061a41c;
        }
    }
    else {
        state = session_get_state_flag();
        if ((state == 4) &&
            ((*(int *)(element + 0x38) == 1) || (*(int *)(element + 0x38) == 3))) {
            *(float *)(element + 0x5c) = *(float *)(element + 0x5c) - FLOAT_8061a41c;
        }
    }
}

// ============================================================================
// 0x802eebbc : arcsel_menu_scroll_gap
// Signed gap between the static column x-offset and the live scroll offset,
// scaled by FLOAT_8061a438. Used to size the highlight/selection band.
// ============================================================================
double arcsel_menu_scroll_gap(int column)
{
    double xOffset;
    double scroll;

    xOffset = arcsel_menu_x_offset(column);
    scroll  = arcsel_menu_scroll_offset(column);
    return (double)(FLOAT_8061a438 * (float)(xOffset - scroll));
}

// ============================================================================
// 0x802eec10 : arcsel_mode_config_value
// Fetch the per-mode config value for the given index from the DAT_805cf270
// table.
// ============================================================================
int arcsel_mode_config_value(int index)
{
    return (&DAT_805cf270)[index];
}

// ============================================================================
// 0x802eec24 : arcsel_mode_flag4_set
// Return 1 if the active game-mode's flag word has bit 0x4 set, else 0.
// (countLeadingZeros(x & 4) >> 5 == 1 exactly when (x & 4) == 0 -> so this is
// the "flag NOT set" test; kept as the original bit-twiddle.)
// ============================================================================
unsigned int arcsel_mode_flag4_clear(void)
{
    unsigned int clz;
    clz = countLeadingZeros((&DAT_805cf280)[DAT_806119ec] & 4);
    return clz >> 5;
}

// ============================================================================
// 0x802eec4c : arcsel_mode_flag4_and_not_tracking
// 1 if the active mode has flag 0x4 AND stats tracking is inactive.
// ============================================================================
int arcsel_mode_flag4_and_not_tracking(void)
{
    int tracking;
    int result;

    result = 0;
    if ((((&DAT_805cf280)[DAT_806119ec] & 4) != 0) &&
        (tracking = stats_is_tracking_active(), tracking == 0)) {
        result = 1;
    }
    return result;
}

// ============================================================================
// 0x802eeca8 : arcsel_show_handicap_prompt
// Decide whether to show the handicap-related prompt: hidden when handicap is
// off, or when (mode flag 0x4 set AND tracking active), or when the global
// mode-flags word has bit 0x4000000 set but bit 0x2 clear.
// ============================================================================
int arcsel_show_handicap_prompt(void)
{
    unsigned int handicap;
    int tracking;
    int result;

    handicap = game_is_handicap_enabled();
    if ((handicap == 0) ||
        ((((&DAT_805cf280)[DAT_806119ec] & 4) != 0) &&
         (tracking = stats_is_tracking_active(), tracking != 0)) ||
        (((DAT_805015c0 & 0x4000000) != 0) && ((DAT_805015c0 & 2) == 0))) {
        result = 0;
    }
    else {
        result = 1;
    }
    return result;
}

// ============================================================================
// 0x802eed28 : arcsel_prompt_stub_zero
// Always-zero query stub (a disabled prompt slot).
// ============================================================================
int arcsel_prompt_stub_zero(void)
{
    return 0;
}

// ============================================================================
// 0x802eed30 : arcsel_show_teams_prompt
// Teams/handicap prompt visibility from the global mode-flags word and the
// sub-selector byte DAT_805015bd.
// ============================================================================
int arcsel_show_teams_prompt(void)
{
    unsigned int handicap;
    int result;

    handicap = game_is_handicap_enabled();
    if ((handicap == 0) &&
        (((DAT_805015c0 & 0x4000000) == 0) || (DAT_805015bd == '\n'))) {
        result = 1;
    }
    else if (((DAT_805015c0 & 0x8000000) == 0) &&
             ((DAT_805015c0 & 0x40) == 0) &&
             (DAT_805015bd != '\x03')) {
        result = 0;
    }
    else {
        result = 1;
    }
    return result;
}
