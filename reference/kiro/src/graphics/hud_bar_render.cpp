// hud_bar_render.cpp
// HUD bar / sprite / icon renderers - the low-level draw bodies used by the
// menu and scoreboard row renderers (menu_*_render, arcsel_*). Each draws one
// or more textured quads through the GX sprite helpers, reading its geometry
// and material index from a per-element "frame context" block and the active
// material table at *DAT_80610a20 (0x58-byte stride; +0x4e / +0x52 hold the
// packed u/v texel extents).
//
// Address range: 0x802c7ff4 - 0x802c8cb4
// Batch: batch_1181 | Subsystem: graphics
//
// Calling convention: the callers hand each renderer a small element context
// block plus (for some) an explicit draw-rect pointer. The decompiler modelled
// the incoming context as the return of the GPR save/restore prologue helper
// (__save_gpr_N); here it is passed explicitly as the leading `ctx` parameter,
// which is behaviourally identical and avoids the void-return GPR intrinsics.
// `ctx+0xf0` is the element's brightness/alpha scalar and `ctx+0x28` bit0 is the
// focus flag. NON-MATCHING: the flat DAT_80610a20 material-table indexing, the
// 0x43300000 int->double bias dequant idioms, and all raw field offsets are kept
// verbatim (struct layouts not yet typed).
#include "common.h"

// ---- Named cross-module helpers (per-file externs; generated_decls.h is not
//      included here because it currently pulls unrelated module struct types). ---
extern "C" {
extern void gx_set_color_update_mode(void);
extern void gx_set_active_material(int mat);
extern void gx_set_sub_material_id(int sub);
extern void gx_set_blend_mode(int mode);
extern void gx_set_scissor_rect(int x0, int y0, int x1, int y1);
extern void gx_fill_rect_clipped(int x0, int y0, int x1, int y1, unsigned int color);
extern void gx_draw_sprite_rect(int x0, int y0, int x1, int y1, int u0, int v0,
                                unsigned int u1, unsigned int v1);
extern void gx_draw_sprite_quad(double x0, double y0, double x1, double y1,
                                int u0, int v0, unsigned int u1, unsigned int v1);
extern void gx_draw_sprite_quad_c4(double x0, double y0, double x1, double y1,
                                   int u0, int v0, int u1, unsigned int v1);
extern int  sprite_get_texture_id(int id);
extern unsigned int rt_double_to_uint(double v);
extern int  hud_item_is_focused(int ctx);
extern void color_fade_team_a(double alpha, int team);
extern int  award_grant_wrapper1(void);
extern void arcsel_draw_row_text(double a, double b, int c, int d, int e);

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern int  *DAT_80610a20;    // active material table base (0x58 stride; ptr to base)
extern int   DAT_80583228;    // shared sine table (0x800 entries)
extern int   DAT_80612020;    // frame counter (pulse phase source)
extern int   DAT_806109c0;    // frame delta-time raw (dequant base)
extern int   DAT_806109c4;    // screen width
extern int   DAT_806109c8;    // screen height
extern int   DAT_80611748;    // HUD layout block base
extern int   DAT_805cc614;    // active game mode id
extern int   DAT_80476358;    // icon-texture id table (stride 4)
extern float FLOAT_80612b0c;  // attract/fade timer

extern float FLOAT_80619eec, FLOAT_80619ee8, FLOAT_80619ef0;
extern float FLOAT_80619f0c, FLOAT_80619f08, FLOAT_80619f10, FLOAT_80619f14, FLOAT_80619f18;
extern float FLOAT_80619f30, FLOAT_80619f34, FLOAT_80619f38, FLOAT_80619f3c, FLOAT_80619f40;
extern float FLOAT_80619f44, FLOAT_80619f48, FLOAT_80619f4c, FLOAT_80619f50, FLOAT_80619f54;
extern float FLOAT_80619f58, FLOAT_80619f5c, FLOAT_80619f60;
extern float FLOAT_80611a68, FLOAT_8060f958;
extern double DOUBLE_80619f00, DOUBLE_80619f20;

// ---- batch_1181 additions: loadout-row + weapon-preview renderers ----
extern int   DAT_805cc68e;    // active gamemode id (byte, bot-support query)
extern int   DAT_8043f294;    // weapon-def string-id table (stride 0x15 ints)
extern int   DAT_80612b08;    // last-shown weapon slot index
extern int   DAT_80612b34;    // weapon-preview entity (0 = none)
extern float FLOAT_80611674;  // loadout-row height inset
extern float FLOAT_80619f6c, FLOAT_80619f70, FLOAT_80619f84, FLOAT_80619f88;

extern int   mode_loadout_get_slot(int slotIndex);
extern int   gamemode_supports_bots(unsigned int mode, int slot);
extern void  text_set_font_table(int table);
extern void  sprite_draw_gradient_quad(int x0, int y0, int x1, int y1,
                                        unsigned int c0, unsigned int c1, unsigned int c2);
extern void  text_draw_char_shadowed(double x, double y, const void *str,
                                      unsigned int color, unsigned int flags);
extern int   get_string_by_id(int id);
extern void  text_apply_alpha_fade(double x, void *str, int a, int b);
extern void  text_set_color_rgb(double r, double g, double b);
extern int   hud_get_focused_item(int ctx);
extern void  hud_draw_pulsing_box(int x0, int x1, int y0, int y1);
extern unsigned char *text_string_to_upper(unsigned int str);
extern void *text_check_scale(double width, unsigned char *str);
extern void  menu_preview_render_extra_entity(int entity, int x0, int y0, int x1, int y1);
} // extern "C"

// ---------------------------------------------------------------------------
// hud_bar_draw_split (0x802c7ff4)
// Two-segment coloured bar: the filled portion up to a split point plus the
// remainder, using the active material's u/v extents. When the context focus
// flag (ctx+0x28 bit0) is set and an alt material index (param5) is valid, the
// alt material is used and an overlay icon sprite (0x147) is drawn on top.
// (ctx = element context, rect = xyxy draw rect, matA/matB = material indices.)
// ---------------------------------------------------------------------------
void hud_bar_draw_split(int ctx, int ctxLo, float *rect, int matA, int matB)
{
    (void)ctxLo;
    rt_double_to_uint((double)(FLOAT_80619f0c * *(float *)(ctx + 0xf0)));
    if (((*(unsigned int *)(ctx + 0x28) & 1) != 0) && (-1 < matB)) {
        matA = matB;
    }
    int row = matA * 0x58;
    double uSplit = (double)CONCAT44(0x43300000, (unsigned int)*(unsigned short *)(*DAT_80610a20 + row + 0x4e));
    double xSplit = (double)(FLOAT_80619f10 * (rect[2] - *rect) + *rect);
    double uSplitTex = (double)(FLOAT_80619f14 * (float)(uSplit - DOUBLE_80619f20));

    gx_set_color_update_mode();
    gx_set_active_material(matA);
    gx_set_sub_material_id(1);
    gx_draw_sprite_quad_c4((double)*rect, (double)rect[1], xSplit, (double)rect[3], 0, 0,
                           (int)uSplitTex,
                           (unsigned int)*(unsigned short *)(*DAT_80610a20 + row + 0x52) << 4);
    gx_draw_sprite_quad(xSplit, (double)rect[1], (double)rect[2], (double)rect[3], (int)uSplitTex, 0,
                        (unsigned int)*(unsigned short *)(*DAT_80610a20 + row + 0x4e) << 4,
                        (unsigned int)*(unsigned short *)(*DAT_80610a20 + row + 0x52) << 4);
    gx_set_color_update_mode();

    if ((*(unsigned int *)(ctx + 0x28) & 1) != 0) {
        int mat = sprite_get_texture_id(0x147);
        gx_set_active_material(mat);
        float y0 = rect[1];
        float x0 = *rect;
        int r = *DAT_80610a20 + mat * 0x58;
        gx_draw_sprite_rect((int)x0, (int)y0,
                            (int)(FLOAT_80619f18 * (rect[2] - x0) + x0),
                            (int)(FLOAT_80619eec * (rect[3] - y0) + y0), 0, 0,
                            (unsigned int)*(unsigned short *)(r + 0x4e) << 4,
                            (unsigned int)*(unsigned short *)(r + 0x52) << 4);
    }
}

// ---------------------------------------------------------------------------
// hud_bar_draw_pulsed (0x802c8250)
// Focus-pulsed animated bar. When the element is focused the bar's scale is
// modulated by the shared sine table (DAT_80583228, phase derived from the
// frame counter DAT_80612020). An optional overlay icon sprite (0x147) is drawn
// from the HUD layout block (DAT_80611748) when the focus flag is set.
// ---------------------------------------------------------------------------
void hud_bar_draw_pulsed(int ctx, int ctxLo, int rect, int mat)
{
    (void)ctxLo;
    float y0 = *(float *)(rect + 4);
    unsigned int uBase = (unsigned int)(FLOAT_80619f50 * FLOAT_80611a68);
    unsigned int uHalfW = (int)(FLOAT_80619eec * (*(float *)(rect + 0xc) - y0)) ^ 0x80000000;
    double halfW = (double)CONCAT44(0x43300000, uHalfW) - DOUBLE_80619f00;
    unsigned int uCentreX = (unsigned int)-(FLOAT_80619f38 * FLOAT_80611a68 -
        (*(float *)(rect + 8) - (float)((double)CONCAT44(0x43300000, uBase ^ 0x80000000) - DOUBLE_80619f00)));
    double scale = (double)FLOAT_80619f30;
    float halfWf = FLOAT_80619f54 * (float)((double)CONCAT44(0x43300000, uHalfW) - DOUBLE_80619f00);
    double phase = (double)((float)((double)CONCAT44(0x43300000, (unsigned int)DAT_80612020 ^ 0x80000000) - DOUBLE_80619f00)
        * (FLOAT_80619ee8 / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80619f00)));

    rt_double_to_uint((double)(FLOAT_80619f0c * *(float *)(ctx + 0xf0)));
    if (hud_item_is_focused(ctx) != 0) {
        int idx = (int)(FLOAT_80619ef0 * FLOAT_80619f38 * (float)((double)FLOAT_80619f3c * phase) + FLOAT_80619eec) & 0x7ff;
        scale = (double)(float)((double)FLOAT_80619f34 *
            (double)(FLOAT_80619eec * (FLOAT_80619ee8 + (float)(&DAT_80583228)[idx])) + scale);
    }

    gx_set_color_update_mode();
    gx_set_active_material(mat);
    gx_set_sub_material_id(1);
    unsigned int uCx = uCentreX ^ 0x80000000;
    unsigned int uCy = (int)(y0 + (float)halfW) ^ 0x80000000;
    double lhw = (double)CONCAT44(0x43300000, (int)halfWf ^ 0x80000000);
    float hh = (float)((double)(float)(lhw - DOUBLE_80619f00) * scale);
    int r = *DAT_80610a20 + mat * 0x58;
    double lcx = (double)CONCAT44(0x43300000, uCx);
    float hw = (float)((double)(float)((double)CONCAT44(0x43300000, uBase ^ 0x80000000) - DOUBLE_80619f00) * scale);
    gx_draw_sprite_rect((int)((float)(lcx - DOUBLE_80619f00) - hw),
                        (int)((float)((double)CONCAT44(0x43300000, uCy) - DOUBLE_80619f00) - hh),
                        (int)((float)((double)CONCAT44(0x43300000, uCx) - DOUBLE_80619f00) + hw),
                        (int)((float)((double)CONCAT44(0x43300000, uCy) - DOUBLE_80619f00) + hh), 0, 0,
                        (unsigned int)*(unsigned short *)(r + 0x4e) << 4,
                        (unsigned int)*(unsigned short *)(r + 0x52) << 4);
    gx_set_color_update_mode();

    if ((*(unsigned int *)(ctx + 0x28) & 1) != 0) {
        int mat2 = sprite_get_texture_id(0x147);
        gx_set_active_material(mat2);
        unsigned int uIcoX = uCentreX - uBase ^ 0x80000000;
        unsigned int uLay = *(unsigned int *)(DAT_80611748 + 0x20) ^ 0x80000000;
        int r2 = *DAT_80610a20 + mat2 * 0x58;
        double lLay = (double)CONCAT44(0x43300000, uLay);
        double lIcoX = (double)CONCAT44(0x43300000, uIcoX);
        gx_draw_sprite_rect((int)(FLOAT_80619f40 * (float)(lLay - DOUBLE_80619f00) +
                                  (float)((double)CONCAT44(0x43300000, uIcoX) - DOUBLE_80619f00)),
                            (int)(FLOAT_80619f44 * (float)((double)CONCAT44(0x43300000, uLay) - DOUBLE_80619f00) +
                                  *(float *)(rect + 4)),
                            (int)(FLOAT_80619f48 * (float)((double)CONCAT44(0x43300000,
                                      *(unsigned int *)(DAT_80611748 + 0xc) ^ 0x80000000) - DOUBLE_80619f00) +
                                  (float)(lIcoX - DOUBLE_80619f00)),
                            (int)(FLOAT_80619f4c * (float)((double)CONCAT44(0x43300000, uLay) - DOUBLE_80619f00) +
                                  *(float *)(rect + 4)), 0, 0,
                            (unsigned int)*(unsigned short *)(r2 + 0x4e) << 4,
                            (unsigned int)*(unsigned short *)(r2 + 0x52) << 4);
    }
}

// ---------------------------------------------------------------------------
// hud_sprite_draw (0x802c8668)
// Single textured sprite rect using the frame context's material index (ctx)
// and its draw rect (rect). (ctx = material index, rect = xyxy rect.)
// ---------------------------------------------------------------------------
void hud_sprite_draw(int ctx, float *rect)
{
    gx_set_active_material(ctx);
    gx_set_sub_material_id(1);
    int r = *DAT_80610a20 + ctx * 0x58;
    gx_draw_sprite_rect((int)*rect, (int)rect[1], (int)rect[2], (int)rect[3], 0, 0,
                        (unsigned int)*(unsigned short *)(r + 0x4e) << 4,
                        (unsigned int)*(unsigned short *)(r + 0x52) << 4);
}

// ---------------------------------------------------------------------------
// hud_sprite_draw_bg (0x802c8720)
// Additive background sprite (texture 0x134) then the frame context's own
// material sprite on top (when the index is valid). (mat = own material index,
// rect = xyxy rect.)
// ---------------------------------------------------------------------------
void hud_sprite_draw_bg(int mat, float *rect)
{
    int bg = sprite_get_texture_id(0x134);
    gx_set_color_update_mode();
    gx_set_blend_mode(0);
    gx_set_active_material(bg);
    gx_set_sub_material_id(2);
    int r = *DAT_80610a20 + bg * 0x58;
    gx_draw_sprite_rect((int)*rect, (int)rect[1], (int)rect[2], (int)rect[3], 0, 0,
                        (unsigned int)*(unsigned short *)(r + 0x4e) << 4,
                        (unsigned int)*(unsigned short *)(r + 0x52) << 4);
    gx_set_sub_material_id(1);
    if (-1 < mat) {
        gx_set_active_material(mat);
        r = *DAT_80610a20 + mat * 0x58;
        gx_draw_sprite_rect((int)*rect, (int)rect[1], (int)rect[2], (int)rect[3], 0, 0,
                            (unsigned int)*(unsigned short *)(r + 0x4e) << 4,
                            (unsigned int)*(unsigned short *)(r + 0x52) << 4);
    }
}

// ---------------------------------------------------------------------------
// hud_panel_draw_award (0x802c8884)
// Panel background (hud_sprite_draw_bg) plus a conditional overlay icon
// (sprite 0x147). The overlay is suppressed in game mode 9 once the award has
// been granted, and shown only when the context focus flag (ctx+0x28 bit0) set.
// ---------------------------------------------------------------------------
void hud_panel_draw_award(int ctxHi, int ctx, float *rect)
{
    double bright = (double)*(float *)(ctxHi + 0xf0);
    rt_double_to_uint((double)(float)((double)FLOAT_80619f0c * bright));
    rt_double_to_uint((double)(float)((double)FLOAT_80619f58 * bright));
    hud_item_is_focused(ctx);
    if ((*(unsigned int *)(ctx + 0x28) & 1) == 0) {
        hud_sprite_draw_bg(ctx, rect);
    } else {
        hud_sprite_draw_bg(ctx, rect);
    }
    if (((DAT_805cc614 != 9) || (award_grant_wrapper1() == 0)) &&
        ((*(unsigned int *)(ctx + 0x28) & 1) != 0)) {
        int mat = sprite_get_texture_id(0x147);
        gx_set_active_material(mat);
        float y0 = rect[1];
        float x0 = *rect;
        int r = *DAT_80610a20 + mat * 0x58;
        gx_draw_sprite_rect((int)x0, (int)y0,
                            (int)(FLOAT_80619eec * (rect[2] - x0) + x0),
                            (int)(FLOAT_80619eec * (rect[3] - y0) + y0), 0, 0,
                            (unsigned int)*(unsigned short *)(r + 0x4e) << 4,
                            (unsigned int)*(unsigned short *)(r + 0x52) << 4);
    }
}

// ---------------------------------------------------------------------------
// hud_icon_draw_indexed (0x802c8a14)
// Table-selected icon sprite: pick DAT_80476358[idx] (idx = context hi word,
// clamped to 4) and draw it at the given position/size. Skipped when the x
// position (context low word) is negative. (idx = table index, x/y/w/h = rect.)
// ---------------------------------------------------------------------------
void hud_icon_draw_indexed(int idx, int x, int y, int w, int h)
{
    if (x < 0) {
        return;
    }
    if (idx > 4) {
        idx = 4;
    }
    int tex = sprite_get_texture_id(*(int *)(&DAT_80476358 + idx * 4));
    gx_set_sub_material_id(1);
    gx_set_active_material(tex);
    int r = *DAT_80610a20 + tex * 0x58;
    gx_draw_sprite_rect(x, y, x + w, y + h, 0, 0,
                        (unsigned int)*(unsigned short *)(r + 0x4e) << 4,
                        (unsigned int)*(unsigned short *)(r + 0x52) << 4);
}

// ---------------------------------------------------------------------------
// hud_marker_draw (0x802c8b00)
// Indexed icon (hud_icon_draw_indexed) plus a centred marker sprite (texture
// 0x28e) sized by the marker height, with a team-colour fade when the team
// index is positive. (team = team index, x/y/w/h = rect.)
// ---------------------------------------------------------------------------
void hud_marker_draw(int team, int x, int y, unsigned int w, unsigned int h)
{
    int mark = sprite_get_texture_id(0x28e);
    int cx = x + ((int)w >> 1) + (unsigned int)((int)w < 0 && (w & 1) != 0);
    int cy = y + ((int)h >> 1) + (unsigned int)((int)h < 0 && (h & 1) != 0);
    float sizeCoef = (team == 0) ? FLOAT_80619f5c : FLOAT_80619f38;
    int half = (int)(FLOAT_8060f958 *
        (float)((double)CONCAT44(0x43300000, h ^ 0x80000000) - DOUBLE_80619f00) * sizeCoef);
    rt_double_to_uint((double)(float)((double)FLOAT_80619f60 * 0.0));
    int teamIdx = (team > 4) ? 4 : team;
    if (0 < teamIdx) {
        color_fade_team_a(0.0, teamIdx);
    }
    hud_icon_draw_indexed(teamIdx, x, y, w, h);
    gx_set_sub_material_id(2);
    gx_set_active_material(mark);
    int r = *DAT_80610a20 + mark * 0x58;
    gx_draw_sprite_rect(cx - half, cy - half, cx + half, cy + half, 0, 0,
                        (unsigned int)*(unsigned short *)(r + 0x4e) << 4,
                        (unsigned int)*(unsigned short *)(r + 0x52) << 4);
}

// ---------------------------------------------------------------------------
// hud_fullscreen_fade (0x802c8cb4)
// HUD render pass: reset the scissor to the full screen and set up the screen;
// in game mode 1 it fills the screen with a fade whose alpha comes from the
// fade timer FLOAT_80612b0c.
// ---------------------------------------------------------------------------
void hud_fullscreen_fade(void)
{
    gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
    arcsel_draw_row_text((double)FLOAT_80619f08, (double)FLOAT_80619f08, 0, 0, 0);
    if (DAT_805cc614 == 1) {
        unsigned int alpha = rt_double_to_uint((double)(FLOAT_80619f0c * FLOAT_80612b0c));
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, alpha & 0xff);
    }
}

// ---------------------------------------------------------------------------
// hud_loadout_row_draw (0x802c8e18)
// Player-slot loadout list row: gradient-quad background, the player-number
// label, the weapon-name string (via mode_loadout_get_slot indexing the
// DAT_8043f294 string-id table), a bot-support tint, and a pulsing selection
// box when the row is focused. (ctx = element context, rect = xyxy row rect.)
// The per-player gradient palette is a 0x18-byte template copied from the ROM
// block at 0x803e0870 and indexed by the player/slot number.
// ---------------------------------------------------------------------------
void hud_loadout_row_draw(int ctx, int ctxLo, float *rect)
{
    (void)ctxLo;
    unsigned int palette[6];  // 0x18-byte gradient-colour template (local_b4)
    char label[4];            // player-number label (local_b8)

    int slotIndex = *(int *)(ctx + 0x14);
    int x0 = (int)(FLOAT_80619f6c + *rect);

    // Copy the 3x8-byte gradient palette template from ROM 0x803e0870.
    const unsigned int *tmpl = (const unsigned int *)0x803e0870;
    for (int i = 0; i < 6; i++) {
        palette[i] = tmpl[i];
    }

    int slot = mode_loadout_get_slot(slotIndex);
    double bright = (double)*(float *)(ctx + 0xf0);
    unsigned int fillColor = palette[slotIndex];
    unsigned int textColor = (rt_double_to_uint((double)(float)((double)FLOAT_80619f0c * bright)) & 0xff)
                             | 0x6e6e6e00;
    unsigned int shadowAlpha = rt_double_to_uint((double)(float)((double)FLOAT_80619f58 * bright));

    label[0] = (char)slotIndex + '1';
    label[1] = 0;

    if (gamemode_supports_bots((unsigned int)DAT_805cc68e, slot) != 0) {
        fillColor = 0x1e1e1e32;
        textColor = (rt_double_to_uint((double)(FLOAT_80619f0c * *(float *)(ctx + 0xf0))) & 0xff)
                    | 0x3c3c3c00;
    }

    text_set_font_table(0x15);
    int rowH = (int)(FLOAT_80619eec * ((rect[3] - rect[1]) - FLOAT_80611674));
    gx_set_sub_material_id(1);
    unsigned int quadC = (unsigned int)(FLOAT_80619f0c * *(float *)(ctx + 0xf0));
    sprite_draw_gradient_quad(x0 + 5, (int)(FLOAT_80619f38 + rect[1]), (int)(FLOAT_80619f70 + *rect),
                              (int)(rect[3] - FLOAT_80619f38), fillColor, 0, quadC);
    quadC = (unsigned int)(FLOAT_80619f0c * *(float *)(ctx + 0xf0));
    sprite_draw_gradient_quad((int)(FLOAT_80619f70 + *rect), (int)(FLOAT_80619f38 + rect[1]),
                              (int)(rect[2] - FLOAT_80619f38), (int)(rect[3] - FLOAT_80619f38),
                              fillColor, quadC, 0);

    double lx = (double)CONCAT44(0x43300000, (x0 + 5U) ^ 0x80000000);
    double ly = (double)CONCAT44(0x43300000, (unsigned int)rowH ^ 0x80000000);
    text_draw_char_shadowed((double)(float)(lx - DOUBLE_80619f00),
                            (double)(rect[1] + (float)(ly - DOUBLE_80619f00)), label, textColor,
                            (shadowAlpha & 0xff) | 0x2000);

    if (-1 < slot) {
        void *str = (void *)get_string_by_id((&DAT_8043f294)[slot * 0x15]);
        double fx = (double)(CONCAT44(0x43300000, x0 + 0x14) ^ 0x80000000);
        text_apply_alpha_fade((double)(rect[2] - (float)((double)fx - DOUBLE_80619f00)), str, 0, 1);
        unsigned int a = rt_double_to_uint((double)(FLOAT_80619f58 * *(float *)(ctx + 0xf0)));
        int s2 = get_string_by_id((&DAT_8043f294)[slot * 0x15]);
        double lx2 = (double)CONCAT44(0x43300000, (x0 + 0x14U) ^ 0x80000000);
        double ly2 = (double)CONCAT44(0x43300000, (unsigned int)rowH ^ 0x80000000);
        text_draw_char_shadowed((double)(float)(lx2 - DOUBLE_80619f00),
                                (double)(rect[1] + (float)(ly2 - DOUBLE_80619f00)),
                                (const void *)s2, textColor, a & 0xff);
        double one = (double)FLOAT_80619ee8;
        text_set_color_rgb(one, one, one);
    }

    if (hud_get_focused_item(ctx) == ctx) {
        hud_draw_pulsing_box((int)*rect, (int)(rect[2] - FLOAT_80619f38), (int)rect[1], (int)rect[3]);
    }
}

// ---------------------------------------------------------------------------
// hud_weapon_preview_render (0x802c97a8)
// Weapon-preview panel: draw the selected weapon name (uppercased, scaled to
// fit the panel width) and render the preview entity DAT_80612b34 if present.
// Registered as the custom-render callback for menu_weapon_preview_build.
// (ctx = element context, rect = xyxy panel rect.)
// ---------------------------------------------------------------------------
void hud_weapon_preview_render(unsigned int p0, unsigned int ctxArg, float *rect)
{
    (void)p0;
    int ctx = (int)ctxArg;
    double bright = (double)*(float *)(ctx + 0xf0);
    unsigned int color = rt_double_to_uint((double)(float)((double)FLOAT_80619f0c * bright));
    unsigned int shadow = rt_double_to_uint((double)(float)((double)FLOAT_80619f58 * bright));

    text_set_font_table(*(int *)(*(int *)(ctx + 0x1c) + 0x4c));
    if (-1 < DAT_80612b08) {
        unsigned int strId = (unsigned int)get_string_by_id((&DAT_8043f294)[DAT_80612b08 * 0x15]);
        unsigned char *upper = text_string_to_upper(strId);
        void *scaled = text_check_scale((double)(rect[2] - (FLOAT_80619f84 * FLOAT_80611a68 + *rect)),
                                        upper);
        text_draw_char_shadowed((double)(FLOAT_80619f88 * FLOAT_80611a68 + *rect),
                                (double)(FLOAT_80619f6c + rect[1]), scaled,
                                (color & 0xff) | 0x6e6e6e00, (shadow & 0xff) | 0x2000);
    }
    if (DAT_80612b34 != 0) {
        menu_preview_render_extra_entity(DAT_80612b34, (int)*rect,
                                         (int)(FLOAT_80619f50 + rect[1]), (int)rect[2],
                                         (int)rect[3]);
    }
}
