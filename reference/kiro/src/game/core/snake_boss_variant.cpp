// snake_boss_variant.cpp - alternate ("reactor"/level-11) snake-boss FSM and the
// top-level tick/event dispatchers that select between the two boss variants.
//
// The snake boss ships in two flavours selected by the level/mode id DAT_80612614:
//   mode 1  -> the "attack" boss in snake_boss_tick.cpp (snake_boss_behavior_tick)
//   else    -> this "reactor" boss (snake2_boss_behavior_tick), a scripted set
//              piece with a rider (team 'BRJP'), a rising platform, spawned
//              sub-entities (0x4bdc/0x4bd8/0x4bcc/0x4270) and a 0x10-state FSM.
// Both share the DAT_80612a58 boss-global block, the rope subsystem
// (snake_boss_rope.cpp) and the orbiter subsystem (snake_boss_orbiter.cpp).
//
// Functions:
//   snake2_segment_render_sync   (0x802936d8): segment render cb (platform lift,
//     transform-block copy, rider positioning).
//   snake2_damage_flash_material (0x80293868): sine-LUT pulsing flash material.
//   snake2_destroy_entities      (0x80293974): destroy the 4 sub-entities.
//   snake2_set_collision_flag8   (0x80293a04): set collision flag bit 8.
//   snake2_boss_behavior_tick    (0x80293a2c): the alternate boss master FSM.
//   snake2_event_handler         (0x802954a8): alternate-boss event dispatch.
//   snake2_rope_emit_a           (0x80295588): rope emit from bone-pair (A).
//   snake2_rope_emit_b           (0x80295980): rope emit from bone-pair (B).
//   snake_tick_dispatch          (0x80296024): per-frame tick variant selector.
//   snake_event_dispatch         (0x80296058): event variant selector.
//
// Address range: 0x802936d8 - 0x80296058
// Subsystem: game (AI actors / boss)

#include "common.h"

extern "C" {

// --- raw actor-context accessors (still FUN_ in the IDB) -------------------
extern int    FUN_803826e4(void);        // current actor entity record (hi=count, lo=ptr)
extern void   FUN_80382730(void);        // tick epilogue
extern unsigned long long FUN_802ae344(int mtx, float* palette, int a3, int a4, double a5, int a6, int a7); // bone-aim rig
extern void   FUN_802ad80c(double a, double b, double c, double d);   // fx setup
extern int    FUN_802adba4(void);         // resolve spawn type for the aura entity
extern short  FUN_8037daf0(unsigned long long h, double a2, double a3, double a4, double a5,
                           double a6, unsigned long long a7, unsigned long long a8, int a9,
                           int a10, int a11, float* a12, float* a13, int a14, int a15, float a16);

// --- same-module snake helpers (defined in the other snake_boss_*.cpp) ------
extern void snake_rope_fire_probe(void);
extern void snake_rope_integrate_full(void);
extern void snake_rope_ring_push(void* pos, void* dir, int emit, unsigned int tag);
extern void snake_rope_player_touch_damage(void);
extern int  snake_rope_nearest_node(float* pt);
extern void snake_update_target_angle_b(int actor);
extern void snake_boss_behavior_tick(void);
extern void snake_event_bite_react(int a, int b, int c);
extern void snake_orbiter_acquire_targets(void);
extern void snake_orbiter_update_all(void);
extern void snake_orbiter_spawn_ring(void);
extern void snake_lift_audio_state(int actor);

// --- data globals ----------------------------------------------------------
extern int   DAT_80612a58;               // snake-boss global control block base
extern int   DAT_80612614;               // current game/level mode id
extern int   DAT_80612610;               // player count
extern int   DAT_80612020;               // damage/flash timer source
extern int   DAT_806109c0;               // frame delta ticks (signed, xor bias)
extern int   DAT_80611d78;               // player record table base (stride 0x141c)
extern int   DAT_80611d74;               // active-actor global record base
extern const float FLOAT_80583228[];     // 0x800-entry sine LUT (rodata)
extern int   DAT_803e0618, DAT_803e061c, DAT_803e0620;   // spawn offsets (rodata)
extern int   DAT_803e0624, DAT_803e0628, DAT_803e062c;
extern int   DAT_803e0664, DAT_803e0668, DAT_803e066c;
extern int   DAT_803e0670, DAT_803e0674, DAT_803e0678;
extern int   DAT_803e067c, DAT_803e0680, DAT_803e0684;
extern int   DAT_803e0688, DAT_803e068c, DAT_803e0690;
extern int   DAT_803e0694, DAT_803e0698, DAT_803e069c;
extern int   DAT_803e06a0, DAT_803e06a4, DAT_803e06a8;
extern int   DAT_803e06ac, DAT_803e06b0, DAT_803e06b4;
extern int   DAT_803e06b8, DAT_803e06bc, DAT_803e06c0;
extern char  LAB_802937e0;               // rider render callback

// --- float / double constants (rodata, snake block) ------------------------
extern const float  FLOAT_80619560, FLOAT_8061956c, FLOAT_80619564, FLOAT_80619568;
extern const float  FLOAT_80619570, FLOAT_80619574, FLOAT_80619578, FLOAT_80619580;
extern const float  FLOAT_80619584, FLOAT_80619588, FLOAT_8061958c, FLOAT_80619590;
extern const float  FLOAT_8061959c, FLOAT_806195a0, FLOAT_806195a4, FLOAT_806195a8;
extern const float  FLOAT_806195ac, FLOAT_806195b4, FLOAT_806195bc, FLOAT_806195c0;
extern const float  FLOAT_806195c4, FLOAT_806195d4, FLOAT_806195e4, FLOAT_806195e8;
extern const float  FLOAT_806195f4, FLOAT_806195f8, FLOAT_806195fc, FLOAT_80619608;
extern const float  FLOAT_80619618, FLOAT_80619624, FLOAT_80619628, FLOAT_8061963c;
extern const float  FLOAT_80619648, FLOAT_8061964c, FLOAT_80619654, FLOAT_80619670;
extern const float  FLOAT_80619674, FLOAT_80619684, FLOAT_80619688, FLOAT_8061968c;
extern const float  FLOAT_806196c0, FLOAT_806196c4, FLOAT_806196c8, FLOAT_806196cc;
extern const float  FLOAT_806196d0, FLOAT_806196d4, FLOAT_806196d8, FLOAT_806196ec;
extern const float  FLOAT_806196f0, FLOAT_806196f4, FLOAT_8061972c, FLOAT_80619730;
extern const float  FLOAT_80619738, FLOAT_80619754, FLOAT_80619758, FLOAT_80619760;
extern const float  FLOAT_80619764, FLOAT_80619768, FLOAT_80619770, FLOAT_80619774;
extern const float  FLOAT_80619778, FLOAT_8061977c, FLOAT_80619780, FLOAT_80619784;
extern const float  FLOAT_80619788, FLOAT_8061978c, FLOAT_80619700, FLOAT_806108e8;
extern const float  FLOAT_806108ec, FLOAT_8061976c, FLOAT_8061967c;
extern const double DOUBLE_80619610;     // rng bias
extern const double DOUBLE_80619630;     // frame-delta bias

// --- named cross-module helpers (call-site typed) --------------------------
extern double math_sqrt(double x);
extern unsigned int rng_counter_tick(void);
extern void model_pose_eval(void);
extern void dir_to_angles(float* dir, float* pitch, float* yaw);
extern void angles_to_dir(double pitch, double yaw, float* dir);
extern void mtx4_transform_point(float* mtx, float* pt);
extern void mtx4_transform_vector(float* mtx, float* v);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void entity_build_local_matrix(float* outMtx, int entity, int includePos, int mirror);

extern void collision_set_flags(int* collObj, int flags);
extern unsigned long long entity_destroy_with_event(unsigned int* entity);
extern unsigned int* entity_get_world_position(int entity);
extern int  entity_spawn_by_type(unsigned long long a, double b, double c, double d,
                                 unsigned long long e, unsigned long long f, unsigned long long g,
                                 unsigned long long h, int typeId, float* pos, float* rot, int p12, int p13);
extern void entity_register_for_update(int entity, int phase, int cb);
extern void entity_setup_attachment(int entity, int* parentSlot, int a3, int a4, int a5);
extern unsigned long long entity_set_render_type(int entity, void* cb);

extern double anim_primary_clip_phase(int animCtx);
extern int  anim_active_slot_userflags(int animCtx);
extern int  anim_primary_clip_userflags(int actor, int clipId);
extern int  anim_is_clip_active(int actor, int clipId);
extern unsigned long long anim_play_scaled(double a, double b);

extern void aiactor_retarget_visible_teammate(void);
extern void aiactor_turn_toward_leader(void);
extern int  aiactor_find_player_by_team(int teamTag);
extern unsigned long long aiactor_destroy_instance(unsigned long long a, double b, double c, double d,
                          unsigned long long e, unsigned long long f, unsigned long long g,
                          unsigned long long h, int actor);

extern void player_apply_camera_shake(double x, double y, double z, int playerRec);
extern void player_ai_explosion_spawn(int a, int b, float* pos, float* p4, int p5, int p6, int p7);

extern int  screen_effect_alloc_slot(void);
extern void screen_effect_init_slot(int slot, int entity);
extern unsigned int sprite_get_texture_id(int id);
extern int  damage_filter_invincible(void);   // used as a render/filter cb pointer

extern void particle_def_create(double a, double b, int defPtr, int p4, int p5, int p6, int p7, int p8);
extern void particle_def_set_size_keyframes(double a, double b, double c, double d, int e, int f, int g);
extern void particle_def_set_gravity(double g, int flags);
extern void particle_def_set_color_end(double r, double g, double b, double a);
extern void particle_def_set_velocity_ranges(double a, double b, double c, double d, double e, double f);
extern void particle_def_set_drag(double drag);
extern void particle_emitter_create(double a, double b, double c, int emitPtr, int defPtr, int p6, int flags);
extern void particle_emitter_set_color_range(double a, double b, double c, double d);
extern void particle_emitter_set_lifetime_range(double a, double b);
extern void particle_emitter_set_drag(double drag);
extern unsigned long long particle_emitter_set_size_range(double a, double b);
extern void particle_emitter_spawn(int emitPtr, int defPtr, int flags, float* pos, float* dir);
extern unsigned long long particle_handle_clear(unsigned int* handle);
extern void particle_cached_set_drag(double drag, int* handleSlot);
extern void particle_cached_set_size_range(double a, double b, int* handleSlot);

extern void worm_splash_emit_oneshot(double a, int handlePtr, float* pos, char flag);
extern int* worm_event_find_record_by_class(int classId);
extern int  worm_event_record_is_active(int rec);

extern int  soundpad_find_zone_by_id(int id);
extern void sound_init_play_params(int paramsPtr);
extern unsigned int sound_play_request(int* params);

extern unsigned long long statlvl_class_complete(double a, double b, double c, double d,
                          unsigned long long e, unsigned long long f,
                          unsigned long long g, unsigned long long h);
extern unsigned long long ghz_reactor_match_end(unsigned long long a, double b, double c, double d,
                          unsigned long long e, unsigned long long f,
                          unsigned long long g, unsigned long long h);

} // extern "C"

// forward decl: defined below, called by snake2_boss_behavior_tick
void snake2_rope_emit_b(int actor);

// ===========================================================================
// 0x802936d8  snake2_segment_render_sync
// Segment render callback: evaluate the pose, then depending on the lift state
// either lift the platform Y (state 5) or copy the cached transform block into
// the segment entity (state 7); finally park the linked entity above the boss.
// ===========================================================================
void snake2_segment_render_sync(int entity) {
    model_pose_eval();
    if (*(int*)(DAT_80612a58 + 0x4be4) == 5) {
        *(float*)(entity + 0x68) += *(float*)(DAT_80612a58 + 0x4bec);
    } else if (*(int*)(DAT_80612a58 + 0x4be4) == 7) {
        *(int*)(entity + 0x274) = *(int*)(DAT_80612a58 + 0x4290);
        *(int*)(entity + 0x278) = *(int*)(DAT_80612a58 + 0x4294);
        *(int*)(entity + 0x27c) = *(int*)(DAT_80612a58 + 0x4298);
        *(int*)(entity + 0x284) = *(int*)(DAT_80612a58 + 0x4284);
        *(int*)(entity + 0x288) = *(int*)(DAT_80612a58 + 0x4288);
        *(int*)(entity + 0x28c) = *(int*)(DAT_80612a58 + 0x428c);
        *(int*)(entity + 0x294) = *(int*)(DAT_80612a58 + 0x4278);
        *(int*)(entity + 0x298) = *(int*)(DAT_80612a58 + 0x427c);
        *(int*)(entity + 0x29c) = *(int*)(DAT_80612a58 + 0x4280);
    }
    if ((*(int*)(DAT_80612a58 + 0x4bd4) != 0) && (*(int*)(DAT_80612a58 + 0x4be0) == 2)) {
        int linked = *(int*)(DAT_80612a58 + 0x4bd4);
        *(float*)(linked + 0x70) = FLOAT_80619568;
        *(float*)(linked + 0xc4) = FLOAT_80619568;
        *(float*)(linked + 0x68) = FLOAT_806196c0 + *(float*)(entity + 0x68);
    }
}

// ===========================================================================
// 0x80293868  snake2_damage_flash_material
// Build a pulsing flash color from a sine LUT indexed by the damage timer and
// per-frame delta, store it into the boss material block at +0x4c28.., and bind
// that block as the entity's render override material (+0x254, flag 0x10).
// ===========================================================================
void snake2_damage_flash_material(int entity) {
    float w = FLOAT_80619764;
    float a = FLOAT_806195a8;
    float t = FLOAT_80619574 * (FLOAT_80619628 - *(float*)(DAT_80612a58 + 0x4274));
    float sub = t * FLOAT_80619760;
    int lutIdx = (int)(FLOAT_80619754 * FLOAT_80619578 * FLOAT_80619588 *
        (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_80612020 ^ 0x80000000)) - DOUBLE_80619630) *
        (FLOAT_80619580 / (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630)) *
        (FLOAT_80619580 + t) + FLOAT_80619574) & 0x7ff;
    float v = FLOAT_806196d8 * (FLOAT_80619580 + FLOAT_80583228[lutIdx]) + FLOAT_80619574;
    *(float*)(DAT_80612a58 + 0x4c28) = v;
    v = v - sub;
    *(float*)(DAT_80612a58 + 0x4c2c) = v;
    *(float*)(DAT_80612a58 + 0x4c30) = v;
    *(float*)(DAT_80612a58 + 0x4c78) = a;
    *(float*)(DAT_80612a58 + 0x4c6c) = w;
    *(float*)(DAT_80612a58 + 0x4c60) = w;
    *(int*)(entity + 0x254) = DAT_80612a58 + 0x4c28;
    *(unsigned short*)(*(int*)(entity + 4) + 0x1ee) |= 0x10;
}

// ===========================================================================
// 0x80293974  snake2_destroy_entities
// Destroy the 4 spawned boss sub-entities and null their slot pointers.
// ===========================================================================
void snake2_destroy_entities(void) {
    const int off[4] = {0x4bdc, 0x4bd8, 0x4bcc, 0x4270};
    for (int i = 0; i < 4; i++) {
        if (*(unsigned int**)(DAT_80612a58 + off[i]) != 0) {
            entity_destroy_with_event(*(unsigned int**)(DAT_80612a58 + off[i]));
        }
    }
    for (int i = 0; i < 4; i++) {
        *(int*)(DAT_80612a58 + off[i]) = 0;
    }
}

// ===========================================================================
// 0x80293a04  snake2_set_collision_flag8
// Set collision flag bit 8 on the entity's collision object.
// ===========================================================================
void snake2_set_collision_flag8(int entity) {
    collision_set_flags(*(int**)(entity + 4), 8);
}

// ===========================================================================
// 0x80293a2c  snake2_boss_behavior_tick
// The alternate "reactor" snake-boss master FSM (states 0-0x10). Runs the rope
// fire-probe + target update every frame, and once the boss is "live"
// (+0x3e88 != 0) also runs the rope-B integrate, orbiter acquire/update, lift
// audio, rider positioning and the bone-aim rig. The switch drives the scripted
// sequence: spawn/setup (0), attack cycle (1-8, 10, 0xb, 0xc), idle (0xd),
// death spawn + orbiter destroy (0xe), cleanup (0xf), match-end (0x10).
// ===========================================================================
void snake2_boss_behavior_tick(void) {
    int actorRec = FUN_803826e4();
    int rec = *(int*)(actorRec + 0x7c);
    float palette[17];      // afStack_9c local matrix
    float aimMtx[13];       // local_dc mtx4_multiply output block
    float boneMid1[3];      // local_17c/178/174
    float boneMid2[3];      // local_188/184/180
    float headMid[3];       // local_194/190/18c

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
            (FLOAT_80619580 / (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630))
            + *(float*)(rec + 0xa90);
    }

    entity_build_local_matrix(palette, actorRec, 1, 0);

    // Two mid-bone world points + the aim matrix.
    int* skel = *(int**)(*(int*)(actorRec + 4) + 4);
    {
        float* bp = (float*)skel[*skel * -0x30 + 0x298];
        boneMid1[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
        boneMid1[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
        boneMid1[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
        mtx4_transform_point((float*)(*(int*)(*(int*)(actorRec + 4) + 0x10) + *(char*)((int)skel + *skel * -0xc0 + 0x9fd) * 0x40), boneMid1);
        mtx4_transform_point(palette, boneMid1);
    }
    skel = *(int**)(*(int*)(actorRec + 4) + 4);
    int skelN = *skel;
    {
        float* bp = (float*)skel[skelN * -0x30 + 0x358];
        boneMid2[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
        boneMid2[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
        boneMid2[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
        int aimBone = *(int*)(*(int*)(actorRec + 4) + 0x10) + *(char*)((int)skel + skelN * -0xc0 + 0xcfd) * 0x40;
        mtx4_transform_point((float*)aimBone, boneMid2);
        mtx4_transform_point(palette, boneMid2);
        mtx4_multiply((float*)aimBone, palette, aimMtx);
    }
    // Cache the aim rotation/translation (aimMtx layout: rows at +0x30/+0x40/+0x50).
    *(int*)(DAT_80612a58 + 0x4278) = *(int*)&aimMtx[8];
    *(int*)(DAT_80612a58 + 0x427c) = *(int*)&aimMtx[9];
    *(int*)(DAT_80612a58 + 0x4280) = *(int*)&aimMtx[10];
    *(int*)(DAT_80612a58 + 0x4284) = *(int*)&aimMtx[4];
    *(int*)(DAT_80612a58 + 0x4288) = *(int*)&aimMtx[5];
    *(int*)(DAT_80612a58 + 0x428c) = *(int*)&aimMtx[6];
    *(float*)(DAT_80612a58 + 0x4290) = aimMtx[0];
    *(int*)(DAT_80612a58 + 0x4294) = *(int*)&aimMtx[1];
    *(int*)(DAT_80612a58 + 0x4298) = *(int*)&aimMtx[2];

    // Muzzle direction = aim-fwd * range - boneMid2, negated.
    if (*(int*)(rec + 0x20) == 0xb) {
        double phase = anim_primary_clip_phase(*(int*)(actorRec + 4));
        if ((double)FLOAT_80619574 < phase) phase = (double)(float)((double)FLOAT_80619580 - phase);
        float k = (float)((double)FLOAT_8061972c - phase);
        boneMid1[0] = *(float*)(DAT_80612a58 + 0x4290) * k - boneMid2[0];
        boneMid1[1] = *(float*)(DAT_80612a58 + 0x4294) * k - boneMid2[1];
        boneMid1[2] = *(float*)(DAT_80612a58 + 0x4298) * k - boneMid2[2];
    } else {
        boneMid1[0] = FLOAT_8061972c * *(float*)(DAT_80612a58 + 0x4290) - boneMid2[0];
        boneMid1[1] = FLOAT_8061972c * *(float*)(DAT_80612a58 + 0x4294) - boneMid2[1];
        boneMid1[2] = FLOAT_8061972c * *(float*)(DAT_80612a58 + 0x4298) - boneMid2[2];
    }
    boneMid1[0] = -boneMid1[0]; boneMid1[1] = -boneMid1[1]; boneMid1[2] = -boneMid1[2];
    *(float*)(DAT_80612a58 + 0x429c) = boneMid1[0];
    *(float*)(DAT_80612a58 + 0x42a0) = boneMid1[1];
    *(float*)(DAT_80612a58 + 0x42a4) = boneMid1[2];

    skel = *(int**)(*(int*)(actorRec + 4) + 4);
    {
        float* bp = (float*)skel[*skel * -0x30 + 0x7d8];
        headMid[0] = (float)((double)FLOAT_80619574 * (double)(float)((double)bp[3] - (double)bp[0]) + (double)bp[0]);
        headMid[1] = (float)((double)FLOAT_80619574 * (double)(float)((double)bp[4] - (double)bp[1]) + (double)bp[1]);
        headMid[2] = (float)((double)FLOAT_80619574 * (double)(float)((double)bp[5] - (double)bp[2]) + (double)bp[2]);
        mtx4_transform_point((float*)(*(int*)(*(int*)(actorRec + 4) + 0x10) + *(char*)((int)skel + *skel * -0xc0 + 0x1efd) * 0x40), headMid);
        mtx4_transform_point(palette, headMid);
    }
    snake_rope_fire_probe();
    snake_update_target_angle_b(actorRec);

    if (*(int*)(DAT_80612a58 + 0x3e88) != 0) {
        if (*(int*)(rec + 0x20) < 0xe) {
            *(float*)(DAT_80612a58 + 0x423c) =
                (float)((double)FLOAT_806108e8 * (double)(float)((double)FLOAT_80619580 /
                    (double)(float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630))
                    + (double)*(float*)(DAT_80612a58 + 0x423c));
        }
        snake2_rope_emit_b(actorRec);
        snake_rope_integrate_full();
        snake_orbiter_acquire_targets();
        snake_orbiter_update_all();
        snake_lift_audio_state(rec);

        int rider = *(int*)(DAT_80612a58 + 0x4270);
        if (rider != 0) {
            *(int*)(actorRec + 0x1cc) = *(int*)(DAT_80612a58 + 0x4274);
            int st = *(int*)(rec + 0x20);
            if ((FLOAT_80619568 < *(float*)(DAT_80612a58 + 0x4274)) || (st == 0xe) || (st == 0xf)) {
                *(float*)(rider + 100)  = boneMid1[0];
                *(float*)(rider + 0x68) = boneMid1[1];
                *(float*)(rider + 0x6c) = boneMid1[2];
                dir_to_angles((float*)(DAT_80612a58 + 0x4290), (float*)(rider + 0xc0), (float*)(rider + 0x70));
                if (*(float*)(rider + 0x70) < FLOAT_80619568) *(float*)(rider + 0x70) += FLOAT_806195ac;
                *(float*)(rider + 0xc0) = (float)((double)*(float*)(rider + 0xc0) * (double)FLOAT_80619570);
                *(float*)(rider + 0xc0) = (float)((double)*(float*)(rider + 0xc0) + (double)FLOAT_806195b4);
                if (*(float*)(rider + 0xc0) < FLOAT_80619568) *(float*)(rider + 0xc0) += FLOAT_806195ac;
                *(int*)(rider + 0xc4) = *(int*)(rider + 0x70);
            } else if (st != 0xe) {
                *(int*)(rec + 0x9d4) = st;
                *(int*)(rec + 0x20)  = 0xe;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        int brjp = aiactor_find_player_by_team(0x50524a42);
        *(int*)(DAT_80612a58 + 0x4bd4) = brjp;
        if ((brjp != 0) && (*(int*)(DAT_80612a58 + 0x4be0) == 0)) {
            *(int*)(DAT_80612a58 + 0x4be0) = 2;
            *(int*)(brjp + 0x34) = *(int*)(DAT_80611d78 + 0xdec);
        }
        if (1 < *(int*)(rec + 0x20)) {
            aiactor_turn_toward_leader();
            *(int*)(actorRec + 0x1a4) = *(int*)(DAT_80612a58 + 0x423c);
        }
        // Bone-aim rig: with no rider, identity aim; else build a two-target basis.
        int poseBone = *(int*)(*(int*)(actorRec + 4) + 0x10) + 0x280;
        if (*(int*)(DAT_80612a58 + 0x4270) == 0) {
            FUN_802ae344(poseBone, palette, 0, 0, 0.0, 0, 0);
        } else {
            int i = 0, dOff = 0, sOff = 0;
            do {
                float* tgt = *(float**)(DAT_80612a58 + sOff + 0x4240);
                if (tgt != 0) {
                    double dz = (double)(tgt[2] - boneMid1[2]);
                    double dx = (double)(float)((double)tgt[0] - (double)boneMid1[0]);
                    double dy = (double)(tgt[1] - boneMid1[1]);
                    double len = math_sqrt((double)(float)(dz*dz + (double)(float)(dx*dx + (double)(float)(dy*dy))));
                    double inv = (double)(float)((double)FLOAT_806195a4 / len);
                    *(float*)(DAT_80612a58 + dOff + 0x424c) = (float)(dx*inv);
                    *(float*)(DAT_80612a58 + dOff + 0x4250) = (float)(dy*inv);
                    *(float*)(DAT_80612a58 + dOff + 0x4254) = (float)(dz*inv);
                }
                i++; sOff += 4; dOff += 0xc;
            } while (i < 2);
            FUN_802ae344(poseBone, palette,
                         *(int*)(DAT_80612a58 + 0x4240), *(int*)(DAT_80612a58 + 0x4244),
                         (double)*(float*)(DAT_80612a58 + 0x424c), DAT_80612a58 + 0x4258, 1);
        }
    }

    switch (*(int*)(rec + 0x20)) {
    case 0: {
        // Spawn + emitter/attachment setup. Build the 13-zone lookup and the
        // aura/rider entities. Reconstructed structurally; exact spawn args match.
        float spawnA[3] = {*(float*)&DAT_803e0618, *(float*)&DAT_803e061c, *(float*)&DAT_803e0620};
        float spawnB[3] = {*(float*)&DAT_803e0624, *(float*)&DAT_803e0628, *(float*)&DAT_803e062c};
        int zoneIds[13];
        // (zone id list is copied from the +0x110 stack image in the original;
        // here we resolve each zone id straight into the +0x4bf0 table.)
        for (int i = 0; i < 13; i++) {
            *(int*)(DAT_80612a58 + i*4 + 0x4bf0) = soundpad_find_zone_by_id(zoneIds[i]);
        }
        *(float*)(actorRec + 100)  = FLOAT_806196ec;
        *(float*)(actorRec + 0x68) = FLOAT_806196f4;
        *(float*)(actorRec + 0x6c) = FLOAT_806196f0;
        *(unsigned int*)(actorRec + 0x28) |= 1;
        *(int*)(DAT_80612a58 + 0x3e88) = 1;
        *(float*)(actorRec + 0x1cc) = FLOAT_80619628;
        *(float*)(actorRec + 0x1d8) = FLOAT_80619628;
        *(int*)(DAT_80612a58 + 0x4bd0) = actorRec;
        *(int*)(DAT_80611d74 + 0xe8c) = actorRec;
        int slot = screen_effect_alloc_slot();
        *(int*)(actorRec + 0x268) = slot;
        screen_effect_init_slot(*(int*)(actorRec + 0x268), actorRec);
        entity_set_render_type(actorRec, (void*)&damage_filter_invincible);
        *(int*)(rec + 0x9dc) = -1;
        *(float*)(actorRec + 0xc4) = FLOAT_80619568;
        *(float*)(actorRec + 0x70) = FLOAT_80619568;
        int body = entity_spawn_by_type(0, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0x249, (float*)(actorRec + 100), spawnA, 0, 0);
        *(int*)(DAT_80612a58 + 0x4bcc) = body;
        *(unsigned int*)(body + 0x28) |= 0x70;
        *(float*)(DAT_80612a58 + 0x415c) = FLOAT_80619568;
        *(int*)(DAT_80612a58 + 0x4160) = 2;
        // Rope emitter A (0x3e94 def / 0x3f60 emitter -> 0x4154).
        particle_def_create((double)FLOAT_806195e8, (double)FLOAT_80619580, DAT_80612a58 + 0x3e94, 0, 1, 1, 0x28d, 0);
        particle_def_set_size_keyframes((double)FLOAT_80619580, (double)FLOAT_80619568, (double)FLOAT_80619568,
                                        (double)FLOAT_80619590, 0x64000000, 0, 0x28140a00);
        particle_def_set_gravity((double)FLOAT_80619670, 0x32140a00);
        particle_def_set_color_end((double)FLOAT_80619560, (double)FLOAT_8061956c, (double)FLOAT_8061963c, (double)FLOAT_80619674);
        particle_def_set_velocity_ranges((double)FLOAT_80619578, (double)FLOAT_80619578, (double)FLOAT_80619574,
                                         (double)FLOAT_806195a8, (double)FLOAT_80619580, (double)FLOAT_80619624);
        particle_def_set_drag((double)FLOAT_80619584);
        particle_emitter_create((double)FLOAT_80619580, (double)FLOAT_80619568, (double)FLOAT_80619574,
                                DAT_80612a58 + 0x3f60, DAT_80612a58 + 0x3e94, 0, 0x4000);
        particle_emitter_set_color_range((double)FLOAT_806195f4, (double)FLOAT_80619568, (double)FLOAT_806195c4, (double)FLOAT_80619568);
        particle_emitter_set_lifetime_range((double)FLOAT_806195f4, (double)FLOAT_80619568);
        particle_handle_clear((unsigned int*)(DAT_80612a58 + 0x4154));
        particle_emitter_spawn(DAT_80612a58 + 0x4154, DAT_80612a58 + 0x3f60, 0x1000, (float*)(actorRec + 100), spawnA);
        particle_emitter_set_drag((double)FLOAT_80619580);
        particle_emitter_set_size_range((double)FLOAT_80619580, (double)FLOAT_80619580);
        // 6 orbiter trail-emitter handle pairs + one-shot splashes.
        *(unsigned int*)(DAT_80612a58 + 0x4c24) =
            (unsigned int)FUN_8037daf0(0, (double)FLOAT_80619580, (double)FLOAT_806195c4, (double)FLOAT_80619568,
                                       (double)FLOAT_80619580, (double)FLOAT_80619624, 0, 0, -0x7fd70754, 6,
                                       0x1000, (float*)(actorRec + 100), spawnA, 0, body, 0.0f);
        {
            int i = 0, o = 0, tok = 0;
            do {
                particle_handle_clear((unsigned int*)(DAT_80612a58 + o + 0x4a68));
                particle_handle_clear((unsigned int*)(DAT_80612a58 + o + 0x4a6c));
                tok = *(int*)(DAT_80612a58 + 0x4c24) + i;
                worm_splash_emit_oneshot((double)FLOAT_8061963c, DAT_80612a58 + o + 0x4a68, (float*)(actorRec + 100), (char)tok);
                i++; o += 0x44;
            } while (i < 6);
            particle_handle_clear((unsigned int*)(DAT_80612a58 + 0x3e8c));
            unsigned long long h = particle_handle_clear((unsigned int*)(DAT_80612a58 + 0x3e90));
            short f = FUN_8037daf0(h, (double)FLOAT_80619580, (double)FLOAT_806195c4, (double)FLOAT_80619568,
                                   (double)FLOAT_80619580, (double)FLOAT_80619624, 0, 0, -0x7fd70a14, 1, tok,
                                   (float*)(actorRec + 100), spawnA, 0, body, 0.0f);
            worm_splash_emit_oneshot((double)FLOAT_80619578, DAT_80612a58 + 0x3e8c, (float*)(actorRec + 100), (char)f);
        }
        sprite_get_texture_id(0x300);
        FUN_802ad80c((double)FLOAT_806195f8, (double)FLOAT_80619648, (double)FLOAT_80619568, (double)FLOAT_80619578);
        int auraType = FUN_802adba4();
        *(unsigned int*)(actorRec + 0x28) &= 0xffffff9f;
        *(unsigned int*)(actorRec + 0x28) &= 0xfffffffe;
        *(int*)(actorRec + 0x1a0) = -1;
        int aura = entity_spawn_by_type((unsigned)auraType, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0x24b, boneMid1, spawnB, 0, 0);
        *(int*)(DAT_80612a58 + 0x4bdc) = aura;
        entity_register_for_update(actorRec, 0xc, -0x7fd6c68c);
        entity_setup_attachment(*(int*)(DAT_80612a58 + 0x4bdc), *(int**)(actorRec + 0x74), 1, -1, 1);
        *(float*)(*(int*)(DAT_80612a58 + 0x4bdc) + 0x140) = FLOAT_806195b4;
        *(float*)(*(int*)(DAT_80612a58 + 0x4bdc) + 0x144) = FLOAT_806195b4;
        *(int*)(DAT_80612a58 + 0x4234) = 1;
        *(float*)(DAT_80612a58 + 0x423c) = FLOAT_80619568;
        int* evtRec = worm_event_find_record_by_class(0x4e4d4d45);
        *(int**)(DAT_80612a58 + 0x4138) = evtRec;
        if (!worm_event_record_is_active(*(int*)(DAT_80612a58 + 0x4138))) {
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619768);
        }
        if (*(int*)(rec + 0x20) != 1) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20)  = 1;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        break;
    }
    case 1: {
        float riderSpawn[3] = {*(float*)&DAT_803e0664, *(float*)&DAT_803e0668, *(float*)&DAT_803e066c};
        double phase = anim_primary_clip_phase(*(int*)(actorRec + 4));
        *(unsigned int*)(actorRec + 0x28) |= 1;
        if ((*(int*)(DAT_80612a58 + 0x4270) == 0) && ((double)FLOAT_8061976c < phase)) {
            int r = entity_spawn_by_type(0, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0x24a, boneMid1, riderSpawn, 0, 0);
            *(int*)(DAT_80612a58 + 0x4270) = r;
            *(char*)(r + 0x32) = 2;
            *(unsigned int*)(r + 0x28) |= 0x10;
            *(int*)(r + 0x34) = *(int*)(DAT_80611d78 + 0xdec);
            entity_set_render_type(r, (void*)&LAB_802937e0);
        }
        if (worm_event_record_is_active(*(int*)(DAT_80612a58 + 0x4138))) {
            float platSpawn[3] = {*(float*)&DAT_803e0670, *(float*)&DAT_803e0674, *(float*)&DAT_803e0678};
            float platRot[3]   = {*(float*)&DAT_803e067c, *(float*)&DAT_803e0680, *(float*)&DAT_803e0684};
            int plat = entity_spawn_by_type(0, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0x246, platSpawn, platRot, 0, 0);
            *(int*)(DAT_80612a58 + 0x4bd8) = plat;
            *(char*)(plat + 0x32) = 2;
            entity_register_for_update(plat, 2, -0x7fd6c928);
            entity_register_for_update(actorRec, 2, -0x7fd6ee7c);
            entity_register_for_update(*(int*)(DAT_80612a58 + 0x4bcc), 2, -0x7fd6e960);
            entity_register_for_update(*(int*)(DAT_80612a58 + 0x4bcc), 4, -0x7fd6c5fc);
            *(int*)(DAT_80612a58 + 0x4be4) = 1;
            *(float*)(DAT_80612a58 + 0x423c) = FLOAT_80619568;
            *(int*)(DAT_80612a58 + 0x4160) = 0;
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            if (*(int*)(rec + 0x20) != 6) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 6;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
            if (*(int*)(DAT_80612a58 + 0x4270) == 0) {
                float riderSpawn2[3] = {*(float*)&DAT_803e0688, *(float*)&DAT_803e068c, *(float*)&DAT_803e0690};
                int r = entity_spawn_by_type(0, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0x24a, riderSpawn2, riderSpawn, 0, 0);
                *(int*)(DAT_80612a58 + 0x4270) = r;
                *(char*)(r + 0x32) = 2;
                *(unsigned int*)(r + 0x28) |= 0x10;
                *(int*)(r + 0x34) = *(int*)(DAT_80611d78 + 0xdec);
                entity_set_render_type(r, (void*)&LAB_802937e0);
            }
            entity_register_for_update(*(int*)(DAT_80612a58 + 0x4270), 3, -0x7fd6c798);
            if (*(int*)(DAT_80612a58 + 0x4bd4) != 0) {
                int b = *(int*)(DAT_80612a58 + 0x4bd4);
                *(int*)(b + 0x34) = *(int*)(DAT_80611d78 + 0xdec);
                *(float*)(b + 100)  = platSpawn[0];
                *(float*)(b + 0x68) = FLOAT_806196c0 + platSpawn[1];
                *(float*)(b + 0x6c) = FLOAT_80619770 + platSpawn[2];
            }
            *(int*)(actorRec + 0x1a0) = 1;
        }
        break;
    }
    case 2: {
        if (anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) {
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            if (*(int*)(rec + 0x20) != 3) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 3;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 3: {
        if (anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) {
            anim_play_scaled((double)FLOAT_806195fc, (double)FLOAT_80619580);
            if (*(int*)(rec + 0x20) != 2) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 2;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 4: {
        float c = *(float*)(rec + 0xb2c);
        if (FLOAT_80619684 <= c) { if (FLOAT_8061968c < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
        else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        if (anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) {
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            if (*(int*)(DAT_80612a58 + 0x42a8) != *(int*)(rec + 0x20)) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = *(int*)(DAT_80612a58 + 0x42a8);
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
            if (*(int*)(DAT_80612a58 + 0x42a8) == 6) *(int*)(DAT_80612a58 + 0x4160) = 0;
        }
        break;
    }
    case 5: {
        float c = *(float*)(rec + 0xb2c);
        if (FLOAT_806196c4 <= c) { if (FLOAT_80619560 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
        else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        if (anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) {
            anim_play_scaled((double)FLOAT_806195fc, (double)FLOAT_80619580);
            if (*(int*)(rec + 0x20) != 7) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 7;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 6: {
        float c = *(float*)(rec + 0xb2c);
        if (FLOAT_806196c8 <= c) { if (FLOAT_806196cc < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
        else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        if (anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) {
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            if (*(int*)(rec + 0x20) != 4) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 4;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
            *(int*)(DAT_80612a58 + 0x42a8) = 10;
            *(int*)(DAT_80612a58 + 0x42ac) = 0x23d;
        }
        break;
    }
    case 7: {
        double phase = anim_primary_clip_phase(*(int*)(actorRec + 4));
        float c = *(float*)(rec + 0xb2c);
        if (phase <= (double)FLOAT_806195a8) {
            if (FLOAT_806196c4 <= c) { if (FLOAT_80619560 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
            else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        } else {
            if (FLOAT_806196d0 <= c) { if (FLOAT_806196d4 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
            else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        }
        float dir[3];
        angles_to_dir((double)FLOAT_80619568, (double)*(float*)(actorRec + 0xc4), dir);
        if (phase < (double)FLOAT_8061959c) {
            double pp = phase;
            if ((double)FLOAT_80619564 < pp) {
                pp = -(double)(float)((double)FLOAT_80619574 * (double)(float)((double)FLOAT_80619564 - pp) - (double)FLOAT_80619564);
            }
            double m = math_sqrt(pp);
            dir[0] = dir[0] * (float)((double)FLOAT_80619624 * m);
            dir[2] = dir[2] * (float)((double)FLOAT_80619624 * m);
            *(float*)(actorRec + 100)  = FLOAT_806196ec - dir[0];
            *(float*)(actorRec + 0x68) = FLOAT_806196f4;
            *(float*)(actorRec + 0x6c) = FLOAT_806196f0 - dir[2];
        }
        if (anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) {
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            if (*(int*)(rec + 0x20) != 0xb) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 0xb;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 8: {
        *(float*)(actorRec + 0xc4) += FLOAT_80619564;
        *(float*)(actorRec + 0x70) += FLOAT_80619564;
        if ((anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) &&
            (FLOAT_80619568 == *(float*)(DAT_80612a58 + 0x4bc8))) {
            *(float*)(DAT_80612a58 + 0x4bc8) = FLOAT_806195ac;
            if (*(int*)(rec + 0x20) != 3) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 3;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 10: {
        float c = *(float*)(rec + 0xb2c);
        if (FLOAT_806196c4 <= c) { if (FLOAT_80619560 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
        else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        snake_orbiter_spawn_ring();
        anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
        if (*(int*)(rec + 0x20) != 5) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20)  = 5;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        break;
    }
    case 0xb: {
        double amp = (double)(FLOAT_80619590 * (FLOAT_80619580 - ABS(FLOAT_80619780 * *(float*)(rec + 0xae0))));
        double phase = anim_primary_clip_phase(*(int*)(actorRec + 4));
        float c = *(float*)(rec + 0xb2c);
        if (FLOAT_806196c4 <= c) { if (FLOAT_80619560 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
        else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        if ((double)FLOAT_80619758 < phase) {
            double half = (double)FLOAT_80619574;
            if (half <= phase) {
                if (phase < (double)FLOAT_80619608) {
                    double p2 = (double)(float)((double)FLOAT_80619648 - phase);
                    amp = (double)(float)(amp * (double)(float)(p2 * p2));
                    player_apply_camera_shake((double)(float)(half * amp), amp, (double)(float)((double)FLOAT_806195a0 * amp), DAT_80611d78);
                    phase = p2;
                }
            } else {
                double p2 = (double)(float)(phase + half);
                amp = (double)(float)(amp * (double)(float)(p2 * p2));
                player_apply_camera_shake((double)(float)(half * amp), amp, (double)(float)((double)FLOAT_806195a0 * amp), DAT_80611d78);
                phase = p2;
            }
            if ((double)FLOAT_8061959c < phase) {
                float xc = FLOAT_806196ec, x = *(float*)(actorRec + 100);
                if (xc <= x) { if (xc < x) { *(float*)(actorRec + 100) = x - FLOAT_80619700; if (*(float*)(actorRec + 100) < xc) *(float*)(actorRec + 100) = xc; } }
                else { *(float*)(actorRec + 100) = x + FLOAT_80619700; if (xc < *(float*)(actorRec + 100)) *(float*)(actorRec + 100) = xc; }
                float zc = FLOAT_806196f0, z = *(float*)(actorRec + 0x6c);
                if (zc <= z) { if (zc < z) { *(float*)(actorRec + 0x6c) = z - FLOAT_80619700; if (*(float*)(actorRec + 0x6c) < zc) *(float*)(actorRec + 0x6c) = zc; } }
                else { *(float*)(actorRec + 0x6c) = z + FLOAT_80619700; if (zc < *(float*)(actorRec + 0x6c)) *(float*)(actorRec + 0x6c) = zc; }
            }
        }
        if (anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) {
            if (*(int*)(rec + 0x20) != 4) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 4;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
            *(int*)(DAT_80612a58 + 0x42a8) = 6;
            *(int*)(DAT_80612a58 + 0x42ac) = 0x242;
            *(float*)(DAT_80612a58 + 0x4238) = FLOAT_80619568;
            *(float*)(actorRec + 100)  = FLOAT_806196ec;
            *(float*)(actorRec + 0x68) = FLOAT_806196f4;
            *(float*)(actorRec + 0x6c) = FLOAT_806196f0;
        }
        break;
    }
    case 0xc: {
        double phase = anim_primary_clip_phase(*(int*)(actorRec + 4));
        float c = *(float*)(rec + 0xb2c);
        if (FLOAT_806196c4 <= c) { if (FLOAT_80619560 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
        else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        if ((*(int*)(DAT_80612a58 + 0x4bd4) != 0) && (*(int*)(DAT_80612a58 + 0x4be0) == 2) &&
            ((double)FLOAT_80619774 < phase) && (anim_is_clip_active(actorRec, 0x23f) != 0)) {
            int b = *(int*)(DAT_80612a58 + 0x4bd4);
            *(float*)(b + 100)  = FLOAT_80619730;
            *(float*)(b + 0x68) = FLOAT_80619778;
            *(float*)(b + 0x6c) = FLOAT_80619738;
            *(int*)(DAT_80612a58 + 0x4be0) = 1;
            *(int*)(DAT_80612a58 + 0x4be4) = 7;
        }
        if (*(int*)(DAT_80612a58 + 0x4be4) == 7) {
            int plat = *(int*)(DAT_80612a58 + 0x4bd8);
            *(float*)(plat + 100)  = boneMid2[0];
            *(float*)(plat + 0x68) = boneMid2[1];
            *(float*)(plat + 0x6c) = boneMid2[2];
        }
        if (anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) {
            if (anim_is_clip_active(actorRec, 0x23f) == 0) {
                anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                if (DAT_80612610 == 1) {
                    unsigned int* wp = entity_get_world_position(actorRec);
                    int p[9];
                    int pitch = (int)(FLOAT_8061977c * (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630));
                    sound_init_play_params((int)p);
                    p[0] = 0x56b;
                    *(float*)&p[3] = FLOAT_80619580;   // local_12c
                    *(float*)&p[6] = FLOAT_80619618;   // local_128
                    *(float*)&p[8] = FLOAT_806195d4;   // local_114
                    p[2] = (int)wp[0]; p[3] = (int)wp[1]; p[4] = (int)wp[2];
                    p[5] = pitch;
                    sound_play_request(p);
                }
            } else if (*(int*)(rec + 0x20) != 0x10) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 0x10;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 0xd: {
        if (anim_active_slot_userflags(*(int*)(actorRec + 4)) != 0) {
            anim_play_scaled((double)FLOAT_806195fc, (double)FLOAT_80619580);
        }
        break;
    }
    case 0xe: {
        if (anim_is_clip_active(actorRec, 0x241) == 0) {
            player_ai_explosion_spawn(0x15, 0x17, (float*)(*(int*)(DAT_80612a58 + 0x4270) + 100), (float*)0, 0, 0, 1);
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            unsigned int* wp = entity_get_world_position(actorRec);
            int p[8];
            sound_init_play_params((int)p);
            p[0] = 0x568;
            *(float*)&p[6] = FLOAT_80619578;   // local_158
            *(float*)&p[7] = FLOAT_806195bc;   // local_144
            p[2] = (int)wp[0]; p[3] = (int)wp[1]; p[4] = (int)wp[2];
            sound_play_request(p);
            entity_destroy_with_event(*(unsigned int**)(DAT_80612a58 + 0x4270));
            *(int*)(DAT_80612a58 + 0x4270) = 0;
        }
        if (anim_primary_clip_userflags(actorRec, 0x241) != 0) {
            int i = 0, o = 0;
            do {
                if (0 < *(int*)(DAT_80612a58 + o + 0x4a54)) {
                    player_ai_explosion_spawn(0x1b, 0x17, (float*)(*(int*)(DAT_80612a58 + o + 0x4a30) + 100), (float*)0, 0, 0, 1);
                    *(int*)(DAT_80612a58 + o + 0x4a54) = 0;
                }
                aiactor_destroy_instance(0, 0.0, 0.0, 0.0, 0, 0, 0, 0, *(int*)(DAT_80612a58 + o + 0x4a30));
                i++;
                *(int*)(DAT_80612a58 + o + 0x4a30) = 0;
                o += 0x44;
            } while (i < 6);
            *(int*)(DAT_80611d74 + 0xe8c) = 0;
            statlvl_class_complete((double)FLOAT_80619568, 0.0, 0.0, 0.0, 0, 0, 0, 0);
            *(int*)(DAT_80612a58 + 0x4be4) = 5;
            if (*(int*)(rec + 0x20) != 0xf) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 0xf;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 0xf: {
        if (*(int*)(DAT_80612a58 + 0x4be4) == 6) {
            const int off[3] = {0x4bdc, 0x4bd8, 0x4bcc};
            for (int i = 0; i < 3; i++) {
                if (*(unsigned int**)(DAT_80612a58 + off[i]) != 0)
                    entity_destroy_with_event(*(unsigned int**)(DAT_80612a58 + off[i]));
                *(int*)(DAT_80612a58 + off[i]) = 0;
            }
            *(int*)(DAT_80612a58 + 0x3e88) = 0;
            aiactor_destroy_instance(0, 0.0, 0.0, 0.0, 0, 0, 0, 0, actorRec);
        }
        break;
    }
    case 0x10: {
        int i = 0, o = 0;
        do {
            if (*(int*)(DAT_80612a58 + o + 0x4a54) == 0) {
                aiactor_destroy_instance(0, 0.0, 0.0, 0.0, 0, 0, 0, 0, *(int*)(DAT_80612a58 + o + 0x4a30));
                *(int*)(DAT_80612a58 + o + 0x4a30) = 0;
            }
            i++; o += 0x44;
        } while (i < 6);
        *(int*)(DAT_80611d74 + 0xe8c) = 0;
        const int off[3] = {0x4bdc, 0x4bd8, 0x4bcc};
        for (int j = 0; j < 3; j++) {
            if (*(unsigned int**)(DAT_80612a58 + off[j]) != 0)
                entity_destroy_with_event(*(unsigned int**)(DAT_80612a58 + off[j]));
            *(int*)(DAT_80612a58 + off[j]) = 0;
        }
        unsigned long long h = ghz_reactor_match_end(0, 0.0, 0.0, 0.0, 0, 0, 0, 0);
        aiactor_destroy_instance(h, 0.0, 0.0, 0.0, 0, 0, 0, 0, actorRec);
        break;
    }
    }
    FUN_80382730();
}

// ===========================================================================
// 0x802954a8  snake2_event_handler
// Alternate-boss event dispatch. Event 5 destroys all sub-entities. Events in
// [5,6] clear the entity's active bit. Event 2 and handled events return -1.
// ===========================================================================
int snake2_event_handler(int entity, int event) {
    if (event == 5) {
        const int off[4] = {0x4bdc, 0x4bd8, 0x4bcc, 0x4270};
        for (int i = 0; i < 4; i++) {
            if (*(unsigned int**)(DAT_80612a58 + off[i]) != 0)
                entity_destroy_with_event(*(unsigned int**)(DAT_80612a58 + off[i]));
        }
        for (int i = 0; i < 4; i++) *(int*)(DAT_80612a58 + off[i]) = 0;
    } else {
        if (4 < event) {
            if (6 < event) return 0;
            *(unsigned int*)(entity + 0x28) &= 0xfffffffe;
            return 0;
        }
        if (event == 2) return -1;
        if (1 < event) return 0;
        if (event < 1) return 0;
    }
    return -1;
}

// Shared rsqrt-normalize + rope-emit helper matching the two emit variants: takes
// the averaged bone direction, normalizes it and pushes 4 jittered rope points.
static inline float sr_rsqrt_scale(float sumSq, float lenScale) {
    float rs = 1.0f / (float)math_sqrt((double)sumSq);
    float t = FLOAT_80619574 * sumSq;
    rs = rs * -(t * rs * rs - FLOAT_80619648);
    return lenScale * rs * -(t * rs * rs - FLOAT_80619648);
}

// ===========================================================================
// 0x80295588  snake2_rope_emit_a
// Emit rope points from the midpoint of two aim bones (matrices +0x280/+0x2c0).
// When the bones' dot product is high, or the boss isn't in an emitting state,
// the emitters are drag-idled and no point is pushed. Otherwise the averaged
// bone direction is normalized and 4 jittered points are pushed into the rope.
// ===========================================================================
void snake2_rope_emit_a(int actor) {
    int rec = *(int*)(actor + 0x7c);
    float b0[3] = {*(float*)&DAT_803e0694, *(float*)&DAT_803e0698, *(float*)&DAT_803e069c};
    float b1[3] = {*(float*)&DAT_803e06a0, *(float*)&DAT_803e06a4, *(float*)&DAT_803e06a8};
    int skelBase = *(int*)(*(int*)(actor + 4) + 0x10);
    mtx4_transform_vector((float*)(skelBase + 0x280), b0);
    mtx4_transform_vector((float*)(skelBase + 0x2c0), b1);
    int st = *(int*)(rec + 0x20);
    if ((FLOAT_80619574 <= b0[2]*b1[2] + b0[0]*b1[0] + b0[1]*b1[1]) ||
        ((st != 3) && (st != 4) && (st != 5))) {
        int hA = *(int*)(DAT_80612a58 + 0x3e8c); particle_cached_set_drag((double)FLOAT_80619568, &hA);
        int hB = *(int*)(DAT_80612a58 + 0x3e90); particle_cached_set_drag((double)FLOAT_80619568, &hB);
        snake_rope_ring_push((void*)0, (void*)0, 0, 0);
    } else {
        unsigned int r1 = rng_counter_tick();
        double j0 = (double)((float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80619610) / FLOAT_806195e4);
        unsigned int r2 = rng_counter_tick();
        double j1 = (double)((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80619610) / FLOAT_806195e4);
        int hA = *(int*)(DAT_80612a58 + 0x3e8c);
        particle_cached_set_drag((double)((FLOAT_80619784 *
            (float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(DAT_80612a58 + 0x3e84) ^ 0x80000000)) - DOUBLE_80619630)) / FLOAT_80619788), &hA);
        int hB = *(int*)(DAT_80612a58 + 0x3e90);
        particle_cached_set_drag((double)((FLOAT_80619784 *
            (float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(DAT_80612a58 + 0x3e84) ^ 0x80000000)) - DOUBLE_80619630)) / FLOAT_80619788), &hB);
        snake_rope_player_touch_damage();
        float dir[3];
        dir[0] = (b0[0] + b1[0]) * FLOAT_80619574;
        dir[1] = (b0[1] + b1[1]) * FLOAT_80619574;
        dir[2] = (b0[2] + b1[2]) * FLOAT_80619574;
        float scale = sr_rsqrt_scale(dir[2]*dir[2] + dir[0]*dir[0] + dir[1]*dir[1], FLOAT_806195c0);
        dir[0] *= scale; dir[1] *= scale; dir[2] *= scale;
        mtx4_transform_vector((float*)(actor + 0x274), dir);
        int sb = *(int*)(*(int*)(actor + 4) + 0x10);
        float pos[3] = {*(float*)(sb + 0x2b0), *(float*)(sb + 0x2b4), *(float*)(sb + 0x2b8)};
        mtx4_transform_vector((float*)(actor + 0x274), pos);
        pos[0] += *(float*)(actor + 100);
        pos[1] += *(float*)(actor + 0x68);
        pos[2] += *(float*)(actor + 0x6c);
        snake_rope_ring_push(pos, dir, 1, 0);
        pos[0] = (float)((double)pos[0] + j0); pos[1] = (float)((double)pos[1] + j1); pos[2] = (float)((double)pos[2] + j0);
        snake_rope_ring_push(pos, dir, 1, 0);
        pos[0] = (float)((double)pos[0] - j1); pos[1] = (float)((double)pos[1] - j0); pos[2] = (float)((double)pos[2] - j1);
        snake_rope_ring_push(pos, dir, 1, 0);
        pos[0] = (float)((double)pos[0] + j0); pos[1] = (float)((double)pos[1] + j0); pos[2] = (float)((double)pos[2] + j0);
        snake_rope_ring_push(pos, dir, 1, 0);
    }
}

// ===========================================================================
// 0x80295980  snake2_rope_emit_b
// Rope emit variant B (used by the reactor boss). Adds a state-7/state-0xc gate,
// a rider-relative billboard emit path (when the 'BRJP' rider is engaged), and
// nearest-node tagging on the 4 pushed points.
// ===========================================================================
void snake2_rope_emit_b(int actor) {
    int rec = *(int*)(actor + 0x7c);
    float b0[3] = {*(float*)&DAT_803e06ac, *(float*)&DAT_803e06b0, *(float*)&DAT_803e06b4};
    float b1[3] = {*(float*)&DAT_803e06b8, *(float*)&DAT_803e06bc, *(float*)&DAT_803e06c0};
    int skelBase = *(int*)(*(int*)(actor + 4) + 0x10);
    mtx4_transform_vector((float*)(skelBase + 0x280), b0);
    mtx4_transform_vector((float*)(skelBase + 0x2c0), b1);
    int st = *(int*)(rec + 0x20);
    if ((st != 7) || (FLOAT_80619574 <= b0[2]*b1[2] + b0[0]*b1[0] + b0[1]*b1[1])) {
        if ((*(int*)(DAT_80612a58 + 0x4bd4) == 0) || (st != 0xc) || (*(int*)(DAT_80612a58 + 0x4be0) != 2)) {
            int hA = *(int*)(DAT_80612a58 + 0x3e8c); particle_cached_set_drag((double)FLOAT_80619568, &hA);
            int hB = *(int*)(DAT_80612a58 + 0x3e90); particle_cached_set_drag((double)FLOAT_80619568, &hB);
            snake_rope_ring_push((void*)0, (void*)0, 0, 0);
        } else {
            // Rider engaged: emit a billboard puff at the rider's jittered position.
            int hA0 = *(int*)(DAT_80612a58 + 0x3e8c); particle_cached_set_size_range((double)FLOAT_80619580, (double)FLOAT_80619580, &hA0);
            int hB0 = *(int*)(DAT_80612a58 + 0x3e90); particle_cached_set_size_range((double)FLOAT_80619580, (double)FLOAT_80619580, &hB0);
            int hA = *(int*)(DAT_80612a58 + 0x3e8c);
            particle_cached_set_drag((double)((FLOAT_8061978c *
                (float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(DAT_80612a58 + 0x3e84) ^ 0x80000000)) - DOUBLE_80619630)) / FLOAT_80619788), &hA);
            int hB = *(int*)(DAT_80612a58 + 0x3e90);
            particle_cached_set_drag((double)((FLOAT_8061978c *
                (float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(DAT_80612a58 + 0x3e84) ^ 0x80000000)) - DOUBLE_80619630)) / FLOAT_80619788), &hB);
            unsigned int r1 = rng_counter_tick();
            float dir[3];
            dir[0] = FLOAT_806195a8 * ((float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80619610) / FLOAT_806195e4) - FLOAT_806195a4;
            unsigned int r2 = rng_counter_tick();
            dir[1] = FLOAT_8061967c * ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80619610) / FLOAT_806195e4);
            unsigned int r3 = rng_counter_tick();
            dir[2] = FLOAT_806195a8 * ((float)((double)CONCAT44(0x43300000, r3) - DOUBLE_80619610) / FLOAT_806195e4) - FLOAT_806195a4;
            int b = *(int*)(DAT_80612a58 + 0x4bd4);
            unsigned int r4 = rng_counter_tick();
            float pos[3];
            pos[0] = *(float*)(b + 100) + (FLOAT_80619618 * ((float)((double)CONCAT44(0x43300000, r4) - DOUBLE_80619610) / FLOAT_806195e4) - FLOAT_8061972c);
            pos[1] = *(float*)(b + 0x68) - FLOAT_806195f8;
            unsigned int r5 = rng_counter_tick();
            pos[2] = *(float*)(b + 0x6c) + (FLOAT_80619618 * ((float)((double)CONCAT44(0x43300000, r5) - DOUBLE_80619610) / FLOAT_806195e4) - FLOAT_8061972c);
            snake_rope_ring_push(pos, dir, 1, 0);
        }
    } else {
        unsigned int r1 = rng_counter_tick();
        double j0 = (double)(FLOAT_80619574 * ((float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80619610) / FLOAT_806195e4));
        unsigned int r2 = rng_counter_tick();
        double j1 = (double)(FLOAT_80619574 * ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80619610) / FLOAT_806195e4));
        int hA = *(int*)(DAT_80612a58 + 0x3e8c);
        particle_cached_set_drag((double)((FLOAT_806195c0 *
            (float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(DAT_80612a58 + 0x3e84) ^ 0x80000000)) - DOUBLE_80619630)) / FLOAT_80619788), &hA);
        int hB = *(int*)(DAT_80612a58 + 0x3e90);
        particle_cached_set_drag((double)((FLOAT_806195c0 *
            (float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(DAT_80612a58 + 0x3e84) ^ 0x80000000)) - DOUBLE_80619630)) / FLOAT_80619788), &hB);
        snake_rope_player_touch_damage();
        float dir[3];
        dir[0] = (b0[0] + b1[0]) * FLOAT_80619574;
        dir[1] = (b0[1] + b1[1]) * FLOAT_80619574;
        dir[2] = (b0[2] + b1[2]) * FLOAT_80619574;
        float scale = sr_rsqrt_scale(dir[2]*dir[2] + dir[0]*dir[0] + dir[1]*dir[1], FLOAT_80619580);
        dir[0] *= scale; dir[1] *= scale; dir[2] *= scale;
        mtx4_transform_vector((float*)(actor + 0x274), dir);
        int sb = *(int*)(*(int*)(actor + 4) + 0x10);
        float pos[3] = {*(float*)(sb + 0x2b0), *(float*)(sb + 0x2b4), *(float*)(sb + 0x2b8)};
        mtx4_transform_vector((float*)(actor + 0x274), pos);
        float scale2 = sr_rsqrt_scale(dir[2]*dir[2] + dir[0]*dir[0] + dir[1]*dir[1], FLOAT_80619580);
        pos[0] += *(float*)(actor + 100);
        pos[1] += *(float*)(actor + 0x68);
        pos[2] += *(float*)(actor + 0x6c);
        dir[0] *= scale2; dir[1] *= scale2; dir[2] *= scale2;
        int node = snake_rope_nearest_node(pos);
        snake_rope_ring_push(pos, dir, 1, (unsigned)node);
        pos[0] = (float)((double)pos[0] + j1); pos[1] = (float)((double)pos[1] + j1); pos[2] = (float)((double)pos[2] + j0);
        snake_rope_ring_push(pos, dir, 1, (unsigned)node);
        pos[0] = (float)((double)pos[0] - j0); pos[1] = (float)((double)pos[1] - j0); pos[2] = (float)((double)pos[2] - j1);
        snake_rope_ring_push(pos, dir, 1, (unsigned)node);
        pos[0] = (float)((double)pos[0] + j1); pos[1] = (float)((double)pos[1] + j1); pos[2] = (float)((double)pos[2] - j1);
        snake_rope_ring_push(pos, dir, 1, (unsigned)node);
    }
}

// ===========================================================================
// 0x80296024  snake_tick_dispatch
// Per-frame tick: route to the attack-boss (mode 1) or reactor-boss FSM.
// ===========================================================================
void snake_tick_dispatch(void) {
    if (DAT_80612614 == 1) {
        snake_boss_behavior_tick();
    } else {
        snake2_boss_behavior_tick();
    }
}

// ===========================================================================
// 0x80296058  snake_event_dispatch
// Event: route to the attack-boss bite handler (mode 1) or reactor event handler.
// ===========================================================================
void snake_event_dispatch(int a, int b, int c) {
    if (DAT_80612614 == 1) {
        snake_event_bite_react(a, b, c);
    } else {
        snake2_event_handler(a, b);
    }
}
