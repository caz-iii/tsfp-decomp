// worm_web_net_driver.cpp
// Worm mission (CDH1) "web-net" BOSS / MISSION DRIVER. This is the entity-level
// state machine that sits on top of the strand primitives in worm_web_net.cpp.
// It drives the WRTH boss entity (handle 0x57525448): spawn/construct, the master
// per-frame tick, the multi-phase animation timeline, projectile firing, capture
// damage, and the arm-bone animation.
//
// Global net life-state selector (DAT_805cadb8) phases:
//   0 intro-idle, 1 rise (sub-states via DAT_805cae1c 1..8), 2/3 sweep-attack,
//   4 begin/charge, 5 pre-explode, 6 grow (boss variant), 7 web-net engaged,
//   8 defeated-settle, 9 score-complete/death, 10 done.
// Animation timeline: DAT_805cae1c selects a timeline row in the pointer table
//   PTR_DAT_8046d634 / count/loop table &DAT_8046d630 (stride 0xc); DAT_805cae24
//   is the keyframe cursor (keyframes stride 0x18); DAT_805cae28 is the phase timer.
// Event table &DAT_8046d6fc (stride 0x18): per-phase scripted events (type, time,
//   handle, arg) applied in webnet_master_tick.
//
// Entity handles (FourCC): WRTH boss=0x57525448, LPTR=0x4c505452, HLC2=0x484c4332,
//   BLK1=0x424c4b31, BLK2=0x424c4b32.
//
// Address range: 0x80247bc4 - 0x802496e4
//
// Self-contained translation unit (no shared-header include): all raw symbols and
// helper prototypes are declared file-locally with extern "C" linkage matching the
// ROM's unmangled symbols. Ghidra-style intrinsics (SQRT) are file-local inlines.

static inline float SQRT(float x) { return __builtin_sqrtf(x); }
static inline float ABS(float x)  { return x < 0.0f ? -x : x; }

extern "C" {

// ---- named cross-module helpers (file-local, ROM signatures per call site) ----
int    entity_handle_resolve(int handle);
void   entity_register_for_update(int entity, int slot, int callback);
void   entity_set_render_type(int entity, void *callback);
void   entity_destroy_with_event(void *entity);
void   mtx4_set_translation(double x, double y, double z);
void   mtx4_set_rotation_zyx(double rz, double ry, double rx, float *mtx);
void   mtx4_multiply(float *a, float *b, float *out);
void   mtx4_transform_point(float *mtx, float *pt);
int    rng_counter_tick(void);
void   sound_init_play_params(int params);
int    sound_play_request(int *params);
void   sound_set_voice_volume_scale(int scale);
void   sound_stop_handle(void);
void   sound_play_persistent(int id, int a2, int a3);
void   sound_play_or_resume_3d(double a1, double a2, double a3, int id, int a5, int a6);
void   sound_update_3d_volume(double vol, int handle, void *pos);
int    sound_source_get_nth_active(int list, int n);
void   particle_emitter_spawn(int *handleOut, int *desc, int a3, float *pos, float *vel);
void   particle_handle_destroy(int *handle);
void   player_add_damage_indicator(double a1, double a2, int *player, float *pos, int a5);
void   player_ai_explosion_spawn(int a1, int a2, float *pos, float *a4, int a5, int a6, int a7);
void   player_ai_projectile_jitter_init(float *state);
void   player_ai_projectile_jitter_tick(double dt, int *state);
void   player_ai_projectile_compute_jittered_dir(double a1, double a2, float *state, float *out);
int    player_ai_projectile_spawn_simple(double a1, double a2, int type, float *pos, int *dir,
                                         int owner, int *a7, int a8, int a9, int a10, int projId,
                                         int a12);
void   player_ai_spawn_trail_particle(int a1, int a2, int a3, float *pos, float *dir, int id);
void   statlvl_class_complete(double a1, double a2, double a3, double a4, double a5, double a6,
                              double a7, double a8);
int    statlvl_class_is_state5(int a1);
void   anim_set_blend_weight(double w, int node);
int    switch_slot_reset(int ctx);
void   damage_dispatch_to_entity(int ctx, double a2, double a3, double a4, double a5, double a6,
                                 double a7, double a8, int *params);
void   debris_seed_fill_timers(void);
double webnet_phase_param_a(void);   // defined in worm_web_net.cpp (batch_719)
double webnet_phase_param_b(void);   // defined in worm_web_net.cpp (batch_719)
int    webnet_phase_status(void);    // defined in worm_web_net.cpp (batch_719)

// ---- render/anim list context enter (return WRTH ctx/entity ptr) + leave ----
extern int  FUN_803826dc(void);   // master-tick context enter
extern void FUN_80382728(void);   // master-tick context leave
extern int  FUN_803826e4(void);   // bone-anim context enter
extern void FUN_80382730(void);   // bone-anim context leave
extern int  __save_gpr_28(void);  // spawn/tick prologue: returns entity ctx ptr
extern void __restore_gpr_28(void);
extern double FUN_80390324(double x);  // smoothstep-ish easing

// ---- internal web-net helpers defined in later batches (file-local externs) ----
extern void webnet_arm_compute_endpoints(int entity, int *frontOut, float *sideOut);
extern void FUN_8024b9a8(void);              // projectile spawn tick (0x8024b9a8)
extern void FUN_8024ce94(void);              // arm-hit fx (0x8024ce94)
extern int  FUN_8024cb4c(int ctx);           // charge helper (0x8024cb4c)
extern void FUN_8024c418(double a1, double a2, int id, int *pos, int a5); // debris burst
extern void FUN_8024be98(int a1, double a2, double a3, double a4, double a5, double a6,
                         double a7, double a8);   // phase-9 death update (0x8024be98)
extern void FUN_8024ccb8(void);              // per-frame sub-update (0x8024ccb8)
extern void FUN_8024b53c(void);              // keyframe event sub-update (0x8024b53c)
extern void FUN_8024ade0(void);              // sub-update (0x8024ade0)
extern void FUN_80249c54(void);              // sub-update (0x80249c54)
extern void FUN_80249efc(void);              // sub-update (0x80249efc)

// ---- state globals ----
extern int   DAT_805cadb8;   // net life-state / phase (0..10)
extern int   DAT_805cae1c;   // timeline row index
extern int   DAT_805cae24;   // keyframe cursor
extern float DAT_805cae28;   // phase timer
extern int   DAT_805cae30;   // boss-variant flag
extern int   DAT_805cae34;   // one-shot latch (defeat sound-source)
extern int   DAT_805cae38;   // web-net engaged sub-phase (0..3)
extern float DAT_805cae04;   // boss vertical offset
extern float DAT_805cae00;
extern float DAT_805cae08;   // secondary spin phase
extern float DAT_805cae0c;   // arm spin phase
extern float DAT_805cae10;   // fire cadence accumulator
extern float DAT_805cae14;   // per-shot timer
extern float DAT_805cae18;   // idle-between-bursts timer
extern float DAT_805cae20;   // keyframe dwell timer
extern float DAT_805cadfc;   // burst rate
extern float DAT_805cadf0;   // spawn jitter x
extern float DAT_805cadf4;   // spawn jitter y
extern float DAT_805cadf8;   // spawn jitter z
extern float DAT_805cadbc;   // homing target xyz
extern float DAT_805cadc0;
extern float DAT_805cadc4;
extern float DAT_805cadcc;   // (init block)
extern float DAT_805cadd0;
extern float DAT_805cadd4;
extern float DAT_805cadd8;
extern float DAT_805caddc;
extern float DAT_805cade0;
extern float DAT_805cade4;
extern float DAT_805cade8;
extern float DAT_805cadec;
extern int   DAT_806126c4;   // one-time init guard
extern int   DAT_80611d78;   // player record table base
extern int   DAT_80612610;   // player record count
extern int   DAT_806108e4;   // per-frame dt fixed-point base
extern unsigned int DAT_806109c0;
extern char  DAT_805015bd;   // co-op mode flag
extern char  DAT_805015bf;   // difficulty selector
extern int   DAT_80612990;   // capture particle emitter handle

// keyframe-driven offset globals (written by webnet_timeline_advance)
extern float DAT_805caef8;
extern float DAT_805caefc;
extern float DAT_805caf00;
extern float DAT_805caf04;
extern float DAT_805caf08;
extern float DAT_805caf0c;
extern float DAT_805caf10;
extern float DAT_805caf14;
extern float DAT_805caf18;
extern float DAT_805caf1c;
extern float DAT_805caf20;
extern float DAT_805caf24;
extern int   DAT_805caf28;
extern int   DAT_805caf2c;
extern int   DAT_805caf30;
extern int   DAT_805caf34;

// FLOAT/DOUBLE constants
extern float  FLOAT_80618ac0;
extern float  FLOAT_80618ac4;   // 0.0
extern float  FLOAT_80618ac8;
extern float  FLOAT_80618acc;   // 65536/360-ish (deg->fixed)
extern float  FLOAT_80618ad0;
extern float  FLOAT_80618ad4;
extern float  FLOAT_80618ad8;
extern float  FLOAT_80618adc;
extern float  FLOAT_80618ae0;
extern float  FLOAT_80618ae4;
extern float  FLOAT_80618ae8;
extern float  FLOAT_80618aec;   // 1.5
extern float  FLOAT_80618af0;
extern float  FLOAT_80618af4;
extern float  FLOAT_806108ec;   // frame dt (float)
extern float  FLOAT_80618b08;
extern float  FLOAT_80618b0c;
extern float  FLOAT_80618b10;
extern float  FLOAT_80618b14;
extern float  FLOAT_80618b18;
extern float  FLOAT_80618b1c;   // 0.5
extern float  FLOAT_80618b20;   // 1.0
extern float  FLOAT_80618b24;
extern float  FLOAT_80618b28;
extern float  FLOAT_80618b2c;
extern float  FLOAT_80618b30;
extern float  FLOAT_80618b34;
extern float  FLOAT_80618b38;
extern float  FLOAT_80618b3c;
extern float  FLOAT_80618b40;
extern float  FLOAT_80618b44;
extern float  FLOAT_80618b48;
extern float  FLOAT_80618b4c;
extern float  FLOAT_80618b50;
extern float  FLOAT_80618b54;
extern float  FLOAT_80618b58;
extern float  FLOAT_80618b5c;
extern float  FLOAT_80618b60;
extern float  FLOAT_80618b64;
extern float  FLOAT_80618b68;
extern double DOUBLE_80618af8;
extern double DOUBLE_80618b00;

// constant vector templates
extern float DAT_803e02a4;
extern float DAT_803e02a8;
extern float DAT_803e02ac;
extern float DAT_803e02b0;
extern float DAT_803e02b4;
extern float DAT_803e02b8;
extern float DAT_803e02bc;
extern float DAT_803e02c0;
extern float DAT_803e02c4;
extern float DAT_803e02c8;
extern float DAT_803e02cc;
extern float DAT_803e02d0;
extern float DAT_803e02d4;
extern float DAT_803e02d8;
extern float DAT_803e02dc;
extern float DAT_803e02e0;
extern float DAT_803e02e4;
extern float DAT_803e02e8;
extern float DAT_803e02ec;
extern float DAT_803e02f0;
extern float DAT_803e02f4;
extern int   DAT_8046d268;
extern int   DAT_8046d26c;
extern int   DAT_8046d270;
extern int   DAT_8046d274;

// tables: timeline row table (&DAT_8046d630 stride 0xc: [0]=loop-count,[1]=keyframe
// base ptr,[2]=data ptr), event table (&DAT_8046d6fc stride 0x18), event-fired
// latch (&DAT_8046d710), boss body-part scratch (&DAT_8046d710 zeroed as 9x0x18).
extern int  DAT_8046d630;
extern int  DAT_8046d6fc;
extern int  DAT_8046d700;
extern int  DAT_8046d704;
extern int  DAT_8046d708;
extern int  DAT_8046d70c;
extern int  DAT_8046d710;
extern int *PTR_DAT_8046d634;
extern int  DAT_805cae74;    // capture particle descriptor
extern int  DAT_805cae3c;    // jitter state block A
extern int  DAT_805cae58;    // jitter state block B

// forward decls for this file
void webnet_place_damage_indicators(int *player);
void webnet_entity_spawn(void);
void webnet_render_callback(unsigned long long p1, double p2, double p3, double p4,
                            double p5, double p6, double p7, double p8, int *param_9);
void webnet_master_tick(unsigned long long p1, double p2, double p3, double p4,
                        double p5, double p6, double p7, unsigned long long p8);
void webnet_apply_capture_damage(double damage, int attacker, int target);
void webnet_begin_phase4(void);
void webnet_projectile_spawn_tick(void);
void webnet_animate_arm_bones(void);
void webnet_timeline_advance(void);

// ============================================================================
// 0x80247bc4  webnet_arm_compute_endpoints
// Compute two world points (a "front" and a "side" endpoint) from the WRTH
// entity's tip bone, transformed through the entity's world matrix built from
// its position (+0x64/0x68/0x6c) and ZYX euler (+0xc0/0x70/0xc8, deg->fixed via
// FLOAT_80618acc/ad0), then translated by the bone anchor. The entity pointer
// and the "front" output pointer arrive packed in r28 (Ghidra __save_gpr_28).
// ============================================================================
void webnet_arm_compute_endpoints(int entity, int *frontOut, float *sideOut)
{
    int   *piVar3;
    int    boneRec;
    float  front[3];
    float  side[3];
    float  mtxA[16];
    float  mtxR[16];

    piVar3 = *(int **)(*(int *)(entity + 4) + 4);
    boneRec = piVar3[*piVar3 * -0x30 + 0x1b];

    front[0] = *(float *)(boneRec + 0x78) - *(float *)(boneRec + 0x30);
    front[1] = *(float *)(boneRec + 0x7c) - *(float *)(boneRec + 0x34);
    front[2] = *(float *)(boneRec + 0x80) - *(float *)(boneRec + 0x38);
    side[0]  = *(float *)(boneRec + 0x90) - *(float *)(boneRec + 0x30);
    side[1]  = *(float *)(boneRec + 0x94) - *(float *)(boneRec + 0x34);
    side[2]  = *(float *)(boneRec + 0x98) - *(float *)(boneRec + 0x38);

    mtx4_set_translation((double)*(float *)(entity + 100),
                         (double)*(float *)(entity + 0x68),
                         (double)*(float *)(entity + 0x6c));
    mtx4_set_rotation_zyx((double)((FLOAT_80618acc * *(float *)(entity + 0xc0)) / FLOAT_80618ad0),
                          (double)((FLOAT_80618acc * *(float *)(entity + 0x70)) / FLOAT_80618ad0),
                          (double)((FLOAT_80618acc * *(float *)(entity + 200)) / FLOAT_80618ad0),
                          mtxR);
    mtx4_multiply(mtxR, mtxA, mtxA);
    mtx4_set_translation((double)*(float *)(boneRec + 0x30),
                         (double)*(float *)(boneRec + 0x34),
                         (double)*(float *)(boneRec + 0x38));
    mtx4_multiply(mtxR, mtxA, mtxA);
    mtx4_transform_point(mtxA, front);
    mtx4_transform_point(mtxA, side);

    if (frontOut != (int *)0) {
        ((float *)frontOut)[0] = front[0];
        ((float *)frontOut)[1] = front[1];
        ((float *)frontOut)[2] = front[2];
    }
    if (sideOut != (float *)0) {
        sideOut[0] = side[0];
        sideOut[1] = side[1];
        sideOut[2] = side[2];
    }
}

// ============================================================================
// 0x80247d7c  webnet_place_damage_indicators
// While the web-net is engaged (phase 7), place player damage indicators at the
// engaged player's position plus the WRTH arm endpoints, keyed by the engaged
// sub-phase (DAT_805cae38).
// ============================================================================
void webnet_place_damage_indicators(int *player)
{
    int   entity;
    float front[3];
    int   side[7];
    float playerPos[3];

    if (DAT_805cadb8 == 7) {
        entity = *player;
        playerPos[0] = *(float *)(entity + 100)  + *(float *)(entity + 0x1b4);
        playerPos[1] = *(float *)(entity + 0x68) + *(float *)(entity + 0x1b8);
        playerPos[2] = *(float *)(entity + 0x6c) + *(float *)(entity + 0x1bc);
        webnet_arm_compute_endpoints(entity, side, front);
        if (DAT_805cae38 == 2) {
            player_add_damage_indicator((double)FLOAT_80618ad8, (double)FLOAT_80618ad8,
                                        player, playerPos, 1);
        } else if (DAT_805cae38 < 2) {
            if (DAT_805cae38 == 0) {
                player_add_damage_indicator((double)FLOAT_80618ad4, (double)FLOAT_80618ad4,
                                            player, front, 1);
            } else if (-1 < DAT_805cae38) {
                player_add_damage_indicator((double)FLOAT_80618ad4, (double)FLOAT_80618ad4,
                                            player, (float *)side, 1);
            }
        }
    }
}

// ============================================================================
// 0x80247e70  webnet_entity_spawn
// WRTH boss spawn/construct callback. Registers the update handlers (slots 2/3/9)
// and the render callback, sets entity flags, seeds all web-net state globals,
// plays the intro SFX (0x390), and chooses the base or boss variant by player
// count (>= 16 -> boss, tied to LPTR/HLC2 helper entities). Entity ptr in r28.
// ============================================================================
void webnet_entity_spawn(void)
{
    int   entity;
    int   posX, posY, posZ;
    float zero;
    int   helperL, helperH;
    unsigned int sndHandle;
    unsigned int rngU;
    int   idx, count;
    int   sndParamsA[13];
    int   sndParamsB[13];

    entity = __save_gpr_28();
    posZ = *(int *)&DAT_803e02ac;
    posY = *(int *)&DAT_803e02a8;
    posX = *(int *)&DAT_803e02a4;

    entity_register_for_update(entity, 2, -0x7fdb8584);
    entity_register_for_update(entity, 3, -0x7fdb7c40);
    entity_set_render_type(entity, (void *)webnet_render_callback);
    entity_register_for_update(entity, 9, -0x7fdb8284);
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x40000000;
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x10;
    *(unsigned int *)(entity + 0x9c) = *(unsigned int *)(entity + 0x9c) | 0x4000000;
    *(int *)(entity + 0x34) = 0xd5;
    *(char *)(entity + 0x32) = 2;

    // (all difficulty branches seed the same field 0x1d8 in this build)
    *(float *)(entity + 0x1d8) = FLOAT_80618adc;
    *(int *)(entity + 0x1cc) = *(int *)(entity + 0x1d8);

    zero = FLOAT_80618ac4;
    if (DAT_806126c4 != 0) {
        __restore_gpr_28();
        return;
    }

    DAT_805cae20 = FLOAT_80618ac4;
    DAT_805cae24 = 0;
    if (*(int *)(DAT_80611d78 + 0x1400) < 0x10) {
        // Base variant.
        DAT_805cae30 = 0;
        *(int *)(entity + 100)  = posX;
        *(int *)(entity + 0x68) = posY;
        *(int *)(entity + 0x6c) = posZ;
        DAT_805cadb8 = 0;
        DAT_805cae28 = zero;
        DAT_805cae1c = 0;
        DAT_805cae04 = zero;
        if (*(int *)(entity + 0x22c) < 0) {
            sound_set_voice_volume_scale(3);
            sound_init_play_params((int)sndParamsA);
            *(char *)((int)sndParamsA + 0x30) = 1;
            sndParamsA[0] = 0x390;
            *(int *)((int)sndParamsA + 0x1c) = *(int *)(entity + 100);
            *(int *)((int)sndParamsA + 0x20) = *(int *)(entity + 0x68);
            *(int *)((int)sndParamsA + 0x24) = *(int *)(entity + 0x6c);
            sndHandle = sound_play_request(sndParamsA);
            *(unsigned int *)(entity + 0x22c) = sndHandle;
        }
    } else {
        // Boss variant (>= 16 players): anchored to LPTR/HLC2 helper entities.
        helperL = entity_handle_resolve(0x4c505452);
        helperH = entity_handle_resolve(0x484c4332);
        DAT_805cae28 = FLOAT_80618ac4;
        DAT_805cae30 = 1;
        *(int *)(entity + 100)  = DAT_8046d268;
        *(int *)(entity + 0x68) = DAT_8046d26c;
        *(int *)(entity + 0x6c) = DAT_8046d270;
        *(int *)(entity + 0x70) = DAT_8046d274;
        DAT_805cadb8 = 6;
        DAT_805cae1c = 0xd;
        DAT_805cae04 = *(float *)(helperL + 100);
        *(float *)(entity + 100) = *(float *)(entity + 100) + DAT_805cae04;
        if (*(int *)(entity + 0x22c) < 0) {
            sound_set_voice_volume_scale(3);
            sound_init_play_params((int)sndParamsB);
            *(char *)((int)sndParamsB + 0x30) = 1;
            sndParamsB[0] = 0x390;
            *(int *)((int)sndParamsB + 0x1c) = *(int *)(entity + 100);
            *(int *)((int)sndParamsB + 0x20) = *(int *)(entity + 0x68);
            *(int *)((int)sndParamsB + 0x24) = *(int *)(entity + 0x6c);
            sndHandle = sound_play_request(sndParamsB);
            *(unsigned int *)(entity + 0x22c) = sndHandle;
        }
        if (helperH != 0) {
            *(float *)(helperH + 100) = *(float *)(helperH + 100) + *(float *)(helperL + 100);
            *(int *)(*(int *)(helperH + 4) + 0x30) = 1;
            *(int *)(helperH + 0x34) = 0xd5;
            *(unsigned int *)(helperH + 0x28) = *(unsigned int *)(helperH + 0x28) | 0x40000000;
        }
    }

    // Common init: rates, keyframe scratch, jitter blocks.
    DAT_805cae00 = FLOAT_80618ac4;
    DAT_805cae08 = FLOAT_80618ac4;
    DAT_805cae38 = 0;
    DAT_805cae34 = 0;
    rngU = DAT_806109c0 ^ 0x80000000;
    DAT_805cadfc = FLOAT_80618ae0 *
                   (FLOAT_80618ae4 / (float)((double)(int)rngU - DOUBLE_80618af8));
    idx = 0;
    count = 9;
    do {
        *(int *)((int)&DAT_8046d710 + idx) = 0;
        count += -1;
        idx += 0x18;
    } while (count != 0);
    DAT_805cadd8 = FLOAT_80618ac4;
    DAT_805caddc = FLOAT_80618ac4;
    DAT_805cade0 = FLOAT_80618ac4;
    DAT_805cade4 = FLOAT_80618ac4;
    DAT_805cade8 = FLOAT_80618ac4;
    DAT_805cadec = FLOAT_80618ac4;
    DAT_805cadcc = FLOAT_80618ac4;
    DAT_805cadd0 = FLOAT_80618ac4;
    DAT_805cadd4 = FLOAT_80618ac4;
    rngU = (unsigned int)rng_counter_tick();
    DAT_805cadf0 = FLOAT_80618ac8 *
                   ((float)((double)(int)rngU - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
    rngU = (unsigned int)rng_counter_tick();
    DAT_805cadf4 = FLOAT_80618ac8 *
                   ((float)((double)(int)rngU - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
    unsigned int rngU2 = (unsigned int)rng_counter_tick();
    DAT_805cadf4 = DAT_805cadf4 * FLOAT_80618af0;
    DAT_805cadf0 = DAT_805cadf0 * FLOAT_80618ae0;
    DAT_805cadf8 = FLOAT_80618ac8 *
                   ((float)((double)(int)rngU2 - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec)
                   * FLOAT_80618af4;
    FUN_8024b9a8();
    player_ai_projectile_jitter_init((float *)&DAT_805cae3c);
    player_ai_projectile_jitter_init((float *)&DAT_805cae58);

    __restore_gpr_28();
}

// ============================================================================
// 0x802482e4  webnet_render_callback
// Registered update/render callback (slot 9). During the engaged phase (7) each
// time the boss arm bone (id 0x2a) connects it advances the engaged sub-phase
// (DAT_805cae38); the third connection triggers the capture-particle burst and
// marks the mission class complete. Phase 4 resets the entity's charge field.
// ============================================================================
void webnet_render_callback(unsigned long long p1, double p2, double p3, double p4,
                            double p5, double p6, double p7, double p8, int *param_9)
{
    int   entity;
    float burstPos[3];

    entity = *param_9;
    burstPos[0] = DAT_803e02b0;
    ((int *)burstPos)[1] = *(int *)&DAT_803e02b4;
    ((int *)burstPos)[2] = *(int *)&DAT_803e02b8;

    if (DAT_805cadb8 == 7) {
        if ((param_9[6] == 0x2a) && (DAT_805cae38 < 3)) {
            FUN_8024ce94();
            DAT_805cae38 = DAT_805cae38 + 1;
            if (DAT_805cae38 == 3) {
                *(float *)(entity + 0x1cc) = FLOAT_80618ac4;
                particle_emitter_spawn(&DAT_80612990, &DAT_805cae74, 0,
                                       (float *)(entity + 100), burstPos);
                statlvl_class_complete((double)FLOAT_80618ac4, p2, p3, p4, p5, p6, p7, p8);
            }
        }
    } else if (DAT_805cadb8 == 4) {
        *(float *)(entity + 0x1cc) = FLOAT_80618b08;
    }
    (void)p1;
}

// ============================================================================
// 0x802483c0  webnet_master_tick
// Master per-frame tick for the whole web-net encounter, bracketed by the
// render-list context lock (FUN_803826dc / FUN_80382728). Runs the phase state
// machine (charge -> pre-explode -> grow -> engaged -> defeated -> score), sweeps
// players for capture damage, spawns the explosion/debris on defeat, advances the
// scripted keyframe events, drives the jitter, and calls the per-frame sub-updates.
// ============================================================================
void webnet_master_tick(unsigned long long p1, double p2, double p3, double p4,
                        double p5, double p6, double p7, unsigned long long p8)
{
    float  fVar1, fVar2;
    int    iVar3;
    int   *ctx;
    unsigned int uVar5;
    int   *puVar6;
    int    iVar7, iVar8;
    double dVar9;
    unsigned long long uVar10 = 0;
    float  local_134[3];   // emitter spawn vec
    float  dir[3];         // local_110/10c/108
    float  srcPos[3];      // local_104/100/fc
    int    slot[12];       // damage params (local_98 block)
    int    emitDesc;
    int    sndBuf[12];
    unsigned int rngU;

    ctx = (int *)FUN_803826dc();

    if ((2 < DAT_805cae38) || (DAT_805cadb8 == 5)) {
        uVar10 = (unsigned long long)FUN_8024cb4c((int)ctx);
    }

    if (DAT_805cadb8 != 6) {
        if (DAT_805cadb8 < 6) {
            if (DAT_805cadb8 == 4) {
                if ((float)ctx[0x73] < FLOAT_80618ac4) {
                    local_134[0] = DAT_803e02d4;
                    ((int *)local_134)[1] = *(int *)&DAT_803e02d8;
                    ((int *)local_134)[2] = *(int *)&DAT_803e02dc;
                    DAT_805cadb8 = 5;
                    DAT_805cae1c = 0xb;
                    DAT_805cae24 = 0;
                    DAT_805cae20 = FLOAT_80618ac4;
                    DAT_805cae28 = FLOAT_80618ac4;
                    particle_emitter_spawn(&DAT_80612990, &DAT_805cae74, 0,
                                           (float *)(ctx + 0x19), local_134);
                }
            } else if (DAT_805cadb8 < 4) {
                if ((1 < DAT_805cadb8) && (0 < DAT_80612610)) {
                    iVar8 = 0;
                    iVar7 = DAT_80612610;
                    do {
                        iVar3 = *(int *)(DAT_80611d78 + iVar8 + 0x14);
                        if ((*(float *)(iVar3 + 100) < FLOAT_80618b0c) &&
                            (FLOAT_80618ac4 < *(float *)(iVar3 + 0x1cc))) {
                            rngU = (unsigned int)rng_counter_tick();
                            srcPos[0] = *(float *)(iVar3 + 100) + *(float *)(iVar3 + 0x1a8);
                            srcPos[1] = *(float *)(iVar3 + 0x1ac) *
                                        (FLOAT_80618b14 *
                                         ((float)((double)(int)rngU - DOUBLE_80618b00) / FLOAT_80618ae8)
                                         + FLOAT_80618b10) + *(float *)(iVar3 + 0x68);
                            srcPos[2] = *(float *)(iVar3 + 0x6c) + *(float *)(iVar3 + 0x1b0);
                            dir[0] = (float)ctx[0x19] - srcPos[0];
                            dir[1] = (float)ctx[0x1a] - srcPos[1];
                            dir[2] = (float)ctx[0x1b] - srcPos[2];
                            fVar1 = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
                            if (FLOAT_80618b18 < fVar1) {
                                fVar2 = 1.0f / SQRT(fVar1);
                                fVar2 = fVar2 * -(FLOAT_80618aec * fVar1 * fVar2 * fVar2 - FLOAT_80618b1c);
                                float s = (float)((double)FLOAT_80618b20 *
                                                  (double)(fVar2 * -(FLOAT_80618aec * fVar1 * fVar2 * fVar2 - FLOAT_80618b1c)));
                                dir[0] = dir[0] * s;
                                dir[1] = dir[1] * s;
                                dir[2] = dir[2] * s;
                            }
                            uVar10 = (unsigned long long)switch_slot_reset((int)&slot[0]);
                            slot[0]  = iVar3;
                            slot[1]  = (int)ctx;
                            slot[2]  = (int)ctx;
                            *(int *)((int)slot + 0x1c) = 0x28;   // local_84
                            *(int *)((int)slot + 0x20) = 0x28;   // local_80
                            *(float *)((int)slot + 0x24) = FLOAT_80618b24;  // local_78
                            *(float **)((int)slot + 0x28) = srcPos;  // local_70
                            *(float **)((int)slot + 0x2c) = dir;     // local_6c
                            *(float **)((int)slot + 0x30) = dir;     // local_68
                            damage_dispatch_to_entity((int)uVar10, (double)dir[0], (double)dir[2],
                                                      p4, p5, p6, p7, p8, &slot[0]);
                            break;
                        }
                        iVar8 = iVar8 + 0x141c;
                        iVar7 = iVar7 + -1;
                    } while (iVar7 != 0);
                }
            } else {
                // phase 5: pre-explode charge timer -> explosion / partial burst.
                rngU = DAT_806109c0 ^ 0x80000000;
                DAT_805cae28 = (float)((double)FLOAT_806108ec *
                               (double)(float)((double)FLOAT_80618b20 / (double)(float)((double)(int)rngU - DOUBLE_80618af8))
                               + (double)DAT_805cae28);
                if (FLOAT_80618b28 <= DAT_805cae28) {
                    float epos[3];
                    epos[0] = DAT_803e02bc;
                    ((int *)epos)[1] = *(int *)&DAT_803e02c0;
                    ((int *)epos)[2] = *(int *)&DAT_803e02c4;
                    emitDesc = DAT_80612990;
                    particle_handle_destroy(&emitDesc);
                    sound_set_voice_volume_scale(0);
                    sound_stop_handle();
                    ctx[0x8b] = -1;
                    sound_init_play_params((int)sndBuf);
                    sndBuf[0] = 0x39c;
                    sound_play_request(sndBuf);
                    entity_destroy_with_event(ctx);
                    player_ai_explosion_spawn(0x21, 0, (float *)(ctx + 0x19), epos, 0, 0, 1);
                    if ((DAT_805015bd != 10) || (DAT_80612610 < 2)) {
                        for (iVar7 = 0; iVar7 < 5; iVar7++) {
                            FUN_8024c418((double)FLOAT_80618b2c, (double)FLOAT_80618b30, 0x38b, ctx + 0x19, 1);
                            FUN_8024c418((double)FLOAT_80618b2c, (double)FLOAT_80618b30, 0x38c, ctx + 0x19, 1);
                            FUN_8024c418((double)FLOAT_80618b2c, (double)FLOAT_80618b30, 0x38d, ctx + 0x19, 1);
                        }
                    }
                    goto done;
                }
                if ((FLOAT_80618b34 <= DAT_805cae28) &&
                    (statlvl_class_is_state5(0x18) == 0)) {
                    float epos[3];
                    epos[0] = DAT_803e02c8;
                    ((int *)epos)[1] = *(int *)&DAT_803e02cc;
                    ((int *)epos)[2] = *(int *)&DAT_803e02d0;
                    player_ai_explosion_spawn(0x21, 0, (float *)(ctx + 0x19), epos, 0, 0, 1);
                    if ((DAT_805015bd != 10) || (DAT_80612610 < 2)) {
                        player_ai_explosion_spawn(0x21, 0, (float *)(ctx + 0x19), epos, 0, 0, 1);
                    }
                    sound_init_play_params((int)sndBuf);
                    sndBuf[0] = 0x39c;
                    sound_play_request(sndBuf);
                    if ((DAT_805015bd != 10) || (DAT_80612610 < 2)) {
                        for (iVar7 = 0; iVar7 < 5; iVar7++) {
                            FUN_8024c418((double)FLOAT_80618b2c, (double)FLOAT_80618b30, 0x38b, ctx + 0x19, 1);
                            FUN_8024c418((double)FLOAT_80618b2c, (double)FLOAT_80618b30, 0x38c, ctx + 0x19, 1);
                            FUN_8024c418((double)FLOAT_80618b2c, (double)FLOAT_80618b30, 0x38d, ctx + 0x19, 1);
                        }
                    }
                    statlvl_class_complete((double)FLOAT_80618ac4, p2, p3, p4, p5, p6, p7, p8);
                }
            }
        } else if (DAT_805cadb8 < 10) {
            if (DAT_805cadb8 < 8) {
                if ((DAT_805cae30 == 0) && (FLOAT_80618b38 < DAT_805cae28)) {
                    ctx[0x73] = *(int *)&FLOAT_80618b08;
                }
                if ((float)ctx[0x73] <= FLOAT_80618ac4) {
                    sound_play_persistent(0x39b, 0, 0);
                    DAT_805cadb8 = 8;
                    statlvl_class_complete((double)FLOAT_80618ac4, p2, p3, p4, p5, p6, p7, p8);
                    DAT_805cae28 = FLOAT_80618ac4;
                    DAT_805cae24 = 0;
                    DAT_805cae20 = FLOAT_80618ac4;
                    DAT_805cae1c = 0x10;
                }
            } else if (DAT_805cadb8 == 9) {
                FUN_8024be98((int)uVar10, p2, p3, p4, p5, p6, p7, p8);
            }
        }
    }

    if (-1 < (int)ctx[0x8b]) {
        sound_update_3d_volume((double)FLOAT_80618b1c, ctx[0x8b], ctx + 0x19);
    }
    FUN_8024ccb8();

    if ((DAT_806108e4 != 0) && ((ctx[10] & 1) != 0) && (DAT_805cadb8 != 9)) {
        player_ai_projectile_jitter_tick((double)FLOAT_80618ac8, (int *)&DAT_805cae3c);
        player_ai_projectile_jitter_tick((double)FLOAT_80618ac8, (int *)&DAT_805cae58);
        rngU = DAT_806109c0 ^ 0x80000000;
        DAT_805cae28 = (float)((double)FLOAT_806108ec *
                       (double)(float)((double)FLOAT_80618b20 / (double)(float)((double)(int)rngU - DOUBLE_80618af8))
                       + (double)DAT_805cae28);

        if (DAT_805cae30 == 0) {
            iVar8 = 0;
            for (iVar7 = 0; iVar7 < 9; iVar7++) {
                if ((DAT_805cadb8 == *(int *)((int)&DAT_8046d700 + iVar8)) &&
                    (*(float *)((int)&DAT_8046d704 + iVar8) <= DAT_805cae28) &&
                    (*(int *)((int)&DAT_8046d710 + iVar8) == 0)) {
                    iVar3 = *(int *)((int)&DAT_8046d6fc + iVar8);
                    if (iVar3 == 1) {
                        statlvl_class_complete((double)FLOAT_80618ac4, p2, p3, p4, p5, p6, p7, p8);
                    } else if ((iVar3 < 1) && (-1 < iVar3)) {
                        iVar3 = entity_handle_resolve(*(int *)((int)&DAT_8046d708 + iVar8));
                        if (iVar3 != 0) {
                            webnet_apply_capture_damage((double)*(float *)((int)&DAT_8046d70c + iVar8), (int)ctx, iVar3);
                        }
                    }
                    *(int *)((int)&DAT_8046d710 + iVar8) = 1;
                }
                iVar8 = iVar8 + 0x18;
            }
        }

        if ((DAT_805cae30 != 0) &&
            (ctx[0x19] = *(int *)&(*(float *)&ctx[0x19]), 1)) {
            // boss variant: descend arm by DAT_805cae04, then check defeat sound-source.
            *(float *)&ctx[0x19] = (float)ctx[0x19] - DAT_805cae04;
            if (DAT_805cae34 == 0) {
                iVar8 = 0;
                for (iVar7 = 0; iVar7 < DAT_80612610; iVar7++) {
                    puVar6 = (int *)sound_source_get_nth_active(DAT_80611d78 + iVar8, 1);
                    if (puVar6 == ctx) {
                        statlvl_class_complete((double)FLOAT_80618ac4, p2, p3, p4, p5, p6, p7, p8);
                        DAT_805cae34 = 1;
                        break;
                    }
                    iVar8 = iVar8 + 0x141c;
                }
            }
        }

        if (FLOAT_80618b20 <= DAT_805cae20) {
            webnet_timeline_advance();
        }
        if ((*(unsigned int *)((&PTR_DAT_8046d634)[DAT_805cae1c * 3] + DAT_805cae24 * 0x18 + 0x10) & 0x210) != 0) {
            FUN_8024b53c();
        }
        FUN_8024ade0();
        FUN_80249c54();
        FUN_80249efc();
        if (DAT_805cae30 != 0) {
            iVar7 = entity_handle_resolve(0x4c505452);
            iVar8 = entity_handle_resolve(0x484c4332);
            fVar1 = FLOAT_80618b3c;
            DAT_805cae04 = *(float *)(iVar7 + 100);
            *(float *)&ctx[0x19] = (float)ctx[0x19] + DAT_805cae04;
            *(float *)(iVar8 + 100) = *(float *)(iVar7 + 100) - fVar1;
        }
    }

done:
    (void)dVar9; (void)uVar5; (void)p1;
    FUN_80382728();
}

// ============================================================================
// 0x80248bfc  webnet_apply_capture_damage
// Apply web-net capture damage from the boss (attacker) to a target entity,
// computing a jittered direction from the target's aim point and dispatching a
// type-0x28 damage packet.
// ============================================================================
void webnet_apply_capture_damage(double damage, int attacker, int target)
{
    float fVar1, fVar2;
    int   slot[12];
    float srcPos[3];   // local_9c/98/94
    float dir[3];      // local_a8/a4/a0
    unsigned int rngU;

    srcPos[0] = *(float *)(target + 100) + *(float *)(target + 0x1a8);
    rngU = (unsigned int)rng_counter_tick();
    srcPos[1] = *(float *)(target + 0x1ac) *
                (FLOAT_80618b14 *
                 ((float)((double)(int)rngU - DOUBLE_80618b00) / FLOAT_80618ae8) + FLOAT_80618b10)
                + *(float *)(target + 0x68);
    srcPos[2] = *(float *)(target + 0x6c) + *(float *)(target + 0x1b0);
    dir[0] = *(float *)(attacker + 100)  - srcPos[0];
    dir[1] = *(float *)(attacker + 0x68) - srcPos[1];
    dir[2] = *(float *)(attacker + 0x6c) - srcPos[2];
    fVar1 = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
    if (FLOAT_80618b18 < fVar1) {
        fVar2 = 1.0f / SQRT(fVar1);
        fVar2 = fVar2 * -(FLOAT_80618aec * fVar1 * fVar2 * fVar2 - FLOAT_80618b1c);
        float s = (float)((double)FLOAT_80618b20 *
                          (double)(fVar2 * -(FLOAT_80618aec * fVar1 * fVar2 * fVar2 - FLOAT_80618b1c)));
        dir[0] = dir[0] * s;
        dir[1] = dir[1] * s;
        dir[2] = dir[2] * s;
    }
    int ctx = switch_slot_reset((int)&slot[0]);
    slot[0] = target;
    slot[1] = attacker;
    slot[2] = attacker;
    *(int *)((int)slot + 0x1c) = 0x28;
    *(int *)((int)slot + 0x20) = 0x28;
    *(float *)((int)slot + 0x24) = (float)damage;
    *(float **)((int)slot + 0x28) = srcPos;
    *(float **)((int)slot + 0x2c) = dir;
    *(float **)((int)slot + 0x30) = dir;
    damage_dispatch_to_entity(ctx, (double)dir[0], (double)dir[2], (double)dir[1],
                              (double)dir[0], (double)srcPos[2], 0.0, 0.0, &slot[0]);
}

// ============================================================================
// 0x80248d84  webnet_begin_phase4
// Enter the charge phase (4): reset the timeline row/cursor/timers.
// ============================================================================
void webnet_begin_phase4(void)
{
    DAT_805cadb8 = 4;
    DAT_805cae1c = 10;
    DAT_805cae20 = FLOAT_80618ac4;
    DAT_805cae24 = 0;
    DAT_805cae28 = FLOAT_80618ac4;
}

// ============================================================================
// 0x80248db4  webnet_projectile_spawn_tick
// Burst-fire tick for the web-net projectiles. Gated by a fire-cadence timer
// (DAT_805cae10 / DAT_805cae14 vs webnet_phase_param_a) with an idle window
// (DAT_805cae18 vs webnet_phase_param_b). When firing, computes two spawn
// directions (homing toward DAT_805cadbc/c0/c4 in sweep phases 2/3, else an
// entity-oriented spread), applies jitter, and spawns two projectiles + trails.
// Entity ctx in r28.
// ============================================================================
void webnet_projectile_spawn_tick(void)
{
    float  fVar1, fVar2;
    int    entity;
    int    phaseIdx;
    double dtScaled, ease;
    unsigned int rngU;
    float  jitterDir[3];   // local_e8/e4/e0
    float  spawnA[3];      // local_f4/f0/ec (front)
    float  spawnB[3];      // local_100/fc/f8 (side)
    float  velA[3];        // local_d0/cc/c8
    float  velB[3];        // local_dc/d8/d4
    int    sndBuf[6];
    unsigned char sndFlag;
    float  sndVol, sndPitch;
    float  sndPos[3];
    float  mtx[17];

    entity = __save_gpr_28();

    if (DAT_805cadb8 == 6) {
        rngU = DAT_806109c0 ^ 0x80000000;
        fVar1 = FLOAT_80618ae4 / (float)((double)(int)rngU - DOUBLE_80618af8);
    } else {
        rngU = DAT_806109c0 ^ 0x80000000;
        fVar1 = FLOAT_80618ac8 * (FLOAT_80618ae4 / (float)((double)(int)rngU - DOUBLE_80618af8));
    }
    dtScaled = (double)fVar1;

    if ((double)DAT_805cae10 < webnet_phase_param_a()) {
        if ((double)DAT_805cae14 <= dtScaled) {
            rngU = (unsigned int)(DAT_806108e4 ^ 0x80000000);
            DAT_805cae14 = (float)((double)DAT_805cae14 +
                           (double)(float)((double)(int)rngU - DOUBLE_80618af8));
        } else {
            velA[0] = DAT_803e02e0; velA[1] = DAT_803e02e4; velA[2] = DAT_803e02e8;
            velB[0] = DAT_803e02ec; velB[1] = DAT_803e02f0; velB[2] = DAT_803e02f4;
            player_ai_projectile_compute_jittered_dir((double)FLOAT_80618af4, (double)FLOAT_80618af0,
                                                      (float *)&DAT_805cae3c, jitterDir);
            webnet_arm_compute_endpoints(entity, (int *)spawnA, spawnB);
            if (DAT_805cae30 != 0) {
                spawnA[0] = spawnA[0] + DAT_805cae04;
                spawnB[0] = spawnB[0] + DAT_805cae04;
            }
            if ((DAT_805cadb8 == 2) || (DAT_805cadb8 == 3)) {
                velA[0] = DAT_805cadbc - spawnA[0];
                velA[1] = DAT_805cadc0 - spawnA[1];
                velB[0] = DAT_805cadbc - spawnB[0];
                velA[2] = DAT_805cadc4 - spawnA[2];
                velB[1] = DAT_805cadc0 - spawnB[1];
                velB[2] = DAT_805cadc4 - spawnB[2];
                fVar1 = velA[2] * velA[2] + velA[0] * velA[0] + velA[1] * velA[1];
                if (FLOAT_80618b18 < fVar1) {
                    fVar2 = 1.0f / SQRT(fVar1);
                    fVar2 = fVar2 * -(FLOAT_80618aec * fVar1 * fVar2 * fVar2 - FLOAT_80618b1c);
                    fVar1 = FLOAT_80618b20 * fVar2 * -(FLOAT_80618aec * fVar1 * fVar2 * fVar2 - FLOAT_80618b1c);
                    velA[0] = velA[0] * fVar1; velA[1] = velA[1] * fVar1; velA[2] = velA[2] * fVar1;
                }
                fVar1 = velB[2] * velB[2] + velB[0] * velB[0] + velB[1] * velB[1];
                if (FLOAT_80618b18 < fVar1) {
                    fVar2 = 1.0f / SQRT(fVar1);
                    fVar2 = fVar2 * -(FLOAT_80618aec * fVar1 * fVar2 * fVar2 - FLOAT_80618b1c);
                    fVar1 = FLOAT_80618b20 * fVar2 * -(FLOAT_80618aec * fVar1 * fVar2 * fVar2 - FLOAT_80618b1c);
                    velB[0] = velB[0] * fVar1; velB[1] = velB[1] * fVar1; velB[2] = velB[2] * fVar1;
                }
            } else {
                mtx4_set_rotation_zyx((double)((FLOAT_80618acc * *(float *)(entity + 0xc0)) / FLOAT_80618ad0),
                                      (double)((FLOAT_80618acc * *(float *)(entity + 0x70)) / FLOAT_80618ad0),
                                      (double)((FLOAT_80618acc * *(float *)(entity + 200)) / FLOAT_80618ad0),
                                      mtx);
                mtx4_transform_point(mtx, velA);
                mtx4_transform_point(mtx, velB);
            }
            phaseIdx = webnet_phase_status();  // FUN_802478a8: projectile phase tag
            if (DAT_805cae38 < 2) {
                velA[0] = velA[0] + jitterDir[0];
                velA[1] = velA[1] + jitterDir[1];
                velA[2] = velA[2] + jitterDir[2];
                sound_init_play_params((int)sndBuf);
                sndFlag = 1;
                sndBuf[0] = 0x391;
                sndVol = FLOAT_80618b40;
                sndPitch = FLOAT_80618b44;
                sndPos[0] = spawnA[0]; sndPos[1] = spawnA[1]; sndPos[2] = spawnA[2];
                (void)sndFlag; (void)sndVol; (void)sndPitch; (void)sndPos;
                sound_play_request(sndBuf);
                player_ai_projectile_spawn_simple((double)FLOAT_80618b48, (double)FLOAT_80618ac4,
                                                  0x28, spawnA, (int *)velA, entity, (int *)0,
                                                  6, 0, 0xd5, 0x184, phaseIdx);
                player_ai_spawn_trail_particle(0, 0, -1, spawnA, velA, 0xd5);
            }
            if (DAT_805cae38 < 1) {
                velB[0] = velB[0] + jitterDir[0];
                velB[1] = velB[1] + jitterDir[1];
                velB[2] = velB[2] + jitterDir[2];
                player_ai_projectile_spawn_simple((double)FLOAT_80618b48, (double)FLOAT_80618ac4,
                                                  0x28, spawnB, (int *)velB, entity, (int *)0,
                                                  6, 0, 0xd5, 0x184, phaseIdx);
                player_ai_spawn_trail_particle(0, 0, -1, spawnB, velB, 0xd5);
            }
            DAT_805cae14 = FLOAT_80618ac4;
        }
        dtScaled = (double)(FLOAT_80618b4c * FLOAT_806108ec + DAT_805cae08);
        ease = FUN_80390324((double)((float)(dtScaled - (double)FLOAT_80618ac4) / FLOAT_80618b50));
        rngU = DAT_806109c0 ^ 0x80000000;
        DAT_805cae08 = -(float)((double)FLOAT_80618b50 * (double)(float)ease - dtScaled);
        DAT_805cae10 = FLOAT_806108ec *
                       (FLOAT_80618b20 / (float)((double)(int)rngU - DOUBLE_80618af8)) + DAT_805cae10;
    } else {
        rngU = DAT_806109c0 ^ 0x80000000;
        DAT_805cae18 = FLOAT_806108ec *
                       (FLOAT_80618b20 / (float)((double)(int)rngU - DOUBLE_80618af8)) + DAT_805cae18;
        if (webnet_phase_param_b() <= (double)DAT_805cae18) {
            DAT_805cae10 = FLOAT_80618ac4;
            DAT_805cae18 = FLOAT_80618ac4;
        }
    }

    __restore_gpr_28();
}

// ============================================================================
// 0x80249364  webnet_animate_arm_bones
// Per-frame arm-bone animation (the matrix commit referenced by
// worm_web_net.cpp webnet_update_bones). Advances a shared spin phase
// (DAT_805cae0c) and rotates specific bones (indices 1, 2, 4, 5) about their
// pivot points using that phase and the secondary spin (DAT_805cae08).
// Bracketed by the bone-anim context lock (FUN_803826e4 / FUN_80382730).
// ============================================================================
void webnet_animate_arm_bones(void)
{
    int    entity;
    float *boneMtx;
    int   *piVar3;
    int    boneRec;
    int    boneOff;
    int    boneIdx;
    float  mtx[22];

    entity = FUN_803826e4();
    DAT_805cae0c = FLOAT_80618b4c * FLOAT_806108ec + DAT_805cae0c;
    if (FLOAT_80618b50 < DAT_805cae0c) {
        DAT_805cae0c = DAT_805cae0c - FLOAT_80618b50;
    }

    boneOff = 0;
    piVar3 = *(int **)(*(int *)(entity + 4) + 4);
    for (boneIdx = 0; boneIdx < piVar3[1]; boneIdx++) {
        piVar3 = *(int **)(*(int *)(entity + 4) + 4);
        if (boneIdx == 1) {
            boneRec = piVar3[*piVar3 * -0x30 + 0x1b];
            mtx4_set_translation((double)*(float *)(boneRec + 0x30),
                                 (double)*(float *)(boneRec + 0x34),
                                 (double)*(float *)(boneRec + 0x38));
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
            mtx4_set_rotation_zyx((double)FLOAT_80618ac4,
                                  (double)((FLOAT_80618acc * DAT_805cae0c) / FLOAT_80618ad0),
                                  (double)FLOAT_80618ac4, mtx);
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
            mtx4_set_translation(-(double)*(float *)(boneRec + 0x30),
                                 -(double)*(float *)(boneRec + 0x34),
                                 -(double)*(float *)(boneRec + 0x38));
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
        } else if (boneIdx == 2) {
            boneRec = piVar3[*piVar3 * -0x30 + 0x1b];
            mtx4_set_translation((double)*(float *)(boneRec + 0x18),
                                 (double)*(float *)(boneRec + 0x1c),
                                 (double)*(float *)(boneRec + 0x20));
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
            mtx4_set_rotation_zyx((double)FLOAT_80618ac4,
                                  -(double)((FLOAT_80618acc * DAT_805cae0c) / FLOAT_80618ad0),
                                  (double)FLOAT_80618ac4, mtx);
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
            mtx4_set_translation(-(double)*(float *)(boneRec + 0x18),
                                 -(double)*(float *)(boneRec + 0x1c),
                                 -(double)*(float *)(boneRec + 0x20));
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
        } else if (boneIdx == 4) {
            boneRec = piVar3[*piVar3 * -0x30 + 0x1b];
            mtx4_set_translation((double)*(float *)(boneRec + 0x48),
                                 (double)*(float *)(boneRec + 0x4c),
                                 (double)*(float *)(boneRec + 0x50));
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
            mtx4_set_rotation_zyx((double)FLOAT_80618ac4, (double)FLOAT_80618ac4,
                                  (double)((FLOAT_80618acc * DAT_805cae08) / FLOAT_80618ad0), mtx);
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
            mtx4_set_translation(-(double)*(float *)(boneRec + 0x48),
                                 -(double)*(float *)(boneRec + 0x4c),
                                 -(double)*(float *)(boneRec + 0x50));
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
        } else if (boneIdx == 5) {
            boneRec = piVar3[*piVar3 * -0x30 + 0x1b];
            mtx4_set_translation((double)*(float *)(boneRec + 0x60),
                                 (double)*(float *)(boneRec + 100),
                                 (double)*(float *)(boneRec + 0x68));
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
            mtx4_set_rotation_zyx((double)FLOAT_80618ac4, (double)FLOAT_80618ac4,
                                  (double)((FLOAT_80618acc * DAT_805cae08) / FLOAT_80618ad0), mtx);
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
            mtx4_set_translation(-(double)*(float *)(boneRec + 0x60),
                                 -(double)*(float *)(boneRec + 100),
                                 -(double)*(float *)(boneRec + 0x68));
            boneMtx = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            mtx4_multiply(mtx, boneMtx, boneMtx);
        }
        boneOff = boneOff + 0x40;
    }
    FUN_80382730();
}

// ============================================================================
// 0x802496e4  webnet_timeline_advance
// Advance the scripted animation timeline: step the keyframe cursor
// (DAT_805cae24) through the current timeline row (&DAT_8046d630 + row*0xc), run
// phase/state transitions on loop (including the LPTR/BLK1/BLK2 gated boss steps),
// and evaluate the current keyframe's flags to compute the per-frame offset globals
// (DAT_805caef8..DAT_805caf34) with optional rng jitter. Entity ctx in r28.
// ============================================================================
void webnet_timeline_advance(void)
{
    int   entity;
    int   iVar1, iVar2;
    unsigned int uVar3;
    int  *row;
    unsigned int flags;
    int   kfOff;

    entity = __save_gpr_28();
    DAT_805cae20 = FLOAT_80618ac4;
    DAT_805cae24 = DAT_805cae24 + 1;
    row = (int *)(&DAT_8046d630 + DAT_805cae1c * 0xc);

    if (DAT_805cae24 == *row) {
        DAT_805cae24 = 0;
        switch (DAT_805cadb8) {
        case 0:
            DAT_805cae28 = FLOAT_80618ac4;
            DAT_805cadb8 = 1;
            DAT_805cae1c = 1;
            break;
        case 1:
            switch (DAT_805cae1c) {
            case 1: DAT_805cae1c = 2; break;
            case 3: DAT_805cae1c = 4; break;
            case 4:
                iVar2 = entity_handle_resolve(0x424c4b31);
                if ((*(float *)(iVar2 + 0x1cc) <= FLOAT_80618ac4) || (iVar2 == 0)) {
                    DAT_805cae1c = 5;
                }
                break;
            case 5: DAT_805cae1c = 6; break;
            case 6:
                iVar2 = entity_handle_resolve(0x424c4b32);
                if ((*(float *)(iVar2 + 0x1cc) <= FLOAT_80618ac4) || (iVar2 == 0)) {
                    DAT_805cae1c = 7;
                }
                break;
            case 7: DAT_805cae1c = 8; break;
            }
            break;
        case 5:
            DAT_805cae28 = FLOAT_80618b34;
            break;
        case 6:
            DAT_805cadb8 = 7;
            DAT_805cae1c = 0xe;
            DAT_805cae28 = FLOAT_80618ac4;
            break;
        case 8:
            anim_set_blend_weight((double)FLOAT_80618b20, *(int *)(entity + 4));
            DAT_805cadb8 = 9;
            sound_play_or_resume_3d((double)FLOAT_80618ac4, (double)FLOAT_80618b20,
                                    (double)FLOAT_80618b20, 0x39b, 0, 0);
            DAT_805cae28 = FLOAT_80618ac4;
            break;
        }
        row = (int *)(&DAT_8046d630 + DAT_805cae1c * 0xc);
    }

    if ((*(unsigned int *)(row[1] + DAT_805cae24 * 0x18 + 0x10) & 0x8000) != 0) {
        debris_seed_fill_timers();
    }

    kfOff = DAT_805cae24 * 0x18;
    flags = *(unsigned int *)(row[1] + kfOff + 0x10);
    if ((flags & 2) != 0) {
        // component 0
        iVar1 = *(int *)(row[1] + kfOff);
        if (iVar1 < 0) {
            DAT_805caef8 = DAT_805caf08; DAT_805caefc = DAT_805caf0c;
            DAT_805caf00 = DAT_805caf10; DAT_805caf04 = DAT_805caf14;
        } else {
            iVar1 = iVar1 * 0x10;
            DAT_805caef8 = *(float *)(row[2] + iVar1);
            DAT_805caefc = *(float *)(row[2] + iVar1 + 4);
            DAT_805caf00 = *(float *)(row[2] + iVar1 + 8);
            DAT_805caf04 = *(float *)(row[2] + iVar1 + 0xc);
        }
        // component 1
        iVar1 = *(int *)(row[1] + kfOff + 4);
        if (iVar1 < 0) {
            DAT_805caf08 = DAT_805caf18; DAT_805caf0c = DAT_805caf1c;
            DAT_805caf10 = DAT_805caf20; DAT_805caf14 = DAT_805caf24;
        } else {
            iVar1 = iVar1 * 0x10;
            DAT_805caf08 = *(float *)(row[2] + iVar1);
            DAT_805caf0c = *(float *)(row[2] + iVar1 + 4);
            DAT_805caf10 = *(float *)(row[2] + iVar1 + 8);
            DAT_805caf14 = *(float *)(row[2] + iVar1 + 0xc);
        }
        // component 2 (with optional rng jitter based on flag bits)
        iVar2 = *(int *)(row[1] + kfOff + 8);
        if (iVar2 < 0) {
            DAT_805caf18 = *(float *)(row[2] + iVar2 * -0x10);
            DAT_805caf1c = *(float *)(row[2] + iVar2 * -0x10 + 4);
            DAT_805caf20 = *(float *)(row[2] + iVar2 * -0x10 + 8);
            DAT_805caf24 = *(float *)(row[2] + iVar2 * -0x10 + 0xc);
            if ((flags & 0x400) == 0) {
                if ((flags & 0x800) != 0) {
                    if ((flags & 0x1000) != 0) {
                        uVar3 = (unsigned int)rng_counter_tick();
                        DAT_805caf18 = DAT_805caf18 + FLOAT_80618b34 *
                                       ((float)((double)(int)uVar3 - DOUBLE_80618b00) / FLOAT_80618ae8)
                                       + FLOAT_80618b5c;
                    }
                    if ((flags & 0x2000) != 0) {
                        uVar3 = (unsigned int)rng_counter_tick();
                        DAT_805caf1c = DAT_805caf1c + FLOAT_80618ac0 *
                                       ((float)((double)(int)uVar3 - DOUBLE_80618b00) / FLOAT_80618ae8)
                                       + FLOAT_80618b68;
                    }
                    if ((flags & 0x4000) != 0) {
                        uVar3 = (unsigned int)rng_counter_tick();
                        DAT_805caf20 = DAT_805caf20 + FLOAT_80618b58 *
                                       ((float)((double)(int)uVar3 - DOUBLE_80618b00) / FLOAT_80618ae8)
                                       + FLOAT_80618b54;
                    }
                }
            } else {
                if ((flags & 0x1000) != 0) {
                    uVar3 = (unsigned int)rng_counter_tick();
                    DAT_805caf18 = DAT_805caf18 + FLOAT_80618b58 *
                                   ((float)((double)(int)uVar3 - DOUBLE_80618b00) / FLOAT_80618ae8)
                                   + FLOAT_80618b54;
                }
                if ((flags & 0x2000) != 0) {
                    uVar3 = (unsigned int)rng_counter_tick();
                    DAT_805caf1c = DAT_805caf1c + FLOAT_80618b34 *
                                   ((float)((double)(int)uVar3 - DOUBLE_80618b00) / FLOAT_80618ae8)
                                   + FLOAT_80618b5c;
                }
                if ((flags & 0x4000) != 0) {
                    uVar3 = (unsigned int)rng_counter_tick();
                    DAT_805caf20 = DAT_805caf20 + FLOAT_80618b64 *
                                   ((float)((double)(int)uVar3 - DOUBLE_80618b00) / FLOAT_80618ae8)
                                   + FLOAT_80618b60;
                }
            }
        } else {
            iVar2 = iVar2 * 0x10;
            DAT_805caf18 = *(float *)(row[2] + iVar2);
            DAT_805caf1c = *(float *)(row[2] + iVar2 + 4);
            DAT_805caf20 = *(float *)(row[2] + iVar2 + 8);
            DAT_805caf24 = *(float *)(row[2] + iVar2 + 0xc);
        }
        iVar2 = *(int *)(row[1] + DAT_805cae24 * 0x18 + 0xc) * 0x10;
        DAT_805caf28 = *(int *)(row[2] + iVar2);
        DAT_805caf2c = *(int *)(row[2] + iVar2 + 4);
        DAT_805caf30 = *(int *)(row[2] + iVar2 + 8);
        DAT_805caf34 = *(int *)(row[2] + iVar2 + 0xc);
    }

    __restore_gpr_28();
}

// ============================================================================
// batch_721 additions: movement / steering / AI-target-select layer.
// Address range 0x80249c54 - 0x8024b3c0.
// ============================================================================

// ---- additional cross-module helpers ----
double math_sqrt(double x);
double math_atan2(double y, double x);
void   spline_eval_pos(double t, float *out, float *p0, float *p1, float *p2, float *p3);
double spline_eval_scalar(double t, double s0, double s1, double s2, double s3);
int    particle_handle_is_valid(int *handle);
int    soundpad_get_entity_id_from_zone(int zone);

// ---- additional context locks (enter returns WRTH ctx/entity ptr; leave) ----
extern int  FUN_803826d8(void);   // spline-follow context enter
extern void FUN_80382724(void);   // spline-follow context leave
extern int  FUN_803826c0(void);   // target-select context enter
extern void FUN_8038270c(void);   // target-select context leave

// ---- additional state globals ----
extern float DAT_805cadc8;   // path target yaw
extern int   DAT_805cae2c;   // capture-radius latch flag

// spline control-point table (parallel to PTR_DAT_8046d634; per-row data ptr)
extern int *PTR_DAT_8046d638;
// sin/cos lookup table (2048 entries, quarter-turn indexed)
extern float DAT_80583228;

// anchor position tables (vec4: x/y/z/yaw)
extern int DAT_8046d180;
extern int DAT_8046d184;
extern int DAT_8046d188;
extern int DAT_8046d18c;
extern int DAT_8046d1a0;
extern int DAT_8046d1a4;
extern int DAT_8046d1a8;
extern int DAT_8046d1ac;
extern int DAT_8046cf58;
extern int DAT_8046cf5c;
extern int DAT_8046cf60;
extern int DAT_8046cf64;

// additional FLOAT constants
extern float FLOAT_80618b6c;
extern float FLOAT_80618b70;
extern float FLOAT_80618b74;
extern float FLOAT_80618b78;
extern float FLOAT_80618b7c;
extern float FLOAT_80618b80;
extern float FLOAT_80618b84;
extern float FLOAT_80618b88;
extern float FLOAT_80618b8c;
extern float FLOAT_80618b90;
extern float FLOAT_80618b94;
extern float FLOAT_80618b98;
extern float FLOAT_80618b9c;
extern float FLOAT_80618ba0;
extern float FLOAT_80618ba4;

// ---- forward decls for batch_721 functions ----
void webnet_follow_spline_path(void);
void webnet_move_toward_target(void);
void webnet_aim_at_clamped_target(int entity);
void webnet_aim_pitch_at_point(int entity, float *pt);
void webnet_slew_angle_clamped(double target, double rate, double clampBand, float *angle);
void webnet_slew_angle(double target, double rate, float *angle);
void webnet_select_target_behavior(void);
void webnet_destroy_trcp(void);
int  webnet_get_trcp_position(int *outPos);
int  webnet_set_end_state(int outcome);

// ============================================================================
// 0x80249c54  webnet_follow_spline_path
// Evaluate the current keyframe's spline control points into the path target
// position (DAT_805cadcc/d0/d4) and yaw (DAT_805cadc8), and advance the spline
// parameter DAT_805cae20 by an arc-length ratio. Keyframe flag &4 snaps directly
// to the first point; flag &2 uses the jittered offset globals as control points,
// else the row's control-point table PTR_DAT_8046d638. Bracketed by FUN_803826d8.
// ============================================================================
void webnet_follow_spline_path(void)
{
    int    entity;
    int    row;
    int    kfOff;
    unsigned char *kf;
    unsigned char *dataBase;
    float *p0, *p1, *p2, *p3;
    float  dx, dy, dz;
    double segLen, targetDist;

    entity = FUN_803826d8();
    row = DAT_805cae1c;
    kfOff = DAT_805cae24 * 0x18;
    kf = (unsigned char *)(&PTR_DAT_8046d634)[DAT_805cae1c * 3];

    if ((*(unsigned int *)(kf + kfOff + 0x10) & 4) == 0) {
        if ((*(unsigned int *)(kf + kfOff + 0x10) & 2) == 0) {
            dataBase = (unsigned char *)(&PTR_DAT_8046d638)[DAT_805cae1c * 3];
            p0 = (float *)(dataBase + *(int *)(kf + kfOff) * 0x10);
            p1 = (float *)(dataBase + *(int *)(kf + kfOff + 4) * 0x10);
            p2 = (float *)(dataBase + *(int *)(kf + kfOff + 8) * 0x10);
            p3 = (float *)(dataBase + *(int *)(kf + kfOff + 0xc) * 0x10);
        } else {
            p0 = (float *)&DAT_805caef8;
            p1 = (float *)&DAT_805caf08;
            p2 = (float *)&DAT_805caf18;
            p3 = (float *)&DAT_805caf28;
        }
        spline_eval_pos((double)DAT_805cae20, &DAT_805cadcc, p0, p1, p2, p3);
        DAT_805cadc8 = (float)spline_eval_scalar((double)DAT_805cae20, (double)p0[3], (double)p1[3],
                                                 (double)p2[3], (double)p3[3]);
        if ((*(unsigned int *)((&PTR_DAT_8046d634)[row * 3] + DAT_805cae24 * 0x18 + 0x10) & 0x200) != 0) {
            DAT_805cadcc = DAT_805cadcc + DAT_805cadbc;
        }
        DAT_805cae00 = (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))
                       * *(float *)((&PTR_DAT_8046d634)[row * 3] + DAT_805cae24 * 0x18 + 0x14);
        dy = DAT_805cadd0 - *(float *)(entity + 0x68);
        dx = DAT_805cadcc - *(float *)(entity + 100);
        dz = DAT_805cadd4 - *(float *)(entity + 0x6c);
        double sx = (double)(p1[0] - p2[0]);
        double sy = (double)(p1[1] - p2[1]);
        double sz = (double)(p1[2] - p2[2]);
        targetDist = (double)(float)((double)FLOAT_80618b70 + math_sqrt((double)(dz * dz + dx * dx + dy * dy)));
        segLen = math_sqrt((double)(float)(sz * sz + (double)(float)(sx * sx + (double)(float)(sy * sy))));
        DAT_805cae20 = FLOAT_80618b6c * DAT_805cae00 * (float)(segLen / targetDist) + DAT_805cae20;
    } else {
        int i = *(int *)(kf + kfOff) * 0x10;
        dataBase = (unsigned char *)(&PTR_DAT_8046d638)[DAT_805cae1c * 3];
        DAT_805cadcc = *(float *)(dataBase + i);
        DAT_805cadd0 = *(float *)(dataBase + i + 4);
        DAT_805cadd4 = *(float *)(dataBase + i + 8);
        DAT_805cadc8 = *(float *)(dataBase + i + 0xc);
        DAT_805cae00 = (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))
                       * *(float *)(kf + kfOff + 0x14);
    }
    FUN_80382724();
}

// ============================================================================
// 0x80249efc  webnet_move_toward_target
// Steer the WRTH entity toward the current path target. Accumulates a normalized
// acceleration (DAT_805cadd8/dc/e0) scaled by DAT_805cadfc*dt with drag, clamps
// to the speed limit DAT_805cae00, integrates position, re-rolls the positional
// jitter (DAT_805cadf0/f4/f8), then derives facing/pitch from the velocity via a
// sin/cos table and orients the entity through the slew helpers. Entity in r28.
// ============================================================================
void webnet_move_toward_target(void)
{
    float  fVar1, fVar2, fVar3, fVar4, fVar5;
    int    entity;
    unsigned int flags;
    double dt;
    double vx, vy, vz, inv;
    double yaw, mag;
    unsigned int idx;

    entity = __save_gpr_28();
    fVar2 = *(float *)(entity + 0x68) - DAT_805cadd0;
    fVar1 = *(float *)(entity + 100)  - DAT_805cadcc;
    fVar3 = *(float *)(entity + 0x6c) - DAT_805cadd4;
    dt = (double)(DAT_805cadfc * FLOAT_806108ec);
    fVar2 = fVar3 * fVar3 + fVar1 * fVar1 + fVar2 * fVar2;
    if ((FLOAT_80618b20 <= fVar2) || (DAT_805cadb8 == 8)) {
        if ((FLOAT_80618b74 < fVar2) && (DAT_805cae2c == 0)) {
            DAT_805cae2c = 1;
        }
    } else {
        DAT_805cae2c = 0;
        if (DAT_805cadb8 == 5) {
            DAT_805cae28 = FLOAT_80618b34;
        }
    }
    if (DAT_805cae2c == 0) {
        DAT_805cae00 = DAT_805cae00 * fVar2 * FLOAT_80618b78;
    }

    fVar2 = DAT_805cadd0 - *(float *)(entity + 0x68);
    fVar1 = DAT_805cadcc - *(float *)(entity + 100);
    fVar3 = DAT_805cadd4 - *(float *)(entity + 0x6c);
    fVar4 = fVar3 * fVar3 + fVar1 * fVar1 + fVar2 * fVar2;
    if (FLOAT_80618b18 < fVar4) {
        fVar5 = 1.0f / SQRT(fVar4);
        fVar5 = fVar5 * -(FLOAT_80618aec * fVar4 * fVar5 * fVar5 - FLOAT_80618b1c);
        fVar4 = FLOAT_80618b20 * fVar5 * -(FLOAT_80618aec * fVar4 * fVar5 * fVar5 - FLOAT_80618b1c);
        fVar1 = fVar1 * fVar4; fVar2 = fVar2 * fVar4; fVar3 = fVar3 * fVar4;
    }
    vy = (double)(fVar2 + DAT_805cade8);
    vx = (double)(fVar1 + DAT_805cade4);
    vz = (double)(fVar3 + DAT_805cadec);
    fVar2 = (float)(vz * vz + (double)(float)(vx * vx + (double)(float)(vy * vy)));
    if (FLOAT_80618b18 < fVar2) {
        fVar1 = 1.0f / SQRT(fVar2);
        fVar1 = fVar1 * -(FLOAT_80618aec * fVar2 * fVar1 * fVar1 - FLOAT_80618b1c);
        inv = (double)(FLOAT_80618b20 * fVar1 * -(FLOAT_80618aec * fVar2 * fVar1 * fVar1 - FLOAT_80618b1c));
        vx = (double)(float)(vx * inv);
        vy = (double)(float)(vy * inv);
        vz = (double)(float)(vz * inv);
    }
    DAT_805cadd8 = DAT_805cadd8 + (float)(vx * dt);
    DAT_805caddc = DAT_805caddc + (float)(vy * dt);
    DAT_805cade0 = DAT_805cade0 + (float)(vz * dt);
    if (DAT_805cae2c == 0) {
        DAT_805cadd8 = DAT_805cadd8 * FLOAT_80618b7c;
        DAT_805caddc = DAT_805caddc * FLOAT_80618b7c;
        DAT_805cade0 = DAT_805cade0 * FLOAT_80618b7c;
    }
    mag = math_sqrt((double)(DAT_805cade0 * DAT_805cade0 + DAT_805cadd8 * DAT_805cadd8 + DAT_805caddc * DAT_805caddc));
    fVar2 = FLOAT_80618b80;
    if ((double)DAT_805cae00 < mag) {
        fVar3 = (float)(mag - dt);
        fVar1 = (float)((double)FLOAT_80618b20 / mag);
        DAT_805cadd8 = DAT_805cadd8 * fVar1 * fVar3;
        DAT_805caddc = DAT_805caddc * fVar1 * fVar3;
        DAT_805cade0 = DAT_805cade0 * fVar1 * fVar3;
    }
    *(float *)(entity + 100)  = *(float *)(entity + 100)  + DAT_805cadd8 * FLOAT_806108ec;
    *(float *)(entity + 0x68) = *(float *)(entity + 0x68) + DAT_805caddc * FLOAT_806108ec;
    *(float *)(entity + 0x6c) = *(float *)(entity + 0x6c) + DAT_805cade0 * FLOAT_806108ec;
    DAT_805cade4 = DAT_805cade4 * fVar2;
    DAT_805cade8 = DAT_805cade8 * fVar2;
    DAT_805cadec = DAT_805cadec * fVar2;
    *(float *)(entity + 100)  = *(float *)(entity + 100)  + DAT_805cadf0;
    *(float *)(entity + 0x68) = *(float *)(entity + 0x68) + DAT_805cadf4;
    *(float *)(entity + 0x6c) = *(float *)(entity + 0x6c) + DAT_805cadf8;

    // Re-roll positional jitter occasionally.
    if (FLOAT_80618b80 < (float)((double)(int)(unsigned int)rng_counter_tick() - DOUBLE_80618b00) / FLOAT_80618ae8) {
        unsigned int r0 = (unsigned int)rng_counter_tick();
        DAT_805cadf0 = FLOAT_80618ac8 * ((float)((double)(int)r0 - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
        unsigned int r1 = (unsigned int)rng_counter_tick();
        DAT_805cadf4 = FLOAT_80618ac8 * ((float)((double)(int)r1 - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
        unsigned int r2 = (unsigned int)rng_counter_tick();
        DAT_805cadf0 = DAT_805cadf0 * FLOAT_80618ae0;
        DAT_805cadf4 = DAT_805cadf4 * FLOAT_80618af0;
        DAT_805cadf8 = FLOAT_80618ac8 * ((float)((double)(int)r2 - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec) * FLOAT_80618af4;
    }

    // Facing yaw + pitch from velocity, via the sin/cos lookup table.
    yaw = math_atan2((double)(FLOAT_80618b84 + DAT_805cadd8), (double)DAT_805cade0);
    double yawDeg = (double)((FLOAT_80618ad0 * (float)yaw) / FLOAT_80618acc);
    mag = math_sqrt((double)((FLOAT_80618b84 + DAT_805cadd8) * (FLOAT_80618b84 + DAT_805cadd8) + DAT_805cade0 * DAT_805cade0));
    double ddown = (double)DAT_805cadd8;
    if (ddown < (double)FLOAT_80618ac4) {
        yawDeg = (double)(float)(yawDeg * (double)FLOAT_80618b08);
    }
    fVar2 = (float)(yawDeg - (double)*(float *)(entity + 0x70));
    while (fVar2 <= FLOAT_80618b88) { fVar2 = fVar2 + FLOAT_80618b50; }
    while (FLOAT_80618ad0 < fVar2) { fVar2 = fVar2 - FLOAT_80618b50; }
    idx = (unsigned int)(FLOAT_80618b8c * ((FLOAT_80618acc * fVar2) / FLOAT_80618ad0) + FLOAT_80618aec);
    double cosv = (double)((float)((double)FLOAT_80618adc * mag) * *(float *)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc)));
    double sinv = (double)((float)((double)FLOAT_80618adc * mag) * -(float)(&DAT_80583228)[idx & 0x7ff]);
    if (ddown < (double)FLOAT_80618ac4) {
        double sc = (double)(float)((double)FLOAT_80618b90 * ddown);
        double clamped = sc;
        if ((double)FLOAT_80618ac0 < sc) { clamped = (double)FLOAT_80618ac0; }
        if ((double)FLOAT_80618ac4 <= clamped) {
            if ((double)FLOAT_80618ac0 < sc) { sc = (double)FLOAT_80618ac0; }
        } else {
            sc = (double)FLOAT_80618ac4;
        }
        cosv = (double)(float)(cosv * sc);
    }

    flags = *(unsigned int *)((&PTR_DAT_8046d634)[DAT_805cae1c * 3] + DAT_805cae24 * 0x18 + 0x10);
    if ((flags & 8) == 0) {
        if ((flags & 0x40) == 0) {
            if (DAT_805cadb8 == 3) {
                yaw = math_atan2((double)(DAT_805cadbc - *(float *)(entity + 100)),
                                 (double)(DAT_805cadc4 - *(float *)(entity + 0x6c)));
                webnet_projectile_spawn_tick();
                webnet_slew_angle_clamped(cosv, (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                                          (double)FLOAT_80618b9c, (float *)(entity + 0xc0));
                fVar2 = FLOAT_80618b14;
                if (DAT_805cadb8 != 8) {
                    fVar2 = FLOAT_80618b1c * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8));
                }
                webnet_slew_angle((double)((float)((double)FLOAT_80618ad0 * (double)(float)yaw) / FLOAT_80618acc),
                                  (double)fVar2, (float *)(entity + 0x70));
                webnet_slew_angle_clamped(sinv, (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                                          (double)FLOAT_80618b38, (float *)(entity + 200));
            } else if ((flags & 0x10) == 0) {
                webnet_slew_angle_clamped(cosv, (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                                          (double)FLOAT_80618b9c, (float *)(entity + 0xc0));
                fVar2 = FLOAT_80618b14;
                if (DAT_805cadb8 != 8) {
                    fVar2 = FLOAT_80618b1c * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8));
                }
                webnet_slew_angle((double)DAT_805cadc8, (double)fVar2, (float *)(entity + 0x70));
                webnet_slew_angle_clamped(sinv, (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                                          (double)FLOAT_80618b38, (float *)(entity + 200));
            } else {
                webnet_aim_at_clamped_target(entity);
                webnet_slew_angle_clamped(sinv, (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                                          (double)FLOAT_80618b38, (float *)(entity + 200));
            }
        } else if (DAT_805cae1c == 9) {
            if ((*(int *)(DAT_80611d78 + 0x1400) == 7) ||
                ((1 < DAT_80612610) && (*(int *)(DAT_80611d78 + 0x281c) == 7))) {
                webnet_aim_at_clamped_target(entity);
                webnet_slew_angle_clamped(sinv, (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                                          (double)FLOAT_80618b38, (float *)(entity + 200));
            } else {
                webnet_slew_angle_clamped(cosv, (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                                          (double)FLOAT_80618b9c, (float *)(entity + 0xc0));
                fVar2 = FLOAT_80618b14;
                if (DAT_805cadb8 != 8) {
                    fVar2 = FLOAT_80618b1c * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8));
                }
                webnet_slew_angle((double)DAT_805cadc8, (double)fVar2, (float *)(entity + 0x70));
                webnet_slew_angle_clamped(sinv, (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                                          (double)FLOAT_80618b38, (float *)(entity + 200));
            }
        }
    } else {
        float pt[3];
        pt[0] = *(float *)(entity + 100);
        pt[1] = FLOAT_80618b08;
        pt[2] = FLOAT_80618b94;
        webnet_aim_pitch_at_point(entity, pt);
        fVar2 = FLOAT_80618b14;
        if (DAT_805cadb8 != 8) {
            fVar2 = FLOAT_80618b1c * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8));
        }
        webnet_slew_angle((double)DAT_805cadc8, (double)fVar2, (float *)(entity + 0x70));
        webnet_slew_angle_clamped(sinv, (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                                  (double)FLOAT_80618b38, (float *)(entity + 200));
    }

    __restore_gpr_28();
}

// ============================================================================
// 0x8024a968  webnet_aim_at_clamped_target
// Aim the boss at the sweep target, first clamping the target to a box around
// DAT_805cadbc/c4 (when keyframe flags 0x10000/0x20000 set), then slewing yaw and
// pitch toward it and firing the projectile tick.
// ============================================================================
void webnet_aim_at_clamped_target(int entity)
{
    float  tx, tz;
    double dx, dy, dz;
    double yaw, pitch, planar, easeIn, easedYaw;
    float  pitchRate;

    tx = DAT_805cadbc;
    if ((*(unsigned int *)((&PTR_DAT_8046d634)[DAT_805cae1c * 3] + DAT_805cae24 * 0x18 + 0x10) & 0x10000) != 0) {
        float cur = *(float *)(entity + 100);
        float hi = FLOAT_80618ba0 + DAT_805cadbc;
        float clamped = cur;
        if (hi < cur) { clamped = hi; }
        if (DAT_805cadbc - FLOAT_80618ba0 <= clamped) {
            tx = (hi < cur) ? hi : cur;
        } else {
            tx = DAT_805cadbc - FLOAT_80618ba0;
        }
    }
    tz = DAT_805cadc4;
    if ((*(unsigned int *)((&PTR_DAT_8046d634)[DAT_805cae1c * 3] + DAT_805cae24 * 0x18 + 0x10) & 0x20000) != 0) {
        float cur = *(float *)(entity + 0x6c);
        float hi = FLOAT_80618b1c + DAT_805cadc4;
        float lo = DAT_805cadc4 - FLOAT_80618b1c;
        float clamped = cur;
        if (hi < cur) { clamped = hi; }
        if (lo <= clamped) {
            tz = (hi < cur) ? hi : cur;
        } else {
            tz = lo;
        }
    }

    dx = (double)(tx - *(float *)(entity + 100));
    dz = (double)(tz - *(float *)(entity + 0x6c));
    dy = (double)(DAT_805cadc0 - *(float *)(entity + 0x68));
    yaw = math_atan2(dx, dz);
    double yawDelta = (double)((FLOAT_80618ad0 * (float)yaw) / FLOAT_80618acc - *(float *)(entity + 0x70));
    easeIn = FUN_80390324((double)((float)(yawDelta - (double)FLOAT_80618b88) / FLOAT_80618b50));
    easedYaw = (double)(*(float *)(entity + 0x70) + -(float)((double)FLOAT_80618b50 * (double)(float)easeIn - yawDelta));
    pitchRate = FLOAT_80618b9c;
    if (DAT_805cadb8 == 7) { pitchRate = FLOAT_80618b38; }
    planar = math_sqrt((double)(float)(dx * dx + (double)(float)(dz * dz)));
    pitch = math_atan2(dy, planar);
    webnet_slew_angle_clamped(-(double)((FLOAT_80618ad0 * (float)pitch) / FLOAT_80618acc),
                              (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                              (double)pitchRate, (float *)(entity + 0xc0));
    float yawRate = FLOAT_80618b14;
    if (DAT_805cadb8 != 8) {
        yawRate = FLOAT_80618b1c * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8));
    }
    webnet_slew_angle(easedYaw, (double)yawRate, (float *)(entity + 0x70));
    webnet_projectile_spawn_tick();
}

// ============================================================================
// 0x8024ac0c  webnet_aim_pitch_at_point
// Aim the boss pitch toward an explicit world point and fire the projectile tick.
// ============================================================================
void webnet_aim_pitch_at_point(int entity, float *pt)
{
    float  dx, dz;
    double dy, planar, pitch;

    dz = pt[2] - *(float *)(entity + 0x6c);
    dx = pt[0] - *(float *)(entity + 100);
    dy = (double)(pt[1] - *(float *)(entity + 0x68));
    planar = math_sqrt((double)(dx * dx + dz * dz));
    pitch = math_atan2(dy, planar);
    webnet_slew_angle_clamped(-(double)((FLOAT_80618ad0 * (float)pitch) / FLOAT_80618acc),
                              (double)(FLOAT_80618b98 * (FLOAT_80618ae4 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8))),
                              (double)FLOAT_80618b9c, (float *)(entity + 0xc0));
    webnet_projectile_spawn_tick();
}

// ============================================================================
// 0x8024acdc  webnet_slew_angle_clamped
// Slew an angle toward the target (webnet_slew_angle) then clamp the result to
// the band [clampBand, 360-clampBand] when clampBand > 0.
// ============================================================================
void webnet_slew_angle_clamped(double target, double rate, double clampBand, float *angle)
{
    double cur;
    webnet_slew_angle(target, rate, angle);
    if ((double)FLOAT_80618b08 < clampBand) {
        cur = (double)*angle;
        if ((double)FLOAT_80618ad0 <= cur) {
            if (cur < (double)(float)((double)FLOAT_80618b50 - clampBand)) {
                *angle = (float)((double)FLOAT_80618b50 - clampBand);
            }
        } else if (clampBand < cur) {
            *angle = (float)clampBand;
        }
    }
}

// ============================================================================
// 0x8024ad5c  webnet_slew_angle
// Core angle slew: wrap the target-current delta into (-180,180], then step the
// angle toward the target by rate*dt, or snap if within one step.
// ============================================================================
void webnet_slew_angle(double target, double rate, float *angle)
{
    float delta;
    float step;

    for (delta = (float)(target - (double)*angle); delta <= FLOAT_80618b88; delta = delta + FLOAT_80618b50) {}
    for (; FLOAT_80618ad0 < delta; delta = delta - FLOAT_80618b50) {}
    step = (float)(rate * (double)FLOAT_806108ec);
    if (step <= ABS(delta)) {
        if (delta <= FLOAT_80618ac4) {
            *angle = *angle - step;
        } else {
            *angle = *angle + step;
        }
        return;
    }
    *angle = (float)target;
}

// ============================================================================
// 0x8024ade0  webnet_select_target_behavior
// AI target/behaviour selector, bracketed by the target-select context lock
// (FUN_803826c0 / FUN_8038270c). Sweeps the player table and, based on each
// player's game-state field (+0x1400) and sound-pad zone, transitions the boss
// between behaviour phases (rise/sweep/co-op/grow) and re-anchors it to the
// matching position template. Skipped in phases 3 and 7..10.
// ============================================================================
void webnet_select_target_behavior(void)
{
    float fVar1;
    int   ctx;
    int   iVar2;
    int   playerRec, playerEnt;
    int   idx;
    int   pn;
    double zero;

    ctx = FUN_803826c0();
    if (DAT_805cadb8 != 5) {
        if (DAT_805cadb8 < 5) {
            if (DAT_805cadb8 == 3) goto leave;
        } else if ((DAT_805cadb8 < 0xb) && (6 < DAT_805cadb8)) {
            goto leave;
        }
        zero = (double)FLOAT_80618ac4;
        idx = 0;
        for (pn = 0; pn < DAT_80612610; pn++) {
            fVar1 = FLOAT_80618ac4;
            playerRec = DAT_80611d78 + idx;
            playerEnt = *(int *)(playerRec + 0x14);
            if (zero < (double)*(float *)(playerEnt + 0x1cc)) {
                iVar2 = *(int *)(playerRec + 0x1400);
                if (iVar2 == 8) {
                    if (DAT_805cadb8 != 6) {
                        *(int *)(ctx + 100)  = DAT_8046d268;
                        *(int *)(ctx + 0x68) = DAT_8046d26c;
                        *(int *)(ctx + 0x6c) = DAT_8046d270;
                        *(int *)(ctx + 0x70) = DAT_8046d274;
                        DAT_805cadb8 = 6;
                        DAT_805cae1c = 0xc;
                        DAT_805cae28 = (float)zero;
                        DAT_805cae20 = (float)zero;
                        DAT_805cae24 = 0;
                        FUN_8024b53c();
                    }
                } else if (iVar2 < 8) {
                    if (iVar2 == 6) {
                        if (DAT_805cadb8 != 4) {
                            if ((*(int *)(playerRec + 0x114) == 0x200) || (*(float *)(playerEnt + 100) < FLOAT_80618ba4)) {
                                if ((DAT_805015bd == 10) && (1 < DAT_80612610)) {
                                    if ((DAT_805cae1c != 4) && (DAT_805cae1c != 6)) {
                                        DAT_805cae1c = 6;
                                    }
                                    DAT_805cadb8 = 3;
                                } else if ((DAT_805cadb8 != 2) && (DAT_805cadb8 != 3)) {
                                    *(int *)(ctx + 100)  = DAT_8046d180;
                                    *(int *)(ctx + 0x68) = DAT_8046d184;
                                    *(int *)(ctx + 0x6c) = DAT_8046d188;
                                    *(float *)(ctx + 100) = *(float *)(ctx + 100) + DAT_805cadbc;
                                    *(int *)(ctx + 0x70) = DAT_8046d18c;
                                    DAT_805cadb8 = 2;
                                    DAT_805cae1c = 9;
                                    DAT_805cae20 = fVar1;
                                    DAT_805cae24 = 0;
                                    DAT_805cae28 = fVar1;
                                }
                            } else if ((DAT_805cadb8 != 3) ||
                                       ((playerRec = DAT_80611d78 + (1 - pn) * 0x141c,
                                         FLOAT_80618ba4 <= *(float *)(playerRec + 0xa8) &&
                                         (*(int *)(playerRec + 0x114) != 0x200)))) {
                                DAT_805cadb8 = 1;
                                DAT_805cae28 = FLOAT_80618ac4;
                                if ((8 < DAT_805cae1c) || (DAT_805cae1c < 3)) {
                                    int blk1 = entity_handle_resolve(0x424c4b31);
                                    int blk2 = entity_handle_resolve(0x424c4b32);
                                    if ((blk2 == 0) || (*(float *)(blk2 + 0x1cc) <= FLOAT_80618ac4)) {
                                        DAT_805cae1c = 8;
                                    } else if ((blk1 == 0) || (*(float *)(blk1 + 0x1cc) <= FLOAT_80618ac4)) {
                                        DAT_805cae1c = 6;
                                    } else {
                                        DAT_805cae1c = 3;
                                    }
                                    DAT_805cae24 = 0;
                                    DAT_805cae20 = FLOAT_80618ac4;
                                    FUN_8024b53c();
                                }
                            }
                        }
                    } else if (iVar2 < 6) {
                        if (4 < iVar2) {
                            int zoneEnt = soundpad_get_entity_id_from_zone(*(int *)(*(int *)(playerEnt + 0x7c) + 0x678));
                            fVar1 = FLOAT_80618ac4;
                            if (zoneEnt == 0x430) {
                                *(int *)(ctx + 100)  = DAT_8046cf58;
                                *(int *)(ctx + 0x68) = DAT_8046cf5c;
                                *(int *)(ctx + 0x6c) = DAT_8046cf60;
                                *(int *)(ctx + 0x70) = DAT_8046cf64;
                                DAT_805cadb8 = 1;
                                DAT_805cae1c = 2;
                                DAT_805cae20 = fVar1;
                                DAT_805cae24 = 0;
                                DAT_805cae28 = fVar1;
                            } else if ((zoneEnt < 0x430) && (0x42e < zoneEnt) && (DAT_805cadb8 != 1)) {
                                DAT_805cadb8 = 1;
                                DAT_805cae1c = 1;
                                DAT_805cae20 = FLOAT_80618ac4;
                                DAT_805cae24 = 0;
                                DAT_805cae28 = FLOAT_80618ac4;
                            }
                        }
                    } else if ((DAT_805cadb8 != 4) && (*(int *)(playerRec + 0x114) == 0x200)) {
                        if ((DAT_805015bd == 10) && (1 < DAT_80612610)) {
                            if ((DAT_805cae1c != 4) && (DAT_805cae1c != 6)) {
                                DAT_805cae1c = 6;
                            }
                            DAT_805cadb8 = 3;
                        } else if ((DAT_805cadb8 != 2) && (DAT_805cadb8 != 3)) {
                            *(int *)(ctx + 100)  = DAT_8046d1a0;
                            *(int *)(ctx + 0x68) = DAT_8046d1a4;
                            *(int *)(ctx + 0x6c) = DAT_8046d1a8;
                            *(int *)(ctx + 0x70) = DAT_8046d1ac;
                            *(float *)(ctx + 100) = *(float *)(ctx + 100) + DAT_805cadbc;
                            DAT_805cadb8 = 2;
                            DAT_805cae28 = fVar1;
                            DAT_805cae1c = 9;
                            DAT_805cae20 = fVar1;
                            DAT_805cae24 = 0;
                        }
                    }
                } else if ((iVar2 == 0x14) && (DAT_805cadb8 != 6)) {
                    *(int *)(ctx + 100)  = DAT_8046d268;
                    *(int *)(ctx + 0x68) = DAT_8046d26c;
                    *(int *)(ctx + 0x6c) = DAT_8046d270;
                    *(int *)(ctx + 0x70) = DAT_8046d274;
                    DAT_805cadb8 = 6;
                    DAT_805cae1c = 0xd;
                    DAT_805cae28 = (float)zero;
                    DAT_805cae20 = (float)zero;
                    DAT_805cae24 = 0;
                    FUN_8024b53c();
                }
            }
            idx = idx + 0x141c;
        }
    }
leave:
    FUN_8038270c();
}

// ============================================================================
// 0x8024b2f8  webnet_destroy_trcp
// Destroy the TRCP helper entity (0x54524350) and its capture particle emitter.
// ============================================================================
void webnet_destroy_trcp(void)
{
    int *trcp;
    int  handle;
    int  chk[4];

    trcp = (int *)entity_handle_resolve(0x54524350);
    if (trcp != (int *)0) {
        chk[0] = DAT_80612990;
        if (particle_handle_is_valid(chk)) {
            handle = DAT_80612990;
            particle_handle_destroy(&handle);
        }
        entity_destroy_with_event(trcp);
    }
}

// ============================================================================
// 0x8024b360  webnet_get_trcp_position
// Copy the TRCP entity's world position into outPos; returns 1 if TRCP exists.
// ============================================================================
int webnet_get_trcp_position(int *outPos)
{
    int trcp = entity_handle_resolve(0x54524350);
    if (trcp != 0) {
        outPos[0] = *(int *)(trcp + 100);
        outPos[1] = *(int *)(trcp + 0x68);
        outPos[2] = *(int *)(trcp + 0x6c);
    }
    return trcp != 0;
}

// ============================================================================
// 0x8024b3c0  webnet_set_end_state
// Set the encounter's end state by outcome code: 0 -> boss-variant victory
// (phase 10 done), 4 -> base-variant victory, 1 -> reset the TRCP entity to its
// start template and re-enter phase 1. Returns 1 (0 only if TRCP missing on reset).
// ============================================================================
int webnet_set_end_state(int outcome)
{
    int trcp;

    if (outcome != 2) {
        if (outcome < 2) {
            if (outcome == 0) {
                DAT_805cadb8 = 10;
                DAT_805cae28 = FLOAT_80618ac4;
                DAT_805cae24 = 0;
                DAT_805cae20 = FLOAT_80618ac4;
                DAT_805cae1c = 0x10;
                DAT_805cae30 = 1;
                DAT_805cae38 = 3;
            } else if (-1 < outcome) {
                trcp = entity_handle_resolve(0x54524350);
                if (trcp == 0) {
                    return 0;
                }
                *(int *)(trcp + 100)  = DAT_8046cf58;
                *(int *)(trcp + 0x68) = DAT_8046cf5c;
                *(int *)(trcp + 0x6c) = DAT_8046cf60;
                *(int *)(trcp + 0x70) = DAT_8046cf64;
                DAT_805cadb8 = 1;
                DAT_805cae1c = 2;
                DAT_805cae20 = FLOAT_80618ac4;
                DAT_805cae24 = 0;
                DAT_805cae28 = FLOAT_80618ac4;
            }
        } else if (outcome == 4) {
            DAT_805cadb8 = 10;
            DAT_805cae28 = FLOAT_80618ac4;
            DAT_805cae24 = 0;
            DAT_805cae20 = FLOAT_80618ac4;
            DAT_805cae1c = 0x10;
            DAT_805cae30 = 0;
            DAT_805cae38 = 3;
        }
    }
    return 1;
}

// ============================================================================
// batch_722 additions: FX/particle setup, defeat sequence, and helper utilities.
// Address range 0x8024b4f8 - 0x8024ce94.
// ============================================================================

// ---- additional cross-module helpers (particle system, anim, spawn, damage) ----
void particle_def_create(double a1, double a2, int *def, int a4, int a5, int a6, int a7, int a8);
void particle_def_set_size_keyframes(double a1, double a2, double a3, double a4, int c0, int c1, int a7);
void particle_def_set_gravity(double g, int mask);
void particle_def_set_size_start_end(double a1, double a2, double a3, double a4);
void particle_def_set_color_end(double a1, double a2, double a3, double a4);
void particle_def_set_velocity_ranges(double a1, double a2, double a3, double a4, double a5, double a6);
void particle_def_set_drag(double drag);
void particle_emitter_create(double a1, double a2, double a3, int *emitter, int *def, int a6, int a7);
void particle_emitter_set_color_range(double a1, double a2, double a3, double a4);
void particle_emitter_set_lifetime_range(double a1, double a2);
void particle_emitter_set_texture(int *tex);
void particle_emitter_set_scale(int *scale);
void particle_emitter_set_target(int *target);
void particle_overlay_create(double a1, double a2, double a3, double a4, double a5, int *ov, int *scale, int a8, int c0, int c1);
void particle_overlay_set_fade_params(double a1, int a2, int a3);
void particle_cached_set_position(int *rec, float *pos);
void particle_cached_set_velocity(int *rec, float *vel);
void particle_cached_set_size_range(double lo, double hi, int *rec);
void particle_cached_set_delay(double delay);
void particle_handle_clear(int *handle);
void particle_kill_by_entity(void);
void particle_spawn_random_variant(double a1, double a2, double a3, int a4, int *owner, int *pos, float *vel);
void mtx4_transform_vector(float *mtx, float *v);
void damage_melee_area_scan(double a1, double a2, double a3, double a4, double a5, double a6, double a7, double a8, int handle, int *params, int a11);
int  entity_spawn_with_activation(double a1, double a2, double a3, double a4, double a5, int a6, int a7, int a8, int a9, int a10, float *pos, float *vel, int a13, int a14);
int  anim_active_clip_slot(int entity);
void anim_get_frame_times(int slot, int *outA, int *outB);
int  anim_active_slot_userflags(int slot);

// ---- additional context locks (render/work/list; enter returns ctx ptr) ----
extern int  FUN_803826cc(void);   // render-list enter (returns WRTH ctx ptr)
extern void FUN_80382718(void);   // render-list leave
extern int  FUN_803826e8(void);   // work-ctx enter
extern void FUN_80382734(void);   // work-ctx leave
extern int  FUN_803826f0(void);   // list-lock enter (returns spawn ctx/pos ptr)
extern void FUN_8038273c(void);   // list-lock leave
extern int  __save_gpr_25(void);  // engaged-emitter prologue: returns WRTH entity ptr
extern void __restore_gpr_25(void);

// ---- particle handle blocks (each an int handle / small struct) ----
extern int DAT_805caf38;
extern int DAT_805cb004;
extern int DAT_805cb0d0;
extern int DAT_805cb108;
extern int DAT_805cb18c;
extern int DAT_805cb258;
extern int DAT_805cb2dc;
extern int DAT_805cb3a8;
extern int DAT_805cb42c;
extern int DAT_805cb4f8;
extern int DAT_805cb57c;
extern int DAT_805cb580;

// anchor / template data (float vectors)
extern float DAT_803e02f8;
extern float DAT_803e02fc;
extern float DAT_803e0300;
extern float DAT_803e0304;
extern float DAT_803e0308;
extern float DAT_803e030c;
extern float DAT_803e0314;
extern float DAT_803e0318;
extern float DAT_803e0320;
extern float DAT_803e0324;
extern float DAT_803e032c;
extern float DAT_803e0330;
extern float DAT_803e0338;
extern float DAT_803e033c;
extern float DAT_803e0340;
extern float DAT_803e0344;
extern float DAT_803e0348;
extern float DAT_803e034c;
extern float DAT_803e0350;
extern float DAT_803e0354;
extern float DAT_803e0358;
extern float DAT_803e035c;
extern float DAT_803e0360;
extern float DAT_803e0364;
extern float DAT_803e0368;
extern float DAT_803e036c;
extern float DAT_803e0370;
extern float DAT_803e0374;
extern float DAT_803e0378;
extern float DAT_803e037c;
extern float DAT_803e0380;
extern float DAT_803e0384;
extern float DAT_803e0388;
extern float DAT_803e038c;
extern float DAT_803e0390;
extern float DAT_803e0394;
extern float DAT_803e0398;
extern float DAT_803e039c;
extern float DAT_803e03a0;
extern float DAT_803e03a4;
extern float DAT_803e03a8;
extern float DAT_803e03ac;
extern float DAT_803e03b0;
extern float DAT_803e03b4;
extern float DAT_803e03b8;
extern float DAT_803e03bc;
extern float DAT_803e03c0;
extern float DAT_803e03c4;
extern float DAT_803e03c8;
extern float DAT_803e03cc;
extern float DAT_803e03d0;
extern float DAT_803e03d4;
extern float DAT_803e03d8;

// additional FLOAT constants
extern float FLOAT_80618ba8;
extern float FLOAT_80618bac;
extern float FLOAT_80618bb0;
extern float FLOAT_80618bb4;
extern float FLOAT_80618bb8;
extern float FLOAT_80618bbc;
extern float FLOAT_80618bc0;
extern float FLOAT_80618bc4;
extern float FLOAT_80618bc8;
extern float FLOAT_80618bcc;
extern float FLOAT_80618bd0;
extern float FLOAT_80618bd4;
extern float FLOAT_80618bd8;
extern float FLOAT_80618bdc;
extern float FLOAT_80618be0;
extern float FLOAT_80618be4;
extern float FLOAT_80618be8;
extern float FLOAT_80618bec;
extern float FLOAT_80618bf0;
extern float FLOAT_80618bf4;
extern float FLOAT_80618bf8;
extern float FLOAT_80618bfc;
extern float FLOAT_80618c00;
extern float FLOAT_80618c04;
extern float FLOAT_80618c08;
extern float FLOAT_80618c0c;
extern float FLOAT_80618c10;
extern float FLOAT_80618c14;
extern float FLOAT_80618c18;
extern float FLOAT_80618c1c;
extern float FLOAT_80618c20;

// ---- forward decls for batch_722 functions ----
int  webnet_outcome_to_status(int outcome);
void webnet_resolve_target_anchor(void);
void webnet_init_particle_effects(void);
void webnet_death_sequence(int a1, int a2, double p3, double p4, double p5, double p6, double p7, int a8);
void webnet_spawn_explosion_debris(void);
void webnet_spawn_debris_chunk(double a1, double scale, int id, int *pos, unsigned int mod);
void webnet_debris_chunk_tick(int *chunk);
void webnet_update_capture_emitter(int entity);
void webnet_update_engaged_emitters(void);
void webnet_spawn_engaged_emitters(void);

// ============================================================================
// 0x8024b4f8  webnet_outcome_to_status
// Remap an outcome code to a coarse status: 2 -> 1, 0/1 -> 3, 2/3 (>=2,<4) -> 4,
// else 0.
// ============================================================================
int webnet_outcome_to_status(int outcome)
{
    if (outcome == 2) {
        return 1;
    }
    if (outcome < 2) {
        if (-1 < outcome) {
            return 3;
        }
    } else if (outcome < 4) {
        return 4;
    }
    return 0;
}

// ============================================================================
// 0x8024b53c  webnet_resolve_target_anchor
// Resolve the boss target/anchor position (DAT_805cadbc/c0/c4). Finds the nearest
// player (min field +0xa8); in sweep phase 3 tracks that player, otherwise sets
// the target from per-timeline-row anchor templates (DAT_803e02f8..) or the
// CZS1/CZS2 escort entities with axis clamps. Bracketed by FUN_803826cc/80382718.
// ============================================================================
void webnet_resolve_target_anchor(void)
{
    float fVar1, fVar2, fVar3, fVar4;
    bool  bVar5;
    int   ctx;
    int   nearest;
    int   idx;
    int   pn;
    int   remaining;
    int   playerEnt;

    ctx = FUN_803826cc();
    fVar4 = DAT_803e033c;
    fVar3 = DAT_803e0338;
    nearest = 0;
    pn = 0;
    idx = 0;
    remaining = DAT_80612610;
    fVar1 = FLOAT_80618ba8;
    if (0 < DAT_80612610) {
        do {
            fVar2 = *(float *)(DAT_80611d78 + idx + 0xa8);
            if (fVar2 < fVar1) {
                nearest = pn;
                fVar1 = fVar2;
            }
            pn = pn + 1;
            idx = idx + 0x141c;
            remaining = remaining + -1;
        } while (remaining != 0);
    }

    if (DAT_805cadb8 == 3) {
        playerEnt = *(int *)(DAT_80611d78 + nearest * 0x141c + 0x14);
        DAT_805cadbc = *(float *)(playerEnt + 100);
        DAT_805cadc0 = *(float *)(playerEnt + 0x68);
        DAT_805cadc4 = *(float *)(playerEnt + 0x6c);
    } else {
        switch (DAT_805cae1c) {
        case 3:
            DAT_805cadbc = DAT_803e02f8; DAT_805cadc0 = DAT_803e02fc; DAT_805cadc4 = DAT_803e0300;
            break;
        case 4:
            DAT_805cadbc = DAT_803e0304; DAT_805cadc0 = DAT_803e0308; DAT_805cadc4 = DAT_803e030c;
            break;
        case 6:
            DAT_805cadbc = *(float *)(*(int *)(DAT_80611d78 + nearest * 0x141c + 0x14) + 100);
            DAT_805cadc0 = DAT_803e0314; DAT_805cadc4 = DAT_803e0318;
            break;
        case 8: {
            fVar1 = *(float *)(*(int *)(DAT_80611d78 + nearest * 0x141c + 0x14) + 100);
            fVar3 = fVar1;
            if (FLOAT_80618bcc < fVar1) { fVar3 = FLOAT_80618bcc; }
            DAT_805cadbc = FLOAT_80618bc8;
            if ((FLOAT_80618bc8 <= fVar3) && (DAT_805cadbc = fVar1, FLOAT_80618bcc < fVar1)) {
                DAT_805cadbc = FLOAT_80618bcc;
            }
            DAT_805cadc0 = DAT_803e0320; DAT_805cadc4 = DAT_803e0324;
            break;
        }
        case 9: {
            playerEnt = *(int *)(DAT_80611d78 + nearest * 0x141c + 0x14);
            DAT_805cadbc = *(float *)(playerEnt + 100);
            DAT_805cadc0 = *(float *)(playerEnt + 0x68);
            DAT_805cadc4 = *(float *)(playerEnt + 0x6c);
            break;
        }
        case 0xc:
        case 0xd:
            DAT_805cadbc = FLOAT_80618bd0 + *(float *)(ctx + 100);
            DAT_805cadc0 = DAT_803e032c; DAT_805cadc4 = DAT_803e0330;
            break;
        case 0xe:
            if (DAT_805cae30 == 0) {
                bVar5 = false;
                int czs = entity_handle_resolve(0x435a5331);
                if ((czs != 0) && (*(int *)(*(int *)(czs + 0x7c) + 4) == ctx)) {
                    fVar1 = *(float *)(ctx + 100);
                    fVar2 = fVar1;
                    if (FLOAT_80618bc4 < fVar1) { fVar2 = FLOAT_80618bc4; }
                    DAT_805cadbc = FLOAT_80618bc0;
                    if ((FLOAT_80618bc0 <= fVar2) && (DAT_805cadbc = fVar1, FLOAT_80618bc4 < fVar1)) {
                        DAT_805cadbc = FLOAT_80618bc4;
                    }
                    bVar5 = true;
                    DAT_805cadc0 = fVar3; DAT_805cadc4 = fVar4;
                }
                if (!bVar5) {
                    DAT_805cadc4 = *(float *)(*(int *)(DAT_80611d78 + 0x14) + 0x6c);
                    DAT_805cadbc = *(float *)(*(int *)(DAT_80611d78 + 0x14) + 100);
                    DAT_805cadc0 = FLOAT_80618b08;
                }
            } else if (DAT_805cae34 == 0) {
                int czs = entity_handle_resolve(0x435a5332);
                if (czs != 0) {
                    fVar1 = *(float *)(ctx + 100);
                    fVar4 = fVar1;
                    if (FLOAT_80618bb8 < fVar1) { fVar4 = FLOAT_80618bb8; }
                    fVar2 = FLOAT_80618bb4;
                    if ((FLOAT_80618bb4 <= fVar4) && (fVar2 = fVar1, FLOAT_80618bb8 < fVar1)) {
                        fVar2 = FLOAT_80618bb8;
                    }
                    DAT_805cadbc = fVar2 - DAT_805cae04;
                    DAT_805cadc0 = fVar3; DAT_805cadc4 = FLOAT_80618bbc;
                }
            } else {
                fVar1 = *(float *)(*(int *)(DAT_80611d78 + 0x14) + 100);
                DAT_805cadc4 = *(float *)(*(int *)(DAT_80611d78 + 0x14) + 0x6c);
                fVar3 = fVar1;
                if (FLOAT_80618bb0 < fVar1) { fVar3 = FLOAT_80618bb0; }
                fVar4 = FLOAT_80618bac;
                if ((FLOAT_80618bac <= fVar3) && (fVar4 = fVar1, FLOAT_80618bb0 < fVar1)) {
                    fVar4 = FLOAT_80618bb0;
                }
                DAT_805cadc0 = FLOAT_80618b08;
                DAT_805cadbc = fVar4 - DAT_805cae04;
            }
            break;
        }
    }
    FUN_80382718();
}

// ============================================================================
// 0x8024b9a8  webnet_init_particle_effects
// Build the five web-net particle definitions + emitters (capture puff, trail,
// spark, smoke, debris) and the projected overlay, seeding the shared handle
// blocks (DAT_805caf38/cb004/cb18c/cb2dc/cb42c and overlay DAT_805cb0d0). Called
// once from webnet_entity_spawn.
// ============================================================================
void webnet_init_particle_effects(void)
{
    double dVar1;
    int    ovScale[3];   // local_1c/18/14
    int    emScale[3];   // local_28/24/20

    ovScale[0] = *(int *)&DAT_803e0340;
    ovScale[1] = *(int *)&DAT_803e0344;
    ovScale[2] = *(int *)&DAT_803e0348;
    emScale[0] = *(int *)&DAT_803e034c;
    emScale[1] = *(int *)&DAT_803e0350;
    emScale[2] = *(int *)&DAT_803e0354;

    // Def 1: capture puff.
    particle_def_create((double)FLOAT_80618bd4, (double)FLOAT_80618b20, &DAT_805caf38, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80618b20, (double)FLOAT_80618ac4, (double)FLOAT_80618ac4,
                                    (double)FLOAT_80618bd8, 0xffa00000, 0x202020ff, 0);
    particle_def_set_gravity((double)FLOAT_80618bdc, 0x7f);
    particle_def_set_size_start_end((double)FLOAT_80618ad0, (double)FLOAT_80618b50, (double)FLOAT_80618b10, (double)FLOAT_80618b10);
    particle_def_set_color_end((double)FLOAT_80618b70, (double)FLOAT_80618be0, (double)FLOAT_80618b44, (double)FLOAT_80618ac4);
    particle_def_set_velocity_ranges((double)FLOAT_80618b20, (double)FLOAT_80618be4, (double)FLOAT_80618b2c, (double)FLOAT_80618b10, (double)FLOAT_80618b20, (double)FLOAT_80618bd4);
    particle_def_set_drag((double)FLOAT_80618be8);
    particle_emitter_create((double)FLOAT_80618bd4, (double)FLOAT_80618ac4, (double)FLOAT_80618af0, &DAT_805cae74, &DAT_805caf38, 0, 0x4000);
    particle_emitter_set_color_range((double)FLOAT_80618bec, (double)FLOAT_80618ac4, (double)FLOAT_80618bd0, (double)FLOAT_80618ac4);
    particle_emitter_set_lifetime_range((double)FLOAT_80618bec, (double)FLOAT_80618ac4);

    // Def 2: trail (with overlay).
    particle_def_create((double)FLOAT_80618bd4, (double)FLOAT_80618b20, &DAT_805cb004, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80618b20, (double)FLOAT_80618ac4, (double)FLOAT_80618ac4, (double)FLOAT_80618bd8, 0xffa00000, 0xff, 0);
    particle_def_set_size_start_end((double)FLOAT_80618ad0, (double)FLOAT_80618b50, (double)FLOAT_80618b10, (double)FLOAT_80618b10);
    particle_def_set_color_end((double)FLOAT_80618b70, (double)FLOAT_80618be0, (double)FLOAT_80618b44, (double)FLOAT_80618ac4);
    particle_def_set_velocity_ranges((double)FLOAT_80618ac0, (double)FLOAT_80618be4, (double)FLOAT_80618b2c, (double)FLOAT_80618b10, (double)FLOAT_80618b20, (double)FLOAT_80618bd4);
    particle_overlay_create((double)FLOAT_80618bf0, (double)FLOAT_80618bf4, (double)FLOAT_80618bf8, (double)FLOAT_80618b84, (double)FLOAT_80618b84, &DAT_805cb0d0, ovScale, 0x290, 0xffdc8cff, 0xffa03c00);
    particle_overlay_set_fade_params((double)FLOAT_80618bf0, 0, 0x7f7f7f1e);
    particle_emitter_create((double)FLOAT_80618b48, (double)FLOAT_80618ac8, (double)FLOAT_80618b20, &DAT_805cb108, &DAT_805cb004, 0, 0x4000);
    particle_emitter_set_color_range((double)FLOAT_80618bd0, (double)FLOAT_80618ac4, (double)FLOAT_80618bd0, (double)FLOAT_80618ac4);
    particle_emitter_set_lifetime_range((double)FLOAT_80618bd0, (double)FLOAT_80618ac4);
    particle_emitter_set_texture(&DAT_805cb0d0);

    // Def 3: spark.
    particle_def_create((double)FLOAT_80618b20, (double)FLOAT_80618b20, &DAT_805cb18c, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80618b20, (double)FLOAT_80618ac4, (double)FLOAT_80618ac4, (double)FLOAT_80618b7c, 0xff, 0x80808ff, 0);
    particle_def_set_size_start_end((double)FLOAT_80618ad0, (double)FLOAT_80618b50, (double)FLOAT_80618b10, (double)FLOAT_80618b10);
    particle_def_set_color_end((double)FLOAT_80618b70, (double)FLOAT_80618be0, (double)FLOAT_80618bfc, (double)FLOAT_80618ac4);
    particle_def_set_velocity_ranges((double)FLOAT_80618b20, (double)FLOAT_80618be4, (double)FLOAT_80618bd4, (double)FLOAT_80618b10, (double)FLOAT_80618b20, (double)FLOAT_80618bd4);
    particle_emitter_create((double)FLOAT_80618ac4, (double)FLOAT_80618ac4, (double)FLOAT_80618af0, &DAT_805cb258, &DAT_805cb18c, 0, 0x4000);
    particle_emitter_set_color_range((double)FLOAT_80618c00, (double)FLOAT_80618ac4, (double)FLOAT_80618b48, (double)FLOAT_80618b70);
    particle_emitter_set_lifetime_range((double)FLOAT_80618ac4, (double)FLOAT_80618ac4);

    // Def 4: smoke.
    particle_def_create((double)FLOAT_80618b20, (double)FLOAT_80618b20, &DAT_805cb2dc, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80618b84, (double)FLOAT_80618ac4, (double)FLOAT_80618ac4, (double)FLOAT_80618bf0, 0xff, 0x101010ff, 0);
    particle_def_set_size_start_end((double)FLOAT_80618ad0, (double)FLOAT_80618b50, (double)FLOAT_80618b10, (double)FLOAT_80618b10);
    particle_def_set_color_end((double)FLOAT_80618b70, (double)FLOAT_80618be0, (double)FLOAT_80618bfc, (double)FLOAT_80618ac4);
    particle_def_set_velocity_ranges((double)FLOAT_80618aec, (double)FLOAT_80618be4, (double)FLOAT_80618bec, (double)FLOAT_80618b10, (double)FLOAT_80618b20, (double)FLOAT_80618bd4);
    particle_def_set_drag((double)FLOAT_80618adc);
    particle_emitter_create((double)FLOAT_80618aec, (double)FLOAT_80618ac4, (double)FLOAT_80618af0, &DAT_805cb3a8, &DAT_805cb2dc, 0, 0);
    dVar1 = (double)FLOAT_80618ac4;
    particle_emitter_set_color_range(dVar1, dVar1, (double)FLOAT_80618bec, dVar1);
    particle_emitter_set_lifetime_range((double)FLOAT_80618ac4, (double)FLOAT_80618ac4);

    // Def 5: debris.
    particle_def_create((double)FLOAT_80618b7c, (double)FLOAT_80618b20, &DAT_805cb42c, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80618b20, (double)FLOAT_80618ac4, (double)FLOAT_80618ac4, (double)FLOAT_80618aec, 0xcf800000, 0x202020ff, 0x30200000);
    particle_def_set_gravity((double)FLOAT_80618b7c, 0x7f);
    particle_def_set_size_start_end((double)FLOAT_80618ad0, (double)FLOAT_80618b50, (double)FLOAT_80618b10, (double)FLOAT_80618b10);
    particle_def_set_color_end((double)FLOAT_80618b70, (double)FLOAT_80618be0, (double)FLOAT_80618b5c, (double)FLOAT_80618ac4);
    particle_def_set_velocity_ranges((double)FLOAT_80618c04, (double)FLOAT_80618be4, (double)FLOAT_80618bd4, (double)FLOAT_80618b10, (double)FLOAT_80618b20, (double)FLOAT_80618bd4);
    particle_def_set_drag((double)FLOAT_80618c08);
    particle_emitter_create((double)FLOAT_80618adc, (double)FLOAT_80618ac4, (double)FLOAT_80618c0c, &DAT_805cb4f8, &DAT_805cb42c, 2, 1);
    particle_emitter_set_color_range((double)FLOAT_80618b2c, (double)FLOAT_80618ac4, (double)FLOAT_80618be8, (double)FLOAT_80618ac4);
    particle_emitter_set_lifetime_range((double)FLOAT_80618b2c, (double)FLOAT_80618ac4);
    particle_emitter_set_scale(emScale);
}

// ============================================================================
// 0x8024be98  webnet_death_sequence
// Boss defeat sequence (phase 9). When the HLC2 death anim (clip 0xba1) passes
// its trigger frame (>0x176), spawns the big explosion + debris, kills lingering
// particles, deals the finishing area damage (BLKC), and emits the member-part
// spark bursts; on the terminal anim flag it destroys the TRCP entity and stops
// the loop sound. Bracketed by the work-context lock FUN_803826e8/FUN_80382734.
// ============================================================================
void webnet_death_sequence(int a1, int a2, double p3, double p4, double p5, double p6, double p7, int a8)
{
    int    hlc2;
    int   *trcp;
    int    lptr;
    int   *clip;
    float *bone;
    unsigned int i;
    int    frA, frB;
    double dVar7;
    float  burstPos[3];   // local_8c/88/84
    float  vel[3];        // local_74/70/6c
    int    dmgParams[3];  // local_68/64/60
    float  tgt[3];        // local_80/7c/78
    float  ring[5][3];    // local_98/94/90 + local_a4/a0/9c ...
    float  explPos[3];    // local_5c
    int    sndBuf[20];
    int    hClear;

    FUN_803826e8();
    hlc2 = entity_handle_resolve(0x484c4332);
    trcp = (int *)entity_handle_resolve(0x54524350);
    lptr = entity_handle_resolve(0x4c505452);

    if ((trcp != (int *)0) && (hlc2 != 0) &&
        (clip = (int *)anim_active_clip_slot((int)trcp), *clip == 0xba1)) {
        anim_get_frame_times((int)clip, &frA, &frB);
        if ((0x176 < frA) && (*(int *)(*(int *)(hlc2 + 4) + 0x30) != 2)) {
            dmgParams[0] = *(int *)&DAT_803e0364;
            dmgParams[1] = *(int *)&DAT_803e0368;
            dmgParams[2] = *(int *)&DAT_803e036c;
            tgt[0] = DAT_803e035c;
            *(int *)&tgt[1] = *(int *)&DAT_803e0360;
            vel[0] = DAT_803e0370;
            *(int *)&vel[1] = *(int *)&DAT_803e0374;
            *(int *)&vel[2] = *(int *)&DAT_803e0378;
            explPos[0] = DAT_803e0358 + *(float *)(lptr + 100);
            webnet_spawn_explosion_debris();
            if ((DAT_805015bd != 10) || (DAT_80612610 < 2)) {
                player_ai_explosion_spawn(0x1e, 0, explPos, vel, 0, 0, 1);
            }
            particle_kill_by_entity();
            *(int *)(*(int *)(hlc2 + 4) + 0x30) = 2;
            entity_register_for_update(hlc2, 3, 0);
            hClear = DAT_80612990;
            particle_handle_destroy(&hClear);
            damage_melee_area_scan((double)FLOAT_80618bd0, (double)FLOAT_80618ac4, p3, p4, p5, p6, p7,
                                   (double)a8, 0x424c4b43, dmgParams, 9);
            dVar7 = (double)FLOAT_80618ac4;
            if ((DAT_805015bd != 10) || (DAT_80612610 < 2)) {
                float set98[3];
                set98[0] = DAT_803e0388; set98[1] = DAT_803e038c; set98[2] = DAT_803e0390;
                float setA4[3];
                setA4[0] = DAT_803e0394; setA4[1] = DAT_803e0398; setA4[2] = DAT_803e039c;
                tgt[0] = DAT_803e037c; *(int *)&tgt[1] = *(int *)&DAT_803e0380; *(int *)&tgt[2] = *(int *)&DAT_803e0384;
                i = 0;
                do {
                    int *piVar4 = *(int **)(*(int *)(hlc2 + 4) + 4);
                    bone = (float *)(piVar4[*piVar4 * -0x30 + ((int)i >> 0x10) * 0x30 + 0x1b] + (i & 0xffff) * 0x18);
                    burstPos[0] = bone[0] + *(float *)(hlc2 + 100);
                    burstPos[1] = bone[1] + *(float *)(hlc2 + 0x68);
                    burstPos[2] = bone[2] + *(float *)(hlc2 + 0x6c);
                    particle_emitter_spawn(0, &DAT_805cb258, 0, burstPos, vel);
                    particle_emitter_set_target((int *)tgt);
                    particle_cached_set_delay((double)FLOAT_80618b20);
                    i = i + 1;
                } while ((int)i < 5);
                burstPos[0] = set98[0] + *(float *)(hlc2 + 100);
                burstPos[1] = set98[1] + *(float *)(hlc2 + 0x68);
                burstPos[2] = set98[2] + *(float *)(hlc2 + 0x6c);
                particle_emitter_spawn(0, &DAT_805cb258, 0, burstPos, vel);
                particle_emitter_set_target((int *)tgt);
                particle_cached_set_delay((double)FLOAT_80618b20);
                burstPos[0] = setA4[0] + *(float *)(hlc2 + 100);
                burstPos[1] = (float)((double)setA4[1] + (double)*(float *)(hlc2 + 0x68));
                burstPos[2] = setA4[2] + *(float *)(hlc2 + 0x6c);
                particle_emitter_spawn(0, &DAT_805cb258, 0, burstPos, vel);
                particle_emitter_set_target((int *)tgt);
                particle_cached_set_delay((double)FLOAT_80618b20);
                dVar7 = (double)setA4[1];
            }
            statlvl_class_complete((double)FLOAT_80618ac4, dVar7, p3, p4, p5, p6, p7, (double)a8);
        }
        if (anim_active_slot_userflags(trcp[1]) != 0) {
            int j = 0;
            for (int k = 0; k < 2; k++) {
                int h1 = *(int *)((int)&DAT_805cb57c + j);
                particle_handle_destroy(&h1);
                int h2 = *(int *)((int)&DAT_805cb580 + j);
                particle_handle_destroy(&h2);
                j = j + 8;
            }
            sound_set_voice_volume_scale(0);
            sound_stop_handle();
            trcp[0x8b] = -1;
            sound_init_play_params((int)sndBuf);
            sndBuf[0] = 0x39c;
            sound_play_request(sndBuf);
            entity_destroy_with_event(trcp);
        }
    }
    (void)a1; (void)a2; (void)ring;
    FUN_80382734();
}

// ============================================================================
// 0x8024c29c  webnet_spawn_explosion_debris
// Anchored at the LPTR entity: spawn the main defeat explosion(s) and a burst of
// 0x14 (or 0 in co-op) rock-debris chunks (ids 0x377/0x378/0x379).
// ============================================================================
void webnet_spawn_explosion_debris(void)
{
    unsigned char coop;
    int   lptr;
    int   count;
    float vel[3];    // local_28/24/20
    float pos[3];    // local_1c/18/14

    lptr = entity_handle_resolve(0x4c505452);
    pos[1] = DAT_803e03a4;
    *(int *)&pos[2] = *(int *)&DAT_803e03a8;
    vel[0] = DAT_803e03ac;
    *(int *)&vel[1] = *(int *)&DAT_803e03b0;
    *(int *)&vel[2] = *(int *)&DAT_803e03b4;
    pos[0] = DAT_803e03a0 + *(float *)(lptr + 100);
    player_ai_explosion_spawn(0x1d, 0, pos, vel, 0, 0, 1);
    if ((DAT_805015bd != 10) || (DAT_80612610 < 2)) {
        player_ai_explosion_spawn(0x1f, 0, pos, vel, 0, 0, 1);
    }
    count = 0;
    while (true) {
        coop = 0;
        if ((DAT_805015bd == 10) && (1 < DAT_80612610)) {
            coop = 1;
        }
        if ((int)(~-(unsigned int)coop & 0x14) <= count) break;
        webnet_spawn_debris_chunk((double)FLOAT_80618b2c, (double)FLOAT_80618b30, 0x377, (int *)pos, 5);
        webnet_spawn_debris_chunk((double)FLOAT_80618b2c, (double)FLOAT_80618b30, 0x378, (int *)pos, 5);
        webnet_spawn_debris_chunk((double)FLOAT_80618b2c, (double)FLOAT_80618b30, 0x379, (int *)pos, 5);
        count = count + 1;
    }
}

// ============================================================================
// 0x8024c418  webnet_spawn_debris_chunk
// Spawn one rock-debris chunk entity: randomized velocity (scaled by `scale`),
// random spin, and a 3-in-5 chance of a homing arc toward the source point; the
// chunk is registered to update via webnet_debris_chunk_tick. Bracketed by the
// list lock FUN_803826f0/FUN_8038273c. The incoming f1 registers a drag scalar.
// ============================================================================
void webnet_spawn_debris_chunk(double a1, double scale, int id, int *pos, unsigned int mod)
{
    float  fVar1, fVar2, fVar3, fVar4;
    int    chunk;
    unsigned int r;
    float *src;
    float  seed[3];   // local_78/74/70
    double dragScalar = a1;

    src = (float *)FUN_803826f0();

    r = (unsigned int)rng_counter_tick();
    seed[0] = FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
    r = (unsigned int)rng_counter_tick();
    seed[1] = FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
    r = (unsigned int)rng_counter_tick();
    double dScale = (double)FLOAT_80618c10;
    double rz = (double)((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
    seed[0] = (float)((double)seed[0] * dScale);
    seed[1] = (float)((double)seed[1] * dScale);
    double vz = (double)(float)((double)FLOAT_80618ac8 * rz);
    seed[2] = (float)(vz * dScale);

    chunk = entity_spawn_with_activation((double)seed[1], (double)seed[0], dScale, vz, rz,
                                         0, 0, 0, 1, 0, src, seed, 0, 0);
    if (chunk != 0) {
        *(float *)(chunk + 0xe0) = seed[0];
        *(float *)(chunk + 0xe4) = seed[1];
        *(float *)(chunk + 0xe8) = seed[2];
        *(float *)(chunk + 0xe0) = (float)((double)*(float *)(chunk + 0xe0) * scale);
        *(float *)(chunk + 0xe4) = (float)((double)*(float *)(chunk + 0xe4) * scale);
        *(float *)(chunk + 0xe8) = (float)((double)*(float *)(chunk + 0xe8) * scale);
        r = (unsigned int)rng_counter_tick();
        *(float *)(chunk + 0xd0) = (float)((double)(int)(r - (r / mod) * mod) - DOUBLE_80618b00);
        r = (unsigned int)rng_counter_tick();
        *(float *)(chunk + 0xb4) = FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
        r = (unsigned int)rng_counter_tick();
        *(float *)(chunk + 0xb8) = FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
        r = (unsigned int)rng_counter_tick();
        *(float *)(chunk + 0xbc) = FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
        r = (unsigned int)rng_counter_tick();
        if ((r % 5 < 3) && (mod != 1)) {
            *(float *)(chunk + 100) = FLOAT_80618c14;
            r = (unsigned int)rng_counter_tick();
            *(float *)(chunk + 0x68) = FLOAT_80618ac0 * (FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8) - FLOAT_80618b20) + FLOAT_80618b68;
            r = (unsigned int)rng_counter_tick();
            fVar2 = FLOAT_80618b20;
            fVar3 = FLOAT_80618b18;
            fVar1 = FLOAT_80618ac8;
            *(float *)(chunk + 0x6c) = FLOAT_80618bd4 * (FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8) - FLOAT_80618b20);
            *(float *)(chunk + 0xb4) = *(float *)(chunk + 100) - src[0];
            *(float *)(chunk + 0xb8) = *(float *)(chunk + 0x68) - src[1];
            *(float *)(chunk + 0xbc) = *(float *)(chunk + 0x6c) - src[2];
            *(float *)(chunk + 0xb8) = *(float *)(chunk + 0xb8) + fVar1;
            fVar1 = *(float *)(chunk + 0xb4);
            fVar4 = *(float *)(chunk + 0xbc) * *(float *)(chunk + 0xbc) + fVar1 * fVar1 + *(float *)(chunk + 0xb8) * *(float *)(chunk + 0xb8);
            if (fVar3 < fVar4) {
                fVar3 = 1.0f / SQRT(fVar4);
                fVar3 = fVar3 * -(FLOAT_80618aec * fVar4 * fVar3 * fVar3 - FLOAT_80618b1c);
                fVar2 = fVar2 * fVar3 * -(FLOAT_80618aec * fVar4 * fVar3 * fVar3 - FLOAT_80618b1c);
                *(float *)(chunk + 0xb4) = fVar1 * fVar2;
                *(float *)(chunk + 0xb8) = *(float *)(chunk + 0xb8) * fVar2;
                *(float *)(chunk + 0xbc) = *(float *)(chunk + 0xbc) * fVar2;
            }
        } else {
            *(float *)(chunk + 0xb8) = *(float *)(chunk + 0xb8) + FLOAT_80618b20;
        }
        *(float *)(chunk + 0xb4) = (float)((double)*(float *)(chunk + 0xb4) * dragScalar);
        *(float *)(chunk + 0xb8) = (float)((double)*(float *)(chunk + 0xb8) * dragScalar);
        *(float *)(chunk + 0xbc) = (float)((double)*(float *)(chunk + 0xbc) * dragScalar);
        entity_register_for_update(chunk, 3, -0x7fdb37d0);
    }
    (void)id; (void)pos;
    FUN_8038273c();
}

// ============================================================================
// 0x8024c830  webnet_debris_chunk_tick
// Per-frame update for a rock-debris chunk: emit a smoke/spark trail while its
// timer (+0xd0) is zero, integrate drift + spin (fields +0x2d/2e/2f, +0x30/1c/32)
// and gravity into its position (+0x19/1a/1b), and self-destruct once it falls
// below the kill height (FLOAT_80618c20).
// ============================================================================
void webnet_debris_chunk_tick(int *chunk)
{
    double dVar1;
    float  fVar2, fVar3;
    float  spawn[3];    // local_68/64/60
    unsigned int r, r2;

    if (FLOAT_80618ac4 == *(float *)&chunk[0x34]) {
        r = (unsigned int)rng_counter_tick();
        spawn[0] = FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
        r = (unsigned int)rng_counter_tick();
        spawn[1] = FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec);
        r = (unsigned int)rng_counter_tick();
        spawn[0] = spawn[0] * FLOAT_80618b84;
        spawn[1] = spawn[1] * FLOAT_80618b84;
        spawn[2] = FLOAT_80618ac8 * ((float)((double)(int)r - DOUBLE_80618b00) / FLOAT_80618ae8 - FLOAT_80618aec) * FLOAT_80618b84;
        particle_spawn_random_variant((double)FLOAT_80618c18, (double)FLOAT_80618ac4, (double)FLOAT_80618b98, 1, chunk, chunk + 0x19, spawn);
    }
    fVar3 = FLOAT_80618c1c;
    fVar2 = FLOAT_80618b20;
    dVar1 = DOUBLE_80618af8;

    *(float *)&chunk[0x2d] = -(FLOAT_80618b70 * (FLOAT_80618b88 + *(float *)&chunk[0x2d]) - *(float *)&chunk[0x2d]);
    *(float *)&chunk[0x2e] = -(fVar3 * (float)((double)(int)(DAT_806108e4 ^ 0x80000000) - dVar1) *
                              (fVar2 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - dVar1)) - *(float *)&chunk[0x2e]);
    spawn[2] = (float)((double)(int)(DAT_806108e4 ^ 0x80000000) - dVar1) *
               (fVar2 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - dVar1));
    spawn[0] = *(float *)&chunk[0x2d] * spawn[2];
    spawn[1] = *(float *)&chunk[0x2e] * spawn[2];
    spawn[2] = *(float *)&chunk[0x2f] * spawn[2];
    *(float *)&chunk[0x19] = *(float *)&chunk[0x19] + spawn[0];
    *(float *)&chunk[0x1a] = *(float *)&chunk[0x1a] + spawn[1];
    *(float *)&chunk[0x1b] = *(float *)&chunk[0x1b] + spawn[2];
    *(float *)&chunk[0x30] = (fVar2 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - dVar1)) *
                             *(float *)&chunk[0x38] * (float)((double)(int)(DAT_806108e4 ^ 0x80000000) - dVar1) + *(float *)&chunk[0x30];
    fVar3 = FLOAT_80618c20;
    *(float *)&chunk[0x1c] = (fVar2 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - dVar1)) *
                             *(float *)&chunk[0x39] * (float)((double)(int)(DAT_806108e4 ^ 0x80000000) - dVar1) + *(float *)&chunk[0x1c];
    *(float *)&chunk[0x32] = (fVar2 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - dVar1)) *
                             *(float *)&chunk[0x3a] * (float)((double)(int)(DAT_806108e4 ^ 0x80000000) - dVar1) + *(float *)&chunk[0x32];
    chunk[0xd] = 0xd5;
    (void)r2;
    if (*(float *)&chunk[0x1a] < fVar3) {
        entity_destroy_with_event(chunk);
    }
}

// ============================================================================
// 0x8024cb4c  webnet_update_capture_emitter
// While the capture particle emitter (DAT_80612990) is alive, track it to the
// boss capture bone, grow its charge (+0x74) over time (capped at 1.0), and drive
// its render scale + size range from the charge level.
// ============================================================================
void webnet_update_capture_emitter(int entity)
{
    float fVar1;
    int  *piVar4;
    int   boneRec;
    double dVar5, dVar6, dVar7;
    int   chk;
    float pos[3];   // local_2c/28/24
    int   h;

    chk = DAT_80612990;
    if (particle_handle_is_valid(&chk)) {
        piVar4 = *(int **)(*(int *)(entity + 4) + 4);
        boneRec = piVar4[*piVar4 * -0x30 + 0x1b];
        pos[0] = *(float *)(boneRec + 0x30) + *(float *)(entity + 100);
        pos[1] = (*(float *)(boneRec + 0x34) + *(float *)(entity + 0x68)) - FLOAT_80618aec;
        pos[2] = *(float *)(boneRec + 0x38) + *(float *)(entity + 0x6c);
        h = DAT_80612990;
        particle_cached_set_position(&h, pos);
        fVar1 = FLOAT_80618b20;
        *(float *)(DAT_80612990 + 0x74) = *(float *)(DAT_80612990 + 0x74) +
            ((FLOAT_80618b20 / (float)((double)(int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618af8)) *
             (float)((double)(int)(DAT_806108e4 ^ 0x80000000) - DOUBLE_80618af8)) / FLOAT_80618adc;
        if (fVar1 < *(float *)(DAT_80612990 + 0x74)) {
            *(float *)(DAT_80612990 + 0x74) = fVar1;
        }
        dVar7 = (double)FLOAT_80618c04;
        *(float *)(*(int *)(*(int *)(DAT_80612990 + 0xc) + 0x4c) + 0x28) = *(float *)(DAT_80612990 + 0x74) / FLOAT_80618bd0;
        dVar6 = (double)*(float *)(DAT_80612990 + 0x74);
        if (dVar7 < dVar6) { dVar7 = dVar6; }
        dVar5 = (double)FLOAT_80618c04;
        if (dVar5 < dVar6) { dVar5 = dVar6; }
        h = DAT_80612990;
        particle_cached_set_size_range(dVar5, dVar7, &h);
    }
}

// ============================================================================
// 0x8024ccb8  webnet_update_engaged_emitters
// Per-frame positioning of the two pairs of "engaged" capture emitters
// (DAT_805cb57c / DAT_805cb580) at transformed offsets from the boss entity while
// the web-net is engaged (DAT_805cae38 > 0). Entity ptr in r25.
// ============================================================================
void webnet_update_engaged_emitters(void)
{
    int   entity;
    bool  valid;
    int  *piVar3;
    int   pair;
    int   off;
    float p[3];    // local_34/30/2c
    float v[3];    // local_40/3c/38
    int   h;

    entity = __save_gpr_25();
    if ((entity != 0) && (0 < DAT_805cae38)) {
        pair = 0;
        off = 0;
        do {
            p[0] = DAT_803e03b8; p[1] = DAT_803e03bc; p[2] = DAT_803e03c0;
            v[0] = DAT_803e03c4; v[1] = DAT_803e03c8; v[2] = DAT_803e03cc;
            if (pair == 1) {
                p[0] = -DAT_803e03b8;
            }
            mtx4_transform_vector((float *)(entity + 0x274), p);
            mtx4_transform_vector((float *)(entity + 0x274), v);
            piVar3 = (int *)((int)&DAT_805cb57c + off);
            p[0] = p[0] + *(float *)(entity + 100);
            p[1] = p[1] + *(float *)(entity + 0x68);
            p[2] = p[2] + *(float *)(entity + 0x6c);
            h = *piVar3;
            if (particle_handle_is_valid(&h)) {
                h = *piVar3;
                particle_cached_set_position(&h, p);
                h = *piVar3;
                particle_cached_set_velocity(&h, v);
            }
            v[0] = -v[0]; v[1] = -v[1]; v[2] = -v[2];
            piVar3 = (int *)((int)&DAT_805cb580 + off);
            p[0] = p[0] + v[0];
            p[1] = p[1] + v[1];
            p[2] = p[2] + v[2];
            h = *piVar3;
            if (particle_handle_is_valid(&h)) {
                h = *piVar3;
                particle_cached_set_position(&h, p);
                h = *piVar3;
                particle_cached_set_velocity(&h, v);
            }
            pair = pair + 1;
            off = off + 8;
        } while (pair < 2);
    }
    __restore_gpr_25();
}

// ============================================================================
// 0x8024ce94  webnet_spawn_engaged_emitters
// Spawn a pair of engaged capture emitters (from the debris def DAT_805cb4f8) for
// the current engaged sub-phase slot (DAT_805cae38) and position them. Entity in r28.
// ============================================================================
void webnet_spawn_engaged_emitters(void)
{
    int   slot;
    float pos[3];   // local_28/24/20

    __save_gpr_28();
    slot = 0;
    pos[0] = DAT_803e03d0;
    *(int *)&pos[1] = *(int *)&DAT_803e03d4;
    *(int *)&pos[2] = *(int *)&DAT_803e03d8;
    if (DAT_805cae38 == 0) {
        slot = 1;
    } else if (1 < DAT_805cae38) {
        __restore_gpr_28();
        return;
    }
    particle_handle_clear(&DAT_805cb57c + slot * 2);
    particle_handle_clear(&DAT_805cb580 + slot * 2);
    particle_emitter_spawn(&DAT_805cb57c + slot * 2, &DAT_805cb4f8, 0x1000, pos, pos);
    particle_emitter_spawn(&DAT_805cb580 + slot * 2, &DAT_805cb4f8, 0x1000, pos, pos);
    webnet_update_engaged_emitters();
    __restore_gpr_28();
}

} // extern "C"
