// snake_boss_serpent_fsm.cpp - the master FSM and per-frame update suite for the
// second segmented "serpent"/spine boss (DAT_80612a38, introduced in
// snake_boss_serpent.cpp / batch_769). This file holds the boss's per-frame
// behaviour tick (a 0x12-state machine), the rope/tentacle segment simulation,
// projectile hit routing, body-collision damage, camera shake, debris spawning
// and the rope-attractor driver.
//
// The three animated body segments live at DAT_80612a38 + 0x2c00 (stride 0x5c):
//   +0x2bfc..+0x2c04  segment base world position
//   +0x2bf0..         segment forward dir; +0x2bd8.. right basis
//   +0x2c0c           current sweep angle; +0x2c18 sweep direction
//   +0x2c1c/+0x2c20   collision-sphere count / array pointer (0x10-byte spheres)
//   +0x2c2c           per-segment damage accumulator
//   +0x2c30           spawned-debris entity handle
// Head/tail launch accumulators live at +0x1d84 / +0x1d88; the head world
// position at +0x1cfc..+0x1d04. Section-enable bits are +0x2df4; the FSM's
// public state is the actor record's +0x20 (mirrored to +0x9d0/+0x9d4).
//
// Functions:
//   snake3_rope_attractors_update (0x802972b0)
//   snake3_debris_fade_tick       (0x8029736c)
//   snake3_debris_set_spin        (0x80297430)
//   snake3_spawn_debris_chunk     (0x80297498)
//   snake3_rope_segments_update   (0x802976dc)
//   snake3_camera_shake_players   (0x802980cc)
//   snake3_body_collision_damage  (0x802981c8)
//   snake3_projectile_hit_react   (0x80298338)
//   snake3_push_players_out       (0x802987ec)
//   snake3_boss_behavior_tick     (0x80298904)
//
// Address range: 0x802972b0 - 0x80298904
// Subsystem: game (AI actors / boss)

#include "common.h"

extern "C" {

// --- data globals ----------------------------------------------------------
extern int*  DAT_80612a38;               // serpent-boss control block base
extern int   DAT_80612610;               // player count
extern int   DAT_80611d78;               // player record table base (stride 0x141c)
extern int   DAT_80611d74;               // active-actor global record base
extern int   DAT_806109c0;               // frame-delta ticks (xor bias)
extern int   DAT_806108e4;               // debris fade step (int)
extern const float FLOAT_806108e8;       // per-frame scalar step
extern const float FLOAT_806108ec;       // per-frame scalar step (alt)
extern float FLOAT_80612a50;             // scratch aim scalar (shared w/ anim-select)
extern int   DAT_8048ca9c;               // camera/effect scale table base

// rodata spawn vectors / constants used by the debris + FSM setup
extern float DAT_803e06c4, DAT_803e06c8, DAT_803e06cc;
extern int   DAT_803e06d0, DAT_803e06d4, DAT_803e06d8, DAT_803e06dc, DAT_803e06e0, DAT_803e06e4;
extern int   DAT_803e06e8, DAT_803e06ec, DAT_803e06f0, DAT_803e06f4, DAT_803e06f8, DAT_803e06fc;
extern int   DAT_803e0700, DAT_803e0704, DAT_803e0708, DAT_803e070c, DAT_803e0710, DAT_803e0714;

// --- float / double constants (rodata, boss block) -------------------------
extern const float  FLOAT_80619560, FLOAT_80619564, FLOAT_80619568, FLOAT_8061956c;
extern const float  FLOAT_80619574, FLOAT_80619578, FLOAT_80619580, FLOAT_80619588;
extern const float  FLOAT_8061958c, FLOAT_80619590, FLOAT_8061959c, FLOAT_806195a0;
extern const float  FLOAT_806195a4, FLOAT_806195a8, FLOAT_806195ac, FLOAT_806195b0;
extern const float  FLOAT_806195b4, FLOAT_80619584, FLOAT_806195b8, FLOAT_806195c0;
extern const float  FLOAT_806195cc, FLOAT_806195d4;
extern const float  FLOAT_806195e4, FLOAT_806195f4, FLOAT_806195f8, FLOAT_806195fc;
extern const float  FLOAT_80619600, FLOAT_80619604, FLOAT_80619608, FLOAT_8061961c;
extern const float  FLOAT_80619620, FLOAT_80619624, FLOAT_8061963c, FLOAT_80619648;
extern const float  FLOAT_8061964c, FLOAT_8061965c, FLOAT_8061967c, FLOAT_806196c0;
extern const float  FLOAT_806196d8, FLOAT_806196e4, FLOAT_806196e8, FLOAT_806196f8;
extern const float  FLOAT_80619700, FLOAT_80619770, FLOAT_80619790, FLOAT_806197a0;
extern const float  FLOAT_806197a4, FLOAT_806197a8, FLOAT_806197ac, FLOAT_806197b0;
extern const float  FLOAT_806197b4, FLOAT_806197b8, FLOAT_806197bc, FLOAT_806197c0;
extern const float  FLOAT_806197c4, FLOAT_806197cc, FLOAT_806197d0, FLOAT_806197d4;
extern const float  FLOAT_806197d8, FLOAT_806197dc, FLOAT_806197e0, FLOAT_806197e4;
extern const float  FLOAT_806197e8, FLOAT_806197ec;
extern const double DOUBLE_80619610;     // rng bias
extern const double DOUBLE_80619630;     // frame-delta bias

// --- raw actor / render-rig helpers (still FUN_ in the IDB) ----------------
extern int  FUN_803826f0(void);          // render rig begin (used elsewhere)
extern int  FUN_803826dc(void);          // rig begin (rope segments)
extern int  FUN_803826e8(void);          // rig begin (body collision) -> team/mtx id
extern int  FUN_803826c0(void);          // rig begin (master FSM) -> actor context
extern void FUN_80382728(void);          // rig end (rope segments)
extern void FUN_80382734(void);          // rig end (body collision)
extern void FUN_8038270c(void);          // rig end (master FSM)
extern void FUN_8038273c(void);          // rig end (debris spawn)

// rope solver + capsule tests
extern void   FUN_802b34a4(int a1);
extern double FUN_802b34fc(int a1, float* a2, float* a3, float* a4);
extern void   FUN_802b1884(int a1, int a2, float* a3, float* a4);
extern int    FUN_802b15a4(int a1, float* a2, float* a3);
extern void   FUN_802b0cb4(double a, double b, double c, double d, int a5);
extern void   FUN_802b22d8(int a1, int a2, float* a3, float* a4, int a5, int a6);
extern void   FUN_802b13bc(void);
// anim-frame / sequence selectors used by the FSM
extern int    FUN_802b0e3c(int* a1, int* a2);
extern int    FUN_802b0ea0(int a1, int a2, int a3, int a4, int a5, float* a6, int a7);
extern int    FUN_802b0bfc(int a1, int a2, int a3, int a4);
extern int    FUN_802b0c58(int a1, int a2, int a3, int a4);
extern int    FUN_802b0b2c(int a1, int a2, int a3, int a4);
extern int    FUN_802b0ba0(int a1, int a2, int a3, int a4);
// render binding + fx setup
extern void   FUN_802aeccc(int a1, int a2, int a3, int a4, int a5, unsigned int flags);
extern void   FUN_802af678(void);
extern void   FUN_802ad80c(double a, double b, double c, double d);
extern void   FUN_802af38c(void);
extern int*   FUN_802a472c(int* skel, int boneIdx);  // resolve bone record by index

// --- named cross-module helpers (call-site typed) --------------------------
extern double math_sqrt(double x);
extern double math_poly_trig(double x);
extern unsigned int rng_counter_tick(void);
extern void mtx4_transform_point(float* mtx, float* pt);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void dir_to_angles(float* dir, int pitchOrNull, float* yaw);
extern void entity_build_local_matrix(float* outMtx, int entity, int includePos, int mirror);
extern void entity_register_for_update(int entity, int phase, int cb);
extern unsigned long long entity_destroy_with_event(int* entity);
extern unsigned long long entity_set_render_type(int entity, void* cb);
extern int  entity_spawn_by_type(unsigned long long a, double b, double c, double d,
    unsigned long long e, unsigned long long f, unsigned long long g, unsigned long long h,
    int typeId, float* pos, float* rot, int p12, int p13);

extern int  anim_active_slot_userflags(int animCtx);
extern int  anim_is_clip_active(int actor, int clipId);
extern unsigned long long anim_play_scaled(double a, double b);
extern void anim_set_blend_weight_alt(double w, int animCtx);
extern void anim_compute_root_velocity(double a, double b, double c, double d, int actor, int p6);

extern void aiactor_retarget_visible_teammate(void);

extern int  player_ai_projectile_spawn_simple(double a, double b, int type, int* posBlk,
    int* dirBlk, int owner, int* p7, int p8, int p9, int p10, int classId, int p12);
extern void player_ai_spawn_trail_particle(int a1, int a2, int a3, float* pos, float* dir, int a6);
extern unsigned long long player_ai_explosion_spawn(int a1, int a2, float* pos, float* p4, int p5, int p6, int p7);
extern void player_apply_camera_shake(double x, double y, double z, int playerRec);
extern int  player_ai_get_active_index(void);
extern void player_ai_get_set_raycast_mode(int mode);

extern int  subobj_pool_alloc(void);
extern void subobj_build_descriptor_default(double a, double b, double c, double d,
    unsigned long long e, unsigned long long f, unsigned long long g, unsigned long long h,
    int handle, int entity, int p11, int p12, float p13);
extern void subobj_set_collision_groups(unsigned int handle, int g1, int g2);
extern void subobj_set_mass(double m, unsigned int handle);
extern void subobj_add_force_at_point(unsigned int handle, float* point, float* force);
extern int  subobj_get_contact_origin(unsigned int handle);
extern void subobj_detach_from_entity(int obj);

extern void collision_set_shape_params(int* collObj, int a2, int a3, int a4, char a5);
extern int  screen_effect_alloc_slot(void);
extern void screen_effect_init_slot(int slot, int entity);
extern unsigned int sprite_get_texture_id(int id);

extern void switch_slot_reset(int slotPtr);
extern void damage_dispatch_to_entity(double a, double b, double c, unsigned long long d,
    unsigned long long e, unsigned long long f, unsigned long long g, unsigned long long h, int* params);
extern void worm_play_positional_sound(double a, int soundId, unsigned int* pos);
extern void sound_init_play_params(int paramsPtr);
extern unsigned int sound_play_request(int* params);
extern unsigned long long statlvl_class_force_complete(int a);

// --- same-module serpent-boss helpers (snake_boss_serpent.cpp, batch_769) --
extern void snake3_attractor_nudge(float* rec, int p2, int p3, int enabled);
extern void snake3_dual_projectile_launch(void);
extern void snake3_anim_spit_spawn(int actor);
extern int  snake3_segment_sphere_sweep(int idx);

} // extern "C"

// forward decls (defined below / referenced as render+update callbacks)
static void snake3_spawn_debris_chunk(void);
static void snake3_rope_segments_update(void);
static void snake3_camera_shake_players(void);
static void snake3_body_collision_damage(void);
void snake3_projectile_hit_react(int proj);
static void snake3_push_players_out(void);
static void snake3_rope_attractors_update(void);

// Helper: the recurring "(double)(u32 ^ 0x80000000) - bias" unsigned->float idiom.
static inline float u32_to_float_biased(unsigned int v) {
    return (float)((double)CONCAT44(0x43300000, (unsigned)(v ^ 0x80000000)) - DOUBLE_80619630);
}

// ===========================================================================
// 0x802972b0  snake3_rope_attractors_update
// Drive the five rope attractors (3 body segments at +0x2d3c stride 0x20, plus
// the head/tail attractors at +0x2d9c / +0x2dbc). The enable flag is 1 only when
// +0x2ddc is zero (countLeadingZeros>>5 maps 0 -> 1, non-zero -> 0).
// ===========================================================================
static void snake3_rope_attractors_update(void) {
    unsigned int en = countLeadingZeros(*(unsigned int*)((int)DAT_80612a38 + 0x2ddc)) >> 5;
    int off = 0, off2 = 0;
    for (int i = 0; i < 3; i++) {
        snake3_attractor_nudge((float*)((int)DAT_80612a38 + off + 0x2d3c),
                               (int)DAT_80612a38 + 0x1d14, (int)DAT_80612a38 + off2 + 0x2bfc, en);
        off += 0x20;
        off2 += 0x5c;
    }
    snake3_attractor_nudge((float*)((int)DAT_80612a38 + 0x2d9c),
                           (int)DAT_80612a38 + 0x1d14, (int)DAT_80612a38 + 0x1d3c, en);
    snake3_attractor_nudge((float*)((int)DAT_80612a38 + 0x2dbc),
                           (int)DAT_80612a38 + 0x1d14, (int)DAT_80612a38 + 0x1d48, en);
}

// ===========================================================================
// 0x8029736c  snake3_debris_fade_tick
// Update-callback on a spawned debris chunk. Counts its lifetime scalar
// (obj[0x69]) down by the frame step; once it drops below 0x7f either shrink its
// collision shape and detach it from the entity (fading path) or destroy it.
// ===========================================================================
static void snake3_debris_fade_tick(unsigned int* obj) {
    if (0 < (int)obj[0x69]) {
        obj[0x69] = obj[0x69] - DAT_806108e4;
        unsigned int life = obj[0x69];
        if ((int)life < 0x7f) {
            if (FLOAT_806195a0 * u32_to_float_biased((unsigned)DAT_806109c0) <=
                u32_to_float_biased(life)) {
                collision_set_shape_params((int*)obj[1], 0xf, 0xffffffff, 0xff, (char)life);
                subobj_detach_from_entity((int)obj);
                obj[10] = obj[10] & 0xffffff9f;
            } else {
                entity_destroy_with_event((int*)obj);
            }
        }
    }
}

// ===========================================================================
// 0x80297430  snake3_debris_set_spin
// Give a freshly-spawned debris entity an angular spin proportional to the frame
// delta and register it for the physics update phase.
// ===========================================================================
static void snake3_debris_set_spin(double spin, int entity) {
    *(int*)(entity + 0x1a0) = 1;
    *(int*)(entity + 0x1a4) = (int)(spin * (double)u32_to_float_biased((unsigned)DAT_806109c0));
    entity_register_for_update(entity, 3, -0x7fd68c94);
}

// ===========================================================================
// 0x80297498  snake3_spawn_debris_chunk
// Spawn one physics debris chunk at the current body segment: detonate a small
// explosion, spawn the chunk entity, allocate + configure its rigid-body subobj
// (collision groups, mass, spin, launch impulse), and play an impact sound with
// an RNG gate.
// ===========================================================================
static void snake3_spawn_debris_chunk(void) {
    int actorCtx = FUN_803826dc();
    float* seg = (float*)((int)DAT_80612a38 + actorCtx * 0x5c + 0x2bd8);
    int params[16];

    float rotVec[3];
    rotVec[0] = DAT_803e06c4;
    rotVec[1] = (float)DAT_803e06c8;
    rotVec[2] = (float)DAT_803e06cc;

    float spawnPos[3];
    spawnPos[0] = seg[9];
    spawnPos[1] = seg[10];
    spawnPos[2] = seg[0xb];

    unsigned long long boom = player_ai_explosion_spawn(0x20, 0x17, seg + 9, (float*)0, 0, 0, 1);
    int chunk = entity_spawn_by_type(boom, 0.0, 0.0, 0.0, 0, 0, 0, 0,
                                     actorCtx + 0x4be, spawnPos, rotVec, 0, 0);
    int handle = subobj_pool_alloc();
    *(int*)(chunk + 0x80) = handle;
    subobj_build_descriptor_default(0.0, 0.0, 0.0, 0.0, 0, 0, 0, 0,
                                    *(int*)(chunk + 0x80), chunk, 3, 0x19, 1.4013e-45f);
    subobj_set_collision_groups(*(unsigned int*)(chunk + 0x80), 1, 0xff);
    subobj_set_mass((double)FLOAT_806195b8, *(unsigned int*)(chunk + 0x80));
    entity_register_for_update(chunk, 3, -0x7fd68ca4);
    snake3_debris_set_spin((double)FLOAT_806195f4, chunk);

    // Launch impulse: -0.x * segment right basis, applied at the contact origin.
    float force[3];
    force[0] = FLOAT_806195d4 * -seg[0];
    force[2] = FLOAT_806195d4 * -seg[2];
    force[1] = FLOAT_806197a4;
    float* origin = (float*)subobj_get_contact_origin(*(unsigned int*)(chunk + 0x80));
    (void)origin;  // contact origin is read to seed the impulse point below
    subobj_add_force_at_point(*(unsigned int*)(chunk + 0x80), seg + 9, force);

    if (u32_to_float_biased(rng_counter_tick()) / FLOAT_806195e4 < FLOAT_8061963c) {
        int blk = (int)DAT_80612a38;
        sound_init_play_params((int)params);
        *(unsigned char*)&params[10] = 1;
        params[0] = 0x4ff;
        params[3] = *(int*)(blk + 0x1cfc);
        params[4] = *(int*)(blk + 0x1d00);
        params[5] = *(int*)(blk + 0x1d04);
        sound_play_request(params);
    }
    *(int*)((int)DAT_80612a38 + actorCtx * 0x5c + 0x2c30) = chunk;
    FUN_8038273c();
}

// ===========================================================================
// 0x802976dc  snake3_rope_segments_update
// The per-frame tentacle/rope simulation. For each of the three enabled body
// segments it either (a) collapses the rope to a single point when the segment
// has <2 nodes, or (b) rebuilds each node from a swept sine wave and projects it
// against the collision surface (FUN_802b34fc). When the boss is "grabbing"
// (+0x2ddc) it steers the segment toward the head; otherwise it advances the
// idle sweep. Finally it fires the three head/segment muzzle projectiles when
// their charge timers cross threshold and updates the stun timer from the
// per-frame hit count.
// ===========================================================================
static void snake3_rope_segments_update(void) {
    int actorCtx = FUN_803826dc();
    int* owner = *(int**)(actorCtx + 0x7c);
    int blk = (int)DAT_80612a38;
    int local[16];   // sound-params scratch (reused per emit)

    FUN_802b34a4(blk + 0x1dc8);

    // Pass 1: rebuild / project each enabled segment's rope nodes.
    int segOff = 0;
    for (int s = 0; s < 3; s++) {
        if ((*(unsigned int*)(blk + 0x2df4) & (1 << s)) != 0) {
            int seg = blk + segOff;
            if (*(int*)(seg + 0x2c1c) < 2) {
                float* node0 = *(float**)(blk + segOff + 0x2c20);
                *(float*)((int)node0 + 0xc) = FLOAT_806196f8;
                node0[0] = *(float*)(blk + segOff + 0x2bf0);
                node0[1] = *(float*)(blk + segOff + 0x2bf4);
                node0[2] = *(float*)(blk + segOff + 0x2bf8);
                double d = FUN_802b34fc(blk + 0xf10, (float*)(blk + 0xee8),
                                        (float*)(blk + segOff + 0x2bfc),
                                        *(float**)(blk + segOff + 0x2c20));
                int n = *(int*)(blk + segOff + 0x2c20);
                if (d < (double)*(float*)(n + 0xc)) {
                    *(float*)(n + 0xc) = (float)d;
                }
            } else {
                *(float*)(seg + 0x2c10) = *(float*)(seg + 0x2c10) + FLOAT_806195a4;
                double c0 = math_poly_trig((double)*(float*)(blk + segOff + 0x2c14));
                double c1 = math_poly_trig((double)(*(float*)(blk + segOff + 0x2c14) - FLOAT_806196e8));
                int nodeOff = 0;
                for (unsigned int i = 0; (int)i < *(int*)(segOff + blk + 0x2c1c); i++) {
                    float* np = (float*)(*(int*)(segOff + blk + 0x2c20) + nodeOff);
                    np[3] = FLOAT_806196f8;
                    float t = (float)((double)(float)(FLOAT_80619588 *
                        (float)(FLOAT_806196e4 * u32_to_float_biased(i))) / (double)FLOAT_8061958c);
                    double s0 = math_poly_trig((double)(float)((double)*(float*)(segOff + blk + 0x2c10) + (double)t));
                    float w0 = (float)(c1 * s0);
                    double s1 = math_poly_trig((double)(float)((double)(float)((double)*(float*)(segOff + blk + 0x2c10) + (double)t) - (double)FLOAT_806196e8));
                    float w1 = (float)(c1 * s1);
                    int b = segOff + blk;
                    np[0] = (float)(c0 * (double)*(float*)(b + 0x2bf0) +
                        (double)(w1 * *(float*)(b + 0x2bd8) + (float)(w0 * (double)*(float*)(b + 0x2be4))));
                    b = segOff + blk;
                    np[1] = (float)(c0 * (double)*(float*)(b + 0x2bf4) +
                        (double)(w1 * *(float*)(b + 0x2bdc) + (float)(w0 * (double)*(float*)(b + 0x2be8))));
                    b = segOff + blk;
                    np[2] = (float)(c0 * (double)*(float*)(b + 0x2bf8) +
                        (double)(w1 * *(float*)(b + 0x2be0) + (float)(w0 * (double)*(float*)(b + 0x2bec))));
                    double dd = FUN_802b34fc(blk + 0xf10, (float*)(blk + 0xee8),
                                             (float*)(blk + segOff + 0x2bfc),
                                             (float*)(*(int*)(segOff + blk + 0x2c20) + nodeOff));
                    int nb = *(int*)(segOff + blk + 0x2c20);
                    if (dd < (double)*(float*)(nb + nodeOff + 0xc)) {
                        *(float*)(nb + nodeOff + 0xc) = (float)dd;
                    }
                    nodeOff += 0x10;
                }
            }
            if (*(float*)(blk + segOff + 0x2c2c) < FLOAT_80619568) {
                *(unsigned int*)(blk + 0x2df4) &= ~(1 << s);
                snake3_spawn_debris_chunk();
            }
        }
        segOff += 0x5c;
    }

    // Pass 2: sweep / steer each segment (idle sweep vs. grab-toward-head).
    if (*(int*)(blk + 0x2ddc) == 0) {
        int segOff2 = 0;
        for (int s = 0; s < 3; s++) {
            if (((*(unsigned int*)(blk + 0x2df4) & (1 << s)) != 0) &&
                ((*(unsigned int*)(blk + 0x2e00) & (1 << s)) != 0)) {
                int seg = blk + segOff2;
                if (*(int*)(seg + 0x2c18) == 1) {
                    *(float*)(seg + 0x2c0c) = *(float*)(seg + 0x2c0c) + FLOAT_80619560;
                    seg = blk + segOff2;
                    if (*(float*)(seg + 0x2c24) < *(float*)(seg + 0x2c0c)) {
                        *(int*)(seg + 0x2c18) = 0;
                    }
                } else {
                    *(float*)(seg + 0x2c0c) = *(float*)(seg + 0x2c0c) - FLOAT_80619560;
                    seg = blk + segOff2;
                    if (*(float*)(seg + 0x2c0c) < *(float*)(seg + 0x2c28)) {
                        *(int*)(seg + 0x2c18) = 1;
                    }
                }
                float phase = *(float*)(blk + segOff2 + 0x2c14);
                if (phase < FLOAT_80619790) {
                    *(float*)(blk + segOff2 + 0x2c14) = phase + FLOAT_80619560;
                }
                if (snake3_segment_sphere_sweep(s) == 1) {
                    if (*(int*)(blk + 0x2ddc) == 0) {
                        sound_init_play_params((int)local);
                        local[0] = 0x4fb;
                        *(float*)&local[9] = FLOAT_80619580;
                        sound_play_request(local);
                    }
                    *(int*)(blk + 0x2de0) = 0x5a;
                    *(int*)(blk + 0x2ddc) = 1;
                }
            }
            segOff2 += 0x5c;
        }
    } else {
        int segOff2 = 0;
        for (int s = 0; s < 3; s++) {
            if ((*(unsigned int*)(blk + 0x2df4) & (1 << s)) != 0) {
                *(float*)(blk + segOff2 + 0x2c08) = *(float*)(blk + segOff2 + 0x2c08) + FLOAT_80619580;
                float phase = *(float*)(blk + segOff2 + 0x2c14);
                if (FLOAT_80619568 < phase) {
                    *(float*)(blk + segOff2 + 0x2c14) = phase - FLOAT_80619560;
                }
                float dir[3];
                dir[0] = *(float*)(blk + 0x1d14) - *(float*)(blk + segOff2 + 0x2bfc);
                dir[1] = *(float*)(blk + 0x1d18) - *(float*)(blk + segOff2 + 0x2c00);
                dir[2] = *(float*)(blk + 0x1d1c) - *(float*)(blk + segOff2 + 0x2c04);
                float yawTgt, yawCur;
                dir_to_angles(dir, 0, &yawTgt);
                dir_to_angles((float*)(blk + segOff2 + 0x2bf0), 0, &yawCur);
                float d = yawTgt - yawCur;
                if (d <= FLOAT_8061958c) {
                    if (d < FLOAT_806195b0) d += FLOAT_806195ac;
                } else {
                    d -= FLOAT_806195ac;
                }
                int seg = blk + segOff2;
                float cur = *(float*)(seg + 0x2c0c);
                float tgt = cur + (FLOAT_80619588 * d) / FLOAT_8061958c;
                if (cur <= tgt) {
                    if (cur < tgt) {
                        *(int*)(seg + 0x2c18) = 1;
                        *(float*)(blk + segOff2 + 0x2c0c) = *(float*)(blk + segOff2 + 0x2c0c) + FLOAT_806195a4;
                        if (tgt < *(float*)(blk + segOff2 + 0x2c0c)) {
                            *(float*)(blk + segOff2 + 0x2c0c) = tgt;
                        }
                    }
                } else {
                    *(int*)(seg + 0x2c18) = 0;
                    *(float*)(blk + segOff2 + 0x2c0c) = *(float*)(blk + segOff2 + 0x2c0c) - FLOAT_806195a4;
                    if (*(float*)(blk + segOff2 + 0x2c0c) < tgt) {
                        *(float*)(blk + segOff2 + 0x2c0c) = tgt;
                    }
                }
                seg = blk + segOff2;
                if (*(float*)(seg + 0x2c0c) <= *(float*)(seg + 0x2c24)) {
                    if (*(float*)(seg + 0x2c0c) < *(float*)(seg + 0x2c28)) {
                        *(float*)(seg + 0x2c0c) = *(float*)(seg + 0x2c28);
                    }
                } else {
                    *(float*)(seg + 0x2c0c) = *(float*)(seg + 0x2c24);
                }
            }
            segOff2 += 0x5c;
        }
    }

    if (*(float*)(blk + 0x2de4) < FLOAT_80619568) {
        *(float*)(blk + 0x2de4) = *(float*)(blk + 0x2de4) + FLOAT_8061967c;
    }

    // Head muzzle projectile.
    if (FLOAT_806197a8 < *(float*)(blk + 0x2c08)) {
        player_ai_spawn_trail_particle(0, 0, 0xffffffff, (float*)(blk + 0x2bfc), (float*)(blk + 0x2bf0), -1);
        int b = (int)DAT_80612a38;
        sound_init_play_params((int)local);
        *(unsigned char*)&local[10] = 1;
        local[0] = 0xc72;
        local[3] = *(int*)(b + 0x2bfc);
        local[4] = *(int*)(b + 0x2c00);
        local[5] = *(int*)(b + 0x2c04);
        sound_play_request(local);
        player_ai_projectile_spawn_simple((double)FLOAT_80619590, (double)FLOAT_806195a8, 10,
            (int*)((int)DAT_80612a38 + 0x2bfc), (int*)((int)DAT_80612a38 + 0x2bf0), actorCtx,
            (int*)0, 6, 0, *(int*)(actorCtx + 0x34), 0x2010000a, 0xc);
        *(float*)(blk + 0x2de4) = FLOAT_80619620;
        *(float*)(blk + 0x2c08) = FLOAT_80619568;
    }

    // Two arm muzzle projectiles (+0x2c4c / +0x2ca8 emitters).
    if (FLOAT_806197ac < *(float*)(blk + 0x2c64)) {
        float pos[3];
        pos[0] = FLOAT_80619578 * *(float*)(blk + 0x2c4c) + *(float*)(blk + 0x2c58);
        pos[1] = FLOAT_80619578 * *(float*)(blk + 0x2c50) + *(float*)(blk + 0x2c5c);
        pos[2] = FLOAT_80619578 * *(float*)(blk + 0x2c54) + *(float*)(blk + 0x2c60);
        sound_init_play_params((int)local);
        *(unsigned char*)&local[10] = 1;
        local[0] = 0xc73;
        local[3] = *(int*)&pos[0];
        local[4] = *(int*)&pos[1];
        local[5] = *(int*)&pos[2];
        sound_play_request(local);
        player_ai_spawn_trail_particle(0, 0, 0xffffffff, (float*)(blk + 0x2c58), (float*)(blk + 0x2c4c), -1);
        player_ai_projectile_spawn_simple((double)FLOAT_80619590, (double)FLOAT_806195a8, 4,
            (int*)pos, (int*)((int)DAT_80612a38 + 0x2c4c), actorCtx, owner, 2, 0,
            *(int*)(actorCtx + 0x34), 0x2010000a, 5);
        *(float*)(blk + 0x2c64) = FLOAT_80619568;
    }
    if (FLOAT_806197ac < *(float*)(blk + 0x2cc0)) {
        float pos[3];
        pos[0] = FLOAT_80619578 * *(float*)(blk + 0x2ca8) + *(float*)(blk + 0x2cb4);
        pos[1] = FLOAT_80619578 * *(float*)(blk + 0x2cac) + *(float*)(blk + 0x2cb8);
        pos[2] = FLOAT_80619578 * *(float*)(blk + 0x2cb0) + *(float*)(blk + 0x2cbc);
        sound_init_play_params((int)local);
        *(unsigned char*)&local[10] = 1;
        local[0] = 0xc73;
        local[3] = *(int*)&pos[0];
        local[4] = *(int*)&pos[1];
        local[5] = *(int*)&pos[2];
        sound_play_request(local);
        player_ai_spawn_trail_particle(0, 0, 0xffffffff, (float*)(blk + 0x2cb4), (float*)(blk + 0x2ca8), -1);
        player_ai_projectile_spawn_simple((double)FLOAT_80619590, (double)FLOAT_806195a8, 4,
            (int*)pos, (int*)((int)DAT_80612a38 + 0x2ca8), actorCtx, owner, 2, 0,
            *(int*)(actorCtx + 0x34), 0x2010000a, 5);
        *(float*)(blk + 0x2cc0) = FLOAT_80619568;
    }

    // Stun timer from per-frame segment-sweep hit count.
    int hits = 0;
    for (int s = 0; s < 3; s++) {
        if ((*(unsigned int*)(blk + 0x2df4) & (1 << s)) != 0) {
            hits += snake3_segment_sphere_sweep(s);
        }
    }
    if (hits == 0) {
        *(int*)(blk + 0x2de0) = *(int*)(blk + 0x2de0) + -1;
        if (*(int*)(blk + 0x2de0) < 0) {
            *(int*)(blk + 0x2ddc) = 0;
        }
    } else {
        *(int*)(blk + 0x2de0) = 0x5a;
    }
    FUN_80382728();
}

// ===========================================================================
// 0x802980cc  snake3_camera_shake_players
// For each of the boss's 8 sub-body blocks (+0x11a0 stride 0x168) in state 5,
// shake the camera of any player within FLOAT_806197b0 (XZ). The active player
// record is passed in via the rig-begin's high word (idx*0x141c).
// ===========================================================================
static void snake3_camera_shake_players(int playerRecOff, float* focus) {
    int off = 0;
    for (int i = 0; i < 8; i++) {
        int blk = (int)DAT_80612a38 + off + 0x11a0;
        if (*(int*)(blk + 0xf8) == 5) {
            float dz = *(float*)(blk + 0x128) - focus[2];
            float dx = *(float*)(blk + 0x120) - focus[0];
            float d2 = dx * dx + dz * dz;
            if (d2 < FLOAT_806197b0) {
                double amt = (double)(FLOAT_806197b4 * (FLOAT_806197b0 - d2));
                player_apply_camera_shake((double)(float)((double)FLOAT_80619574 * amt), amt,
                    (double)(float)((double)FLOAT_806195a0 * amt), DAT_80611d78 + playerRecOff);
                double cap = (double)FLOAT_80619608;
                *(float*)(playerRecOff + DAT_80611d78 + 0xe54) = FLOAT_806196c0;
                if (cap < amt) amt = cap;
                *(float*)(playerRecOff + DAT_80611d78 + 0xe58) = (float)amt;
            }
        }
        off += 0x168;
    }
}

// ===========================================================================
// 0x802981c8  snake3_body_collision_damage
// Build the boss body capsule (FUN_802b1884) and, for each player, test the
// player origin against it (FUN_802b15a4). On overlap deal contact damage and,
// with an RNG gate, play the body-hit sound. Also drives per-player camera shake.
// ===========================================================================
static void snake3_body_collision_damage(void) {
    int teamId = FUN_803826e8();
    int blk = (int)DAT_80612a38;
    FUN_802b1884(blk + 0x11a0, blk + 0xf10, (float*)(blk + 0x1cfc), (float*)(blk + 0xee8));

    int recOff = 0;
    int params[16];
    int dmgParams[16];
    for (int p = 0; p < DAT_80612610; p++) {
        int prec = *(int*)(DAT_80611d78 + recOff + 0x14);
        float hitPt[3];
        int hit = FUN_802b15a4((int)DAT_80612a38 + 0x11a0, (float*)(prec + 100), hitPt);
        if (hit == 1) {
            switch_slot_reset((int)dmgParams);
            *(int*)&dmgParams[8] = prec + 100;              // local_54
            *(float**)&dmgParams[13] = (float*)hitPt;       // local_4c
            *(float*)&dmgParams[9]  = FLOAT_806195a0;       // local_5c
            *(int*)&dmgParams[10]   = 0;                    // local_58
            *(int*)&dmgParams[11]   = 0x24;                 // local_64 region
            *(float*)&dmgParams[14] = FLOAT_80619584;       // local_48
            *(float*)&dmgParams[15] = FLOAT_8061956c;       // local_30
            *(int*)&dmgParams[0]    = prec;                 // local_7c
            *(int*)&dmgParams[1]    = teamId;               // local_78
            *(int*)&dmgParams[2]    = teamId;               // local_74
            damage_dispatch_to_entity((double)FLOAT_80619578, (double)FLOAT_8061956c,
                (double)FLOAT_80619584, 0, 0, 0, 0, 0, dmgParams);
            if (u32_to_float_biased(rng_counter_tick()) / FLOAT_806195e4 < FLOAT_806196d8) {
                int b = (int)DAT_80612a38;
                sound_init_play_params((int)params);
                *(unsigned char*)&params[10] = 1;
                params[0] = 0x4fd;
                params[3] = *(int*)(b + 0x1cfc);
                params[4] = *(int*)(b + 0x1d00);
                params[5] = *(int*)(b + 0x1d04);
                sound_play_request(params);
            }
        }
        snake3_camera_shake_players(recOff, (float*)(prec + 100));
        recOff += 0x141c;
    }
    FUN_80382734();
}

// ===========================================================================
// 0x80298338  snake3_projectile_hit_react
// Damage-routing callback for projectiles that hit the boss. If the projectile
// came from a player it finds which body part was struck (one of three segments,
// the head, or the tail) and subtracts the projectile's damage from that part's
// accumulator. Player rockets (subtype 0x17) route to the nearest part; other
// projectiles do a swept capsule/sphere test to pick the part.
// ===========================================================================
void snake3_projectile_hit_react(int proj) {
    float* dir = *(float**)(proj + 0x30);
    if (dir == (float*)0) return;
    float* pos = *(float**)(proj + 0x28);
    if (pos == (float*)0) return;
    int blk = (int)DAT_80612a38;

    // Only react to player-owned projectiles.
    bool byPlayer = false;
    int recOff = 0, n = DAT_80612610;
    if (0 < DAT_80612610) {
        do {
            if (*(int*)(proj + 4) == *(int*)(DAT_80611d78 + recOff + 0x14)) { byPlayer = true; break; }
            recOff += 0x141c; n += -1;
        } while (n != 0);
    }
    if (!byPlayer) return;

    if (*(int*)(proj + 0x18) == 0x17) {   // rocket: nearest-part routing
        if (player_ai_get_active_index() != 4) return;
        int part = 0, idx = 0, segOff = 0;
        float best = FLOAT_8061964c;
        for (int i = 3; i != 0; i--) {
            pos = *(float**)(proj + 0x28);
            int seg = blk + segOff;
            float dy = *(float*)(seg + 0x2c00) - pos[1];
            float dx = *(float*)(seg + 0x2bfc) - pos[0];
            float dz = *(float*)(seg + 0x2c04) - pos[2];
            float d2 = dz * dz + dx * dx + dy * dy;
            if (d2 < best) { part = idx; best = d2; }
            idx++; segOff += 0x5c;
        }
        pos = *(float**)(proj + 0x28);
        {
            float dy = *(float*)(blk + 0x1d40) - pos[1];
            float dx = *(float*)(blk + 0x1d3c) - pos[0];
            float dz = *(float*)(blk + 0x1d44) - pos[2];
            float d2 = dz * dz + dx * dx + dy * dy;
            if (d2 < best) { part = 3; best = d2; }
        }
        {
            float dy = *(float*)(blk + 0x1d4c) - pos[1];
            float dx = *(float*)(blk + 0x1d48) - pos[0];
            float dz = *(float*)(blk + 0x1d50) - pos[2];
            if (dz * dz + dx * dx + dy * dy < best) part = 4;
        }
        if (part < 3) {
            int seg = blk + part * 0x5c;
            *(float*)(seg + 0x2c2c) = -(FLOAT_8061959c * *(float*)(proj + 0x20) - *(float*)(seg + 0x2c2c));
        } else if (part == 3) {
            *(float*)(blk + 0x1d84) = -(FLOAT_8061959c * *(float*)(proj + 0x20) - *(float*)(blk + 0x1d84));
        } else {
            *(float*)(blk + 0x1d88) = -(FLOAT_8061959c * *(float*)(proj + 0x20) - *(float*)(blk + 0x1d88));
        }
        return;
    }

    // Non-rocket: swept sphere test against parts to pick one hit.
    bool hit = false;
    float py = pos[1];
    float px = pos[0];
    float e0 = py - *(float*)(blk + 0x1d98);
    float f0 = py - (FLOAT_80619648 + *(float*)(blk + 0x1d00));
    float g0 = px - *(float*)(blk + 0x1cfc);
    float pz = pos[2];
    float h0 = px - *(float*)(blk + 0x1d94);
    float i0 = pz - *(float*)(blk + 0x1d04);
    float j0 = pz - *(float*)(blk + 0x1d9c);
    float dHead = i0 * i0 + g0 * g0 + f0 * f0;
    float dTail = j0 * j0 + h0 * h0 + e0 * e0;
    float dMin = dTail < dHead ? dTail : dHead;

    float rx = FLOAT_80619574 * (*(float*)(blk + 0x1da4) + *(float*)(blk + 0x2c00)) - py;
    float ry = FLOAT_80619574 * (*(float*)(blk + 0x1da0) + *(float*)(blk + 0x2bfc)) - px;
    float rz = FLOAT_80619574 * (*(float*)(blk + 0x1da8) + *(float*)(blk + 0x2c04)) - pz;
    float rSq = rz * rz + ry * ry + rx * rx;
    if (rSq < dMin) {
        float dot = rz * dir[2] + ry * dir[0] + rx * dir[1];
        if ((FLOAT_80619568 < dot) && (-(dot * dot - rSq) < FLOAT_80619770)) {
            hit = true;
            *(float*)(blk + 0x2c2c) = *(float*)(blk + 0x2c2c) - *(float*)(proj + 0x20);
        }
    }
    if (!hit) {
        int segOff = 0x5c;
        for (int i = 2; i != 0; i--) {
            pos = *(float**)(proj + 0x28);
            int seg = blk + segOff;
            float a = *(float*)(seg + 0x2c00) - pos[1];
            float b = *(float*)(seg + 0x2bfc) - pos[0];
            float c = *(float*)(seg + 0x2c04) - pos[2];
            float d2 = c * c + b * b + a * a;
            if (d2 < dMin) {
                dir = *(float**)(proj + 0x30);
                float dot = c * dir[2] + b * dir[0] + a * dir[1];
                if ((FLOAT_80619568 < dot) && (-(dot * dot - d2) < FLOAT_80619564)) {
                    hit = true;
                    *(float*)(seg + 0x2c2c) = *(float*)(seg + 0x2c2c) - *(float*)(proj + 0x20);
                    break;
                }
            }
            segOff += 0x5c;
        }
    }
    if (!hit) {
        pos = *(float**)(proj + 0x28);
        float a = *(float*)(blk + 0x1d40) - pos[1];
        float b = *(float*)(blk + 0x1d3c) - pos[0];
        float c = *(float*)(blk + 0x1d44) - pos[2];
        float d2 = c * c + b * b + a * a;
        if (d2 < dMin) {
            dir = *(float**)(proj + 0x30);
            float dot = c * dir[2] + b * dir[0] + a * dir[1];
            if ((FLOAT_80619568 < dot) && (-(dot * dot - d2) < FLOAT_80619564)) {
                hit = true;
                *(float*)(blk + 0x1d84) = *(float*)(blk + 0x1d84) - *(float*)(proj + 0x20);
            }
        }
    }
    if (!hit) {
        pos = *(float**)(proj + 0x28);
        float a = *(float*)(blk + 0x1d4c) - pos[1];
        float b = *(float*)(blk + 0x1d48) - pos[0];
        float c = *(float*)(blk + 0x1d50) - pos[2];
        float d2 = c * c + b * b + a * a;
        if (d2 < dMin) {
            dir = *(float**)(proj + 0x30);
            float dot = c * dir[2] + b * dir[0] + a * dir[1];
            if ((FLOAT_80619568 < dot) && (-(dot * dot - d2) < FLOAT_80619564)) {
                *(float*)(blk + 0x1d88) = *(float*)(blk + 0x1d88) - *(float*)(proj + 0x20);
            }
        }
    }
}

// ===========================================================================
// 0x802987ec  snake3_push_players_out
// Push any player standing inside the boss head's XZ radius back out along the
// radial direction (keeps players from clipping through the head).
// ===========================================================================
static void snake3_push_players_out(void) {
    double radius = (double)FLOAT_806197a8;
    int recOff = 0;
    for (int p = 0; p < DAT_80612610; p++) {
        double dz = (double)(*(float*)(DAT_80611d78 + recOff + 0xb0) - *(float*)((int)DAT_80612a38 + 0x1d04));
        double dx = (double)(*(float*)(DAT_80611d78 + recOff + 0xa8) - *(float*)((int)DAT_80612a38 + 0x1cfc));
        double d2 = (double)(float)(dx * dx + (double)(float)(dz * dz));
        if (d2 < radius) {
            double d = math_sqrt(d2);
            double scale = (double)FLOAT_80619624;
            *(float*)(DAT_80611d78 + recOff + 0xa8) =
                *(float*)((int)DAT_80612a38 + 0x1cfc) + (float)(dx * (double)(float)(scale / d));
            *(float*)(DAT_80611d78 + recOff + 0xb0) =
                *(float*)((int)DAT_80612a38 + 0x1d04) + (float)(dz * (double)(float)(scale / d));
            *(int*)(DAT_80611d78 + recOff + 0xb4) = *(int*)(DAT_80611d78 + recOff + 0xa8);
            *(int*)(DAT_80611d78 + recOff + 0xbc) = *(int*)(DAT_80611d78 + recOff + 0xb0);
        }
        recOff += 0x141c;
    }
}

// ===========================================================================
// 0x80298904  snake3_boss_behavior_tick
// The master per-frame FSM for the serpent boss (state = actor +0x20, range
// 0..0x12). Every frame it: retargets a visible teammate, integrates a hit-stun
// wobble, caches the head/aim bone world points (bones 4/5/7/8/10 via
// FUN_802a472c) into the control block, runs the body-collision / projectile /
// rope / attractor sub-updates, binds the rope render segments (FUN_802aeccc),
// then dispatches the state switch (spawn/intro/rise/attack-select/lunge/recover/
// death). Bones are only recached while the boss is alive (+0xb7e == 0).
// ===========================================================================
void snake3_boss_behavior_tick(void) {
    int actorCtx = FUN_803826c0();
    int rec = *(int*)(actorCtx + 0x7c);
    float localMtx[16];   // afStack_98
    float basis[16];      // local_d8.. mtx4_multiply output block
    float pts[24];        // local_f0.. six transformed leg points
    unsigned int renderMask = 0;

    aiactor_retarget_visible_teammate();
    if (-1 < *(int*)(rec + 0x9d0)) {
        if (*(int*)(rec + 0x9d0) != *(int*)(rec + 0x20)) {
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20)  = *(int*)(rec + 0x9d0);
        }
        *(int*)(rec + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(rec + 0x2c) & 0x80) == 0) {
        *(float*)(rec + 0xa90) = FLOAT_80619568;
    } else {
        *(float*)(rec + 0xa90) = FLOAT_806108ec *
            (FLOAT_80619580 / u32_to_float_biased((unsigned)DAT_806109c0)) + *(float*)(rec + 0xa90);
    }

    // Spawn-vector rodata copied to leg point locals (used by the leg cache).
    pts[0]=*(float*)&DAT_803e06d0; pts[1]=*(float*)&DAT_803e06d4; pts[2]=*(float*)&DAT_803e06d8;
    pts[3]=*(float*)&DAT_803e06dc; pts[4]=*(float*)&DAT_803e06e0; pts[5]=*(float*)&DAT_803e06e4;
    pts[6]=*(float*)&DAT_803e06e8; pts[7]=*(float*)&DAT_803e06ec; pts[8]=*(float*)&DAT_803e06f0;
    pts[9]=*(float*)&DAT_803e06f4; pts[10]=*(float*)&DAT_803e06f8; pts[11]=*(float*)&DAT_803e06fc;
    pts[12]=*(float*)&DAT_803e0700; pts[13]=*(float*)&DAT_803e0704; pts[14]=*(float*)&DAT_803e0708;
    pts[15]=*(float*)&DAT_803e070c; pts[16]=*(float*)&DAT_803e0710; pts[17]=*(float*)&DAT_803e0714;

    int viewRec = *(int*)(DAT_80611d78 + 0x14);
    *(unsigned int*)(rec + 0x28) |= 0x4000;
    *(float*)(rec + 0xb3c) = FLOAT_80619568;
    DAT_80612a38[0x745] = *(int*)(viewRec + 100);
    DAT_80612a38[0x746] = *(int*)(viewRec + 0x68);
    DAT_80612a38[0x747] = *(int*)(viewRec + 0x6c);
    entity_build_local_matrix(localMtx, actorCtx, 1, 0);
    DAT_80612a38[0x73f] = *(int*)(actorCtx + 100);
    DAT_80612a38[0x740] = *(int*)(actorCtx + 0x68);
    DAT_80612a38[0x741] = *(int*)(actorCtx + 0x6c);

    // Cache the head-aim bone (index 4) world midpoint into +0x765.
    int* skel = *(int**)(*(int*)(actorCtx + 4) + 4);
    int* bone = FUN_802a472c(skel, 4);
    float k = FLOAT_80619574;
    float* bp = (float*)bone[0x28];
    *(float*)&DAT_80612a38[0x765] = bp[3] - bp[0];
    *(float*)&DAT_80612a38[0x766] = bp[4] - bp[1];
    *(float*)&DAT_80612a38[0x767] = bp[5] - bp[2];
    *(float*)&DAT_80612a38[0x765] = k * *(float*)&DAT_80612a38[0x765] + bp[0];
    *(float*)&DAT_80612a38[0x766] = k * *(float*)&DAT_80612a38[0x766] + bp[1];
    *(float*)&DAT_80612a38[0x767] = k * *(float*)&DAT_80612a38[0x767] + bp[2];
    mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)bone + 0x3d) * 0x40),
                         (float*)(DAT_80612a38 + 0x765));
    mtx4_transform_point(localMtx, (float*)(DAT_80612a38 + 0x765));
    *(float*)&DAT_80612a38[0x742] = FLOAT_8061963c * (*(float*)(actorCtx + 100) - *(float*)&DAT_80612a38[0x765]) + *(float*)&DAT_80612a38[0x765];
    *(float*)&DAT_80612a38[0x743] = FLOAT_8061963c * (*(float*)(actorCtx + 0x68) - *(float*)&DAT_80612a38[0x766]) + *(float*)&DAT_80612a38[0x766];
    *(float*)&DAT_80612a38[0x744] = FLOAT_8061963c * (*(float*)(actorCtx + 0x6c) - *(float*)&DAT_80612a38[0x767]) + *(float*)&DAT_80612a38[0x767];
    FUN_802b0cb4((double)*(float*)(viewRec + 100), (double)*(float*)(viewRec + 0x6c),
                 (double)*(float*)&DAT_80612a38[0x742], (double)*(float*)&DAT_80612a38[0x744],
                 (int)(DAT_80612a38 + 1));
    snake3_push_players_out();
    *(float*)&DAT_80612a38[0x766] = *(float*)&DAT_80612a38[0x766] - FLOAT_80619578;

    if (DAT_80612a38[0xb7e] == 0) {
        // Cache the two arm bones (5, 7) + shoulder basis, plus tail bones (8, 10).
        skel = *(int**)(*(int*)(actorCtx + 4) + 4);
        bone = FUN_802a472c(skel, 5);
        k = FLOAT_80619574; bp = (float*)bone[0x28];
        *(float*)&DAT_80612a38[0x768] = k * (bp[3] - bp[0]) + bp[0];
        *(float*)&DAT_80612a38[0x769] = k * (bp[4] - bp[1]) + bp[1];
        *(float*)&DAT_80612a38[0x76a] = k * (bp[5] - bp[2]) + bp[2];
        mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)bone + 0x3d) * 0x40), (float*)(DAT_80612a38 + 0x768));
        mtx4_transform_point(localMtx, (float*)(DAT_80612a38 + 0x768));

        bone = FUN_802a472c(*(int**)(*(int*)(actorCtx + 4) + 4), 7);
        k = FLOAT_80619574; bp = (float*)bone[0x28];
        *(float*)&DAT_80612a38[0xaff] = k * (bp[3] - bp[0]) + bp[0];
        *(float*)&DAT_80612a38[0xb00] = k * (bp[4] - bp[1]) + bp[1];
        *(float*)&DAT_80612a38[0xb01] = k * (bp[5] - bp[2]) + bp[2];
        int boneMtx7 = *(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)bone + 0x3d) * 0x40;
        mtx4_transform_point((float*)boneMtx7, (float*)(DAT_80612a38 + 0xaff));
        mtx4_transform_point(localMtx, (float*)(DAT_80612a38 + 0xaff));
        mtx4_multiply((float*)boneMtx7, localMtx, basis);
        DAT_80612a38[0xaf6]=*(int*)&basis[8];  DAT_80612a38[0xaf7]=*(int*)&basis[9];  DAT_80612a38[0xaf8]=*(int*)&basis[10];
        DAT_80612a38[0xaf9]=*(int*)&basis[4];  DAT_80612a38[0xafa]=*(int*)&basis[5];  DAT_80612a38[0xafb]=*(int*)&basis[6];
        DAT_80612a38[0xafc]=*(int*)&basis[0];  DAT_80612a38[0xafd]=*(int*)&basis[1];  DAT_80612a38[0xafe]=*(int*)&basis[2];

        bone = FUN_802a472c(*(int**)(*(int*)(actorCtx + 4) + 4), 8);
        k = FLOAT_80619574; bp = (float*)bone[0x28];
        *(float*)&DAT_80612a38[0xb16] = k * (bp[3] - bp[0]) + bp[0];
        *(float*)&DAT_80612a38[0xb17] = k * (bp[4] - bp[1]) + bp[1];
        *(float*)&DAT_80612a38[0xb18] = k * (bp[5] - bp[2]) + bp[2];
        mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)bone + 0x3d) * 0x40), (float*)(DAT_80612a38 + 0xb16));
        mtx4_transform_point(localMtx, (float*)(DAT_80612a38 + 0xb16));
        mtx4_multiply((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x180), localMtx, basis);
        DAT_80612a38[0xb0d]=*(int*)&basis[4]; DAT_80612a38[0xb0e]=*(int*)&basis[5]; DAT_80612a38[0xb0f]=*(int*)&basis[6];
        DAT_80612a38[0xb10]=*(int*)&basis[0]; DAT_80612a38[0xb11]=*(int*)&basis[1]; DAT_80612a38[0xb12]=*(int*)&basis[2];
        {
            double c = math_poly_trig((double)FLOAT_806197bc);
            double sxz = (double)(float)((double)basis[2] * c);
            double s = math_poly_trig((double)FLOAT_806197b8);
            *(float*)&DAT_80612a38[0xb13] = (float)((double)basis[0] * s - sxz);
            *(int*)&DAT_80612a38[0xb14] = *(int*)&basis[1];
            c = math_poly_trig((double)FLOAT_806197b8);
            double cxz = (double)(float)((double)basis[2] * c);
            s = math_poly_trig((double)FLOAT_806197bc);
            *(float*)&DAT_80612a38[0xb15] = (float)((double)basis[0] * s + cxz);
        }

        bone = FUN_802a472c(*(int**)(*(int*)(actorCtx + 4) + 4), 10);
        k = FLOAT_80619574; bp = (float*)bone[0x28];
        *(float*)&DAT_80612a38[0xb2d] = k * (bp[3] - bp[0]) + bp[0];
        *(float*)&DAT_80612a38[0xb2e] = k * (bp[4] - bp[1]) + bp[1];
        *(float*)&DAT_80612a38[0xb2f] = k * (bp[5] - bp[2]) + bp[2];
        mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)bone + 0x3d) * 0x40), (float*)(DAT_80612a38 + 0xb2d));
        mtx4_transform_point(localMtx, (float*)(DAT_80612a38 + 0xb2d));
        mtx4_multiply((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x200), localMtx, basis);
        DAT_80612a38[0xb24]=*(int*)&basis[4]; DAT_80612a38[0xb25]=*(int*)&basis[5]; DAT_80612a38[0xb26]=*(int*)&basis[6];
        DAT_80612a38[0xb27]=*(int*)&basis[0]; DAT_80612a38[0xb28]=*(int*)&basis[1]; DAT_80612a38[0xb29]=*(int*)&basis[2];
        DAT_80612a38[0xb2a]=*(int*)&basis[8]; DAT_80612a38[0xb2b]=*(int*)&basis[9]; DAT_80612a38[0xb2c]=*(int*)&basis[10];
        {
            double c = math_poly_trig((double)FLOAT_806197c4);
            double sxz = (double)(float)((double)basis[10] * c);
            double s = math_poly_trig((double)FLOAT_806197c0);
            *(float*)&DAT_80612a38[0xb2a] = (float)((double)basis[8] * s - sxz);
            *(int*)&DAT_80612a38[0xb2b] = *(int*)&basis[9];
            c = math_poly_trig((double)FLOAT_806197c0);
            double cxz = (double)(float)((double)basis[10] * c);
            s = math_poly_trig((double)FLOAT_806197c4);
            *(float*)&DAT_80612a38[0xb2c] = (float)((double)basis[8] * s + cxz);
        }

        // Six leg points transformed through bone 4.
        bone = FUN_802a472c(*(int**)(*(int*)(actorCtx + 4) + 4), 4);
        int legMtx = *(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)bone + 0x3d) * 0x40;
        for (int i = 0; i < 6; i++) {
            mtx4_transform_point((float*)legMtx, &pts[i * 3]);
            mtx4_transform_point(localMtx, &pts[i * 3]);
        }
        DAT_80612a38[0x752]=*(int*)&pts[0];  DAT_80612a38[0x753]=*(int*)&pts[1];  DAT_80612a38[0x754]=*(int*)&pts[2];
        DAT_80612a38[0x74f]=*(int*)&pts[3];  DAT_80612a38[0x750]=*(int*)&pts[4];  DAT_80612a38[0x751]=*(int*)&pts[5];
        DAT_80612a38[0x758]=*(int*)&pts[6];  DAT_80612a38[0x759]=*(int*)&pts[7];  DAT_80612a38[0x75a]=*(int*)&pts[8];
        DAT_80612a38[0x755]=*(int*)&pts[9];  DAT_80612a38[0x756]=*(int*)&pts[10]; DAT_80612a38[0x757]=*(int*)&pts[11];
        *(float*)&DAT_80612a38[0x75e]=pts[12]-pts[6]; *(float*)&DAT_80612a38[0x75f]=pts[13]-pts[7]; *(float*)&DAT_80612a38[0x760]=pts[14]-pts[8];
        *(float*)&DAT_80612a38[0x75b]=pts[15]-pts[9]; *(float*)&DAT_80612a38[0x75c]=pts[16]-pts[10]; *(float*)&DAT_80612a38[0x75d]=pts[17]-pts[11];
    }

    snake3_body_collision_damage();
    snake3_dual_projectile_launch();
    snake3_rope_segments_update();
    if (3 < *(int*)(rec + 0x20)) {
        FUN_802af678();
    }
    snake3_rope_attractors_update();

    // Compute the render segment enable mask + accumulate the boss visual scale.
    if (DAT_80612a38[0xb80] != 0) {
        *(int*)(actorCtx + 0x1cc) = DAT_80612a38[0x771];
        if ((DAT_80612a38[0xb7d] & 1) == 0) { renderMask = 1; }
        else { *(float*)(actorCtx + 0x1cc) = *(float*)(actorCtx + 0x1cc) + *(float*)&DAT_80612a38[0xb0b]; }
        if ((DAT_80612a38[0xb7d] & 2) == 0) { renderMask |= 2; }
        else { *(float*)(actorCtx + 0x1cc) = *(float*)(actorCtx + 0x1cc) + *(float*)&DAT_80612a38[0xb22]; }
        if ((DAT_80612a38[0xb7d] & 4) == 0) { renderMask |= 4; }
        else { *(float*)(actorCtx + 0x1cc) = *(float*)(actorCtx + 0x1cc) + *(float*)&DAT_80612a38[0xb39]; }
        if ((DAT_80612a38[0xb7d] & 8) == 0) { renderMask |= 8; }
        else { *(float*)(actorCtx + 0x1cc) = *(float*)(actorCtx + 0x1cc) + *(float*)&DAT_80612a38[0x761]; }
        if ((DAT_80612a38[0xb7d] & 0x20) == 0) { renderMask |= 0x10; }
        else { *(float*)(actorCtx + 0x1cc) = *(float*)(actorCtx + 0x1cc) + *(float*)&DAT_80612a38[0x762]; }
    }
    FUN_802aeccc((int)(DAT_80612a38 + 0xaff), (int)(DAT_80612a38 + 0xb16), (int)(DAT_80612a38 + 0xb2d),
                 (int)(DAT_80612a38 + 0x74f), (int)(DAT_80612a38 + 0x752), renderMask);

    *(unsigned int*)(actorCtx + 0x9c) &= 0xfbffffff;
    *(float*)&DAT_80612a38[0xb7c] = FLOAT_806108e8 * (FLOAT_80619580 / u32_to_float_biased((unsigned)DAT_806109c0)) + *(float*)&DAT_80612a38[0xb7c];

    int state = *(int*)(rec + 0x20);
    switch (state) {
    case 0:
        *(unsigned int*)(rec + 0x28) |= 0x4000000;
        *(float*)(rec + 0x9e0) = FLOAT_806195f4;
        *(float*)(rec + 0x9dc) = FLOAT_80619648;
        *(float*)(actorCtx + 100)  = FLOAT_806197cc;
        *(float*)(actorCtx + 0xc4) = FLOAT_806195cc;
        *(float*)(actorCtx + 0x70) = FLOAT_806195cc;
        *DAT_80612a38 = 1;
        DAT_80612a38[0x749] = 1;
        DAT_80612a38[0x748] = 1;
        DAT_80612a38[0x74a] = 0;
        DAT_80612a38[0xb7e] = 0;
        *(float*)&DAT_80612a38[0xb7f] = FLOAT_80619568;
        DAT_80612a38[0xb80] = 0;
        FUN_802b0cb4((double)*(float*)(viewRec + 100), (double)*(float*)(viewRec + 0x6c),
                     (double)*(float*)(actorCtx + 100), (double)*(float*)(actorCtx + 0x6c),
                     (int)(DAT_80612a38 + 1));
        *(int*)(actorCtx + 100)  = DAT_80612a38[DAT_80612a38[0x466] * 0x46 + DAT_80612a38[0x467] * 10 + 0x260];
        *(int*)(actorCtx + 0x6c) = DAT_80612a38[DAT_80612a38[0x466] * 0x46 + DAT_80612a38[0x467] * 10 + 0x261];
        *DAT_80612a38 = 1;
        sprite_get_texture_id(0x300);
        FUN_802ad80c((double)FLOAT_80619568, (double)FLOAT_80619568, (double)FLOAT_80619568, (double)FLOAT_80619580);
        sprite_get_texture_id(0x300);
        FUN_802af38c();
        entity_set_render_type(actorCtx, (void*)&snake3_projectile_hit_react);
        entity_register_for_update(actorCtx, 2, -0x7fd699e0);
        entity_register_for_update(actorCtx, 4, 0);
        *(float*)(actorCtx + 0x1cc) =
            *(float*)&DAT_80612a38[0x762] + *(float*)&DAT_80612a38[0x761] +
            *(float*)&DAT_80612a38[0xb39] + *(float*)&DAT_80612a38[0xb22] +
            *(float*)&DAT_80612a38[0x771] + *(float*)&DAT_80612a38[0xb0b];
        *(float*)(actorCtx + 0x1d8) = FLOAT_806197d0;
        *(int*)(DAT_80611d74 + 0xe8c) = actorCtx;
        {
            int slot = screen_effect_alloc_slot();
            *(int*)(actorCtx + 0x268) = slot;
            screen_effect_init_slot(*(int*)(actorCtx + 0x268), actorCtx);
        }
        if (*(int*)(DAT_80611d78 + 0x114) == 0x100) {
            if (*(int*)(rec + 0x20) != 2) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 2;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        } else if (*(int*)(rec + 0x20) != 1) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = 1;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        *(float*)(&DAT_8048ca9c + *(short*)(rec + 0x48) * 0x30) = FLOAT_806195f8;
        break;
    case 1:
        if ((*(int*)(DAT_80611d78 + 0x114) == 0x100) && (state != 2)) {
            *(int*)(rec + 0x9d4) = state;
            *(int*)(rec + 0x20) = 2;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        break;
    case 2: {
        *(float*)&DAT_80612a38[0xb7f] = FLOAT_806108e8 * (FLOAT_80619580 / u32_to_float_biased((unsigned)DAT_806109c0)) + *(float*)&DAT_80612a38[0xb7f];
        float cap = FLOAT_806195c0;
        if (((DAT_80612a38[0xb80] & 8) != 0) && (*(float*)&DAT_80612a38[0x761] < cap)) {
            *(float*)&DAT_80612a38[0x761] = *(float*)&DAT_80612a38[0x761] + FLOAT_806197d4;
            if (cap < *(float*)&DAT_80612a38[0x761]) *(float*)&DAT_80612a38[0x761] = cap;
        }
        if (((DAT_80612a38[0xb80] & 0x20) != 0) && (*(float*)&DAT_80612a38[0x762] < cap)) {
            *(float*)&DAT_80612a38[0x762] = *(float*)&DAT_80612a38[0x762] + FLOAT_806197d4;
            if (cap < *(float*)&DAT_80612a38[0x762]) *(float*)&DAT_80612a38[0x762] = cap;
        }
        if (((DAT_80612a38[0xb80] & 2) != 0) && (*(float*)&DAT_80612a38[0xb22] < cap)) {
            *(float*)&DAT_80612a38[0xb22] = *(float*)&DAT_80612a38[0xb22] + FLOAT_806197d4;
            if (cap < *(float*)&DAT_80612a38[0xb22]) *(float*)&DAT_80612a38[0xb22] = cap;
        }
        if (((DAT_80612a38[0xb80] & 4) != 0) && (*(float*)&DAT_80612a38[0xb39] < cap)) {
            *(float*)&DAT_80612a38[0xb39] = *(float*)&DAT_80612a38[0xb39] + FLOAT_806197d4;
            if (cap < *(float*)&DAT_80612a38[0xb39]) *(float*)&DAT_80612a38[0xb39] = cap;
        }
        float capB = FLOAT_8061965c;
        if (((DAT_80612a38[0xb80] & 1) != 0) && (*(float*)&DAT_80612a38[0xb0b] < capB)) {
            *(float*)&DAT_80612a38[0xb0b] = *(float*)&DAT_80612a38[0xb0b] + FLOAT_806197d8;
            if (capB < *(float*)&DAT_80612a38[0xb0b]) *(float*)&DAT_80612a38[0xb0b] = capB;
        }
        if (*(int*)(DAT_80611d78 + 0x114) == 0x100) {
            if (FLOAT_806197dc < *(float*)&DAT_80612a38[0xb7f]) {
                anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                worm_play_positional_sound((double)FLOAT_80619578, 0x4ff, (unsigned int*)(actorCtx + 100));
                if (*(int*)(rec + 0x20) != 3) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 3;
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
                *(float*)&DAT_80612a38[0xb7f] = FLOAT_80619568;
            }
        } else {
            DAT_80612a38[0xb80] = 0x2f;
            worm_play_positional_sound((double)FLOAT_80619578, 0x4ff, (unsigned int*)(actorCtx + 100));
            if (*(int*)(rec + 0x20) != 4) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 4;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
            *(float*)&DAT_80612a38[0xb7f] = FLOAT_80619580;
            DAT_80612a38[0xb77] = 0;
            *(float*)&DAT_80612a38[0xb0b] = FLOAT_8061965c;
            *(float*)&DAT_80612a38[0xb22] = FLOAT_806195c0;
            *(float*)&DAT_80612a38[0xb39] = FLOAT_806195c0;
            *(float*)&DAT_80612a38[0x761] = FLOAT_806195c0;
            *(float*)&DAT_80612a38[0x762] = FLOAT_806195c0;
        }
        break;
    }
    case 3:
        if (anim_active_slot_userflags(*(int*)(actorCtx + 4)) != 0) {
            *(float*)&DAT_80612a38[0xb7f] = FLOAT_806108e8 * (FLOAT_80619580 / u32_to_float_biased((unsigned)DAT_806109c0)) + *(float*)&DAT_80612a38[0xb7f];
            if (FLOAT_80619580 < *(float*)&DAT_80612a38[0xb7f]) {
                *(float*)&DAT_80612a38[0xb0b] = FLOAT_8061965c;
                *(float*)&DAT_80612a38[0xb22] = FLOAT_806195c0;
                *(float*)&DAT_80612a38[0xb39] = FLOAT_806195c0;
                *(float*)&DAT_80612a38[0x761] = FLOAT_806195c0;
                *(float*)&DAT_80612a38[0x762] = FLOAT_806195c0;
                DAT_80612a38[0xb77] = 0;
                if (*(int*)(rec + 0x20) != 4) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 4;
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
            }
        }
        break;
    case 4: {
        int sel = FUN_802b0e3c(DAT_80612a38 + 0x464, DAT_80612a38 + 0x466);
        DAT_80612a38[0xb80] = 0x2f;
        if (DAT_80612a38[0xb77] == 0) {
            if ((*(float*)&DAT_80612a38[0xb7c] <= FLOAT_80619624) && ((unsigned)sel != 0xffffffff)) {
                if ((FLOAT_806195fc <= u32_to_float_biased(rng_counter_tick()) / FLOAT_806195e4) &&
                    ((DAT_80612a38[0xb7a] != 0xb) || ((int)DAT_80612a38[0xb7b] < 1))) {
                    if (DAT_80612a38[0xb7a] == 0xb) { DAT_80612a38[0xb7b] = DAT_80612a38[0xb7b] + 1; }
                    else { DAT_80612a38[0xb7b] = 0; DAT_80612a38[0xb7a] = 0xb; }
                    if (sel == DAT_80612a38[0x748]) {
                        anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                        if (*(int*)(rec + 0x20) != 0xb) {
                            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                            *(int*)(rec + 0x20) = 0xb;
                            *(float*)(rec + 0x9d8) = FLOAT_80619568;
                            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                        }
                    } else {
                        DAT_80612a38[0x749] = sel;
                        *(float*)&DAT_80612a38[0x74d] = FLOAT_80619580;
                        *(float*)&DAT_80612a38[0x74e] = FLOAT_80619568;
                        if (*(int*)(rec + 0x20) != 6) {
                            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                            *(int*)(rec + 0x20) = 6;
                            *(float*)(rec + 0x9d8) = FLOAT_80619568;
                            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                        }
                    }
                    break;
                }
            }
            if (DAT_80612a38[0xb7a] == 10) { DAT_80612a38[0xb7b] = DAT_80612a38[0xb7b] + 1; }
            else { DAT_80612a38[0xb7b] = 0; DAT_80612a38[0xb7a] = 10; }
            if (*(int*)(rec + 0x20) != 5) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 5;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 5: {
        int sel = FUN_802b0ea0((int)(DAT_80612a38 + 0x25c), (int)(DAT_80612a38 + 0x464), (int)(DAT_80612a38 + 0x466),
                               (int)(DAT_80612a38 + 0x73b), (int)(DAT_80612a38 + 0x73d), &FLOAT_80612a50, 0);
        DAT_80612a38[0x749] = sel;
        if (sel == -1) {
            if (*(int*)(rec + 0x20) != 10) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 10;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        } else if (sel == -2) {
            if (*(int*)(rec + 0x20) != 4) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 4;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        } else if (sel == DAT_80612a38[0x748]) {
            *(float*)&DAT_80612a38[0x74c] = FLOAT_8061967c;
            *(float*)&DAT_80612a38[0x74b] = FLOAT_80619604;
            anim_play_scaled((double)FLOAT_80619568, (double)*(float*)&DAT_80612a38[0x74b]);
            if (*(int*)(rec + 0x20) != 8) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 8;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        } else {
            *(float*)&DAT_80612a38[0x74d] = FLOAT_80619580;
            *(float*)&DAT_80612a38[0x74e] = FLOAT_80619568;
            if (*(int*)(rec + 0x20) != 6) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 6;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 6: {
        DAT_80612a38[0x74a] = DAT_80612a38[0x749] - DAT_80612a38[0x748];
        if (DAT_80612a38[0x74a] == 3) { DAT_80612a38[0x74a] = -1; }
        else if (DAT_80612a38[0x74a] == -3) { DAT_80612a38[0x74a] = 1; }
        DAT_80612a38[0x748] = DAT_80612a38[0x749];
        if (DAT_80612a38[0xb7e] == -1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) - FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) - FLOAT_806195b4;
            DAT_80612a38[0xb7e] = 0;
        } else if (DAT_80612a38[0xb7e] == 1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) + FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) + FLOAT_806195b4;
            DAT_80612a38[0xb7e] = 0;
        }
        if (DAT_80612a38[0x74a] < 1) {
            DAT_80612a38[0x74a] = DAT_80612a38[0x74a] + 1;
            anim_play_scaled((double)FLOAT_80619600, (double)*(float*)&DAT_80612a38[0x74d]);
        } else {
            DAT_80612a38[0x74a] = DAT_80612a38[0x74a] + -1;
            anim_play_scaled((double)FLOAT_80619600, (double)*(float*)&DAT_80612a38[0x74d]);
        }
        if (*(int*)(rec + 0x20) != 7) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = 7;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        break;
    }
    case 7: {
        if (DAT_80612a38[0xb7e] == -1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) - FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) - FLOAT_80619604;
            DAT_80612a38[0xb7e] = 0;
        } else if (DAT_80612a38[0xb7e] == 1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) + FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) + FLOAT_80619604;
            DAT_80612a38[0xb7e] = 0;
        }
        if (*(float*)&DAT_80612a38[0x74d] < FLOAT_806197e0) {
            *(float*)&DAT_80612a38[0x74d] = FLOAT_8061967c * *(float*)&DAT_80612a38[0x74e] * *(float*)&DAT_80612a38[0x74e] + *(float*)&DAT_80612a38[0x74d];
            if (*(float*)&DAT_80612a38[0x74e] < FLOAT_80619580) {
                *(float*)&DAT_80612a38[0x74e] = *(float*)&DAT_80612a38[0x74e] + FLOAT_80619560;
            }
            anim_set_blend_weight_alt((double)*(float*)&DAT_80612a38[0x74d], *(int*)(actorCtx + 4));
        }
        if (anim_active_slot_userflags(*(int*)(actorCtx + 4)) == 0) {
            DAT_80612a38[0xb7e] = 0;
        } else {
            if (anim_is_clip_active(actorCtx, 0x812) == 0) { DAT_80612a38[0xb7e] = 1; }
            else { DAT_80612a38[0xb7e] = -1; }
            if ((int)DAT_80612a38[0x74a] < 1) {
                if ((int)DAT_80612a38[0x74a] < 0) {
                    anim_play_scaled((double)FLOAT_80619568, (double)*(float*)&DAT_80612a38[0x74d]);
                    DAT_80612a38[0xb7e] = 1;
                    DAT_80612a38[0x74a] = DAT_80612a38[0x74a] + 1;
                } else {
                    int prev = DAT_80612a38[0xb7a];
                    if ((prev == 10) || (prev == 0xf) || (prev == 0xe)) {
                        *(float*)&DAT_80612a38[0x74c] = FLOAT_8061967c;
                        *(float*)&DAT_80612a38[0x74b] = FLOAT_80619604;
                        anim_play_scaled((double)FLOAT_80619568, (double)*(float*)&DAT_80612a38[0x74b]);
                        if (*(int*)(rec + 0x20) != 8) {
                            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                            *(int*)(rec + 0x20) = 8;
                            *(float*)(rec + 0x9d8) = FLOAT_80619568;
                            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                        }
                    } else {
                        anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                        if (DAT_80612a38[0xb7a] != *(int*)(rec + 0x20)) {
                            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                            *(int*)(rec + 0x20) = DAT_80612a38[0xb7a];
                            *(float*)(rec + 0x9d8) = FLOAT_80619568;
                            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                        }
                    }
                }
            } else {
                anim_play_scaled((double)FLOAT_80619568, (double)*(float*)&DAT_80612a38[0x74d]);
                DAT_80612a38[0xb7e] = -1;
                DAT_80612a38[0x74a] = DAT_80612a38[0x74a] + -1;
            }
        }
        break;
    }
    case 8: {
        if (DAT_80612a38[0xb7e] == -1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) - FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) - FLOAT_80619604;
            DAT_80612a38[0xb7e] = 0;
        } else if (DAT_80612a38[0xb7e] == 1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) + FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) + FLOAT_80619604;
            DAT_80612a38[0xb7e] = 0;
        }
        if (*(float*)&DAT_80612a38[0x74b] < FLOAT_8061961c) {
            *(float*)&DAT_80612a38[0x74b] = FLOAT_8061967c * *(float*)&DAT_80612a38[0x74c] * *(float*)&DAT_80612a38[0x74c] + *(float*)&DAT_80612a38[0x74b];
            if (*(float*)&DAT_80612a38[0x74c] < FLOAT_80619580) {
                *(float*)&DAT_80612a38[0x74c] = *(float*)&DAT_80612a38[0x74c] + FLOAT_80619560;
            }
            anim_set_blend_weight_alt((double)*(float*)&DAT_80612a38[0x74b], *(int*)(actorCtx + 4));
        }
        if (anim_active_slot_userflags(*(int*)(actorCtx + 4)) != 0) {
            float d;
            if ((DAT_80612a38[0x749] == 0) || (DAT_80612a38[0x749] == 2)) {
                d = *(float*)&DAT_80612a38[0x73c] - *(float*)&DAT_80612a38[0x744];
            } else {
                d = *(float*)&DAT_80612a38[0x73b] - *(float*)&DAT_80612a38[0x742];
            }
            FLOAT_80612a50 = ABS(d);
            anim_play_scaled((double)FLOAT_80619568, (double)*(float*)&DAT_80612a38[0x74b]);
            if (*(int*)(rec + 0x20) != 9) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 9;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
            anim_compute_root_velocity((double)FLOAT_80619568, (double)*(float*)(rec + 0xb08),
                                       (double)FLOAT_80612a50, (double)FLOAT_8061963c, actorCtx, 1);
        }
        break;
    }
    case 9: {
        if (*(float*)&DAT_80612a38[0x74b] < FLOAT_8061961c) {
            *(float*)&DAT_80612a38[0x74b] = FLOAT_8061967c * *(float*)&DAT_80612a38[0x74c] * *(float*)&DAT_80612a38[0x74c] + *(float*)&DAT_80612a38[0x74b];
            if (*(float*)&DAT_80612a38[0x74c] < FLOAT_80619580) {
                *(float*)&DAT_80612a38[0x74c] = *(float*)&DAT_80612a38[0x74c] + FLOAT_80619560;
            }
            anim_set_blend_weight_alt((double)*(float*)&DAT_80612a38[0x74b], *(int*)(actorCtx + 4));
        }
        if (anim_active_slot_userflags(*(int*)(actorCtx + 4)) != 0) {
            int lunge = DAT_80612a38[0x749];
            bool done = false;
            if (lunge == 0) {
                done = (*(float*)&DAT_80612a38[0x73c] - FLOAT_80619580 < *(float*)&DAT_80612a38[0x744]);
            } else if (lunge == 2) {
                done = (*(float*)&DAT_80612a38[0x744] < FLOAT_80619580 + *(float*)&DAT_80612a38[0x73c]);
            } else if (lunge == 3) {
                done = (*(float*)&DAT_80612a38[0x73b] - FLOAT_80619580 < *(float*)&DAT_80612a38[0x742]);
            } else if (lunge == 1) {
                done = (*(float*)&DAT_80612a38[0x742] < FLOAT_80619580 + *(float*)&DAT_80612a38[0x73b]);
            }
            if (done) {
                anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619604);
                if (DAT_80612a38[0xb7a] != *(int*)(rec + 0x20)) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = DAT_80612a38[0xb7a];
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
                *(float*)&DAT_80612a38[0xb7c] = FLOAT_80619568;
            }
        }
        break;
    }
    case 10:
        if (((1 < (int)(DAT_80612a38[0x466] - DAT_80612a38[0x73d])) ||
             ((int)(DAT_80612a38[0x466] - DAT_80612a38[0x73d]) < -1)) ||
            ((1 < (int)(DAT_80612a38[0x467] - DAT_80612a38[0x73e])) ||
             ((int)(DAT_80612a38[0x467] - DAT_80612a38[0x73e]) < -1))) {
            float d138[3];   // local_138/134/130
            float d12c[3];   // local_12c/128/124
            d12c[0] = *(float*)&DAT_80612a38[0x742] - *(float*)(actorCtx + 100);
            d12c[1] = FLOAT_80619568;
            d138[0] = *(float*)&DAT_80612a38[0x742] + d12c[0];
            d12c[2] = *(float*)&DAT_80612a38[0x744] - *(float*)(actorCtx + 0x6c);
            d138[2] = *(float*)&DAT_80612a38[0x744] + d12c[2];
            d138[1] = *(float*)&DAT_80612a38[0x743] - FLOAT_80619580;
            FUN_802b22d8((int)(DAT_80612a38 + DAT_80612a38[0x738] * 0x5a + 0x468),
                         (int)(DAT_80612a38 + DAT_80612a38[0x73e] * 10 + DAT_80612a38[0x73d] * 0x46 + 0x25c),
                         d138, d12c, DAT_80612a38[0x73d], DAT_80612a38[0x73e]);
            DAT_80612a38[0x738] = DAT_80612a38[0x738] + 1;
            if (DAT_80612a38[0x738] == 8) DAT_80612a38[0x738] = 0;
        }
        if (*(int*)(rec + 0x20) != 4) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = 4;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        break;
    case 0xb:
        snake3_anim_spit_spawn(actorCtx);
        if ((anim_active_slot_userflags(*(int*)(actorCtx + 4)) != 0) && (*(int*)(rec + 0x20) != 4)) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = 4;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        if (DAT_80612a38[0xb7e] == -1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) - FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) - FLOAT_806195b4;
            DAT_80612a38[0xb7e] = 0;
        } else if (DAT_80612a38[0xb7e] == 1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) + FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) + FLOAT_806195b4;
            DAT_80612a38[0xb7e] = 0;
        }
        break;
    case 0xe: {
        int sel = FUN_802b0bfc((int)(DAT_80612a38 + 0x25c), (int)(DAT_80612a38 + 0x466), (int)(DAT_80612a38 + 0x73b), (int)(DAT_80612a38 + 0x73d));
        DAT_80612a38[0x749] = sel;
        if (sel == -1) {
            sel = FUN_802b0c58((int)(DAT_80612a38 + 0x25c), (int)(DAT_80612a38 + 0x466), (int)(DAT_80612a38 + 0x73b), (int)(DAT_80612a38 + 0x73d));
            DAT_80612a38[0x749] = sel;
            if (sel == -1) {
                player_ai_get_set_raycast_mode(1);
                DAT_80612a38[0xb7a] = 0x10;
                if (*(int*)(rec + 0x20) != 0x10) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 0x10;
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
            } else {
                *(float*)&DAT_80612a38[0x74d] = FLOAT_80619580;
                *(float*)&DAT_80612a38[0x74e] = FLOAT_80619568;
                if (*(int*)(rec + 0x20) != 6) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 6;
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
            }
        } else {
            *(float*)&DAT_80612a38[0x74d] = FLOAT_80619580;
            *(float*)&DAT_80612a38[0x74e] = FLOAT_80619568;
            if (*(int*)(rec + 0x20) != 6) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 6;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 0xf: {
        int sel = FUN_802b0b2c((int)(DAT_80612a38 + 0x25c), (int)(DAT_80612a38 + 0x466), (int)(DAT_80612a38 + 0x73b), (int)(DAT_80612a38 + 0x73d));
        DAT_80612a38[0x749] = sel;
        if (sel == -1) {
            sel = FUN_802b0ba0((int)(DAT_80612a38 + 0x25c), (int)(DAT_80612a38 + 0x466), (int)(DAT_80612a38 + 0x73b), (int)(DAT_80612a38 + 0x73d));
            DAT_80612a38[0x749] = sel;
            if (sel == -1) {
                DAT_80612a38[0xb7a] = 0xe;
                if (*(int*)(rec + 0x20) != 0xe) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 0xe;
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
            } else if (sel == DAT_80612a38[0x748]) {
                *(float*)&DAT_80612a38[0x74c] = FLOAT_8061967c;
                *(float*)&DAT_80612a38[0x74b] = FLOAT_80619604;
                anim_play_scaled((double)FLOAT_80619568, (double)*(float*)&DAT_80612a38[0x74b]);
                if (*(int*)(rec + 0x20) != 8) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 8;
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
            } else {
                *(float*)&DAT_80612a38[0x74d] = FLOAT_80619580;
                *(float*)&DAT_80612a38[0x74e] = FLOAT_80619568;
                if (*(int*)(rec + 0x20) != 6) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 6;
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
            }
        } else if (sel == DAT_80612a38[0x748]) {
            *(float*)&DAT_80612a38[0x74c] = FLOAT_8061967c;
            *(float*)&DAT_80612a38[0x74b] = FLOAT_80619604;
            anim_play_scaled((double)FLOAT_80619568, (double)*(float*)&DAT_80612a38[0x74b]);
            if (*(int*)(rec + 0x20) != 8) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 8;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        } else {
            *(float*)&DAT_80612a38[0x74d] = FLOAT_80619580;
            *(float*)&DAT_80612a38[0x74e] = FLOAT_80619568;
            if (*(int*)(rec + 0x20) != 6) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 6;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 0x11:
        if (DAT_80612a38[0xb7e] == -1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) - FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) - FLOAT_80619604;
            DAT_80612a38[0xb7e] = 0;
        } else if (DAT_80612a38[0xb7e] == 1) {
            *(float*)(actorCtx + 0x70) = *(float*)(actorCtx + 0x70) + FLOAT_806195b4;
            *(float*)(actorCtx + 0xc4) = *(float*)(actorCtx + 0xc4) + FLOAT_80619604;
            DAT_80612a38[0xb7e] = 0;
        }
        if (anim_active_slot_userflags(*(int*)(actorCtx + 4)) != 0) {
            *(float*)&DAT_80612a38[0xb7f] = FLOAT_806108e8 * (FLOAT_80619580 / u32_to_float_biased((unsigned)DAT_806109c0)) + *(float*)&DAT_80612a38[0xb7f];
            if (FLOAT_806195f8 < *(float*)&DAT_80612a38[0xb7f]) {
                *(float*)(actorCtx + 100)  = FLOAT_806197e4;
                *(float*)(actorCtx + 0x68) = FLOAT_806197a0;
                *(float*)(actorCtx + 0x6c) = FLOAT_806197e8;
                DAT_80612a38[0x73f] = *(int*)(actorCtx + 100);
                DAT_80612a38[0x740] = *(int*)(actorCtx + 0x68);
                DAT_80612a38[0x741] = *(int*)(actorCtx + 0x6c);
                *(float*)(actorCtx + 0x70) = FLOAT_806197ec;
                *(float*)(actorCtx + 0xc4) = FLOAT_806197ec;
                player_ai_get_set_raycast_mode(0);
                *(int*)(DAT_80611d74 + 0xe8c) = 0;
                if (*(int*)(rec + 0x20) != 0x12) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 0x12;
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
                statlvl_class_force_complete(0);
                FUN_802b13bc();
                *(float*)&DAT_80612a38[0xb7f] = FLOAT_80619568;
            }
        }
        break;
    }
    FUN_8038270c();
}
