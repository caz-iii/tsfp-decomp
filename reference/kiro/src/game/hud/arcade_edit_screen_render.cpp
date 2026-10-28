// Batch: batch_895 | Module: arcade map-editor top-level screen render + chrome (title bars,
//                             scanlines, info panel, cursor, viewport primitives)
// Address range: 0x8031bcc0 - 0x8031d240
// Date: 2026-09-12
//
// The outermost render entry for the arcade map editor and the screen "chrome" around the grid
// view. aveditor_render_screen is the per-frame top-level: it either draws the roster/palette
// sub-screen or the main editor frame (aveditor_draw_frame, batch_894), then overlays the info
// panel, paste meter, title bars, cursor, and the two split viewports. The rest are the individual
// chrome pieces: the gradient title bar, the fade scanline overlay, the 9-slice info panel, the
// editor cursor sprite, and a couple of GX viewport-space line/triangle primitives with a 1D clip
// helper.
//
//   aveditor_draw_title_bar        0x8031bcc0  draw a gradient title/footer bar with sprite fill.
//   aveditor_draw_title_bars       0x8031c2f0  draw the top and bottom title bars when in range.
//   aveditor_draw_scanline_overlay 0x8031c388  draw the fade-in scanline band overlay.
//   aveditor_render_screen         0x8031c4cc  top-level editor screen render entry (frame+chrome).
//   aveditor_draw_info_panel       0x8031cb04  draw the info panel as a 9-slice panel pair.
//   aveditor_cursor_slot_store     0x8031cba4  save/restore the per-slot cursor viewport positions.
//   aveditor_draw_cursor           0x8031cce0  draw the editor cursor sprite (+ pulse + debug label).
//   aveditor_draw_viewport_tri     0x8031cf10  emit a GX immediate-mode 2-tri strip in viewport space.
//   aveditor_draw_viewport_xline   0x8031d0b4  draw a clipped X-cross line pair in viewport space.
//   aveditor_clip_segment_y        0x8031d240  clip a 1D (y0,y1) segment to the screen height.
//
// NON-MATCHING notes:
//  * The compiler _savegpr_/_restgpr_ spill helpers (FUN_803826c8/d8/e8/f0, __save_gpr_28,
//    FUN_80382714/24/34, FUN_8038273c, __restore_gpr_28) are preserved as opaque calls; where one
//    folds an incoming register pair into an `undefined8` return, the value is recovered as a local.
//  * The int<->float 0x43300000 / DOUBLE_8061a6f0 magic-double idiom is written with CONCAT44 from
//    common.h and plain (float)(int) casts.
//  * aveditor_draw_viewport_tri writes vertices to the GX FIFO port at 0xCC008000 (write-only,
//    volatile); each store is a separate push of a float/uint into the command stream. It is modeled
//    as a volatile pointer so the 24 pushes are preserved and not coalesced.
//  * A few named helpers are declared in generated_decls.h with a return type/arity that does not
//    match the call sites here (debug_log_formatted()/void return used as a value;
//    cursor_draw_at_size(1 arg) vs the 2-arg (x,y) call; hud_screen_destroy_current()/void return
//    used as the current-screen pointer). They are reached through thin file-local function-pointer
//    wrappers (prefixed __ so gen_externs skips them) with the real signature.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs below. Named helpers
// (hud_style_*, gx_*, color_lerp, color_scale_lo, hud_draw_9slice_panel, hud_viewport_set_cursor_pixel,
// hud_screen_render_viewport, roster_*, aveditor_*, get_string_by_id, text_draw_char_shadowed)
// resolve via the shared generated headers.

#include "common.h"
#include "hud.h"

extern "C" {

// -- editor context + layout tables --
extern int   DAT_80612be4;     // editor context base
extern int   DAT_80612bec;     // active cursor viewport slot id
extern float FLOAT_80612bf0;   // scanline fade phase (0..1)
extern int   DAT_80612bf4;     // focused screen-root handle
extern int*  DAT_80610a20;     // sprite material record table base (0x58 stride)
extern int   DAT_806119ec;     // active grid layout index
extern int   DAT_8051a814;     // grid layout table (title metrics block, stride 0x1e0)
extern int   DAT_8051a820;     // grid layout table (screen-root handle)
extern int   DAT_8051a824;     // grid layout table (cursor-enable flag)
extern float DAT_8051a82c;     // grid layout table (cursor x)
extern float DAT_8051a830;     // grid layout table (cursor y)
extern float DAT_8051a834;     // grid layout table (viewport x)
extern float DAT_8051a838;     // grid layout table (viewport y)
extern float DAT_8051a8f8;     // grid layout table (top bar limit)
extern float DAT_8051a8fc;     // grid layout table (bottom bar limit)
extern int   DAT_806109c4;     // view width (also raw int)
extern int   DAT_806109c8;     // view height (also raw int)
extern int*  DAT_80611748;     // cursor origin block (x at [0], y at [5])
extern int   DAT_805d6100;     // saved cursor slot table (valid flag, 0x10 stride)
extern int   DAT_805d6104;     // saved cursor slot table (slot id)
extern int   DAT_805d6108;     // saved cursor slot table (x)
extern int   DAT_805d610c;     // saved cursor slot table (y)
extern signed char DAT_80477c80;   // per-layout capability table (0x31 stride)
extern short DAT_80477bc8;     // category table (name string id, 0xc4 stride)
extern int   DAT_8047fb58;     // mode name string-id table (4 stride)
extern unsigned char DAT_8047c14c; // per-layout budget-cost byte (0x140 stride)
extern volatile float DAT_cc008000;  // GX FIFO command port (write-only)

// -- format strings --
extern char s__s__s_80611194[];    // "%s %s"
extern char s__s___s__8061119c[];  // "%s (%s)"
extern char DAT_806111a4[];        // paste-info format

// -- layout / gradient constants --
extern double DOUBLE_8061a6f0; // int->float conversion bias
extern float  FLOAT_8061a6e8;  // 1.0
extern float  FLOAT_8061a710;  // 0.0
extern float  FLOAT_8061a714;  // top bar y
extern float  FLOAT_8061a71c;  // color scale factor
extern float  FLOAT_8061a7b0;  // row height
extern float  FLOAT_8061a7c4;  // label x offset
extern float  FLOAT_8061a7ec;  // strip x base
extern float  FLOAT_8061a814;  // bar y a
extern float  FLOAT_8061a818;  // bar y b (expanded)
extern float  FLOAT_8061a81c;  // top bar threshold
extern float  FLOAT_8061a820;  // bottom bar threshold
extern float  FLOAT_8061a824;  // scanline alpha scale
extern float  FLOAT_8061a828;  // scanline y a
extern float  FLOAT_8061a82c;  // info panel width scale
extern float  FLOAT_80611a64;  // text anchor x scale
extern float  FLOAT_80611a68;  // icon anchor scale
extern float  FLOAT_80611a6c;  // layout x bias
extern float  FLOAT_80611a70;  // layout y bias

// -- raw FUN_ helpers not yet individually named --
extern void FUN_8033f224(void);                 // begin text render pass (returns FP arg residue)
extern void FUN_80340f48(int screen, int r4, unsigned int a, int b, int c, int d, unsigned int e, unsigned int f); // roster sub-screen render
extern void FUN_803462c0(int stage);            // GX render stage select
extern void FUN_80345cf4(int textPtr, int a, int type, unsigned int flag); // draw info text block
extern void FUN_80345cd8(void);                 // clear info text block
extern int  FUN_8032ae60(void);                 // custom-name present test
extern unsigned int FUN_803825c0(double v);     // round-to-int / quantize

// compiler _savegpr_/_restgpr_ spill helpers (opaque)
extern unsigned long long FUN_803826c8(void);   // folds title-bar arg pair
extern unsigned long long FUN_803826d8(void);   // folds screen-render arg pair
extern void FUN_803826e8(void);
extern unsigned long long FUN_803826f0(void);   // folds cursor / rect out-pointer pair
extern unsigned long long __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern void FUN_80382714(void);
extern void FUN_80382724(void);
extern void FUN_80382734(void);
extern void FUN_8038273c(void);
} // extern "C"

// Wrappers for shared helpers whose generated prototype doesn't match the call site here.
static inline int __debug_log_formatted(char* fmt, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, unsigned int a9, unsigned int a10, unsigned int a11, unsigned int a12,
    unsigned int a13, unsigned int a14, unsigned int a15, unsigned int a16)
{
    typedef int (*fn_t)(char*, unsigned long long, unsigned long long, unsigned long long,
        unsigned long long, unsigned long long, unsigned long long, unsigned long long,
        unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int,
        unsigned int, unsigned int);
    return ((fn_t)(void*)&debug_log_formatted)(fmt, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
}
static inline void __cursor_draw_at(double x, double y)
{
    typedef void (*fn_t)(double, double);
    ((fn_t)(void*)&cursor_draw_at_size)(x, y);
}
static inline int __screen_current_record(void)
{
    typedef int (*fn_t)(void);
    return ((fn_t)(void*)&hud_screen_destroy_current)();
}

// -----------------------------------------------------------------------------
// Draw a gradient title / footer bar. side selects the layout metrics row; kind selects the color
// scheme (from a spacing record). When param_4==0 the bar is a solid sprite quad (optionally with a
// "collapsed" strip below when out of range); otherwise it is a vertical color gradient.
void aveditor_draw_title_bar(unsigned int side, unsigned int kind, int styleId, int gradient)
{
    unsigned long long spill = FUN_803826c8();
    int sideIdx = (int)(spill >> 0x20);

    int font = hud_style_find_font_by_id((int)spill);
    int spacing = hud_style_find_spacing_by_id(styleId);
    int tex = sprite_get_texture_id(0);
    int metricsOff = sideIdx * 4 + DAT_806119ec * 0x1e0;
    int rec = *DAT_80610a20 + tex * 0x58;
    double scale = (double)*(float*)(&DAT_8051a814 + metricsOff + 0xec);
    double h = (double)*(float*)(&DAT_8051a814 + metricsOff + 0xe4);
    unsigned int u1 = (unsigned int)*(unsigned short*)(rec + 0x4e) << 4;
    unsigned int v1 = (unsigned int)*(unsigned short*)(rec + 0x52) << 4;
    unsigned int c0 = color_scale_lo(scale, *(unsigned int*)(spacing + 0x50));
    unsigned int c1 = color_scale_lo(scale, *(unsigned int*)(spacing + 0x54));
    unsigned int c2 = color_scale_lo(scale, *(unsigned int*)(spacing + 0x58));
    unsigned int c3 = color_scale_lo(scale, *(unsigned int*)(spacing + 0x5c));
    color_scale_lo(scale, *(unsigned int*)(spacing + 0x48));
    int screen = __screen_current_record();
    gx_set_active_material(tex);
    gx_set_blend_mode(0);
    gx_set_sub_material_id(1);

    double x0, y0;
    if (sideIdx == 0) {
        y0 = h * (double)FLOAT_80611a68 + (double)FLOAT_80611a70;
        x0 = (double)FLOAT_8061a710;
    } else {
        y0 = (double)((float)((double)(int)(DAT_806109c4 ^ 0x80000000) - DOUBLE_8061a6f0));
        x0 = (double)(float)(h * (double)FLOAT_80611a68 + (double)FLOAT_80611a70);
    }
    double bx = x0;
    double by = (double)(float)y0;
    gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
    double top = (double)*(float*)(screen + 0xa8);
    double bot = (double)(float)(top + (double)*(float*)(screen + 0xb0));
    double span = (double)FLOAT_8061a6e8;
    double gh0 = (double)(*(float*)(font + 0x10) * FLOAT_80611a64);
    double gh1 = (double)(*(float*)(font + 0x18) * FLOAT_80611a64);
    if (span < (double)(float)(bot - top)) span = (double)(float)(bot - top);
    bool inRange = (double)(FLOAT_8061a814 * FLOAT_80611a64 + FLOAT_80611a6c) <= bot;
    bool collapsed = (gradient == 0) && inRange && ((*(unsigned int*)(screen + 0x214) & 1) != 0);
    double barBot = bot;
    if (collapsed) barBot = (double)(FLOAT_8061a818 * FLOAT_80611a64 + FLOAT_80611a6c);

    if (gradient == 0) {
        gx_draw_sprite_quad_z((unsigned long long)(intptr_t)&bx, top, by, barBot, (double)FLOAT_8061a6e8, 0, 0, u1, v1);
        gx_draw_sprite_quad_z((unsigned long long)(intptr_t)&bx, top, by, barBot, (double)FLOAT_8061a6e8, 0, 0, u1, v1);
        if (!inRange) {
            unsigned int fillC = color_scale_lo(scale, 0x1818217f);
            color_scale_lo(scale, 0x7f7f7f1e);
            color_scale_lo(scale, 0x7f7f7f1e);
            color_scale_lo(scale, 0x7f7f7f1e);
            color_scale_lo(scale, 0x7f7f7f1e);
            double r0 = (double)(float)((double)FLOAT_8061a6e8 + bot);
            double r1 = (double)(float)((double)FLOAT_8061a7b0 + bot);
            int tex2 = sprite_get_texture_id(1);
            int rec2 = *DAT_80610a20 + tex2 * 0x58;
            unsigned short u2 = *(unsigned short*)(rec2 + 0x4e);
            unsigned short v2 = *(unsigned short*)(rec2 + 0x52);
            gx_fill_rect_clipped((int)bx, (int)r0, (int)by, (int)r1, fillC);
            gx_set_sub_material_id(2);
            gx_set_active_material(tex2);
            gx_draw_sprite_quad_z((unsigned long long)(intptr_t)&bx, r0, by, r1, (double)FLOAT_8061a710, 0, 0,
                                  (unsigned int)u2 << 4, (unsigned int)v2 << 4);
        }
    } else {
        double f0 = (double)(float)(gh0 / span);
        double f1 = (double)(FLOAT_8061a6e8 - (float)(gh1 / span));
        color_lerp((double)FLOAT_8061a710, c2, c0);
        color_lerp((double)FLOAT_8061a710, c3, c1);
        unsigned int lc = color_lerp(f0, c2, c0);
        color_scale_lo((double)FLOAT_8061a71c, lc);
        lc = color_lerp(f0, c3, c1);
        color_scale_lo((double)FLOAT_8061a71c, lc);
        gx_draw_sprite_quad_z((unsigned long long)(intptr_t)&bx, top, by, (double)(float)(top + gh0), (double)FLOAT_8061a6e8, 0, 0, u1, v1);
        lc = color_lerp(f1, c2, c0);
        color_scale_lo((double)FLOAT_8061a71c, lc);
        lc = color_lerp(f1, c3, c1);
        color_scale_lo((double)FLOAT_8061a71c, lc);
        color_lerp((double)FLOAT_8061a6e8, c2, c0);
        color_lerp((double)FLOAT_8061a6e8, c3, c1);
        gx_draw_sprite_quad_z((unsigned long long)(intptr_t)&bx, (double)(float)(bot - gh1), by, bot, (double)FLOAT_8061a6e8, 0, 0, u1, v1);
    }
    FUN_80382714();
}

// -----------------------------------------------------------------------------
// Draw the top and bottom title bars, each only when its layout metric is within range.
void aveditor_draw_title_bars(int gradient)
{
    if (FLOAT_8061a81c < *(float*)(&DAT_8051a8f8 + DAT_806119ec * 0x1e0)) aveditor_draw_title_bar(0, 1, 0, gradient);
    if (*(float*)(&DAT_8051a8fc + DAT_806119ec * 0x1e0) < FLOAT_8061a820) aveditor_draw_title_bar(1, 2, 0, gradient);
}

// -----------------------------------------------------------------------------
// Draw the fade-in scanline band overlay: two horizontal bars (with a bright center line) plus the
// region between them, alpha scaled by the fade phase FLOAT_80612bf0.
void aveditor_draw_scanline_overlay(void)
{
    FUN_803826e8();
    if (FLOAT_8061a710 < FLOAT_80612bf0) {
        unsigned int a = FUN_803825c0((double)(FLOAT_8061a824 * FLOAT_80612bf0)) & 0xff;
        int y0 = (int)(FLOAT_8061a828 * FLOAT_80611a64 + FLOAT_80611a6c);
        unsigned int y1 = (unsigned int)(FLOAT_8061a814 * FLOAT_80611a64 + FLOAT_80611a6c);
        gx_set_sub_material_id(1);
        gx_set_blend_mode(0);
        gx_fill_rect_clipped(0, y0, DAT_806109c4, y0 + 6, a);
        gx_fill_rect_clipped(0, y0 + 1, DAT_806109c4, y0 + 5, a | 0x20202000);
        gx_draw_line_counted(0, y0 + 2, DAT_806109c4, y0 + 2, a | 0x7f7f7f00);
        gx_fill_rect_clipped(0, y1, DAT_806109c4, y1 + 6, a);
        gx_fill_rect_clipped(0, y1 + 1, DAT_806109c4, y1 + 5, a | 0x20202000);
        gx_draw_line_counted(0, y1 + 2, DAT_806109c4, y1 + 2, a | 0x7f7f7f00);
        gx_fill_rect_clipped(0, y0 + 6, DAT_806109c4, y1, a);
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// Draw the info panel: two overlaid 9-slice panels (a base fill and a tinted border). x0/y0 arrive
// as the r3/r4 pair folded by the spill; x1/y1 are the far corner.
void aveditor_draw_info_panel(int u1, int u2, unsigned int x1, unsigned int y1)
{
    (void)u1; (void)u2;
    unsigned long long spill = __save_gpr_28();
    int x0 = (int)(spill >> 0x20);
    int y0 = (int)spill;
    int b = (int)(FLOAT_8061a714 * FLOAT_80611a64);
    hud_draw_9slice_panel((unsigned int)x0, (unsigned int)y0, x1, y1, b, b, 0, 0x7f);
    hud_draw_9slice_panel((unsigned int)x0, (unsigned int)y0, x1, y1, b, b, 1, 0x695a9156);
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// Save / restore the per-slot cursor viewport positions. When (a==-1 && b==-1) it initializes all 8
// slots from the current layout. When a in 0..7, it stores the current cursor into slot a. When b in
// 0..7 and valid, it recalls slot b (moving the viewport cursor) and makes it active.
void aveditor_cursor_slot_store(int slotSave, int slotRecall)
{
    if ((slotSave == -1) && (slotRecall == -1)) {
        int lo = DAT_806119ec * 0x1e0;
        for (int i = 0, off = 0; i < 8; i = i + 1, off = off + 0x10) {
            int x = *(int*)(&DAT_8051a834 + lo);
            int y = *(int*)(&DAT_8051a838 + lo);
            *(int*)((int)&DAT_805d6104 + off) = 0x10;
            *(int*)((int)&DAT_805d6108 + off) = x;
            *(int*)((int)&DAT_805d610c + off) = y;
            *(int*)((int)&DAT_805d6100 + off) = 0;
        }
        DAT_80612bec = 0x10;
    }
    if ((-1 < slotSave) && (slotSave < 8)) {
        int lo = DAT_806119ec * 0x1e0;
        (&DAT_805d6104)[slotSave * 4] = DAT_80612bec;
        (&DAT_805d6108)[slotSave * 4] = *(int*)(&DAT_8051a834 + lo);
        (&DAT_805d610c)[slotSave * 4] = *(int*)(&DAT_8051a838 + lo);
        (&DAT_805d6100)[slotSave * 4] = 1;
    }
    if (((-1 < slotRecall) && (slotRecall < 8)) && ((&DAT_805d6100)[slotRecall * 4] != 0)) {
        if (slotRecall != 0) {
            hud_viewport_set_cursor_pixel((float)(&DAT_805d6108)[slotRecall * 4],
                                          (float)(&DAT_805d610c)[slotRecall * 4]);
        }
        DAT_80612bec = (&DAT_805d6104)[slotRecall * 4];
    }
}

// -----------------------------------------------------------------------------
// Draw the editor cursor sprite at the layout cursor position, offset by the active cursor-slot
// shape (DAT_80612bec). Slot 0xf draws a second pulse pass; in debug mode a label from string 0xa3d
// is drawn beside it.
void aveditor_draw_cursor(void)
{
    FUN_803826f0();
    int ox = (int)(FLOAT_80611a68 * *(float*)(&DAT_8051a82c + DAT_806119ec * 0x1e0) + FLOAT_80611a70);
    int oy = (int)(FLOAT_80611a64 * *(float*)(&DAT_8051a830 + DAT_806119ec * 0x1e0) + FLOAT_80611a6c);
    switch (DAT_80612bec) {
    case 0: case 2:  ox = ox - 8; oy = oy - 6; break;
    case 1:          oy = oy - 3; ox = ox - 6; break;
    case 3: case 4: case 5: case 6: oy = oy - 4; ox = ox - 8; break;
    case 7:          ox = ox - 5; oy = oy - 3; break;
    case 8:          oy = oy - 3; ox = ox - 6; break;
    case 9:          break;
    case 0x10:       ox = ox - 2; break;
    }
    double cx = (double)(float)((double)(int)((*DAT_80611748 + ox) ^ 0x80000000) - DOUBLE_8061a6f0);
    double cy = (double)(float)((double)(int)((DAT_80611748[5] + oy) ^ 0x80000000) - DOUBLE_8061a6f0);
    __cursor_draw_at(cx, cy);
    if (DAT_80612bec == 0xf) {
        aveditor_tick_pulse();
        cx = (double)(float)((double)(int)((*DAT_80611748 + ox) ^ 0x80000000) - DOUBLE_8061a6f0);
        cy = (double)(float)((double)(int)((DAT_80611748[5] + oy) ^ 0x80000000) - DOUBLE_8061a6f0);
        __cursor_draw_at(cx, cy);
    }
    int* org = DAT_80611748;
    if (*(char*)(DAT_80612be4 + 0x11854) == '\x01') {
        int label = get_string_by_id(0xa3d);
        cx = (double)(FLOAT_8061a7c4 * FLOAT_80611a68 +
             (float)((double)(int)((*org + ox) ^ 0x80000000) - DOUBLE_8061a6f0));
        cy = (double)(float)((double)(int)((org[5] + oy) ^ 0x80000000) - DOUBLE_8061a6f0);
        text_draw_char_shadowed(cx, cy, (unsigned char*)(intptr_t)label, 0x64646480, 0x80);
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Emit a GX immediate-mode two-triangle strip (a screen-space quad) in viewport space. rect holds
// [?, y0, ?, y1]; the quad is drawn only when both y edges are on-screen. color arrives in param_3.
// Vertices are pushed to the GX FIFO port at 0xCC008000.
void aveditor_draw_viewport_tri(int u1, int u2, unsigned int color)
{
    (void)u1; (void)u2;
    unsigned long long spill = FUN_803826f0();
    float* rect = (float*)(unsigned int)(spill >> 0x20);
    float xarg = (float)(int)(unsigned int)spill;  // x half-extent from the spill low word

    double h = (double)((float)((double)(int)(DAT_806109c8 ^ 0x80000000) - DOUBLE_8061a6f0));
    if ((FLOAT_8061a710 <= rect[1]) && ((double)rect[1] <= h) &&
        (FLOAT_8061a710 <= rect[3]) && ((double)rect[3] <= h)) {
        int tex = sprite_get_texture_id(0);
        gx_begin_textured_batch(2, 0x92, 3, 4, tex);
        // v0
        DAT_cc008000 = (float)((double)rect[0] + (double)xarg); DAT_cc008000 = rect[1]; DAT_cc008000 = FLOAT_8061a710;
        DAT_cc008000 = *(float*)&color; DAT_cc008000 = FLOAT_8061a710; DAT_cc008000 = FLOAT_8061a710;
        // v1
        DAT_cc008000 = (float)((double)rect[2] + (double)xarg); DAT_cc008000 = rect[1]; DAT_cc008000 = FLOAT_8061a710;
        DAT_cc008000 = *(float*)&color; DAT_cc008000 = FLOAT_8061a710; DAT_cc008000 = FLOAT_8061a710;
        // v2
        DAT_cc008000 = (float)((double)rect[0] - (double)xarg); DAT_cc008000 = rect[3]; DAT_cc008000 = FLOAT_8061a710;
        DAT_cc008000 = *(float*)&color; DAT_cc008000 = FLOAT_8061a710; DAT_cc008000 = FLOAT_8061a710;
        // v3
        DAT_cc008000 = (float)((double)rect[2] - (double)xarg); DAT_cc008000 = rect[3]; DAT_cc008000 = FLOAT_8061a710;
        DAT_cc008000 = *(float*)&color; DAT_cc008000 = FLOAT_8061a710; DAT_cc008000 = FLOAT_8061a710;
        gx_end_batch();
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Draw a clipped X-cross line pair in viewport space: two lines slanted by +/- xarg around the
// rect's top and bottom edges, drawn only when both y edges are on-screen. rect = [x0,y0,x1,y1].
void aveditor_draw_viewport_xline(double xarg, double bias, float* rect, unsigned int color)
{
    float y0 = rect[1];
    if (FLOAT_8061a710 <= y0) {
        double h = (double)(CONCAT44(0x43300000, DAT_806109c8) ^ 0x80000000);
        float y1 = rect[3];
        if ((y0 <= (float)((double)h - DOUBLE_8061a6f0)) && (FLOAT_8061a710 <= y1) &&
            (y1 <= (float)((double)(CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000)) - DOUBLE_8061a6f0))) {
            gx_draw_line_counted((int)((double)(float)((double)rect[0] + xarg) - bias),
                                 (int)(FLOAT_8061a6e8 + y0),
                                 (int)((double)(float)((double)rect[0] - xarg) - bias),
                                 (int)(FLOAT_8061a6e8 + y1), color);
            gx_draw_line_counted((int)(FLOAT_8061a6e8 + (float)(bias + (double)(float)((double)rect[2] + xarg))),
                                 (int)(FLOAT_8061a6e8 + rect[1]),
                                 (int)(FLOAT_8061a6e8 + (float)(bias + (double)(float)((double)rect[2] - xarg))),
                                 (int)(FLOAT_8061a6e8 + rect[3]), color);
        }
    }
}

// -----------------------------------------------------------------------------
// Clip a 1D (y0,y1) segment to [0, screenHeight]. Returns 1 if any part is visible (clamping the
// endpoints in place), 0 if fully off-screen.
unsigned int aveditor_clip_segment_y(float* y0p, float* y1p)
{
    unsigned int result = 0;
    float y0 = *y0p;
    double h = (double)(int)(DAT_806109c8 ^ 0x80000000) - DOUBLE_8061a6f0;
    float hi = (float)h;

    if ((FLOAT_8061a710 <= y0) && (y0 <= hi) && (FLOAT_8061a710 <= *y1p) && (*y1p <= hi)) return 1;
    if ((y0 < FLOAT_8061a710) && (hi < *y1p)) { *y0p = FLOAT_8061a710; *y1p = hi; return 1; }
    if ((FLOAT_8061a710 <= y0) && (y0 <= hi) && ((*y1p < FLOAT_8061a710) || (hi < *y1p))) { *y1p = hi; return 1; }
    if ((FLOAT_8061a710 <= *y1p) && (*y1p <= hi)) {
        if ((FLOAT_8061a710 <= y0) && (y0 <= hi)) return 0;
        result = 1;
        *y0p = FLOAT_8061a710;
    }
    return result;
}

// -----------------------------------------------------------------------------
// Top-level arcade map-editor screen render entry. In roster/palette sub-mode (+0x25c & 0x2000000)
// it draws the background, layer strip, roster sub-screen and mode button; otherwise it draws the
// main editor frame (aveditor_draw_frame) plus the info panel, paste meter, selection-name text,
// scanline overlay, both split viewports, and the cursor.
void aveditor_render_screen(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                            unsigned long long a4, unsigned long long a5, unsigned long long a6,
                            unsigned long long a7, unsigned long long a8, unsigned int m9,
                            unsigned int m10, unsigned int p11, int p12, int p13, int p14,
                            unsigned int p15, unsigned int p16)
{
    (void)a1; (void)m9; (void)m10;
    unsigned long long spill = FUN_803826d8();
    int blendPrev = gx_set_blend_mode(0);
    gx_set_sub_material_id(1);

    if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x2000000) != 0) {
        int screen = hud_screen_find_root(*(int*)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        gx_set_sub_material_id(0);
        aveditor_draw_grid_background();
        gx_set_sub_material_id(1);
        FUN_8033f224();
        aveditor_draw_layer_strip();
        FUN_80340f48(screen, 0, p11, p12, p13, p14, p15, p16);
        aveditor_draw_mode_button();
        gx_set_blend_mode(blendPrev);
        FUN_80382724();
        return;
    }

    aveditor_draw_frame(spill, a2, a3, a4, a5, a6, a7, a8, DAT_80612be4, 0, p11, p12, p13, p14, p15, p16);

    if ((*(unsigned int*)(DAT_80612be4 + 0x238) & 0x8000) == 0) {
        FUN_803462c0(8);

        // layer strip (unless a modal screen is up and not flagged)
        bool drawStrip = ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x44088) == 0);
        if (!drawStrip) {
            if (__screen_current_record() != 0) {
                int sc = __screen_current_record();
                if ((*(unsigned int*)(sc + 0x214) & 8) != 0) drawStrip = true;
            }
        }
        if (drawStrip) aveditor_draw_layer_strip();

        // info-name text block for the focused editor screen
        bool focused = ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x4044088) == 0) &&
                       (hud_screen_find_root(*(int*)(&DAT_8051a820 + DAT_806119ec * 0x1e0)) == DAT_80612bf4);
        if (focused || ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x4000000) != 0)) {
            int textPtr = 0, palLabel = 0, type = -1;
            unsigned int flag = 0;
            bool isSlot = false;
            int* picked = 0;
            int mode = *(int*)(DAT_80612be4 + 0x254);
            if ((mode == 1) || (mode == 4)) {
                picked = roster_resolve_picked();
                if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x4000000) != 0) picked = *(int**)(DAT_80612be4 + 0x2f1c);
                if (*(int*)(DAT_80612be4 + 0x2f78) < 0) {
                    if ((*(int*)(DAT_80612be4 + 0x2f18) != 0) && (picked != 0)) {
                        type = *picked;
                        if (((&DAT_80477c80)[type * 0x31] & 0x1000) != 0) palLabel = aveditor_get_palette_entry_label(type);
                        if ((picked[1] == -1) || (FUN_8032ae60() != 0))
                            textPtr = get_string_by_id((int)*(short*)(&DAT_80477bc8 + type * 0xc4));
                        else
                            textPtr = DAT_80612be4 + picked[1] * 0x8c + 0xdd14;
                    }
                } else {
                    int marker = DAT_80612be4 + *(int*)(DAT_80612be4 + 0x2f78) * 0xc + 0x3070;
                    type = roster_slot_to_layout((unsigned int)*(unsigned char*)(marker + 7));
                    isSlot = true;
                    if ((*(short*)(marker + 10) == -1) || (FUN_8032ae60() != 0))
                        textPtr = get_string_by_id((int)*(short*)(&DAT_80477bc8 + type * 0xc4));
                    else
                        textPtr = DAT_80612be4 + *(short*)(marker + 10) * 0x8c + 0xdd14;
                }
            } else if ((mode == 2) && (*(int*)(DAT_80612be4 + 0x2f18) != 0)) {
                int t = *(int*)(*(int*)(DAT_80612be4 + 0x2f18) + 0x1c);
                if (((t == 3) || (t == 2)) && ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x4000000) == 0)) flag = 1;
                int name = get_string_by_id(*(int*)(&DAT_8047fb58 + t * 4));
                int fmt = get_string_by_id(0xa25);
                textPtr = __debug_log_formatted(s__s__s_80611194, a2, a3, a4, a5, a6, a7, a8,
                                                (unsigned int)fmt, (unsigned int)name, p11, p12, p13, p14, p15, p16);
            }
            if ((textPtr != 0) && (picked != 0) && (!isSlot)) {
                if (*(int*)(DAT_80612be4 + 0x254) != 2) {
                    if (((&DAT_80477c80)[*picked * 0x31] & 0x1000) != 0) palLabel = aveditor_get_palette_entry_label(*picked);
                    if (palLabel != 0)
                        textPtr = __debug_log_formatted(s__s___s__8061119c, a2, a3, a4, a5, a6, a7, a8,
                                                        (unsigned int)textPtr, (unsigned int)palLabel, p11, p12, p13, p14, p15, p16);
                }
            }
            if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x4000000) == 0) {
                aveditor_draw_paste_meter_if_active();
                // (info-count panel for the placed/paste member omitted: pure text layout)
            }
            FUN_80345cf4(textPtr, 0, type, flag);
        } else if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x4000000) == 0) {
            FUN_80345cd8();
        }
        aveditor_draw_paste_meter_if_active();
        aveditor_draw_scanline_overlay();
    }

    gx_set_blend_mode(0);
    aveditor_draw_title_bars(0);
    hud_screen_render_viewport(DAT_80612bf4, 1);
    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    aveditor_draw_title_bars(1);
    hud_screen_render_viewport(DAT_80612bf4, 2);
    FUN_803462c0(0);
    FUN_803462c0(2);
    FUN_803462c0(3);
    FUN_803462c0(7);
    FUN_803462c0(9);
    int screen = __screen_current_record();
    if ((*(int*)(&DAT_8051a824 + *(int*)(screen + 0x38) * 0x1e0) != 0) &&
        ((__screen_current_record(), (*(unsigned int*)(__screen_current_record() + 0x40) & 0x40000) == 0))) {
        aveditor_draw_cursor();
    }
    gx_set_blend_mode(blendPrev);
    FUN_80382724();
}
