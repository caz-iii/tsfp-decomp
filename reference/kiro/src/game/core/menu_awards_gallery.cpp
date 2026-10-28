// Address: 0x802dd86c | Name: menu_awards_gallery | Date: 2026-09-11T09:10:00Z
//
// Front-end awards / medals gallery screens (batch_825), continuation of the
// menu family from batch_817-824. Contains:
//   - the "earned awards" counter + scrollable text list (0x3b award slots),
//   - the award grid: per-cell sprite render (locked placeholder vs unlocked
//     icon), an animated flip/scale variant, and the whole-grid render that
//     walks the HUD item list drawing each cell and a detail popup for the
//     focused award, and
//   - the award-detail popup screen (weapon icon + medal-time frame corners)
//     with its builder and pop handler.
//
// Heavy floating-point placement uses the PowerPC int->float magic-bias idiom
// (CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_80619f00), which equals the
// signed int->double conversion of x; simplified to (double)(int)x where the
// result is numerically identical. Non-matching translation.
//
// Address range: 0x802dd86c - 0x802deb5c

#include "common.h"

// ---- Per-file externs -------------------------------------------------------

extern int    DAT_805cc61c;       // active savegame slot
extern int    DAT_805cc6e4;       // grid animation phase
extern int    DAT_80612020;       // popup animation counter
extern int    DAT_80612244;       // detail zoom flag
extern int    DAT_80612ad8;       // selected award/weapon index
extern int    DAT_806109c0;       // record-scale reference
extern int   *DAT_806119e0;       // layout centering context
extern int   *DAT_80610a20;       // sprite material record table base

extern u8     DAT_80474680;       // award/map descriptor table (stride 0xc int view, 0x30 byte view)
extern u8     DAT_8047467d;       // ^ byte field within descriptor
extern u8     DAT_80474684;       // ^ short label field
extern u8     DAT_80474688;       // ^ unlocked sprite id field
extern u8     DAT_8047468c;       // ^ locked sprite id field
extern u8     DAT_804659d8;       // earned-award label-id table (stride 2)
extern float  DAT_80583228;       // trig/curve lookup table (0x800 entries)

extern u8     DAT_80611090;       // "all earned" fmt string
extern u8     DAT_80611098;       // "N earned" fmt string

extern float  FLOAT_8060f958;
extern float  FLOAT_80611a5c;
extern float  FLOAT_80611a64;
extern float  FLOAT_80611a68;
extern float  FLOAT_80619ee8;
extern float  FLOAT_80619eec;
extern float  FLOAT_80619ef0;
extern float  FLOAT_80619f08;
extern float  FLOAT_80619f0c;
extern float  FLOAT_80619f18;
extern float  FLOAT_80619f38;
extern float  FLOAT_80619f3c;
extern float  FLOAT_80619f4c;
extern float  FLOAT_80619f50;
extern float  FLOAT_80619f60;
extern float  FLOAT_80619f6c;
extern float  FLOAT_80619f74;
extern float  FLOAT_80619f84;
extern float  FLOAT_80619fc4;
extern float  FLOAT_80619fe0;
extern float  FLOAT_8061a010;
extern float  FLOAT_8061a014;
extern float  FLOAT_8061a04c;
extern float  FLOAT_8061a09c;
extern float  FLOAT_8061a114;
extern float  FLOAT_8061a118;
extern float  FLOAT_8061a14c;
extern float  FLOAT_8061a148;
extern float  FLOAT_8061a150;
extern float  FLOAT_8061a154;
extern float  FLOAT_8061a158;
extern float  FLOAT_8061a15c;
extern float  FLOAT_8061a160;
extern float  FLOAT_8061a164;
extern double DOUBLE_80619f00;

// Frame-actor scope thunks.
extern int    FUN_803826d4(void);
extern u64    FUN_803826d8(void);
extern int    FUN_803826e4(void);
extern u64    FUN_803826e8(void);
extern u64    FUN_803826dc(void);
extern void   FUN_803826f0(void);
extern uint   FUN_803825c0(double v);
extern void   FUN_80382720(void);
extern void   FUN_80382724(void);
extern void   FUN_80382728(void);
extern void   FUN_80382730(void);
extern void   FUN_80382734(void);
extern void   FUN_8038273c(void);

// Varargs text formatter.
extern u64    FUN_80387fb8(...);

// Returns the constant 5 (per-mode slot count used for column layout).
extern uint   mode_get_const_five(void);

// HUD builders / helpers.
extern int    get_string_by_id(int id);
extern u8    *stats_get_player_ptr(int slot);
extern int    player_get_entity_ptr(int slot);
extern void   text_set_font_table(int table);
extern u64    text_get_char_width(byte *str);
extern u64    text_get_char_height(byte *str);
extern void   text_draw_char(double x, double y, byte *str, uint color);
extern void   text_draw_char_drop_shadow(double x, double y, byte *str, uint color, uint shadow);
extern void   text_draw_char_shadowed(double x, double y, int str, uint shadow, uint color);
extern byte  *text_string_to_upper(uint str);
extern void   text_apply_alpha(double a, byte *str, int b);
extern void   text_set_color_rgb(double r, double g, double b);
extern void   gx_set_sub_material_id(int id);
extern void   gx_set_active_material(int material);
extern void   gx_set_scissor_rect(int x, int y, int w, int h);
extern void   gx_draw_sprite_quad_c4(double x0, double y0, double x1, double y1,
                                     int u0, int v0, uint u1, uint v1);
extern int    sprite_get_texture_id(int id);
extern int    award_check_map_completion(int a, int b);
extern uint   color_scale_lo(double a, int color);
extern int    weapon_def_get_hud_icon(int index);
extern void   hud_element_init_from_layout(int flags, int titleStr, int a, void *callback);
extern int    hud_add_list_item(void);
extern void   hud_add_callback_item_default(int a);
extern void   hud_add_spacer_item(double h, int flags, void *callback);
extern void   hud_add_padding_item(double h);
extern void   hud_add_default_padding(void);
extern void   hud_element_apply_center_xy(int *ctx);
extern void   hud_element_scroll_horizontal(double h, int a);
extern void   hud_element_apply_scroll(void);
extern void   hud_element_finalize_frame(void);
extern void   hud_set_margins(double a, double b, int c);
extern void   hud_screen_register_handler_global(int slot, int str, int a, int b);
extern void   hud_screen_pop_current(void);
extern int    hud_find_child_by_id(int element, int id);
extern void   hud_item_calc_screen_rect(int element, int item, float *outRect);
extern void   FUN_802e2988(int x0, int y0, int x1, int y1, int a);
extern void   FUN_802e4560(double x0, double y0, double x1, double y1, double a);

// Forward decls for by-address / recursive references.
int  menu_awards_count_earned(void);
void menu_awards_list_render(int param_1, int param_2, float *param_3);
void menu_awards_grid_cell_render(int param_1, int param_2, float *param_3);
void menu_awards_grid_cell_render_anim(int param_1, int param_2, float *param_3);
void menu_awards_detail_pop(void);

// ---------------------------------------------------------------------------

// 0x802dd86c - Count how many of the 0x3b award slots the active player earned
// (iterates the player record; returns the running count).
int menu_awards_count_earned(void)
{
    FUN_803826f0();
    int count = 0;
    do {
        stats_get_player_ptr(DAT_805cc61c);
        count++;
    } while (count < 0x3b);
    FUN_8038273c();
    return count;
}

// 0x802dd8d4 - Render the "N earned / all earned" award counter text, right-
// aligned within the element rect.
void menu_awards_counter_render(int param_1, int param_2, int param_3, u64 param_4,
                                u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    u64 in_f4 = 0, in_f5 = 0, in_f6 = 0, in_f7 = 0, in_f8 = 0;
    u64 extraout_f1 = 0;
    byte textBuf[32];
    (void)param_1; (void)param_2;

    u64 packed = FUN_803826e8();
    int element = (int)((u64)packed >> 0x20);
    uint fade = FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0)));

    double slotY = (double)(int)mode_get_const_five();
    uint x = (uint)-(float)((double)FLOAT_80611a68 * slotY - *(float *)(param_3 + 8));

    int earned = menu_awards_count_earned();
    text_set_font_table(*(int *)(*(int *)(element + 0x1c) + 0x44));
    gx_set_sub_material_id(1);

    if (earned == 0x3b) {
        FUN_80387fb8(extraout_f1, slotY, DOUBLE_80619f00, in_f4, in_f5, in_f6, in_f7, in_f8,
                     (int)textBuf, &DAT_80611090, 0x3b, 0x3b, param_5, param_6, param_7, param_8);
    } else {
        FUN_80387fb8(extraout_f1, slotY, DOUBLE_80619f00, in_f4, in_f5, in_f6, in_f7, in_f8,
                     (int)textBuf, &DAT_80611098, earned, param_4, param_5, param_6, param_7, param_8);
    }

    double w = (double)text_get_char_width(textBuf);
    text_draw_char((double)(int)x - w,
                   (double)(*(float *)((int)packed + 0x40) * FLOAT_80611a64 + *(float *)(param_3 + 4)),
                   textBuf, fade & 0xff | 0x64646400);
    FUN_80382734();
}

// 0x802dd9f8 - Render the scrollable list of earned award labels (from
// DAT_804659d8). If none are earned, draws a centered "no awards" message
// (string 0x4b1) instead.
void menu_awards_list_render(int param_1, int param_2, float *param_3)
{
    (void)param_1; (void)param_2;
    int element = FUN_803826d4();
    uint five = mode_get_const_five();
    float baseX = *param_3;
    float stepX = FLOAT_80611a68 * (float)(int)five;
    float rowStep = FLOAT_80619f6c * FLOAT_80611a64;
    double topY = (double)-(float)(*(float *)(element + 0x80) * FLOAT_80611a64 - param_3[1]);

    uint fade = FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0))) & 0xff;
    text_set_font_table(0x15);
    gx_set_sub_material_id(1);
    double y = (double)(float)(topY + (double)FLOAT_80619f6c);
    gx_set_scissor_rect((int)*param_3, (int)(FLOAT_80619fc4 + param_3[1]), (int)param_3[2],
                        (int)(FLOAT_80619ee8 + param_3[3]));

    int shown = 0;
    int labelOff = 0;
    for (int i = 0; i < 0x3b; i++) {
        u8 *rec = stats_get_player_ptr(DAT_805cc61c);
        if (rec[i + 0xbb4] != '\0') {
            byte *label = (byte *)get_string_by_id((int)*(short *)(&DAT_804659d8 + labelOff));
            text_draw_char_drop_shadow((double)(int)(stepX + baseX), y, label,
                                       fade | 0x64646400, fade);
            shown++;
            y = (double)(float)(y + (double)(int)rowStep);
        }
        labelOff += 2;
    }

    if (shown == 0) {
        byte *msg = text_string_to_upper(get_string_by_id(0x4b1));
        double cx = (double)(FLOAT_80619eec * (param_3[2] - *param_3) + *param_3);
        double cy = (double)(param_3[1] + rowStep);
        text_set_font_table(*(int *)(*(int *)(element + 0x1c) + 0x44));
        double w = (double)text_get_char_width(msg);
        text_draw_char_drop_shadow(-(double)(float)((double)FLOAT_80619eec * w - cx), cy, msg,
                                   fade | 0x64646400, fade);
        *(float *)(element + 0x80) = FLOAT_80619f08;
    } else {
        *(float *)(element + 0x58) = FLOAT_80611a5c * ABS((float)(y - topY));
    }
    FUN_80382720();
}

// 0x802ddc70 - Build the earned-awards list screen. Sizes the scroll region to
// the list height (clamped), lays out the player row + list spacer, centers,
// and registers the back handler.
void menu_awards_list_screen_build(int element)
{
    uint earned = menu_awards_count_earned();
    float listH = FLOAT_8061a010;
    if (earned != 0) {
        listH = FLOAT_80619f6c * (float)(int)earned + FLOAT_80619f6c;
    }
    double h = (double)listH;
    double maxH = (double)FLOAT_80619f60;

    int str = get_string_by_id(0x4ae);
    if (h <= maxH) {
        hud_element_init_from_layout(0x2000, str, 0, 0);
    } else {
        hud_element_init_from_layout(0x200c, str, 0, 0);
        h = maxH;
    }

    *(int *)(element + 0x214) = 8;
    player_get_entity_ptr(DAT_805cc61c);
    hud_add_list_item();
    hud_add_callback_item_default(1);
    *(int *)(element + 0x68) = *(int *)(element + 100);
    hud_add_spacer_item(h, 1, (void *)menu_awards_list_render);
    *(float *)(element + 0x6c) = (float)((double)*(float *)(element + 0x68) + h);
    hud_element_apply_center_xy(DAT_806119e0);
    hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
}

// 0x802dddc0 - Render a single award-grid cell sprite. Draws either the unlocked
// award icon or, if not completed, a placeholder frame + locked sprite.
void menu_awards_grid_cell_render(int param_1, int param_2, float *param_3)
{
    (void)param_1; (void)param_2;
    u64 packed = FUN_803826e8();
    int element = (int)((u64)packed >> 0x20);
    FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0)));
    int idx = *(int *)((int)packed + 0x14);
    double left = (double)*param_3;
    double right = (double)param_3[2];

    int sprite;
    int completed = award_check_map_completion((&DAT_80474680)[idx * 0xc],
                                               (int)(char)(&DAT_8047467d)[idx * 0x30]);
    if (completed == 0 || (&DAT_80474688)[idx * 0xc] == -1) {
        double inset = (double)FLOAT_80619fc4;
        FUN_802e2988((int)(left - inset), (int)((double)param_3[1] - inset), (int)(inset + right),
                     (int)(inset + (double)param_3[3]), 10);
        sprite = sprite_get_texture_id(*(int *)(&DAT_8047468c + idx * 0x30));
        FUN_803825c0((double)(FLOAT_8061a114 * *(float *)(element + 0xf0)));
    } else {
        sprite = sprite_get_texture_id((&DAT_80474688)[idx * 0xc]);
    }

    int rec = *DAT_80610a20 + sprite * 0x58;
    ushort u1 = *(ushort *)(rec + 0x4e);
    ushort v1 = *(ushort *)(rec + 0x52);
    gx_set_sub_material_id(1);
    gx_set_active_material(sprite);
    double m = (double)FLOAT_80619f6c;
    gx_draw_sprite_quad_c4((double)(float)(left - m), (double)(float)((double)param_3[1] - m),
                           (double)(float)(m + right), (double)(float)(m + (double)param_3[3]),
                           0, 0, (uint)u1 << 4, (uint)v1 << 4);
    FUN_80382734();
}

// 0x802ddf80 - Render the focused award-grid cell with an animated flip/scale
// (the "coin spin" reveal). Uses the DAT_80583228 sin/cos curve table indexed by
// the animation phase to warp the sprite quad. Kept as a structural (non-matching)
// translation of the trig-driven vertex placement.
void menu_awards_grid_cell_render_anim(int param_1, int param_2, float *param_3)
{
    (void)param_1; (void)param_2;
    u64 packed = FUN_803826dc();
    int element = (int)((u64)packed >> 0x20);
    double left = (double)*param_3;
    double right = (double)param_3[2];
    FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0)));

    float zoom = (DAT_80612244 != 0) ? FLOAT_8061a148 : FLOAT_80619ee8;
    uint idx = *(uint *)((int)packed + 0x14);

    int sprite;
    int completed = award_check_map_completion((&DAT_80474680)[idx * 0xc],
                                               (int)(char)(&DAT_8047467d)[idx * 0x30]);
    if (completed == 0 || (&DAT_80474688)[idx * 0xc] == -1) {
        FUN_802e2988((int)(FLOAT_80619f38 + *param_3), (int)(FLOAT_80619f38 + param_3[1]),
                     (int)(param_3[2] - FLOAT_80619f38), (int)(param_3[3] - FLOAT_80619f38), 8);
        sprite = sprite_get_texture_id(*(int *)(&DAT_8047468c + idx * 0x30));
    } else {
        sprite = sprite_get_texture_id((&DAT_80474688)[idx * 0xc]);
    }

    int rec = *DAT_80610a20 + sprite * 0x58;
    uint spriteW = (uint)*(ushort *)(rec + 0x4e);
    uint spriteH = (uint)*(ushort *)(rec + 0x52);

    // Animation warp factors (derived from the phase counter and curve table).
    double scale = (double)FLOAT_8061a09c;
    double halfH = (double)(int)(spriteH >> 1);
    double halfW = (double)(int)(spriteW >> 1);
    double phase = (double)(FLOAT_8061a158 * DAT_805cc6e4);
    float centerBias = (float)((double)halfW * scale);
    double warpX = (double)(((float)(scale * ((double)((float)((int)idx % 5) *
                    (FLOAT_80619ee8 + (float)(int)spriteW))) + (double)(float)((double)FLOAT_80619f6c + left)) +
                    centerBias) - centerBias);
    (void)zoom; (void)halfH; (void)warpX;

    float elemFade = *(float *)(element + 0xf0);
    double sxA = (double)(FLOAT_8061a150 * elemFade);
    double sxB = (double)(FLOAT_8061a154 * elemFade);

    // Emit the four warped corner samples through the fade helper (side effects
    // only in the original; the returned scalars are discarded).
    for (int c = 0; c < 4; c++) {
        int tblIdx = (int)(FLOAT_80619ef0 * (float)(phase * warpX) + FLOAT_80619eec) & 0x7ff;
        FUN_803825c0((double)(float)(sxB * (double)(&DAT_80583228)[tblIdx] + sxA));
    }

    gx_set_sub_material_id(1);
    gx_set_active_material(sprite);
    gx_draw_sprite_quad_c4(left, (double)param_3[1], right, (double)param_3[3], 0, 0,
                           spriteW << 4, spriteH << 4);
    FUN_80382728();
}

// 0x802de3fc - Render the award-detail sprite (large icon) plus the four frame
// corner brackets around it, and the weapon-icon glyph for the selected award.
void menu_awards_detail_sprite_render(int param_1, int param_2, float *param_3)
{
    (void)param_1; (void)param_2;
    u64 packed = FUN_803826d8();
    int element = (int)((u64)packed >> 0x20);
    double leftX = (double)(FLOAT_80619fe0 + *param_3);
    FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0)));
    uint idx = *(uint *)((int)packed + 0x14);

    double rightX = (double)(float)((double)FLOAT_8061a04c *
                    (double)(float)((double)(float)((double)FLOAT_8060f958 *
                    (double)(FLOAT_8061a15c * (param_3[3] - param_3[1])) + leftX) - leftX) + leftX);

    text_set_font_table(9);
    byte *icon = (byte *)get_string_by_id(weapon_def_get_hud_icon(idx));
    uint glyphW = (uint)text_get_char_width(icon);
    icon = (byte *)get_string_by_id(weapon_def_get_hud_icon(idx));
    double glyphH = (double)text_get_char_height(icon);
    float scaledW = FLOAT_80619eec * (float)(int)glyphW;

    if (((param_3[2] - *param_3) - FLOAT_80619f74) - (float)(rightX - leftX) < (float)(int)glyphW) {
        // Fall back to a smaller font if the icon would overflow.
        text_set_font_table(6);
        icon = (byte *)get_string_by_id(weapon_def_get_hud_icon(idx));
        glyphW = (uint)text_get_char_width(icon);
        icon = (byte *)get_string_by_id(weapon_def_get_hud_icon(idx));
        glyphH = (double)text_get_char_height(icon);
        scaledW = FLOAT_80619eec * (float)(int)glyphW;
    }
    uint scaledWi = (uint)scaledW;

    int sprite = sprite_get_texture_id((&DAT_80474688)[idx * 0xc]);
    int rec = *DAT_80610a20 + sprite * 0x58;
    ushort u1 = *(ushort *)(rec + 0x4e);
    ushort v1 = *(ushort *)(rec + 0x52);
    gx_set_sub_material_id(1);
    gx_set_active_material(sprite);
    gx_draw_sprite_quad_c4(leftX, (double)param_3[1], rightX, (double)param_3[3], 0, 0,
                           (uint)u1 << 4, (uint)v1 << 4);

    float fade = FLOAT_80619eec * *(float *)(element + 0xf0);
    // Four corner brackets around the detail sprite.
    FUN_802e4560((double)((param_3[2] - FLOAT_80619f74) - (float)(int)glyphW), (double)param_3[1],
                 (double)((param_3[2] - FLOAT_8061a014) - (float)(int)scaledWi),
                 (double)FLOAT_80619f08, (double)fade);
    FUN_802e4560((double)((float)((double)param_3[2] - (double)FLOAT_8061a014) - (float)(int)scaledWi),
                 (double)param_3[1], (double)param_3[2], (double)fade, (double)FLOAT_80619f08);
    FUN_802e4560((double)((param_3[2] - FLOAT_80619f74) - (float)(int)glyphW),
                 (double)(FLOAT_80619fc4 + param_3[1] + (float)(int)glyphH),
                 (double)((param_3[2] - FLOAT_8061a014) - (float)(int)scaledWi),
                 (double)FLOAT_80619f08, (double)fade);
    FUN_802e4560((double)((float)((double)param_3[2] - (double)FLOAT_8061a014) - (float)(int)scaledWi),
                 (double)(FLOAT_80619fc4 + param_3[1] + (float)(int)glyphH),
                 (double)param_3[2], (double)fade, (double)FLOAT_80619f08);

    uint textFade = FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0)));
    byte *sel = (byte *)get_string_by_id(weapon_def_get_hud_icon(DAT_80612ad8));
    text_draw_char((double)((param_3[2] - FLOAT_8061a014) - (float)(int)glyphW),
                   (double)(FLOAT_80619f38 + param_3[1]), sel, textFade & 0xff | 0x7f7f7f00);
    FUN_80382724();
}

// 0x802de83c - Render the award grid: draws the header label/icon for the
// selected award (locked vs unlocked), then walks the HUD item list drawing each
// award cell (menu_awards_grid_cell_render_anim) and finally the focused cell
// (menu_awards_grid_cell_render).
void menu_awards_grid_render(void)
{
    double in_f29 = 0, in_f30 = 0, in_f31 = 0;
    double in_ps29_1 = 0, in_ps30_1 = 0, in_ps31_1 = 0;
    float rect[4];
    (void)in_f29; (void)in_f30; (void)in_f31; (void)in_ps29_1; (void)in_ps30_1; (void)in_ps31_1;

    int element = FUN_803826e4();
    int focused = 0;
    double scaleY = (double)(FLOAT_8061a160 * FLOAT_80611a64);
    int itemList = *(int *)(element + 0x174);

    // Header animation factor from the popup counter / record-scale ratio.
    uint tblIdx = (uint)(FLOAT_80619ef0 * FLOAT_80619f38 * FLOAT_80619f3c *
                  (float)(int)DAT_80612020 *
                  (FLOAT_80619ee8 / (float)(int)DAT_806109c0) + FLOAT_80619eec);
    double headerFade = (double)(*(float *)(element + 0xf0) *
                        (FLOAT_80619f4c * (FLOAT_80619ee8 + (&DAT_80583228)[tblIdx & 0x7ff]) + FLOAT_80619eec));

    text_set_font_table(9);
    int completed = award_check_map_completion((&DAT_80474680)[DAT_80612ad8 * 0xc],
                                               (int)(char)(&DAT_8047467d)[DAT_80612ad8 * 0x30]);
    if (completed == 0) {
        uint colBright = color_scale_lo(headerFade, 0xa0a1e40);
        uint colDim    = color_scale_lo(headerFade, 0x28283250);
        int str = get_string_by_id(0x471);
        byte *s = (byte *)get_string_by_id(0x471);
        double w = (double)text_get_char_width(s);
        text_draw_char_shadowed((double)(FLOAT_80619eec * (float)((double)*(float *)(element + 0xac) - w) +
                                *(float *)(element + 0xa4)),
                                (double)(float)((double)FLOAT_80619f18 * scaleY + (double)*(float *)(element + 0xa8)),
                                str, colDim, colBright);
    } else {
        byte *s = (byte *)get_string_by_id(weapon_def_get_hud_icon(DAT_80612ad8));
        text_apply_alpha(0, s, 0);
        uint colBright = color_scale_lo(headerFade, 0x5235a50);
        uint colDim    = color_scale_lo(headerFade, 0x7f7f7f7f);
        int str = get_string_by_id(weapon_def_get_hud_icon(DAT_80612ad8));
        byte *s2 = (byte *)get_string_by_id(weapon_def_get_hud_icon(DAT_80612ad8));
        double w = (double)text_get_char_width(s2);
        text_draw_char_shadowed((double)(FLOAT_80619eec * (float)((double)*(float *)(element + 0xac) - w) +
                                *(float *)(element + 0xa4)),
                                (double)(float)((double)FLOAT_80619f18 * scaleY + (double)*(float *)(element + 0xa8)),
                                str, colDim, colBright);
        text_set_color_rgb((double)FLOAT_80619ee8, (double)FLOAT_80619ee8, (double)FLOAT_80619ee8);
    }

    for (int item = itemList; item != 0; item = *(int *)(item + 4)) {
        uint id = *(uint *)(item + 0x14);
        if (id == (uint)DAT_80612ad8 || (int)id < 0 || 0x95 < id) {
            if ((-1 < (int)id) && (id < 0x96)) {
                focused = item;
            }
        } else {
            hud_item_calc_screen_rect(element, item, rect);
            menu_awards_grid_cell_render_anim(element, item, rect);
        }
    }

    hud_find_child_by_id(element, DAT_80612ad8);
    if (focused != 0) {
        hud_item_calc_screen_rect(element, focused, rect);
        menu_awards_grid_cell_render(element, focused, rect);
    }
    FUN_80382730();
}

// 0x802deb3c - Pop the award-detail popup.
void menu_awards_detail_pop(void)
{
    hud_screen_pop_current();
}

// 0x802deb5c - Build the award-detail popup screen (title 0x28a): a scrolling
// horizontal frame with the selected award's label, back handler, centered.
void menu_awards_detail_screen_build(void)
{
    int str = get_string_by_id(0x28a);
    hud_element_init_from_layout(0x100, str, 0, (void *)menu_awards_detail_pop);
    hud_set_margins((double)FLOAT_80619f38, (double)FLOAT_80619f38, 1);
    hud_add_padding_item((double)FLOAT_80619f84);
    hud_add_list_item();
    hud_add_default_padding();
    hud_add_callback_item_default(1);
    hud_element_scroll_horizontal((double)FLOAT_8061a118, 0);
    hud_add_default_padding();
    get_string_by_id((int)*(short *)(&DAT_80474684 + DAT_80612ad8 * 0x30));
    int item = hud_add_list_item();
    *(int *)(item + 0x1c) = 0x15;   // set the label's font table
    hud_add_default_padding();
    hud_element_apply_scroll();
    hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
    hud_element_apply_center_xy(DAT_806119e0);
    hud_element_finalize_frame();
}
