// cutscene_prop_scripts.cpp
// Level-specific in-game-cinematic (IGC) / cutscene prop scripts. These are the
// per-cutscene "prop behaviour" callbacks registered by the cutscene system
// (cutscene_system_startup / cutscene_render_dispatch_if_enabled). Each drives a
// scripted prop (collapsing track, platform, re-entry hull, explosion/projectile
// spawns) as a function of the current cutscene playback time
// (modelData.time * FLOAT_8060faf8 -> integer frame index).
//
// The level is selected by DAT_80612614 / DAT_8061294c (current cutscene id); the
// prop model handle is resolved from a named prop string via cutscene_get_model_data.
//
// Address range: 0x80253e74 - 0x802545d4  (batch_730)
//
// Self-contained translation unit: all raw ROM symbols and helper prototypes are
// declared file-locally with extern "C" linkage matching the ROM symbol names.

typedef unsigned char      undefined1;
typedef unsigned int       undefined4;
typedef unsigned long long undefined8;
typedef unsigned int       uint;
typedef long long          longlong;
typedef unsigned long long ulonglong;

static inline unsigned long long CONCAT44(unsigned int hi, unsigned int lo) {
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
}

extern "C" {
// ---- cutscene context enter/leave (packed slotFlag|entityCtx) ----
unsigned long long FUN_803826f0(void);
void FUN_8038273c(void);
double FUN_80390324(double x);              // smoothstep easing
void FUN_8038b510(int *dst, int *srcName);  // copy/build prop-name string
double math_sqrt(double x);

// ---- cutscene / prop model + scene helpers ----
int  cutscene_get_model_data(uint *propName);
void entity_preload_resources(undefined8 a1, undefined8 a2, undefined8 a3, undefined8 a4,
                              undefined8 a5, undefined8 a6, undefined8 a7, undefined8 a8, int id);
void decal_spawn_with_entity(undefined8 a1, double a2, double a3, undefined8 a4, undefined8 a5,
                             undefined8 a6, undefined8 a7, undefined8 a8, int a9, int id, int a11,
                             int *a12, float *a13);
void scene_node_set_flags(int a1, int a2);
void ekun_arc_state_reset(void);
void particle_set_global_intensity(double v);
void str_replace_substring(char *buf, void *find, int *repl);

// ---- collision helpers ----
void collision_set_layer_mask(int *body, int mask);
void collision_set_shape_params(int *body, int a2, int a3, int a4, char a5);
void collision_set_flags(int *body, int flags);
int  collision_alloc_face_table(int *body);
void collision_set_face_entry(undefined4 body, int idx, undefined4 mtx);
int  mem_advance_secondary(int size);
void mtx4_set_translation(double x, double y, double z);

// ---- level 0x21/0x22 arc + platform helpers ----
void FUN_8035b414(double amount);
void FUN_8035b2fc(int a1, int a2, int a3, int a4);

// ---- anim sampling / spawn ----
void anim_sample_keyframe(undefined4 track, int a2, uint frame, float *out);
void quat_to_matrix(float *mtxOut, float *quat);
void mtx4_transform_point(float *mtx, float *pt);
void player_ai_explosion_spawn(undefined4 type, int subtype, float *pos, float *tint,
                               int a5, int a6, int a7);
int  floor_height_from_position(double x, double z);
void player_ai_projectile_spawn_simple(double a1, double a2, undefined4 a3, float *pos,
                                       int *dir, int a6, int *a7, int a8, int a9, int a10,
                                       int a11, int a12);

// ---- raw globals ----
extern float FLOAT_806129a0;   // current scripted-prop intensity
extern int   DAT_806129a4;
extern int   DAT_806129a8;     // keyframe activation floor (frame index)
extern int   DAT_80612614;     // current cutscene id (variant A)
extern int   DAT_8061294c;     // current cutscene id (variant B)
extern uint  DAT_806109c0;     // frame counter
extern int   DAT_806108e4;     // wind sample count
extern float FLOAT_8060faf8;   // seconds -> frame-index scale
extern int   DAT_80611824;     // anim track table base
extern short DAT_804d1cd0;     // per-prop track index table (short[])
extern float DAT_80472478, DAT_8047247c, DAT_80472480; // prev track prop position (x,y,z)
extern int   DAT_8047243c;     // decal descriptor ptr
extern float DAT_80472454;     // decal params ptr
extern void  *DAT_80610fd8;    // substring to find
extern int   DAT_80610fe0;     // substring replacement

// prop-name string literals (labeled data in ROM)
extern unsigned int s_cs_01_reenter_prop_gcr_80472460;
extern int s_igcs_16_tracks_prop_raw_804724b4;
extern int s_igcs_14_tracks_prop_raw_80472484;
extern int s_igcs_15_tracks_prop_raw_8047249c;
extern unsigned int s_cs_20_platform_prop_gcr_804724cc;

// ---- float/double literal pool ----
extern float FLOAT_80618df0, FLOAT_80618df4, FLOAT_80618df8, FLOAT_80618dfc, FLOAT_80618e00;
extern float FLOAT_80618e04, FLOAT_80618e08, FLOAT_80618e18, FLOAT_80618e20;
extern double DOUBLE_80618e10;

// ---- dispatcher (0x802546b0) helpers + tables ----
int  prop_type_to_sound_class(int propType);
void ekun_arc_nodes_update(void);
int  FUN_80355348(double a1, double a2, int a3, undefined4 *a4, int a5); // seq-event alloc/start
void FUN_80355574(double t, int handle);                                // seq-event set-duration
void FUN_8035560c(double t, int handle);                                // seq-event stop
void FUN_8035b1ec(int a1, int ctx, int desc);                           // fire scripted sequence event
// prop-behaviour table (normal path): stride-0x10 records selected by sound class.
// Declared as int so &SYMBOL is an int* into the labeled table region.
extern int   PTR_DAT_80471f24;   // -> DAT_806119a0 intensity ptr
extern int   PTR_DAT_80471f2c;   // -> DAT_8061197c event-table base
extern int   DAT_80471f30;       // -> DAT_80611980 event count
extern int   DAT_80471f28;       // -> DAT_8061198c aux
// prop-behaviour table (variant-B path): stride-0x10 records selected by cutscene id
extern int   PTR_DAT_80472064;
extern int   DAT_8047206c;
extern int   DAT_80472070;
extern int   DAT_80472068;
// resolved active prop-behaviour table + counters
extern int   DAT_806119a0;       // current intensity ptr (stored as raw pointer value)
extern char *DAT_8061197c;       // current event-table base (byte-addressed, stride 0x50)
extern int   DAT_80611980;       // event count
extern int   DAT_8061198c;       // aux
extern int   DAT_80611978;       // per-tick scratch
extern int   DAT_805015c0;       // global cutscene flags (bit 0x200 selects variant-B table)
extern float FLOAT_80618e24, FLOAT_80618e28;
// ---- intra-file forward decls ----
void FUN_80254400(int a1, undefined4 a2, int a3, undefined4 a4, float *a5, float *a6);

// ============================================================================
// 0x80253e74  csprop_get_intensity
// Accessor for the current scripted-prop intensity value (returned as double).
// ============================================================================
double FUN_80253e74(void)
{
    return (double)FLOAT_806129a0;
}

// ============================================================================
// 0x80253e7c  csprop_preload_level1e
// Preloads the prop resource set (id 0x24d) when the current cutscene is 0x1e.
// ============================================================================
void FUN_80253e7c(undefined8 p1, undefined8 p2, undefined8 p3, undefined8 p4, undefined8 p5,
                  undefined8 p6, undefined8 p7, undefined8 p8)
{
    if (DAT_80612614 == 0x1e) {
        entity_preload_resources(p1, p2, p3, p4, p5, p6, p7, p8, 0x24d);
    }
}

// ============================================================================
// 0x80253eac  csprop_init_level_effect
// Resets the scripted-prop state and, per cutscene id, spawns the re-entry decal
// (0x1e) or resets the Ekun arc state (0x21/0x22).
// ============================================================================
void FUN_80253eac(undefined8 p1, double p2, double p3, undefined8 p4, undefined8 p5,
                  undefined8 p6, undefined8 p7, undefined8 p8)
{
    DAT_806129a8 = (int)0xfffffff6;
    DAT_806129a4 = 0;
    FLOAT_806129a0 = FLOAT_80618df0;
    if (DAT_80612614 == 0x1e) {
        decal_spawn_with_entity(p1, p2, p3, p4, p5, p6, p7, p8, 1, 0x24d, 0,
                                (int *)&DAT_8047243c, (float *)&DAT_80472454);
        scene_node_set_flags(2, 8);
    } else if ((DAT_80612614 == 0x21) || (DAT_80612614 == 0x22)) {
        ekun_arc_state_reset();
    }
}

// ============================================================================
// 0x80253f30  csprop_reenter_collision_tick
// cs_01 re-entry: ramps the hull collision opacity/params over the cutscene time
// window and adjusts particle intensity.
// ============================================================================
void FUN_80253f30(void)
{
    int    ctx;
    int    model;
    int    frame;
    int    prop;
    double amt;
    double local_28;

    ctx   = (int)FUN_803826f0();
    model = cutscene_get_model_data((uint *)&s_cs_01_reenter_prop_gcr_80472460);
    frame = (int)(*(float *)(ctx + 8) * FLOAT_8060faf8);
    FLOAT_806129a0 = FLOAT_80618df0;
    if (frame < 0xb54) {
        FLOAT_806129a0 = FLOAT_80618df4;
    }
    particle_set_global_intensity((double)FLOAT_80618df8);
    if ((-1 < model) && (prop = *(int *)(ctx + model * 0x2c + 0x48), prop != 0)) {
        amt = (double)FLOAT_80618df0;
        if ((0xd65 < frame) && (frame < 0xed9)) {
            amt = (double)FLOAT_80618dfc;
        }
        if ((frame < 0xd66) || (0xdca < frame)) {
            if ((0xea5 < frame) && (frame < 0xed9)) {
                local_28 = (double)(CONCAT44(0x43300000, frame - 0xea6) ^ 0x80000000);
                amt = (double)(FLOAT_80618dfc *
                               (FLOAT_80618df4 - (float)(local_28 - DOUBLE_80618e10) / FLOAT_80618e04));
            }
        } else {
            local_28 = (double)(CONCAT44(0x43300000, frame - 0xd66) ^ 0x80000000);
            amt = (double)((FLOAT_80618dfc * (float)(local_28 - DOUBLE_80618e10)) / FLOAT_80618e00);
        }
        collision_set_layer_mask(*(int **)(prop + 4), 5);
        collision_set_shape_params(*(int **)(prop + 4), 0xff, 0xffffffff, 0xff,
                                   (char)(int)((double)FLOAT_80618e08 * amt));
        collision_set_flags(*(int **)(prop + 4), 0x20);
    }
    FUN_8038273c();
}

// ============================================================================
// 0x802540b8  csprop_tracks_shake_tick
// igcs_14/15/16 collapsing-tracks prop: measures the prop's frame-to-frame motion
// and applies a randomized, eased angular wobble to the prop, allocating a face
// table + transform when needed.
// ============================================================================
void FUN_802540b8(int param_1)
{
    float      dx, dy, dz;
    int        model;
    int        node;
    undefined4 mtx;
    double     dist;
    double     wobble;
    uint       auStack_68[16];
    undefined4 local_28, local_20;
    uint       uStack_24, uStack_1c;

    if (DAT_8061294c == 0x12) {
        FUN_8038b510((int *)auStack_68, (int *)&s_igcs_16_tracks_prop_raw_804724b4);
    } else if (DAT_8061294c < 0x12) {
        if (DAT_8061294c == 0x10) {
            FUN_8038b510((int *)auStack_68, (int *)&s_igcs_14_tracks_prop_raw_80472484);
        } else if (0xf < DAT_8061294c) {
            FUN_8038b510((int *)auStack_68, (int *)&s_igcs_15_tracks_prop_raw_8047249c);
        }
    } else if (DAT_8061294c == 0x17) {
        FUN_8038b510((int *)auStack_68, (int *)&s_igcs_15_tracks_prop_raw_8047249c);
    }
    str_replace_substring((char *)auStack_68, &DAT_80610fd8, (int *)&DAT_80610fe0);
    model = cutscene_get_model_data(auStack_68);
    if ((-1 < model) && (model = *(int *)(param_1 + model * 0x2c + 0x48), model != 0)) {
        node = *(int *)(*(int *)(model + 4) + 0x10);
        dy = *(float *)(node + 0x34) - DAT_8047247c;
        dx = *(float *)(node + 0x30) - DAT_80472478;
        dz = *(float *)(node + 0x38) - DAT_80472480;
        DAT_80472478 = *(float *)(node + 0x30);
        DAT_8047247c = *(float *)(node + 0x34);
        DAT_80472480 = *(float *)(node + 0x38);
        dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
        uStack_1c = DAT_806109c0 ^ 0x80000000;
        local_20  = 0x43300000;
        uStack_24 = DAT_806108e4 ^ 0x80000000;
        local_28  = 0x43300000;
        (void)local_20; (void)local_28;
        wobble = (double)(FLOAT_80618e18 *
                          (FLOAT_80618df4 /
                           (float)((double)CONCAT44(0x43300000, uStack_1c) - DOUBLE_80618e10)) *
                          (float)((double)(float)((double)CONCAT44(0x43300000, uStack_24) -
                                                  DOUBLE_80618e10) * dist) +
                          *(float *)(model + 0x7c));
        dist = FUN_80390324((double)((float)(wobble - (double)FLOAT_80618df0) / FLOAT_80618df4));
        *(float *)(model + 0x7c) = -(float)((double)FLOAT_80618df4 * (double)(float)dist - wobble);
        if (((*(int **)(model + 4))[0x7a] != 0) ||
            (node = collision_alloc_face_table(*(int **)(model + 4)), node != 0)) {
            node = *(int *)(*(int *)(model + 4) + 0x1e8);
            *(uint *)(node + 0x1c) = *(uint *)(node + 0x1c) | 0x8000;
            mtx = mem_advance_secondary(0x40);
            mtx4_set_translation((double)*(float *)(model + 0x7c), (double)FLOAT_80618df0,
                                 (double)FLOAT_80618df0);
            collision_set_face_entry(*(undefined4 *)(model + 4), 0, mtx);
        }
    }
}

// ============================================================================
// 0x802542b8  csprop_platform_tick
// cs_20 platform prop: over one time window drives a platform helper on the model;
// over two later windows ramps an arc/tilt value up then down.
// ============================================================================
void FUN_802542b8(int param_1)
{
    int    frame;
    int    model;
    double local_18;

    frame = (int)(*(float *)(param_1 + 8) * FLOAT_8060faf8);
    if ((frame < 0x10e4) || (0x1102 < frame)) {
        if ((frame < 0x1663) || (0x1677 < frame)) {
            if ((0x1677 < frame) && (frame < 0x168d)) {
                local_18 = (double)(CONCAT44(0x43300000, frame - 0x1678) ^ 0x80000000);
                FUN_8035b414((double)(FLOAT_80618df4 -
                                      (float)(local_18 - DOUBLE_80618e10) / FLOAT_80618e20));
            }
        } else {
            local_18 = (double)(CONCAT44(0x43300000, frame - 0x1663) ^ 0x80000000);
            FUN_8035b414((double)((float)(local_18 - DOUBLE_80618e10) / FLOAT_80618e20));
        }
    } else {
        model = cutscene_get_model_data((uint *)&s_cs_20_platform_prop_gcr_804724cc);
        if ((-1 < model) && (model = *(int *)(param_1 + model * 0x2c + 0x48), model != 0)) {
            FUN_8035b2fc(1, param_1, 0x13, model);
        }
    }
}

// ============================================================================
// 0x80254400  csprop_sample_anim_transform
// Samples the prop's animation track at the current frame, writing the sampled
// position to param_5 and a quaternion-derived transformed vector to param_6.
// ============================================================================
void FUN_80254400(int param_1, undefined4 param_2, int param_3, undefined4 param_4,
                  float *param_5, float *param_6)
{
    uint       frame;
    float      fVar2, fVar3;
    // anim_sample_keyframe fills a contiguous sample block: [0..2]=position,
    // [3..6]=quaternion (matches the ROM's stack layout local_78..local_60).
    float      sample[8];
    float      quat[4];
    float      afStack_5c[17];

    (void)param_2; (void)param_4;
    frame = (uint)(*(float *)(param_1 + 8) * FLOAT_8060faf8);
    anim_sample_keyframe(
        *(undefined4 *)(DAT_80611824 +
                        (short)(&DAT_804d1cd0)[*(int *)(param_1 + param_3 * 0x2c + 0x30)] * 4),
        0, frame, sample);
    fVar2 = FLOAT_80618df0;
    *param_5 = sample[0];
    fVar3 = FLOAT_80618df4;
    param_5[1] = sample[1];
    param_5[2] = sample[2];
    *param_6 = fVar2;
    param_6[1] = fVar2;
    param_6[2] = fVar3;
    quat[0] = sample[3];
    quat[1] = sample[4];
    quat[2] = sample[5];
    quat[3] = sample[6];
    quat_to_matrix(afStack_5c, quat);
    mtx4_transform_point(afStack_5c, param_6);
}

// ============================================================================
// 0x802544e8  csprop_find_active_keyframe
// Scans a keyframe-time array (param_2, param_3 entries) for the first frame that
// is <= the given time and >= the activation floor (DAT_806129a8); returns the
// truncated time on hit, -1 otherwise.
// ============================================================================
int FUN_802544e8(double param_1, int param_2, int param_3)
{
    int    off;
    double t;

    off = 0;
    if (0 < param_3) {
        do {
            t = (double)(CONCAT44(0x43300000, *(int *)(param_2 + off)) ^ 0x80000000);
            if (((double)(float)(t - DOUBLE_80618e10) <= param_1) &&
                (DAT_806129a8 <= *(int *)(param_2 + off))) {
                return (int)param_1;
            }
            off = off + 4;
            param_3 = param_3 + -1;
        } while (param_3 != 0);
    }
    return -1;
}

// ============================================================================
// 0x80254558  csprop_spawn_explosion_event
// Cutscene event (phases 1/2): samples the prop transform and spawns an explosion
// of the prop's type at the sampled position.
// ============================================================================
undefined4 FUN_80254558(int param_1, int param_2, int param_3, int param_4, undefined4 param_5)
{
    float afStack_28[3];
    float afStack_1c[6];

    if ((param_4 == 1) || (param_4 == 2)) {
        FUN_80254400(param_1, param_3, param_2, param_5, afStack_1c, afStack_28);
        player_ai_explosion_spawn(*(undefined4 *)(param_3 + 0x3c), 0x17, afStack_1c, (float *)0,
                                  0, 0, 1);
    }
    return 1;
}

// ============================================================================
// 0x802545d4  csprop_spawn_projectile_event
// Cutscene render event (phases 1/2): samples the prop transform and spawns a
// simple AI projectile of the prop's type, using the floor height as the target.
// ============================================================================
void FUN_802545d4(undefined4 param_1, undefined4 param_2, int param_3, int param_4,
                  undefined4 param_5)
{
    int        ctx;
    int        floorId;
    double     dVar3;
    double     in_f31 = 0.0, in_ps31_1 = 0.0;
    unsigned long long packed;
    float      afStack_40[3];
    float      afStack_34[11];
    float      local_8, fStack_4;

    (void)param_1; (void)param_2;
    local_8  = (float)in_f31;
    fStack_4 = (float)in_ps31_1;
    (void)local_8; (void)fStack_4;

    packed = FUN_803826f0();
    ctx    = (int)(unsigned int)(packed >> 0x20);
    if ((param_4 == 1) || (param_4 == 2)) {
        FUN_80254400(ctx, param_3, (int)(unsigned int)packed, param_5, afStack_34, afStack_40);
        dVar3   = (double)*(float *)(param_3 + 0x48);
        floorId = floor_height_from_position((double)FLOAT_80618df0, (double)FLOAT_80618df0);
        if (floorId == 0) {
            floorId = 0xd5;
        }
        player_ai_projectile_spawn_simple(
            dVar3, (double)FLOAT_80618df0, *(undefined4 *)(param_3 + 0x3c), afStack_34,
            (int *)afStack_40, *(int *)(ctx + (int)(unsigned int)packed * 0x2c + 0x48),
            (int *)0, 0, 0, floorId, 0, -1);
    }
    FUN_8038273c();
}

// ============================================================================
// 0x802546b0  cutscene_prop_script_tick
// Top-level scripted-prop dispatcher, invoked once per cutscene keyframe from
// cutscene_process_keyframe. It:
//   1. Resolves the current cutscene's prop-behaviour table (DAT_8061197c / count
//      DAT_80611980) either from the prop-type -> sound-class mapping table
//      (PTR_DAT_80471f24 family, normal path) or the alternate variant-B table
//      (PTR_DAT_80472064 family, when DAT_805015c0 & 0x200 is set).
//   2. Runs the level-specific per-frame prop tick for the active cutscene id
//      (0x21/0x22 arc nodes, 0x18 re-entry hull, 0x05 collapsing tracks, 0x52
//      platform).
//   3. Walks the prop-event table (stride 0x50): for each event whose time window
//      contains the current frame index (uVar9 = time * FLOAT_8060faf8), evaluates
//      an activation phase (iVar7 = 1 enter / 2 exit / 3 one-shot) then dispatches
//      by event opcode (*puVar8): 0/1 anim-transform playback, 2 anim stop, 3/4
//      projectile spawn (immediate / by active keyframe), 5/6 sequence-event fire,
//      7/8 set/clear per-node flag bit, 9 explosion spawn.
// DAT_806129a8 is the running "activation floor" carried between events; the raw
// iVar4 context pointer comes from the cutscene enter helper (FUN_803826f0), same
// as the sibling prop ticks in this module.
// ============================================================================
void FUN_802546b0(void)
{
    float  fVar1;
    bool   bVar2;
    int    iVar3;
    int    ctx;      /* cutscene context pointer (FUN_803826f0) */
    uint   uVar5;
    int    iVar6;
    int    iVar7;    /* activation phase: 1 enter, 2 exit, 3 one-shot */
    undefined4 *puVar8;
    uint   uVar9;    /* current frame index */
    int    iVar10;
    int    iVar11;
    double dVar12;
    double dVar13;
    float  afStack_88[3];
    float  afStack_7c[3];
    int    local_70;
    undefined4 local_6c, local_68, local_64, local_60;
    int    local_5c;
    undefined4 local_58, local_54, local_50, local_4c;
    double local_48;
    undefined4 local_40;
    uint   uStack_3c;
    double local_38;

    ctx  = (int)FUN_803826f0();
    uVar9 = (uint)(*(float *)(ctx + 8) * FLOAT_8060faf8);
    local_48 = (double)(longlong)(int)uVar9;

    if ((DAT_805015c0 & 0x200) == 0) {
        iVar11 = prop_type_to_sound_class(DAT_80612614);
        DAT_806119a0 = (&PTR_DAT_80471f24)[iVar11 * 4];
        DAT_8061197c = (char *)(&PTR_DAT_80471f2c)[iVar11 * 4];
        DAT_80611980 = *(int *)((char *)&DAT_80471f30 + iVar11 * 0x10);
        DAT_8061198c = *(int *)((char *)&DAT_80471f28 + iVar11 * 0x10);
    } else {
        iVar11 = DAT_8061294c * 0x10;
        DAT_806119a0 = (&PTR_DAT_80472064)[DAT_8061294c * 4];
        DAT_8061197c = *(char **)((char *)&DAT_8047206c + iVar11);
        DAT_80611980 = *(int *)((char *)&DAT_80472070 + iVar11);
        DAT_8061198c = *(int *)((char *)&DAT_80472068 + iVar11);
    }

    if ((DAT_80612614 == 0x21) || (DAT_80612614 == 0x22)) {
        ekun_arc_nodes_update();
    } else if (DAT_80612614 == 0x18) {
        FUN_80253f30();                 /* csprop_reenter_collision_tick */
        uVar9 = uVar9 - 0xa8c;
    } else if ((DAT_80612614 == 5) && ((DAT_805015c0 & 0x200) != 0)) {
        FUN_802540b8(ctx);              /* csprop_tracks_shake_tick */
    } else if (DAT_80612614 == 0x52) {
        FUN_802542b8(ctx);              /* csprop_platform_tick */
    }

    iVar11 = 0;
    DAT_80611978 = 0;
    uVar5 = DAT_806129a8;
    if (DAT_8061197c != (char *)0x0) {
        dVar13 = (double)FLOAT_80618df0;
        for (iVar10 = 0; uVar5 = uVar9, iVar10 < DAT_80611980; iVar10 = iVar10 + 1) {
            iVar7 = 0;
            puVar8 = (undefined4 *)(DAT_8061197c + iVar11);
            dVar12 = (double)(float)puVar8[8];
            iVar3 = puVar8[1];
            if ((dVar13 == dVar12) || (iVar6 = puVar8[7], iVar6 < 1)) {
                iVar6 = (int)(float)puVar8[6];
                local_48 = (double)(longlong)iVar6;
                if ((iVar6 <= (int)uVar9) && ((int)DAT_806129a8 < iVar6)) {
                    iVar7 = 1;
                }
            } else {
                uVar5 = uVar9 ^ 0x80000000;
                local_48 = (double)CONCAT44(0x43300000, uVar5);
                fVar1 = (float)puVar8[6];
                if (((float)(local_48 - DOUBLE_80618e10) < fVar1) ||
                    (local_48 = (double)CONCAT44(0x43300000, uVar5),
                     dVar12 < (double)(float)(local_48 - DOUBLE_80618e10))) {
                    local_48 = (double)CONCAT44(0x43300000, uVar5);
                    local_38 = (double)CONCAT44(0x43300000, uVar9 ^ 0x80000000);
                    if ((fVar1 <= (float)(local_38 - DOUBLE_80618e10)) &&
                        (local_38 = (double)(CONCAT44(0x43300000, DAT_806129a8) ^ 0x80000000),
                         (double)(float)(local_38 - DOUBLE_80618e10) <= dVar12)) {
                        iVar7 = 3;
                    }
                } else {
                    uVar5 = (uint)fVar1;
                    local_48 = (double)(longlong)(int)uVar5;
                    for (; (int)(uVar5 + iVar6) <= (int)uVar9; uVar5 = uVar5 + iVar6) {
                    }
                    if (((int)uVar5 <= (int)uVar9) && ((int)DAT_806129a8 <= (int)uVar5)) {
                        uVar5 = uVar5 ^ 0x80000000;
                        bVar2 = false;
                        local_48 = (double)CONCAT44(0x43300000, uVar5);
                        if (fVar1 <= (float)(local_48 - DOUBLE_80618e10)) {
                            local_38 = (double)(CONCAT44(0x43300000, DAT_806108e4) ^ 0x80000000);
                            local_40 = 0x43300000;
                            uStack_3c = uVar5;
                            if ((float)((double)CONCAT44(0x43300000, uVar5) - DOUBLE_80618e10) <
                                fVar1 + (float)(local_38 - DOUBLE_80618e10)) {
                                bVar2 = true;
                            }
                        }
                        iVar7 = 2;
                        if (bVar2) {
                            iVar7 = 1;
                        }
                    }
                }
            }
            if (iVar7 != 0) {
                switch (*puVar8) {
                case 0:
                    if (iVar7 == 1) {
                        DAT_806129a4 = FUN_80355348((double)FLOAT_80618df0, (double)FLOAT_80618e24,
                                                    0, puVar8 + 9, 0xd5);
                        FUN_80355574((double)(((float)puVar8[8] - (float)puVar8[6]) / FLOAT_8060faf8),
                                     DAT_806129a4);
                    }
                    break;
                case 1:
                    if (iVar7 == 1) {
                        local_38 = (double)(longlong)(int)(float)puVar8[6];
                        FUN_80254400(ctx, *puVar8, iVar3, (int)(float)puVar8[6],
                                     afStack_7c, afStack_88);   /* csprop_sample_anim_transform */
                        DAT_806129a4 = FUN_80355348((double)FLOAT_80618e28, (double)FLOAT_80618e24,
                                                    1, (undefined4 *)afStack_7c, 0xd5);
                        FUN_80355574((double)FLOAT_80618df4, DAT_806129a4);
                    }
                    break;
                case 2:
                    if (iVar7 == 1) {
                        FUN_8035560c((double)((float)(dVar12 - (double)(float)puVar8[6]) / FLOAT_8060faf8),
                                     DAT_806129a4);
                        DAT_806129a4 = 0;
                    }
                    break;
                case 3:
                    local_38 = (double)(longlong)(int)(float)puVar8[6];
                    FUN_802545d4(ctx, iVar3, (int)puVar8, iVar7, (int)(float)puVar8[6]);
                    break;
                case 4:
                    local_38 = (double)CONCAT44(0x43300000, uVar9 ^ 0x80000000);
                    iVar6 = FUN_802544e8((double)(float)(local_38 - DOUBLE_80618e10),
                                         puVar8[0x10], puVar8[0x11]);
                    if (iVar6 != -1) {
                        FUN_802545d4(ctx, iVar3, (int)puVar8, iVar7, iVar6);
                    }
                    break;
                case 5:
                    local_38 = (double)CONCAT44(0x43300000, uVar9 ^ 0x80000000);
                    local_58 = puVar8[2];
                    local_54 = puVar8[3];
                    local_50 = puVar8[4];
                    local_4c = puVar8[5];
                    local_5c = iVar3;
                    FUN_8035b1ec(1, ctx, (int)&local_5c);
                    break;
                case 6:
                    local_38 = (double)CONCAT44(0x43300000, uVar9 ^ 0x80000000);
                    local_6c = puVar8[2];
                    local_68 = puVar8[3];
                    local_64 = puVar8[4];
                    local_60 = puVar8[5];
                    local_70 = iVar3;
                    FUN_8035b1ec(1, ctx, (int)&local_70);
                    break;
                case 7:
                    iVar3 = iVar3 * 0x2c + 0x58;
                    *(uint *)(ctx + iVar3) = *(uint *)(ctx + iVar3) | 1;
                    break;
                case 8:
                    iVar3 = iVar3 * 0x2c + 0x58;
                    *(uint *)(ctx + iVar3) = *(uint *)(ctx + iVar3) & 0xfffffffe;
                    break;
                case 9:
                    local_38 = (double)(longlong)(int)(float)puVar8[6];
                    FUN_80254558(ctx, iVar3, (int)puVar8, iVar7, (int)(float)puVar8[6]);
                }
            }
            iVar11 = iVar11 + 0x50;
        }
    }
    DAT_806129a8 = uVar5;
    (void)local_48; (void)local_40; (void)uStack_3c; (void)local_38;
}

} // extern "C"
