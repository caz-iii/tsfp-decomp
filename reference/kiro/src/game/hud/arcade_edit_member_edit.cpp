// Batch: batch_909 | Module: arcade map-editor member placement / preview grid / tool params
// Address range: 0x80322210 - 0x80322f04
// Date: 2026-09-12
//
// The member-edit layer of the arcade map editor: resolving companion segments across placed
// members, creating a member from a palette type, drawing the member preview grid, driving the
// preview panel, building the palette scroll list, and configuring per-category tool parameters.
//
//   aveditor_find_member_companion   0x80322210  scan placed members for a matching companion cell.
//   aveditor_place_selected_member   0x803223b0  create a member of the selected type at position.
//   aveditor_draw_member_preview     0x803224dc  draw the member preview grid (scaled lines + cells).
//   aveditor_update_preview_panel    0x80322840  refresh the preview panel when the type changes.
//   aveditor_screen_close_if_done    0x80322a3c  screen tick: finalize when all children are closing.
//   aveditor_build_palette_list      0x80322a68  build the palette scroll-list HUD element.
//   aveditor_find_matching_cell      0x80322c5c  find a cell of the same id in another member.
//   aveditor_copy_clamp_sign3        0x80322cac  copy a 3-int record, clamping field[2] to -1/0/1.
//   aveditor_set_tool_params_by_cat  0x80322d14  set editor tool params from the entry's category.
//   aveditor_set_current_edit_type   0x80322f04  set the current edit type + reset edit cursors.
//
// NON-MATCHING notes:
//  * 0x80322210 opens with the GPR-save runtime helper (bl 0x803826e0, restored by 0x8038272c);
//    Ghidra folds the incoming r3:r4 pair into that helper's fake return. Recovered ABI:
//    aveditor_find_member_companion(int *targetMember, int type) returning the matched member
//    pointer (r31) or 0. The 0x43300000 int->float magic-double idiom is written as (float)(int).
//  * 0x803224dc opens with the FP-register save helper (bl 0x803826d0, restored by 0x8038271c);
//    its many in_ps*/in_f* "locals" are the decompiler modelling the saved paired-single registers
//    as inputs. Recovered ABI: aveditor_draw_member_preview(int ctxHi, int item, float *rect,
//    int arg4, uint a5, uint a6, uint a7, int a8) where ctxHi/item/rect come from r3/r4/r5 and
//    a5..a8 are forwarded to aveditor_draw_member_grid. The phantom PS locals are dropped.
//  * 0x80322a68 opens with the GPR-save helper (bl 0x803826dc) and closes with 0x80382728; no
//    incoming register args (void), so the helper call is simply dropped.
//  * DAT_80612c14 is temporarily zeroed across the preview draw (save/restore of the active member
//    list head) so the grid renderer walks only the preview member.
//
// Named cross-module helpers are declared locally (see arcade_edit_options.cpp convention) with
// their real ABI; only common.h is pulled for shared types/intrinsics. Raw FUN_/DAT_/FLOAT_ get
// correctly-typed per-file externs below.

#include "common.h"

extern "C" {

// -- editor context + member list --
extern int   DAT_80612be4;    // editor context base
extern int  *DAT_80612c14;    // active member list head (next at +0x40, cell list at +0x3c)
extern int   DAT_80612c74;    // view transform: offset X (float via FLOAT_80612c74)
extern int   DAT_80612c84;    // view origin X
extern int   DAT_80612c88;    // view origin Y
extern float FLOAT_80612c54;  // view scale Y
extern float FLOAT_80612c58;  // view scale X
extern float FLOAT_80612c74;  // view offset X
extern float FLOAT_80612c78;  // view offset Y
extern int  *DAT_80612d68;    // preview panel state block (floats/ints by index)

// -- type geometry tables --
extern int   DAT_805d6038;    // type-record pointer array (index*4 -> record; +0x14 cols, +0x18 rows, +0x1c mode)
extern int   DAT_805d603c;    // (same table, alt access via typeIndex*4)
extern char  DAT_8047c158;    // per-type group byte: (&DAT_8047c158)[type*0x140]
extern short DAT_80477bca;    // per-type category id: (&DAT_80477bca)[type*0x62]

// -- palette group tables for the scroll list --
extern int   DAT_804808ec;    // mode-0 palette group table (0xc stride, count at +8)
extern int   DAT_8048088c;    // mode-1 palette group table (0xc stride, count at +8)

// -- misc globals --
extern int   DAT_806109c0;    // grid dimension A
extern int   DAT_806109c4;    // grid dimension B
extern int   DAT_806119e0;    // pos-y metric id

// -- float pool constants --
extern float FLOAT_8061a894;  // 0.5 inset ratio
extern float FLOAT_8061a8a4;  // text pad
extern float FLOAT_8061a8c0;  // padding metric / 0.0
extern float FLOAT_8061a8c4;  // preview grid inset ratio
extern float FLOAT_8061a8c8;  // preview grid cell divisor
extern float FLOAT_8061a8cc;  // preview panel base X
extern float FLOAT_8061a8d0;  // preview panel width
extern float FLOAT_8061a8d4;  // preview panel gap
extern float FLOAT_8061a8dc;  // scroll overflow base
extern float FLOAT_8061a8e0;  // scroll term A
extern float FLOAT_8061a8e4;  // scroll term B / tool param
extern float FLOAT_8061a8e8;  // scroll term C
extern float FLOAT_8061a8ec;  // list item metric X
extern float FLOAT_8061a8f0;  // list item metric Y
extern float FLOAT_8061a8f4;  // tool param float
extern float FLOAT_80611a5c;  // layout scale
extern float FLOAT_80611a64;  // layout unit
extern float FLOAT_80611a6c;  // layout base

// -- raw helpers (typed to match the call sites) --
extern int  aselgrid_find_companion_segment(int *member, int x, int y, int z, unsigned int a, unsigned int b);
extern int  aveditor_entity_in_active_chain(int entity);   // batch_908 (arcade_edit_palette_item.cpp)
extern int  aveditor_compute_placement_flags(void);
extern void aveditor_create_member(int typeIndex, int a2, int a3, int a4, float x, float y);
extern void avanim_play_move_sound(int soundId);
extern void screen_exit_clear_mode(void);
extern int  hud_element_is_closing(int element);
extern int  hud_screen_all_children_closing(int screen);
extern int  FUN_8033d0a0(int typeIndex, char mode);
extern void FUN_80340b98(int a);
extern void FUN_8033ced0(int a, int b, int c, int d);
extern void FUN_8033cf64(int a, int b, unsigned int c, int d, int e);
extern void FUN_8033ccdc(void);
extern void FUN_8033cff4(void);
extern void FUN_8033f224(void);
extern unsigned int color_scale_lo(double alpha, int color);
extern void gx_draw_fill_rect_f(unsigned int color, double x0, double y0, double x1);
extern void gx_draw_line_f(double x0, double y0, double x1, double y1, unsigned int color);
extern int  hud_item_is_focused(int item);
extern void aveditor_save_view_scale(void);
extern void aveditor_set_view_scale(double sx, double sy);
extern void aveditor_restore_view_scale(void);
extern void aveditor_draw_member_grid(double a1, double a2, double a3, double a4, double a5,
                                      double f6, double f7, double f8, int record, int flags,
                                      float *scratch, int z, unsigned int a5u, unsigned int a6u,
                                      unsigned int a7u, int a8);
extern void get_string_by_id(int stringId);
extern void hud_element_init_from_layout(int layout, int a, int b, void (*cb)(void));
extern void hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void hud_set_context_property(int prop);
extern void hud_element_scroll_h_overflow(double overflow, double a, double b, int c);
extern void hud_begin_scroll_region(void);
extern void hud_begin_sub_list(int a, int b, int c);
extern void hud_add_item_internal(double x, double y);
extern void hud_end_sub_list(void);
extern void hud_end_scroll_region(void);
extern void hud_add_padding_item(double pad);
extern void hud_element_apply_scroll(void);
extern void hud_element_set_pos_y(double y, int metric);
extern void hud_element_finalize_frame(void);

} // extern "C"

// forward prototypes for this file's own functions used before definition
int *aveditor_find_matching_cell(int *cell);
void aveditor_copy_clamp_sign3(unsigned int *dst, unsigned int *src);

// -----------------------------------------------------------------------------

// Scan every placed member for a cell whose tag byte matches `type`, resolving its companion
// segment. When the target member is itself in the active chain, a transient "editing" flag
// (context+0x25c bit 0x10) is set for the lookup. Returns after the first member whose companion
// resolves within range. (NON-MATCHING: GPR-save spill dropped; r3:r4 recovered as params.)
int aveditor_find_member_companion(int *targetMember, int type)
{
    int result = 0;
    int *member = DAT_80612c14;
    if (targetMember == DAT_80612c14) {
        member = (int *)DAT_80612c14[0x10];
    }

    for (; member != (int *)0x0; member = (int *)member[0x10]) {
        int cellOff = 0;
        int row = member[(int)member[3] + 0xb];
        for (int i = 0; i < *(int *)(row + 0x44); i++) {
            unsigned char *cell = (unsigned char *)(row + cellOff + 0x48);
            if ((unsigned int)*cell == (unsigned int)type) {
                if (aveditor_entity_in_active_chain((int)targetMember) != 0) {
                    *(unsigned int *)(DAT_80612be4 + 0x25c) |= 0x10;
                }
                int z = (int)(member[2] + (float)(int)cell[3]);
                int seg = aselgrid_find_companion_segment(targetMember, (int)member[0],
                                                          (int)member[1], z,
                                                          (unsigned int)*cell,
                                                          (unsigned int)cell[4]);
                int delta = (int)(member[2] - (float)(int)*(int *)(DAT_80612be4 + 0x264));
                int mag = (delta >> 0x1f);
                result = -1;
                if (((mag ^ delta) - mag) < 2) {
                    result = seg;
                }
                if ((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x10) != 0) {
                    *(unsigned int *)(DAT_80612be4 + 0x25c) &= 0xffffffef;
                }
            }
            cellOff += 6;
        }
        if (result != -1) {
            break;
        }
    }
    return result;
}

// Create a member of the currently-selected type (context+0x188) at the type's default cell
// position, then make it the active edit target, play the place sound, and leave clear mode.
void aveditor_place_selected_member(int screen)
{
    int typeIndex = *(int *)(screen + 0x188);
    if (typeIndex < 0) {
        return;
    }
    int rec = *(int *)(&DAT_805d6038 + typeIndex * 4);
    float x = (float)(-*(int *)(rec + 0x14)) * FLOAT_8061a894;
    float y = (float)(-*(int *)(rec + 0x18)) * FLOAT_8061a894;

    int flags = aveditor_compute_placement_flags();
    int group = (unsigned char)(&DAT_8047c158)[typeIndex * 0x140];
    int slot = *(int *)(*(int *)(*(int *)(DAT_80612be4 + group * 4 + 0x4064) + 4) + 0x30) - flags;

    aveditor_create_member(typeIndex, (int)FLOAT_8061a8c0, 0, slot, x, y);
    *(int *)(DAT_80612be4 + 0x2f7c) = typeIndex;
    avanim_play_move_sound(0x157c);
    screen_exit_clear_mode();
}

// Draw the member preview grid: a scaled line grid sized to the type's cell extents, the member's
// cells, and (unfocused, mode-2 types) a second ghost grid. The active member list head is
// temporarily cleared so only the preview member is walked.
// (NON-MATCHING: FP-save spill dropped; ctxHi/item/rect recovered from r3/r4/r5; a5..a8 forwarded.)
void aveditor_draw_member_preview(int ctxHi, int item, float *rect, int arg4,
                                  unsigned int a5, unsigned int a6, unsigned int a7, int a8)
{
    float width  = rect[2] - rect[0];
    float height = rect[3] - rect[1];
    float insetW = FLOAT_8061a8c4 * width;
    float insetH = FLOAT_8061a8c4 * height;
    float cellX  = insetW / FLOAT_8061a8c8;
    float cellY  = insetH / FLOAT_8061a8c8;
    int   type4  = *(int *)(item + 0x14) * 4;

    int focus = hud_item_is_focused(item);
    int *savedList = DAT_80612c14;
    DAT_80612c14 = 0;

    float x0 = FLOAT_8061a894 * (width  - insetW) + rect[0];
    float y0 = FLOAT_8061a894 * (height - insetH) + rect[1];
    unsigned int fill = color_scale_lo((double)*(float *)(ctxHi + 0xf0), 0xa0a0a20);
    gx_draw_fill_rect_f(fill, (double)x0, (double)y0, (double)(x0 + insetW));

    int rec = *(int *)(&DAT_805d6038 + type4);
    int cols = *(int *)(rec + 0x14); if (cols < 3) cols = 3;
    int rows = *(int *)(rec + 0x18); if (rows < 3) rows = 3;

    unsigned int line = color_scale_lo((double)*(float *)(ctxHi + 0xf0), 0x3c50643f);

    // vertical grid lines
    float gy1 = (float)cols * cellY + y0;
    float gx  = x0;
    for (int i = 0; i <= cols; i++) {
        gx_draw_line_f((double)gx, (double)y0, (double)gx, (double)gy1, line);
        gx = gx + cellX;
    }
    // horizontal grid lines
    float gx1 = (float)rows * cellX + x0;
    float gy  = y0;
    for (int i = 0; i <= rows; i++) {
        gx_draw_line_f((double)x0, (double)gy, (double)gx1, (double)gy, line);
        gy = gy + cellY;
    }

    aveditor_save_view_scale();
    aveditor_set_view_scale((double)cellX, (double)cellY);

    float scratch[2];
    double px = FLOAT_80612c58 * (x0 - (float)DAT_80612c88) + FLOAT_80612c78;
    double py = (double)FLOAT_80612c54 * (double)(y0 - (float)DAT_80612c84) + (double)FLOAT_80612c74;
    scratch[0] = (float)(-*(int *)(DAT_80612be4 + 0x264));
    int flags = (focus != 0) ? 8 : 10;

    aveditor_draw_member_grid(px, py, (double)*(float *)(ctxHi + 0xf0), (double)FLOAT_80612c54,
                              0.0, 0.0, 0.0, 0.0, *(int *)(&DAT_805d6038 + type4), flags,
                              scratch, 0, a5, a6, a7, a8);

    if ((*(int *)(rec + 0x1c) == 2) && (focus == 0)) {
        scratch[0] = (float)(*(int *)(DAT_80612be4 + 0x264) - 1);
        aveditor_draw_member_grid(px, py, (double)*(float *)(ctxHi + 0xf0), (double)FLOAT_80612c54,
                                  0.0, 0.0, 0.0, 0.0, *(int *)(&DAT_805d6038 + type4), 9,
                                  scratch, 0, a5, a6, a7, a8);
    }

    if ((*(unsigned int *)(item + 0x28) & 1) != 0) {
        unsigned int sel = color_scale_lo((double)*(float *)(ctxHi + 0xf0), 0x32323248);
        gx_draw_fill_rect_f(sel, (double)rect[0], (double)rect[1], (double)rect[2]);
    }

    aveditor_restore_view_scale();
    DAT_80612c14 = savedList;
}

// Refresh the preview panel when the selected type changes: initializes the preview state block,
// rebuilds its geometry from the type, and (on an actual type change) plays a sound + rebuilds the
// preview member with the placement flags.
void aveditor_update_preview_panel(int screen)
{
    if (hud_element_is_closing(screen) != 0) {
        return;
    }
    int type = *(int *)(screen + 0x188);
    if (type < 0) {
        return;
    }

    *DAT_80612d68 = 1;
    DAT_80612d68[0xe] = (int)(float)FUN_8033d0a0(type, '\x02');
    DAT_80612d68[0x20] = 4;
    DAT_80612d68[0x20] = DAT_80612d68[0x20] | 0x50;
    FUN_80340b98(0);

    if (type != DAT_80612d68[0x24]) {
        avanim_play_move_sound(0x157e);
        DAT_80612d68[0x24] = type;

        float baseX = FLOAT_8061a8cc * FLOAT_80611a64 + FLOAT_80611a6c;
        float span  = (FLOAT_8061a8d0 * FLOAT_80611a64 + FLOAT_80611a6c) - baseX - FLOAT_8061a8d4;
        int halfB = (DAT_806109c4 >> 1) - 1;
        FUN_8033ced0((DAT_806109c4 - (halfB >> 1)) + 3,
                     (int)(FLOAT_8061a8a4 + span * FLOAT_8061a894 + baseX), 0xb4, (int)span);

        baseX = FLOAT_8061a8cc * FLOAT_80611a64 + FLOAT_80611a6c;
        halfB = (DAT_806109c4 >> 1) - 1;
        span  = (FLOAT_8061a8d0 * FLOAT_80611a64 + FLOAT_80611a6c) - baseX - FLOAT_8061a8d4;
        FUN_8033cf64((DAT_806109c4 - (halfB >> 1)) + 3,
                     (int)(FLOAT_8061a8a4 + span * FLOAT_8061a894 + baseX),
                     (unsigned int)halfB, (int)span, (DAT_806109c0 >> 3));
        FUN_8033ccdc();

        int group = (unsigned char)(&DAT_8047c158)[type * 0x140];
        DAT_80612d68[0x21] = *(int *)(DAT_80612be4 + group * 4 + 0x4064);
        int flags = aveditor_compute_placement_flags();
        *(int *)(*(int *)(DAT_80612d68[0x21] + 4) + 0x30) = flags;
        *(unsigned int *)(*(int *)(DAT_80612d68[0x21] + 4) + 0x30) |= 0x2000000;
    }
    FUN_8033cff4();
}

// Screen tick: when all child elements are closing, finalize/tear down the editor screen.
void aveditor_screen_close_if_done(int screen)
{
    if (hud_screen_all_children_closing(screen) != 0) {
        FUN_8033f224();
    }
}

// Build the palette scroll-list HUD element: one sub-list per palette group (mode selects the
// group table), one item per entry in the group, wrapped in scroll regions with padding.
// (NON-MATCHING: leading GPR-save spill helper dropped; no incoming args.)
void aveditor_build_palette_list(void)
{
    int groupCount = 3;
    if (*(int *)(DAT_80612be4 + 0x200) != 0) {
        groupCount = 8;
    }

    hud_element_init_from_layout(0x840c, 0, 0, screen_exit_clear_mode);
    hud_set_item_padding_mode((double)FLOAT_8061a8c0, 0, 1, 1);
    hud_set_context_property(0x28000);

    float scrollTerms = (FLOAT_8061a8e0 * FLOAT_80611a64 + FLOAT_8061a8e4 * FLOAT_80611a64 +
                         FLOAT_8061a8e8 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c;
    hud_element_scroll_h_overflow(-(double)(FLOAT_80611a5c * scrollTerms - FLOAT_8061a8dc),
                                  (double)FLOAT_8061a8c0, (double)FLOAT_8061a8c0, 0);

    int groupOff = 0;
    for (int g = 0; g < groupCount; g++) {
        int *table = (*(int *)(DAT_80612be4 + 0x200) == 0) ? &DAT_804808ec : &DAT_8048088c;
        int *group = (int *)((int)table + groupOff);
        get_string_by_id(group[0]);
        hud_begin_scroll_region();
        hud_begin_sub_list(1, 1, 4);
        for (int i = 0; i < group[2]; i++) {
            hud_add_item_internal((double)FLOAT_8061a8ec, (double)FLOAT_8061a8f0);
        }
        hud_end_sub_list();
        hud_end_scroll_region();
        hud_add_padding_item((double)FLOAT_8061a8c0);
        groupOff += 0xc;
    }

    hud_element_apply_scroll();
    hud_element_set_pos_y((double)(FLOAT_80611a5c * scrollTerms), DAT_806119e0);
    hud_element_finalize_frame();
}

// Find a cell in another member whose id (word 0) matches `cell`'s id (excluding `cell` itself).
// Walks every active member's cell list. Returns the matching cell or 0.
int *aveditor_find_matching_cell(int *cell)
{
    int member = (int)DAT_80612c14;
    while (member != 0) {
        for (int *c = *(int **)(member + 0x3c); c != (int *)0x0; c = (int *)c[5]) {
            if ((*c == *cell) && (c != cell)) {
                return c;
            }
        }
        member = *(int *)(member + 0x40);
    }
    return (int *)0x0;
}

// Copy a 3-int record; the third field is clamped to its sign: >1 -> -1 (0xffffffff), <-1 -> -1,
// 0 -> 1, in [-1,1] -> 0. (Matches the branchy clamp in the original.)
void aveditor_copy_clamp_sign3(unsigned int *dst, unsigned int *src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    int v = (int)src[2];
    if (v == 0) {
        dst[2] = 1;
    } else if (v < 0) {
        if (v >= -1) {
            dst[2] = 0xffffffff;
        } else {
            dst[2] = 0xffffffff;
        }
    } else if (v < 2) {
        dst[2] = 0;
    } else {
        dst[2] = 0xffffffff;
    }
}

// Configure the editor tool parameters (context+0x2f4c..+0x2f6c) from the current entry's category
// (context+0x2f34 indexes the category table). Each category sets a different parameter layout;
// the "link" categories (0xe..0x11) also copy the sign-clamped delta from a matching cell.
void aveditor_set_tool_params_by_cat(void)
{
    float paramF = FLOAT_8061a8f4;
    int type = *(int *)(DAT_80612be4 + 0x2f34);
    switch ((&DAT_80477bca)[type * 0x62]) {
    case 1:
    case 2:
        *(int *)(DAT_80612be4 + 0x2f4c) = 1;
        break;
    case 8:
    case 9:
    case 0x13:
    case 0x14:
        *(int *)(DAT_80612be4 + 0x2f4c) = 0;
        *(int *)(DAT_80612be4 + 0x2f50) = 0x19;
        break;
    case 0xe:
    case 0xf:
    case 0x10:
    case 0x11: {
        *(int *)(DAT_80612be4 + 0x2f50) = -1;
        *(int *)(DAT_80612be4 + 0x2f4c) = -1;
        *(int *)(DAT_80612be4 + 0x2f54) = -1;
        int *match = aveditor_find_matching_cell((int *)(DAT_80612be4 + 0x2f34));
        if (match != (int *)0x0) {
            aveditor_copy_clamp_sign3((unsigned int *)(DAT_80612be4 + 0x2f4c),
                                      (unsigned int *)(match + 6));
        }
        break;
    }
    case 0x15:
        *(int *)(DAT_80612be4 + 0x2f4c) = 0;
        *(int *)(DAT_80612be4 + 0x2f50) = 0x3c;
        break;
    case 0x18:
    case 0x19:
        if (*(int *)(DAT_80612be4 + 0x2f34) == 0x43) {
            *(int *)(DAT_80612be4 + 0x2f4c) = 0;
            *(int *)(DAT_80612be4 + 0x2f54) = 0;
            *(int *)(DAT_80612be4 + 0x2f58) = 0;
            *(int *)(DAT_80612be4 + 0x2f5c) = 3;
            *(int *)(DAT_80612be4 + 0x2f60) = 3;
            *(int *)(DAT_80612be4 + 0x2f50) = 0;
        }
        break;
    case 0x1b:
    case 0x1c:
        *(float *)(DAT_80612be4 + 0x2f4c) = FLOAT_8061a8e4;
        *(float *)(DAT_80612be4 + 0x2f50) = paramF;
        *(int *)(DAT_80612be4 + 0x2f58) = 0;
        *(int *)(DAT_80612be4 + 0x2f5c) = 0;
        *(int *)(DAT_80612be4 + 0x2f54) = 0x2a;
        *(int *)(DAT_80612be4 + 0x2f60) = 0xc;
        *(int *)(DAT_80612be4 + 0x2f68) = 1;
        *(unsigned char *)(DAT_80612be4 + 0x2f6c) = 0;
        break;
    case 0x1d: {
        *(float *)(DAT_80612be4 + 0x2f4c) = FLOAT_8061a894;
        int t = *(int *)(DAT_80612be4 + 0x2f34);
        if (t == 0x33) {
            *(int *)(DAT_80612be4 + 0x2f50) = 0x10;
        } else if (t < 0x33) {
            if (t < 0x32) {
                *(int *)(DAT_80612be4 + 0x2f50) = 0x23;
            } else {
                *(int *)(DAT_80612be4 + 0x2f50) = 0x2a;
            }
        } else {
            if (t > 0x34) {
                *(int *)(DAT_80612be4 + 0x2f50) = 0x23;
            } else {
                *(int *)(DAT_80612be4 + 0x2f50) = 0xd;
            }
        }
        *(int *)(DAT_80612be4 + 0x2f54) = 0;
        break;
    }
    case 0x22:
        *(int *)(DAT_80612be4 + 0x2f54) = 0;
        break;
    default:
        break;
    }
}

// Set the current edit type (context+0x2f34) from the screen's selected type (+0x188) and reset
// the edit sub-cursors.
void aveditor_set_current_edit_type(int screen)
{
    *(int *)(DAT_80612be4 + 0x2f34) = *(int *)(screen + 0x188);
    *(int *)(DAT_80612be4 + 0x2f3c) = 0;
    *(int *)(DAT_80612be4 + 0x2f38) = -1;
}
