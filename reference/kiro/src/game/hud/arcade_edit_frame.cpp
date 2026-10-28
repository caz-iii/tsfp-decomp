// Batch: batch_894 | Module: arcade map-editor top-level frame + arrow / layer-strip / paste-meter
// Address range: 0x8031a38c - 0x8031bc40
// Date: 2026-09-12
//
// The arcade map-editor's top-level per-frame compositor and its remaining decoration helpers. It
// draws the background + grid + members (delegating to the batch_891/892/893 layers), overlays the
// cursor / selection cell, the directional link arrows, the Z-layer selector strip down the side,
// and the paste-buffer capacity meter. The arrow helpers rotate a unit quad about Y to point a
// facing marker; the layer strip draws one sprite per Z level plus scroll chevrons.
//
//   aveditor_draw_arrow_quad       0x8031a38c  rotate a unit quad about Y and draw it (facing arrow).
//   aveditor_draw_grid_tri         0x8031a578  draw a degenerate (3-point) grid quad.
//   aveditor_draw_diag_arrow       0x8031a5bc  draw a 4-segment diagonal arrow glyph.
//   aveditor_draw_link_arrow       0x8031a76c  dispatch a link/spawn arrow by its direction type.
//   aveditor_draw_dir_marker       0x8031a944  draw a directional marker sprite (8 compass dirs).
//   aveditor_draw_frame            0x8031ab9c  top-level editor frame compositor (bg+grid+overlays).
//   aveditor_draw_layer_strip      0x8031b5c8  draw the Z-layer selector strip + scroll chevrons.
//   aveditor_paste_fill_ratio      0x8031b984  compute the paste buffer fill fraction (0..1).
//   aveditor_draw_paste_meter      0x8031ba1c  draw the paste capacity meter bar + label.
//   aveditor_draw_paste_meter_if_active 0x8031bc40 draw the paste meter only when paste mode is live.
//
// NON-MATCHING notes:
//  * The compiler _savegpr_/_restgpr_ spill helpers (__save_gpr_25/28, __restore_gpr_25/28,
//    FUN_803826dc / FUN_80382728) are preserved as opaque calls; where one folds an incoming
//    register pair into an `undefined8` return, the value is recovered as a local.
//  * The int<->float 0x43300000 / DOUBLE_8061a6f0 magic-double idiom is written with CONCAT44 from
//    common.h and plain (float)(int) casts. The paired-single FP-register spill temps the
//    decompiler surfaces (in_f23..in_f31 / in_psNN_1) are callee-saved residue with no semantic
//    effect and are omitted.
//  * A few named helpers are declared in generated_decls.h with a narrower signature than the call
//    sites here (aveditor_render_all_members()/void, aveditor_draw_group_markers()/void return,
//    hud_screen_destroy_current()/void return, sprite_draw_progress_bar(3 args) vs 4). To stay
//    compile-clean without editing the shared header, they are reached through thin file-local
//    function-pointer wrappers (prefixed __ so gen_externs skips them) with the real signature.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs below. Named helpers
// (mtx4_*, aveditor_draw_grid_quad/_rect/_cell_icon/_click_rect/_background/_render_grid_view/
// _group_markers/_paste_buffer/_cursor_box, cursor_get_texture_id, sprite_get_texture_id, gx_*,
// text_*, get_string_by_id, avanim_layout_is_0x22, aveditor_get_record_capacity) resolve via the
// shared generated headers.

#include "common.h"
#include "hud.h"

extern "C" {

// -- editor context + view transform --
extern int   DAT_80612be4;     // editor context base (mode +0x254, gridZ +0x264, flags +0x25c)
extern int   DAT_80612be8;     // paste/record capacity context
extern int   DAT_80612bf4;     // focused screen-root handle
extern int*  DAT_80610a20;     // sprite material record table base (0x58 stride)
extern int   DAT_806119ec;     // active grid layout index
extern int   DAT_8051a820;     // grid layout table (screen-root handle, stride 0x1e0)
extern float DAT_8051a834;     // grid layout table X (stride 0x1e0)
extern float DAT_8051a838;     // grid layout table Y (stride 0x1e0)
extern int   DAT_806109c4;     // view width (magic-double)
extern int   DAT_80612c88;     // pixel origin x (magic-double)
extern int   DAT_80612c84;     // pixel origin y (magic-double)
extern float FLOAT_80612c60;   // world->pixel x scale
extern float FLOAT_80612c5c;   // world->pixel y scale
extern float FLOAT_80612c58;   // inverse x scale
extern float FLOAT_80612c54;   // inverse y scale
extern float FLOAT_80612c78;   // world origin x
extern float FLOAT_80612c74;   // world origin y
extern unsigned char DAT_80611a50; // selection alpha low
extern unsigned char DAT_80611a51; // selection alpha high
extern int   DAT_80612028;     // global frame counter
extern signed char DAT_80477c80;   // per-layout capability table (0x31 stride)

// -- layout / anchor constants --
extern double DOUBLE_8061a6f0; // int->float conversion bias
extern double DOUBLE_8061a780; // int->float conversion bias (byte variant)
extern float  FLOAT_8061a6e8;  // 1.0
extern float  FLOAT_8061a704;  // meter y pad
extern float  FLOAT_8061a70c;  // 0.5
extern float  FLOAT_8061a710;  // 0.0
extern float  FLOAT_8061a714;  // layout row height
extern float  FLOAT_8061a730;  // marker half-size
extern float  FLOAT_8061a768;  // zoom threshold denom
extern float  FLOAT_8061a788;  // arrow thickness
extern float  FLOAT_8061a78c;  // arrow corner extent
extern float  FLOAT_8061a794;  // diag arrow size A
extern float  FLOAT_8061a798;  // diag arrow size B
extern float  FLOAT_8061a79c;  // arrow arg c
extern float  FLOAT_8061a7b0;  // icon anchor scale
extern float  FLOAT_8061a7d4;  // diag arrow d
extern float  FLOAT_8061a7d8;  // diag arrow e
extern float  FLOAT_8061a7dc;  // diag arrow f
extern float  FLOAT_8061a7e0;  // link arrow head
extern float  FLOAT_8061a7e4;  // marker inactive scale
extern float  FLOAT_8061a7e8;  // marker offset scale
extern float  FLOAT_8061a7ec;  // strip x base
extern float  FLOAT_8061a7f0;  // strip x span
extern float  FLOAT_8061a7f4;  // strip cell height
extern float  FLOAT_8061a7f8;  // strip y0
extern float  FLOAT_8061a7fc;  // strip y1
extern float  FLOAT_8061a800;  // strip cell width
extern float  FLOAT_8061a804;  // strip cell height2
extern float  FLOAT_8061a808;  // paste ratio denom
extern float  FLOAT_8061a80c;  // meter label x pad
extern float  FLOAT_8061a810;  // meter bar width
extern float  FLOAT_80611a64;  // text anchor x scale
extern float  FLOAT_80611a68;  // icon anchor scale
extern float  FLOAT_80611a6c;  // layout x bias
extern float  FLOAT_80611a70;  // layout y bias

// -- raw FUN_ helpers not yet individually named --
// aveditor_paste_record_size resolves via the shared headers; do not redeclare.
extern int  FUN_803450bc(void);                    // per-cell selection-visibility test
extern unsigned long long FUN_803826dc(void);      // _savegpr_ spill; folds meter x/y pair
extern void FUN_80382728(void);                    // _restgpr_ restore
extern void FUN_80382724(void);                    // _restgpr_ restore

// compiler _savegpr_/_restgpr_ spill helpers (opaque)
extern void __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern void __save_gpr_28(void);
extern void __restore_gpr_28(void);
} // extern "C"

// Wrappers for shared helpers whose generated prototype doesn't match the call site here.
// (prefixed __ so gen_externs skips them; they resolve via the shared header symbol.)
static inline unsigned long long __group_markers_spill(void)
{
    typedef unsigned long long (*fn_t)(void);
    return ((fn_t)(void*)&aveditor_draw_group_markers)();
}
static inline void __render_all_members16(unsigned long long a, unsigned long long b, unsigned long long c,
    unsigned long long d, unsigned long long e, unsigned long long f, unsigned long long g,
    unsigned long long h, int i, int ctx, unsigned int k, int l, int m, int n, unsigned int o, unsigned int p)
{
    typedef void (*fn_t)(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
        unsigned long long, unsigned long long, unsigned long long, unsigned long long, int, int,
        unsigned int, int, int, int, unsigned int, unsigned int);
    ((fn_t)(void*)&aveditor_render_all_members)(a, b, c, d, e, f, g, h, i, ctx, k, l, m, n, o, p);
}
static inline int __screen_current_record(void)
{
    typedef int (*fn_t)(void);
    return ((fn_t)(void*)&hud_screen_destroy_current)();
}
static inline void __progress_bar4(int x, int y, int w, int h)
{
    typedef void (*fn_t)(int, int, int, int);
    ((fn_t)(void*)&sprite_draw_progress_bar)(x, y, w, h);
}

// -----------------------------------------------------------------------------
// Rotate a unit quad (spanning [x0..x1] x [y0..y1] built from args 3..6) about Y by angle
// param_7, translate it to (param_1,param_2), and draw it as a grid quad. Used to point the facing
// arrows. The four corner z components are zeroed (2D overlay).
void aveditor_draw_arrow_quad(double tx, double ty, double x0, double y0, double x1, double y1, double angle)
{
    float m[17];
    float p0x, p0y, p0z, p1x, p1y, p1z, p2x, p2y, p2z, p3x, p3y, p3z;
    double t;

    mtx4_set_identity((int)(intptr_t)m);
    t = x0; if (x0 < x1) { t = x1; x1 = x0; }
    p0x = (float)x1; p1x = (float)t; p2x = (float)x1; p3x = (float)t;
    t = y0; if (y1 < y0) { t = y1; y1 = y0; }
    p3y = (float)y1; p1y = (float)y1; p2y = (float)t; p0y = (float)t;
    p0z = FLOAT_8061a710; p2z = FLOAT_8061a710; p1z = FLOAT_8061a710; p3z = FLOAT_8061a710;

    // pack as 4 xyz points laid out contiguously (p0..p3) for mtx4_transform_point
    float pt[4][3] = { {p0x,p0y,p0z}, {p1x,p1y,p1z}, {p2x,p2y,p2z}, {p3x,p3y,p3z} };
    mtx4_set_rotation_y(angle, m);
    mtx4_transform_point(m, pt[0]);
    mtx4_transform_point(m, pt[1]);
    mtx4_transform_point(m, pt[2]);
    mtx4_transform_point(m, pt[3]);
    for (int i = 0; i < 4; i++) { pt[i][0] = (float)((double)pt[i][0] + tx); pt[i][1] = (float)((double)pt[i][1] + ty); }

    aveditor_draw_grid_quad((double)pt[0][0], (double)pt[0][1], (double)pt[1][0], (double)pt[1][1],
                            (double)pt[2][0], (double)pt[2][1], (double)pt[3][0], (double)pt[3][1]);
}

// -----------------------------------------------------------------------------
// Draw a degenerate (3-point) grid quad: the first vertex is repeated so the quad collapses to a
// triangle. (x0,y0) is the apex; (x1,y1)/(x2,y2) are the base corners.
void aveditor_draw_grid_tri(double x0, double y0, double x1, double y1, double x2, double y2)
{
    aveditor_draw_grid_quad(x0, y0, x0, y0, x1, y1, x2, y2);
}

// -----------------------------------------------------------------------------
// Draw a 4-segment diagonal arrow glyph rooted at the cell coord in *coord, sized by type (0xc/0xe
// use size B, others size A). Each segment is an arrow quad at a stepped size.
void aveditor_draw_diag_arrow(short* coord, int type)
{
    float sz = FLOAT_8061a798;
    if ((type != 0xc) && (type != 0xe)) sz = FLOAT_8061a794;
    double d = (double)sz;
    double cx = (double)(float)((double)(int)((int)coord[0] ^ 0x80000000) - DOUBLE_8061a6f0);
    double cy = (double)(float)((double)(int)((int)coord[1] ^ 0x80000000) - DOUBLE_8061a6f0);

    aveditor_draw_arrow_quad(cx, cy, (double)FLOAT_8061a710, (double)FLOAT_8061a7d4, (double)FLOAT_8061a7d8, (double)FLOAT_8061a79c, d);
    aveditor_draw_arrow_quad(cx, cy, (double)FLOAT_8061a7d8, (double)FLOAT_8061a7d4, (double)FLOAT_8061a7dc, (double)FLOAT_8061a78c, d);
    aveditor_draw_arrow_quad(cx, cy, (double)FLOAT_8061a78c, (double)FLOAT_8061a78c, (double)FLOAT_8061a7dc, (double)FLOAT_8061a788, d);
    aveditor_draw_arrow_quad(cx, cy, (double)FLOAT_8061a710, (double)FLOAT_8061a79c, (double)FLOAT_8061a78c, (double)FLOAT_8061a788, d);
}

// -----------------------------------------------------------------------------
// Dispatch a link/spawn arrow by its direction type. Types 1/3/7/9 = up-right family, 2/4/8/10 =
// down-left family (axis-aligned corner arrows), 0xb..0xe defer to the diagonal arrow. The arrow
// color arrives in r5 (in_r5). Draws four corner rects forming the arrowhead.
void aveditor_draw_link_arrow(short* coord, int type)
{
    unsigned int color = 0; // r5 (arrow color); the decompiler surfaces it as in_r5
    double x0, y1, off0, off1, x1;

    switch (type) {
    default:
        off0 = (double)FLOAT_8061a710; y1 = off0; x0 = off0; off1 = off0;
        break;
    case 1: case 3: case 7: case 9:
        off0 = (double)FLOAT_8061a7e0; off1 = (double)FLOAT_8061a788;
        x0 = (double)(FLOAT_8061a78c + (float)((double)(int)((int)coord[0] ^ 0x80000000) - DOUBLE_8061a6f0));
        y1 = (double)((float)((double)(int)((int)coord[1] ^ 0x80000000) - DOUBLE_8061a6f0) - FLOAT_8061a78c);
        break;
    case 2: case 4: case 8: case 10:
        off0 = (double)FLOAT_8061a788; off1 = (double)FLOAT_8061a7e0;
        x0 = (double)((float)((double)(int)((int)coord[0] ^ 0x80000000) - DOUBLE_8061a6f0) - FLOAT_8061a78c);
        y1 = (double)(FLOAT_8061a78c + (float)((double)(int)((int)coord[1] ^ 0x80000000) - DOUBLE_8061a6f0));
        break;
    case 0xb: case 0xc: case 0xd: case 0xe: {
        short tmp[3] = { coord[0], coord[1], coord[2] };
        aveditor_draw_diag_arrow(tmp, type);
        return;
    }
    }

    off0 = (double)(float)(x0 + off0);
    aveditor_draw_grid_rect(color, (double)(float)(x0 - (double)FLOAT_8061a78c),
        (double)(float)(y1 - (double)FLOAT_8061a78c), off0, y1);
    off1 = (double)(float)(y1 + off1);
    x1 = (double)(float)((double)FLOAT_8061a78c + off0);
    aveditor_draw_grid_rect(color, off0, (double)(float)(y1 - (double)FLOAT_8061a78c), x1, off1);
    off0 = (double)(float)((double)FLOAT_8061a78c + off1);
    aveditor_draw_grid_rect(color, x0, off1, x1, off0);
    aveditor_draw_grid_rect(color, (double)(float)(x0 - (double)FLOAT_8061a78c), y1, x0, off0);
}

// -----------------------------------------------------------------------------
// Draw a directional marker sprite (8 compass directions, dir index in dir) at (mx,my). Offsets the
// sprite by dir; active markers (active!=0) use full scale. Skipped when zoomed out past a threshold.
void aveditor_draw_dir_marker(double mx, double my, int dir, int active)
{
    if ((float)((double)(int)(DAT_806109c4 ^ 0x80000000) - DOUBLE_8061a6f0) / FLOAT_8061a768 > FLOAT_80612c60)
        return;

    int tex = cursor_get_texture_id(dir + 0x37);
    float s1 = FLOAT_8061a7e4, s3 = FLOAT_8061a730;
    if (active != 0) { s1 = FLOAT_8061a6e8; s3 = FLOAT_8061a6e8; }
    double sz = (double)(FLOAT_8061a730 * s1);
    float px = (float)(mx + (double)FLOAT_8061a70c);
    float py = (float)(my + (double)FLOAT_8061a70c);
    float step = FLOAT_8061a7e8 * s3;
    switch (dir) {
    case 0: py = step + py; break;
    case 1: px = px - step; py = py + step; break;
    case 2: px = -(step - px); break;
    case 3: px = px - step; py = py - step; break;
    case 4: py = -(step - py); break;
    case 5: px = px + step; py = py - step; break;
    case 6: px = step + px; break;
    case 7: px = px + step; py = py + step; break;
    }
    double sx = (double)FLOAT_80612c60, sy = (double)FLOAT_80612c5c;
    double half = sz * (double)FLOAT_8061a70c;
    double qx = (double)(float)(sx * (double)((px - (float)half) - FLOAT_80612c78) +
                 (double)(float)((double)(int)(DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0));
    double qy = (double)(float)(sy * (double)((py - (float)half) - FLOAT_80612c74) +
                 (double)(float)((double)(int)(DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0));
    gx_set_active_material(tex);
    gx_draw_sprite_quad(qx, qy, (double)(float)(qx + (double)(float)(sz * sx)),
                        (double)(float)(qy + (double)(float)(sz * sy)), 0, 0, 0x200, 0x200);
}

// -----------------------------------------------------------------------------
// Top-level editor frame compositor: draws the grid background, the grid view (members), the group
// markers, then - unless a modal flag is set - the selected/cursor cell highlight, the active piece
// icon, the link-arrow overlays for the selected/hover cell, and finally either the paste buffer or
// the cursor box. Most FP args are register residue; only the editor context globals matter.
void aveditor_draw_frame(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, unsigned int m9,
                         unsigned int m10, unsigned int p11, int p12, int p13, int p14,
                         unsigned int p15, unsigned int p16)
{
    int selNeg, mode, m5;
    unsigned int uVar8, color;
    unsigned long long grp;
    double cx, cy;
    (void)a1; (void)m9; (void)m10; (void)p11; (void)p12;

    __save_gpr_25();
    selNeg = -*(int*)(DAT_80612be4 + 0x254);
    gx_set_sub_material_id(0);
    aveditor_draw_grid_background();
    gx_set_sub_material_id(1);
    aveditor_render_grid_view();
    grp = __group_markers_spill();

    if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x44088) != 0) { __restore_gpr_25(); return; }

    if (*(int*)(DAT_80612be4 + 0x254) == 3) {
        __render_all_members16(grp, a2, a3, a4, a5, a6, a7, a8, 0, DAT_80612be4, p11, p12, p13, p14, p15, p16);
    }

    mode = *(int*)(DAT_80612be4 + 0x254);
    if (((mode == 1) || (mode == 3) || ((mode == 4) && (*(int*)(DAT_80612be4 + 0x1185c) != 6))) &&
        (*(int*)(DAT_80612be4 + 0x2f18) != 0)) {
        cx = (double)(float)((double)(int)((int)*(short*)(DAT_80612be4 + 0x2f2e) ^ 0x80000000) - DOUBLE_8061a6f0);
        cy = (double)(float)((double)(int)((int)*(short*)(DAT_80612be4 + 0x2f30) ^ 0x80000000) - DOUBLE_8061a6f0);
        int mat = gx_set_sub_material_id(2);
        color = (unsigned char)DAT_80611a50 >> 1 | 0x7f000000;
        if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) != 0) color = (unsigned char)DAT_80611a51 >> 1 | 0x7f0000;

        // draw the selected cell highlight unless a special avanim layout is active
        if (((*(int*)(DAT_80612be4 + 0x254) != 1) ||
             (((*(int*)(DAT_80612be4 + 0x2f34) < 0 ||
                (avanim_layout_is_0x22(*(int*)(DAT_80612be4 + 0x2f34)) == 0)) &&
               (*(int*)(DAT_80612be4 + 0x2f78) < 0)))) &&
            ((*(int*)(DAT_80612be4 + 0x254) != 4 ||
              ((*(int*)(DAT_80612be4 + 0x1185c) != 3 && (*(int*)(DAT_80612be4 + 0x1185c) != 5)))))) {
            aveditor_draw_grid_rect(color, cx, cy, (double)(float)((double)FLOAT_8061a6e8 + cx),
                                    (double)(float)((double)FLOAT_8061a6e8 + cy));
        }
        gx_set_sub_material_id(mat);

        // active piece icon / cell icon in edit or place mode
        if ((*(int*)(DAT_80612be4 + 0x254) == 1) || (*(int*)(DAT_80612be4 + 0x254) == 4)) {
            if ((*(int*)(DAT_80612be4 + 0x2f34) < 0) ||
                (avanim_layout_is_0x22(*(int*)(DAT_80612be4 + 0x2f34)) != 0)) {
                int ctx = DAT_80612be4;
                int* sel = *(int**)(DAT_80612be4 + 0x2f1c);
                if ((sel != 0) && (avanim_layout_is_0x22(*sel) == 0) &&
                    (((&DAT_80477c80)[*sel * 0x31] & 1) != 0) &&
                    (selNeg + 4 != (unsigned int)(selNeg == -4) + selNeg + 3)) {
                    aveditor_draw_dir_marker(cx, cy, sel[2], 0);
                }
                (void)ctx;
            } else {
                unsigned int layout = *(unsigned int*)(DAT_80612be4 + 0x2f34);
                uVar8 = (unsigned char)DAT_80611a50 | 0x7f404000;
                if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) != 0) uVar8 = (unsigned char)DAT_80611a51 | 0x407f4000;
                unsigned int glow = (*(int*)(DAT_80612be4 + 0x2f38) < 0) ? 0u : 0x5064787fu;
                aveditor_draw_cell_icon(cx, cy, (int*)(DAT_80612be4 + 0x2f34), uVar8, glow, layout, p13, p14, p15, p16);
                if ((((&DAT_80477c80)[layout * 0x31] & 1) != 0) &&
                    ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) != 0)) {
                    aveditor_draw_dir_marker(cx, cy, *(int*)(DAT_80612be4 + 0x2f3c), 1);
                }
            }
        }
        // hover marker in group mode
        if (*(int*)(DAT_80612be4 + 0x254) == 3) {
            if (*(short*)(DAT_80612be4 + 0x2f10) < 0) {
                if (-1 < *(int*)(DAT_80612be4 + 0x2f20)) {
                    int rec = DAT_80612be4 + (*(int*)(DAT_80612be4 + 0x2f20) >> 0x10) * 0xe4 + 0x288;
                    int t = *(int*)(rec + 0x34);
                    if ((t == 0) || (t == 2) || (t == 4)) aveditor_draw_dir_marker(cx, cy, *(int*)(rec + 0x54), 0);
                }
            } else {
                int rec = DAT_80612be4 + *(short*)(DAT_80612be4 + 0x2f10) * 0xe4 + 0x288;
                int t = *(int*)(rec + 0x34);
                if (((t == 0) || (t == 2) || (t == 4)) && ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) != 0))
                    aveditor_draw_dir_marker(cx, cy, *(int*)(rec + 0x54), 1);
            }
        }
    }

    // link-arrow overlays for the piece's cells (avanim 0x22 pieces)
    if (((*(int*)(DAT_80612be4 + 0x254) == 1) || (*(int*)(DAT_80612be4 + 0x254) == 4)) &&
        (avanim_layout_is_0x22(*(int*)(DAT_80612be4 + 0x2f34)) != 0)) {
        int mem = *(int*)(DAT_80612be4 + 0x2f18);
        if (mem != 0) {
            int piece = *(int*)(mem + *(int*)(mem + 0xc) * 4 + 0x2c);
            for (int i = 0, off = 0; i < *(int*)(piece + 0x44); i = i + 1, off = off + 6) {
                unsigned char* rec = (unsigned char*)(piece + off + 0x48);
                float* member = *(float**)(DAT_80612be4 + 0x2f18);
                if ((float)((double)(int)(*(unsigned int*)(DAT_80612be4 + 0x264) ^ 0x80000000) - DOUBLE_8061a6f0) ==
                    member[2] + (float)((double)(int)(unsigned int)rec[3] - DOUBLE_8061a780)) {
                    int px = (int)(*member + (float)((double)(int)(unsigned int)rec[1] - DOUBLE_8061a780));
                    int py = (int)(member[1] + (float)((double)(int)(unsigned int)rec[2] - DOUBLE_8061a780));
                    short cell[3] = { (short)px, (short)py, (short)(*(int*)(mem + 0xc)) };
                    if (rec[5] == 1) {
                        for (int k = 0; k < (int)(unsigned int)rec[4]; k = k + 1) {
                            unsigned char dir = rec[0];
                            float d = (k != 0) ? FLOAT_8061a6e8 : FLOAT_8061a710;
                            if (dir == 2 || dir == 0 || dir > 3)
                                cell[1] = (short)((int)((float)((double)(int)((int)cell[1] ^ 0x80000000) - DOUBLE_8061a6f0) + d));
                            else
                                cell[0] = (short)((int)((float)((double)(int)((int)cell[0] ^ 0x80000000) - DOUBLE_8061a6f0) + d));
                            if (FUN_803450bc() != 0) {
                                short c2[3] = { cell[0], cell[1], cell[2] };
                                aveditor_draw_link_arrow(c2, rec[0]);
                            }
                        }
                    } else if (FUN_803450bc() != 0) {
                        short c2[3] = { cell[0], cell[1], cell[2] };
                        aveditor_draw_link_arrow(c2, rec[0]);
                    }
                }
            }
        }
    }

    // finally the paste buffer or cursor box
    if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x20) == 0) aveditor_draw_paste_buffer();
    else                                                      aveditor_draw_cursor_box();

    __restore_gpr_25();
}

// -----------------------------------------------------------------------------
// Draw the Z-layer selector strip down the side: one sprite per layer (up to 5), highlighting the
// current/copy/paste layers, then a click rect and (when this screen is focused) the up/down scroll
// chevrons.
void aveditor_draw_layer_strip(void)
{
    float rect[4];
    double sx = (double)FLOAT_80611a64;

    __save_gpr_28();
    double y0 = (double)(float)((double)FLOAT_8061a714 * sx +
                (double)(float)((double)FLOAT_8061a7b0 * sx + (double)FLOAT_80611a6c));
    double y1 = (double)(float)((double)FLOAT_8061a7f8 * sx + y0);
    double yStrip = (double)(float)((double)FLOAT_8061a7b0 * (double)(float)((double)FLOAT_8061a7f4 * sx) + y1);
    double yStep = (double)(float)((double)(float)((double)FLOAT_8061a7f4 * sx) + y1);
    double yBot = (double)(float)((double)FLOAT_8061a7fc * sx + yStrip);
    double cellW = (double)(FLOAT_8061a800 * FLOAT_80611a68);
    double x0 = -(double)(FLOAT_8061a7f0 * FLOAT_80611a68 -
                 ((float)((double)(int)(DAT_806109c4 ^ 0x80000000) - DOUBLE_8061a6f0) -
                  (FLOAT_8061a7ec * FLOAT_80611a68 + FLOAT_80611a70)));

    if (*(int*)(DAT_80612be4 + 0x200) == 0) { __restore_gpr_28(); return; }

    double xr = (double)(float)(x0 + cellW);
    double h = (double)FLOAT_8061a804;
    double cy = yStrip;
    for (int i = 0; i < 5; i = i + 1) {
        int tex = 0x0b;
        if (((((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x400) == 0) ||
              (i - 2 != *(int*)(DAT_80612be4 + 0x26c))) &&
             (i - 2 != *(int*)(DAT_80612be4 + 0x264))) &&
            (i - 2 != *(int*)(DAT_80612be4 + 0x268))) {
            tex = 10;
        }
        tex = cursor_get_texture_id(tex);
        gx_set_active_material(tex);
        int rec = *DAT_80610a20 + tex * 0x58;
        gx_draw_sprite_quad(x0, cy, xr, (double)(float)(h * (double)FLOAT_80611a64 + cy), 0, 0,
                            (unsigned int)*(unsigned short*)(rec + 0x4e) << 4,
                            (unsigned int)*(unsigned short*)(rec + 0x52) << 4);
        cy = -(double)(float)((double)FLOAT_8061a7f4 * (double)FLOAT_80611a64 - cy);
    }

    rect[0] = (float)x0; rect[1] = (float)yStep; rect[2] = (float)(x0 + cellW); rect[3] = (float)yBot;
    aveditor_click_rect(rect, 0x3e);

    if ((((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x4044088) == 0) &&
         (hud_screen_find_root(*(int*)(&DAT_8051a820 + DAT_806119ec * 0x1e0)) == DAT_80612bf4)) ||
        (*(char*)(DAT_80612be4 + 0x11854) == '\x01')) {
        int upTex = sprite_get_texture_id(0x3f2);
        int dnTex = sprite_get_texture_id(0x3f1);
        double cw = (double)(FLOAT_8061a800 * FLOAT_80611a68);
        double ch = (double)(FLOAT_8061a804 * FLOAT_80611a64);
        gx_set_active_material(upTex);
        int xi = (int)x0;
        unsigned int xr2 = (unsigned int)(x0 + cw);
        int rec = *DAT_80610a20 + upTex * 0x58;
        gx_draw_sprite_rect(xi, (int)y0, xr2, (int)(y0 + ch), 0, 0,
                            (unsigned int)*(unsigned short*)(rec + 0x4e) << 4,
                            (unsigned int)*(unsigned short*)(rec + 0x52) << 4);
        gx_set_active_material(dnTex);
        rec = *DAT_80610a20 + dnTex * 0x58;
        gx_draw_sprite_rect(xi, (int)yBot, xr2, (int)(yBot + ch), 0, 0,
                            (unsigned int)*(unsigned short*)(rec + 0x4e) << 4,
                            (unsigned int)*(unsigned short*)(rec + 0x52) << 4);
    }
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// Compute the paste-buffer fill fraction: (capacity - used)/denom * (1 + used/denom), clamped so a
// zero capacity returns 0. Used to size the paste meter bar.
double aveditor_paste_fill_ratio(void)
{
    unsigned int used = (unsigned int)aveditor_paste_record_size();
    double usedF = (double)((float)((double)(int)(used ^ 0x80000000) - DOUBLE_8061a6f0) / FLOAT_8061a808);
    int cap = aveditor_get_record_capacity((int*)&DAT_80612be8);
    double capF = (double)((float)((double)(int)cap - DOUBLE_8061a780) / FLOAT_8061a808);
    if (capF != (double)FLOAT_8061a710) {
        capF = (double)((float)(capF - usedF) * (float)((double)FLOAT_8061a6e8 + usedF));
    }
    return capF;
}

// -----------------------------------------------------------------------------
// Draw the paste-buffer capacity meter: a filled rect + progress bar sprite at (x,y), a label from
// string 0xa24, and a registered click rect. The bar/label color flashes when the buffer is over
// capacity or during the paste blink phase.
void aveditor_draw_paste_meter(unsigned long long xy_pair, double y)
{
    float rect[4];
    (void)xy_pair;

    unsigned long long spill = FUN_803826dc();
    double x = (double)spill;                 // meter x (folded from the spill pair)

    int fontPrev = text_set_font_table(0x12);
    unsigned char* label = (unsigned char*)get_string_by_id(0xa24);
    aveditor_paste_fill_ratio();
    double w = text_get_char_width(label);
    float rx = FLOAT_8061a80c + (float)(x + w);
    unsigned int color = 0x6e6e6e7f;
    unsigned int y1 = (unsigned int)((float)((double)FLOAT_8061a704 + y) - FLOAT_8061a6e8);
    unsigned int x1 = (unsigned int)((FLOAT_8061a810 + rx) - FLOAT_8061a6e8);
    if (((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x410000) != 0) &&
        ((color = 0x80500080), (DAT_80612028 & 0x20) != 0)) {
        color = 0x80800080;
    }
    int xi = (int)rx, yi = (int)y;
    gx_fill_rect_clipped(xi, yi, x1, y1, 0x0f0f0f64);
    __progress_bar4(xi, yi, 0x50, 0x10);
    if (*(float*)(DAT_80612be4 + 0x270) + *(float*)(DAT_80612be4 + 0x274) <= FLOAT_8061a6e8) {
        color = 0x3746557f;
    }
    text_draw_char_shadowed(x, y, label, color, 0x80);
    text_set_font_table(fontPrev);

    rect[0] = (float)x;
    rect[1] = (float)y;
    rect[2] = (float)((double)(int)(x1 ^ 0x80000000) - DOUBLE_8061a6f0);
    rect[3] = (float)((double)(int)(y1 ^ 0x80000000) - DOUBLE_8061a6f0);
    aveditor_click_rect(rect, 0x3d);
    FUN_80382728();
}

// -----------------------------------------------------------------------------
// Draw the paste meter only when paste mode is live: if a modal-screen flag (+0x25c & 0x44088) is
// set, require the current screen to be present and flagged (+0x214 & 8) before drawing.
void aveditor_draw_paste_meter_if_active(void)
{
    if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x44088) != 0) {
        if (__screen_current_record() == 0) return;
        int screen = __screen_current_record();
        if ((*(unsigned int*)(screen + 0x214) & 8) == 0) return;
    }
    aveditor_draw_paste_meter(0,
        (double)(FLOAT_8061a714 * FLOAT_80611a64 + FLOAT_8061a7b0 * FLOAT_80611a64 + FLOAT_80611a6c));
}
