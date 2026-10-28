// Arcade select-screen frame backbone: the per-frame button-prompt strip,
// the row frame/separator draw, the load-bar texture loader, the full state
// build (fonts, spacing, colour, element styles + the 3D preview camera rig),
// the per-frame camera/projection tick, the 3D background render, and the
// full-screen load-bar overlay blit.
//
// Batch: batch_834
// Module: arcade-select-frame (new file). Companion to arcade_select_screen.cpp
//   / _render.cpp / _intro_fx.cpp. arcsel_state_build_full is called from
//   arcsel_screen_build; arcsel_camera_matrix_tick from arcsel_viewport_tick;
//   arcsel_render_background from the render dispatch family (0x802e6ec4).
//   Heavy use of hud_style_* (font/spacing/colour/element builders), the
//   mtx4_* matrix helpers, and raw gx_* draw calls. Subsystem: graphics.
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns.

#include "common.h"

// ---- Per-file externs (named cross-module helpers resolve via headers, but
//      these raw FUN_/DAT_ symbols are declared locally with the arity/types
//      used at this file's call sites) ----

// Frame-guard / stack-frame prologue helpers used across this module.
extern int   FUN_803826dc(void);            // begin text-prompt frame -> ctx ptr
extern void  FUN_80382728(void);            // end text-prompt frame
extern u64   FUN_803826e8(void);            // begin build frame -> alloc ctx
extern void  FUN_80382734(void);            // end build frame
extern int   FUN_803825c0(double v);        // alpha byte from float
extern u64   __save_gpr_28(void);
extern void  __restore_gpr_28(void);

// Text / button-prompt helpers.
extern void  text_set_font_table(int table);
extern void  text_prepend_label(byte *dst, u32 arg, int strId);
extern double text_get_char_width(byte *str);
extern int   hud_button_prompt_is_active(int ctx, int idx);
extern int   hud_button_prompt_update_state(void);
extern void  arcsel_draw_row_text(double width, double baseY, byte *strs, uint *colors, int count);

// Row frame draw + scissor / fill helpers.
extern void  gx_set_sub_material_id(int id);
extern void  gx_get_scissor_rect(int *x, int *y, int *w, int *h);
extern void  gx_set_scissor_rect(int x, int y, int w, int h);
extern void  gx_fill_rect_clipped(int x0, int y0, int x1, int y1, int a);
extern void  gx_set_blend_mode(int mode);
extern void  gx_set_active_material(int tex);
extern void  gx_draw_sprite_rect(int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1);
extern u32   gx_set_shadow_enable(int enable);
extern void  gx_set_reflect_matrix(float *mtx);
extern void  gx_material_state_nop(void);
extern void  gx_bind_material_by_index_w(int idx);
extern void  sprite_draw_fullscreen_quad(int x0, int y0, int x1, int y1);
extern u32   color_scale_lo(double factor, u32 color);
extern int   sprite_get_texture_id(int slot);
extern byte *sprite_get_texture_name(int slot);
extern int   is_widescreen(void);
extern int   get_display_width(void);
extern int   get_display_height(void);

// arcsel intra-module helpers (defined in sibling files).
extern void  arcsel_draw_separator(double x0, double y, double x1, double aL, double aR);
extern double arcsel_row_highlight_alpha(int idx);
extern void  arcsel_camera_state_reset(void);
extern void  arcsel_intro_tick(void);
extern void  arcsel_marquee_reset(uint *state);
extern void  arcsel_fx_draw_flash(void);
extern void  arcsel_fx_draw_panel_clamped(void);
extern void  arcsel_draw_row_bar(void);
extern void  player_set_active_count(int n);

// hud_style_* builder chain (used to define the arcade-select widget styles).
extern u64   hud_style_create_font(u64 ctx, double a, double b, double c, double d,
                                   u64 f6, u64 f7, u64 f8, int e, int f, u32 g, int h,
                                   int i, u32 j, u32 k);
extern void  hud_style_font_set_resource(int res);
extern void  hud_style_font_set_scale(double sx, double sy);
extern void  hud_style_font_set_color(double r, double g, double b, double a);
extern void  hud_style_font_set_offset(double ox, double oy);
extern void  hud_style_font_set_shadow(double sx, double sy);
extern void  hud_style_font_set_bounds(double w, double h, int a, int b);
extern void  hud_style_font_set_shadow_color(double r, double g, double b, double a);
extern void  hud_style_font_set_style(int a, int b, int c);
extern void  hud_style_font_set_margins(double a, double b, double c, double d, double e);
extern u64   hud_style_font_set_animation(int a, void *cb1, int b, void *cb2, int c, void *cb3);
extern void  hud_style_font_set_line_height(double h);
extern void  hud_style_font_set_letter_spacing(double s);
extern u64   hud_style_font_set_truncation(void *cb);
extern void  hud_style_font_set_truncation_params(void *a, void *b);
extern void  hud_style_font_set_alignment(int a);
extern u64   hud_style_font_set_case_mode(void *cb);
extern u64   hud_style_create_spacing(u64 ctx, double a, double b, double c, double d,
                                      u64 f6, u64 f7, u64 f8, int e, int f, u32 g, void *h,
                                      u32 i, void *j, u32 k, u32 l);
extern void  hud_style_spacing_set_margin(int idx, u32 a, u32 b);
extern void  hud_style_spacing_set_padding(int idx, u32 a);
extern void  hud_style_spacing_set_gap(int idx, u32 a);
extern void  hud_style_spacing_set_transition(double t, u32 a, u32 b, u32 c, u32 d, u32 e);
extern void  hud_style_spacing_set_scroll(double s, u32 a, u32 b);
extern u64   hud_style_spacing_set_alignment(u32 a, int b, int c);
extern u64   hud_style_create_color(u64 ctx, double a, double b, double c, double d,
                                    u64 f6, u64 f7, u64 f8, u32 g, u32 h, u32 i, u32 j,
                                    u32 k, void *l, u32 m, u32 n);
extern void  hud_style_color_set_rgba(double r, double g, double b, double a);
extern u64   hud_style_create_element(u64 ctx, double a, double b, double c, double d,
                                      u64 f6, u64 f7, u64 f8, int e, int f, u32 g, u32 h,
                                      u32 i, void *j, u32 k, u32 l);
extern void  hud_style_element_set_variants(int a, int b, int c);
extern u64   hud_style_reset_counters(void);

// Memory + matrix helpers.
extern int   mem_alloc(u64 ctx, double a, double b, double c, double d, u64 f6, u64 f7, u64 f8,
                       int size, int align, int flags, int g, int h, u32 i, u32 j, u32 k);
extern void  mtx4_set_perspective(double fov, double aspect, double zn, double zf, float *out);
extern void  mtx4_build_lookat_points(float *out, float *eye, float *at, float *up);
extern void  mtx4_multiply(float *a, float *b, float *out);
extern void  mtx4_transpose(float *src, float *dst);
extern double math_poly_trig(double x);
extern double math_sqrt(double x);

// Asset / pack loaders for the load-bar texture.
extern u64   pack_open(u64 a, double b, double c, u64 d, u64 e, u64 f6, u64 f7, u64 f8,
                       char *name, int g, int h, int i, uint j, u32 k, u32 l, u32 m);
extern u64   FUN_80387fb8(u64 a, double b, double c, u64 d, u64 e, u64 f6, u64 f7, u64 f8,
                          int dst, char *fmt, byte *name, int g, uint h, u32 i, u32 j, u32 k);
extern uint  asset_load_from_dvd(u64 a, double b, double c, u64 d, u64 e, u64 f6, u64 f7, u64 f8,
                                 byte *ctx, int g, uint *h, int i, uint j, u32 k, u32 l, u32 m);
extern int   asset_get_size(u64 a, double b, double c, u64 d, u64 e, u64 f6, u64 f7, u64 f8,
                            byte *ctx);
extern void  FUN_803994d0(uint a, int b);
extern void  render_target_load_from_data(u64 a, double b, double c, u64 d, u64 e, u64 f6, u64 f7,
                                          u64 f8, uint src, int g, int *out, int h, uint i, u32 j,
                                          u32 k, u32 l);

// arcsel builder callbacks referenced as function pointers.
extern void  arcsel_button_prompts_render(void);                       // 0x802e57bc
extern void  arcsel_button_prompts_render_with_cursors(int handle);    // 0x802e5908
extern void  arcsel_screen_push_thunk(void);                           // 0x802e59b4
extern void  arcsel_draw_row_frame(u32 a, u32 b, float *rect);         // 0x802e5a40
extern void  arcsel_load_loadbar_texture(u64 a, double b, double c, u64 d, u64 e, u64 f6,
                                         u64 f7, u64 f8);              // 0x802e5c7c
extern void  arcsel_preview_add_billboard(double a, double b, double c, double d, double e,
                                          double f, double g, double h, int slot, int i, int j,
                                          int tex, int k, int l, u32 m, u32 n);          // 0x802e7098
extern void  arcsel_preview_add_grid(double a, double b, double c, double d, double e, double f,
                                     double g, double h, int slot, int i, int j, int list,
                                     u32 k, u32 l, u32 m, u32 n, float o, int p);        // 0x802e75f0
extern void  arcsel_apply_aspect_layout(double a, double b);           // 0x802e7948
extern void  arcsel_element_transform_tick(double t, int slot);        // 0x802e741c
extern void  arcsel_element_matrix_tick(double t, int slot);           // 0x802e7b18
extern void  arcsel_render_element(int slot, float *cam, float *ctx);  // 0x802e7bb0
extern void  arcsel_render_billboard_element(int slot, float *cam, float *ctx); // 0x802e7460
extern void  arcsel_render_loadbar_overlay(void);                      // 0x802e6e28
extern void  arcsel_render_bg_dispatch(void);                          // 0x802e6ec4

// ---- Raw data symbols ----
extern int   DAT_80612b64;              // arcsel owner object ptr (int view)
extern int  *DAT_80611748;              // scissor/layout rect array
extern int   DAT_806109c4;              // screen width
extern int   DAT_806109c8;              // screen height
extern int   DAT_806109c0;              // frame-rate scale
extern int  *DAT_80610a20;              // sprite descriptor table base ptr
extern int   DAT_805cc614;              // current screen phase
extern int   DAT_805cc630;              // current character slot index
extern int   DAT_805cc6e0;              // intro-active flag (>=0 => run intro)
extern int   DAT_80612614;              // current arcade mode id
extern int   DAT_806110b8;              // load-bar preview texture handle
extern int   DAT_80612250;             // gx tex-matrix-share flag
extern int   DAT_80612258;             // gx tex-matrix index
extern char  s_pak_loadbar_pak_804773b8[];
extern char  s_assets_ts_textures_gamecube__s_g_804773c8[];
extern int   DAT_803f2930;              // per-mode texture-id table base
extern float DAT_803e0918;              // default up-vector.x
extern int   DAT_803e091c;              // default up-vector.y (raw word)
extern int   DAT_803e0920;              // default up-vector.z (raw word)
extern int   DAT_8058e3cc;              // active view matrix dst

// Scalar float/double literals from the .rodata pool (0x8061a1xx region).
extern float FLOAT_8061a180;   // 0.0f
extern float FLOAT_8061a184;   // 1.0f
extern float FLOAT_8061a188;   // 0.5f
extern float FLOAT_8061a1a4;
extern float FLOAT_8061a1a8;
extern float FLOAT_8061a1ac;
extern float FLOAT_8061a1dc;
extern float FLOAT_8061a1e4;
extern float FLOAT_8061a1e8;
extern float FLOAT_8061a204;
extern float FLOAT_8061a20c;
extern float FLOAT_8061a218;
extern float FLOAT_8061a220;
extern float FLOAT_8061a224;
extern float FLOAT_8061a228;
extern float FLOAT_8061a22c;
extern float FLOAT_8061a230;
extern float FLOAT_8061a234;
extern float FLOAT_8061a238;
extern float FLOAT_8061a23c;
extern float FLOAT_8061a240;
extern float FLOAT_8061a244;
extern float FLOAT_8061a248;
extern float FLOAT_8061a24c;
extern float FLOAT_8061a250;
extern float FLOAT_8061a254;
extern float FLOAT_8061a258;
extern float FLOAT_8061a25c;
extern float FLOAT_8061a260;
extern float FLOAT_8061a264;
extern float FLOAT_8061a268;
extern float FLOAT_8061a26c;
extern float FLOAT_8061a270;
extern float FLOAT_8061a274;
extern float FLOAT_8061a278;
extern float FLOAT_8061a27c;
extern float FLOAT_8061a280;
extern float FLOAT_8061a284;
extern float FLOAT_8061a288;
extern float FLOAT_8061a28c;
extern float FLOAT_8061a290;
extern float FLOAT_8061a294;
extern float FLOAT_8061a298;
extern float FLOAT_8061a29c;
extern float FLOAT_8061a2a0;
extern float FLOAT_8061a2a4;
extern float FLOAT_8061a2a8;
extern float FLOAT_8061a2ac;
extern float FLOAT_8061a2b0;
extern float FLOAT_8061a2b4;
extern float FLOAT_8061a2b8;
extern float FLOAT_8061a2bc;
extern float FLOAT_8061a2c0;
extern float FLOAT_8061a2c4;
extern float FLOAT_8061a2c8;
extern double DOUBLE_8061a190;   // 4503599627370496.0 (u32->double bias)
extern float FLOAT_80611a64;
extern float FLOAT_80611a6c;
extern float FLOAT_806104a0;
extern float FLOAT_806104a4;
extern float FLOAT_806108e8;
extern float FLOAT_80611a5c;
extern float FLOAT_80612b4c;
extern float FLOAT_80612b50;
extern float FLOAT_80612b54;
extern float FLOAT_80612b58;
extern float FLOAT_80612b5c;

// Owner object as a float array view. DAT_80612b64 stores the object *base
// pointer* (allocated in arcsel_state_build_full); ARCSEL indexes off that
// pointer value, matching the raw `DAT_80612b64 + off` byte accesses.
#define ARCSEL ((float *)DAT_80612b64)

// Convert the biased-u32-to-double idiom Ghidra emits into a plain float cast.
static inline float u32_to_f(unsigned int v) { return (float)v; }

// ---------------------------------------------------------------------------
// Address: 0x802e57bc | Name: arcsel_button_prompts_render
// Builds the bottom button-prompt strip: for each of 4 prompt slots that are
// active, prepend the button-glyph label, measure width, pick a colour (dim
// 0x28 vs bright 0x64 by pressed state), then hand off to arcsel_draw_row_text.
void arcsel_button_prompts_render(void)
{
    int   ctx;
    int   slotRec;
    uint  alpha;
    int   activeCount;
    int   colorOff;
    int   strOff;
    int   slot;
    int   slotStride;
    int   pressed;
    float totalWidth;
    uint  colors[4];
    byte  strBuf[560];

    ctx = FUN_803826dc();
    totalWidth = FLOAT_8061a180;
    activeCount = 0;
    strOff = 0;
    colorOff = 0;
    text_set_font_table(0x15);
    slot = 0;
    slotStride = 0;
    do {
        slotRec = ctx + slotStride;
        if ((*(int *)(slotRec + 0x124) != -1) &&
            (hud_button_prompt_is_active(ctx, slot) != 0)) {
            text_prepend_label(strBuf + strOff, *(u32 *)(slotRec + 0x128),
                               *(int *)(slotRec + 0x124));
            totalWidth = (float)(totalWidth + (float)text_get_char_width(strBuf + strOff));
            pressed = hud_button_prompt_update_state();
            alpha = (uint)FUN_803825c0((double)(FLOAT_8061a1a4 * *(float *)(ctx + 0xf0)));
            if (pressed == 0) {
                colors[colorOff >> 2] = (alpha & 0xff) | 0x28282800;
            } else {
                colors[colorOff >> 2] = (alpha & 0xff) | 0x64646400;
            }
            if (activeCount > 0) {
                totalWidth = totalWidth + FLOAT_8061a20c;
            }
            activeCount = activeCount + 1;
            colorOff = colorOff + 4;
            strOff = strOff + 0x80;
        }
        slot = slot + 1;
        slotStride = slotStride + 0x10;
    } while (slot < 4);

    arcsel_draw_row_text(totalWidth, (double)FLOAT_8061a180, strBuf, colors, activeCount);
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// Address: 0x802e5908 | Name: arcsel_button_prompts_render_with_cursors
// Renders the prompt strip, then (when the screen is open and phase==5) draws
// the player "ready" cursors positioned by the current scroll offset.
void arcsel_button_prompts_render_with_cursors(int handle)
{
    extern int hud_element_is_closing(int h);
    extern void menu_loadout_ready_cursors_render(int a, int b, int y);

    arcsel_button_prompts_render();
    if ((hud_element_is_closing(handle) == 0) && (DAT_805cc614 == 5)) {
        float scroll = *(float *)(DAT_80612b64 + 0x650);
        int y = (int)((FLOAT_8061a218 + (FLOAT_8061a184 - scroll)) *
                      (float)((float)(unsigned)DAT_806109c8));
        menu_loadout_ready_cursors_render(0x14, DAT_806109c4 - 0x14, y);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x802e59b4 | Name: arcsel_screen_push_thunk
void arcsel_screen_push_thunk(void)
{
    extern void hud_screen_push(void);
    hud_screen_push();
}

// ---------------------------------------------------------------------------
// Address: 0x802e59e0 | Name: arcsel_apply_aspect_layout
// Applies the 4:3 or 16:9 layout offsets and resets the screen scroll field.
void arcsel_apply_aspect_layout(double param_1, double param_2)
{
    (void)param_1;
    (void)param_2;
    if ((is_widescreen() & 0xff) == 0) {
        arcsel_apply_aspect_layout((double)FLOAT_8061a228, (double)FLOAT_8061a22c);
    } else {
        arcsel_apply_aspect_layout((double)FLOAT_8061a220, (double)FLOAT_8061a224);
    }
    *(float *)(DAT_80612b64 + 0x674) = FLOAT_8061a180;
}

// ---------------------------------------------------------------------------
// Address: 0x802e5a40 | Name: arcsel_draw_row_frame
// Draws a scissor-clipped translucent quad for one row plus four separator
// edges around it. rect = {x0,y0,x1,y1}.
void arcsel_draw_row_frame(u32 param_1, u32 param_2, float *rect)
{
    int   ctx;
    bool  clipped;
    u32   fillColor;
    int   scX, scY, scW, scH;
    float edge;
    double x0, y0, x1, y1, midX;

    (void)param_1;
    (void)param_2;
    ctx = __save_gpr_28();
    clipped = false;
    midX = (double)(FLOAT_8061a1ac * (rect[2] - rect[0]) + rect[0]);
    fillColor = color_scale_lo((double)*(float *)(ctx + 0xf0), 0x2f3c503c);
    gx_set_sub_material_id(2);
    gx_get_scissor_rect(&scX, &scY, &scW, &scH);
    edge = *(float *)(ctx + 0xe4);
    y0 = (double)rect[1];
    y1 = (double)rect[3];
    x0 = (double)rect[0];
    x1 = (double)rect[2];
    if ((double)(*(float *)(*(int *)(ctx + 0x1c) + 0x10) * FLOAT_80611a64 + edge) < y0) {
        edge = *(float *)(ctx + 0xec);
        if (-(double)(*(float *)(*(int *)(ctx + 0x1c) + 0x18) * FLOAT_80611a64 - edge) <= y1) {
            scH = (int)edge;
            clipped = true;
            y1 = (double)(edge - FLOAT_8061a230);
        }
    } else {
        scY = (int)edge;
        clipped = true;
        y0 = (double)(FLOAT_8061a230 + edge);
    }
    if (clipped) {
        gx_set_scissor_rect(scX, scY, scW, scH);
    }
    gx_fill_rect_clipped((int)x0, (int)y0, (int)x1, (int)y1, fillColor);
    arcsel_draw_separator(x0, y0, midX, (double)FLOAT_8061a180,
                          (double)(FLOAT_8061a1ac * *(float *)(ctx + 0xf0)));
    arcsel_draw_separator(midX, y0, x1, (double)(FLOAT_8061a1ac * *(float *)(ctx + 0xf0)),
                          (double)FLOAT_8061a180);
    arcsel_draw_separator(x0, y1, midX, (double)FLOAT_8061a180,
                          (double)(FLOAT_8061a1ac * *(float *)(ctx + 0xf0)));
    arcsel_draw_separator(midX, y1, x1, (double)(FLOAT_8061a1ac * *(float *)(ctx + 0xf0)),
                          (double)FLOAT_8061a180);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x802e5c7c | Name: arcsel_load_loadbar_texture
// Opens loadbar.pak, resolves the per-mode background texture name, streams it
// from DVD, and installs it as the load-bar preview render target
// (DAT_806110b8). Falls back to a static texture id on failure.
void arcsel_load_loadbar_texture(u64 param_1, double param_2, double param_3, u64 param_4,
                                 u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    int   texId;
    byte *texName;
    uint  loaded;
    u64   handle;
    byte  ctxBuf[132];

    handle = pack_open(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                       s_pak_loadbar_pak_804773b8, 0, 1, 0, 0, 0, 0, 0);
    if (DAT_80612614 == 0x6b) {
        texId = 0x18e;
    } else {
        texId = *(int *)((byte *)&DAT_803f2930 + DAT_805cc630 * 0x44);
    }
    texName = sprite_get_texture_name(texId);
    handle = FUN_80387fb8(handle, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                          (int)ctxBuf, s_assets_ts_textures_gamecube__s_g_804773c8, texName,
                          0, 0, 0, 0, 0);
    loaded = asset_load_from_dvd(handle, param_2, param_3, param_4, param_5, param_6, param_7,
                                 param_8, ctxBuf, 0, (uint *)0, 0, 0, 0, 0, 0);
    if (-1 < asset_get_size(handle, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            ctxBuf)) {
        int sz = asset_get_size(handle, param_2, param_3, param_4, param_5, param_6, param_7,
                                param_8, ctxBuf);
        FUN_803994d0(loaded, sz);
    }
    if (loaded == 0) {
        DAT_806110b8 = sprite_get_texture_id(0x333);
    } else {
        render_target_load_from_data(handle, param_2, param_3, param_4, param_5, param_6, param_7,
                                     param_8, loaded, 0, &DAT_806110b8, 0, 0, 0, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x802e5d68 | Name: arcsel_state_build_full
// Full arcade-select state build: allocates the owner state block, defines the
// row/label font + spacing + colour + element styles (wiring the fx/draw
// callbacks), and — for gameplay modes — resets the 3D preview camera and
// registers all the preview billboards/grids for the character stand.
void arcsel_state_build_full(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                             u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    int    i;
    int    stride;
    int    inner;
    int    row;
    int    idx;
    u64    ctx;
    double d0, d1, d2, d3;
    int    texSlot[20];
    int    texSlot2[20];
    int    marqueeBuf[2];
    uint   biasHi;
    double alpha, scroll, span;

    ctx = FUN_803826e8();

    // Zero two 0x50-byte rows of the local texture-id scratch.
    row = 0;
    stride = 0;
    do {
        inner = 0;
        int base = 0;
        do {
            *(int *)((int)texSlot + stride + base) = 0;
            base = base + 4;
            inner = inner + 1;
        } while (inner < 0x14);
        row = row + 1;
        stride = stride + 0x50;
    } while (row < 2);

    DAT_80612b64 = mem_alloc(ctx, 0, 0, 0, 0, param_6, param_7, param_8, 0xa90, 1, 0, 0, 0, 0, 0, 0);

    // --- row/label font style ---
    ctx = hud_style_create_font(ctx, 0, 0, 0, 0, param_6, param_7, param_8, 0, 0xffffffff, 0,
                                0, 0, 0, 0);
    hud_style_font_set_resource(0x5020);
    hud_style_font_set_scale((double)FLOAT_8061a1a8, (double)FLOAT_8061a1a8);
    hud_style_font_set_color((double)FLOAT_8061a184, (double)FLOAT_8061a234,
                             (double)FLOAT_8061a184, (double)FLOAT_8061a1a8);
    hud_style_font_set_offset((double)FLOAT_8061a238, (double)FLOAT_8061a238);
    hud_style_font_set_shadow((double)FLOAT_8061a1dc, (double)FLOAT_8061a1ac);
    hud_style_font_set_bounds((double)FLOAT_8061a23c, (double)FLOAT_8061a180, 1, 0);
    hud_style_font_set_shadow_color((double)FLOAT_8061a180, (double)FLOAT_8061a240,
                                    (double)FLOAT_8061a188, (double)FLOAT_8061a188);
    hud_style_font_set_style(0x11, 3, 4);
    hud_style_font_set_margins((double)FLOAT_8061a244, (double)FLOAT_8061a1e8,
                               (double)FLOAT_8061a1e8, (double)FLOAT_8061a180,
                               (double)FLOAT_8061a180);
    ctx = hud_style_font_set_animation(0, (void *)arcsel_fx_draw_panel_clamped, 0,
                                       (void *)arcsel_fx_draw_flash, 0, (void *)arcsel_draw_row_frame);

    // --- row spacing style ---
    hud_style_create_spacing(ctx, (double)FLOAT_8061a1e8, (double)FLOAT_8061a1e8,
                             (double)FLOAT_8061a180, (double)FLOAT_8061a180, param_6, param_7,
                             param_8, 0, 0xffffffff, 0, (void *)arcsel_fx_draw_flash, 0,
                             (void *)arcsel_draw_row_frame, 0, 0);
    hud_style_spacing_set_margin(0, 0x7878787f, 0x1e1e1e7f);
    hud_style_spacing_set_margin(1, 0x7f7f7f7f, 0x5235a50);
    hud_style_spacing_set_margin(2, 0x7f7f7f7f, 0x7f7f7f7f);
    hud_style_spacing_set_margin(3, 0x2828287f, 0x2828287f);
    hud_style_spacing_set_margin(4, 0x28283250, 0xa0a1e40);
    hud_style_spacing_set_padding(0, 0x1e2d4646);
    hud_style_spacing_set_padding(1, 0x2d4b697f);
    hud_style_spacing_set_padding(2, 0x69737f7f);
    hud_style_spacing_set_padding(3, 0x1e283237);
    hud_style_spacing_set_gap(2, 0x120a1b7f);
    hud_style_spacing_set_gap(3, 0x7f);
    hud_style_spacing_set_gap(4, 0x14383c);
    hud_style_spacing_set_transition((double)FLOAT_8061a248, 0x2d2f377f, 0x2d2f377f, 0x14151e7f,
                                     0x14141e7f, 0x7f);
    hud_style_spacing_set_scroll((double)FLOAT_8061a24c, 0x484a5a7f, 0x1a1b227f);
    ctx = hud_style_spacing_set_alignment(0x1e235a00, 0, 0);

    // --- highlight colour style ---
    hud_style_create_color(ctx, (double)FLOAT_8061a1e8, (double)FLOAT_8061a1e8,
                           (double)FLOAT_8061a180, (double)FLOAT_8061a180, param_6, param_7,
                           param_8, 0xffffffff, 0xffffffff, 0, 0x14141e7f, 0x7f,
                           (void *)arcsel_draw_row_frame, 0, 0);
    hud_style_color_set_rgba((double)FLOAT_8061a20c, (double)FLOAT_8061a20c,
                             (double)FLOAT_8061a20c, (double)FLOAT_8061a20c);

    // --- scrolling label font (drives the marquee via row-highlight alpha) ---
    alpha = arcsel_row_highlight_alpha(1);
    biasHi = (uint)DAT_80611748[8] ^ 0x80000000;
    scroll = (double)u32_to_f(biasHi);
    span = (double)(FLOAT_80611a5c * (float)(scroll * alpha - (double)FLOAT_80611a6c));
    *(float *)(DAT_80612b64 + 0x650) = (float)arcsel_row_highlight_alpha(1);
    hud_style_font_set_line_height((double)FLOAT_8061a250);
    hud_style_font_set_letter_spacing((double)(float)((double)FLOAT_8061a204 - span));
    hud_style_font_set_truncation((void *)arcsel_button_prompts_render);
    hud_style_font_set_truncation_params((void *)arcsel_draw_row_bar, (void *)arcsel_draw_row_bar);
    hud_style_font_set_alignment(0);
    ctx = hud_style_font_set_case_mode((void *)arcsel_screen_push_thunk);
    ctx = hud_style_create_font(ctx, 0, 0, 0, 0, param_6, param_7, param_8, 1, 0, 0, 0, 0, 0, 0);
    hud_style_font_set_style(0x11, 0x15, 4);

    // --- list element style (2 variants) ---
    ctx = hud_style_create_element(ctx, 0, 0, 0, 0, param_6, param_7, param_8, 0, 0xffffffff, 4,
                                   0, 0, 0, 0, 0);
    hud_style_element_set_variants(-1, 0, 0);
    hud_style_element_set_variants(5, 1, 0);

    // --- value/icon font ---
    ctx = hud_style_create_font(ctx, 0, 0, 0, 0, param_6, param_7, param_8, 2, 0, 0, 0, 0, 0, 0);
    hud_style_font_set_resource(0x1000);
    hud_style_font_set_scale((double)FLOAT_8061a180, (double)FLOAT_8061a180);
    hud_style_font_set_color((double)FLOAT_8061a180, (double)FLOAT_8061a180,
                             (double)FLOAT_8061a180, (double)FLOAT_8061a180);
    hud_style_font_set_offset((double)FLOAT_8061a180, (double)FLOAT_8061a180);
    hud_style_font_set_animation(0, 0, 0, 0, 0, 0);
    ctx = hud_style_font_set_truncation((void *)arcsel_button_prompts_render_with_cursors);
    hud_style_create_element(ctx, 0, 0, 0, 0, param_6, param_7, param_8, 1, 0, 0, 0, 0, 0, 0, 0);
    hud_style_element_set_variants(-1, 2, 0);

    ctx = hud_style_reset_counters();

    if ((DAT_80612614 == 0x6a) || (DAT_80612614 == 0x6b)) {
        arcsel_load_loadbar_texture(ctx, 0, 0, 0, 0, param_6, param_7, param_8);
        FUN_80382734();
        return;
    }

    // --- gameplay-mode 3D preview camera + billboard/grid rig ---
    arcsel_camera_state_reset();
    FLOAT_80612b5c = FLOAT_8061a180;
    player_set_active_count(1);
    FLOAT_80612b58 = FLOAT_8061a180;
    FLOAT_80612b50 = FLOAT_8061a180;
    FLOAT_80612b54 = FLOAT_8061a180;
    FLOAT_80612b4c = FLOAT_8061a180;

    texSlot[0] = sprite_get_texture_id(0x2d7);
    texSlot[1] = sprite_get_texture_id(0x2d8);
    texSlot[4] = sprite_get_texture_id(0x2ca);
    texSlot[9] = sprite_get_texture_id(0x2c0);
    texSlot[10] = sprite_get_texture_id(0x2c6);
    texSlot[11] = sprite_get_texture_id(0x2c1);
    texSlot[12] = sprite_get_texture_id(0x2c3);
    texSlot[13] = sprite_get_texture_id(0x2c5);
    texSlot[14] = sprite_get_texture_id(0xb);
    texSlot[15] = sprite_get_texture_id(0x2be);
    texSlot2[0] = sprite_get_texture_id(0x2d9);
    texSlot2[1] = sprite_get_texture_id(0x2c1);
    texSlot2[2] = sprite_get_texture_id(0x2da);
    texSlot2[3] = sprite_get_texture_id(0x2db);

    arcsel_preview_add_billboard((double)FLOAT_8061a248, (double)FLOAT_8061a188,
                                 (double)FLOAT_8061a254, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a180, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a1a8, (double)FLOAT_8061a258,
                                 DAT_80612b64 + 0x140, 3, 0x31, texSlot2[2], 0, 1, 0x547f7f7f, 0);
    arcsel_preview_add_billboard((double)FLOAT_8061a248, (double)FLOAT_8061a25c,
                                 (double)FLOAT_8061a254, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a180, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a1a8, (double)FLOAT_8061a258,
                                 DAT_80612b64 + 0x168, 3, 0x31, texSlot2[2], 0, 1, 0x547f7f7f, 0);
    arcsel_preview_add_billboard((double)FLOAT_8061a248, (double)FLOAT_8061a260,
                                 (double)FLOAT_8061a264, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a180, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a23c, (double)FLOAT_8061a23c,
                                 DAT_80612b64 + 400, 3, 0x31, texSlot2[3], 0, 0, 0x547f7f7f, 0);

    // Copy two texSlot2 ids into the marquee grid list.
    i = 0;
    inner = 2;
    stride = 0;
    do {
        *(int *)((int)marqueeBuf + i) = *(int *)((int)texSlot2 + stride);
        i = i + 4;
        inner = inner + -1;
        stride = stride + 0x50;
    } while (inner != 0);

    if ((is_widescreen() & 0xff) == 0) {
        arcsel_preview_add_grid((double)FLOAT_8061a268, (double)FLOAT_8061a258,
                                (double)FLOAT_8061a26c, (double)FLOAT_8061a228,
                                (double)FLOAT_8061a22c, (double)FLOAT_8061a184,
                                (double)FLOAT_8061a270, (double)FLOAT_8061a180,
                                DAT_80612b64 + 0x1b8, 8, 8, (int)marqueeBuf, 0, 0, 0x547f7f7f, 0,
                                FLOAT_8061a184, (int)FLOAT_8061a274);
    } else {
        arcsel_preview_add_grid((double)FLOAT_8061a268, (double)FLOAT_8061a258,
                                (double)FLOAT_8061a26c, (double)FLOAT_8061a220,
                                (double)FLOAT_8061a224, (double)FLOAT_8061a184,
                                (double)FLOAT_8061a270, (double)FLOAT_8061a180,
                                DAT_80612b64 + 0x1b8, 8, 8, (int)marqueeBuf, 0, 0, 0x547f7f7f, 0,
                                FLOAT_8061a184, (int)FLOAT_8061a274);
    }

    *(float *)(DAT_80612b64 + 0x1d8) = FLOAT_8061a180;
    arcsel_preview_add_billboard((double)FLOAT_8061a248, (double)FLOAT_8061a278,
                                 (double)FLOAT_8061a1ac, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a180, (double)FLOAT_8061a27c,
                                 (double)FLOAT_8061a23c, (double)FLOAT_8061a23c,
                                 DAT_80612b64 + 200, 3, 0x31, texSlot[0], 0, 0, 0x7f7f7f7f, 0);
    arcsel_preview_add_billboard((double)FLOAT_8061a248, (double)FLOAT_8061a278,
                                 (double)FLOAT_8061a188, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a180, (double)FLOAT_8061a280,
                                 (double)FLOAT_8061a1a8, (double)FLOAT_8061a1a8,
                                 DAT_80612b64 + 0x118, 3, 0x31, texSlot2[1], 0, 0, 0x7f7f7f7f, 0);
    arcsel_preview_add_billboard((double)FLOAT_8061a248, (double)FLOAT_8061a284,
                                 (double)FLOAT_8061a254, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a180, (double)FLOAT_8061a184,
                                 (double)FLOAT_8061a23c, (double)FLOAT_8061a23c,
                                 DAT_80612b64 + 0xf0, 3, 0x31, texSlot[1], 0, 0, 0x7f7f7f7f, 0);

    *(float *)(DAT_80612b64 + 0x104) = FLOAT_8061a288;
    *(float *)(DAT_80612b64 + 0x1a4) = FLOAT_8061a274;
    *(float *)(DAT_80612b64 + 0xdc) = FLOAT_8061a28c;
    *(float *)(DAT_80612b64 + 300) = FLOAT_8061a180;

    texSlot[0] = sprite_get_texture_id(0x2c7);
    texSlot[1] = sprite_get_texture_id(0x2c8);
    texSlot[2] = sprite_get_texture_id(0x2c9);
    texSlot[3] = sprite_get_texture_id(0x2ca);
    texSlot[4] = sprite_get_texture_id(0x2cb);
    texSlot[5] = sprite_get_texture_id(0x2cc);
    texSlot[6] = sprite_get_texture_id(0x2cd);
    texSlot[7] = sprite_get_texture_id(0x2ce);
    texSlot[8] = sprite_get_texture_id(0x2ce);
    texSlot[9] = sprite_get_texture_id(0x2dc);
    texSlot2[8]  = sprite_get_texture_id(0x2cf);
    texSlot2[9]  = sprite_get_texture_id(0x2d0);
    texSlot2[10] = sprite_get_texture_id(0x2d1);
    texSlot2[11] = sprite_get_texture_id(0x2d2);
    texSlot2[12] = sprite_get_texture_id(0x2d3);
    texSlot2[13] = sprite_get_texture_id(0x2d4);
    texSlot2[14] = sprite_get_texture_id(0x2d5);
    texSlot2[15] = sprite_get_texture_id(0x2d6);

    *(float *)(DAT_80612b64 + 0x63c) = FLOAT_8061a184;
    *(float *)(DAT_80612b64 + 0xc0) = FLOAT_8061a184;
    *(float *)(DAT_80612b64 + 0xc4) = FLOAT_8061a180;
    *(float *)(DAT_80612b64 + 0xa84) = FLOAT_8061a180;

    // Left-column billboards (8 rows).
    idx = 0;
    stride = 0;
    do {
        i = 0;
        inner = 2;
        do {
            *(int *)((int)marqueeBuf + i) = texSlot[9];
            i = i + 4;
            inner = inner + -1;
        } while (inner != 0);
        arcsel_preview_add_grid((double)FLOAT_8061a248, (double)FLOAT_8061a278,
                                (double)FLOAT_8061a290, (double)FLOAT_8061a294,
                                (double)FLOAT_8061a298, (double)FLOAT_8061a184,
                                (double)FLOAT_8061a270, (double)FLOAT_8061a180,
                                DAT_80612b64 + stride + 0x1fc, 4, 4, (int)marqueeBuf, 0, 0,
                                0x547f7f7f, 0, FLOAT_8061a184, (int)FLOAT_8061a274);
        biasHi = (uint)idx ^ 0x80000000;
        *(float *)(DAT_80612b64 + stride + 0x21c) =
            (float)((double)FLOAT_8061a29c * (double)u32_to_f(biasHi));
        *(float *)(DAT_80612b64 + stride + 0x220) = FLOAT_8061a2a0;
        idx = idx + 1;
        stride = stride + 0x44;
    } while (idx < 8);

    // Right-column billboards (8 rows).
    idx = 0;
    stride = 0;
    inner = 0;
    do {
        i = 0;
        int col = 0;
        int cnt = 2;
        do {
            *(int *)((int)marqueeBuf + i) = *(int *)((int)texSlot + (col + inner));
            col = col + 0x50;
            i = i + 4;
            cnt = cnt + -1;
        } while (cnt != 0);
        arcsel_preview_add_grid((double)FLOAT_8061a248, (double)FLOAT_8061a278,
                                (double)FLOAT_8061a2a4, (double)FLOAT_8061a2a8,
                                (double)FLOAT_8061a2ac, (double)FLOAT_8061a184,
                                (double)FLOAT_8061a270, (double)FLOAT_8061a180,
                                DAT_80612b64 + stride + 0x41c, 4, 4, (int)marqueeBuf, 0, 0,
                                0x547f7f7f, 0, FLOAT_8061a184, (int)FLOAT_8061a274);
        biasHi = (uint)(-idx) ^ 0x80000000;
        *(float *)(DAT_80612b64 + stride + 0x43c) =
            (float)((double)FLOAT_8061a29c * (double)u32_to_f(biasHi));
        *(float *)(DAT_80612b64 + stride + 0x440) = FLOAT_8061a2b0;
        idx = idx + 1;
        stride = stride + 0x44;
        inner = inner + 4;
    } while (idx < 8);

    arcsel_marquee_reset((uint *)(DAT_80612b64 + 0x678));
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// Address: 0x802e67f8 | Name: arcsel_camera_matrix_tick
// Per-frame update of the preview camera: advances the orbit phase, builds the
// perspective + look-at matrices (aspect-corrected), applies a small sinusoidal
// sway, folds in popup open/close fade, then ticks every preview element's
// transform and per-cell matrix.
void arcsel_camera_matrix_tick(void)
{
    float  dt;
    int    destroyed;
    int    popup;
    double popupH, fadePopup, fadeDestroy, blend;
    double eye[3], at[3], up[3];
    double aspectNum, aspectDen, wDen, wNum, hDen;

    dt = (float)__save_gpr_28();

    // Phase advance scaled by frame-rate.
    float invFrames = FLOAT_8061a184 / u32_to_f(DAT_806109c0 ^ 0x80000000);
    double phaseAdv = (double)(FLOAT_806108e8 * invFrames);

    // Aspect ratio correction terms.
    wNum  = (double)u32_to_f((uint)DAT_80611748[3] ^ 0x80000000);
    wDen  = (double)u32_to_f((uint)get_display_width() ^ 0x80000000);
    hDen  = (double)u32_to_f((uint)get_display_height() ^ 0x80000000);
    aspectDen = (double)u32_to_f((uint)DAT_80611748[8] ^ 0x80000000);
    aspectNum = (double)u32_to_f((uint)DAT_80611748[3] ^ 0x80000000);

    eye[0] = (double)FLOAT_8061a184;   // placeholder set below via at/up arrays
    up[0]  = (double)FLOAT_8061a180;
    up[1]  = (double)FLOAT_8061a180;
    up[2]  = (double)FLOAT_8061a270;
    at[0]  = (double)FLOAT_8061a2b4;
    at[1]  = (double)FLOAT_8061a180;
    at[2]  = (double)FLOAT_8061a180;
    eye[1] = (double)FLOAT_8061a2b4;
    eye[2] = (double)FLOAT_8061a184;   // (mirrors decompiler local layout)

    ARCSEL[0x31] = (float)((double)ARCSEL[0x31] + phaseAdv);

    mtx4_set_perspective((double)((float)((float)((float)wNum / (float)aspectDen) /
                                          (float)(wDen / hDen)) *
                                  (float)((float)aspectNum / (float)aspectDen)),
                         (double)FLOAT_8061a2b8, (double)FLOAT_806104a0, (double)FLOAT_806104a4,
                         ARCSEL + 0x20);
    {
        float e[3], a[3], u[3];
        a[0] = FLOAT_8061a2b4; a[1] = FLOAT_8061a180; a[2] = FLOAT_8061a180;
        e[0] = FLOAT_8061a180; e[1] = FLOAT_8061a180; e[2] = FLOAT_8061a184;
        u[0] = FLOAT_8061a180; u[1] = FLOAT_8061a180; u[2] = FLOAT_8061a270;
        mtx4_build_lookat_points(ARCSEL, a, e, u);
        mtx4_multiply(ARCSEL, ARCSEL + 0x20, ARCSEL + 0x10);

        double s1 = math_poly_trig((double)(FLOAT_8061a2c0 * ARCSEL[0x31] - FLOAT_8061a2c4));
        a[0] = (float)((double)FLOAT_8061a2bc * s1 + (double)a[0]);
        double s2 = math_poly_trig((double)(FLOAT_8061a188 * (FLOAT_8061a1ac + ARCSEL[0x31]) -
                                            FLOAT_8061a2c4));
        a[2] = (float)((double)FLOAT_8061a2c8 * s2 + (double)a[2]);
        mtx4_build_lookat_points(ARCSEL, a, e, u);
    }

    if (-1 < DAT_805cc6e0) {
        arcsel_intro_tick();
    }

    destroyed = 0; { extern int hud_screen_destroy_current(void); destroyed = hud_screen_destroy_current(); }
    { extern int hud_screen_find_current_popup(void); popup = hud_screen_find_current_popup(); }
    { extern double hud_screen_get_popup_height(void); popupH = hud_screen_get_popup_height(); }

    fadePopup   = (double)FLOAT_8061a184;
    fadeDestroy = fadePopup;
    if ((popup != 0) && ((*(uint *)(popup + 0x40) & 0x40000000) != 0)) {
        fadePopup = (double)FLOAT_8061a180;
    }
    if ((destroyed != 0) && ((*(uint *)(destroyed + 0x40) & 0x40000000) != 0)) {
        fadeDestroy = (double)FLOAT_8061a180;
    }
    blend = (double)(float)(fadePopup * (double)(float)((double)FLOAT_8061a184 - popupH) +
                            (double)(float)(fadeDestroy * popupH));
    if ((double)FLOAT_8061a180 < blend) {
        blend = math_sqrt(blend);
    }
    ARCSEL[0x192] = (float)((double)FLOAT_8061a1e4 * blend);
    ARCSEL[0x191] = (float)blend;
    ARCSEL[400] = dt;
    ARCSEL[399] = FLOAT_806108e8;

    arcsel_element_transform_tick((double)ARCSEL[399], (int)(ARCSEL + 0x32));
    arcsel_element_transform_tick((double)ARCSEL[399], (int)(ARCSEL + 0x3c));
    arcsel_element_transform_tick((double)ARCSEL[399], (int)(ARCSEL + 100));
    arcsel_element_transform_tick((double)ARCSEL[399], (int)(ARCSEL + 0x50));
    arcsel_element_transform_tick((double)ARCSEL[399], (int)(ARCSEL + 0x5a));

    {
        int r = 0, rowOff = 0;
        do {
            int c = 0, cellOff = 0;
            do {
                arcsel_element_matrix_tick((double)ARCSEL[399],
                                           (int)DAT_80612b64 + rowOff + cellOff + 0x1fc);
                c = c + 1;
                cellOff = cellOff + 0x44;
            } while (c < 8);
            r = r + 1;
            rowOff = rowOff + 0x220;
        } while (r < 2);
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x802e6b60 | Name: arcsel_render_background
// Renders the arcade-select 3D background: reflected fill, the preview grid
// elements (two columns of 8), the fixed billboards, then a full-screen sprite
// pass. Returns the previous shadow-enable state (restored by caller).
u8 arcsel_render_background(void)
{
    u8   prevShadow;
    int  i, off;
    float up[3];
    float eye[3], at[3], upv[3];

    // Seed the local ctx block (local_1c..local_14) from the default vector
    // constant DAT_803e0918. The decompiler copies three consecutive words.
    up[0] = DAT_803e0918;
    *(int *)&up[1] = DAT_803e091c;
    *(int *)&up[2] = DAT_803e0920;
    gx_set_reflect_matrix(ARCSEL + 0x20);

    at[0]  = FLOAT_8061a180; at[1] = FLOAT_8061a184; at[2] = FLOAT_8061a180;
    eye[0] = FLOAT_8061a180; eye[1] = FLOAT_8061a2b4; eye[2] = FLOAT_8061a180;
    upv[0] = FLOAT_8061a180; upv[1] = FLOAT_8061a2b4; upv[2] = FLOAT_8061a270;

    gx_set_blend_mode(0);
    gx_set_sub_material_id(0xe);
    gx_fill_rect_clipped(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1] + 1,
                         DAT_80611748[6] + 1, 0x7f);
    if (((uint)ARCSEL[400] & 0x10) == 0) {
        gx_set_sub_material_id(0xe);
    } else {
        gx_set_sub_material_id(1);
    }
    gx_material_state_nop();
    arcsel_render_element((int)(ARCSEL + 0x6e), ARCSEL, up);
    mtx4_transpose(ARCSEL, (float *)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;
    gx_set_blend_mode(0);
    gx_set_sub_material_id(2);

    off = 0;
    for (i = 0; i < 8; i++) {
        arcsel_render_element((int)DAT_80612b64 + off + 0x41c, ARCSEL, up);
        off = off + 0x44;
    }
    off = 0;
    for (i = 0; i < 8; i++) {
        arcsel_render_element((int)DAT_80612b64 + off + 0x1fc, ARCSEL, up);
        off = off + 0x44;
    }
    gx_set_sub_material_id(1);
    arcsel_render_billboard_element((int)(ARCSEL + 0x50), ARCSEL, up);
    arcsel_render_billboard_element((int)(ARCSEL + 0x5a), ARCSEL, up);
    arcsel_render_billboard_element((int)(ARCSEL + 100), ARCSEL, up);
    gx_set_sub_material_id(2);
    arcsel_render_billboard_element((int)(ARCSEL + 0x32), ARCSEL, up);
    arcsel_render_billboard_element((int)(ARCSEL + 0x46), ARCSEL, up);
    arcsel_render_billboard_element((int)(ARCSEL + 0x3c), ARCSEL, up);

    mtx4_build_lookat_points(ARCSEL, eye, at, upv);
    mtx4_transpose(ARCSEL, (float *)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;
    prevShadow = (u8)gx_set_shadow_enable(0);
    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x47);
    gx_bind_material_by_index_w(-6);
    gx_draw_sprite_rect(0, 0, DAT_806109c4, DAT_806109c8, 0, 0, DAT_806109c4 << 4,
                        DAT_806109c8 << 4);
    prevShadow = (u8)gx_set_shadow_enable(prevShadow);
    return prevShadow;
}

// ---------------------------------------------------------------------------
// Address: 0x802e6e28 | Name: arcsel_render_loadbar_overlay
// Blits the load-bar preview render target to the full screen, then draws the
// fullscreen quad over it.
void arcsel_render_loadbar_overlay(void)
{
    int desc;

    gx_set_blend_mode(0);
    gx_set_sub_material_id(0xe);
    gx_set_active_material(DAT_806110b8);
    desc = *DAT_80610a20 + DAT_806110b8 * 0x58;
    gx_draw_sprite_rect(0, 0, DAT_806109c4, DAT_806109c8, 0, 0,
                        (uint)*(u16 *)(desc + 0x4e) << 4, (uint)*(u16 *)(desc + 0x52) << 4);
    sprite_draw_fullscreen_quad(0, 0, DAT_806109c4, DAT_806109c8);
}
