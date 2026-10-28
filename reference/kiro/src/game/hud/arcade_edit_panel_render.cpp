// Batch: batch_974 | Module: arcade/challenge editor slide-panel strip rendering + interaction
// Address range: 0x80345e80 - 0x80346bac
// Date: 2026-09-12
//
// The render/interaction half of batch_973's animated slide-panel strip. Each visible panel slot
// is drawn either as the tab-row (slot 3, a horizontal strip of sprite tabs with the selected tab
// highlighted by a pulsing color-lerp) or as a generic labelled panel (a 3-slice sprite background
// plus a per-slot-type label/icon selected by a switch on the slot index). The strip also owns two
// scroll-arrow hit rects (DAT_805e2e30 left / DAT_805e2e40 right) and a 0x10000-byte scratch buffer
// (DAT_80612d90) with two label pointers (DAT_80612d8c mode-2, DAT_80612d88 mode-3).
//
//   panel_scroll_hittest      0x80345e80  hit-test a point against the two scroll-arrow rects (-1/1/0).
//   panel_draw_tab_row        0x80345f2c  draw slot 3: the horizontal tab strip + scroll-arrow rects.
//   panel_slot_draw           0x803462c0  dispatch one active slot to the tab-row or generic drawer.
//   panel_draw_slot_content   0x80346348  draw a generic panel: 3-slice bg + per-type label/icon.
//   panel_get_scratch_ptr     0x80346b18  return the panel scratch/state pointer (&DAT_805e2e50).
//   panel_get_mode2_label     0x80346b24  return the mode-2 label string ptr, or 0.
//   panel_get_mode3_label     0x80346b40  return the mode-3 label string ptr, or 0.
//   panel_get_state94         0x80346b5c  return the DAT_80612d94 state word.
//   panel_alloc_scratch       0x80346b64  allocate + zero the 0x10000 scratch buffer, reset ptrs.
//   panel_noop                0x80346bac  empty stub.
//
// Callers: the arcade editor screen render path (per active slot) and the pointer-input dispatch.
//
// NON-MATCHING: reproduces the decompiler's structure. The panel slot array is addressed through
// the original &DAT_805e2ab4 + idx*0x48 / &DAT_805e2de0 + i*0x10 dual indexing, reproduced verbatim.
// The paired-single register spills (in_f24..f31 / in_ps24_1..ps31_1 and the extraout_f1 argument
// carried in a paired-single lane) are expressed as plain double locals per Compile-Clean rule 3;
// every branch predicate is encoded literally. The int->double magic-constant conversions (CONCAT44
// with 0x43300000 minus DOUBLE_8061ac68) are reproduced as-is. Raw FUN_/DAT_/FLOAT_ symbols are
// declared file-locally with call-site-accurate arity (rule 5); named gx_*/text_*/sprite_*/aveditor_*
// helpers resolve through the shared headers (rule 6). CONCAT44 comes from common.h.

#include "common.h"

extern "C" {

// ---- panel slot array (shared with arcade_edit_panel_anim.cpp) -------------
extern float  DAT_805e2aac;   // slot +0x1c : geometry float 6
extern float  DAT_805e2ab0;   // slot +0x20 : geometry float 7
extern float  DAT_805e2ab4;   // slot +0x24 : interpolated current X
extern float  DAT_805e2ab8;   // slot +0x28 : interpolated current Y
extern float  DAT_805e2ac8;   // slot +0x38 : interpolated alpha/scale
extern int    DAT_805e2acc;   // slot +0x3c : active flag
extern int    DAT_805e2ad0;   // slot +0x40 : edge-clip predicate 0
extern int    DAT_805e2ad4;   // slot +0x44 : edge-clip predicate 1

// slot-3 (tab row) prebuilt geometry
extern float  fRam805e2b84, fRam805e2b88, fRam805e2b8c, fRam805e2b90, fRam805e2ba0;

// scroll-arrow hit rects + tab hit-rect table
extern float  DAT_805e2de0, DAT_805e2de4, DAT_805e2de8, DAT_805e2dec; // tab rects [n] x 0x10
extern float  DAT_805e2e30, DAT_805e2e34, DAT_805e2e38, DAT_805e2e3c; // left scroll-arrow rect
extern float  DAT_805e2e40, DAT_805e2e44, DAT_805e2e48, DAT_805e2e4c; // right scroll-arrow rect
extern int    DAT_805e2e50;   // panel scratch/state base
extern int    DAT_805e2d60;   // slot-9 label buffer

// ---- editor context + scratch buffer ---------------------------------------
extern int    DAT_80612be4;   // editor context base (mode +0x200, selected tab +0x254)
extern int    DAT_80612d70;   // panel-strip visible flag
extern int    DAT_80612d74;   // scroll drag target A (palette item index)
extern int    DAT_80612d78;   // scroll drag target B
extern int    DAT_80612d80;   // scratch cursor
extern int    DAT_80612d88;   // mode-3 label record ptr
extern int    DAT_80612d8c;   // mode-2 label record ptr
extern int    DAT_80612d90;   // 0x10000 scratch buffer ptr
extern int    DAT_80612d94;   // panel misc state word
extern int    DAT_806119ec;   // active screen-desc index
extern int    DAT_80611928;   // frame tick (mod 0x3c pulse)

// ---- sprite/material tables ------------------------------------------------
extern int    DAT_80481b08;   // tab sprite-id table (int[n])
extern float  DAT_80481b1c;   // tab X-advance table (float[n])
extern float  DAT_80481b30;   // tab width table (float[n])
extern int    DAT_80610a20;   // material bank base ptr
extern int    DAT_80611748;   // scissor rect params (int[7])
extern float  DAT_80583228;   // pulse sine LUT (float[0x800])
extern int    DAT_8047f9b0;   // slot-2 string-id table (stride 0x18)
extern int    DAT_8047f9b4;   // (unused padding sibling)
extern int    DAT_80477bcc;   // palette-item swatch color table (stride 0xc4)
extern int    DAT_8051a820;   // screen-root table (stride 0x1e0)

// ---- layout constants (FLOAT_8061ac.. block) -------------------------------
extern double DOUBLE_8061ac68; // int->double magic bias
extern float  FLOAT_80611a64;  // viewport scale A
extern float  FLOAT_80611a68;  // viewport scale B
extern float  FLOAT_80611a70;  // viewport bias B
extern float  FLOAT_8061ac3c;  // 1.0f
extern float  FLOAT_8061ac40, FLOAT_8061ac48, FLOAT_8061ac4c, FLOAT_8061ac60;
extern float  FLOAT_8061ac70, FLOAT_8061ac7c, FLOAT_8061ac84, FLOAT_8061ac88, FLOAT_8061ac8c;
extern float  FLOAT_8061ac90, FLOAT_8061ac94, FLOAT_8061ac98, FLOAT_8061ac9c, FLOAT_8061aca0;
extern float  FLOAT_8061aca4, FLOAT_8061aca8, FLOAT_8061acac, FLOAT_8061acb0, FLOAT_8061acb4;
extern float  FLOAT_8061acb8, FLOAT_8061acbc, FLOAT_8061acc0;

// ---- named cross-module helpers --------------------------------------------
extern void   memset_wrapper(int dst, int val, int len);
extern unsigned int FUN_803825c0(double v);      // pack color/alpha from float
extern double FUN_803826c0(void);                // read cursor base X
extern unsigned long long FUN_803826d0(void);    // push scope (returns slot|hi)
extern void   FUN_8038270c(void);                // pop scope (tab row)
extern void   FUN_8038271c(void);                // pop scope (slot content)
extern int    mem_alloc(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                        unsigned long long a4, unsigned long long a5, unsigned long long a6,
                        unsigned long long a7, unsigned long long a8, int size, int tag,
                        int a11, int a12, int a13, int a14, int a15, int a16);

extern void   gx_set_sub_material_id(int id);
extern void   gx_set_active_material(int id);
extern void   gx_set_scissor_rect(int x0, int x1, int y0, int y1);
extern void   gx_draw_sprite_rect(int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1);
extern int    sprite_get_texture_id(int spriteId);
extern void   sprite_draw_ui_element(int x0, int y0, int x1, int y1, int kind);
extern void   cursor_draw_stretched(double x0, double y0, double x1, double y1);

extern unsigned int color_scale_lo(double alpha, unsigned int rgba);
extern void   color_lerp(double t, unsigned int a, unsigned int b);

extern void   text_set_font_table(int id);
extern void   text_set_color_rgb(double r, double g, double b);
extern double text_get_char_width(unsigned char* str);
extern void   text_apply_alpha_fade(double x, unsigned char* str, int a, int b);
extern void   text_apply_alpha(double x, int* str, int a);
extern void   text_draw_char(double x, double y, unsigned char* str, unsigned int color);
extern void   text_draw_char_drop_shadow(double x, double y, unsigned char* str,
                                         unsigned int color, unsigned int shadow);
extern unsigned char* get_string_by_id(int id);
extern void   input_get_button_name(int btn, int* out);

extern int    hud_screen_destroy_current(void);
extern int    hud_screen_find_root(int desc);
extern void** aveditor_get_screen_desc(void);
extern unsigned int aveditor_is_mode_2(int node);
extern unsigned int aveditor_is_mode_3(int node);
extern int    aveditor_palette_item_texture_id(int item);
extern void   aveditor_draw_palette_item(int kind, float* rect, int tex, unsigned int color,
                                         int alpha, int flag);
extern void   aveditor_draw_selection_cursor(double x0, double y0, double x1, double y1);

// ---- forward decls within this TU ------------------------------------------
int  panel_get_mode2_label(void);
int  panel_get_mode3_label(void);
void panel_draw_slot_content(unsigned long long p1, double y, double w, double h, double alpha,
                             unsigned int p6, int p7, int state, unsigned char* label);
void panel_draw_tab_row(unsigned long long p1, double baseY, unsigned long long p3,
                        unsigned long long p4, double alpha);

// ---------------------------------------------------------------------------
// 0x80345e80 : hit-test a point against the two scroll-arrow rects (-1/1/0)
// ---------------------------------------------------------------------------
unsigned int panel_scroll_hittest(double px, double py)
{
    if (DAT_80612d70 != 0) {
        if (((((double)DAT_805e2e30 <= px) && (px <= (double)DAT_805e2e38)) &&
             ((double)DAT_805e2e34 <= py)) && (py <= (double)DAT_805e2e3c)) {
            return 0xffffffff;
        }
        if ((((double)DAT_805e2e40 <= px) && (px <= (double)DAT_805e2e48)) &&
            (((double)DAT_805e2e44 <= py && (py <= (double)DAT_805e2e4c)))) {
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80345f2c : draw slot 3 - the horizontal tab strip + scroll-arrow rects
// ---------------------------------------------------------------------------
void panel_draw_tab_row(unsigned long long p1, double baseY, unsigned long long p3,
                        unsigned long long p4, double alpha)
{
    float f1, f2;
    unsigned short u4, u5;
    unsigned int color;
    int tex, mat, i, tabIdx, rectOff;
    double baseX, vpB, tabW, arrowW, advX, curY, rowTop, colH, iconW;
    double d16, d15, d14, pulse;
    (void)p3; (void)p4;

    baseX = FUN_803826c0();
    int tabCount = 3;
    if (*(int*)(DAT_80612be4 + 0x200) != 0) {
        tabCount = 5;
    }
    vpB = (double)FLOAT_80611a68;
    tabW = (double)(float)((double)FLOAT_8061ac40 * vpB);
    arrowW = (double)(float)((double)FLOAT_8061ac84 * vpB);
    advX = (double)(float)((double)FLOAT_8061ac70 * tabW);
    rowTop = (double)(float)(baseY + advX);
    color = FUN_803825c0((double)(float)((double)FLOAT_8061ac88 * alpha));
    gx_set_sub_material_id(1);
    DAT_805e2e38 = (float)(baseX + arrowW);
    DAT_805e2e30 = (float)baseX;
    colH = (double)(FLOAT_8061ac70 * (float)((double)FLOAT_8061ac8c * vpB));
    DAT_805e2e34 = (float)(rowTop - colH);
    curY = (double)DAT_805e2e34;
    DAT_805e2e3c = (float)(rowTop + colH);
    d15 = (double)DAT_805e2e3c;
    cursor_draw_stretched(baseX, curY, (double)DAT_805e2e38, d15);
    baseX = baseX + arrowW;
    DAT_80612d70 = 1;
    double adv = (double)FLOAT_8061ac70;
    rectOff = 0;
    i = 0;
    for (tabIdx = 0; baseX = (double)(float)baseX, tabIdx < tabCount; tabIdx = tabIdx + 1) {
        tex = sprite_get_texture_id(*(int*)((int)&DAT_80481b08 + i));
        mat = *(int*)DAT_80610a20 + tex * 0x58;
        iconW = (double)FLOAT_8061ac90;
        u4 = *(unsigned short*)(mat + 0x4e);
        u5 = *(unsigned short*)(mat + 0x52);
        if (tabIdx == *(int*)(DAT_80612be4 + 0x254)) {
            pulse = (double)(CONCAT44(0x43300000, DAT_80611928 % 0x3c) ^ 0x80000000);
            color_lerp((double)(FLOAT_8061ac94 *
                                (FLOAT_8061ac70 *
                                 (float)(&DAT_80583228)[(int)(FLOAT_8061ac98 * FLOAT_8061ac60 *
                                                              FLOAT_8061ac9c *
                                                              ((float)(pulse - DOUBLE_8061ac68) /
                                                               FLOAT_8061ac4c) +
                                                              FLOAT_8061ac70) & 0x7ff] +
                                 FLOAT_8061ac70)),
                       color & 0xff | 0x7d6ea500, color & 0xff | 0xb4aab400);
            iconW = (double)(float)(iconW * (double)FLOAT_8061aca0);
        }
        d14 = (double)(float)(advX * (double)*(float*)((int)&DAT_80481b1c + i) + baseX);
        iconW = (double)(float)(adv * (double)((float)(tabW * iconW) *
                                               *(float*)((int)&DAT_80481b30 + i)));
        f2 = (float)(d14 - iconW);
        d16 = (double)f2;
        d15 = (double)(float)(rowTop - iconW);
        f1 = (float)(d14 + iconW);
        double d15b = (double)f1;
        *(float*)((int)&DAT_805e2de0 + rectOff) = f2;
        d14 = (double)(float)(rowTop + iconW);
        *(float*)((int)&DAT_805e2de4 + rectOff) = (float)(rowTop - iconW);
        *(float*)((int)&DAT_805e2de8 + rectOff) = f1;
        *(float*)((int)&DAT_805e2dec + rectOff) = (float)(rowTop + iconW);
        gx_set_active_material(tex);
        gx_draw_sprite_rect((int)d16, (int)d15, (int)d15b, (int)d14, 0, 0,
                            (unsigned int)u4 << 4, (unsigned int)u5 << 4);
        rectOff = rectOff + 0x10;
        i = i + 4;
        baseX = tabW * (double)*(float*)((int)&DAT_80481b1c + (i - 4)) + baseX;
    }
    DAT_805e2e44 = (float)curY;
    DAT_805e2e40 = (float)(baseX + (double)FLOAT_80611a68);
    DAT_805e2e4c = (float)d15;
    DAT_805e2e48 = (float)((double)DAT_805e2e40 + arrowW);
    cursor_draw_stretched((double)DAT_805e2e40, curY, (double)DAT_805e2e48, (double)DAT_805e2e3c);
    FUN_8038270c();
    (void)p1;
}

// ---------------------------------------------------------------------------
// 0x803462c0 : dispatch one active slot to the tab-row or generic drawer
// ---------------------------------------------------------------------------
void panel_slot_draw(int idx)
{
    int off = idx * 0x48;

    if ((&DAT_805e2acc)[idx * 0x12] != 0) {
        if (idx == 3) {
            panel_draw_tab_row((double)fRam805e2b8c, (double)fRam805e2b90, (double)fRam805e2b84,
                               (double)fRam805e2b88, (double)fRam805e2ba0);
        }
        else {
            panel_draw_slot_content((double)*(float*)(&DAT_805e2ab4 + off),
                                    (double)*(float*)(&DAT_805e2ab8 + off),
                                    (double)*(float*)(&DAT_805e2aac + off),
                                    (double)*(float*)(&DAT_805e2ab0 + off),
                                    (double)*(float*)(&DAT_805e2ac8 + off), idx,
                                    *(int*)(&DAT_805e2ad0 + off), *(int*)(&DAT_805e2ad4 + off),
                                    (unsigned char*)0);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80346348 : draw a generic panel - 3-slice bg + per-type label/icon
// ---------------------------------------------------------------------------
void panel_draw_slot_content(unsigned long long p1, double y, double w, double h, double alpha,
                             unsigned int p6, int p7, int state, unsigned char* label)
{
    float f1;
    unsigned int color, shadow, textColor;
    int t4, t5, t6, kind;
    void** desc;
    unsigned long long slot;
    double x;
    double d12, d13, d15, d16;
    unsigned char abStack_f0[8];
    float e8, e4, e0, dc;
    (void)p1; (void)p6; (void)p7;

    slot = FUN_803826d0();
    kind = (int)((unsigned long long)slot >> 0x20);
    x = 0.0;   // cursor base X (arrives in a paired-single lane; 0 at scope entry)
    color = FUN_803825c0((double)(float)((double)FLOAT_8061aca4 * alpha));
    textColor = color_scale_lo(alpha, 0x7f7f7f7f);
    color_scale_lo(alpha, 0x645f877f);
    color = color & 0xff;
    gx_set_sub_material_id(1);
    gx_set_scissor_rect(*(int*)DAT_80611748, *(int*)((int)&DAT_80611748 + 0x14),
                        *(int*)((int)&DAT_80611748 + 4), *(int*)((int)&DAT_80611748 + 0x18));
    if (kind != 7) {
        d12 = (double)(FLOAT_8061aca8 * FLOAT_80611a68 + FLOAT_80611a70);
        t4 = sprite_get_texture_id(0x3ee);
        t5 = sprite_get_texture_id(0x3f0);
        t6 = sprite_get_texture_id(0x3ef);
        d13 = (double)(float)(y + (double)FLOAT_8061ac60);
        d15 = w;
        d16 = x;
        if ((int)slot != 0) {
            d16 = (double)(float)(x + d12);
            d15 = (double)(float)(w - d12);
            gx_set_active_material(t4);
            t4 = *(int*)DAT_80610a20 + t4 * 0x58;
            gx_draw_sprite_rect((int)x, (int)d13, (int)d16, (int)(d13 + h), 0, 0,
                                (unsigned int)*(unsigned short*)(t4 + 0x4e) << 4,
                                (unsigned int)*(unsigned short*)(t4 + 0x52) << 4);
        }
        if (state != 0) {
            d15 = (double)(float)(d15 - d12);
            gx_set_active_material(t6);
            t6 = *(int*)DAT_80610a20 + t6 * 0x58;
            unsigned int right = (unsigned int)(x + w);
            int left = (int)((double)(float)(x + w) - d12);
            gx_draw_sprite_rect(left, (int)d13, right, (int)(d13 + h), 0, 0,
                                (unsigned int)*(unsigned short*)(t6 + 0x4e) << 4,
                                (unsigned int)*(unsigned short*)(t6 + 0x52) << 4);
        }
        gx_set_active_material(t5);
        t5 = *(int*)DAT_80610a20 + t5 * 0x58;
        gx_draw_sprite_rect((int)d16, (int)d13, (int)(d16 + d15), (int)(d13 + h), 0, 0,
                            (unsigned int)*(unsigned short*)(t5 + 0x4e) << 4,
                            (unsigned int)*(unsigned short*)(t5 + 0x52) << 4);
        y = (double)(float)(d13 - (double)FLOAT_8061ac60);
    }
    text_set_font_table(5);
    switch (kind) {
    case 0: {
        int scr = hud_screen_destroy_current();
        unsigned char* str = get_string_by_id(0x364);
        if ((((*(unsigned int*)(scr + 0x214) & 4) != 0) && (scr != 0)) &&
            ((*(unsigned char**)(scr + 0x200) != (unsigned char*)0) &&
             ((*(unsigned int*)(scr + 0x40) & 0x800) != 0))) {
            str = *(unsigned char**)(scr + 0x200);
        }
        double cw = text_get_char_width(str);
        f1 = FLOAT_80611a68;
        if (cw <= w) {
            f1 = FLOAT_8061ac48 * FLOAT_80611a68;
        }
        double d = (double)f1;
        text_apply_alpha_fade(-(double)(float)((double)FLOAT_8061ac60 * d - w), str, 0, 1);
        text_draw_char_drop_shadow((double)(float)(x + d),
                                   (double)(float)((double)FLOAT_8061acac * h + y), str,
                                   textColor, color);
        double one = (double)FLOAT_8061ac3c;
        text_set_color_rgb(one, one, one);
        break;
    }
    case 2: {
        int scr = hud_screen_destroy_current();
        if (((((*(unsigned int*)(scr + 0x214) & 4) == 0) || (scr == 0)) ||
             (*(unsigned char**)(scr + 0x200) == (unsigned char*)0)) ||
            ((*(unsigned int*)(scr + 0x40) & 0x400) == 0)) {
            unsigned char* str = get_string_by_id(*(int*)(&DAT_8047f9b0 +
                                                          *(int*)(DAT_80612be4 + 0x254) * 0x18));
            text_draw_char_drop_shadow(
                (double)(float)((double)FLOAT_8061ac48 * (double)FLOAT_80611a68 + x),
                (double)(float)((double)FLOAT_8061acac * h + y), str, textColor, color);
        }
        else {
            text_draw_char_drop_shadow(
                (double)(float)((double)FLOAT_8061ac48 * (double)FLOAT_80611a68 + x),
                (double)(float)((double)FLOAT_8061acac * h + y),
                *(unsigned char**)(scr + 0x200), textColor, color);
        }
        break;
    }
    case 4: {
        unsigned char* str = get_string_by_id(0xa53);
        unsigned int yy = (unsigned int)((double)FLOAT_8061ac7c * h + y);
        int xx = (int)((double)FLOAT_8061ac60 * (double)FLOAT_80611a68 + x);
        unsigned int x2 = (unsigned int)((double)FLOAT_8061acc0 * (double)FLOAT_80611a68 + x);
        sprite_draw_ui_element(xx, yy - 5, x2, yy + 0x11, 5);
        double yb = (double)(CONCAT44(0x43300000, yy ^ 0x80000000) - DOUBLE_8061ac68);
        text_draw_char((double)(float)((double)FLOAT_8061acc0 * (double)FLOAT_80611a68 + x),
                       (double)(float)yb, str, textColor);
        break;
    }
    case 6: {
        unsigned char* str = get_string_by_id(0xa5d);
        unsigned int yy = (unsigned int)((double)FLOAT_8061ac7c * h + y);
        int xx = (int)((double)FLOAT_8061ac60 * (double)FLOAT_80611a68 + x);
        unsigned int x2 = (unsigned int)((double)FLOAT_8061acc0 * (double)FLOAT_80611a68 + x);
        sprite_draw_ui_element(xx, yy - 5, x2, yy + 0x11, 4);
        double yb = (double)(CONCAT44(0x43300000, yy ^ 0x80000000) - DOUBLE_8061ac68);
        text_draw_char((double)(float)((double)FLOAT_8061acc0 * (double)FLOAT_80611a68 + x),
                       (double)(float)yb, str, textColor);
        break;
    }
    case 8: {
        desc = aveditor_get_screen_desc();
        int scr = hud_screen_find_root(*(int*)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        unsigned char* str = (unsigned char*)0;
        if (*(void***)(scr + 4) != desc) {
            scr = *(int*)(scr + 0x2c);
        }
        if ((scr != 0) && (*(int*)(scr + 0x194) != 0)) {
            if (aveditor_is_mode_2(*(int*)(scr + 0x188)) != 0) {
                str = (unsigned char*)(size_t)panel_get_mode2_label();
            }
            if (aveditor_is_mode_3(*(int*)(scr + 0x188)) != 0) {
                str = (unsigned char*)(size_t)panel_get_mode3_label();
            }
            if (str != (unsigned char*)0) {
                text_draw_char(
                    (double)(float)((double)FLOAT_8061aca0 * (double)FLOAT_80611a68 + x),
                    (double)(float)((double)FLOAT_8061aca0 * (double)FLOAT_80611a64 + y),
                    str, textColor);
            }
        }
        break;
    }
    case 9: {
        double d13 = (double)(FLOAT_8061ac48 * FLOAT_80611a68);
        f1 = (float)(x + (double)*(float*)(&DAT_805e2aac + kind * 0x48)) - FLOAT_80611a68;
        d16 = (double)f1;
        d12 = (double)(FLOAT_8061ac84 * FLOAT_80611a68);
        d15 = -(double)(float)((double)FLOAT_8061acb4 * d13 -
                               (double)*(float*)(&DAT_805e2aac + kind * 0x48));
        if (DAT_80612d78 == 0) {
            if (-1 < DAT_80612d74) {
                e8 = (float)(d16 - (double)(FLOAT_8061acb0 * FLOAT_80611a68));
                unsigned int swatch = *(unsigned int*)(&DAT_80477bcc + DAT_80612d74 * 0xc4);
                e4 = (float)((double)FLOAT_8061ac70 *
                             (double)(float)(h - (double)(FLOAT_8061acbc * FLOAT_80611a64)) + y);
                dc = (float)((double)e4 + (double)(FLOAT_8061acbc * FLOAT_80611a64));
                e0 = f1;
                int tex = aveditor_palette_item_texture_id(DAT_80612d74);
                aveditor_draw_palette_item(2, &e8, tex, swatch, 0x7f, 0);
                d15 = -(double)(float)((double)FLOAT_8061acb8 * d13 - (double)e8);
            }
        }
        else {
            d15 = (double)(float)(d16 - d12);
            double yc = (double)(float)((double)FLOAT_8061acac * h + y);
            aveditor_draw_selection_cursor(d15, yc, d16,
                                           (double)(float)((double)FLOAT_8061ac94 * h + y));
            input_get_button_name(0xc, (int*)abStack_f0);
            d15 = (double)(float)(d15 - d12);
            text_draw_char(d15, yc, abStack_f0, textColor);
            d15 = -(double)(float)((double)FLOAT_8061acb8 * d13 - d15);
        }
        text_apply_alpha(d15, &DAT_805e2d60, 0);
        text_draw_char_drop_shadow((double)(float)(x + d13),
                                   (double)(float)((double)FLOAT_8061acac * h + y),
                                   (unsigned char*)&DAT_805e2d60, textColor, color);
        double one = (double)FLOAT_8061ac3c;
        text_set_color_rgb(one, one, one);
        break;
    }
    case -1:
        if (label != (unsigned char*)0) {
            text_draw_char_drop_shadow(
                (double)(float)((double)FLOAT_8061ac48 * (double)FLOAT_80611a68 + x),
                (double)(float)((double)FLOAT_8061acac * h + y), label, textColor, color);
        }
        break;
    }
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// 0x80346b18 : return the panel scratch/state pointer (&DAT_805e2e50)
// ---------------------------------------------------------------------------
int* panel_get_scratch_ptr(void)
{
    return &DAT_805e2e50;
}

// ---------------------------------------------------------------------------
// 0x80346b24 : return the mode-2 label string ptr, or 0
// ---------------------------------------------------------------------------
int panel_get_mode2_label(void)
{
    if (DAT_80612d8c != 0) {
        return *(int*)(DAT_80612d8c + 8);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80346b40 : return the mode-3 label string ptr, or 0
// ---------------------------------------------------------------------------
int panel_get_mode3_label(void)
{
    if (DAT_80612d88 != 0) {
        return *(int*)(DAT_80612d88 + 8);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80346b5c : return the DAT_80612d94 state word
// ---------------------------------------------------------------------------
int panel_get_state94(void)
{
    return DAT_80612d94;
}

// ---------------------------------------------------------------------------
// 0x80346b64 : allocate + zero the 0x10000 scratch buffer, reset ptrs
// ---------------------------------------------------------------------------
void panel_alloc_scratch(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int p9, int p10,
                         int p11, int p12, int p13, int p14, int p15, int p16)
{
    (void)p9; (void)p10;
    DAT_80612d90 = mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8, 0x10000, 2,
                             p11, p12, p13, p14, p15, p16);
    memset_wrapper(DAT_80612d90, 0, 0x10000);
    DAT_80612d8c = 0;
    DAT_80612d88 = 0;
    DAT_80612d80 = 0;
}

// ---------------------------------------------------------------------------
// 0x80346bac : empty stub
// ---------------------------------------------------------------------------
void panel_noop(void)
{
}

} // extern "C"
