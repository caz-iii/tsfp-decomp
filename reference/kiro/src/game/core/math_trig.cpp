// Address block: 0x801311d8 - 0x801319xx | Batch: batch_357 | Date: 2026-09-04
//
// Core math library: direction<->angle conversion, clamped acos/asin,
// fast sqrt, axis-angle vector rotation, angle wrapping, and cubic-spline
// (Catmull-Rom style) position/tangent evaluation for vec3.
//
// Used broadly by cameras, particles, shell casings, and gameplay AI.
//
// Non-matching decomp: behaviour preserved, register-pair returns modelled
// as parameters, types concretised. Trig/const magic reproduced verbatim.

#include "common.h"

// ---------------------------------------------------------------------------
// Per-file externs for raw Ghidra math primitives / constants.
// ---------------------------------------------------------------------------

// Trig primitives (double->double) in the math runtime.
extern double FUN_80390914(double x); // sin
extern double FUN_80390250(double x); // cos
extern double FUN_80390324(double x); // fmod-style reduction
extern double FUN_80390dac(double x); // acos primitive
extern double FUN_80390dd0(double x); // asin primitive
extern double math_atan2(double y, double x);

// Loads a working vec3 into a register pair (returns hi/lo pointers).
// Modelled here as returning the current transform's forward-vector pointer.
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void); // matching pop/cleanup of FUN_803826f0

// Quaternion helpers (named in IDB, not yet in shared decls).
extern void quat_from_axis_angle(double angle, float *quatOut, float *axisVec3);
extern void quat_rotate_vector(float *quat, float *vecIn, float *vecOut);

// Ghidra SQRT intrinsic (float/double square root).
static inline double SQRT(double x) { return __builtin_sqrt(x); }

// Math constants (IEEE bit patterns from the .rodata pool).
extern float  FLOAT_80615ad8;  // amplitude / 1.0
extern double DOUBLE_80615ae0; // 0x4330000000000000 magic bias
extern float  FLOAT_80615aec;  // 0.0
extern float  FLOAT_80615af0;  // pi
extern float  FLOAT_80615af4;  // 180.0 (deg conversion)
extern float  FLOAT_80615af8;  // epsilon for length test
extern float  FLOAT_80615ae8;  // epsilon for atan2 branch
extern float  FLOAT_80615afc;  // reduction threshold (poly)
extern float  FLOAT_80615b00;  // 1/(2pi)-style reduction scale
extern float  FLOAT_80615b04;  // poly segment bound
extern float  FLOAT_80615b08;  // poly coefficient c0
extern float  FLOAT_80615b0c;  // poly coefficient c1
extern float  FLOAT_80615b10;  // poly coefficient c2
extern float  FLOAT_80615b14;  // poly coefficient c3
extern float  FLOAT_80615b18;  // poly segment bound 2
extern float  FLOAT_80615b1c;  // poly bias
extern float  FLOAT_80615b20;  // -1.0 clamp lo
extern float  FLOAT_80615b24;  //  1.0 clamp hi / small bias
extern float  FLOAT_80615b28;  // rsqrt Newton coeff (a)
extern float  FLOAT_80615b2c;  // rsqrt Newton coeff (b)
extern float  FLOAT_80615b30;  // cross-product length epsilon
extern float  FLOAT_80615b34;  // angle wrap span (2pi)
extern float  FLOAT_80615b38;  // angle wrap bias (pi)
extern float  FLOAT_80615b40;  // spline 0.5 scale
extern float  FLOAT_80615b44;  // spline coeff
extern float  FLOAT_80615b48;  // spline coeff
extern float  FLOAT_80615b4c;  // spline coeff
extern float  FLOAT_80615b50;  // spline coeff
extern float  FLOAT_80615b54;  // spline coeff
extern float  FLOAT_80615b58;  // spline coeff
extern float  FLOAT_80615b5c;  // spline coeff
extern float  FLOAT_80615b60;  // spline coeff
extern float  FLOAT_80615b64;  // spline coeff
extern float  FLOAT_80615b68;  // spline coeff

// ---------------------------------------------------------------------------
// Forward decls for this file's own functions (cross-references below).
// ---------------------------------------------------------------------------
double math_sqrt(double x);
double math_acos(double x);

// ---------------------------------------------------------------------------
// math_sqrt().  Address: 0x80131538
// Fast sqrt: returns 0 for inputs <= epsilon, else Newton-refined via rsqrt.
// Heavily used across the engine.
// ---------------------------------------------------------------------------
double math_sqrt(double x)
{
    if (x <= (double)FLOAT_80615aec) {
        return (double)FLOAT_80615aec;
    }
    double inv = 1.0 / SQRT(x);
    float r = (float)(inv * -(double)((float)((double)FLOAT_80615b28 * x) *
                                      (float)(inv * inv) - FLOAT_80615b2c));
    return (double)(float)(x *
                (double)(r * -((float)((double)FLOAT_80615b28 * x) * r * r - FLOAT_80615b2c)));
}

// ---------------------------------------------------------------------------
// math_acos().  Address: 0x801314b0
// Clamps input to [-1, 1] then calls the acos primitive.
// ---------------------------------------------------------------------------
double math_acos(double x)
{
    double v = (double)FLOAT_80615b20;
    if ((v <= x) && (v = x, (double)FLOAT_80615b24 < x)) {
        v = (double)FLOAT_80615b24;
    }
    return FUN_80390dac(v);
}

// ---------------------------------------------------------------------------
// math_asin().  Address: 0x801314f4
// Clamps input to [-1, 1] then calls the asin primitive.
// ---------------------------------------------------------------------------
double math_asin(double x)
{
    double v = (double)FLOAT_80615b20;
    if ((v <= x) && (v = x, (double)FLOAT_80615b24 < x)) {
        v = (double)FLOAT_80615b24;
    }
    return FUN_80390dd0(v);
}

// ---------------------------------------------------------------------------
// math_poly_trig().  Address: 0x801313a4
// Range-reduced polynomial trig approximation (sin/tan style) over a segmented
// domain. Returns a double result from a double argument.
// ---------------------------------------------------------------------------
double math_poly_trig(double x)
{
    float sq = (float)(x * x);
    if (FLOAT_80615afc < sq) {
        x = -(double)(float)((double)FLOAT_80615ad8 *
                             (double)(float)((double)CONCAT44(0x43300000,
                                     (int)(FLOAT_80615b00 *
                                           (float)((double)CONCAT44(0x43300000,
                                                   (int)x ^ 0x80000000) - DOUBLE_80615ae0)) ^ 0x80000000)
                                     - DOUBLE_80615ae0)
                             - x);
        sq = (float)(x * x);
    }
    double result;
    if (FLOAT_80615b04 <= sq) {
        if (FLOAT_80615b18 <= sq) {
            sq = (float)(ABS(x) - (double)FLOAT_80615ad8) *
                 (float)(ABS(x) - (double)FLOAT_80615ad8);
            result = (double)(sq * (sq * (FLOAT_80615b14 * sq + FLOAT_80615b10) + FLOAT_80615b0c)
                              + FLOAT_80615b08);
        } else {
            sq = (float)(ABS(x) - (double)FLOAT_80615af4) *
                 (float)(ABS(x) - (double)FLOAT_80615af4);
            result = -(double)(sq * (sq * (FLOAT_80615b14 * sq + FLOAT_80615b10) + FLOAT_80615b0c)
                               - FLOAT_80615b1c);
        }
    } else {
        result = (double)(sq * (sq * (FLOAT_80615b14 * sq + FLOAT_80615b10) + FLOAT_80615b0c) +
                          FLOAT_80615b08);
    }
    return result;
}

// ---------------------------------------------------------------------------
// dir_to_angles().  Address: 0x801311d8
// Converts the current working direction vector to yaw (param_3[0]) and
// pitch (yawOut[0]) angles in the engine's degree-scaled units.
// Called by particle/shell-casing spawn to orient effects along a direction.
// ---------------------------------------------------------------------------
void dir_to_angles(int /*unused*/, int /*unused2*/, float *yawOut)
{
    unsigned long long pair = FUN_803826f0();
    float *vec = (float *)(uintptr_t)(pair >> 0x20);
    float *pitchOut = (float *)(uintptr_t)(pair & 0xffffffffu);

    if (yawOut != (float *)0) {
        if ((double)FLOAT_80615ae8 <= ABS((double)*vec)) {
            double a = math_atan2((double)*vec, (double)vec[2]);
            *yawOut = (FLOAT_80615af0 * (float)a) / FLOAT_80615af4;
        } else if (FLOAT_80615aec <= vec[2]) {
            *yawOut = FLOAT_80615aec;
        } else {
            *yawOut = FLOAT_80615af0;
        }
    }
    if (pitchOut != (float *)0) {
        double len = math_sqrt((double)(vec[2] * vec[2] + *vec * *vec + vec[1] * vec[1]));
        if ((double)FLOAT_80615af8 <= ABS(len)) {
            double a = math_acos((double)(float)((double)vec[1] / len));
            *pitchOut = (float)((double)FLOAT_80615af0 * a) / FLOAT_80615af4;
        } else {
            *pitchOut = FLOAT_80615aec;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// angles_to_dir().  Address: 0x801312d4
// Converts yaw (param_1) and pitch (param_2) angles back to a unit direction
// vector written to dirOut[0..2]. Used by camera aim.
// ---------------------------------------------------------------------------
void angles_to_dir(double yaw, double pitch, float *dirOut)
{
    double p = (double)((float)((double)FLOAT_80615af4 * pitch) / FLOAT_80615af0);
    double sinP = (double)(float)FUN_80390914(p);
    double cosP = (double)(float)FUN_80390250(p);

    double y = (double)((float)((double)FLOAT_80615af4 * yaw) / FLOAT_80615af0);
    double sinY = (double)(float)FUN_80390914(y);
    double cosY = (double)(float)FUN_80390250(y);

    dirOut[0] = (float)(cosY * sinP);
    dirOut[1] = (float)sinY;
    dirOut[2] = (float)(cosY * cosP);
}

// ---------------------------------------------------------------------------
// vec3_rotate_yaw_pitch().  Address: 0x8013157c
// Builds a perpendicular basis for the input vector, then rotates it by a yaw
// axis-angle quaternion followed by a pitch axis-angle quaternion.
// ---------------------------------------------------------------------------
void vec3_rotate_yaw_pitch(double yaw, double pitch, float *vec)
{
    float quat[4];
    float perp[3];   // local_38/34/30
    float work[3];   // local_2c/28/24

    work[0] = vec[0];
    work[1] = vec[1];
    work[2] = vec[2];

    float ax = work[0];
    float ay = work[1];
    float az = work[2];

    // Choose the least-dominant axis to build a stable perpendicular.
    if (ABS(work[1]) <= ABS(work[0])) {
        if (ABS(work[2]) <= ABS(work[1])) {
            az = work[2] + FLOAT_80615b24;
        } else {
            ay = work[1] + FLOAT_80615b24;
        }
    } else if (ABS(work[2]) <= ABS(work[0])) {
        az = work[2] + FLOAT_80615b24;
    } else {
        ax = work[0] + FLOAT_80615b24;
    }

    perp[0] = work[1] * az - work[2] * ay;
    perp[1] = work[2] * ax - work[0] * az;
    perp[2] = work[0] * ay - work[1] * ax;

    float lenSq = perp[2] * perp[2] + perp[0] * perp[0] + perp[1] * perp[1];
    if (FLOAT_80615b30 < lenSq) {
        float inv = (float)(1.0f / SQRT(lenSq));
        inv = inv * -(FLOAT_80615b28 * lenSq * inv * inv - FLOAT_80615b2c);
        float scale = FLOAT_80615b24 * inv * -(FLOAT_80615b28 * lenSq * inv * inv - FLOAT_80615b2c);
        perp[0] = perp[0] * scale;
        perp[1] = perp[1] * scale;
        perp[2] = perp[2] * scale;
    }

    quat_from_axis_angle(yaw, quat, work);
    quat_rotate_vector(quat, perp, perp);
    quat_from_axis_angle(pitch, quat, perp);
    quat_rotate_vector(quat, work, work);

    vec[0] = work[0];
    vec[1] = work[1];
    vec[2] = work[2];
}

// ---------------------------------------------------------------------------
// angle_wrap_clamp().  Address: 0x8013170c
// If [lo,hi] spans a full turn (>= 2pi) or lo==hi, wraps 'value' into the
// canonical range via fmod. Otherwise clamps value into [lo, hi].
// Called by the camera look tick.
// ---------------------------------------------------------------------------
double angle_wrap_clamp(double value, double lo, double hi)
{
    if ((FLOAT_80615b34 <= ABS((float)(hi - lo))) || (hi == lo)) {
        double m = FUN_80390324((double)((float)(value - (double)FLOAT_80615b38) / FLOAT_80615b34));
        lo = -(double)(float)((double)FLOAT_80615b34 * (double)(float)m - value);
    } else {
        double clamped = value;
        if (hi < value) {
            clamped = hi;
        }
        if ((lo <= clamped) && (lo = value, hi < value)) {
            lo = hi;
        }
    }
    return lo;
}

// ---------------------------------------------------------------------------
// spline_eval_pos().  Address: 0x801317b8
// Catmull-Rom style cubic interpolation of a vec3 at parameter t (param_1)
// using four control points (p1..p4 -> param_3..param_6). Result -> out.
// ---------------------------------------------------------------------------
void spline_eval_pos(double t, float *out, float *p1, float *p2, float *p3, float *p4)
{
    double t2 = (double)(float)(t * t);
    double t3 = (double)(float)(t2 * t);

    float w2 = FLOAT_80615b40 *
               (float)((double)FLOAT_80615b44 +
                       (double)(float)((double)FLOAT_80615b48 * t3 -
                                       (double)(float)((double)FLOAT_80615b4c * t2)));
    float w1 = FLOAT_80615b40 *
               (float)((double)(float)((double)FLOAT_80615b44 * t2 + -t3) - t);
    float w3 = FLOAT_80615b40 *
               (float)(t +
                       (double)(float)((double)FLOAT_80615b50 * t3 +
                                       (double)(float)((double)FLOAT_80615b54 * t2)));
    float w4 = FLOAT_80615b40 * (float)(t3 - t2);

    out[0] = w4 * p4[0] + w3 * p3[0] + w1 * p1[0] + w2 * p2[0];
    out[1] = w4 * p4[1] + w3 * p3[1] + w1 * p1[1] + w2 * p2[1];
    out[2] = w4 * p4[2] + w3 * p3[2] + w1 * p1[2] + w2 * p2[2];
}

// ---------------------------------------------------------------------------
// spline_eval_tangent().  Address: 0x80131880
// Derivative of the cubic spline w.r.t. t at parameter t (param_1); writes the
// tangent vec3 to out (if non-null) and returns its length via math_sqrt.
// ---------------------------------------------------------------------------
void spline_eval_tangent(double t, float *out, float *p1, float *p2, float *p3, float *p4)
{
    float t2 = (float)(t * t);

    float w2 = FLOAT_80615b40 * (FLOAT_80615b5c * t2 - (float)((double)FLOAT_80615b60 * t));
    float w3 = FLOAT_80615b40 *
               ((FLOAT_80615b50 * t2 + (float)((double)FLOAT_80615b54 * t)) - FLOAT_80615b58);
    float w4 = FLOAT_80615b40 *
               (FLOAT_80615b58 + FLOAT_80615b64 * t2 + (float)((double)FLOAT_80615b68 * t));
    float w1 = FLOAT_80615b40 * (FLOAT_80615b48 * t2 - (float)((double)FLOAT_80615b44 * t));

    float x = w1 * p4[0] + w4 * p3[0] + w3 * p1[0] + w2 * p2[0];
    float y = w1 * p4[1] + w4 * p3[1] + w3 * p1[1] + w2 * p2[1];
    float z = w1 * p4[2] + w4 * p3[2] + w3 * p1[2] + w2 * p2[2];

    if (out != (float *)0) {
        out[0] = x;
        out[1] = y;
        out[2] = z;
    }
    math_sqrt((double)(z * z + x * x + y * y));
}
