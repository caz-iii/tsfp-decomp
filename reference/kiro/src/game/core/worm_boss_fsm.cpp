// worm_boss_fsm.cpp - "Worm" / tentacle boss AI actor: main FSM + model-pose,
// projectile, particle, damage and event helpers.
//
// This module implements the large burrowing "worm" boss (used on the arcade
// map-100 hook) and its supporting routines. It shares the aiactor actor-record
// layout (state word +0x20 / int-index [8], pending-state +0x9d0 / [0x274],
// prev-state [0x275], enter-time [0x276], per-state timer [0x278], turn-rate
// accumulator +0xa90) and the 0x806194xx rodata constant block with the
// creature FSMs in aiactor_creature_fsm.cpp.
//
//  worm_pose_yaw_from_angle    (0x8028a574): build a Y-rotation matrix from the
//    boss's heading error (charge angle - facing).
//  worm_pose_pitch_segment     (0x8028a5b4): apply an X-rotation (from the morph
//    charge timer) plus two translations to a body-segment bone matrix.
//  worm_compute_lunge_vector   (0x8028a64c): compute the normalized lunge
//    direction from the boss head toward the target, scaled by distance.
//  worm_pose_deform_body       (0x8028a7d4): per-state body-segment bone-matrix
//    deform (rear-up state 4, coil states <9, thrash state 9, plus a base roll).
//  worm_spawn_spit_projectile  (0x8028aa04): fire a "spit" projectile from a mouth
//    bone with a homing direction toward the target, plus trail + sound.
//  worm_setup_dust_emitters    (0x8028ace4): configure the 4 cached dust/debris
//    particle emitters for a burrow/surface event.
//  worm_event_react            (0x8028ae00): external event handler (event 2 =
//    kill/finisher -> stat + state 9; event 7 = re-enter surface state 4).
//  worm_damage_scale_apply     (0x8028aff4): scale incoming damage by char-def
//    flags / weak-point state, then apply it.
//  worm_boss_behavior_tick     (0x8028b08c): the main worm-boss per-frame FSM
//    (11 states: spawn/idle, approach, burrow-in, chase, surface, spit, reposition,
//    dive, thrash, projectile volley, destroy).
//  worm_burrower_tick          (0x8028cc34): smaller burrower-creature FSM
//    (buried/emerge/attack) that shares the worm constant block.
//
// Address range: 0x8028a574 - 0x8028cd6b
// Subsystem: game (AI actors)

#include "common.h"

extern "C" {

// --- raw actor-context accessors (still FUN_ in the IDB) -------------------
extern int    FUN_803826e8(void);   // current actor entity record
extern int    FUN_803826f0(void);   // current actor entity record (alt entry)
extern void   FUN_80382734(void);   // tick epilogue A
extern void   FUN_8038273c(void);   // tick epilogue B
extern double FUN_80390324(double x);// fmod-style range reducer (angle wrap)
extern int    __save_gpr_28(void);   // compiler save-regs stub (returns r-frame)
extern void   __restore_gpr_28(void);// compiler restore-regs stub

// --- scalar / data globals -------------------------------------------------
extern unsigned char DAT_805015bd;   // co-op sub-mode selector ('\n' == co-op)
extern int   DAT_806109c0;           // frame delta ticks (signed, xor bias)
extern int   DAT_80611824;           // clip-class -> table base
extern int   DAT_804d1e96;           // burrower clip-class row index (byte)
extern int   DAT_80611d78;           // player record table base
extern int   DAT_806120f8;           // "player is active target" sentinel
extern int   DAT_80611e2c;           // fallback particle-emitter record
extern int   DAT_8048ca9c;           // per-class scratch float table (stride 0x30)
extern int   DAT_804731b4;           // worm mouth-bone {hi:index, lo:vtx} table
extern int   DAT_804731c4;           // worm dust-emitter slot-id table (4 ints)

// --- float / double constants (rodata, worm block) -------------------------
extern const float  FLOAT_806108ec;  // per-frame time-scale numerator
extern const float  FLOAT_80619478;  // 0.0
extern const float  FLOAT_8061947c;  // 1.0
extern const float  FLOAT_80619488;  // anim rate (idle) / distance threshold
extern const float  FLOAT_80619490;  // lerp/step factor
extern const float  FLOAT_8061948c;  // aggression threshold
extern const float  FLOAT_8061949c;  // inv-sqrt Newton coeff
extern const float  FLOAT_806194bc;  // deg->rad-ish rotation scale numerator
extern const float  FLOAT_806194c0;  // rotation scale denominator
extern const float  FLOAT_806194c4;  // pitch translate Y a
extern const float  FLOAT_806194c8;  // pitch translate Z a
extern const float  FLOAT_806194cc;  // pitch translate Y b
extern const float  FLOAT_806194d0;  // pitch translate Z b
extern const float  FLOAT_806194d4;  // min length-sq for normalize
extern const float  FLOAT_806194d8;  // inv-sqrt Newton const (1.5)
extern const float  FLOAT_806194dc;  // distance->scale bias
extern const float  FLOAT_806194e0;  // turn slew factor
extern const float  FLOAT_806194e4;  // angle wrap period (2pi-ish)
extern const float  FLOAT_806194e8;  // small epsilon / max clamp
extern const float  FLOAT_806194ec;  // 3d sound range
extern const float  FLOAT_806194f0;  // engage distance-sq
extern const float  FLOAT_806194f4;  // pitch preset
extern const float  FLOAT_806194f8;  // pitch clamp lo
extern const float  FLOAT_806194fc;  // pitch clamp hi
extern const float  FLOAT_80619500;  // pitch slew factor
extern const float  FLOAT_80619504;  // speed clamp lo
extern const float  FLOAT_80619508;  // yaw-rate clamp lo
extern const float  FLOAT_8061950c;  // yaw-rate clamp hi
extern const float  FLOAT_80619510;  // yaw-rate scale
extern const float  FLOAT_80619514;  // yaw slew factor
extern const float  FLOAT_80619518;  // yaw clamp lo
extern const float  FLOAT_8061951c;  // banking scale
extern const float  FLOAT_80619520;  // decel factor (state 4)
extern const float  FLOAT_80619524;  // turn sign step
extern const float  FLOAT_80619528;  // dir alignment threshold A
extern const float  FLOAT_8061952c;  // timer preset (state 6 entry)
extern const float  FLOAT_80619530;  // break-off distance-sq
extern const float  FLOAT_80619534;  // spit windup divisor
extern const float  FLOAT_80619538;  // spit spread scale
extern const float  FLOAT_8061953c;  // yaw clamp lo (state 5)
extern const float  FLOAT_80619540;  // dir alignment threshold B
extern const float  FLOAT_80619544;  // turn boost hi
extern const float  FLOAT_80619548;  // turn boost gate
extern const float  FLOAT_8061954c;  // yaw damping (state 6)
extern const float  FLOAT_80619550;  // dir alignment threshold C
extern const double DOUBLE_806194a8; // frame-delta bias
extern const float  FLOAT_80619554;  // min yaw error to slew (state 9)
extern const float  FLOAT_80619558;  // 3d sound pitch

// --- named cross-module helpers (call-site typed; still FUN_ in IDB) --------
extern int  aiactor_retarget_visible_teammate(void);
extern void aiactor_facing_update(void);
extern void aiactor_leader_zone_update(int actor);
extern void aiactor_follow_zone_path(void);
extern int  aiactor_check_zone_target_players(int actor);
extern void aiactor_set_stance_flags(int actor, unsigned int flag);
extern int  aiactor_acquire_nearest_target(int actorIn, int preferred, int mode);
extern int  aiactor_nav_advance_path(int actor, int* nav);
extern int  aiactor_dir_alignment_test(double threshold, float* p2, float* p3, float* p4);
extern void aiactor_destroy_instance(unsigned long long a, double b, double c, double d,
                                     unsigned long long e, unsigned long long f,
                                     unsigned long long g, unsigned long long h, int actor);

extern unsigned long long anim_play_scaled(unsigned long long ctx, double speed);
extern int  anim_is_clip_active(int actor, int clipId);
extern int  anim_primary_clip_userflags(int actor, int clipId);
extern int  anim_primary_clip_time_left(double rate, int animCtx);
extern int  anim_active_slot_userflags(int animCtx);
extern int  anim_sample_keyframe(int clip, int a, int b, float* out);
extern void anim_set_clip_speed_fields(double rate, int* actorCtx);
extern int  anim_event_scan_active(void);
extern int  anim_event_has_passed(int entity, int mask, int nth);

extern void mtx4_set_translation(float* mtx, double x, double y, double z);
extern void mtx4_set_rotation_x(double angle, int mtx);
extern void mtx4_set_rotation_y(double angle, float* mtx);
extern void mtx4_set_rotation_z(double angle, float* mtx);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void mtx4_transform_point(float* mtx, float* pt);
extern void mtx4_copy(float* src, float* dst);
extern double mtx4_invert(float* out, float* src);
extern double math_sqrt(double x);
extern void angles_to_dir(double yaw, double pitch, float* dirOut);
extern void dir_to_angles(float* dir, float* pitchOut, float* yawOut);

extern void model_pose_eval(void);

extern void particle_cached_set_drag(double drag, int* pCachedRef);
extern void particle_cached_set_lifetime(double lifetime, int* pCachedRef);
extern void particle_cached_set_delay(double delay, int* pCachedRef);

extern int  scene_ray_portal_intersect(double a, double b, int posPtr, float* dir, int room,
                                       int p6, int p7, float* p8, float* p9);
extern void player_ai_spawn_trail_particle(int a, int b, int c, float* position,
                                           float* direction, int facing);
extern void player_ai_projectile_spawn_simple(double zero, double accuracy, int weaponId,
                                              float* posPtr, int* direction, int owner,
                                              int* actorRec, int weaponMode, int a9,
                                              int room, int flags, int ownerId);
extern void projectile_spawn(double a, double b, double c, double d, double e, double f,
                             unsigned long long g, unsigned long long h);

extern void sound_init_play_params(int paramsPtr);
extern unsigned int sound_play_request(int* params);
extern void sound_update_3d_source(double range, double pitch, double volume, int sampleId,
                                   int* handlePtr, int posPtr, int flags);

extern int  worm_stat_on_kill(int* killerRec, int* victimRec);
extern int  worm_map100_arcade_hook(int actor);

extern unsigned int player_ai_char_def_get_flags(int charIndex);
extern void damage_apply_simple(int* eventData);

extern void entity_register_for_update(int entity, int listType, int flag);
extern void entity_set_render_type(int entity, void* renderFilter);

// damage-filter callbacks passed by pointer to entity_set_render_type
extern void damage_filter_invincible(int* damageCtx);

} // extern "C"

// Reconstruct the "(double)CONCAT44(0x43300000, bits) - bias" rng/float idiom.
static inline float wb_unit(unsigned int bits, double bias) {
    return (float)((double)CONCAT44(0x43300000, bits) - bias);
}

// Set an int-indexed pending state transition (records prev + enter time).
static void wb_set_pending_i(int* rec, int newState) {
    if (rec[8] != newState) {
        rec[0x275] = rec[8];
        rec[8] = newState;
        rec[0x276] = (int)FLOAT_80619478;
        rec[0x274] = rec[8];
    }
}

// Normalize (x,y,z) in place using the game's two-step inverse-sqrt, scaling by
// `outScale`. Returns nothing; leaves the vector untouched if below the min-sq.
static void wb_normalize_scaled(float* x, float* y, float* z, float outScale) {
    float lenSq = *z * *z + *x * *x + *y * *y;
    if (FLOAT_806194d4 < lenSq) {
        float inv = 1.0f / (float)math_sqrt((double)lenSq);
        inv = inv * -(FLOAT_8061949c * lenSq * inv * inv - FLOAT_806194d8);
        float s = outScale * inv * -(FLOAT_8061949c * lenSq * inv * inv - FLOAT_806194d8);
        *x = *x * s;
        *y = *y * s;
        *z = *z * s;
    }
}

// ===========================================================================
// 0x8028a574  worm_pose_yaw_from_angle
// ===========================================================================
void worm_pose_yaw_from_angle(float* mtx, int entity) {
    int rec = *(int*)(entity + 0x7c);
    double angle = (double)((FLOAT_806194bc *
                             (*(float*)(rec + 0x9e0) - *(float*)(entity + 0x70))) /
                            FLOAT_806194c0);
    mtx4_set_rotation_y(angle, mtx);
}

// ===========================================================================
// 0x8028a5b4  worm_pose_pitch_segment
// ===========================================================================
void worm_pose_pitch_segment(float* mtx, int entity) {
    int rec = *(int*)(entity + 0x7c);
    float tmp[16];
    mtx4_set_translation(tmp, (double)FLOAT_80619478, (double)FLOAT_806194c4, (double)FLOAT_806194c8);
    mtx4_set_rotation_x((double)((FLOAT_806194bc * -*(float*)(rec + 0x9dc)) / FLOAT_806194c0), (int)(size_t)tmp);
    mtx4_multiply(tmp, mtx, mtx);
    mtx4_set_translation(tmp, (double)FLOAT_80619478, (double)FLOAT_806194cc, (double)FLOAT_806194d0);
    mtx4_multiply(tmp, mtx, mtx);
}

// ===========================================================================
// 0x8028a64c  worm_compute_lunge_vector
// Compute the boss's normalized lunge direction (into the record-supplied out
// vector) and scale it by the distance to the target head.
// ===========================================================================
void worm_compute_lunge_vector(void) {
    int entity = FUN_803826f0();
    int rec = *(int*)(entity + 0x7c);
    // The IDB packs {out-vector ptr : hi, entity : lo}; model as an explicit
    // scratch vector derived from the record to keep this compile-clean.
    float out[3];
    float body[16];
    mtx4_set_rotation_y((double)((FLOAT_806194bc * *(float*)(rec + 0x9e0)) / FLOAT_806194c0),
                        (float*)&body);
    worm_pose_pitch_segment(body, entity);
    out[0] = FLOAT_80619478;
    out[1] = FLOAT_80619478;
    out[2] = FLOAT_8061947c;
    mtx4_transform_point(body, out);
    // (Ghidra shows a second transform through the pitch matrix; folded above.)
    wb_normalize_scaled(&out[0], &out[1], &out[2], FLOAT_8061947c);

    int leaderRec = *(int*)(*(int*)(rec + 4) + 0x7c);
    float dx = *(float*)(entity + 0x64) - *(float*)(leaderRec + 0xbb4);
    float dy = *(float*)(entity + 0x68) - *(float*)(leaderRec + 0xbb8);
    float dz = *(float*)(entity + 0x6c) - *(float*)(leaderRec + 0xbbc);
    double dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
    float scale = (float)((double)FLOAT_806194dc + dist);
    out[0] = out[0] * scale;
    out[1] = out[1] * scale;
    out[2] = out[2] * scale;
    FUN_8038273c();
}

// ===========================================================================
// 0x8028a7d4  worm_pose_deform_body
// ===========================================================================
void worm_pose_deform_body(void) {
    int entity = __save_gpr_28();
    int rec = *(int*)(entity + 0x7c);
    float base[16], seg[16], inv[16], keep[16];
    (void)inv; (void)keep;

    model_pose_eval();
    if (((*(unsigned int*)(entity + 0x10) | *(unsigned int*)(entity + 0xc)) & 0xffff) == 0) {
        __restore_gpr_28();
        return;
    }
    int state = *(int*)(rec + 0x20);
    if (state == 4) {
        mtx4_copy(*(float**)(*(int*)(entity + 4) + 0x10), keep);
        mtx4_invert(inv, keep);
        mtx4_set_rotation_x((double)((FLOAT_806194bc *
                                     (*(float*)(rec + 0x9e0) - *(float*)(entity + 0x70))) /
                                    FLOAT_806194c0), (int)(size_t)seg);
        int off = 0x40;
        for (int i = 1; i < 5; i++) {
            float* m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + off);
            mtx4_multiply(m, inv, m);
            mtx4_multiply(m, seg, m);
            mtx4_multiply(m, keep, m);
            off += 0x40;
        }
    } else if (state >= 4) {
        if (state == 9) {
            worm_pose_yaw_from_angle(seg, entity);
            int off = 0x40;
            for (int i = 1; i < 5; i++) {
                float* m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + off);
                mtx4_multiply(m, seg, m);
                off += 0x40;
            }
        } else if (state < 9) {
            float pitch[16];
            worm_pose_yaw_from_angle(seg, entity);
            worm_pose_pitch_segment(pitch, entity);
            float* m0 = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x40);
            mtx4_multiply(m0, seg, m0);
            int off = 0x80;
            for (int i = 2; i < 5; i++) {
                float* m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + off);
                mtx4_multiply(m, pitch, m);
                mtx4_multiply(m, seg, m);
                off += 0x40;
            }
        }
        __restore_gpr_28();
        return;
    } else if (state < 3) {
        __restore_gpr_28();
        return;
    }
    // shared base roll (states 3, 4)
    mtx4_set_rotation_z(-(double)((FLOAT_806194bc * *(float*)(rec + 0x9dc)) / FLOAT_806194c0), base);
    int off = 0;
    int boneCount = *(int*)(*(int*)(*(int*)(entity + 4) + 4) + 4);
    for (int i = 0; i < boneCount; i++) {
        float* m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + off);
        mtx4_multiply(m, base, m);
        off += 0x40;
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x8028aa04  worm_spawn_spit_projectile
// ===========================================================================
void worm_spawn_spit_projectile(int variant, int slot, float* dirMtx, float* targetPos) {
    (void)variant;
    int entity = FUN_803826f0();
    int* skel = *(int**)(*(int*)(entity + 4) + 4);
    int hiIdx = (int)*(unsigned int*)(&DAT_804731b4 + (unsigned int)slot * 4) >> 0x10;
    int vtxOff = (*(unsigned int*)(&DAT_804731b4 + (unsigned int)slot * 4) & 0xffff) * 0x18;
    float src[3];
    src[0] = *(float*)(skel[*skel * -0x30 + hiIdx * 0x30 + 0x1b] + vtxOff);
    src[1] = *(float*)(skel[*skel * -0x30 + hiIdx * 0x30 + 0x1b] + vtxOff + 4);
    src[2] = *(float*)(skel[*skel * -0x30 + hiIdx * 0x30 + 0x1b] + vtxOff + 8);

    if (((*(unsigned int*)(entity + 0x10) | *(unsigned int*)(entity + 0xc)) & 0xffff) != 0) {
        unsigned int u = (unsigned int)slot ^ 2;
        int boneSel = (3 - ((int)(((int)u >> 1) - (u & 2)) >> 0x1f)) * 0x40;
        mtx4_transform_point((float*)(*(int*)(*(int*)(entity + 4) + 0x10) + boneSel), src);
    }
    mtx4_transform_point(dirMtx, src);

    float mouth[3];
    mouth[0] = src[0] + *(float*)(entity + 0x64);
    mouth[1] = src[1] + *(float*)(entity + 0x68);
    mouth[2] = src[2] + *(float*)(entity + 0x6c);
    float dir[3];
    dir[0] = (targetPos[0] + *(float*)(entity + 0x64)) - mouth[0];
    dir[1] = (targetPos[1] + *(float*)(entity + 0x68)) - mouth[1];
    dir[2] = (targetPos[2] + *(float*)(entity + 0x6c)) - mouth[2];
    wb_normalize_scaled(&dir[0], &dir[1], &dir[2], FLOAT_8061947c);

    mouth[0] = FLOAT_80619490 * dir[0] + mouth[0];
    mouth[1] = FLOAT_80619490 * dir[1] + mouth[1];
    mouth[2] = FLOAT_80619490 * dir[2] + mouth[2];

    float rayDir[3];
    rayDir[0] = mouth[0] - *(float*)(entity + 0x64);
    rayDir[1] = (float)((double)mouth[1] - (double)*(float*)(entity + 0x68));
    rayDir[2] = (float)((double)mouth[2] - (double)*(float*)(entity + 0x6c));
    int room = scene_ray_portal_intersect((double)mouth[2], (double)mouth[1], entity + 0x64,
                                          rayDir, *(int*)(entity + 0x34), 0, 0, (float*)0, (float*)0);
    player_ai_spawn_trail_particle(0, 2, -1, mouth, dir, -1);
    player_ai_projectile_spawn_simple((double)FLOAT_8061947c, (double)FLOAT_806194e0, 0x1e,
                                      mouth, (int*)dir, entity, *(int**)(entity + 0x7c),
                                      2, 0, room, 0x184, 1);
    int sp[7];
    sound_init_play_params((int)sp);
    *(unsigned char*)((int)sp + 0x24) = 1;
    sp[0] = 0x4c4;
    *(float*)((int)sp + 0x1c) = mouth[0];
    *(float*)((int)sp + 0x20) = mouth[1];
    *(float*)((int)sp + 0x24) = mouth[2];  // overwrites flag; matches decomp layout
    sound_play_request(sp);
    FUN_8038273c();
}

// ===========================================================================
// 0x8028ace4  worm_setup_dust_emitters
// ===========================================================================
void worm_setup_dust_emitters(void) {
    int entity = FUN_803826e8();
    // low word of the packed return selects an on/off preset; model as flag.
    int flag = 0;
    float drag = (flag != 0) ? FLOAT_8061947c : FLOAT_80619478;
    float life = (flag != 0) ? FLOAT_8061947c : FLOAT_80619478;
    int off = 0;
    for (int i = 0; i < 4; i++) {
        int* slotPtr;
        if (*(int*)((int)&DAT_804731c4 + off) < *(int*)(entity + 0x1e8)) {
            slotPtr = (int*)(*(int*)(entity + 0x1fc) + *(int*)((int)&DAT_804731c4 + off) * 4);
        } else {
            slotPtr = (int*)&DAT_80611e2c;
        }
        int em = *slotPtr;
        *(unsigned int*)(em + 4) &= 0xffdfffff;
        int e0 = em; particle_cached_set_drag((double)drag, &e0);
        int e1 = em; particle_cached_set_lifetime((double)life, &e1);
        int e2 = em; particle_cached_set_delay((double)FLOAT_80619478, &e2);
        *(char*)(em + 0x10) = (char)flag;
        *(unsigned char*)(em + 0x11) = 0;
        off += 4;
    }
    FUN_80382734();
}

// ===========================================================================
// 0x8028ae00  worm_event_react
// ===========================================================================
int worm_event_react(int entity, int event, int otherHit) {
    int ret = 0;
    if (event == 2) {
        int* rec = *(int**)(entity + 0x7c);
        int* killer = 0;
        int oh;
        if (otherHit != 0 && (oh = *(int*)(otherHit + 4)) != 0 && *(int*)(oh + 0x20) == 8) {
            killer = *(int**)(oh + 0x7c);
        }
        worm_stat_on_kill(killer, rec);
        *(unsigned int*)(entity + 0x28) &= 0xffffffbf;
        aiactor_set_stance_flags(entity, 8);
        anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
        double target = (double)(FLOAT_806194c0 + (float)rec[0x278]);
        double w = FUN_80390324((double)((float)(target - (double)FLOAT_80619478) / FLOAT_806194e4));
        rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * (double)(float)w - target);
        if (rec[8] != 9) {
            rec[0x274] = 9;
        }
        int head = *rec;
        int sp[7];
        sound_init_play_params((int)sp);
        *(unsigned char*)((int)sp + 0x24) = 1;
        sp[0] = 0x4d1;
        *(int*)((int)sp + 0x1c) = *(int*)(head + 0x64);
        *(int*)((int)sp + 0x20) = *(int*)(head + 0x68);
        *(int*)((int)sp + 0x24) = *(int*)(head + 0x6c);
        sound_play_request(sp);
        ret = -1;
    } else if (event < 2) {
        if (0 < event) {
            ret = -1;
        }
    } else if (event == 7) {
        int* rec = *(int**)(entity + 0x7c);
        anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
        if (rec[8] != 4) {
            rec[0x274] = 4;
        }
        double target = (double)(FLOAT_806194c0 + *(float*)(entity + 0x70));
        double w = FUN_80390324((double)((float)(target - (double)FLOAT_80619478) / FLOAT_806194e4));
        rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * (double)(float)w - target);
        int head = *rec;
        int sp[7];
        sound_init_play_params((int)sp);
        *(unsigned char*)((int)sp + 0x24) = 1;
        sp[0] = 0x4d2;
        *(int*)((int)sp + 0x1c) = *(int*)(head + 0x64);
        *(int*)((int)sp + 0x20) = *(int*)(head + 0x68);
        *(int*)((int)sp + 0x24) = *(int*)(head + 0x6c);
        sound_play_request(sp);
        ret = -1;
    }
    return ret;
}

// ===========================================================================
// 0x8028aff4  worm_damage_scale_apply
// ===========================================================================
void worm_damage_scale_apply(int* eventData) {
    int rec = *(int*)(*eventData + 0x7c);
    unsigned int flags = player_ai_char_def_get_flags(eventData[5]);
    if ((flags & 2) == 0) {
        if ((*(unsigned int*)(*(int*)(rec + 0xbf0) + 0x58) & 0x2000000) == 0) {
            eventData[8] = (int)((float)eventData[8] / FLOAT_806194d8);
        } else {
            eventData[8] = (int)((float)eventData[8] * FLOAT_806194d8);
        }
    } else {
        eventData[8] = (int)((float)eventData[8] * FLOAT_806194e8);
    }
    damage_apply_simple(eventData);
}

// ===========================================================================
// 0x8028b08c  worm_boss_behavior_tick
// Main worm-boss per-frame FSM (states 0..10). Acquires a target, drives the
// spawn/approach/burrow/chase/surface/spit/reposition/dive/thrash/volley/destroy
// lifecycle, and updates the 3D sound source at the end of each frame.
// ===========================================================================
void worm_boss_behavior_tick(void) {
    float fVar1, fVar2, fVar3, fVar4, fVar5, fVar6, fVar7, fVar8;
    unsigned int uVar12;
    int iVar13;
    int iVar14;
    int iVar15;
    int* rec;
    double dVar17, dVar19;
    float local_190, local_18c, local_188;
    float local_184[3];
    float local_19c, local_198, local_194;
    float local_1c0, local_1bc, local_1b8;
    float local_1fc, local_200, local_204, local_208, local_20c, local_210;
    float local_1f0, local_1ec, local_1e8, local_1f4, local_1f8;
    float local_1e4, local_1e0, local_1dc;
    float dirScratch[3];
    float bodyMtx[16], bodyMtx2[16];
    float spitDir1[3], spitDir2[3];
    int sp0[7], sp1[7], sp2[7], sp3[7];
    double param_2 = 0, param_3 = 0, param_4 = 0, param_5 = 0;
    unsigned long long param_7 = 0, param_8 = 0;

    iVar13 = FUN_803826f0();
    rec = *(int**)(iVar13 + 0x7c);
    iVar14 = aiactor_retarget_visible_teammate();

    if (-1 < rec[0x274]) {
        if (rec[0x274] != rec[8]) {
            rec[0x276] = (int)FLOAT_80619478;
            rec[0x275] = rec[8];
            rec[8] = rec[0x274];
        }
        rec[0x274] = -1;
    }
    if ((rec[0xb] & 0x80) == 0) {
        rec[0x2a4] = (int)FLOAT_80619478;
    } else {
        unsigned int biasBits = (unsigned int)DAT_806109c0 ^ 0x80000000u;
        rec[0x2a4] = (int)(float)(FLOAT_806108ec *
                                  (double)(float)(FLOAT_8061947c / wb_unit(biasBits, DOUBLE_806194a8)) +
                                  (double)(float)rec[0x2a4]);
    }

    double dVar24 = (double)FLOAT_806194ec;
    if (rec[1] == 0 || DAT_806120f8 == rec[2]) {
        rec[1] = aiactor_acquire_nearest_target(iVar13, *(int*)(DAT_80611d78 + 0x14), 0);
    }
    iVar15 = rec[1];
    if (iVar15 == 0) { FUN_8038273c(); return; }

    switch (rec[8]) {
    case 0:
        worm_setup_dust_emitters();
        entity_register_for_update(iVar13, 2, -0x7fd7582c);
        rec[10] |= 0x4000000;
        *(float*)(&DAT_8048ca9c + *(short*)(rec + 0x12) * 0x30) = FLOAT_8061947c;
        if (*(short*)(rec[0x2fc] + 0x48) == -1) {
            uVar12 = *(unsigned int*)(rec[0x2fc] + 0x58);
            if ((uVar12 & 0x100000) == 0) {
                if ((uVar12 & 0x200000) == 0) {
                    anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                    wb_set_pending_i(rec, 1);
                    *(unsigned int*)(iVar13 + 0x9c) |= 0x4000000;
                    entity_set_render_type(iVar13, (void*)&damage_filter_invincible);
                } else {
                    anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                    wb_set_pending_i(rec, 7);
                    entity_set_render_type(iVar13, (void*)&worm_damage_scale_apply);
                    rec[0x278] = *(int*)(iVar13 + 0x70);
                    rec[0x277] = (int)FLOAT_80619478;
                    rec[0x2cc] = (int)FLOAT_80619478;
                }
            } else {
                rec[0x277] = (int)FLOAT_80619478;
                rec[0x2a8] = (int)FLOAT_8061947c;
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)(float)rec[0x2a8]);
                wb_set_pending_i(rec, 3);
                *(unsigned int*)(iVar13 + 0x9c) |= 0x4000000;
                entity_set_render_type(iVar13, (void*)&damage_filter_invincible);
                iVar14 = rec[0x26e];
                if (iVar14 != 0) {
                    rec[0x1a1] = (iVar14 < 0)
                        ? *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678)
                        : iVar14;
                    rec[0x26b] = 0; rec[0x26a] = 0;
                }
            }
        } else {
            anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
            wb_set_pending_i(rec, 2);
            *(unsigned int*)(iVar13 + 0x9c) |= 0x4000000;
            entity_set_render_type(iVar13, (void*)&damage_filter_invincible);
        }
        break;
    case 1:
        if (aiactor_check_zone_target_players(iVar13) != 0) {
            int tgt = rec[1];
            fVar1 = *(float*)(iVar13 + 0x64) - *(float*)(tgt + 0x64);
            fVar2 = *(float*)(iVar13 + 0x68) - *(float*)(tgt + 0x68);
            fVar5 = *(float*)(iVar13 + 0x6c) - *(float*)(tgt + 0x6c);
            if (FLOAT_806194f0 <= fVar5 * fVar5 + fVar1 * fVar1 + fVar2 * fVar2) {
                rec[0x277] = (int)FLOAT_80619478;
                rec[0x2a8] = (int)FLOAT_8061947c;
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                wb_set_pending_i(rec, 3);
                iVar14 = rec[0x26e];
                if (iVar14 != 0) {
                    rec[0x1a1] = (iVar14 < 0)
                        ? *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678)
                        : iVar14;
                    rec[0x26b] = 0; rec[0x26a] = 0;
                }
            } else {
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                wb_set_pending_i(rec, 4);
                double t = (double)(FLOAT_806194c0 + *(float*)(iVar13 + 0x70));
                double w = FUN_80390324((double)((float)(t - (double)FLOAT_80619478) / FLOAT_806194e4));
                rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * (double)(float)w - t);
                int head = *rec;
                sound_init_play_params((int)sp3);
                *(unsigned char*)((int)sp3 + 0x24) = 1;
                sp3[0] = 0x4d2;
                *(int*)((int)sp3 + 0x1c) = *(int*)(head + 0x64);
                *(int*)((int)sp3 + 0x20) = *(int*)(head + 0x68);
                *(int*)((int)sp3 + 0x24) = *(int*)(head + 0x6c);
                sound_play_request(sp3);
            }
        }
        break;
    case 2:
        if (anim_active_slot_userflags(*(int*)(iVar13 + 4)) != 0) {
            *(float*)(iVar13 + 0x70) = FLOAT_806194f4;
            *(float*)(iVar13 + 0xc4) = FLOAT_806194f4;
            rec[0x277] = (int)FLOAT_80619478;
            rec[0x2a8] = (int)FLOAT_8061947c;
            anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)(float)rec[0x2a8]);
            wb_set_pending_i(rec, 3);
            iVar14 = rec[0x26e];
            if (iVar14 != 0) {
                rec[0x1a1] = (iVar14 < 0)
                    ? *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678)
                    : iVar14;
                rec[0x26b] = 0; rec[0x26a] = 0;
            }
        }
        break;
    case 3:
        dVar24 = (double)FLOAT_8061947c;
        rec[0x2cf] = (int)FLOAT_80619478;
        if (anim_primary_clip_userflags(iVar13, 0x7f8) != 0) {
            anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)(float)rec[0x2a8]);
        }
        if (rec[0x300] != 0) {
            angles_to_dir((double)FLOAT_80619478, (double)*(float*)(iVar13 + 0x70), local_184);
            iVar14 = rec[0x300];
            fVar4 = *(float*)(iVar14 + 0x3c) - *(float*)(iVar14 + 0x54);
            fVar2 = *(float*)(iVar14 + 0x44) - *(float*)(iVar14 + 0x2c);
            fVar5 = *(float*)(iVar14 + 0x44) - *(float*)(iVar14 + 0x5c);
            fVar1 = *(float*)(iVar14 + 0x3c) - *(float*)(iVar14 + 0x24);
            fVar7 = *(float*)(iVar14 + 0x40) - *(float*)(iVar14 + 0x28);
            fVar8 = *(float*)(iVar14 + 0x40) - *(float*)(iVar14 + 0x58);
            fVar3 = fVar5 * fVar1 - fVar4 * fVar2;
            fVar2 = fVar8 * fVar2 - fVar5 * fVar7;
            fVar1 = fVar4 * fVar7 - fVar8 * fVar1;
            wb_normalize_scaled(&fVar2, &fVar3, &fVar1, FLOAT_8061947c);
            fVar3 = -(-(fVar1 * fVar5 + fVar2 * fVar4 + fVar3 * fVar8) +
                      fVar2 * local_184[0] + fVar1 * local_184[2]) / fVar3;
            fVar4 = (FLOAT_806194fc < fVar3) ? FLOAT_806194fc : fVar3;
            fVar1 = FLOAT_806194f8;
            if (FLOAT_806194f8 <= fVar4) { fVar1 = (FLOAT_806194fc < fVar3) ? FLOAT_806194fc : fVar3; }
            fVar4 = ((FLOAT_8061947c - FLOAT_80619488 * (fVar1 / FLOAT_806194fc)) - (float)rec[0x2a8]) *
                    FLOAT_80619500 + (float)rec[0x2a8];
            fVar1 = (FLOAT_806194e8 < fVar4) ? FLOAT_806194e8 : fVar4;
            fVar2 = FLOAT_80619504;
            if (FLOAT_80619504 <= fVar1) { fVar2 = (FLOAT_806194e8 < fVar4) ? FLOAT_806194e8 : fVar4; }
            rec[0x2a8] = (int)fVar2;
            anim_set_clip_speed_fields((double)(float)rec[0x2a8], *(int**)(iVar13 + 4));
        }
        if (rec[0x26e] == 0) {
            iVar14 = *(int*)(*(int*)(rec[1] + 0x7c) + 0x678);
            if (rec[0x1a1] != iVar14) {
                rec[0x1a1] = (iVar14 < 0)
                    ? *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678)
                    : iVar14;
                rec[0x26b] = 0; rec[0x26a] = 0;
            }
        }
        aiactor_follow_zone_path();
        aiactor_facing_update();
        aiactor_leader_zone_update(iVar13);
        local_190 = (float)rec[0xc] - *(float*)(iVar13 + 0x64);
        local_18c = (float)rec[0xd] - *(float*)(iVar13 + 0x68);
        local_188 = (float)rec[0xe] - *(float*)(iVar13 + 0x6c);
        dir_to_angles(&local_190, 0, &local_1f4);
        {
            double t = (double)(local_1f4 - *(float*)(iVar13 + 0x70));
            double w = FUN_80390324((double)((float)(t - (double)FLOAT_806194f4) / FLOAT_806194e4));
            float yerr = -(float)((double)FLOAT_806194e4 * (double)(float)w - t);
            fVar4 = (FLOAT_8061950c < yerr) ? FLOAT_8061950c : yerr;
            local_1f4 = FLOAT_80619508;
            if (FLOAT_80619508 <= fVar4) { local_1f4 = (FLOAT_8061950c < yerr) ? FLOAT_8061950c : yerr; }
        }
        fVar1 = ((FLOAT_80619510 * local_1f4) / FLOAT_8061950c - (float)rec[0x277]) *
                FLOAT_80619514 + (float)rec[0x277];
        fVar4 = (FLOAT_80619510 < fVar1) ? FLOAT_80619510 : fVar1;
        fVar2 = FLOAT_80619518;
        if (FLOAT_80619518 <= fVar4) { fVar2 = (FLOAT_80619510 < fVar1) ? FLOAT_80619510 : fVar1; }
        rec[0x277] = (int)fVar2;
        rec[0x2cb] = (int)(FLOAT_8061947c + (FLOAT_8061951c * ABS((float)rec[0x277])) / FLOAT_80619510);
        if (rec[0x26e] == 0) {
            iVar14 = rec[1];
            fVar4 = *(float*)(iVar13 + 0x64) - *(float*)(iVar14 + 0x64);
            fVar1 = *(float*)(iVar13 + 0x68) - *(float*)(iVar14 + 0x68);
            fVar2 = *(float*)(iVar13 + 0x6c) - *(float*)(iVar14 + 0x6c);
            uVar12 = (unsigned int)(fVar2 * fVar2 + fVar4 * fVar4 + fVar1 * fVar1 < FLOAT_806194f0);
        } else {
            uVar12 = (unsigned int)((rec[0x26e] - rec[0x19e]) == 0 ? 1 : 0);
        }
        if (uVar12 != 0 && anim_event_scan_active() != 0) {
            anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
            wb_set_pending_i(rec, 4);
            double t = (double)(FLOAT_806194c0 + *(float*)(iVar13 + 0x70));
            double w = FUN_80390324((double)((float)(t - (double)FLOAT_80619478) / FLOAT_806194e4));
            rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * (double)(float)w - t);
            int head = *rec;
            sound_init_play_params((int)sp0);
            *(unsigned char*)((int)sp0 + 0x24) = 1;
            sp0[0] = 0x4d2;
            *(int*)((int)sp0 + 0x1c) = *(int*)(head + 0x64);
            *(int*)((int)sp0 + 0x20) = *(int*)(head + 0x68);
            *(int*)((int)sp0 + 0x24) = *(int*)(head + 0x6c);
            sound_play_request(sp0);
        }
        break;
    case 4: {
        fVar4 = (FLOAT_80619478 - (float)rec[0x277]) * FLOAT_80619520 + (float)rec[0x277];
        fVar1 = (FLOAT_80619510 < fVar4) ? FLOAT_80619510 : fVar4;
        fVar2 = FLOAT_80619518;
        if (FLOAT_80619518 <= fVar1) { fVar2 = (FLOAT_80619510 < fVar4) ? FLOAT_80619510 : fVar4; }
        rec[0x277] = (int)fVar2;
        entity_set_render_type(iVar13, (void*)&worm_damage_scale_apply);
        *(unsigned int*)(iVar13 + 0x9c) &= 0xfbffffff;
        if (anim_event_has_passed(iVar13, 0x200, 1) != 0) {
            int tgtRec = *(int*)(rec[1] + 0x7c);
            local_19c = *(float*)(tgtRec + 0xbb4) - *(float*)(iVar13 + 0x64);
            local_198 = (*(float*)(tgtRec + 0xbb8) - *(float*)(iVar13 + 0x68)) - FLOAT_806194c4;
            local_194 = *(float*)(tgtRec + 0xbbc) - *(float*)(iVar13 + 0x6c);
            dir_to_angles(&local_19c, 0, &local_1f8);
            fVar4 = (float)rec[0x278];
            float d1 = local_1f8 - (fVar4 + FLOAT_806194e4);
            float d0 = ABS(local_1f8 - fVar4);
            float d2 = local_1f8 - (fVar4 - FLOAT_806194e4);
            float pick;
            if (ABS(d1) <= d0) { pick = (ABS(d1) < ABS(d2)) ? d1 : d2; }
            else { pick = (d0 < ABS(d2)) ? (local_1f8 - fVar4) : d2; }
            double t = (double)(FLOAT_80619524 * (float)(((unsigned int)pick & 0x80000000) | 0x3f800000) + fVar4);
            double w = FUN_80390324((double)((float)(t - (double)FLOAT_80619478) / FLOAT_806194e4));
            rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * (double)(float)w - t);
        }
        if (anim_primary_clip_userflags(iVar13, 0x7f9) != 0) {
            angles_to_dir((double)FLOAT_80619478, (double)(float)rec[0x278], spitDir1);
            int aligned = (iVar14 == 0) ? 0
                : aiactor_dir_alignment_test((double)FLOAT_80619528, (float*)(iVar13 + 0x64),
                                             spitDir1, (float*)(rec[1] + 0x64));
            if (aligned == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                wb_set_pending_i(rec, 6);
                rec[0x277] = (int)FLOAT_80619478;
                rec[0x2a8] = (int)FLOAT_80619478;
                rec[0x2cc] = (int)FLOAT_8061952c;
            } else {
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                wb_set_pending_i(rec, 5);
                rec[0x277] = (int)FLOAT_80619478;
                rec[0x2a8] = (int)FLOAT_80619478;
                rec[0x2cc] = (int)FLOAT_80619478;
            }
        }
        break;
    }
    case 5:
        if ((float)rec[0x2cc] <= FLOAT_806194e8) {
            int tgt = iVar15;
            fVar4 = *(float*)(tgt + 0xfc);
            fVar1 = *(float*)(tgt + 0xf8);
            fVar2 = *(float*)(tgt + 0x100);
            wb_normalize_scaled(&fVar1, &fVar4, &fVar2, FLOAT_8061947c);
            int tgtRec = *(int*)(tgt + 0x7c);
            local_1c0 = (fVar1 * FLOAT_80619538 + *(float*)(tgtRec + 0xbb4)) - *(float*)(iVar13 + 0x64);
            local_1bc = ((fVar4 * FLOAT_80619538 + *(float*)(tgtRec + 0xbb8)) - *(float*)(iVar13 + 0x68)) - FLOAT_806194c4;
            local_1b8 = (fVar2 * FLOAT_80619538 + *(float*)(tgtRec + 0xbbc)) - *(float*)(iVar13 + 0x6c);
            dir_to_angles(&local_1c0, &local_1fc, &local_200);
            fVar4 = (float)rec[0x278];
            float d1 = local_200 - (fVar4 + FLOAT_806194e4);
            float d0 = ABS(local_200 - fVar4);
            float d2 = local_200 - (fVar4 - FLOAT_806194e4);
            float pick;
            if (ABS(d1) <= d0) { pick = (ABS(d1) < ABS(d2)) ? d1 : d2; }
            else { pick = (d0 < ABS(d2)) ? (local_200 - fVar4) : d2; }
            double t = (double)(pick * FLOAT_806194e0 + fVar4);
            double w = FUN_80390324((double)((float)(t - (double)FLOAT_80619478) / FLOAT_806194e4));
            rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * (double)(float)w - t);
            fVar4 = (local_1fc - (float)rec[0x277]) * FLOAT_80619490 + (float)rec[0x277];
            fVar1 = (FLOAT_8061952c < fVar4) ? FLOAT_8061952c : fVar4;
            fVar2 = FLOAT_8061953c;
            if (FLOAT_8061953c <= fVar1) { fVar2 = (FLOAT_8061952c < fVar4) ? FLOAT_8061952c : fVar4; }
            rec[0x277] = (int)fVar2;
            if (anim_event_scan_active() == 0) {
                if (anim_event_scan_active() != 0) {
                    mtx4_set_rotation_y((double)((FLOAT_806194bc * *(float*)(iVar13 + 0x70)) / FLOAT_806194c0), bodyMtx);
                    worm_compute_lunge_vector();
                    worm_spawn_spit_projectile(iVar13, 2, bodyMtx, spitDir1);
                    worm_spawn_spit_projectile(iVar13, 3, bodyMtx, spitDir1);
                }
            } else {
                mtx4_set_rotation_y((double)((FLOAT_806194bc * *(float*)(iVar13 + 0x70)) / FLOAT_806194c0), bodyMtx2);
                worm_compute_lunge_vector();
                worm_spawn_spit_projectile(iVar13, 0, bodyMtx2, spitDir2);
                worm_spawn_spit_projectile(iVar13, 1, bodyMtx2, spitDir2);
            }
        } else {
            int tgt = iVar15;
            if ((*(unsigned int*)(rec[0x2fc] + 0x58) & 0x200000) == 0) {
                fVar4 = *(float*)(iVar13 + 0x64) - *(float*)(tgt + 0x64);
                fVar1 = *(float*)(iVar13 + 0x68) - *(float*)(tgt + 0x68);
                fVar2 = *(float*)(iVar13 + 0x6c) - *(float*)(tgt + 0x6c);
                if (FLOAT_80619530 < fVar2 * fVar2 + fVar4 * fVar4 + fVar1 * fVar1 || iVar14 == 0) {
                    int z = *(int*)(*(int*)(tgt + 0x7c) + 0x678);
                    rec[0x1a1] = (z < 0)
                        ? *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678) : z;
                    rec[0x26b] = 0; rec[0x26a] = 0;
                    if (aiactor_nav_advance_path(iVar13, rec + 0x19e) != -2) {
                        wb_set_pending_i(rec, 8);
                        int head = *rec;
                        sound_init_play_params((int)sp1);
                        *(unsigned char*)((int)sp1 + 0x24) = 1;
                        sp1[0] = 0x4d3;
                        *(int*)((int)sp1 + 0x1c) = *(int*)(head + 0x64);
                        *(int*)((int)sp1 + 0x20) = *(int*)(head + 0x68);
                        *(int*)((int)sp1 + 0x24) = *(int*)(head + 0x6c);
                        sound_play_request(sp1);
                        anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                        goto done;
                    }
                }
            }
            angles_to_dir((double)FLOAT_80619478, (double)(float)rec[0x278], spitDir2);
            int aligned = (iVar14 == 0) ? 0
                : aiactor_dir_alignment_test((double)FLOAT_80619528, (float*)(iVar13 + 0x64),
                                             spitDir2, (float*)(rec[1] + 0x64));
            if (aligned == 0) {
                rec[0x2cc] = (int)FLOAT_80619478;
                rec[0x2cd] = (int)FLOAT_806194e8;
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                wb_set_pending_i(rec, 7);
            } else {
                rec[0x2a8] = (int)FLOAT_80619478;
                rec[0x2cc] = (int)FLOAT_80619478;
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                wb_set_pending_i(rec, 6);
            }
            worm_setup_dust_emitters();
        }
        break;
    case 6:
        if (iVar14 == 0) {
            if ((float)rec[0x2cd] < FLOAT_8061947c) { iVar14 = 1; }
        } else {
            rec[0x2cd] = (int)FLOAT_80619478;
        }
        if (iVar14 == 0) {
            rec[0x2cc] = (int)FLOAT_80619478;
            if (rec[8] != 7) {
                rec[0x275] = rec[8]; rec[8] = 7;
                rec[0x276] = (int)FLOAT_80619478; rec[0x274] = rec[8];
            }
        } else {
            int tgtRec = *(int*)(rec[1] + 0x7c);
            local_1f0 = *(float*)(tgtRec + 0xbb4) - *(float*)(iVar13 + 0x64);
            local_1ec = (*(float*)(tgtRec + 0xbb8) - *(float*)(iVar13 + 0x68)) - FLOAT_806194c4;
            local_1e8 = *(float*)(tgtRec + 0xbbc) - *(float*)(iVar13 + 0x6c);
            dir_to_angles(&local_1f0, &local_20c, &local_210);
            local_210 = local_210 + (float)rec[0x2a8];
            rec[0x2a8] = (int)((float)rec[0x2a8] * FLOAT_8061954c);
            fVar4 = (float)rec[0x278];
            float d1 = local_210 - (fVar4 + FLOAT_806194e4);
            float d0 = ABS(local_210 - fVar4);
            float d2 = local_210 - (fVar4 - FLOAT_806194e4);
            float pick;
            if (ABS(d1) <= d0) { pick = (ABS(d1) < ABS(d2)) ? d1 : d2; }
            else { pick = (d0 < ABS(d2)) ? (local_210 - fVar4) : d2; }
            double t = (double)(pick * FLOAT_806194e0 + fVar4);
            double w = FUN_80390324((double)((float)(t - (double)FLOAT_80619478) / FLOAT_806194e4));
            rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * (double)(float)w - t);
            fVar4 = (local_20c - (float)rec[0x277]) * FLOAT_80619490 + (float)rec[0x277];
            fVar1 = (FLOAT_8061952c < fVar4) ? FLOAT_8061952c : fVar4;
            fVar2 = FLOAT_8061953c;
            if (FLOAT_8061953c <= fVar1) { fVar2 = (FLOAT_8061952c < fVar4) ? FLOAT_8061952c : fVar4; }
            rec[0x277] = (int)fVar2;
            angles_to_dir((double)FLOAT_80619478, (double)(float)rec[0x278], &local_1f0);
            if (FLOAT_80619490 <= ABS((float)rec[0x2a8]) || (float)rec[0x2cc] <= FLOAT_806194e8 ||
                aiactor_dir_alignment_test((double)FLOAT_80619540, (float*)(iVar13 + 0x64),
                                           &local_1f0, (float*)(rec[1] + 0x64)) == 0) {
                if (aiactor_dir_alignment_test((double)FLOAT_80619550, (float*)(iVar13 + 0x64),
                                               &local_1f0, (float*)(rec[1] + 0x64)) == 0) {
                    rec[0x2cc] = (int)FLOAT_80619478;
                    if (rec[8] != 7) {
                        rec[0x275] = rec[8]; rec[8] = 7;
                        rec[0x276] = (int)FLOAT_80619478; rec[0x274] = rec[8];
                    }
                }
            } else {
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                wb_set_pending_i(rec, 5);
                rec[0x2cc] = (int)FLOAT_80619478;
            }
        }
        break;
    case 7:
        if ((*(unsigned int*)(rec[0x2fc] + 0x58) & 0x200000) == 0) {
            int tgt = iVar15;
            fVar4 = *(float*)(iVar13 + 0x64) - *(float*)(tgt + 0x64);
            fVar1 = *(float*)(iVar13 + 0x68) - *(float*)(tgt + 0x68);
            fVar2 = *(float*)(iVar13 + 0x6c) - *(float*)(tgt + 0x6c);
            if ((FLOAT_80619530 < fVar2 * fVar2 + fVar4 * fVar4 + fVar1 * fVar1 || iVar14 == 0) &&
                FLOAT_806194e8 < (float)rec[0x2cc]) {
                int z = *(int*)(*(int*)(tgt + 0x7c) + 0x678);
                rec[0x1a1] = (z < 0)
                    ? *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678) : z;
                rec[0x26b] = 0; rec[0x26a] = 0;
                if (aiactor_nav_advance_path(iVar13, rec + 0x19e) != -2) {
                    wb_set_pending_i(rec, 8);
                    int head = *rec;
                    sound_init_play_params((int)sp2);
                    *(unsigned char*)((int)sp2 + 0x24) = 1;
                    sp2[0] = 0x4d3;
                    *(int*)((int)sp2 + 0x1c) = *(int*)(head + 0x64);
                    *(int*)((int)sp2 + 0x20) = *(int*)(head + 0x68);
                    *(int*)((int)sp2 + 0x24) = *(int*)(head + 0x6c);
                    sound_play_request(sp2);
                    anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                    goto done;
                }
                rec[0x2cc] = (int)FLOAT_80619478;
            }
        }
        if (iVar14 == 0) {
            if ((float)rec[0x2cd] < FLOAT_8061947c) { iVar14 = 1; }
        } else {
            rec[0x2cd] = (int)FLOAT_80619478;
        }
        if (iVar14 != 0) {
            int tgtRec = *(int*)(rec[1] + 0x7c);
            local_1e4 = *(float*)(tgtRec + 0xbb4) - *(float*)(iVar13 + 0x64);
            local_1e0 = (*(float*)(tgtRec + 0xbb8) - *(float*)(iVar13 + 0x68)) - FLOAT_806194c4;
            local_1dc = *(float*)(tgtRec + 0xbbc) - *(float*)(iVar13 + 0x6c);
            dir_to_angles(&local_1e4, &local_204, &local_208);
            fVar4 = (float)rec[0x278];
            float d1 = local_208 - (fVar4 + FLOAT_806194e4);
            float d0 = ABS(local_208 - fVar4);
            float d2 = local_208 - (fVar4 - FLOAT_806194e4);
            float pick;
            if (ABS(d1) <= d0) { pick = (ABS(d1) < ABS(d2)) ? d1 : d2; }
            else { pick = (d0 < ABS(d2)) ? (local_208 - fVar4) : d2; }
            rec[0x2a8] = ((unsigned int)pick & 0x80000000) | 0x3f800000;
            fVar4 = (float)rec[0x278];
            d1 = local_208 - (fVar4 + FLOAT_806194e4);
            d0 = ABS(local_208 - fVar4);
            d2 = local_208 - (fVar4 - FLOAT_806194e4);
            if (ABS(d1) <= d0) { pick = (ABS(d1) < ABS(d2)) ? d1 : d2; }
            else { pick = (d0 < ABS(d2)) ? (local_208 - fVar4) : d2; }
            double t = (double)(FLOAT_80619524 * (float)(((unsigned int)pick & 0x80000000) | 0x3f800000) + fVar4);
            double w = FUN_80390324((double)((float)(t - (double)FLOAT_80619478) / FLOAT_806194e4));
            rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * (double)(float)w - t);
            fVar4 = (local_204 - (float)rec[0x277]) * FLOAT_80619490 + (float)rec[0x277];
            fVar1 = (FLOAT_8061952c < fVar4) ? FLOAT_8061952c : fVar4;
            fVar2 = FLOAT_8061953c;
            if (FLOAT_8061953c <= fVar1) { fVar2 = (FLOAT_8061952c < fVar4) ? FLOAT_8061952c : fVar4; }
            rec[0x277] = (int)fVar2;
            angles_to_dir((double)FLOAT_80619478, (double)(float)rec[0x278], &local_1e4);
            if (aiactor_dir_alignment_test((double)FLOAT_80619540, (float*)(iVar13 + 0x64),
                                           &local_1e4, (float*)(rec[1] + 0x64)) != 0) {
                fVar4 = (FLOAT_80619548 < ABS((float)rec[0x2a8])) ? FLOAT_80619544 : FLOAT_80619478;
                rec[0x2a8] = (int)(fVar4 * (float)(((unsigned int)rec[0x2a8] & 0x80000000) | 0x3f800000));
                wb_set_pending_i(rec, 6);
                rec[0x2cc] = (int)FLOAT_8061948c;
            }
        }
        break;
    case 8:
        {
            fVar4 = (float)rec[0x278];
            fVar1 = FLOAT_806194c0 + *(float*)(iVar13 + 0x70);
            float d1 = fVar1 - (fVar4 + FLOAT_806194e4);
            float d0 = ABS(fVar1 - fVar4);
            float d2 = fVar1 - (fVar4 - FLOAT_806194e4);
            float pick;
            if (ABS(d1) <= d0) { pick = (ABS(d1) < ABS(d2)) ? d1 : d2; }
            else { pick = (d0 < ABS(d2)) ? (fVar1 - fVar4) : d2; }
            double t = (double)(FLOAT_80619490 * (float)(((unsigned int)pick & 0x80000000) | 0x3f800000) + fVar4);
            double w = FUN_80390324((double)((float)(t - (double)FLOAT_80619478) / FLOAT_806194e4));
            rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * (double)(float)w - t);
            fVar4 = (FLOAT_80619478 - (float)rec[0x277]) * FLOAT_80619490 + (float)rec[0x277];
            fVar1 = (FLOAT_8061952c < fVar4) ? FLOAT_8061952c : fVar4;
            fVar2 = FLOAT_8061953c;
            if (FLOAT_8061953c <= fVar1) { fVar2 = (FLOAT_8061952c < fVar4) ? FLOAT_8061952c : fVar4; }
            rec[0x277] = (int)fVar2;
            if (anim_is_clip_active(iVar13, 0x7fb) == 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
            } else if (anim_active_slot_userflags(*(int*)(iVar13 + 4)) != 0) {
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_8061947c);
                wb_set_pending_i(rec, 3);
                *(unsigned int*)(iVar13 + 0x9c) |= 0x4000000;
                entity_set_render_type(iVar13, (void*)&damage_filter_invincible);
            }
        }
        break;
    case 9:
        {
            double curYaw = (double)*(float*)(iVar13 + 0x70);
            double tgtYaw = (double)(float)rec[0x278];
            double diff = (double)(float)(curYaw - tgtYaw);
            if ((double)FLOAT_80619554 <= ABS(diff)) {
                double d1 = (double)(float)(curYaw - (double)(float)(tgtYaw + (double)FLOAT_806194e4));
                double d2 = (double)(float)(curYaw - (double)(float)(tgtYaw - (double)FLOAT_806194e4));
                double pick = d2;
                if (ABS(d1) <= ABS(diff)) { if (ABS(d1) < ABS(d2)) { pick = d1; } }
                else if (ABS(diff) < ABS(d2)) { pick = diff; }
                double t = (double)(float)(pick * (double)FLOAT_806194e0 + tgtYaw);
                double w = (double)(float)FUN_80390324((double)((float)(t - (double)FLOAT_80619478) / FLOAT_806194e4));
                rec[0x278] = (int)-(float)((double)FLOAT_806194e4 * w - t);
            } else {
                rec[0x278] = (int)*(float*)(iVar13 + 0x70);
            }
            if (anim_active_slot_userflags(*(int*)(iVar13 + 4)) != 0) {
                projectile_spawn(param_2, param_3, param_4, param_5, 0, 0, param_7, param_8);
                worm_setup_dust_emitters();
                wb_set_pending_i(rec, 10);
                goto done;
            }
        }
        break;
    case 10:
        {
            unsigned long long h = (unsigned long long)(unsigned int)worm_map100_arcade_hook(iVar13);
            aiactor_destroy_instance(h, param_2, param_3, param_4, param_5, 0, param_7, param_8, iVar13);
        }
        goto done;
    }
    sound_update_3d_source(dVar24, (double)FLOAT_80619558, (double)FLOAT_8061947c,
                           *rec + 0x22c, rec, *rec + 0x64, 0);
    (void)dVar17; (void)dVar19; (void)dirScratch;
done:
    FUN_8038273c();
}

// ===========================================================================
// 0x8028cc34  worm_burrower_tick
// Smaller burrower-creature FSM (buried(0)/emerge(1)/attack(2)) that shares the
// worm rodata constant block.
// ===========================================================================
void worm_burrower_tick(int entity) {
    int rec = *(int*)(entity + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(rec + 0x9d0)) {
        if (*(int*)(rec + 0x9d0) != *(int*)(rec + 0x20)) {
            *(float*)(rec + 0x9d8) = FLOAT_80619478;
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = *(int*)(rec + 0x9d0);
        }
        *(int*)(rec + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(rec + 0x2c) & 0x80) == 0) {
        *(float*)(rec + 0xa90) = FLOAT_80619478;
    } else {
        unsigned int biasBits = (unsigned int)DAT_806109c0 ^ 0x80000000u;
        *(float*)(rec + 0xa90) =
            FLOAT_806108ec * (FLOAT_8061947c / wb_unit(biasBits, DOUBLE_806194a8)) +
            *(float*)(rec + 0xa90);
    }

    int state = *(int*)(rec + 0x20);
    if (state == 1) {
        if ((*(unsigned int*)(rec + 0x28) & 0x8400) != 0 ||
            aiactor_check_zone_target_players(entity) != 0) {
            int sp[7];
            sound_init_play_params((int)sp);
            *(unsigned char*)((int)sp + 0x24) = 1;
            sp[0] = 0x4d8;
            *(int*)((int)sp + 0x1c) = *(int*)(entity + 0x64);
            *(int*)((int)sp + 0x20) = *(int*)(entity + 0x68);
            *(int*)((int)sp + 0x24) = *(int*)(entity + 0x6c);
            sound_play_request(sp);
            if (*(int*)(rec + 0x20) != 2) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 2;
                *(float*)(rec + 0x9d8) = FLOAT_80619478;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
    } else if (state < 1) {
        if (-1 < state) {
            aiactor_set_stance_flags(entity, 4);
            if ((*(unsigned int*)(rec + 0x28) & 0x20000000) == 0) {
                float kf[8];
                anim_sample_keyframe(*(int*)(DAT_80611824 + DAT_804d1e96 * 4), 0, 0, kf);
                *(int*)(*(int*)(entity + 4) + 0xec) = *(int*)&kf[1];
                anim_play_scaled((unsigned long long)(double)FLOAT_80619478, (double)FLOAT_80619478);
                if (*(int*)(rec + 0x20) != 1) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 1;
                    *(float*)(rec + 0x9d8) = FLOAT_80619478;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
            } else if (*(int*)(rec + 0x20) != 2) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 2;
                *(float*)(rec + 0x9d8) = FLOAT_80619478;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
    } else if (state < 3) {
        anim_set_clip_speed_fields((double)FLOAT_8061947c, *(int**)(entity + 4));
        if (anim_primary_clip_time_left((double)FLOAT_80619488, *(int*)(entity + 4)) != 0) {
            int skip = 0;
            if (*(int*)(rec + 0xaac) == 1 && *(int*)(rec + 0xaa8) == 0) {
                unsigned int u = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x14)
                                                        : *(unsigned int*)(rec + 0x1c);
                if ((u & 1) == 0) { skip = 1; }
            }
            if (!skip) {
                *(int*)(rec + 0xaac) = 4;
                *(int*)(rec + 0xaa8) = 0;
            }
        }
    }
}
