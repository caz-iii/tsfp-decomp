// Batch 351 | Date: 2026-09-04
// 4x4 float matrix helpers (16 floats, row-major, 0x40 bytes each).
//
//   mtx4_set_identity          (0x8012ef38) fill with identity
//   mtx4_set_rotation_x        (0x8012efe0) rotation about X by angle
//   mtx4_set_rotation_y        (0x8012f05c) rotation about Y by angle
//   mtx4_pool_alloc            (0x8012ef14) allocate one matrix from frame pool
//   mtx4_table_reset_identity  (0x8012ef8c) reset the context's matrix table
//
// Subsystem: game (core math). Compile-Clean Standard.

#include "common.h"

extern float FLOAT_80615a70;  // 1.0 (identity diagonal)
extern float FLOAT_80615a74;  // 0.0 (identity off-diagonal)
extern float FLOAT_80615a78;  // pi/2 (quarter-turn for cos = sin(x - pi/2))

extern double FUN_801313a4(double x);      // sin
extern int    mem_advance_secondary(int n); // frame-pool bump allocator
extern unsigned long long FUN_803826f0(void); // context push (matrix table + count)
extern void   FUN_8038273c(void);              // context pop

// ----------------------------------------------------------------------------
// mtx4_set_identity (0x8012ef38)
// ----------------------------------------------------------------------------
void mtx4_set_identity(int param_1)
{
    int iVar4 = 0;
    int iVar2 = 0;
    do {
        int iVar5 = 0;
        int iVar3 = 0;
        int iVar6 = 4;
        do {
            float fVar1 = FLOAT_80615a74;
            if (iVar4 == iVar5) {
                fVar1 = FLOAT_80615a70;
            }
            *(float*)(param_1 + iVar2 + iVar3) = fVar1;
            iVar5 = iVar5 + 1;
            iVar3 = iVar3 + 4;
            iVar6 = iVar6 - 1;
        } while (iVar6 != 0);
        iVar4 = iVar4 + 1;
        iVar2 = iVar2 + 0x10;
    } while (iVar4 < 4);
}

// ----------------------------------------------------------------------------
// mtx4_set_rotation_x (0x8012efe0)  rotation about X by angle param_1
// ----------------------------------------------------------------------------
void mtx4_set_rotation_x(double param_1, int param_2)
{
    mtx4_set_identity(param_2);
    double cosv = FUN_801313a4((double)(float)(param_1 - (double)FLOAT_80615a78));  // sin(a - pi/2) = -cos? engine cos
    double sinv = FUN_801313a4(param_1);
    *(float*)(param_2 + 0x14) = (float)sinv;
    *(float*)(param_2 + 0x18) = (float)cosv;
    *(float*)(param_2 + 0x24) = (float)-cosv;
    *(float*)(param_2 + 0x28) = (float)sinv;
}

// ----------------------------------------------------------------------------
// mtx4_set_rotation_y (0x8012f05c)  rotation about Y by angle param_1
// ----------------------------------------------------------------------------
void mtx4_set_rotation_y(double param_1, float* param_2)
{
    mtx4_set_identity((int)param_2);
    double cosv = FUN_801313a4((double)(float)(param_1 - (double)FLOAT_80615a78));
    double sinv = FUN_801313a4(param_1);
    param_2[10] = (float)sinv;
    param_2[8] = (float)cosv;
    param_2[2] = (float)-cosv;
    param_2[0] = (float)sinv;
}

// ----------------------------------------------------------------------------
// mtx4_pool_alloc (0x8012ef14)  allocate one 0x40-byte matrix from the frame pool
// ----------------------------------------------------------------------------
void mtx4_pool_alloc(void)
{
    mem_advance_secondary(0x40);
}

// ----------------------------------------------------------------------------
// mtx4_table_reset_identity (0x8012ef8c)  reset each matrix in the context table
// ----------------------------------------------------------------------------
void mtx4_table_reset_identity(void)
{
    unsigned long long uVar4 = FUN_803826f0();
    int count = (int)uVar4;
    int base = (int)(uVar4 >> 0x20);
    int off = count << 6;   // count * 0x40
    while (count != 0) {
        count = count - 1;
        off = off - 0x40;
        mtx4_set_identity(base + off);
    }
    FUN_8038273c();
}

// ============================================================================
// Batch 352 | Date: 2026-09-04
// More 4x4 float matrix builders (16 floats, column-major storage: element
// [c*4 + r]; translation lives in floats [12],[13],[14] = byte 0x30/0x34/0x38).
//
//   mtx4_set_rotation_z       (0x8012f0d8) rotation about Z by angle
//   mtx4_set_rotation_zyx     (0x8012f154) combined Z*Y*X Euler rotation basis
//   mtx4_set_translate_rotz   (0x8012f2c4) translation + rotation about Z
//   mtx4_set_transform_euler  (0x8012f388) translation + full ZYX Euler basis
//   mtx4_set_translation      (0x8012f534) identity then set translation
//   mtx4_write_translation    (0x8012f588) overwrite translation only
//   mtx4_scale_axes           (0x8012f598) multiply each axis column by scale
//   mtx4_set_scale            (0x8012f62c) identity then diagonal scale
//   mtx4_build_lookat         (0x8012f680) orthonormal look-at basis + eye
//   mtx4_build_lookat_points  (0x8012f8b8) look-at from eye/target/up points
//
// The engine "cos" is FUN_801313a4(x) and "sin" is obtained as
// FUN_801313a4(x - pi/2); the raw code names them the same way, preserved here.
// Subsystem: game (core math). Compile-Clean Standard.
// ============================================================================

extern float FLOAT_80615a7c;  // -1.0

extern double FUN_80131538(double x);  // fast length/sqrt helper (returns ~sqrt)

// ----------------------------------------------------------------------------
// mtx4_set_rotation_z (0x8012f0d8)  rotation about Z by angle param_1
// ----------------------------------------------------------------------------
void mtx4_set_rotation_z(double param_1, float* param_2)
{
    mtx4_set_identity((int)param_2);
    double sinv = FUN_801313a4((double)(float)(param_1 - (double)FLOAT_80615a78));
    double cosv = FUN_801313a4(param_1);
    param_2[0] = (float)cosv;
    param_2[1] = (float)sinv;
    param_2[4] = (float)-sinv;
    param_2[5] = (float)cosv;
}

// ----------------------------------------------------------------------------
// mtx4_set_rotation_zyx (0x8012f154)  Z*Y*X Euler rotation basis (no translate)
// ----------------------------------------------------------------------------
void mtx4_set_rotation_zyx(double param_1, double param_2, double param_3, float* param_4)
{
    double s1 = FUN_801313a4((double)(float)(param_1 - (double)FLOAT_80615a78));
    double c1 = FUN_801313a4(param_1);
    double s2 = FUN_801313a4((double)(float)(param_2 - (double)FLOAT_80615a78));
    double c2 = FUN_801313a4(param_2);
    double s3 = FUN_801313a4((double)(float)(param_3 - (double)FLOAT_80615a78));
    double c3 = FUN_801313a4(param_3);
    float zero = FLOAT_80615a74;
    float one  = FLOAT_80615a70;

    param_4[0] = (float)(c3 * c2 + (double)(float)(s3 * (double)(float)(s1 * s2)));
    param_4[1] = (float)(s3 * c1);
    param_4[2] = (float)(c3 * -s2 + (double)(float)(s3 * (double)(float)(s1 * c2)));
    param_4[3] = zero;
    param_4[4] = (float)(-s3 * c2 + (double)(float)(c3 * (double)(float)(s1 * s2)));
    param_4[5] = (float)(c3 * c1);
    param_4[6] = (float)(s3 * s2 + (double)(float)(c3 * (double)(float)(s1 * c2)));
    param_4[7] = zero;
    param_4[8] = (float)(c1 * s2);
    param_4[9] = (float)-s1;
    param_4[10] = (float)(c1 * c2);
    param_4[11] = zero;
    param_4[12] = zero;
    param_4[13] = zero;
    param_4[14] = zero;
    param_4[15] = one;
}

// ----------------------------------------------------------------------------
// mtx4_set_translate_rotz (0x8012f2c4)  translation (x,y,z) + rotation about Z
//   param_1..3 = translation, param_4 = angle
// ----------------------------------------------------------------------------
void mtx4_set_translate_rotz(double param_1, double param_2, double param_3,
                             double param_4, float* param_5)
{
    mtx4_set_identity((int)param_5);
    double sinv = FUN_801313a4((double)(float)(param_4 - (double)FLOAT_80615a78));
    double cosv = FUN_801313a4(param_4);
    param_5[2] = (float)-sinv;
    param_5[0] = (float)cosv;
    param_5[10] = (float)cosv;
    param_5[8] = (float)sinv;
    param_5[12] = (float)param_1;
    param_5[13] = (float)param_2;
    param_5[14] = (float)param_3;
}

// ----------------------------------------------------------------------------
// mtx4_set_transform_euler (0x8012f388)  translation (x,y,z) + ZYX Euler basis
//   param_1..3 = translation, param_4..6 = euler angles
// ----------------------------------------------------------------------------
void mtx4_set_transform_euler(double param_1, double param_2, double param_3,
                              double param_4, double param_5, double param_6,
                              float* param_7)
{
    double s1 = FUN_801313a4((double)(float)(param_4 - (double)FLOAT_80615a78));
    double c1 = FUN_801313a4(param_4);
    double s2 = FUN_801313a4((double)(float)(param_5 - (double)FLOAT_80615a78));
    double c2 = FUN_801313a4(param_5);
    double s3 = FUN_801313a4((double)(float)(param_6 - (double)FLOAT_80615a78));
    double c3 = FUN_801313a4(param_6);
    float zero = FLOAT_80615a74;
    float one  = FLOAT_80615a70;

    param_7[0] = (float)(c3 * c2 + (double)(float)(s3 * (double)(float)(s1 * s2)));
    param_7[1] = (float)(s3 * c1);
    param_7[2] = (float)(c3 * -s2 + (double)(float)(s3 * (double)(float)(s1 * c2)));
    param_7[3] = zero;
    param_7[4] = (float)(-s3 * c2 + (double)(float)(c3 * (double)(float)(s1 * s2)));
    param_7[5] = (float)(c3 * c1);
    param_7[6] = (float)(s3 * s2 + (double)(float)(c3 * (double)(float)(s1 * c2)));
    param_7[7] = zero;
    param_7[8] = (float)(c1 * s2);
    param_7[9] = (float)-s1;
    param_7[10] = (float)(c1 * c2);
    param_7[11] = zero;
    param_7[12] = (float)param_1;
    param_7[13] = (float)param_2;
    param_7[14] = (float)param_3;
    param_7[15] = one;
}

// ----------------------------------------------------------------------------
// mtx4_set_translation (0x8012f534)  identity, then set translation (x,y,z)
//   Register-pair note: the raw code copies the incoming pointer (r3) and the
//   three incoming floats (f1/f2/f3), wrapped by the __save_fpr_29/__restore
//   compiler helpers. Modeled here as explicit params.
// ----------------------------------------------------------------------------
void mtx4_set_translation(float* mtx, float x, float y, float z)
{
    mtx4_set_identity((int)mtx);
    *(float*)((int)mtx + 0x30) = x;
    *(float*)((int)mtx + 0x34) = y;
    *(float*)((int)mtx + 0x38) = z;
}

// ----------------------------------------------------------------------------
// mtx4_write_translation (0x8012f588)  overwrite translation only, keep basis
// ----------------------------------------------------------------------------
void mtx4_write_translation(double param_1, double param_2, double param_3, int param_4)
{
    *(float*)(param_4 + 0x30) = (float)param_1;
    *(float*)(param_4 + 0x34) = (float)param_2;
    *(float*)(param_4 + 0x38) = (float)param_3;
}

// ----------------------------------------------------------------------------
// mtx4_scale_axes (0x8012f598)  multiply column 0 by sx, col 1 by sy, col 2 by sz
//   (scales the X/Y/Z axis vectors + their translation term, all 4 rows).
// ----------------------------------------------------------------------------
void mtx4_scale_axes(double param_1, double param_2, double param_3, float* param_4)
{
    param_4[0]  = (float)((double)param_4[0]  * param_1);
    param_4[4]  = (float)((double)param_4[4]  * param_1);
    param_4[8]  = (float)((double)param_4[8]  * param_1);
    param_4[12] = (float)((double)param_4[12] * param_1);
    param_4[1]  = (float)((double)param_4[1]  * param_2);
    param_4[5]  = (float)((double)param_4[5]  * param_2);
    param_4[9]  = (float)((double)param_4[9]  * param_2);
    param_4[13] = (float)((double)param_4[13] * param_2);
    param_4[2]  = (float)((double)param_4[2]  * param_3);
    param_4[6]  = (float)((double)param_4[6]  * param_3);
    param_4[10] = (float)((double)param_4[10] * param_3);
    param_4[14] = (float)((double)param_4[14] * param_3);
}

// ----------------------------------------------------------------------------
// mtx4_set_scale (0x8012f62c)  identity then set diagonal scale (sx,sy,sz)
//   Same __save_fpr wrapping as mtx4_set_translation; pointer is the incoming r3.
// ----------------------------------------------------------------------------
void mtx4_set_scale(float* mtx, float sx, float sy, float sz)
{
    // Raw code: pointer in r3, scale x/y/z arrive in f1/f2/f3, wrapped by the
    // __save_fpr_29/__restore_fpr_29 compiler helpers (same shape as
    // mtx4_set_translation). Modeled with explicit params.
    mtx4_set_identity((int)mtx);
    mtx[0]  = sx;   // element [0][0]
    mtx[5]  = sy;   // element [1][1]
    mtx[10] = sz;   // element [2][2]
}

// ----------------------------------------------------------------------------
// mtx4_build_lookat (0x8012f680)  build an orthonormal look-at basis at an eye
//   point. param_1..3 = eye position, param_4..6 = forward dir, param_7..8 +
//   in_stack = up-ish reference vector. Produces basis columns + negated eye
//   translation (view matrix).
// ----------------------------------------------------------------------------
void mtx4_build_lookat(double param_1, double param_2, double param_3,
                       double param_4, double param_5, double param_6,
                       double param_7, double param_8, double param_ref_z,
                       float* param_9)
{
    double refz = param_ref_z;

    // normalize forward (param_4,5,6)
    double len = (double)(float)(param_6 * param_6 +
                 (double)(float)(param_4 * param_4 + (double)(float)(param_5 * param_5)));
    if ((double)FLOAT_80615a74 < len) {
        len = FUN_80131538(len);
        len = (double)(float)((double)FLOAT_80615a7c / len);
    }
    double fz = (double)(float)(param_6 * len);
    double fx = (double)(float)(param_4 * len);
    double fy = (double)(float)(param_5 * len);

    // right = ref x forward
    double rz = (double)(float)(refz   * fx - (double)(float)(param_7 * fz));
    double rx = (double)(float)(param_7 * fy - (double)(float)(param_8 * fx));
    double ry = (double)(float)(param_8 * fz - (double)(float)(refz   * fy));

    len = (double)(float)(rx * rx + (double)(float)(ry * ry + (double)(float)(rz * rz)));
    if ((double)FLOAT_80615a74 < len) {
        len = FUN_80131538(len);
        len = (double)(float)((double)FLOAT_80615a70 / len);
    }
    double Rx = (double)(float)(rx * len);
    double Ry = (double)(float)(ry * len);
    double Rz = (double)(float)(rz * len);

    // up = forward x right
    double ux = (double)(float)(fz * Ry - (double)(float)(fx * Rx));
    double uz = (double)(float)(fx * Rz - (double)(float)(fy * Ry));
    double uy = (double)(float)(fy * Rx - (double)(float)(fz * Rz));

    len = (double)(float)(uz * uz + (double)(float)(uy * uy + (double)(float)(ux * ux)));
    if ((double)FLOAT_80615a74 < len) {
        len = FUN_80131538(len);
        len = (double)(float)((double)FLOAT_80615a70 / len);
    }

    float zero = FLOAT_80615a74;
    float one  = FLOAT_80615a70;
    param_9[0]  = (float)Ry;
    param_9[1]  = (float)(uy * len);
    param_9[2]  = (float)fx;
    param_9[3]  = zero;
    param_9[4]  = (float)Rz;
    param_9[5]  = (float)(ux * len);
    param_9[6]  = (float)fy;
    param_9[7]  = zero;
    param_9[8]  = (float)Rx;
    param_9[9]  = (float)(uz * len);
    param_9[10] = (float)fz;
    param_9[11] = zero;
    param_9[12] = -(float)(param_3 * Rx +
                  (double)(float)(param_1 * Ry + (double)(float)(param_2 * Rz)));
    param_9[13] = -(float)(param_3 * (double)(float)(uz * len) +
                  (double)(float)(param_1 * (double)(float)(uy * len) +
                  (double)(float)(param_2 * (double)(float)(ux * len))));
    param_9[14] = -(float)(param_3 * fz +
                  (double)(float)(param_1 * fx + (double)(float)(param_2 * fy)));
    param_9[15] = one;
}

// ----------------------------------------------------------------------------
// mtx4_build_lookat_points (0x8012f8b8)  look-at view matrix from three points:
//   param_2 = eye, param_3 = target, param_4 = up reference. Forward = target-eye.
// ----------------------------------------------------------------------------
void mtx4_build_lookat_points(float* param_1, float* param_2, float* param_3, float* param_4)
{
    mtx4_build_lookat((double)param_2[0], (double)param_2[1], (double)param_2[2],
                      (double)(float)((double)param_3[0] - (double)param_2[0]),
                      (double)(float)((double)param_3[1] - (double)param_2[1]),
                      (double)(float)((double)param_3[2] - (double)param_2[2]),
                      (double)param_4[0], (double)param_4[1], (double)param_4[2],
                      param_1);
}

// ============================================================================
// Batch 353 | Date: 2026-09-04
// 4x4 matrix transform + projection helpers (continuation of matrix4.cpp).
//
//   mtx4_set_perspective          (0x8012f90c) perspective proj, then transpose
//   mtx4_copy_array               (0x8012f95c) copy N matrices out of a table
//   mtx4_transform_point          (0x8012f9b8) transform a point in place (w=1)
//   mtx4_transform_point_alias    (0x8012fa34) thunk -> mtx4_transform_point
//   mtx4_inverse_transform_point  (0x8012fa54) inverse transform a point
//   mtx4_transform_point4         (0x8012fad0) transform point -> dst, 4 comps
//   mtx4_transform_point4_alias   (0x8012fb70) thunk -> mtx4_transform_point4
//   mtx4_transform_vector         (0x8012fb90) transform a direction in place
//   mtx4_transform_vector_alias   (0x8012fbf4) thunk -> mtx4_transform_vector
//   mtx4_transform_vector_copy    (0x8012fc14) copy src->dst then transform
//
// Storage note: matrix is 16 floats; the basis rows are [0..2],[4..6],[8..10]
// and the translation is [12],[13],[14]. Point transform applies rotation +
// translation; vector transform applies rotation only.
// Subsystem: game (core math). Compile-Clean Standard.
// ============================================================================

extern float FLOAT_80615a80;  // 180.0 (radians->degrees numerator)
extern float FLOAT_80615a84;  // pi     (radians->degrees denominator)

// perspective/projection matrix builder (fov_deg, aspect/near/far params)
extern void FUN_803a135c(double fov_deg, double a, double b, double c, float* out);
// mtx4_transpose (0x80130cd8), mtx4_copy (0x80130b90) and mtx4_multiply
// (0x80130bd4) are defined in the batch 355 section at the end of this file;
// forward-declared here so earlier callers resolve.
void mtx4_transpose(float* src, float* dst);
void mtx4_copy(float* src, float* dst);
void mtx4_multiply(float* a, float* b, float* out);
extern int  leaf_31e8(unsigned int dst, unsigned int src, int len);  // 0x800031e8 blockcopy

// ----------------------------------------------------------------------------
// mtx4_set_perspective (0x8012f90c)
//   Build a perspective projection into a scratch matrix (converting the FOV
//   in param_2 from radians to degrees: 180*fov/pi), then transpose into out.
//   param_1/3/4 are the projection's aspect/near/far parameters.
// ----------------------------------------------------------------------------
void mtx4_set_perspective(double param_1, double param_2, double param_3,
                          double param_4, float* param_5)
{
    float scratch[17];
    FUN_803a135c((double)((float)((double)FLOAT_80615a80 * param_2) / FLOAT_80615a84),
                 param_1, param_3, param_4, scratch);
    mtx4_transpose(scratch, param_5);
}

// ----------------------------------------------------------------------------
// mtx4_copy_array (0x8012f95c)
//   Copy param_3 matrices (0x40 bytes each) from the source table into dst,
//   iterating high->low. src table = param_1, dst = param_2, count = param_3.
// ----------------------------------------------------------------------------
void mtx4_copy_array(float* param_1, float* param_2, int param_3)
{
    int off = param_3 << 6;   // count * 0x40
    while (true) {
        bool last = (param_3 == 0);
        param_3 = param_3 - 1;
        off = off - 0x40;
        if (last) break;
        mtx4_copy((float*)((int)param_1 + off), param_2);
    }
}

// ----------------------------------------------------------------------------
// mtx4_transform_point (0x8012f9b8)
//   p' = M * p + translation (in place). Uses basis columns [0/4/8], [1/5/9],
//   [2/6/10] and translation [12/13/14].
// ----------------------------------------------------------------------------
void mtx4_transform_point(float* param_1, float* param_2)
{
    float y = param_2[1];
    float x = param_2[0];
    float z = param_2[2];
    param_2[0] = param_1[12] + z * param_1[8]  + x * param_1[0] + y * param_1[4];
    param_2[1] = param_1[13] + z * param_1[9]  + x * param_1[1] + y * param_1[5];
    param_2[2] = param_1[14] + z * param_1[10] + x * param_1[2] + y * param_1[6];
}

// ----------------------------------------------------------------------------
// mtx4_transform_point_alias (0x8012fa34)  thunk
// ----------------------------------------------------------------------------
void mtx4_transform_point_alias(float* param_1, float* param_2)
{
    mtx4_transform_point(param_1, param_2);
}

// ----------------------------------------------------------------------------
// mtx4_inverse_transform_point (0x8012fa54)
//   Inverse of a rigid transform: subtract translation, then multiply by the
//   transpose of the rotation basis (rows instead of columns).
// ----------------------------------------------------------------------------
void mtx4_inverse_transform_point(float* param_1, float* param_2)
{
    float y = param_2[1] - param_1[13];
    float x = param_2[0] - param_1[12];
    float z = param_2[2] - param_1[14];
    param_2[0] = z * param_1[2]  + x * param_1[0] + y * param_1[1];
    param_2[1] = z * param_1[6]  + x * param_1[4] + y * param_1[5];
    param_2[2] = z * param_1[10] + x * param_1[8] + y * param_1[9];
}

// ----------------------------------------------------------------------------
// mtx4_transform_point4 (0x8012fad0)
//   Full 4-component point transform (including the w row [3/7/11/15]) into a
//   separate destination.
// ----------------------------------------------------------------------------
void mtx4_transform_point4(float* param_1, float* param_2, float* param_3)
{
    float y = param_2[1];
    float x = param_2[0];
    float z = param_2[2];
    param_3[0] = param_1[12] + z * param_1[8]  + x * param_1[0] + y * param_1[4];
    param_3[1] = param_1[13] + z * param_1[9]  + x * param_1[1] + y * param_1[5];
    param_3[2] = param_1[14] + z * param_1[10] + x * param_1[2] + y * param_1[6];
    param_3[3] = param_1[15] + z * param_1[11] + x * param_1[3] + y * param_1[7];
}

// ----------------------------------------------------------------------------
// mtx4_transform_point4_alias (0x8012fb70)  thunk
// ----------------------------------------------------------------------------
void mtx4_transform_point4_alias(float* param_1, float* param_2, float* param_3)
{
    mtx4_transform_point4(param_1, param_2, param_3);
}

// ----------------------------------------------------------------------------
// mtx4_transform_vector (0x8012fb90)
//   Rotate a direction vector by the basis only (no translation), in place.
// ----------------------------------------------------------------------------
void mtx4_transform_vector(float* param_1, float* param_2)
{
    float y  = param_2[1];
    float m5 = param_1[5];
    float x  = param_2[0];
    float m6 = param_1[6];
    float m1 = param_1[1];
    float z  = param_2[2];
    float m2 = param_1[2];
    float m9 = param_1[9];
    float m10 = param_1[10];
    param_2[0] = z * param_1[8] + x * param_1[0] + y * param_1[4];
    param_2[1] = z * m9  + x * m1 + y * m5;
    param_2[2] = z * m10 + x * m2 + y * m6;
}

// ----------------------------------------------------------------------------
// mtx4_transform_vector_alias (0x8012fbf4)  thunk
// ----------------------------------------------------------------------------
void mtx4_transform_vector_alias(float* param_1, float* param_2)
{
    mtx4_transform_vector(param_1, param_2);
}

// ----------------------------------------------------------------------------
// mtx4_transform_vector_copy (0x8012fc14)
//   Copy the source vector (0x10 bytes) into dst, then rotate dst in place.
//   param_1 = matrix, param_2 = source vector, param_3 = destination vector.
// ----------------------------------------------------------------------------
void mtx4_transform_vector_copy(float* param_1, unsigned int param_2, float* param_3)
{
    leaf_31e8((unsigned int)param_3, param_2, 0x10);
    mtx4_transform_vector(param_1, param_3);
}

// ============================================================================
// Batch 354 | Date: 2026-09-04
// 4x4 matrix inverse / determinant / quaternion / axis-angle helpers.
//
//   mtx4_transform_vector_transpose (0x8012fc60) rotate vec by transpose basis
//   vec3_scalar_triple_product      (0x8012fcc4) a . (b x c), sparse-optimized
//   mtx4_determinant3               (0x8012fd4c) det of the upper-left 3x3
//   mtx4_invert                     (0x8012fdc0) affine inverse via adjugate/det
//   mtx4_to_quat                    (0x8012ffe4) rotation matrix -> quaternion
//   mtx4_slerp                      (0x80130198) interpolate two matrices (slerp)
//   mtx4_set_axis_angle             (0x8013025c) axis-angle rotation matrix
//   mtx4_rotate_about_point         (0x80130374) rotate about a pivot point
//   mtx4_to_euler                   (0x80130438) extract euler angles from matrix
//   mtx4_from_basis                 (0x80130588) build matrix from 3 basis vecs
//
// Several routines allocate scratch matrices from the frame matrix-stack via
// the FUN_803826f0 / FUN_8038273c push/pop pair (compiler GPR save/restore that
// the decompiler surfaces as a (base,count) register pair); preserved as-is.
// Subsystem: game (core math). Compile-Clean Standard.
// ============================================================================

extern float FLOAT_80615a88;  // ~1.19e-7 (determinant singularity epsilon)
extern float FLOAT_80615a8c;  // 0.5      (quaternion half factor)
extern float FLOAT_80615a90;  // ~0.01    (gimbal-lock cos threshold)

// mtx4_multiply forward-declared near top of file (defined in batch 355 section)
extern double math_acos(double x);                             // 0x801314b0
extern double math_atan2(double y, double x);                  // 0x80390aa4
extern void   quat_slerp(double t, float* qa, float* qb, float* out);
extern void   quat_to_matrix(float* out_mtx, float* quat);
extern unsigned long long FUN_803826f0(void);  // frame matrix-stack push (base,count)
extern void   FUN_8038273c(void);              // frame matrix-stack pop
extern unsigned long long __save_gpr_28(void); // GPR save intrinsic (surfaces frame pair)
extern void   __restore_gpr_28(void);          // GPR restore intrinsic

// ----------------------------------------------------------------------------
// mtx4_transform_vector_transpose (0x8012fc60)
//   Rotate a direction by the transpose of the basis (rows [0/1/2] etc.),
//   in place; used for inverse rotation of vectors.
// ----------------------------------------------------------------------------
void mtx4_transform_vector_transpose(float* param_1, float* param_2)
{
    float y = param_2[1];
    float x = param_2[0];
    float z = param_2[2];
    param_2[0] = z * param_1[2]  + x * param_1[0] + y * param_1[1];
    param_2[1] = z * param_1[6]  + x * param_1[4] + y * param_1[5];
    param_2[2] = z * param_1[10] + x * param_1[8] + y * param_1[9];
}

// ----------------------------------------------------------------------------
// vec3_scalar_triple_product (0x8012fcc4)
//   Returns a . (b x c) evaluated per-component but skipping any term whose
//   'a' component is exactly zero (the caller passes matrix column pointers).
// ----------------------------------------------------------------------------
double vec3_scalar_triple_product(float* param_1, float* param_2, float* param_3)
{
    double acc = (double)FLOAT_80615a74;
    if (acc != (double)*param_1) {
        acc = (double)(float)((double)*param_1 *
              (double)(param_2[1] * param_3[2] - param_3[1] * param_2[2]) + acc);
    }
    if ((double)FLOAT_80615a74 != (double)*param_2) {
        acc = -(double)(float)((double)*param_2 *
              (double)(param_1[1] * param_3[2] - param_3[1] * param_1[2]) - acc);
    }
    if ((double)FLOAT_80615a74 == (double)*param_3) {
        return acc;
    }
    return (double)(float)((double)*param_3 *
           (double)(param_1[1] * param_2[2] - param_2[1] * param_1[2]) + acc);
}

// ----------------------------------------------------------------------------
// mtx4_determinant3 (0x8012fd4c)
//   Determinant of the upper-left 3x3 rotation/scale block. Packs the three
//   columns into local vectors and calls the scalar triple product.
// ----------------------------------------------------------------------------
double mtx4_determinant3(float* param_1)
{
    float col0[3];
    float col1[3];
    float col2[3];
    col0[0] = param_1[0];  col0[1] = param_1[4];  col0[2] = param_1[8];
    col1[0] = param_1[1];  col1[1] = param_1[5];  col1[2] = param_1[9];
    col2[0] = param_1[2];  col2[1] = param_1[6];  col2[2] = param_1[10];
    return vec3_scalar_triple_product(col0, col1, col2);
}

// ----------------------------------------------------------------------------
// mtx4_invert (0x8012fdc0)
//   Affine inverse: adjugate of the 3x3 basis divided by the determinant, with
//   the inverse-rotated negated translation. Returns the determinant (0 if the
//   matrix is singular). param_1 = out, param_2 = in.
// ----------------------------------------------------------------------------
double mtx4_invert(float* param_1, float* param_2)
{
    double det = mtx4_determinant3(param_2);
    if ((double)FLOAT_80615a88 <= ABS(det)) {
        float zero = FLOAT_80615a74;
        float one  = FLOAT_80615a70;
        double invIter = (double)FLOAT_80615a70;  // 1.0 numerator for 1/det
        // adjugate (cofactor transpose) of the 3x3
        param_1[0]  = param_2[5] * param_2[10] - param_2[6] * param_2[9];
        param_1[1]  = param_2[2] * param_2[9]  - param_2[1] * param_2[10];
        param_1[2]  = param_2[1] * param_2[6]  - param_2[2] * param_2[5];
        param_1[4]  = param_2[6] * param_2[8]  - param_2[4] * param_2[10];
        param_1[5]  = param_2[0] * param_2[10] - param_2[2] * param_2[8];
        param_1[6]  = param_2[2] * param_2[4]  - param_2[0] * param_2[6];
        param_1[8]  = param_2[4] * param_2[9]  - param_2[5] * param_2[8];
        param_1[9]  = param_2[1] * param_2[8]  - param_2[0] * param_2[9];
        param_1[10] = param_2[0] * param_2[5]  - param_2[1] * param_2[4];
        // divide the 3x3 by the determinant
        int row = 0;
        int rowByte = 0;
        do {
            int colByte = 0;
            int c = 3;
            do {
                float* cell = (float*)((int)param_1 + colByte + rowByte);
                *cell = *cell * (float)(invIter / det);
                colByte = colByte + 4;
                c = c - 1;
            } while (c != 0);
            row = row + 1;
            rowByte = rowByte + 0x10;
        } while (row < 3);
        // inverse-rotated negated translation
        param_1[12] = -(param_2[14] * param_1[8]  + param_2[12] * param_1[0] + param_2[13] * param_1[4]);
        param_1[13] = -(param_2[14] * param_1[9]  + param_2[12] * param_1[1] + param_2[13] * param_1[5]);
        param_1[14] = -(param_2[14] * param_1[10] + param_2[12] * param_1[2] + param_2[13] * param_1[6]);
        param_1[11] = zero;
        param_1[7]  = zero;
        param_1[3]  = zero;
        param_1[15] = one;
        return det;
    }
    return (double)FLOAT_80615a74;  // singular -> 0
}

// ----------------------------------------------------------------------------
// mtx4_to_quat (0x8012ffe4)
//   Convert a rotation matrix (from the frame matrix stack) to a quaternion
//   (written to the stack's paired output). Standard trace-based branch with
//   the largest-diagonal fallback. Uses FUN_80131538 for 1/sqrt.
// ----------------------------------------------------------------------------
void mtx4_to_quat(void)
{
    unsigned long long frame = FUN_803826f0();
    float* out = (float*)(int)(frame >> 0x20);
    float* mtx = (float*)(int)frame;
    static const int nextIndex[3] = { 1, 2, 0 };  // DAT_803cca70/74/78
    float trace = mtx[10] + mtx[0] + mtx[5];
    if (trace <= FLOAT_80615a74) {
        unsigned int i = (unsigned int)(mtx[0] < mtx[5]);
        if (mtx[i * 5] < mtx[10]) {
            i = 2;
        }
        int j = nextIndex[i];
        int k = nextIndex[j];
        double s = FUN_80131538((double)(FLOAT_80615a70 +
                   (mtx[i * 5] - (mtx[j * 5] + mtx[k * 5]))));
        float m_ij = mtx[i * 4 + j];
        float half_over_s = (float)((double)FLOAT_80615a8c / s);
        float m_ji = mtx[j * 4 + i];
        float m_ik = mtx[i * 4 + k];
        float m_ki = mtx[k * 4 + i];
        float q[3];
        q[i] = (float)((double)FLOAT_80615a8c * s);
        q[j] = half_over_s * (m_ij + m_ji);
        q[k] = half_over_s * (m_ik + m_ki);
        out[0] = q[0];
        out[1] = q[1];
        out[2] = q[2];
        out[3] = half_over_s * (mtx[j * 4 + k] - mtx[k * 4 + j]);
    }
    else {
        double s = FUN_80131538((double)(FLOAT_80615a70 + trace));
        float half_over_s = (float)((double)FLOAT_80615a8c / s);
        out[3] = (float)((double)FLOAT_80615a8c * s);
        out[0] = half_over_s * (mtx[6] - mtx[9]);
        out[1] = half_over_s * (mtx[8] - mtx[2]);
        out[2] = half_over_s * (mtx[1] - mtx[4]);
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// mtx4_slerp (0x80130198)
//   Interpolate between two matrices on the frame stack by factor t: convert
//   both to quaternions, slerp, rebuild the rotation, then lerp the translation.
//   param_3 = output matrix. (t arrives in f1; the two source matrices come
//   from the pushed frame slots.)
// ----------------------------------------------------------------------------
void mtx4_slerp(float* param_3, double t)
{
    float qa[12];
    float qb[4];
    float qout[4];
    unsigned long long frame = FUN_803826f0();
    int b = (int)(frame >> 0x20);
    int a = (int)frame;
    mtx4_to_quat();
    mtx4_to_quat();
    quat_slerp(t, qa, qb, qout);
    quat_to_matrix(param_3, qout);
    float inv = (float)((double)FLOAT_80615a70 - t);
    param_3[12] = inv * (*(float*)(b + 0x30) - *(float*)(a + 0x30)) + *(float*)(a + 0x30);
    param_3[13] = inv * (*(float*)(b + 0x34) - *(float*)(a + 0x34)) + *(float*)(a + 0x34);
    param_3[14] = inv * (*(float*)(b + 0x38) - *(float*)(a + 0x38)) + *(float*)(a + 0x38);
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// mtx4_set_axis_angle (0x8013025c)
//   Rodrigues' rotation: build a rotation of 'param_1' radians about the unit
//   axis param_3, written into param_2.
// ----------------------------------------------------------------------------
void mtx4_set_axis_angle(double param_1, float* param_2, float* param_3)
{
    double sinv = FUN_801313a4((double)(float)(param_1 - (double)FLOAT_80615a78));
    double cosv = FUN_801313a4(param_1);
    float zero = FLOAT_80615a74;
    float one  = FLOAT_80615a70;
    double ax = (double)param_3[0];
    double oneMinusCos = (double)(float)((double)FLOAT_80615a70 - cosv);
    double ay = (double)param_3[1];
    double az = (double)param_3[2];
    float xy = (float)(ay * (double)(float)(oneMinusCos * ax));
    float xz = (float)(az * (double)(float)(oneMinusCos * ax));
    param_2[0] = (float)(oneMinusCos * (double)(float)(ax * ax) + cosv);
    param_2[1] = xy - (float)(sinv * az);
    float yz = (float)(az * (double)(float)(oneMinusCos * ay));
    param_2[2] = xz + (float)(sinv * ay);
    param_2[4] = xy + (float)(sinv * az);
    param_2[5] = (float)(oneMinusCos * (double)(float)(ay * ay) + cosv);
    param_2[6] = yz - (float)(sinv * ax);
    param_2[8] = xz - (float)(sinv * ay);
    param_2[9] = yz + (float)(sinv * ax);
    param_2[10] = (float)(oneMinusCos * (double)(float)(az * az) + cosv);
    param_2[11] = zero;
    param_2[7]  = zero;
    param_2[3]  = zero;
    param_2[14] = zero;
    param_2[13] = zero;
    param_2[12] = zero;
    param_2[15] = one;
}

// ----------------------------------------------------------------------------
// mtx4_rotate_about_point (0x80130374)
//   Rotate 'param_1' radians (arriving in f1) about the axis/pivot at param_3:
//   translate to pivot, apply axis-angle rotation, translate back, composing
//   onto the frame-stack matrix via mtx4_multiply.
// ----------------------------------------------------------------------------
void mtx4_rotate_about_point(float* param_3, double angle)
{
    float scratch[24];
    unsigned long long frame = FUN_803826f0();
    float* acc = (float*)(int)(frame >> 0x20);
    float* result = (float*)(int)frame;
    mtx4_set_identity((int)acc);
    mtx4_set_translation(acc, (float)param_3[0], (float)param_3[1], (float)param_3[2]);
    mtx4_multiply(scratch, acc, acc);
    mtx4_set_axis_angle(-angle, scratch, result);
    mtx4_multiply(scratch, acc, acc);
    mtx4_set_translation(acc, -(float)param_3[0], -(float)param_3[1], -(float)param_3[2]);
    mtx4_multiply(scratch, acc, acc);
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// mtx4_to_euler (0x80130438)
//   Extract euler angles from the rotation basis (frame-stack matrix) with the
//   gimbal-lock branch when the pitch cosine is near zero. Outputs pitch to the
//   pushed slot, yaw to param_3, roll to param_4 (each optional / may be null).
// ----------------------------------------------------------------------------
void mtx4_to_euler(float* param_3, float* param_4)
{
    unsigned long long frame = __save_gpr_28();
    float* mtx = (float*)(int)(frame >> 0x20);
    float* pitchOut = (float*)(int)frame;

    // clamp mtx[9] into [-1, 1] before acos
    double m9 = (double)mtx[9];
    double clamped = m9;
    if ((double)FLOAT_80615a70 < m9)  clamped = (double)FLOAT_80615a70;
    double clampArg;
    if ((double)FLOAT_80615a7c <= clamped) {
        clampArg = ((double)FLOAT_80615a70 < m9) ? (double)FLOAT_80615a70 : m9;
    } else {
        clampArg = (double)FLOAT_80615a7c;
    }
    double pitch = (double)math_acos(-clampArg);
    double cosPitch = FUN_801313a4(pitch);

    double yaw, roll;
    if (ABS(cosPitch) <= (double)FLOAT_80615a90) {
        yaw  = -(double)(float)math_atan2(-(double)mtx[4], (double)mtx[0]);
        roll = (double)FLOAT_80615a74;
    } else {
        double invc = (double)(float)((double)FLOAT_80615a70 / cosPitch);
        yaw  = (double)(float)math_atan2((double)(float)((double)mtx[8] * invc),
                                         (double)(float)((double)mtx[10] * invc));
        roll = (double)(float)math_atan2((double)(float)((double)mtx[1] * invc),
                                         (double)(float)((double)mtx[5] * invc));
    }
    if (pitchOut != (float*)0) { *pitchOut = (float)pitch; }
    if (param_3  != (float*)0) { *param_3  = (float)yaw; }
    if (param_4  != (float*)0) { *param_4  = (float)roll; }
    __restore_gpr_28();
}

// ----------------------------------------------------------------------------
// mtx4_from_basis (0x80130588)
//   Build a 4x4 matrix from three basis vectors (X=param_2, Y=param_3,
//   Z=param_4) with zero translation.
// ----------------------------------------------------------------------------
void mtx4_from_basis(float* param_1, float* param_2, float* param_3, float* param_4)
{
    float zero = FLOAT_80615a74;
    float one  = FLOAT_80615a70;
    param_1[0] = param_2[0];  param_1[1] = param_2[1];  param_1[2] = param_2[2];  param_1[3] = zero;
    param_1[4] = param_3[0];  param_1[5] = param_3[1];  param_1[6] = param_3[2];  param_1[7] = zero;
    param_1[8] = param_4[0];  param_1[9] = param_4[1];  param_1[10] = param_4[2]; param_1[11] = zero;
    param_1[12] = zero; param_1[13] = zero; param_1[14] = zero; param_1[15] = one;
}

// ============================================================================
// Batch 355 | Date: 2026-09-04
// Axis-angle (sin/cos form) + orthonormal-basis builders, plus the three core
// matrix primitives (copy / transpose / multiply) whose bodies live here.
//
//   mtx4_set_axis_angle_sc    (0x801305f8) axis-angle from precomputed sin/cos
//   mtx4_set_axis_angle_rad   (0x801306b4) axis-angle wrapper (angle in radians)
//   mtx4_basis_from_forward   (0x80130730) orthonormal basis from a forward vec
//   mtx4_basis_from_fwd_ref   (0x801308a0) basis from forward + reference vec
//   mtx4_orthonormalize_basis (0x80130a00) re-orthonormalize an existing basis
//   mtx4_copy                 (0x80130b90) copy 16 floats
//   mtx4_multiply             (0x80130bd4) out = a * b (4x4)
//   mtx4_transpose            (0x80130cd8) transpose 4x4
//
// The reciprocal-sqrt normalize uses the game's two-step Newton refinement with
// constants FLOAT_80615a8c (0.5), FLOAT_80615a9c (1.5), thresholds a98/aa0.
// Subsystem: game (core math). Compile-Clean Standard.
// ============================================================================

extern float FLOAT_80615a94;  // ~0.9999 (parallel-vector cos threshold)
extern float FLOAT_80615a98;  // small length^2 threshold
extern float FLOAT_80615a9c;  // 1.5 (rsqrt Newton term)
extern float FLOAT_80615aa0;  // near-1 dot threshold (degenerate basis)

extern float DAT_803cca7c;    // default up/ref vector x
extern float DAT_803cca80;    // default up/ref vector y
extern float DAT_803cca84;    // default up/ref vector z

// Ghidra emits SQRT() as a p-code intrinsic; provide a self-contained scalar
// sqrt (Newton-Raphson, no <cmath> which conflicts with the project headers).
static inline float SQRT(float x)
{
    if (x <= 0.0f) return 0.0f;
    float g = x;
    for (int i = 0; i < 8; i++) {
        g = 0.5f * (g + x / g);
    }
    return g;
}

// ----------------------------------------------------------------------------
// mtx4_set_axis_angle_sc (0x801305f8)
//   Build a rotation matrix from a unit axis (param_4) using a precomputed
//   sin (param_1) and cos (param_2). param_3 = output matrix.
// ----------------------------------------------------------------------------
void mtx4_set_axis_angle_sc(double param_1, double param_2, float* param_3, float* param_4)
{
    float zero = FLOAT_80615a74;
    float one  = FLOAT_80615a70;
    double ax = (double)param_4[0];
    double oneMinusCos = (double)(float)((double)FLOAT_80615a70 - param_2);
    float az = param_4[2];
    double sinAy = (double)(float)((double)param_4[1] * param_1);
    double omcAx = (double)(float)(ax * oneMinusCos);
    double omcAy = (double)(float)((double)param_4[1] * oneMinusCos);
    param_3[0] = (float)(omcAx * ax + param_2);
    param_3[1] = (float)(omcAx * (double)param_4[1] - (double)(float)((double)az * param_1));
    param_3[2] = (float)(omcAx * (double)param_4[2] + sinAy);
    param_3[3] = zero;
    param_3[4] = (float)(omcAy * ax + (double)(float)((double)az * param_1));
    param_3[5] = (float)(omcAy * (double)param_4[1] + param_2);
    param_3[6] = (float)(omcAy * (double)param_4[2] - (double)(float)(ax * param_1));
    param_3[7] = zero;
    param_3[8] = (float)(omcAx * (double)param_4[2] - sinAy);
    param_3[9] = (float)(omcAy * (double)param_4[2] + (double)(float)(ax * param_1));
    param_3[10] = (float)(param_2 +
                  (double)(float)((double)param_4[2] * (double)(float)(oneMinusCos * (double)param_4[2])));
    param_3[11] = zero;
    param_3[12] = zero;
    param_3[13] = zero;
    param_3[14] = zero;
    param_3[15] = one;
}

// ----------------------------------------------------------------------------
// mtx4_set_axis_angle_rad (0x801306b4)
//   Axis-angle wrapper: compute sin/cos of param_1 (radians) then delegate to
//   mtx4_set_axis_angle_sc. param_2 = out, param_3 = axis.
// ----------------------------------------------------------------------------
void mtx4_set_axis_angle_rad(double param_1, float* param_2, float* param_3)
{
    double cosv = FUN_801313a4(param_1);
    double sinv = FUN_801313a4((double)(float)(param_1 - (double)FLOAT_80615a78));
    mtx4_set_axis_angle_sc(sinv, cosv, param_2, param_3);
}

// ----------------------------------------------------------------------------
// mtx4_basis_from_forward (0x80130730)
//   Build an orthonormal basis whose Y axis is the given forward vector
//   (param_2). Picks a world up reference (falling back to +Z if forward is
//   near-parallel to it), Gram-Schmidt orthogonalizes, and normalizes.
//   param_1 = output matrix.
// ----------------------------------------------------------------------------
void mtx4_basis_from_forward(float* param_1, float* param_2)
{
    float zero = FLOAT_80615a74;
    float one  = FLOAT_80615a70;
    float refX = DAT_803cca7c;
    float refY = DAT_803cca80;
    float refZ = DAT_803cca84;
    if (FLOAT_80615a94 <
        ABS(param_2[2] * DAT_803cca84 + param_2[0] * DAT_803cca7c + param_2[1] * DAT_803cca80)) {
        refX = FLOAT_80615a74;
        refY = FLOAT_80615a74;
        refZ = FLOAT_80615a70;
    }
    float fy = param_2[1];
    float fx = param_2[0];
    float fz = param_2[2];
    // Gram-Schmidt: ref -= (ref.f) f
    float d = fz * refZ + fx * refX + fy * refY;
    refX = refX - fx * d;
    refY = refY - fy * d;
    refZ = refZ - fz * d;
    float len2 = refZ * refZ + refY * refY + refX * refX;
    if (FLOAT_80615a98 < len2) {
        float r = 1.0f / SQRT(len2);
        r = r * -(FLOAT_80615a8c * len2 * r * r - FLOAT_80615a9c);
        float n = FLOAT_80615a70 * r * -(FLOAT_80615a8c * len2 * r * r - FLOAT_80615a9c);
        refX = refX * n;
        refY = refY * n;
        refZ = refZ * n;
    }
    // X axis = Y x ref
    param_1[0] = fy * refZ - fz * refY;
    param_1[1] = fz * refX - fx * refZ;
    param_1[2] = fx * refY - fy * refX;
    param_1[3] = zero;
    param_1[4] = param_2[0];
    param_1[5] = param_2[1];
    param_1[6] = param_2[2];
    param_1[7] = zero;
    param_1[8] = refX;
    param_1[9] = refY;
    param_1[10] = refZ;
    param_1[11] = zero;
    param_1[12] = zero;
    param_1[13] = zero;
    param_1[14] = zero;
    param_1[15] = one;
}

// ----------------------------------------------------------------------------
// mtx4_basis_from_fwd_ref (0x801308a0)
//   Like mtx4_basis_from_forward but takes an explicit reference vector param_3
//   (which is also fixed up to +Z/-Z when nearly parallel to forward). param_1
//   = out, param_2 = forward, param_3 = reference (modified in place).
// ----------------------------------------------------------------------------
void mtx4_basis_from_fwd_ref(float* param_1, float* param_2, float* param_3)
{
    float dot = param_2[2] * param_3[2] + param_2[0] * param_3[0] + param_2[1] * param_3[1];
    if (FLOAT_80615aa0 < ABS(dot)) {
        if (dot <= FLOAT_80615a74) {
            param_3[0] = FLOAT_80615a74;
            param_3[1] = FLOAT_80615a74;
            param_3[2] = FLOAT_80615a70;
        } else {
            param_3[0] = FLOAT_80615a74;
            param_3[1] = FLOAT_80615a74;
            param_3[2] = FLOAT_80615a7c;
        }
    }
    float fy = param_2[1];
    float fx = param_2[0];
    float fz = param_2[2];
    float d = fz * param_3[2] + fx * param_3[0] + fy * param_3[1];
    float rx = param_3[0] - fx * d;
    float ry = param_3[1] - fy * d;
    float rz = param_3[2] - fz * d;
    float len2 = rz * rz + ry * ry + rx * rx;
    if (FLOAT_80615a98 < len2) {
        float r = 1.0f / SQRT(len2);
        r = r * -(FLOAT_80615a8c * len2 * r * r - FLOAT_80615a9c);
        float n = FLOAT_80615a70 * r * -(FLOAT_80615a8c * len2 * r * r - FLOAT_80615a9c);
        rx = rx * n;
        ry = ry * n;
        rz = rz * n;
    }
    float zero = FLOAT_80615a74;
    float one  = FLOAT_80615a70;
    param_1[0] = fx;
    param_1[1] = param_2[1];
    param_1[2] = param_2[2];
    param_1[3] = zero;
    param_1[4] = ry * fz - rz * fy;
    param_1[5] = rz * fx - rx * fz;
    param_1[6] = rx * fy - ry * fx;
    param_1[7] = zero;
    param_1[8] = rx;
    param_1[9] = ry;
    param_1[10] = rz;
    param_1[11] = zero;
    param_1[12] = zero;
    param_1[13] = zero;
    param_1[14] = zero;
    param_1[15] = one;
}

// ----------------------------------------------------------------------------
// mtx4_orthonormalize_basis (0x80130a00)
//   Re-orthonormalize an existing matrix's basis against a new forward vector
//   param_2, keeping the current Z axis (rows [8..10]) as the reference. param_1
//   = matrix (modified in place), param_2 = forward.
// ----------------------------------------------------------------------------
void mtx4_orthonormalize_basis(float* param_1, float* param_2)
{
    float zero = FLOAT_80615a74;
    float one  = FLOAT_80615a70;
    float zy = param_1[9];
    float zx = param_1[8];
    float zz = param_1[10];
    float dot = param_2[2] * zz + param_2[0] * zx + param_2[1] * zy;
    if (FLOAT_80615aa0 < ABS(dot)) {
        // flip the current X axis by the sign of the dot product
        float s = (float)(((unsigned int)dot & 0x80000000u) | 0x3f800000u);
        zx = param_1[0] * s;
        zy = param_1[1] * s;
        zz = param_1[2] * s;
    }
    float fy = param_2[1];
    float fx = param_2[0];
    float fz = param_2[2];
    float d = fz * zz + fx * zx + fy * zy;
    zx = zx - fx * d;
    zy = zy - fy * d;
    zz = zz - fz * d;
    float len2 = zz * zz + zy * zy + zx * zx;
    if (FLOAT_80615a98 < len2) {
        float r = 1.0f / SQRT(len2);
        r = r * -(FLOAT_80615a8c * len2 * r * r - FLOAT_80615a9c);
        float n = FLOAT_80615a70 * r * -(FLOAT_80615a8c * len2 * r * r - FLOAT_80615a9c);
        zx = zx * n;
        zy = zy * n;
        zz = zz * n;
    }
    param_1[0] = fy * zz - fz * zy;
    param_1[1] = fz * zx - fx * zz;
    param_1[2] = fx * zy - fy * zx;
    param_1[3] = zero;
    param_1[4] = param_2[0];
    param_1[5] = param_2[1];
    param_1[6] = param_2[2];
    param_1[7] = zero;
    param_1[8] = zx;
    param_1[9] = zy;
    param_1[10] = zz;
    param_1[11] = zero;
    param_1[12] = zero;
    param_1[13] = zero;
    param_1[14] = zero;
    param_1[15] = one;
}

// ----------------------------------------------------------------------------
// mtx4_copy (0x80130b90)  copy 16 floats src -> dst
// ----------------------------------------------------------------------------
void mtx4_copy(float* src, float* dst)
{
    for (int i = 0; i < 16; i++) {
        dst[i] = src[i];
    }
}

// ----------------------------------------------------------------------------
// mtx4_multiply (0x80130bd4)  out = a * b  (4x4, column-major)
// ----------------------------------------------------------------------------
void mtx4_multiply(float* a, float* b, float* out)
{
    float b0 = b[0],  b1 = b[1],  b4 = b[4],  b5 = b[5];
    float b8 = b[8],  b9 = b[9],  b12 = b[12], b13 = b[13];
    float b2 = b[2],  b3 = b[3],  b6 = b[6],  b7 = b[7];
    float b10 = b[10], b11 = b[11], b14 = b[14], b15 = b[15];

    float a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
    out[0]  = b12 * a3 + b8 * a2 + b4 * a1 + b0 * a0;
    out[1]  = b13 * a3 + b9 * a2 + b5 * a1 + b1 * a0;
    float a4 = a[4], a5 = a[5], a6 = a[6], a7 = a[7];
    out[4]  = b12 * a7 + b8 * a6 + b4 * a5 + b0 * a4;
    out[5]  = b13 * a7 + b9 * a6 + b5 * a5 + b1 * a4;
    float a8 = a[8], a9 = a[9], a10 = a[10], a11 = a[11];
    out[8]  = b12 * a11 + b8 * a10 + b4 * a9 + b0 * a8;
    out[9]  = b13 * a11 + b9 * a10 + b5 * a9 + b1 * a8;
    float a12 = a[12], a13 = a[13], a14 = a[14], a15 = a[15];
    out[12] = b12 * a15 + b8 * a14 + b4 * a13 + b0 * a12;
    out[13] = b13 * a15 + b9 * a14 + b5 * a13 + b1 * a12;

    out[2]  = b14 * a3  + b10 * a2  + b6 * a1  + b2 * a0;
    out[3]  = b15 * a3  + b11 * a2  + b7 * a1  + b3 * a0;
    out[6]  = b14 * a7  + b10 * a6  + b6 * a5  + b2 * a4;
    out[7]  = b15 * a7  + b11 * a6  + b7 * a5  + b3 * a4;
    out[10] = b14 * a11 + b10 * a10 + b6 * a9  + b2 * a8;
    out[11] = b15 * a11 + b11 * a10 + b7 * a9  + b3 * a8;
    out[14] = b14 * a15 + b10 * a14 + b6 * a13 + b2 * a12;
    out[15] = b15 * a15 + b11 * a14 + b7 * a13 + b3 * a12;
}

// ----------------------------------------------------------------------------
// mtx4_transpose (0x80130cd8)  transpose a 4x4 matrix src -> dst
// ----------------------------------------------------------------------------
void mtx4_transpose(float* src, float* dst)
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            dst[c * 4 + r] = src[r * 4 + c];
        }
    }
}
