// =============================================================================
// viewport.cpp - Screen viewport / scissor-region state management
// Address range: 0x8001c554 - 0x8001c8eb
// Batch: 383 | Date: 2026-09-05
//
// The software viewport state layer that feeds the GX scissor/viewport. A
// viewport record (0x44 bytes) stores the pixel rect plus derived centre,
// half-size and downscale factors relative to the base render resolution
// (DAT_806109c4 x DAT_806109c8). One "active" viewport pointer (DAT_80611748)
// is the implicit target of the convenience wrappers. Callers: gx_init_render_
// mode, game_main_loop, particle_render_list and the render setup at 0x8018xxxx.
//
//   * viewport_reset_default_bounds   (0x8001c554) reset default bound globals
//   * viewport_commit_to_gx           (0x8001c59c) publish bounds to GX block
//   * viewport_set_active             (0x8001c6c8) set active viewport pointer
//   * viewport_set_rect_active        (0x8001c6d0) set rect on active viewport
//   * viewport_set_rect               (0x8001c710) fill a viewport record
//   * viewport_set_depth_params       (0x8001c834) store 3 depth/scale floats
//   * viewport_set_depth_params_active(0x8001c844) same on active viewport
//   * viewport_save_active            (0x8001c868) copy active -> saved global
//   * viewport_swap_active_saved      (0x8001c898) swap active <-> saved
// =============================================================================
#include "common.h"

// ---- base render resolution + viewport-bound globals ----------------------
extern int DAT_806109c4;   // base render width  (pixels)
extern int DAT_806109c8;   // base render height (pixels)

extern int DAT_80611720;   // default bound: y half
extern int DAT_80611724;   // default bound: y full
extern int DAT_80611728;   // default bound: y half
extern int DAT_8061172c;   // default bound: y full-1
extern int DAT_80611730;   // default bound: y 0
extern int DAT_80611734;   // default bound: x half
extern int DAT_80611738;   // default bound: x full
extern int DAT_8061173c;   // default bound: x half
extern int DAT_80611740;   // default bound: x full-1
extern int DAT_80611744;   // default bound: x 0
extern int DAT_80611748;   // active viewport pointer

// ---- GX render-state viewport block (published copy) ----------------------
extern int   DAT_804cdb98;
extern int   DAT_804cdb9c;
extern int   DAT_804cdba0;
extern int   DAT_804cdba4;
extern int   DAT_804cdba8;
extern int   DAT_804cdbac;
extern int   DAT_804cdbb0;
extern int   DAT_804cdbb4;
extern int   DAT_804cdbb8;
extern int   DAT_804cdbbc;
extern float DAT_804cdbc0;
extern float DAT_804cdbc4;
extern int   DAT_804cdbc8;
extern int   DAT_804cdbcc;
extern float DAT_804cdbd0;
extern float DAT_804cdbd4;
extern float DAT_804cdbd8;

// ---- numeric constants ----------------------------------------------------
extern double DOUBLE_806138e8;  // int->double conversion magic bias
extern float  FLOAT_806138e0;   // half-pixel / depth base
extern float  FLOAT_806138e4;   // depth-range constant
extern float  FLOAT_8060fad0;   // half-pixel offset

// leaf_31e8(dst, src, len) is a memcpy-like helper (declared in generated_decls.h
// as void leaf_31e8(u32,u32,s32)). This TU only includes common.h, so declare it
// locally with the matching plain signature.
void leaf_31e8(unsigned int dst, unsigned int src, int len);

// forward declarations for intra-unit wrappers
void viewport_set_active(int vp);
void viewport_set_rect(int* vp, int left, int top, int right, int bottom);
void viewport_set_depth_params(double a, double b, double c, int vp);

// viewport_reset_default_bounds (0x8001c554)
// Reset the default viewport bound globals from the current base resolution
// (DAT_806109c4 x DAT_806109c8): full/half spans on each axis, plus the origin.
void viewport_reset_default_bounds(void)
{
    DAT_80611744 = 0;
    DAT_80611740 = DAT_806109c4 + -1;
    DAT_8061173c = DAT_806109c4 >> 1;
    DAT_80611738 = DAT_806109c4;
    DAT_80611734 = DAT_806109c4 >> 1;
    DAT_80611730 = 0;
    DAT_8061172c = DAT_806109c8 + -1;
    DAT_80611728 = DAT_806109c8 >> 1;
    DAT_80611724 = DAT_806109c8;
    DAT_80611720 = DAT_806109c8 >> 1;
}

// viewport_commit_to_gx (0x8001c59c)
// Publish the default viewport bounds into the GX render-state block
// (DAT_804cdb98..) with the derived inverse scale factors and depth range, then
// mark that block as the active viewport.
void viewport_commit_to_gx(void)
{
    DAT_804cdbc0 = (float)((double)CONCAT44(0x43300000, DAT_80611738 ^ 0x80000000) - DOUBLE_806138e8) /
                   (float)((double)CONCAT44(0x43300000, DAT_80611738 ^ 0x80000000) - DOUBLE_806138e8);
    DAT_804cdb98 = DAT_80611744;
    DAT_804cdbd4 = FLOAT_806138e0 + FLOAT_8060fad0;
    DAT_804cdbc4 = (float)((double)CONCAT44(0x43300000, DAT_80611724 ^ 0x80000000) - DOUBLE_806138e8) /
                   (float)((double)CONCAT44(0x43300000, DAT_80611724 ^ 0x80000000) - DOUBLE_806138e8);
    DAT_804cdb9c = DAT_80611740;
    DAT_804cdba0 = DAT_8061173c;
    DAT_804cdba4 = DAT_80611738;
    DAT_804cdba8 = DAT_80611734;
    DAT_804cdbd0 = -DAT_804cdbd4;
    DAT_804cdbc8 = 0;
    DAT_804cdbac = DAT_80611730;
    DAT_804cdbb0 = DAT_8061172c;
    DAT_804cdbb4 = DAT_80611728;
    DAT_804cdbb8 = DAT_80611724;
    DAT_804cdbbc = DAT_80611720;
    DAT_804cdbcc = 0;
    DAT_804cdbd8 = FLOAT_806138e4;
    viewport_set_active((int)(unsigned long)&DAT_804cdb98);
}

// viewport_set_active (0x8001c6c8)
// Set the implicit "active" viewport pointer used by the convenience wrappers.
void viewport_set_active(int vp)
{
    DAT_80611748 = vp;
}

// viewport_set_rect_active (0x8001c6d0)
// Set the pixel rect on the currently-active viewport.
void viewport_set_rect_active(int left, int top, int right, int bottom)
{
    viewport_set_rect((int*)(unsigned long)(unsigned int)DAT_80611748, left, top, right, bottom);
}

// viewport_set_rect (0x8001c710)
// Fill a viewport record from a pixel rect (left/top/right/bottom). Computes the
// centre, full/half size on each axis, the inverse scale relative to the default
// bound span, and a downscale flag when the region is at or below half the base
// resolution.
void viewport_set_rect(int* vp, int left, int top, int right, int bottom)
{
    double bias = DOUBLE_806138e8;
    vp[0] = left;
    vp[1] = right;
    vp[5] = top;
    vp[6] = bottom;
    vp[2] = left + right + 1 >> 1;
    vp[3] = (right - left) + 1;
    vp[4] = vp[3] >> 1;
    vp[10] = (int)((float)((double)CONCAT44(0x43300000, vp[3] ^ 0x80000000) - bias) /
                   (float)((double)CONCAT44(0x43300000, DAT_80611738 ^ 0x80000000) - bias));
    vp[0xc] = (DAT_806109c4 >> 0x1f) +
              ((unsigned int)((unsigned int)vp[3] <= (unsigned int)(DAT_806109c4 >> 1)) - (vp[3] >> 0x1f));
    vp[7] = top + bottom + 1 >> 1;
    vp[8] = (bottom - top) + 1;
    vp[9] = vp[8] >> 1;
    vp[0xb] = (int)((float)((double)CONCAT44(0x43300000, vp[8] ^ 0x80000000) - bias) /
                    (float)((double)CONCAT44(0x43300000, DAT_80611724 ^ 0x80000000) - bias));
    vp[0xd] = (DAT_806109c8 >> 0x1f) +
              ((unsigned int)((unsigned int)vp[8] <= (unsigned int)(DAT_806109c8 >> 1)) - (vp[8] >> 0x1f));
}

// viewport_set_depth_params (0x8001c834)
// Store three float parameters (depth/scale) at vp+0x38/+0x3c/+0x40.
void viewport_set_depth_params(double a, double b, double c, int vp)
{
    *(float*)(vp + 0x3c) = (float)a;
    *(float*)(vp + 0x38) = (float)b;
    *(float*)(vp + 0x40) = (float)c;
}

// viewport_set_depth_params_active (0x8001c844)
// Store depth params on the currently-active viewport.
void viewport_set_depth_params_active(double a, double b, double c)
{
    viewport_set_depth_params(a, b, c, DAT_80611748);
}

// viewport_save_active (0x8001c868)
// Copy the active viewport record (0x44 bytes) into the saved slot 0x804cdbdc.
void viewport_save_active(void)
{
    leaf_31e8(0x804cdbdc, (unsigned int)DAT_80611748, 0x44);
}

// viewport_swap_active_saved (0x8001c898)
// Swap the active viewport record with the saved slot (0x804cdbdc) using a
// stack temporary (three 0x44-byte copies).
void viewport_swap_active_saved(void)
{
    unsigned char tmp[72];
    leaf_31e8((unsigned int)(unsigned long)tmp, (unsigned int)DAT_80611748, 0x44);
    leaf_31e8((unsigned int)DAT_80611748, 0x804cdbdc, 0x44);
    leaf_31e8(0x804cdbdc, (unsigned int)(unsigned long)tmp, 0x44);
}

// =============================================================================
// Batch 384: viewport GX submission + 2D/overlay draw helpers
// Address range: 0x8001c8ec - 0x8001d4f4
//   * viewport_set_gx_active     (0x8001c8ec) apply active vp to GX (35 xrefs)
//   * viewport_set_gx            (0x8001c910) GXSetViewport from vp record
//   * viewport_ndc_to_screen     (0x8001c9bc) NDC -> screen-pixel transform
//   * screen_norm_width          (0x8001ca5c) value / render-target width
//   * screen_norm_height         (0x8001cac0) value / render-target height
//   * ribbon_strip_render        (0x8001cb24) GX ribbon/trail tri-strip emit
//   * line_gradient_clipped_draw (0x8001d030) clipped fade line span
//   * debug_draw_flush_a         (0x8001d4b4) flush wrapper
//   * debug_draw_flush_b         (0x8001d4d4) flush wrapper
//   * render2d_list_reset        (0x8001d4f4) reset 2D render list + state
// =============================================================================

// ---- GX FIFO write-gather port (0xCC008000, write-only volatile) ----------
#define GX_FIFO_ADDR 0xCC008000u
static volatile float* const kGxFifoF32 = (volatile float*)GX_FIFO_ADDR;
static volatile unsigned int* const kGxFifoU32 = (volatile unsigned int*)GX_FIFO_ADDR;
#define GX_FIFO_F32 (*kGxFifoF32)
#define GX_FIFO_U32 (*kGxFifoU32)

// ---- extra viewport / render-state globals --------------------------------
extern int   DAT_80611d54;   // camera / render context pointer
extern int   DAT_80611750;
extern int   DAT_80611754;
extern int   DAT_80611758;
extern int   DAT_8061175c;
extern int   DAT_80611760;
extern float FLOAT_80611764;
extern float FLOAT_80611768;

// ---- additional numeric constants -----------------------------------------
extern float  FLOAT_806138f0;   // depth base (1.0-ish)
extern float  FLOAT_806138f4;   // depth divisor (2^24 z-range)
extern float  FLOAT_806138f8;   // 0.0
extern float  FLOAT_806138fc;   // 0.5
extern double DOUBLE_80613900;  // uint->double conversion magic bias
extern float  FLOAT_80613908;   // 1.0
extern float  FLOAT_8061390c;   // 2.0
extern float  FLOAT_80613910;   // small epsilon (len^2 floor)
extern float  FLOAT_80613914;   // rsqrt lower bound
extern float  FLOAT_80613918;   // 0.5 (rsqrt Newton)
extern float  FLOAT_8061391c;   // 3.0 (rsqrt Newton)
extern double DOUBLE_80613920;  // byte->double conversion magic bias
extern float  FLOAT_80613928;   // render2d default param a
extern float  FLOAT_8061392c;   // render2d default param b

// ---- helpers (still FUN_/runtime; self-extern at call-site arity) ----------
extern double math_sqrt(double x);

// Ghidra emits SQRT() for the hardware sqrt used in fast-rsqrt seeds. This TU
// only includes common.h (no generated_externs), so provide a local shim.
static inline double SQRT(double x) { return math_sqrt(x); }
extern "C" {
    void gx_set_tex_transform_params(double x, double y, double w, double h, double zn, double zf);
    unsigned int FUN_80174894(int ctx);   // render-target width
    unsigned int FUN_801748bc(int ctx);   // render-target height
    void FUN_80172a4c(int a, int b, int c, short d, int e);   // GXBegin
    void FUN_80172c54(void);                                  // GXEnd
    void FUN_801708b4(double a, double x0, double y, double x1, unsigned int cHi, int c0, unsigned int col, int c1); // solid line
    void FUN_80170b10(double a, double x0, double y, double x1, unsigned int cHi, int c0, unsigned int col, int c1); // gradient line
    void FUN_803825c0(double alpha);       // set draw alpha
    void FUN_8017c658(void);
    void FUN_8017c670(void);
    unsigned long long FUN_803826cc(void); // scratch alloc (polyline/frame)
    void FUN_80382718(void);               // scratch free
    unsigned long long __save_gpr_25(void);
    void __restore_gpr_25(void);
}

// viewport_set_gx_active (0x8001c8ec)
// Apply the currently-active viewport to the GX hardware viewport (35 xrefs).
void viewport_set_gx_active(void)
{
    extern void viewport_set_gx(unsigned int* vp);
    viewport_set_gx((unsigned int*)(unsigned long)(unsigned int)DAT_80611748);
}

// viewport_set_gx (0x8001c910)
// Program the GX viewport from a viewport record: origin (vp[0],vp[5]), extent
// (vp[3],vp[8]) and the near/far depth range derived from vp[0xf]/vp[0xe].
void viewport_set_gx(unsigned int* vp)
{
    double bias = DOUBLE_806138e8;
    gx_set_tex_transform_params(
        (double)(float)((double)CONCAT44(0x43300000, vp[0] ^ 0x80000000) - bias),
        (double)(float)((double)CONCAT44(0x43300000, vp[5] ^ 0x80000000) - bias),
        (double)(float)((double)CONCAT44(0x43300000, vp[3] ^ 0x80000000) - bias),
        (double)(float)((double)CONCAT44(0x43300000, vp[8] ^ 0x80000000) - bias),
        (double)(FLOAT_806138f0 - (float)vp[0xf] / FLOAT_806138f4),
        (double)(FLOAT_806138f0 - ((float)vp[0xf] + (float)vp[0xe]) / FLOAT_806138f4));
}

// viewport_ndc_to_screen (0x8001c9bc)
// Transform a normalized coordinate (in[0..2]) into screen-pixel space using the
// active viewport's half-width (+0x10), centre-x (+8), -half-height (+0x24) and
// centre-y (+0x1c). Z is passed through.
void viewport_ndc_to_screen(float* out, float* in)
{
    double bias = DOUBLE_806138e8;
    int vp = DAT_80611748;
    out[0] = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(vp + 0x10) ^ 0x80000000) - bias) * in[0] +
             (float)((double)CONCAT44(0x43300000, *(unsigned int*)(vp + 8) ^ 0x80000000) - bias);
    out[1] = (float)((double)CONCAT44(0x43300000, -*(int*)(vp + 0x24) ^ 0x80000000) - bias) * in[1] +
             (float)((double)CONCAT44(0x43300000, *(unsigned int*)(vp + 0x1c) ^ 0x80000000) - bias);
    out[2] = in[2];
}

// screen_norm_width (0x8001ca5c)
// Divide 'v' by the render-target width for the given context (0 if width is 0).
double screen_norm_width(double v, int ctx)
{
    unsigned int w = FUN_80174894(ctx);
    float r = FLOAT_806138f8;
    if (w != 0) {
        r = (float)(v / (double)(float)((double)CONCAT44(0x43300000, w ^ 0x80000000) - DOUBLE_80613900));
    }
    return (double)r;
}

// screen_norm_height (0x8001cac0)
// Divide 'v' by the render-target height for the given context (0 if height 0).
double screen_norm_height(double v, int ctx)
{
    unsigned int h = FUN_801748bc(ctx);
    float r = FLOAT_806138f8;
    if (h != 0) {
        r = (float)(v / (double)(float)((double)CONCAT44(0x43300000, h ^ 0x80000000) - DOUBLE_80613900));
    }
    return (double)r;
}

// ribbon_strip_render (0x8001cb24)
// Emit a camera-facing ribbon/trail triangle strip through the GX FIFO. For each
// of 'count' polyline points (in the scratch vertex buffer) it estimates the
// local tangent (central difference), builds a screen-facing side vector from
// the camera basis (DAT_80611d54+0x334..0x33c), scales it by the ribbon width
// (an alpha-ramp with taper mode 'taperMode' 0..5) and writes the left/right
// strip vertices with a packed colour.
void ribbon_strip_render(unsigned int a, unsigned int b, unsigned int count,
                         int fmt, unsigned int colorPacked, int taperMode)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826cc();
    int vbuf = (int)(scratch >> 0x20);
    unsigned int alphaBuf = (unsigned int)scratch;   // per-point width scale (or 0)

    double invCount = (double)(FLOAT_80613908 /
        (float)((double)CONCAT44(0x43300000, count ^ 0x80000000) - DOUBLE_80613900));
    double alphaBase = (double)(float)((double)CONCAT44(0x43300000, colorPacked >> 0x18) - DOUBLE_80613920);
    unsigned int rgb = colorPacked & 0xffffff;

    FUN_80172a4c(2, 0x92, 0, (short)(count << 1), fmt);

    double lenFloor = (double)FLOAT_80613910;
    double rsqrtLo = (double)FLOAT_80613914;
    float t = FLOAT_80613908;   // ramp position (1.0 -> 0.0)
    int vByte = 0;
    int aByte = 0;

    for (int i = 0; (int)count > i; ++i) {
        double ramp = (double)t;

        // per-point width scale
        double widthScale = alphaBase;
        if (alphaBuf != 0) {
            widthScale = (double)(float)(alphaBase * (double)*(float*)(alphaBuf + aByte));
        }
        // taper-mode alpha
        unsigned int colHi;
        if (taperMode == 3) {
            colHi = rgb | (unsigned int)(int)(widthScale *
                (double)(float)((double)FLOAT_80613908 -
                    ABS((double)(float)((double)FLOAT_8061390c * ramp - (double)FLOAT_80613908)))) << 0x18;
        } else if (taperMode < 3) {
            if (taperMode == 1) {
                colHi = rgb | (unsigned int)(int)widthScale << 0x18;
            } else if (taperMode < 1) {
                colHi = rgb | (unsigned int)(int)(widthScale * ramp) << 0x18;
            } else {
                colHi = rgb | (unsigned int)(int)(widthScale *
                    (double)(float)((double)FLOAT_80613908 - ramp)) << 0x18;
            }
        } else if (taperMode < 5) {
            colHi = rgb | (unsigned int)(int)(widthScale *
                ABS((double)(float)((double)FLOAT_8061390c * ramp - (double)FLOAT_80613908))) << 0x18;
        } else {
            colHi = rgb | (unsigned int)(int)(widthScale * ramp) << 0x18;
        }

        // tangent via central difference over the polyline
        float* pv = (float*)(vbuf + vByte);
        float tx, ty, tz;
        if (i < 1) {
            tx = pv[3] - pv[0]; ty = pv[4] - pv[1]; tz = pv[5] - pv[2];
        } else {
            tx = pv[0] - pv[-3]; ty = pv[1] - pv[-2]; tz = pv[2] - pv[-1];
        }
        float fx, fy, fz;
        if (i < (int)(count - 1)) {
            fx = pv[3] - pv[0]; fy = pv[4] - pv[1]; fz = pv[5] - pv[2];
        } else {
            fx = pv[0] - pv[-3]; fy = pv[1] - pv[-2]; fz = pv[2] - pv[-1];
        }

        // blend the two tangents (length-weighted) into a single direction
        double len2 = (double)(float)(tz * tz + (double)(float)(tx * tx + (double)(float)(ty * ty)));
        double dx = (double)tx, dy = (double)ty, dz = (double)tz;
        if ((lenFloor <= len2) &&
            ((len2 = math_sqrt((double)(float)((double)(float)(fz * fz +
                (double)(float)(fx * fx + (double)(float)(fy * fy))) / len2))),
             dx = (double)fx, dy = (double)fy, dz = (double)fz,
             (double)FLOAT_806138f8 < len2)) {
            double inv = (double)(float)((double)FLOAT_80613908 / len2);
            dz = (double)(float)(fz * inv + (double)(float)(tz * len2));
            dy = (double)(float)(fy * inv + (double)(float)(ty * len2));
            dx = (double)(float)(fx * inv + (double)(float)(tx * len2));
        }

        // normalise the tangent (fast rsqrt with one Newton step)
        double tl2 = (double)(float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy)));
        if (rsqrtLo < tl2) {
            double seed = 1.0 / SQRT(tl2);
            float r = (float)(seed * -(double)((float)((double)FLOAT_80613918 * tl2) *
                (float)(seed * seed) - FLOAT_8061391c));
            double refine = (double)(FLOAT_80613908 * r *
                -((float)((double)FLOAT_80613918 * tl2) * r * r - FLOAT_8061391c));
            dz = (double)(float)(dz * refine);
            dy = (double)(float)(dy * refine);
            dx = (double)(float)(dx * refine);
        }

        // side = tangent x camera-forward, normalised, scaled by width
        float sx = (float)(dz * (double)*(float*)(DAT_80611d54 + 0x334) -
                          (double)(float)(dx * (double)*(float*)(DAT_80611d54 + 0x33c)));
        float sy = (float)(dy * (double)*(float*)(DAT_80611d54 + 0x33c) -
                          (double)(float)(dz * (double)*(float*)(DAT_80611d54 + 0x338)));
        float sz = (float)(dx * (double)*(float*)(DAT_80611d54 + 0x338) -
                          (double)(float)(dy * (double)*(float*)(DAT_80611d54 + 0x334)));
        double sl2 = (double)(sz * sz + sy * sy + sx * sx);
        if (lenFloor < sl2) {
            double seed = 1.0 / SQRT(sl2);
            float r = (float)(seed * -(double)((float)((double)FLOAT_80613918 * sl2) *
                (float)(seed * seed) - FLOAT_8061391c));
            r = (float)(widthScale * (double)(r *
                -((float)((double)FLOAT_80613918 * sl2) * r * r - FLOAT_8061391c)));
            sy = sy * r; sx = sx * r; sz = sz * r;
        }

        // emit the two strip vertices (position, swizzled colour, texcoord)
        float* pos = (float*)(vbuf + vByte);
        unsigned int col = colHi << 0x18 | (colHi & 0xff00) << 8 |
                           colHi >> 8 & 0xff00 | colHi >> 0x18;
        GX_FIFO_F32 = sy + pos[0];
        GX_FIFO_F32 = sx + pos[1];
        GX_FIFO_F32 = sz + pos[2];
        GX_FIFO_U32 = col;
        GX_FIFO_F32 = FLOAT_806138f8;
        GX_FIFO_F32 = FLOAT_806138f8;
        GX_FIFO_F32 = pos[0] - sy;
        GX_FIFO_F32 = pos[1] - sx;
        GX_FIFO_F32 = pos[2] - sz;
        GX_FIFO_U32 = col;
        GX_FIFO_F32 = FLOAT_806138f8;
        GX_FIFO_F32 = FLOAT_80613908;

        t = (float)(ramp - invCount);
        vByte += 0xc;
        aByte += 4;
    }

    FUN_80172c54();
    FUN_80382718();
}

// line_gradient_clipped_draw (0x8001d030)
// Draw a horizontal line span (from x0=a to x1) at row y, clipped to [clipLo,
// clipHi], with an alpha gradient interpolated over the clip band. The span is
// split into a solid part (FUN_801708b4) and a faded part (FUN_80170b10), with
// FUN_803825c0 setting the per-endpoint alpha derived from alphaByte.
void line_gradient_clipped_draw(unsigned long long ctx, double a, double y,
                                double x1, double clipLo, double clipHi,
                                double bandLo, double bandHi,
                                unsigned int p9, unsigned int p10,
                                unsigned int col0, unsigned int col1,
                                unsigned int alphaByte)
{
    (void)p9;
    (void)p10;

    unsigned long long saved = __save_gpr_25();
    unsigned int cHi = (unsigned int)(saved >> 0x20);
    unsigned int cLo = (unsigned int)saved;
    unsigned int alpha = alphaByte & 0xff;

    if (clipLo <= a) {
        if (x1 < bandLo) {
            FUN_801708b4(0.0, a, y, x1, cHi, cLo, col0, col1);
        } else if (a < bandHi) {
            double band = (double)(float)(bandHi - bandLo);
            double startX = (bandLo < a) ? a : bandLo;
            double endX = (x1 < bandHi) ? x1 : bandHi;
            unsigned int cSpan = col1 - cLo ^ 0x80000000;
            double c0 = (double)(((float)(startX - a) / (float)(x1 - a)) *
                (float)((double)CONCAT44(0x43300000, cSpan) - DOUBLE_80613900) +
                (float)((double)CONCAT44(0x43300000, cLo ^ 0x80000000) - DOUBLE_80613900));
            double c1 = (double)(((float)(endX - a) / (float)(x1 - a)) *
                (float)((double)CONCAT44(0x43300000, cSpan) - DOUBLE_80613900) +
                (float)((double)CONCAT44(0x43300000, cLo ^ 0x80000000) - DOUBLE_80613900));
            if ((double)FLOAT_806138f8 < band) {
                FUN_803825c0((double)((float)((double)CONCAT44(0x43300000, alpha) - DOUBLE_80613920) *
                    (FLOAT_80613908 - (float)((double)(float)(startX - bandLo) / band))));
                FUN_803825c0((double)((float)((double)CONCAT44(0x43300000, alpha) - DOUBLE_80613920) *
                    (FLOAT_80613908 - (float)((double)(float)(endX - bandLo) / band))));
                FUN_80170b10(0.0, startX, y, endX, cHi, (int)c0, col0, (int)c1);
            }
            if (a < startX) {
                FUN_801708b4(0.0, a, y, startX, cHi, cLo, col0, (int)c0);
            }
        }
    } else if (clipHi <= x1) {
        double band = (double)(float)(clipLo - clipHi);
        double startX = (clipHi < a) ? a : clipHi;
        double endX = (x1 < clipLo) ? clipLo : x1;
        (void)endX;
        double useHi = (x1 < clipLo) ? clipLo : x1;
        unsigned int cSpan = col1 - cLo ^ 0x80000000;
        double c1 = (double)(((float)(startX - a) / (float)(x1 - a)) *
            (float)((double)CONCAT44(0x43300000, cSpan) - DOUBLE_80613900) +
            (float)((double)CONCAT44(0x43300000, cLo ^ 0x80000000) - DOUBLE_80613900));
        double c0 = (double)(((float)(useHi - a) / (float)(x1 - a)) *
            (float)((double)CONCAT44(0x43300000, cSpan) - DOUBLE_80613900) +
            (float)((double)CONCAT44(0x43300000, cLo ^ 0x80000000) - DOUBLE_80613900));
        if ((double)FLOAT_806138f8 < band) {
            FUN_803825c0((double)((float)((double)CONCAT44(0x43300000, alpha) - DOUBLE_80613920) *
                (float)((double)(float)(startX - clipHi) / band)));
            FUN_803825c0((double)((float)((double)CONCAT44(0x43300000, alpha) - DOUBLE_80613920) *
                (float)((double)(float)(useHi - clipHi) / band)));
            FUN_80170b10(0.0, startX, y, useHi, cHi, (int)c1, col0, (int)c0);
        }
        if (useHi < x1) {
            FUN_801708b4(0.0, useHi, y, x1, cHi, (int)c0, col0, col1);
        }
    }
    (void)ctx;
    __restore_gpr_25();
}

// debug_draw_flush_a (0x8001d4b4)
void debug_draw_flush_a(void)
{
    FUN_8017c658();
}

// debug_draw_flush_b (0x8001d4d4)
void debug_draw_flush_b(void)
{
    FUN_8017c670();
}

// render2d_list_reset (0x8001d4f4)
// Clear the 2D render list: zero the first slot of each of the 199 entries in
// the 0x40-byte-stride table at 0x804cdc38, then reset the render-state globals.
void render2d_list_reset(void)
{
    int i = 199;
    while (true) {
        i = i + -1;
        if (i < 0) break;
        *(unsigned int*)(i * 0x40 + (int)0x804cdc38) = 0;
    }
    DAT_80611750 = 0;
    DAT_80611758 = 0;
    DAT_80611754 = 0;
    DAT_8061175c = 0;
    FLOAT_80611764 = FLOAT_80613928;
    FLOAT_80611768 = FLOAT_8061392c;
    DAT_80611760 = 0;
}

// =============================================================================
// Batch 390 additions (subsystem:graphics) - viewport per-frame commit wrappers
// Address: 0x8001c6a4 | Name: viewport_commit_frame  | Date: 2026-09-05
// Address: 0x8001c6c4 | Name: viewport_commit_frame_stub | Date: 2026-09-05
// =============================================================================

// viewport_commit_frame (0x8001c6a4)  [FUN_8001c6a4]
// Per-frame hook (called from outer_game_loop): republish the default viewport
// bounds into the active GX render-state block. A thin forwarder over
// viewport_commit_to_gx so the frame loop has a stable entry point.
extern "C" void FUN_8001c6a4(void)
{
    viewport_commit_to_gx();
}

// viewport_commit_frame_stub (0x8001c6c4)  [FUN_8001c6c4]
// Empty companion hook (no-op on this build/region). Kept as a distinct entry
// point so the caller table layout matches the original.
extern "C" void FUN_8001c6c4(void)
{
}
