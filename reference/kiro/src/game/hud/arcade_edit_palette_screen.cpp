// Batch: batch_915 | Module: arcade map-editor palette/colour screens + selection pulse
// Address range: 0x8032613c - 0x80326b50
// Date: 2026-09-12
//
// The screen-level layer above the batch_914 colour picker: it builds the colour-picker screen
// (R/G/B/value toggle rows) and the 0x40-swatch palette-grid screen, renders individual palette
// swatches and the palette label text, renders the "current colour" item (resolving the colour
// from the active tool/member/piece via a callback dispatch on element+0x38), and animates the
// selection cursor via a per-member "pulse" value at member+0x28.
//
//   aveditor_build_colorpicker_screen 0x8032613c  build the R/G/B/value colour-picker screen.
//   aveditor_draw_palette_swatch      0x8032634c  draw one palette-grid swatch (focus tint + fill).
//   aveditor_build_palette_grid       0x803264d0  build the 0x40-swatch palette-grid screen.
//   aveditor_draw_palette_label       0x803265bc  draw the palette label text (auto-fit + shadow).
//   aveditor_draw_current_color       0x803266dc  draw the "current colour" item (resolved colour).
//   aveditor_pulse_round              0x80326944  round member.pulse*scale to the nearest int.
//   aveditor_pulse_step               0x80326a0c  step a member's pulse by +/-, clamped [min,1].
//   aveditor_pulse_step_selected      0x80326a5c  step pulse on the active + all selected members.
//   aveditor_draw_selection_cursor    0x80326ad8  advance the pulse and draw the stretched cursor.
//   aveditor_draw_selection_cursor_rc 0x80326b50  compute the cursor rect and draw it.
//
// aveditor_draw_current_color dispatches on the element's callback pointer (element+0x38): each
// known editor callback (FUN_80332f08 / FUN_803277bc / the FUN_803276.. family) selects where the
// colour index comes from (a fixed default block, the active member/piece record, or the palette by
// a per-record byte), falling back to the live RGB DAT_805d6748 block; a zero result pulses.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs; named cross-module
// helpers (and the batch_908/914 palette helpers) are re-declared with their real ABI.

#include "common.h"
#include <cstring>   // memcpy for the u32->double bit reinterpretation

extern "C" {

extern int    DAT_80612be4;    // editor context base
extern int    DAT_80612c14;    // active member list head (chain via +0x40)

// -- palette / HSV state (shared with the batch_914 picker) --
extern unsigned int DAT_805d6630;   // working palette, 0x40 words
extern float  DAT_805d6738;    // HSV value channel
extern float  DAT_805d6740;    // derived RGB r
extern float  DAT_805d6744;    // derived RGB g
extern float  DAT_805d6748;    // derived RGB b (start of the 3-float RGB block)
extern float  DAT_805d675c;    // default pulse value when no member bound

// -- default colour block + trig bias --
extern unsigned int DAT_803e0a8c;   // fixed default colour record (copied when no member)
extern float  DAT_80583228;    // (unused here, kept off) sin/cos table base
extern double DOUBLE_8061a948; // u32->double bias constant

// -- layout / geometry float constants --
extern float  FLOAT_8061a92c;  // clamp min
extern float  FLOAT_8061a930;  // padding / 0.5
extern float  FLOAT_8061a940;  // 1.0 clamp max
extern float  FLOAT_8061a95c;  // colour-picker item size
extern float  FLOAT_8061a960;  // picker pos term 0
extern float  FLOAT_8061a964;  // picker pos term 1
extern float  FLOAT_8061a968;  // picker pos term 2
extern float  FLOAT_8061a96c;  // swatch focus-alpha scale
extern float  FLOAT_8061a970;  // palette-grid swatch size
extern float  FLOAT_8061a978;  // pulse->index scale
extern float  FLOAT_8061a97c;  // pulse step magnitude
extern float  FLOAT_8061a980;  // cursor rect width term
extern float  FLOAT_8061a984;  // cursor rect height term
extern float  FLOAT_80611a5c;  // scroll scale
extern float  FLOAT_80611a64;  // scroll unit
extern float  FLOAT_80611a68;  // text right-margin / cursor unit
extern float  FLOAT_80611a6c;  // scroll bias
extern int    DAT_806119e0;    // shared position/anim descriptor arg

// -- raw helpers not yet individually named --
extern void  FUN_803826e4(void);    // begin picker screen build context (grid)
extern void  FUN_80382730(void);    // end picker screen build context (grid)
extern unsigned long long FUN_803826f0(void);   // fetch element (register pair)
extern void  FUN_8038273c(void);    // finalize screen (variant B)
extern void  FUN_803826e8(void);    // begin draw context (current-colour)
extern void  FUN_80382734(void);    // end draw context (current-colour)
extern unsigned long long FUN_80382654(void);   // begin cursor draw context (register pair)
extern void  FUN_803826a0(void);    // end cursor draw context
extern unsigned int FUN_803825c0(double v);         // float->packed alpha byte
extern double FUN_803900e4(double v);               // floor
extern double FUN_80390324(double v);               // ceil
extern int  *FUN_80336ba8(int record);              // resolve a member/piece colour record ptr
extern unsigned long long __save_gpr_28(void);      // GPR spill (r28..) -> value in r3:r4
extern void  __restore_gpr_28(void);

// editor callback identities compared against element+0x38
extern void  FUN_80332f08(void);
extern void  FUN_803277bc(void);
extern void  FUN_803276d0(void);
extern void  FUN_80327728(void);
extern void  FUN_80327d38(void);
extern void  FUN_803276fc(void);

// modal picker vtable compared in the label draw
extern void *PTR_FUN_80481898;

// -- named cross-module helpers (real ABI) --
extern int   get_string_by_id(int stringId);
extern void  hud_element_init_from_layout(int layoutId, int a, int b, void *closeCb);
extern void  hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void  hud_set_context_property(int prop);
extern void  hud_screen_register_handler_global(int slot, int stringId, int a, int b);
extern void  hud_add_item_internal(double w, double h);
extern void  hud_begin_scroll_with_item(void);
extern void  hud_begin_scroll_region(void);
extern void  hud_item_add_labeled_pair(int a, int b, int c, int d);
extern int   hud_get_last_created_item(void);
extern void  hud_item_set_toggle(void *value, int item);
extern void  hud_end_scroll_region(void);
extern void  hud_element_set_pos_y(double y, int descriptor);
extern void  hud_add_padding_item(double size);
extern void  hud_begin_sub_list(int a, int b, int cols);
extern void  hud_end_sub_list(void);
extern void  hud_add_list_item(void);
extern void  hud_set_last_child_tag(int tag);
extern int   hud_item_is_focused(int item);
extern int   hud_item_is_selectable(int item, int which);
extern void  gx_fill_rect_clipped(int x0, int y0, int x1, int y1, unsigned int color);
extern unsigned int color_scale_lo(double alpha, unsigned int color);
extern void  hud_item_compute_colors(int element, int item, unsigned int *outColors, unsigned int *outFlags);
extern int   hud_element_get_dimension(int element, int which);
extern void  text_set_font_table(int table);
extern unsigned char *text_check_scale(double width, void *str);
extern double text_get_char_height(unsigned char *glyphs);
extern void  text_draw_char_shadowed(double x, double y, unsigned char *glyphs, unsigned int color, unsigned int flags);
extern int   cursor_get_texture_id(int id);
extern void  cursor_draw_stretched(unsigned long long ctx, double a, double b, double c);
extern void  aveditor_commit_picked_color(void);
extern void  aveditor_init_wset_entry(unsigned char *entry, int set);
extern void  aveditor_draw_palette_item(unsigned int focused, unsigned int rect, int textureId, unsigned int color, unsigned int frame, int flags);
extern unsigned int aveditor_tick_pulse(void);

} // extern "C"

// Convert an unsigned 32-bit value to double using the game's magic-number idiom.
static double u32_to_double(unsigned int x)
{
    unsigned long long bits = ((unsigned long long)0x43300000 << 32) | (unsigned int)(x ^ 0x80000000u);
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d - DOUBLE_8061a948;
}

// -----------------------------------------------------------------------------

// Build the colour-picker screen: register the accept soft-key, then four labeled toggle rows bound
// to the live RGB channels (DAT_805d6740/44/48) and the value channel (DAT_805d6738), close callback
// aveditor_commit_picked_color; finally position the element.
void aveditor_build_colorpicker_screen(void)
{
    int item;
    hud_element_init_from_layout(0x10400, 0, 0, (void *)aveditor_commit_picked_color);
    hud_set_item_padding_mode((double)FLOAT_8061a930, 1, 1, 1);
    hud_set_context_property(0x20000);
    item = get_string_by_id(0xa5d);
    hud_screen_register_handler_global(1, item, 0, 0);
    hud_add_item_internal((double)FLOAT_8061a95c, (double)FLOAT_8061a95c);

    get_string_by_id(0xa37);
    hud_begin_scroll_with_item();
    hud_item_add_labeled_pair(0, 0, 0, 0);
    item = hud_get_last_created_item();
    hud_item_set_toggle(&DAT_805d6740, item);
    hud_end_scroll_region();

    get_string_by_id(0xa38);
    hud_begin_scroll_with_item();
    hud_item_add_labeled_pair(0, 0, 0, 0);
    item = hud_get_last_created_item();
    hud_item_set_toggle(&DAT_805d6744, item);
    hud_end_scroll_region();

    get_string_by_id(0xa39);
    hud_begin_scroll_with_item();
    hud_item_add_labeled_pair(0, 0, 0, 0);
    item = hud_get_last_created_item();
    hud_item_set_toggle(&DAT_805d6748, item);
    hud_end_scroll_region();

    get_string_by_id(0xa3a);
    hud_begin_scroll_with_item();
    hud_item_add_labeled_pair(0, 0, 0, 0);
    item = hud_get_last_created_item();
    hud_item_set_toggle(&DAT_805d6738, item);
    hud_end_scroll_region();

    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061a960 * FLOAT_80611a64 +
                   FLOAT_8061a964 * FLOAT_80611a64 +
                   FLOAT_8061a968 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
}

// Draw one palette-grid swatch: an outer frame (tinted 0x78787800 when the item is focused) and an
// inner fill from the palette colour scaled by the item's focus alpha (element+0xf0). The GPR-save
// spill provides the item pointer in r3 and the element in r4 (recovered as `element`/`item`).
void aveditor_draw_palette_swatch(unsigned int a, unsigned int b, float *rect)
{
    (void)a; (void)b;
    unsigned long long spill = __save_gpr_28();
    int item = (int)spill;
    int element = (int)(spill >> 0x20);

    int focused = hud_item_is_focused(item);
    double alpha = (double)*(float *)(element + 0xf0);
    unsigned int frameAlpha = FUN_803825c0((double)(float)((double)FLOAT_8061a96c * alpha));
    unsigned int fill = color_scale_lo(alpha, (&DAT_805d6630)[*(int *)(item + 0x14)]);

    if (focused == 0) {
        gx_fill_rect_clipped((int)rect[0], (int)rect[1], (int)rect[2], (int)rect[3], frameAlpha & 0xff);
    } else {
        gx_fill_rect_clipped((int)rect[0], (int)rect[1], (int)rect[2], (int)rect[3],
                             frameAlpha & 0xff | 0x78787800);
    }
    gx_fill_rect_clipped((int)rect[0] + 1, (int)rect[1] + 1, (int)rect[2] - 1, (int)rect[3] - 1, fill);
    __restore_gpr_28();
}

// Build the palette-grid screen: a padded scroll region containing an 8-wide sub-list of 0x40
// swatch items (tag 0xa1), then a trailing action list item (tag 0xa2). The trailing string id
// arrives in r6 (recovered as `trailingStringId`).
void aveditor_build_palette_grid(int trailingStringId)
{
    FUN_803826e4();
    get_string_by_id(0x95f);
    hud_begin_scroll_region();
    hud_add_padding_item((double)FLOAT_8061a940);
    hud_begin_sub_list(1, 1, 8);
    int i = 0;
    do {
        hud_add_item_internal((double)FLOAT_8061a970, (double)FLOAT_8061a970);
        hud_set_last_child_tag(0xa1);
        i = i + 1;
    } while (i < 0x40);
    hud_end_sub_list();
    hud_add_padding_item((double)FLOAT_8061a940);
    get_string_by_id(trailingStringId);
    hud_add_list_item();
    hud_set_last_child_tag(0xa2);
    hud_end_scroll_region();
    FUN_80382730();
}

// Draw the palette label text: pick the string (item value at +0x14 for the modal picker vtable,
// else the default id 0xb67), auto-fit the font to the rect width/height, and draw it shadowed.
void aveditor_draw_palette_label(unsigned int a, unsigned int b, float *rect)
{
    (void)a; (void)b;
    unsigned int colors[8];
    unsigned int flags;
    unsigned long long spill = FUN_803826f0();
    int element = (int)(spill >> 0x20);
    int stringId;
    if (*(void ***)(element + 4) == &PTR_FUN_80481898) {
        stringId = *(int *)((int)spill + 0x14);
    } else {
        stringId = 0xb67;
    }

    double x = (double)rect[0];
    double y = (double)rect[1];
    double width = (double)(float)((double)(rect[2] - FLOAT_80611a68) - x);

    hud_item_compute_colors(element, (int)spill, colors, &flags);
    int fontTable = hud_element_get_dimension(element, 0);
    text_set_font_table(fontTable);
    unsigned char *glyphs = text_check_scale(width, (void *)get_string_by_id(stringId));
    double h = text_get_char_height(glyphs);
    if ((double)(rect[3] - rect[1]) < h) {
        text_set_font_table(2);
        glyphs = text_check_scale(width, (void *)get_string_by_id(stringId));
    }
    text_draw_char_shadowed(x, y, glyphs, colors[0], flags);
    FUN_8038273c();
}

// Draw the "current colour" palette item. The colour index is resolved from the element's callback
// pointer (element+0x38): a fixed default block (FUN_80332f08), the active piece/member record
// (FUN_803277bc / the FUN_80327728/d38/276fc family via FUN_80336ba8), a default weapon-set entry
// (FUN_803276d0), or the live RGB block (fallback). A zero result pulses. Then draws the item.
void aveditor_draw_current_color(unsigned int a, unsigned int b, unsigned int rect)
{
    (void)a; (void)b;
    unsigned int color = 0;
    int element;
    unsigned int scratch[15];   // uStack_3c + local_38[14]

    FUN_803826e8();
    // element pointer arrives in r4 (extraout_r4)
    element = (int)FUN_803826f0();   // recovered element base
    void (*cb)(void) = *(void (**)(void))(element + 0x38);

    if (cb == FUN_80332f08) {
        // copy the fixed default colour record (7 words)
        int count = 2;
        unsigned int *src = &DAT_803e0a8c;
        unsigned int *dst = &scratch[0];
        unsigned int *pSrc, *pDst;
        do {
            pDst = dst; pSrc = src;
            unsigned int v2 = pSrc[2];
            pDst[1] = pSrc[1]; pDst[2] = v2;
            count = count + -1;
            src = pSrc + 2; dst = pDst + 2;
        } while (count != 0);
        pDst[3] = pSrc[3];
    } else if ((cb == FUN_803277bc) && (*(int *)(DAT_80612be4 + 0x11858) != 0)) {
        unsigned int *rec = (unsigned int *)FUN_80336ba8(
            DAT_80612be4 + *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14);
        int count = 2;
        rec = rec + -1;
        unsigned int *dst = &scratch[0];
        unsigned int *pSrc, *pDst;
        do {
            pDst = dst; pSrc = rec;
            unsigned int v2 = pSrc[2];
            pDst[1] = pSrc[1]; pDst[2] = v2;
            count = count + -1;
            rec = pSrc + 2; dst = pDst + 2;
        } while (count != 0);
        pDst[3] = pSrc[3];
    } else if (cb == FUN_803276d0) {
        aveditor_init_wset_entry((unsigned char *)&scratch[1], 1);
    } else if ((((cb == FUN_80327728) || (cb == FUN_80327d38)) || (cb == FUN_803276fc)) &&
               ((*(int *)(DAT_80612be4 + 0x2f18) != 0) ||
                (*(int *)(DAT_80612be4 + 0x11858) != 0))) {
        int active = *(int *)(DAT_80612be4 + 0x2f18);
        int rec = 0;
        if (*(int *)(DAT_80612be4 + 0x11858) != 0) {
            rec = DAT_80612be4 + *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14;
        }
        unsigned char idx;
        if (cb == FUN_803276fc) {
            if (active == 0) {
                idx = *(unsigned char *)FUN_80336ba8(rec);
            } else {
                idx = *(unsigned char *)(active + 0x18);
            }
        } else if (active == 0) {
            unsigned char *p = (unsigned char *)FUN_80336ba8(rec);
            idx = p[1];
        } else {
            idx = *(unsigned char *)(active + 0x19);
        }
        color = (&DAT_805d6630)[idx];
    } else {
        // fallback: copy the live RGB block (DAT_805d6748..)
        int count = 2;
        unsigned int *src = (unsigned int *)&DAT_805d6748;
        unsigned int *dst = &scratch[0];
        unsigned int *pSrc, *pDst;
        do {
            pDst = dst; pSrc = src;
            unsigned int v2 = pSrc[2];
            pDst[1] = pSrc[1]; pDst[2] = v2;
            count = count + -1;
            src = pSrc + 2; dst = pDst + 2;
        } while (count != 0);
        pDst[3] = pSrc[3];
    }

    if (color == 0) {
        color = aveditor_tick_pulse();
    }
    int texId = cursor_get_texture_id(0x27);
    unsigned int focused = hud_item_is_focused(element);
    aveditor_draw_palette_item(focused, rect, texId, color, 0xa0a0a20, 0);
    FUN_80382734();
}

// Round member.pulse*scale to the nearest int (compare the fractional distances to floor and ceil).
// Uses the default pulse when no member is bound.
unsigned int aveditor_pulse_round(int member)
{
    float pulse = DAT_805d675c;
    if (member != 0) {
        pulse = *(float *)(member + 0x28);
    }
    double scaled = (double)(FLOAT_8061a978 * pulse);
    unsigned int fl = (unsigned int)FUN_803900e4(scaled);
    double ce = FUN_80390324(scaled);
    if ((float)(scaled - u32_to_double((unsigned int)(int)ce)) <
        (float)(u32_to_double(fl) - scaled)) {
        fl = (int)ce;
    }
    return fl;
}

// Step a member's pulse value at +0x28 by +/- FLOAT_8061a97c (sign by dir!=1), clamped to
// [FLOAT_8061a92c, FLOAT_8061a940].
void aveditor_pulse_step(int member, int dir)
{
    float hi = FLOAT_8061a940;
    float step = FLOAT_8061a97c;
    if (dir != 1) {
        step = -FLOAT_8061a97c;
    }
    *(float *)(member + 0x28) = *(float *)(member + 0x28) + step;
    if (hi < *(float *)(member + 0x28)) {
        *(float *)(member + 0x28) = hi;
    }
    if (*(float *)(member + 0x28) < FLOAT_8061a92c) {
        *(float *)(member + 0x28) = FLOAT_8061a92c;
    }
}

// Step the pulse on the active member (ctx+0x2f18) and every selected member (flag bit 2 at +0x14)
// in the member chain (head DAT_80612c14, next at +0x40).
void aveditor_pulse_step_selected(int dir)
{
    for (int m = DAT_80612c14; m != 0; m = *(int *)(m + 0x40)) {
        if (m == *(int *)(DAT_80612be4 + 0x2f18)) {
            aveditor_pulse_step(m, dir);
        } else if ((*(unsigned char *)(m + 0x14) & 2) != 0) {
            aveditor_pulse_step(m, dir);
        }
    }
}

// Draw the animated selection cursor: advance the active member's pulse (via pulse_round) then draw
// the cursor texture stretched to the given rect. The three doubles are forwarded to the draw call
// (the original's first slot is an ignored register-pair placeholder).
void aveditor_draw_selection_cursor(double x0, double y0, double x1)
{
    unsigned long long ctx = FUN_80382654();
    aveditor_pulse_round(*(int *)(DAT_80612be4 + 0x2f18));
    cursor_draw_stretched(ctx, x0, y0, x1);
    FUN_803826a0();
}

// Compute the selection-cursor rect from the item rect and draw it.
void aveditor_draw_selection_cursor_rc(unsigned int a, unsigned int b, float *rect)
{
    (void)a; (void)b;
    aveditor_draw_selection_cursor(
        (double)rect[1],
        (double)(float)((double)FLOAT_8061a980 * (double)FLOAT_80611a68 + (double)rect[0]),
        (double)(float)((double)FLOAT_8061a984 * (double)FLOAT_80611a64 + (double)rect[1]));
}
