// player_camera_movement.cpp
// Player aim/turn input integration, ledge-grab / mantle climbing, swim movement
// and camera control for the local player object (global player context
// DAT_80611d74). These are driven from player_main_tick (0x800a9138) and the
// player state dispatcher (player_state_dispatch 0x800ab000):
//   - player_process_aim_input: the main per-frame aim/turn processor. Reads the
//     raw stick vector, applies dead-zone / response-curve shaping and per-mode
//     gain, then integrates it into the player look angles (delegating to the
//     turret-aim path for mode 0x40, the mounted-weapon path for mode 0x80, or
//     the free-look integrator otherwise). Also runs the auto-centre state machine
//     at +0x108.
//   - player_apply_turret_aim: the mode-0x40 turret variant of the look-angle
//     integrator, with per-axis wrap/clamp against the turret limits.
//   - player_prop_interact_input: use-button handling for interactive props the
//     player is standing on.
//   - player_ledge_grab_tick / player_ledge_hang_update / player_try_grab_ledge /
//     player_exit_ledge_state: the ledge-hang / mantle state (state id 0x200):
//     detect a grabbable ledge from the movement direction and nav sweep, drive
//     the hang animation and orbit camera, and mantle up when the climb completes.
//   - player_swim_move_tick: water-surface movement (buoyancy, rail-track follow,
//     advanced-nav collision, dive/surface buttons) writing the eye position.
//   - player_is_special_arcade_mode / player_scan_teammate_aim_block: small
//     predicates/helpers used by the aim-assist path.
//
// Address range: 0x80255f7c - 0x802580ac  (batch_732)
//
// Self-contained translation unit. DAT_80611d74 is the raw player-context base
// address (int); ROM array-style accesses (DAT_80611d74[N]) are expressed as
// explicit *(T*)(DAT_80611d74 + N*4) byte offsets to keep one consistent type.
// The callee-saved FP-register spill locals emitted by the decompiler
// (local_8 = (float)in_f31, ...) are dead prologue artifacts and are omitted.

typedef unsigned char      undefined1;
typedef unsigned short     undefined2;
typedef unsigned int       undefined4;
typedef unsigned int       uint;
typedef unsigned short     ushort;
typedef long long          longlong;
typedef unsigned long long ulonglong;
typedef unsigned long long undefined8;

static inline unsigned long long CONCAT44(unsigned int hi, unsigned int lo) {
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
}
static inline double ABS_D(double x) { return x < 0.0 ? -x : x; }
static inline float  ABS_F(float x)  { return x < 0.0f ? -x : x; }
static inline float  SQRT_F(float x) { return (float)__builtin_sqrt((double)x); }

extern "C" {
// ---- context enter/leave (packed self|target register pair) ----
undefined8 FUN_803826f0(void);   void FUN_8038273c(void);   // aim ctx
undefined8 FUN_803826d0(void);   void FUN_8038271c(void);   // ledge ctx
undefined8 FUN_803826e4(void);   void FUN_80382730(void);   // swim ctx
undefined8 FUN_803826d8(void);   void FUN_80382724(void);   // teammate-scan ctx
int  __save_gpr_28(void);        void __restore_gpr_28(void);

// ---- math / anim / input / camera helpers ----
double math_sqrt(double x);
double math_atan2(double y, double x);
double FUN_80390324(double x);                          // smoothstep easing
double player_get_camera_lerp_factor(int cam);
void   mtx4_set_rotation_y(double angle, float *mtxOut);
void   mtx4_transform_point_alias(float *mtx, float *pt);
void   anim_play_scaled(double a1, double a2);
void   anim_start_clip(double a1, double a2);
int    anim_clip_flags_active_slot(int rig, void *clipRec, int a3, int mask);
int    anim_event_triggered_this_frame(int rig, int which, int idx);
void   anim_clip_field_b08(int rig);
uint   input_get_buttons_held(int dev);
uint   input_get_buttons_pressed(int dev);
void   sound_init_play_params(int params);
void   sound_play_request(int *params);
void   camera_set_mode_by_index(int cam, int mode, int rig);
void   camera_set_orbit_params(double a1, double a2, double a3, double a4, double a5,
                               double a6, double a7, double a8, int cam);
void   player_set_state(int player, int state);
void   player_set_eye_position(double x, double y, double z, int player);
void   object_model_swap_in(int obj);
void   object_model_swap_out_and_clear(int obj);
int    prop_is_interactive_type(int obj);
void   prop_reset_active_state(int *prop);
void   prop_set_idle_if_valid(int *prop);
int    game_is_standard_arcade(void);
void   player_ai_get_aim_position(int *actor, float *outA, float *outB, undefined4 *outC);
int    swept_sphere_vs_segment(int a1, undefined4 a2, float *a3, float *a4, float *a5, float *a6);
void   player_ai_rail_track_follow(int dev, float *pos, float *dir);
uint   nav_advanced_movement_dispatch(double a1, double a2, int dev, int *ctx, float *pos,
                                      int *outHit, int a7, int a8);
int    floor_query_with_interact(double a1, double a2, int posOut, int a4, int *a5, int a6);
void   aiactor_leader_zone_advance(int rig, int a2, int a3);

// ---- intra-file forward decls ----
void player_apply_turret_aim(void);       // 0x80255f7c
int  player_ledge_hang_update(void);      // 0x80256bf4
int  player_try_grab_ledge(int player, float *pos, float *dir, int hit, int surf); // 0x802575d8

// forward decls of helpers this module calls (renamed elsewhere in the IDB)
void player_scale_move_input(float *move, float *aim);   // 0x80255ca8
void player_apply_aim_delta(int obj, float *aim);        // 0x80255e10

// ---- batch_733 intra-file forward decls ----
undefined4 player_can_aim_or_fire(void);   // 0x80258654
void player_reset_zoom_state(void);        // 0x802586e0
void player_update_fov_zoom_lerp(void);    // 0x80259278

// ---- batch_733 additional helpers ----
uint   input_get_buttons_released(int dev);
double math_poly_trig(double x);
int    render_mode_class_a(void);
int    object_can_fire_in_mode(int modeField);
int    object_is_context_flagged(int ctxField);
void   worm_stat_add(double amount, int statId, int slot);
void   anim_set_playback_rate(double rate, int *clip, int flag);
void   player_ai_assign_team(int object);
void   player_ai_set_team_id(int object, int team);
void   entity_set_render_type(int obj, void *fn);
void   damage_filter_melee_zero(void);     // callback symbol taken by address
void   mtx4_set_translate_rotz(double x, double y, double z, double zAngle, float *mtxOut);
void   mtx4_multiply(float *a, float *b, float *out);
void   sound_source_compute_dot_distance(int srcVec, float *pos, int out);
double player_get_min_speed(int player);
double player_get_movement_speed(int player, int which);
int    scene_raycast(undefined4 world, int origin, float *dst, int a4, float *a5, float *a6,
                     int *a7, uint mask, int a9, int *a10);
int    soundpad_get_current_reverb_state(void);
int    event_get_type_data(int idx);

// ---- batch_733 raw globals ----
extern int   DAT_80610fe8, DAT_80610fec;  // sound-occlusion sweep cursors
extern int   DAT_803e0500, DAT_803e0504, DAT_803e0508;  // camera up-vector (x,y,z)
extern short DAT_804d1cd0;   // per-model anim-track index table (short[])
extern int   DAT_804d34dc;   // anim-track flags table (stride 8)
extern char  DAT_8043e22c;   // per-anim-state flags table (stride 0xf)
extern float FLOAT_80618f30, FLOAT_80618f34, FLOAT_80618f38, FLOAT_80618f3c, FLOAT_80618f40;
extern float FLOAT_80618f44, FLOAT_80618f48, FLOAT_80618f4c;

// ---- batch_734 intra-file forward decls ----
void player_ledge_grab_tick(void);         // 0x80257340 (defined earlier)

// ---- batch_734 additional helpers ----
int    __save_gpr_25(void);   void __restore_gpr_25(void);
void   viewport_set_depth_params(double nearP, double farP, double bias, int viewport);
void   viewport_set_rect(int *viewport, int x0, int y0, int x1, int y1);
int    session_get_state_flag(void);
int    player_autoaim_scan_events(void);
void   sound_source_update_tick(void);
void   mtx4_transform_vector_alias(float *mtx, float *vec);
int    entity_is_in_active_list(int obj);
int    player_is_valid_target(int player, int obj);
void   FUN_80280594(int *ctx);              // co-op death effect (mode 0x12)
void   character_damage_effect_wrapper(void);
undefined4 rng_counter_tick(void);
undefined8 FUN_803826dc(void);  void FUN_80382728(void);   // subobj ctx
uint   subobj_get_flags(uint subobj);
uint   subobj_is_idle(uint subobj);
void   nav_movement_dispatch(double a1, double a2, int dev, int *ctx, float *pos,
                             float *a6, int *outHit, int a8, int a9);
void   switch_slot_reset(int slot);
void   subobj_find_active_grab(int ctx, int *out);
void   damage_dispatch_to_entity(double a1, double a2, double a3, double a4, undefined8 a5,
                                 undefined8 a6, undefined8 a7, undefined8 a8, int *params);
int    anim_is_clip_active(int rig, int clipId);

// ---- batch_734 raw globals ----
extern int  DAT_806109c4, DAT_806109c8;   // render target width / height
extern int  DAT_805015d8;   // active level id (split-screen special)
extern int  DAT_80611770;   // spectate/render mode
extern float FLOAT_80618e60;
extern float FLOAT_80618f50, FLOAT_8060fad0, FLOAT_80618f54, FLOAT_80618f58, FLOAT_80618f5c;
extern float FLOAT_80618f60, FLOAT_80618f64, FLOAT_80618f68, FLOAT_80618f6c, FLOAT_80618f70;
extern float FLOAT_80618f74, FLOAT_80618f78, FLOAT_80618f7c, FLOAT_80618f88;
extern double DOUBLE_80618f80;

// ---- raw globals ----
extern int  DAT_80611d74;   // local player context base address
extern int  DAT_80611d54;   // active camera object (0 if none)
extern int  DAT_80611824;   // anim track table base
extern int  DAT_80611d90;   // per-player scratch table base
extern int  DAT_80611d78;   // player array base
extern int  DAT_80612610;   // player count
extern int  DAT_80612098;   // teammate count
extern int  DAT_80612614;   // current level/mission id
extern uint DAT_806109c0;   // frame counter
extern int  DAT_80588610;   // teammate pointer table
extern int  DAT_8048ca94, DAT_8048ca9c;   // floor-query per-zone params (stride 0x30)
extern char DAT_805015bd, DAT_805015be;   // co-op mode / mission variant
extern int  DAT_805015c0;   // global cutscene/game flags
extern int  DAT_80501570, DAT_80501600;   // control-scheme table / active-device id
extern int  DAT_8060faec;   // manual-look-disable flag
extern int  DAT_80611928;   // frame timestamp

extern float FLOAT_806108e8, FLOAT_806108ec;   // half / full frame delta scale
extern float FLOAT_80611d7c, FLOAT_80611d80, FLOAT_80611d84;  // pitch clamp / yaw / pitch sensitivity

// float / double literal pool (0x80618e30 .. 0x80618f30)
extern float FLOAT_80618e58, FLOAT_80618e5c, FLOAT_80618e64, FLOAT_80618e70, FLOAT_80618e74;
extern float FLOAT_80618e78, FLOAT_80618e80, FLOAT_80618e84, FLOAT_80618e90, FLOAT_80618e98;
extern float FLOAT_80618e9c, FLOAT_80618ea0, FLOAT_80618eac, FLOAT_80618eb0, FLOAT_80618eb4;
extern float FLOAT_80618eb8, FLOAT_80618ebc, FLOAT_80618ec0, FLOAT_80618ec4, FLOAT_80618ec8;
extern float FLOAT_80618ecc, FLOAT_80618ed0, FLOAT_80618ed4, FLOAT_80618ed8, FLOAT_80618ee8;
extern float FLOAT_80618eec, FLOAT_80618ef0, FLOAT_80618ef4, FLOAT_80618ef8, FLOAT_80618efc;
extern float FLOAT_80618f00, FLOAT_80618f04, FLOAT_80618f08, FLOAT_80618f0c, FLOAT_80618f18;
extern float FLOAT_80618f1c, FLOAT_80618f20, FLOAT_80618f24, FLOAT_80618f28, FLOAT_80618f2c;
extern double DOUBLE_80618e90, DOUBLE_80618ee0;

// ============================================================================
// 0x80255f7c  player_apply_turret_aim
// Mode-0x40 (turret) look-angle integrator. Smooths the aim-response scalar at
// +0xe18, integrates the stick vector into yaw (+0x100) and pitch (+0x104) using
// the camera lerp factor and per-axis sensitivity, then wraps/clamps both angles
// against the turret's per-axis limits (rig +0x34/+0x38 and +0x3c/+0x40) via the
// smoothstep helper. Called from player_process_aim_input for turret mode.
// ============================================================================
void player_apply_turret_aim(void)
{
    float  fVar1, fVar2, fVar3, fVar4;
    int    ctx;    /* self entity (hi word of packed pair) */
    float *stick;  /* aim stick vector (lo word) */
    int    iVar6;
    double dVar7, dVar8, dVar9, dVar10;
    undefined8 uVar11;

    uVar11 = FUN_803826f0();
    ctx   = (int)((ulonglong)uVar11 >> 0x20);
    stick = (float *)(uint)uVar11;
    FUN_80390324((double)(((*(float *)(*(int *)(ctx + 0x7c) + 0x40) + *(float *)(ctx + 0x70)) -
                           FLOAT_80618e84) / FLOAT_80618e80));
    FUN_80390324((double)(((*(float *)(*(int *)(ctx + 0x7c) + 0x3c) + *(float *)(ctx + 0x70)) -
                           FLOAT_80618e84) / FLOAT_80618e80));
    iVar6 = *(int *)(ctx + 0x7c);
    fVar1 = FLOAT_80618e64;
    if (DAT_80611d54 != 0) {
        dVar7 = player_get_camera_lerp_factor(DAT_80611d54);
        fVar1 = (float)((double)*(float *)(DAT_80611d54 + 900) / dVar7);
    }
    dVar7 = (double)DOUBLE_80618e90;
    fVar2 = FLOAT_80618e80;
    dVar8 = (double)FLOAT_80618e84;
    *(float *)(DAT_80611d74 + 0xe18) =
        FLOAT_80618eb0 * (FLOAT_80618e64 - *(float *)(DAT_80611d74 + 0xe18)) +
        *(float *)(DAT_80611d74 + 0xe18);
    *(float *)(DAT_80611d74 + 0x100) =
        FLOAT_806108ec *
        fVar1 * *stick *
        ((*(float *)(DAT_80611d74 + 0xe18) * FLOAT_80611d84) /
         (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - dVar7)) +
        *(float *)(DAT_80611d74 + 0x100);
    *(float *)(DAT_80611d74 + 0x104) =
        FLOAT_806108ec *
        fVar1 * stick[1] *
        ((*(float *)(DAT_80611d74 + 0xe18) * FLOAT_80611d80) /
         (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - dVar7)) +
        *(float *)(DAT_80611d74 + 0x104);
    *(float *)(DAT_80611d74 + 0x100) = *(float *)(DAT_80611d74 + 0x100) - *(float *)(ctx + 0x70);
    dVar10 = (double)*(float *)(DAT_80611d74 + 0x100);
    dVar8 = FUN_80390324((double)((float)(dVar10 - dVar8) / fVar2));
    dVar9 = (double)FLOAT_80618e80;
    *(float *)(DAT_80611d74 + 0x100) = -(float)(dVar9 * (double)(float)dVar8 - dVar10);
    dVar8 = (double)*(float *)(DAT_80611d74 + 0x104);
    dVar7 = FUN_80390324((double)(float)((double)(float)(dVar8 - (double)FLOAT_80618e84) / dVar9));
    *(float *)(DAT_80611d74 + 0x104) = -(float)((double)FLOAT_80618e80 * (double)(float)dVar7 - dVar8);
    fVar1 = *(float *)(iVar6 + 0x34);
    fVar2 = *(float *)(iVar6 + 0x38);
    if (fVar1 == fVar2) {
        fVar1 = *(float *)(DAT_80611d74 + 0x104);
        fVar2 = fVar1;
        if (FLOAT_80618e70 < fVar1) { fVar2 = FLOAT_80618e70; }
        fVar3 = FLOAT_80618e84;
        if ((FLOAT_80618e84 <= fVar2) && (fVar3 = fVar1, FLOAT_80618e70 < fVar1)) { fVar3 = FLOAT_80618e70; }
        *(float *)(DAT_80611d74 + 0x104) = fVar3;
    } else {
        fVar3 = *(float *)(DAT_80611d74 + 0x104);
        fVar4 = fVar3;
        if (fVar2 < fVar3) { fVar4 = fVar2; }
        if ((fVar1 <= fVar4) && (fVar1 = fVar3, fVar2 < fVar3)) { fVar1 = fVar2; }
        *(float *)(DAT_80611d74 + 0x104) = fVar1;
    }
    fVar1 = *(float *)(iVar6 + 0x3c);
    fVar2 = *(float *)(iVar6 + 0x40);
    if (fVar1 != fVar2) {
        fVar3 = *(float *)(DAT_80611d74 + 0x100);
        fVar4 = fVar3;
        if (fVar2 < fVar3) { fVar4 = fVar2; }
        if ((fVar1 <= fVar4) && (fVar1 = fVar3, fVar2 < fVar3)) { fVar1 = fVar2; }
        *(float *)(DAT_80611d74 + 0x100) = fVar1;
    }
    fVar1 = FLOAT_80618e80;
    *(float *)(DAT_80611d74 + 0x100) = *(float *)(DAT_80611d74 + 0x100) + *(float *)(ctx + 0x70);
    dVar8 = (double)*(float *)(DAT_80611d74 + 0x100);
    dVar7 = FUN_80390324((double)((float)(dVar8 - (double)FLOAT_80618e84) / fVar1));
    *(float *)(DAT_80611d74 + 0x100) = -(float)((double)FLOAT_80618e80 * (double)(float)dVar7 - dVar8);
    FUN_8038273c();
}

// ============================================================================
// 0x80256284  player_process_aim_input
// Main per-frame aim/turn input processor (called from player_main_tick). Reads
// the raw stick vector (via the aim ctx), applies dead-zone / response-curve
// shaping (with a mounted-weapon variant and a sighted-weapon slow-down), scales
// by the level aim gain, runs the auto-centre state machine at +0x108, then
// dispatches the integrated angles: mode 0x80 -> player_apply_aim_delta on the
// mounted weapon, mode 0x40 -> player_apply_turret_aim, otherwise the free-look
// integrator (buoyancy-style smoothing at +0xe18, yaw +0x100 / pitch +0x104 with
// wrap and pitch clamp against FLOAT_80611d7c).
// ============================================================================
void player_process_aim_input(undefined4 param_1, undefined4 param_2, float *param_3)
{
    float  fVar1, fVar2, fVar3, fVar4, fVar5;
    int    iVar6, iVar7, iVar10, iVar11;
    float *stickHi;   /* self / shaped stick (hi word) */
    float *stickLo;   /* raw stick (lo word) */
    double dVar12, dVar13;
    undefined8 uVar14;

    uVar14 = FUN_803826f0();
    stickHi = (float *)(uint)((ulonglong)uVar14 >> 0x20);
    stickLo = (float *)(uint)uVar14;
    dVar13 = (double)(FLOAT_80618ea0 + *(float *)(*(int *)(DAT_80611d74 + 0x1398) + 0x78));
    iVar10 = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    if (*(int *)(DAT_80611d74 + 0x114) == 0x80) {
        iVar6 = *(int *)(*(int *)(*(int *)(iVar10 + 0xc14) + 0x7c) + 0x10);
        if ((iVar6 == 9) || (iVar6 == 1)) { goto LAB_80256a88; }
    } else if (*(int *)(DAT_80611d74 + 0x114) == 0x40) {
        *(int *)(DAT_80611d74 + 0x108) = 1;
    }
    fVar3 = FLOAT_80618e58;
    if ((((*(uint *)(DAT_80611d74 + 0xc) & 0x80) != 0) && (*(int *)(DAT_80611d74 + 0x114) != 0x100)) ||
        ((((DAT_80611d54 != 0 && (*(int **)(DAT_80611d54 + 0x26c) != (int *)0x0)) &&
           (**(int **)(DAT_80611d54 + 0x26c) == 0x18)) &&
          ((*(int *)(DAT_80611d74 + 0x114) != 0x400 && (*(int *)(DAT_80611d74 + 0x114) != 0x200)))))) {
        if (*(int *)(DAT_80611d74 + 0xdf4) == 0) {
            fVar1 = FLOAT_80618eb8;
            if (*(int *)(DAT_80611d74 + 0x114) == 0x40) { fVar1 = FLOAT_80618eb4; }
            if (*(int *)(*(int *)(DAT_80611d74 + 0x1398) + 0x74) == 2) { fVar1 = FLOAT_80618e58; }
            *param_3 = *stickHi;
            param_3[1] = stickHi[1];
            fVar2 = *stickHi;
            if (fVar2 <= fVar3) {
                fVar5 = FLOAT_80618e64 - fVar3;
                if (fVar2 < -fVar3) { fVar3 = fVar2 + fVar3; }
                fVar3 = (FLOAT_80618e64 / fVar5) * fVar3;
            } else {
                fVar5 = FLOAT_80618e64 - fVar3;
                if (fVar3 < fVar2) { fVar3 = fVar2 - fVar3; }
                fVar3 = (FLOAT_80618e64 / fVar5) * fVar3;
            }
            *param_3 = fVar3;
            fVar3 = stickHi[1];
            if (fVar3 <= FLOAT_80618e58) {
                fVar2 = FLOAT_80618e58;
                if (fVar3 < -fVar1) { fVar2 = fVar3 + fVar1; }
                fVar2 = (FLOAT_80618e64 / (FLOAT_80618e64 - fVar1)) * fVar2;
            } else {
                fVar2 = FLOAT_80618e58;
                if (fVar1 < fVar3) { fVar2 = fVar3 - fVar1; }
                fVar2 = (FLOAT_80618e64 / (FLOAT_80618e64 - fVar1)) * fVar2;
            }
            param_3[1] = fVar2;
            if ((*(ushort *)(*(int *)(DAT_80611d74 + 0x1398) + 2) & 1) != 0) {
                param_3[1] = -param_3[1];
            }
        } else {
            fVar3 = FLOAT_80618ea0;
            if (*(int *)(*(int *)(DAT_80611d74 + 0x1398) + 0x74) == 2) { fVar3 = FLOAT_80618e58; }
            fVar1 = *stickHi;
            if (fVar1 <= FLOAT_80618e58) {
                fVar2 = FLOAT_80618e58;
                if (fVar1 < -fVar3) { fVar2 = fVar1 + fVar3; }
                *param_3 = (FLOAT_80618e64 / (FLOAT_80618e64 - fVar3)) * fVar2;
            } else {
                fVar2 = FLOAT_80618e58;
                if (fVar3 < fVar1) { fVar2 = fVar1 - fVar3; }
                *param_3 = (FLOAT_80618e64 / (FLOAT_80618e64 - fVar3)) * fVar2;
            }
            fVar1 = stickHi[1];
            if (fVar1 <= FLOAT_80618e58) {
                fVar2 = FLOAT_80618e58;
                if (fVar1 < -fVar3) { fVar2 = fVar1 + fVar3; }
                param_3[1] = (FLOAT_80618e64 / (FLOAT_80618e64 - fVar3)) * fVar2;
            } else {
                fVar2 = FLOAT_80618e58;
                if (fVar3 < fVar1) { fVar2 = fVar1 - fVar3; }
                param_3[1] = (FLOAT_80618e64 / (FLOAT_80618e64 - fVar3)) * fVar2;
            }
        }
        fVar5 = FLOAT_80618ec8;
        fVar2 = FLOAT_80618ec4;
        fVar1 = FLOAT_80618ebc;
        fVar3 = FLOAT_80618ea0;
        if (*(int *)(*(int *)(DAT_80611d74 + 0x1398) + 0x74) == 2) {
            iVar6 = 0;
            iVar11 = 2;
            do {
                fVar4 = FLOAT_80618e64;
                if (*(float *)((int)param_3 + iVar6) <= fVar1) {
                    fVar4 = *(float *)(DAT_80611d74 + iVar6 + 0xe74);
                    if (FLOAT_80618e58 < fVar4) { fVar4 = FLOAT_80618e58; }
                    *(float *)(DAT_80611d74 + iVar6 + 0xe74) = fVar4;
                } else {
                    iVar7 = iVar6 + 0xe74;
                    *(float *)(DAT_80611d74 + iVar7) =
                        FLOAT_80618ec0 * FLOAT_806108e8 *
                        (FLOAT_80618e64 /
                         (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) +
                        *(float *)(DAT_80611d74 + iVar7);
                    if (fVar4 < *(float *)(DAT_80611d74 + iVar7)) { *(float *)(DAT_80611d74 + iVar7) = fVar4; }
                }
                fVar4 = FLOAT_80618e9c;
                if (fVar2 <= *(float *)((int)param_3 + iVar6)) {
                    fVar4 = *(float *)(DAT_80611d74 + iVar6 + 0xe74);
                    if (fVar4 < FLOAT_80618e58) { fVar4 = FLOAT_80618e58; }
                    *(float *)(DAT_80611d74 + iVar6 + 0xe74) = fVar4;
                } else {
                    iVar7 = iVar6 + 0xe74;
                    *(float *)(DAT_80611d74 + iVar7) =
                        -(FLOAT_80618ec0 * FLOAT_806108e8 *
                          (FLOAT_80618e64 /
                           (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) -
                          *(float *)(DAT_80611d74 + iVar7));
                    if (*(float *)(DAT_80611d74 + iVar7) < fVar4) { *(float *)(DAT_80611d74 + iVar7) = fVar4; }
                }
                *(float *)((int)param_3 + iVar6) = fVar5 * *(float *)((int)param_3 + iVar6);
                *(float *)((int)param_3 + iVar6) =
                    fVar3 * *(float *)(DAT_80611d74 + iVar6 + 0xe74) + *(float *)((int)param_3 + iVar6);
                iVar6 = iVar6 + 4;
                iVar11 = iVar11 + -1;
            } while (iVar11 != 0);
        }
    }
    iVar6 = *(int *)(DAT_80611d74 + 0x108);
    if (iVar6 == 1) {
        if (FLOAT_80618e58 == ABS_F(*stickLo)) { *(int *)(DAT_80611d74 + 0x108) = 0; }
    } else if (iVar6 < 1) {
        if (-1 < iVar6) {
            if ((FLOAT_80618e58 == param_3[1]) || (ABS_F(*stickLo) <= FLOAT_80618e58)) {
                if ((FLOAT_80618e58 == param_3[1]) && (FLOAT_80618ecc < ABS_F(*stickLo))) {
                    *(int *)(DAT_80611d74 + 0x108) = 2;
                }
            } else {
                *(int *)(DAT_80611d74 + 0x108) = 1;
            }
        }
    } else if ((iVar6 < 3) && (FLOAT_80618e58 != param_3[1])) {
        *(int *)(DAT_80611d74 + 0x108) = 1;
    }
    player_scale_move_input(stickLo, param_3);
    *param_3 = (float)((double)*param_3 * dVar13);
    param_3[1] = (float)((double)param_3[1] * dVar13);
    iVar6 = *(int *)(DAT_80611d74 + 0x114);
    if (iVar6 == 0x80) {
        player_apply_aim_delta(*(int *)(iVar10 + 0xc14), param_3);
    } else if (iVar6 == 0x40) {
        player_apply_turret_aim();
    } else if ((iVar6 != 0x20) && (iVar6 != 0x100)) {
        dVar13 = (double)FLOAT_80618e64;
        if (((*(uint *)(DAT_80611d74 + 0xc) & 0x80) != 0) ||
            ((((DAT_80611d54 != 0 && (*(int **)(DAT_80611d54 + 0x26c) != (int *)0x0)) &&
               (**(int **)(DAT_80611d54 + 0x26c) == 0x18)) && ((iVar6 != 0x400 && (iVar6 != 0x200)))))) {
            fVar3 = FLOAT_80618e64;
            if (DAT_80611d54 != 0) {
                dVar12 = player_get_camera_lerp_factor(DAT_80611d54);
                fVar3 = (float)((double)*(float *)(DAT_80611d54 + 900) / dVar12);
            }
            if (*(int *)(DAT_80611d74 + 0xe14) != 0) {
                dVar13 = (double)FLOAT_80618ed0;
            }
            if (*(int *)(DAT_80611d74 + 0xdf4) != 0) {
                dVar13 = (double)FLOAT_80618e64;
                *(float *)(DAT_80611d74 + 0xe18) = FLOAT_80618e64;
            }
            dVar12 = (double)DOUBLE_80618e90;
            *(float *)(DAT_80611d74 + 0xe18) =
                (float)((double)FLOAT_80618eb0 *
                        (double)(float)(dVar13 - (double)*(float *)(DAT_80611d74 + 0xe18)) +
                        (double)*(float *)(DAT_80611d74 + 0xe18));
            *(float *)(DAT_80611d74 + 0x100) =
                FLOAT_806108ec *
                fVar3 * *param_3 *
                ((*(float *)(DAT_80611d74 + 0xe18) * FLOAT_80611d84) /
                 (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - dVar12)) +
                *(float *)(DAT_80611d74 + 0x100);
            fVar1 = FLOAT_80618ea0;
            if ((*(int *)(DAT_80611d74 + 0x108) == 2) &&
                ((*(ushort *)(*(int *)(DAT_80611d74 + 0x1398) + 2) & 8) != 0)) {
                *(float *)(DAT_80611d74 + 0x104) =
                    -(FLOAT_80618eb4 * *(float *)(DAT_80611d74 + 0x104) * FLOAT_806108ec -
                      *(float *)(DAT_80611d74 + 0x104));
                fVar3 = FLOAT_80618e80;
                fVar2 = FLOAT_80618e58;
                if (ABS_F(*(float *)(DAT_80611d74 + 0x104)) < fVar1) {
                    *(int *)(DAT_80611d74 + 0x108) = 1;
                    fVar3 = FLOAT_80618e80;
                    fVar2 = FLOAT_80618e58;
                }
            } else {
                *(float *)(DAT_80611d74 + 0x104) =
                    FLOAT_806108ec *
                    fVar3 * param_3[1] *
                    ((-(FLOAT_80618ea0 * (FLOAT_80618e64 - *(float *)(DAT_80611d74 + 0xe18)) -
                        FLOAT_80618e64) * FLOAT_80611d80) /
                     (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) +
                    *(float *)(DAT_80611d74 + 0x104);
                fVar3 = FLOAT_80618e80;
                fVar2 = FLOAT_80618e58;
            }
            while (fVar1 = FLOAT_80618e80, *(float *)(DAT_80611d74 + 0x100) < fVar2) {
                *(float *)(DAT_80611d74 + 0x100) = *(float *)(DAT_80611d74 + 0x100) + fVar3;
            }
            while (fVar1 <= *(float *)(DAT_80611d74 + 0x100)) {
                *(float *)(DAT_80611d74 + 0x100) = *(float *)(DAT_80611d74 + 0x100) - fVar1;
            }
        }
        if (*(float *)(DAT_80611d74 + 0x104) < -FLOAT_80611d7c) {
            *(float *)(DAT_80611d74 + 0x104) = -FLOAT_80611d7c;
        }
        if (FLOAT_80611d7c < *(float *)(DAT_80611d74 + 0x104)) {
            *(float *)(DAT_80611d74 + 0x104) = FLOAT_80611d7c;
        }
    }
    fVar3 = FLOAT_80618eac;
    iVar10 = *(int *)(DAT_80611d74 + 0x114);
    if ((((iVar10 != 0x80) && (iVar10 != 0x40)) && (iVar10 != 0x400)) &&
        ((iVar10 != 0x200 && ((*(uint *)(DAT_80611d74 + 0xc) & 0x10) != 0)))) {
        *stickLo = *stickLo * FLOAT_80618eac;
        stickLo[1] = stickLo[1] * fVar3;
    }
LAB_80256a88:
    FUN_8038273c();
}

// ============================================================================
// 0x80256aa8  player_prop_interact_input
// Use-button handling for interactive props the player is standing on. In the
// two interactable player states (0x10 / 0x21) it fetches the prop(s) under the
// player (rig +0xc14 / +0xc18) and, if interactive, toggles them (reset active /
// set idle) when the use button (mask at +0x24) is held. Called from
// player_state_dispatch.
// ============================================================================
void player_prop_interact_input(void)
{
    int   iVar1;
    uint  uVar2;
    int   iVar3;
    int  *piVar4;
    int  *piVar5;

    FUN_803826f0();
    iVar1  = *(int *)(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c) + 0xc14);
    piVar5 = *(int **)(iVar1 + 0x7c);
    if ((*(int *)(DAT_80611d74 + 0x1390) == 10) || (*(int *)(DAT_80611d74 + 0x1390) == 0x21)) {
        iVar3  = *(int *)(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c) + 0xc18);
        piVar4 = (int *)0x0;
        if (iVar3 != 0) { piVar4 = *(int **)(iVar3 + 0x7c); }
        iVar1 = prop_is_interactive_type(iVar1);
        if (iVar1 != 0) {
            if (piVar5[4] == 1) {
                uVar2 = input_get_buttons_held(*(int *)DAT_80611d74);
                if ((*(int *)(DAT_80611d74 + 0x24) & (int)uVar2) != 0) { prop_reset_active_state(piVar5); }
            } else {
                uVar2 = input_get_buttons_held(*(int *)DAT_80611d74);
                if ((*(int *)(DAT_80611d74 + 0x24) & (int)uVar2) != 0) { prop_set_idle_if_valid(piVar5); }
            }
        }
        if ((iVar3 != 0) && (iVar1 = prop_is_interactive_type(iVar3), iVar1 != 0)) {
            if (piVar4[4] == 1) {
                uVar2 = input_get_buttons_held(*(int *)DAT_80611d74);
                if ((*(int *)(DAT_80611d74 + 0x24) & (int)uVar2) != 0) { prop_reset_active_state(piVar4); }
            } else {
                uVar2 = input_get_buttons_held(*(int *)DAT_80611d74);
                if ((*(int *)(DAT_80611d74 + 0x24) & (int)uVar2) != 0) { prop_set_idle_if_valid(piVar4); }
            }
        }
    }
    FUN_8038273c();
}

// ============================================================================
// 0x80256bf4  player_ledge_hang_update
// Ledge-hang shimmy/mantle progress update. Sweeps other players in mode 0x200
// to constrain the shimmy direction (bVar6/bVar7 = free left/right), fires the
// per-level scrape SFX pair on anim events, rebuilds the hang world transform
// (yaw from the ledge tangent via atan2 + smoothstep, position via a rotated
// offset), advances the mantle progress at (rig+4)+0x100, and on completion does
// a floor query + starts the get-up anim. Returns nonzero when the climb finishes.
// ============================================================================
int player_ledge_hang_update(void)
{
    float fVar1, fVar2, fVar3, fVar4, fVar5;
    bool  bVar6, bVar7;
    int  *piVar8;
    int   iVar9, iVar11, iVar12, iVar13, iVar14, iVar15, iVar16, iVar17;
    double dVar18, dVar19, dVar20, dVar21, dVar22, dVar23, dVar24, dVar25, dVar26;
    undefined8 uVar27;
    float local_288, local_284, local_280, local_27c, local_278, local_274;
    int   local_270[12], local_240[12], local_210[12], local_1e0[12];
    int   local_1b0[12], local_180[12], local_150[12], local_120[12];
    float afStack_f0[16];
    double local_b0;

    uVar27 = FUN_803826d0();
    piVar8 = (int *)(uint)((ulonglong)uVar27 >> 0x20);
    iVar11 = (int)uVar27;
    iVar15 = *piVar8;
    iVar16 = *(int *)(iVar15 + 4);
    iVar14 = *(int *)(DAT_80611824 + *(int *)(iVar16 + 0xd0) * 4);
    dVar19 = (double)FLOAT_80618e58;   /* incoming f1 (mantle rate); seeded 0 */
    dVar18 = FUN_80390324((double)(((*(float *)(iVar11 + 0x18) - *(float *)(iVar11 + 0xc)) -
                                    FLOAT_80618e78) / FLOAT_80618ea0));
    local_b0 = (double)(longlong)(int)dVar18;
    bVar6 = true;
    bVar7 = true;
    iVar9  = 0;
    iVar17 = DAT_80612610;
    if (0 < DAT_80612610) {
        do {
            iVar12 = DAT_80611d78 + iVar9;
            iVar13 = *(int *)(iVar12 + 0x14);
            if (((iVar13 != iVar15) && (*(int *)(iVar12 + 0xe90) == iVar11)) &&
                (*(int *)(iVar12 + 0x114) == 0x200)) {
                fVar1 = *(float *)(iVar15 + 0x68) + *(float *)(iVar15 + 0x1ac);
                fVar3 = *(float *)(iVar13 + 0x68) + *(float *)(iVar13 + 0x1ac);
                fVar2 = fVar1 - *(float *)(iVar15 + 0x1c4);
                fVar1 = *(float *)(iVar15 + 0x1c4) + fVar1;
                fVar4 = fVar3 - *(float *)(iVar13 + 0x1c4);
                fVar3 = *(float *)(iVar13 + 0x1c4) + fVar3;
                fVar5 = fVar1;
                if (fVar1 < fVar2) { fVar5 = fVar2; }
                if (fVar2 < fVar1) { fVar1 = fVar2; }
                fVar2 = fVar3;
                if (fVar3 < fVar4) { fVar2 = fVar4; }
                if (fVar5 < fVar2) {
                    fVar2 = fVar3;
                    if (fVar4 < fVar3) { fVar2 = fVar4; }
                    if (fVar2 < fVar5) { bVar6 = false; }
                }
                fVar2 = fVar3;
                if (fVar3 < fVar4) { fVar2 = fVar4; }
                if (fVar1 < fVar2) {
                    if (fVar4 < fVar3) { fVar3 = fVar4; }
                    if (fVar3 < fVar1) { bVar7 = false; }
                }
            }
            iVar9  = iVar9 + 0x141c;
            iVar17 = iVar17 + -1;
        } while (iVar17 != 0);
    }
    iVar17 = *(int *)(iVar15 + 0x150);
    if (iVar17 != 0) {
        if (*(float *)(iVar16 + 0x100) <= FLOAT_80618e58) {
            if (*(float *)(iVar16 + 0x100) < FLOAT_80618e58) {
                uint uVar10 = *(uint *)(iVar17 + 0x13b0);
                iVar9 = ((int)uVar10 >> 1) + (uint)((int)uVar10 < 0 && (uVar10 & 1) != 0);
                if (0 < iVar9) {
                    if (((uVar10 & 1) != 0) &&
                        (iVar12 = anim_event_triggered_this_frame(iVar15, 1, iVar9), iVar12 != 0)) {
                        if (DAT_80612614 == 0xb) {
                            sound_init_play_params((int)local_1e0); local_1e0[0] = 0x395; sound_play_request(local_1e0);
                        } else if (DAT_80612614 == 0x12) {
                            sound_init_play_params((int)local_210); local_210[0] = 0x601; sound_play_request(local_210);
                        }
                        *(int *)(iVar17 + 0x13b0) = *(int *)(iVar17 + 0x13b0) + -1;
                    }
                    if (((uVar10 & 1) == 0) &&
                        (iVar9 = anim_event_triggered_this_frame(iVar15, 2, iVar9), iVar9 != 0)) {
                        if (DAT_80612614 == 0xb) {
                            sound_init_play_params((int)local_240); local_240[0] = 0x396; sound_play_request(local_240);
                        } else if (DAT_80612614 == 0x12) {
                            sound_init_play_params((int)local_270); local_270[0] = 0x602; sound_play_request(local_270);
                        }
                        *(int *)(iVar17 + 0x13b0) = *(int *)(iVar17 + 0x13b0) + -1;
                    }
                }
            }
        } else {
            uint uVar10 = *(uint *)(iVar17 + 0x13b0);
            iVar9 = ((int)uVar10 >> 1) + (uint)((int)uVar10 < 0 && (uVar10 & 1) != 0) + 1;
            if (0 < iVar9) {
                if (((uVar10 & 1) != 0) &&
                    (iVar12 = anim_event_triggered_this_frame(iVar15, 1, iVar9), iVar12 != 0)) {
                    if (DAT_80612614 == 0xb) {
                        sound_init_play_params((int)local_120); local_120[0] = 0x395; sound_play_request(local_120);
                    } else if (DAT_80612614 == 0x12) {
                        sound_init_play_params((int)local_150); local_150[0] = 0x601; sound_play_request(local_150);
                    }
                    *(int *)(iVar17 + 0x13b0) = *(int *)(iVar17 + 0x13b0) + 1;
                }
                if (((uVar10 & 1) == 0) &&
                    (iVar9 = anim_event_triggered_this_frame(iVar15, 2, iVar9), iVar9 != 0)) {
                    if (DAT_80612614 == 0xb) {
                        sound_init_play_params((int)local_180); local_180[0] = 0x396; sound_play_request(local_180);
                    } else if (DAT_80612614 == 0x12) {
                        sound_init_play_params((int)local_1b0); local_1b0[0] = 0x602; sound_play_request(local_1b0);
                    }
                    *(int *)(iVar17 + 0x13b0) = *(int *)(iVar17 + 0x13b0) + 1;
                }
            }
        }
    }
    dVar20 = (double)(float)((double)*(float *)(iVar11 + 0x14) - (double)*(float *)(iVar11 + 8));
    dVar23 = (double)(float)((double)*(float *)(iVar11 + 0x1c) - (double)*(float *)(iVar11 + 0x10));
    dVar22 = -dVar20;
    dVar26 = (double)(float)((double)FLOAT_80618ea0 * dVar20 + (double)*(float *)(iVar11 + 8));
    dVar25 = (double)(FLOAT_80618eb4 + *(float *)(iVar11 + 0xc));
    dVar24 = (double)(float)((double)FLOAT_80618ea0 * dVar23 + (double)*(float *)(iVar11 + 0x10));
    dVar20 = (double)math_atan2(dVar23, dVar22);
    dVar21 = (double)((FLOAT_80618e70 * -(float)dVar20) / FLOAT_80618e74);
    dVar20 = FUN_80390324((double)((float)(dVar21 - (double)FLOAT_80618e58) / FLOAT_80618e80));
    fVar2 = FLOAT_80618e74;
    fVar1 = FLOAT_80618e70;
    *(float *)(iVar15 + 0x70) = -(float)((double)FLOAT_80618e80 * (double)(float)dVar20 - dVar21);
    mtx4_set_rotation_y((double)((fVar2 * *(float *)(iVar15 + 0x70)) / fVar1), afStack_f0);
    dVar20 = math_sqrt((double)(float)(dVar23 * dVar23 + (double)(float)(dVar22 * dVar22)));
    local_288 = (float)((double)FLOAT_80618ea0 * dVar20);
    local_284 = FLOAT_80618e58;
    local_280 = FLOAT_80618e58;
    mtx4_transform_point_alias(afStack_f0, &local_288);
    local_27c = *(float *)(iVar16 + 0xdc);
    dVar26 = (double)(float)(dVar26 + (double)local_288);
    local_278 = *(float *)(iVar16 + 0xe0);
    dVar23 = (double)(float)(dVar25 + (double)local_284);
    dVar22 = (double)(float)(dVar24 + (double)local_280);
    local_274 = *(float *)(iVar16 + 0xe4);
    mtx4_transform_point_alias(afStack_f0, &local_27c);
    fVar1  = FLOAT_80618ea0;
    dVar20 = (double)FLOAT_80618e78;
    dVar21 = (double)DOUBLE_80618ee0;
    *(float *)(iVar15 + 100)  = (float)((double)local_27c + dVar26);
    *(float *)(iVar15 + 0x68) = (float)((double)local_278 + dVar23);
    *(float *)(iVar15 + 0x6c) = (float)((double)local_274 + dVar22);
    dVar20 = (double)((*(float *)(iVar15 + 0x68) - (float)(dVar20 + dVar23)) / fVar1);
    if (((double)FLOAT_80618ed4 <= dVar20) || (FLOAT_80618ed8 <= *(float *)(iVar16 + 0x100))) {
        dVar21 = FUN_80390324(dVar20);
        local_b0 = (double)CONCAT44(0x43300000, (int)dVar18 ^ 0x80000000);
        if ((DOUBLE_80618ee0 + dVar21 < (double)(float)(local_b0 - DOUBLE_80618e90)) ||
            (*(float *)(iVar16 + 0x100) <= FLOAT_80618e58)) {
            if (((dVar20 <= (double)(((*(float *)(iVar11 + 0x18) - *(float *)(iVar11 + 0xc)) -
                                      FLOAT_80618ea0) / FLOAT_80618ea0)) ||
                 (FLOAT_80618e58 == *(float *)(iVar16 + 0x100))) &&
                (dVar18 = FUN_80390324(dVar20), ABS_F((float)(dVar20 - dVar18)) < FLOAT_80618eb8)) {
                if ((dVar19 <= (double)FLOAT_80618ed8) || (!bVar6)) {
                    if (((double)FLOAT_80618eec <= dVar19) || (!bVar7)) {
                        *(float *)(iVar16 + 0x100) = FLOAT_80618e58;
                    } else {
                        *(float *)(iVar16 + 0x100) = FLOAT_80618e9c;
                    }
                } else {
                    *(float *)(iVar16 + 0x100) = FLOAT_80618e64;
                }
            }
            goto LAB_80257268;
        }
    }
    if (((*(int *)(iVar15 + 0x150) != 0) &&
         (iVar17 = *(int *)(*(int *)(iVar15 + 0x150) + 8), iVar17 != 0)) &&
        ((*(int *)(iVar17 + 0x218) == 0x18 || (*(int *)(iVar17 + 0x218) == 0x19)))) {
        camera_set_mode_by_index(iVar17, 6, iVar15);
    }
    if (ABS_F(*(float *)(iVar16 + 0x100)) <= FLOAT_80618e64) {
        *(float *)(iVar16 + 0x100) = *(float *)(iVar16 + 0x100) * FLOAT_80618ee8;
    }
LAB_80257268:
    if ((*(float *)(iVar16 + 0xd4) <= FLOAT_80618e58) ||
        (*(float *)(iVar14 + 0x18) <= *(float *)(iVar16 + 0xd4))) {
        dVar19 = (double)floor_query_with_interact(
                     (double)*(float *)(&DAT_8048ca94 + *(short *)((int)piVar8 + 0x48) * 0x30),
                     (double)*(float *)(&DAT_8048ca9c + *(short *)((int)piVar8 + 0x48) * 0x30),
                     iVar15 + 100, iVar15 + 0x21c, (int *)0x0, 0);
        *(int *)((int)piVar8 + 0xbfc) = (int)(float)dVar19;
        anim_play_scaled((double)FLOAT_80618e58, (double)FLOAT_80618e64);
        aiactor_leader_zone_advance(iVar15, 0, 1);
    }
    FUN_8038271c();
    return (int)dVar19;
}

// ============================================================================
// 0x80257340  player_ledge_grab_tick
// State tick for the ledge-hang state (0x200). On first entry (rig +0xe90 == 0)
// it sets the mounted state and switches the camera to the ledge orbit mode;
// each subsequent frame it drives the orbit camera parameters from the stick,
// calls player_ledge_hang_update, and on completion mantles the player up
// (state 2, model swap-in) and rebuilds the hang anchor transform. Called from
// FUN_8025a004 (state machine).
// ============================================================================
void player_ledge_grab_tick(void)
{
    float  fVar1, fVar2;
    float *pfVar3;
    int    iVar4, object, iVar5;
    float  local_78, local_74, local_70;
    float  afStack_6c[25];

    pfVar3 = (float *)__save_gpr_28();
    iVar5 = *(int *)(DAT_80611d74 + 0x14);
    iVar4 = *(int *)(DAT_80611d74 + 8);
    object = *(int *)(iVar5 + 0x7c);
    if (*(int *)(DAT_80611d74 + 0xe90) == 0) {
        player_set_state(DAT_80611d74, 4);
        *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) | 0x200;
        if (iVar4 != 0) { camera_set_mode_by_index(iVar4, 6, iVar5); }
    } else {
        if (iVar4 != 0) {
            fVar1 = FLOAT_80618e64;
            if ((*(ushort *)(*(int *)(DAT_80611d74 + 0x1398) + 2) & 1) != 0) { fVar1 = FLOAT_80618e9c; }
            camera_set_orbit_params(
                (double)*(float *)(iVar4 + 0xfec),
                (double)(FLOAT_80618e98 * pfVar3[1] + FLOAT_80618ea0),
                (double)*(float *)(iVar4 + 0x2e8), (double)*(float *)(iVar4 + 0x2ec),
                (double)FLOAT_80618e58, (double)*(float *)(iVar4 + 0x2f0),
                (double)(FLOAT_80618ef0 * *pfVar3), (double)(FLOAT_80618ef4 * pfVar3[1] * fVar1),
                iVar4);
        }
        iVar4 = player_ledge_hang_update();
        if (iVar4 != 0) {
            player_set_state(DAT_80611d74, 2);
            *(int *)(DAT_80611d74 + 0xe90) = 0;
            object_model_swap_in(object);
        }
        local_70 = FLOAT_80618e58;
        *(int *)(DAT_80611d74 + 0x100) = *(int *)(iVar5 + 0x70);
        fVar2 = FLOAT_80618e74;
        fVar1 = FLOAT_80618e70;
        *(float *)(DAT_80611d74 + 0x104) = local_70;
        *(int *)(DAT_80611d74 + 0xa8) = *(int *)(iVar5 + 100);
        *(int *)(DAT_80611d74 + 0xac) = *(int *)(iVar5 + 0x68);
        *(int *)(DAT_80611d74 + 0xb0) = *(int *)(iVar5 + 0x6c);
        iVar4 = *(int *)(*(int *)(iVar5 + 4) + 0x10) +
                *(int *)(*(int *)(*(int *)(iVar5 + 4) + 0x84) + 0x54) * 0x40;
        local_78 = *(float *)(iVar4 + 0x30);
        local_74 = *(float *)(iVar4 + 0x34);
        mtx4_set_rotation_y((double)((fVar2 * *(float *)(iVar5 + 0x70)) / fVar1), afStack_6c);
        mtx4_transform_point_alias(afStack_6c, &local_78);
        *(float *)(DAT_80611d74 + 0xb4) = local_78 + *(float *)(iVar5 + 100);
        *(float *)(DAT_80611d74 + 0xb8) = local_74 + *(float *)(iVar5 + 0x68);
        *(float *)(DAT_80611d74 + 0xbc) = local_70 + *(float *)(iVar5 + 0x6c);
    }
    __restore_gpr_28();
}

// ============================================================================
// 0x8025755c  player_exit_ledge_state
// Force-exits the ledge-hang state (0x200): restores the normal player state,
// clears the ledge ref (+0xe90), swaps the model back in and resets the camera.
// ============================================================================
void player_exit_ledge_state(int player)
{
    int object;
    if (((*(uint *)(player + 0x114) & 0x200) != 0) && (*(int *)(player + 0x14) != 0)) {
        object = *(int *)(*(int *)(player + 0x14) + 0x7c);
        player_set_state(player, 2);
        *(int *)(player + 0xe90) = 0;
        object_model_swap_in(object);
        if (*(int *)(player + 8) != 0) {
            camera_set_mode_by_index(*(int *)(player + 8), 0, *(int *)(player + 0x14));
        }
    }
}

// ============================================================================
// 0x802575d8  player_try_grab_ledge
// Ledge/mantle detection: given a candidate surface record (surf) hit by the nav
// sweep and the current move vector, checks the surface is grabbable (flags +4,
// height band vs eye), the approach angle is within tolerance (dot < threshold),
// and the surface is nearer the top than the bottom; if so it enters the hang
// state (0x200): plays the grab anim, records the ledge ref (+0xe90), swaps the
// model out, and sets the ledge orbit camera. Returns the hang-entered flag.
// Called from player_swim_move_tick / the movement dispatch.
// ============================================================================
int player_try_grab_ledge(int player, float *pos, float *dir, int hit, int surf)
{
    int    iVar1, iVar6, object;
    float  fVar2, fVar3, fVar4, fVar5;
    double dVar7;
    int    entered = 0;
    (void)player; (void)pos;

    iVar6 = __save_gpr_28();
    /* NOTE: the decompiler's incoming f1 (mantle progress) is not observable as a
       C parameter here; the ROM reads it from the register pair set up by the
       caller. Modeled via the surface height band test below. */
    float mantleT = (*(float *)(surf + 0x18) - *(float *)(surf + 0xc));
    if (((((*(uint *)(surf + 4) & 0x20) != 0) && ((DAT_805015c0 & 0x200) == 0)) &&
         ((double)FLOAT_80618ef8 < (double)mantleT)) &&
        ((*(float *)(surf + 0xc) <= *(float *)(iVar6 + 0xac) + *(float *)(iVar6 + 0x84) &&
          (*(float *)(iVar6 + 0xb8) < *(float *)(surf + 0x18))))) {
        if (*dir * *(float *)(hit + 4) + dir[1] * *(float *)(hit + 0xc) < FLOAT_80618efc) {
            fVar2 = *(float *)(iVar6 + 0xc0);
            fVar3 = *(float *)(iVar6 + 200);
            fVar4 = fVar3 * fVar3 + fVar2 * fVar2 + FLOAT_80618e58;
            if (FLOAT_80618f00 < fVar4) {
                fVar5 = 1.0f / SQRT_F(fVar4);
                fVar5 = fVar5 * -(FLOAT_80618ea0 * fVar4 * fVar5 * fVar5 - FLOAT_80618e5c);
                fVar4 = FLOAT_80618e64 * fVar5 * -(FLOAT_80618ea0 * fVar4 * fVar5 * fVar5 - FLOAT_80618e5c);
                fVar2 = fVar2 * fVar4;
                fVar3 = fVar3 * fVar4;
            }
            if ((fVar2 * *(float *)(hit + 4) + fVar3 * *(float *)(hit + 0xc) < FLOAT_80618efc) &&
                (ABS_F(*(float *)(iVar6 + 0xac) - *(float *)(surf + 0xc)) <
                 ABS_F(*(float *)(iVar6 + 0xac) - *(float *)(surf + 0x18)))) {
                object = *(int *)(*(int *)(iVar6 + 0x14) + 0x7c);
                dVar7 = FUN_80390324((double)(((*(float *)(surf + 0x18) - *(float *)(surf + 0xc)) -
                                               FLOAT_80618e78) / FLOAT_80618ea0));
                iVar1 = (int)dVar7;
                if (iVar1 == 0x20) {
                    anim_clip_field_b08(*(int *)(iVar6 + 0x14));
                    anim_start_clip((double)FLOAT_80618e58, (double)FLOAT_80618e64);
                } else if ((iVar1 < 0x20) && (iVar1 == 0xb)) {
                    anim_clip_field_b08(*(int *)(iVar6 + 0x14));
                    anim_start_clip((double)FLOAT_80618e58, (double)FLOAT_80618e64);
                }
                player_set_state(iVar6, 0x200);
                *(int *)(iVar6 + 0xe90) = surf;
                *(int *)(iVar6 + 0x13b0) = 0;
                object_model_swap_out_and_clear(object);
                if (*(int *)(iVar6 + 8) != 0) {
                    dVar7 = (double)FLOAT_80618e58;
                    camera_set_orbit_params(
                        (double)FLOAT_80618e5c, (double)FLOAT_80618ea0, (double)FLOAT_80618f04,
                        (double)FLOAT_80618f08, dVar7, (double)FLOAT_80618f0c, dVar7, dVar7,
                        *(int *)(iVar6 + 8));
                    camera_set_mode_by_index(*(int *)(iVar6 + 8), 5, *(int *)(iVar6 + 0x14));
                }
                entered = 1;
            }
        }
    }
    __restore_gpr_28();
    return entered;
}

// ============================================================================
// 0x80257824  player_swim_move_tick
// Water-surface movement tick. Clamps the move speed to the per-frame budget,
// applies the dive/surface button boosts and the drag field (+0x501), computes
// the surface normal from the current tri (if flagged) to orient movement, then
// integrates the eye position through player_ai_rail_track_follow +
// nav_advanced_movement_dispatch (attempting a ledge grab via player_try_grab_ledge
// on a wall hit) and finally writes the eye position and vertical bob. Called
// from player_state_dispatch.
// ============================================================================
void player_swim_move_tick(undefined8 param_1, double param_2, undefined4 param_3, undefined4 param_4,
                           float *param_5, float *param_6)
{
    float  fVar1, fVar2;
    bool   bVar3;
    float *pfVar4;
    uint   uVar5, uVar7;
    int    iVar6, iVar9, iVar10, iVar11, iVar12;
    double dVar13, dVar14, dVar15, dVar16, dVar17, dVar18, dVar19;
    float  local_e8[6];
    int    local_d0[5];
    int    local_bc;
    (void)param_1; (void)param_3; (void)param_4;

    pfVar4 = (float *)FUN_803826e4();
    dVar18 = (double)FLOAT_80618e58;
    uVar7  = *(uint *)(DAT_80611d74 + 0x14);
    iVar11 = *(int *)(uVar7 + 0x7c);
    bVar3  = false;
    dVar17 = param_2;   /* incoming move-x (f1 alias); dive/surface pipeline below */
    dVar19 = dVar18;
    dVar14 = math_sqrt((double)(float)(dVar17 * dVar17 + (double)(float)(param_2 * param_2)));
    dVar13 = ABS_D((double)((FLOAT_80618e64 /
                             (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) *
                            FLOAT_806108ec *
                            *(float *)(DAT_80611d74 + (*(int *)(DAT_80611d74 + 0x11c) + 0x1c) * 4)));
    if (dVar13 < dVar14) {
        dVar13  = (double)(float)(dVar13 / dVar14);
        param_2 = (double)(float)(dVar13 * param_2);
        dVar17  = (double)(float)(dVar13 * dVar17);
    }
    void *pfVar8 = (void *)&DAT_80501570;
    if ((((DAT_805015be == '\x11') && (*(int *)DAT_80611d74 == DAT_80501600)) &&
         (uVar5 = input_get_buttons_pressed(*(int *)DAT_80611d74), (uVar5 & 0x400) != 0)) &&
        (FLOAT_80618e58 < *(float *)(uVar7 + 0x1d0))) {
        param_2 = (double)(float)(param_2 * (double)FLOAT_80618f18);
        dVar17  = (double)(float)(dVar17 * (double)FLOAT_80618f18);
        *(float *)(uVar7 + 0x1d0) = *(float *)(uVar7 + 0x1d0) - FLOAT_80618f1c;
        *(int *)(uVar7 + 0x1d4) = DAT_80611928;
    }
    if (FLOAT_80618e58 < *(float *)(DAT_80611d74 + 0x1404)) {
        dVar13  = (double)(FLOAT_80618e64 - *(float *)(DAT_80611d74 + 0x1404));
        param_2 = (double)(float)(param_2 * dVar13);
        dVar17  = (double)(float)(dVar17 * dVar13);
    }
    iVar9 = *(int *)(iVar11 + 0xc00);
    if ((iVar9 != 0) && ((*(ushort *)(iVar9 + 8) & 2) != 0)) {
        float *tri = local_e8;
        iVar6  = 0;
        iVar12 = 3;
        do {
            iVar10 = iVar9 + iVar6;
            fVar1 = *(float *)(iVar10 + 0x3c);
            fVar2 = *(float *)(iVar10 + 0x24);
            *(float *)((int)local_e8 + iVar6 + 0xc) = fVar1 - *(float *)(iVar10 + 0x54);
            *(float *)((int)tri + iVar6) = fVar1 - fVar2;
            iVar6  = iVar6 + 4;
            iVar12 = iVar12 + -1;
        } while (iVar12 != 0);
        dVar15 = (double)(local_e8[5] * local_e8[0] - local_e8[2] * local_e8[3]);
        dVar16 = (double)(local_e8[4] * local_e8[2] - local_e8[1] * local_e8[5]);
        dVar14 = (double)(local_e8[3] * local_e8[1] - local_e8[0] * local_e8[4]);
        dVar13 = math_sqrt((double)(float)(dVar14 * dVar14 +
                                           (double)(float)(dVar16 * dVar16 +
                                                           (double)(float)(dVar15 * dVar15))));
        if (dVar15 < dVar13) {
            dVar19 = (double)(float)(dVar16 / dVar13);
            bVar3  = true;
            dVar18 = (double)(float)(dVar14 / dVar13);
        }
        pfVar8 = tri;
    }
    fVar1  = FLOAT_80618ea0;
    if (bVar3) {
        *pfVar4 = *(float *)(DAT_80611d74 + 0xa8) +
                  (*(float *)(DAT_80611d74 + (*(int *)(DAT_80611d74 + 0x11c) + 0x1c) * 4) *
                   (float)((double)FLOAT_806108ec * dVar19)) /
                  (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90);
        pfVar4[1] = *(float *)(DAT_80611d74 + 0xac) - fVar1;
        pfVar4[2] = *(float *)(DAT_80611d74 + 0xb0) +
                    (*(float *)(DAT_80611d74 + (*(int *)(DAT_80611d74 + 0x11c) + 0x1c) * 4) *
                     (float)((double)FLOAT_806108ec * dVar18)) /
                    (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90);
    } else {
        iVar9 = anim_clip_flags_active_slot(uVar7, pfVar8, 0, 0x40000);
        if (iVar9 == 0) {
            if (*(int *)(DAT_80611d74 + 0x11c) == 1) {
                *pfVar4    = *(float *)(DAT_80611d74 + 0xa8) + *(float *)(DAT_80611d74 + 0x120);
                pfVar4[1]  = *(float *)(DAT_80611d74 + 0xac);
                pfVar4[2]  = *(float *)(DAT_80611d74 + 0xb0) + *(float *)(DAT_80611d74 + 0x124);
            } else {
                *pfVar4    = (float)((double)*param_6 * param_2 + (double)*(float *)(DAT_80611d74 + 0xa8));
                pfVar4[1]  = *(float *)(DAT_80611d74 + 0xac);
                pfVar4[2]  = (float)((double)param_6[1] * param_2 + (double)*(float *)(DAT_80611d74 + 0xb0));
            }
            *pfVar4   = (float)((double)*param_5 * dVar17 + (double)*pfVar4);
            pfVar4[2] = (float)((double)param_5[1] * dVar17 + (double)pfVar4[2]);
            *pfVar4   = *pfVar4   + *(float *)(DAT_80611d74 + 0xf4);
            pfVar4[1] = pfVar4[1] + *(float *)(DAT_80611d74 + 0xf8);
            pfVar4[2] = pfVar4[2] + *(float *)(DAT_80611d74 + 0xfc);
        } else {
            *pfVar4    = *(float *)(DAT_80611d74 + 0xa8);
            pfVar4[1]  = *(float *)(DAT_80611d74 + 0xac);
            pfVar4[2]  = *(float *)(DAT_80611d74 + 0xb0);
        }
    }
    if ((DAT_8060faec == 0) && (-1 < *(int *)(DAT_80611d74 + 4))) {
        uVar7 = input_get_buttons_pressed(*(int *)DAT_80611d74);
        if ((uVar7 & 0x400) != 0) { pfVar4[1] = pfVar4[1] - FLOAT_80618eb4; }
        uVar7 = input_get_buttons_pressed(*(int *)DAT_80611d74);
        if ((uVar7 & 0x800) != 0) { pfVar4[1] = pfVar4[1] + FLOAT_80618eb4; }
    } else {
        pfVar4[1] = -(FLOAT_80618ef8 * FLOAT_806108ec - pfVar4[1]);
        dVar13 = (double)pfVar4[1];
        if ((*(int *)(iVar11 + 0xc00) == 0) ||
            ((*(ushort *)(*(int *)(iVar11 + 0xc00) + 8) & 0x2000) == 0)) {
            player_ai_rail_track_follow(*(int *)DAT_80611d74, pfVar4, (float *)(DAT_80611d74 + 0x100));
        }
        dVar14 = (double)(float)(dVar13 - (double)pfVar4[1]);
        dVar13 = (double)*(float *)(iVar11 + 0xbfc);
        uVar7 = nav_advanced_movement_dispatch(
                    (double)(*(float *)(DAT_80611d74 + 0x94) + *(float *)(DAT_80611d74 + 0x8c)),
                    (double)*(float *)(DAT_80611d74 + 0xa0), *(int *)DAT_80611d74,
                    (int *)(DAT_80611d74 + 0xa8), pfVar4, local_d0, 1, 0);
        if (((uVar7 & 0xe) != 0) && ((local_d0[0] != 0 || (local_bc != 0)))) {
            if (local_d0[0] != 0) { local_bc = local_d0[0]; }
            uVar5 = (uint)player_try_grab_ledge(DAT_80611d74, pfVar4, param_6, (int)local_d0, local_bc);
            uVar7 = uVar7 | uVar5;
        }
        if (*(int *)(DAT_80611d74 + 0x114) == 2) {
            fVar1 = (float)((double)(float)(dVar13 - (double)*(float *)(iVar11 + 0xbfc)) - dVar14);
            if (ABS_F(fVar1) <= FLOAT_80618f20) {
                pfVar4[1] = *(float *)(iVar11 + 0xbfc);
                uVar7 = uVar7 | 1;
                *(float *)(DAT_80611d74 + 0x9c) = *(float *)(DAT_80611d74 + 0x9c) + fVar1;
            }
        }
        if ((uVar7 & 1) == 0) {
            player_set_state(DAT_80611d74, 4);
            *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) | 0x200;
            fVar1 = FLOAT_80618e58;
            if (bVar3) {
                *(float *)(DAT_80611d74 + 0xe38) =
                    (float)(dVar19 * (double)*(float *)(DAT_80611d74 + (*(int *)(DAT_80611d74 + 0x11c) + 0x1c) * 4)) /
                    (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90);
                *(float *)(DAT_80611d74 + 0xe40) =
                    (float)(dVar18 * (double)*(float *)(DAT_80611d74 + (*(int *)(DAT_80611d74 + 0x11c) + 0x1c) * 4)) /
                    (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90);
                *(float *)(iVar11 + 0xb1c) = fVar1;
            } else {
                *(float *)(DAT_80611d74 + 0xe38) =
                    (float)((double)*param_6 * param_2 + (double)(float)((double)*param_5 * dVar17)) / FLOAT_806108ec;
                *(float *)(DAT_80611d74 + 0xe40) =
                    (float)((double)param_6[1] * param_2 +
                            (double)(float)((double)param_5[1] * dVar17)) / FLOAT_806108ec;
                *(float *)(iVar11 + 0xb1c) = fVar1;
                if (*(int *)(DAT_80611d74 + 0x11c) == 1) {
                    *(float *)(iVar11 + 0xb1c) = FLOAT_80618eb8;
                }
            }
        }
        fVar1 = FLOAT_80618e58;
        if (FLOAT_80618e58 <= *(float *)(iVar11 + 0xb1c)) {
            fVar2 = *(float *)(DAT_80611d74 + 0x98);
            if (FLOAT_80618e58 <= fVar2) {
                if ((FLOAT_80618e58 < fVar2) &&
                    (*(float *)(DAT_80611d74 + 0x98) = -(FLOAT_80618f28 * FLOAT_806108ec - fVar2),
                     *(float *)(DAT_80611d74 + 0x98) < fVar1)) {
                    *(float *)(DAT_80611d74 + 0x98) = fVar1;
                }
                goto LAB_80257f98;
            }
        }
        if (*(float *)(iVar11 + 0xb1c) < FLOAT_80618f24) {
            *(float *)(iVar11 + 0xb1c) = FLOAT_80618f24;
        }
        fVar2 = FLOAT_80618f2c;
        fVar1 = FLOAT_80618e58;
        *(float *)(iVar11 + 0xb1c) = FLOAT_80618f28 * FLOAT_806108ec + *(float *)(iVar11 + 0xb1c);
        *(float *)(DAT_80611d74 + 0x98) =
            fVar2 * *(float *)(iVar11 + 0xb1c) * FLOAT_806108ec + *(float *)(DAT_80611d74 + 0x98);
        if (fVar1 < *(float *)(DAT_80611d74 + 0x98)) { *(float *)(DAT_80611d74 + 0x98) = fVar1; }
        if (*(float *)(DAT_80611d74 + 0x90) + *(float *)(DAT_80611d74 + 0x98) < FLOAT_80618ea0) {
            *(float *)(DAT_80611d74 + 0x98) = FLOAT_80618ea0 - *(float *)(DAT_80611d74 + 0x90);
        }
    }
LAB_80257f98:
    *(float *)(DAT_80611d74 + 0xa8) = *pfVar4;
    *(float *)(DAT_80611d74 + 0xac) = pfVar4[1];
    *(float *)(DAT_80611d74 + 0xb0) = pfVar4[2];
    player_set_eye_position(
        (double)*(float *)(DAT_80611d74 + 0xa8),
        (double)(*(float *)(DAT_80611d74 + 0x98) +
                 *(float *)(DAT_80611d74 + 0xac) + *(float *)(DAT_80611d74 + 0x90)),
        (double)*(float *)(DAT_80611d74 + 0xb0), (int)DAT_80611d74);
    *(uint *)(iVar11 + 0x24) = *(uint *)(iVar11 + 0x24) | 1;
    *(uint *)(iVar11 + 0x24) = *(uint *)(iVar11 + 0x24) & 0xfffffff7;
    FUN_80382730();
}

// ============================================================================
// 0x80258048  player_is_special_arcade_mode
// Predicate: true when the player has a valid rig, the game is NOT standard
// arcade, and the player mission-mode field (+0x1390) == 0x11 (the special mode).
// ============================================================================
undefined4 player_is_special_arcade_mode(int player)
{
    int        iVar1;
    undefined4 uVar2 = 0;
    if (((*(int *)(*(int *)(player + 0x14) + 0x7c) != 0) &&
         (iVar1 = game_is_standard_arcade(), iVar1 == 0)) && (*(int *)(player + 0x1390) == 0x11)) {
        uVar2 = 1;
    }
    return uVar2;
}

// ============================================================================
// 0x802580ac  player_scan_teammate_aim_block
// Sweeps the teammate table for a teammate whose aim-target segment is blocked by
// this player's swept sphere (used by the aim-assist / friendly-fire avoidance
// path). Skips self, culled teammates, and same-vehicle teammates; breaks on the
// first blocking hit. Called from FUN_8025967c.
// ============================================================================
int player_scan_teammate_aim_block(int player, int aimPos, int targetVec)
{
    int  *piVar1;
    int   iVar2 = 0, iVar4, iVar6, iVar7;
    uint  uVar3;
    int  *piVar5;
    undefined8 uVar8;
    undefined4 local_48;
    float afStack_44[3];
    float afStack_38[14];
    (void)player; (void)aimPos;

    uVar8  = FUN_803826d8();
    piVar1 = (int *)(uint)((ulonglong)uVar8 >> 0x20);
    iVar6  = DAT_80611d90 + *piVar1 * 0xea0;
    iVar4  = 0;
    for (iVar7 = 0; iVar7 < DAT_80612098; iVar7 = iVar7 + 1) {
        piVar5 = *(int **)((int)&DAT_80588610 + iVar4);
        if ((((iVar7 != *piVar1) && ((*(uint *)(*piVar5 + 0x10) & 0xffff) != 0)) &&
             ((piVar5[9] & 8U) == 0)) && (*(int *)(iVar6 + 0x84) != piVar5[0x21])) {
            if (DAT_805015bd == '\n') {
                uVar3 = piVar5[5];
            } else {
                uVar3 = piVar5[7];
            }
            if ((uVar3 & 0x18000) == 0) {
                player_ai_get_aim_position(piVar5, afStack_38, afStack_44, &local_48);
                iVar2 = swept_sphere_vs_segment((int)uVar8, targetVec, afStack_44, afStack_38,
                                                (float *)(piVar1 + 0x388), (float *)(piVar1 + 0x38b));
                if (iVar2 != 0) break;
            }
        }
        iVar4 = iVar4 + 4;
    }
    FUN_80382724();
    return iVar2;
}

// ============================================================================
// 0x80258654  player_can_aim_or_fire
// Predicate: whether the player is allowed to aim/fire right now. True for turret
// mode (0x40); otherwise true when the weapon is drawn (rig +0x94 non-zero and
// != +0x98 raised state, or a firing-capable mode 0x740) and the object can fire
// in its current mode, unless render_mode_class_a() overrides. Called from
// player_update_zoom_toggle.
// ============================================================================
undefined4 player_can_aim_or_fire(void)
{
    int  iVar1, iVar3;
    uint uVar2;

    uVar2 = *(uint *)(DAT_80611d74 + 0x114);
    iVar3 = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    if ((uVar2 & 0x40) != 0) { return 1; }
    iVar1 = *(int *)(iVar3 + 0x94);
    if (((((iVar1 == 0) && (uVar2 != 0x80)) || (iVar1 != *(int *)(iVar3 + 0x98))) ||
         (((uVar2 & 0x740) != 0 || (iVar3 = object_can_fire_in_mode(DAT_80611d74 + 0x4e4), iVar3 == 0)))) &&
        (iVar3 = render_mode_class_a(), iVar3 == 0)) {
        return 0;
    }
    return 1;
}

// ============================================================================
// 0x802586e0  player_reset_zoom_state
// Resets the camera zoom/FOV lerp endpoints (+0x38c/+900 <- +0x388, refresh +0x388
// from the camera lerp factor) and clears the player zoom-active / zoom-held flags
// (0x20 / 0x40) at +0xc.
// ============================================================================
void player_reset_zoom_state(void)
{
    double dVar1;
    if (DAT_80611d54 != 0) {
        *(int *)(DAT_80611d54 + 0x38c) = *(int *)(DAT_80611d54 + 0x388);
        *(int *)(DAT_80611d54 + 900)   = *(int *)(DAT_80611d54 + 0x388);
        dVar1 = player_get_camera_lerp_factor(DAT_80611d54);
        *(float *)(DAT_80611d54 + 0x388) = (float)dVar1;
    }
    *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xffffffdf;
    *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xffffffbf;
}

// ============================================================================
// 0x8025874c  player_update_zoom_toggle
// Per-frame zoom / iron-sight toggle handling (from player_main_tick). Reads the
// zoom button (mask +0x28) against player_can_aim_or_fire, toggles/holds the zoom
// flag (0x20) with the +0x100 latch and the hold-vs-toggle option
// (rig +0x4e6 flag 2), then drives the camera FOV lerp: while zoomed it advances
// the zoom stat, applies the FOV-step buttons (+0x30/+0x34) clamped to the FOV
// range (+0x394/+0x390), and eases the current FOV (+900) toward the target
// (+0x388). Calls player_reset_zoom_state on release and player_update_fov_zoom_lerp.
// ============================================================================
void player_update_zoom_toggle(void)
{
    float  fVar1, fVar2;
    int    iVar3, iVar4;
    uint   uVar5, uVar6;
    int    iVar7;
    double dVar8;

    FUN_803826f0();
    uVar6  = *(uint *)(DAT_80611d74 + 0xc);
    iVar7  = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    iVar3  = object_is_context_flagged(DAT_80611d74 + 0x4e4);
    if (*(int *)(DAT_80611d74 + 0x114) == 0x10) {
        *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xffffffdf;
        *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xfffffeff;
    } else {
        iVar4 = player_can_aim_or_fire();
        if (iVar4 == 0) {
            if ((*(uint *)(DAT_80611d74 + 0xc) & 0x20) != 0) {
                *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) | 0x100;
            }
            *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xffffffdf;
        } else {
            if ((*(uint *)(DAT_80611d74 + 0xc) & 0x100) == 0) {
                uVar5 = input_get_buttons_held(*(int *)DAT_80611d74);
                if ((*(int *)(DAT_80611d74 + 0x28) & (int)uVar5) == 0) {
                    if ((*(ushort *)(*(int *)(DAT_80611d74 + 0x1398) + 2) & 2) == 0) {
                        uVar5 = input_get_buttons_released(*(int *)DAT_80611d74);
                        if ((*(int *)(DAT_80611d74 + 0x28) & (int)uVar5) != 0) {
                            *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xffffffdf;
                        }
                    }
                    goto LAB_80258884;
                }
            }
            if ((*(ushort *)(*(int *)(DAT_80611d74 + 0x1398) + 2) & 2) == 0) {
                *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) | 0x20;
            } else {
                *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) ^ 0x20;
            }
            *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xfffffeff;
        }
    }
LAB_80258884:
    if ((*(ushort *)(*(int *)(DAT_80611d74 + 0x1398) + 2) & 2) == 0) {
        uVar5 = input_get_buttons_released(*(int *)DAT_80611d74);
        if ((*(int *)(DAT_80611d74 + 0x28) & (int)uVar5) != 0) {
            *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xfffffeff;
        }
    }
    if ((DAT_805015bd == '\n') && (DAT_80612614 == 0x38)) {
        *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xffffffdf;
    }
    if (DAT_80611d54 != 0) {
        if (*(uint *)(DAT_80611d74 + 0xc) != uVar6) {
            if ((*(uint *)(DAT_80611d74 + 0xc) & 0x20) == 0) {
                player_reset_zoom_state();
            } else {
                *(int *)(DAT_80611d54 + 0x388) = *(int *)(DAT_80611d54 + 0x38c);
                *(int *)(DAT_80611d54 + 900)   = *(int *)(DAT_80611d54 + 0x390);
                if (iVar3 != 0) {
                    *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) | 0x40;
                }
            }
        }
        if ((*(uint *)(DAT_80611d74 + 0xc) & 0x20) == 0) {
            if (((iVar3 != 0) && ((*(uint *)(DAT_80611d74 + 0xc) & 0x40) != 0)) &&
                (*(float *)(DAT_80611d54 + 900) == *(float *)(DAT_80611d54 + 0x388))) {
                dVar8 = player_get_camera_lerp_factor(DAT_80611d54);
                *(float *)(DAT_80611d54 + 0x388) = (float)dVar8;
                *(int *)(DAT_80611d54 + 900) = *(int *)(DAT_80611d54 + 0x388);
                *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xffffffbf;
            }
        } else {
            worm_stat_add((double)(FLOAT_806108ec *
                                   (FLOAT_80618e64 /
                                    (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90))),
                          0x3a, *(int *)(iVar7 + 8));
            uVar6 = input_get_buttons_pressed(*(int *)DAT_80611d74);
            if ((*(int *)(DAT_80611d74 + 0x30) & (int)uVar6) != 0) {
                *(float *)(DAT_80611d54 + 0x388) =
                    -((FLOAT_80618ef0 /
                       (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) *
                      FLOAT_806108ec - *(float *)(DAT_80611d54 + 0x388));
            }
            uVar6 = input_get_buttons_pressed(*(int *)DAT_80611d74);
            if ((*(int *)(DAT_80611d74 + 0x34) & (int)uVar6) != 0) {
                *(float *)(DAT_80611d54 + 0x388) =
                    (FLOAT_80618ef0 /
                     (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) *
                    FLOAT_806108ec + *(float *)(DAT_80611d54 + 0x388);
            }
            if (*(float *)(DAT_80611d54 + 0x388) < *(float *)(DAT_80611d54 + 0x394)) {
                *(float *)(DAT_80611d54 + 0x388) = *(float *)(DAT_80611d54 + 0x394);
            }
            if (*(float *)(DAT_80611d54 + 0x390) < *(float *)(DAT_80611d54 + 0x388)) {
                *(float *)(DAT_80611d54 + 0x388) = *(float *)(DAT_80611d54 + 0x390);
            }
        }
    }
    if (DAT_80611d54 != 0) {
        if ((*(int *)(DAT_80611d54 + 0x218) != 0x1b) && (*(int *)(DAT_80611d54 + 0x218) != 0x1e)) {
            if (*(float *)(DAT_80611d54 + 0x388) != *(float *)(DAT_80611d54 + 900)) {
                fVar1 = *(float *)(DAT_80611d54 + 0x388) - *(float *)(DAT_80611d54 + 900);
                fVar2 = (FLOAT_80618f30 /
                         (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) *
                        FLOAT_806108ec;
                if (fVar2 < fVar1) { fVar1 = fVar2; }
                if (fVar1 < -fVar2) { fVar1 = -fVar2; }
                *(float *)(DAT_80611d54 + 900) = *(float *)(DAT_80611d54 + 900) + fVar1;
            }
        }
        *(uint *)(DAT_80611d54 + 0x21c) = *(uint *)(DAT_80611d54 + 0x21c) & 0xffffff7f;
        if ((*(uint *)(DAT_80611d74 + 0xc) & 0x40) != 0) {
            *(uint *)(DAT_80611d54 + 0x21c) = *(uint *)(DAT_80611d54 + 0x21c) | 0x80;
        }
        player_update_fov_zoom_lerp();
    }
    FUN_8038273c();
}

// ============================================================================
// 0x80258b98  player_update_lean
// Integrates the weapon-lean / view-sway state while the weapon is drawn (flag
// 0x80, non-vehicle mode 0x4000). Advances the two sway phase accumulators
// (+0x138/+0x13c) with wrap, grows the sway amplitude (+0x134/+0x13d8) toward its
// limit and decays it, then evaluates the sway offset (+0x140/+0x144) from the
// phase via math_poly_trig unless render_mode_class_a() suppresses it. Called from
// player_main_tick.
// ============================================================================
void player_update_lean(void)
{
    float  fVar1, fVar2, fVar3;
    int    iVar4;
    double dVar5, dVar6;

    fVar2 = FLOAT_80618e80;
    fVar1 = FLOAT_80618e58;
    if (((*(uint *)(DAT_80611d74 + 0xc) & 0x80) != 0) && (*(int *)(DAT_80611d74 + 0x114) != 0x4000)) {
        *(float *)(DAT_80611d74 + 0x138) =
            FLOAT_80618ea0 * *(float *)(DAT_80611d74 + 0x13d8) * FLOAT_806108ec +
            *(float *)(DAT_80611d74 + 0x138);
        if (fVar2 <= *(float *)(DAT_80611d74 + 0x138)) { *(float *)(DAT_80611d74 + 0x138) = fVar1; }
        fVar2 = FLOAT_80618e80;
        *(float *)(DAT_80611d74 + 0x13c) =
            FLOAT_80618ea0 * *(float *)(DAT_80611d74 + 0x13dc) * FLOAT_806108ec +
            *(float *)(DAT_80611d74 + 0x13c);
        if (fVar2 < *(float *)(DAT_80611d74 + 0x13c)) { *(float *)(DAT_80611d74 + 0x13c) = FLOAT_80618e58; }
        if ((FLOAT_80618f34 * FLOAT_806108ec) /
                (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90) <
            *(float *)(DAT_80611d74 + 0x16c)) {
            if (*(float *)(DAT_80611d74 + 0x134) < FLOAT_80618e64) {
                *(float *)(DAT_80611d74 + 0x134) =
                    FLOAT_80618f38 * FLOAT_806108ec + *(float *)(DAT_80611d74 + 0x134);
            }
            if (*(float *)(DAT_80611d74 + 0x13d8) < FLOAT_80618ec0) {
                *(float *)(DAT_80611d74 + 0x13d8) =
                    FLOAT_80618f38 * FLOAT_806108ec + *(float *)(DAT_80611d74 + 0x13d8);
            }
        }
        if (FLOAT_80618ea0 < *(float *)(DAT_80611d74 + 0x134)) {
            *(float *)(DAT_80611d74 + 0x134) = -(FLOAT_80618f3c * FLOAT_806108ec - *(float *)(DAT_80611d74 + 0x134));
        }
        if (FLOAT_80618e64 < *(float *)(DAT_80611d74 + 0x13d8)) {
            *(float *)(DAT_80611d74 + 0x13d8) = -(FLOAT_80618f3c * FLOAT_806108ec - *(float *)(DAT_80611d74 + 0x13d8));
        }
        *(float *)(DAT_80611d74 + 0x13dc) = FLOAT_80618f40 * *(float *)(DAT_80611d74 + 0x13d8);
        if ((*(uint *)(DAT_80611d74 + 0xc) & 0x40) == 0) { fVar1 = *(float *)(DAT_80611d74 + 0x134); }
        dVar6 = (double)fVar1;
        iVar4 = render_mode_class_a();
        if (iVar4 == 0) {
            dVar5 = math_poly_trig((double)((FLOAT_80618e74 * *(float *)(DAT_80611d74 + 0x138)) /
                                            FLOAT_80618e70 - FLOAT_80618f44));
            fVar3 = FLOAT_80618f44;
            fVar2 = FLOAT_80618e74;
            fVar1 = FLOAT_80618e70;
            *(float *)(DAT_80611d74 + 0x140) = (float)((double)(float)((double)FLOAT_80618ea0 * dVar6) * dVar5);
            dVar5 = math_poly_trig((double)((fVar2 * *(float *)(DAT_80611d74 + 0x13c)) / fVar1 - fVar3));
            *(float *)(DAT_80611d74 + 0x144) = (float)(dVar6 * dVar5);
        }
    }
}

// ============================================================================
// 0x80258dbc  player_update_sprint_flag
// Sets the "aim disabled while sprinting" flag at +0xdf4: cleared unless the
// sprint state bit (0xc & 0x20) is set and the control mode (+0x114) is not one of
// the aim-locked modes (mask 0x650). Called from player_main_tick.
// ============================================================================
void player_update_sprint_flag(void)
{
    *(int *)(DAT_80611d74 + 0xdf4) = 0;
    if ((*(uint *)(DAT_80611d74 + 0xc) & 0x20) == 0) { return; }
    if ((*(uint *)(DAT_80611d74 + 0x114) & 0x650) != 0) { return; }
    *(int *)(DAT_80611d74 + 0xdf4) = 1;
}

// ============================================================================
// 0x80258df0  player_build_aim_basis
// Builds the player's aim direction and orthonormal right/up basis from the
// combined look angles (yaw +0x100 + sway +0x140, pitch +0x104 + sway +0x144)
// unless a scripted camera override is active (camera +0x21c bit 0x10000). Writes
// forward (+0xc0..+200), right (+0xe8..+0xf0, normalized) via cross with the world
// up (DAT_803e0500..508), and up (+0xdc..+0xe4, normalized) via cross of forward x
// right. Called from player_main_tick.
// ============================================================================
void player_build_aim_basis(void)
{
    float  fVar1, fVar2, fVar3, fVar4;
    double dVar5, dVar6, dVar7, dVar8;

    fVar2 = DAT_803e0508;
    fVar3 = DAT_803e0504;
    fVar1 = DAT_803e0500;
    if ((*(int *)(DAT_80611d74 + 8) == 0) ||
        ((*(uint *)(*(int *)(DAT_80611d74 + 8) + 0x21c) & 0x10000) == 0)) {
        dVar5 = math_poly_trig((double)((FLOAT_80618e74 *
                                         (*(float *)(DAT_80611d74 + 0x100) + *(float *)(DAT_80611d74 + 0x140))) /
                                        FLOAT_80618e70));
        dVar6 = math_poly_trig((double)((FLOAT_80618e74 *
                                         (*(float *)(DAT_80611d74 + 0x100) + *(float *)(DAT_80611d74 + 0x140))) /
                                        FLOAT_80618e70 - FLOAT_80618f44));
        dVar7 = math_poly_trig((double)((FLOAT_80618e74 *
                                         (*(float *)(DAT_80611d74 + 0x104) + *(float *)(DAT_80611d74 + 0x144))) /
                                        FLOAT_80618e70));
        dVar8 = math_poly_trig((double)((FLOAT_80618e74 *
                                         (*(float *)(DAT_80611d74 + 0x104) + *(float *)(DAT_80611d74 + 0x144))) /
                                        FLOAT_80618e70 - FLOAT_80618f44));
        *(float *)(DAT_80611d74 + 0xc0) = (float)(dVar7 * dVar6);
        fVar4 = FLOAT_80618f00;
        *(float *)(DAT_80611d74 + 0xc4) = (float)dVar8;
        *(float *)(DAT_80611d74 + 200)  = (float)(dVar7 * dVar5);
        *(float *)(DAT_80611d74 + 0xe8) =
            fVar3 * *(float *)(DAT_80611d74 + 200) - fVar2 * *(float *)(DAT_80611d74 + 0xc4);
        *(float *)(DAT_80611d74 + 0xec) =
            fVar2 * *(float *)(DAT_80611d74 + 0xc0) - fVar1 * *(float *)(DAT_80611d74 + 200);
        *(float *)(DAT_80611d74 + 0xf0) =
            fVar1 * *(float *)(DAT_80611d74 + 0xc4) - fVar3 * *(float *)(DAT_80611d74 + 0xc0);
        fVar1 = *(float *)(DAT_80611d74 + 0xe8);
        fVar3 = *(float *)(DAT_80611d74 + 0xf0) * *(float *)(DAT_80611d74 + 0xf0) +
                fVar1 * fVar1 + *(float *)(DAT_80611d74 + 0xec) * *(float *)(DAT_80611d74 + 0xec);
        if (fVar4 < fVar3) {
            fVar2 = 1.0f / SQRT_F(fVar3);
            fVar2 = fVar2 * -(FLOAT_80618ea0 * fVar3 * fVar2 * fVar2 - FLOAT_80618e5c);
            fVar3 = FLOAT_80618e64 * fVar2 * -(FLOAT_80618ea0 * fVar3 * fVar2 * fVar2 - FLOAT_80618e5c);
            *(float *)(DAT_80611d74 + 0xe8) = fVar1 * fVar3;
            *(float *)(DAT_80611d74 + 0xec) = *(float *)(DAT_80611d74 + 0xec) * fVar3;
            *(float *)(DAT_80611d74 + 0xf0) = *(float *)(DAT_80611d74 + 0xf0) * fVar3;
        }
        fVar2 = FLOAT_80618f00;
        *(float *)(DAT_80611d74 + 0xdc) =
            *(float *)(DAT_80611d74 + 0xc4) * *(float *)(DAT_80611d74 + 0xf0) -
            *(float *)(DAT_80611d74 + 200) * *(float *)(DAT_80611d74 + 0xec);
        *(float *)(DAT_80611d74 + 0xe0) =
            *(float *)(DAT_80611d74 + 200) * *(float *)(DAT_80611d74 + 0xe8) -
            *(float *)(DAT_80611d74 + 0xc0) * *(float *)(DAT_80611d74 + 0xf0);
        *(float *)(DAT_80611d74 + 0xe4) =
            *(float *)(DAT_80611d74 + 0xc0) * *(float *)(DAT_80611d74 + 0xec) -
            *(float *)(DAT_80611d74 + 0xc4) * *(float *)(DAT_80611d74 + 0xe8);
        fVar1 = *(float *)(DAT_80611d74 + 0xdc);
        fVar3 = *(float *)(DAT_80611d74 + 0xe4) * *(float *)(DAT_80611d74 + 0xe4) +
                fVar1 * fVar1 + *(float *)(DAT_80611d74 + 0xe0) * *(float *)(DAT_80611d74 + 0xe0);
        if (fVar2 < fVar3) {
            fVar2 = 1.0f / SQRT_F(fVar3);
            fVar2 = fVar2 * -(FLOAT_80618ea0 * fVar3 * fVar2 * fVar2 - FLOAT_80618e5c);
            fVar3 = FLOAT_80618e64 * fVar2 * -(FLOAT_80618ea0 * fVar3 * fVar2 * fVar2 - FLOAT_80618e5c);
            *(float *)(DAT_80611d74 + 0xdc) = fVar1 * fVar3;
            *(float *)(DAT_80611d74 + 0xe0) = *(float *)(DAT_80611d74 + 0xe0) * fVar3;
            *(float *)(DAT_80611d74 + 0xe4) = *(float *)(DAT_80611d74 + 0xe4) * fVar3;
        }
    }
}

// ============================================================================
// 0x80259124  player_update_anim_rate
// Sets the player rig's animation playback rate. When the rig is in a reload/
// action sub-state (fields +0x2bc/+0x18c set, and not clip 0x36), it selects rate
// mode 1 or 2 from the per-model anim-track flag table (DAT_804d34dc via track
// index DAT_804d1cd0); otherwise it forces mode 1. Called from player_main_tick.
// ============================================================================
void player_update_anim_rate(void)
{
    int  iVar1, iVar3;
    int *piVar2;

    iVar1 = *(int *)(DAT_80611d74 + 0x14);
    iVar3 = *(int *)(iVar1 + 0x7c);
    if (((*(int *)(iVar3 + 700) != 0) || (*(int *)(iVar3 + 0x18c) != 0)) &&
        (*(int *)(iVar3 + 0x94) != 0x36)) {
        piVar2 = *(int **)(iVar1 + 4);
        if ((*(uint *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[piVar2[0x33]] * 8) & 0x100) != 0) {
            anim_set_playback_rate((double)FLOAT_80618f2c, piVar2, 2);
            return;
        }
        anim_set_playback_rate((double)FLOAT_80618f2c, piVar2, 1);
        return;
    }
    anim_set_playback_rate((double)FLOAT_80618f2c, *(int **)(iVar1 + 4), 1);
}

// ============================================================================
// 0x802591f0  player_assign_team_by_mode
// Assigns the player object to a team based on the co-op / game mode
// (DAT_805015bd). Most co-op modes call player_ai_assign_team; mode 5 also sets a
// zero-damage melee render filter first; mode 10 forces team id 1. Called from
// player_character_system_init / player_respawn.
// ============================================================================
void player_assign_team_by_mode(int player)
{
    int iVar1;
    iVar1 = *(int *)(*(int *)(player + 0x14) + 0x7c);
    switch (DAT_805015bd) {
    case 1: case 2: case 3: case 6: case 7: case 9:
        player_ai_assign_team(iVar1);
        break;
    case 5:
        entity_set_render_type(*(int *)(player + 0x14), (void *)&damage_filter_melee_zero);
        player_ai_assign_team(iVar1);
        break;
    case 10:
        player_ai_set_team_id(iVar1, 1);
    }
}

// ============================================================================
// 0x80259278  player_update_fov_zoom_lerp
// Eases the zoom FOV factor at +0xe50 while zoomed (flag 0x40): when the camera
// FOV endpoints match it decays toward zero, otherwise it grows proportional to
// the endpoint delta, clamped to [0,1]. Cleared to zero when not zoomed. Called
// from player_update_zoom_toggle.
// ============================================================================
void player_update_fov_zoom_lerp(void)
{
    float fVar1, fVar2, fVar3;

    fVar3 = FLOAT_80618e64;
    fVar2 = FLOAT_80618e58;
    if ((*(uint *)(DAT_80611d74 + 0xc) & 0x40) == 0) {
        *(float *)(DAT_80611d74 + 0xe50) = FLOAT_80618e58;
    } else {
        fVar1 = *(float *)(DAT_80611d54 + 900) - *(float *)(DAT_80611d54 + 900);
        if (fVar1 < FLOAT_80618e58) { fVar1 = -fVar1; }
        if (FLOAT_80618e58 == fVar1) {
            *(float *)(DAT_80611d74 + 0xe50) =
                -(FLOAT_80618f48 * FLOAT_806108ec *
                  (FLOAT_80618e64 /
                   (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) -
                  *(float *)(DAT_80611d74 + 0xe50));
            if (*(float *)(DAT_80611d74 + 0xe50) < fVar2) { *(float *)(DAT_80611d74 + 0xe50) = fVar2; }
        } else {
            *(float *)(DAT_80611d74 + 0xe50) =
                *(float *)(DAT_80611d74 + 0xe50) + (FLOAT_80618f4c * fVar1) / FLOAT_80618ef0;
            if (fVar3 < *(float *)(DAT_80611d74 + 0xe50)) { *(float *)(DAT_80611d74 + 0xe50) = fVar3; }
        }
    }
}

// ============================================================================
// 0x80259354  player_setup_viewport
// Configures this player's viewport rectangle + depth range for the current
// split-screen layout. session_get_state_flag() selects the layout: state 1
// full-screen (or left-half for the special level DAT_805015d8==0x13 when flag
// 0x40 set); state 2 top/bottom halves; state 3 the 3/4-player quadrant layout
// (indexed by the packed player-index bits in the __save_gpr_28 pair); state 4
// the generic quadrant grid. Also stores the per-viewport FOV (+900). Called from
// camera_splitscreen_restore / game_phase_state_machine / match reset / init.
// ============================================================================
void player_setup_viewport(void)
{
    uint      uVar1, uVar2;
    int       iVar3, iVar4, iVar6, iVar7;
    int      *piVar5;
    ulonglong uVar8;

    uVar8  = (ulonglong)(uint)__save_gpr_28();
    iVar3  = (int)(uVar8 >> 0x20);
    piVar5 = (int *)(uint)uVar8;
    iVar7  = (int)DAT_806109c4 >> 1;
    iVar6  = (int)DAT_806109c8 >> 1;
    viewport_set_depth_params(
        (double)(FLOAT_80618f50 + FLOAT_8060fad0), -(double)(FLOAT_80618f50 + FLOAT_8060fad0),
        (double)FLOAT_80618f54, (int)piVar5);
    iVar4 = session_get_state_flag();
    if (iVar4 == 3) {
        if (iVar3 == 1) {
            viewport_set_rect(piVar5, 0, iVar6, iVar7 + -1, DAT_806109c8 - 1);
            *(float *)(*(int *)(DAT_80611d78 + 0x1424) + 900) = FLOAT_80618f5c;
        } else if ((long long)uVar8 < 0x100000000LL) {
            if (-1 < (long long)uVar8) {
                viewport_set_rect(piVar5, 0, 0, DAT_806109c4 - 1, iVar6 + -1);
                *(float *)(*(int *)(DAT_80611d78 + iVar3 * 0x141c + 8) + 900) = FLOAT_80618f58;
            }
        } else if ((long long)uVar8 < 0x300000000LL) {
            viewport_set_rect(piVar5, iVar7, iVar6, DAT_806109c4 - 1, DAT_806109c8 - 1);
            *(float *)(*(int *)(DAT_80611d78 + iVar3 * 0x141c + 8) + 900) = FLOAT_80618f5c;
        }
    } else if (iVar4 < 3) {
        if (iVar4 == 1) {
            if (((DAT_805015c0 & 0x40) == 0) || (DAT_805015d8 != 0x13)) {
                viewport_set_rect(piVar5, 0, 0, DAT_806109c4 - 1, DAT_806109c8 - 1);
            } else {
                viewport_set_rect(piVar5, 0, 0, iVar7 + -1, DAT_806109c8 - 1);
            }
        } else if (0 < iVar4) {
            if (iVar3 == 1) {
                viewport_set_rect(piVar5, 0, iVar6, DAT_806109c4 - 1, DAT_806109c8 - 1);
            } else if (((long long)uVar8 < 0x100000000LL) && (-1 < (long long)uVar8)) {
                viewport_set_rect(piVar5, 0, 0, DAT_806109c4 - 1, iVar6 + -1);
            }
        }
    } else if (iVar4 < 5) {
        uVar1 = (int)DAT_806109c8 >> 1;
        if ((uVar8 & 0x200000000ULL) != 0) { uVar1 = DAT_806109c8; }
        uVar2 = (int)DAT_806109c4 >> 1;
        if ((uVar8 & 0x100000000ULL) != 0) { uVar2 = DAT_806109c4; }
        viewport_set_rect(piVar5, (int)DAT_806109c4 >> 1 & -(uint)((uVar8 & 0x100000000ULL) != 0),
                          (int)DAT_806109c8 >> 1 & -(uint)((uVar8 & 0x200000000ULL) != 0), uVar2 - 1,
                          uVar1 - 1);
    }
    __restore_gpr_28();
}

// ============================================================================
// 0x802595d0  player_death_mode_effects
// On-death, mode-specific effects (from player_on_death_dispatch). For co-op
// mode 0x12 runs the shared co-op death effect (FUN_80280594); for mode 0x0c a
// self-kill triggers the character damage flash; for mode 0x0b a self-kill sets
// the player 0x1000 status flag at +0xc. The self-kill test is killer==victim
// (ctx[1] == ctx[0]).
// ============================================================================
void player_death_mode_effects(void)
{
    int *piVar1;
    piVar1 = (int *)FUN_803826f0();
    if (DAT_805015be == '\x12') {
        FUN_80280594(piVar1);
    }
    if ((DAT_805015be == '\f') && (piVar1[1] == *piVar1)) {
        character_damage_effect_wrapper();
    }
    if ((DAT_805015be == '\v') && (piVar1[1] == *piVar1)) {
        *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) | 0x1000;
    }
    FUN_8038273c();
}

// ============================================================================
// 0x8025967c  player_update_autoaim
// Per-frame aim-assist / autoaim + crosshair world-position update (from
// player_main_tick). When the weapon is holstered (flag 0x80 clear) it just runs
// the auto-aim event scan and writes the raw aim vector. When drawn it: updates
// the 3D sound source, resolves a target via player_scan_teammate_aim_block +
// scene_raycast (validated with player_is_valid_target / entity_is_in_active_list),
// falls back to the auto-aim event scan or free-aim stick, low-pass filters the
// aim rates at +0xdfc..+0xe08, and rebuilds the crosshair world ray (rig +0xb7c
// origin, +0xb88 far point) using the camera basis. Marks nearby target objects
// with the aim-highlight bits (0x60 at obj+0x28).
// ============================================================================
void player_update_autoaim(void)
{
    float *pfVar1;
    float  fVar2, fVar3, fVar4, fVar5, fVar6, fVar7, fVar8, fVar9, fVar10, fVar11, fVar12;
    bool   bVar13;
    float  fVar14, fVar15, fVar16;
    float *pfVar17;
    int    iVar18, iVar19, iVar20, iVar22, iVar23;
    undefined4 uVar21;
    double dVar24, dVar25, dVar26, dVar27;
    undefined4 local_c0, local_bc, local_b8, local_b4, local_b0, local_ac;
    undefined4 local_a8, local_a4, local_a0, local_9c, local_98, local_94;
    float  local_90, local_8c, local_88;

    pfVar17 = (float *)__save_gpr_25();
    dVar25 = (double)FLOAT_80618e58;
    iVar23 = *(int *)(DAT_80611d74 + 0x14);
    iVar22 = *(int *)(iVar23 + 0x7c);
    if (((*(uint *)(DAT_80611d74 + 0xc) & 0x80) == 0) || (*(int *)(DAT_80611d74 + 4) < 0)) {
        *(int *)(DAT_80611d74 + 0xe1c) = 0;
        iVar23 = player_autoaim_scan_events();
        if (iVar23 == 0) {
            *(int *)(DAT_80611d74 + 0xe10) = 0;
            *(int *)(iVar22 + 0xb7c) = *(int *)(DAT_80611d74 + 0xc0);
            *(int *)(iVar22 + 0xb80) = *(int *)(DAT_80611d74 + 0xc4);
            *(int *)(iVar22 + 0xb84) = *(int *)(DAT_80611d74 + 200);
        } else {
            *(undefined4 *)(DAT_80611d74 + 0xe20) = local_b4;
            *(undefined4 *)(DAT_80611d74 + 0xe24) = local_b0;
            *(undefined4 *)(DAT_80611d74 + 0xe28) = local_ac;
            *(int *)(DAT_80611d74 + 0xe10) = iVar23;
            *(undefined4 *)(iVar22 + 0xb7c) = local_c0;
            *(undefined4 *)(iVar22 + 0xb80) = local_bc;
            *(undefined4 *)(iVar22 + 0xb84) = local_b8;
        }
        fVar2 = FLOAT_80618eb8;
        if (*(int *)(DAT_80611d74 + 0xdf4) != 0) {
            *(int *)(DAT_80611d74 + 0xe10) = 0;
            fVar12 = FLOAT_80618f00;
            fVar10 = FLOAT_80618e64;
            fVar3 = pfVar17[1];
            fVar7 = *(float *)(DAT_80611d74 + 0xc4);
            fVar4 = *(float *)(DAT_80611d74 + 200);
            fVar6 = FLOAT_80618e58 * fVar7;
            pfVar1 = (float *)(DAT_80611d74 + 0xe00);
            fVar5 = *(float *)(DAT_80611d74 + 0xc0);
            fVar8 = FLOAT_80618e58 * fVar5 - FLOAT_80618e58 * fVar4;
            *(float *)(DAT_80611d74 + 0xdfc) =
                fVar2 * (-*pfVar17 - *(float *)(DAT_80611d74 + 0xdfc)) * FLOAT_806108ec +
                *(float *)(DAT_80611d74 + 0xdfc);
            fVar9 = fVar10 * fVar4 - fVar6;
            fVar6 = -(fVar10 * fVar5 - fVar6);
            fVar11 = fVar6 * fVar6 + fVar9 * fVar9 + fVar8 * fVar8;
            *(float *)(DAT_80611d74 + 0xe00) =
                fVar2 * (-fVar3 - *pfVar1) * FLOAT_806108ec + *(float *)(DAT_80611d74 + 0xe00);
            *(int *)(DAT_80611d74 + 0xe04) = *(int *)(DAT_80611d74 + 0xdfc);
            *(int *)(DAT_80611d74 + 0xe08) = *(int *)(DAT_80611d74 + 0xe00);
            fVar16 = FLOAT_80618f78;
            fVar15 = FLOAT_80618f74;
            fVar14 = FLOAT_80618f6c;
            fVar3 = FLOAT_80618e64;
            fVar2 = FLOAT_80618e58;
            if (fVar12 < fVar11) {
                fVar12 = 1.0f / SQRT_F(fVar11);
                fVar12 = fVar12 * -(FLOAT_80618ea0 * fVar11 * fVar12 * fVar12 - FLOAT_80618e5c);
                fVar10 = fVar10 * fVar12 * -(FLOAT_80618ea0 * fVar11 * fVar12 * fVar12 - FLOAT_80618e5c);
                fVar9 = fVar9 * fVar10;
                fVar8 = fVar8 * fVar10;
                fVar6 = fVar6 * fVar10;
            }
            *(float *)(iVar22 + 0xb7c) =
                FLOAT_80618f6c * FLOAT_80618f78 * *(float *)(DAT_80611d74 + 0xe08) * FLOAT_80618e58 +
                FLOAT_80618f6c * FLOAT_80618f74 * *(float *)(DAT_80611d74 + 0xe04) * fVar9 + fVar5;
            *(float *)(iVar22 + 0xb80) =
                fVar14 * fVar16 * *(float *)(DAT_80611d74 + 0xe08) * fVar3 +
                fVar14 * fVar15 * *(float *)(DAT_80611d74 + 0xe04) * fVar8 + fVar7;
            *(float *)(iVar22 + 0xb84) =
                fVar14 * fVar16 * *(float *)(DAT_80611d74 + 0xe08) * fVar2 +
                fVar14 * fVar15 * *(float *)(DAT_80611d74 + 0xe04) * fVar6 + fVar4;
        }
        fVar3 = FLOAT_80618f70;
        fVar2 = FLOAT_80618e98;
        *(float *)(iVar22 + 0xb88) = FLOAT_80618f70 * *(float *)(iVar22 + 0xb7c) + *(float *)(iVar22 + 0xbb4);
        *(float *)(iVar22 + 0xb8c) = fVar3 * *(float *)(iVar22 + 0xb80) + fVar2 + *(float *)(iVar22 + 0xbc0);
        *(float *)(iVar22 + 0xb90) = fVar3 * *(float *)(iVar22 + 0xb84) + *(float *)(iVar22 + 0xbbc);
    } else {
        dVar27 = dVar25;
        dVar24 = dVar25;
        dVar26 = dVar25;
        sound_source_update_tick();
        if ((*(int *)(*(int *)(DAT_80611d74 + 0x1398) + 0x74) == 2) ||
            ((*(int *)(DAT_80611d74 + 0xdf4) == 0 && (DAT_80611770 != 7)))) {
            *(int *)(DAT_80611d74 + 0xe1c) = 0;
            *(int *)(DAT_80611d74 + 0xcc) = *(int *)(DAT_80611d74 + 0xc0);
            *(int *)(DAT_80611d74 + 0xd0) = *(int *)(DAT_80611d74 + 0xc4);
            *(int *)(DAT_80611d74 + 0xd4) = *(int *)(DAT_80611d74 + 200);
            if (DAT_80611d54 != 0) {
                bVar13 = true;
                iVar20 = 0;
                uVar21 = *(undefined4 *)(*(int *)(DAT_80611d74 + 0x14) + 0x28);
                iVar18 = 1;
                if (*(int *)(DAT_80611d74 + 0xdec) != 0) { iVar18 = *(int *)(DAT_80611d74 + 0xdec); }
                local_9c = *(undefined4 *)(DAT_80611d74 + 0xb4);
                local_98 = *(undefined4 *)(DAT_80611d74 + 0xb8);
                local_94 = *(undefined4 *)(DAT_80611d74 + 0xbc);
                iVar19 = player_is_special_arcade_mode(DAT_80611d74);
                if ((iVar19 != 0) && (*(int *)(DAT_80611d74 + 0xe1c) == 0)) {
                    iVar20 = player_scan_teammate_aim_block(DAT_80611d74, (int)&local_9c, iVar22 + 0xb7c);
                }
                if (iVar20 == 0) {
                    *(uint *)(*(int *)(DAT_80611d74 + 0x14) + 0x28) =
                        *(uint *)(*(int *)(DAT_80611d74 + 0x14) + 0x28) & 0xffffffdf;
                    iVar20 = scene_raycast(iVar18, (int)&local_9c, (float *)(iVar22 + 0xb7c), 1,
                                           (float *)(DAT_80611d74 + 0xe20), (float *)(DAT_80611d74 + 0xe2c),
                                           (int *)0, 0x2010070, 0, (int *)0);
                    *(undefined4 *)(*(int *)(DAT_80611d74 + 0x14) + 0x28) = uVar21;
                }
                if ((iVar20 != 0) &&
                    (((*(int *)(iVar20 + 0x20) == 8 ||
                       ((*(int *)(iVar20 + 0x20) != 2 &&
                         (iVar18 = entity_is_in_active_list(iVar20), iVar18 != 0)))) &&
                      (iVar18 = player_is_valid_target(DAT_80611d74, iVar20), iVar18 != 0)))) {
                    bVar13 = false;
                    *(int *)(DAT_80611d74 + 0xe14) = iVar20;
                    *(int *)(DAT_80611d74 + 0xe1c) = iVar20;
                }
                if (bVar13) {
                    iVar18 = player_autoaim_scan_events();
                    if ((iVar18 == 0) || (*(int *)(DAT_80611d74 + 0x114) == 0x80)) {
                        bVar13 = false;
                    } else {
                        *(int *)(DAT_80611d74 + 0xe10) = iVar18;
                        mtx4_transform_vector_alias(*(float **)(DAT_80611d54 + 0x208), &local_90);
                        dVar27 = (double)((-(local_90 / local_88) / *(float *)(DAT_80611d54 + 0x380)) / FLOAT_80618f60);
                        dVar25 = (double)(((local_8c / local_88) / *(float *)(DAT_80611d54 + 0x37c)) / FLOAT_80618f60);
                        dVar24 = dVar25;
                        dVar26 = dVar27;
                    }
                }
                fVar2 = FLOAT_80618e60;
                if (!bVar13) {
                    *(int *)(DAT_80611d74 + 0xe10) = 0;
                    dVar26 = (double)(fVar2 * -*pfVar17);
                    fVar3 = FLOAT_80618f64;
                    if ((*(ushort *)(*(int *)(DAT_80611d74 + 0x1398) + 2) & 1) != 0) { fVar3 = fVar2; }
                    dVar24 = (double)(pfVar17[1] * fVar3);
                    dVar25 = dVar24;
                    dVar27 = dVar26;
                    if (*(int *)(*(int *)(DAT_80611d74 + 0x1398) + 0x74) == 2) {
                        dVar27 = (double)(float)(dVar26 * (double)FLOAT_80618ea0);
                        dVar25 = (double)(float)(dVar24 * (double)FLOAT_80618ea0);
                    }
                }
            }
        } else {
            dVar25 = (double)FLOAT_80618e64;
            *(float *)(DAT_80611d74 + 0xd8) = FLOAT_80618e64;
            iVar18 = session_get_state_flag();
            fVar2 = FLOAT_80618e64;
            if (iVar18 == 2) { dVar25 = (double)FLOAT_80618ea0; }
            fVar3 = pfVar17[1];
            fVar7 = -*pfVar17 * FLOAT_80618e64;
            *(int *)(DAT_80611d74 + 0xe1c) = 0;
            dVar24 = (double)(-fVar3 * fVar2);
            dVar26 = (double)(float)(dVar25 * (double)fVar7);
            dVar25 = dVar24;
            dVar27 = dVar26;
        }
        fVar2 = (float)(dVar26 - (double)*(float *)(DAT_80611d74 + 0xe04));
        fVar3 = (float)(dVar24 - (double)*(float *)(DAT_80611d74 + 0xe08));
        if (FLOAT_80618f68 <= ABS_F(fVar2)) {
            *(float *)(DAT_80611d74 + 0xe04) =
                (float)((double)(FLOAT_80618eb8 * fVar2) * (double)FLOAT_806108ec + (double)*(float *)(DAT_80611d74 + 0xe04));
        } else {
            *(float *)(DAT_80611d74 + 0xe04) = (float)dVar26;
        }
        if (FLOAT_80618f68 <= ABS_F(fVar3)) {
            *(float *)(DAT_80611d74 + 0xe08) =
                FLOAT_80618eb8 * fVar3 * FLOAT_806108ec + *(float *)(DAT_80611d74 + 0xe08);
        } else {
            *(float *)(DAT_80611d74 + 0xe08) = (float)dVar24;
        }
        fVar2 = (float)(dVar27 - (double)*(float *)(DAT_80611d74 + 0xdfc));
        fVar3 = (float)(dVar25 - (double)*(float *)(DAT_80611d74 + 0xe00));
        if (FLOAT_80618f68 <= ABS_F(fVar2)) {
            *(float *)(DAT_80611d74 + 0xdfc) =
                (float)((double)(FLOAT_80618eb8 * fVar2) * (double)FLOAT_806108ec + (double)*(float *)(DAT_80611d74 + 0xdfc));
        } else {
            *(float *)(DAT_80611d74 + 0xdfc) = (float)dVar27;
        }
        if (FLOAT_80618f68 <= ABS_F(fVar3)) {
            *(float *)(DAT_80611d74 + 0xe00) =
                FLOAT_80618eb8 * fVar3 * FLOAT_806108ec + *(float *)(DAT_80611d74 + 0xe00);
        } else {
            *(float *)(DAT_80611d74 + 0xe00) = (float)dVar25;
        }
        fVar7 = FLOAT_80618f70;
        fVar3 = FLOAT_80618f6c;
        fVar2 = FLOAT_80618e98;
        if (*(int *)(DAT_80611d74 + 0x114) == 0x80) {
            fVar4 = *(float *)(DAT_80611d54 + 0x334);
            fVar5 = *(float *)(DAT_80611d54 + 0x338);
            fVar6 = *(float *)(DAT_80611d54 + 0x33c);
        } else {
            fVar4 = *(float *)(DAT_80611d74 + 0xc0);
            fVar5 = *(float *)(DAT_80611d74 + 0xc4);
            fVar6 = *(float *)(DAT_80611d74 + 200);
        }
        *(float *)(iVar22 + 0xb7c) =
            *(float *)(DAT_80611d54 + 0x364) * FLOAT_80618f6c * *(float *)(DAT_80611d74 + 0xe08) * *(float *)(DAT_80611d54 + 0x37c) +
            *(float *)(DAT_80611d54 + 0x370) * FLOAT_80618f6c * *(float *)(DAT_80611d74 + 0xe04) * *(float *)(DAT_80611d54 + 0x380) + fVar4;
        *(float *)(iVar22 + 0xb80) =
            *(float *)(DAT_80611d54 + 0x368) * fVar3 * *(float *)(DAT_80611d74 + 0xe08) * *(float *)(DAT_80611d54 + 0x37c) +
            *(float *)(DAT_80611d54 + 0x374) * fVar3 * *(float *)(DAT_80611d74 + 0xe04) * *(float *)(DAT_80611d54 + 0x380) + fVar5;
        *(float *)(iVar22 + 0xb84) =
            *(float *)(DAT_80611d54 + 0x36c) * fVar3 * *(float *)(DAT_80611d74 + 0xe08) * *(float *)(DAT_80611d54 + 0x37c) +
            *(float *)(DAT_80611d54 + 0x378) * fVar3 * *(float *)(DAT_80611d74 + 0xe04) * *(float *)(DAT_80611d54 + 0x380) + fVar6;
        *(float *)(iVar22 + 0xb88) = fVar7 * *(float *)(iVar22 + 0xb7c) + *(float *)(iVar22 + 0xbb4);
        *(float *)(iVar22 + 0xb8c) = fVar7 * *(float *)(iVar22 + 0xb80) + fVar2 + *(float *)(iVar22 + 3000);
        *(float *)(iVar22 + 0xb90) = fVar7 * *(float *)(iVar22 + 0xb84) + *(float *)(iVar22 + 0xbbc);
        if ((*(int *)(DAT_80611d74 + 0xe1c) == 0) || (*(int *)(iVar22 + 0xc14) != 0)) {
            *(uint *)(iVar23 + 0x28) = *(uint *)(iVar23 + 0x28) & 0xffffff9f;
            iVar18 = *(int *)(iVar22 + 0x18c);
            if (iVar18 != 0) { *(uint *)(iVar18 + 0x28) = *(uint *)(iVar18 + 0x28) & 0xffffff9f; }
            iVar18 = *(int *)(iVar22 + 700);
            if (iVar18 != 0) { *(uint *)(iVar18 + 0x28) = *(uint *)(iVar18 + 0x28) & 0xffffff9f; }
            if (*(int *)(iVar22 + 0xc14) == 0) {
                iVar18 = 1;
                if (*(int *)(DAT_80611d74 + 0xdec) != 0) { iVar18 = *(int *)(DAT_80611d74 + 0xdec); }
                local_a8 = *(undefined4 *)(DAT_80611d74 + 0xb4);
                local_a4 = *(undefined4 *)(DAT_80611d74 + 0xb8);
                local_a0 = *(undefined4 *)(DAT_80611d74 + 0xbc);
                iVar20 = player_is_special_arcade_mode(DAT_80611d74);
                if ((iVar20 != 0) && (*(int *)(DAT_80611d74 + 0xe1c) == 0)) {
                    *(int *)(DAT_80611d74 + 0xe1c) =
                        player_scan_teammate_aim_block(DAT_80611d74, (int)&local_a8, iVar22 + 0xb7c);
                }
                iVar20 = player_is_special_arcade_mode(DAT_80611d74);
                if ((iVar20 == 0) || (*(int *)(DAT_80611d74 + 0xe1c) == 0)) {
                    *(int *)(DAT_80611d74 + 0xe1c) =
                        scene_raycast(iVar18, (int)&local_a8, (float *)(iVar22 + 0xb7c), 1,
                                      (float *)(DAT_80611d74 + 0xe20), (float *)(DAT_80611d74 + 0xe2c),
                                      (int *)0, 0x2010070, 0, (int *)0);
                }
            }
            *(uint *)(iVar23 + 0x28) = *(uint *)(iVar23 + 0x28) | 0x60;
            iVar23 = *(int *)(iVar22 + 0x18c);
            if (iVar23 != 0) { *(uint *)(iVar23 + 0x28) = *(uint *)(iVar23 + 0x28) | 0x60; }
            iVar23 = *(int *)(iVar22 + 700);
            if (iVar23 != 0) { *(uint *)(iVar23 + 0x28) = *(uint *)(iVar23 + 0x28) | 0x60; }
            iVar22 = *(int *)(iVar22 + 0xc14);
            if (iVar22 != 0) { *(uint *)(iVar22 + 0x28) = *(uint *)(iVar22 + 0x28) | 0x60; }
        }
        fVar2 = FLOAT_80618e58;
        if (*(int *)(*(int *)(DAT_80611d74 + 0x1398) + 0x74) == 2) {
            *(float *)(DAT_80611d74 + 0xe04) = FLOAT_80618e58;
            *(float *)(DAT_80611d74 + 0xe08) = fVar2;
        }
    }
    __restore_gpr_25();
}

// ============================================================================
// 0x8025a004  player_tick_ledge_state
// Gate: if the player is in the ledge-hang control mode (+0x114 == 0x200) runs
// the ledge-grab tick and returns true; otherwise returns false. Called from the
// player state machine.
// ============================================================================
bool player_tick_ledge_state(void)
{
    bool bVar1;
    bVar1 = *(int *)(DAT_80611d74 + 0x114) == 0x200;
    if (bVar1) { player_ledge_grab_tick(); }
    return bVar1;
}

// ============================================================================
// 0x8025a04c  player_play_hit_reaction
// Plays a hit-reaction / flinch animation for a damaged player unless the source
// object is in an immune control mode (0x40 turret, 0x200 ledge, 0x400 mounted,
// 0x2000 vehicle) or already flagged (subobj +0xc6c bit 4). The rng roll selects
// between two flinch variants. Called from damage_event_main_handler.
// ============================================================================
void player_play_hit_reaction(int *param_1)
{
    int        iVar1;
    undefined4 uVar2;
    if ((*(uint *)(*(int *)(*param_1 + 0x7c) + 0xc6c) & 4) == 0) {
        iVar1 = *(int *)(*(int *)(*param_1 + 0x150) + 0x114);
        if (iVar1 != 0x200) {
            if (iVar1 < 0x200) {
                if (iVar1 == 0x40) { return; }
            } else {
                if (iVar1 == 0x2000) { return; }
                if ((iVar1 < 0x2000) && (iVar1 == 0x400)) { return; }
            }
            uVar2 = rng_counter_tick();
            if ((float)((double)CONCAT44(0x43300000, uVar2) - DOUBLE_80618f80) / FLOAT_80618f7c <= FLOAT_80618ea0) {
                anim_play_scaled((double)FLOAT_80618e58, (double)FLOAT_80618e64);
            } else {
                anim_play_scaled((double)FLOAT_80618e58, (double)FLOAT_80618e64);
            }
        }
    }
}

// ============================================================================
// 0x8025a120  player_subobj_melee_tick
// Per-frame melee/grab handling for a held sub-object (from
// subobj_constraint_resolve_events). While the sub-object is active and in a
// melee-capable mode (flags 0x2800), it: on a forward swing (param_3 < 0, |turn|
// large) nudges the player position via nav_movement_dispatch; and on a completed
// swing (param_2 > 0, not mid-clip) issues a type-0x24 melee damage_dispatch with
// the impact vector, resolving the active grab target for the 0x400000 subtype.
// ============================================================================
void player_subobj_melee_tick(undefined8 param_1, double param_2, double param_3, double param_4,
                              undefined8 param_5, undefined8 param_6, undefined8 param_7, undefined8 param_8,
                              undefined4 param_9, undefined4 param_10, float *param_11, undefined4 param_12)
{
    bool   bVar1, bVar2;
    int    iVar3, iVar6, iVar7;
    uint   uVar4, uVar5;
    undefined4 *puVar8;
    double dVar9, dVar10, dVar11;
    undefined8 uVar12;
    float  local_e8, local_e4, local_e0;
    int    aiStack_dc[11];
    int    local_b0[6];
    undefined4 local_98;
    float  local_90;
    undefined4 local_8c, local_88;
    float *local_80;
    float  local_7c;
    undefined4 local_68;
    float  local_64, local_60;
    (void)param_1; (void)param_9; (void)param_10;

    uVar12 = FUN_803826dc();
    iVar3  = (int)((ulonglong)uVar12 >> 0x20);
    iVar7  = (int)uVar12;
    puVar8 = *(undefined4 **)(iVar3 + 0x150);
    dVar9  = param_3;
    uVar4  = subobj_get_flags(*(uint *)(iVar7 + 0x80));
    dVar11 = ABS_D(param_2);
    if ((((puVar8[0x45] & 0x10) == 0) && ((uVar4 & 0x2800) != 0)) &&
        (uVar5 = subobj_is_idle(*(uint *)(iVar7 + 0x80)), uVar5 == 0)) {
        bVar2 = true;
        bVar1 = false;
        iVar6 = anim_is_clip_active(iVar3, 0x92);
        if (((iVar6 != 0) || (iVar6 = anim_is_clip_active(iVar3, 0x95), iVar6 != 0)) ||
            ((iVar6 = anim_is_clip_active(iVar3, 0x93), iVar6 != 0 ||
              ((iVar6 = anim_is_clip_active(iVar3, 0x96), iVar6 != 0 ||
                (iVar6 = anim_is_clip_active(iVar3, 0x94), iVar6 != 0)))))) {
            bVar1 = true;
        }
        if ((!bVar1) && (iVar6 = anim_is_clip_active(iVar3, 0x97), iVar6 == 0)) { bVar2 = false; }
        uVar5 = subobj_is_idle(*(uint *)(iVar7 + 0x80));
        if ((((uVar5 == 0) && ((uVar4 & 0x2000) != 0)) && (dVar9 < (double)FLOAT_80618e58)) &&
            ((double)FLOAT_80618eb8 < dVar11)) {
            param_4 = -dVar9;
            param_3 = (double)(float)((double)param_11[1] * param_4);
            local_e8 = (float)((double)*param_11 * param_4) + (float)puVar8[0x2a];
            local_e4 = (float)(param_3 + (double)(float)puVar8[0x2b]);
            local_e0 = (float)((double)param_11[2] * param_4) + (float)puVar8[0x2c];
            uVar4 = *(uint *)(iVar7 + 0x28) & 0x60;
            *(uint *)(iVar7 + 0x28) = *(uint *)(iVar7 + 0x28) & ~uVar4;
            nav_movement_dispatch((double)((float)puVar8[0x25] + (float)puVar8[0x23]),
                                  (double)(float)puVar8[0x28], (int)*puVar8, (int *)(puVar8 + 0x2a),
                                  &local_e8, (float *)0, aiStack_dc, 1, 0);
            *(uint *)(iVar7 + 0x28) = *(uint *)(iVar7 + 0x28) | uVar4;
            *(undefined4 *)(iVar3 + 0xc4) = puVar8[0x40];
            puVar8[0x2a] = *(undefined4 *)&local_e8;
            puVar8[0x2b] = *(undefined4 *)&local_e4;
            puVar8[0x2c] = *(undefined4 *)&local_e0;
        }
        if ((!bVar2) && ((double)FLOAT_80618e58 < param_2)) {
            dVar9  = (double)FLOAT_80618eb4;
            dVar10 = (double)(float)(dVar9 * ABS_D(dVar11));
            if ((double)FLOAT_80618f2c < dVar10) {
                switch_slot_reset((int)local_b0);
                if (*(int *)(iVar7 + 0x20) == 0x400000) {
                    subobj_find_active_grab(iVar7, local_b0 + 1);
                } else {
                    local_b0[1] = 0;
                }
                local_68 = 0;
                local_8c = 0;
                local_7c = (float)dVar11;
                local_98 = 0x24;
                local_64 = FLOAT_80618f88;
                local_60 = FLOAT_80618f34;
                local_90 = (float)dVar10;
                local_b0[0] = iVar3;
                local_b0[2] = iVar7;
                local_88 = param_12;
                local_80 = param_11;
                damage_dispatch_to_entity((double)FLOAT_80618f88, dVar9, param_3, param_4,
                                          param_5, param_6, param_7, param_8, local_b0);
            }
        }
    }
    FUN_80382728();
}

// ============================================================================
// 0x8025a3dc  player_push_footstep_history
// Rolls the 3-deep floor/material id history (+0x13f4/+0x13f8/+0x13fc) when the
// current floor id under the player (rig +0x678) differs from the newest entry.
// Used to detect floor-material transitions for footstep sounds. Called from
// player_main_tick.
// ============================================================================
void player_push_footstep_history(void)
{
    int iVar1;
    iVar1 = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    if (*(int *)(iVar1 + 0x678) == *(int *)(DAT_80611d74 + 0x13f4)) { return; }
    *(int *)(DAT_80611d74 + 0x13fc) = *(int *)(DAT_80611d74 + 0x13f8);
    *(int *)(DAT_80611d74 + 0x13f8) = *(int *)(DAT_80611d74 + 0x13f4);
    *(int *)(DAT_80611d74 + 0x13f4) = *(int *)(iVar1 + 0x678);
}

} // extern "C"
