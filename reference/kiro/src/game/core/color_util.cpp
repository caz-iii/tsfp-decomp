// Address block: 0x80131de8 - 0x80132228 | Batch: batch_358 | Date: 2026-09-04
//
// Color-space utilities: RGB<->HSV conversion and packed-color helpers
// (HSV->packed RGBA, packed->RGB, and two packed-color lerps used by HUD /
// beam-trail rendering).
//
// Packed color layout is 0xAARRGGBB-style bytes; the low byte (alpha) is
// forced to 0x80 by hsv_to_color_packed and preserved by color_lerp_keep_lo.
//
// Non-matching decomp: behaviour preserved, types concretised.

#include "common.h"

// HSV<->RGB constants.
extern float FLOAT_80615b80;  // 0.0
extern float FLOAT_80615b84;  // 2.0  (sector offset for green max)
extern float FLOAT_80615b88;  // 4.0  (sector offset for blue max)
extern float FLOAT_80615b8c;  // 60.0 (degrees per sector)
extern float FLOAT_80615b90;  // 360.0
extern float FLOAT_80615b94;  // 1.0
extern float FLOAT_80615b98;  // 255.0 (float->byte scale)
extern float FLOAT_80615b9c;  // 1/254 or similar (byte->float scale)
extern double DOUBLE_80615ba0; // 0x4330000000000000 magic bias

// fmod-style reduction primitive.
extern double FUN_80390324(double x);

// Forward decls for intra-file callers.
void rgb_to_hsv(float *rgb, float *hsvOut);
void hsv_to_rgb(float *hsv, float *rgbOut);

// ---------------------------------------------------------------------------
// rgb_to_hsv().  Address: 0x80131de8
// rgb[0..2] -> hsvOut: [0]=hue (0..1), [1]=saturation, [2]=value(max channel).
// ---------------------------------------------------------------------------
void rgb_to_hsv(float *rgb, float *hsvOut)
{
    float r = rgb[0];
    float g = rgb[1];
    float b = rgb[2];

    float maxC = r;
    if (r < g) maxC = g;
    if (maxC < b) maxC = b;

    float minC = r;
    if (g < r) minC = g;
    if (b < minC) minC = b;

    hsvOut[2] = maxC;                       // value
    if (FLOAT_80615b80 == maxC) {
        hsvOut[1] = FLOAT_80615b80;         // saturation
    } else {
        hsvOut[1] = (maxC - minC) / maxC;
    }

    if (FLOAT_80615b80 == hsvOut[1]) {
        hsvOut[0] = FLOAT_80615b80;         // hue undefined -> 0
        return;
    }

    float chroma = maxC - minC;
    if (maxC == r) {
        hsvOut[0] = (g - b) / chroma;
    } else if (maxC == g) {
        hsvOut[0] = FLOAT_80615b84 + (b - r) / chroma;
    } else if (maxC == b) {
        hsvOut[0] = FLOAT_80615b88 + (r - g) / chroma;
    }

    hsvOut[0] = hsvOut[0] * FLOAT_80615b8c; // sector -> degrees
    if (hsvOut[0] < FLOAT_80615b80) {
        hsvOut[0] = hsvOut[0] + FLOAT_80615b90; // wrap negative hue
    }
}

// ---------------------------------------------------------------------------
// hsv_to_rgb().  Address: 0x80131ef8
// hsv[0]=hue(deg), hsv[1]=sat, hsv[2]=value -> rgbOut[0..2].
// ---------------------------------------------------------------------------
void hsv_to_rgb(float *hsv, float *rgbOut)
{
    float hue = hsv[0];
    while (FLOAT_80615b90 <= hue) hue = hue - FLOAT_80615b90; // wrap into [0,360)

    double sectorF = (double)(hue / FLOAT_80615b8c);
    double floorD = FUN_80390324(sectorF);
    int sector = (int)floorD;

    float sat = hsv[1];
    float val = hsv[2];
    float frac = (float)(sectorF - (double)(float)floorD);

    float p = val * (FLOAT_80615b94 - sat);
    float q = val * -(sat * frac - FLOAT_80615b94);
    float ti = val * -(sat * (FLOAT_80615b94 - frac) - FLOAT_80615b94);

    if (sector == 3) {
        rgbOut[0] = p;   rgbOut[1] = q;   rgbOut[2] = hsv[2];
    } else if (sector < 3) {
        if (sector == 1) {
            rgbOut[0] = q;   rgbOut[1] = hsv[2]; rgbOut[2] = p;
        } else if (sector < 1) {
            if (-1 < sector) {
                rgbOut[0] = val; rgbOut[1] = ti;  rgbOut[2] = p;
            }
        } else { // sector == 2
            rgbOut[0] = p;   rgbOut[1] = hsv[2]; rgbOut[2] = ti;
        }
    } else if (sector == 5) {
        rgbOut[0] = val; rgbOut[1] = p;   rgbOut[2] = q;
    } else { // sector == 4
        rgbOut[0] = ti;  rgbOut[1] = p;   rgbOut[2] = hsv[2];
    }
}

// ---------------------------------------------------------------------------
// hsv_to_color_packed().  Address: 0x80132044
// Converts an HSV float-triple to RGB, scales to bytes, and packs into a
// 0xRRGGBB value with the low byte (alpha) forced to 0x80.
// ---------------------------------------------------------------------------
uint hsv_to_color_packed(float *hsv)
{
    float rgb[3];
    hsv_to_rgb(hsv, rgb);
    return ((int)(FLOAT_80615b98 * rgb[2]) & 0xff) << 8
         | (int)(FLOAT_80615b98 * rgb[0]) << 0x18
         | ((int)(FLOAT_80615b98 * rgb[1]) & 0xff) << 0x10
         | 0x80;
}

// ---------------------------------------------------------------------------
// color_to_rgb_via_hsv().  Address: 0x801320bc
// Unpacks a packed color's RGB bytes to floats, and (via rgb_to_hsv) writes
// the resulting HSV-derived RGB to rgbOut.
// ---------------------------------------------------------------------------
void color_to_rgb_via_hsv(uint packed, float *rgbOut)
{
    uint rByte = packed >> 0x17 & 0xfe;
    uint gByte = packed >> 0x0f & 0xfe;
    uint bByte = packed >> 0x07 & 0xfe;

    float hsv[3];
    hsv[0] = (float)((double)CONCAT44(0x43300000, rByte) - DOUBLE_80615ba0) * FLOAT_80615b9c;
    hsv[1] = (float)((double)CONCAT44(0x43300000, gByte) - DOUBLE_80615ba0) * FLOAT_80615b9c;
    hsv[2] = (float)((double)CONCAT44(0x43300000, bByte) - DOUBLE_80615ba0) * FLOAT_80615b9c;

    rgb_to_hsv(hsv, rgbOut);
}

// ---------------------------------------------------------------------------
// color_lerp_keep_lo().  Address: 0x80132140
// Linearly interpolates two packed colors (a at weight t, b at 1-t) on the
// R/G/B bytes while preserving color a's low byte (alpha) unchanged.
// ---------------------------------------------------------------------------
uint color_lerp_keep_lo(double t, uint a, uint b)
{
    float u = (float)((double)FLOAT_80615b94 - t);
    return (a & 0xff)
         | ((int)((double)(float)((double)CONCAT44(0x43300000, a >> 8 & 0xff) - DOUBLE_80615ba0) * t
                + (double)((float)((double)CONCAT44(0x43300000, b >> 8 & 0xff) - DOUBLE_80615ba0) * u)) & 0xff) << 8
         | (int)((double)(float)((double)CONCAT44(0x43300000, a >> 0x18) - DOUBLE_80615ba0) * t
                + (double)((float)((double)CONCAT44(0x43300000, b >> 0x18) - DOUBLE_80615ba0) * u)) << 0x18
         | ((int)((double)(float)((double)CONCAT44(0x43300000, a >> 0x10 & 0xff) - DOUBLE_80615ba0) * t
                + (double)((float)((double)CONCAT44(0x43300000, b >> 0x10 & 0xff) - DOUBLE_80615ba0) * u)) & 0xff) << 0x10;
}

// ---------------------------------------------------------------------------
// color_lerp().  Address: 0x80132228
// Linearly interpolates two packed colors (a at weight t, b at 1-t) across all
// four bytes. Used by HUD draws and beam-trail rendering.
// ---------------------------------------------------------------------------
uint color_lerp(double t, uint a, uint b)
{
    float u = (float)((double)FLOAT_80615b94 - t);
    return (int)((double)(float)((double)CONCAT44(0x43300000, a & 0xff) - DOUBLE_80615ba0) * t
                + (double)((float)((double)CONCAT44(0x43300000, b & 0xff) - DOUBLE_80615ba0) * u)) & 0xff
         | ((int)((double)(float)((double)CONCAT44(0x43300000, a >> 8 & 0xff) - DOUBLE_80615ba0) * t
                + (double)((float)((double)CONCAT44(0x43300000, b >> 8 & 0xff) - DOUBLE_80615ba0) * u)) & 0xff) << 8
         | (int)((double)(float)((double)CONCAT44(0x43300000, a >> 0x18) - DOUBLE_80615ba0) * t
                + (double)((float)((double)CONCAT44(0x43300000, b >> 0x18) - DOUBLE_80615ba0) * u)) << 0x18
         | ((int)((double)(float)((double)CONCAT44(0x43300000, a >> 0x10 & 0xff) - DOUBLE_80615ba0) * t
                + (double)((float)((double)CONCAT44(0x43300000, b >> 0x10 & 0xff) - DOUBLE_80615ba0) * u)) & 0xff) << 0x10;
}
