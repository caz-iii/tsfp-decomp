// Module: prop_object_behavior
// TSFP GameCube main.dol non-matching decompilation.
//
// Level-prop behaviour and geometry helpers. This is the per-frame "AI" driver
// for non-weapon props (the counterpart to the AI-actor tick), the two prop
// destruction paths (timed burst / explosion), and the small family of bone /
// aim-anchor -> world-space geometry helpers those and the fire path share.
//
// Prop model record (rec = obj+0x7c) fields used here:
//   rec+0x08 : model flags (bit1 collision-active, bit2 static, bit4 damageable,
//              bit0x40 weapon, and various behaviour bits 0x20000/0x40000/...)
//   rec+0x10 : behaviour state (1 idle, 2 bounce/drift, 3 turret-aim,
//              4 return-to-rest, 9 destroyed, 0xa/0xb timed)
//   rec+0x1c/0x24/0x2c : euler orientation (pitch/yaw/roll, degrees)
//   rec+0x34..0x48     : per-axis min/max clamp limits
//   rec+0x58/0x5c/0x60 : per-axis drift velocities
//   rec+0x68 : target entity ; rec+0x6c : owning object ; rec+0x70 : bone cursor
//   rec+0xac : turret charge/cooldown timer ; rec+0xbc : sound-slot id
//   rec+0xd0 : retry counter ; rec+0xd8 : has-anim flag
//
// Batch: batch_711  |  Addresses: 0x8023e890 - 0x8024001c
// NON-MATCHING decompilation.

#include "common.h"

// Ghidra float-sqrt intrinsic (common.h owns no math intrinsics).
static inline float tsfp_sqrtf(float x) {
    float r = x;
    if (x > 0.0f) {
        r = x;
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
    }
    return r;
}
#ifndef SQRT
#define SQRT(x) tsfp_sqrtf((float)(x))
#endif

// ---------------------------------------------------------------------------
// Shared timing / math constants (file-local externs to raw symbols).
// ---------------------------------------------------------------------------
extern float  FLOAT_806108ec;   // frame delta scale
extern float  FLOAT_806188f8;   // 1.0f
extern float  FLOAT_80618908;   // deg->rad numerator (PI)
extern float  FLOAT_8061890c;   // deg->rad denominator (180)
extern float  FLOAT_80618910;   // 0.0f
extern float  FLOAT_8061891c;   // rate / 0.5f
extern float  FLOAT_80618924;   // rng range divisor
extern float  FLOAT_80618928;   // small step scale
extern float  FLOAT_8061892c;   // 1.5f (fast-inv-sqrt refine)
extern float  FLOAT_80618938;   // recoil accum step
extern float  FLOAT_8061893c;   // aim-settle epsilon / recoil scale
extern float  FLOAT_80618940;   // min-distance^2 threshold
extern float  FLOAT_80618944;   // angle-wrap low bound
extern float  FLOAT_80618948;   // 360 (angle wrap span)
extern float  FLOAT_8061894c;   // pitch-track epsilon
extern float  FLOAT_80618950;   // pitch slew rate
extern float  FLOAT_80618954;   // yaw slew rate
extern float  FLOAT_80618958;   // eye-point default Z
extern float  FLOAT_8061895c;   // weapon muzzle sentinel
extern int    DAT_806108e4;     // global frame-advance enable/step
extern int    DAT_806109c0;     // rng/frame counter (int->double magic src)
extern double DOUBLE_80618900;  // 0x43300000_00000000 bias
extern double DOUBLE_80618930;  // rng bias

extern int    DAT_80610ba0;     // slot-record table base (0x29c stride)
extern int    DAT_80611d78;     // co-op partner table base (0x141c stride)
extern int    DAT_80612610;     // co-op partner count
extern unsigned char DAT_805cb94c[]; // per-sound-slot active flags (8 stride)

// ---------------------------------------------------------------------------
// Cross-module / this-module helpers (file-local; common.h owns no raw syms).
// ---------------------------------------------------------------------------
extern int  prop_def_by_index(int idx);          // prop_table_loader.cpp
extern void prop_skeleton_build(void);           // prop_object_render.cpp
extern void prop_fire_tick(int self_unused, int owner_unused, int side, int a4,
                           int param5, int *param6); // render.cpp

extern void mtx4_set_rotation_y(double angle, float *out);
extern void mtx4_set_rotation_zyx(double rx, double ry, double rz, float *out);
extern void mtx4_transform_vector_alias(float *mtx, float *v);
extern void mtx4_transform_point_alias(float *mtx, float *v);
extern void entity_build_local_matrix(float *out, int obj, int a3, int a4);
extern unsigned int rng_counter_tick(void);
extern double math_sqrt(double x);
extern double math_atan2(double y, double x);
extern double angle_step_toward_clamped(double target, double cur, double lo, double hi);

extern void collision_result_restore(int outBuf);
extern void sound_init_play_params(int outBuf);
extern void sound_play_request(int *req);
extern int  session_get_state_flag(void);
extern int  particle_spawn_directed(int a1, int id, unsigned int flags, float *pos, unsigned int *a5);
extern int  particle_spawn_trail(int a1, int id, unsigned int flags, float *pos, float *dir);
extern void particle_slot_set_scale(double scale, int slot);
extern void particle_kill_by_entity(void);
extern void player_ai_explosion_spawn(int a1, int a2, float *pos, float *a4, int a5, int entity, int a7);
extern void entity_set_render_type(int entity, int type);
extern unsigned char *weapon_get_def_from_entity(void);
extern void weapon_fire_dispatch_callback(int self, unsigned int boneIdx, int a3, void *out);

extern void prop_read_root_anchor(int obj, float *out);   // prop_object_util.cpp (0x80240324)
extern void prop_read_root_anchor_ctx(void);              // prop_object_util.cpp (0x80240440)
extern void prop_get_world_pivot(int obj, float *out);    // prop_object_util.cpp (0x802405e8)
extern int  prop_acquire_target(void);                       // prop_target_scan.cpp (0x80240fd4)
extern int  prop_can_target(int self_unused, int target, int doRaycast, unsigned int rayMask); // (0x80241370)
extern int  prop_scan_projectile_targets(void);             // prop_target_scan.cpp (0x80241710)
extern void FUN_8025caf0(int soundSlot, int a2, int target); // spawn spawn/effect at slot
extern double FUN_80390324(double x);                // roundf
extern double FUN_80390ae4(double a, double b);      // fmodf / range-fold
extern int  __save_gpr_28(void);                     // Ghidra spill marker (self hi / cursor lo)

extern unsigned long long FUN_803826dc(void); // ai-tick work context
extern unsigned long long FUN_803826e4(void); // death-tick work context (ptr)
extern unsigned long long FUN_803826f0(void); // geometry-helper work context
extern void FUN_80382728(void);               // release ai-tick context
extern void FUN_80382730(void);               // release death-tick context
extern void FUN_8038273c(void);               // release geometry-helper context

// Forward declarations (mutual use between geometry helpers).
static void prop_bone_to_world(int obj, float *localAnchor, float *out);
static void prop_read_muzzle_anchor(void);

// 0x8023ff0c | prop_bone_to_world
// Transform a bone-local anchor point into world space: rotate the delta from
// the object base by the object's ZYX euler orientation, then by its local
// animation matrix, and add the object world position.
static void prop_bone_to_world(int obj, float *localAnchor, float *out)
{
    (void)obj;
    (void)localAnchor;
    unsigned long long ctx = __save_gpr_28();
    int self = (int)(ctx >> 0x20);
    float *anchor = (float *)ctx;
    int rec = *(int *)(self + 0x7c);

    float base[3];   // local_68/64/60
    float d[3];      // local_5c/58/54
    float m[20];     // afStack_50

    prop_read_root_anchor(self, base);
    d[0] = anchor[0] - base[0];
    d[1] = anchor[1] - base[1];
    d[2] = anchor[2] - base[2];
    mtx4_set_rotation_zyx((double)((FLOAT_80618908 * *(float *)(rec + 0x1c)) / FLOAT_8061890c),
                          (double)((FLOAT_80618908 * *(float *)(rec + 0x24)) / FLOAT_8061890c),
                          (double)((FLOAT_80618908 * *(float *)(rec + 0x2c)) / FLOAT_8061890c), m);
    mtx4_transform_vector_alias(m, d);
    entity_build_local_matrix(m, self, 0, 0);
    mtx4_transform_vector_alias(m, d);
    prop_get_world_pivot(self, base);
    out[0] = d[0] + base[0];
    out[1] = d[1] + base[1];
    out[2] = d[2] + base[2];
}

// 0x8023fac0 | prop_read_bone_anchor
// Read prop animation channel 'chan' (arg2) anchor xyz from the animated bone
// matrix table into 'out'; falls back to the default pose (FUN_80240440) when
// the channel is absent or the model has no per-channel bone data (rec+8 bit4).
void prop_read_bone_anchor(int obj, int chan, float *out)
{
    (void)obj;
    unsigned long long ctx = FUN_803826f0();
    int self = (int)(ctx >> 0x20);
    if ((*(unsigned int *)(*(int *)(self + 0x7c) + 8) & 4) != 0) {
        int def = prop_def_by_index(*(int *)(*(int *)(self + 0x7c) + 0xc));
        def = def + chan * 0xc;
        unsigned int packed = *(unsigned int *)(def + 0x3c);
        if (-1 < (int)packed) {
            int *node = *(int **)(*(int *)(self + 4) + 4);
            out[0] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                (packed & 0xffff) * 0x18);
            node = *(int **)(*(int *)(self + 4) + 4);
            packed = *(unsigned int *)(def + 0x3c);
            out[1] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                (packed & 0xffff) * 0x18 + 4);
            node = *(int **)(*(int *)(self + 4) + 4);
            packed = *(unsigned int *)(def + 0x3c);
            out[2] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                (packed & 0xffff) * 0x18 + 8);
            FUN_8038273c();
            return;
        }
    }
    prop_read_root_anchor_ctx();
    FUN_8038273c();
}

// 0x8023fbe0 | prop_read_muzzle_anchor
// Read the current fire-bone (rec+0x70 cursor) muzzle anchor into the work
// context's output slot. For a weapon-driven object it dispatches to the weapon
// callback; otherwise it reads directly from the bone matrix table (falling
// back to the default pose when the bone is absent).
static void prop_read_muzzle_anchor(void)
{
    unsigned long long ctx = FUN_803826f0();
    int self = (int)(ctx >> 0x20);
    float *out = (float *)ctx;
    int rec = *(int *)(self + 0x7c);

    if ((*(unsigned int *)(rec + 8) & 0x40) == 0) {
        if ((*(unsigned int *)(rec + 8) & 4) != 0) {
            int def = prop_def_by_index(*(int *)(rec + 0xc));
            unsigned int packed = *(unsigned int *)(def + *(int *)(rec + 0x70) * 0xc + 0x38);
            if (-1 < (int)packed) {
                int *node = *(int **)(*(int *)(self + 4) + 4);
                out[0] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                    (packed & 0xffff) * 0x18);
                node = *(int **)(*(int *)(self + 4) + 4);
                packed = *(unsigned int *)(def + *(int *)(rec + 0x70) * 0xc + 0x38);
                out[1] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                    (packed & 0xffff) * 0x18 + 4);
                node = *(int **)(*(int *)(self + 4) + 4);
                packed = *(unsigned int *)(def + *(int *)(rec + 0x70) * 0xc + 0x38);
                out[2] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                    (packed & 0xffff) * 0x18 + 8);
                FUN_8038273c();
                return;
            }
        }
        prop_read_root_anchor_ctx();
    } else {
        weapon_fire_dispatch_callback(self, *(unsigned int *)(rec + 0x70), 0, out);
    }
    FUN_8038273c();
}

// 0x8023ff0c helper wrapper (declared above); definition order keeps callers happy.

// 0x8023fd34 | prop_read_aim_anchor
// Compute the world-space aim anchor: read the prop-def aim bone (def+0x28) from
// the bone matrix table and transform it to world via prop_bone_to_world; if the
// def has no aim bone (< 0), fall back to the object world position.
void prop_read_aim_anchor(int obj, float *out)
{
    int def = prop_def_by_index(*(int *)(*(int *)(obj + 0x7c) + 0xc));
    unsigned int packed = *(unsigned int *)(def + 0x28);
    if ((int)packed < 0) {
        prop_get_world_pivot(obj, out);
    } else {
        int hi = (int)packed >> 0x10;
        int *node = *(int **)(*(int *)(obj + 4) + 4);
        int lo = (packed & 0xffff) * 0x18;
        int base0 = *node;
        float local[3];
        local[0] = *(float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo);
        local[1] = *(float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo + 4);
        local[2] = *(float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo + 8);
        prop_bone_to_world(obj, local, out);
    }
}

// 0x8023fec0 | prop_compute_muzzle_origin
// Read the muzzle anchor into the work context, then transform it to world via
// prop_bone_to_world, writing the result to 'out'.
void prop_compute_muzzle_origin(int obj, float *out)
{
    unsigned char anchor[16];
    prop_read_muzzle_anchor();
    prop_bone_to_world(obj, (float *)anchor, out);
}

// 0x8023fe0c | prop_compute_aim_origin
// Compute the muzzle/aim world origin from the prop-def aim bone (def+0x24, or
// def+0x20 when +0x24 is empty) via the object's local matrix, writing xyz into
// the work-context output slot.
void prop_compute_aim_origin(void)
{
    unsigned long long ctx = FUN_803826f0();
    int self = (int)(ctx >> 0x20);
    float *out = (float *)ctx;
    int def = prop_def_by_index(*(int *)(*(int *)(self + 0x7c) + 0xc));

    int bone = *(int *)(def + 0x24);
    if (bone < 1) {
        bone = *(int *)(def + 0x20);
    }
    float m[23];       // afStack_5c
    entity_build_local_matrix(m, self, 0, 0);
    int slot = *(int *)(*(int *)(self + 4) + 0x10) + bone * 0x40;
    float p[3];        // local_68/64/60
    p[0] = *(float *)(slot + 0x20);
    p[1] = *(float *)(slot + 0x24);
    p[2] = *(float *)(slot + 0x28);
    mtx4_transform_point_alias(m, p);
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    FUN_8038273c();
}

// 0x8024001c | prop_compute_eye_point
// Compute the object aim/eye point in world space, applying any weapon-def
// muzzle offset (def+0x30/0x34/0x38) rotated by the weapon and object yaw, then
// offset by the object world position. Falls back to a fixed height (obj+0x218)
// when no explicit muzzle bone applies.
void prop_compute_eye_point(void)
{
    unsigned long long ctx = __save_gpr_28();
    int self = (int)(ctx >> 0x20);
    float *out = (float *)ctx;
    int rec = *(int *)(self + 0x7c);
    bool useFallbackHeight = false;

    // FUN_80240440 refreshes the scratch pose; the yaw angle it leaves behind
    // is consumed below (kept as a plain object-yaw read for this build).
    prop_read_root_anchor_ctx();
    double yaw = (double)*(float *)(rec + 0x24);

    float base[3];   // reused muzzle-offset template
    float off[3];    // local_70/6c/68
    off[0] = FLOAT_80618910;
    off[1] = *(float *)(rec + 0x1c);   // local_84 (pose Y)
    off[2] = FLOAT_80618958;
    base[1] = *(float *)(rec + 0x1c);

    if ((*(unsigned int *)(rec + 8) & 0x40) != 0) {
        unsigned char *wdef = weapon_get_def_from_entity();
        if ((FLOAT_8061895c == *(float *)(wdef + 0x34)) || ((*(unsigned int *)(wdef + 0xac) & 2) == 0)) {
            unsigned int packed = *(unsigned int *)(wdef + 0x30);
            if ((int)packed < 0) {
                useFallbackHeight = true;
            } else {
                int hi = (int)packed >> 0x10;
                int *node = *(int **)(*(int *)(self + 4) + 4);
                int lo = (packed & 0xffff) * 0x18;
                int base0 = *node;
                off[0] = -*(float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo);
                off[1] = -*(float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo + 4);
                off[2] = -*(float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo + 8);
                if (*(int *)(wdef + 0x38) < 0) {
                    off[1] = base[1];
                }
            }
        } else {
            useFallbackHeight = true;
            off[2] = *(float *)(wdef + 0x34);
        }
    }

    float m[23];   // afStack_64
    mtx4_set_rotation_y((double)((float)((double)FLOAT_80618908 * yaw) / FLOAT_8061890c), m);
    mtx4_transform_vector_alias(m, off);

    float worldOff[3];   // local_7c/78/74
    worldOff[0] = base[0];
    worldOff[1] = base[1];
    worldOff[2] = base[2];
    mtx4_set_rotation_y((double)((FLOAT_80618908 * *(float *)(self + 0x70)) / FLOAT_8061890c), m);
    mtx4_transform_vector_alias(m, worldOff);
    worldOff[0] = worldOff[0] + *(float *)(self + 100);
    worldOff[1] = worldOff[1] + *(float *)(self + 0x68);
    worldOff[2] = worldOff[2] + *(float *)(self + 0x6c);
    out[0] = worldOff[0] - off[0];
    out[1] = worldOff[1] - off[1];
    out[2] = worldOff[2] - off[2];
    if (useFallbackHeight) {
        out[1] = *(float *)(self + 0x218);
    }
}

// 0x8023f64c | prop_death_burst_tick
// Timed-destruction tick: when the object's death timer (obj+0x1cc) reaches 0
// and the model is not already in the destroyed state, play the break sound,
// spawn a directed burst plus a ring of trail particles, flip the prop's
// visibility flag set, kill the entity's particles, set model state 9, and hide
// the render.
void prop_death_burst_tick(void)
{
    int *ctx = (int *)FUN_803826e4();
    int entity = ctx[0];
    int rec = *(int *)(entity + 0x7c);
    int def = prop_def_by_index(*(int *)(rec + 0xc));

    unsigned char collBuf[4];   // auStack_c0
    int collZone;               // local_bc
    collision_result_restore((int)collBuf);
    (void)collZone;

    *(int *)(entity + 0x210) = ctx[2];
    if ((*(int *)(def + 0x24) < 0) || (collZone != *(int *)(def + 0x24))) {
        *(float *)(entity + 0x1cc) = *(float *)(entity + 0x1cc) - (float)ctx[8];
    } else {
        *(float *)(entity + 0x1cc) = FLOAT_80618910;
    }

    if ((*(float *)(entity + 0x1cc) <= FLOAT_80618910) && (*(int *)(rec + 0x10) != 9)) {
        float pos[3];   // local_cc/c8/c4 (via local_cc..)
        int req[7];     // local_a0
        float reqPos[3];
        prop_get_world_pivot(entity, pos);
        sound_init_play_params((int)req);
        req[0] = 0xbf9;
        reqPos[0] = pos[0];
        reqPos[1] = pos[1];
        reqPos[2] = pos[2];
        // (request buffer carries id + position + a 1 flag; layout matches the
        // original spill; play the break SFX)
        sound_play_request(req);

        int s = session_get_state_flag();
        int slot = particle_spawn_directed(0, 0x14, ~-(unsigned int)(s == 1) & 0x80000, pos, 0);
        particle_slot_set_scale((double)FLOAT_806188f8, slot);

        float dir[3];   // local_d8/d4/d0
        for (int i = 0; i < 0xe; i = i + 1) {
            dir[0] = FLOAT_80618910;
            dir[1] = FLOAT_806188f8;
            dir[2] = FLOAT_80618910;
            int ps = session_get_state_flag();
            int tp;
            if (i < 9) {
                tp = particle_spawn_trail(0, 0x35, ~-(unsigned int)(ps == 1) & 0x80000, pos, dir);
            } else {
                tp = particle_spawn_trail(0, 0x36, ~-(unsigned int)(ps == 1) & 0x80000, pos, dir);
            }
            float rnd = (float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80618930) /
                        FLOAT_80618924;
            particle_slot_set_scale((double)(FLOAT_80618928 + rnd), tp);
        }

        *(unsigned int *)(*(int *)(entity + 4) + 0x30) =
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) &
            ~(*(unsigned int *)(def + 0x7c) | *(unsigned int *)(def + 0x80) |
              *(unsigned int *)(def + 0x8c) | *(unsigned int *)(def + 0x84));
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) =
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) |
            *(unsigned int *)(def + 0x90) | *(unsigned int *)(def + 0x88);
        particle_kill_by_entity();
        *(int *)(rec + 0x10) = 9;
        *(float *)(entity + 0x1cc) = FLOAT_80618910;
        entity_set_render_type(entity, 0);
    }
    FUN_80382730();
}

// 0x8023f8f8 | prop_explode_tick
// Explosion-triggered destruction tick: when the object's fuse (obj+0x1cc)
// reaches 0 while armed (obj+0x1d8 > 0) and the collision flag matches, invoke
// the prop's destroy callback (def+0xb0), spawn an explosion, flip the
// visibility flag set, set model state 9 and hide the render. When the
// collision zone matches the def's, instead clear the fired flags and drop the
// state to 4 (return-to-rest).
void prop_explode_tick(void)
{
    int *ctx = (int *)__save_gpr_28();
    int entity = ctx[0];
    int rec = *(int *)(entity + 0x7c);
    int def = prop_def_by_index(*(int *)(rec + 0xc));

    *(int *)(entity + 0x210) = ctx[1];
    if (FLOAT_80618910 < *(float *)(entity + 0x1d8)) {
        *(float *)(entity + 0x1cc) = *(float *)(entity + 0x1cc) - (float)ctx[8];
    }

    unsigned char collBuf[4];   // auStack_3c
    int collZone;               // local_38
    collision_result_restore((int)collBuf);

    if ((*(int *)(def + 0x24) < 0) || (collZone != *(int *)(def + 0x24))) {
        if (FLOAT_80618910 < *(float *)(entity + 0x1d8)) {
            if ((*(float *)(entity + 0x1cc) <= FLOAT_80618910) &&
                ((*(unsigned int *)(*(int *)(entity + 4) + 0x30) & *(unsigned int *)(def + 0x84)) != 0)) {
                int rec2 = *(int *)(entity + 0x7c);
                if (*(void **)(def + 0xb0) != 0) {
                    ((void (*)(int))*(void **)(def + 0xb0))(entity);
                }
                float pos[3];   // afStack_48
                prop_get_world_pivot(entity, pos);
                player_ai_explosion_spawn(1, ctx[6], pos, (float *)ctx[0xc], ctx[1], entity, 1);
                *(unsigned int *)(*(int *)(entity + 4) + 0x30) =
                    *(unsigned int *)(*(int *)(entity + 4) + 0x30) &
                    ~(*(unsigned int *)(def + 0x7c) | *(unsigned int *)(def + 0x80) |
                      *(unsigned int *)(def + 0x8c) | *(unsigned int *)(def + 0x84));
                *(unsigned int *)(*(int *)(entity + 4) + 0x30) =
                    *(unsigned int *)(*(int *)(entity + 4) + 0x30) |
                    *(unsigned int *)(def + 0x88) | *(unsigned int *)(def + 0x90);
                *(int *)(rec2 + 0x10) = 9;
                particle_kill_by_entity();
                *(float *)(entity + 0x1cc) = FLOAT_80618910;
                entity_set_render_type(entity, 0);
            }
        }
    } else {
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) =
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) &
            ~(*(unsigned int *)(def + 0x7c) | *(unsigned int *)(def + 0x8c) | *(unsigned int *)(def + 0x80));
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) =
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) | *(unsigned int *)(def + 0x90);
        if (*(int *)(rec + 0x10) != 1) {
            *(int *)(rec + 0x10) = 4;
        }
    }
}

// 0x8023e890 | prop_ai_tick
// Per-frame behaviour driver for non-weapon props. Runs the model's behaviour
// state machine (rec+0x10):
//   state 10 : idle settle (FUN_80240fd4)
//   state 0xb: charge timer -> transition to state 3
//   state 2  : bounce/drift within per-axis clamp limits, periodic flag toggle,
//              optional charge/hit resolution
//   state 3  : turret aim toward the target entity (rec+0x68) with pitch/yaw
//              slew and clamp, auto-fire and co-op partner targeting
//   state 4  : return orientation to rest
// After the state machine it rebuilds the skeleton (prop_skeleton_build),
// evaluates the auto-fire condition (prop_fire_tick), and flags the object
// dirty (obj+0x18 bit2) when its orientation changed this frame.
void prop_ai_tick(void)
{
    unsigned long long ctx = FUN_803826dc();
    int self = (int)(ctx >> 0x20);
    int rec = *(int *)(self + 0x7c);
    int def = prop_def_by_index(*(int *)(rec + 0xc));

    double prevPitch = (double)*(float *)(rec + 0x1c);
    bool aimReady = false;
    double prevYaw = (double)*(float *)(rec + 0x24);
    double prevRoll = (double)*(float *)(rec + 0x2c);

    if (DAT_806108e4 != 0) {
        if ((*(int *)(rec + 0x10) == 9) || (*(int *)(rec + 0x10) == 1)) {
            *(unsigned int *)(*(int *)(self + 4) + 0x30) =
                *(unsigned int *)(*(int *)(self + 4) + 0x30) &
                ~(*(unsigned int *)(def + 0x7c) | *(unsigned int *)(def + 0x80));
            if ((*(float *)(rec + 0x1c) < *(float *)(def + 0xc)) &&
                ((*(unsigned int *)(rec + 8) & 0x40000) == 0)) {
                *(float *)(rec + 0x1c) = FLOAT_80618928 * FLOAT_806108ec + *(float *)(rec + 0x1c);
            }
            if (((*(int *)(rec + 0xbc) != -1) && (DAT_805cb94c[*(int *)(rec + 0xbc) * 8] != 0)) &&
                ((*(unsigned int *)(rec + 8) & 0x200000) == 0)) {
                *(int *)(rec + 0x10) = 4;
            }
        }

        if (((*(unsigned int *)(rec + 8) & 1) == 0) && (0 < DAT_806108e4)) {
            int state = *(int *)(rec + 0x10);
            if (state == 10) {
                prop_acquire_target();
            } else if (state == 0xb) {
                float dt = FLOAT_806108ec *
                    (FLOAT_806188f8 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618900));
                *(float *)(rec + 0x194) = dt + *(float *)(rec + 0x194);
                if (FLOAT_8061891c < *(float *)(rec + 0x194)) {
                    *(float *)(rec + 0x194) = FLOAT_8061891c;
                    *(int *)(rec + 0x10) = 3;
                }
            } else if (state == 2) {
                *(int *)(self + 0x1a4) = *(int *)(self + 0x1a4) + 2;
                if ((int)DAT_806109c0 < *(int *)(self + 0x1a4)) {
                    *(unsigned int *)(*(int *)(self + 4) + 0x30) =
                        *(unsigned int *)(*(int *)(self + 4) + 0x30) ^ *(unsigned int *)(def + 0x7c);
                    *(int *)(self + 0x1a4) = 0;
                }
                if ((*(unsigned int *)(rec + 8) & 0x40000) == 0) {
                    *(float *)(rec + 0x1c) = *(float *)(rec + 0x58) * FLOAT_806108ec + *(float *)(rec + 0x1c);
                    *(float *)(rec + 0x24) = *(float *)(rec + 0x5c) * FLOAT_806108ec + *(float *)(rec + 0x24);
                    *(float *)(rec + 0x2c) = *(float *)(rec + 0x60) * FLOAT_806108ec + *(float *)(rec + 0x2c);
                    // Reflect each axis off its clamp limits (bounce).
                    bool flip;
                    flip = false;
                    if (*(float *)(rec + 0x1c) <= *(float *)(rec + 0x38)) {
                        if (*(float *)(rec + 0x1c) < *(float *)(rec + 0x34)) {
                            *(float *)(rec + 0x1c) = *(float *)(rec + 0x34); flip = true;
                        }
                    } else { *(float *)(rec + 0x1c) = *(float *)(rec + 0x38); flip = true; }
                    if (flip) { *(float *)(rec + 0x58) = -*(float *)(rec + 0x58); }
                    flip = false;
                    if (*(float *)(rec + 0x24) <= *(float *)(rec + 0x40)) {
                        if (*(float *)(rec + 0x24) < *(float *)(rec + 0x3c)) {
                            *(float *)(rec + 0x24) = *(float *)(rec + 0x3c); flip = true;
                        }
                    } else { *(float *)(rec + 0x24) = *(float *)(rec + 0x40); flip = true; }
                    if (flip) { *(float *)(rec + 0x5c) = -*(float *)(rec + 0x5c); }
                    flip = false;
                    if (*(float *)(rec + 0x2c) <= *(float *)(rec + 0x48)) {
                        if (*(float *)(rec + 0x2c) < *(float *)(rec + 0x44)) {
                            *(float *)(rec + 0x2c) = *(float *)(rec + 0x44); flip = true;
                        }
                    } else { *(float *)(rec + 0x2c) = *(float *)(rec + 0x48); flip = true; }
                    if (flip) { *(float *)(rec + 0x60) = -*(float *)(rec + 0x60); }
                }
                *(int *)(rec + 0x20) = *(int *)(rec + 0x1c);
                *(int *)(rec + 0x28) = *(int *)(rec + 0x24);
                *(int *)(rec + 0x30) = *(int *)(rec + 0x2c);
                int busy = prop_acquire_target();
                if (((busy == 0) && ((*(unsigned int *)(rec + 8) & 4) != 0)) &&
                    ((*(unsigned int *)(rec + 8) & 0x400000) != 0)) {
                    int los = prop_scan_projectile_targets();
                    if (los == 0) {
                        if (FLOAT_80618910 < *(float *)(rec + 0xac)) {
                            float dt = FLOAT_806108ec *
                                (FLOAT_806188f8 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618900));
                            *(float *)(rec + 0xac) = -(FLOAT_8061893c * dt - *(float *)(rec + 0xac));
                            if (*(float *)(rec + 0xac) < FLOAT_80618910) {
                                *(float *)(rec + 0xac) = FLOAT_80618910;
                            }
                        }
                    } else {
                        float dt = FLOAT_806108ec *
                            (FLOAT_806188f8 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618900));
                        *(float *)(rec + 0xac) = FLOAT_80618928 * dt + *(float *)(rec + 0xac);
                        double folded = FUN_80390ae4((double)*(float *)(rec + 0xac),
                            (double)(*(float *)(def + 0xa4) /
                                     (float)((double)CONCAT44(0x43300000, *(unsigned int *)(def + 0xac) ^ 0x80000000) - DOUBLE_80618900)));
                        aimReady = *(float *)(def + 0xa8) < (float)folded;
                        if ((*(unsigned int *)(rec + 8) & 4) != 0) {
                            *(float *)(rec + 0xb8) = FLOAT_80618938 * FLOAT_806108ec + *(float *)(rec + 0xb8);
                        }
                    }
                }
            } else if (state == 3) {
                // Turret aim toward the target entity (rec+0x68). Compute the
                // desired pitch/yaw to the target, slew toward it with clamping,
                // and set aimReady once settled and line-of-sight holds.
                *(int *)(self + 0x1a4) = *(int *)(self + 0x1a4) + 3;
                if ((int)DAT_806109c0 < *(int *)(self + 0x1a4)) {
                    *(unsigned int *)(*(int *)(self + 4) + 0x30) =
                        *(unsigned int *)(*(int *)(self + 4) + 0x30) ^ *(unsigned int *)(def + 0x80);
                    *(int *)(self + 0x1a4) = 0;
                }
                float basePos[3];   // local_d8/d4/d0
                prop_get_world_pivot(self, basePos);
                int target = *(int *)(rec + 0x68);
                if (target == 0) {
                    *(int *)(rec + 0x10) = 4;
                } else {
                    float tgt[3];   // local_cc/c8/c4
                    if (*(int *)(target + 0x20) == 8) {
                        int trec = *(int *)(target + 0x7c);
                        tgt[0] = *(float *)(trec + 0xbb4);
                        tgt[1] = *(float *)(trec + 0xbb8);
                        tgt[2] = *(float *)(trec + 0xbbc);
                    } else {
                        tgt[0] = *(float *)(target + 100);
                        tgt[1] = *(float *)(target + 0x68) + *(float *)(target + 0x1ac);
                        tgt[2] = *(float *)(target + 0x6c);
                    }
                    tgt[2] = tgt[2] - basePos[2];
                    tgt[1] = tgt[1] - basePos[1];
                    tgt[0] = tgt[0] - basePos[0];
                    float d2 = tgt[2] * tgt[2] + tgt[0] * tgt[0] + tgt[1] * tgt[1];
                    if (FLOAT_80618940 < d2) {
                        float inv = 1.0f / SQRT(d2);
                        inv = inv * -(FLOAT_80618928 * d2 * inv * inv - FLOAT_8061892c);
                        float k = FLOAT_806188f8 * inv * -(FLOAT_80618928 * d2 * inv * inv - FLOAT_8061892c);
                        tgt[0] = tgt[0] * k;
                        tgt[1] = tgt[1] * k;
                        tgt[2] = tgt[2] * k;
                    }
                    float m[16];   // afStack_c0
                    mtx4_set_rotation_y(-(double)((FLOAT_80618908 * *(float *)(self + 0x70)) / FLOAT_8061890c), m);
                    mtx4_transform_vector_alias(m, tgt);
                    double flat = math_sqrt((double)(tgt[0] * tgt[0] + tgt[2] * tgt[2]));
                    double pitch = math_atan2((double)tgt[1], flat);
                    double desiredPitch = -(double)((FLOAT_8061890c * (float)pitch) / FLOAT_80618908);
                    double rounded = FUN_80390324((double)((float)(desiredPitch - (double)FLOAT_80618944) / FLOAT_80618948));
                    desiredPitch = -(double)(float)((double)FLOAT_80618948 * (double)(float)rounded - desiredPitch);
                    double yawA = math_atan2((double)tgt[0], (double)tgt[2]);
                    double desiredYaw = (double)((FLOAT_8061890c * (float)yawA) / FLOAT_80618908);

                    unsigned char clampedP;
                    if (desiredPitch <= (double)*(float *)(rec + 0x38)) {
                        if ((double)*(float *)(rec + 0x34) <= desiredPitch) {
                            clampedP = 0;
                        } else { clampedP = 1; desiredPitch = (double)*(float *)(rec + 0x34); }
                    } else { clampedP = 1; desiredPitch = (double)*(float *)(rec + 0x38); }
                    unsigned int clamped = clampedP;
                    if (clamped == 0) {
                        if (desiredYaw <= (double)*(float *)(rec + 0x40)) {
                            if ((double)*(float *)(rec + 0x3c) <= desiredYaw) {
                                clampedP = 0;
                            } else { clampedP = 1; desiredYaw = (double)*(float *)(rec + 0x3c); }
                        } else { clampedP = 1; desiredYaw = (double)*(float *)(rec + 0x40); }
                        clamped = clampedP;
                    }
                    int inArc = 1 - clamped;

                    float wrap = (float)((double)*(float *)(rec + 0x24) - desiredYaw);
                    for (; wrap <= FLOAT_80618944; wrap = wrap + FLOAT_80618948) { }
                    for (; FLOAT_8061890c < wrap; wrap = wrap - FLOAT_80618948) { }
                    if (ABS(wrap) < FLOAT_8061894c) {
                        double curP = (double)*(float *)(rec + 0x1c);
                        if (desiredPitch <= curP) {
                            *(float *)(rec + 0x1c) = -(float)((double)FLOAT_80618950 * (double)FLOAT_806108ec - curP);
                            if ((double)*(float *)(rec + 0x1c) < desiredPitch) {
                                *(float *)(rec + 0x1c) = (float)desiredPitch;
                            }
                        } else {
                            *(float *)(rec + 0x1c) = (float)((double)FLOAT_80618950 * (double)FLOAT_806108ec + curP);
                            if (desiredPitch < (double)*(float *)(rec + 0x1c)) {
                                *(float *)(rec + 0x1c) = (float)desiredPitch;
                            }
                        }
                    }
                    double yawStep = angle_step_toward_clamped(desiredYaw, (double)*(float *)(rec + 0x24),
                                        (double)*(float *)(rec + 0x3c), (double)*(float *)(rec + 0x40));
                    if ((double)FLOAT_80618910 <= yawStep) {
                        if ((double)FLOAT_80618910 < yawStep) {
                            float step = FLOAT_80618954 * FLOAT_806108ec;
                            *(float *)(rec + 0x24) = (float)((double)*(float *)(rec + 0x24) - (double)step);
                            if (((double)*(float *)(rec + 0x24) < desiredYaw) &&
                                ((double)(float)(desiredYaw - (double)step) <= (double)*(float *)(rec + 0x24))) {
                                *(float *)(rec + 0x24) = (float)desiredYaw;
                            }
                        }
                    } else {
                        float step = FLOAT_80618954 * FLOAT_806108ec;
                        *(float *)(rec + 0x24) = (float)((double)*(float *)(rec + 0x24) + (double)step);
                        if ((desiredYaw < (double)*(float *)(rec + 0x24)) &&
                            ((double)*(float *)(rec + 0x24) <= (double)(float)(desiredYaw + (double)step))) {
                            *(float *)(rec + 0x24) = (float)desiredYaw;
                        }
                    }
                    // Clamp pitch/yaw into [min,max].
                    {
                        float hi = *(float *)(rec + 0x38);
                        float cur = *(float *)(rec + 0x1c);
                        float lo = *(float *)(rec + 0x34);
                        float t = cur; if (hi < cur) t = hi;
                        float outv = cur;
                        if (lo <= t) { outv = cur; if (hi < cur) outv = hi; }
                        else { outv = lo; }
                        *(float *)(rec + 0x1c) = outv;
                    }
                    {
                        float hi = *(float *)(rec + 0x40);
                        float cur = *(float *)(rec + 0x24);
                        float lo = *(float *)(rec + 0x3c);
                        float t = cur; if (hi < cur) t = hi;
                        float outv = cur;
                        if (lo <= t) { outv = cur; if (hi < cur) outv = hi; }
                        else { outv = lo; }
                        *(float *)(rec + 0x24) = outv;
                    }

                    if ((*(unsigned int *)(rec + 8) & 2) == 0) {
                        if (inArc != 0) {
                            double folded = FUN_80390ae4((double)*(float *)(rec + 0xac),
                                (double)(*(float *)(def + 0xa4) /
                                         (float)((double)CONCAT44(0x43300000, *(unsigned int *)(def + 0xac) ^ 0x80000000) - DOUBLE_80618900)));
                            if (*(float *)(def + 0xa8) < (float)folded) {
                                inArc = 0;
                            }
                            if ((*(unsigned int *)(rec + 8) & 4) != 0) {
                                *(float *)(rec + 0xb8) = FLOAT_80618938 * FLOAT_806108ec + *(float *)(rec + 0xb8);
                            }
                        }
                        aimReady = false;
                        if ((inArc != 0) &&
                            (ABS((float)((double)*(float *)(rec + 0x24) - desiredYaw)) < FLOAT_8061893c) &&
                            (ABS((float)((double)*(float *)(rec + 0x1c) - desiredPitch)) < FLOAT_8061893c)) {
                            aimReady = true;
                        }
                    }

                    float dt = FLOAT_806108ec *
                        (FLOAT_806188f8 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618900));
                    *(float *)(rec + 0xac) = dt + *(float *)(rec + 0xac);
                    if ((*(unsigned int *)(rec + 8) & 2) == 0) {
                        if (*(float *)(def + 0xa4) <= *(float *)(rec + 0xac)) {
                            int trec = *(int *)(*(int *)(rec + 0x68) + 0x7c);
                            int hit;
                            if ((trec == 0) ||
                                (((*(unsigned int *)(trec + 0x24) & 8) == 0) &&
                                 ((hit = prop_can_target(self, *(int *)(rec + 0x68), 1, 0)) != 0))) {
                                *(float *)(rec + 0xac) = FLOAT_80618910;
                                *(int *)(rec + 0xd0) = *(int *)(rec + 0xd0) + DAT_806108e4;
                                if (*(int *)(rec + 0xd0) < (int)DAT_806109c0) {
                                    aimReady = false;
                                }
                            } else {
                                *(int *)(rec + 0xd0) = 0;
                                *(int *)(rec + 0x10) = 4;
                                *(unsigned int *)(*(int *)(self + 4) + 0x30) =
                                    *(unsigned int *)(*(int *)(self + 4) + 0x30) & ~*(unsigned int *)(def + 0x80);
                                *(unsigned int *)(*(int *)(self + 4) + 0x30) =
                                    *(unsigned int *)(*(int *)(self + 4) + 0x30) | *(unsigned int *)(def + 0x7c);
                            }
                        }
                    } else {
                        // Multi-target (co-op) variant: sweep the partner table.
                        int chosen = 0;
                        int off = 0;
                        for (int i = 0; i < DAT_80612610; i = i + 1) {
                            chosen = *(int *)(DAT_80611d78 + off + 0x14);
                            int hit = prop_can_target(self, chosen, 1, 0);
                            if (hit != 0) break;
                            off = off + 0x141c;
                            chosen = 0;
                        }
                        if (chosen == 0) {
                            *(int *)(rec + 0xd0) = 0;
                            *(int *)(rec + 0x10) = 4;
                            *(unsigned int *)(*(int *)(self + 4) + 0x30) =
                                *(unsigned int *)(*(int *)(self + 4) + 0x30) & ~*(unsigned int *)(def + 0x80);
                            *(unsigned int *)(*(int *)(self + 4) + 0x30) =
                                *(unsigned int *)(*(int *)(self + 4) + 0x30) | *(unsigned int *)(def + 0x7c);
                        } else {
                            *(int *)(rec + 0xd0) = *(int *)(rec + 0xd0) + DAT_806108e4;
                            if ((DAT_805cb94c[*(int *)(rec + 0xbc) * 8] != 1) &&
                                ((int)DAT_806109c0 <= *(int *)(rec + 0xd0)) &&
                                (*(float *)(rec + 0xac) = FLOAT_80618910, (*(unsigned int *)(rec + 8) & 0x20000) == 0)) {
                                FUN_8025caf0(*(int *)(rec + 0xbc), 1, chosen);
                            }
                        }
                    }
                }
            } else if ((state == 4) && ((*(unsigned int *)(rec + 8) & 0x40000) == 0)) {
                // Return-to-rest: slew each of pitch/yaw back to the rest pose
                // (rec+0x20 / rec+0x28) at FLOAT_8061891c per frame.
                if (*(float *)(rec + 0x28) == *(float *)(rec + 0x24)) {
                    float cur = *(float *)(rec + 0x1c);
                    if (*(float *)(rec + 0x20) <= cur) {
                        *(float *)(rec + 0x1c) = -(FLOAT_8061891c * FLOAT_806108ec - cur);
                        if (*(float *)(rec + 0x1c) < *(float *)(rec + 0x20)) {
                            *(float *)(rec + 0x1c) = *(float *)(rec + 0x20);
                        }
                    } else {
                        *(float *)(rec + 0x1c) = FLOAT_8061891c * FLOAT_806108ec + cur;
                        if (*(float *)(rec + 0x20) < *(float *)(rec + 0x1c)) {
                            *(float *)(rec + 0x1c) = *(float *)(rec + 0x20);
                        }
                    }
                } else if (*(float *)(rec + 0x28) <= *(float *)(rec + 0x24)) {
                    *(float *)(rec + 0x24) = -(FLOAT_8061891c * FLOAT_806108ec - *(float *)(rec + 0x24));
                    if (*(float *)(rec + 0x24) < *(float *)(rec + 0x28)) {
                        *(float *)(rec + 0x24) = *(float *)(rec + 0x28);
                    }
                } else {
                    *(float *)(rec + 0x24) = FLOAT_8061891c * FLOAT_806108ec + *(float *)(rec + 0x24);
                    if (*(float *)(rec + 0x28) < *(float *)(rec + 0x24)) {
                        *(float *)(rec + 0x24) = *(float *)(rec + 0x28);
                    }
                }
                if ((*(float *)(rec + 0x1c) == *(float *)(rec + 0x20)) &&
                    (*(float *)(rec + 0x24) == *(float *)(rec + 0x28))) {
                    *(int *)(rec + 0x10) = 2;
                }
            }
        }
    }

    if (((*(unsigned int *)(self + 0x28) & 0x100) == 0) &&
        (((*(unsigned int *)(self + 0x18) & 8) == 0) || (*(int *)(rec + 0xd8) != 0))) {
        prop_skeleton_build();
        *(unsigned int *)(self + 0x18) = *(unsigned int *)(self + 0x18) | 8;
    }

    if (DAT_806108e4 != 0) {
        if ((*(unsigned int *)(rec + 8) & 2) == 0) {
            int owner = *(int *)(rec + 0x6c);
            unsigned int burst = *(unsigned int *)(DAT_80610ba0 + *(int *)(owner + 0x30) * 0x29c + 0xc) & 0x200;
            if (aimReady || ((burst != 0) && (FLOAT_80618910 < *(float *)(rec + 0xac)))) {
                prop_fire_tick(self, owner, 0, 1, 3, (int *)0);
            } else {
                if ((FLOAT_80618910 == *(float *)(owner + 0x104)) && (burst != 0)) {
                    float dt = FLOAT_806108ec *
                        (FLOAT_806188f8 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618900));
                    float v = -(dt - *(float *)(owner + 0x108));
                    if (v < FLOAT_80618910) { v = FLOAT_80618910; }
                    *(float *)(owner + 0x108) = v;
                }
                *(float *)(*(int *)(rec + 0x6c) + 0x104) = FLOAT_80618910;
            }
        }
        if ((prevPitch != (double)*(float *)(rec + 0x1c)) ||
            (prevYaw != (double)*(float *)(rec + 0x24)) ||
            (prevRoll != (double)*(float *)(rec + 0x2c))) {
            *(unsigned int *)(self + 0x18) = *(unsigned int *)(self + 0x18) | 2;
        }
    }
    FUN_80382728();
}
