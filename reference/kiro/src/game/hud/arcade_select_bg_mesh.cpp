// Arcade select-screen animated background: the warped sine-deformed mesh
// grid that sits behind the character/level columns, plus the per-frame
// render pipeline that composites it (background panel -> mesh warp ->
// fade/streak overlay -> post overlay) and the reflection/viewport setup.
//
// Module: arcade-select background mesh + render pipeline. Companion to
//   arcade_select_render.cpp / arcade_select_nav.cpp / arcade_select_screen.cpp.
//   Entry points (arcsel_render_prepass / arcsel_render_main /
//   arcsel_render_post_overlay) are driven by the HUD screen render dispatch;
//   the geometry is built into a scratch grid descriptor and streamed through
//   the gx_* sprite/quad helpers. Subsystem: graphics.
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns.
//
// Address map (this file):
//   0x802f0644  arcsel_render_scaled_label   label strip w/ fit red-scale
//   0x802f075c  arcsel_render_fade_overlay   fade-in streak/rain overlay
//   0x802f08f0  arcsel_setup_reflection      reflect matrix + scissor setup
//   0x802f0970  arcsel_render_background     background panel + warp mesh
//   0x802f0d88  arcsel_render_prepass        render-target activate pre-pass
//   0x802f0e8c  arcsel_render_main           main viewport render entry
//   0x802f135c  arcsel_render_post_overlay   damage-ring / post overlay
//   0x802f1548  arcsel_warp_vertex_compute   single warped vertex position
//   0x802f1630  arcsel_grid_desc_init        init grid descriptor (w,h,vtbl)
//   0x802f1648  arcsel_warp_mesh_build       fill deformed mesh grid

#include "common.h"

// ---- Per-file externs: named cross-module helpers (correct signatures) ----
extern int    FUN_803825c0(double v);              // color/alpha byte from float
extern u64    FUN_803826e8(void);                  // fetch active grid descriptor
extern void   FUN_803826f0(void);                  // render scope enter
extern void   FUN_8038273c(void);                  // render scope leave
extern void   FUN_80382734(void);                  // grid scope leave (mesh)
extern u64    __save_gpr_28(void);
extern void   __restore_gpr_28(void);

extern void   text_apply_red_scale(double s);
extern void   text_set_color_rgb(double r, double g, double b);
extern double arcsel_label_strip_render(double a, double b, double c);

extern void   gx_set_sub_material_id(int id);
extern void   gx_set_blend_mode(int mode);
extern void   gx_set_active_material(int tex);
extern void   gx_set_scissor_rect(int x, int y, int w, int h);
extern void   gx_set_color_update_mode(void);
extern void   gx_set_reflect_matrix(float *mtx);
extern void   gx_fill_rect_clipped(int x0, int y0, int x1, int y1, int color);
extern void   gx_draw_sprite_rect(int x0, int y0, int x1, int y1,
                                  int u0, int v0, uint u1, uint v1);
extern void   gx_bind_material_by_index_w(int idx);
extern void   gx_material_state_nop(void);
extern void   gx_material_state_nop_b(void);
extern void   gx_material_state_nop_c(void);
extern void   gx_glow_render_nop(void);

extern void   mtx4_transpose(float *src, float *dst);
extern int    sprite_get_texture_id(int slot);
extern uint   is_widescreen(void);
extern double math_sqrt(double v);
extern void   weather_render_rain_streaks(void);

extern int    session_get_state_flag(void);
extern void   hud_viewport_set_all_split_mode(void);
extern void   render_target_activate(int handle, int x, int y, uint w, uint h, int flags);
extern void   render_target_reset_cursor(void);
extern void   viewport_set_active(int desc);
extern void   viewport_set_gx_active(void);

extern int    hud_screen_destroy_current(void);
extern int    hud_screen_find_current_popup(void);
extern double hud_screen_get_popup_height(void);
extern void   hud_screen_render_viewport(int handle, int mode);
extern void   hud_dmgring_render_strips(double x, double y, double intensity,
                                        u32 tint, int a, int b, int c, int d);

extern double arcsel_menu_x_offset(int slot);
extern double arcsel_menu_scroll_offset(int slot);
extern double arcsel_menu_scroll_gap(int slot);
extern void   arcsel_draw_separator(double x0, double y, double x1,
                                    double a, double b);

// Forward references within this file.
void arcsel_setup_reflection(void);
void arcsel_render_background(void);
void arcsel_render_scaled_label(double param_1, int param_2, uint param_3);
void arcsel_render_fade_overlay(void);
void arcsel_render_pipeline_stub(void);   // 0x802f209c (declared elsewhere)
void arcsel_render_noop(void);   // 0x802f209c (empty)
void arcsel_grid_desc_init(u32 *param_1, u32 param_2, u32 param_3);
void arcsel_warp_vertex_compute(double param_1, double param_2, double param_3, float *param_4);
void arcsel_warp_mesh_build(double p1, double p2, double p3, double p4,
                            double p5, double p6, double p7, double p8);
// Mesh helpers (defined at the end of this file; batch_843).
void arcsel_warp_ramp_apply(double v, u32 *grid);                                // 0x802f190c
void arcsel_mesh_emit_layer(u32 *grid, int a, uint color);                       // 0x802f1ae4
void arcsel_mesh_emit_bordered(double a, double b, double c, u32 *grid,
                               int d, uint color, uint rgba, int src);           // 0x802f1c00
void arcsel_mesh_set_uv(double a, double b, double c, double d, u32 *grid);      // 0x802f1840

// GX FIFO write port + batch helpers used by the mesh emitters.
extern volatile u32 DAT_cc008000;                    // GX command FIFO port
extern u64  FUN_803826e4(void);                      // begin strip-emit scope A
extern u64  FUN_803826b4(void);                      // begin strip-emit scope B
extern void FUN_80382700(void);                      // end strip-emit scope B
extern void FUN_80382730(void);                      // end strip-emit scope A
extern void gx_begin_textured_batch(int a, int b, int c, short vtxcount, int scope);
extern void gx_end_batch(void);
extern u32  color_add_saturate(uint base, uint add);

// ---- Raw data symbols ----
extern int   *DAT_80610a20;      // sprite/texture descriptor table base
extern int   *DAT_80611748;      // scissor/layout rect array (x,y1,x2,?,?,y0,y2,?,h,...)
extern int    DAT_80611d54;      // viewport owner base
extern int    DAT_80611d74;      // per-frame context ptr (slot at +4, +8, +0x14)
extern int    DAT_806119ec;      // active screen index
extern int    DAT_80611934;      // session mode
extern int    DAT_80612250;      // reflect state flag
extern int    DAT_80612258;      // reflect state flag 2
extern int    DAT_80612b9c;      // popup-active flag
extern int    DAT_80612bc8;      // damage-ring enable flag
extern int    DAT_806109c4;      // screen width
extern int    DAT_806109c8;      // screen height
extern int    DAT_805cf270;      // screen-object table (render viewport)
extern int    DAT_805cf280;      // screen-object table (main)
extern int    DAT_805cf290;      // grid descriptor vtable/data ptr
extern float  DAT_8058e3cc;      // transposed reflect matrix scratch
extern float  DAT_80583228;      // sine/warp lookup table (2K entries)

extern float  FLOAT_8060f958;
extern float  FLOAT_8061a418;    // 0.0
extern float  FLOAT_8061a41c;    // 1.0
extern float  FLOAT_8061a420;
extern float  FLOAT_8061a444;
extern float  FLOAT_8061a448;
extern float  FLOAT_8061a44c;
extern float  FLOAT_8061a450;
extern float  FLOAT_8061a470;
extern float  FLOAT_8061a480;
extern float  FLOAT_8061a48c;
extern float  FLOAT_8061a498;
extern float  FLOAT_8061a49c;
extern float  FLOAT_8061a4a0;
extern float  FLOAT_8061a4a4;
extern float  FLOAT_8061a4a8;
extern float  FLOAT_8061a4ac;
extern float  FLOAT_8061a4b0;
extern float  FLOAT_8061a4b4;
extern float  FLOAT_8061a4b8;
extern float  FLOAT_8061a4bc;
extern float  FLOAT_8061a4c0;
extern float  FLOAT_8061a4c4;
extern float  FLOAT_8061a4c8;
extern float  FLOAT_8061a4cc;
extern float  FLOAT_8061a4d0;
extern float  FLOAT_8061a4d4;
extern float  FLOAT_8061a4d8;
extern float  FLOAT_8061a4dc;
extern float  FLOAT_8061a4e0;
extern double DOUBLE_8061a430;   // 4503599627370496.0 magic bias
extern double DOUBLE_8061a478;   // signed-int magic bias
extern float  FLOAT_8061a494;
extern float  FLOAT_8061a4e4;
extern float  FLOAT_8061a4e8;
extern float  FLOAT_8061a4ec;

// ----------------------------------------------------------------------------
// 0x802f0644  arcsel_render_scaled_label
// Draws a label strip; if it would overflow the available width the text is
// red-scaled down to fit before being rendered, then the color is reset.
// param_2/param_3 are the (negated, biased) strip pixel extents.
// ----------------------------------------------------------------------------
void arcsel_render_scaled_label(double param_1, int param_2, uint param_3) {
    double avail;
    double used;
    double ratio;

    avail = (double)(FLOAT_8061a48c *
                     (float)((double)CONCAT44(0x43300000, DAT_80611748[3] ^ 0x80000000) -
                             DOUBLE_8061a430));
    used = arcsel_label_strip_render((double)FLOAT_8061a418, (double)FLOAT_8061a418, param_1);
    if (avail < used) {
        ratio = avail / used;
        used = avail;
        text_apply_red_scale((double)(float)ratio);
    }

    arcsel_label_strip_render(
        (double)(FLOAT_8061a444 *
                 (float)((double)(float)((double)CONCAT44(0x43300000,
                                                          DAT_80611748[3] ^ 0x80000000) -
                                         DOUBLE_8061a430) - used) +
                 (float)((double)CONCAT44(0x43300000, *DAT_80611748 ^ 0x80000000) -
                         DOUBLE_8061a430)),
        (double)(float)((double)CONCAT44(0x43300000, param_3 ^ 0x80000000) - DOUBLE_8061a430),
        param_1);

    double gray = (double)FLOAT_8061a41c;
    text_set_color_rgb(gray, gray, gray);
    (void)param_2;
}

// ----------------------------------------------------------------------------
// 0x802f075c  arcsel_render_fade_overlay
// Fade-in overlay: when the active screen object is fading (flag 8), builds a
// tinted color from the fade timer (two tint ramps depending on flag 0x20) and
// fills the full layout rect via the streak renderer.
// ----------------------------------------------------------------------------
void arcsel_render_fade_overlay(void) {
    float timer;
    float scale;
    uint flags;
    uint color;

    flags = *(uint *)(&DAT_805cf280)[DAT_806119ec];
    if (((flags & 8) != 0) &&
        (timer = (float)((uint *)(&DAT_805cf280)[DAT_806119ec])[3], FLOAT_8061a418 <= timer)) {
        scale = FLOAT_8061a418;
        if (FLOAT_8061a420 <= timer) {
            if (timer < FLOAT_8061a450) {
                scale = FLOAT_8061a41c - (timer - FLOAT_8061a420) / FLOAT_8061a444;
                scale = scale * scale;
            }
        } else {
            scale = timer / FLOAT_8061a420;
        }

        if ((flags & 0x20) == 0) {
            color = (uint)FUN_803825c0((double)(FLOAT_8061a480 * scale));
            color = (color & 0xff) | 0x87644800;
        } else {
            color = (uint)FUN_803825c0((double)(FLOAT_8061a480 * scale));
            color = (color & 0xff) | 0x6e4b2f00;
        }

        weather_render_rain_streaks();
        gx_set_sub_material_id(2);
        gx_fill_rect_clipped(*DAT_80611748, DAT_80611748[5],
                             DAT_80611748[1] + 1, DAT_80611748[6] + 1, (int)color);
        gx_fill_rect_clipped(*DAT_80611748, DAT_80611748[5],
                             DAT_80611748[1] + 1, DAT_80611748[6] + 1, (int)color);
        gx_fill_rect_clipped(*DAT_80611748, DAT_80611748[5],
                             DAT_80611748[1] + 1, DAT_80611748[6] + 1, (int)color);
    }
}

// ----------------------------------------------------------------------------
// 0x802f08f0  arcsel_setup_reflection
// Uploads the current screen object's reflection matrix and its transpose,
// arms the reflect state flags, and sets the scissor rect to the layout.
// ----------------------------------------------------------------------------
void arcsel_setup_reflection(void) {
    int obj;

    obj = (&DAT_805cf280)[DAT_806119ec];
    gx_set_reflect_matrix(*(float **)(obj + 0x14));
    mtx4_transpose(*(float **)(obj + 0x10), (float *)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;
    gx_set_scissor_rect(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    gx_material_state_nop();
}

// ----------------------------------------------------------------------------
// 0x802f0970  arcsel_render_background
// Composites the arcade-select background: base panel sprite, the warped mesh
// grid (built into a stack descriptor), a color ramp keyed off the animation
// timer, and (for 4:3 vs 16:9) the mesh UV set. Finishes with the foreground
// vignette / overlay sprites.
//
// The eight incoming float registers (f29..f31 and paired-single halves) are
// the mesh corner parameters; they are threaded through as the animation
// timer / corner floats. Expressed here as the recovered timer value.
// ----------------------------------------------------------------------------
void arcsel_render_background(void) {
    int *rect;
    u32  base_tex;
    int  desc;
    uint tw;
    uint th;
    uint color;
    int  obj_off;
    double timer;
    double ramp;
    u32  grid[10];

    timer = (double)(float)__save_gpr_28();     // animation timer (from f31 pair)
    uint *screen = (uint *)(&DAT_805cf280)[DAT_806119ec];
    color = 0x7f605532;
    double alpha = (double)FLOAT_8061a498;

    obj_off = *DAT_80610a20 + screen[1] * 0x58;
    tw = (uint)*(unsigned short *)(obj_off + 0x4e);
    th = (uint)*(unsigned short *)(obj_off + 0x52);

    gx_set_sub_material_id(0xe);
    gx_set_blend_mode(0);
    if ((*screen & 0x100) == 0) {
        gx_fill_rect_clipped(*DAT_80611748, DAT_80611748[5],
                             DAT_80611748[1] + 1, DAT_80611748[6] + 1, 0x7f);
        gx_set_active_material((int)screen[1]);
        gx_draw_sprite_rect(*DAT_80611748, DAT_80611748[5],
                            *DAT_80611748 + (int)tw, DAT_80611748[5] + (int)th,
                            0, 0, tw << 4, th << 4);
        gx_material_state_nop_c();
    }
    gx_material_state_nop_b();
    arcsel_setup_reflection();
    arcsel_grid_desc_init(grid, 10, 10);

    if ((is_widescreen() & 0xff) == 0) {
        double zero = (double)FLOAT_8061a418;
        double one  = (double)FLOAT_8061a41c;
        arcsel_warp_mesh_build(zero, zero, one, one, zero, zero, one, one);
    } else {
        double zero = (double)FLOAT_8061a418;
        double one  = (double)FLOAT_8061a41c;
        arcsel_warp_mesh_build(-(double)FLOAT_8061a49c, zero, (double)FLOAT_8061a4a0, one,
                               zero, zero, one, one);
    }

    if ((double)FLOAT_8061a41c <= timer) {
        ramp = (double)FLOAT_8061a4b8;
    } else {
        uint cr = (uint)FUN_803825c0(-(double)(float)((double)FLOAT_8061a4ac * timer - (double)FLOAT_8061a480));
        uint cg = (uint)FUN_803825c0(-(double)(float)((double)FLOAT_8061a4a8 * timer - (double)FLOAT_8061a480));
        uint cb = (uint)FUN_803825c0(-(double)(float)((double)FLOAT_8061a4a4 * timer - (double)FLOAT_8061a480));
        alpha = (double)(float)((double)FLOAT_8061a498 * timer);
        color = ((cb & 0xff) << 0x10) | (cg & 0xff) | ((cr & 0xff) << 8) | 0x7f000000;
        ramp = (double)(float)((double)FLOAT_8061a4b4 * timer + (double)FLOAT_8061a4b0);
        arcsel_warp_ramp_apply(timer, grid);
    }

    if ((*screen & 0x100) == 0) {
        gx_set_sub_material_id(0xe);
        arcsel_mesh_emit_layer(grid, -6, color);
        arcsel_mesh_emit_bordered((double)FLOAT_8061a4bc, (double)FLOAT_8061a4c0,
                                  (double)FLOAT_8061a4b4, grid, -6, color,
                                  0x6e4a54, (int)(screen + 6));
    }

    gx_set_sub_material_id(2);
    base_tex = (u32)sprite_get_texture_id(0xb);
    arcsel_mesh_set_uv((double)FLOAT_8061a418, (double)FLOAT_8061a418,
                       (double)FLOAT_8061a4b4, (double)FLOAT_8061a4b4, grid);
    desc = FUN_803825c0(ramp);
    arcsel_mesh_emit_layer(grid, (int)base_tex, (uint)((desc << 0x18) | 0x6f6049));

    if ((*screen & 0x100) == 0) {
        gx_set_sub_material_id(10);
        int tex = sprite_get_texture_id(0xc);
        gx_set_active_material(tex);
        rect = DAT_80611748;
        obj_off = *DAT_80610a20;
        FUN_803825c0((double)(float)((double)FLOAT_8061a480 * timer));
        obj_off = obj_off + tex * 0x58;
        gx_draw_sprite_rect(*rect, rect[5], rect[1] + 1, rect[6] + 1, 0, 0,
                            (uint)*(unsigned short *)(obj_off + 0x4e) << 4,
                            (uint)*(unsigned short *)(obj_off + 0x52) << 4);
        gx_set_sub_material_id(1);
        gx_bind_material_by_index_w(-6);
        uint sh = (uint)DAT_806109c8;
        uint sw = (uint)DAT_806109c4;
        FUN_803825c0((double)(float)((double)FLOAT_8061a4c4 * timer));
        gx_draw_sprite_rect(0, 0, (int)sw, (int)sh, 0, 0, sw << 4, sh << 4);
        gx_set_sub_material_id(1);
        rect = DAT_80611748;
        desc = FUN_803825c0(alpha);
        gx_fill_rect_clipped(*rect, rect[5], rect[1] + 1, rect[6] + 1,
                             (desc << 0x18) | 0x1e1405);
    }
    __restore_gpr_28();
}

// ----------------------------------------------------------------------------
// 0x802f0d88  arcsel_render_prepass
// Pre-pass for the active screen: if it is the visible/foreground screen (and
// not a split-mode child), it arms the offscreen render target so the mesh /
// reflection pass renders into it.
// ----------------------------------------------------------------------------
void arcsel_render_prepass(void) {
    int state;
    uint flags;
    uint *screen;

    DAT_806119ec = *(int *)(DAT_80611d74 + 4);
    screen = (uint *)(&DAT_805cf280)[DAT_806119ec];
    state = session_get_state_flag();
    if ((1 < state) && (DAT_80611934 == 4)) {
        return;
    }
    flags = *screen;
    if (((flags & 1) != 0) && ((flags & 0x80) != 0) && ((flags & 0x100) == 0)) {
        hud_viewport_set_all_split_mode();
        gx_set_sub_material_id(0xe);
        gx_set_blend_mode(0);
        gx_glow_render_nop();
        render_target_activate((int)screen[1], *DAT_80611748, DAT_80611748[5],
                               (uint)((DAT_80611748[1] - *DAT_80611748) + 1) & 0xfffffffe,
                               (uint)((DAT_80611748[6] - DAT_80611748[5]) + 1) & 0xfffffffe,
                               0xc000);
        gx_set_color_update_mode();
    }
}

// ----------------------------------------------------------------------------
// 0x802f0e8c  arcsel_render_main
// Main per-frame render entry for the arcade-select screen. Sets up the
// viewport, computes the two horizontal wipe boundaries (x95/x96) from the
// popup transition / menu offsets, fills the letterbox bars, draws the four
// separators, the scaled label, and finally renders the child HUD viewport.
// ----------------------------------------------------------------------------
void arcsel_render_main(void) {
    uint flags;
    int  popup_a;
    int  popup_b;
    uint *screen;
    double h;
    double wa;
    double wb;
    double blend;

    FUN_803826f0();
    DAT_806119ec = *(int *)(DAT_80611d74 + 4);
    screen = (uint *)(&DAT_805cf280)[DAT_806119ec];
    viewport_set_active(DAT_80611d54 + 0x224);
    viewport_set_gx_active();
    gx_set_scissor_rect(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);

    flags = *screen;
    if ((((flags & 1) != 0) && (((flags & 0x80) == 0) || ((flags & 0x100) != 0))) &&
        ((DAT_80612b9c == 0) || (*(int *)(DAT_80611d74 + 4) == 0))) {
        popup_a = hud_screen_destroy_current();
        popup_b = hud_screen_find_current_popup();
        h = hud_screen_get_popup_height();
        double one = (double)FLOAT_8061a41c;
        wb = one;
        if ((popup_b != 0) && ((*(uint *)(popup_b + 0x40) & 0x40000000) != 0)) {
            wb = (double)FLOAT_8061a418;
        }
        wa = one;
        if ((popup_a != 0) && ((*(uint *)(popup_a + 0x40) & 0x40000000) != 0)) {
            wa = (double)FLOAT_8061a418;
        }
        blend = (double)(float)(wb * (double)(float)((double)FLOAT_8061a41c - h) +
                                (double)(float)(wa * h));
        double blend_sq = blend;
        if ((double)FLOAT_8061a418 < blend) {
            blend = math_sqrt(blend);
        }

        double scroll = (double)FLOAT_8061a418;
        if ((float)screen[0x94] < FLOAT_8061a444) {
            scroll = (double)((float)screen[0x94] / FLOAT_8061a444);
            if ((double)FLOAT_8061a418 < scroll) {
                scroll = math_sqrt(scroll);
            }
            blend = (double)(float)(blend * scroll);
        }

        double xoff = arcsel_menu_x_offset(*(int *)(DAT_80611d74 + 4));
        screen[0x95] = (u32)(float)(blend * xoff +
                                    (double)(float)((double)CONCAT44(0x43300000,
                                                                     DAT_80611748[5] ^ 0x80000000) -
                                                    DOUBLE_8061a430));
        double soff = arcsel_menu_scroll_offset(*(int *)(DAT_80611d74 + 4));
        screen[0x96] = (u32)-(float)(scroll * soff -
                                     (double)(float)((double)CONCAT44(0x43300000,
                                                                      DAT_80611748[6] ^ 0x80000000) -
                                                     DOUBLE_8061a430));

        render_target_reset_cursor();
        arcsel_render_noop();
        arcsel_render_background();
        gx_set_sub_material_id(1);
        gx_set_blend_mode(0);
        gx_fill_rect_clipped(*DAT_80611748, DAT_80611748[5],
                             DAT_80611748[1] + 1, (int)(float)screen[0x95], 0x7f);
        gx_fill_rect_clipped(*DAT_80611748, (int)(float)screen[0x96],
                             DAT_80611748[1] + 1, DAT_80611748[6] + 1, 0x7f);
        gx_set_sub_material_id(2);

        double sep_left = (double)(float)((double)CONCAT44(0x43300000, *DAT_80611748 ^ 0x80000000) -
                                          DOUBLE_8061a430);
        double sep_mid  = (double)(float)((double)CONCAT44(0x43300000, DAT_80611748[2] ^ 0x80000000) -
                                          DOUBLE_8061a430);
        double sep_right = (double)(float)((double)CONCAT44(0x43300000, DAT_80611748[1] ^ 0x80000000) -
                                           DOUBLE_8061a430);
        double x95 = (double)(float)screen[0x95];
        double x96 = (double)(float)screen[0x96];
        arcsel_draw_separator(sep_left, x95, sep_mid, (double)FLOAT_8061a418, blend_sq);
        arcsel_draw_separator(sep_mid, x95, sep_right, blend_sq, (double)FLOAT_8061a418);
        arcsel_draw_separator(sep_left, x96, sep_mid, (double)FLOAT_8061a418, (double)FLOAT_8061a41c);
        arcsel_draw_separator(sep_mid, x96, sep_right, (double)FLOAT_8061a41c, (double)FLOAT_8061a418);

        gx_set_sub_material_id(1);
        arcsel_render_scaled_label(
            blend_sq,
            (int)-(FLOAT_8061a4cc *
                   (float)((double)CONCAT44(0x43300000, DAT_80611748[8] ^ 0x80000000) -
                           DOUBLE_8061a430) - (float)screen[0x95]),
            (uint)(int)-(FLOAT_8061a4d0 *
                         (float)((double)CONCAT44(0x43300000, DAT_80611748[8] ^ 0x80000000) -
                                 DOUBLE_8061a430) - (float)screen[0x95]));

        arcsel_render_noop();
        arcsel_render_noop();
        viewport_set_active((int)(screen + 0xd5));
        gx_set_scissor_rect(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
        viewport_set_gx_active();
        hud_screen_render_viewport((&DAT_805cf270)[DAT_806119ec], 0);
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// 0x802f135c  arcsel_render_post_overlay
// Post overlay: unless the current popup suppresses it (flag 0x80000), renders
// the damage-ring style edge strips scaled by the popup transition, then the
// fade overlay, and clears the "dirty" flag on the screen object.
// ----------------------------------------------------------------------------
void arcsel_render_post_overlay(void) {
    float ring;
    bool  suppress;
    int  *rect;
    int   popup_a;
    int   popup_b;
    uint *screen;
    double h;
    double intensity;

    __save_gpr_28();
    suppress = false;
    popup_a = hud_screen_destroy_current();
    popup_b = hud_screen_find_current_popup();
    DAT_806119ec = *(int *)(DAT_80611d74 + 4);
    screen = (uint *)(&DAT_805cf280)[DAT_806119ec];

    if ((popup_a != 0) && ((*(uint *)(popup_a + 0x40) & 0x80000) != 0)) {
        suppress = true;
    }
    if ((popup_b != 0) && ((*(uint *)(popup_b + 0x40) & 0x80000) != 0)) {
        suppress = true;
    }

    if ((!suppress) && ((*(uint *)(*(int *)(DAT_80611d74 + 8) + 0x21c) & 1) != 0) &&
        ((*screen & 1) != 0) && (DAT_80612bc8 != 0) && ((*screen & 0x100) == 0)) {
        h = hud_screen_get_popup_height();
        ring = FLOAT_8061a4d4;
        if (popup_a != 0) {
            ring = *(float *)(popup_a + 0x20c);
        }
        intensity = (double)ring;
        if (popup_b != 0) {
            double t = (double)FLOAT_8061a418;
            if (t < h) {
                t = math_sqrt(h);
            }
            intensity = (double)(*(float *)(popup_b + 0x20c) * (float)((double)FLOAT_8061a41c - t) +
                                 (float)(intensity * t));
        }

        viewport_set_active(*(int *)(DAT_80611d74 + 8) + 0x224);
        viewport_set_gx_active();
        gx_set_scissor_rect(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
        int c = FUN_803825c0((double)FLOAT_8061a480);
        rect = DAT_80611748;
        double gap = arcsel_menu_scroll_gap(*(int *)(DAT_80611d74 + 4));
        hud_dmgring_render_strips(
            (double)(float)((double)CONCAT44(0x43300000, *rect ^ 0x80000000) - DOUBLE_8061a430),
            (double)(float)((double)(float)((double)CONCAT44(0x43300000, rect[5] ^ 0x80000000) -
                                            DOUBLE_8061a430) + gap),
            (double)(float)(intensity * (double)FLOAT_8060f958),
            *(u32 *)(*(int *)(DAT_80611d74 + 0x14) + 0x268), c, c, c, c);
    }

    arcsel_render_fade_overlay();
    if ((*screen & 0x100) == 0) {
        *screen = *screen & 0xffffff7f;
    }
    gx_set_blend_mode(0);
    __restore_gpr_28();
}

// ----------------------------------------------------------------------------
// 0x802f1548  arcsel_warp_vertex_compute
// Computes one warped vertex position into out[0..2]. Two phase angles are
// derived from the (1 - param) offsets, indexed into the shared sine table,
// and combined to displace the vertex along x/y and add a z ripple.
// ----------------------------------------------------------------------------
void arcsel_warp_vertex_compute(double param_1, double param_2, double param_3, float *out) {
    uint idx_a;
    uint idx_b;
    float amp_z;
    float amp_x;
    uint off_a;
    double one = (double)FLOAT_8061a41c;
    double base = (double)FLOAT_8061a444;

    amp_x = FLOAT_8061a4dc;
    amp_z = FLOAT_8061a4e0 * (float)(one - param_3);

    idx_a = (uint)((double)FLOAT_8061a470 *
                   (double)(FLOAT_8061a448 * (float)((double)(float)(one - param_2) - base) *
                            FLOAT_8061a4c8) + base);
    idx_b = (uint)((double)FLOAT_8061a470 *
                   (double)(FLOAT_8061a448 * (float)((double)(float)(one - param_1) - base) *
                            FLOAT_8061a4d8) + base);

    off_a = ((idx_a + 0x200) * 4) & 0x1ffc;

    out[0] = (float)(one - param_3) * (&DAT_80583228)[idx_b & 0x7ff] *
             (float)(one + ABS((double)(FLOAT_8061a4b0 * *(float *)((int)&DAT_80583228 + off_a))));
    out[1] = amp_x * (&DAT_80583228)[idx_a & 0x7ff] *
             (float)((double)(float)(param_1 - base) * (double)(float)(param_1 - base) + one);
    out[2] = amp_z * (float)(base * (double)*(float *)((int)&DAT_80583228 +
                                                       (((idx_b + 0x200) * 4) & 0x1ffc)) +
                             (double)(float)(base * (double)*(float *)((int)&DAT_80583228 + off_a)));
}

// ----------------------------------------------------------------------------
// 0x802f1630  arcsel_grid_desc_init
// Initializes a 3-word mesh grid descriptor: columns, rows, and the shared
// vertex-buffer/vtable pointer (DAT_805cf290).
// ----------------------------------------------------------------------------
void arcsel_grid_desc_init(u32 *desc, u32 cols, u32 rows) {
    desc[0] = cols;
    desc[1] = rows;
    desc[2] = (u32)(uintptr_t)&DAT_805cf290;
}

// ----------------------------------------------------------------------------
// 0x802f1648  arcsel_warp_mesh_build
// Builds the deformed mesh grid: for each (row, col) it bilerps the four
// corner params, calls arcsel_warp_vertex_compute for the displaced position,
// and writes a 5-float vertex (pos.xyz + u + v) into the grid vertex buffer.
//
// The 24 incoming float-register locals (f20..f31 + paired-single halves) are
// SDK-saved callee registers spilled to the stack frame; they are not inputs
// to the mesh math (only the eight named corner params drive it), so they are
// preserved as local spill storage to keep the frame layout faithful.
// ----------------------------------------------------------------------------
void arcsel_warp_mesh_build(double p1, double p2, double p3, double p4,
                            double p5, double p6, double p7, double p8) {
    uint *desc;
    uint  cols;
    float *vtx;
    uint  row;
    uint  col;
    double t_row;
    double edge_row_x;
    double edge_row_v;
    double dv_x = (double)(float)(p4 - p2);
    double dv_z = (double)(float)(p3 - p1);
    double du_v = (double)(float)(p8 - p6);
    double du_u = (double)(float)(p7 - p5);
    double bias = DOUBLE_8061a430;
    float  out[3];

    desc = (uint *)FUN_803826e8();

    for (row = 0; (int)row <= (int)desc[1]; row = row + 1) {
        uint num = row ^ 0x80000000;
        uint den = desc[1] ^ 0x80000000;
        t_row = (double)((float)((double)CONCAT44(0x43300000, num) - bias) /
                         (float)((double)CONCAT44(0x43300000, den) - bias));
        edge_row_x = (double)(float)(t_row * dv_x + p2);
        edge_row_v = (double)(float)(t_row * du_v + p6);

        for (col = 0; cols = desc[0], (int)col <= (int)cols; col = col + 1) {
            uint cnum = col ^ 0x80000000;
            uint cden = cols ^ 0x80000000;
            vtx = (float *)(desc[2] + (col + row * (cols + 1)) * 0x14);
            double t_col = (double)((float)((double)CONCAT44(0x43300000, cnum) - bias) /
                                    (float)((double)CONCAT44(0x43300000, cden) - bias));
            arcsel_warp_vertex_compute((double)(float)(t_col * dv_z + p1), edge_row_x,
                                       (double)FLOAT_8061a418, out);
            vtx[0] = out[0];
            vtx[1] = out[1];
            vtx[2] = out[2];
            vtx[3] = (float)(t_col * du_u + p5);
            vtx[4] = (float)edge_row_v;
        }
    }

    FUN_80382734();
}

// ============================================================================
// Mesh helpers (batch_843). These complete the render pipeline above:
// arcsel_render_background streams the warped grid through the strip emitters
// and sets its per-vertex UVs / ramp with the two lerp passes.
// ============================================================================

// ----------------------------------------------------------------------------
// 0x802f209c  arcsel_render_noop
// Empty function (a render-pipeline hook that does nothing in this build).
// ----------------------------------------------------------------------------
void arcsel_render_noop(void) {
}

// ----------------------------------------------------------------------------
// 0x802f1840  arcsel_mesh_set_uv
// Assigns per-vertex UV coordinates across the grid: u lerps a..c across the
// columns, v lerps b..d down the rows, written into vtx[+0xc]/[+0x10].
// ----------------------------------------------------------------------------
void arcsel_mesh_set_uv(double param_1, double param_2, double param_3, double param_4,
                        u32 *param_5) {
    double bias = DOUBLE_8061a430;
    uint rows;
    uint cols;
    uint row;
    uint col;

    for (row = 0; rows = param_5[1], (int)row <= (int)rows; row = row + 1) {
        for (col = 0; cols = param_5[0], (int)col <= (int)cols; col = col + 1) {
            int vtx = (int)param_5[2] + (int)((col + row * (cols + 1)) * 0x14);
            *(float *)(vtx + 0xc) =
                (float)((double)((float)((double)CONCAT44(0x43300000, col ^ 0x80000000) - bias) /
                                 (float)((double)CONCAT44(0x43300000, cols ^ 0x80000000) - bias)) *
                        (double)(float)(param_3 - param_1) + param_1);
            *(float *)(vtx + 0x10) =
                (float)((double)((float)((double)CONCAT44(0x43300000, row ^ 0x80000000) - bias) /
                                 (float)((double)CONCAT44(0x43300000, rows ^ 0x80000000) - bias)) *
                        (double)(float)(param_4 - param_2) + param_2);
        }
    }
}

// ----------------------------------------------------------------------------
// 0x802f190c  arcsel_warp_ramp_apply
// Lerps every mesh vertex (from FUN_803826e8's active descriptor) toward a
// per-cell perspective target by factor `param_1`: x/y toward an aspect-scaled
// screen-space ramp, z toward a fixed depth. Widescreen scales the ramp.
// ----------------------------------------------------------------------------
void arcsel_warp_ramp_apply(double param_1, u32 *param_2) {
    double ramp_x = (double)FLOAT_8061a4e4;
    double ramp_y = (double)FLOAT_8061a4e8;
    double aspect = (double)(FLOAT_8061a450 *
                             ((float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_8061a430) /
                              (float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_8061a430)));
    double neg_aspect = -aspect;
    double base_y = (double)(float)(aspect * (double)FLOAT_8061a444);

    if ((is_widescreen() & 0xff) != 0) {
        ramp_x = (double)(float)(ramp_x * (double)FLOAT_8061a4ec);
        ramp_y = (double)(float)(ramp_y * (double)FLOAT_8061a4ec);
    }

    double depth = (double)FLOAT_8061a444;
    double bias = DOUBLE_8061a430;
    double denom_x = (double)CONCAT44(0x43300000, *param_2 ^ 0x80000000) - DOUBLE_8061a430;
    double denom_y = (double)CONCAT44(0x43300000, param_2[1] ^ 0x80000000) - DOUBLE_8061a430;

    uint row;
    uint col;
    for (row = 0; (int)row <= (int)param_2[1]; row = row + 1) {
        double ty = (double)(float)((double)(float)(neg_aspect / (double)(float)denom_y) *
                                    (double)(float)((double)CONCAT44(0x43300000, row ^ 0x80000000) - bias) +
                                    base_y);
        for (col = 0; (int)col <= (int)*param_2; col = col + 1) {
            float *vtx = (float *)(param_2[2] + (col + row * (*param_2 + 1)) * 0x14);
            double tx = (double)(float)((double)(float)(ramp_x / (double)(float)denom_x) *
                                        (double)(float)((double)CONCAT44(0x43300000, col ^ 0x80000000) - bias) +
                                        ramp_y);
            vtx[0] = (float)(param_1 * (double)(float)((double)vtx[0] - tx) + tx);
            vtx[1] = (float)(param_1 * (double)(float)((double)vtx[1] - ty) + ty);
            vtx[2] = (float)(param_1 * (double)(float)((double)vtx[2] - depth) + depth);
        }
    }
}

// ----------------------------------------------------------------------------
// 0x802f1ae4  arcsel_mesh_emit_layer
// Streams the mesh as row-major triangle strips to the GX FIFO: two vertices
// per column (current row + next row), each with position (xyz), the given
// endian-swapped color, and UV. param_1 is the grid descriptor pointer.
// ----------------------------------------------------------------------------
void arcsel_mesh_emit_layer(u32 *param_1, int param_2, uint param_3) {
    int *desc;
    int cols1;
    u32 *v0;
    u32 *v1;
    int row;
    int col;
    u64 scope;

    (void)param_2;
    scope = FUN_803826e4();
    desc = (int *)(uintptr_t)((ulonglong)scope >> 0x20);
    u32 swapped = (param_3 << 0x18) | ((param_3 & 0xff00) << 8) |
                  ((param_3 >> 8) & 0xff00) | (param_3 >> 0x18);
    (void)param_1;

    for (row = 0; row < desc[1]; row = row + 1) {
        gx_begin_textured_batch(2, 0x91, 0, (short)((desc[0] + 1) * 2), (int)scope);
        col = 0;
        while (col <= desc[0]) {
            cols1 = desc[0] + 1;
            v0 = (u32 *)(desc[2] + (col + row * cols1) * 0x14);
            DAT_cc008000 = v0[0];
            DAT_cc008000 = v0[1];
            DAT_cc008000 = v0[2];
            v1 = (u32 *)(desc[2] + (col + (row + 1) * cols1) * 0x14);
            DAT_cc008000 = swapped;
            DAT_cc008000 = v0[3];
            DAT_cc008000 = v0[4];
            DAT_cc008000 = v1[0];
            DAT_cc008000 = v1[1];
            DAT_cc008000 = v1[2];
            DAT_cc008000 = swapped;
            col = col + 1;
            DAT_cc008000 = v1[3];
            DAT_cc008000 = v1[4];
        }
        gx_end_batch();
    }
    FUN_80382730();
}

// ----------------------------------------------------------------------------
// 0x802f1c00  arcsel_mesh_emit_bordered
// Emits an edge-highlighted sub-strip of the mesh: walks the grid, and for the
// row band that contains the source value (from `src` float array) subdivides
// the cell and emits vertices with a per-fragment color add (ramped by the
// packed RGBA in `rgba`) plus a small vertex offset. `a` (param_1) is an unused
// packed register pair; the real first float comes in via f1 (edge0 below).
// ----------------------------------------------------------------------------
void arcsel_mesh_emit_bordered(double a, double b, double c, u32 *grid,
                               int d, uint color, uint rgba, int src) {
    float add_z;
    double edge0;
    float step_c;
    int *desc;
    uint col_i;
    int base;
    int i0;
    int i1;
    float *p_a;
    float *p_b;
    float *p_c;
    float *p_d;
    int seg;
    int src_off;
    double t_span;
    double du;
    u64 scope;

    (void)a; (void)d; (void)grid;
    scope = FUN_803826b4();
    desc = (int *)(uintptr_t)((ulonglong)scope >> 0x20);
    double one = (double)FLOAT_8061a41c;
    float vtx_count = FLOAT_8061a494 * (float)(one + c);

    double ch_a = (double)(float)((double)CONCAT44(0x43300000, rgba >> 0x18) - DOUBLE_8061a478);
    src_off = 0;
    double ch_g = (double)(float)((double)CONCAT44(0x43300000, (rgba >> 8) & 0xff) - DOUBLE_8061a478);
    double ch_r = (double)(float)((double)CONCAT44(0x43300000, (rgba >> 0x10) & 0xff) - DOUBLE_8061a478);
    double ch_b = (double)(float)((double)CONCAT44(0x43300000, rgba & 0xff) - DOUBLE_8061a478);
    edge0 = one;                 // f1 incoming edge origin (was extraout_f1)
    double bias = DOUBLE_8061a430;

    for (i0 = 0; i0 < *desc; i0 = i0 + 1) {
        double sval = (double)*(float *)(src + src_off);
        double lo = (double)(float)(sval - edge0);
        double hi = sval - lo;
        for (col_i = 0; (int)col_i < desc[1]; col_i = col_i + 1) {
            double rows_d = (double)CONCAT44(0x43300000, desc[1] ^ 0x80000000);
            du = (double)(float)(one / (double)(float)(rows_d - bias));
            double v_lo = (double)(float)((double)(float)((double)CONCAT44(0x43300000, col_i ^ 0x80000000) - bias) * du);
            double v_hi = (double)(float)((double)(float)((double)CONCAT44(0x43300000, (col_i + 1) ^ 0x80000000) - bias) * du);
            if ((v_lo < sval) && (lo < v_hi)) {
                base = desc[2];
                i1 = i0 + col_i * (*desc + 1);
                int i2 = i0 + (col_i + 1) * (*desc + 1);
                p_a = (float *)(base + i1 * 0x14);
                p_b = (float *)(base + (i1 + 1) * 0x14);
                p_c = (float *)(base + i2 * 0x14);
                p_d = (float *)(base + (i2 + 1) * 0x14);
                gx_begin_textured_batch(2, 0x91, 0, (short)(int)vtx_count, (int)scope);
                double one_i = (double)FLOAT_8061a41c;
                seg = 0;
                double sbias = DOUBLE_8061a430;
                double seg_d;
                while (seg_d = (double)(CONCAT44(0x43300000, seg) ^ 0x80000000),
                       (double)(float)(seg_d - sbias) <= c) {
                    double sr = (double)(CONCAT44(0x43300000, seg) ^ 0x80000000);
                    t_span = (double)(float)((double)(float)(sr - sbias) / c);
                    step_c = (float)((double)(float)(t_span * du + v_lo) - lo) / (float)hi;
                    double sq = (double)(step_c * step_c);
                    uint col_frag = color;
                    add_z = FLOAT_8061a418;
                    if ((FLOAT_8061a418 < step_c) && (step_c < FLOAT_8061a41c)) {
                        uint cr = (uint)FUN_803825c0((double)(float)(ch_r * sq));
                        int  ca = FUN_803825c0((double)(float)(ch_a * sq));
                        uint cg = (uint)FUN_803825c0((double)(float)(ch_g * sq));
                        uint cb = (uint)FUN_803825c0((double)(float)(ch_b * sq));
                        col_frag = color_add_saturate(color,
                                                      (cb & 0xff) | ((cg & 0xff) << 8) |
                                                      (ca << 0x18) | ((cr & 0xff) << 0x10));
                        add_z = (float)(b * sq);
                    }
                    float w1 = (float)(one_i - t_span);
                    DAT_cc008000 = (u32)(p_a[0] * w1 + (float)((double)p_c[0] * t_span));
                    DAT_cc008000 = (u32)(p_a[1] * w1 + (float)((double)p_c[1] * t_span));
                    DAT_cc008000 = (u32)(p_a[2] * w1 + (float)((double)p_c[2] * t_span));
                    DAT_cc008000 = (col_frag << 0x18) | ((col_frag & 0xff00) << 8) |
                                   ((col_frag >> 8) & 0xff00) | (col_frag >> 0x18);
                    DAT_cc008000 = (u32)(add_z + p_a[3] * w1 + (float)((double)p_c[3] * t_span));
                    DAT_cc008000 = (u32)(p_a[4] * w1 + (float)((double)p_c[4] * t_span));
                    DAT_cc008000 = (u32)(p_b[0] * w1 + (float)((double)p_d[0] * t_span));
                    DAT_cc008000 = (u32)(p_b[1] * w1 + (float)((double)p_d[1] * t_span));
                    DAT_cc008000 = (u32)(p_b[2] * w1 + (float)((double)p_d[2] * t_span));
                    DAT_cc008000 = (col_frag << 0x18) | ((col_frag & 0xff00) << 8) |
                                   ((col_frag >> 8) & 0xff00) | (col_frag >> 0x18);
                    seg = seg + 1;
                    DAT_cc008000 = (u32)(add_z + p_b[3] * w1 + (float)((double)p_d[3] * t_span));
                    DAT_cc008000 = (u32)(p_b[4] * w1 + (float)((double)p_d[4] * t_span));
                }
                gx_end_batch();
            }
        }
        src_off = src_off + 4;
    }
    FUN_80382700();
}
