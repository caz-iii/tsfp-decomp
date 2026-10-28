// Address range: 0x8016f7d8 - 0x801706cb | batch_449 | 2026-09-06
//
// GX 2D PRIMITIVE DRAWERS.  Immediate-mode 2D fill / line / quad / triangle /
// sprite drawers used by the HUD, overlays and debug rendering.  All of them
// enter the shared 2D draw state (FUN_8017c658 / FUN_8017fab4 / FUN_80172818),
// stream vertices straight into the GX FIFO write port at 0xCC008000 (positions
// via the int->float magic-bias idiom, colours byte-swapped with stwbrx), then
// close the 2D state (FUN_8017c670) and bump the per-frame draw-stat counters
// (DAT_80611570/74/78).  Companion to gx_material_state.cpp (batch_443-448):
// gx_draw_fill_rect_i/f, gx_fill_rect_clipped, gx_set_active_material etc.
//
//   gx_draw_gradient_rect     0x8016f7d8  axis-aligned rect, 4 per-corner colours
//   gx_draw_quad_pos4         0x8016f93c  quad from 4 vertex-position pointers
//   gx_draw_quad_f4           0x8016fa50  arbitrary quad, 4 float corners + colours
//   gx_draw_line_3d           0x8016fc04  3D line (2 verts, frame colours)
//   gx_draw_line_i            0x8016fcd0  int-coord 2D line (64 xrefs)
//   gx_draw_line_f            0x8016fde8  float-coord 2D line
//   gx_draw_line_counted      0x8016fee0  line wrapper + counter bump (188 xrefs)
//   gx_draw_vertex_list       0x8016ff24  N-vertex 2D poly/strip (optional UV)
//   gx_draw_tri_textured      0x80170160  textured 2D triangle
//   gx_draw_sprite_rect       0x80170410  textured 2D sprite/rect (260 xrefs)
#include "common.h"
#include "generated_decls.h"

// ---- GX FIFO write port (write-only volatile at 0xCC008000) ----
extern volatile float        DAT_cc008000;     // FIFO float write port
extern volatile unsigned int DAT_cc008000_u;   // FIFO u32 write port (same address)
#define GXF(v) (DAT_cc008000   = (float)(v))
#define GXU(v) (DAT_cc008000_u = (unsigned int)(v))

// ---- draw-state globals ----
extern int  DAT_80611570;   // draw-stat counter (verts)
extern int  DAT_80611574;   // draw-stat counter (prims)
extern int  DAT_80611578;   // draw-stat counter (batches)
extern int  DAT_80612254;   // active material record ptr (from gx_material_state.cpp)

// ---- rodata ----
extern float  FLOAT_806164f8;   // 0.0
extern float  FLOAT_806164fc;   // 1.0
extern float  FLOAT_80616554;   // fill/quad depth (z)
extern float  FLOAT_8061655c;   // line depth (z)
extern float  FLOAT_80616560;   // texel->normalized UV scale base
extern double DOUBLE_80616530;  // int->double magic bias 0x4330000080000000
extern double DOUBLE_80616548;  // ushort->double magic bias 0x4330000000000000

// ---- shared 2D draw-state helpers (raw addresses) ----
extern void FUN_8017c658(void);            // enter 2D draw state
extern void FUN_8017c474(int leaveOrCount);// 2D begin(0)/end(1)
extern void FUN_8017fab4(void);            // set 2D vertex format
extern void gx_begin_primitive(int a, int b, int c); // begin GX primitive (0x98 quads / 0xa8 lines)
extern void FUN_8017c670(void);            // finish 2D draw state

// ---- colour pass-through hook (FUN_8016c590 = glow_render_stub, identity) ----
// glow_render_stub is declared void in generated_decls; the draw code uses it as
// an identity colour pass-through (PPC r3 in == r3 out).  Call via a typed alias.
typedef unsigned int (*GlowColorFn)(unsigned int);
#define GLOW_COLOR ((GlowColorFn)glow_render_stub)

// ---- spawn-frame register readers / pops ----
extern unsigned long long FUN_803826ec(void);
extern unsigned long long FUN_803826dc(void);
extern unsigned long long FUN_803826e4(void);
extern unsigned long long FUN_803826e8(void);
extern unsigned long long FUN_803826bc(void);
extern unsigned long long FUN_8038264c(void);
extern void FUN_80382738(void);
extern void FUN_80382728(void);
extern void FUN_80382730(void);
extern void FUN_80382734(void);
extern void FUN_80382708(void);
extern void FUN_80382698(void);
extern unsigned long long __save_gpr_28(void);
extern void __restore_gpr_28(void);

// byte-swap a 32-bit colour word (matches the stwbrx store)
static inline unsigned int gx_bswap32(unsigned int c)
{
    return (c << 24) | ((c & 0xff00) << 8) | ((c >> 8) & 0xff00) | (c >> 24);
}

// int (word) -> float via the magic-bias idiom (DOUBLE_80616530 = 0x4330000080000000)
static inline float gx_i2f(unsigned int v)
{
    union { double d; unsigned int u[2]; } m;
    m.u[0] = 0x43300000u;
    m.u[1] = v ^ 0x80000000u;
    return (float)(m.d - DOUBLE_80616530);
}

// ushort -> float via the magic-bias idiom (DOUBLE_80616548 = 0x4330000000000000)
static inline float gx_u16f(unsigned int v)
{
    union { double d; unsigned int u[2]; } m;
    m.u[0] = 0x43300000u;
    m.u[1] = v;
    return (float)(m.d - DOUBLE_80616548);
}

// texel->normalized UV divisor for the active material axis (tex dim at +0x4e / +0x52)
static inline float gx_uv_scale(unsigned int texDim)
{
    return FLOAT_806164fc / (FLOAT_80616560 * gx_u16f(texDim));
}


// gx_draw_gradient_rect (0x8016f7d8)
// Axis-aligned fill-rect with an independent colour at each of the 4 corners.
// x0 arrives via the frame register (FUN_803826ec, as a float); y0/x1/y1 are the
// float params; the 4 corner colours pass through glow_render_stub and are
// byte-swapped into the FIFO.  Depth = FLOAT_80616554.
void gx_draw_gradient_rect(unsigned long long frameArgs, double y0, double x1, double y1)
{
    float fx0;
    unsigned int c0, c1, c2, c3;
    unsigned long long frame;

    (void)frameArgs;
    frame = __save_gpr_28();
    fx0 = (float)(double)frame;   /* x0 arrives in f1 */
    c0 = GLOW_COLOR((unsigned int)(frame >> 0x20));
    c1 = GLOW_COLOR((unsigned int)frame);
    c2 = GLOW_COLOR(0);
    c3 = GLOW_COLOR(0);

    FUN_8017c658();
    FUN_8017c474(0);
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);

    GXF(fx0);        GXF((float)y0); GXF(FLOAT_80616554); GXU(gx_bswap32(c0));
    GXF((float)x1);  GXF((float)y0); GXF(FLOAT_80616554); GXU(gx_bswap32(c1));
    GXF(fx0);        GXF((float)y1); GXF(FLOAT_80616554); GXU(gx_bswap32(c2));
    GXF((float)x1);  GXF((float)y1); GXF(FLOAT_80616554); GXU(gx_bswap32(c3));

    FUN_8017c670();
    FUN_8017c474(1);
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    __restore_gpr_28();
}

// gx_draw_quad_pos4 (0x8016f93c)
// Draw a 4-vertex quad from 4 vertex-position pointers (each pos = 3 floats
// x,y,z) with a byte-swapped colour per corner.  Positions 0/1 come from the
// frame register pair (FUN_803826dc); 2/3 are params.
void gx_draw_quad_pos4(unsigned int a, unsigned int b, float* pos2, float* pos3,
                       unsigned int c0, unsigned int c1, unsigned int c2, unsigned int c3)
{
    float* pos0;
    float* pos1;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826dc();
    pos0 = (float*)(unsigned int)(frame >> 0x20);
    pos1 = (float*)(unsigned int)frame;

    FUN_8017c474(0);
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);

    GXF(pos0[0]); GXF(pos0[1]); GXF(pos0[2]); GXU(gx_bswap32(c0));
    GXF(pos1[0]); GXF(pos1[1]); GXF(pos1[2]); GXU(gx_bswap32(c1));
    GXF(pos2[0]); GXF(pos2[1]); GXF(pos2[2]); GXU(gx_bswap32(c2));
    GXF(pos3[0]); GXF(pos3[1]); GXF(pos3[2]); GXU(gx_bswap32(c3));

    FUN_8017c474(1);
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_80382728();
}

// gx_draw_quad_f4 (0x8016fa50)
// Arbitrary 4-vertex quad with independent float (x,y) coords per corner and a
// byte-swapped colour per corner.  Corner 0 x arrives via the frame register
// (__save_gpr_28, as a float); the rest are float params.  Depth FLOAT_80616554.
void gx_draw_quad_f4(unsigned long long frameArgs, double y0, double x1, double y1,
                     double x2, double y2, double x3, double y3)
{
    float fx0;
    unsigned int c0, c1, c2, c3;
    unsigned long long frame;

    (void)frameArgs;
    frame = __save_gpr_28();
    fx0 = (float)(double)frame;
    c0 = GLOW_COLOR((unsigned int)(frame >> 0x20));
    c1 = GLOW_COLOR((unsigned int)frame);
    c2 = GLOW_COLOR(0);
    c3 = GLOW_COLOR(0);

    FUN_8017c658();
    FUN_8017c474(0);
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);

    GXF(fx0);       GXF((float)y0); GXF(FLOAT_80616554); GXU(gx_bswap32(c0));
    GXF((float)x1); GXF((float)y1); GXF(FLOAT_80616554); GXU(gx_bswap32(c1));
    GXF((float)x2); GXF((float)y2); GXF(FLOAT_80616554); GXU(gx_bswap32(c2));
    GXF((float)x3); GXF((float)y3); GXF(FLOAT_80616554); GXU(gx_bswap32(c3));

    FUN_8017c670();
    FUN_8017c474(1);
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    __restore_gpr_28();
}

// gx_draw_line_3d (0x8016fc04)
// 3D line (GX primitive 0xa8, 2 verts): point A (x from frame f1, y=param_2,
// z=param_3) and point B (param_4/5/6), with two colours from the frame pair.
void gx_draw_line_3d(unsigned long long frameArgs, double ay, double az,
                     double bx, double by, double bz)
{
    float ax;
    unsigned long long frame;

    (void)frameArgs;
    frame = FUN_8038264c();
    ax = (float)(double)frame;   /* A.x in f1 */

    FUN_8017c474(0);
    FUN_8017fab4();
    gx_begin_primitive(0xa8, 0, 2);

    GXF(ax);        GXF((float)ay); GXF((float)az); GXU((unsigned int)(frame >> 0x20));
    GXF((float)bx); GXF((float)by); GXF((float)bz); GXU((unsigned int)frame);

    FUN_8017c474(1);
    DAT_80611570 = DAT_80611570 + 2;
    DAT_80611574 = DAT_80611574 + 1;
    DAT_80611578 = DAT_80611578 + 1;
    FUN_80382698();
}

// gx_draw_line_i (0x8016fcd0)
// Int-coordinate 2D line (2 verts). (x0,y0) via the frame register pair
// (FUN_803826e8); (x1,y1)=x1i/y1i; single colour.  Depth FLOAT_8061655c.
// ~64 xrefs.
void gx_draw_line_i(unsigned int a, unsigned int b, unsigned int x1i, unsigned int y1i,
                    unsigned int color)
{
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826e8();
    FUN_8017c658();
    FUN_8017c474(0);
    FUN_8017fab4();
    gx_begin_primitive(0xa8, 0, 2);

    GXF(gx_i2f((unsigned int)(frame >> 0x20))); GXF(gx_i2f((unsigned int)frame));
    GXF(FLOAT_8061655c); GXU(color);
    GXF(gx_i2f(x1i)); GXF(gx_i2f(y1i)); GXF(FLOAT_8061655c); GXU(color);

    FUN_8017c670();
    FUN_8017c474(1);
    DAT_80611570 = DAT_80611570 + 2;
    DAT_80611574 = DAT_80611574 + 1;
    DAT_80611578 = DAT_80611578 + 1;
    FUN_80382734();
}

// gx_draw_line_f (0x8016fde8)
// Float-coordinate 2D line (2 verts): (x0,y0)-(x1,y1), single colour, depth
// FLOAT_8061655c.  No frame register.
void gx_draw_line_f(double x0, double y0, double x1, double y1, unsigned int color)
{
    FUN_8017c658();
    FUN_8017c474(0);
    FUN_8017fab4();
    gx_begin_primitive(0xa8, 0, 2);

    GXF((float)x0); GXF((float)y0); GXF(FLOAT_8061655c); GXU(color);
    GXF((float)x1); GXF((float)y1); GXF(FLOAT_8061655c); GXU(color);

    FUN_8017c670();
    FUN_8017c474(1);
    DAT_80611570 = DAT_80611570 + 2;
    DAT_80611574 = DAT_80611574 + 1;
    DAT_80611578 = DAT_80611578 + 1;
}

// gx_draw_line_counted (0x8016fee0)
// Common int-coord line entry (~188 xrefs): draw via gx_draw_line_i then bump
// the draw-stat counters a second time.
void gx_draw_line_counted(unsigned int a, unsigned int b, unsigned int x1i, unsigned int y1i,
                          unsigned int color)
{
    gx_draw_line_i(a, b, x1i, y1i, color);
    DAT_80611570 = DAT_80611570 + 2;
    DAT_80611574 = DAT_80611574 + 1;
    DAT_80611578 = DAT_80611578 + 1;
}

// gx_draw_vertex_list (0x8016ff24)
// General N-vertex 2D poly/strip drawer.  `count` (lo word of the frame register
// FUN_803826e4) vertices are streamed from a pointer (hi word), each = 3 words
// (x,y,colour) when textured==0, or 5 words (x,y,colour,u,v) when textured!=0,
// with the UV scaled by the active material's texel->UV factors.  Depth
// FLOAT_80616554.
void gx_draw_vertex_list(unsigned int a, unsigned int b, int textured)
{
    unsigned int* vp;
    int count;
    int i;
    float uScale;
    float vScale;
    unsigned int color;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826e4();
    count = (int)frame;
    uScale = FLOAT_806164fc;
    vScale = FLOAT_806164fc;

    FUN_8017c658();
    if (textured == 0) {
        FUN_8017fab4();
        FUN_8017c474(0);
        gx_begin_primitive(0x98, 0, (short)count);
    } else {
        if (DAT_80612254 != 0) {
            uScale = gx_uv_scale(*(unsigned short*)(DAT_80612254 + 0x4e));
            vScale = gx_uv_scale(*(unsigned short*)(DAT_80612254 + 0x52));
        }
        FUN_8017fab4();
        gx_begin_primitive(0x98, 0, (short)count);
    }

    vp = (unsigned int*)(unsigned int)(frame >> 0x20);
    for (i = 0; i < count; i = i + 1) {
        color = GLOW_COLOR(0);
        GXF(gx_i2f(vp[1]));
        GXF(gx_i2f(vp[2]));
        GXF(FLOAT_80616554);
        GXU(color);
        if (textured != 0) {
            GXF((float)((double)gx_i2f(vp[3]) * (double)uScale));
            GXF((float)((double)gx_i2f(vp[4]) * (double)vScale));
            vp = vp + 5;
        } else {
            vp = vp + 3;
        }
    }

    FUN_8017c670();
    if (textured == 0) {
        FUN_8017c474(1);
    }
    DAT_80611570 = DAT_80611570 + count;
    DAT_80611574 = count + DAT_80611574 - 2;
    DAT_80611578 = count + DAT_80611578 - 2;
    FUN_80382730();
}

// gx_draw_tri_textured (0x80170160)
// Textured 2D triangle (GX primitive 0x98, 3 verts).  Vertex 0 position comes
// from the frame register (FUN_803826bc); vertices are (pos int->float, colour,
// UV scaled by the active material tex dims).  params: v0.xy via frame, v1.xy =
// (p3,p4), v2.xy = (p5,p6); UV v0=(p7,p8) v1=(p9,p10) v2=(p11,p12); colours
// p13/p14/p15.
void gx_draw_tri_textured(unsigned int a, unsigned int b,
                          unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2,
                          unsigned int u0, unsigned int v0, unsigned int u1, unsigned int v1,
                          unsigned int u2, unsigned int v2,
                          unsigned int c0, unsigned int c1, unsigned int c2)
{
    float uScale;
    float vScale;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826bc();
    uScale = FLOAT_806164fc;
    vScale = FLOAT_806164fc;
    if (DAT_80612254 != 0) {
        uScale = gx_uv_scale(*(unsigned short*)(DAT_80612254 + 0x4e));
        vScale = gx_uv_scale(*(unsigned short*)(DAT_80612254 + 0x52));
    }

    FUN_8017c658();
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 3);

    GXF(gx_i2f((unsigned int)(frame >> 0x20))); GXF(gx_i2f((unsigned int)frame));
    GXF(FLOAT_80616554); GXU(c0);
    GXF((float)((double)gx_i2f(u0) * (double)uScale)); GXF((float)((double)gx_i2f(v0) * (double)vScale));

    GXF(gx_i2f(x1)); GXF(gx_i2f(y1)); GXF(FLOAT_80616554); GXU(c1);
    GXF((float)((double)gx_i2f(u1) * (double)uScale)); GXF((float)((double)gx_i2f(v1) * (double)vScale));

    GXF(gx_i2f(x2)); GXF(gx_i2f(y2)); GXF(FLOAT_80616554); GXU(c2);
    GXF((float)((double)gx_i2f(u2) * (double)uScale)); GXF((float)((double)gx_i2f(v2) * (double)vScale));

    FUN_8017c670();
    DAT_80611570 = DAT_80611570 + 3;
    DAT_80611574 = DAT_80611574 + 1;
    DAT_80611578 = DAT_80611578 + 1;
    FUN_80382708();
}

// gx_draw_sprite_rect (0x80170410)
// Textured axis-aligned 2D sprite/rect (GX quad, ~260 xrefs - the primary UI
// sprite blitter).  (x0,y0) via the frame register pair (FUN_803826e8);
// (x1,y1)=x1i/y1i; UV rect (u0,v0)=(p5,p6),(u1,v1)=(p7,p8) scaled by the active
// material tex dims.  Single colour from glow_render_stub.  Depth FLOAT_80616554.
void gx_draw_sprite_rect(unsigned int a, unsigned int b, unsigned int x1i, unsigned int y1i,
                         unsigned int u0, unsigned int v0, unsigned int u1, unsigned int v1)
{
    float uScale;
    float vScale;
    float fu0, fv0, fu1, fv1;
    unsigned int color;
    unsigned int frameHi;
    unsigned int frameLo;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826e8();
    uScale = FLOAT_806164fc;
    vScale = FLOAT_806164fc;
    if (DAT_80612254 != 0) {
        vScale = gx_uv_scale(*(unsigned short*)(DAT_80612254 + 0x52));
        uScale = gx_uv_scale(*(unsigned short*)(DAT_80612254 + 0x4e));
    }
    fu0 = (float)((double)gx_i2f(u0) * (double)uScale);
    fv0 = (float)((double)gx_i2f(v0) * (double)vScale);
    fu1 = (float)((double)gx_i2f(u1) * (double)uScale);
    fv1 = (float)((double)gx_i2f(v1) * (double)vScale);
    color = GLOW_COLOR(0);

    FUN_8017c658();
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);

    frameHi = (unsigned int)(frame >> 0x20);
    frameLo = (unsigned int)frame;

    GXF(gx_i2f(frameHi)); GXF(gx_i2f(frameLo)); GXF(FLOAT_80616554); GXU(color);
    GXF(fu0);             GXF(fv0);
    GXF(gx_i2f(x1i));     GXF(gx_i2f(frameLo)); GXF(FLOAT_80616554); GXU(color);
    GXF(fu1);             GXF(fv0);
    GXF(gx_i2f(frameHi)); GXF(gx_i2f(y1i));     GXF(FLOAT_80616554); GXU(color);
    GXF(fu0);             GXF(fv1);
    GXF(gx_i2f(x1i));     GXF(gx_i2f(y1i));     GXF(FLOAT_80616554); GXU(color);
    GXF(fu1);             GXF(fv1);

    FUN_8017c670();
    DAT_80611570 = DAT_80611570 + 2;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_80382734();
}


// ===========================================================================
// batch_450 : 0x801706cc - 0x80171f9c
// More GX 2D primitive drawers (solid/gradient rects, textured sprite quads
// with material-driven UV scaling, ptr-fed quads, clipped gradient lines) plus
// the env/reflect-matrix reset.  Same conventions as the batch_449 block above:
// enter 2D state, stream verts to the 0xCC008000 FIFO, bump DAT_80611570/74/78.
//
//   gx_fill_rect_solid            0x801706cc  solid axis-aligned rect (single colour)
//   gx_draw_sprite_quad           0x801708b4  textured quad, single colour, UV scaled
//   gx_draw_sprite_quad_c4        0x80170b10  textured quad, 4 per-corner colours
//   gx_draw_sprite_strip4         0x80170dbc  4-corner textured strip + per-vert z (HUD)
//   gx_draw_quad_ptr4_mat         0x80171130  quad from 4 vertex ptrs + material select
//   gx_draw_quad_ptr4_mat_z       0x80171324  same + per-vertex z
//   gx_draw_gradient_line_clipped 0x80171568  clipped gradient line strip
//   gx_draw_sprite_quad_z         0x80171cbc  textured quad, shared depth, 4 colours
//   gx_draw_sprite_quad_z_w       0x80171f7c  wrapper over gx_draw_sprite_quad_z
//   gx_set_reflect_matrix         0x80171f9c  set env-tex/reflect reset matrix + flags
// ===========================================================================

// ---- batch_450 additional externs ----
extern float  FLOAT_80616564;   // near-1.0 depth clamp (~0.99902)
extern int   *DAT_80610a20;     // texture-object / material table base+bounds
extern int    DAT_8058e40c;     // env-tex reflect reset matrix (3x4)
extern char   DAT_80612310;     // reflect-matrix dirty flag
extern int    DAT_80612258;     // env reflect latch
extern char   DAT_80612321;     // proj-tex reset flag

extern unsigned long long FUN_803826d8(void);  // frame reg reader (ptr+idx pair)
extern unsigned long long FUN_803826b4(void);   // frame reg reader (line-clip pair)
extern void FUN_80382724(void);
extern void FUN_80382700(void);
extern void FUN_801720a8(void);                 // active-material range fallback
extern double FUN_803825c0(double v);            // float->int quantize helper

// gx_fill_rect_solid (0x801706cc)
// Solid axis-aligned fill rect, single colour for all 4 corners.  x0 arrives via
// the frame register (FUN_803826e8) as a float; the colour is a single word run
// through glow_render_stub; y0/x1/y1 are the float params.  Callers: weather
// overlays (weather_render_particle_overlay / weather_render_gate).
void gx_fill_rect_solid(unsigned long long frameArgs, double y0, double x1, double y1,
                        unsigned int param7, unsigned int param8)
{
    unsigned long long frame;
    double fx0;      /* x0 in f1 */
    unsigned int color;
    unsigned int hi, lo;

    (void)frameArgs;
    frame = FUN_803826e8();
    fx0 = (double)frame;
    color = GLOW_COLOR(0);               /* identity colour pass-through */

    FUN_8017c658();
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);
    hi = (unsigned int)((frame ^ 0x8000000080000000ULL) >> 0x20);
    lo = (unsigned int)(frame ^ 0x8000000080000000ULL);
    /* 4 quad corners: UVs come from the frame pair + param7/param8 */
    GXF(gx_i2f(hi));     GXF(gx_i2f(lo)); GXF(FLOAT_80616554); GXU(color);
    GXF((float)fx0);     GXF((float)y0);
    GXF(gx_i2f(param7)); GXF(gx_i2f(lo)); GXF(FLOAT_80616554); GXU(color);
    GXF((float)y1);      GXF((float)y0);
    GXF(gx_i2f(hi));     GXF(gx_i2f(param8)); GXF(FLOAT_80616554); GXU(color);
    GXF((float)fx0);     GXF((float)x1);
    GXF(gx_i2f(param7)); GXF(gx_i2f(param8)); GXF(FLOAT_80616554); GXU(color);
    GXF((float)y1);      GXF((float)x1);
    FUN_8017c670();
    DAT_80611570 = DAT_80611570 + 2;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_80382734();
}

// gx_draw_sprite_quad (0x801708b4)
// Textured 2D quad (4 verts), single colour, UVs generated from the corner
// positions scaled by the active material's texel dims (+0x4e / +0x52) when a
// material is bound.  Positions param_1..param_4 (float), UV extents param_5..8.
void gx_draw_sprite_quad(double x0, double y0, double x1, double y1,
                         unsigned int u0, unsigned int v0,
                         unsigned int u1, unsigned int v1)
{
    float sx = FLOAT_806164fc, sy = FLOAT_806164fc;
    float fu0, fv0, fu1, fv1;
    unsigned int color;

    if (DAT_80612254 != 0) {
        sx = gx_uv_scale((unsigned int)*(unsigned short *)(DAT_80612254 + 0x52));
        sy = gx_uv_scale((unsigned int)*(unsigned short *)(DAT_80612254 + 0x4e));
    }
    fu0 = gx_i2f_neg(u0) * sy;
    fv0 = gx_i2f_neg(v0) * sx;
    fu1 = gx_i2f_neg(u1) * sy;
    fv1 = gx_i2f_neg(v1) * sx;

    color = GLOW_COLOR(0);
    FUN_8017c658();
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);
    GXF((float)x0); GXF((float)y0); GXF(FLOAT_80616554); GXU(color); GXF(fu0); GXF(fv0);
    GXF((float)x1); GXF((float)y0); GXF(FLOAT_80616554); GXU(color); GXF(fu1); GXF(fv0);
    GXF((float)x0); GXF((float)y1); GXF(FLOAT_80616554); GXU(color); GXF(fu0); GXF(fv1);
    GXF((float)x1); GXF((float)y1); GXF(FLOAT_80616554); GXU(color); GXF(fu1); GXF(fv1);
    FUN_8017c670();
    DAT_80611570 = DAT_80611570 + 2;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
}

void gx_draw_sprite_quad_c4(unsigned long long frameArgs, double y0, double x1, double y1,
                            unsigned int param5, unsigned int param6,
                            unsigned int u1, unsigned int v1)
{
    unsigned long long frame;
    float sx = FLOAT_806164fc, sy = FLOAT_806164fc;
    float fx0, fu0, fv0, fu1, fv1;
    unsigned int c0, c1, c2, c3;

    (void)frameArgs; (void)param5; (void)param6;
    frame = FUN_803826dc();
    fx0 = (float)(double)frame;          /* x0 in f1 */
    c0 = GLOW_COLOR(0); c1 = GLOW_COLOR(0); c2 = GLOW_COLOR(0); c3 = GLOW_COLOR(0);

    if (DAT_80612254 != 0) {
        sx = gx_uv_scale((unsigned int)*(unsigned short *)(DAT_80612254 + 0x52));
        sy = gx_uv_scale((unsigned int)*(unsigned short *)(DAT_80612254 + 0x4e));
    }
    /* UV corners from the frame reg pair + params */
    {
        unsigned int uhi = (unsigned int)((frame ^ 0x8000000080000000ULL) >> 0x20);
        unsigned int ulo = (unsigned int)(frame ^ 0x8000000080000000ULL);
        fu0 = gx_i2f(uhi) * sy;
        fv0 = gx_i2f(ulo) * sx;
        fu1 = gx_i2f(u1) * sy;
        fv1 = gx_i2f(v1) * sx;
    }

    FUN_8017c658();
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);
    GXF(fx0);       GXF((float)y0); GXF(FLOAT_80616554); GXU(gx_bswap32(c0)); GXF(fu0); GXF(fv0);
    GXF((float)x1); GXF((float)y0); GXF(FLOAT_80616554); GXU(gx_bswap32(c1)); GXF(fu1); GXF(fv0);
    GXF(fx0);       GXF((float)y1); GXF(FLOAT_80616554); GXU(gx_bswap32(c2)); GXF(fu0); GXF(fv1);
    GXF((float)x1); GXF((float)y1); GXF(FLOAT_80616554); GXU(gx_bswap32(c3)); GXF(fu1); GXF(fv1);
    FUN_8017c670();
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_80382728();
}

// gx_draw_sprite_strip4 (0x80170dbc)
// Four-corner textured strip with a per-primitive z depth (param_17, clamped to
// >= FLOAT_80616554) and 4 per-corner byte-swapped colours; UVs from the frame
// reg pair + params, material-scaled.  Heavy HUD user (charge meter, damage ring,
// target box) and sprite_grid_render_distortion.
void gx_draw_sprite_strip4(unsigned long long frameArgs, double x0, double y0,
                           double x1, double y1, double x2, double y2, double x3,
                           unsigned int u1, unsigned int v1, unsigned int u2,
                           unsigned int v2, unsigned int u3, unsigned int v3,
                           float z, unsigned int color0, unsigned int color1,
                           unsigned int color2, unsigned int color3)
{
    unsigned long long frame;
    float sx = FLOAT_806164fc, sy = FLOAT_806164fc;
    float fu0, fv0, fu1, fv1, fu2, fv2, fu3, fv3;
    double depth;

    (void)frameArgs;
    frame = FUN_803826e4();

    if (DAT_80612254 != 0) {
        sx = gx_uv_scale((unsigned int)*(unsigned short *)(DAT_80612254 + 0x52));
        sy = gx_uv_scale((unsigned int)*(unsigned short *)(DAT_80612254 + 0x4e));
    }
    {
        unsigned int uhi = (unsigned int)((frame ^ 0x8000000080000000ULL) >> 0x20);
        unsigned int ulo = (unsigned int)(frame ^ 0x8000000080000000ULL);
        fu0 = gx_i2f_bias(uhi) * sy;  fv0 = gx_i2f_bias(ulo) * sx;
        fu1 = gx_i2f_neg(u1) * sy;    fv1 = gx_i2f_neg(v1) * sx;
        fu2 = gx_i2f_neg(u2) * sy;    fv2 = gx_i2f_neg(v2) * sx;
        fu3 = gx_i2f_neg(u3) * sy;    fv3 = gx_i2f_neg(v3) * sx;
    }
    depth = (double)z;
    if (depth < (double)FLOAT_80616554)
        depth = (double)FLOAT_80616554;

    FUN_8017c658();
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);
    GXF((float)x0); GXF((float)y0); GXF((float)depth); GXU(gx_bswap32(color0)); GXF(fu0); GXF(fv0);
    GXF((float)x1); GXF((float)y1); GXF((float)depth); GXU(gx_bswap32(color1)); GXF(fu1); GXF(fv1);
    GXF((float)x2); GXF((float)y2); GXF((float)depth); GXU(gx_bswap32(color2)); GXF(fu2); GXF(fv2);
    GXF((float)x3); GXF((float)z);  GXF((float)depth); GXU(gx_bswap32(color3)); GXF(fu3); GXF(fv3);
    FUN_8017c670();
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_80382730();
}

// gx_draw_quad_ptr4_mat (0x80171130)
// Textured quad from 4 vertex-position pointers (xyz each), 4 per-corner colours,
// selecting the active material from a frame-supplied index (bounds-checked
// against DAT_80610a20; out-of-range -> FUN_801720a8 fallback).  First vertex
// ptr + material index arrive in the frame register (FUN_803826d8).
void gx_draw_quad_ptr4_mat(unsigned long long frameArgs, double y0, double x1, double y1,
                           unsigned int param5, unsigned int param6,
                           int *pos1, int *pos2, int *pos3)
{
    unsigned long long frame;
    long long fpair;
    int matIndex;
    int *pos0;
    double z0;
    unsigned int c0, c1, c2, c3;

    (void)frameArgs; (void)param5; (void)param6;
    fpair = (long long)FUN_803826d8();
    matIndex = (int)((unsigned long long)fpair >> 0x20);
    pos0 = (int *)(long)fpair;
    z0 = (double)(float)(long)fpair;    /* f1 depth carried alongside */
    c0 = GLOW_COLOR(0); c1 = GLOW_COLOR(0); c2 = GLOW_COLOR(0); c3 = GLOW_COLOR(0);

    if ((fpair < 0) || (DAT_80610a20[3] <= matIndex) || (matIndex <= DAT_80610a20[2]))
        FUN_801720a8();
    else
        DAT_80612254 = *DAT_80610a20 + matIndex * 0x58;

    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);
    GXF((float)pos0[0]); GXF((float)pos0[1]); GXF((float)pos0[2]); GXU(gx_bswap32(c0)); GXF((float)z0);      GXF((float)y0);
    GXF((float)pos1[0]); GXF((float)pos1[1]); GXF((float)pos1[2]); GXU(gx_bswap32(c1)); GXF((float)x1);      GXF((float)y0);
    GXF((float)pos2[0]); GXF((float)pos2[1]); GXF((float)pos2[2]); GXU(gx_bswap32(c2)); GXF((float)z0);      GXF((float)y1);
    GXF((float)pos3[0]); GXF((float)pos3[1]); GXF((float)pos3[2]); GXU(gx_bswap32(c3)); GXF((float)x1);      GXF((float)y1);
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_80382724();
}

// gx_draw_quad_ptr4_mat_z (0x80171324)
// As gx_draw_quad_ptr4_mat but each corner carries its own (u,v) pair supplied as
// the 8 float params (param_2..param_8 + the frame f1), material-select identical.
void gx_draw_quad_ptr4_mat_z(unsigned long long frameArgs, double y0, double u1, double v1,
                             double u2, double v2, double u3, double v3,
                             unsigned int param9, unsigned int param10,
                             int *pos1, int *pos2, int *pos3)
{
    unsigned long long frame;
    long long fpair;
    int matIndex;
    int *pos0;
    double z0;
    unsigned int c0, c1, c2, c3;

    (void)frameArgs; (void)param9; (void)param10;
    fpair = (long long)FUN_803826d8();
    matIndex = (int)((unsigned long long)fpair >> 0x20);
    pos0 = (int *)(long)fpair;
    z0 = (double)(float)(long)fpair;
    c0 = GLOW_COLOR(0); c1 = GLOW_COLOR(0); c2 = GLOW_COLOR(0); c3 = GLOW_COLOR(0);

    if ((fpair < 0) || (DAT_80610a20[3] <= matIndex) || (matIndex <= DAT_80610a20[2]))
        FUN_801720a8();
    else
        DAT_80612254 = *DAT_80610a20 + matIndex * 0x58;

    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);
    GXF((float)pos0[0]); GXF((float)pos0[1]); GXF((float)pos0[2]); GXU(gx_bswap32(c0)); GXF((float)z0); GXF((float)y0);
    GXF((float)pos1[0]); GXF((float)pos1[1]); GXF((float)pos1[2]); GXU(gx_bswap32(c1)); GXF((float)u1); GXF((float)v1);
    GXF((float)pos2[0]); GXF((float)pos2[1]); GXF((float)pos2[2]); GXU(gx_bswap32(c2)); GXF((float)u2); GXF((float)v2);
    GXF((float)pos3[0]); GXF((float)pos3[1]); GXF((float)pos3[2]); GXU(gx_bswap32(c3)); GXF((float)u3); GXF((float)v3);
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_80382724();
}

// gx_draw_gradient_line_clipped (0x80171568)
// Clipped gradient line span: clips the [y0,y1] segment against a pair of bounds
void gx_draw_gradient_line_clipped(unsigned long long frameArgs, double y0, double y1a, double y1b,
                                   unsigned int param5, unsigned int param6,
                                   unsigned int colorA, unsigned int colorB,
                                   unsigned int colorC, unsigned int colorD,
                                   unsigned int bound0, unsigned int bound1,
                                   unsigned int bound2, unsigned int bound3)
{
    unsigned long long frame;
    unsigned int startColor, spanU;
    double lo, hi, t0, t1;

    (void)frameArgs;
    frame = FUN_803826b4();
    startColor = (unsigned int)(frame >> 0x20);
    spanU = (unsigned int)frame;
    startColor = (unsigned int)(frame >> 0x20);
    if (gx_i2f(bound2) <= y0) {
        /* ascending clip against bound3 / colorC..colorD (param_15/16) */
        if (y1a < gx_i2f(bound3)) {
            gx_draw_sprite_quad_c4(frame, y0, y1a, y1b, startColor, spanU, param5, param6);
        } else if (y0 < gx_i2f(colorD)) {
            lo = y0;
            if (y0 <= gx_i2f(colorC))
                lo = gx_i2f(colorC);
            hi = y1a;
            if (gx_i2f(colorD) <= y1a)
                hi = gx_i2f(colorD);
            t0 = (double)((float)(lo - gx_i2f(colorC)) / gx_i2f(colorD - colorC));
            t1 = (double)((float)(hi - gx_i2f(colorC)) / gx_i2f(colorD - colorC));
            (void)color_lerp(t0, colorA, colorC);
            (void)color_lerp(t0, colorB, colorD);
            (void)color_lerp(t1, colorA, colorC);
            (void)color_lerp(t1, colorB, colorD);
            FUN_803825c0((double)(float)(gx_u16f(colorD >> 0x18) * (float)((double)FLOAT_806164fc - t1)));
            FUN_803825c0((double)(float)(gx_u16f(colorC >> 0x18) * (float)((double)FLOAT_806164fc - t1)));
            FUN_803825c0((double)(float)(gx_u16f(colorB >> 0x18) * (float)((double)FLOAT_806164fc - t0)));
            FUN_803825c0((double)(float)(gx_u16f(colorA >> 0x18) * (float)((double)FLOAT_806164fc - t0)));
            gx_draw_sprite_quad_c4(frame, lo, y1a, hi, startColor, spanU, param5, param6);
            if (y0 < lo)
                gx_draw_sprite_quad_c4(frame, y0, y1a, lo, startColor, spanU, param5, param6);
        }
    } else if (gx_i2f(bound1) <= y1a) {
        /* descending clip against bound0 / bound1 (param_13/14) */
        lo = y0;
        if (y0 <= gx_i2f(bound1))
            lo = gx_i2f(bound1);
        hi = y1a;
        if (gx_i2f(bound2) <= y1a)
            hi = gx_i2f(bound2);
        t0 = (double)((float)(lo - gx_i2f(bound0)) / gx_i2f(bound2 - bound0));
        t1 = (double)((float)(hi - gx_i2f(bound0)) / gx_i2f(bound2 - bound0));
        (void)color_lerp(t0, colorA, colorC);
        (void)color_lerp(t0, colorB, colorD);
        (void)color_lerp(t1, colorA, colorC);
        (void)color_lerp(t1, colorB, colorD);
        FUN_803825c0((double)(float)(gx_u16f(colorD >> 0x18) * (float)t1));
        FUN_803825c0((double)(float)(gx_u16f(colorC >> 0x18) * (float)t1));
        FUN_803825c0((double)(float)(gx_u16f(colorB >> 0x18) * (float)t0));
        FUN_803825c0((double)(float)(gx_u16f(colorA >> 0x18) * (float)t0));
        gx_draw_sprite_quad_c4(frame, lo, y1a, hi, startColor, spanU, param5, param6);
        if (hi < y1a)
            gx_draw_sprite_quad_c4(frame, hi, y1a, y1b, startColor, spanU, param5, param6);
    }
    FUN_80382700();
}

// gx_draw_sprite_quad_z (0x80171cbc)
// Textured 2D quad with a shared depth (param_5, clamped to <= FLOAT_80616564),
// 4 per-corner byte-swapped colours, material-scaled UVs; x0/u0 from frame reg.
void gx_draw_sprite_quad_z(unsigned long long frameArgs, double y0, double x1, double y1,
                           double z, unsigned int param6, unsigned int param7,
                           unsigned int u1, unsigned int v1)
{
    unsigned long long frame;
    float sx = FLOAT_806164fc, sy = FLOAT_806164fc;
    float fx0, fu0, fv0, fu1, fv1;
    unsigned int c0, c1, c2, c3;

    (void)frameArgs; (void)param6; (void)param7;
    frame = FUN_803826dc();
    fx0 = (float)(double)frame;
    c0 = GLOW_COLOR(0); c1 = GLOW_COLOR(0); c2 = GLOW_COLOR(0); c3 = GLOW_COLOR(0);

    if (DAT_80612254 != 0) {
        sx = gx_uv_scale((unsigned int)*(unsigned short *)(DAT_80612254 + 0x52));
        sy = gx_uv_scale((unsigned int)*(unsigned short *)(DAT_80612254 + 0x4e));
    }
    {
        unsigned int uhi = (unsigned int)((frame ^ 0x8000000080000000ULL) >> 0x20);
        unsigned int ulo = (unsigned int)(frame ^ 0x8000000080000000ULL);
        fu0 = gx_i2f_bias(uhi) * sy; fv0 = gx_i2f_bias(ulo) * sx;
        fu1 = gx_i2f_neg(u1) * sy;   fv1 = gx_i2f_neg(v1) * sx;
    }
    if ((double)FLOAT_80616564 < z)
        z = (double)FLOAT_80616564;

    FUN_8017c658();
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);
    GXF(fx0);       GXF((float)y0); GXF((float)z); GXU(gx_bswap32(c0)); GXF(fu0); GXF(fv0);
    GXF((float)x1); GXF((float)y0); GXF((float)z); GXU(gx_bswap32(c1)); GXF(fu1); GXF(fv0);
    GXF(fx0);       GXF((float)y1); GXF((float)z); GXU(gx_bswap32(c2)); GXF(fu0); GXF(fv1);
    GXF((float)x1); GXF((float)y1); GXF((float)z); GXU(gx_bswap32(c3)); GXF(fu1); GXF(fv1);
    FUN_8017c670();
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_80382728();
}

// gx_draw_sprite_quad_z_w (0x80171f7c)
// Thin forwarding wrapper over gx_draw_sprite_quad_z.
void gx_draw_sprite_quad_z_w(unsigned long long frameArgs, double y0, double x1, double y1,
                             double z, unsigned int param6, unsigned int param7,
                             unsigned int u1, unsigned int v1)
{
    gx_draw_sprite_quad_z(frameArgs, y0, x1, y1, z, param6, param7, u1, v1);
}

// gx_set_reflect_matrix (0x80171f9c)
// Store the transpose of the supplied 3x4 matrix into the env-tex/reflect reset
// matrix (DAT_8058e40c) and mark it dirty + reset the env-reflect latch and the
// projected-texture reset flag.
void gx_set_reflect_matrix(float *matrix)
{
    mtx4_transpose(matrix, (float *)&DAT_8058e40c);
    DAT_80612310 = 1;
    DAT_80612258 = 0;
    DAT_80612321 = 0;
}
