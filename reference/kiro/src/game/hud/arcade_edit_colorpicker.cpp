// Batch: batch_914 | Module: arcade map-editor color picker / palette (HSV wheel)
// Address range: 0x803256c8 - 0x80326090
// Date: 2026-09-12
//
// The arcade map-editor's colour picker. It manages the 0x40-entry working palette at DAT_805d6630
// (compared against the default palette DAT_80480d10), the HSV state at DAT_805d6730.. and its
// derived RGB DAT_805d6740, and draws an interactive HSV colour wheel with a cursor. The two large
// routines use the game's paired-single FP path and gx_draw_vertex_list, but they belong to the
// arcade editor UI, so the module stays subsystem:game (as with the rest of arcade_edit_*).
//
//   aveditor_palette_changed        0x803256c8  is the working palette different from the default.
//   aveditor_is_default_wset_entry  0x8032570c  is a palette entry the default weapon-set entry.
//   aveditor_init_wset_entry        0x803257a0  init a palette entry to the default weapon-set.
//   aveditor_palette_brighten       0x80325804  brighten a palette colour (mask/x2/+0xff).
//   aveditor_palette_index_of_rgb   0x8032582c  find the palette index whose RGB matches (or 0).
//   aveditor_colorpicker_back       0x80325874  colour-picker back/exit (pop, or exit + anim snap).
//   aveditor_draw_color_wheel       0x803258e4  draw the HSV colour wheel ring + cursor.
//   aveditor_color_wheel_tick       0x80325c5c  hit-test the wheel, update HSV, clamp, sync toggles.
//   aveditor_draw_color_swatch      0x80326048  draw the current-colour fill swatch.
//   aveditor_commit_picked_color    0x80326090  commit the picked colour into the palette slot.
//
// Palette entry layout (per DAT_805d6630 word / weapon-set entry at *param): byte0 = weapon set id,
// byte1 = kind (7 = default), +4/+8 = zero pair, +0xc/+0x10 = two fixed floats (FLOAT_8061a928/2c).
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs; named cross-module
// helpers are re-declared here with their real ABI. The trig/CONCAT44 int<->double idioms are kept
// verbatim from the decompiler (the compiler folds them); intrinsics come from common.h.

#include "common.h"
#include <cstring>   // memcpy for the u32->double bit reinterpretation

extern "C" {

extern int    DAT_80612be4;    // editor context base
extern int    DAT_806119ec;    // active screen slot index (stride 0x1e0)

// -- working palette + defaults --
extern unsigned int DAT_805d6630;   // working palette, 0x40 words
extern unsigned int DAT_80480d10;   // default palette, 0x40 words

// -- HSV picker state --
extern float  DAT_805d6730;    // HSV: hue
extern float  DAT_805d6734;    // HSV: saturation/radius
extern float  DAT_805d6738;    // HSV: value
extern float  DAT_805d6740;    // derived RGB r (then g at +4, b at +8)
extern float  DAT_805d6744;    // toggle target g
extern float  DAT_805d6748;    // toggle target b
extern int    DAT_805d674c;    // current palette slot index (low byte used)

// -- trig / geometry tables + float constants --
extern float  DAT_80583228;    // sin/cos lookup table (0x800 entries, wrap mask 0x7ff)
extern unsigned int DAT_806109c4;    // aspect ratio numerator (unsigned)
extern unsigned int DAT_806109c8;    // aspect ratio denominator (unsigned)

// three modal colour-picker screen vtables (compared by address in the back handler)
extern void *PTR_FUN_80480e50;
extern void *PTR_FUN_80480e60;
extern void *PTR_FUN_80480e70;
extern int    DAT_8051a820;    // screen root table base (stride 0x1e0)
extern int    DAT_8051a834;    // cursor x within screen table
extern int    DAT_8051a838;    // cursor y within screen table
extern double DOUBLE_8061a948; // unsigned->double bias constant
extern float  FLOAT_8061a928;  // fixed palette-entry float 0
extern float  FLOAT_8061a92c;  // fixed palette-entry float 1 / clamp min
extern float  FLOAT_8061a930;  // 0.5 (midpoint scale)
extern float  FLOAT_8061a934;  // angle->table scale
extern float  FLOAT_8061a938;  // angle span
extern float  FLOAT_8061a93c;  // angle divisor
extern float  FLOAT_8061a940;  // clamp max (1.0)
extern float  FLOAT_8061a944;  // ring step increment
extern float  FLOAT_8061a950;  // hit-test rect inflate
extern float  FLOAT_8061a954;  // radius scale

// -- raw helpers not yet individually named --
extern void  FUN_803826e8(void);   // begin draw context (wheel)
extern void  FUN_80382734(void);   // end draw context (wheel)
extern int   FUN_803826e4(void);   // fetch picker screen element (tick)
extern void  FUN_80382730(void);   // end picker tick context
extern int   FUN_803826f0(void);   // fetch picker screen element (commit)
extern void  FUN_8038273c(void);   // finalize screen (commit)
extern void  FUN_80348238(int soundId);   // play editor UI sound
extern void  leaf_31e8(unsigned int dst, unsigned int src, int count);   // copy first ring vertex

// -- named cross-module helpers (real ABI) --
extern int   aveditor_get_current_weapon_set(void);
extern void  hud_screen_pop_current(void);
extern void  arcade_edit_screen_exit(void);
extern void  avanim_begin_snap(void);
extern int   hud_element_has_unsaved_changes(int element);
extern void  hud_item_calc_screen_rect(int element, int itemList, float *outRect);
extern int   hud_screen_find_root(int screen);
extern int   hud_cursor_hit_test_rect(float *rect);
extern void  hud_viewport_set_cursor_pixel(double x, double y);
extern void  hud_item_set_toggle(void *value, int item);
extern unsigned int hsv_to_color_packed(float *hsv);
extern void  hsv_to_rgb(float *hsv, float *rgb);
extern void  rgb_to_hsv(float *rgb, float *hsv);
extern void  gx_draw_vertex_list(void *verts, int count, int mode);
extern void  gx_draw_fill_rect_f(unsigned int color, double a, double b, double c);
extern void  cursor_draw_at_size(double x, double y);
extern double math_sqrt(double x);
extern double math_atan2(double y, double x);

} // extern "C"

// -----------------------------------------------------------------------------

// Convert an unsigned 32-bit value to double using the game's standard magic-number idiom
// (CONCAT44(0x43300000, x ^ 0x80000000) reinterpreted as double, minus the 2^52+2^31 bias).
static double u32_to_double(unsigned int x)
{
    unsigned long long bits = ((unsigned long long)0x43300000 << 32) | (unsigned int)(x ^ 0x80000000u);
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d - DOUBLE_8061a948;
}

// The wheel-circularising ratio: smaller-extent / larger-extent, computed as a float, using the
// unsigned aspect terms DAT_806109c8 (hi) and DAT_806109c4 (lo).
static float aspect_ratio_hi_over_lo(void) { return (float)(u32_to_double(DAT_806109c8) / u32_to_double(DAT_806109c4)); }
static float aspect_ratio_lo_over_hi(void) { return (float)(u32_to_double(DAT_806109c4) / u32_to_double(DAT_806109c8)); }

// Return 1 if the working palette (DAT_805d6630, 0x40 words) differs from the default palette
// (DAT_80480d10), else 0.
int aveditor_palette_changed(void)
{
    int off = 0;
    int count = 0x40;
    do {
        if (*(int *)((int)&DAT_805d6630 + off) != *(int *)((int)&DAT_80480d10 + off)) {
            return 1;
        }
        off = off + 4;
        count = count + -1;
    } while (count != 0);
    return 0;
}

// Is a palette entry the "default weapon-set" entry: set id matches the current weapon set, the two
// int slots are zero, the two floats equal the fixed defaults, and kind byte == 7. The odd return
// (-1 - !flag) yields -1 when true and -2 when false, matching the original boolean encoding.
char aveditor_is_default_wset_entry(unsigned char *entry)
{
    bool match = false;
    unsigned int set = (unsigned int)aveditor_get_current_weapon_set();
    if (((*entry == set) && (*(int *)(entry + 4) == 0)) && (*(int *)(entry + 8) == 0) &&
        (((FLOAT_8061a928 == *(float *)(entry + 0xc) &&
           (FLOAT_8061a92c == *(float *)(entry + 0x10))) && (entry[1] == 7)))) {
        match = true;
    }
    return (char)(-1 - (int)(!match));
}

// Initialise a palette entry to the default weapon-set entry. When set==0 the set-id byte is 0,
// otherwise it is the current weapon set; the two int slots are zeroed, the two floats set to the
// fixed defaults, and the kind byte to 7.
void aveditor_init_wset_entry(unsigned char *entry, int set)
{
    unsigned char id;
    if (set == 0) {
        id = 0;
    } else {
        id = (unsigned char)aveditor_get_current_weapon_set();
    }
    *entry = id;
    float f0 = FLOAT_8061a928;
    *(int *)(entry + 4) = 0;
    float f1 = FLOAT_8061a92c;
    *(int *)(entry + 8) = 0;
    *(float *)(entry + 0xc) = f0;
    *(float *)(entry + 0x10) = f1;
    entry[1] = 7;
}

// Brighten a palette colour by index: keep the top bits of each channel (mask 0x7f7f7f00), double
// them and add 0xff so the alpha/low byte saturates.
int aveditor_palette_brighten(int index)
{
    return ((&DAT_805d6630)[index] & 0x7f7f7f00) * 2 + 0xff;
}

// Find the palette index (0..0x3f) whose RGB (ignoring the low byte) matches `color`; returns 0 if
// none matched (the original also returns the running index on a match).
char aveditor_palette_index_of_rgb(unsigned int color)
{
    char index = '\0';
    int off = 0;
    int count = 0x40;
    do {
        if ((color & 0xffffff00) == (*(unsigned int *)((int)&DAT_805d6630 + off) & 0xffffff00)) {
            return index;
        }
        index = index + '\x01';
        off = off + 4;
        count = count + -1;
    } while (count != 0);
    return '\0';
}

// Colour-picker back/exit: pop the current screen when there is no bound context (ctx+0x2f18==0) or
// the screen's vtable is one of the three modal picker vtables; otherwise run the editor screen
// exit and begin the anim snap transition.
void aveditor_colorpicker_back(int screen)
{
    void **vtable;
    if ((((*(int *)(DAT_80612be4 + 0x2f18) == 0) ||
          (vtable = *(void ***)(screen + 4), vtable == &PTR_FUN_80480e50)) ||
         (vtable == &PTR_FUN_80480e60)) || (vtable == &PTR_FUN_80480e70)) {
        hud_screen_pop_current();
    } else {
        arcade_edit_screen_exit();
        avanim_begin_snap();
    }
}

// Draw the HSV colour wheel: build a 0x31-segment vertex ring (colour from the hue sweep) via
// gx_draw_vertex_list, then draw the cursor at the current saturation/hue position. The FP context
// spill (f20..f31) is preserved verbatim; the CONCAT44/DOUBLE_8061a948 pairs are the standard
// int<->double conversion idiom.
void aveditor_draw_color_wheel(unsigned int a, unsigned int b, float *rect)
{
    // preserve the non-volatile FP context exactly as the original spills it
    volatile float fpSpill[24];
    (void)a; (void)b;

    FUN_803826e8();
    unsigned int *vp;
    unsigned int vertBuf[295 + 1];
    vp = vertBuf;

    double halfW = (double)((rect[2] - rect[0]) * FLOAT_8061a930);
    double halfH = (double)((rect[3] - rect[1]) * FLOAT_8061a930);
    float cxF = (rect[0] + rect[2]) * FLOAT_8061a930;
    double cx = (double)cxF;
    float cyF = (rect[1] + rect[3]) * FLOAT_8061a930;
    double cy = (double)cyF;

    // keep the wheel circular: scale the larger half-extent down to match the smaller one
    if (halfH < halfW) {
        halfW = (double)(float)(halfH * (double)aspect_ratio_hi_over_lo());
    } else {
        halfH = (double)(float)(halfW * (double)aspect_ratio_lo_over_hi());
    }

    int cxi = (int)cxF;
    double step = (double)FLOAT_8061a944;
    double angScale = (double)FLOAT_8061a934;
    double angSpan  = (double)FLOAT_8061a938;
    double angDiv   = (double)FLOAT_8061a93c;
    (void)step; (void)angScale; (void)angSpan; (void)angDiv;

    int i = 0;
    float hue = FLOAT_8061a92c;
    do {
        double h = (double)hue;
        unsigned int t = (unsigned int)(angScale *
                          (double)(float)((double)(float)(angSpan * h) / angDiv) +
                          (double)FLOAT_8061a930);
        double sinv = (double)(float)(&DAT_80583228)[t & 0x7ff];
        double cosv = (double)*(float *)((int)&DAT_80583228 + ((t + 0x200) * 4 & 0x1ffc));

        if (i == 0x30) {
            // last segment closes the ring back onto the first vertex
            leaf_31e8((unsigned int)vp, (unsigned int)vertBuf, 0x18);
        } else {
            float hsv[3];
            vp[0] = 0x7f7f7f80;
            vp[1] = cxi;
            hsv[1] = FLOAT_8061a940;
            hsv[2] = FLOAT_8061a940;
            vp[2] = (int)cyF;
            hsv[0] = hue;
            unsigned int col = hsv_to_color_packed(hsv);
            vp[3] = col;
            int px = (int)(cosv * halfW + cx);
            int py = (int)-(float)(sinv * halfH - cy);
            vp[4] = px;
            vp[5] = py;
            vp = vp + 6;
        }
        i = i + 1;
        hue = (float)(h + step);
    } while (i < 0x31);

    gx_draw_vertex_list(vertBuf, 0x62, 0);

    // cursor at the current (saturation, hue) polar position
    unsigned int t = (unsigned int)(FLOAT_8061a934 *
                     ((FLOAT_8061a938 * DAT_805d6730) / FLOAT_8061a93c) + FLOAT_8061a930);
    float sinv = (&DAT_80583228)[t & 0x7ff];
    float cosv = *(float *)((int)&DAT_80583228 + ((t + 0x200) * 4 & 0x1ffc));
    double curX = (double)(cosv * DAT_805d6734 * (float)halfW + cxF);
    double curY = (double)-(sinv * DAT_805d6734 * (float)halfH - cyF);
    cursor_draw_at_size(curX, curY);

    FUN_80382734();
    (void)fpSpill;
}

// Per-frame colour-wheel update: compute the wheel screen rect, hit-test the cursor against it, and
// when the screen is active and hit, convert the cursor offset to a new hue (atan2) and radius
// (clamped saturation), regenerate the RGB from HSV, and push the RGB into the item toggles. Then
// clamp each RGB channel and the value to [FLOAT_8061a92c, FLOAT_8061a940] and refresh the HSV.
void aveditor_color_wheel_tick(void)
{
    volatile float fpSpill[14];
    float rect[4];   // local_b8, local_b4, local_b0, local_ac

    int element = FUN_803826e4();
    hud_item_calc_screen_rect(element, *(int *)(element + 0x174), &rect[0]);

    double halfW = (double)((rect[2] - rect[0]) * FLOAT_8061a930);
    double halfH = (double)((rect[3] - rect[1]) * FLOAT_8061a930);
    double cx = (double)((rect[0] + rect[2]) * FLOAT_8061a930);
    double cy = (double)((rect[1] + rect[3]) * FLOAT_8061a930);

    if (halfH < halfW) {
        halfW = (double)(float)(halfH * (double)aspect_ratio_hi_over_lo());
    } else {
        halfH = (double)(float)(halfW * (double)aspect_ratio_lo_over_hi());
    }

    rect[0] = rect[0] - FLOAT_8061a950;
    rect[1] = rect[1] - FLOAT_8061a950;
    rect[2] = rect[2] + FLOAT_8061a950;
    rect[3] = rect[3] + FLOAT_8061a950;

    int root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if (((*(unsigned int *)(root + 0xc4) & 0x10) != 0) &&
        (hud_cursor_hit_test_rect(&rect[0]) != 0)) {
        double dy = (double)(float)((double)*(float *)(&DAT_8051a838 + DAT_806119ec * 0x1e0) - cy);
        double dx = (double)(float)((double)*(float *)(&DAT_8051a834 + DAT_806119ec * 0x1e0) - cx);
        double r = math_sqrt((double)(float)(dx * dx + (double)(float)(dy * dy)));
        r = (double)((float)((double)FLOAT_8061a954 * r) / (float)(halfW + halfH));
        if ((double)FLOAT_8061a940 < r) {
            unsigned int t = (unsigned int)(FLOAT_8061a934 *
                             ((FLOAT_8061a938 * DAT_805d6730) / FLOAT_8061a93c) + FLOAT_8061a930);
            hud_viewport_set_cursor_pixel(
                (double)(float)((double)*(float *)((int)&DAT_80583228 +
                                                   ((t + 0x200) * 4 & 0x1ffc)) * halfW + cx),
                -(double)(float)((double)(float)(&DAT_80583228)[t & 0x7ff] * halfH - cy));
            r = (double)FLOAT_8061a940;
        }
        double ang = (double)math_atan2(dy, -dx);
        DAT_805d6734 = (float)r;
        DAT_805d6730 = (FLOAT_8061a93c * (FLOAT_8061a938 + (float)ang)) / FLOAT_8061a938;
        hsv_to_rgb(&DAT_805d6730, &DAT_805d6740);

        int rowOff = 0;
        for (int row = 0; row < *(int *)(element + 0x178); row = row + 1) {
            int item = *(int *)(element + 0x174) + rowOff;
            int kind = *(int *)(item + 0x14);
            if (kind == 2) {
                hud_item_set_toggle(&DAT_805d6744, item);
            } else if (kind < 2) {
                if (0 < kind) {
                    hud_item_set_toggle(&DAT_805d6740, item);
                }
            } else if (kind < 4) {
                hud_item_set_toggle(&DAT_805d6748, item);
            }
            rowOff = rowOff + 0x10c;
        }
    }

    // clamp the 3 RGB channels to [FLOAT_8061a92c, FLOAT_8061a940]
    float hi = FLOAT_8061a940;
    float lo = FLOAT_8061a92c;
    int off = 0;
    int n = 3;
    do {
        float *ch = (float *)((int)&DAT_805d6740 + off);
        if (*ch <= hi) {
            if (*ch < lo) {
                *ch = lo;
            }
        } else {
            *ch = hi;
        }
        off = off + 4;
        n = n + -1;
    } while (n != 0);

    // clamp the value channel too
    if (DAT_805d6738 <= FLOAT_8061a940) {
        if (DAT_805d6738 < FLOAT_8061a92c) {
            DAT_805d6738 = FLOAT_8061a92c;
        }
    } else {
        DAT_805d6738 = FLOAT_8061a940;
    }

    float savedValue = DAT_805d6738;
    rgb_to_hsv(&DAT_805d6740, &DAT_805d6730);
    DAT_805d6738 = savedValue;
    FUN_80382730();
    (void)fpSpill;
}

// Draw the current-colour fill swatch: pack the HSV to a colour and fill the given rect.
void aveditor_draw_color_swatch(unsigned int a, unsigned int b, float *rect)
{
    (void)a; (void)b;
    unsigned int color = hsv_to_color_packed((float *)&DAT_805d6730);
    gx_draw_fill_rect_f(color, (double)rect[0], (double)rect[1], (double)rect[2]);
}

// Commit the picked colour into the current palette slot on exit: if the picker element is dirty,
// pack the HSV into DAT_805d6630[slot]; when the value actually changed (and the no-sound bit
// ctx+0x238 0x20 is clear) play the confirm sound and latch the bit. Then pop the screen.
void aveditor_commit_picked_color(void)
{
    int element = FUN_803826f0();
    if (hud_element_has_unsaved_changes(element) != 0) {
        int slot = (int)(unsigned char)DAT_805d674c;
        int prev = (&DAT_805d6630)[slot];
        unsigned int packed = hsv_to_color_packed((float *)&DAT_805d6730);
        (&DAT_805d6630)[slot] = packed;
        if ((prev != (int)(&DAT_805d6630)[slot]) &&
            ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
            FUN_80348238(0xb);
            *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
        }
    }
    hud_screen_pop_current();
    FUN_8038273c();
}
