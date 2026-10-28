// Address block: 0x80132348 - 0x8013248c | Batch: batch_359 | Date: 2026-09-04
//
// Packed-color arithmetic helpers: scale the alpha (low) byte, scale the RGB
// bytes, and saturating add of two packed colors. Widely used by render code.
//
// Packed layout: byte3=R (bits 24-31), byte2=G (16-23), byte1=B (8-15),
// byte0=alpha (0-7). Non-matching decomp; behaviour preserved.

#include "common.h"

extern float  FLOAT_80615b80;  // 0.0
extern float  FLOAT_80615b94;  // 1.0
extern double DOUBLE_80615ba0; // 0x4330000000000000 magic bias

// Float -> clamped byte [0,255].
extern int FUN_803825c0(double x);

// ---------------------------------------------------------------------------
// color_scale_lo().  Address: 0x80132348
// Scales the low byte (alpha) of a packed color by factor (clamped to [0,1]),
// leaving R/G/B unchanged.
// ---------------------------------------------------------------------------
uint color_scale_lo(double factor, uint color)
{
    double f = (double)FLOAT_80615b94;
    if ((factor <= f) && (f = factor, factor < (double)FLOAT_80615b80)) {
        f = (double)FLOAT_80615b80;
    }
    uint scaled = (uint)FUN_803825c0((double)(float)((double)(float)((double)CONCAT44(
                        0x43300000, color & 0xff) - DOUBLE_80615ba0) * f));
    return (color & 0xffffff00) | scaled;
}

// ---------------------------------------------------------------------------
// color_scale_rgb().  Address: 0x801323c0
// Scales the R/G/B bytes of a packed color by factor, leaving alpha unchanged.
// ---------------------------------------------------------------------------
uint color_scale_rgb(double factor, uint color)
{
    uint g = (uint)FUN_803825c0((double)(float)(factor *
                (double)(float)((double)CONCAT44(0x43300000, color >> 0x10 & 0xff) - DOUBLE_80615ba0)));
    int r = FUN_803825c0((double)(float)(factor *
                (double)(float)((double)CONCAT44(0x43300000, color >> 0x18) - DOUBLE_80615ba0)));
    uint b = (uint)FUN_803825c0((double)(float)(factor *
                (double)(float)((double)CONCAT44(0x43300000, color >> 8 & 0xff) - DOUBLE_80615ba0)));
    return (color & 0xff) | (b & 0xff) << 8 | r << 0x18 | (g & 0xff) << 0x10;
}

// ---------------------------------------------------------------------------
// color_add_saturate().  Address: 0x8013248c
// Per-channel additive blend of two packed colors, each channel clamped to 255.
// ---------------------------------------------------------------------------
uint color_add_saturate(uint a, uint b)
{
    uint rSum = (a >> 0x18) + (b >> 0x18);
    uint r = (rSum < 0x100) ? rSum : 0xff;

    uint gSum = (a >> 0x10 & 0xff) + (b >> 0x10 & 0xff);
    uint g = (gSum < 0x100) ? gSum : 0xff;

    uint bSum = (a >> 8 & 0xff) + (b >> 8 & 0xff);
    uint bl = (bSum < 0x100) ? bSum : 0xff;

    uint aSum = (a & 0xff) + (b & 0xff);
    uint al = (aSum < 0x100) ? aSum : 0xff;

    return (al & 0xff) | (bl & 0xff) << 8 | r << 0x18 | (g & 0xff) << 0x10;
}
