// Arcade-select prompt navigation (prev/next), column-availability predicates,
// the auto-advance handler, and the select-screen camera matrix setup.
//
// Batch: batch_840  |  Address range: 0x802ef14c - 0x802ef6f4
// Module: arcade-select-nav (new file). Subsystem: game (menu/HUD logic).
//
// nav_prev / nav_next resolve the current screen's index in the prompt table
// (0x80477410, 0xc-byte records), then step to the adjacent enabled slot: they
// invoke that slot's predicate and, if it declines, recurse to skip it. On a
// successful step they play the move sound (DAT_80611a58 sound id), record
// whether the step landed on the edge (comparing against the forward/reverse
// scan result) into the mode block's +0x264 field, and set the slide timer at
// +0x25c / +0x260. nav_prev_entry / nav_next_entry are thin thunks.
//
// The column-availability predicates (column_is_selectable / any_column_ready /
// column_flag1_ready / column_ready_full) test the per-column flag words in
// DAT_805cf280[0..3] against the global gates DAT_80612614 (< 0x65) and
// DAT_805015c0 bit 0x20000000.
//
// auto_advance_if_ready fires nav_next when the current mode is not masked out
// by DAT_80612bd0 / DAT_80612bd4 and a step is available, then navigates the HUD
// screen. select_camera_build_matrices builds the perspective + lookat matrices
// for the select screen from the live cursor extents.
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns. The prompt
// table is indexed as int[] with the original ((idx)*3) record stride. The
// signed-int->double CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE idiom and the
// FP-register-pair locals are preserved.

#include "common.h"

// ---- Named cross-module helpers (resolve via generated_decls.h) ----
extern int    arcsel_screen_find_index(int context);
extern int    arcsel_prompt_scan_forward(void);
extern int    arcsel_prompt_scan_reverse(void);
extern int    session_get_state_flag(void);
extern void   sound_init_play_params(int params);
extern void   sound_play_request(int *params);
extern unsigned int get_display_width(void);
extern unsigned int get_display_height(void);
extern int    mem_advance_secondary(int size);
extern void   mtx4_set_perspective(double fovAspect, double fovY, double zn, double zf, float *out);
extern void   mtx4_build_lookat_points(float *out, float *eye, float *at, float *up);
extern void   hud_screen_navigate_to(int screen, unsigned int target, int dir);

// Forward declaration: arcsel_any_column_ready (0x802ef550) calls this helper,
// which is defined later in this file.
unsigned int arcsel_column_flag1_ready(int column);

// ---- Frame-guard prologue/epilogue (raw) ----
extern int    FUN_803826f0(void);  // begin nav frame -> current screen context ptr
extern void   FUN_8038273c(void);  // end nav frame

// ---- Raw data symbols (per-file externs; typed to match the call sites) ----
extern int    DAT_805cf270;    // per-mode screen-id table (int[])
extern int    DAT_805cf280;    // per-mode/column flag-word table (int[])
extern int    DAT_806119ec;    // active game-mode index
extern int    DAT_80611748;    // live cursor/selection state block
extern int    DAT_80611a54;    // "move sound enabled" flag
extern int    DAT_80611a58;    // move-sound descriptor (id at +8)
extern int    DAT_80611b64;    // column-ready suppression flag
extern int    DAT_80612614;    // progress/loaded-count gate (< 0x65)
extern unsigned int DAT_80612bd0;  // per-mode auto-advance mask A
extern unsigned int DAT_80612bd4;  // per-mode auto-advance mask B
extern unsigned int DAT_805015c0;  // global mode-flags word

extern float  FLOAT_8061a41c;  // slide-timer reset value
extern float  FLOAT_8061a448;
extern float  FLOAT_8061a44c;
extern float  FLOAT_806104a0;  // near/far or fov params
extern float  FLOAT_806104a4;
extern float  FLOAT_806110d8;
extern double DOUBLE_8061a430; // 2^52 bias constant for int->double

extern int    DAT_8047748c;    // lookat: up vector
extern int    DAT_80477498;    // lookat: eye point
extern int    DAT_804774a4;    // lookat: target point

// Button-prompt predicate table: 11 records of 0xc bytes (int[] view, record*3).
extern int    PTR_arcsel_mode_flag4_clear_80477410;

// ============================================================================
// 0x802ef14c : arcsel_nav_prev
// Step to the previous enabled prompt slot (recurses to skip disabled slots).
// ============================================================================
int arcsel_nav_prev(void)
{
    int screen;
    int index;
    int scanResult;
    int params[18];
    int soundId;

    screen = FUN_803826f0();
    index = arcsel_screen_find_index(screen);
    if (0 < index) {
        int (*pred)(void) = (int (*)(void))(&PTR_arcsel_mode_flag4_clear_80477410)[(index - 1) * 3];
        if ((pred == (int (*)(void))0) || ((*pred)() == 0)) {
            arcsel_nav_prev();
        }
        else {
            if (DAT_80611a54 != 0) {
                soundId = *(int *)(DAT_80611a58 + 8);
                sound_init_play_params((int)params);
                params[0] = soundId;
                sound_play_request(params);
            }
            scanResult = arcsel_prompt_scan_forward();
            if (index - 1 == scanResult) {
                *(int *)((&DAT_805cf280)[DAT_806119ec] + 0x264) = 1;
            }
            else {
                *(int *)((&DAT_805cf280)[DAT_806119ec] + 0x264) = 0;
            }
            *(float *)((&DAT_805cf280)[DAT_806119ec] + 0x25c) = FLOAT_8061a41c;
        }
    }
    FUN_8038273c();
    return index;
}

// ============================================================================
// 0x802ef25c : arcsel_nav_next
// Step to the next enabled prompt slot (recurses to skip disabled slots).
// ============================================================================
int arcsel_nav_next(void)
{
    int screen;
    int index;
    int scanResult;
    int params[18];
    int soundId;

    screen = FUN_803826f0();
    index = arcsel_screen_find_index(screen);
    if ((-1 < index) && (index < 10)) {
        int (*pred)(void) = (int (*)(void))(&PTR_arcsel_mode_flag4_clear_80477410)[(index + 1) * 3];
        if ((pred == (int (*)(void))0) || ((*pred)() == 0)) {
            arcsel_nav_next();
        }
        else {
            if (DAT_80611a54 != 0) {
                soundId = *(int *)(DAT_80611a58 + 8);
                sound_init_play_params((int)params);
                params[0] = soundId;
                sound_play_request(params);
            }
            scanResult = arcsel_prompt_scan_reverse();
            if (index + 1 == scanResult) {
                *(int *)((&DAT_805cf280)[DAT_806119ec] + 0x264) = 2;
            }
            else {
                *(int *)((&DAT_805cf280)[DAT_806119ec] + 0x264) = 0;
            }
            *(float *)((&DAT_805cf280)[DAT_806119ec] + 0x260) = FLOAT_8061a41c;
        }
    }
    FUN_8038273c();
    return index;
}

// ============================================================================
// 0x802ef374 : arcsel_nav_prev_entry  (thunk -> arcsel_nav_prev)
// ============================================================================
int arcsel_nav_prev_entry(void)
{
    return arcsel_nav_prev();
}

// ============================================================================
// 0x802ef398 : arcsel_nav_next_entry  (thunk -> arcsel_nav_next)
// ============================================================================
int arcsel_nav_next_entry(void)
{
    return arcsel_nav_next();
}

// ============================================================================
// 0x802ef3bc : arcsel_camera_build_matrices
// Build the select-screen perspective + lookat matrices from the live cursor
// extents (DAT_80611748+0xc width, +0x20 height) and the display resolution.
// Allocates two 0x40-byte matrices into the mode block at +0x10 / +0x14.
// ============================================================================
void arcsel_camera_build_matrices(void)
{
    unsigned int w;
    unsigned int h;
    unsigned int cursorH;
    int modeBlock;
    int mtx;
    double aspectCursor;
    double aspectDisplay;
    double fovNear;
    double fovFar;

    modeBlock = (&DAT_805cf280)[DAT_806119ec];

    w = get_display_width();
    aspectDisplay = (double)((float)((double)CONCAT44(0x43300000, *(unsigned int *)(DAT_80611748 + 0xc) ^ 0x80000000) - DOUBLE_8061a430) /
                             (float)((double)CONCAT44(0x43300000, w ^ 0x80000000) - DOUBLE_8061a430));
    h = get_display_height();
    cursorH = *(unsigned int *)(DAT_80611748 + 0x20) ^ 0x80000000;
    fovNear = (double)FLOAT_806104a0;
    fovFar  = (double)FLOAT_806104a4;

    {
        double t = (double)(float)(((float)((double)CONCAT44(0x43300000, cursorH) - DOUBLE_8061a430) /
                                    (float)((double)CONCAT44(0x43300000, h ^ 0x80000000) - DOUBLE_8061a430)) / aspectDisplay);
        double u = (double)((float)((double)CONCAT44(0x43300000, *(unsigned int *)(DAT_80611748 + 0xc) ^ 0x80000000) - DOUBLE_8061a430) /
                            (float)((double)CONCAT44(0x43300000, cursorH) - DOUBLE_8061a430));

        *(int *)(modeBlock + 0x10) = mem_advance_secondary(0x40);
        *(int *)(modeBlock + 0x14) = mem_advance_secondary(0x40);

        mtx4_set_perspective((double)(float)(t * u),
                             (double)((FLOAT_8061a448 * FLOAT_806110d8) / FLOAT_8061a44c),
                             fovNear, fovFar,
                             *(float **)(modeBlock + 0x14));
    }
    mtx = modeBlock; /* keep modeBlock referenced for clarity */
    (void)mtx;
    mtx4_build_lookat_points(*(float **)(modeBlock + 0x10),
                             (float *)&DAT_80477498, (float *)&DAT_804774a4,
                             (float *)&DAT_8047748c);
}

// ============================================================================
// 0x802ef550 : arcsel_any_column_ready
// True if any active column [0 .. state) is selectable (flag bit 0x1 set and the
// global gates open).
// ============================================================================
int arcsel_any_column_ready(void)
{
    unsigned int ready;
    int stateCount;
    int i;

    i = 0;
    while (true) {
        stateCount = session_get_state_flag();
        if (stateCount <= i) {
            return 0;
        }
        ready = arcsel_column_flag1_ready(i);
        if (ready != 0) break;
        i = i + 1;
    }
    return 1;
}

// ============================================================================
// 0x802ef5a8 : arcsel_column_flag1_ready
// Column [0..3] selectable gate: returns flag bit 0x1 when the global gates are
// open, else 0.
// ============================================================================
unsigned int arcsel_column_flag1_ready(int column)
{
    if (((DAT_80612614 < 0x65) && ((DAT_805015c0 & 0x20000000) == 0)) &&
        (-1 < column) && (column < 4)) {
        return *(unsigned int *)(&DAT_805cf280)[column] & 1;
    }
    return 0;
}

// ============================================================================
// 0x802ef5fc : arcsel_column_is_selectable
// Column [0..3] is selectable when flag 0x1 set and flags 0x2 / 0x100 clear, with
// the global gates open.
// ============================================================================
int arcsel_column_is_selectable(int column)
{
    unsigned int flags;

    if (((DAT_80612614 < 0x65) && ((DAT_805015c0 & 0x20000000) == 0)) && (-1 < column)) {
        if (column < 4) {
            flags = *(unsigned int *)(&DAT_805cf280)[column];
            if ((flags & 1) == 0) {
                return 0;
            }
            if ((flags & 2) != 0) {
                return 0;
            }
            if ((flags & 0x100) != 0) {
                return 0;
            }
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// 0x802ef66c : arcsel_column_ready_full
// Column [0..3] "ready" test used by the auto-advance path: excluded when flag
// 0x200 set or DAT_80611b64 raised; when the global gates are open returns
// whether flag 0x1 is CLEAR (clz idiom), else 1.
// ============================================================================
unsigned int arcsel_column_ready_full(int column)
{
    unsigned int clz;

    if ((-1 < column) && (column < 4)) {
        if ((*(unsigned int *)(&DAT_805cf280)[column] & 0x200) != 0) {
            return 0;
        }
        if (DAT_80611b64 != 0) {
            return 0;
        }
        if ((DAT_80612614 < 0x65) && ((DAT_805015c0 & 0x20000000) == 0)) {
            clz = countLeadingZeros(*(unsigned int *)(&DAT_805cf280)[column] & 1);
            return clz >> 5;
        }
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x802ef6f4 : arcsel_auto_advance_if_ready
// When the active mode isn't masked out by the auto-advance masks and a prev
// step is available, navigate the HUD screen to the resolved target.
// ============================================================================
void arcsel_auto_advance_if_ready(void)
{
    int step;
    unsigned int target;

    if (((DAT_80612bd0 & (1u << DAT_806119ec)) == 0) &&
        ((DAT_80612bd4 & (1u << DAT_806119ec)) == 0) &&
        (step = arcsel_nav_prev_entry(), step != 0)) {
        target = (unsigned int)arcsel_nav_prev_entry();
        hud_screen_navigate_to((&DAT_805cf270)[DAT_806119ec], target, -1);
    }
}
