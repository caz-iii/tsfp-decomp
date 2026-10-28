// TSFP GameCube MTX 3x4 matrix library (from main.dol, PPC/Gekko).
//
// Address: 0x803a10e8 | Name: mtx_identity | Date: 2026-09-15
// Address: 0x803a1114 | Name: mtx_copy     | Date: 2026-09-15
// Address: 0x803a1148 | Name: mtx_concat   | Date: 2026-09-15
// Address: 0x803a1214 | Name: mtx_trans    | Date: 2026-09-15
// Address: 0x803a1248 | Name: mtx_scale    | Date: 2026-09-15
//
// The GameCube SDK stores a transform as a 3x4 (3 rows of 4) row-major matrix of
// 32 floats, i.e. the top three rows of a 4x4 affine matrix; the implicit bottom
// row is (0,0,0,1). Element layout (index into the float[12]):
//     [ 0  1  2  3 ]
//     [ 4  5  6  7 ]
//     [ 8  9 10 11 ]
// Translation lives in column 3 (indices 3,7,11); the 3x3 rotation/scale is the
// left block. These are the standard MTXIdentity/MTXCopy/MTXConcat/MTXTrans/
// MTXScale routines.
//
// NON-MATCHING: the identity constants (1.0f/0.0f) that the original reads from
// FLOAT_8061bfe8 / FLOAT_8061bfec / DAT_806113f8 / DAT_806113fc are written as
// literals here; the arithmetic and element ordering are transcribed verbatim.

#include "common.h"

extern "C" {

// MTXIdentity: set m to the 3x4 identity (diagonal 1.0, everything else 0.0).
void mtx_identity(float *m)
{
    m[0]  = 1.0f; m[1]  = 0.0f; m[2]  = 0.0f; m[3]  = 0.0f;
    m[4]  = 0.0f; m[5]  = 1.0f; m[6]  = 0.0f; m[7]  = 0.0f;
    m[8]  = 0.0f; m[9]  = 0.0f; m[10] = 1.0f; m[11] = 0.0f;
}

// MTXCopy: copy the 12 floats of a 3x4 matrix from src to dst.
void mtx_copy(float *src, float *dst)
{
    dst[0]  = src[0];  dst[1]  = src[1];
    dst[2]  = src[2];  dst[3]  = src[3];
    dst[4]  = src[4];  dst[5]  = src[5];
    dst[6]  = src[6];  dst[7]  = src[7];
    dst[8]  = src[8];  dst[9]  = src[9];
    dst[10] = src[10]; dst[11] = src[11];
}

// MTXConcat: out = a * b (both 3x4, treated as 4x4 with implicit affine bottom
// row (0,0,0,1)). Column 3 of the result also picks up a's translation via the
// implicit 1.0 in b's fourth row (the DAT_806113f8=0.0 / DAT_806113fc=1.0 terms).
// out must not alias a or b for correct results (matches the original).
void mtx_concat(float *a, float *b, float *out)
{
    const float row3_0 = 0.0f;   // DAT_806113f8
    const float row3_1 = 1.0f;   // DAT_806113fc

    float a00 = a[0],  a01 = a[1],  a02 = a[2],  a03 = a[3];
    float a10 = a[4],  a11 = a[5],  a12 = a[6],  a13 = a[7];
    float a20 = a[8],  a21 = a[9],  a22 = a[10], a23 = a[11];

    float b00 = b[0],  b01 = b[1],  b02 = b[2],  b03 = b[3];
    float b10 = b[4],  b11 = b[5],  b12 = b[6],  b13 = b[7];
    float b20 = b[8],  b21 = b[9],  b22 = b[10], b23 = b[11];

    // Element-by-element, reproducing the original ordering:
    out[0]  = a00 * b00 + a01 * b10 + a02 * b20;
    out[1]  = a00 * b01 + a01 * b11 + a02 * b21;
    out[4]  = a10 * b00 + a11 * b10 + a12 * b20;
    out[5]  = a10 * b01 + a11 * b11 + a12 * b21;
    out[2]  = a00 * b02 + a01 * b12 + a02 * b22 + a03 * row3_0;
    out[3]  = a00 * b03 + a01 * b13 + a02 * b23 + a03 * row3_1;
    out[6]  = a10 * b02 + a11 * b12 + a12 * b22 + a13 * row3_0;
    out[7]  = a10 * b03 + a11 * b13 + a12 * b23 + a13 * row3_1;
    out[8]  = a20 * b00 + a21 * b10 + a22 * b20;
    out[9]  = a20 * b01 + a21 * b11 + a22 * b21;
    out[10] = a20 * b02 + a21 * b12 + a22 * b22 + a23 * row3_0;
    out[11] = a20 * b03 + a21 * b13 + a22 * b23 + a23 * row3_1;
}

// MTXTrans: build a 3x4 translation matrix (identity rotation, translation xyz).
void mtx_trans(float x, float y, float z, float *m)
{
    m[0]  = 1.0f; m[1]  = 0.0f; m[2]  = 0.0f; m[3]  = x;
    m[4]  = 0.0f; m[5]  = 1.0f; m[6]  = 0.0f; m[7]  = y;
    m[8]  = 0.0f; m[9]  = 0.0f; m[10] = 1.0f; m[11] = z;
}

// MTXScale: build a 3x4 scale matrix (diagonal xyz, no translation).
void mtx_scale(float x, float y, float z, float *m)
{
    m[0]  = x;    m[1]  = 0.0f; m[2]  = 0.0f; m[3]  = 0.0f;
    m[4]  = 0.0f; m[5]  = y;    m[6]  = 0.0f; m[7]  = 0.0f;
    m[8]  = 0.0f; m[9]  = 0.0f; m[10] = z;    m[11] = 0.0f;
}

// ---------------------------------------------------------------------------
// batch_1115: MTXScaleApply + projection builders + 4x4 copy/concat.
//
// Address: 0x803a1270 | Name: mtx_scale_apply   | Date: 2026-09-15
// Address: 0x803a12c8 | Name: mtx_light_frustum | Date: 2026-09-15
// Address: 0x803a135c | Name: mtx_perspective   | Date: 2026-09-15
// Address: 0x803a142c | Name: mtx_ortho         | Date: 2026-09-15
// Address: 0x803a14c4 | Name: mtx44_copy        | Date: 2026-09-15
// Address: 0x803a1508 | Name: mtx44_concat      | Date: 2026-09-15
//
// NON-MATCHING: the projection builders read their coefficients (1.0/2.0/0.0/
// -1.0, 0.5, pi/180) from FLOAT_8061bff0..8061c00c; written as literals here.
// mtx_perspective uses math_tanf for cot(fovY/2). Element ordering transcribed
// verbatim.
// ---------------------------------------------------------------------------

// math_tanf is defined elsewhere (msl_math_trig); generated_decls.h is not
// pulled in via common.h, so declare it per-file.
extern float math_tanf(double x);

// MTXScaleApply: scale the three rows of 3x4 matrix src by (x,y,z) into dst.
void mtx_scale_apply(float x, float y, float z, float *src, float *dst)
{
    dst[0]  = src[0]  * x; dst[1]  = src[1]  * x;
    dst[2]  = src[2]  * x; dst[3]  = src[3]  * x;
    dst[4]  = src[4]  * y; dst[5]  = src[5]  * y;
    dst[6]  = src[6]  * y; dst[7]  = src[7]  * y;
    dst[8]  = src[8]  * z; dst[9]  = src[9]  * z;
    dst[10] = src[10] * z; dst[11] = src[11] * z;
}

// MTXLightFrustum: build a 3x4 projected-texture matrix from frustum extents
// (t,b,l,r,n) with an output scale (scaleS,scaleT) and bias (transS,transT).
// Produces the texture-coordinate generation matrix used for projected lights.
void mtx_light_frustum(float t, float b, float l, float r, float n,
                       float scaleS, float scaleT, float transS, float transT,
                       float *m)
{
    float invRL = 1.0f / (r - l);
    float invTB = 1.0f / (t - b);

    m[0]  = scaleS * ((2.0f * n) * invRL);
    m[1]  = 0.0f;
    m[2]  = (scaleS * (invRL * (r + l))) - transS;
    m[3]  = 0.0f;
    m[4]  = 0.0f;
    m[5]  = scaleT * ((2.0f * n) * invTB);
    m[6]  = (scaleT * (invTB * (t + b))) - transT;
    m[7]  = 0.0f;
    m[8]  = 0.0f;
    m[9]  = 0.0f;
    m[10] = -1.0f;
    m[11] = 0.0f;
}

// MTXPerspective: build a 4x4 perspective matrix. fovY in degrees.
void mtx_perspective(float fovY, float aspect, float n, float f, float *m)
{
    float cot = 1.0f / math_tanf((double)(0.5f * fovY * 0.01745329238f)); // pi/180
    float invNF = 1.0f / (f - n);

    m[0]  = cot / aspect;
    m[1]  = 0.0f;
    m[2]  = 0.0f;
    m[3]  = 0.0f;
    m[4]  = 0.0f;
    m[5]  = cot;
    m[6]  = 0.0f;
    m[7]  = 0.0f;
    m[8]  = 0.0f;
    m[9]  = 0.0f;
    m[10] = -n * invNF;
    m[11] = invNF * -(f * n);
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = -1.0f;
    m[15] = 0.0f;
}

// MTXOrtho: build a 4x4 orthographic matrix from (t,b,l,r,n,f).
void mtx_ortho(float t, float b, float l, float r, float n, float f, float *m)
{
    float invRL = 1.0f / (r - l);
    float invTB = 1.0f / (t - b);
    float invNF = 1.0f / (f - n);

    m[0]  = 2.0f * invRL;
    m[1]  = 0.0f;
    m[2]  = 0.0f;
    m[3]  = invRL * -(r + l);
    m[4]  = 0.0f;
    m[5]  = 2.0f * invTB;
    m[6]  = 0.0f;
    m[7]  = invTB * -(t + b);
    m[8]  = 0.0f;
    m[9]  = 0.0f;
    m[10] = -1.0f * invNF;
    m[11] = -f * invNF;
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;
}

// MTX44Copy: copy a full 4x4 (16-float) matrix from src to dst.
void mtx44_copy(float *src, float *dst)
{
    for (int k = 0; k < 16; k++) dst[k] = src[k];
}

// MTX44Concat: full 4x4 matrix multiply out = a * b (row-major).
void mtx44_concat(float *a, float *b, float *out)
{
    float a00 = a[0],  a01 = a[1],  a02 = a[2],  a03 = a[3];
    float a10 = a[4],  a11 = a[5],  a12 = a[6],  a13 = a[7];
    float a20 = a[8],  a21 = a[9],  a22 = a[10], a23 = a[11];
    float a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];

    float b00 = b[0],  b01 = b[1],  b02 = b[2],  b03 = b[3];
    float b10 = b[4],  b11 = b[5],  b12 = b[6],  b13 = b[7];
    float b20 = b[8],  b21 = b[9],  b22 = b[10], b23 = b[11];
    float b30 = b[12], b31 = b[13], b32 = b[14], b33 = b[15];

    out[0]  = a00 * b00 + a01 * b10 + a02 * b20 + a03 * b30;
    out[1]  = a00 * b01 + a01 * b11 + a02 * b21 + a03 * b31;
    out[2]  = a00 * b02 + a01 * b12 + a02 * b22 + a03 * b32;
    out[3]  = a00 * b03 + a01 * b13 + a02 * b23 + a03 * b33;
    out[4]  = a10 * b00 + a11 * b10 + a12 * b20 + a13 * b30;
    out[5]  = a10 * b01 + a11 * b11 + a12 * b21 + a13 * b31;
    out[6]  = a10 * b02 + a11 * b12 + a12 * b22 + a13 * b32;
    out[7]  = a10 * b03 + a11 * b13 + a12 * b23 + a13 * b33;
    out[8]  = a20 * b00 + a21 * b10 + a22 * b20 + a23 * b30;
    out[9]  = a20 * b01 + a21 * b11 + a22 * b21 + a23 * b31;
    out[10] = a20 * b02 + a21 * b12 + a22 * b22 + a23 * b32;
    out[11] = a20 * b03 + a21 * b13 + a22 * b23 + a23 * b33;
    out[12] = a30 * b00 + a31 * b10 + a32 * b20 + a33 * b30;
    out[13] = a30 * b01 + a31 * b11 + a32 * b21 + a33 * b31;
    out[14] = a30 * b02 + a31 * b12 + a32 * b22 + a33 * b32;
    out[15] = a30 * b03 + a31 * b13 + a32 * b23 + a33 * b33;
}

} // extern "C"
