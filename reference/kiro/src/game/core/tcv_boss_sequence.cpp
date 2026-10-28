// tcv_boss_sequence.cpp
// TCV boss finale / countdown "screen-effect" entity controller.
// This entity drives the multi-part "TCV" boss set-piece: a global phase/state
// selector (DAT_805cb5c4) plus a countdown timer (DAT_805cb5cc / entity+0x1cc)
// that ends in a big staged explosion. It owns a screen-effect overlay slot, two
// particle-emitter effect definitions, and a per-frame aim/damage sweep against
// the TCV member entities.
//
// Phase selector DAT_805cb5c4:
//   0 idle/armed, 1 pre-roll, 9 active-countdown (progress bar shown on HUD),
//   10 finished/detonated. FUN_8024cf68 is the phase setter (phase, arg).
//
// TCV entity family (FourCC handles):
//   TCV1=0x54435631, TCV2=0x54435632, TCV3=0x54435633, TCV4=0x54435634,
//   TCV5=0x54435635, TCV6=0x54435636, TCV7=0x54435637.
//
// HUD hook: tcv_progress_fraction (0x8024cf84) is queried by
//   hud_objective_status_dispatch to draw the countdown/progress bar.
// Class descriptor table at 0x8044ec48 wires tcv_entity_spawn (0x8024d2e8) as the
//   spawn handler and tcv_setup_render (0x8024d010) as the init handler.
//
// Address range: 0x8024cf68 - 0x8024dbe8
//
// Self-contained translation unit (no shared-header include): all raw symbols and
// helper prototypes are declared file-locally with extern "C" linkage matching the
// ROM's unmangled symbols. Ghidra-style intrinsics (SQRT) are file-local inlines.

typedef unsigned char      undefined1;
typedef unsigned int       undefined4;
typedef unsigned long long undefined8;
typedef unsigned int       uint;
typedef long long          longlong;
typedef unsigned long long ulonglong;

static inline float SQRT(float x) { return __builtin_sqrtf(x); }

extern "C" {
// ---- named cross-module helpers (file-local, ROM signatures per call site) ----
int  entity_handle_resolve(int handle);
void entity_register_for_update(int entity, int slot, int callback);
void entity_set_render_type(int entity, void *callback);
void entity_destroy_with_event(void *entity);
int  screen_effect_alloc_slot(void);
void screen_effect_init_slot(int slot, int entity);
void particle_kill_by_entity(void);
void sound_set_voice_volume_scale(int scale);
void sound_stop_handle(void);
void sound_init_play_params(int params);
int  sound_play_request(int *params);
int  switch_slot_reset(int ctx);
void damage_dispatch_to_entity(double a1, double a2, double a3, double a4, double a5,
                               double a6, double a7, undefined8 a8, int *params);
double math_asin(double x);
void player_ai_projectile_jitter_init(float *state);

// particle effect-definition / emitter builder API (from the particle subsystem)
void particle_def_create(double a1, double a2, void *tag, int a4, int a5, int a6, int id, int a8);
void particle_def_set_size_keyframes(double a1, double a2, double a3, double a4,
                                     int color0, int color1, int a7);
void particle_def_set_size_start_end(double a1, double a2, double a3, double a4);
void particle_def_set_color_end(double r, double g, double b, double a);
void particle_def_set_velocity_ranges(double a1, double a2, double a3, double a4,
                                      double a5, double a6);
void particle_def_set_drag(double drag);
void particle_def_set_keyframes_raw(int a1, int a2, int count);
void particle_emitter_create(double a1, double a2, double a3, void *emitterOut,
                             void *def, int a6, int flags);
void particle_emitter_set_color_range(double a1, double a2, double a3, double a4);
void particle_emitter_set_lifetime_range(double a1, double a2);
void particle_emitter_set_velocity_range(double a1, double a2);
void particle_emitter_set_scale(void *scale);

// ---- render/anim list context enter/leave (batch_723 range) ----
extern int  FUN_803826dc(void);   // particle-list context enter
extern void FUN_80382728(void);   // particle-list context leave
extern unsigned long long FUN_803826e8(void);   // aim-tick context enter (returns packed target|self)
extern void FUN_80382734(void);   // aim-tick context leave
extern int  __save_gpr_28(void);  // init/reset prologue: returns entity ctx ptr
extern void __restore_gpr_28(void);
extern double FUN_80390324(double x);  // smoothstep-ish easing

// ---- TCV member-explosion helpers defined in later batches (file-local externs) ----
extern "C" void FUN_802520cc(float *pos);   // 0x802520cc explosion @pos
extern "C" void FUN_8025212c(void);          // 0x8025212c explosion (TCV1)
extern "C" void FUN_802521b0(void);          // 0x802521b0 burst (TCV5)
extern "C" void FUN_8025224c(void);          // 0x8025224c burst (TCV6)
extern "C" void FUN_802522e8(void);          // 0x802522e8 burst (TCV7)

// ---- forward prototypes for batch_724 functions defined below (referenced by
//      the batch_723 tick/finish functions that appear earlier in this file) ----
void tcv_render_tick_cb(undefined8 a1, undefined8 a2, double a3, undefined8 a4, undefined8 a5,
                        undefined8 a6, undefined8 a7, undefined8 a8, int *param9);  // 0x8024dcec
void tcv_big_finale(undefined8 a1, undefined8 a2, double a3, undefined8 a4, undefined8 a5,
                    undefined8 a6, undefined8 a7, undefined8 a8);                    // 0x8024ea90
void tcv_kick_capture_emitter(double amount, int entity);                            // 0x8024ea34

// ---- raw globals (file-local externs typed per call site) ----
extern int   DAT_805cb5c4;   // phase/state selector
extern int   DAT_805cb5cc;   // countdown accumulator (int, sign-flipped for CONCAT44)
extern int   DAT_805cb5d0;   // phase arg / secondary state
extern int   DAT_805cb5f0;   // reset-on-phase-change flag
extern int   DAT_805cb5ec;   // finale-once guard
extern int   DAT_805cb590;   // ptr to anim state block A (&DAT_8046d978)
extern int   DAT_805cb594;   // ptr to anim state block B (&DAT_8046d948)
extern int   DAT_805cb598;   // ptr to member-index block (&DAT_8046d8d0)
extern int   DAT_805cb59c;   // ptr to block D (&DAT_8046daf4)
extern int   DAT_805cb5bc;
extern int   DAT_805cb5c8;
extern int   DAT_805cb5a0;
extern int   DAT_805cb5d4;
extern int   DAT_805cb5dc;
extern int   DAT_805cb5e8;
extern int   DAT_805cb5b4;
extern float DAT_805cb5a8;
extern float DAT_805cb5d8;
extern float DAT_805cb5b0;
extern float DAT_805cb5b8;
extern int   DAT_80611d74;   // active player/session record base
extern int   DAT_805015bf;   // difficulty selector (0=easy,1,2,3+)
extern int   DAT_806109c0;   // frame/tick counter (int)

extern float DAT_805cb894;   // jitter-state array base (stride 0x1c, 4 entries)
extern char  DAT_805cb5f4;   // particle def A tag/buffer
extern char  DAT_805cb6c0;   // emitter A buffer
extern char  DAT_805cb744;   // particle def B tag/buffer
extern char  DAT_805cb810;   // emitter B buffer

extern int   DAT_8046daa8;
extern int   DAT_8046d970;
extern int   DAT_8046d958;
extern float DAT_8046d974;
extern float DAT_8046d95c;
extern int   DAT_8046d978;
extern int   DAT_8046d948;
extern int   DAT_8046d8d0;
extern int   DAT_8046daf4;

// particle-keyframe source palette (copied into local frames)
extern int   DAT_803e03e0, DAT_803e03e4, DAT_803e03e8, DAT_803e03ec;
extern int   DAT_803e03f0, DAT_803e03f4, DAT_803e03f8, DAT_803e03fc;
extern int   DAT_803e0400, DAT_803e0404, DAT_803e0408;

// sine/cosine lookup table (0x800 entries of float, mask 0x7ff)
extern float DAT_80583228[];

// float / double literal pool
extern float FLOAT_80618c28, FLOAT_80618c2c, FLOAT_80618c30;
extern float FLOAT_80618c40, FLOAT_80618c44, FLOAT_80618c48, FLOAT_80618c4c, FLOAT_80618c50;
extern float FLOAT_80618c54, FLOAT_80618c58, FLOAT_80618c5c, FLOAT_80618c60, FLOAT_80618c64;
extern float FLOAT_80618c68, FLOAT_80618c6c, FLOAT_80618c70, FLOAT_80618c74, FLOAT_80618c78;
extern float FLOAT_80618c7c, FLOAT_80618c80, FLOAT_80618c84, FLOAT_80618c88, FLOAT_80618c8c;
extern float FLOAT_80618c90, FLOAT_80618c94, FLOAT_80618c98, FLOAT_80618c9c, FLOAT_80618ca0;
extern float FLOAT_80618ca4, FLOAT_80618ca8, FLOAT_80618cac, FLOAT_80618cb0, FLOAT_80618cb4;
extern float FLOAT_80618cb8, FLOAT_80618cbc, FLOAT_80618cc0, FLOAT_80618cc4, FLOAT_80618cc8;
extern double DOUBLE_80618c38;

// ---- additional symbols referenced by batch_724 (per-frame ticks / FSM) ----
// render/anim/collision-list context enter/leave used by the tick + effect code
extern int  FUN_803826f0(void);   // fx/particle-list context enter (returns ctx pair)
extern void FUN_8038273c(void);   // fx/particle-list context leave
// helpers defined in later batches (particle/skeleton subsystem 0x8024ec.. / 0x8025..)
extern int  FUN_8024eef8(float *pos);     // 0x8024eef8 point-in-arena-region test (ret 0/1)
extern int  FUN_8024eec4(void);           // 0x8024eec4 target-below-threshold test
extern int  FUN_8024ee7c(void);           // 0x8024ee7c target-in-region test (defined this batch)
extern void FUN_8024ed2c(int member);     // 0x8024ed2c debris-pair spawn (defined this batch)
extern void FUN_8024ef88(void);           // 0x8024ef88 phase-change on-enter handler (later batch)
extern void FUN_8024f118(void);           // 0x8024f118 per-member spin smoothing (defined later)
extern void FUN_8024f23c(void);           // 0x8024f23c per-node collision face build (later)
extern void FUN_80251468(void);           // 0x80251468 root-bone matrix chain rebuild (later)
extern void FUN_802512f0(void);           // 0x802512f0 per-node arm transform commit (later)
extern void FUN_802500a0(void);           // 0x802500a0 per-arm aim/point update (later)
extern void FUN_8024fadc(void);           // 0x8024fadc per-arm strike/collision update (later)
extern void FUN_8025041c(void);           // 0x8025041c pre-arm accumulate (later)
extern void FUN_802506a8(void);           // 0x802506a8 arm color/mode commit (later)
extern void FUN_802509dc(void);           // 0x802509dc post-arm update A (later)
extern void FUN_802510d4(void);           // 0x802510d4 post-arm update B (later)
extern void FUN_802515d8(void);           // 0x802515d8 post-arm update C (later)
extern int  FUN_80251718(void);           // 0x80251718 activation gate (ret nonzero when armed)
extern void FUN_802517b0(undefined8 a1, undefined8 a2, double a3, undefined8 a4, undefined8 a5,
                         undefined8 a6, undefined8 a7, undefined8 a8, undefined4 a9, undefined4 a10);

// ---- additional helpers/globals referenced by batch_725 (arm sub-updates) ----
extern int  mtx4_pool_alloc(void);
extern void collision_set_face_entry(undefined4 a1, undefined4 a2, undefined4 mtx);
extern void particle_cached_set_drag(double drag, int *handle);
extern int  player_ai_projectile_spawn_simple(double a1, double a2, int type, void *pos, int *dir,
                                              int owner, int *a7, int a8, int a9, int a10, int projId,
                                              int a12);
extern void player_ai_projectile_compute_jittered_dir(double a1, double a2, float *state, float *out);
extern void player_ai_spawn_trail_particle(int a1, int a2, int a3, float *pos, float *dir, int id);
extern void scalar_step_toward_snap(double target, double rate, float *value);
extern int *entity_get_world_position(int entity);
extern void sound_update_3d_source(double a1, double a2, double a3, int handlePtr, int *pos,
                                   int id, int a7);
extern double math_sqrt(double x);
extern float FLOAT_806108ec;   // per-frame time step (float form of DAT_806108e4)
extern int   DAT_805cb5a4;     // arm-fire cooldown state
extern float FLOAT_80618cf8, FLOAT_80618cfc, FLOAT_80618d00, FLOAT_80618d04, FLOAT_80618d08;
extern float FLOAT_80618d0c, FLOAT_80618d10, FLOAT_80618d14, FLOAT_80618d18, FLOAT_80618d1c;
extern float FLOAT_80618d20, FLOAT_80618d24, FLOAT_80618d28;

// named cross-module helpers used by batch_724
extern int  entity_spawn_with_activation(double a1, double a2, double a3, undefined8 a4,
                                         undefined8 a5, undefined8 a6, undefined8 a7, undefined8 a8,
                                         int a9, int id, int posPtr, float *desc, int a13, int a14);
extern void player_ai_explosion_spawn(int id, int a2, float *pos, float *vel, int a5, int a6, int a7);
extern void player_ai_projectile_jitter_tick(double dt, undefined4 *state);
extern void player_ai_projectile_count_targeted(int owner, undefined4 kind, int *out);
extern void particle_cached_disable(int *handle);
extern void collision_set_layer_mask(int *shape, int mask);
extern void collision_forward_shape_data(int a1, int data);
extern void mtx4_table_reset_identity(void);
extern void mtx4_set_rotation_y(double angle, float *mtx);
extern void mtx4_transform_vector(float *mtx, float *vec);
extern void sound_play_stereo_pair(double a1, double a2, double a3);
extern void anim_set_blend_weight(double w, int node);
extern void statlvl_class_complete(double a1, double a2, double a3, double a4,
                                   undefined8 a5, undefined8 a6, undefined8 a7, undefined8 a8);
extern int  entity_handle_resolve(int handle);
extern double math_asin(double x);

// ---- additional globals referenced by batch_724 ----
extern int   DAT_806108e4;   // per-frame delta-ticks (frame time step)
extern int   DAT_805cb5e0;   // active target/guide entity ptr
extern int   DAT_805cb5c0;   // arm-active gate flag
extern float DAT_805cb5ac;   // root-bone z offset
extern int   DAT_80611928;   // trail stamp index / modulus source
extern int   DAT_80611e2c;   // default particle-handle fallback
extern float DAT_8046db08;   // arena region table base (stride 0x10, x0/z0 pairs)
extern int   iRam80610f14;   // FUN_802517b0 pass-through arg B
extern int   DAT_80610f10;   // FUN_802517b0 pass-through arg A
extern float DAT_805cb6fc;   // capture-emitter charge scale

// DAT_803e03.. / 04.. keyframe + offset palette (batch_724 range)
extern int   DAT_803e040c, DAT_803e0410, DAT_803e0414;
extern int   DAT_803e0418, DAT_803e041c, DAT_803e0420;
extern int   DAT_803e0424, DAT_803e0428, DAT_803e042c;
extern int   DAT_803e0430, DAT_803e0434, DAT_803e0438;
extern int   DAT_803e043c, DAT_803e0440, DAT_803e0444;
extern int   DAT_803e0448, DAT_803e044c, DAT_803e0450;

// float literal pool (batch_724 additions)
extern float FLOAT_80618ccc, FLOAT_80618cd0, FLOAT_80618cd4, FLOAT_80618cd8, FLOAT_80618cdc;
extern float FLOAT_80618ce0, FLOAT_80618ce4, FLOAT_80618ce8, FLOAT_80618cec, FLOAT_80618cf0;
extern float FLOAT_80618cf4;

// ---- additional symbols referenced by batch_726 (arm aim physics + scan) ----
extern double math_atan2(double y, double x);
extern double math_poly_trig(double x);   // 3D-osc curve helper
extern int    rng_counter_tick(void);
extern void   mtx4_set_rotation_zyx(double rz, double ry, double rx, float *mtx);
extern void   mtx4_set_rotation_x(double rx, int mtx);
extern void   mtx4_transform_point(float *mtx, float *pt);
extern void   mtx4_multiply(float *a, float *b, float *out);
extern int    DAT_80612610;   // active player count
extern int    DAT_80611d78;   // player record table base (stride 0x141c)
extern float  DAT_805cb5e4;   // best-target aim error scalar
extern float  DAT_803e0454, DAT_803e0458, DAT_803e045c;   // strike offset A
extern float  DAT_803e0460, DAT_803e0464, DAT_803e0468;   // strike offset B
extern float  DAT_803e046c, DAT_803e0470, DAT_803e0474;   // aim offset
extern int    DAT_803e0478, DAT_803e047c, DAT_803e0480;   // phase8 target point
extern float  FLOAT_80618d2c, FLOAT_80618d30, FLOAT_80618d34, FLOAT_80618d38, FLOAT_80618d3c;
extern float  FLOAT_80618d40, FLOAT_80618d50, FLOAT_80618d54, FLOAT_80618d58, FLOAT_80618d5c;
extern float  FLOAT_80618d60, FLOAT_80618d64, FLOAT_80618d68, FLOAT_80618d6c, FLOAT_80618d70;
extern double DOUBLE_80618d48;
}

// Ghidra count-leading-zeros intrinsic (self-contained; common.h not included)
static inline unsigned int countLeadingZeros(unsigned int x)
{
    if (x == 0) return 32u;
    unsigned int n = 0;
    while ((x & 0x80000000u) == 0u) { x <<= 1; n++; }
    return n;
}

// ---------------------------------------------------------------------------
// tcv_set_phase (0x8024cf68)
// Set the boss phase/state selector and its argument, and clear the
// reset-on-change flag. Called with (9,0) to enter active countdown and (10,0)
// when the finale detonates.
// ---------------------------------------------------------------------------
void tcv_set_phase(undefined4 phase, undefined4 arg)
{
    DAT_805cb5c4 = (int)phase;
    DAT_805cb5d0 = (int)arg;
    DAT_805cb5f0 = 0;
}

// ---------------------------------------------------------------------------
// tcv_progress_fraction (0x8024cf84)
// HUD objective query: returns the countdown progress bar fraction. During the
// active phase (9) the fraction is (1 - elapsed/total) clamped to a base value;
// otherwise it returns the default full value FLOAT_80618c30.
// ---------------------------------------------------------------------------
double tcv_progress_fraction(void)
{
    float fVar1;
    float fVar2;

    fVar2 = FLOAT_80618c30;
    if (DAT_805cb5c4 == 9) {
        // total-countdown ticks (frame counter delta, sign-flipped CONCAT44 int->double)
        fVar1 = FLOAT_80618c28 *
                (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38);
        float elapsed =
            (float)((double)(longlong)(unsigned)(DAT_805cb5cc ^ 0x80000000) - DOUBLE_80618c38);
        if (elapsed < fVar1) {
            fVar2 = FLOAT_80618c2c - elapsed / fVar1;
        }
    }
    return (double)fVar2;
}

// ---------------------------------------------------------------------------
// tcv_setup_render (0x8024d010)
// Init handler (class descriptor @0x8044ec48): install the TCV render callback,
// flag the entity for the render pass (+0x28 |= 0x10), and for the TCV5/6/7
// variants also set the child-node visible bit (+4->+0x30 |= 1).
// ---------------------------------------------------------------------------
extern "C" void tcv_render_callback(undefined4 *entity);  // fwd (0x8024d0c0)

void tcv_setup_render(int entity, int *fourcc)
{
    entity_set_render_type(entity, (void *)tcv_render_callback);
    *(uint *)(entity + 0x28) = *(uint *)(entity + 0x28) | 0x10;
    if (*fourcc == 0x54435635) {  // TCV5
        *(uint *)(*(int *)(entity + 4) + 0x30) = *(uint *)(*(int *)(entity + 4) + 0x30) | 1;
    }
    if (*fourcc == 0x54435636) {  // TCV6
        *(uint *)(*(int *)(entity + 4) + 0x30) = *(uint *)(*(int *)(entity + 4) + 0x30) | 1;
    }
    if (*fourcc == 0x54435637) {  // TCV7
        *(uint *)(*(int *)(entity + 4) + 0x30) = *(uint *)(*(int *)(entity + 4) + 0x30) | 1;
    }
}

// ---------------------------------------------------------------------------
// tcv_render_callback (0x8024d0c0)
// Per-TCV-member render/destroy handler. When the active TCV member is at
// state 0x17 and its timer (+8) has expired (> FLOAT_80618c40), destroy the
// member and its linked members, spawning the appropriate explosion effect for
// each FourCC (TCV1/4 -> chained destroy, TCV5/6/7 -> one-shot bursts).
// ---------------------------------------------------------------------------
void tcv_render_callback(undefined4 *entity)
{
    undefined4 *self;
    uint flags;
    undefined4 *other;
    float posA[3];  // local_1c/18/14
    float posB[3];  // local_28/24/20

    self = (undefined4 *)*entity;
    if ((entity[6] == 0x17) && (FLOAT_80618c40 < (float)entity[8])) {
        other = (undefined4 *)entity_handle_resolve(0x54435634);  // TCV4
        if (self == other) {
            other = (undefined4 *)entity_handle_resolve(0x54435631);  // TCV1
            if (other != (undefined4 *)0x0) {
                posA[0] = (float)other[0x19] + (float)other[0x6a];
                posA[1] = (float)other[0x1a] + (float)other[0x6b];
                posA[2] = (float)other[0x1b] + (float)other[0x6c];
                entity_destroy_with_event(other);
                FUN_802520cc(posA);
                particle_kill_by_entity();
            }
            FUN_8025212c();
            entity_destroy_with_event(self);
        }
        else {
            other = (undefined4 *)entity_handle_resolve(0x54435631);  // TCV1
            if ((self == other) ||
                (other = (undefined4 *)entity_handle_resolve(0x54435632), self == other) ||  // TCV2
                (other = (undefined4 *)entity_handle_resolve(0x54435633), self == other)) {  // TCV3
                posB[0] = (float)self[0x19] + (float)self[0x6a];
                posB[1] = (float)self[0x1a] + (float)self[0x6b];
                posB[2] = (float)self[0x1b] + (float)self[0x6c];
                FUN_802520cc(posB);
                entity_destroy_with_event(self);
            }
            else {
                other = (undefined4 *)entity_handle_resolve(0x54435635);  // TCV5
                if (self == other) {
                    flags = *(uint *)(self[1] + 0x30);
                    if ((flags & 2) == 0) {
                        *(uint *)(self[1] + 0x30) = flags | 2;
                        *(uint *)(self[1] + 0x30) = *(uint *)(self[1] + 0x30) & 0xfffffffe;
                        FUN_802521b0();
                        particle_kill_by_entity();
                    }
                }
                else {
                    other = (undefined4 *)entity_handle_resolve(0x54435636);  // TCV6
                    if (self == other) {
                        flags = *(uint *)(self[1] + 0x30);
                        if ((flags & 2) == 0) {
                            *(uint *)(self[1] + 0x30) = flags | 2;
                            *(uint *)(self[1] + 0x30) = *(uint *)(self[1] + 0x30) & 0xfffffffe;
                            FUN_8025224c();
                            particle_kill_by_entity();
                        }
                    }
                    else {
                        other = (undefined4 *)entity_handle_resolve(0x54435637);  // TCV7
                        if (self == other) {
                            flags = *(uint *)(self[1] + 0x30);
                            if ((flags & 2) == 0) {
                                *(uint *)(self[1] + 0x30) = flags | 2;
                                *(uint *)(self[1] + 0x30) = *(uint *)(self[1] + 0x30) & 0xfffffffe;
                                FUN_802522e8();
                                particle_kill_by_entity();
                            }
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// tcv_init_effects (0x8024d438)
// Build the two particle effect definitions + emitters used by the TCV finale
// (smoke/ember plumes). Bracketed by the particle-list context lock.
// ---------------------------------------------------------------------------
void tcv_init_effects(void)
{
    double dVar1;
    int kfA[4];   // local_3c..30 (def A keyframes)
    int kfB[4];   // local_4c..40 (def B keyframes)
    int emScale[3]; // local_58..50 (emitter scale)

    FUN_803826dc();
    kfA[0] = DAT_803e03e0;
    kfA[1] = DAT_803e03e4;
    kfA[2] = DAT_803e03e8;
    kfA[3] = DAT_803e03ec;
    kfB[0] = DAT_803e03f0;
    kfB[1] = DAT_803e03f4;
    kfB[2] = DAT_803e03f8;
    kfB[3] = DAT_803e03fc;
    emScale[0] = DAT_803e0400;
    emScale[1] = DAT_803e0404;
    emScale[2] = DAT_803e0408;

    particle_def_create((double)FLOAT_80618c54, (double)FLOAT_80618c2c, &DAT_805cb5f4, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80618c2c, (double)FLOAT_80618c44,
                                    (double)FLOAT_80618c44, (double)FLOAT_80618c58,
                                    0x404040ff, 0x101010ff, 0);
    particle_def_set_size_start_end((double)FLOAT_80618c5c, (double)FLOAT_80618c60,
                                    (double)FLOAT_80618c64, (double)FLOAT_80618c64);
    particle_def_set_color_end((double)FLOAT_80618c68, (double)FLOAT_80618c6c,
                               (double)FLOAT_80618c70, (double)FLOAT_80618c44);
    particle_def_set_velocity_ranges((double)FLOAT_80618c74, (double)FLOAT_80618c78,
                                     (double)FLOAT_80618c70, (double)FLOAT_80618c64,
                                     (double)FLOAT_80618c2c, (double)FLOAT_80618c70);
    particle_def_set_drag((double)FLOAT_80618c7c);
    particle_def_set_keyframes_raw((int)kfA, (int)kfB, 4);
    particle_emitter_create((double)FLOAT_80618c80, (double)FLOAT_80618c70,
                            (double)FLOAT_80618c74, &DAT_805cb6c0, &DAT_805cb5f4, 0, 0x2201);
    dVar1 = (double)FLOAT_80618c44;
    particle_emitter_set_color_range(dVar1, dVar1, (double)FLOAT_80618c4c, dVar1);
    particle_emitter_set_lifetime_range((double)FLOAT_80618c44, (double)FLOAT_80618c44);
    particle_emitter_set_velocity_range((double)FLOAT_80618c44, (double)FLOAT_80618c44);

    particle_def_create((double)FLOAT_80618c40, (double)FLOAT_80618c2c, &DAT_805cb744, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80618c2c, (double)FLOAT_80618c44,
                                    (double)FLOAT_80618c44, (double)FLOAT_80618c84,
                                    0x404040ff, 0x101010ff, 0);
    particle_def_set_size_start_end((double)FLOAT_80618c5c, (double)FLOAT_80618c60,
                                    (double)FLOAT_80618c64, (double)FLOAT_80618c64);
    particle_def_set_color_end((double)FLOAT_80618c68, (double)FLOAT_80618c6c,
                               (double)FLOAT_80618c88, (double)FLOAT_80618c44);
    particle_def_set_velocity_ranges((double)FLOAT_80618c8c, (double)FLOAT_80618c78,
                                     (double)FLOAT_80618c70, (double)FLOAT_80618c64,
                                     (double)FLOAT_80618c2c, (double)FLOAT_80618c70);
    particle_def_set_drag((double)FLOAT_80618c90);
    particle_emitter_create((double)FLOAT_80618c44, (double)FLOAT_80618c44,
                            (double)FLOAT_80618c2c, &DAT_805cb810, &DAT_805cb744, 4, 0x201);
    dVar1 = (double)FLOAT_80618c44;
    particle_emitter_set_color_range(dVar1, dVar1, (double)FLOAT_80618c94, dVar1);
    particle_emitter_set_scale(emScale);
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// tcv_reset_state (0x8024d694)
// Zero and seed all TCV controller state globals, wire the four anim/member
// block pointers, and init the four projectile-jitter states.
// ---------------------------------------------------------------------------
extern "C" void memset_wrapper(int dst, int val, int len);

void tcv_reset_state(void)
{
    int ctx;
    int seedIdx;
    int i;

    ctx = __save_gpr_28();
    memset_wrapper((int)&DAT_805cb894, 0, 100);
    seedIdx = 0;
    DAT_805cb590 = (int)&DAT_8046d978;
    DAT_805cb594 = (int)&DAT_8046d948;
    i = 0;
    DAT_805cb598 = (int)&DAT_8046d8d0;
    DAT_805cb59c = (int)&DAT_8046daf4;
    DAT_805cb5bc = 0;
    DAT_805cb5c4 = 0;
    DAT_805cb5cc = 0;
    DAT_805cb5d0 = 0;
    DAT_805cb5c8 = 0;
    DAT_805cb5a8 = FLOAT_80618c44;
    DAT_805cb5a0 = 0;
    DAT_805cb5d4 = 0;
    DAT_805cb5d8 = FLOAT_80618c44;
    DAT_805cb5dc = 0;
    DAT_8046daa8 = (int)FLOAT_80618c44;
    DAT_8046d970 = 0;
    DAT_8046d958 = 0;
    DAT_8046d974 = FLOAT_80618c98;
    DAT_8046d95c = FLOAT_80618c98;
    DAT_805cb5b0 = FLOAT_80618c9c;
    DAT_805cb5b4 = *(int *)(ctx + 0x68);
    DAT_805cb5b8 = FLOAT_80618ca0;
    DAT_805cb5e8 = 0;
    DAT_805cb5ec = 0;
    DAT_805cb5f0 = 0;
    do {
        player_ai_projectile_jitter_init((float *)((int)&DAT_805cb894 + seedIdx));
        i = i + 1;
        seedIdx = seedIdx + 0x1c;
    } while (i < 4);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// tcv_entity_spawn (0x8024d2e8)
// Spawn handler (class descriptor @0x8044ec48): register the entity for update
// slots 3/7/2, install the render/tick callback, init state + effects, allocate
// a screen-effect slot, and set the difficulty-scaled countdown duration
// (+0x1cc / +0x1d8) from DAT_805015bf.
// ---------------------------------------------------------------------------
void tcv_entity_spawn(int entity)
{
    float duration;
    int slot;

    entity_register_for_update(entity, 3, -0x7fdb1e18);  // 0x8024e1e8
    entity_register_for_update(entity, 7, -0x7fdb2854);  // 0x8024d7ac
    entity_register_for_update(entity, 2, -0x7fdb1ea8);  // 0x8024e158
    entity_set_render_type(entity, (void *)tcv_render_tick_cb);
    tcv_reset_state();
    tcv_init_effects();
    if (*(int *)(entity + 0x268) == 0) {
        slot = screen_effect_alloc_slot();
        *(int *)(entity + 0x268) = slot;
    }
    duration = FLOAT_80618c44;
    *(uint *)(entity + 0x28) = *(uint *)(entity + 0x28) | 0x10;
    *(uint *)(entity + 0x28) = *(uint *)(entity + 0x28) & 0xfffffffe;
    *(uint *)(*(int *)(entity + 4) + 0x30) = *(uint *)(*(int *)(entity + 4) + 0x30) | 1;
    *(uint *)(*(int *)(entity + 4) + 0x30) = *(uint *)(*(int *)(entity + 4) + 0x30) | 4;
    *(float *)(DAT_805cb598 + 0x68) = duration;
    *(float *)(DAT_805cb598 + 0x74) = duration;
    *(undefined1 *)(entity + 0x32) = 2;

    if (DAT_805015bf != 1) {
        if (DAT_805015bf == 0) {
            *(float *)(entity + 0x1d8) = FLOAT_80618c48;  // easy
            goto LAB_apply;
        }
        if (DAT_805015bf < 3) {
            *(float *)(entity + 0x1d8) = FLOAT_80618c4c;  // hard
            goto LAB_apply;
        }
    }
    *(float *)(entity + 0x1d8) = FLOAT_80618c50;          // normal (==1) / very-hard (>=3)
LAB_apply:
    *(int *)(entity + 0x1cc) = *(int *)(entity + 0x1d8);
    screen_effect_init_slot(*(int *)(entity + 0x268), entity);
}

// ---------------------------------------------------------------------------
// tcv_aim_and_damage_tick (0x8024d7ac)
// Update slot 7: per-frame homing/aim solve against the active target. Reads the
// incoming aim-basis vectors from the PPC float registers (f26..f31 pairs) that
// the caller sets up, resolves the target/self entity pair via the aim-tick
// context, computes a yaw/pitch aim error (asin) plus an easing curve, then
// gates a melee damage dispatch (type 0x24) based on the per-side state fields.
// ---------------------------------------------------------------------------
void tcv_aim_and_damage_tick(float aim_f26, float aim_ps26, float aim_f27, float aim_ps27,
                             float aim_f28, float aim_ps28, float aim_f29, float aim_ps29,
                             float aim_f30, float aim_ps30, float aim_f31, float aim_ps31)
{
    uint uVar1;
    float fVar2;
    float fVar3;
    int selfEnt;
    int stateA;
    int stateB;
    int targetEnt;
    bool sideNeg;
    double dVar9, dVar10, dVar11, dVar12, dVar13;
    double dVar14, dVar15, dVar16, dVar17, dVar18, dVar19;
    unsigned long long ctxPair;

    // basis vectors staged from the incoming register pairs
    float basis[12];
    basis[0]  = aim_f31; basis[1]  = aim_ps31;
    basis[2]  = aim_f30; basis[3]  = aim_ps30;
    basis[4]  = aim_f29; basis[5]  = aim_ps29;
    basis[6]  = aim_f28; basis[7]  = aim_ps28;
    basis[8]  = aim_f27; basis[9]  = aim_ps27;
    basis[10] = aim_f26; basis[11] = aim_ps26;
    (void)basis;

    ctxPair   = FUN_803826e8();                 // packed: high32=selfEnt, low32=&targetEnt
    selfEnt   = (int)(ctxPair >> 0x20);
    targetEnt = *(int *)(unsigned)ctxPair;
    dVar11 = (double)FLOAT_80618c74;
    dVar12 = (double)FLOAT_80618ca4;
    dVar15 = (double)FLOAT_80618c30;
    stateB = *(int *)(DAT_805cb594 + 0x28);
    stateA = *(int *)(DAT_805cb594 + 0x10);

    uVar1 = (uint)((double)FLOAT_80618ca8 *
                   (double)((FLOAT_80618cac * *(float *)(selfEnt + 0x70)) / FLOAT_80618c5c) + dVar11);
    dVar19 = (double)(float)(dVar12 * (double)DAT_80583228[uVar1 & 0x7ff]);
    dVar18 = (double)(float)(dVar12 * (double)DAT_80583228[(uVar1 + 0x200) & 0x7ff]);
    dVar13 = (double)(float)(dVar18 * dVar18 +
                             (double)(float)(dVar19 * dVar19 + (double)FLOAT_80618c44));
    if ((double)FLOAT_80618cb0 < dVar13) {
        dVar12 = 1.0 / SQRT((float)dVar13);
        fVar2 = (float)(dVar12 * -(double)((float)(dVar11 * dVar13) * (float)(dVar12 * dVar12) -
                                           FLOAT_80618c8c));
        dVar11 = (double)(fVar2 * -((float)(dVar11 * dVar13) * fVar2 * fVar2 - FLOAT_80618c8c));
        dVar19 = (double)(float)(dVar19 * (double)(float)((double)FLOAT_80618c2c * dVar11));
        dVar18 = (double)(float)(dVar18 * (double)(float)((double)FLOAT_80618c2c * dVar11));
    }

    fVar2 = *(float *)(selfEnt + 0x68) - *(float *)(targetEnt + 0x68);
    dVar17 = (double)(*(float *)(selfEnt + 100) - *(float *)(targetEnt + 100));
    dVar16 = (double)(*(float *)(selfEnt + 0x6c) - *(float *)(targetEnt + 0x6c));
    fVar2 = (float)(dVar16 * dVar16 + (double)(float)(dVar17 * dVar17 + (double)(fVar2 * fVar2)));
    if (FLOAT_80618cb0 < fVar2) {
        fVar3 = 1.0f / SQRT(fVar2);
        fVar3 = fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c);
        dVar9 = (double)(FLOAT_80618c2c *
                         fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c));
        dVar17 = (double)(float)(dVar17 * dVar9);
        dVar16 = (double)(float)(dVar16 * dVar9);
    }

    dVar10 = (double)(float)(dVar17 * dVar19 + (double)(float)(dVar16 * dVar18));
    dVar9 = dVar10;
    if ((double)FLOAT_80618c2c < dVar10) {
        dVar9 = (double)FLOAT_80618c2c;
    }
    if ((double)FLOAT_80618c30 <= dVar9) {
        if ((double)FLOAT_80618c2c < dVar10) {
            dVar10 = (double)FLOAT_80618c2c;
        }
    }
    else {
        dVar10 = (double)FLOAT_80618c30;
    }
    dVar9 = math_asin(dVar10);
    dVar14 = (double)((float)((double)FLOAT_80618c5c * dVar9) / FLOAT_80618cac);
    dVar9 = FUN_80390324((double)((float)(dVar14 - (double)FLOAT_80618cb4) / FLOAT_80618c60));
    dVar10 = (double)FLOAT_80618c60;
    dVar9 = (double)(float)dVar9;
    fVar2 = -(float)(dVar10 * dVar9 - dVar14);
    sideNeg = (float)(dVar18 * dVar17 - (double)(float)(dVar19 * dVar16)) <= FLOAT_80618c44;

    if (FLOAT_80618cb8 <= fVar2) {
        if (FLOAT_80618cbc <= fVar2) {
            if (FLOAT_80618cc4 <= fVar2) {
                if ((sideNeg) || (stateA != 2)) {
                    if ((sideNeg) && (stateB == 2)) {
                        dVar15 = (double)FLOAT_80618c2c;
                    }
                    else if ((stateB == 2) && (stateA == 2)) {
                        dVar15 = (double)FLOAT_80618c2c;
                    }
                }
                else {
                    dVar15 = (double)FLOAT_80618c2c;
                }
            }
            else if ((sideNeg) || (stateB != 2)) {
                if ((sideNeg) && (stateA == 2)) {
                    dVar15 = (double)FLOAT_80618cc0;
                }
            }
            else {
                dVar15 = (double)FLOAT_80618cc0;
            }
        }
        else if ((sideNeg) || (stateA != 2)) {
            if ((sideNeg) && (stateB == 2)) {
                dVar15 = (double)FLOAT_80618cc0;
            }
        }
        else {
            dVar15 = (double)FLOAT_80618cc0;
        }
    }
    else if ((sideNeg) || (stateA != 1)) {
        if ((sideNeg) && (stateB == 1)) {
            dVar15 = (double)FLOAT_80618c2c;
        }
        else if ((stateB == 1) && (stateA == 1)) {
            dVar15 = (double)FLOAT_80618c2c;
        }
    }
    else {
        dVar15 = (double)FLOAT_80618c2c;
    }

    if ((double)FLOAT_80618c44 < dVar15) {
        int params[24];
        switch_slot_reset((int)params);
        // damage descriptor fields
        *(undefined4 *)((char *)params + 0x30) = 0;             // local_a0
        *(undefined4 *)((char *)params + 0x0c) = 0;             // local_c4
        *(undefined4 *)((char *)params + 0x10) = 0;             // local_c0
        *(undefined4 *)((char *)params + 0x18) = 0;             // local_b8
        *(float *)((char *)params + 0x1c)      = FLOAT_80618c44;// local_b4
        params[0] = 0x24;                                       // damage type
        *(float *)((char *)params + 0x34) = FLOAT_80618cc8;     // local_9c
        *(float *)((char *)params + 0x38) = FLOAT_80618c80;     // local_98
        *(float *)((char *)params + 0x08) = (float)dVar15;      // local_c8
        params[16] = targetEnt;                                 // local_e8[0]
        params[18] = selfEnt;                                   // local_e0
        damage_dispatch_to_entity((double)FLOAT_80618cc8, (double)FLOAT_80618c44, dVar10, dVar9,
                                  dVar11, dVar12, dVar13, 0, params);
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// tcv_finish_sequence (0x8024db6c)
// Detonation/finish: run the big finale (FUN_8024ea90), switch to phase 10,
// silence voices, clear the session's TCV flag, and stop the two looping SFX
// handles held in the entity (+0x228 / +0x22c).
// ---------------------------------------------------------------------------
void tcv_finish_sequence(undefined8 a1, undefined8 a2, double a3, undefined8 a4, undefined8 a5,
                         undefined8 a6, undefined8 a7, undefined8 a8, int entity)
{
    tcv_big_finale(a1, a2, a3, a4, a5, a6, a7, a8);
    tcv_set_phase(10, 0);
    sound_set_voice_volume_scale(0);
    *(undefined4 *)(DAT_80611d74 + 0xe8c) = 0;
    if (-1 < *(int *)(entity + 0x22c)) {
        sound_stop_handle();
        *(int *)(entity + 0x22c) = -1;
    }
    if (-1 < *(int *)(entity + 0x228)) {
        sound_stop_handle();
        *(int *)(entity + 0x228) = -1;
    }
}

// ---------------------------------------------------------------------------
// tcv_countdown_tick (0x8024dbe8)
// Master countdown driver (called from the render/tick cb): decrement the
// entity countdown timer (+0x1cc) by the frame delta. When it crosses zero it
// either enters active phase 9 (kick emitter + start warning SFX 0x207) or,
// once already active/finished, runs the finish sequence.
// ---------------------------------------------------------------------------
void tcv_countdown_tick(double dt, undefined8 a2, double a3, undefined8 a4, undefined8 a5,
                        undefined8 a6, undefined8 a7, undefined8 a8, int entity)
{
    double zero;
    float prev;
    int params[7];
    undefined4 sp_x, sp_y, sp_z;   // local_2c/28/24
    undefined1 sp_flag;            // local_20

    zero = (double)FLOAT_80618c44;
    prev = *(float *)(entity + 0x1cc);
    *(float *)(entity + 0x1cc) = (float)((double)prev - dt);
    if (zero < (double)*(float *)(entity + 0x1cc)) {
        if (((DAT_805cb5c4 != 9) && (DAT_805cb5c4 != 1)) && (DAT_805cb5ec == 0)) {
            float ticks = (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000)
                                  - DOUBLE_80618c38);
            tcv_kick_capture_emitter((double)(FLOAT_80618c84 * ((FLOAT_80618c28 * ticks) / ticks)), entity);
            tcv_set_phase(9, 0);
            sound_init_play_params((int)params);
            sp_flag = 1;
            params[0] = 0x207;   // warning SFX
            sp_x = *(undefined4 *)(entity + 100);
            sp_y = *(undefined4 *)(entity + 0x68);
            sp_z = *(undefined4 *)(entity + 0x6c);
            *(undefined4 *)((char *)params + 0x0c) = sp_x;
            *(undefined4 *)((char *)params + 0x10) = sp_y;
            *(undefined4 *)((char *)params + 0x14) = sp_z;
            *(undefined1 *)((char *)params + 0x18) = sp_flag;
            sound_play_request(params);
        }
    }
    else {
        tcv_finish_sequence((double)*(float *)(entity + 0x1cc), (double)prev, a3, a4, a5, a6,
                            a7, a8, entity);
    }
}

// ===========================================================================
// batch_724: per-frame update ticks (slots 2 & 3), collision shape build,
//            the big finale, and small phase helpers. APPEND to batch_723.
// ===========================================================================

// ---------------------------------------------------------------------------
// tcv_render_tick_cb (0x8024dcec)
// Render/tick callback installed via entity_set_render_type. Unless detonated
// (phase 10), when the entity's animation sub-state (+0x14 => param9[5]) is 0xc
// (or 8) and its per-frame delta (+0x20 => param9[8]) has advanced enough, drive
// the countdown by that scaled delta.
// ---------------------------------------------------------------------------
void tcv_render_tick_cb(undefined8 a1, undefined8 a2, double a3, undefined8 a4, undefined8 a5,
                        undefined8 a6, undefined8 a7, undefined8 a8, int *param9)
{
    int substate;

    if (DAT_805cb5c4 != 10) {
        substate = param9[5];
        if (substate == 0xc) {
            if (FLOAT_80618ca4 < (float)param9[8]) {
                tcv_countdown_tick((double)(FLOAT_80618ccc * (float)param9[8]), a2, a3, a4,
                                   a5, a6, a7, a8, *param9);
            }
        }
        else if (((substate < 0xc) && (substate == 8)) &&
                 ((double)FLOAT_80618c74 < (double)(float)param9[8])) {
            tcv_countdown_tick((double)(float)param9[8], a2, a3, a4, a5, a6, a7, a8, *param9);
        }
    }
}

// ---------------------------------------------------------------------------
// tcv_active_countdown_tick (0x8024dd80)
// Alternate per-frame countdown path used while an anim clip (member+0x15c) is
// in the 0xf..0x11 range and phase == 9: subtract a difficulty-scaled step from
// the entity countdown, finish if it hits zero, then run the debris/explosion
// sweep FUN_8024ed2c.
// ---------------------------------------------------------------------------
void tcv_active_countdown_tick(undefined8 a1, undefined8 a2, double a3, undefined8 a4, undefined8 a5,
                               undefined8 a6, undefined8 a7, undefined8 a8, int entity, int member)
{
    if (DAT_805cb5c4 != 9) {
        return;
    }
    if (0x11 < *(int *)(member + 0x15c)) {
        return;
    }
    if (*(int *)(member + 0x15c) < 0xf) {
        return;
    }
    if (DAT_805015bf != 1) {
        if (DAT_805015bf == 0) {
            *(float *)(entity + 0x1cc) = *(float *)(entity + 0x1cc) - FLOAT_80618cd0;
            goto LAB_check;
        }
        if (DAT_805015bf < 3) {
            *(float *)(entity + 0x1cc) = *(float *)(entity + 0x1cc) - FLOAT_80618cd4;
            goto LAB_check;
        }
    }
    *(float *)(entity + 0x1cc) = *(float *)(entity + 0x1cc) - FLOAT_80618cd0;
LAB_check:
    if ((double)*(float *)(entity + 0x1cc) <= (double)FLOAT_80618c44) {
        tcv_finish_sequence((double)*(float *)(entity + 0x1cc), a2, a3, a4, a5, a6, a7, a8, entity);
    }
    FUN_8024ed2c(entity);
}

// ---------------------------------------------------------------------------
// tcv_pre_arm_pulse (0x8024de60)
// Pulse the 4 arm sub-updates when no phase-arg delay is active; accumulate a
// jittered strike counter (DAT_805cb5f0) and fire the strike sub-update when it
// goes positive, clamping it against the frame counter. Bracketed by fx lock.
// ---------------------------------------------------------------------------
void tcv_pre_arm_pulse(void)
{
    int i;

    FUN_803826f0();
    if (DAT_805cb5d0 == 0) {
        i = 0;
        do {
            FUN_802500a0();
            i = i + 1;
        } while (i < 4);
        DAT_805cb5f0 = DAT_805cb5f0 + DAT_806108e4;
        if (0 < DAT_805cb5f0) {
            i = 0;
            do {
                FUN_8024fadc();
                i = i + 1;
            } while (i < 4);
        }
        if ((int)DAT_806109c0 < DAT_805cb5f0) {
            DAT_805cb5f0 = -(((int)DAT_806109c0 >> 1) +
                             (int)((unsigned)((int)DAT_806109c0 < 0 && (DAT_806109c0 & 1) != 0)));
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// tcv_build_collision_shape (0x8024df18)
// Build the per-frame damage/collision shape for the active TCV member: derive a
// jittered color/id triple from the frame counter + sine table, set the shape's
// layer mask, fill the 3 arm entries with the per-arm hazard ids (0xf/0x10/0x11)
// gated by the member render bits, copy 6 vertex pairs, and forward to collision.
// ---------------------------------------------------------------------------
void tcv_build_collision_shape(int entity)
{
    undefined4 *pdst;
    uint uVar2;
    float fVar3;
    int iVar4;
    uint uVar5;
    int iVar6;
    int iVar7;
    uint uVar8;
    undefined4 *psrc;
    undefined4 val;

    undefined4 uStack_ac;
    undefined4 local_a8[11];
    undefined4 uStack_7c;
    undefined4 local_78[6];
    uint local_60[6];
    uint tickMod;
    uint tickNeg;

    val = 0;
    tickNeg = DAT_806109c0 ^ 0x80000000;
    local_60[2] = 0x40000000;
    local_60[1] = 0x40000000;
    uVar2 = (uint)(FLOAT_80618cd8 * (float)((double)(longlong)(unsigned)tickNeg - DOUBLE_80618c38));
    local_60[0] = 0x40000000;
    // per-frame modulus of the trail-stamp index against uVar2
    tickMod = (DAT_80611928 - (DAT_80611928 / (int)uVar2) * uVar2) ^ 0x80000000;
    uint uStack_2c = uVar2 ^ 0x80000000;
    uVar2 = (uint)(FLOAT_80618ca8 * FLOAT_80618c40 * FLOAT_80618cac *
                   ((float)((double)(longlong)(unsigned)tickMod - DOUBLE_80618c38) /
                    (float)((double)(longlong)(unsigned)uStack_2c - DOUBLE_80618c38)) + FLOAT_80618c74);
    fVar3 = FLOAT_80618c74 * DAT_80583228[uVar2 & 0x7ff] + FLOAT_80618c74;
    uVar2 = (uint)(FLOAT_80618ce0 * fVar3);
    uVar8 = (uint)(FLOAT_80618cdc * fVar3);
    iVar4 = ((int)uVar2 >> 2) + (int)((unsigned)((int)uVar2 < 0 && (uVar2 & 3) != 0));
    iVar7 = 0x7f;
    if (iVar4 < 0x7f) {
        iVar7 = iVar4;
    }
    uVar5 = ((int)uVar8 >> 2) + (unsigned)((int)uVar8 < 0 && (uVar8 & 3) != 0);
    uVar8 = 0x7f;
    if ((int)uVar5 < 0x7f) {
        uVar8 = uVar5;
    }
    iVar6 = ((int)uVar2 >> 2) + (int)((unsigned)((int)uVar2 < 0 && (uVar2 & 3) != 0));
    iVar4 = 0x7f;
    if (iVar6 < 0x7f) {
        iVar4 = iVar6;
    }
    local_60[3] = iVar4 << 0x10 | uVar8 | iVar7 << 8;
    collision_set_layer_mask(*(int **)(entity + 4), 8);

    iVar4 = 0;
    iVar7 = 0;
    iVar6 = 3;
    do {
        if (((iVar4 != 2) || ((*(uint *)(*(int *)(entity + 4) + 0x30) & 5) == 0)) &&
            ((iVar4 == 2 || ((*(uint *)(*(int *)(entity + 4) + 0x30) & 2) == 0)))) {
            if (iVar4 == 1) {
                val = 0xf;
            }
            else if (iVar4 < 1) {
                if (-1 < iVar4) {
                    val = 0x11;
                }
            }
            else if (iVar4 < 3) {
                val = 0x10;
            }
            *(undefined4 *)((int)local_60 + iVar7) = val;
        }
        iVar4 = iVar4 + 1;
        iVar7 = iVar7 + 4;
        iVar6 = iVar6 + -1;
    } while (iVar6 != 0);

    // copy 6 vertex pairs from the local_78 staging into the shape header buffer
    pdst = &uStack_ac;
    psrc = &uStack_7c;
    iVar7 = 6;
    do {
        undefined4 *p1 = psrc + 1;
        psrc = psrc + 2;
        val = *psrc;
        pdst[1] = *p1;
        pdst = pdst + 2;
        *pdst = val;
        iVar7 = iVar7 + -1;
    } while (iVar7 != 0);
    collision_forward_shape_data(8, (int)local_a8);
}

// ---------------------------------------------------------------------------
// tcv_render_matrix_update (0x8024e158)
// Update slot 2 (render/matrix pass): reset the matrix table, rebuild the root
// bone chain, commit the 2 per-arm transform passes, then the arm nodes, and
// flag the entity dirty (+0x28 |= 0x200000).
// ---------------------------------------------------------------------------
void tcv_render_matrix_update(void)
{
    int ctx;
    int i;

    ctx = __save_gpr_28();
    mtx4_table_reset_identity();
    FUN_80251468();
    i = 0;
    do {
        FUN_8024f23c();
        i = i + 1;
    } while (i < 2);
    FUN_802512f0();
    *(uint *)(ctx + 0x28) = *(uint *)(ctx + 0x28) | 0x200000;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// tcv_master_fsm_tick (0x8024e1e8)
// Update slot 3: the master boss state machine. Handles activation gating, the
// per-frame jitter/timer decrements, and the big phase switch (1 rise, 3 sweep,
// 4 charge, 5/6/7/8 engaged, 9 score-complete). Advances the phase timer
// DAT_805cb5cc and runs the on-phase-change handler FUN_8024ef88. Bracketed by
// the particle-list context lock FUN_803826dc/80382728.
// ---------------------------------------------------------------------------
void tcv_master_fsm_tick(void)
{
    float fVar1;
    double dVar2;
    float fVar3;
    bool bVar4;
    double dVar5;
    int ctx;
    int iVar7;
    bool bVar8;
    undefined4 uVar9;
    int iVar11;
    int iVar10;
    double dVar12;
    int aiStack_68[3];
    int local_5c;

    ctx = FUN_803826dc();
    bVar4 = false;
    if (DAT_805cb5c4 == 10) goto LAB_leave;

    if (DAT_805cb5bc == 0) {
        iVar7 = FUN_80251718();
        if (iVar7 == 0) goto LAB_leave;
        *(uint *)(ctx + 0x28) = *(uint *)(ctx + 0x28) | 1;
        DAT_805cb5bc = 1;
        tcv_set_phase(1, 0);
        *(int *)(DAT_80611d74 + 0xe8c) = ctx;
        sound_set_voice_volume_scale(3);
    }
    if (DAT_805cb5c4 == 9) {
        tcv_build_collision_shape(ctx);
    }
    if (DAT_806108e4 == 0) goto LAB_leave;

    iVar11 = 0;
    iVar7 = 0;
    do {
        player_ai_projectile_jitter_tick((double)FLOAT_80618c40,
                                         (undefined4 *)((int)&DAT_805cb894 + iVar7));
        iVar11 = iVar11 + 1;
        iVar7 = iVar7 + 0x1c;
    } while (iVar11 < 4);
    FUN_8025041c();

    if (0 < (int)DAT_805cb5ec) {
        DAT_805cb5ec = (DAT_805cb5ec - DAT_806108e4) & ~((int)(DAT_805cb5ec - DAT_806108e4) >> 0x1f);
    }
    if (0 < (int)DAT_805cb5d0) {
        DAT_805cb5d0 = (DAT_805cb5d0 - DAT_806108e4) & ~((int)(DAT_805cb5d0 - DAT_806108e4) >> 0x1f);
    }

    if (DAT_805cb5e0 == 0) {
        tcv_set_phase(5, 0);
    }
    else {
        switch (DAT_805cb5c4) {
        default:
LAB_case0:
            bVar8 = (FUN_8024ee7c() != 0);
            if (bVar8) {
                tcv_set_phase(6, 10);
            }
            else {
                bVar8 = (FUN_8024eec4() != 0);
                if (bVar8) {
                    tcv_set_phase(7, 0);
                }
            }
            break;
        case 1:
        case 4:
        case 6:
        case 9:
            break;
        case 8: {
            double elapsed = (double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000);
            (void)elapsed;
            iVar7 = (int)(FLOAT_80618c80 *
                          (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38));
            if (iVar7 <= (int)DAT_805cb5cc) goto LAB_case0;
            break;
        }
        }
    }

    dVar5 = DOUBLE_80618c38;
    iVar7 = DAT_805cb590;
    switch (DAT_805cb5c4) {
    case 1:
        fVar1 = *(float *)(ctx + 100) - DAT_805cb5b0;
        fVar3 = *(float *)(ctx + 0x6c) - DAT_805cb5b8;
        if (fVar1 * fVar1 + fVar3 * fVar3 < FLOAT_80618c80) {
            tcv_set_phase(3, 0);
        }
        {
            float total = (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38);
            float elapsed = (float)((double)(longlong)(unsigned)(DAT_805cb5cc ^ 0x80000000) - DOUBLE_80618c38);
            if ((FLOAT_80618cb8 * (FLOAT_80618ce4 / total) <= elapsed) &&
                (iVar7 = entity_handle_resolve(0x544b4452), iVar7 != 0)) {  // TKDR
                if (*(float *)(*(int *)(iVar7 + 4) + 0x100) < FLOAT_80618c74) {
                    sound_play_stereo_pair((double)FLOAT_80618c64, (double)FLOAT_80618ce8,
                                           (double)FLOAT_80618cec);
                }
                anim_set_blend_weight((double)FLOAT_80618c2c, *(int *)(iVar7 + 4));
            }
        }
        break;
    case 3:
        tcv_pre_arm_pulse();
        iVar7 = ((int)DAT_806109c0 >> 1) + (int)((unsigned)((int)DAT_806109c0 < 0 && (DAT_806109c0 & 1) != 0));
        if ((int)(DAT_806109c0 * 3 + iVar7 * 2) < (int)DAT_805cb5cc) {
            tcv_set_phase(4, iVar7);
        }
        break;
    case 4:
        bVar4 = false;
        iVar11 = DAT_805cb590 + 0x130;
        if ((DAT_805cb5e0 != 0) &&
            (fVar1 = *(float *)(ctx + 100) - *(float *)(DAT_805cb5e0 + 100),
             fVar3 = *(float *)(ctx + 0x6c) - *(float *)(DAT_805cb5e0 + 0x6c),
             fVar1 * fVar1 + fVar3 * fVar3 < FLOAT_80618cf0)) {
            bVar4 = true;
        }
        iVar10 = 0;
        do {
            FUN_802500a0();
            if (bVar4) {
                FUN_8024fadc();
            }
            iVar10 = iVar10 + 1;
        } while (iVar10 < 4);
        if ((DAT_805cb5d0 == 0) && (FUN_8024fadc(), 1)) {
            (**(void (***)(int))(iVar7 + 0x168))(iVar11);
        }
        if (0 < DAT_805cb5dc) {
            FUN_802500a0();
        }
        if (DAT_805cb5dc == 0) {
            iVar7 = (int)(FLOAT_80618c8c *
                          (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38));
            tcv_set_phase(3, iVar7);
        }
        bVar4 = true;
        break;
    case 5:
        if (DAT_805cb5e0 != 0) {
            tcv_set_phase(3, 0);
        }
        break;
    case 6:
        tcv_pre_arm_pulse();
        iVar7 = (int)(FLOAT_80618c70 *
                      (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38));
        if (iVar7 + -5 < (int)(DAT_805cb5cc - ((int)DAT_805cb5cc / iVar7) * iVar7)) {
            FUN_8025041c();
            bVar8 = (FUN_8024ee7c() != 0);
            if (!bVar8) {
                tcv_set_phase(3, 0);
            }
        }
        iVar7 = FUN_8024eef8((float *)(ctx + 100));
        if (iVar7 == 0) {
            tcv_set_phase(8, 0);
        }
        FUN_802506a8();
        break;
    case 7:
        iVar7 = 0;
        do {
            FUN_802500a0();
            iVar7 = iVar7 + 1;
        } while (iVar7 < 5);
        if (FLOAT_80618cf4 < *(float *)(DAT_805cb5e0 + 0x68)) {
            tcv_set_phase(3, 0);
        }
        bVar4 = true;
        break;
    case 8:
        iVar7 = 0;
        do {
            FUN_802500a0();
            FUN_8024fadc();
            iVar7 = iVar7 + 1;
        } while (iVar7 < 4);
        iVar7 = FUN_8024eef8((float *)(ctx + 100));
        if (iVar7 == 0) {
LAB_dist_check:
            fVar1 = *(float *)(ctx + 100) - DAT_805cb5b0;
            fVar3 = *(float *)(ctx + 0x6c) - DAT_805cb5b8;
            if (fVar1 * fVar1 + fVar3 * fVar3 < FLOAT_80618c80) goto LAB_set3;
        }
        else {
            iVar7 = (int)(FLOAT_80618c80 *
                          (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38));
            if ((int)DAT_805cb5cc <= iVar7) goto LAB_dist_check;
LAB_set3:
            tcv_set_phase(3, 0);
        }
        bVar4 = true;
        break;
    case 9: {
        float total   = FLOAT_80618c28 * (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38);
        float elapsed = (float)((double)(longlong)(unsigned)(DAT_805cb5cc ^ 0x80000000) - DOUBLE_80618c38);
        fVar1 = elapsed / total;
        if (FLOAT_80618c2c < fVar1) {
            fVar1 = FLOAT_80618c2c;
        }
        fVar1 = FLOAT_80618c2c - fVar1;
        dVar12 = (double)FLOAT_80618c44;
        *(float *)(DAT_805cb598 + 0x68) = fVar1;
        *(float *)(DAT_805cb598 + 0x74) = fVar1;
        statlvl_class_complete(dVar12, (double)elapsed, (double)(longlong)(unsigned)(DAT_805cb5cc ^ 0x80000000),
                               dVar5, 0, 0, 0, 0);
        float total2 = FLOAT_80618c28 * (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38);
        float elapsed2 = (float)((double)(longlong)(unsigned)(DAT_805cb5cc ^ 0x80000000) - DOUBLE_80618c38);
        if (total2 <= elapsed2) {
            bVar8 = false;
            uVar9 = 0;
            iVar7 = 0;
            do {
                if (iVar7 == 1) {
                    uVar9 = 0xf;
                }
                else if (iVar7 < 1) {
                    if (-1 < iVar7) {
                        uVar9 = 0x11;
                    }
                }
                else if (iVar7 < 3) {
                    uVar9 = 0x10;
                }
                local_5c = 0;
                player_ai_projectile_count_targeted(ctx, uVar9, aiStack_68);
                fVar1 = FLOAT_80618c44;
                if (0 < local_5c) {
                    bVar8 = true;
                }
                iVar7 = iVar7 + 1;
            } while (iVar7 < 3);
            if (!bVar8) {
                *(float *)(DAT_805cb598 + 0x68) = FLOAT_80618c44;
                *(float *)(DAT_805cb598 + 0x74) = fVar1;
                DAT_805cb5ec = DAT_806109c0 << 1;
                bVar8 = (FUN_8024ee7c() != 0);
                if (bVar8) {
                    tcv_set_phase(6, 0);
                    DAT_805cb5f0 = -DAT_806109c0;
                }
                else {
                    tcv_set_phase(3, 0);
                }
            }
        }
        break;
    }
    }

    if ((-1 < *(int *)(ctx + 0x228)) && (!bVar4)) {
        sound_stop_handle();
        *(int *)(ctx + 0x228) = -1;
    }
    FUN_802509dc();
    FUN_802510d4();
    FUN_802515d8();
    FUN_8024f118();
    if (DAT_805cb5d0 == 0) {
        DAT_805cb5cc = DAT_805cb5cc + DAT_806108e4;
    }
    FUN_8024ef88();
LAB_leave:
    FUN_80382728();
    (void)dVar2;
}

// ---------------------------------------------------------------------------
// tcv_kick_capture_emitter (0x8024ea34)
// Spawn / re-kick the capture particle emitter at the entity origin (+0x64),
// storing the charge scale into DAT_805cb6fc.
// ---------------------------------------------------------------------------
void tcv_kick_capture_emitter(double amount, int entity)
{
    float local_18;
    undefined4 local_14;
    undefined4 local_10;

    local_18 = (float)DAT_803e040c;
    local_14 = DAT_803e0410;
    local_10 = DAT_803e0414;
    DAT_805cb6fc = (float)amount;
    particle_emitter_spawn(0, &DAT_805cb6c0, 0, (float *)(entity + 100), &local_18);
}

// ---------------------------------------------------------------------------
// tcv_big_finale (0x8024ea90)
// The detonation set-piece: spawn the finale entity (id 0x298) at the current
// root-bone tip, fire two staged explosions (0x16 + 0x12), run the shockwave
// helper FUN_802517b0, snap the entity back to origin, disable all 10 cached
// member particles, and play the stereo boom.
// ---------------------------------------------------------------------------
void tcv_big_finale(undefined8 a1, undefined8 a2, double a3, undefined8 a4, undefined8 a5,
                    undefined8 a6, undefined8 a7, undefined8 a8)
{
    int ctx;
    float *pfVar2;
    int *piVar3;
    int i;
    int off;
    double dVar6, dVar7;
    int local_98;
    float local_94;
    undefined4 local_90, local_8c;
    undefined4 local_88, local_84, local_80;
    float local_7c, local_78, local_74, local_70;
    undefined4 local_6c, local_68;
    undefined4 local_64, local_60, local_5c;
    float afStack_58[22];

    ctx = __save_gpr_28();
    int rootBlk = DAT_805cb590;
    local_70 = (float)DAT_803e0424;
    local_6c = DAT_803e0428;
    local_68 = DAT_803e042c;
    local_88 = DAT_803e0430;
    local_84 = DAT_803e0434;
    local_80 = DAT_803e0438;
    piVar3 = *(int **)(*(int *)(ctx + 4) + 4);
    local_64 = DAT_803e0418;
    local_60 = DAT_803e041c;
    local_5c = DAT_803e0420;
    pfVar2 = (float *)(piVar3[*piVar3 * -0x30 +
                             ((int)*(uint *)(DAT_805cb590 + 0x15c) >> 0x10) * 0x30 + 0x1b] +
                       (*(uint *)(DAT_805cb590 + 0x15c) & 0xffff) * 0x18);
    local_7c = *pfVar2;
    local_78 = pfVar2[1];
    local_74 = pfVar2[2];
    mtx4_set_rotation_y((double)((FLOAT_80618cac * *(float *)(ctx + 0x70)) / FLOAT_80618c5c),
                        afStack_58);
    mtx4_transform_vector(afStack_58, &local_7c);
    dVar7 = (double)local_78;
    local_7c = local_7c + *(float *)(ctx + 100);
    dVar6 = (double)local_74;
    local_78 = (float)(dVar7 + (double)*(float *)(ctx + 0x68));
    local_74 = (float)(dVar6 + (double)*(float *)(ctx + 0x6c));
    local_94 = *(float *)(ctx + 0xc0);
    local_90 = *(undefined4 *)(ctx + 0x70);
    local_8c = *(undefined4 *)(ctx + 200);
    DAT_805cb5e8 = entity_spawn_with_activation(dVar6, dVar7, a3, a4, a5, a6, a7, a8, 1, 0x298,
                                                ctx + 100, &local_94, 0, 0);
    local_78 = local_78 - FLOAT_80618c2c;
    player_ai_explosion_spawn(0x16, 0, &local_7c, &local_70, 0, 0, 1);
    player_ai_explosion_spawn(0x12, 0, (float *)(ctx + 100), &local_70, 0, 0, 1);
    local_78 = local_78 + FLOAT_80618c8c;
    FUN_802517b0((double)((float)((double)FLOAT_80618cac *
                                  (double)(float)((double)*(float *)(ctx + 0x70) +
                                                  (double)*(float *)(rootBlk + 0x130))) / FLOAT_80618c5c),
                 (double)*(float *)(ctx + 0x70), (double)FLOAT_80618cac, a4, a5, a6, a7, a8,
                 (undefined4)(unsigned)(int)&local_7c, (undefined4)(unsigned)(int)&local_88);
    *(undefined4 *)(ctx + 100) = local_64;
    i = 0;
    off = 0;
    *(undefined4 *)(ctx + 0x68) = local_60;
    *(undefined4 *)(ctx + 0x6c) = local_5c;
    do {
        if (*(int *)(DAT_805cb598 + off) < *(int *)(ctx + 0x1e8)) {
            piVar3 = (int *)(*(int *)(ctx + 0x1fc) + *(int *)(DAT_805cb598 + off) * 4);
        }
        else {
            piVar3 = (int *)&DAT_80611e2c;
        }
        local_98 = *piVar3;
        particle_cached_disable(&local_98);
        i = i + 1;
        off = off + 0xc;
    } while (i < 10);
    dVar6 = (double)FLOAT_80618c64;
    dVar7 = (double)FLOAT_80618ce8;
    *(uint *)(ctx + 0x28) = *(uint *)(ctx + 0x28) & 0xfffffffe;
    *(uint *)(ctx + 0x28) = *(uint *)(ctx + 0x28) & 0xfbffffff;
    sound_play_stereo_pair(dVar6, dVar7, (double)FLOAT_80618cec);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// tcv_spawn_debris_pair (0x8024ed2c)
// Spawn a pair of debris explosions (0x12) at an offset from the member origin
// (rotated by member yaw) and at the origin, then reset the member render bits.
// Called each frame by tcv_active_countdown_tick.
// ---------------------------------------------------------------------------
void FUN_8024ed2c(int member)
{
    float local_68, local_64, local_60;
    float local_5c, local_58, local_54;
    float afStack_50[19];

    local_5c = (float)DAT_803e043c;
    local_58 = (float)DAT_803e0440;
    local_54 = (float)DAT_803e0444;
    local_68 = (float)DAT_803e0448;
    local_64 = (float)DAT_803e044c;
    local_60 = (float)DAT_803e0450;
    mtx4_set_rotation_y((double)((FLOAT_80618cac * *(float *)(member + 0x70)) / FLOAT_80618c5c),
                        afStack_50);
    mtx4_transform_vector(afStack_50, &local_68);
    mtx4_transform_vector(afStack_50, &local_5c);
    local_68 = local_68 + *(float *)(member + 100);
    local_64 = local_64 + *(float *)(member + 0x68);
    local_60 = local_60 + *(float *)(member + 0x6c);
    player_ai_explosion_spawn(0x12, 0, &local_68, &local_5c, 0, 0, 1);
    local_5c = FLOAT_80618c44;
    local_58 = FLOAT_80618c2c;
    local_54 = FLOAT_80618c44;
    player_ai_explosion_spawn(0x12, 0, (float *)(member + 100), &local_5c, 0, 0, 1);
    *(uint *)(*(int *)(member + 4) + 0x30) = *(uint *)(*(int *)(member + 4) + 0x30) & 0xfffffffe;
    *(uint *)(*(int *)(member + 4) + 0x30) = *(uint *)(*(int *)(member + 4) + 0x30) | 2;
    *(uint *)(*(int *)(member + 4) + 0x30) = *(uint *)(*(int *)(member + 4) + 0x30) & 0xfffffffb;
}

// ---------------------------------------------------------------------------
// tcv_target_in_region (0x8024ee7c)
// True when there is an active target (DAT_805cb5e0) and it is inside one of the
// arena regions (delegates to FUN_8024eef8 on the target origin).
// ---------------------------------------------------------------------------
int FUN_8024ee7c(void)
{
    if (DAT_805cb5e0 == 0) {
        return 0;
    }
    int r = FUN_8024eef8((float *)(DAT_805cb5e0 + 100));
    return (r != 0) ? 1 : 0;
}

// ===========================================================================
// batch_725: TCV arm/skeleton sub-update layer - small phase/region helpers
//            plus the per-arm aim/attack behavior callbacks (referenced from
//            the arm-record function-pointer table @0x8046d9b4). APPEND.
// ===========================================================================

// ---------------------------------------------------------------------------
// tcv_target_below_threshold (0x8024eec4)
// True when there is an active target and its X (+0x64) is below FLOAT_80618c30.
// ---------------------------------------------------------------------------
int FUN_8024eec4(void)
{
    if (DAT_805cb5e0 != 0) {
        return (*(float *)(DAT_805cb5e0 + 0x68) < FLOAT_80618c30) ? 1 : 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// tcv_point_in_arena_region (0x8024eef8)
// Test whether a point is inside any of the 3 arena rectangles in the table at
// DAT_8046db08 (stride 0x10: x0,x1 at +0/+8, z0,z1 at +4/+0xc). Returns 1 on
// first containing region, 0 if none.
// ---------------------------------------------------------------------------
int FUN_8024eef8(float *pos)
{
    float x0, x1, z0, z1, tmp;
    int off;
    int remaining;

    off = 0;
    remaining = 3;
    while (true) {
        x0  = *(float *)((int)&DAT_8046db08 + off);
        x1  = *(float *)((int)&DAT_8046db08 + off + 8);
        z0  = *(float *)((int)&DAT_8046db08 + off + 4);
        z1  = *(float *)((int)&DAT_8046db08 + off + 0xc);
        tmp = x0;
        if (x1 < x0) {   // normalize x range
            tmp = x1;
            x1  = x0;
        }
        x0 = z0;
        if (z1 < z0) {   // normalize z range
            x0 = z1;
            z1 = z0;
        }
        if (((pos[0] < x1) && (tmp < pos[0])) && (pos[2] < z1) && (x0 < pos[2])) break;
        off = off + 0x10;
        remaining = remaining + -1;
        if (remaining == 0) {
            return 0;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// tcv_on_phase_enter (0x8024ef88)
// On-enter handler run once per phase change (DAT_805cb5c8 tracks last phase):
// sets the arm-active gate DAT_805cb5c0, and for phase 7 primes the 5 arm
// records with the engage color/mode triple (chosen by target height), for
// phase 4 fires the arm vtbl init callback (+0x168). Resets phase timer.
// ---------------------------------------------------------------------------
void FUN_8024ef88(void)
{
    float fVar1;
    float fVar2;
    float fVar3;
    int iVar4;
    int iVar5;
    int iVar6;
    int iVar7;

    if (DAT_805cb5c8 != DAT_805cb5c4) {
        switch (DAT_805cb5c4) {
        case 1:
        case 2:
        case 3:
        case 6:
            DAT_805cb5c0 = 1;
            break;
        case 4:
            (**(void (***)(void))(DAT_805cb590 + 0x168))();
            if (DAT_805cb5dc == 0) {
                DAT_805cb5dc = 1;
            }
            DAT_805cb5c0 = 0;
            break;
        case 5:
        case 9:
        case 10:
            DAT_805cb5c0 = 0;
            break;
        case 7:
            fVar1 = FLOAT_80618d08;
            fVar2 = FLOAT_80618d0c;
            fVar3 = FLOAT_80618cf0;
            if (FLOAT_80618cf8 < *(float *)(DAT_805cb5e0 + 100)) {
                fVar1 = FLOAT_80618cfc;
                fVar2 = FLOAT_80618d00;
                fVar3 = FLOAT_80618d04;
            }
            iVar5 = 0;
            iVar7 = 5;
            do {
                iVar6 = iVar5 + 0x14;
                iVar4 = iVar5 + 0x18;
                *(float *)(DAT_805cb590 + iVar5 + 0x10) = fVar1;
                iVar5 = iVar5 + 0x4c;
                *(float *)(DAT_805cb590 + iVar6) = fVar2;
                *(float *)(DAT_805cb590 + iVar4) = fVar3;
                iVar7 = iVar7 + -1;
            } while (iVar7 != 0);
            *(undefined4 *)(DAT_805cb590 + 0x14c) = 100;
            FUN_802506a8();
            DAT_805cb5c0 = 1;
            break;
        case 8:
            FUN_802506a8();
            DAT_805cb5c0 = 1;
        }
        DAT_805cb5c8 = DAT_805cb5c4;
        DAT_805cb5cc = 0;
    }
}

// ---------------------------------------------------------------------------
// tcv_smooth_arm_spins (0x8024f118)
// Per-frame smoothing of the 10 member spin values: apply the cached drag, then
// for the first 8 either copy or ease the current spin toward its target
// (snap by FLOAT_80618d10 when the gap is large, otherwise decay). Lock ctx.
// ---------------------------------------------------------------------------
void FUN_8024f118(void)
{
    float fVar1;
    float fVar2;
    int ctx;
    int *pHandle;
    int *pRec;
    int i;
    int off;
    int local_28[10];

    ctx = FUN_803826e8();
    i = 0;
    off = 0;
    do {
        pRec = (int *)(DAT_805cb598 + off);
        if (*pRec < *(int *)(ctx + 0x1e8)) {
            pHandle = (int *)(*(int *)(ctx + 0x1fc) + *pRec * 4);
        }
        else {
            pHandle = (int *)&DAT_80611e2c;
        }
        local_28[0] = *pHandle;
        particle_cached_set_drag((double)(float)pRec[2], local_28);
        if (i < 8) {
            if ((i == 0) || (i == 1)) {
                pRec[2] = pRec[1];
            }
            else {
                fVar1 = (float)pRec[1];
                fVar2 = (float)pRec[2];
                if (FLOAT_80618c98 <= ABS(fVar1 - fVar2)) {
                    if (fVar1 <= fVar2) {
                        pRec[2] = (int)(fVar2 - FLOAT_80618d10);
                    }
                    else {
                        pRec[2] = (int)(fVar2 + FLOAT_80618d10);
                    }
                }
                else {
                    if (fVar1 <= FLOAT_80618c74) {
                        pRec[1] = (int)(fVar1 * FLOAT_80618c40);
                    }
                    else {
                        pRec[1] = (int)(fVar1 * FLOAT_80618ccc);
                    }
                    pRec[2] = (int)fVar1;
                }
            }
        }
        i = i + 1;
        off = off + 0xc;
    } while (i < 10);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// tcv_build_node_collision_face (0x8024f23c)
// For one arm node: allocate a matrix, translate it to the node's Y offset, and
// register a collision face entry. Bracketed by the fx-list lock.
// ---------------------------------------------------------------------------
void FUN_8024f23c(void)
{
    undefined4 mtx;
    unsigned long long ctxPair;
    int ctxLo;
    int ctxHi;

    ctxPair = (unsigned long long)(unsigned)FUN_803826f0();  // packed hi=block, lo=node ptr
    ctxLo = (int)(unsigned)ctxPair;
    ctxHi = (int)(ctxPair >> 0x20);
    mtx = mtx4_pool_alloc();
    mtx4_set_translation((double)FLOAT_80618c44, (double)*(float *)(ctxLo + 0xc), (double)FLOAT_80618c44);
    collision_set_face_entry(*(undefined4 *)(ctxHi + 4), *(undefined4 *)(ctxLo + 8), mtx);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// tcv_ease_arm_scalar (0x8024f298)
// Ease one arm scalar (+0xc) toward its integrated value (+0xc + step*+0x14)
// using the smoothstep curve FUN_80390324.
// ---------------------------------------------------------------------------
void FUN_8024f298(int arm)
{
    double integrated;
    double eased;

    integrated = (double)(FLOAT_806108ec * *(float *)(arm + 0x14) + *(float *)(arm + 0xc));
    eased = FUN_80390324((double)((float)(integrated - (double)FLOAT_80618c44) / FLOAT_80618c2c));
    *(float *)(arm + 0xc) = -(float)((double)FLOAT_80618c2c * (double)(float)eased - integrated);
}

// ---------------------------------------------------------------------------
// tcv_arm_fire_beam (0x8024f35c)
// Arm behavior callback: play the beam SFX 0xc47 at the arm origin and spawn the
// type-8 beam projectile (projId 0x10000e), then set the arm charge/z state.
// ---------------------------------------------------------------------------
void FUN_8024f35c(undefined4 a1, undefined4 a2, int *dir)
{
    int ctxHi;
    undefined4 *ctxLo;
    unsigned long long ctxPair;
    int local_40[6];
    float local_28;
    undefined4 local_24, local_20, local_1c;
    undefined1 local_18;

    ctxPair = (unsigned long long)(unsigned)FUN_803826f0();
    ctxHi = (int)(ctxPair >> 0x20);
    ctxLo = (undefined4 *)(unsigned)ctxPair;
    sound_init_play_params((int)local_40);
    local_18 = 1;
    local_40[0] = 0xc47;
    local_28 = FLOAT_80618c40;
    local_24 = *ctxLo;
    local_20 = ctxLo[1];
    local_1c = ctxLo[2];
    sound_play_request(local_40);
    player_ai_projectile_spawn_simple((double)FLOAT_80618c2c, (double)FLOAT_80618c64, 8, ctxLo, dir,
                                      ctxHi, (int *)0, 6, 0, *(int *)(ctxHi + 0x34), 0x10000e, 2);
    DAT_805cb5a4 = (int)FLOAT_80618c8c;
    DAT_805cb5ac = FLOAT_80618d18;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// tcv_arm_fire_bolt (0x8024f42c)
// Arm behavior callback (4 arms): apply a jittered spread to the fire direction,
// play SFX 0x205, spawn a trail particle and the type-4 bolt projectile
// (projId 0x184). arm4 selects the jitter state DAT_805cb894[arm4].
// ---------------------------------------------------------------------------
void FUN_8024f42c(undefined4 a1, undefined4 a2, float *dir, int arm4)
{
    int ctxHi;
    unsigned long long ctxPair;
    float local_50, local_4c, local_48;
    int local_44[7];
    undefined4 local_28, local_24, local_20;
    undefined1 local_1c;

    ctxPair = (unsigned long long)(unsigned)FUN_803826f0();
    ctxHi = (int)(ctxPair >> 0x20);
    player_ai_projectile_compute_jittered_dir((double)FLOAT_80618d1c, (double)FLOAT_80618d20,
                                              (float *)((int)&DAT_805cb894 + arm4 * 0x1c), &local_50);
    dir[0] = dir[0] + local_50;
    dir[1] = dir[1] + local_4c;
    dir[2] = dir[2] + local_48;
    sound_init_play_params((int)local_44);
    local_1c = 1;
    local_44[0] = 0x205;
    local_28 = *(undefined4 *)(ctxHi + 100);
    local_24 = *(undefined4 *)(ctxHi + 0x68);
    local_20 = *(undefined4 *)(ctxHi + 0x6c);
    sound_play_request(local_44);
    player_ai_spawn_trail_particle(0, 0, 0xffffffff, (float *)(unsigned)ctxPair, dir, -1);
    player_ai_projectile_spawn_simple((double)FLOAT_80618c2c, (double)FLOAT_80618c64, 4,
                                      (void *)(unsigned)ctxPair, (int *)dir, ctxHi, (int *)0, 6, 0,
                                      *(int *)(ctxHi + 0x34), 0x184, 0x16);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// tcv_arm_select_nearest_member (0x8024f548)
// Arm target selection: scan the 5 member handles in the DAT_805cb59c table,
// skip any TCV5/6/7 already flagged destroyed (+0x30 & 2), and pick the first
// within FLOAT_80618ca4 of the active target; store its position into the arm
// record (+0x10..+0x18). Decrement the pending-fire counter DAT_805cb5dc.
// ---------------------------------------------------------------------------
void FUN_8024f548(void)
{
    float fVar1;
    float fVar2;
    bool destroyed;
    int arm;
    int off;
    int member;
    int other;
    int i;
    double dist;

    arm = __save_gpr_25();
    i = 0;
    off = 0;
    member = 0;
    do {
        member = entity_handle_resolve(*(uint *)(DAT_805cb59c + off));
        if (member != 0) {
            destroyed = false;
            other = entity_handle_resolve(0x54435635);  // TCV5
            if ((member == other) && ((*(uint *)(*(int *)(member + 4) + 0x30) & 2) != 0)) {
                destroyed = true;
            }
            other = entity_handle_resolve(0x54435636);  // TCV6
            if ((member == other) && ((*(uint *)(*(int *)(member + 4) + 0x30) & 2) != 0)) {
                destroyed = true;
            }
            other = entity_handle_resolve(0x54435637);  // TCV7
            if ((member == other) && ((*(uint *)(*(int *)(member + 4) + 0x30) & 2) != 0)) {
                destroyed = true;
            }
            if ((!destroyed) &&
                (fVar1 = *(float *)(member + 100) - *(float *)(DAT_805cb5e0 + 100),
                 fVar2 = *(float *)(member + 0x6c) - *(float *)(DAT_805cb5e0 + 0x6c),
                 dist = math_sqrt((double)(fVar1 * fVar1 + fVar2 * fVar2)),
                 dist < (double)FLOAT_80618ca4)) {
                break;
            }
        }
        i = i + 1;
        off = off + 4;
        member = 0;
    } while (i < 5);

    if (member == 0) {
        if (DAT_805cb5e0 != 0) {
            int tgtRec = *(int *)(DAT_805cb5e0 + 0x7c);
            *(undefined4 *)(arm + 0x10) = *(undefined4 *)(tgtRec + 0xbb4);
            *(undefined4 *)(arm + 0x14) = *(undefined4 *)(tgtRec + 3000);
            *(undefined4 *)(arm + 0x18) = *(undefined4 *)(tgtRec + 0xbbc);
        }
    }
    else {
        *(undefined4 *)(arm + 0x10) = *(undefined4 *)(member + 100);
        *(undefined4 *)(arm + 0x14) = *(undefined4 *)(member + 0x68);
        *(undefined4 *)(arm + 0x18) = *(undefined4 *)(member + 0x6c);
    }
    DAT_805cb5dc = DAT_805cb5dc + -1;
    if (DAT_805cb5dc < 0) {
        DAT_805cb5dc = 0;
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// tcv_arm_slew_aim (0x8024f774)
// Arm behavior callback: slew the arm's aim scalar (+0) toward a clamped target
// pitch (param2, clamped to [FLOAT_80618d28, FLOAT_80618d14]) at a frame-scaled
// rate, integrate +3, drive the 3D loop SFX 0x206 by the aim delta, and advance
// the arm hold timer (+7). param5 flips the slew direction.
// ---------------------------------------------------------------------------
void FUN_8024f774(undefined8 a1, double param2, undefined4 a3, undefined4 a4, int param5)
{
    float fVar1;
    int entity;
    int *worldPos;
    float *arm;
    double dVar4;
    double extraout_f1;
    double dVar5;
    double dVar6;
    double dVar8;
    unsigned long long ctxPair;

    ctxPair = (unsigned long long)(unsigned)FUN_803826f0();
    entity = (int)(ctxPair >> 0x20);
    arm = (float *)(unsigned)ctxPair;
    dVar8 = (double)*arm;
    fVar1 = FLOAT_80618ce4 /
            (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38);
    double step = (double)(FLOAT_80618d24 * FLOAT_806108ec * fVar1);

    dVar4 = param2;
    if ((double)FLOAT_80618d14 < param2) {
        dVar4 = (double)FLOAT_80618d14;
    }
    if ((double)FLOAT_80618d28 <= dVar4) {
        if ((double)FLOAT_80618d14 < param2) {
            param2 = (double)FLOAT_80618d14;
        }
    }
    else {
        param2 = (double)FLOAT_80618d28;
    }
    dVar4 = extraout_f1;
    scalar_step_toward_snap(param2, (double)(FLOAT_80618c74 * FLOAT_806108ec * fVar1), arm + 3);
    if (ABS((double)(float)(dVar4 - (double)*arm)) < step) {
        *arm = (float)dVar4;
    }
    else {
        dVar5 = step;
        if (param5 != 0) {
            dVar5 = -step;
        }
        *arm = (float)((double)*arm + dVar5);
    }
    worldPos = entity_get_world_position(entity);
    dVar6 = (double)FLOAT_80618c2c;
    dVar5 = (double)FLOAT_80618c7c;
    ((float *)worldPos)[1] = (float)((double)(float)((float *)worldPos)[1] + dVar6);
    sound_update_3d_source((double)(float)(ABS((double)(float)(dVar8 - (double)*arm)) / step),
                           dVar5, dVar6, entity + 0x228, worldPos, 0x206, 0);
    dVar8 = (double)*arm;
    double eased = FUN_80390324((double)((float)(dVar8 - (double)FLOAT_80618cb4) / FLOAT_80618c60));
    fVar1 = FLOAT_80618ca4;
    *arm = -(float)((double)FLOAT_80618c60 * (double)(float)eased - dVar8);
    DAT_805cb5d4 = DAT_805cb5d4 + 1;
    if (ABS((float)(dVar4 - (double)*arm)) < fVar1) {
        arm[7] = (float)((int)arm[7] + DAT_806108e4);
    }
    FUN_8038273c();
}

// ===========================================================================
// batch_726: TCV arm aim/attack physics + enemy-target scan + arm-record commit.
//            These are the per-arm update workers driven by the master FSM. APPEND.
// ===========================================================================

// ---------------------------------------------------------------------------
// tcv_arm_slew_aim_clamped (0x8024f94c)
// Arm behavior callback variant: slew the arm aim scalar (+0) toward a clamped
// target pitch [FLOAT_80618cf8, FLOAT_80618d14] at a frame-scaled rate, take the
// min of the 3 scalar candidates, apply the smoothstep curve, and advance the
// hold timer (+7). param5 flips the slew direction.
// ---------------------------------------------------------------------------
void FUN_8024f94c(double a1, double param2, undefined4 a3, float *arm, int param5)
{
    float fVar1, fVar2, fVar3, fVar4;
    double step;
    double clamped;

    fVar1 = FLOAT_80618ce4 /
            (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38);
    step = (double)(FLOAT_806108ec * fVar1);

    clamped = param2;
    if ((double)FLOAT_80618d14 < param2) {
        clamped = (double)FLOAT_80618d14;
    }
    if ((double)FLOAT_80618cf8 <= clamped) {
        if ((double)FLOAT_80618d14 < param2) {
            param2 = (double)FLOAT_80618d14;
        }
    }
    else {
        param2 = (double)FLOAT_80618cf8;
    }
    scalar_step_toward_snap(param2, (double)(FLOAT_80618c74 * FLOAT_806108ec * fVar1), arm + 3);
    if (ABS((double)(float)(a1 - (double)*arm)) < step) {
        *arm = (float)a1;
    }
    else {
        if (param5 != 0) {
            step = -step;
        }
        *arm = (float)((double)*arm + step);
    }
    fVar1 = arm[1];
    fVar2 = *arm;
    fVar3 = arm[2];
    fVar4 = fVar2;
    if (fVar1 < fVar2) {
        fVar4 = fVar1;
    }
    if ((fVar3 <= fVar4) && (fVar3 = fVar2, fVar1 < fVar2)) {
        fVar3 = fVar1;
    }
    *arm = fVar3;
    double d = (double)*arm;
    double eased = FUN_80390324((double)((float)(d - (double)FLOAT_80618cb4) / FLOAT_80618c60));
    *arm = -(float)((double)FLOAT_80618c60 * (double)(float)eased - d);
    arm[7] = (float)((int)arm[7] + DAT_806108e4);
}

// ---------------------------------------------------------------------------
// tcv_arm_strike_update (0x8024fadc)
// Per-arm strike/collision update: when the arm's hold timer (+7) has expired
// (rate gated by mode +8), build the arm-tip world transform, aim at the target
// (atan2 + smoothstep via the arm vtbl +0x11 predicate), and on hit build the
// full 4-joint arm segment transform and fire the strike vtbl +0xf callback.
// ---------------------------------------------------------------------------
void FUN_8024fadc(void)
{
    float fVar1, fVar2;
    int iVar3, iVar5, iVar6;
    int arm4;
    int *piVar7;
    float *pfVar8, *pfVar9, *pfVar10, *pfVar11, *pfVar12;
    double dVar13, dVar14, dVar15;
    unsigned long long uVar16;
    float local_f8, local_f4, local_f0, local_ec, local_e8, local_e4;
    float local_e0, local_dc, local_d8;
    float afStack_d4[16];
    float afStack_94[17];

    uVar16 = (unsigned long long)(unsigned)__save_gpr_25();
    arm4 = (int)(uVar16 >> 0x20);
    pfVar8 = (float *)(DAT_805cb590 + (int)(unsigned)uVar16 * 0x4c);
    fVar2 = pfVar8[8];
    if (fVar2 == 1.4013e-45f) {   // denormal sentinel (raw 0x00000001)
        fVar1 = FLOAT_80618c40 *
                (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38);
    }
    else {
        fVar1 = FLOAT_80618c44;
        if (((int)fVar2 < 1) && (-1 < (int)fVar2)) {
            fVar1 = FLOAT_80618c98 *
                    (float)((double)(longlong)(unsigned)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618c38);
        }
    }
    float holdNeg = -pfVar8[7];
    if (fVar1 < (float)((double)(longlong)(unsigned)*(unsigned int *)&holdNeg - DOUBLE_80618c38)) {
        local_e0 = DAT_803e0454;
        local_dc = DAT_803e0458;
        local_d8 = DAT_803e045c;
        local_ec = DAT_803e0460;
        local_e8 = DAT_803e0464;
        local_e4 = DAT_803e0468;
        piVar7 = *(int **)(*(int *)(arm4 + 4) + 4);
        iVar3 = *piVar7;
        pfVar9 = (float *)piVar7[iVar3 * -0x30 + 0xab];
        pfVar12 = (float *)(piVar7[iVar3 * -0x30 + ((int)pfVar8[0xd] >> 0x10) * 0x30 + 0x1b] +
                            ((uint)pfVar8[0xd] & 0xffff) * 0x18);
        pfVar11 = (float *)(piVar7[iVar3 * -0x30 + ((int)pfVar8[0xc] >> 0x10) * 0x30 + 0x1b] +
                            ((uint)pfVar8[0xc] & 0xffff) * 0x18);
        pfVar10 = (float *)(piVar7[iVar3 * -0x30 + ((int)pfVar8[0xb] >> 0x10) * 0x30 + 0x1b] +
                            ((uint)pfVar8[0xb] & 0xffff) * 0x18);
        mtx4_set_rotation_zyx((double)((FLOAT_80618cac * pfVar8[3]) / FLOAT_80618c5c),
                              (double)((FLOAT_80618cac * (*pfVar8 + *(float *)(arm4 + 0x70))) / FLOAT_80618c5c),
                              (double)FLOAT_80618c44, afStack_d4);
        mtx4_transform_point(afStack_d4, &local_e0);
        piVar7 = *(int **)(*(int *)(arm4 + 4) + 4);
        iVar6 = (int)pfVar8[0xb] >> 0x10;
        iVar3 = *piVar7;
        iVar5 = ((uint)pfVar8[0xb] & 0xffff) * 0x18;
        local_f8 = *(float *)(piVar7[iVar3 * -0x30 + iVar6 * 0x30 + 0x1b] + iVar5);
        local_f4 = *(float *)(piVar7[iVar3 * -0x30 + iVar6 * 0x30 + 0x1b] + iVar5 + 4);
        local_f0 = *(float *)(piVar7[iVar3 * -0x30 + iVar6 * 0x30 + 0x1b] + iVar5 + 8);
        mtx4_set_translation((double)*(float *)(arm4 + 100), (double)*(float *)(arm4 + 0x68),
                             (double)*(float *)(arm4 + 0x6c));
        mtx4_set_rotation_y((double)((FLOAT_80618cac * *(float *)(arm4 + 0x70)) / FLOAT_80618c5c), afStack_94);
        mtx4_multiply(afStack_94, afStack_d4, afStack_d4);
        mtx4_transform_point(afStack_d4, &local_f8);
        dVar14 = (double)(pfVar8[4] - local_f8);
        dVar15 = (double)(pfVar8[6] - local_f0);
        fVar2 = (float)(dVar15 * dVar15 +
                        (double)(float)(dVar14 * dVar14 +
                                        (double)((pfVar8[5] - local_f4) * (pfVar8[5] - local_f4))));
        if (FLOAT_80618cb0 < fVar2) {
            fVar1 = 1.0f / SQRT(fVar2);
            fVar1 = fVar1 * -(FLOAT_80618c74 * fVar2 * fVar1 * fVar1 - FLOAT_80618c8c);
            dVar13 = (double)(FLOAT_80618c2c *
                              fVar1 * -(FLOAT_80618c74 * fVar2 * fVar1 * fVar1 - FLOAT_80618c8c));
            dVar14 = (double)(float)(dVar14 * dVar13);
            dVar15 = (double)(float)(dVar15 * dVar13);
        }
        fVar2 = local_d8 * local_d8 + local_e0 * local_e0 + local_dc * local_dc;
        if (FLOAT_80618cb0 < fVar2) {
            fVar1 = 1.0f / SQRT(fVar2);
            fVar1 = fVar1 * -(FLOAT_80618c74 * fVar2 * fVar1 * fVar1 - FLOAT_80618c8c);
            fVar2 = FLOAT_80618c2c * fVar1 * -(FLOAT_80618c74 * fVar2 * fVar1 * fVar1 - FLOAT_80618c8c);
            local_e0 = local_e0 * fVar2;
            local_dc = local_dc * fVar2;
            local_d8 = local_d8 * fVar2;
        }
        dVar14 = math_atan2(dVar14, dVar15);
        dVar15 = (double)(((FLOAT_80618c5c * (float)dVar14) / FLOAT_80618cac - *(float *)(arm4 + 0x70)) + FLOAT_80618c5c);
        dVar14 = FUN_80390324((double)((float)(dVar15 - (double)FLOAT_80618cb4) / FLOAT_80618c60));
        iVar3 = ((int (*)(double, float *))(void *)pfVar8[0x11])
                    (-(double)(float)((double)FLOAT_80618c60 * (double)(float)dVar14 - dVar15), pfVar8);
        if (iVar3 != 0) {
            local_ec = *pfVar12 - *pfVar11;
            local_e8 = pfVar12[1] - pfVar11[1];
            local_e4 = pfVar12[2] - pfVar11[2];
            local_f8 = *pfVar10 - *pfVar9;
            local_f4 = pfVar10[1] - pfVar9[1];
            local_f0 = pfVar10[2] - pfVar9[2];
            mtx4_set_translation((double)(*pfVar11 - *pfVar10), (double)(pfVar11[1] - pfVar10[1]),
                                 (double)(pfVar11[2] - pfVar10[2]));
            mtx4_set_rotation_x((double)((FLOAT_80618cac * pfVar8[3]) / FLOAT_80618c5c), (int)afStack_94);
            mtx4_multiply(afStack_94, afStack_d4, afStack_d4);
            mtx4_transform_point(afStack_d4, &local_ec);
            mtx4_set_translation((double)local_f8, (double)local_f4, (double)local_f0);
            mtx4_set_rotation_y((double)((FLOAT_80618cac * *pfVar8) / FLOAT_80618c5c), afStack_94);
            mtx4_multiply(afStack_94, afStack_d4, afStack_d4);
            mtx4_transform_point(afStack_d4, &local_ec);
            mtx4_set_translation((double)*pfVar9, (double)pfVar9[1], (double)pfVar9[2]);
            mtx4_set_rotation_x((double)((FLOAT_80618cac * DAT_805cb5a8) / FLOAT_80618c5c), (int)afStack_94);
            mtx4_multiply(afStack_94, afStack_d4, afStack_d4);
            mtx4_transform_point(afStack_d4, &local_ec);
            mtx4_set_translation((double)*(float *)(arm4 + 100), (double)*(float *)(arm4 + 0x68),
                                 (double)*(float *)(arm4 + 0x6c));
            mtx4_set_rotation_y((double)((FLOAT_80618cac * *(float *)(arm4 + 0x70)) / FLOAT_80618c5c), afStack_94);
            mtx4_multiply(afStack_94, afStack_d4, afStack_d4);
            mtx4_transform_point(afStack_d4, &local_ec);
            ((void (*)(int, float *, float *, int))(void *)pfVar8[0xf])(arm4, &local_ec, &local_e0, (int)(unsigned)uVar16);
            pfVar8[7] = 0.0f;
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// tcv_arm_aim_update (0x802500a0)
// Per-arm aim update: build the arm-tip world position, compute the aim yaw and
// pitch to the arm's target point (+0x10..+0x18) via atan2, apply the smoothstep
// curve, and drive the arm's aim vtbl +0x10 callback. Non-4 arms first run their
// self-update vtbl +0xe. Lock FUN_803826f0/8038273c.
// ---------------------------------------------------------------------------
void FUN_802500a0(void)
{
    float fVar1, fVar2;
    int iVar3, iVar4, iVar5, iVar6, iVar7;
    int *piVar8;
    float *pfVar9;
    double dVar10, dVar11, dVar12, dVar13, dVar14;
    unsigned long long uVar15;
    float local_f8, local_f4, local_f0, local_ec, local_e8, local_e4;
    float afStack_e0[16];
    float afStack_a0[22];

    uVar15 = (unsigned long long)(unsigned)FUN_803826f0();
    iVar3 = (int)(uVar15 >> 0x20);
    iVar4 = (int)(unsigned)uVar15;
    local_ec = DAT_803e046c;
    pfVar9 = (float *)(DAT_805cb590 + iVar4 * 0x4c);
    local_e8 = DAT_803e0470;
    local_e4 = DAT_803e0474;
    if (iVar4 != 4) {
        ((void (*)(float *))(void *)pfVar9[0xe])(pfVar9);
    }
    mtx4_set_rotation_zyx((double)((FLOAT_80618cac * pfVar9[3]) / FLOAT_80618c5c),
                          (double)((FLOAT_80618cac * (*pfVar9 + *(float *)(iVar3 + 0x70))) / FLOAT_80618c5c),
                          (double)FLOAT_80618c44, afStack_e0);
    mtx4_transform_point(afStack_e0, &local_ec);
    piVar8 = *(int **)(*(int *)(iVar3 + 4) + 4);
    iVar7 = (int)pfVar9[0xb] >> 0x10;
    iVar5 = *piVar8;
    iVar6 = ((uint)pfVar9[0xb] & 0xffff) * 0x18;
    local_f8 = *(float *)(piVar8[iVar5 * -0x30 + iVar7 * 0x30 + 0x1b] + iVar6);
    local_f4 = *(float *)(piVar8[iVar5 * -0x30 + iVar7 * 0x30 + 0x1b] + iVar6 + 4);
    local_f0 = *(float *)(piVar8[iVar5 * -0x30 + iVar7 * 0x30 + 0x1b] + iVar6 + 8);
    mtx4_set_translation((double)*(float *)(iVar3 + 100), (double)*(float *)(iVar3 + 0x68),
                         (double)*(float *)(iVar3 + 0x6c));
    mtx4_set_rotation_y((double)((FLOAT_80618cac * *(float *)(iVar3 + 0x70)) / FLOAT_80618c5c), afStack_a0);
    mtx4_multiply(afStack_a0, afStack_e0, afStack_e0);
    mtx4_transform_point(afStack_e0, &local_f8);
    if (iVar4 != 4) {
        local_f4 = local_f4 + FLOAT_80618cc0;
    }
    dVar13 = (double)(pfVar9[5] - local_f4);
    dVar14 = (double)(pfVar9[4] - local_f8);
    dVar12 = (double)(pfVar9[6] - local_f0);
    fVar1 = (float)(dVar12 * dVar12 + (double)(float)(dVar14 * dVar14 + (double)(float)(dVar13 * dVar13)));
    if (FLOAT_80618cb0 < fVar1) {
        fVar2 = 1.0f / SQRT(fVar1);
        fVar2 = fVar2 * -(FLOAT_80618c74 * fVar1 * fVar2 * fVar2 - FLOAT_80618c8c);
        dVar10 = (double)(FLOAT_80618c2c * fVar2 * -(FLOAT_80618c74 * fVar1 * fVar2 * fVar2 - FLOAT_80618c8c));
        dVar14 = (double)(float)(dVar14 * dVar10);
        dVar13 = (double)(float)(dVar13 * dVar10);
        dVar12 = (double)(float)(dVar12 * dVar10);
    }
    fVar1 = local_e4 * local_e4 + local_ec * local_ec + local_e8 * local_e8;
    if (FLOAT_80618cb0 < fVar1) {
        fVar2 = 1.0f / SQRT(fVar1);
        fVar2 = fVar2 * -(FLOAT_80618c74 * fVar1 * fVar2 * fVar2 - FLOAT_80618c8c);
        fVar1 = FLOAT_80618c2c * fVar2 * -(FLOAT_80618c74 * fVar1 * fVar2 * fVar2 - FLOAT_80618c8c);
        local_ec = local_ec * fVar1;
        local_e8 = local_e8 * fVar1;
        local_e4 = local_e4 * fVar1;
    }
    dVar10 = math_sqrt((double)(float)(dVar14 * dVar14 + (double)(float)(dVar12 * dVar12)));
    dVar13 = math_atan2(dVar13, dVar10);
    dVar10 = (double)(float)dVar13;
    dVar13 = math_atan2(dVar14, dVar12);
    dVar11 = (double)(((FLOAT_80618c5c * (float)dVar13) / FLOAT_80618cac - *(float *)(iVar3 + 0x70)) + FLOAT_80618c5c);
    dVar13 = FUN_80390324((double)((float)(dVar11 - (double)FLOAT_80618cb4) / FLOAT_80618c60));
    ((void (*)(double, double, int, float *, int))(void *)pfVar9[0x10])
        (-(double)(float)((double)FLOAT_80618c60 * (double)(float)dVar13 - dVar11),
         (double)((float)((double)FLOAT_80618c5c * dVar10) / FLOAT_80618cac), iVar3, pfVar9,
         (float)(dVar12 * (double)local_ec - (double)(float)(dVar14 * (double)local_e4)) < FLOAT_80618c44);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// tcv_scan_target (0x8025041c)
// Scan all active players (DAT_80611d78, stride 0x141c, count DAT_80612610) for
// the one with the largest aim-error magnitude relative to the boss facing +
// sine-table-derived aim vector; store the winner into DAT_805cb5e0 (and its
// error into DAT_805cb5e4). Skips players whose +0x1cc timer is zero.
// Lock FUN_803826e8/80382734.
// ---------------------------------------------------------------------------
void FUN_8025041c(void)
{
    uint uVar1;
    float fVar2, fVar3;
    int ctx;
    int off;
    int idx;
    int player;
    double dVar8, dVar9, dVar10, dVar11, dVar12, dVar13, dVar14;

    ctx = FUN_803826e8();
    dVar12 = (double)FLOAT_80618c44;
    uVar1 = (uint)(FLOAT_80618ca8 * ((FLOAT_80618cac * *(float *)(ctx + 0x70)) / FLOAT_80618c5c) + FLOAT_80618c74);
    dVar14 = (double)(FLOAT_80618ca4 * DAT_80583228[uVar1 & 0x7ff]);
    dVar13 = (double)(FLOAT_80618ca4 * DAT_80583228[(uVar1 + 0x200) & 0x7ff]);
    fVar2 = (float)(dVar13 * dVar13 + (double)(float)(dVar14 * dVar14 + dVar12));
    if (FLOAT_80618cb0 < fVar2) {
        fVar3 = 1.0f / SQRT(fVar2);
        fVar3 = fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c);
        dVar8 = (double)(FLOAT_80618c2c * fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c));
        dVar14 = (double)(float)(dVar14 * dVar8);
        dVar13 = (double)(float)(dVar13 * dVar8);
    }
    off = 0;
    dVar8 = (double)FLOAT_80618c44;
    DAT_805cb5e0 = 0;
    for (idx = 0; idx < DAT_80612610; idx = idx + 1) {
        player = *(int *)(DAT_80611d78 + off + 0x14);
        if (dVar8 < (double)*(float *)(player + 0x1cc)) {
            fVar2 = *(float *)(ctx + 0x68) - *(float *)(player + 0x68);
            dVar10 = (double)(*(float *)(ctx + 100) - *(float *)(player + 100));
            dVar11 = (double)(*(float *)(ctx + 0x6c) - *(float *)(player + 0x6c));
            fVar2 = (float)(dVar11 * dVar11 + (double)(float)(dVar10 * dVar10 + (double)(fVar2 * fVar2)));
            if (FLOAT_80618cb0 < fVar2) {
                fVar3 = 1.0f / SQRT(fVar2);
                fVar3 = fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c);
                dVar9 = (double)(FLOAT_80618c2c * fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c));
                dVar10 = (double)(float)(dVar10 * dVar9);
                dVar11 = (double)(float)(dVar11 * dVar9);
            }
            dVar11 = (double)(float)(dVar10 * dVar14 + (double)(float)(dVar11 * dVar13));
            dVar10 = dVar11;
            if ((double)FLOAT_80618c2c < dVar11) {
                dVar10 = (double)FLOAT_80618c2c;
            }
            if ((double)FLOAT_80618c30 <= dVar10) {
                if ((double)FLOAT_80618c2c < dVar11) {
                    dVar11 = (double)FLOAT_80618c2c;
                }
            }
            else {
                dVar11 = (double)FLOAT_80618c30;
            }
            dVar10 = math_asin(dVar11);
            dVar11 = (double)((float)((double)FLOAT_80618c5c * dVar10) / FLOAT_80618cac);
            dVar10 = FUN_80390324((double)((float)(dVar11 - (double)FLOAT_80618cb4) / FLOAT_80618c60));
            dVar10 = -(double)(float)((double)FLOAT_80618c60 * (double)(float)dVar10 - dVar11);
            if ((ABS(dVar12) < ABS(dVar10)) || (DAT_805cb5e0 == 0)) {
                DAT_805cb5e4 = (float)dVar10;
                dVar12 = dVar10;
                DAT_805cb5e0 = player;
            }
        }
        off = off + 0x141c;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// tcv_pick_arm_focus (0x802506a8)
// Choose the world-space focus point the arms aim at, by phase: phase6 -> the
// target origin, phase8 -> a fixed point, phase7 -> a root-bone-derived point,
// else search a jittered ring of candidate points (via math_poly_trig) inside
// the arena for the farthest one from the boss. Commits via FUN_802509b8.
// ---------------------------------------------------------------------------
void FUN_802506a8(void)
{
    float fVar1, fVar2;
    int ctx;
    int iVar4, iVar5;
    double dVar6, dVar7, dVar8, dVar9, dVar10, dVar11, dVar12;
    double dVar13, dVar14, dVar15, dVar16, dVar17;
    float local_f8;
    undefined4 local_f4;
    float local_f0;
    undefined4 local_ec;
    float local_e8, local_e4;
    undefined4 local_e0, local_dc, local_d8;
    undefined4 local_d0;
    undefined4 uStack_cc;

    ctx = FUN_803826f0();
    if (DAT_805cb5c4 == 6) {
        FUN_802509b8((undefined4 *)(DAT_805cb5e0 + 100));
    }
    else if (DAT_805cb5c4 == 8) {
        local_e0 = (undefined4)DAT_803e0478;
        local_dc = (undefined4)DAT_803e047c;
        local_d8 = (undefined4)DAT_803e0480;
        FUN_802509b8(&local_e0);
    }
    else if (DAT_805cb5c4 == 7) {
        local_ec = *(undefined4 *)(DAT_805cb590 + 0x140);
        local_e4 = *(float *)(DAT_805cb590 + 0x148) - FLOAT_80618cf0;
        local_e8 = FLOAT_80618d2c;
        FUN_802509b8(&local_ec);
    }
    else if (DAT_805cb5e0 != 0) {
        dVar12 = (double)FLOAT_80618c44;
        iVar5 = 0;
        dVar8 = (double)*(float *)(DAT_805cb5e0 + 100);
        dVar9 = (double)*(float *)(DAT_805cb5e0 + 0x6c);
        dVar7 = dVar8;
        if ((double)FLOAT_80618d28 < dVar8) {
            dVar7 = (double)FLOAT_80618d28;
        }
        if ((double)FLOAT_80618d30 <= dVar7) {
            if ((double)FLOAT_80618d28 < dVar8) {
                dVar8 = (double)FLOAT_80618d28;
            }
        }
        else {
            dVar8 = (double)FLOAT_80618d30;
        }
        dVar7 = dVar9;
        if ((double)FLOAT_80618d34 < dVar9) {
            dVar7 = (double)FLOAT_80618d34;
        }
        if ((double)FLOAT_80618cf8 <= dVar7) {
            if ((double)FLOAT_80618d34 < dVar9) {
                dVar9 = (double)FLOAT_80618d34;
            }
        }
        else {
            dVar9 = (double)FLOAT_80618cf8;
        }
        dVar14 = (double)FLOAT_80618d3c;
        dVar15 = (double)FLOAT_80618d38;
        dVar16 = (double)FLOAT_80618d40;
        dVar17 = (double)FLOAT_80618d14;
        dVar10 = (double)FLOAT_80618c48;
        dVar7 = dVar12;
        dVar13 = DOUBLE_80618d48;
        while ((dVar12 < dVar10 && (iVar5 < 10))) {
            uStack_cc = rng_counter_tick();
            local_d0 = 0x43300000;
            dVar11 = (double)(float)(dVar15 * (double)(float)((double)(float)((double)(longlong)(unsigned)(unsigned int)uStack_cc - dVar13) / dVar14));
            dVar6 = math_poly_trig((double)(float)(dVar11 - dVar16));
            local_f8 = (float)(dVar17 * dVar6 + dVar8);
            local_f4 = *(undefined4 *)(ctx + 0x68);
            dVar6 = math_poly_trig(dVar11);
            local_f0 = (float)(dVar17 * dVar6 + dVar9);
            iVar4 = FUN_8024eef8(&local_f8);
            if (iVar4 != 0) {
                fVar1 = local_f8 - *(float *)(ctx + 100);
                fVar2 = local_f0 - *(float *)(ctx + 0x6c);
                dVar6 = math_sqrt((double)(fVar1 * fVar1 + fVar2 * fVar2));
                if (dVar12 < dVar6) {
                    dVar12 = dVar6;
                    dVar7 = dVar11;
                }
                iVar5 = iVar5 + 1;
            }
        }
        dVar12 = math_poly_trig((double)(float)(dVar7 - (double)FLOAT_80618d40));
        local_f8 = (float)((double)FLOAT_80618d14 * dVar12 + dVar8);
        local_f4 = *(undefined4 *)(ctx + 0x68);
        dVar7 = math_poly_trig(dVar7);
        local_f0 = (float)((double)FLOAT_80618d14 * dVar7 + dVar9);
        FUN_802509b8(&local_f8);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// tcv_set_focus_point (0x802509b8)
// Store the 3-float arm focus point into the globals DAT_805cb5b0/b4/b8.
// ---------------------------------------------------------------------------
void FUN_802509b8(undefined4 *pt)
{
    DAT_805cb5b0 = *(float *)&pt[0];
    DAT_805cb5b4 = (int)pt[1];
    DAT_805cb5b8 = *(float *)&pt[2];
}

// ---------------------------------------------------------------------------
// tcv_update_arm_side_states (0x802509dc)
// Compute the two per-side arm engage states (DAT_805cb594 +0x10 / +0x28) from
// the boss->focus direction: when the focus is far enough, classify by the
// aim-error angle bands (FLOAT_80618cb8/c4/90/d50) and the cross-product side;
// phase 7 uses a facing-angle test; otherwise clear + recompute focus. Lock.
// ---------------------------------------------------------------------------
void FUN_802509dc(void)
{
    uint uVar1;
    float fVar2, fVar3;
    uint uVar4;
    int ctx;
    undefined4 uVar6, uVar7;
    double dVar8, dVar9, dVar10, dVar11, dVar12, dVar13;

    ctx = FUN_803826f0();
    if (DAT_805cb5c0 == 0) {
        *(undefined4 *)(DAT_805cb594 + 0x10) = 0;
        *(undefined4 *)(DAT_805cb594 + 0x28) = 0;
    }
    else {
        dVar10 = (double)(*(float *)(ctx + 100) - DAT_805cb5b0);
        dVar11 = (double)(*(float *)(ctx + 0x6c) - DAT_805cb5b8);
        if (FLOAT_80618c80 <= (float)(dVar10 * dVar10) + (float)(dVar11 * dVar11)) {
            uVar7 = 1;
            uVar4 = countLeadingZeros((unsigned)(8 - DAT_805cb5c4));
            uVar4 = uVar4 >> 5;
            if (uVar4 != 0) {
                uVar7 = 2;
            }
            uVar6 = 2;
            if (uVar4 != 0) {
                uVar6 = 1;
            }
            uVar1 = (uint)(FLOAT_80618ca8 * ((FLOAT_80618cac * *(float *)(ctx + 0x70)) / FLOAT_80618c5c) + FLOAT_80618c74);
            dVar13 = (double)(FLOAT_80618ca4 * DAT_80583228[uVar1 & 0x7ff]);
            dVar12 = (double)(FLOAT_80618ca4 * DAT_80583228[(uVar1 + 0x200) & 0x7ff]);
            fVar2 = (float)(dVar12 * dVar12 + (double)(float)(dVar13 * dVar13 + (double)FLOAT_80618c44));
            if (FLOAT_80618cb0 < fVar2) {
                fVar3 = 1.0f / SQRT(fVar2);
                fVar3 = fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c);
                dVar8 = (double)(FLOAT_80618c2c * fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c));
                dVar13 = (double)(float)(dVar13 * dVar8);
                dVar12 = (double)(float)(dVar12 * dVar8);
            }
            fVar2 = *(float *)(ctx + 0x68) - DAT_805cb5b4;
            fVar2 = (float)(dVar11 * dVar11 + (double)(float)(dVar10 * dVar10 + (double)(fVar2 * fVar2)));
            if (FLOAT_80618cb0 < fVar2) {
                fVar3 = 1.0f / SQRT(fVar2);
                fVar3 = fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c);
                dVar8 = (double)(FLOAT_80618c2c * fVar3 * -(FLOAT_80618c74 * fVar2 * fVar3 * fVar3 - FLOAT_80618c8c));
                dVar10 = (double)(float)(dVar10 * dVar8);
                dVar11 = (double)(float)(dVar11 * dVar8);
            }
            dVar9 = (double)(float)(dVar10 * dVar13 + (double)(float)(dVar11 * dVar12));
            dVar8 = dVar9;
            if ((double)FLOAT_80618c2c < dVar9) {
                dVar8 = (double)FLOAT_80618c2c;
            }
            if ((double)FLOAT_80618c30 <= dVar8) {
                if ((double)FLOAT_80618c2c < dVar9) {
                    dVar9 = (double)FLOAT_80618c2c;
                }
            }
            else {
                dVar9 = (double)FLOAT_80618c30;
            }
            dVar8 = math_asin(dVar9);
            fVar2 = ABS((float)((double)FLOAT_80618c5c * dVar8) / FLOAT_80618cac);
            if (((uVar4 == 0) && (FLOAT_80618cb8 < fVar2)) || ((uVar4 != 0 && (fVar2 < FLOAT_80618cc4)))) {
                if ((float)(dVar12 * dVar10 - (double)(float)(dVar13 * dVar11)) <= FLOAT_80618c44) {
                    *(undefined4 *)(DAT_805cb594 + 0x10) = uVar7;
                    *(undefined4 *)(DAT_805cb594 + 0x28) = uVar6;
                }
                else {
                    *(undefined4 *)(DAT_805cb594 + 0x10) = uVar6;
                    *(undefined4 *)(DAT_805cb594 + 0x28) = uVar7;
                }
            }
            else if (((uVar4 == 0) && (FLOAT_80618c90 < fVar2)) || ((uVar4 != 0 && (fVar2 < FLOAT_80618d50)))) {
                if ((float)(dVar12 * dVar10 - (double)(float)(dVar13 * dVar11)) <= FLOAT_80618c44) {
                    *(undefined4 *)(DAT_805cb594 + 0x10) = uVar7;
                    *(undefined4 *)(DAT_805cb594 + 0x28) = 0;
                }
                else {
                    *(undefined4 *)(DAT_805cb594 + 0x10) = 0;
                    *(undefined4 *)(DAT_805cb594 + 0x28) = uVar7;
                }
            }
            else {
                *(undefined4 *)(DAT_805cb594 + 0x10) = uVar7;
                *(undefined4 *)(DAT_805cb594 + 0x28) = uVar7;
            }
        }
        else if (DAT_805cb5c4 == 7) {
            if (ABS(FLOAT_80618cbc - *(float *)(ctx + 0x70)) < FLOAT_80618c48) {
                *(undefined4 *)(DAT_805cb594 + 0x10) = 0;
                *(undefined4 *)(DAT_805cb594 + 0x28) = 0;
            }
            else if (DAT_80583228[((int)(FLOAT_80618ca8 * ((FLOAT_80618cac * *(float *)(ctx + 0x70)) / FLOAT_80618c5c) + FLOAT_80618c74) + 0x200) & 0x7ff] <= FLOAT_80618c44) {
                *(undefined4 *)(DAT_805cb594 + 0x10) = 1;
                *(undefined4 *)(DAT_805cb594 + 0x28) = 2;
            }
            else {
                *(undefined4 *)(DAT_805cb594 + 0x10) = 2;
                *(undefined4 *)(DAT_805cb594 + 0x28) = 1;
            }
        }
        else {
            *(undefined4 *)(DAT_805cb594 + 0x10) = 0;
            *(undefined4 *)(DAT_805cb594 + 0x28) = 0;
            FUN_802506a8();
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// tcv_apply_node_spin (0x80250e14)
// Apply a ZYX rotation about one skeleton node's local pivot to that node's
// world matrix (translate to pivot, multiply, rotate, multiply, translate back).
// Node selected by param6 (hi=row, lo=col). Lock FUN_803826f0/8038273c.
// ---------------------------------------------------------------------------
void FUN_80250e14(undefined8 a1, double param2, double param3, undefined4 a4, undefined4 a5, uint param6)
{
    int iVar1;
    int iVar2;
    float *pfVar3;
    int *piVar4;
    float *pfVar5;
    double dVar6 = (double)FLOAT_80618c44;   // rz (was extraout_f1; identity angle)
    unsigned long long uVar7;
    float afStack_88[24];

    uVar7 = (unsigned long long)(unsigned)FUN_803826f0();
    iVar2 = (int)(uVar7 >> 0x20);
    piVar4 = *(int **)(*(int *)(iVar2 + 4) + 4);
    pfVar5 = (float *)(piVar4[*piVar4 * -0x30 + ((int)param6 >> 0x10) * 0x30 + 0x1b] +
                       (param6 & 0xffff) * 0x18);
    mtx4_set_translation((double)*pfVar5, (double)pfVar5[1], (double)pfVar5[2]);
    iVar1 = (int)(unsigned)uVar7 * 0x40;
    pfVar3 = (float *)(*(int *)(*(int *)(iVar2 + 4) + 0x10) + iVar1);
    mtx4_multiply(afStack_88, pfVar3, pfVar3);
    mtx4_set_rotation_zyx(dVar6, param2, param3, afStack_88);
    pfVar3 = (float *)(*(int *)(*(int *)(iVar2 + 4) + 0x10) + iVar1);
    mtx4_multiply(afStack_88, pfVar3, pfVar3);
    mtx4_set_translation(-(double)*pfVar5, -(double)pfVar5[1], -(double)pfVar5[2]);
    pfVar3 = (float *)(*(int *)(*(int *)(iVar2 + 4) + 0x10) + iVar1);
    mtx4_multiply(afStack_88, pfVar3, pfVar3);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// tcv_step_arm_charge (0x80250f48)
// Step one arm's charge/color scalar (+0x14 select 0/1/2) toward its target by
// FLOAT_80618d54, mirror the pair member index emphasis into DAT_805cb598, and
// accumulate the shared arm-charge scalar DAT_805cb5a4 (clamped [d68, c8c]).
// ---------------------------------------------------------------------------
void FUN_80250f48(undefined4 a1, int *arm)
{
    float fVar1, fVar2, fVar3;
    int mode;

    fVar3 = FLOAT_80618d5c;
    fVar2 = FLOAT_80618c98;
    mode = arm[4];
    fVar1 = (float)arm[5];
    if (mode == 1) {
        if (FLOAT_80618c44 <= fVar1) {
            if ((fVar1 < FLOAT_80618c98) &&
                (arm[5] = (int)(fVar1 + FLOAT_80618d54), fVar2 < (float)arm[5])) {
                arm[5] = (int)fVar2;
            }
        }
        else {
            arm[5] = (int)FLOAT_80618d54;
        }
    }
    else if (mode < 1) {
        if (-1 < mode) {
            if ((FLOAT_80618c44 < fVar1) || (fVar1 < FLOAT_80618c44)) {
                arm[5] = (int)FLOAT_80618c44;
            }
            fVar2 = FLOAT_80618c44;
            *(float *)(DAT_805cb598 + arm[1] * 0xc + 4) = FLOAT_80618c44;
            *(float *)(DAT_805cb598 + *arm * 0xc + 4) = fVar2;
        }
    }
    else if (mode < 3) {
        if (fVar1 <= FLOAT_80618c44) {
            if ((FLOAT_80618d5c < fVar1) &&
                (arm[5] = (int)(fVar1 - FLOAT_80618d54), (float)arm[5] < fVar3)) {
                arm[5] = (int)fVar3;
            }
        }
        else {
            arm[5] = (int)FLOAT_80618d58;
        }
    }
    fVar3 = FLOAT_80618c2c;
    fVar2 = (float)arm[5];
    if (FLOAT_80618d60 <= ABS(fVar2 - fVar1)) {
        *(float *)(DAT_805cb598 + arm[1] * 0xc + 4) = FLOAT_80618c2c;
        *(float *)(DAT_805cb598 + *arm * 0xc + 4) = fVar3;
    }
    DAT_805cb5a4 = (int)(FLOAT_80618d64 * (fVar2 - fVar1) + (float)DAT_805cb5a4);
    if ((float)DAT_805cb5a4 <= FLOAT_80618c8c) {
        if ((float)DAT_805cb5a4 < FLOAT_80618d68) {
            DAT_805cb5a4 = (int)FLOAT_80618d68;
        }
    }
    else {
        DAT_805cb5a4 = (int)FLOAT_80618c8c;
    }
}

// ---------------------------------------------------------------------------
// tcv_commit_arm_pair (0x802510d4)
// Commit the two-arm pair: decay the beam z-wobble DAT_805cb5ac, step each arm's
// charge (FUN_80250f48), drive the arm loop SFX 0x1a2 by total charge, and when
// charged, rotate the boss facing (+0x70) + slide its position toward the
// arm-pair midpoint using the sine table. Ease both arm scalars, wrap facing.
// ---------------------------------------------------------------------------
void FUN_802510d4(void)
{
    float fVar1, fVar3, fVar4;
    int *piVar2;
    int entity;
    int *worldPos;
    int *piVar6;
    double dVar7, dVar8, dVar9;

    entity = FUN_803826f0();
    piVar2 = (int *)DAT_805cb594;
    piVar6 = (int *)DAT_805cb594 + 6;
    if ((FLOAT_80618c44 < DAT_805cb5ac) &&
        (DAT_805cb5ac = DAT_805cb5ac * FLOAT_80618d6c, ABS(DAT_805cb5ac) < FLOAT_80618d54)) {
        DAT_805cb5ac = FLOAT_80618c44;
    }
    FUN_80250f48((undefined4)entity, (int *)DAT_805cb594);
    FUN_80250f48((undefined4)entity, piVar6);
    dVar8 = (double)(FLOAT_80618c48 * (ABS((float)piVar2[5]) + ABS((float)piVar2[0xb])));
    worldPos = entity_get_world_position(entity);
    sound_update_3d_source((double)(float)((double)FLOAT_80618c8c * dVar8), (double)FLOAT_80618c7c,
                           (double)(float)((double)FLOAT_80618c74 * dVar8 + (double)FLOAT_80618c74),
                           entity + 0x22c, worldPos, 0x1a2, 1);
    if ((FLOAT_80618d54 <= ABS((float)piVar2[5])) || (FLOAT_80618d54 <= ABS((float)piVar2[0xb]))) {
        dVar9 = (double)((float)piVar2[5] + (float)piVar2[0xb]);
        dVar8 = math_asin((double)(((float)piVar2[0xb] - (float)piVar2[5]) / FLOAT_80618d70));
        fVar4 = FLOAT_80618cac;
        fVar3 = FLOAT_80618ca8;
        fVar1 = FLOAT_80618c74;
        dVar7 = (double)FLOAT_80618c5c;
        *(float *)(entity + 0x70) =
            FLOAT_80618c40 * (FLOAT_80618cbc - (float)(dVar7 * dVar8) / FLOAT_80618cac) +
            *(float *)(entity + 0x70);
        *(float *)(entity + 100) =
            (float)(dVar9 * (double)DAT_80583228[(int)(fVar3 * (float)((double)(fVar4 * (float)(dVar7 + (double)*(float *)(entity + 0x70))) / dVar7) + fVar1) & 0x7ff]) * fVar1 +
            *(float *)(entity + 100);
        *(float *)(entity + 0x6c) =
            (float)(dVar9 * (double)DAT_80583228[((int)(fVar3 * (float)((double)(fVar4 * (float)(dVar7 + (double)*(float *)(entity + 0x70))) / dVar7) + fVar1) + 0x200) & 0x7ff]) * fVar1 +
            *(float *)(entity + 0x6c);
    }
    FUN_8024f298((int)piVar2);
    FUN_8024f298((int)piVar6);
    fVar1 = *(float *)(entity + 0x70);
    if (fVar1 <= FLOAT_80618c5c) {
        if (fVar1 < FLOAT_80618cb4) {
            *(float *)(entity + 0x70) = fVar1 + FLOAT_80618c60;
        }
    }
    else {
        *(float *)(entity + 0x70) = fVar1 - FLOAT_80618c60;
    }
    FUN_8038273c();
}
