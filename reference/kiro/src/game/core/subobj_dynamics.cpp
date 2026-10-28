// Address range: 0x80145df4 - 0x80146318 | batch_411 | 2026-09-05
//
// Entity render sub-object DYNAMICS / impulse helpers (companion to
// subobj_pool.cpp / subobj_build.cpp).
//
// Each sub-object descriptor (DAT_8061218c + (handle & 0xffff) * 0x200) carries
// a small rigid-body-style accumulator used to give render sub-objects a bit of
// physical response (recoil, ragdoll nudges, gib spin). The relevant descriptor
// fields (byte offsets into the 0x200 block):
//   +0x110..+0x118  linear force accumulator (Fx, Fy, Fz)
//   +0x11c..+0x124  angular force / torque accumulator (Tx, Ty, Tz)
//   +0x134..+0x13c  object-matrix rotation row 0 (world->local basis X)
//   +0x140..+0x148  object-matrix translation (world origin of the sub-object)
//   +0x14c..+0x154  object-matrix rotation row 2 (basis Z)
//   +0x1a8          inverse scale / inverse mass scalar
// Angular contributions are the cross product r x F where r is the lever arm
// from the sub-object origin (+0x140) to the application point.
//
// After every mutation the descriptor is marked dirty via FUN_801493f4 so the
// integrator re-derives the world transform next frame.

#include "common.h"
#include "generated_decls.h"

extern "C" {
// spawn-frame push/pop for the impulse-at-local-point path
extern unsigned long long FUN_803826f0(void);  // push frame -> packed (ptr | handle<<32)
extern void               FUN_8038273c(void);   // pop frame

// mark descriptor transform dirty (subobj)
extern void FUN_801493f4(unsigned int handle);
}

// descriptor pool base (shared with subobj_pool.cpp / subobj_build.cpp)
extern int DAT_8061218c;

// shared float constants
extern float FLOAT_80615ea8;   // 0.0f
extern float FLOAT_80615eb0;   // 1.0f
extern float FLOAT_80615ecc;   // rotation scale numerator
extern float FLOAT_80615ed0;   // rotation scale denominator

// ============================================================================
// 0x80145df4  subobj_apply_euler_to_entity
// Extract the sub-object object-matrix euler angles, scale by
// (FLOAT_80615ecc / FLOAT_80615ed0) and write them to the owner entity's
// rotation fields (+0xc0 pitch, +0x70 yaw, +0xc8 roll), then flag the entity
// transform dirty (bit 0x1000) and set its transform-source byte (+0x32 = 2).
// ============================================================================
void subobj_apply_euler_to_entity(unsigned int handle, int entity)
{
    float num, den;
    float yaw;
    float roll;
    float euler[3];   // local_10 (pitch), local_14 (yaw), local_18 (roll)

    mtx4_to_euler((float*)(DAT_8061218c + (handle & 0xffff) * 0x200), euler);
    yaw  = euler[1];  // returned via &local_14 in the original
    roll = euler[2];  // returned via &local_18
    den = FLOAT_80615ed0;
    num = FLOAT_80615ecc;
    *(float*)(entity + 0xc0) = (num * euler[0]) / den;
    *(float*)(entity + 0x70) = (num * yaw) / den;
    *(float*)(entity + 200)  = (num * roll) / den;   // +0xc8
    *(unsigned int*)(entity + 0x28) |= 0x1000;
    *(unsigned char*)(entity + 0x32) = 2;
}

// ============================================================================
// 0x80145e84  subobj_world_to_local_dir
// Rotate a world-space point (worldPt) into the sub-object local frame using
// the object matrix rows (+0x134 / +0x148) and re-add the row-2 translation
// (+0x14c). Output written to outLocal. Used to convert an application point
// before force accumulation.
// ============================================================================
void subobj_world_to_local_dir(unsigned int handle, float* worldPt, float* outLocal)
{
    int   d = DAT_8061218c + (handle & 0xffff) * 0x200;
    float dy = worldPt[1] - *(float*)(d + 0x144);
    float dz = worldPt[2] - *(float*)(d + 0x148);
    float dx = worldPt[0] - *(float*)(d + 0x140);

    outLocal[0] = *(float*)(d + 0x138) * dz - *(float*)(d + 0x13c) * dy;
    outLocal[1] = *(float*)(d + 0x13c) * dx - *(float*)(d + 0x134) * dz;
    outLocal[2] = *(float*)(d + 0x134) * dy - *(float*)(d + 0x138) * dx;
    outLocal[0] += *(float*)(d + 0x14c);
    outLocal[1] += *(float*)(d + 0x150);
    outLocal[2] += *(float*)(d + 0x154);
}

// ============================================================================
// 0x80145f24  subobj_project_point_torque_axis
// Project a point (pt) onto the ray from the sub-object origin along dir, then
// compute the cross product of the perpendicular offset with the object-matrix
// row-2 axis (+0x14c). Yields a torque axis in outAxis. Used to derive spin
// direction for an impact that misses the center of mass.
// ============================================================================
void subobj_project_point_torque_axis(unsigned int handle, float* pt, float* dir, float* outAxis)
{
    int   d = DAT_8061218c + (handle & 0xffff) * 0x200;
    float t;
    float px, py, pz;

    t = (*(float*)(d + 0x148) - pt[2]) * dir[2] +
        (*(float*)(d + 0x140) - pt[0]) * dir[0] +
        (*(float*)(d + 0x144) - pt[1]) * dir[1];

    py = *(float*)(d + 0x144) - (dir[1] * t + pt[1]);
    pz = *(float*)(d + 0x148) - (dir[2] * t + pt[2]);
    px = *(float*)(d + 0x140) - (dir[0] * t + pt[0]);

    outAxis[0] = *(float*)(d + 0x150) * pz - *(float*)(d + 0x154) * py;
    outAxis[1] = *(float*)(d + 0x154) * px - *(float*)(d + 0x14c) * pz;
    outAxis[2] = *(float*)(d + 0x14c) * py - *(float*)(d + 0x150) * px;
}

// ============================================================================
// 0x80145fc4  subobj_add_linear_force
// Accumulate a linear force (force[0..2]) into the sub-object linear
// accumulator (+0x110), then mark dirty.
// ============================================================================
void subobj_add_linear_force(unsigned int handle, float* force)
{
    int d = DAT_8061218c + (handle & 0xffff) * 0x200;
    *(float*)(d + 0x110) += force[0];
    *(float*)(d + 0x114) += force[1];
    *(float*)(d + 0x118) += force[2];
    FUN_801493f4(handle);
}

// ============================================================================
// 0x80146020  subobj_add_angular_force
// Accumulate a torque (torque[0..2]) into the sub-object angular accumulator
// (+0x11c), then mark dirty.
// ============================================================================
void subobj_add_angular_force(unsigned int handle, float* torque)
{
    int d = DAT_8061218c + (handle & 0xffff) * 0x200;
    *(float*)(d + 0x11c) += torque[0];
    *(float*)(d + 0x120) += torque[1];
    *(float*)(d + 0x124) += torque[2];
    FUN_801493f4(handle);
}

// ============================================================================
// 0x8014607c  subobj_apply_impulse_at_local_point
// Spawn-frame entry: transform a local-space impulse (impulse) and a local
// application point by the sub-object object matrix into world space, offset
// the point by the sub-object world origin (+0x50 words), and feed both to
// subobj_add_force_at_point.
// ============================================================================
void subobj_apply_impulse_at_local_point(unsigned int dummy0, unsigned int dummy1, float* impulse)
{
    unsigned long long frame;
    unsigned int handle;
    float* pt;
    float* desc;
    float  dir[3];    // local_1c/18/14
    float  worldPt[3];// local_28/24/20

    (void)dummy0; (void)dummy1;
    frame  = FUN_803826f0();
    handle = (unsigned int)(frame >> 0x20);
    pt     = (float*)(unsigned int)frame;
    dir[0] = impulse[0];
    desc   = (float*)(DAT_8061218c + (handle & 0xffff) * 0x200);
    dir[1] = impulse[1];
    dir[2] = impulse[2];
    mtx4_transform_vector_alias(desc, dir);

    worldPt[0] = pt[0];
    worldPt[1] = pt[1];
    worldPt[2] = pt[2];
    mtx4_transform_vector_alias(desc, worldPt);
    worldPt[0] += desc[0x50];
    worldPt[1] += desc[0x51];
    worldPt[2] += desc[0x52];

    subobj_add_force_at_point(handle, worldPt, dir);
    FUN_8038273c();
}

// ============================================================================
// 0x80146144  subobj_add_force_at_point
// Apply a world-space force (force) at a world-space point (point): add the
// force to the linear accumulator (+0x110) and add r x force (lever arm from
// the sub-object origin +0x140) to the angular accumulator (+0x11c). Mark dirty.
// ============================================================================
void subobj_add_force_at_point(unsigned int handle, float* point, float* force)
{
    int d = DAT_8061218c + (handle & 0xffff) * 0x200;
    float fx, fy, fz;   // force components
    float rx, ry, rz;   // lever arm

    *(float*)(d + 0x110) += force[0];
    *(float*)(d + 0x114) += force[1];
    *(float*)(d + 0x118) += force[2];

    rz = point[2] - *(float*)(d + 0x148);
    fy = force[1];
    ry = point[1] - *(float*)(d + 0x144);
    fz = force[2];
    rx = point[0] - *(float*)(d + 0x140);
    fx = force[0];

    *(float*)(d + 0x11c) += (ry * fz - rz * fy);
    *(float*)(d + 0x120) += (rz * fx - rx * fz);
    *(float*)(d + 0x124) += (rx * fy - ry * fx);
    FUN_801493f4(handle);
}

// ============================================================================
// 0x8014620c  subobj_add_torque_from_offset
// Angular-only variant of subobj_add_force_at_point: add r x force (lever arm
// from origin +0x140) to the angular accumulator without touching the linear
// accumulator. Mark dirty.
// ============================================================================
void subobj_add_torque_from_offset(unsigned int handle, float* point, float* force)
{
    int d = DAT_8061218c + (handle & 0xffff) * 0x200;
    float fx = force[0], fy = force[1], fz = force[2];
    float rx = point[0] - *(float*)(d + 0x140);
    float ry = point[1] - *(float*)(d + 0x144);
    float rz = point[2] - *(float*)(d + 0x148);

    *(float*)(d + 0x11c) += (ry * fz - rz * fy);
    *(float*)(d + 0x120) += (rz * fx - rx * fz);
    *(float*)(d + 0x124) += (rx * fy - ry * fx);
    FUN_801493f4(handle);
}

// ============================================================================
// 0x801462a4  subobj_add_scaled_linear_force
// Add a linear force scaled by inverse mass (1 / +0x1a8) to the linear
// accumulator, but only when the inverse-mass field is positive. Mark dirty
// unconditionally.
// ============================================================================
void subobj_add_scaled_linear_force(unsigned int handle, float* force)
{
    int d = DAT_8061218c + (handle & 0xffff) * 0x200;
    float invMass;

    if (FLOAT_80615ea8 < *(float*)(d + 0x1a8)) {
        invMass = FLOAT_80615eb0 / *(float*)(d + 0x1a8);
        *(float*)(d + 0x110) += force[0] * invMass;
        *(float*)(d + 0x114) += force[1] * invMass;
        *(float*)(d + 0x118) += force[2] * invMass;
    }
    FUN_801493f4(handle);
}

// ============================================================================
// 0x80146318  subobj_add_force_at_point_scaled
// Scale a world-space force by inverse mass (1 / +0x1a8) and apply it at a
// point via subobj_add_force_at_point, only when inverse-mass is positive.
// ============================================================================
void subobj_add_force_at_point_scaled(unsigned int handle, float* point, float* force)
{
    float invMass;
    float scaled[3];   // local_18/14/10

    invMass = *(float*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1a8);
    if (FLOAT_80615ea8 < invMass) {
        invMass = FLOAT_80615eb0 / invMass;
        scaled[0] = force[0] * invMass;
        scaled[1] = force[1] * invMass;
        scaled[2] = force[2] * invMass;
        subobj_add_force_at_point(handle, point, scaled);
    }
}
