// worm_ghz_weather.cpp
// GHZ reactor set-piece: weather (rain/lightning) hazard driver plus the
// first-person electro-arc tool. Sibling of worm_ghz_state/level/arc/object.
// Two sub-clusters here:
//   * Weather: indoor/outdoor toggle, random lightning-flash scheduler, GX
//     rain-streak render pass (+ per-player rain damage), and the class-0x557
//     arc-body soft-separation pass.
//   * FP electro-tool: resource preload, muzzle-flash placement, the per-frame
//     aim/charge/fire state machine, and the ambient hum 3D sound.
//
// Batch: batch_660  (addresses 0x8021703c .. 0x802185d4)

#include "common.h"
#include <math.h>

// Ghidra emits SQRT() for the fsqrt intrinsic; map it to the CRT single here.
static inline float SQRT(float x) { return sqrtf(x); }

// ---------------------------------------------------------------------------
// Named cross-module helpers (resolved through shared generated_decls.h):
//   scene_node_get_render_data (void*), sound_play_request, sound_init_play_params,
//   sound_clear_bank_ref, sound_register_fade_out, sound_update_3d_source,
//   weather_set_layer_param, weather_render_rain_streaks, sprite_get_texture_id,
//   gx_draw_sprite_rect, switch_slot_reset, input_get_buttons_held/pressed,
//   scalar_step_toward_snap, entity_preload_resources, damage_dispatch_to_entity,
//   player_ai_projectile_spawn_simple, player_ai_is_ranged_weapon.
// These are NOT redeclared here; they come in via common.h.
//
// Helpers whose shared decl is `void` but which return a value at the ABI level
// are invoked through file-local typed aliases bound to their ROM address.
// ---------------------------------------------------------------------------
typedef int  (*rng_tick_fp)(void);
typedef int  (*raycast_fp)(int, void *, void *, int, float *, float *, int *,
                           unsigned int, unsigned int, int *);
typedef int  (*gx_setmat_fp)(int);
typedef int  (*gx_setblend_fp)(int);
typedef int  (*gx_setsub_fp)(int);
static const rng_tick_fp   __ghz_rng          = (rng_tick_fp)0x8013104c;   // rng_counter_tick
static const raycast_fp    __ghz_raycast      = (raycast_fp)0x8005bb44;    // scene_raycast
static const gx_setmat_fp  __ghz_gx_set_mat   = (gx_setmat_fp)0x8016e77c;  // gx_set_active_material
static const gx_setblend_fp __ghz_gx_blend    = (gx_setblend_fp)0x8016e8ac;// gx_set_blend_mode
static const gx_setsub_fp  __ghz_gx_submat    = (gx_setsub_fp)0x8016f00c;  // gx_set_sub_material_id
typedef long long (*slotreset_fp)(int);
static const slotreset_fp  __ghz_slot_reset   = (slotreset_fp)0x8008dc3c;  // switch_slot_reset (ret in r3:r4)
// Wide-ABI reactor helpers invoked with the game's 8-float-register calling
// convention; the shared decls carry incompatible arity/types, so bind by ROM
// address with the call-site-accurate signature (Compile-Clean Standard).
typedef void (*preload_fp)(long long, double, double, long long, long long,
                           long long, long long, long long, int);
typedef void (*damage_fp)(long long, double, double, long long, long long,
                          long long, long long, long long, int *);
typedef void (*fadeout_fp)(double, double, int *, int *);
typedef void (*snd3d_fp)(double, double, double, void *, int, int, int);
typedef int  (*projspawn_fp)(double, double, int, float *, int *, int, int *,
                             int, int, int, int, int);
static const preload_fp    __ghz_preload      = (preload_fp)0x80070ba8;    // entity_preload_resources
static const damage_fp     __ghz_damage       = (damage_fp)0x80090180;     // damage_dispatch_to_entity
static const fadeout_fp    __ghz_fadeout      = (fadeout_fp)0x8001063c;    // sound_register_fade_out
static const snd3d_fp      __ghz_snd3d        = (snd3d_fp)0x800102e4;      // sound_update_3d_source
static const projspawn_fp  __ghz_projspawn    = (projspawn_fp)0x800bc760;  // player_ai_projectile_spawn_simple

// ---------------------------------------------------------------------------
// File-local externs for raw ROM symbols (address-bound; different signatures
// across files per the Compile-Clean Standard).
// ---------------------------------------------------------------------------
extern int    FUN_8027eb3c(int *weapon);          // weapon-slot usable query
extern double FUN_800a4498(int *pad, int axis);   // analog axis read
extern double FUN_80390324(double x);             // floor()/trunc helper
extern void   FUN_80221074(double, double, double, double, long long, long long,
                           long long, long long);  // reactor sequence kickoff
extern int    FUN_8023c59c(int tag, int out, int stride); // gather by tag
extern int    __save_gpr_25(void);
extern void   __restore_gpr_25(void);
extern void   FUN_803826f0(void);                  // current-entity push
extern void   FUN_8038273c(void);                  // current-entity pop

// Raw data / constant symbols
extern int    DAT_80612098;               // spark-source count
extern int   *DAT_80588610[1];            // spark-source ptr table
extern int    DAT_805c6c90;               // lightning ring index
extern float  DAT_805c6c8c;               // lightning master flash level
extern float  DAT_805c6c94;               // lightning bolt X ring (stride 8)
extern float  DAT_805c6c98;               // lightning bolt Y ring (stride 8)
extern float  DAT_805c6cbc;               // per-bolt intensity ring (5)
extern int   *DAT_80610a20;               // material table base
extern unsigned int DAT_806109c0;         // frame delta (raw)
extern unsigned int DAT_80611748[16];     // rng scratch block
extern int    DAT_80611d78;               // player-record array base
extern int    DAT_80612610;               // active player count
extern float  DAT_805c6d08;               // muzzle flash offset X
extern float  DAT_805c6d10;               // muzzle flash offset Z
extern float  DAT_805c6d0c;               // muzzle flash offset Y
extern float  DAT_805c6cd0;               // muzzle emitter mode
extern float  DAT_805c6cd8;               // muzzle world X
extern float  DAT_805c6cdc;               // muzzle world Y
extern float  DAT_805c6ce0;               // muzzle world Z
extern float  DAT_80583228;               // sine LUT (0x800 entries)
extern int    DAT_80611d54;               // active player camera record
extern int   *DAT_80610e5c;               // arc-tool state block ptr
extern int    DAT_80611c24;               // entity handle table base
extern float  DAT_80612820;               // arc anim phase accumulator
extern float  DAT_806108e4;               // arc anim phase step
extern int    DAT_80610834;               // arc loop sound handle
extern int    DAT_805015c0;               // global game flags
extern float  DAT_80612828;               // ambient hum level (smoothed)
extern int    DAT_80612824;               // ambient hum 3D source handle
extern float  FLOAT_8061281c;             // arc charge (elevation)
extern float  FLOAT_80612818;             // arc charge (azimuth)
extern float  FLOAT_80612828;             // ambient hum level (smoothed, addr form)
extern int    DAT_8045a208[4];            // arc-body source tag tables
extern float  DAT_803dff64, DAT_803dff68, DAT_803dff6c; // rain damage dir
extern float  DAT_803dff70, DAT_803dff74, DAT_803dff78; // rain damage pos

extern float  FLOAT_80618090;   // 1.0f
extern float  FLOAT_80618094;
extern float  FLOAT_80618098;   // 0.0f
extern float  FLOAT_806180a4;
extern float  FLOAT_806180b0;
extern float  FLOAT_806180b4;
extern float  FLOAT_806180b8;
extern float  FLOAT_806180bc;
extern float  FLOAT_806180c8;
extern float  FLOAT_806180e4;
extern float  FLOAT_806180f8;
extern float  FLOAT_806180fc;
extern float  FLOAT_8061810c;
extern float  FLOAT_80618110;
extern float  FLOAT_8061811c;
extern float  FLOAT_80618118;
extern float  FLOAT_80618134;
extern float  FLOAT_8061814c;
extern float  FLOAT_8061815c;
extern float  FLOAT_80618158;
extern float  FLOAT_80618164;
extern float  FLOAT_806181a4;
extern float  FLOAT_806181cc;
extern float  FLOAT_806181d0;
extern float  FLOAT_806181d4;
extern float  FLOAT_806181d8;
extern float  FLOAT_806181dc;
extern float  FLOAT_806181e0;
extern float  FLOAT_806181e4;
extern float  FLOAT_806181e8;
extern float  FLOAT_806181ec;
extern float  FLOAT_806181f0;
extern float  FLOAT_806181f4;
extern float  FLOAT_806181f8;
extern float  FLOAT_806181fc;
extern float  FLOAT_80618200;
extern float  FLOAT_80618108;  // placeholder for pattern completeness
extern float  FLOAT_806108ec;  // frame-time scale
extern double DOUBLE_806180d0;
extern double DOUBLE_806180d8;

// CONCAT44 (u32->double bit pair) is provided by common.h.

// ---------------------------------------------------------------------------
// 0x8021703c  ghz_reactor_find_los_spark_source
// Walks the spark-source list (DAT_80588610[0..DAT_80612098)) and returns the
// first source that: is not a ranged weapon (or is usable), is not weapon type
// 0x20, is not flagged (bit3), lies within the entity's radius, and is visible
// via a line-of-sight raycast. Saves/restores each candidate's cull bits
// (mask ~0x60) around the ray so it does not self-occlude.
// ---------------------------------------------------------------------------
void ghz_reactor_find_los_spark_source(void)
{
    int self;
    int radiusEnt;
    int found;
    int i;
    int *src;
    int srcEnt;
    int ranged;
    unsigned int usable;
    int los;
    unsigned int selfFlags;
    unsigned int srcFlags;
    float origin[3];   // local_54, local_50, local_4c
    float delta[3];    // local_60, local_5c, local_58

    self = __save_gpr_25();
    found = 0;
    radiusEnt = *(int *)(self + 0x7c);
    i = 0;
    while ((i < DAT_80612098) && (found == 0)) {
        src = DAT_80588610[i];
        srcEnt = *src;
        ranged = player_ai_is_ranged_weapon((int)*(short *)(src + 0x12));
        if ((((ranged == 0) || (usable = (unsigned int)FUN_8027eb3c(src), usable != 0)) &&
             (*(short *)(src + 0x12) != 0x20)) && ((src[9] & 8U) == 0)) {
            origin[0] = *(float *)(self + 100) + *(float *)(self + 0x1a8);
            origin[1] = *(float *)(self + 0x68) + *(float *)(self + 0x1ac);
            origin[2] = *(float *)(self + 0x6c) + *(float *)(self + 0x1b0);
            delta[0] = *(float *)&src[0x2ed] - origin[0];
            delta[1] = *(float *)&src[0x2ee] - origin[1];
            delta[2] = *(float *)&src[0x2ef] - origin[2];
            if (delta[2] * delta[2] + delta[0] * delta[0] + delta[1] * delta[1] <
                *(float *)(radiusEnt + 100) * *(float *)(radiusEnt + 100)) {
                selfFlags = *(unsigned int *)(self + 0x28);
                srcFlags = *(unsigned int *)(srcEnt + 0x28);
                *(unsigned int *)(srcEnt + 0x28) = *(unsigned int *)(srcEnt + 0x28) & 0xffffff9f;
                *(unsigned int *)(self + 0x28) = *(unsigned int *)(self + 0x28) & 0xffffff9f;
                los = __ghz_raycast(*(int *)(self + 0x34), origin, delta, 0, (float *)0,
                                    (float *)0, (int *)0, 0x2111070, 1, (int *)0);
                if (los == 0) {
                    found = srcEnt;
                }
                *(unsigned int *)(srcEnt + 0x28) = srcFlags;
                *(unsigned int *)(self + 0x28) = selfFlags;
            }
        }
        i = i + 1;
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x80217280  ghz_weather_toggle_indoor
// Toggles the GHZ weather layers on/off based on the trigger volume flag
// (*param+0xc). Indoor (flag==0): disable both rain layers, set scene node
// flags (+0x29ba/+0x2a1a bit0). Outdoor: enable layers, clear flags, play a
// one-shot ambient sound (id 0x351).
// ---------------------------------------------------------------------------
void ghz_weather_toggle_indoor(int *ctx)
{
    int rd;
    int node;
    int params[12];

    rd = (int)(intptr_t)scene_node_get_render_data(0x71);
    if (*(int *)(*ctx + 0xc) == 0) {
        weather_set_layer_param(0, 0);
        weather_set_layer_param(1, 0);
        node = *(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14);
        *(unsigned char *)(node + 0x29ba) = *(unsigned char *)(node + 0x29ba) | 1;
        node = *(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14);
        *(unsigned char *)(node + 0x2a1a) = *(unsigned char *)(node + 0x2a1a) | 1;
    } else {
        weather_set_layer_param(0, 1);
        weather_set_layer_param(1, 1);
        node = *(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14);
        *(unsigned char *)(node + 0x29ba) = *(unsigned char *)(node + 0x29ba) & 0xfe;
        node = *(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14);
        *(unsigned char *)(node + 0x2a1a) = *(unsigned char *)(node + 0x2a1a) & 0xfe;
        sound_init_play_params((int)params);
        params[0] = 0x351;
        sound_play_request(params);
    }
}

// ---------------------------------------------------------------------------
// 0x80217388  ghz_weather_lightning_tick
// Random lightning-flash scheduler. Decays the master flash level each frame;
// when the scene weather node is valid, its rain height is in range, and a
// randomised frame-cadence hits, spawns 3 new lightning bolts (random screen
// positions into the DAT_805c6c94/98 rings) and resets flash level. Also
// decays the per-bolt intensity ring each frame.
// ---------------------------------------------------------------------------
void ghz_weather_lightning_tick(void)
{
    int rd;
    unsigned int r;
    unsigned int cadence;
    int i;

    rd = (int)(intptr_t)scene_node_get_render_data(0x71);
    DAT_805c6c8c = DAT_805c6c8c * FLOAT_80618158;
    if (((rd != 0) && (*(int *)(rd + 4) != 0)) && (*(int *)(*(int *)(rd + 4) + 4) != 0) &&
        (*(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14) != 0) &&
        (*(int *)(*(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14) + 0x29b4) != 0) &&
        (*(int *)(*(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14) + 0x2a14) != 0) &&
        (FLOAT_806180c8 <
         *(float *)(*(int *)(*(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14) + 0x29b4) + 0x30)) &&
        (*(float *)(*(int *)(*(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14) + 0x29b4) + 0x30) <
         FLOAT_806181cc)) {
        r = (unsigned int)__ghz_rng();
        cadence = ((int)DAT_806109c0 >> 2) +
                  (unsigned int)((int)DAT_806109c0 < 0 && (DAT_806109c0 & 3) != 0);
        if (r == (r / cadence) * cadence) {
            DAT_805c6c8c = FLOAT_80618090;
            for (i = 0; i < 3; i = i + 1) {
                unsigned int rr = (unsigned int)__ghz_rng();
                *(float *)(&DAT_805c6c94 + DAT_805c6c90 * 8) =
                    (float)((double)CONCAT44(0x43300000, DAT_80611748[3] ^ 0x80000000) -
                            DOUBLE_806180d0) *
                        ((float)((double)CONCAT44(0x43300000, rr) - DOUBLE_806180d8) /
                         FLOAT_806180a4) +
                    (float)((double)CONCAT44(0x43300000, DAT_80611748[0] ^ 0x80000000) -
                            DOUBLE_806180d0);
                rr = (unsigned int)__ghz_rng();
                *(float *)(&DAT_805c6c98 + DAT_805c6c90 * 8) =
                    (float)((double)CONCAT44(0x43300000, DAT_80611748[8] ^ 0x80000000) -
                            DOUBLE_806180d0) *
                        ((float)((double)CONCAT44(0x43300000, rr) - DOUBLE_806180d8) /
                         FLOAT_806180a4) +
                    (float)((double)CONCAT44(0x43300000, DAT_80611748[5] ^ 0x80000000) -
                            DOUBLE_806180d0);
                (&DAT_805c6cbc)[DAT_805c6c90] = FLOAT_80618090;
                DAT_805c6c90 = (DAT_805c6c90 + 1) % 5;
            }
        }
    }
    for (i = 0; i < 5; i = i + 1) {
        (&DAT_805c6cbc)[i] = (&DAT_805c6cbc)[i] * FLOAT_806181a4;
    }
}

// ---------------------------------------------------------------------------
// 0x80217668  ghz_weather_rain_render  (called by weather_render_post_pass)
// GX render pass for the reactor storm: draws up to 5 flickering lightning
// sprite quads (sprite 0x28f) at the ring positions, then the streaming rain
// streaks, then applies rain damage-over-time to every player whose meltdown
// timer (+0x1cc) is positive.
// ---------------------------------------------------------------------------
void ghz_weather_rain_render(long long p1, double p2, double p3, long long p4,
                             long long p5, long long p6, long long p7,
                             long long p8)
{
    int mat;
    unsigned int texW;
    unsigned int texH;
    int i;
    int idx;
    int ctx[5];       // local_b8
    int dmgKind0;     // local_a4
    int dmgKind1;     // local_a0
    float dmgScale;   // local_98
    int dmgAux;       // local_90
    void *dmgDirPtr;  // local_8c
    void *dmgPosPtr;  // local_88
    float pos[3];     // local_c4/c0/bc
    float dir[3];     // local_d0/cc/c8
    long long boom;

    FUN_803826f0();
    __ghz_gx_blend(0);
    __ghz_gx_submat(2);
    mat = sprite_get_texture_id(0x28f);
    __ghz_gx_set_mat(mat);
    texW = (unsigned int)*(unsigned short *)(*DAT_80610a20 + mat * 0x58 + 0x4e);
    texH = (unsigned int)*(unsigned short *)(*DAT_80610a20 + mat * 0x58 + 0x52);

    for (i = 0; i < 5; i = i + 1) {
        idx = (i + DAT_805c6c90) % 5;
        if (FLOAT_8061810c < (&DAT_805c6cbc)[idx]) {
            int hx = (int)(*(float *)(&DAT_805c6c94 + idx * 8) - (float)((int)texW >> 1));
            int hy = (int)(*(float *)(&DAT_805c6c98 + idx * 8) - (float)((int)texH >> 1));
            int hx2 = (int)(*(float *)(&DAT_805c6c94 + idx * 8) + (float)((int)texW >> 1));
            int hy2 = (int)(*(float *)(&DAT_805c6c98 + idx * 8) + (float)((int)texH >> 1));
            gx_draw_sprite_rect(hx, hy, hx2, hy2, 0, 0, texW << 4, texH << 4);
        }
    }

    if (FLOAT_8061810c < DAT_805c6c8c) {
        weather_render_rain_streaks();
        for (i = 0; i < DAT_80612610; i = i + 1) {
            int rec = *(int *)(DAT_80611d78 + i * 0x141c + 0x14);
            if (rec != 0) {
                if (FLOAT_80618090 <= *(float *)(rec + 0x1cc)) {
                    dir[0] = DAT_803dff64;
                    dir[1] = DAT_803dff68;
                    dir[2] = DAT_803dff6c;
                    pos[0] = DAT_803dff70;
                    pos[1] = DAT_803dff74;
                    pos[2] = DAT_803dff78;
                    boom = __ghz_slot_reset((int)ctx);
                    ctx[0] = rec;
                    dmgKind0 = 0x15;
                    dmgKind1 = 0x15;
                    dmgScale = FLOAT_8061815c;
                    dmgAux = DAT_80611d78 + i * 0x141c + 0xa8;
                    dmgDirPtr = dir;
                    dmgPosPtr = pos;
                    (void)dmgKind0; (void)dmgKind1; (void)dmgScale;
                    (void)dmgAux; (void)dmgDirPtr; (void)dmgPosPtr;
                    __ghz_damage(boom, p2, p3, p4, p5, p6, p7, p8, ctx);
                }
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80217a68  ghz_arc_body_separation_tick
// Soft-body separation for the arc bodies (class 0x557). Gathers all class-
// 0x557 entities from the four DAT_8045a208 tables, then for every pair within
// the separation radius pushes them apart along the normalised difference
// (fast rsqrt refinement), keeping the swarm from overlapping.
// ---------------------------------------------------------------------------
void ghz_arc_body_separation_tick(void)
{
    float d2;
    float dx, dy, dz;
    float invLen;
    float push;
    int count;
    int t;
    int n;
    int i;
    int j;
    int a, b;
    int bodies[8];    // local_88
    int gathered[20]; // local_68

    count = 0;
    for (t = 0; t < 4; t = t + 1) {
        n = FUN_8023c59c(DAT_8045a208[t], (int)gathered, 0x14);
        for (i = 0; i < n; i = i + 1) {
            if (*(short *)(gathered[i] + 0x94) == 0x557) {
                bodies[count] = gathered[i];
                count = count + 1;
            }
        }
    }

    for (i = 0; i < count; i = i + 1) {
        for (j = i + 1; j < count; j = j + 1) {
            a = bodies[i];
            b = bodies[j];
            d2 = (*(float *)(a + 0x6c) - *(float *)(b + 0x6c)) *
                     (*(float *)(a + 0x6c) - *(float *)(b + 0x6c)) +
                 (*(float *)(a + 100) - *(float *)(b + 100)) *
                     (*(float *)(a + 100) - *(float *)(b + 100)) +
                 (*(float *)(a + 0x68) - *(float *)(b + 0x68)) *
                     (*(float *)(a + 0x68) - *(float *)(b + 0x68));
            if ((d2 < FLOAT_806181dc) && (d2 < FLOAT_806181e0)) {
                dx = *(float *)(a + 100) - *(float *)(b + 100);
                dy = *(float *)(a + 0x68) - *(float *)(b + 0x68);
                dz = *(float *)(a + 0x6c) - *(float *)(b + 0x6c);
                d2 = dz * dz + dx * dx + dy * dy;
                invLen = 1.0f / SQRT(d2);
                d2 = FLOAT_806180b0 * d2;
                invLen = invLen * -(d2 * invLen * invLen - FLOAT_806180bc);
                push = FLOAT_806181e4 * invLen * -(d2 * invLen * invLen - FLOAT_806180bc);
                dx = (dx * push - dx) * FLOAT_806180b0;
                dy = (dy * push - dy) * FLOAT_806180b0;
                dz = (dz * push - dz) * FLOAT_806180b0;
                *(float *)(a + 100) = *(float *)(a + 100) + dx;
                *(float *)(a + 0x68) = *(float *)(a + 0x68) + dy;
                *(float *)(a + 0x6c) = *(float *)(a + 0x6c) + dz;
                *(float *)(b + 100) = *(float *)(b + 100) - dx;
                *(float *)(b + 0x68) = *(float *)(b + 0x68) - dy;
                *(float *)(b + 0x6c) = *(float *)(b + 0x6c) - dz;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80217cb8  ghz_arc_tool_preload
// Preloads the arc-tool entity classes (0x559/0x556/0x557/0x558) and clears
// four sound-bank references (0x4e8/0x4e9/0x4e7/0xc55) for the reactor tool.
// ---------------------------------------------------------------------------
void ghz_arc_tool_preload(long long p1, double p2, double p3, long long p4,
                          long long p5, long long p6, long long p7, long long p8)
{
    __ghz_preload(p1, p2, p3, p4, p5, p6, p7, p8, 0x559);
    __ghz_preload(p1, p2, p3, p4, p5, p6, p7, p8, 0x556);
    __ghz_preload(p1, p2, p3, p4, p5, p6, p7, p8, 0x557);
    __ghz_preload(p1, p2, p3, p4, p5, p6, p7, p8, 0x558);
    sound_clear_bank_ref(0x4e8);
    sound_clear_bank_ref(0x4e9);
    sound_clear_bank_ref(0x4e7);
    sound_clear_bank_ref(0xc55);
}

// ---------------------------------------------------------------------------
// 0x80217d14  ghz_arc_tool_muzzle_update
// Recomputes the arc-tool muzzle-flash emitter position and orientation from
// the current elevation/azimuth charge (FLOAT_8061281c / FLOAT_80612818),
// sampling the shared sine LUT (DAT_80583228, 0x800 entries). Writes the world
// position relative to the tool model (ent+0x7c body + 0xbb4/0xbb8/0xbbc).
// ---------------------------------------------------------------------------
void ghz_arc_tool_muzzle_update(int ent)
{
    float elev;
    int body;

    elev = FLOAT_806181e8;
    if (FLOAT_806181e8 < FLOAT_8061281c) {
        elev = FLOAT_8061281c;
    }
    body = *(int *)(ent + 0x7c);

    DAT_805c6d08 =
        (&DAT_80583228)[(int)(FLOAT_806180b4 * ((FLOAT_80618110 * elev) / FLOAT_8061811c) +
                              FLOAT_806180b0) + 0x200U & 0x7ff] *
        (&DAT_80583228)[(int)(FLOAT_806180b4 *
                                  -((FLOAT_80618110 * FLOAT_80612818) / FLOAT_8061811c) +
                              FLOAT_806180b0) + 0x200U & 0x7ff];
    DAT_805c6d10 =
        (&DAT_80583228)[(int)(FLOAT_806180b4 * ((FLOAT_80618110 * elev) / FLOAT_8061811c) +
                              FLOAT_806180b0) + 0x200U & 0x7ff] *
        (&DAT_80583228)[(int)(FLOAT_806180b4 *
                                  -((FLOAT_80618110 * FLOAT_80612818) / FLOAT_8061811c) +
                              FLOAT_806180b0) & 0x7ff];
    DAT_805c6cd0 = 3;
    DAT_805c6cd8 = FLOAT_806181ec * DAT_805c6d08 + *(float *)(body + 0xbb4);
    DAT_805c6cdc = FLOAT_806181ec *
                       (&DAT_80583228)[(int)(FLOAT_806180b4 *
                                                 ((FLOAT_80618110 * elev) / FLOAT_8061811c) +
                                             FLOAT_806180b0) & 0x7ff] +
                   *(float *)(body + 3000) + FLOAT_80618090;
    DAT_805c6ce0 = FLOAT_806181ec * DAT_805c6d10 + *(float *)(body + 0xbbc);
    DAT_805c6d0c = (&DAT_80583228)[(int)(FLOAT_806180b4 *
                                             ((FLOAT_80618110 * elev) / FLOAT_8061811c) +
                                         FLOAT_806180b0) & 0x7ff];
}

// ---------------------------------------------------------------------------
// 0x80217f2c  ghz_arc_tool_fire_tick
// Per-frame update for the first-person electro-arc tool. Reads the player's
// analog aim axes, smooths the elevation/azimuth charge angles (clamped away
// from a dead band), fires an aim raycast to find a target, runs a small tether
// state machine (*DAT_80610e5c: 0 idle / 1 charging / 2 locked), and on trigger
// fires either a beam projectile (type 0xb, held trigger) or a released bolt
// (type 4). Latches the hit class-0x557 body as the tether target. The acting
// entity arrives in r4 (decompiler 'extraout_r4'); modelled here as a param.
// ---------------------------------------------------------------------------
void ghz_arc_tool_fire_tick(int *self)
{
    bool frameTick;
    unsigned short flags;
    float len2;
    float invLen;
    int hit;
    int state;
    unsigned int held;
    unsigned int pressed;
    double axisY;
    double axisX;
    double elevD;
    double sinE, cosE, mag;
    float origin[3];   // local_d8, local_d4, local_d0
    float aimDir[3];   // local_f0, local_ec, local_e8
    float hitPos[3];   // local_e4, local_e0, local_dc
    int params[12];    // local_cc / local_9c

    FUN_803826f0();
    flags = *(unsigned short *)(self[0x4e6] + 2);

    axisY = FUN_800a4498(self, 0x11);
    elevD = (double)(float)((double)FLOAT_806108ec * -axisY);
    axisX = FUN_800a4498(self, 0x10);
    {
        float gain = FLOAT_80618090;
        if ((flags & 1) != 0) {
            gain = FLOAT_806180e4;
        }
        double raw = (double)((float)((double)FLOAT_806108ec * axisX) * gain + FLOAT_8061281c);
        double q = FUN_80390324((double)(float)((double)(float)(raw - (double)FLOAT_80618098) /
                                                (double)FLOAT_8061814c));
        FLOAT_8061281c = -(float)((double)FLOAT_8061814c * (double)(float)q - raw);
    }
    if ((FLOAT_8061281c <= FLOAT_80618164) || (FLOAT_806181f0 <= FLOAT_8061281c)) {
        if ((FLOAT_8061281c < FLOAT_806181f4) && (FLOAT_806181f8 < FLOAT_8061281c)) {
            FLOAT_8061281c = FLOAT_806181f4;
        }
    } else {
        FLOAT_8061281c = FLOAT_80618164;
    }
    {
        double raw = (double)(float)((double)FLOAT_80618134 * elevD + (double)FLOAT_80612818);
        double q = FUN_80390324((double)(float)((double)(float)(raw - (double)FLOAT_80618098) /
                                                (double)FLOAT_8061814c));
        FLOAT_80612818 = -(float)((double)FLOAT_8061814c * (double)(float)q - raw);
    }

    {
        unsigned int lutX = (unsigned int)(FLOAT_806180b4 *
            ((FLOAT_80618110 * (FLOAT_80618094 + FLOAT_80612818)) / FLOAT_8061811c) +
            FLOAT_806180b0);
        cosE = (double)(&DAT_80583228)[lutX & 0x7ff];
        int lutZ = (int)(FLOAT_806180b4 *
            ((FLOAT_80618110 * (FLOAT_80618094 + FLOAT_80612818)) / FLOAT_8061811c) +
            FLOAT_806180b0);
        sinE = (double)(&DAT_80583228)[lutZ + 0x200U & 0x7ff];
        mag = (double)(float)((double)FLOAT_806180e4 * sinE);
    }

    origin[0] = (float)((double)FLOAT_806180b0 * cosE + (double)*(float *)(DAT_80611d54 + 0x27c));
    origin[1] = (float)((double)FLOAT_806180b0 * (double)FLOAT_80618098 +
                        (double)*(float *)(DAT_80611d54 + 0x280));
    origin[2] = (float)((double)FLOAT_806180b0 * sinE + (double)*(float *)(DAT_80611d54 + 0x284));

    hit = __ghz_raycast(*(int *)(DAT_80611d54 + 0x2a0), origin,
                        (void *)(DAT_80611d54 + 0x334), 1, hitPos, (float *)0, (int *)0,
                        0x2000070, 0, (int *)0);
    if ((hit != 0) && (*(short *)(hit + 0x94) == 0x557)) {
        DAT_80610e5c[2] = *(int *)(hit + 0x2c);
    }

    state = *DAT_80610e5c;
    if (state == 1) {
        DAT_80610e5c[1] =
            (int)((FLOAT_80618090 /
                   (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                           DOUBLE_806180d0)) *
                      FLOAT_806108ec +
                  (float)DAT_80610e5c[1]);
        if (FLOAT_806180b0 < (float)DAT_80610e5c[1]) {
            *DAT_80610e5c = 2;
        }
    } else if (state < 1) {
        if (-1 < state) {
            DAT_80610e5c[1] =
                (int)-((FLOAT_80618090 /
                        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                                DOUBLE_806180d0)) *
                           FLOAT_806108ec -
                       (float)DAT_80610e5c[1]);
            if ((float)DAT_80610e5c[1] < FLOAT_80618098) {
                DAT_80610e5c[1] = (int)FLOAT_80618098;
            }
            if (hit != 0) {
                *DAT_80610e5c = 1;
            }
        }
    } else if (state < 3) {
        FUN_80221074((double)FLOAT_80618098, sinE, (double)FLOAT_806180b4, (double)FLOAT_80618098,
                     0, 0, 0, 0);
    }

    origin[1] = origin[1] - FLOAT_806180b0;
    if (hit == 0) {
        aimDir[0] = *(float *)(DAT_80611d54 + 0x334);
        aimDir[1] = *(float *)(DAT_80611d54 + 0x338);
        aimDir[2] = *(float *)(DAT_80611d54 + 0x33c);
    } else {
        aimDir[0] = hitPos[0] - origin[0];
        aimDir[1] = hitPos[1] - origin[1];
        aimDir[2] = hitPos[2] - origin[2];
        len2 = aimDir[2] * aimDir[2] + aimDir[0] * aimDir[0] + aimDir[1] * aimDir[1];
        if (FLOAT_806180b8 < len2) {
            invLen = 1.0f / SQRT(len2);
            invLen = invLen * -(FLOAT_806180b0 * len2 * invLen * invLen - FLOAT_806180bc);
            {
                float s = FLOAT_80618090 * invLen *
                          -(FLOAT_806180b0 * len2 * invLen * invLen - FLOAT_806180bc);
                aimDir[0] = aimDir[0] * s;
                aimDir[1] = aimDir[1] * s;
                aimDir[2] = aimDir[2] * s;
            }
        }
    }

    DAT_80612820 = DAT_80612820 + DAT_806108e4;
    frameTick = 3.0f < DAT_80612820;
    if (frameTick) {
        DAT_80612820 = DAT_80612820 + -3.0f;
    }

    held = (unsigned int)input_get_buttons_held(*self);
    if ((held & 0x400) == 0) {
        pressed = (unsigned int)input_get_buttons_pressed(*self);
        if (((pressed & 0x200) != 0) && (frameTick)) {
            origin[0] = (float)((double)origin[0] - mag);
            origin[1] = (float)((double)origin[1] - (double)FLOAT_80618098);
            origin[2] = (float)((double)origin[2] - cosE);
            __ghz_fadeout((double)FLOAT_80618090, (double)FLOAT_806181fc,
                          (int *)&DAT_80610834, (int *)0);
            sound_init_play_params((int)params);
            params[0] = 0xc55;
            DAT_80610834 = sound_play_request(params);
            __ghz_projspawn((double)FLOAT_806180fc, (double)FLOAT_80618098, 4,
                            origin, (int *)aimDir, 0, (int *)0, 0, 0,
                            *(int *)(DAT_80611d54 + 0x2a0), 0, 1);
        }
    } else {
        origin[0] = (float)((double)origin[0] + mag);
        origin[1] = (float)((double)origin[1] + (double)FLOAT_80618098);
        origin[2] = (float)((double)origin[2] + cosE);
        sound_init_play_params((int)params);
        params[0] = 0x4e7;
        sound_play_request(params);
        hit = __ghz_projspawn((double)FLOAT_80618134, (double)FLOAT_80618098,
                              0xb, origin, (int *)aimDir, 0, (int *)0, 0, 0,
                              *(int *)(DAT_80611d54 + 0x2a0), 0, 2);
        if ((hit != 0) && (*DAT_80610e5c == 2)) {
            unsigned int handle = *(unsigned int *)(DAT_80610e5c + 2);
            int target;
            if (handle == 0) {
                target = 0;
            } else {
                target = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
                if (*(unsigned int *)(target + 0x2c) != handle) {
                    *(unsigned int *)(DAT_80610e5c + 2) = 0;
                    target = 0;
                }
            }
            *(int *)(hit + 0x19c) = target;
            *(unsigned int *)(hit + 0xc) = *(unsigned int *)(hit + 0xc) | 0x10000000;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802185d4  ghz_arc_tool_ambient_tick
// Ramps the reactor ambient-hum level toward a target (louder when game flag
// 0x200 is set) and pushes it to the looping 3D sound source (DAT_80612824).
// ---------------------------------------------------------------------------
void ghz_arc_tool_ambient_tick(void)
{
    float target;

    target = FLOAT_80618090;
    if ((DAT_805015c0 & 0x200) != 0) {
        target = FLOAT_806180f8;
    }
    scalar_step_toward_snap((double)target, (double)FLOAT_80618200, &FLOAT_80612828);
    __ghz_snd3d((double)FLOAT_80612828, (double)FLOAT_80618118,
                (double)FLOAT_80618090, &DAT_80612824, 0, 0x20, 0);
}

// ---------------------------------------------------------------------------
// 0x80218640  ghz_reactor_master_tick
// GHZ reactor set-piece master per-frame driver. Ticks every reactor sub-system
// (arc emitter, turret barrage + R110 bot swap, lightning, ambient hum). Then
// handles the player-mount interaction: unless the reactor is unlatched or the
// interaction gate FUN_80220f30(4) is open, it finds the 'RGIB' team entity
// (aiactor_find_entity_by_team), binds it to the reactor (owner+0xe8c), refreshes
// the muzzle, and — if not already in the tool menu state (owner+0x114 != 0x100)
// — enters the first-person arc-tool state (player_enter_menu_state with
// ghz_arc_tool_fire_tick as the update cb). When the mounted body expires and
// the release gates are set, tears down: runs the reactor sequence, exits the
// menu state, clears bindings and render cb. When no mount is active it falls
// back to ghz_arc_body_separation_tick.
// ---------------------------------------------------------------------------
extern int  aiactor_find_entity_by_team(int team, int filter);
extern void player_enter_menu_state(int owner, void *params, void *updateCb,
                                     int a, int b, int c);
extern void player_exit_menu_state(void);
extern void entity_set_render_type(int ent, void *cb);
extern void ghz_reactor_arc_emitter_tick(void);
extern long long ghz_turret_barrage_tick(void);
extern void ghz_r110_weapon_swap_event(long long, double, double, long long,
                                        long long, long long, long long, long long);

extern int   DAT_80612830;   // mounted reactor owner
extern int   DAT_80612834;   // reactor release-enable latch
extern int   DAT_8061283c;   // arc sub-emitter budget / busy count
extern int   DAT_80612838;   // reactor latched flag
extern int   DAT_8061282c;   // meltdown-triggered latch
extern int   FUN_80220f30(int mode);   // reactor interaction gate query

void ghz_reactor_master_tick(long long p1, double p2, double p3, long long p4,
                             long long p5, long long p6, long long p7, long long p8)
{
    unsigned int gate;
    int entity;
    long long swapArg;
    int params[13];   // local_38

    ghz_reactor_arc_emitter_tick();
    swapArg = ghz_turret_barrage_tick();
    ghz_r110_weapon_swap_event(swapArg, p2, p3, p4, p5, p6, p7, p8);
    ghz_weather_lightning_tick();
    ghz_arc_tool_ambient_tick();

    if (((DAT_80612838 == 0) || (DAT_80612830 == 0)) ||
        (gate = (unsigned int)FUN_80220f30(4), gate != 0)) {
        ghz_arc_body_separation_tick();
    } else {
        entity = aiactor_find_entity_by_team(0x42494752, 3);   // 'RGIB'
        if (entity != 0) {
            *(int *)(DAT_80612830 + 0xe8c) = entity;
            if (FLOAT_80618098 <= *(float *)(entity + 0x1cc)) {
                DAT_8061282c = 0;
            }
            ghz_arc_tool_muzzle_update(entity);
            if (*(int *)(DAT_80612830 + 0x114) != 0x100) {
                FLOAT_80612818 = FLOAT_80618098;
                FLOAT_8061281c = FLOAT_80618098;
                sound_init_play_params((int)params);
                params[0] = 0x4e7;
                sound_play_request(params);
                player_enter_menu_state(DAT_80612830, &DAT_805c6cd0,
                                        (void *)0x80217f2c, 4, 0x26, 0);
            }
        }
        if (entity != 0) {
            if (((FLOAT_80618098 <= *(float *)(entity + 0x1cc)) && (DAT_80612834 != 0)) &&
                (DAT_8061283c == 0)) {
                FUN_80221074((double)FLOAT_80618098, p2, p3, p4, p5, p6, p7, p8);
                if (*(int *)(DAT_80612830 + 0x114) == 0x100) {
                    player_exit_menu_state();
                }
                *(int *)(DAT_80612830 + 0xe8c) = 0;
                DAT_80612838 = 0;
                DAT_80612830 = 0;
                entity_set_render_type(entity, (void *)0);
            }
        }
    }
}
