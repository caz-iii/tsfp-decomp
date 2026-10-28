// ============================================================================
// Address range: 0x8036a30c - 0x8036a937 | batch_1033 | 2026-09-12
//
// Flock / swarm ambient creature system - the ambient "swarm" objects (a school
// of fish / flock of birds style group) that sit right after the banner module.
// A swarm is a group record in the 0x63c-stride table at DAT_80612ec0 holding up
// to 8 sub-groups; each member is a 0x4c-stride record in the table at
// DAT_80612ec4. This slice implements the per-level colour lookup, the member
// -> sub-group assignment pass, and the per-frame member update (player-proximity
// reactions: flee/splash/attract, then a per-sub-group turn + speed integrate).
//
//   flock_level_colors     0x8036a30c  look up a swarm colour pair by level id
//   flock_assign_subgroups 0x8036a3d4  distribute members across 3 live sub-groups
//   flock_member_update    0x8036a590  per-frame proximity reactions + steering
//
// NOTE: non-matching decomp. The float vector math (distances, fast inverse
// sqrt steering) is condensed to equivalent expressions; control flow, table
// strides, particle/sound side effects and the member state field (offset +0x2c
// state, +0x44 group id) match the original. Engine helpers with a raw ABI are
// invoked through typed function-pointer casts (see the batch_1030+ approach).

#include "common.h"
#include "generated_decls.h"

// ---- swarm tables ----------------------------------------------------------
extern int   DAT_80612ec0;   // swarm group table base (stride 0x63c, up to 8)
extern int   DAT_80612ec4;   // swarm member table base (stride 0x4c) *ptr
extern int   DAT_80495c38;   // per-level id table (7 entries, stride 4)
extern int   DAT_80495cd4;   // min member count gate

// ---- rodata ----------------------------------------------------------------
extern float FLOAT_8061b380, FLOAT_8061b384, FLOAT_8061b388, FLOAT_8061b38c;
extern float FLOAT_8061b394, FLOAT_8061b398, FLOAT_8061b39c, FLOAT_8061b3a0, FLOAT_8061b3a4;
extern double DOUBLE_8061b3a8, DOUBLE_8061b3b0;
extern float FLOAT_806112fc, FLOAT_80611304, FLOAT_80611308, FLOAT_8061130c;
extern float FLOAT_806108ec;
extern int   DAT_806109c0;
extern int   DAT_803e0c8c, DAT_803e0c90;         // subgroup default fill vec
extern int   DAT_803e0c94, DAT_803e0c98, DAT_803e0c9c;  // splash particle colour
extern double DOUBLE_8061b288;

extern int   FUN_803826e8(void);                 // scratch enter (self ctx)
extern void  FUN_80382734(void);                 // scratch leave

// raw-ABI engine helpers via typed casts.
typedef int  (*EmitSpawnPtrFn)(float*, int, int, float*, float*);
typedef void (*ImpactRayFn)(int, int, int, int, int, float, float*, float*);
typedef unsigned int (*RngTickFn)(void);   // rng_counter_tick returns in EAX

// -------------------------------------------------------------------------
// 0x8036a30c  flock_level_colors - look up a swarm colour pair by level id.
//   Scans the 7-entry level id table; on a match copies the two RGB colour
//   vectors for that level out of the config table at DAT_80612ec4, else fills
//   both with the default grey FLOAT_8061b380.
// -------------------------------------------------------------------------
void flock_level_colors(int levelId, float* outColorA, float* outColorB)
{
    for (int i = 0, idx = 0; i < 7; i++, idx++) {
        if (levelId == *(int*)((int)&DAT_80495c38 + i * 4)) {
            int off = idx * 0x18;
            int base = *(int*)&DAT_80612ec4;  // (config table pointer)
            outColorA[0] = *(float*)(base + off + 4);
            outColorA[1] = *(float*)(base + off + 8);
            outColorA[2] = *(float*)(base + off + 0xc);
            outColorB[0] = *(float*)(base + off + 0x10);
            outColorB[1] = *(float*)(base + off + 0x14);
            outColorB[2] = *(float*)(base + off + 0x18);
            return;
        }
    }
    outColorA[0] = FLOAT_8061b380; outColorA[1] = FLOAT_8061b380; outColorA[2] = FLOAT_8061b380;
    outColorB[0] = FLOAT_8061b380; outColorB[1] = FLOAT_8061b380; outColorB[2] = FLOAT_8061b380;
}

// -------------------------------------------------------------------------
// 0x8036a3d4  flock_assign_subgroups - distribute members across sub-groups.
//   Picks up to two currently-empty sub-groups of this swarm, rotates their
//   facing vectors by a small angle, then round-robins every member of the
//   swarm that is not already in a fixed state (2/3) across three target
//   sub-groups, bumping each target's member count.
// -------------------------------------------------------------------------
void flock_assign_subgroups(void)
{
    int ctx[6];
    ctx[0] = FUN_803826e8();
    int assigned = 0;
    int found = 0;
    int primary = DAT_80612ec0 + ctx[0] * 0x63c + 0xc;
    ctx[3] = primary;
    ctx[4] = DAT_803e0c8c;
    ctx[5] = DAT_803e0c90;

    int off = 0;
    for (int i = 8; i != 0; i--) {
        int grp = DAT_80612ec0 + off + 0xc;
        if (*(int*)(grp + 0x40) == 0) {
            if (ctx[4] != 0) { ctx[5] = grp; ctx[2] = found; break; }
            ctx[4] = grp; ctx[1] = found;
        }
        found++;
        off += 0x63c;
    }

    double baseAngle = (double)FLOAT_8061b384;
    *(int*)(primary + 0x40) = 0;
    *(int*)(primary + 0x48) = -1;
    float scratch[17];
    for (int k = 1, ci = 4; k < 3; k++, ci += 4) {
        leaf_31e8((unsigned int)ctx[ci / 4 + 2], ctx[3], 0x63c);  // copy source group into primary slot
        float ang = (k == 1) ? FLOAT_8061b388 : FLOAT_8061b38c;
        mtx4_set_axis_angle((double)(float)(baseAngle * (double)ang), scratch, (float*)(primary + 0x24));
        mtx4_transform_vector(scratch, (float*)(ctx[ci / 4 + 2] + 0xc));
    }

    int memberBase = *(int*)&DAT_80612ec4;
    int mOff = 0;
    for (int n = 800; n != 0; n--) {
        int m = memberBase + mOff;
        if ((*(int*)(m + 0x44) == ctx[0]) && (*(int*)(m + 0x2c) != 2) && (*(int*)(m + 0x2c) != 3)) {
            int slot = assigned % 3;
            assigned++;
            int grp = ctx[slot + 3];
            *(int*)(m + 0x44) = ctx[slot];
            *(int*)(grp + 0x40) = *(int*)(grp + 0x40) + 1;
        }
        mOff += 0x4c;
    }
    FUN_80382734();
}

// -------------------------------------------------------------------------
// 0x8036a590  flock_member_update - per-frame proximity reactions + steering.
//   For each live member: if a player is within a mode-dependent radius, react
//   (spawn a splash/impact particle, play a splash sound, or apply a repulsion
//   impulse). If anything reacted, then for each live sub-group turn its facing
//   vector by the base angle and integrate its speed.
// -------------------------------------------------------------------------
void flock_member_update(void)
{
    int self = FUN_803826e8();
    bool reacted = false;
    // Only run once the swarm is populated past the min gate.
    if (*(int*)&DAT_80612ec0 < (int)DAT_80495cd4) { FUN_80382734(); return; }

    int memberBase = *(int*)&DAT_80612ec4;
    int mOff = 0;
    int count = *(int*)((int)&DAT_80612ec0 + 4);
    for (int i = 0; i < count; i++, mOff += 0x4c) {
        float* m = (float*)(memberBase + mOff);
        if ((m[0xb] == 2.8026e-45f) || (m[0xb] == 4.2039e-45f)) continue;  // state 2 or 3
        float* eye = *(float**)(self + 0x28);
        float dx = m[0] - eye[0];
        float dy = m[1] - eye[1];
        float dz = m[2] - eye[2];
        float d2 = dz * dz + dx * dx + dy * dy;
        int mode = *(int*)(self + 0x18);

        if (mode == 0x15) {
            if (d2 < FLOAT_8061130c * FLOAT_8061130c) {
                float col[3];
                *(int*)&col[0] = DAT_803e0c94;
                *(int*)&col[1] = DAT_803e0c98;
                *(int*)&col[2] = DAT_803e0c9c;
                m[0xb] = 2.8026e-45f;               // state -> 2 (caught)
                m[0xc] = 0.62132925f;
                unsigned int r = ((RngTickFn)&rng_counter_tick)();
                m[0xe] = FLOAT_8061b3a0 *
                    ((float)((double)r - DOUBLE_8061b3a8) / FLOAT_8061b3a4);
                int emitHandle;
                ((EmitSpawnPtrFn)&particle_emitter_spawn)(m + 0x10, DAT_80612ec0 + 0xd5c, 0, m, col);
                emitHandle = *(int*)&m[0x10];
                particle_cached_set_lifetime(m[0xe], (ParticleEmitter**)&emitHandle);
                reacted = true;
            }
        } else if (((mode == 0x24) && (d2 < FLOAT_8061b388)) ||
                   (d2 < FLOAT_80611308 * FLOAT_80611308)) {
            float* dir = *(float**)(self + 0x2c);
            float negDir[3] = { -dir[0], -dir[1], -dir[2] };
            int tex = sprite_get_texture_id(0x337);
            ((ImpactRayFn)&particle_spawn_impact_from_ray)(tex, 2, 0x5f000020, 0, 1, m[10], m, negDir);
            ((EmitSpawnPtrFn)&particle_emitter_spawn)((float*)0, DAT_80612ec0 + 0xd3b, 1, m, dir);
            unsigned int r = ((RngTickFn)&rng_counter_tick)();
            int sfx[7];
            unsigned char* tail = (unsigned char*)&sfx[6];
            sound_init_play_params((int)sfx);
            tail[0] = 1;
            float pos[3] = { m[0], m[1], m[2] };
            (void)pos;
            sfx[0] = (int)((r & 3) + 0x156);
            sound_play_request(sfx);
            reacted = true;
            m[0xb] = 4.2039e-45f;                    // state -> 3 (dead/splashed)
        } else if (d2 < FLOAT_80611304 * FLOAT_80611304) {
            float f = d2;
            if (FLOAT_8061b394 < f) {
                float inv = 1.0f / SQRT(f);
                inv = inv * -(FLOAT_8061b398 * f * inv * inv - FLOAT_8061b39c);
                f = FLOAT_8061b388 * inv * -(FLOAT_8061b398 * f * inv * inv - FLOAT_8061b39c);
                dx *= f; dy *= f; dz *= f;
            }
            reacted = true;
            m[0] = dx * FLOAT_80611304 + m[0];
            m[1] = dy * FLOAT_80611304 + m[1];
            m[2] = dz * FLOAT_80611304 + m[2];
        }
    }

    if (reacted) {
        int gOff = 0;
        float scratch[17];
        for (int g = 0; g < 8; g++, gOff += 0x63c) {
            int grpBase = *(int*)&DAT_80612ec0;
            if (0 < *(int*)((int)&DAT_80612ec0 + gOff + 0x4c)) {
                mtx4_set_axis_angle((double)FLOAT_8061b384, scratch, (float*)((int)&DAT_80612ec0 + gOff + 0x30));
                mtx4_transform_vector(scratch, (float*)(grpBase + gOff + 0x18));
                double denom = (double)(int)*(int*)&DAT_806109c0;
                *(float*)(grpBase + gOff + 0x58) =
                    (FLOAT_8061b388 / (float)(-denom - DOUBLE_8061b3b0)) *
                    FLOAT_806112fc * FLOAT_806108ec + *(float*)(grpBase + gOff + 0x58);
            }
        }
    }
    FUN_80382734();
}

// ============================================================================
// Address range: 0x8036a938 - 0x8036be7f | batch_1034 | 2026-09-12
//
// Flock / swarm navigation + steering - the members move across a mesh of
// convex navigation regions ("nodes"); each member (a 0x4c-byte record: +0x00
// pos, +0x18 vel, +0x28 facing angle, +0x3c current region ptr at word 0xf,
// +0x28 region id at word 0xa) walks its current region, snapping to the region
// triangle plane, bouncing off region walls, and crossing into a neighbour
// region when it reaches a shared edge. This slice adds the member draw, the
// plane-projection/clamp/bounds helpers, the AABB overlap tests, and the two
// per-frame navigation updates.
//
//   flock_member_draw       0x8036a938  draw a swarm member (env-tex or sprite)
//   flock_project_to_plane  0x8036aca8  snap a member onto its region triangle plane
//   flock_clamp_to_region   0x8036af1c  clamp a member into its region AABB
//   flock_outside_region    0x8036af9c  test a member is outside its region bounds
//   flock_aabb_overlap      0x8036b03c  AABB overlap test with margin
//   flock_aabb_outside      0x8036b0e4  strict AABB outside test
//   flock_cross_edge        0x8036b168  find the crossed region edge + reflect
//   flock_nav_update        0x8036b3fc  per-frame: nearest node / cross to neighbour
//   flock_bounce_walls      0x8036b6d8  bounce member velocity off region walls
//   flock_steer_navigation  0x8036b8b4  steer toward the best neighbour region edge
//
// NOTE: non-matching decomp (see the batch_1033 NOTE). The plane / edge / fast-
// inverse-sqrt vector math is condensed to equivalent expressions; control flow,
// region-node strides and side effects match. Engine helpers whose canonical
// prototypes differ from the raw ABI are invoked through typed casts.

extern int   DAT_80611b1c;   // per-member visibility flag table
extern int   DAT_80611d54;   // camera/scene state block (env-tex matrices at +0x208/+0x20c/+0x214)
extern int   DAT_805ecab8;   // shared env-tex working matrix
extern int   DAT_8058e3cc;   // transposed env-tex matrix dest
extern int   DAT_80612250, DAT_80612258;   // tex-matrix share flags
extern char  DAT_805015bd;   // game-mode flag ('\n' = splitscreen variant)
extern int   DAT_80612610;   // active player count
extern float FLOAT_8061b3b8, FLOAT_8061b3bc, FLOAT_8061b3c0, FLOAT_8061b3c4, FLOAT_8061b3c8;
extern float FLOAT_8061b3cc, FLOAT_8061b3d0, FLOAT_8061b3d4;
extern float FLOAT_8061b390;

extern int   FUN_803826d8(void);   // scratch enter (nav update: member/out)
extern int   FUN_803826c4(void);   // scratch enter (steer: member)
extern void  FUN_80382724(void);   // scratch leave (nav)
extern void  FUN_80382710(void);   // scratch leave (steer)
extern double math_atan2(double y, double x);

// raw-ABI engine helpers via typed casts.
typedef float* (*Mtx4AllocFn)(void);
typedef int    (*NodeRenderFn)(int);
typedef int    (*MeasureTextFn)(float*, int, int*, float*, int);
typedef void   (*DrawMultipassFn)(double, double, int);
typedef void   (*Rot4xFn)(double, int);
typedef void   (*LoadBatchPosFn)(void*);

// forward decls for the two helpers used before definition.
void flock_project_to_plane(float* member, float* outNormal);   // 0x8036aca8
unsigned int flock_aabb_overlap(float* p, float* box);          // 0x8036b03c
void flock_bounce_walls(float* member);                         // 0x8036b6d8

// Normalise a 2D edge direction (component z=a, x=b) into (outZ,outX,outY),
// matching the original's degenerate-axis fast paths + fast inverse-sqrt.
static inline void norm_dir(float a, float b, float& outZ, float& outX, float& outY)
{
    if (FLOAT_8061b380 == a) {
        float s = FLOAT_8061b388 / ABS(b);
        outZ = a * s; outX = b * s; outY = FLOAT_8061b380 * s;
    } else if (FLOAT_8061b380 == b) {
        float s = FLOAT_8061b388 / ABS(a);
        outZ = a * s; outX = b * s; outY = FLOAT_8061b380 * s;
    } else {
        float q = b * b + a * a + FLOAT_8061b380;
        outZ = a; outX = b; outY = FLOAT_8061b380;
        if (FLOAT_8061b394 < q) {
            float r = 1.0f / SQRT(q);
            r = r * -(FLOAT_8061b398 * q * r * r - FLOAT_8061b39c);
            float s = FLOAT_8061b388 * r * -(FLOAT_8061b398 * q * r * r - FLOAT_8061b39c);
            outZ = a * s; outY = FLOAT_8061b380 * s; outX = b * s;
        }
    }
}

// -------------------------------------------------------------------------
// 0x8036a938  flock_member_draw - draw a swarm member (env-tex or sprite).
//   state 0/2/5 (surface members): if visible and its text region fits, build
//   the reflection/env-texture matrix from the member's position + facing and
//   submit the batch position matrix. state 1 (impact splash): position the
//   shared splash instance at the member and draw it multipass additively.
// -------------------------------------------------------------------------
void flock_member_draw(float* m)
{
    float state = m[0xb];
    if ((state == 0.0f) || (state == 2.8026e-45f) || (state == 5.60519e-45f)) {
        float* pool = ((Mtx4AllocFn)&mtx4_pool_alloc)();
        float region[10];
        if ((*(char*)(DAT_80611b1c + (int)m[10]) != '\0') &&
            (((MeasureTextFn)&sprite_measure_text_region_int)(m, 0, (int*)0, region, 0) == 0) &&
            (region[0] <= FLOAT_8061b388)) {
            if (((double)*(float*)(DAT_80612ec0 + 0x3348) != (double)m[4]) || (m[9] == 0.0f)) {
                if (m[9] == 0.0f) {
                    ((Rot4xFn)&mtx4_set_rotation_x)((double)FLOAT_8061b3b8, (int)&DAT_805ecab8);
                    *(float*)(DAT_80612ec0 + 0x3348) = FLOAT_8061b38c;
                } else {
                    mtx4_set_rotation_y((double)m[4], (float*)&DAT_805ecab8);
                    *(float*)(DAT_80612ec0 + 0x3348) = m[4];
                }
            }
            mtx4_write_translation((double)m[0], (double)m[1], (double)m[2], (int)&DAT_805ecab8);
            float work[19];
            mtx4_copy((float*)&DAT_805ecab8, work);       // (temp copy; original uses a stack mtx)
            float tex[19];
            mtx4_multiply((float*)&DAT_805ecab8, *(float**)(DAT_80611d54 + 0x20c), tex);
            float scaleMtx[16];
            mtx4_set_identity((int)scaleMtx);
            mtx4_multiply(tex, scaleMtx, tex);
            bool wide = (DAT_805015bd == '\n') && (1 < DAT_80612610);
            float sy = wide ? FLOAT_8061b398 : FLOAT_8061b388;
            mtx4_scale_axes((double)FLOAT_8061b388, (double)sy, (double)FLOAT_8061b388, tex);
            if (wide && (*(int*)(DAT_80611d54 + 0x214) != 0)) {
                float extra[16];
                mtx4_set_identity((int)extra);
                mtx4_multiply(tex, extra, tex);
            }
            gx_load_env_tex_matrix(tex);
            mtx4_multiply((float*)&DAT_805ecab8, *(float**)(DAT_80611d54 + 0x208), pool);
            mtx4_transpose(pool, (float*)&DAT_8058e3cc);
            DAT_80612250 = 1;
            DAT_80612258 = 0;
            gx_set_tex_matrix_share(0);
            gx_load_batch_pos_matrix((unsigned int*)(DAT_80612ec0 + 0x3340));
        }
    } else if (state == 1.4013e-45f) {   // state == 1 (impact splash)
        int inst = *(int*)(DAT_80612ec0 + 0x31ec);
        double num = (double)FLOAT_8061b3bc;
        double den = (double)FLOAT_8061b3c0;
        *(float*)(inst + 100) = m[0];
        *(float*)(inst + 0x68) = m[1];
        *(float*)(inst + 0x6c) = m[2];
        *(unsigned char*)(inst + 0x32) = 2;
        *(float*)(inst + 0xc0) = (float)((double)(float)(num * (double)m[3]) / den);
        *(float*)(inst + 0x70) = (float)((double)(float)(num * (double)m[4]) / den);
        *(float*)(inst + 200)  = (float)((double)(float)(num * (double)m[5]) / den);
        *(unsigned int*)(inst + 0x28) |= 0x201001;
        *(unsigned short*)(inst + 0x14) = 0;
        gx_set_blend_mode(4);
        ((DrawMultipassFn)&render_instance_draw_multipass)(den, num, *(int*)(inst + 4));
        gx_set_blend_mode(4);
        *(unsigned int*)(inst + 0x28) &= 0xffdffffe;
    }
}

// -------------------------------------------------------------------------
// 0x8036aca8  flock_project_to_plane - snap a member onto its region plane.
//   Builds the current region triangle's plane normal (member word 0xf points
//   at the region record; verts at +8/+0xc/+0x10 .. +0x1c), normalises it,
//   optionally returns it, and projects the member position (and word-6 vector)
//   onto the plane. On a fresh cross (region flag 0x334c set) it also seeds the
//   facing angle from the plane via atan2.
// -------------------------------------------------------------------------
void flock_project_to_plane(float* member, float* outNormal)
{
    int region = *(int*)&member[0xf];
    float ay = *(float*)(region + 8);
    if ((ay == *(float*)(region + 0x14)) && (*(int*)(DAT_80612ec0 + 0x334c) == 0) && (outNormal == 0)) {
        member[0] = ay;
        return;
    }
    float ax = *(float*)(region + 0x10);
    float bx = *(float*)(region + 0x1c);
    if ((ax == bx) && (*(int*)(DAT_80612ec0 + 0x334c) == 0) && (outNormal == 0)) {
        member[2] = bx;
        return;
    }
    float ez = *(float*)(region + 0x18) - *(float*)(region + 0xc);
    float dx = bx - ax;
    float dz = *(float*)(region + 0x14) - ay;
    // Plane normal (cross product of the two triangle edges).
    double nx = (double)(ez * dx);
    double ny = 0.0;
    double nz = (double)(-ez * dz);
    double px = (double)(-ay + member[0]);
    double py = (double)(-*(float*)(region + 0xc) + member[1]);
    double pz = (double)(-ax + member[2]);
    double inv;
    if (FLOAT_8061b380 == dz)      inv = (double)(FLOAT_8061b388 / ABS(dx * ez));
    else if (FLOAT_8061b380 == dx) inv = (double)(FLOAT_8061b388 / ABS(ez * dz));
    else {
        double len = math_sqrt((double)(float)(nz * nz + (double)(float)(nx * nx + (double)(float)(ny * ny))));
        inv = (double)(float)((double)FLOAT_8061b388 / len);
    }
    double n0 = (double)(float)(nx * inv);
    double n1 = (double)(float)(ny * inv);
    double n2 = (double)(float)(nz * inv);
    if (outNormal != 0) { outNormal[0] = (float)(nx * inv); outNormal[1] = (float)(ny * inv); outNormal[2] = (float)(nz * inv); }
    if (*(int*)(DAT_80612ec0 + 0x334c) != 0) {
        member[5] = FLOAT_8061b380;
        member[4] = (float)math_atan2(n0, n2);
        member[3] = FLOAT_8061b380;
        *(int*)(DAT_80612ec0 + 0x334c) = 0;
    }
    double d = -(double)(float)(n2 * pz + (double)(float)(n0 * px + (double)(float)(n1 * py)));
    member[0] = (float)(n0 * d + (double)member[0]);
    member[1] = (float)(n1 * d + (double)member[1]);
    member[2] = (float)(n2 * d + (double)member[2]);
    double d2 = -(double)(float)(n2 * (double)member[8] +
                (double)(float)(n0 * (double)member[6] + (double)(float)(n1 * (double)member[7])));
    member[6] = (float)(n0 * d2 + (double)member[6]);
    member[7] = (float)(n1 * d2 + (double)member[7]);
    member[8] = (float)(n2 * d2 + (double)member[8]);
}

// -------------------------------------------------------------------------
// 0x8036af1c  flock_clamp_to_region - clamp a member into its region AABB.
// -------------------------------------------------------------------------
void flock_clamp_to_region(float* m)
{
    int r = *(int*)&m[0xf];
    if (*(float*)(r + 0x20) <= m[0]) { if (*(float*)(r + 0x28) < m[0]) m[0] = *(float*)(r + 0x28); }
    else m[0] = *(float*)(r + 0x20);
    if (*(float*)(r + 0xc) <= m[1]) { if (*(float*)(r + 0x18) < m[1]) m[1] = *(float*)(r + 0x18); }
    else m[1] = *(float*)(r + 0xc);
    if (m[2] < *(float*)(r + 0x24)) { m[2] = *(float*)(r + 0x24); return; }
    if (m[2] <= *(float*)(r + 0x2c)) return;
    m[2] = *(float*)(r + 0x2c);
}

// -------------------------------------------------------------------------
// 0x8036af9c  flock_outside_region - test a member is outside its region.
// -------------------------------------------------------------------------
bool flock_outside_region(float* m)
{
    int r = *(int*)&m[0xf];
    if (FLOAT_8061b3c4 < *(float*)(r + 0x20) - m[0]) return true;
    if (*(float*)(r + 0x28) - m[0] < FLOAT_8061b3c8) return true;
    if (m[1] < *(float*)(r + 0xc)) return true;
    if (*(float*)(r + 0x18) < m[1]) return true;
    if (FLOAT_8061b3c4 < *(float*)(r + 0x24) - m[2]) return true;
    return *(float*)(r + 0x2c) - m[2] < FLOAT_8061b3c8;
}

// -------------------------------------------------------------------------
// 0x8036b03c  flock_aabb_overlap - AABB overlap test with margin.
//   Returns 1 if point p is inside box [box0..box3]x[box1..box4]x[box2..box5]
//   expanded by FLOAT_8061b390, else 0.
// -------------------------------------------------------------------------
unsigned int flock_aabb_overlap(float* p, float* box)
{
    if (p[0] < box[0] - FLOAT_8061b390) return 0;
    if (FLOAT_8061b390 + box[3] < p[0]) return 0;
    if (p[1] < box[1] - FLOAT_8061b390) return 0;
    if (FLOAT_8061b390 + box[4] < p[1]) return 0;
    if (p[2] < box[2] - FLOAT_8061b390) return 0;
    return countLeadingZeros((unsigned int)(FLOAT_8061b390 + box[5] < p[2])) >> 5;
}

// -------------------------------------------------------------------------
// 0x8036b0e4  flock_aabb_outside - strict AABB outside test.
// -------------------------------------------------------------------------
bool flock_aabb_outside(float* p, float* box)
{
    if (p[0] < box[0]) return true;
    if (box[3] < p[0]) return true;
    if (p[1] < box[1]) return true;
    if (box[4] < p[1]) return true;
    if (p[2] < box[2]) return true;
    return box[5] < p[2];
}

// -------------------------------------------------------------------------
// 0x8036b168  flock_cross_edge - find the crossed region edge + reflect.
//   Walks the current region's edge list (render data +0x1e4 count / +500 list);
//   for the edge whose Y-span brackets the member, if the member's path crosses
//   it, snap the member onto the neighbour region across that edge, mark the
//   cross flag, and re-project. The member/pair pointers arrive packed in the
//   scratch enter (r28/r29 spill).
// -------------------------------------------------------------------------
void flock_cross_edge(void)
{
    unsigned long long packed = (unsigned long long)FUN_803826c4();  // reuse steer scratch enter
    float* hi = (float*)(unsigned int)(packed >> 0x20);
    float* lo = (float*)(unsigned int)packed;
    int rd = ((NodeRenderFn)&scene_node_get_render_data)((int)hi[0x186]);
    int count = *(int*)(rd + 0x1e4);
    int list = *(int*)(rd + 500);
    int off = 0;
    for (; count > 0; count--, off += 4) {
        int edge = *(int*)(list + off);
        if ((hi[1] <= *(float*)(edge + 0x18)) && (*(float*)(edge + 0xc) <= hi[1])) {
            float ez = *(float*)(edge + 0x18) - *(float*)(edge + 0xc);
            float dz = (float)((double)*(float*)(edge + 0x14) - (double)*(float*)(edge + 8));
            float dx = (float)((double)*(float*)(edge + 0x1c) - (double)*(float*)(edge + 0x10));
            double nz = -(double)(ez * dz - FLOAT_8061b380);
            double nx = (double)(ez * dx - FLOAT_8061b380);
            float base = (float)(nx * (double)*(float*)(edge + 8) +
                         (double)(float)(nz * (double)*(float*)(edge + 0x10)));
            float sHi = (float)((double)hi[0] * nx + (double)(float)((double)hi[2] * nz)) - base;
            float sLo = (float)((double)lo[0] * nx + (double)(float)((double)lo[2] * nz)) - base;
            double ny = (double)(FLOAT_8061b380 * dz - FLOAT_8061b380 * dx);
            if (sHi * sLo < FLOAT_8061b380) {
                sHi = ABS(sHi);
                float sum = sHi + ABS(sLo);
                float t = FLOAT_8061b398;
                if (FLOAT_8061b380 != sum) t = sHi / sum;
                int edge2 = *(int*)(list + off);
                float cross = (lo[0] * t + hi[0] * (FLOAT_8061b388 - t)) * dz +
                              (lo[2] * t + hi[2] * (FLOAT_8061b388 - t)) * dx;
                if ((dz * *(float*)(edge2 + 8) + dx * *(float*)(edge2 + 0x10) <= cross) &&
                    (cross <= dz * *(float*)(edge2 + 0x14) + dx * *(float*)(edge2 + 0x1c))) {
                    double inv;
                    if (FLOAT_8061b380 == dz)      inv = (double)(FLOAT_8061b388 / ABS(dx * ez));
                    else if (FLOAT_8061b380 == dx) inv = (double)(FLOAT_8061b388 / ABS(dz * ez));
                    else {
                        double len = math_sqrt((double)(float)(nz * nz +
                            (double)(float)(nx * nx + (double)(float)(ny * ny))));
                        inv = (double)(float)((double)FLOAT_8061b388 / len);
                    }
                    double n0 = (double)(float)(nx * inv);
                    double ncy = (double)(float)(ny * inv);
                    double n2 = (double)(float)(nz * inv);
                    hi[0x18b] = *(float*)(list + off);
                    *(int*)(DAT_80612ec0 + 0x334c) = 1;
                    flock_project_to_plane(hi + 0x17c, (float*)0);
                    hi[0x185] = 1.4013e-45f;
                    hi[0x183] = hi[0x183] +
                        -(float)(n2 * (double)hi[0x184] +
                        (double)(float)(n0 * (double)hi[0x182] + (double)(float)(ncy * (double)hi[0x183])));
                    hi[9] = (float)n0;
                    hi[10] = (float)ncy;
                    hi[0xb] = (float)n2;
                    break;
                }
            }
        }
    }
}

// -------------------------------------------------------------------------
// 0x8036b3fc  flock_nav_update - per-frame nearest node / cross to neighbour.
//   Finds the nearest edge in the member's current region; if none, searches
//   the 10 neighbour nav nodes for one whose region AABB contains the member
//   and moves it there (or bounces it off the region walls); otherwise snaps
//   the member onto the found edge plane and reflects its velocity.
// -------------------------------------------------------------------------
void flock_nav_update(void)
{
    unsigned long long packed = (unsigned long long)FUN_803826d8();
    float* m = (float*)(unsigned int)(packed >> 0x20);
    float* outN = (float*)(unsigned int)packed;
    double best = (double)FLOAT_8061b3cc;
    int bestIdx = -1;
    int rd = ((NodeRenderFn)&scene_node_get_render_data)((int)m[10]);
    int count = *(int*)(rd + 0x1e4);

    if (count == 0) {
        m[0xb] = 4.2039e-45f;
    } else {
        int idx = 0, off = 0;
        for (int i = count; i > 0; i--, idx++, off += 4) {
            int edge = *(int*)(*(int*)(rd + 500) + off);
            float dx0 = *(float*)(edge + 0x10) - m[2];
            float dz0 = *(float*)(edge + 8) - m[0];
            float dz1 = *(float*)(edge + 0x14) - m[0];
            float dx1 = *(float*)(edge + 0x1c) - m[2];
            double d2 = (double)(dx1 * dx1 + dz1 * dz1 + dz0 * dz0 + dx0 * dx0);
            float ez = *(float*)(edge + 0x14) - *(float*)(edge + 8);
            float ex = *(float*)(edge + 0x1c) - *(float*)(edge + 0x10);
            if ((d2 < best) && (d2 <= (double)(FLOAT_8061b3c4 + ez * ez + ex * ex))) {
                bestIdx = idx;
                best = d2;
            }
        }
        if (bestIdx == -1) {
            float nodes[25];
            int nc = scene_get_neighbor_nodes((int)m[10], (int)nodes, 10);
            int nOff = 0;
            for (int i = 0; i < nc; i++, nOff += 4) {
                for (int j = 0, lo = 0; j < 7; j++, lo += 4) {
                    float nn = *(float*)((int)nodes + nOff);
                    if ((nn == *(float*)((int)&DAT_80495c38 + lo)) && (m[10] != nn)) {
                        float boxA[3], boxB[3];
                        flock_level_colors((int)nn, boxA, boxB);
                        if (flock_aabb_overlap(m, boxA) != 0) {
                            m[10] = *(float*)((int)nodes + nOff);
                            FUN_80382724();
                            return;
                        }
                    }
                }
            }
            float lo0[3], hi0[3];
            flock_level_colors((int)m[10], lo0, hi0);
            if (m[0] < lo0[0]) m[6] = ABS(m[6]);
            if (hi0[0] < m[0]) m[6] = -ABS(m[6]);
            if (m[2] < lo0[1]) m[8] = ABS(m[8]);   // (b4/c0 mapped from the color-pair box)
            if (hi0[1] < m[2]) m[8] = -ABS(m[8]);
        } else {
            double vx = (double)m[6], vy = (double)m[7], vz = (double)m[8];
            m[0xf] = *(float*)(*(int*)(rd + 500) + bestIdx * 4);
            *(int*)(DAT_80612ec0 + 0x334c) = 1;
            float outv[4];
            flock_project_to_plane(m, outv);
            m[9] = 1.4013e-45f;
            m[7] = m[7] + -(float)((double)outv[2] * vz +
                   (double)(float)((double)outv[0] * vx + (double)(float)((double)outv[1] * vy)));
            if (outN != 0) { outN[0] = outv[0]; outN[1] = outv[1]; outN[2] = outv[2]; }
        }
    }
    FUN_80382724();
}

// -------------------------------------------------------------------------
// 0x8036b6d8  flock_bounce_walls - bounce member velocity off region walls.
//   For each axis, if the member is past a region wall, flip that velocity
//   component to point back inward. On the Y (near) wall it instead reflects
//   the velocity about the wall's edge normal (a proper bounce).
// -------------------------------------------------------------------------
void flock_bounce_walls(float* m)
{
    int r = *(int*)&m[0xf];
    if (*(float*)(r + 0x20) <= m[0]) { if (*(float*)(r + 0x28) < m[0]) m[6] = -ABS(m[6]); }
    else m[6] = ABS(m[6]);

    if (*(float*)(r + 0xc) <= m[1]) {
        if (*(float*)(r + 0x18) < m[1]) m[7] = -ABS(m[7]);
    } else {
        double zero = (double)FLOAT_8061b380;
        m[9] = 0.0f;
        float vy = m[7];
        m[7] = FLOAT_8061b380;
        double negVy = -(double)vy;
        double ey = (double)(*(float*)(r + 0x18) - *(float*)(r + 0xc));
        double ez = (double)(*(float*)(r + 0x14) - *(float*)(r + 8));
        double ex = (double)(*(float*)(r + 0x1c) - *(float*)(r + 0x10));
        double nx = -(double)(float)(ey * ez - zero);
        double nz = (double)(float)(ey * ex - zero);
        double inv;
        if (zero == ez)      inv = (double)(FLOAT_8061b388 / ABS((float)(ex * ey)));
        else if (zero == ex) inv = (double)(FLOAT_8061b388 / ABS((float)(ez * ey)));
        else {
            double len = math_sqrt((double)(float)(nx * nx + (double)(float)(nz * nz + zero)));
            inv = (double)(float)((double)FLOAT_8061b388 / len);
        }
        m[6] = (float)((double)(float)(nz * inv) * negVy + (double)m[6]);
        m[7] = (float)((double)(float)(zero * inv) * negVy + (double)m[7]);
        m[8] = (float)((double)(float)(nx * inv) * negVy + (double)m[8]);
    }
    if (*(float*)(r + 0x24) <= m[2]) { if (*(float*)(r + 0x2c) < m[2]) m[8] = -ABS(m[8]); }
    else m[8] = ABS(m[8]);
}

// -------------------------------------------------------------------------
// 0x8036b8b4  flock_steer_navigation - steer toward the best neighbour edge.
//   Main navigation update: if the member is outside its region AABB, look
//   across the 10 neighbour nodes for the region it now overlaps. Then find the
//   neighbour region edge best aligned with the member's chosen exit direction,
//   and reflect the member velocity across that edge (a wall slide), moving it
//   into the new region. Falls back to a wall bounce when no edge fits.
// -------------------------------------------------------------------------
void flock_steer_navigation(void)
{
    float* m = (float*)FUN_803826c4();
    int rd = ((NodeRenderFn)&scene_node_get_render_data)((int)m[10]);
    float node = m[10];
    int region = *(int*)&m[0xf];
    double best = (double)FLOAT_8061b3d0;
    int bestEdge = -1;
    float boxA[3], boxB[3];
    flock_level_colors((int)node, boxA, boxB);

    if (flock_aabb_outside(m, boxA)) {
        float nodes[26];
        int nc = scene_get_neighbor_nodes((int)m[10], (int)nodes, 10);
        int nOff = 0;
        bool moved = false;
        for (int i = 0; i < nc && !moved; i++, nOff += 4) {
            for (int j = 0, lo = 0; j < 7; j++, lo += 4) {
                float nn = *(float*)((int)nodes + nOff);
                if ((nn == *(float*)((int)&DAT_80495c38 + lo)) && (m[10] != nn)) {
                    flock_level_colors((int)nn, boxA, boxB);
                    if (flock_aabb_overlap(m, boxA) != 0) {
                        rd = ((NodeRenderFn)&scene_node_get_render_data)(*(int*)((int)nodes + nOff));
                        node = *(float*)((int)nodes + nOff);
                        moved = true;
                        break;
                    }
                }
            }
        }
    }

    if (rd == 0) {
        flock_bounce_walls(m);
        FUN_80382710();
        return;
    }

    // Pick which region corner the member is nearest (near/far edge).
    double rx1 = (double)*(float*)(region + 0x10);
    double rx2 = (double)*(float*)(region + 0x1c);
    double rz1 = (double)*(float*)(region + 8);
    double rz2 = (double)*(float*)(region + 0x14);
    float d1 = (float)(rx1 - (double)m[2]);
    float d2 = (float)(rx2 - (double)m[2]);
    float e1 = (float)(rz1 - (double)m[0]);
    float e2 = (float)(rz2 - (double)m[0]);
    int cornerSel; double selX, selZ;
    if (e2 * e2 + d2 * d2 <= e1 * e1 + d1 * d1) { cornerSel = 2; selX = rx2; selZ = rz2; }
    else                                        { cornerSel = 1; selX = rx1; selZ = rz1; }
    double edx = (double)(float)(rz2 - rz1);
    double edz = (double)(float)(rx2 - rx1);

    int count = *(int*)(rd + 0x1e4);
    int list = *(int*)(rd + 500);
    int off = 0;
    for (int i = 0; i < count; i++, off += 4) {
        float edge = *(float*)(list + off);
        if (region != (int)edge) {
            bool zNear = (ABS((float)(selZ - (double)*(float*)((int)edge + 8))) < FLOAT_8061b3d4) ||
                         (ABS((float)(selZ - (double)*(float*)((int)edge + 0x14))) < FLOAT_8061b3d4);
            bool xNear = (ABS((float)(selX - (double)*(float*)(*(int*)(list + off) + 0x10))) < FLOAT_8061b3d4) ||
                         (ABS((float)(selX - (double)*(float*)(*(int*)(list + off) + 0x1c))) < FLOAT_8061b3d4);
            if (zNear && xNear &&
                (*(float*)(*(int*)(list + off) + 0xc) < m[1]) &&
                (m[1] < *(float*)(*(int*)(list + off) + 0x18))) {
                double a = (double)(float)(edx * edx + (double)(float)(edz * edz));
                double na = a;
                if ((double)FLOAT_8061b388 != a) { na = math_sqrt(a); }
                double e0 = (na != 0.0) ? (double)(float)(edx / na) : edx;
                double e0z = (na != 0.0) ? (double)(float)(edz / na) : edz;
                int ce = *(int*)(list + off);
                double cx = (double)(*(float*)(ce + 0x1c) - *(float*)(ce + 0x10));
                double cz = (double)(*(float*)(ce + 0x14) - *(float*)(ce + 8));
                double cl = (double)(float)(cz * cz + (double)(float)(cx * cx));
                if ((double)FLOAT_8061b388 != cl) { cl = math_sqrt(cl); cz = (double)(float)(cz / cl); cx = (double)(float)(cx / cl); }
                double dot = ABS((double)(float)(cz * e0 + (double)(float)(cx * e0z)));
                if (best < dot) { bestEdge = i; best = dot; }
            }
        }
    }

    if (bestEdge == -1) {
        flock_bounce_walls(m);
    } else {
        float dz, dx;
        if (cornerSel == 1) {
            dz = *(float*)(region + 8) - *(float*)(region + 0x14);
            dx = *(float*)(region + 0x10) - *(float*)(region + 0x1c);
        } else {
            dz = *(float*)(region + 0x14) - *(float*)(region + 8);
            dx = *(float*)(region + 0x1c) - *(float*)(region + 0x10);
        }
        int ce = *(int*)(list + bestEdge * 4);
        double cx1 = (double)*(float*)(ce + 0x10);
        double cz1 = (double)*(float*)(ce + 8);
        double cx2 = (double)*(float*)(ce + 0x1c);
        double cz2 = (double)*(float*)(ce + 0x14);
        float vx, vz;
        if (ABS((float)(selX - cx2)) + ABS((float)(selZ - cz2)) <=
            ABS((float)(selX - cx1)) + ABS((float)(selZ - cz1))) {
            vz = (float)(cz1 - cz2); vx = (float)(cx1 - cx2);
        } else {
            vz = (float)(cz2 - cz1); vx = (float)(cx2 - cx1);
        }
        // Normalise the two edge directions (with the fast inverse-sqrt path).
        float nz, nx, ny;
        norm_dir(dz, dx, nz, nx, ny);
        float mz, mx, my;
        norm_dir(vz, vx, mz, mx, my);
        float dot = m[8] * nx + m[6] * nz + m[7] * ny;
        m[6] = -(nz * dot - m[6]);
        m[7] = -(ny * dot - m[7]);
        m[8] = -(nx * dot - m[8]);
        m[6] = mz * dot + m[6];
        m[7] = my * dot + m[7];
        m[8] = mx * dot + m[8];
        m[0xf] = *(float*)(list + bestEdge * 4);
        m[10] = node;
        *(int*)(DAT_80612ec0 + 0x334c) = 1;
    }
    FUN_80382710();
}

// ============================================================================
// Address range: 0x8036be80 - 0x8036f5f0 | batch_1035 | 2026-09-12
//
// Flock / swarm ambient creature system - the higher levels of the module: the
// per-frame master tick, the two per-frame member update passes (live-member
// steering + settled/impact handling), the centroid-driven ambient sound, the
// two-pass renderer, one-time level init (allocate + seed all members and build
// the trailing splash/spark particle emitters), the navigation-region AABB
// builder, the bullet/proximity hit-scan, and the per-level sound-id lookup.
//
//   flock_update_subgroups           0x8036be80  per-frame: split/merge sub-groups, steer+integrate each live member, push motion history
//   flock_update_centroid_sound      0x8036ca3c  compute live-member centroid, drive the 3D swarm ambient sound
//   flock_update_impact_members      0x8036cbdc  per-frame: settling / "impacted" members - repulsion, splash, region nav, proximity damage
//   flock_render                     0x8036deb0  two-pass draw: additive members, then env-textured members
//   flock_master_update              0x8036e03c  top-level per-frame tick: count settled, grow/shrink live count, spawn splashes, dispatch sub-passes
//   flock_level_init                 0x8036e3dc  one-time: allocate + seed all members, build the trailing splash/spark particle emitters
//   flock_build_region_bounds        0x8036f138  build the per-node navigation-region AABBs from the collision mesh
//   flock_build_region_bounds_gated  0x8036f488  gated wrapper around the bounds builder + worm_pool_alloc
//   flock_hit_scan_members           0x8036f4b8  ray/proximity hit test: pop a live member on a hit (splash + sound)
//   flock_sound_id_for_level         0x8036f5f0  map a level id -> swarm ambient sound id (pair select by param2)
//
// NOTE: non-matching decomp. The paired-single register spill/reload prologue
// that the GameCube ABI leaves in the pseudocode is dropped (it is a callee-save
// artefact, not behaviour). The float steering/turn math (atan2 facing, fast
// inverse-sqrt speed normalise, integer<->float RNG conversions) is written as
// equivalent expressions. Control flow, the 0x63c group stride, the 0x4c member
// stride, the DAT_806108e4 (dt) / DAT_806109c0 (base) scale factors, and every
// particle/sound/gx side effect match the original. Engine helpers with a raw
// ABI are reached through the generated decls / typed prototypes below.

// ---- additional raw symbols referenced by this slice -----------------------
extern int    DAT_806108e4;    // per-frame delta-time step (integer)
extern int    DAT_80612ec8;    // rolling member-count history write index (0..0x1d)
extern int    DAT_805ecaf8;    // member-count history ring (30 * u32)
extern int    DAT_80611d78;    // player state table base (stride 0x141c)
extern int    DAT_80495c54;    // level -> subgroup-shape descriptor table (stride 0x2c)
extern int    DAT_80495c58, DAT_80495c64, DAT_80495c70, DAT_80495c7c;
extern int    DAT_80495cd8;    // spark fan index table (6 entries)
extern int    DAT_806109c0_i;  // (alias placeholder - see DAT_806109c0)
extern int    DAT_805ecb70;    // spark ring vertex scratch (inner ring)
extern int    DAT_805ecb74, DAT_805ecb78, DAT_805ecbc0, DAT_805ecbc4, DAT_805ecbc8;
extern unsigned int DAT_cc008000;   // GX FIFO write port
extern float  FLOAT_8061b3d8, FLOAT_8061b3dc, FLOAT_8061b3e0, FLOAT_8061b3e4, FLOAT_8061b3e8;
extern float  FLOAT_8061b3ec, FLOAT_8061b3f0, FLOAT_8061b3f4, FLOAT_8061b3f8, FLOAT_8061b3fc;
extern float  FLOAT_8061b400, FLOAT_8061b404, FLOAT_8061b408, FLOAT_8061b40c, FLOAT_8061b410;
extern float  FLOAT_8061b414, FLOAT_8061b418, FLOAT_8061b41c, FLOAT_8061b420, FLOAT_8061b424;
extern float  FLOAT_8061b428, FLOAT_8061b42c, FLOAT_8061b430, FLOAT_8061b434, FLOAT_8061b438;
extern float  FLOAT_8061b43c, FLOAT_8061b440;
extern float  FLOAT_80611300, FLOAT_806112f0, FLOAT_806112f4, FLOAT_806112f8, FLOAT_80611310;
extern float  FLOAT_80611314, FLOAT_80611318, FLOAT_8061131c, FLOAT_8061b398_dup; // see FLOAT_8061b398
extern double DOUBLE_8061b3b0;                     // 2^52 int->float bias
extern float  DAT_803e0ca0, DAT_803e0ca4, DAT_803e0ca8;   // up-vector / basis A
extern float  DAT_803e0cac, DAT_803e0cb0, DAT_803e0cb4;   // up-vector / basis B
extern float  DAT_803e0cb8, DAT_803e0cbc, DAT_803e0cc0;   // settle splash dir
extern float  DAT_803e0cc4, DAT_803e0cc8, DAT_803e0ccc;   // hit damage impulse dir
extern int    DAT_803e0cd0, DAT_803e0cd4, DAT_803e0cd8;   // full-swarm splash vec
extern int    DAT_803e0cdc, DAT_803e0ce0, DAT_803e0ce4;   // init seed vec A
extern int    DAT_803e0ce8, DAT_803e0cec, DAT_803e0cf0;   // init seed vec B
extern int    DAT_803e0d14, DAT_803e0d3c, DAT_803e0d40, DAT_803e0d44;
extern int    DAT_803e0d48, DAT_803e0d4c, DAT_803e0d50;
extern int    DAT_80612614;    // "bounds enabled" gate
extern float  FLOAT_8061b38c;

// Scratch enter/leave helpers and math_atan2 are NOT in generated_decls.h, so
// declare them locally. Everything else in this slice is already prototyped by
// generated_decls.h; to invoke those with the raw GameCube ABI (extra args /
// return in EAX that the generated void-typed prototype hides) we take the
// symbol address and call it through a typed function-pointer, matching the
// batch_1030+ convention already used earlier in this file.
extern int    FUN_803826d4(void);   // scratch enter (impact pass)
extern void   FUN_80382720(void);   // scratch leave (impact pass)
extern int    FUN_803826e4(void);   // scratch enter (subgroup / render / master / hit)
extern void   FUN_80382730(void);   // scratch leave (subgroup / hit / master)
extern float  FUN_80390b44(double a, double b);   // random-lerp helper -> scale
// FUN_803826d8 / FUN_80382724 declared earlier in file. math_atan2 too.

// raw-ABI function-pointer types for helpers whose generated prototype differs
typedef unsigned int (*RngTickFn2)(void);
typedef int   (*NodeRenderFn2)(int);
typedef int   (*SubMatFn)(int);
typedef int   (*EndBatchFn)(void);
typedef unsigned int (*SoundReqFn)(int*);
typedef int   (*SwitchResetFn)(int);
typedef int   (*StepSnapFn)(double, double, float*);
typedef int   (*SetRenderTypeFn)(int, void*);
typedef void  (*ImpactRayFn2)(int, int, int, int, int, float, float*, float*);
typedef int   (*EmitSpawnFn2)(int, int, int, float*, float*);
typedef void  (*CachedPosFn)(int*, float*);
typedef int   (*MemAllocFn)(int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int);
typedef int   (*WormPoolFn)(int, int, int, int, int, int, int, int);
typedef int   (*AllocBatchFn)(double, double, double, double, double, double, double, double, unsigned int*, int, int, int, int, int, int, int);
typedef void  (*SwapPageFn)(int, double, double, double, double, double, double, double);
typedef void  (*StatCompleteFn)(double, double, double, double, double, double, double, double);
typedef void  (*DamageFn)(int, double, double, double, double, int, int, float*, int, int*);
typedef int   (*PdefCreateFn)(double, double, int, int, int, int, int, int);
typedef int   (*PdefSizeKfFn)(double, double, double, double, int, int, int);
typedef int   (*Pdef6dFn)(double, double, double, double, double, double);
typedef int   (*Pdef4dFn)(double, double, double, double);
typedef int   (*PdefVolFn)(void*, void*, void*, void*);
typedef int   (*PemCreateFn)(double, double, double, int, int, int, int);
typedef int   (*PemColorFn)(double, double, double, double);
typedef int   (*SpawnByTypeFn)(int, double, double, double, double, double, double, double, int, void*, float*, int, int);

extern void   memset_wrapper(int dst, int val, int len);   // not in generated_decls.h

typedef void  (*Sound3dFn)(double, double, double, int, int, int, int);
typedef void  (*VoidVoidFn)(void);

// Function-like macros that shadow the generated_decls.h prototypes at the call
// sites in THIS translation unit only. Because they are function-like, a bare
// address-of (&name, no parens) is NOT expanded, so the &name inside each macro
// still refers to the real linker symbol - we just call it through the correct
// GameCube ABI signature. This keeps the call sites readable while satisfying
// the compiler and preserving the true argument list at link time.
#define rng_counter_tick()                       (((RngTickFn2)&rng_counter_tick)())
#define scene_node_get_render_data(n)            (((NodeRenderFn2)&scene_node_get_render_data)(n))
#define gx_set_sub_material_id(id)               (((SubMatFn)&gx_set_sub_material_id)(id))
#define gx_end_batch()                           (((EndBatchFn)&gx_end_batch)())
#define sound_play_request(p)                    (((SoundReqFn)&sound_play_request)(p))
#define sound_stop_handle()                      (((VoidVoidFn)&sound_stop_handle)())
#define switch_slot_reset(s)                     (((SwitchResetFn)&switch_slot_reset)(s))
#define scalar_step_toward_snap(a,b,c)           (((StepSnapFn)&scalar_step_toward_snap)(a,b,c))
#define entity_set_render_type(rd,fn)            (((SetRenderTypeFn)&entity_set_render_type)(rd,fn))
#define particle_spawn_impact_from_ray(a,b,c,d,e,g,p,q) (((ImpactRayFn2)&particle_spawn_impact_from_ray)(a,b,c,d,e,g,p,q))
#define particle_emitter_spawn(a,b,c,p,v)        (((EmitSpawnFn2)&particle_emitter_spawn)(a,b,c,p,v))
#define particle_cached_set_position(p,m)        (((CachedPosFn)&particle_cached_set_position)(p,m))
#define sound_update_3d_source(a,b,c,h,p,n,f)    (((Sound3dFn)&sound_update_3d_source)(a,b,c,h,p,n,f))
#define mem_alloc(...)                           (((MemAllocFn)&mem_alloc)(__VA_ARGS__))
#define mem_alloc_aligned(...)                   (((MemAllocFn)&mem_alloc_aligned)(__VA_ARGS__))
#define worm_pool_alloc(...)                     (((WormPoolFn)&worm_pool_alloc)(__VA_ARGS__))
#define gx_alloc_batch_page(...)                 (((AllocBatchFn)&gx_alloc_batch_page)(__VA_ARGS__))
#define gx_swap_batch_page(...)                  (((SwapPageFn)&gx_swap_batch_page)(__VA_ARGS__))
#define statlvl_class_complete(...)              (((StatCompleteFn)&statlvl_class_complete)(__VA_ARGS__))
#define damage_dispatch_to_entity(...)           (((DamageFn)&damage_dispatch_to_entity)(__VA_ARGS__))
#define particle_def_create(...)                 (((PdefCreateFn)&particle_def_create)(__VA_ARGS__))
#define particle_def_set_size_keyframes(...)     (((PdefSizeKfFn)&particle_def_set_size_keyframes)(__VA_ARGS__))
#define particle_def_set_velocity_ranges(...)    (((Pdef6dFn)&particle_def_set_velocity_ranges)(__VA_ARGS__))
#define particle_def_set_color_end(...)          (((Pdef4dFn)&particle_def_set_color_end)(__VA_ARGS__))
#define particle_def_set_size_start_end(...)     (((Pdef4dFn)&particle_def_set_size_start_end)(__VA_ARGS__))
#define particle_def_set_spawn_volume(...)       (((PdefVolFn)&particle_def_set_spawn_volume)(__VA_ARGS__))
#define particle_emitter_create(...)             (((PemCreateFn)&particle_emitter_create)(__VA_ARGS__))
#define particle_emitter_set_color_range(...)    (((PemColorFn)&particle_emitter_set_color_range)(__VA_ARGS__))
#define entity_spawn_by_type(...)                (((SpawnByTypeFn)&entity_spawn_by_type)(__VA_ARGS__))

// group-record field offsets in the DAT_80612ec0 block (bytes)
//   +0x04 live member count        +0x08 game-mode / spawn mode
//   +0x3c..+0x4c  per-subgroup accum colour (r,g,b) + count, stride 0x63c
//   +0x50 history write cursor      +0x5c.. motion history ring (stride 0xc)
//   +0x3350 active flag             +0x35f4 "full" latch
//   +0x35f8 attract timer  +0x35fc..+0x3604 attract target xyz
//   +0x3608..+0x3610 sound centroid xyz  +0x3614 sound gain  +0x3618 sound handle
//   +0x31f0/+0x32bc/+0x3354/+0x3420/+0x34ec/+0x3570 particle defs / emitters
// member-record fields (float* m, stride 0x4c):
//   m[0..2] pos   m[3..5] euler   m[6..8] velocity/facing
//   m[9] region-node ptr (0 => free-fly)  m[10] group colour id  m[0xb] state
//   m[0xc] argb   m[0xd] ttl   m[0xe] settle timer  m[0xf] region-tri ptr
//   m[0x10] spawn-tri  m[0x11] subgroup index  m[0x12] history phase

// ---------------------------------------------------------------------------
// flock_update_subgroups 0x8036be80
// Per-frame update of the sub-group layout and of every live member. First it
// advances the whole-swarm size timer and, from the per-level shape table,
// decides how many sub-groups are live (splitting/merging as the swarm grows).
// Then for each of the 8 sub-groups it steers and integrates every live member:
// pick a target (attract point or nearest player), turn the facing toward it by
// a randomised rate, integrate position, snap onto the navigation region (or
// wrap the free-fly bounds), and push the new pose into the member motion ring.
// ---------------------------------------------------------------------------
void flock_update_subgroups(void)
{
    float*  grp   = (float*)DAT_80612ec0;
    float** memTblPtr = (float**)DAT_80612ec4;
    int     dt    = DAT_806108e4;
    int     base  = DAT_806109c0;
    int     shapeSel = -1;

    FUN_803826e4();
    if (dt == 0) return;

    // advance the swarm-size timer (float held as grp[0])
    float invBase = FLOAT_8061b388 / (float)base;
    grp[0] = invBase * (float)dt + grp[0];

    // walk the per-level shape descriptor table to pick a subgroup count band
    int shapeIdx = -1;
    {
        int off = 0;
        for (int i = 0; i < 3; ++i) {
            if (grp[0] < *(float*)((int)&DAT_80495c7c + off)) {
                shapeSel = *(int*)((int)&DAT_80495c54 + off);
                // (subgroup shape vectors at +0x58/+0x64/+0x70 relative to entry)
                if (shapeSel == 1) {
                    unsigned u = (unsigned)(i - 1);
                    int band = (int)((u & ((int)(-u & ~u) >> 31)) * 0xb);
                    if (grp[0] < (&DAT_80495c7c)[band]) flock_assign_subgroups();
                }
                shapeIdx = i;
                break;
            }
            off += 0x2c;
        }
    }

    if (shapeIdx == -1) {
        grp[2] = 0.0f;                 // no live band
    } else if (shapeIdx < 2) {
        // seed every history slot to the swarm base position
        int b = 0;
        for (int i = 0; i < base * 2; ++i) {
            *(float*)((int)grp + b + 0x698) = grp[0x192];
            *(float*)((int)grp + b + 0x69c) = grp[0x193];
            *(float*)((int)grp + b + 0x6a0) = grp[0x194];
            b += 0xc;
        }
        grp[2] = 1.4013e-45f;          // state = 1
    } else {
        grp[2] = 2.8026e-45f;          // state = 2
    }

    // steer + integrate every live member of every sub-group
    int subIdx = 0;
    int gOff   = 0;
    do {
        float* m = (float*)((int)grp + gOff + 0xc);
        // ease the per-subgroup speed scale toward its target
        float lerp = FUN_80390b44((double)FLOAT_80611300, (double)FLOAT_806108ec);
        m[0x13] = (m[0x13] - FLOAT_8061b388) * lerp + FLOAT_8061b388;

        if ((int)m[0x10] > 0) {
            // choose a steering target: attract point, or nearest player
            float tgtX, tgtY, tgtZ;
            if (shapeSel == 1 || shapeSel == 2) {
                float* ap = *(float**)((int)&DAT_80495c58 + subIdx * 4); // subgroup anchor
                tgtX = ap[0]; tgtY = ap[1]; tgtZ = ap[2];
            } else {
                float best = FLOAT_8061b3cc;
                tgtX = m[0]; tgtY = m[1]; tgtZ = m[2];
                int pOff = 0;
                for (int p = 0; p < DAT_80612610; ++p) {
                    int ps = DAT_80611d78 + pOff;
                    float dx = *(float*)(ps + 0xa8) - m[0];
                    float dy = *(float*)(ps + 0xac) - m[1];
                    float dz = *(float*)(ps + 0xb0) - m[2];
                    float d2 = dz*dz + dx*dx + dy*dy;
                    if (d2 < best) { best = d2; tgtX = *(float*)(ps+0xa8); tgtY = *(float*)(ps+0xac); tgtZ = *(float*)(ps+0xb0); }
                    pOff += 0x141c;
                }
            }

            // turn the facing (m[9..0xb]) toward the target by a randomised rate
            float turnRate = 0.0f;
            for (int i = 0; i < dt; ++i) {
                double yawT, yawM;
                if (FLOAT_8061b388 == (double)m[10]) {
                    yawT = math_atan2((double)(tgtX - m[0]), (double)(tgtZ - m[2]));
                    yawM = math_atan2((double)m[3], (double)m[5]);
                } else {
                    float ux = DAT_803e0ca0, uy = DAT_803e0ca4, uz = DAT_803e0ca8;
                    float bx = uz * m[9]  - ux * m[0xb];
                    float by = ux * m[10] - uz * m[9];
                    float bz = uy * m[0xb] - ux * m[10];
                    yawT = math_atan2((double)((tgtZ - m[2]) * uz + (tgtX - m[0]) * ux + (tgtY - m[1]) * uy),
                                      (double)((tgtZ - m[2]) * bz + (tgtX - m[0]) * by + (tgtY - m[1]) * bx));
                    yawM = math_atan2((double)(m[5] * uz + m[3] * ux + m[4] * uy),
                                      (double)(m[5] * bz + m[3] * by + m[4] * bx));
                }
                if (i == 0) {
                    // first step: jitter+clamp the per-member turn speed m[0xf]
                    float j = FLOAT_8061b3e0 * invBase *
                              (FLOAT_8061b3a0 * ((float)rng_counter_tick() / FLOAT_8061b3a4) - FLOAT_8061b388);
                    float v = m[0xf] + j;
                    if (v > FLOAT_80611314) v = FLOAT_80611314;
                    if (v < FLOAT_80611310) v = FLOAT_80611310;
                    m[0xf] = v;
                    turnRate = m[0xf] * invBase;
                }
                if (shapeSel == -1) {
                    turnRate += FLOAT_8061131c * ((float)rng_counter_tick() / FLOAT_8061b3a4 - FLOAT_8061b398) * invBase;
                }
                // normalise the yaw delta into (-pi,pi] then rotate
                double delta = (double)(float)(yawM - yawT);
                while (delta <= (double)FLOAT_8061b3e8) delta += (double)FLOAT_8061b3e4;
                while ((double)FLOAT_8061b3c0 < delta) delta -= (double)FLOAT_8061b3e4;

                float rot[16];
                mtx4_set_axis_angle(delta >= (double)FLOAT_8061b380 ? (double)turnRate : -(double)turnRate,
                                    rot, m + 9);
                mtx4_transform_vector(rot, m + 3);

                if (i < dt - 1) {
                    m[0] = m[3] * m[0x13] * FLOAT_806112f4 * invBase + m[0];
                    m[1] = m[4] * m[0x13] * FLOAT_806112f4 * invBase + m[1];
                    m[2] = m[5] * m[0x13] * FLOAT_806112f4 * invBase + m[2];
                }
            }

            // re-orthonormalise facing against the region up axis (m[9..0xb])
            float d = -(m[5]*m[0xb] + m[3]*m[9] + m[4]*m[10]);
            m[3] = m[9]*d + m[3];
            m[4] = m[10]*d + m[4];
            m[5] = m[0xb]*d + m[5];
            float len2 = m[5]*m[5] + m[3]*m[3] + m[4]*m[4];
            if (FLOAT_8061b390 < ABS((len2) - FLOAT_8061b388) && FLOAT_8061b394 < len2) {
                float r = 1.0f / SQRT(len2);
                r = r * -(FLOAT_8061b398 * len2 * r * r - FLOAT_8061b39c);
                float s = FLOAT_8061b388 * r * -(FLOAT_8061b398 * len2 * r * r - FLOAT_8061b39c);
                m[3] *= s; m[4] *= s; m[5] *= s;
            }

            // final integrate at the module speed constant
            m[0] = m[3] * FLOAT_8061b3ec * invBase + m[0];
            m[1] = m[4] * FLOAT_8061b3ec * invBase + m[1];
            m[2] = m[5] * FLOAT_8061b3ec * invBase + m[2];

            // stash the pose in the scratch slots at m[0x17c..],m[0x182..]
            m[0x17c] = m[0]; m[0x17d] = m[1]; m[0x17e] = m[2];
            m[0x182] = m[3]; m[0x183] = m[4]; m[0x184] = m[5];

            if (m[0x185] == 0.0f) {
                // free-fly: keep inside the swarm colour AABB, else re-nav
                float lo[2], hi[3];
                flock_level_colors((int)m[0x186], hi, lo);
                m[0x17d] = hi[1];
                flock_cross_edge();
                if (m[0x185] == 0.0f &&
                    (m[0] < hi[0] || lo[0] < m[0] || m[2] < hi[2] || hi[1] < m[2]))
                    flock_nav_update();
            } else {
                // region member: project onto plane, revert + steer if it left
                float sx = m[0x17c], sy = m[0x17d], sz = m[0x17e];
                flock_project_to_plane(m + 0x17c, m + 9);
                if (flock_outside_region(m + 0x17c)) {
                    m[0x17c] = sx; m[0x17d] = sy; m[0x17e] = sz;
                    flock_steer_navigation();
                    if (m[0x185] == 0.0f) {
                        m[9] = FLOAT_8061b380; m[10] = FLOAT_8061b388; m[0xb] = FLOAT_8061b380;
                    } else {
                        flock_clamp_to_region(m + 0x17c);
                        flock_project_to_plane(m + 0x17c, m + 9);
                    }
                }
            }

            m[0] = m[0x17c]; m[1] = m[0x17d]; m[2] = m[0x17e];
            m[3] = m[0x182]; m[4] = m[0x183]; m[5] = m[0x184];

            // push pose into the motion history ring (stride 3 floats, wrap 2*base)
            int hp = (int)m[0x11];
            m[hp * 3 + 0x14] = m[0];
            m[hp * 3 + 0x15] = m[1];
            m[hp * 3 + 0x16] = m[2];
            m[0x11] = (float)((hp + 1) - ((hp + 1) / (base * 2)) * base * 2);
        }
        subIdx += 1;
        gOff   += 0x63c;
    } while (subIdx < 8);

    FUN_80382730();
}

// ---------------------------------------------------------------------------
// flock_update_centroid_sound 0x8036ca3c
// Accumulate the centroid of every live member, ease the cached sound position
// toward it, derive a gain from how many members are still active, then feed the
// 3D ambient swarm sound source.
// ---------------------------------------------------------------------------
void flock_update_centroid_sound(void)
{
    float*  grp = (float*)DAT_80612ec0;
    int     memBase = *(int*)DAT_80612ec4;
    int     count = *(int*)(DAT_80612ec0 + 4);

    unsigned live = 0;
    int scanned = 0;
    float cx = FLOAT_8061b380, cy = FLOAT_8061b380, cz = FLOAT_8061b380;

    for (int i = count, off = 0; i > 0; --i, off += 0x4c) {
        float* m = (float*)(memBase + off);
        if (m[0xb] != 4.2039e-45f) {           // not "gone"
            live++;
            cx += m[0]; cy += m[1]; cz += m[2];
        }
        scanned++;
    }

    if (live != 0) {
        float inv = FLOAT_8061b388 / (float)live;
        *(float*)(DAT_80612ec0 + 0x3608) = FLOAT_8061b390 * (cx*inv - *(float*)(DAT_80612ec0+0x3608)) + *(float*)(DAT_80612ec0+0x3608);
        *(float*)(DAT_80612ec0 + 0x360c) = FLOAT_8061b390 * (cy*inv - *(float*)(DAT_80612ec0+0x360c)) + *(float*)(DAT_80612ec0+0x360c);
        *(float*)(DAT_80612ec0 + 0x3610) = FLOAT_8061b390 * (cz*inv - *(float*)(DAT_80612ec0+0x3610)) + *(float*)(DAT_80612ec0+0x3610);
    }

    // add the free (state!=3) members from the tail of the 800-entry table
    for (int i = scanned * 0x4c, rem = 800 - scanned; rem > 0; --rem, i += 0x4c) {
        if (*(int*)(*(int*)DAT_80612ec4 + i + 0x2c) != 3) live++;
    }

    // gain: fraction of the pool still present, clamped to 1
    double gain = (double)((float)live / FLOAT_8061b3f0);
    if (gain > (double)FLOAT_8061b388) gain = (double)FLOAT_8061b388;
    scalar_step_toward_snap(gain, (double)FLOAT_8061b390, (float*)(DAT_80612ec0 + 0x3614));
    sound_update_3d_source((double)*(float*)(DAT_80612ec0 + 0x3614), (double)FLOAT_8061b3f0,
                           (double)FLOAT_8061b388, DAT_80612ec0 + 0x3618, DAT_80612ec0 + 0x3608, 0x15a, 4);
    (void)grp; (void)cx;
}

// ---------------------------------------------------------------------------
// flock_update_impact_members 0x8036cbdc
// The second per-frame pass: handles the "impact/settle" life-cycle of members
// (splashing on water, settling, and being repelled by nearby players), the
// region navigation for free-fly members, per-subgroup colour accumulation, and
// the bullet-proximity damage dispatch. Also (re)binds the render callback and
// derives each subgroup's averaged colour at the end of the frame.
// ---------------------------------------------------------------------------
void flock_update_impact_members(void)
{
    int     grpBase = DAT_80612ec0;
    int     memBase = *(int*)DAT_80612ec4;
    int     dt      = DAT_806108e4;
    int     base    = DAT_806109c0;
    float   invBase = FLOAT_8061b388 / (float)base;

    FUN_803826d4();

    // is a "leader" entity present to attract the swarm toward?
    int leader = aiactor_find_entity_by_team(0x464f4f44, 3);

    // visibility gate across the 7 render nodes
    byte vis = 0;
    if (DAT_806108e4 == 0 || *(int*)(DAT_80612ec0 + 0x3350) == 0) { FUN_80382720(); return; }
    for (int i = 0, o = 0; i < 7; ++i, o += 4) {
        int node = *(int*)((int)&DAT_80495c38 + o);
        vis |= *(byte*)(DAT_80611b1c + node);
    }
    if (vis == 0) { FUN_80382720(); return; }

    // attract-target bookkeeping (leader present vs. leaving)
    *(int*)(DAT_80612ec0 + 0x35f4) = 0;
    if (leader != 0 || *(int*)(DAT_80612ec0 + 0x35f8) > 0) {
        if (leader == 0) {
            *(int*)(DAT_80612ec0 + 0x35f8) = *(int*)(DAT_80612ec0 + 0x35f8) - dt;
        } else {
            *(int*)(DAT_80612ec0 + 0x35f8) = base * 3;
            int e = *(int*)(leader + 0x7c);
            *(int*)(DAT_80612ec0 + 0x35fc) = *(int*)(e + 0xbb4);
            *(int*)(DAT_80612ec0 + 0x3600) = *(int*)(e + 3000);
            *(int*)(DAT_80612ec0 + 0x3604) = *(int*)(e + 0xbbc);
        }
    }

    // reset per-subgroup colour accumulators (r,g,b start = white, count = 0)
    {
        float white = FLOAT_8061b380;
        int o = 0;
        for (int i = 0; i < 8; ++i) {
            *(float*)(grpBase + o + 0x3c) = white;
            *(float*)(grpBase + o + 0x40) = white;
            *(float*)(grpBase + o + 0x44) = white;
            *(int*)(grpBase + o + 0x4c)   = 0;
            o += 0x63c;
        }
    }

    // per-member state machine
    int mOff = 0;
    int count = *(int*)(grpBase + 4);
    for (int i = 0; i < count; ++i, mOff += 0x4c) {
        float* m = (float*)(memBase + mOff);
        count = *(int*)(grpBase + 4);

        if (m[0xb] != 4.2039e-45f) {           // accumulate live colour
            float ca, cb;
            flock_level_colors((int)m[10], &cb, &ca);
            int gi = (int)m[0x11] * 0x63c;
            *(float*)(grpBase + gi + 0x3c) += m[0];
            *(float*)(grpBase + gi + 0x40) += m[1];
            *(float*)(grpBase + gi + 0x44) += m[2];
            *(int*)(grpBase + gi + 0x4c)   += 1;
        }

        float state = m[0xb];
        if (state == 0.0f || state == 2.8026e-45f) {
            // ---- active / settling member: build the region up-normal ----
            float up[3];
            if (m[9] == 0.0f) {
                up[2] = FLOAT_8061b380; up[0] = FLOAT_8061b380; up[1] = FLOAT_8061b388;
            } else {
                float* tri = *(float**)((int)&m[0xf]);   // region-tri ptr held in m[0xf]
                float ex = tri[7] - tri[4];
                float ey = tri[6] - tri[3];
                float ez = tri[5] - tri[2];
                up[1] = ey * ex - FLOAT_8061b380;
                up[0] = FLOAT_8061b380 * ez - FLOAT_8061b380 * ex;
                up[2] = -(ey * ez - FLOAT_8061b380);
                float n;
                if (FLOAT_8061b380 == ez)      n = FLOAT_8061b388 / ABS(ex * ey);
                else if (FLOAT_8061b380 == ex) n = FLOAT_8061b388 / ABS(ez * ey);
                else {
                    double L = math_sqrt((double)(up[2]*up[2] + up[1]*up[1] + up[0]*up[0]));
                    n = (float)((double)FLOAT_8061b388 / L);
                }
                up[1] *= n; up[0] *= n; up[2] *= n;
            }

            // face toward the last history sample of this subgroup
            int gi = grpBase + (int)m[0x11] * 0x63c;
            int hi = *(int*)(gi + 0x50) - (int)m[0x12];
            if (hi < 0) hi += base * 2;
            int hs = gi + hi * 0xc;
            float tx = *(float*)(hs + 0x5c) - m[0];
            float ty = *(float*)(hs + 0x60) - m[1];
            float tz = *(float*)(hs + 0x64) - m[2];

            double yawT, yawM;
            if ((double)FLOAT_8061b388 == (double)up[1]) {
                yawT = math_atan2((double)tx, (double)tz);
                yawM = math_atan2((double)m[6], (double)m[8]);
            } else {
                float ax = DAT_803e0cac, ay = DAT_803e0cb0, az = DAT_803e0cb4;
                float bx = az * up[0] - ax * up[2];
                float by = ay * up[2] - az * up[1];
                float bz = ax * up[1] - ay * up[0];
                yawT = math_atan2((double)(tx * az + tz * ax + ty * ay),
                                  (double)(tx * bz + tz * bx + ty * by));
                yawM = math_atan2((double)(m[8] * ay + m[6] * ax + m[7] * az),
                                  (double)(m[8] * bz + m[6] * by + m[7] * bx));
            }

            // randomised turn toward yaw target, integrated over dt
            float ttl = -(FLOAT_8061b3f4 * ((float)(m[0x12] / (base * 2)) - (float)DOUBLE_8061b3b0) + FLOAT_8061b3f4);
            double yaw = yawM;
            for (int s = 0; s < dt; ++s) {
                double d = yaw - yawT;
                while (d <= (double)FLOAT_8061b3e8) d += (double)FLOAT_8061b3e4;
                while ((double)FLOAT_8061b3c0 < d) d -= (double)FLOAT_8061b3e4;
                float step;
                if ((double)FLOAT_8061b380 <= d) {
                    step = (FLOAT_8061b388 / (float)base) *
                           -(float)((double)FLOAT_80611318 * (double)((float)rng_counter_tick() / FLOAT_8061b3a4 - FLOAT_8061b398) + (double)ttl);
                } else {
                    step = (FLOAT_8061b388 / (float)base) *
                           (float)((double)FLOAT_80611318 * (double)((float)rng_counter_tick() / FLOAT_8061b3a4 - FLOAT_8061b398) + (double)ttl);
                }
                yaw += (double)step;
            }
            float rot[16];
            mtx4_set_axis_angle(yaw, rot, up);
            mtx4_transform_vector(rot, m + 6);

            // re-orthonormalise facing, normalise speed
            float d = -(m[8]*up[2] + m[6]*up[0] + m[7]*up[1]);
            m[6] = up[0]*d + m[6];
            m[7] = up[1]*d + m[7];
            m[8] = up[2]*d + m[8];
            float len2 = m[8]*m[8] + m[6]*m[6] + m[7]*m[7];
            if (FLOAT_8061b390 < ABS(len2 - FLOAT_8061b388) && FLOAT_8061b394 < len2) {
                float r = 1.0f / SQRT(len2);
                r = r * -(FLOAT_8061b398 * len2 * r * r - FLOAT_8061b39c);
                float sc = FLOAT_8061b388 * r * -(FLOAT_8061b398 * len2 * r * r - FLOAT_8061b39c);
                m[6] *= sc; m[7] *= sc; m[8] *= sc;
            }

            // integrate position by the subgroup speed
            float spd = *(float*)(grpBase + (int)m[0x11] * 0x63c + 0x58) * invBase * FLOAT_806112f0 * invBase;
            m[0] = m[6] * spd + m[0];
            m[1] = m[7] * spd + m[1];
            m[2] = m[8] * spd + m[2];

            if (m[9] == 0.0f) {
                float loA[2], hiA[2];
                flock_level_colors((int)m[10], hiA, loA);
                m[1] = FLOAT_8061b3f8;
                if (m[0] < hiA[0] || loA[0] < m[0] || m[2] < hiA[1] || loA[1] < m[2])
                    flock_nav_update();
            } else {
                float sx = m[0], sy = m[1], sz = m[2];
                flock_project_to_plane(m, (float*)0);
                if (flock_outside_region(m)) {
                    m[0] = sx; m[1] = sy; m[2] = sz;
                    flock_steer_navigation();
                    if (m[9] != 0.0f) {
                        flock_clamp_to_region(m);
                        flock_project_to_plane(m, (float*)0);
                    }
                }
            }

            // proximity to a player / leader decreases the member ttl
            if (*(int*)(grpBase + 8) == 0) {
                for (int pOff = 0, p = DAT_80612610; p > 0; --p, pOff += 0x141c) {
                    float dx, dy, dz;
                    bool hit = false;
                    if (leader == 0) {
                        int ps = DAT_80611d78 + pOff;
                        dx = *(float*)(ps + 0xb4) - m[0]; dy = *(float*)(ps + 0xb8) - m[1]; dz = *(float*)(ps + 0xbc) - m[2];
                        hit = dz*dz + dx*dx + dy*dy < FLOAT_8061b3fc;
                    } else {
                        dx = *(float*)(grpBase + 0x35fc) - m[0]; dy = *(float*)(grpBase + 0x3600) - m[1]; dz = *(float*)(grpBase + 0x3604) - m[2];
                        hit = dz*dz + dx*dx + dy*dy < FLOAT_8061b3fc;
                    }
                    if (hit) { m[0xd] = (float)((int)m[0xd] - dt); break; }
                }
            } else if (*(int*)(grpBase + 8) == 2) {
                m[0xd] = (float)((int)m[0xd] - dt);
            }

            // ttl expired -> begin the splash/settle sequence
            if ((int)m[0xd] < 1 && m[0xb] == 0.0f) {
                m[0xb] = 1.4013e-45f;                     // state = 1 (settling)
                float ex, ey, ez;
                if (leader == 0 && *(int*)(grpBase + 0x35f8) < 1) {
                    float best = FLOAT_8061b3cc; ex = ey = ez = 0.0f;
                    for (int pOff = 0, p = DAT_80612610; p > 0; --p, pOff += 0x141c) {
                        int ps = DAT_80611d78 + pOff;
                        float dx = *(float*)(ps + 0xb4) - m[0];
                        float dy = *(float*)(ps + 0xb8) - m[1];
                        float dz = *(float*)(ps + 0xbc) - m[2];
                        float d2 = dz*dz + dx*dx + dy*dy;
                        if (d2 < best) { best = d2; ez = dz; ey = dy; ex = dx; }
                    }
                } else {
                    ex = *(float*)(grpBase + 0x35fc) - m[0];
                    ey = *(float*)(grpBase + 0x3600) - m[1];
                    ez = *(float*)(grpBase + 0x3604) - m[2];
                }

                float dir[3] = { -m[6], -m[7], -m[8] };
                int tex = sprite_get_texture_id(0x2e1);
                particle_spawn_impact_from_ray(tex, 1, 0x7f7f7f7f, 0, 1, m[10], m, dir);

                m[6] = (float)((double)m[6] + (double)ex);
                m[7] = (float)((double)m[7] + (double)ey) + FLOAT_8061b388;
                m[8] = (float)((double)m[8] + (double)ez);
                float len2 = m[8]*m[8] + m[6]*m[6] + m[7]*m[7];
                if (FLOAT_8061b394 < len2) {
                    float r = 1.0f / SQRT(len2);
                    r = r * -(FLOAT_8061b398 * len2 * r * r - FLOAT_8061b39c);
                    float sc = FLOAT_8061b388 * r * -(FLOAT_8061b398 * len2 * r * r - FLOAT_8061b39c);
                    m[6] *= sc; m[7] *= sc; m[8] *= sc;
                }
                float boost = FLOAT_806112f8 * (FLOAT_8061b388 + (float)rng_counter_tick() / FLOAT_8061b3a4);
                m[6] *= boost; m[7] *= boost; m[8] *= boost;

                double horiz = math_sqrt((double)(m[6]*m[6] + m[8]*m[8]));
                m[3] = FLOAT_8061b380;
                m[5] = -(float)math_atan2((double)m[7], horiz);
                m[4] = FLOAT_8061b400 + (float)math_atan2((double)m[6], (double)m[8]);
                float t = FLOAT_8061b3ec * invBase;
                m[0xd] = (float)(int)((double)t * (double)FLOAT_8061b398 + (double)((float)t * ((float)rng_counter_tick() / FLOAT_8061b3a4)));
            } else if (m[0xb] == 2.8026e-45f) {
                // ---- state 2: falling/splash timer ----
                m[0xe] = -((FLOAT_8061b388 / (float)base) * FLOAT_806108ec - m[0xe]);
                float pos = m[0x10];
                particle_cached_set_position((int*)&pos, m);
                if (m[0xe] < FLOAT_8061b380) {
                    float splash[3] = { DAT_803e0cb8, DAT_803e0cbc, DAT_803e0cc0 };
                    float rot[16];
                    mtx4_set_rotation_zyx((double)m[3], (double)m[4], (double)m[5], rot);
                    mtx4_transform_vector(rot, splash);
                    float dir[3] = { -splash[0], -splash[1], -splash[2] };
                    int tex = sprite_get_texture_id(0x337);
                    particle_spawn_impact_from_ray(tex, 1, 0x5f000020, 0, 1, m[10], m, dir);
                    particle_emitter_spawn(0, grpBase + 0x34ec, 1, m, splash);
                    unsigned r = rng_counter_tick();
                    int sfx[7]; sound_init_play_params((int)sfx);
                    *(char*)((int)sfx + 0x18) = 1;
                    *(float*)((int)sfx + 0x0c) = m[0];
                    *(float*)((int)sfx + 0x10) = m[1];
                    *(float*)((int)sfx + 0x14) = m[2];
                    sfx[0] = (int)(r & 3) + 0x156;
                    sound_play_request(sfx);
                    m[0xb] = 4.2039e-45f;                 // state = "gone"
                }
            }
        } else if (state == 1.4013e-45f) {
            // ---- state 1: leaping toward the water, then popped by a shot ----
            float g = -(FLOAT_8061b404 * (FLOAT_8061b388 / (float)base) * (float)dt - m[7]);
            m[7] = g;
            m[0] = m[6] * invBase * (float)dt + m[0];
            m[1] = m[7] * invBase * (float)dt + m[1];
            m[2] = (float)((double)m[8] * (double)(invBase * (float)dt) + (double)m[2]);
            m[5] = FLOAT_8061b3ec * (float)dt * invBase + m[5];

            for (int pOff = 0, p = 0; p < DAT_80612610; ++p, pOff += 0x141c) {
                int ps = DAT_80611d78 + pOff;
                float dx = m[0] - *(float*)(ps + 0xb4);
                float dy = m[1] - *(float*)(ps + 0xb8);
                float dz = m[2] - *(float*)(ps + 0xbc);
                if (dz*dz + dx*dx + dy*dy < FLOAT_8061b388) {
                    int impact[16];
                    int sw = switch_slot_reset((int)impact);
                    impact[0] = *(int*)(DAT_80611d78 + pOff + 0x14);
                    damage_dispatch_to_entity(sw, DAT_803e0cc4, DAT_803e0cc8, DAT_803e0ccc,
                                              FLOAT_8061b408, (int)-1, (int)-1, m, (int)0, impact);
                }
            }
            // clamp against the swarm AABB, else re-nav
            float box[6];
            flock_level_colors((int)m[10], box, box + 3);
            for (int a = 0; a < 3; ++a) {
                if ((box[a] < *(float*)((int)m + a*4) && a != 1) ||
                    *(float*)((int)m + a*4) < *(float*)((int)box + a*4 + 0xc)) {
                    if (a == 1) m[9] = 0.0f; else flock_nav_update();
                    m[0xb] = 0.0f;
                }
            }
        }
    }

    // members past the live window: bump their subgroup free counter
    {
        int scanned = count;
        int tail = 800 - scanned;
        int off = scanned * 0x4c;
        for (int i = 0; i < tail; ++i, off += 0x4c) {
            if (*(int*)(*(int*)DAT_80612ec4 + off + 0x2c) != 3) {
                int gi = *(int*)(*(int*)DAT_80612ec4 + off + 0x44) * 0x63c + 0x4c;
                *(int*)(grpBase + gi) += 1;
            }
        }
    }

    // finalise each subgroup's averaged colour
    for (int o = 0, i = 0; i < 8; ++i, o += 0x63c) {
        int c = *(int*)(grpBase + o + 0x4c);
        if (c > 0) {
            float inv = FLOAT_8061b388 / (float)c;
            *(float*)(grpBase + o + 0x24) = *(float*)(grpBase + o + 0x3c) * inv;
            *(float*)(grpBase + o + 0x28) = *(float*)(grpBase + o + 0x40) * inv;
            *(float*)(grpBase + o + 0x2c) = *(float*)(grpBase + o + 0x44) * inv;
        }
    }

    // (re)bind the render callback on the 7 nodes
    for (int o = 0, i = 0; i < 7; ++i, o += 4) {
        int rd = scene_node_get_render_data(*(int*)((int)&DAT_80495c38 + o));
        *(unsigned int*)(rd + 0x28) |= 0x10;
        rd = scene_node_get_render_data(*(int*)((int)&DAT_80495c38 + o));
        entity_set_render_type(rd, (void*)&flock_member_update);
    }

    FUN_80382720();
}

// ---------------------------------------------------------------------------
// flock_render 0x8036deb0
// Two-pass draw. If any node is visible: first pass draws the "solid" members
// additively with a shared material, second pass sets up the environment-texture
// matrix share and draws the settling (state==1) members.
// ---------------------------------------------------------------------------
void flock_render(void)
{
    byte vis = 0;
    if (*(int*)(DAT_80612ec0 + 0x3350) == 0) return;

    for (int i = 0, o = 0; i < 7; ++i, o += 4) {
        int node = *(int*)((int)&DAT_80495c38 + o);
        vis |= *(byte*)(DAT_80611d54 + node + 0xd2c);
    }
    if (vis == 0) return;

    unsigned savedFog = gx_apply_fog_mode(0);
    gx_set_blend_tag(0, 0);
    gx_force_state_flush();
    int savedMat = gx_set_sub_material_id(1);
    gx_bind_material_fallback();

    // pass 1: non-settling, non-gone members
    for (int i = 0, o = 0; i < *(int*)(DAT_80612ec0 + 4); ++i, o += 0x4c) {
        float* m = (float*)(*(int*)DAT_80612ec4 + o);
        if (m[0xb] != 1.4013e-45f && m[0xb] != 4.2039e-45f) flock_member_draw(m);
    }

    // pass 2: env-texture share for the settling members
    mtx4_transpose(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;
    gx_set_tex_matrix_share(1);
    gx_set_fog_params(0x43c);
    for (int i = 0, o = 0; i < *(int*)(DAT_80612ec0 + 4); ++i, o += 0x4c) {
        float* m = (float*)(*(int*)DAT_80612ec4 + o);
        if (m[0xb] == 1.4013e-45f) flock_member_draw(m);
    }

    gx_set_sub_material_id(savedMat);
    gx_apply_fog_mode(savedFog);
}

// ---------------------------------------------------------------------------
// flock_master_update 0x8036e03c   (caller: worm_master_update)
// Top-level per-frame tick. Counts fully-settled members, records the running
// live count into a 30-entry history ring, grows or shrinks the active-member
// window based on player count / game mode, spawns a splash + sound for members
// that are far enough out, then dispatches the two per-member sub-passes. When
// every member is gone it reports the class-complete stat and stops the sound.
// ---------------------------------------------------------------------------
void flock_master_update(double param_1, double param_2, double param_3, double param_4,
                         double param_5, double param_6, double param_7, double param_8)
{
    byte vis = 0;
    unsigned settled = 0;

    FUN_803826e4();
    if (*(int*)(DAT_80612ec0 + 0x3350) == 0) { FUN_80382730(); return; }

    flock_update_centroid_sound();
    int grpBase = DAT_80612ec0;
    int dt      = DAT_806108e4;

    for (int i = 0, o = 0; i < 7; ++i, o += 4) {
        int node = *(int*)((int)&DAT_80495c38 + o);
        vis |= *(byte*)(DAT_80611b1c + node);
    }
    if (vis == 0) { FUN_80382730(); return; }

    // count members in terminal state 3
    for (int o = 0, n = *(int*)(DAT_80612ec0 + 4); n > 0; --n, o += 0x4c) {
        if (*(int*)(*(int*)DAT_80612ec4 + o + 0x2c) == 3) settled++;
    }

    // push the running live count into the 30-entry ring
    int hi = DAT_80612ec8 + 1;
    *(int*)(&DAT_805ecaf8 + DAT_80612ec8 * 4) = DAT_806108e4;
    DAT_80612ec8 = hi;
    if (hi > 0x1d) DAT_80612ec8 = 0;

    // decide whether to grow or shrink the active window
    bool grow = true;
    for (int b = DAT_806109c0; b > 0; --b) {
        bool split = (DAT_805015bd == '\n') && (1 < DAT_80612610);
        if ((int)(2 - (unsigned)!split) < dt) grow = false;
    }
    bool split = (DAT_805015bd == '\n') && (1 < DAT_80612610);
    if ((int)(2 - (unsigned)!split) < dt) {
        int v = *(int*)(grpBase + 4) - 8;
        *(int*)(grpBase + 4) = (v < 0x4b) ? 0x4b : v;
    } else if (grow && *(int*)(grpBase + 4) < 800) {
        int n = *(int*)(grpBase + 4);
        for (int k = 0; k < 4 && k < 800 - n; ++k) {
            int* nm = (int*)(*(int*)DAT_80612ec4 + *(int*)(DAT_80612ec0 + 4) * 0x4c);
            int gi = DAT_80612ec0 + nm[0x11] * 0x63c;
            int off = *(int*)(gi + 0x50) - nm[0x12];
            if (off < 0) off += DAT_806109c0 * 2;
            off *= 0xc;
            nm[0] = *(int*)(gi + off + 0x5c);
            nm[1] = *(int*)(DAT_80612ec0 + nm[0x11] * 0x63c + off + 0x60);
            nm[2] = *(int*)(DAT_80612ec0 + nm[0x11] * 0x63c + off + 0x64);
            *(int*)(DAT_80612ec0 + 4) += 1;
        }
    }

    if (settled == 800) {
        statlvl_class_complete((double)FLOAT_8061b380, param_2, param_3, param_4,
                               param_5, param_6, param_7, param_8);
        *(int*)(DAT_80612ec0 + 0x3350) = 0;
        sound_stop_handle();
        *(int*)(DAT_80612ec0 + 0x3618) = -1;
    } else {
        // spawn a splash + sound for far-out members, then run the sub-passes
        double thresh = (double)FLOAT_8061b40c;
        for (int o = 0, i = 0; i < *(int*)(DAT_80612ec0 + 4); ++i, o += 0x4c) {
            float* m = (float*)(*(int*)DAT_80612ec4 + o);
            if (thresh < (double)(float)settled && m[0xb] == 0.0f) {
                float splash[3] = { (float)DAT_803e0cd0, (float)DAT_803e0cd4, (float)DAT_803e0cd8 };
                particle_emitter_spawn(0, DAT_80612ec0 + 0x34ec, 1, m, splash);
                m[0xb] = 4.2039e-45f;
                unsigned r = rng_counter_tick();
                int sfx[7]; sound_init_play_params((int)sfx);
                *(char*)((int)sfx + 0x18) = 1;
                *(float*)((int)sfx + 0x0c) = m[0];
                *(float*)((int)sfx + 0x10) = m[1];
                *(float*)((int)sfx + 0x14) = m[2];
                sfx[0] = (int)(r & 3) + 0x156;
                sound_play_request(sfx);
            }
        }
        flock_update_subgroups();
        flock_update_impact_members();
    }

    FUN_80382730();
}

// ---------------------------------------------------------------------------
// flock_level_init 0x8036e3dc   (caller: worm_level_init)
// One-time construction of the swarm for a level. Clears both tables, seeds the
// 8 sub-group records (position/colour from the seed vectors, first-3 get a
// nav-region), then seeds all 800 member records with a random start pose, a
// random velocity biased onto the region plane, and randomised ttl/phase. When
// the last member is placed it (re)binds the render callbacks, builds the two
// GX spark fans drawn once, and creates the trailing splash/spark/ripple
// particle emitters used by the runtime passes.
// ---------------------------------------------------------------------------
void flock_level_init(void)
{
    int    grpBase;
    int    base = DAT_806109c0;
    float  seedA0, seedA1, seedA2, seedB0, seedB1, seedB2;

    FUN_803826d8();

    // seed vectors copied out of rodata into locals
    seedA0 = (float)DAT_803e0cdc; seedA1 = (float)DAT_803e0ce0; seedA2 = (float)DAT_803e0ce4;
    float subShape[9];   // 3 subgroup shape vectors (from DAT_803e0cf0.. / DAT_803e0d14..)
    for (int i = 0; i < 9; ++i) subShape[i] = *(float*)((int)&DAT_803e0cf0 + i * 4);
    float subCol[3]  = { (float)DAT_803e0d3c, (float)DAT_803e0d40, (float)DAT_803e0d44 };
    seedB0 = (float)DAT_803e0d48; seedB1 = (float)DAT_803e0d4c; seedB2 = (float)DAT_803e0d50;

    int mesh = scene_node_get_render_data(0x28);
    unsigned vertCount = *(unsigned*)(mesh + 0x1e4);
    int      vertBase  = *(int*)(mesh + 500);

    memset_wrapper(DAT_80612ec0, 0, 0x361c);
    memset_wrapper(*(int*)DAT_80612ec4, 0, 0xed80);
    *(int*)(DAT_80612ec0 + 8) = 0;
    *(int*)(DAT_80612ec0 + 0x3618) = -1;

    // seed the 8 sub-group records
    int gOff = 0;
    for (int g = 0; g < 8; ++g) {
        *(int*)(DAT_80612ec0 + gOff + 0x624) = 0x28;
        unsigned r = rng_counter_tick();
        *(int*)(DAT_80612ec0 + gOff + 0x638) = *(int*)(vertBase + (r - (r / vertCount) * vertCount) * 4);
        *(int*)(DAT_80612ec0 + gOff + 0x620) = 0;
        *(float*)(DAT_80612ec0 + gOff + 0x18) = FLOAT_8061b388;
        *(float*)(DAT_80612ec0 + gOff + 0x34) = FLOAT_8061b388;
        if (g < 3) {
            *(float*)(DAT_80612ec0 + gOff + 0x0c)  = subShape[g*3 + 0];
            *(float*)(DAT_80612ec0 + gOff + 0x10)  = subShape[g*3 + 1];
            *(float*)(DAT_80612ec0 + gOff + 0x14)  = subShape[g*3 + 2];
            *(float*)(DAT_80612ec0 + gOff + 0x5fc) = subShape[g*3 + 0];
            *(float*)(DAT_80612ec0 + gOff + 0x600) = subShape[g*3 + 1];
            *(float*)(DAT_80612ec0 + gOff + 0x604) = subShape[g*3 + 2];
            *(float*)(DAT_80612ec0 + gOff + 0x18)  = subShape[g*3 + 0];  // seed pose
            *(float*)(DAT_80612ec0 + gOff + 0x1c)  = subShape[g*3 + 1];
            *(float*)(DAT_80612ec0 + gOff + 0x20)  = subShape[g*3 + 2];
            *(float*)(DAT_80612ec0 + gOff + 0x624) = subCol[g];
            if (g != 0) flock_nav_update();
        } else {
            *(float*)(DAT_80612ec0 + gOff + 0x0c) = subShape[8];  // last shape reused as default
            *(float*)(DAT_80612ec0 + gOff + 0x10) = seedA1;
            *(float*)(DAT_80612ec0 + gOff + 0x14) = seedA2;
        }
        *(int*)(DAT_80612ec0 + gOff + 0x24) = *(int*)(DAT_80612ec0 + gOff + 0x0c);
        *(int*)(DAT_80612ec0 + gOff + 0x28) = *(int*)(DAT_80612ec0 + gOff + 0x10);
        *(int*)(DAT_80612ec0 + gOff + 0x2c) = *(int*)(DAT_80612ec0 + gOff + 0x14);
        *(int*)(DAT_80612ec0 + gOff + 0x4c) = 0;
        *(int*)(DAT_80612ec0 + gOff + 0x54) = -1;
        *(float*)(DAT_80612ec0 + gOff + 0x58) = FLOAT_8061b388;
        gOff += 0x63c;
    }

    // seed the members (up to 800; the loop finalises on the 800th)
    int mOff = 0;
    int placed = 0;
    while (true) {
        float* m = (float*)(*(int*)DAT_80612ec4 + mOff);

        // random spawn scale factors
        float s0 = (float)rng_counter_tick() / FLOAT_8061b3a4;
        float s1 = ((float)rng_counter_tick() / FLOAT_8061b3a4) * s0;
        unsigned rg = rng_counter_tick();
        m[0x11] = (float)(unsigned)(1 < rg % 3);      // subgroup index 0/1

        float* sg = (float*)(DAT_80612ec0 + (int)m[0x11] * 0x63c + 0xc);
        m[0] = sg[0]; m[1] = sg[1]; m[2] = sg[2];
        m[9]  = sg[0x185 - 0]; // region flag from subgroup (see runtime layout)
        m[0xf] = sg[0x18b];
        m[10]  = sg[0x186];

        // pick a random velocity, either free or biased onto the region plane
        int tries = 8;
        if (FLOAT_8061b388 == sg[10]) {
            m[7] = FLOAT_8061b380;
            do {
                m[6] = (float)rng_counter_tick() / FLOAT_8061b3a4;
                m[8] = (float)rng_counter_tick() / FLOAT_8061b3a4;
                if (--tries == 0) break;
            } while ((double)(m[8]*m[8] + m[6]*m[6] + m[7]*m[7]) <= (double)FLOAT_8061b410);
        } else {
            do {
                m[6] = FLOAT_8061b3a0 * ((float)rng_counter_tick() / FLOAT_8061b3a4 - FLOAT_8061b398);
                m[7] = FLOAT_8061b3a0 * ((float)rng_counter_tick() / FLOAT_8061b3a4 - FLOAT_8061b398);
                m[8] = FLOAT_8061b3a0 * ((float)rng_counter_tick() / FLOAT_8061b3a4 - FLOAT_8061b398);
                if (--tries == 0) break;
            } while ((double)(m[8]*m[8] + m[6]*m[6] + m[7]*m[7]) <= (double)FLOAT_8061b410);
            // remove the component along the region up-axis (sg[9..0xb])
            float d = -(sg[0xb]*m[8] + sg[9]*m[6] + sg[10]*m[7]);
            m[6] = sg[9]*d + m[6];
            m[7] = sg[10]*d + m[7];
            m[8] = sg[0xb]*d + m[8];
        }
        // normalise velocity
        float len2 = m[8]*m[8] + m[6]*m[6] + m[7]*m[7];
        if (FLOAT_8061b394 < len2) {
            float r = 1.0f / SQRT(len2);
            r = r * -(FLOAT_8061b398 * len2 * r * r - FLOAT_8061b39c);
            float sc = FLOAT_8061b388 * r * -(FLOAT_8061b398 * len2 * r * r - FLOAT_8061b39c);
            m[6] *= sc; m[7] *= sc; m[8] *= sc;
        }

        m[0xc] = 0x7f7f7f7f;   // argb
        m[0xb] = 0;            // state
        m[3] = FLOAT_8061b380; m[4] = FLOAT_8061b380; m[5] = FLOAT_8061b380;
        int tri = (int)m[0xf];
        m[4] = (float)math_atan2((double)(*(float*)(tri + 0x1c) - *(float*)(tri + 0x10)),
                                 (double)(*(float*)(tri + 0x14) - *(float*)(tri + 8)));
        m[0xd] = (float)(int)((float)(FLOAT_8061b3ec * (float)base) * ((float)rng_counter_tick() / FLOAT_8061b3a4) + FLOAT_8061b3a0);
        m[0x12] = (float)(int)((float)(FLOAT_8061b3a0 * s1) * (float)base);
        sg[0x10] = sg[0x10] + 1;

        placed++;
        mOff += 0x4c;
        if (placed >= 800) {
            // ---- finalise: render callbacks + one-shot GX spark fans + emitters
            for (int i = 0, o = 0; i < 7; ++i, o += 4) {
                int rd = scene_node_get_render_data(*(int*)((int)&DAT_80495c38 + o));
                *(unsigned int*)(rd + 0x28) |= 0x10;
                rd = scene_node_get_render_data(*(int*)((int)&DAT_80495c38 + o));
                entity_set_render_type(rd, (void*)&flock_member_update);
            }

            // build the spark ring vertex scratch (inner ring + outer fan)
            grpBase = DAT_80612ec0;
            float ang0 = FLOAT_8061b3c4;
            for (int i = 0, o = 0; i < 5; ++i, o += 0x10) {
                float t   = (float)((double)FLOAT_8061b3e4 * (double)(float)(-(float)i) / (double)FLOAT_8061b414);
                float c0  = (float)((double)FLOAT_8061b390 * math_poly_trig((double)t));
                float c1  = (float)((double)FLOAT_8061b390 * math_poly_trig((double)(t - FLOAT_8061b400)));
                *(float*)((int)&DAT_805ecb70 + o) = c0;
                *(float*)((int)&DAT_805ecb74 + o) = c1;
                *(float*)((int)&DAT_805ecb78 + o) = ang0;
                *(float*)((int)&DAT_805ecb70 + o + 0xc) = FLOAT_8061b388;
                float e0 = (float)((double)FLOAT_8061b42c * math_poly_trig((double)t));
                float e1 = (float)((double)FLOAT_8061b42c * math_poly_trig((double)(t - FLOAT_8061b400)));
                *(float*)((int)&DAT_805ecbc0 + o) = e0;
                *(float*)((int)&DAT_805ecbc4 + o) = e1;
                *(float*)((int)&DAT_805ecbc8 + o) = FLOAT_8061b390;
                *(float*)((int)&DAT_805ecbc8 + o + 4) = FLOAT_8061b388;
            }

            *(float*)(DAT_80612ec0 + 0x3348) = FLOAT_8061b38c;
            *(int*)(DAT_80612ec0 + 0x334c)   = 0;

            gx_alloc_batch_page(0, 0, 0, 0, 0, 0, 0, 0, (unsigned int*)(DAT_80612ec0 + 0x3340),
                                1, 0xffe00000, DAT_80612ec0, 0, 0, grpBase, 0);
            // fan 1: inner ring, additive
            gx_begin_textured_batch(2, 0x92, 0, 0xc, -8);
            for (int i = 0, o = 0; i < 5; ++i, o += 0x10) {
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecb70 + o);
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecb74 + o);
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecb78 + o);
                DAT_cc008000 = 0x7f7f7f7f;
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecb70 + o);
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecb74 + o);
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecbc0 + o);
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecbc4 + o);
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecbc8 + o);
                DAT_cc008000 = 0x4f4f4f7f;
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecbc0 + o);
                DAT_cc008000 = *(unsigned int*)((int)&DAT_805ecbc4 + o);
            }
            // wrap vertex (close the fan)
            DAT_cc008000 = DAT_805ecb70; DAT_cc008000 = DAT_805ecb74; DAT_cc008000 = DAT_805ecb78;
            DAT_cc008000 = 0x7f7f7f7f;
            DAT_cc008000 = DAT_805ecb70; DAT_cc008000 = DAT_805ecb74;
            DAT_cc008000 = DAT_805ecbc0; DAT_cc008000 = DAT_805ecbc4; DAT_cc008000 = DAT_805ecbc8;
            DAT_cc008000 = 0x4f4f4f7f;
            DAT_cc008000 = DAT_805ecb70; DAT_cc008000 = DAT_805ecb74;
            gx_end_batch();

            // fan 2: outer ring by index fan
            gx_begin_textured_batch(2, 0x92, 0, 6, -8);
            for (int i = 0, o = 0; i < 6; ++i, o += 4) {
                int idx = *(int*)((int)&DAT_80495cd8 + o);
                DAT_cc008000 = (&DAT_805ecbc0)[idx * 4];
                DAT_cc008000 = (&DAT_805ecbc4)[idx * 4];
                DAT_cc008000 = (&DAT_805ecbc8)[idx * 4];
                DAT_cc008000 = 0x4f4f4f7f;
                DAT_cc008000 = (&DAT_805ecb70)[idx * 4];
                DAT_cc008000 = (&DAT_805ecb74)[idx * 4];
            }
            int page = gx_end_batch();
            gx_swap_batch_page(page, 0, 0, 0, 0, 0, 0, 0);

            // trailing splash emitter
            particle_def_create((double)FLOAT_8061b414, (double)FLOAT_8061b388, DAT_80612ec0 + 0x31f0, 5, 0, 1, 0x3b3, 0);
            particle_def_set_size_keyframes((double)FLOAT_8061b418, (double)FLOAT_8061b380, (double)FLOAT_8061b380, (double)FLOAT_8061b418, 0x175b5a00, 0x175b5a00, 0);
            particle_def_set_velocity_ranges((double)FLOAT_8061b41c, (double)FLOAT_8061b380, (double)FLOAT_8061b390, (double)FLOAT_8061b380, (double)FLOAT_8061b388, (double)FLOAT_8061b398);
            particle_def_set_color_end((double)FLOAT_8061b380, (double)FLOAT_8061b388, (double)FLOAT_8061b388, (double)FLOAT_8061b380);
            particle_def_set_size_start_end((double)FLOAT_8061b380, (double)FLOAT_8061b3e4, (double)FLOAT_8061b380, (double)FLOAT_8061b380);
            particle_def_set_spawn_volume(&seedA0, &seedB0, &seedA0, &seedA0);
            particle_emitter_create((double)FLOAT_8061b420, (double)FLOAT_8061b3a0, (double)FLOAT_8061b424, DAT_80612ec0 + 0x32bc, DAT_80612ec0 + 0x31f0, 0, 0x8007);
            int em = particle_emitter_set_color_range((double)FLOAT_8061b380, (double)FLOAT_8061b380, (double)FLOAT_8061b428, (double)FLOAT_8061b380);
            int ent = entity_spawn_by_type(em, (double)FLOAT_8061b380, (double)FLOAT_8061b428, (double)FLOAT_8061b380,
                                           (double)FLOAT_8061b388, (double)FLOAT_8061b398, 0, 0, 0x1bc, &seedA0, &seedA0, 0, 0);
            *(int*)(DAT_80612ec0 + 0x31ec) = ent;
            *(unsigned int*)(*(int*)(DAT_80612ec0 + 0x31ec) + 0x28) &= 0xffdffffe;

            // ripple ring geometry (5 samples), then two more emitters
            for (int i = 0, o = 0; i < 5; ++i, o += 0x10) {
                float t  = (float)((double)(float)(FLOAT_8061b3e4 * (float)(-(float)i)) / (double)FLOAT_8061b414);
                float td = (float)(t - FLOAT_8061b400);
                *(float*)((int)&DAT_805ecb70 + o) = (float)((double)FLOAT_8061b390 * math_poly_trig((double)t));
                *(float*)((int)&DAT_805ecb74 + o) = (float)((double)FLOAT_8061b390 * math_poly_trig((double)td));
                *(float*)((int)&DAT_805ecb78 + o) = FLOAT_8061b3c4;
                *(float*)((int)&DAT_805ecb70 + o + 0xc) = FLOAT_8061b388;
                *(float*)((int)&DAT_805ecbc0 + o) = (float)((double)FLOAT_8061b42c * math_poly_trig((double)t));
                *(float*)((int)&DAT_805ecbc4 + o) = (float)((double)FLOAT_8061b42c * math_poly_trig((double)td));
                *(float*)((int)&DAT_805ecbc8 + o) = FLOAT_8061b390;
                *(float*)((int)&DAT_805ecbc8 + o + 4) = FLOAT_8061b388;
            }

            particle_def_create((double)FLOAT_8061b398, (double)FLOAT_8061b388, DAT_80612ec0 + 0x3354, 0, 0, 1, 0x34e, 0);
            particle_def_set_size_keyframes((double)FLOAT_8061b388, (double)FLOAT_8061b380, (double)FLOAT_8061b380, (double)FLOAT_8061b380, 0x4f000000, 0x10000000, 0x7030100);
            particle_def_set_velocity_ranges((double)FLOAT_8061b3c4, (double)FLOAT_8061b380, (double)FLOAT_8061b3d4, (double)FLOAT_8061b380, (double)FLOAT_8061b388, (double)FLOAT_8061b388);
            particle_def_set_size_start_end((double)FLOAT_8061b380, (double)FLOAT_8061b380, (double)FLOAT_8061b380, (double)FLOAT_8061b388);
            particle_def_set_color_end((double)FLOAT_8061b380, (double)FLOAT_8061b380, (double)FLOAT_8061b3ec, (double)FLOAT_8061b380);
            particle_emitter_create((double)FLOAT_8061b3a0, (double)FLOAT_8061b420, (double)FLOAT_8061b3d4, DAT_80612ec0 + 0x34ec, DAT_80612ec0 + 0x3354, 0, 1);
            particle_emitter_set_color_range((double)FLOAT_8061b380, (double)FLOAT_8061b380, (double)FLOAT_8061b430, (double)FLOAT_8061b380);

            particle_def_create((double)FLOAT_8061b3d4, (double)FLOAT_8061b388, DAT_80612ec0 + 0x3420, 0, 1, 1, 0x302, 0);
            particle_def_set_size_keyframes((double)FLOAT_8061b388, (double)FLOAT_8061b380, (double)FLOAT_8061b380, (double)FLOAT_8061b380, 0x7f7f7f00, 0x7f7f7f00, 0);
            particle_def_set_velocity_ranges((double)FLOAT_8061b3c4, (double)FLOAT_8061b380, (double)FLOAT_8061b3d4, (double)FLOAT_8061b380, (double)FLOAT_8061b388, (double)FLOAT_8061b388);
            particle_def_set_size_start_end((double)FLOAT_8061b380, (double)FLOAT_8061b434, (double)FLOAT_8061b438, (double)FLOAT_8061b43c);
            particle_def_set_color_end((double)FLOAT_8061b380, (double)FLOAT_8061b380, (double)FLOAT_8061b440, (double)FLOAT_8061b380);
            particle_emitter_create((double)FLOAT_8061b380, (double)FLOAT_8061b3c4, (double)FLOAT_8061b398, DAT_80612ec0 + 0x3570, DAT_80612ec0 + 0x3420, 0, 1);
            particle_emitter_set_color_range((double)FLOAT_8061b3a0, (double)FLOAT_8061b380, (double)FLOAT_8061b430, (double)FLOAT_8061b380);

            FUN_80382724();
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// flock_build_region_bounds 0x8036f138
// Build the per-node navigation-region AABBs. For each of the 7 render nodes it
// walks the mesh vertices to establish the loose min/max box, then tightens it
// against every collision-triangle vertex, storing lo(+4) and hi(+0x10) per node
// in the freshly-allocated member/region scratch tables.
// ---------------------------------------------------------------------------
int flock_build_region_bounds(int ctx, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                              int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    DAT_80612ec0 = mem_alloc(ctx, a2, a3, a4, a5, a6, a7, a8, 0x3620, 1, a11, a12, a13, a14, a15, a16);
    DAT_80612ec4 = mem_alloc(ctx, a2, a3, a4, a5, a6, a7, a8, 0xb0, 1, a11, a12, a13, a14, a15, a16);
    memset_wrapper((int)DAT_80612ec4, 0, 0xac);
    int membuf = mem_alloc_aligned(ctx, a2, a3, a4, a5, a6, a7, a8, 0x20, 0xed80, 1, a12, a13, a14, a15, a16);
    *(int*)DAT_80612ec4 = membuf;

    float bigLo = FLOAT_8061b3cc;   // +INF-ish
    float bigHi = FLOAT_8061b3d0;   // -INF-ish

    int nodeIdx = 0, boxOff = 0, o = 0;
    do {
        int mesh = scene_node_get_render_data(*(int*)((int)&DAT_80495c38 + o));
        float* lo = (float*)((int)DAT_80612ec4 + boxOff + 4);
        float* hi = (float*)((int)DAT_80612ec4 + boxOff + 0x10);
        lo[0] = bigLo; lo[1] = bigLo; lo[2] = bigLo;
        hi[0] = bigHi; hi[1] = bigHi; hi[2] = bigHi;

        // pass 1: loose box from the mesh verts (each vert has lo@+8 / hi@+0x14)
        for (int v = 0, vo = 0; v < *(int*)(mesh + 0x1e4); ++v, vo += 4) {
            int vp = *(int*)(*(int*)(mesh + 500) + vo);
            for (int axis = 0; axis < 3; ++axis) {
                float loV = *(float*)(vp + 8 + axis*4);
                float hiV = *(float*)(vp + 0x14 + axis*4);
                float cur = lo[axis];
                lo[axis] = (loV <= cur) ? ((loV < hiV) ? loV : hiV) : ((cur < hiV) ? cur : hiV);
                cur = hi[axis];
                hi[axis] = (cur <= loV) ? ((hiV < loV) ? loV : hiV) : ((hiV < cur) ? cur : hiV);
            }
        }

        // pass 2: tighten against every collision-triangle vertex
        for (int t = 0, to = 0; t < *(int*)(mesh + 0x1e0); ++t, to += 4) {
            unsigned* tri = *(unsigned**)(*(int*)(mesh + 0x1f0) + to);
            int vbo = 0;
            for (unsigned vi = 0; vi < *tri; ++vi, vbo += 0x18) {
                for (int axis = 0; axis < 3; ++axis) {
                    float p = *(float*)((int)tri + axis*4 + 0x24 + vbo);
                    if (lo[axis] < p) p = lo[axis];
                    lo[axis] = p;
                    p = *(float*)((int)tri + axis*4 + 0x24 + vbo);
                    if (p < hi[axis]) p = hi[axis];
                    hi[axis] = p;
                }
            }
        }
        nodeIdx++; o += 4; boxOff += 0x18;
    } while (nodeIdx < 7);

    (void)a9; (void)a10;
    return DAT_80612ec0;
}

// ---------------------------------------------------------------------------
// flock_build_region_bounds_gated 0x8036f488
// Level-load hook: only builds the swarm region bounds (and its worm pool) when
// the swarm feature is enabled for the level.
// ---------------------------------------------------------------------------
void flock_build_region_bounds_gated(int ctx, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                                     int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    if (DAT_80612614 == 1) {
        int r = flock_build_region_bounds(ctx, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
        worm_pool_alloc(r, a2, a3, a4, a5, a6, a7, a8);
    }
}

// ---------------------------------------------------------------------------
// flock_hit_scan_members 0x8036f4b8
// Bullet / ray hit-scan against the settling (state==1) members: for each such
// member whose facing faces the ray and lies within the hit radius, spawn the
// pop splash + a random splash sound and mark the member "gone". The ray origin
// and direction come back from the scratch-enter helper (packed into the return
// register pair on GameCube).
// ---------------------------------------------------------------------------
void flock_hit_scan_members(void)
{
    unsigned long long packed = (unsigned long long)FUN_803826e4();
    float* rayDir = (float*)(unsigned)(packed >> 0x20);
    float* rayPos = (float*)(unsigned)packed;

    for (int i = 0, o = 0; i < *(int*)(DAT_80612ec0 + 4); ++i, o += 0x4c) {
        float* m = (float*)(*(int*)DAT_80612ec4 + o);
        if (m[0xb] == 1.4013e-45f) {
            float facing = rayPos[2]*m[2] + rayPos[0]*m[0] + rayPos[1]*m[1];
            float dx = rayDir[0] - m[0], dy = rayDir[1] - m[1], dz = rayDir[2] - m[2];
            if (FLOAT_8061b380 < facing && dz*dz + dx*dx + dy*dy < FLOAT_8061b388) {
                particle_emitter_spawn(0, DAT_80612ec0 + 0x34ec, 1, m, m + 6);
                m[0xb] = 4.2039e-45f;
                unsigned r = rng_counter_tick();
                int sfx[7]; sound_init_play_params((int)sfx);
                *(char*)((int)sfx + 0x18) = 1;
                *(float*)((int)sfx + 0x0c) = m[0];
                *(float*)((int)sfx + 0x10) = m[1];
                *(float*)((int)sfx + 0x14) = m[2];
                sfx[0] = (int)(r & 3) + 0x156;
                sound_play_request(sfx);
            }
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// flock_sound_id_for_level 0x8036f5f0
// Map a level/actor id to the swarm ambient sound id. Only a handful of ids own
// a swarm; param2 selects between the paired sound ids (loop vs. one-shot).
// Returns 0xffffffff when the id has no swarm sound.
// ---------------------------------------------------------------------------
unsigned int flock_sound_id_for_level(int levelId, int variant)
{
    if (levelId != 0x4e4) {
        if (levelId < 0x4e4) {
            if (levelId != 0x395) {
                if (levelId < 0x395) {
                    if (levelId < 0x1eb && 0x1e8 < levelId)
                        return (variant == 0) ? 0xffffffffu : 0x5cbu;
                } else if (levelId == 0x3a1) {
                    goto hit;
                }
                return 0xffffffffu;
            }
        } else if (levelId != 0x4fb) {
            if (0x4fa < levelId) return 0xffffffffu;
            if (0x4eb < levelId) return 0xffffffffu;
            if (levelId < 0x4ea) return 0xffffffffu;
        }
    }
hit:
    return (variant == 0) ? 0x1568u : 0x1567u;
}
