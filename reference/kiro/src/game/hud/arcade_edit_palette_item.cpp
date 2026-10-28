// Batch: batch_908 | Module: arcade map-editor palette-item render + placement queries
// Address range: 0x80321a58 - 0x803220c8
// Date: 2026-09-12
//
// The palette-item layer of the arcade map editor: it draws one loadout/palette entry (icon
// sprite + border + placed-count text) and answers the "is this thing placeable / how many are
// placed" queries the palette UI needs. Two clusters:
//
//   render:
//     aveditor_palette_item_texture_root  0x80321a58  accessor: base of the palette-item texture set.
//     aveditor_palette_item_texture_id    0x80321a64  pick cursor vs sprite texture id for an entry.
//     aveditor_draw_palette_item          0x80321ba8  draw one palette item (border + icon + count).
//     aveditor_render_palette_entry       0x80321ea8  render a palette entry (color/focus + overlay).
//   queries:
//     aveditor_group_index_of_type        0x80321ab0  1-based index of a type within a grouped table.
//     aveditor_layout_index_of_type       0x80321b30  group-index lookup keyed by the editor mode.
//     aveditor_count_type_instances       0x80321fa0  count list+grid instances of an entry type.
//     aveditor_type_in_placed_set         0x8032204c  is an entry's id in the "placed" id set.
//     aveditor_entity_in_active_chain     0x80322098  is an entity linked in the active chain.
//     aveditor_grid_scan_type             0x803220c8  recursive grid scan for a matching cell.
//
// NON-MATCHING notes:
//  * 0x80321ba8 / 0x80321ea8 / 0x803220c8 open with the FP-register save runtime helpers
//    (bl 0x803826dc = _savef25.., bl 0x803826e4 = _savef31, restored by 0x80382728/0x80382730).
//    Ghidra models those spill helpers as calls returning a longlong and folds the function's
//    incoming r3:r4 register pair into that fake return. The real ABI, recovered from the
//    disassembly, is expressed as explicit parameters here (Compile-Clean rules 1/3) and the
//    spill-helper "calls" are dropped:
//      - aveditor_draw_palette_item(int mode, float* rect, double alpha, int material,
//                                   uint iconColor, uint countColor, int count)
//        r27=mode(r3) r26=rect(r4) f31=alpha(f1) r29=material(r5) r30=iconColor(r6)
//        r25=countColor(r7) r31=count(r8).  The decompiler's "lVar23 = FUN_803826dc()" is the
//        incoming r3:r4 pair, and "(lVar23>>0x20)==2" is really mode==2.
//      - aveditor_render_palette_entry(int ctxHi, int entry, float* rect)  r26=r3 r27=r4 r28=r5.
//      - aveditor_grid_scan_type(int context, int type, float coord)  r30=r3 r31=r4, returns a
//        matching cell pointer (r26) or 0 (decompiler mistyped the return as void).
//  * The int->float 0x43300000 magic-double idiom in aveditor_grid_scan_type is written as a plain
//    (float)(int) cast.
//  * aveditor_layout_index_of_type calls aveditor_group_index_of_type with the two grouped-table
//    bases as the raw negative sign-extended addresses the loader assigns (-0x7fb7f614 =
//    0x80480 9ec region); kept as (int) constants matching the disassembly's lis/addi pair.
//
// Named cross-module helpers are declared locally below (not via hud.h/generated_decls.h): the
// generated prototypes for sprite_draw_bordered_frame (3-arg) and debug_log_formatted (void
// return) are simplified stubs that disagree with the real ABI used at these call sites, so this
// file follows arcade_edit_options.cpp's convention of declaring the draw helpers with their true
// signatures and pulling only common.h for shared types/intrinsics. Raw FUN_/DAT_/FLOAT_ get
// correctly-typed per-file externs below.

#include "common.h"

extern "C" {

// -- named cross-module helpers (real ABI, matching the disassembly) --
extern int  cursor_get_texture_id(int spriteId);
extern int  sprite_get_texture_id(int spriteId);
extern unsigned int color_scale_lo(double alpha, int color);
extern void color_to_rgb_via_hsv(unsigned int color, float *hsvOut);
extern unsigned int hsv_to_color_packed(float *hsv);
extern int  hud_item_is_focused(int item);
extern void gx_draw_fill_rect_f(unsigned int color, double x0, double y0, double x1);
extern void gx_set_active_material(int material);
extern void gx_draw_sprite_rect(int x0, int y0, unsigned int x1, unsigned int y1,
                                int u0, int v0, unsigned int u1, unsigned int v1);
extern void gx_fill_rect_clipped(int x0, int y0, int x1, int y1, unsigned int color);
extern void sprite_draw_bordered_frame(int x0, int y0, int x1, int y1, int blend);
extern void *debug_log_formatted(void *fmt, int value);   // sprintf-like; returns the buffer
extern int  text_set_font_table(int table);
extern float text_get_char_width(unsigned char *text);
extern void text_draw_char(double x, double y, unsigned char *text, unsigned int color);
extern unsigned int aveditor_count_placed_for_type(int type);
extern int  aveditor_grid_lookup(unsigned int cellType, float y, int coord);


// -- palette entry table (0xc4-byte stride; flags word carries bit 0x400 = "cursor" variant) --
extern int  DAT_80477c7c;   // palette entry record base: sprite/cursor id at +0x0 (stride 0xc4)
extern int  DAT_80477c80;   // palette entry flags base   (stride 0xc4, matched to DAT_80477c7c-4)
extern int  DAT_80477bcc;   // palette entry color base   (stride 0xc4)

// -- palette-item texture set root --
extern int  DAT_80480b94;   // base of the palette-item texture descriptor set

// -- editor context + placed-id set --
extern int  DAT_80612be4;   // editor context base (mode word at +0x200, grid list at +0x280/+0x2ec,
                            //   active chain head at +0x2f7c)
extern int  DAT_804808c4;   // placed-id set count
extern int  DAT_80480848;   // placed-id set array (int ids)

// -- draw geometry constants --
extern int  DAT_80610a20;   // -> material table base (deref: *DAT_80610a20 + material*0x58)
extern int  DAT_806111f4;   // count-text format string
extern float FLOAT_8061a890; // half           (0.5)
extern float FLOAT_8061a894; // inset ratio
extern float FLOAT_8061a898; // icon width ratio
extern float FLOAT_8061a89c; // border inset A
extern float FLOAT_8061a8a0; // border inset B
extern float FLOAT_8061a8a4; // count-text right pad

// -- raw helpers not yet named --
extern int  FUN_8034224c(int cell);   // companion/segment predicate (nonzero = present)

} // extern "C"

// forward prototypes for this file's own functions used before definition
int aveditor_palette_item_texture_id(int type);
int aveditor_group_index_of_type(int table, int groupCount, int type);
void aveditor_draw_palette_item(int mode, float *rect, double alpha, int material,
                                unsigned int iconColor, unsigned int countColor, int count);
int aveditor_grid_scan_type(int context, int type, float coord);

// -----------------------------------------------------------------------------
// render cluster
// -----------------------------------------------------------------------------

// Accessor: base of the palette-item texture descriptor set.
int *aveditor_palette_item_texture_root(void)
{
    return &DAT_80480b94;
}

// Pick the texture id for a palette entry: entries flagged 0x400 use the cursor texture set,
// everything else uses the sprite texture set. Returns the resolved texture id.
int aveditor_palette_item_texture_id(int type)
{
    int spriteId = *(int *)(&DAT_80477c7c + type * 0xc4);
    if (((&DAT_80477c80)[type * 0x31] & 0x400) == 0) {
        return cursor_get_texture_id(spriteId);
    }
    return sprite_get_texture_id(spriteId);
}

// Draw one palette item: fills the entry background, strokes a bordered frame, blits the icon
// sprite, and (when count != 0) draws the placed-count number in the corner.
// mode selects the fill orientation (2 = horizontal fill, else inset fill).
void aveditor_draw_palette_item(int mode, float *rect, double alpha, int material,
                                unsigned int iconColor, unsigned int countColor, int count)
{
    float left   = rect[0];
    float top    = rect[1];
    float width  = rect[2] - rect[0];
    float height = rect[3] - rect[1];
    float insetW = FLOAT_8061a890 * width;
    float insetH = FLOAT_8061a890 * height;

    color_scale_lo(alpha, 0x3c50643f);

    float x0 = FLOAT_8061a894 * (width  - insetW) + left;
    float y0 = FLOAT_8061a894 * (height - insetH) + top;

    unsigned int fill;
    if (mode == 2) {
        fill = color_scale_lo(alpha, countColor);
        gx_draw_fill_rect_f(fill, (double)left, (double)top,
                            (double)(left + FLOAT_8061a898 * width));
    } else {
        fill = color_scale_lo(alpha, countColor);
        gx_draw_fill_rect_f(fill, (double)x0, (double)y0,
                            (double)(x0 + insetW));
    }

    unsigned int borderColor;
    if (mode > 0) {
        borderColor = color_scale_lo(alpha, 0xa28787f);
    } else {
        borderColor = color_scale_lo(alpha, 0x5143c7f);
    }
    color_scale_lo(alpha, 0x1414147f);

    float right  = x0 + insetW;
    float bottom = y0 + insetH;
    int ix0 = (int)x0;
    int iy0 = (int)y0;
    unsigned int ix1 = (unsigned int)right;
    unsigned int iy1 = (unsigned int)bottom;

    sprite_draw_bordered_frame(ix0, iy0, ix1, iy1, 0);
    gx_set_active_material(material);
    color_scale_lo(alpha, iconColor);

    int mat = *(int *)DAT_80610a20 + material * 0x58;
    unsigned int uMax = (unsigned int)*(unsigned short *)(mat + 0x4e) << 4;
    unsigned int vMax = (unsigned int)*(unsigned short *)(mat + 0x52) << 4;
    gx_draw_sprite_rect(ix0, iy0, ix1, iy1, 0, 0, uMax, vMax);

    if (count != 0) {
        unsigned char *text = (unsigned char *)debug_log_formatted(&DAT_806111f4, count);
        gx_fill_rect_clipped((int)(right  - FLOAT_8061a89c), (int)(bottom - FLOAT_8061a89c),
                             (int)(right  - FLOAT_8061a8a0), (int)(bottom - FLOAT_8061a8a0),
                             borderColor);
        int savedFont = text_set_font_table(0xf);
        float textW = text_get_char_width(text);
        float textX = (right - FLOAT_8061a8a4) - textW;
        float textY = bottom - FLOAT_8061a89c;
        unsigned int textColor = color_scale_lo(alpha, 0x7f7f7f80);
        text_draw_char(textX, textY, text, textColor);
        text_set_font_table(savedFont);
    }
}

// Render a palette entry: derives its display color (desaturated when not focused), resolves the
// icon texture + placed count, draws it, and paints a selection overlay when the entry is active.
// ctxHi carries the alpha float at +0xf0; entry has type at +0x14 and flags at +0x28.
void aveditor_render_palette_entry(int ctxHi, int entry, float *rect)
{
    float hsv[3];

    int type = *(int *)(entry + 0x14);
    unsigned int color = *(unsigned int *)(&DAT_80477bcc + type * 0xc4);

    if (hud_item_is_focused(entry) == 0) {
        color_to_rgb_via_hsv(color, hsv);
        hsv[2] = hsv[2] * FLOAT_8061a894;   // dim the value channel
        color = hsv_to_color_packed(hsv);
    }

    unsigned int count = aveditor_count_placed_for_type(type);
    int material = aveditor_palette_item_texture_id(type);
    int focus = hud_item_is_focused(entry);
    aveditor_draw_palette_item(focus, rect, (double)*(float *)(ctxHi + 0xf0),
                               material, color, 0xa0a0a20, count);

    if ((*(unsigned int *)(entry + 0x28) & 1) != 0) {
        unsigned int sel = color_scale_lo((double)*(float *)(ctxHi + 0xf0), 0x32323248);
        gx_draw_fill_rect_f(sel, (double)rect[0], (double)rect[1], (double)rect[2]);
    }
}

// -----------------------------------------------------------------------------
// query cluster
// -----------------------------------------------------------------------------

// Return the 1-based flat index of `type` within a grouped id table. The table is `groupCount`
// records of {?, int* ids, int idCount} (0xc-byte stride); the running base accumulates group
// sizes so the returned index is global across groups. Returns the next free index if not found.
int aveditor_group_index_of_type(int table, int groupCount, int type)
{
    int base = 1;
    for (int group = 0; group < groupCount; group++) {
        int *ids = *(int **)(table + group * 0xc + 4);
        int idCount = *(int *)(table + group * 0xc + 8);
        for (int i = 0; i < idCount; i++) {
            if (type == ids[i]) {
                return base + i;
            }
        }
        base += idCount;
    }
    return base;
}

// Group-index lookup keyed by the current editor mode (context+0x200): mode 1 uses the 0x11-group
// table, modes 0..? use the 9-group table; other modes have no palette (returns 0).
int aveditor_layout_index_of_type(int type)
{
    int mode = *(int *)(DAT_80612be4 + 0x200);
    if (mode == 1) {
        return aveditor_group_index_of_type((int)0x804809ec, 0x11, type);
    }
    if (mode >= 0 && mode < 1) {
        return aveditor_group_index_of_type((int)0x80480ab8, 9, type);
    }
    return 0;
}

// Count how many instances of an entry `type` exist. Walks the entry's own linked list
// (entry+0x3c, chained by word index 5): counts all of them when type == -1, otherwise counts
// nodes whose id matches. Then adds matching slots from the editor grid (context+0x2ec, 8 slots of
// 0x10 bytes per row, 0xe4-byte row stride) when counting all.
int aveditor_count_type_instances(int entry, int type)
{
    int *node = *(int **)(entry + 0x3c);
    int total = 0;

    if (type == -1) {
        for (; node != (int *)0x0; node = (int *)node[5]) {
            total = total + 1;
        }
    } else {
        while (node != (int *)0x0) {
            int id = *node;
            node = (int *)node[5];
            if (id == type) {
                total = total + 1;
            }
        }
    }

    int rowOff = 0;
    for (int row = 0; row < *(int *)(DAT_80612be4 + 0x280); row++) {
        int slotOff = 0;
        for (int slot = 0; slot < 8; slot++) {
            if ((*(int *)(DAT_80612be4 + rowOff + slotOff + 0x2ec) == entry) && (type == -1)) {
                total = total + 1;
            }
            slotOff += 0x10;
        }
        rowOff += 0xe4;
    }
    return total;
}

// Is an entry's id (entry+0x10) present in the "placed" id set (DAT_80480848[DAT_804808c4])?
int aveditor_type_in_placed_set(int entry)
{
    int id = *(int *)(entry + 0x10);
    int count = DAT_804808c4;
    for (int i = 0; count > 0; count--) {
        if (id == *(int *)((int)&DAT_80480848 + i)) {
            return 1;
        }
        i += 4;
    }
    return 0;
}

// Is `entity` linked anywhere in the active chain (head at context+0x2f7c, next at +0x40)?
int aveditor_entity_in_active_chain(int entity)
{
    int node = *(int *)(DAT_80612be4 + 0x2f7c);
    while (node != 0) {
        if (entity == node) {
            return 1;
        }
        node = *(int *)(node + 0x40);
    }
    return 0;
}

// Recursively scan the editor grid for a cell matching `type`. `context` selects the grid row
// (context + row*4 + 0x2c) whose cell array (+0x48, 6-byte stride, count at +0x44) is walked; each
// cell's tag byte (+0x0) must equal type, then aveditor_grid_lookup validates the cell at the
// computed coord. Types 5/6 also gate on FUN_8034224c (companion-segment present); type 6 skips a
// match when a companion exists, type 5 requires one. On a validated match it recurses to continue
// the chain and returns the matched cell pointer (or 0 when nothing matches).
int aveditor_grid_scan_type(int context, int type, float coord)
{
    float bias = (type == 6) ? FLOAT_8061a898 : FLOAT_8061a890; // -0xd38 vs -0xd40 pool floats
    int row = *(int *)(context + *(int *)(context + 0xc) * 4 + 0x2c);

    int index = 0;
    int cellOff = 0;
    while (index < *(int *)(row + 0x44)) {
        unsigned char *cell = (unsigned char *)(row + cellOff + 0x48);
        if (*cell == (unsigned int)type) {
            float y = (float)(int)cell[3] + bias + (float)*(float *)(context + 8);
            int match = aveditor_grid_lookup((unsigned int)cell[3], y, (int)coord);
            if (match != 0) {
                int hit = 0;
                if (type == 6) {
                    if (FUN_8034224c(match) != 0) {
                        return match;
                    }
                } else if (type < 6 && type >= 5) {
                    hit = (FUN_8034224c(match) == 0) ? 0 : 1;
                }
                if (hit != 0) {
                    if (aveditor_grid_scan_type(match, type, coord) != 0) {
                        return match;
                    }
                    return match;
                }
            }
        }
        index++;
        cellOff += 6;
    }
    return 0;
}
