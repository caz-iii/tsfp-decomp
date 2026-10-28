// Arcade-select prompt bar: the per-frame screen update/tick, the auto-advance
// (next) handler, the up/down and left/right arrow-pair renderers, small
// color/flag helpers, and the label-strip renderer that walks the prompt
// predicate table drawing separators + labels.
//
// Batch: batch_841  |  Address range: 0x802ef774 - 0x802f0288
// Module: arcade-select-promptbar (new file). Subsystem: graphics.
//
// arcsel_screen_update (0x802ef83c) is the big per-frame tick: it picks the
// active mode block (DAT_805cf280[DAT_806119ec]), advances transition timers,
// sets the viewport + depth range, drives the HUD camera region/bounds from the
// live cursor extents (DAT_80611748), runs a 10-element jitter loop, and either
// builds the select camera matrices or eases toward a popup screen's transform.
// arcsel_label_strip_render (0x802f0288) walks the 11-record prompt table
// (0x80477410 predicates / 0x80477408 string ids / 0x8047740c contexts), drawing
// the enabled entries' labels with divider lines and, at the ends, the L/R arrow
// pair via arcsel_arrows_lr_render.
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns. The Ghidra
// "extraout_f1" artifacts (a value the frame-guard prologue leaves in FP reg f1)
// are modeled as an explicit double seeded from the frame-begin return value.
// The prompt table is indexed by raw byte offset (record stride 0xc) as in the
// original. Signed-int->double CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE and
// the FP-pair locals are preserved.

#include "common.h"

// ---- Named cross-module helpers (resolve via generated_decls.h) ----
extern int    arcsel_nav_next_entry(void);
extern void   arcsel_camera_build_matrices(void);
extern int    arcsel_screen_close_and_index(void);
extern int    arcsel_prompt_scan_forward(void);
extern int    arcsel_prompt_scan_reverse(void);
extern int    session_get_state_flag(void);
extern void   hud_screen_navigate_to(int screen, unsigned int target, int dir);
extern void   hud_element_update(void);
extern int    hud_screen_destroy_current(void);
extern int    hud_screen_find_current_popup(void);
extern void   hud_viewport_set_camera_region(double l, double t, double r, double b);
extern void   hud_viewport_set_bounds(double l, double t, double r, double b);
extern void   viewport_set_rect(int *vp, int x, int y, int w, int h);
extern void   viewport_set_depth_params(double zn, double zf, double bias, int vp);
extern void   viewport_set_active(void *vp);
extern void   input_reset_pad(int pad);
extern unsigned int input_get_buttons_held(int pad);
extern void   toggle_latch_update(int on);
extern double math_sqrt(double v);
extern unsigned int rng_counter_tick(void);
extern unsigned int color_scale_lo(double factor, unsigned int color);
extern void   sprite_draw_arrow_up(double x0, double y0, double x1, double y1);
extern void   sprite_draw_arrow_down(double x0, double y0, double x1, double y1);
extern void   sprite_draw_arrow_left(double x0, double y0, double x1, double y1);
extern void   sprite_draw_arrow_right(double x0, double y0, double x1, double y1);
extern void   gx_draw_line_i(int x0, int y0, int x1, int y1, unsigned int color);
extern byte  *get_string_by_id(int id);
extern double text_get_char_width(byte *str);
extern void   text_draw_char(double x, double y, byte *str, unsigned int color);
extern void   text_set_font_table(int table);

// ---- Frame-guard prologue/epilogue (raw) ----
extern int    FUN_803826f0(void);   // begin frame -> current screen/mode ptr
extern void   FUN_8038273c(void);   // end frame
extern int    FUN_803826b4(void);   // begin label frame -> draw-enabled flag
extern void   FUN_80382700(void);   // end label frame
extern int    FUN_803825c0(double v);          // float->fixed alpha byte
extern void   FUN_802f3774(void);   // screen transition/confirm
extern void   FUN_802f21d8(void);   // element post-update
extern int   *__save_gpr_28(void);
extern void   __restore_gpr_28(void);

// ---- Raw data symbols (per-file externs) ----
extern int    DAT_805cf270;    // per-mode screen-id table (int[])
extern int    DAT_805cf280;    // per-mode/column flag+state block table (int[])
extern int    DAT_806119ec;    // active game-mode index
extern int   *DAT_80611748;    // live cursor/selection state block (int[])
extern int   *DAT_80611d74;    // arcade-select context (index at [1], pad at [0])
extern int    DAT_80611d54;    // secondary viewport owner
extern int    DAT_80612b9c;    // pad-reset gate
extern unsigned int DAT_80612bd0;  // auto-advance mask A
extern unsigned int DAT_80612bd4;  // auto-advance mask B
extern unsigned int DAT_805015c0;  // global mode-flags word
extern int    DAT_805015d8;    // global mode sub-state
extern int    DAT_80611934;    // active player count
extern int    DAT_806109c0;    // frame-rate denominator
extern int    DAT_806109c4;    // full viewport width
extern int    DAT_806109c8;    // full viewport height
extern int    DAT_80477408;    // prompt table: string-id column base
extern int    PTR_PTR_8047740c;                    // prompt table: context column base
extern int    PTR_arcsel_mode_flag4_clear_80477410;// prompt table: predicate column base
extern int    PTR_FUN_80477674;                    // sentinel popup vtable
extern float  DAT_80583228;    // sine/curve LUT (float[0x800])
extern int    DAT_803f2c90;    // per-index color table
extern int    DAT_8061a478;    // rng bias (2^52 packed)

extern float  FLOAT_8060f958;
extern float  FLOAT_80611674;  // label column width
extern float  FLOAT_806108e8;
extern float  FLOAT_806109c0;
extern float  FLOAT_8061a418;
extern float  FLOAT_8061a41c;
extern float  FLOAT_8061a420;
extern float  FLOAT_8061a444;
extern float  FLOAT_8061a448;
extern float  FLOAT_8061a450;
extern float  FLOAT_8061a454;
extern float  FLOAT_8061a458;
extern float  FLOAT_8061a45c;
extern float  FLOAT_8061a460;
extern float  FLOAT_8061a464;
extern float  FLOAT_8061a468;
extern float  FLOAT_8061a46c;
extern float  FLOAT_8061a470;
extern float  FLOAT_8061a474;
extern float  FLOAT_8061a480;
extern float  FLOAT_8061a484;
extern float  FLOAT_8061a488;
extern double DOUBLE_8061a430; // 2^52 bias for int->double
extern double DOUBLE_8061a478; // rng bias

// ============================================================================
// 0x802ef774 : arcsel_auto_advance_next
// "Next" auto-advance counterpart to arcsel_auto_advance_if_ready: when the
// active mode isn't masked and a next-step is available, navigate the HUD screen.
// ============================================================================
void arcsel_auto_advance_next(void)
{
    int step;
    if (((DAT_80612bd0 & (1u << DAT_806119ec)) == 0) &&
        ((DAT_80612bd4 & (1u << DAT_806119ec)) == 0) &&
        (step = arcsel_nav_next_entry(), step != 0)) {
        int target = arcsel_nav_next_entry();
        hud_screen_navigate_to((&DAT_805cf270)[DAT_806119ec], (unsigned int)target, -1);
    }
}

// ============================================================================
// 0x802ef7f4 : arcsel_column_hidden
// 1 unless the column has flag 0x1 set and flag 0x80 clear (i.e. hidden when not
// an active-but-unfinished column); negative index -> 0.
// ============================================================================
int arcsel_column_hidden(int column)
{
    if (column < 0) {
        return 0;
    }
    if (((*(unsigned int *)(&DAT_805cf280)[column] & 1) != 0) &&
        ((*(unsigned int *)(&DAT_805cf280)[column] & 0x80) == 0)) {
        return 0;
    }
    return 1;
}

// ============================================================================
// 0x802ef838 : arcsel_promptbar_noop
// Empty stub (an unused hook slot).
// ============================================================================
void arcsel_promptbar_noop(void)
{
}

// ============================================================================
// 0x802ef83c : arcsel_screen_update
// Per-frame arcade-select screen tick. Advances transition timers on the active
// mode block, configures the viewport + depth range, drives the HUD camera
// region/bounds from the live cursor extents, runs a 10-element background
// jitter loop, and builds the select camera matrices (or eases toward a popup
// screen's transform).
// ============================================================================
void arcsel_screen_update(void)
{
    float  f1;
    unsigned int flags;
    int    tmp;
    int    i;
    int    off;
    unsigned int *block;
    double dt;

    __save_gpr_28();
    DAT_806119ec = DAT_80611d74[1];
    block = (unsigned int *)(&DAT_805cf280)[DAT_806119ec];
    dt = (double)(FLOAT_806108e8 *
                  (FLOAT_8061a41c /
                   (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061a430)));

    if (((*block & 4) != 0) && ((*block & 1) != 0) && (FLOAT_8061a41c <= (float)block[2])) {
        toggle_latch_update(1);
    }
    if ((*block & 8) != 0) {
        block[3] = (unsigned int)(float)((double)(float)block[3] + dt);
        if ((*block & 0x40) == 0) {
            if ((FLOAT_8061a420 <= (float)block[3]) && ((*block & 1) != 0)) {
                FUN_802f3774();
                *block = *block | 8;
            }
            if (FLOAT_8061a450 <= (float)block[3]) {
                *block = *block & 0xfffffff7;
            }
        }
    }

    if (((DAT_805015c0 & 0x40) == 0) || (DAT_805015d8 != 0x13)) {
        viewport_set_rect((int *)(block + 0xd5), DAT_80611748[0], DAT_80611748[5],
                          DAT_80611748[1], DAT_80611748[6]);
    }
    else {
        viewport_set_rect((int *)(block + 0xd5), 0, 0, DAT_806109c4 - 1, DAT_806109c8 - 1);
    }
    if ((DAT_80611d74[1] != 0) && (DAT_80612b9c != 0)) {
        input_reset_pad(DAT_80611d74[1]);
    }
    viewport_set_depth_params((double)FLOAT_8061a454, (double)FLOAT_8061a458,
                              (double)FLOAT_8061a418, (int)(block + 0xd5));
    DAT_806119ec = DAT_80611d74[1];

    {
        double slide = (double)((float)((double)CONCAT44(0x43300000, DAT_80611748[3] ^ 0x80000000) - DOUBLE_8061a430) * FLOAT_8060f958);
        double left  = (double)(FLOAT_8061a444 *
                        (float)((double)(float)((double)CONCAT44(0x43300000, DAT_80611748[3] ^ 0x80000000) - DOUBLE_8061a430) - slide) +
                        (float)((double)CONCAT44(0x43300000, DAT_80611748[0] ^ 0x80000000) - DOUBLE_8061a430));
        double bottomA = (double)CONCAT44(0x43300000, DAT_80611748[6] ^ 0x80000000);
        double right = (double)(float)(slide + left);
        hud_viewport_set_camera_region(left,
            (double)(float)((double)CONCAT44(0x43300000, DAT_80611748[5] ^ 0x80000000) - DOUBLE_8061a430),
            right, (double)(float)(bottomA - DOUBLE_8061a430));
        double bottomB = (double)(CONCAT44(0x43300000, DAT_80611748[6]) ^ 0x80000000);
        hud_viewport_set_bounds(left,
            (double)(float)((double)CONCAT44(0x43300000, DAT_80611748[5] ^ 0x80000000) - DOUBLE_8061a430),
            right, (double)(float)(bottomB - DOUBLE_8061a430));
    }

    flags = *block;
    if ((flags & 1) == 0) {
        arcsel_camera_build_matrices();
        f1 = FLOAT_8061a41c;
        block[0x1a] = (unsigned int)FLOAT_8061a41c;
        block[0x1b] = (unsigned int)f1;
    }
    else {
        if (((flags & 4) == 0) && ((flags & 2) == 0) &&
            (arcsel_screen_close_and_index() != 0) &&
            ((DAT_80611934 == 7) || (DAT_80611934 == 5)) &&
            ((input_get_buttons_held(DAT_80611d74[0]) & 0x8100) != 0)) {
            unsigned int held = input_get_buttons_held(DAT_80611d74[0]);
            if (((held & 0x10) == 0) || ((*block & 2) == 0)) {
                FUN_802f3774();
            }
        }
        else {
            block[0x94] = (unsigned int)(float)((double)(float)block[0x94] + dt);
            block[2]    = (unsigned int)(float)((double)(float)block[2] + dt);
            if (session_get_state_flag() == 1) {
                double t = (double)((float)block[2] / FLOAT_8061a420);
                if ((double)FLOAT_8061a41c < t) {
                    t = (double)FLOAT_8061a41c;
                }
                t = math_sqrt(t);
                double h8 = (double)(CONCAT44(0x43300000, DAT_80611748[8]) ^ 0x80000000);
                viewport_set_rect((int *)(block + 0xd5), DAT_80611748[0], 0, DAT_80611748[1],
                    (int)((float)((double)CONCAT44(0x43300000, DAT_80611748[6] ^ 0x80000000) - DOUBLE_8061a430) -
                          (float)(h8 - DOUBLE_8061a430) * (float)((double)FLOAT_8061a45c * t)));
            }
            viewport_set_active(block + 0xd5);
            arcsel_camera_build_matrices();
        }
        viewport_set_active(block + 0xd5);
        hud_element_update();

        double hi = (double)FLOAT_8061a460;
        i = 0;
        off = 0;
        do {
            *(float *)((int)block + off + 0x18) =
                (float)(dt * (double)*(float *)((int)block + off + 0x40) +
                        (double)*(float *)((int)block + off + 0x18));
            if (hi <= (double)*(float *)((int)block + off + 0x18)) {
                unsigned int r = rng_counter_tick();
                *(float *)((int)block + off + 0x18) =
                    -((float)((double)CONCAT44(0x43300000, r) - DOUBLE_8061a478) / FLOAT_8061a464);
                r = rng_counter_tick();
                *(float *)((int)block + off + 0x40) =
                    FLOAT_8061a444 *
                    ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_8061a478) / FLOAT_8061a464) +
                    FLOAT_8061a444;
            }
            i = i + 1;
            off = off + 4;
        } while (i < 10);

        FUN_802f21d8();
        tmp = hud_screen_destroy_current();
        {
            int popup = hud_screen_find_current_popup();
            if (popup != 0) {
                tmp = popup;
            }
        }
        if (tmp == 0) {
            arcsel_camera_build_matrices();
            f1 = FLOAT_8061a41c;
            block[0x1a] = (unsigned int)FLOAT_8061a41c;
            block[0x1b] = (unsigned int)f1;
        }
        else {
            f1 = FLOAT_8061a468;
            if (*(void ***)(tmp + 4) != (void **)&PTR_FUN_80477674) {
                f1 = FLOAT_8061a46c *
                     (FLOAT_8061a41c + *(float *)(tmp + 0x5c) + *(float *)(tmp + 0x4c)) -
                     FLOAT_8061a444;
            }
            block[0x1b] = (unsigned int)(FLOAT_8061a41c -
                (FLOAT_8061a444 *
                 (&DAT_80583228)[(int)(FLOAT_8061a470 *
                        (FLOAT_8061a448 * *(float *)(tmp + 0x1b4) + FLOAT_8061a474) +
                        FLOAT_8061a444) & 0x7ff] + FLOAT_8061a444));
            block[0x1a] = (unsigned int)(f1 * (float)block[0x1b]);
        }
    }
    __restore_gpr_28();
}

// ============================================================================
// 0x802efde8 : arcsel_viewport_activate_secondary
// Reselect the active mode, reset the pad if the mode is active, and activate the
// secondary viewport owned by DAT_80611d54+0x224.
// ============================================================================
void arcsel_viewport_activate_secondary(void)
{
    DAT_806119ec = DAT_80611d74[1];
    if ((*(unsigned int *)(&DAT_805cf280)[DAT_806119ec] & 1) != 0) {
        input_reset_pad(DAT_80611748[0]);
    }
    viewport_set_active((void *)(DAT_80611d54 + 0x224));
}

// ============================================================================
// 0x802efe40 : arcsel_promptbar_write_color
// Compute a faded alpha byte from the frame-begin phase and write both a per-mode
// flag byte (*out) and a scaled color word at the frame-begin pointer.
// ============================================================================
void arcsel_promptbar_write_color(int a, int b, unsigned int *out)
{
    unsigned int screen;
    double phase;
    unsigned int alpha;

    (void)a; (void)b;
    screen = (unsigned int)FUN_803826f0();
    phase  = (double)FLOAT_8061a480;  /* value carried in f1 from the frame-begin */
    alpha = (unsigned int)FUN_803825c0((double)(float)(phase));
    *out = alpha & 0xff;
    alpha = color_scale_lo((double)(float)phase, (&DAT_803f2c90)[(int)((unsigned long long)screen >> 0x20) * 5]);
    *(unsigned int *)screen = alpha;
    FUN_8038273c();
}

// ============================================================================
// 0x802efec0 : arcsel_prompt_set_arrow_flags
// Write the arrow-enable flags at obj[+8]: bit 0x1 when `hasPrev`, bit 0x2 when
// `hasNext`.
// ============================================================================
void arcsel_prompt_set_arrow_flags(int obj, int hasPrev, int hasNext)
{
    *(unsigned int *)(obj + 8) = 0;
    if (hasPrev != 0) {
        *(unsigned int *)(obj + 8) = *(unsigned int *)(obj + 8) | 1;
    }
    if (hasNext == 0) {
        return;
    }
    *(unsigned int *)(obj + 8) = *(unsigned int *)(obj + 8) | 2;
}

// ============================================================================
// 0x802efef4 : arcsel_arrows_ud_render
// Draw the up/down arrow pair (with pulse fades from obj[0]/obj[1]) at the
// frame-begin x position.
// ============================================================================
void arcsel_arrows_ud_render(unsigned long long frameArg, double yTop, double yBot,
                             double halfW, double colorFactor)
{
    float *obj;
    double x;

    (void)frameArg;
    obj = (float *)FUN_803826f0();
    x = (double)FLOAT_8061a418;  /* frame-begin x carried in f1 */
    color_scale_lo(colorFactor, 0x2d4b697f);
    color_scale_lo(colorFactor, 0x1e2d4646);

    sprite_draw_arrow_down((double)(float)(x - halfW), yTop, (double)(float)(x + halfW),
                           (double)(float)(yTop + halfW));
    sprite_draw_arrow_up((double)(float)(x - halfW), yBot, (double)(float)(x + halfW),
                         (double)(float)(yBot + halfW));

    if ((double)FLOAT_8061a418 < (double)obj[1]) {
        color_scale_lo((double)(float)(colorFactor * (double)obj[1]), 0x69737f7f);
        sprite_draw_arrow_up((double)(float)(x - halfW), yBot, (double)(float)(x + halfW),
                             (double)(float)(yBot + halfW));
        obj[1] = obj[1] - FLOAT_8061a45c;
    }
    if ((double)FLOAT_8061a418 < (double)obj[0]) {
        color_scale_lo((double)(float)(colorFactor * (double)obj[0]), 0x69737f7f);
        sprite_draw_arrow_down((double)(float)(x - halfW), yTop, (double)(float)(x + halfW),
                               (double)(float)(yTop + halfW));
        obj[0] = obj[0] - FLOAT_8061a45c;
    }
    FUN_8038273c();
}

// ============================================================================
// 0x802f00b4 : arcsel_arrows_lr_render
// Draw the left/right arrow pair (with pulse fades) around the label strip.
// ============================================================================
void arcsel_arrows_lr_render(unsigned long long frameArg, double xRightGap, double y0,
                             double y1, double halfW, double colorFactor)
{
    float *obj;
    double x;

    (void)frameArg;
    obj = (float *)FUN_803826f0();
    x = (double)FLOAT_8061a418;  /* frame-begin x carried in f1 */
    color_scale_lo(colorFactor, 0x2d4b697f);
    color_scale_lo(colorFactor, 0x1e2d4646);

    sprite_draw_arrow_left((double)(float)(x - halfW), y0, x, y1);
    sprite_draw_arrow_right((double)(float)(x + xRightGap), y0,
                            (double)(float)(halfW + (double)(float)(x + xRightGap)), y1);

    if ((double)FLOAT_8061a418 < (double)obj[1]) {
        color_scale_lo((double)(float)(colorFactor * (double)obj[1]), 0x69737f7f);
        sprite_draw_arrow_right((double)(float)(x + xRightGap), y0,
                                (double)(float)(halfW + (double)(float)(x + xRightGap)), y1);
        obj[1] = obj[1] - FLOAT_8061a45c;
    }
    if ((double)FLOAT_8061a418 < (double)obj[0]) {
        color_scale_lo((double)(float)(colorFactor * (double)obj[0]), 0x69737f7f);
        sprite_draw_arrow_left((double)(float)(x - halfW), y0, x, y1);
        obj[0] = obj[0] - FLOAT_8061a45c;
    }
    FUN_8038273c();
}

// ============================================================================
// 0x802f0288 : arcsel_label_strip_render
// Render the prompt label strip: walk the 11-record prompt table drawing each
// enabled entry's localized label (highlighted for the current screen) separated
// by divider lines, then draw the L/R arrow pair when there is room to scroll.
// ============================================================================
void arcsel_label_strip_render(unsigned long long frameArg, double yBase, double colorFactor)
{
    int    drawEnabled;
    bool   first;
    unsigned int colorHi;
    unsigned int colorLo;
    int    fontHighlight;
    int    curScreen;
    int    startIndex;
    byte  *label;
    int    idx;
    int    off;
    unsigned int notCurrent;
    double x;
    double baseX;

    (void)frameArg;
    drawEnabled = FUN_803826b4();
    x = (double)FLOAT_8061a418;   /* frame-begin x carried in f1 */
    baseX = x;

    colorHi = (unsigned int)FUN_803825c0((double)(float)((double)FLOAT_8061a480 * colorFactor));
    colorHi = colorHi & 0xff | 0x7f7f7f00;
    colorLo = (unsigned int)FUN_803825c0((double)(float)((double)FLOAT_8061a484 * colorFactor));
    colorLo = colorLo & 0xff | 0x40404000;

    fontHighlight = (session_get_state_flag() > 1) ? 9 : 0xb;
    curScreen = hud_screen_destroy_current();
    first = true;
    text_set_font_table(3);
    unsigned int labelWidthBits = (unsigned int)FLOAT_80611674;

    if (arcsel_screen_close_and_index() == 0) {
        if ((curScreen != 0) && (*(int *)(curScreen + 0x200) != 0) &&
            (session_get_state_flag() == 1)) {
            label = *(byte **)(curScreen + 0x200);
            text_set_font_table(fontHighlight);
            text_get_char_width(label);
            if (drawEnabled != 0) {
                text_draw_char(baseX, (double)(float)(yBase - (double)FLOAT_80611674), label, colorHi);
            }
        }
        FUN_80382700();
        return;
    }

    {
        int    yTop = (int)yBase;
        double lw = (double)CONCAT44(0x43300000, labelWidthBits ^ 0x80000000);
        int    yBot = (int)(yBase - (double)(float)(lw - DOUBLE_8061a430));
        idx = 0;
        off = 0;
        do {
            int (*pred)(void) = (int (*)(void))*(int *)((int)&PTR_arcsel_mode_flag4_clear_80477410 + off);
            if ((pred != (int (*)(void))0) && ((*pred)() != 0)) {
                label = get_string_by_id(*(int *)((int)&DAT_80477408 + off));
                int nextIdx = idx + 1;
                int nextOff = nextIdx * 0xc;
                notCurrent = (unsigned int)(countLeadingZeros(*(int *)((int)&PTR_PTR_8047740c + off) - *(int *)(curScreen + 4)) >> 5);
                bool nextIsOther = true;
                for (; nextIdx < 0xb; nextIdx = nextIdx + 1) {
                    int (*np)(void) = (int (*)(void))*(int *)((int)&PTR_arcsel_mode_flag4_clear_80477410 + nextOff);
                    if ((np != (int (*)(void))0) && ((*np)() != 0)) {
                        nextIsOther = *(int *)((int)&PTR_PTR_8047740c + nextOff) != *(int *)(curScreen + 4);
                        break;
                    }
                    nextOff = nextOff + 0xc;
                }

                unsigned int labelColor;
                if (notCurrent == 0) {
                    text_set_font_table(3);
                    labelColor = colorLo;
                }
                else {
                    text_set_font_table(fontHighlight);
                    labelColor = colorHi;
                }
                double w = text_get_char_width(label);
                double y = (double)(float)(yBase - (double)FLOAT_80611674);
                if (first || (notCurrent != 0)) {
                    if (drawEnabled != 0) {
                        gx_draw_line_i((int)x, yTop, (int)x, yBot, colorLo);
                    }
                    x = (double)(float)(x + (double)FLOAT_8061a488);
                }
                if (drawEnabled != 0) {
                    text_draw_char(x, y, label, labelColor);
                }
                x = (double)(float)(x + (double)(float)(w + (double)FLOAT_8061a488));
                if ((notCurrent != 0) || nextIsOther) {
                    if (drawEnabled != 0) {
                        gx_draw_line_i((int)x, yTop, (int)x, yBot, colorLo);
                    }
                    x = (double)(float)(x + (double)FLOAT_8061a488);
                }
                first = false;
            }
            idx = idx + 1;
            off = off + 0xc;
        } while (idx < 0xb);

        text_set_font_table(3);
        if (drawEnabled != 0) {
            int rev = arcsel_prompt_scan_reverse();
            int fwd = arcsel_prompt_scan_forward();
            if (fwd != rev) {
                arcsel_arrows_lr_render(frameArg,
                    (double)(float)(baseX - (double)FLOAT_8061a488),
                    (double)(float)(yBase - (double)FLOAT_80611674),
                    (double)(float)((double)FLOAT_8061a488 + (double)(float)(x - baseX)),
                    yBase, (double)FLOAT_80611674);
            }
        }
    }
    FUN_80382700();
}
