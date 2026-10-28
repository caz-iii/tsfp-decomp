// Address block: 0x8013199c - 0x80131d4c | Batch: batch_358 | Date: 2026-09-04
//
// Spline / Bezier evaluators (continuation of the core math library):
//   - Catmull-Rom cubic interpolation of an angle-triple (with per-axis
//     angle wrapping so it interpolates the shortest way around a circle)
//   - Catmull-Rom cubic interpolation of a scalar
//   - Cubic Bezier evaluation of a vec2 and a vec3 (Bernstein basis)
//
// Non-matching decomp: behaviour preserved, types concretised.

#include "common.h"

// Catmull-Rom basis coefficients (shared with math_trig.cpp spline_eval_pos).
extern float FLOAT_80615b40;  // 0.5 scale
extern float FLOAT_80615b44;
extern float FLOAT_80615b48;  // 3.0
extern float FLOAT_80615b4c;
extern float FLOAT_80615b50;
extern float FLOAT_80615b54;
extern float FLOAT_80615b58;  // 1.0

// Angle-wrap constants for spline_eval_angles.
extern float FLOAT_80615b6c;  // full turn (360)
extern float FLOAT_80615b70;  // -half turn (lower wrap bound)
extern float FLOAT_80615b74;  //  half turn (upper wrap bound)
extern float FLOAT_80615b78;  // wrap bias (180)

// fmod-style reduction primitive.
extern double FUN_80390324(double x);

// ---------------------------------------------------------------------------
// spline_eval_angles().  Address: 0x8013199c
// Catmull-Rom interpolation of an angle-triple at parameter t. Before blending
// it unwraps each successive control angle to the nearest equivalent (so the
// path takes the shortest way around), then re-wraps the result into range.
// p1..p4 -> param_3..param_6 (each a 3-angle vector). Result -> out.
// ---------------------------------------------------------------------------
void spline_eval_angles(double t, float *out, float *p1, float *p2, float *p3, float *p4)
{
    double t2 = (double)(float)(t * t);
    double t3 = (double)(float)(t2 * t);

    // Local control-point copy, laid out to match the decompiler's stack frame:
    //   cp[9..11] = p1, cp[6..8] = p2, cp[3..5] = p3, cp[0..2] = p4.
    float cp[12];
    cp[9]  = p1[0]; cp[10] = p1[1]; cp[11] = p1[2];
    cp[6]  = p2[0]; cp[7]  = p2[1]; cp[8]  = p2[2];
    cp[3]  = p3[0]; cp[4]  = p3[1]; cp[5]  = p3[2];
    cp[0]  = p4[0]; cp[1]  = p4[1]; cp[2]  = p4[2];

    // For each component (x,y,z at cp offsets 0,1,2 -> byte offsets 0,4,8),
    // unwrap each successive control angle toward the previous one so the
    // interpolation always takes the shortest arc. Anchor is p1 (cp[9..11]),
    // walking p2 (cp[6..8]) -> p3 (cp[3..5]) -> p4 (cp[0..2]).
    for (int comp = 0; comp < 3; ++comp) {
        float anchor = cp[9 + comp];
        float d;
        for (d = cp[6 + comp] - anchor; d <= FLOAT_80615b70; d += FLOAT_80615b6c) {}
        for (; FLOAT_80615b74 < d; d -= FLOAT_80615b6c) {}
        cp[6 + comp] = anchor + d;

        anchor = cp[6 + comp];
        for (d = cp[3 + comp] - anchor; d <= FLOAT_80615b70; d += FLOAT_80615b6c) {}
        for (; FLOAT_80615b74 < d; d -= FLOAT_80615b6c) {}
        cp[3 + comp] = anchor + d;

        anchor = cp[3 + comp];
        for (d = cp[0 + comp] - anchor; d <= FLOAT_80615b70; d += FLOAT_80615b6c) {}
        for (; FLOAT_80615b74 < d; d -= FLOAT_80615b6c) {}
        cp[0 + comp] = anchor + d;
    }

    // Catmull-Rom weights.
    double w1 = (double)(FLOAT_80615b40 * (float)((double)(float)((double)FLOAT_80615b44 * t2 + -t3) - t));
    double w2 = (double)(FLOAT_80615b40 *
                (float)((double)FLOAT_80615b44 +
                        (double)(float)((double)FLOAT_80615b48 * t3 -
                                        (double)(float)((double)FLOAT_80615b4c * t2))));
    double w4 = (double)(FLOAT_80615b40 * (float)(t3 - t2));
    double w3 = (double)(FLOAT_80615b40 *
                (float)(t + (double)(float)((double)FLOAT_80615b50 * t3 +
                                            (double)(float)((double)FLOAT_80615b54 * t2))));

    for (int comp = 0; comp < 3; ++comp) {
        double blended = (double)(float)(w4 * (double)cp[0 + comp] +
                         (double)(float)(w3 * (double)cp[3 + comp] +
                                        (double)(float)(w1 * (double)cp[9 + comp] +
                                                       (double)(float)(w2 * (double)cp[6 + comp]))));
        double m = FUN_80390324((double)((float)(blended - (double)FLOAT_80615b78) / FLOAT_80615b6c));
        out[comp] = -(float)((double)FLOAT_80615b6c * (double)(float)m - blended);
    }
}

// ---------------------------------------------------------------------------
// spline_eval_scalar().  Address: 0x80131c68
// Catmull-Rom cubic interpolation of a single scalar from 4 control values.
// ---------------------------------------------------------------------------
double spline_eval_scalar(double t, double v1, double v2, double v3, double v4)
{
    double t2 = (double)(float)(t * t);
    double t3 = (double)(float)(t2 * t);

    double w4 = (double)(FLOAT_80615b40 * (float)(t3 - t2));
    double w3 = (double)(FLOAT_80615b40 *
                (float)(t + (double)(float)((double)FLOAT_80615b50 * t3 +
                                            (double)(float)((double)FLOAT_80615b54 * t2))));
    double w1 = (double)(FLOAT_80615b40 *
                (float)((double)(float)((double)FLOAT_80615b44 * t2 + -t3) - t));
    double w2 = (double)(FLOAT_80615b40 *
                (float)((double)FLOAT_80615b44 +
                        (double)(float)((double)FLOAT_80615b48 * t3 -
                                        (double)(float)((double)FLOAT_80615b4c * t2))));

    return (double)(float)(w4 * v4 +
           (double)(float)(w3 * v3 +
                          (double)(float)(w1 * v1 + (double)(float)(w2 * v2))));
}

// ---------------------------------------------------------------------------
// bezier_eval_vec2().  Address: 0x80131cd4
// Cubic Bezier (Bernstein basis) evaluation of a 2D point at t.
// p0..p3 -> param_3..param_6, result -> out[0..1].
// ---------------------------------------------------------------------------
void bezier_eval_vec2(double t, float *out, float *p0, float *p1, float *p2, float *p3)
{
    float oneMinusT = (float)((double)FLOAT_80615b58 - t);
    float b2 = (float)((double)FLOAT_80615b48 * (double)(float)(t * t)) * oneMinusT;  // 3 t^2 (1-t)
    float b3 = (float)(t * (double)(float)(t * t));                                   // t^3
    float b1 = (float)((double)FLOAT_80615b48 * t) * oneMinusT * oneMinusT;           // 3 t (1-t)^2
    float b0 = oneMinusT * oneMinusT * oneMinusT;                                     // (1-t)^3

    out[0] = p3[0] * b3 + p2[0] * b2 + p0[0] * b0 + p1[0] * b1;
    out[1] = p3[1] * b3 + p2[1] * b2 + p0[1] * b0 + p1[1] * b1;
}

// ---------------------------------------------------------------------------
// bezier_eval_vec3().  Address: 0x80131d4c
// Cubic Bezier (Bernstein basis) evaluation of a 3D point at t.
// ---------------------------------------------------------------------------
void bezier_eval_vec3(double t, float *out, float *p0, float *p1, float *p2, float *p3)
{
    float oneMinusT = (float)((double)FLOAT_80615b58 - t);
    float b2 = (float)((double)FLOAT_80615b48 * (double)(float)(t * t)) * oneMinusT;
    float b3 = (float)(t * (double)(float)(t * t));
    float b1 = (float)((double)FLOAT_80615b48 * t) * oneMinusT * oneMinusT;
    float b0 = oneMinusT * oneMinusT * oneMinusT;

    out[0] = p3[0] * b3 + p2[0] * b2 + p0[0] * b0 + p1[0] * b1;
    out[1] = p3[1] * b3 + p2[1] * b2 + p0[1] * b0 + p1[1] * b1;
    out[2] = p3[2] * b3 + p2[2] * b2 + p0[2] * b0 + p1[2] * b1;
}
