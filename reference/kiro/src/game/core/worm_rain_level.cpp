// worm_rain_level.cpp
// Rain/storm level set-piece driver (entity class range 0x4c4..0x4ca). Distinct
// from the GHZ reactor cluster: this level's setup wires ~50 soundpad zones,
// registers two actor tables, and drives a random rain-splash impact effect plus
// an ambient 3D loop. Also handles portal openers and the object-init dispatcher.
//
// Batch: batch_661  (addresses 0x802187d8 .. 0x802195a0)

#include "common.h"

// ---------------------------------------------------------------------------
// File-local typed aliases for shared helpers whose generated_decls signature
// is void-return / mismatched-arity versus the ABI usage at these call sites
// (bound by ROM address per the Compile-Clean Standard).
// ---------------------------------------------------------------------------
typedef int  (*rng_tick_fp)(void);
typedef int  (*raycast_fp)(int, void *, void *, int, float *, float *, int *,
                           unsigned int, unsigned int, int *);
typedef void (*snd3d_fp)(double, double, double, int, int, int, int);
typedef void (*preload_fp)(long long, double, double, long long, long long,
                           long long, long long, long long, int);
static const rng_tick_fp   __rl_rng      = (rng_tick_fp)0x8013104c;   // rng_counter_tick
static const raycast_fp    __rl_raycast  = (raycast_fp)0x8005bb44;    // scene_raycast
static const snd3d_fp      __rl_snd3d    = (snd3d_fp)0x800102e4;      // sound_update_3d_source
static const preload_fp    __rl_preload  = (preload_fp)0x80070ba8;    // entity_preload_resources

// ---------------------------------------------------------------------------
// Named cross-module helpers resolved through generated_decls.h (not redeclared):
//   scene_node_get_render_data, memset_wrapper, level_actors_register,
//   level_resource_pool_setup, soundpad_find_zone_by_id, soundpad_find_active_portal,
//   sound_init_play_params, sound_play_request, sound_clear_bank_ref,
//   player_ai_clear_footstep_sounds, floor_height_from_position,
//   collision_result_restore, entity_register_for_update, ghz_level_particle_setup.
// ---------------------------------------------------------------------------
extern long long ghz_level_particle_setup(void);
extern void  memset_wrapper(int dst, int val, int n);
extern long long level_actors_register(long long, double, double, long long,
                                        long long, long long, long long,
                                        long long, int slot, void *table, int n);
extern void  level_resource_pool_setup(long long, double, double, long long,
                                        long long, long long, long long, long long);
extern int   soundpad_find_zone_by_id(int id);
extern int  *soundpad_find_active_portal(int *zoneA, int *zoneB);
extern void  sound_init_play_params(int params);
extern void  sound_clear_bank_ref(int bank);
extern void  player_ai_clear_footstep_sounds(void);
extern void  collision_result_restore(int out);
extern void  entity_register_for_update(int ent, int slot, int cb);

// player_ai_explosion_spawn has a spawn-simple sibling; the shared decl differs
// in return/args from this call site, so it is bound by address here.
typedef int (*explosionspawn_fp)(int, int, float *, float *, int, int, int);
static const explosionspawn_fp __rl_explosion_spawn = (explosionspawn_fp)0x800c95d8; // player_ai_explosion_spawn

// scene_node_get_render_data returns void* in the shared header.
extern void *scene_node_get_render_data(int id);
// sound_play_request returns unsigned int in the shared header.
extern unsigned int sound_play_request(int *params);

// batch_662 additions: named helpers resolved via generated_decls.h
//   soundpad_resolve_zone_node, soundpad_get_entity_zone_id,
//   soundpad_get_entity_id_from_zone, entity_destroy_with_event,
//   entity_build_local_matrix, mtx4_transform_point_alias, fluid_splat_stub_zero.
extern int  *soundpad_resolve_zone_node(int zoneId);
extern int   soundpad_get_entity_zone_id(int padEntry);
extern int   soundpad_get_entity_id_from_zone(int zoneId);
extern void  entity_destroy_with_event(int *entity);
extern void  entity_build_local_matrix(void *out, int ent, int includePos, unsigned int mirror);
extern void  mtx4_transform_point_alias(float *mtx, float *pt);
extern int   fluid_splat_stub_zero(void);

// mem_alloc shared decl is (size,type); the ABI call here is the wide 16-arg
// form, so it is bound by ROM address. sound_register_fade_out's 4th arg is
// u32* in the shared header; bound here too for the (int*)0 call form.
typedef int  (*memalloc_fp)(long long, double, double, long long, long long,
                            long long, long long, long long, int, int, int, int,
                            int, int, int, int);
typedef void (*fadeout_fp)(double, double, int *, int *);
static const memalloc_fp __rl_mem_alloc = (memalloc_fp)0x80006c44;   // mem_alloc
static const fadeout_fp  __rl_fadeout   = (fadeout_fp)0x8001063c;    // sound_register_fade_out

// ---------------------------------------------------------------------------
// File-local externs for raw ROM symbols.
// ---------------------------------------------------------------------------
extern void FUN_803826e4(void);   // current-entity push (scoped iterate)
extern void FUN_80382730(void);   // current-entity pop
extern long long FUN_803826f0(void); // scoped-entity accessor (ptr|index packed r3:r4)
extern void FUN_8038273c(void);   // scoped-entity pop (paired with FUN_803826f0)
extern double FUN_80390324(double x); // floor/modf helper
extern double math_atan2(double y, double x); // not in generated_decls
extern int  floor_height_from_position(double x, double z); // not in generated_decls

// batch_662 raw data / constants
extern int    DAT_80612840;               // wind ambient state block (0x10)
extern int    DAT_80612848;               // one-shot SFX handle table (0x40)
extern int    DAT_80612610;               // active player count
extern int    DAT_80611d78;               // player-record array base
extern float  FLOAT_806108ec;             // frame-time scale
extern double DOUBLE_80618268;
extern double DOUBLE_80618288;
extern float  FLOAT_80618250;
extern float  FLOAT_80618254;
extern float  FLOAT_80618258;
extern float  FLOAT_8061825c;
extern float  FLOAT_80618260;
extern float  FLOAT_80618270;
extern float  FLOAT_80618274;
extern float  FLOAT_80618278;
extern float  FLOAT_8061827c;
extern float  FLOAT_80618280;
extern float  FLOAT_80618290;
extern float  FLOAT_80618294;
extern float  FLOAT_80618298;
extern float  FLOAT_8061829c;
extern float  FLOAT_806182a0;
extern float  FLOAT_806182a4;

// Raw data / constant symbols
extern int    DAT_80611b8c;               // soundpad zone array base
extern int    DAT_80611d54;               // active player camera record
extern unsigned int DAT_806109c0;         // frame delta (raw)
extern int    DAT_806108e4;               // rain-splash spawn probability step
extern unsigned char DAT_805015bd;        // story progress byte
extern int    DAT_805015fc;               // secondary story flag
extern int    DAT_80612824;               // ambient 3D source handle
extern int    DAT_8061283c;               // reactor budget (shared, reset here)
extern int    DAT_80612838;               // reactor latch (shared, reset here)
extern int    DAT_80612834;               // reactor release latch (shared)
extern int    DAT_80612830;               // reactor owner (shared)
extern float  FLOAT_80612828;             // ambient hum level (reset here)
extern int    DAT_80459e34;               // actor table 0
extern int    DAT_80459f4c;               // actor table 1
extern int    DAT_8045a908;               // portal-pair table A (entry+8 stride)
extern int    DAT_8045a90c;
extern int    DAT_8045a960;               // portal-pair table B
extern int    DAT_8045a964;
extern float  DAT_80583228;               // sine LUT (0x800 entries)
extern float  DAT_803dff80, DAT_803dff84, DAT_803dff88; // splash base pos

extern float  FLOAT_80618098;   // 0.0f
extern float  FLOAT_80618208;
extern float  FLOAT_8061820c;
extern float  FLOAT_80618210;
extern float  FLOAT_80618214;
extern float  FLOAT_80618218;
extern float  FLOAT_8061821c;
extern float  FLOAT_80618220;
extern float  FLOAT_80618224;
extern float  FLOAT_80618228;
extern float  FLOAT_8061822c;
extern float  FLOAT_80618230;
extern float  FLOAT_80618234;
extern float  FLOAT_80618238;
extern double DOUBLE_80618240;
extern double DOUBLE_80618248;

// ---------------------------------------------------------------------------
// 0x80218df8  rainlvl_ambient_sound_tick  (class 0x4ca update cb, slot 3)
// Updates the level's ambient 3D loop (bank 0x1572) at the entity position
// (ent+0x22c orientation, ent+0x64 world pos).
// ---------------------------------------------------------------------------
void rainlvl_ambient_sound_tick(int ent)
{
    __rl_snd3d((double)FLOAT_80618208, (double)FLOAT_8061820c, (double)FLOAT_80618210,
               ent + 0x22c, ent + 100, 0x1572, 0);
}

// ---------------------------------------------------------------------------
// 0x80218e38  rainlvl_rain_splash_tick
// Random rain-splash impact effect. On a randomised per-frame cadence, picks a
// random point around the player, finds the floor scene node, raycasts down to
// a surface, and — if far enough from the camera and not occluded — spawns a
// splash "explosion" (type 0xc, or 0xe over water surface local_74==1) plus the
// splash sound (bank 0x1571).
// ---------------------------------------------------------------------------
void rainlvl_rain_splash_tick(void)
{
    unsigned int r;
    unsigned int cadence;
    float dx, dy, dz;
    int   node;
    int   los;
    int   expType;
    // collision_result_restore fills this block; the surface-kind word lives at
    // offset 0x18 (local_74 in the original frame, immediately after auStack_8c).
    unsigned char splashCtx[0x1c]; // auStack_8c (24) + surfKind (4)
    int  *surfKind = (int *)&splashCtx[0x18];   // local_74
    int   sndParams[7];          // local_6c
    float sndX, sndY, sndZ;      // local_50/4c/48
    unsigned char sndFlag;       // local_44
    float sndVol;                // local_40
    float rayOrigin[3];          // local_b0, local_ac(as float), local_a8
    float rayDir[3];             // local_bc, local_b8, local_b4
    float hitPos[3];             // local_98, local_94, local_90
    float hitNorm[3];            // local_a4, local_a0, local_9c
    double t;

    r = (unsigned int)__rl_rng();
    cadence = (unsigned int)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                             DOUBLE_80618240);
    if (r - (r / cadence) * cadence < (unsigned int)DAT_806108e4) {
        rayOrigin[0] = DAT_803dff80;
        rayOrigin[1] = DAT_803dff84;
        rayOrigin[2] = DAT_803dff88;

        {
            unsigned int rr = (unsigned int)__rl_rng();
            t = (double)(FLOAT_80618208 * FLOAT_80618214 *
                         ((float)((double)CONCAT44(0x43300000, rr) - DOUBLE_80618248) /
                          FLOAT_80618218));
        }
        {
            unsigned int rr = (unsigned int)__rl_rng();
            unsigned int lut = (unsigned int)((double)FLOAT_80618224 * t + (double)FLOAT_80618220);
            float amp = FLOAT_8061821c *
                        ((float)((double)CONCAT44(0x43300000, rr) - DOUBLE_80618248) /
                         FLOAT_80618218);
            rayOrigin[2] = amp * (&DAT_80583228)[lut & 0x7ff] + rayOrigin[2];
            rayOrigin[0] = amp * *(float *)((int)&DAT_80583228 + ((lut + 0x200) * 4 & 0x1ffc)) +
                           rayOrigin[0];
        }

        node = floor_height_from_position((double)FLOAT_80618210, (double)FLOAT_80618220);
        rayDir[0] = FLOAT_80618228;
        rayDir[1] = FLOAT_8061822c;
        rayDir[2] = FLOAT_80618228;
        if ((0 < node) &&
            (los = __rl_raycast(node, rayOrigin, rayDir, 0, hitPos, hitNorm, &node, 0x2010018, 0,
                                (int *)0),
             los != 0) &&
            (FLOAT_80618230 < hitNorm[1]) &&
            (dx = hitPos[0] - *(float *)(DAT_80611d54 + 0x27c),
             dy = hitPos[1] - *(float *)(DAT_80611d54 + 0x280),
             dz = hitPos[2] - *(float *)(DAT_80611d54 + 0x284),
             FLOAT_80618234 < dz * dz + dx * dx + dy * dy)) {
            rayDir[0] = FLOAT_80618228;
            rayDir[1] = FLOAT_80618210;
            rayDir[2] = FLOAT_80618228;
            los = __rl_raycast(node, hitPos, rayDir, 1, (float *)0, (float *)0, (int *)0,
                               0x2010018, 0, (int *)0);
            if (los == 0) {
                expType = 0xc;
                collision_result_restore((int)splashCtx);
                hitPos[0] = FLOAT_80618230 * hitNorm[0] + hitPos[0];
                hitPos[1] = FLOAT_80618230 * hitNorm[1] + hitPos[1];
                hitPos[2] = FLOAT_80618230 * hitNorm[2] + hitPos[2];
                if (*surfKind == 1) {
                    expType = 0xe;
                }
                __rl_explosion_spawn(expType, 0, hitPos, hitNorm, 0, 0, 1);
                sound_init_play_params((int)sndParams);
                sndFlag = 1;
                sndParams[0] = 0x1571;
                sndVol = FLOAT_80618238;
                sndX = hitPos[0];
                sndY = hitPos[1];
                sndZ = hitPos[2];
                (void)sndFlag; (void)sndVol; (void)sndX; (void)sndY; (void)sndZ;
                sound_play_request(sndParams);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80218e38 is invoked through this render wrapper.
// 0x80219194  rainlvl_rain_splash_render
// ---------------------------------------------------------------------------
void rainlvl_rain_splash_render(void)
{
    rainlvl_rain_splash_tick();
}

// ---------------------------------------------------------------------------
// 0x802187d8  rainlvl_level_setup
// Level initialiser: forces the two rain layers on (scene node flags
// +0x29ba/+0x2a1a bit0), runs particle setup, registers two actor tables, sets
// up the resource pool, then flags a large batch of soundpad zones as reverb/
// ambient (0x10000 = small, 0x20000 = large) and resets the shared reactor
// globals for a clean level start.
// ---------------------------------------------------------------------------
void rainlvl_level_setup(long long p1, double p2, double p3, long long p4,
                         long long p5, long long p6, long long p7, long long p8)
{
    int rd;
    int node;
    long long chain;

    rd = (int)(intptr_t)scene_node_get_render_data(0x71);
    node = *(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14);
    *(unsigned char *)(node + 0x29ba) = *(unsigned char *)(node + 0x29ba) | 1;
    node = *(int *)(*(int *)(*(int *)(rd + 4) + 4) + 0x14);
    *(unsigned char *)(node + 0x2a1a) = *(unsigned char *)(node + 0x2a1a) | 1;

    chain = ghz_level_particle_setup();
    memset_wrapper(-0x7fa39380, 0, 0xc);   // 0x805c6c80
    chain = level_actors_register(chain, p2, p3, p4, p5, p6, p7, p8, 0, &DAT_80459e34, 7);
    chain = level_actors_register(chain, p2, p3, p4, p5, p6, p7, p8, 1, &DAT_80459f4c, 7);
    level_resource_pool_setup(chain, p2, p3, p4, p5, p6, p7, p8);

    // Soundpad zone reverb/ambient flags. 0x20000 = large-space, 0x10000 = small.
    static const struct { int id; unsigned int flag; } zones[] = {
        {0x72b, 0x20000}, {0x71f, 0x10000}, {0x71e, 0x10000}, {0x732, 0x10000},
        {0x729, 0x20000}, {0x71d, 0x20000}, {0x727, 0x20000}, {0x72f, 0x20000},
        {0x71a, 0x10000}, {0x71b, 0x10000}, {0x726, 0x10000}, {0x724, 0x10000},
        {0x723, 0x10000}, {0x72d, 0x10000}, {0x731, 0x10000}, {0x730, 0x10000},
        {0x73d, 0x20000}, {0x73a, 0x20000}, {0x643, 0x20000}, {0x628, 0x20000},
        {0x624, 0x20000}, {0x741, 0x20000}, {0x745, 0x20000}, {0x634, 0x20000},
        {0x633, 0x20000}, {0x406, 0x20000}, {0x75a, 0x10000}, {0x754, 0x20000},
        {0x757, 0x20000}, {0x752, 0x20000}, {0x74f, 0x20000}, {0x74e, 0x20000},
        {0x74c, 0x20000}, {0x661, 0x20000}, {0x74a, 0x20000}, {0x748, 0x20000},
        {0x774, 0x10000}, {0x773, 0x10000}, {0x770, 0x10000}, {0x771, 0x10000},
        {0x77b, 0x10000}, {0x77c, 0x10000},
    };
    for (int i = 0; i < (int)(sizeof(zones) / sizeof(zones[0])); i = i + 1) {
        int z = DAT_80611b8c + soundpad_find_zone_by_id(zones[i].id) * 0x20;
        *(unsigned int *)(z + 4) = *(unsigned int *)(z + 4) | zones[i].flag;
    }

    DAT_8061283c = 0;
    DAT_80612838 = 0;
    DAT_80612834 = 0;
    DAT_80612830 = 0;
    DAT_80612824 = -1;
    FLOAT_80612828 = FLOAT_80618098;
}

// ---------------------------------------------------------------------------
// 0x8021913c  rainlvl_preload
// Preloads entity class 0x4ca and clears the rain-level sound banks + footstep
// sound cache.
// ---------------------------------------------------------------------------
void rainlvl_preload(long long p1, double p2, double p3, long long p4, long long p5,
                     long long p6, long long p7, long long p8)
{
    __rl_preload(p1, p2, p3, p4, p5, p6, p7, p8, 0x4ca);
    sound_clear_bank_ref(0x1c);
    sound_clear_bank_ref(0x1d);
    sound_clear_bank_ref(0x1572);
    sound_clear_bank_ref(0x1a7);
    sound_clear_bank_ref(0x1a6);
    sound_clear_bank_ref(0x1e6);
    player_ai_clear_footstep_sounds();
}

// ---------------------------------------------------------------------------
// 0x802191b4  rainlvl_soundzone_setup_b
// A second, smaller soundpad-zone flag pass (zones 0x477..0x485).
// ---------------------------------------------------------------------------
void rainlvl_soundzone_setup_b(void)
{
    static const struct { int id; unsigned int flag; } zones[] = {
        {0x477, 0x20000}, {0x478, 0x20000}, {0x479, 0x20000}, {0x47a, 0x20000},
        {0x47f, 0x10000}, {0x481, 0x20000}, {0x483, 0x20000}, {0x485, 0x20000},
    };
    for (int i = 0; i < (int)(sizeof(zones) / sizeof(zones[0])); i = i + 1) {
        int z = DAT_80611b8c + soundpad_find_zone_by_id(zones[i].id) * 0x20;
        *(unsigned int *)(z + 4) = *(unsigned int *)(z + 4) | zones[i].flag;
    }
}

// ---------------------------------------------------------------------------
// 0x802192d4  rainlvl_open_portals_a
// Opens 11 soundpad portal connections (clears the closed bit 0x10000) between
// zone pairs listed in DAT_8045a908/0x8045a90c (8-byte stride).
// ---------------------------------------------------------------------------
void rainlvl_open_portals_a(void)
{
    int i;
    int off;

    FUN_803826e4();
    i = 0;
    off = 0;
    do {
        int zoneB = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045a90c + off));
        int base = DAT_80611b8c;
        int zoneA = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045a908 + off));
        unsigned int *portal = (unsigned int *)soundpad_find_active_portal(
            (int *)(DAT_80611b8c + zoneA * 0x20), (int *)(base + zoneB * 0x20));
        if (portal != (unsigned int *)0) {
            *portal = *portal & 0xfffeffff;
        }
        i = i + 1;
        off = off + 8;
    } while (i < 0xb);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80219364  rainlvl_open_portals_b
// Opens 5 soundpad portal connections from the DAT_8045a960/0x8045a964 pair
// table (variant of rainlvl_open_portals_a for a later story state).
// ---------------------------------------------------------------------------
void rainlvl_open_portals_b(void)
{
    int i;
    int off;

    FUN_803826e4();
    i = 0;
    off = 0;
    do {
        int zoneB = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045a964 + off));
        int base = DAT_80611b8c;
        int zoneA = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045a960 + off));
        unsigned int *portal = (unsigned int *)soundpad_find_active_portal(
            (int *)(DAT_80611b8c + zoneA * 0x20), (int *)(base + zoneB * 0x20));
        if (portal != (unsigned int *)0) {
            *portal = *portal & 0xfffeffff;
        }
        i = i + 1;
        off = off + 8;
    } while (i < 5);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x802193f4  rainlvl_object_init
// Object-init dispatcher for the rain-level entity classes 0x4c4..0x4ca. Most
// classes set the animation-clip triple (+0x38/+0x3c/+0x40[/+0x44]) and render
// flag 0x20000 with a clip count (ent+0x30). Class 0x4ca instead registers the
// ambient-sound tick callback (rainlvl_ambient_sound_tick @0x80218df8).
// ---------------------------------------------------------------------------
void rainlvl_object_init(int ent)
{
    unsigned short cls;

    cls = *(unsigned short *)(ent + 0x94);
    if (cls == 0x4c8) {
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x20000;
        *(int *)(ent + 0x38) = 0x12;
        *(int *)(ent + 0x3c) = 0x10;
        *(int *)(ent + 0x40) = 0xf;
        *(int *)(ent + 0x34) = *(int *)(ent + 0x38);
        *(unsigned char *)(ent + 0x30) = 3;
    } else if (cls < 0x4c8) {
        if (cls == 0x4c6) {
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x20000;
            *(int *)(ent + 0x38) = 5;
            *(int *)(ent + 0x3c) = 0x14;
            *(int *)(ent + 0x40) = 4;
            *(int *)(ent + 0x44) = 0x15;
            *(int *)(ent + 0x34) = *(int *)(ent + 0x38);
            *(unsigned char *)(ent + 0x30) = 4;
        } else if (cls < 0x4c6) {
            if (0x4c4 < cls) {
                *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x20000;
                *(int *)(ent + 0x38) = 6;
                *(int *)(ent + 0x3c) = 0xf;
                *(int *)(ent + 0x40) = 0x19;
                *(int *)(ent + 0x44) = 0xd;
                *(int *)(ent + 0x34) = *(int *)(ent + 0x38);
                *(unsigned char *)(ent + 0x30) = 4;
            }
        } else {
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x20000;
            *(int *)(ent + 0x38) = 3;
            *(int *)(ent + 0x3c) = 0xc;
            *(int *)(ent + 0x40) = 10;
            *(int *)(ent + 0x44) = 2;
            *(int *)(ent + 0x34) = *(int *)(ent + 0x38);
            *(unsigned char *)(ent + 0x30) = 4;
        }
    } else if (cls == 0x4ca) {
        *(int *)(ent + 0x34) = 0xd5;
        *(unsigned char *)(ent + 0x30) = 1;
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40000000;
        entity_register_for_update(ent, 3, (int)0x80218df8);   // rainlvl_ambient_sound_tick
    } else if (cls < 0x4ca) {
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x20000;
        *(int *)(ent + 0x38) = 0x15;
        *(int *)(ent + 0x3c) = 0x16;
        *(int *)(ent + 0x40) = 0x17;
        *(int *)(ent + 0x34) = *(int *)(ent + 0x38);
        *(unsigned char *)(ent + 0x30) = 3;
    }
}

// ---------------------------------------------------------------------------
// 0x802195a0  rainlvl_door_event
// Door/gate scripted event: plays sound 0x1c, then opens the appropriate set of
// soundpad portals based on story progress (DAT_805015bd==3 -> table A, else if
// secondary flag DAT_805015fc set -> table B).
// ---------------------------------------------------------------------------
void rainlvl_door_event(void)
{
    int params[14];

    sound_init_play_params((int)params);
    params[0] = 0x1c;
    sound_play_request(params);
    if (DAT_805015bd == '\x03') {
        rainlvl_open_portals_a();
    } else if (DAT_805015fc != 0) {
        rainlvl_open_portals_b();
    }
}

// ===========================================================================
// batch_662: rain-level wind-ambient + one-shot SFX scheduler + misc effect
// ticks (0x80219600 .. 0x80219b9c).
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x80219600  rainlvl_wind_alloc
// Allocates the 0x10-byte wind-ambient state block (DAT_80612840) and clears
// the wind sound-bank reference (0x18).
// ---------------------------------------------------------------------------
void rainlvl_wind_alloc(long long p1, double p2, double p3, long long p4, long long p5,
                        long long p6, long long p7, long long p8, int a9, int a10,
                        int a11, int a12, int a13, int a14, int a15, int a16)
{
    DAT_80612840 = __rl_mem_alloc(p1, p2, p3, p4, p5, p6, p7, p8, 0x10, 1, a11, a12,
                                  a13, a14, a15, a16);
    sound_clear_bank_ref(0x18);
}

// ---------------------------------------------------------------------------
// 0x80219634  rainlvl_wind_init
// Zeroes the wind-ambient block and sets its 3D-source handle (+4) to -1.
// ---------------------------------------------------------------------------
void rainlvl_wind_init(void)
{
    memset_wrapper(DAT_80612840, 0, 8);
    *(int *)(DAT_80612840 + 4) = -1;
}

// ---------------------------------------------------------------------------
// 0x8021966c  rainlvl_zone_is_windy
// Predicate: is the given soundpad zone exposed to wind? Resolves the zone's
// node and switches on its zone type; several types are unconditionally windy
// (0xd/0xe/0xf/0x10/0x1d), others are windy only for specific entity-id ranges
// (e.g. type 5 -> [0x446,0x44d], type 0x12 -> [0x578,0x57e]). Type 0x1e returns
// (zone-id == 0x466) via countLeadingZeros trick.
// ---------------------------------------------------------------------------
unsigned int rainlvl_zone_is_windy(int zoneId)
{
    int *node;
    int zoneType;
    int id;
    unsigned int windy;

    node = soundpad_resolve_zone_node(zoneId);
    zoneType = soundpad_get_entity_zone_id((int)node);
    switch (zoneType) {
    case 5:
        id = soundpad_get_entity_id_from_zone(zoneId);
        windy = ((id < 0x44e) && (0x445 < id)) ? 1u : 0u;
        break;
    default:
        windy = 0;
        break;
    case 0xb:
        id = soundpad_get_entity_id_from_zone(zoneId);
        windy = ((id < 0x434) && (0x431 < id)) ? 0u : 1u;
        break;
    case 0xd:
    case 0xe:
    case 0xf:
    case 0x10:
    case 0x1d:
        windy = 1;
        break;
    case 0x12:
        id = soundpad_get_entity_id_from_zone(zoneId);
        windy = ((id < 0x57f) && (0x577 < id)) ? 1u : 0u;
        break;
    case 0x13:
        id = soundpad_get_entity_id_from_zone(zoneId);
        windy = ((id < 0x50f) && (0x507 < id)) ? 1u : 0u;
        break;
    case 0x1a:
        id = soundpad_get_entity_id_from_zone(zoneId);
        windy = ((id < 0x56b) && (0x566 < id)) ? 1u : 0u;
        break;
    case 0x1e:
        id = soundpad_get_entity_id_from_zone(zoneId);
        windy = (unsigned int)countLeadingZeros(0x466 - id) >> 5;
        break;
    }
    return windy;
}

// ---------------------------------------------------------------------------
// 0x802197c0  rainlvl_wind_ambient_tick
// Ramps the wind ambient volume up while every active player is in a windy
// zone (rainlvl_zone_is_windy over each player's current zone), down otherwise,
// clamped to [FLOAT_80618258, FLOAT_8061825c]. Pushes the level to the wind 3D
// loop source (bank 0x18) held in DAT_80612840.
// ---------------------------------------------------------------------------
void rainlvl_wind_ambient_tick(void)
{
    unsigned int allWindy;
    unsigned int windy;
    int i;
    int off;
    float lo, hi;
    float delta;
    float vol;
    float clamped;

    FUN_803826f0();
    allWindy = 1;
    i = 0;
    off = 0;
    while ((hi = FLOAT_8061825c, lo = FLOAT_80618258, i < DAT_80612610 && (allWindy != 0))) {
        if (-1 < *(int *)(DAT_80611d78 + off + 4)) {
            windy = rainlvl_zone_is_windy(
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c) + 0x678));
            allWindy = allWindy & windy;
        }
        i = i + 1;
        off = off + 0x141c;
    }

    if (allWindy == 0) {
        delta = -((FLOAT_80618250 /
                   (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                           DOUBLE_80618268)) /
                  FLOAT_80618254);
    } else {
        delta = (FLOAT_80618250 /
                 (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                         DOUBLE_80618268)) /
                FLOAT_80618254;
    }
    *(float *)DAT_80612840 = *(float *)DAT_80612840 + delta;
    vol = *(float *)DAT_80612840;
    clamped = vol;
    if (hi < vol) {
        clamped = hi;
    }
    hi = FLOAT_80618258;
    if ((lo <= clamped) && (hi = vol, FLOAT_8061825c < vol)) {
        hi = FLOAT_8061825c;
    }
    *(float *)DAT_80612840 = hi;
    __rl_snd3d((double)(float)((double)FLOAT_80618250 - (double)*(float *)DAT_80612840),
               (double)FLOAT_80618260, (double)FLOAT_80618250, DAT_80612840 + 1, 0, 0x18, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80219930  rainlvl_wind_ambient_render  (wrapper -> rainlvl_wind_ambient_tick)
// ---------------------------------------------------------------------------
void rainlvl_wind_ambient_render(void)
{
    rainlvl_wind_ambient_tick();
}

// ---------------------------------------------------------------------------
// 0x80219984  rainlvl_sfx_alloc
// Allocates the 0x40-byte one-shot SFX handle table (DAT_80612848).
// ---------------------------------------------------------------------------
void rainlvl_sfx_alloc(long long p1, double p2, double p3, long long p4, long long p5,
                       long long p6, long long p7, long long p8, int a9, int a10, int a11,
                       int a12, int a13, int a14, int a15, int a16)
{
    DAT_80612848 = __rl_mem_alloc(p1, p2, p3, p4, p5, p6, p7, p8, 0x40, 1, a11, a12,
                                  a13, a14, a15, a16);
}

// ---------------------------------------------------------------------------
// 0x802199b0  rainlvl_spin_tick  (entity update cb)
// Advances a spinning prop's angle at ent+0x70 by rate ent+0xd0 * frame-time,
// wrapping into [FLOAT_80618270, +period) via the floor helper FUN_80390324.
// ---------------------------------------------------------------------------
void rainlvl_spin_tick(int ent)
{
    double angle;
    double wrapped;

    angle = (double)(*(float *)(ent + 0x70) + *(float *)(ent + 0xd0) * FLOAT_806108ec);
    wrapped = FUN_80390324((double)((float)(angle - (double)FLOAT_80618270) / FLOAT_80618274));
    *(float *)(ent + 0x70) = -(float)((double)FLOAT_80618274 * (double)(float)wrapped - angle);
}

// ---------------------------------------------------------------------------
// 0x80219a20  rainlvl_debris_expire_tick  (render cb)
// Counts a debris/prop lifetime timer at obj[0x73] down; on expiry plays a
// random one-shot from the bank starting at 0xdcd (at the object position) and
// destroys the object.
// ---------------------------------------------------------------------------
void rainlvl_debris_expire_tick(unsigned int *ctx)
{
    int sample;
    unsigned int *obj;
    int params[7];    // local_48
    int posX, posY, posZ;   // local_2c/28/24
    unsigned char flag;     // local_20

    obj = (unsigned int *)*ctx;
    obj[0x73] = (unsigned int)(int)(*(float *)&obj[0x73] - *(float *)&ctx[8]);
    if (*(float *)&obj[0x73] <= FLOAT_80618270) {
        {
            unsigned int r = (unsigned int)__rl_rng();
            sample = (int)(FLOAT_8061827c *
                           ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80618288) /
                            FLOAT_80618280) +
                           FLOAT_80618278);
        }
        sound_init_play_params((int)params);
        flag = 1;
        posX = obj[0x19];
        posY = obj[0x1a];
        posZ = obj[0x1b];
        params[0] = sample + 0xdcd;
        (void)flag; (void)posX; (void)posY; (void)posZ;
        sound_play_request(params);
        entity_destroy_with_event((int *)obj);
    }
}

// ---------------------------------------------------------------------------
// 0x80219af0  rainlvl_sfx_play_indexed
// Plays an indexed one-shot SFX for the current scoped entity: fades out the
// previous handle in the table slot (DAT_80612848 + index*4), then plays sound
// (0x66e + index) at the entity position and stores the new handle.
// ---------------------------------------------------------------------------
void rainlvl_sfx_play_indexed(void)
{
    long long scoped;
    int index;
    unsigned int *ent;
    int slotOff;
    unsigned int handle;
    int params[4];      // local_48
    unsigned int flags; // local_38
    float vol;          // local_30
    int posX, posY, posZ;   // local_2c/28/24
    unsigned char flag; // local_20
    float pitch;        // local_1c

    scoped = FUN_803826f0();
    index = (int)((unsigned long long)scoped >> 0x20);
    ent = (unsigned int *)(int)scoped;
    slotOff = index * 4;

    __rl_fadeout((double)FLOAT_80618290, (double)FLOAT_80618294,
                 (int *)(DAT_80612848 + slotOff), (int *)0);
    sound_init_play_params((int)params);
    params[0] = index + 0x66e;
    flags = 0xa0;
    flag = 1;
    pitch = FLOAT_80618298;
    vol = FLOAT_80618290;
    posX = ent[0];
    posY = ent[1];
    posZ = ent[2];
    (void)flags; (void)flag; (void)pitch; (void)vol; (void)posX; (void)posY; (void)posZ;
    handle = sound_play_request(params);
    *(unsigned int *)(DAT_80612848 + slotOff) = handle;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80219b9c  rainlvl_bone_aim_orient
// Computes a bone-relative aim orientation. Fetches the scoped entity's target
// bone vertex, transforms it into local space (entity_build_local_matrix +
// mtx4_transform_point_alias), then sets the output orientation: yaw from
// atan2(target.z - bone.z, target.x - bone.x) minus a lean term proportional to
// the entity's spin angle (+0x70); pitch/base from constants.
// ---------------------------------------------------------------------------
void rainlvl_bone_aim_orient(int a1, int a2, float *outOrient, float *target)
{
    long long scoped;
    int ent;
    int *skel;
    float *vtx;
    float bone[3];       // local_68, local_64, local_60
    float localMtx[23];  // afStack_5c
    double yaw;

    scoped = FUN_803826f0();
    ent = (int)((unsigned long long)scoped >> 0x20);
    skel = *(int **)(*(int *)(ent + 4) + 4);
    vtx = (float *)(skel[*skel * -0x30 + ((int)(unsigned int)scoped >> 0x10) * 0x30 + 0x1b] +
                    ((unsigned int)scoped & 0xffff) * 0x18);
    bone[0] = vtx[0];
    bone[1] = vtx[1];
    bone[2] = vtx[2];
    entity_build_local_matrix(localMtx, ent, 1, 0);
    mtx4_transform_point_alias(localMtx, bone);
    fluid_splat_stub_zero();
    yaw = math_atan2((double)(target[2] - bone[2]), (double)(target[0] - bone[0]));
    outOrient[1] = (float)yaw;
    outOrient[1] = (float)outOrient[1] -
                   (FLOAT_8061829c * *(float *)(ent + 0x70)) / FLOAT_806182a0;
    outOrient[0] = FLOAT_806182a4;
    (void)a1; (void)a2;
    FUN_8038273c();
}

// ===========================================================================
// batch_663: rain-level bone-aim'd multi-part mechanism (entity class 0xb7) —
// per-part aim dispatch, random-part spawn tick, trig-driven limb rotation /
// animation / settle, the mechanism object-init dispatcher, SFX-table init and
// a bot-link event handler (0x80219c9c .. 0x8021a524).
//
// This cluster animates a many-jointed rain-level prop: several "parts"
// (indices 7/8/9/10) are aimed at a target via rainlvl_bone_aim_orient (batch_
// 662), while four limbs are rotated about their bones by a shared oscillating
// angle FLOAT_8061284c.
// ===========================================================================

// batch_663 named helpers. These live in generated_decls.h but that header is
// not pulled into this TU, so (matching the batch_661/662 convention) they are
// declared as file-local externs with signatures matching the call sites.
extern double math_poly_trig(double x);
extern int    scalar_step_toward_snap(double target, double step, float *value);
extern unsigned int aiactor_find_bot_by_linked_entity(int entityId);
extern void   entity_set_render_type(int entity, int renderType);
extern void   mtx4_multiply(float *a, float *b, float *out);
extern void   mtx4_set_rotation_zyx(double rz, double ry, double rx, float *mtx);
extern void   mtx4_table_reset_identity(void);

// mtx4_set_translation's shared decl is (float* mtx, float x, float y, float z);
// the ABI call site here is the 3-arg (x,y,z) translation-accumulate form, so it
// is bound by ROM address as a file-local alias.
typedef void (*mtx4_settrans3_fp)(double x, double y, double z);
static const mtx4_settrans3_fp __rl_mtx_set_translation3 =
    (mtx4_settrans3_fp)0x80390a08;   // mtx4_set_translation (3-arg accumulate form)

// batch_663 raw data / constant symbols
extern float  FLOAT_8061284c;             // shared limb oscillation angle
extern float  FLOAT_80610e7c;             // cached bot metric (from linked bot)
extern float  FLOAT_806182a8;
extern float  FLOAT_806182ac;
extern float  FLOAT_806182b0;
extern float  FLOAT_806182b4;
extern float  FLOAT_806182b8;
extern float  FLOAT_806182c8;
extern float  FLOAT_806182cc;
extern double DOUBLE_806182c0;

// ---------------------------------------------------------------------------
// 0x80219c9c  rainlvl_part_aim_dispatch
// Per-part aim dispatcher for the scoped entity. Reads the current part index
// (packed in the r3:r4 pair returned by the scoped-entity accessor) and, for
// the aimable parts (7, 8/10, 9), calls rainlvl_bone_aim_orient with the matching
// bone-handle slot in the DAT_80612848 table; then flags the entity for a fresh
// aim (bit 0x40 at ent+0x18) and triggers the indexed one-shot SFX.
// ---------------------------------------------------------------------------
int rainlvl_part_aim_dispatch(int a1, int a2, float *target)
{
    long long scoped;
    int ent;
    int partIndex;

    scoped = FUN_803826f0();
    ent = (int)((unsigned long long)scoped >> 0x20);
    partIndex = (int)(unsigned int)scoped;

    switch (partIndex) {
    default:
        // Unhandled part index terminates the random-spawn loop (caller reads
        // the return as its "done" flag).
        FUN_8038273c();
        (void)a1; (void)a2;
        return 1;
    case 1: case 2: case 4: case 5: case 6:
        break;
    case 7:
        rainlvl_bone_aim_orient(ent, 0x70000, (float *)(DAT_80612848 + 0x20), target);
        break;
    case 8:
    case 10:
        rainlvl_bone_aim_orient(ent, 0xa0000, (float *)(DAT_80612848 + 0x30), target);
        break;
    case 9:
        rainlvl_bone_aim_orient(ent, 0x90000, (float *)(DAT_80612848 + 0x28), target);
        break;
    }
    *(unsigned int *)(ent + 0x18) = *(unsigned int *)(ent + 0x18) | 0x40;
    rainlvl_sfx_play_indexed();
    (void)a1; (void)a2;
    FUN_8038273c();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80219d94  rainlvl_part_aim_render
// Render wrapper: if the entity holds a valid part descriptor (ent[0xe]) and a
// valid target orientation (ent[0xa]), run the aim dispatcher for it.
// ---------------------------------------------------------------------------
void rainlvl_part_aim_render(int *ent)
{
    if ((ent[0xe] != 0) && ((float *)ent[10] != (float *)0)) {
        rainlvl_part_aim_dispatch(ent[0], *(int *)(ent[0xe] + 4), (float *)ent[10]);
    }
}

// ---------------------------------------------------------------------------
// 0x80219dd4  rainlvl_part_random_spawn_tick
// Repeatedly picks a random bone from the scoped entity's skeleton, computes a
// randomised world-space aim target near that bone (entity local matrix +
// random spread scaled by constants), and drives rainlvl_part_aim_dispatch until
// it reports "done" (returns non-zero). Used to fire off the random part motion.
// ---------------------------------------------------------------------------
void rainlvl_part_random_spawn_tick(void)
{
    int ent;
    unsigned int r;
    unsigned int boneCount;
    unsigned int *skel;
    unsigned int *bonePtr;
    int boneIdx;
    float *vtx;
    int done;
    float mtx[16];        // afStack_c0
    float aim[3];         // local_cc / local_c8 / local_c4
    float world[3];       // local_d8 / local_d4 / local_d0
    double kScaleA;       // FLOAT_80618278
    double kScaleB;       // FLOAT_806182a8
    double kSpread;       // FLOAT_806182ac
    double kSpanInv;      // FLOAT_80618280
    double kBias;         // DOUBLE_80618288

    ent = (int)((unsigned long long)FUN_803826f0() >> 0x20);
    kScaleA = (double)FLOAT_80618278;
    kSpanInv = (double)FLOAT_80618280;
    kScaleB = (double)FLOAT_806182a8;
    kSpread = (double)FLOAT_806182ac;
    kBias = DOUBLE_80618288;

    do {
        r = (unsigned int)__rl_rng();
        skel = *(unsigned int **)(*(int *)(ent + 4) + 4);
        boneCount = *skel;
        boneIdx = (int)(r - (r / boneCount) * boneCount);
        if ((boneIdx < 0) || ((int)boneCount <= boneIdx)) {
            bonePtr = (unsigned int *)0;
        } else {
            bonePtr = skel + boneCount * -0x30 + boneIdx * 0x30;
        }
        vtx = (float *)bonePtr[0x27];

        world[0] = (float)(kScaleA * (double)*vtx      + (double)(float)(kScaleA * (double)vtx[3]));
        world[1] = (float)(kScaleA * (double)vtx[1]    + (double)(float)(kScaleA * (double)vtx[4]));
        world[2] = (float)(kScaleA * (double)vtx[2]    + (double)(float)(kScaleA * (double)vtx[5]));
        entity_build_local_matrix(mtx, ent, 1, 0);
        mtx4_transform_point_alias(mtx, world);

        {
            unsigned int rx = (unsigned int)__rl_rng();
            unsigned int ry = (unsigned int)__rl_rng();
            unsigned int rz = (unsigned int)__rl_rng();
            aim[0] = (float)(kScaleB * (double)(float)((double)CONCAT44(0x43300000, rx) - kBias) *
                             kSpanInv - kScaleA);
            aim[1] = (float)(kScaleB * (double)(float)((double)CONCAT44(0x43300000, ry) - kBias) *
                             kSpanInv - kScaleA);
            aim[2] = (float)(kScaleB * (double)(float)((double)CONCAT44(0x43300000, rz) - kBias) *
                             kSpanInv - kScaleA);
        }
        aim[0] = (float)((double)aim[0] * kSpread) + world[0];
        aim[1] = (float)((double)aim[1] * kSpread) + world[1];
        aim[2] = (float)((double)aim[2] * kSpread) + world[2];

        done = rainlvl_part_aim_dispatch(ent, boneIdx, aim);
    } while (done == 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80219fd4  rainlvl_limb_rotate_bone
// Rotates one limb about its pivot bone. Computes a Z/Y/X rotation from two trig
// evaluations of the limb's driving angles (param_6[0]/[1] with the constants
// FLOAT_806182b0/a4/b4), fetches the bone's local pivot vertex, and applies the
// standard translate(-pivot) * rotate * translate(pivot) sandwich to the limb's
// skeleton matrix (part index * 0x40 stride).
// ---------------------------------------------------------------------------
void rainlvl_limb_rotate_bone(long long partPair, double amp, int a3, int a4,
                              unsigned int handle, float *drive)
{
    int part;
    int *skel;
    float *pivot;
    float *mtx;
    int mtxOff;
    double base;
    double rotZ;
    double rotX;

    part = (int)((unsigned long long)partPair >> 0x20);

    base = math_poly_trig((double)(float)((double)(FLOAT_806182b0 *
                                                    (FLOAT_806182a4 - *drive)) * amp));
    base = (double)(float)((double)*drive * base);
    rotZ = math_poly_trig((double)(drive[1] - FLOAT_806182b4));
    rotZ = (double)(float)(base * rotZ);
    rotX = math_poly_trig((double)drive[1]);
    rotX = (double)(float)(base * rotX);

    skel = *(int **)(*(int *)(part + 4) + 4);
    pivot = (float *)(skel[*skel * -0x30 + ((int)handle >> 0x10) * 0x30 + 0x1b] +
                      (handle & 0xffff) * 0x18);

    __rl_mtx_set_translation3((double)*pivot, (double)pivot[1], (double)pivot[2]);
    mtxOff = (int)partPair * 0x40;
    mtx = (float *)(*(int *)(*(int *)(part + 4) + 0x10) + mtxOff);
    mtx4_multiply(mtx, mtx, mtx);
    mtx4_set_rotation_zyx(rotZ, (double)FLOAT_80618270, rotX, mtx);
    mtx = (float *)(*(int *)(*(int *)(part + 4) + 0x10) + mtxOff);
    mtx4_multiply(mtx, mtx, mtx);
    __rl_mtx_set_translation3(-(double)*pivot, -(double)pivot[1], -(double)pivot[2]);
    mtx = (float *)(*(int *)(*(int *)(part + 4) + 0x10) + mtxOff);
    mtx4_multiply(mtx, mtx, mtx);
    (void)a3; (void)a4;
}

// ---------------------------------------------------------------------------
// 0x8021a140  rainlvl_limb_animate_tick  (entity update cb)
// Advances the shared limb oscillation angle FLOAT_8061284c by a frame-time
// scaled step (wrapping at FLOAT_8061829c by subtracting FLOAT_806182b0), resets
// the skeleton matrix table, then rotates the four limbs (parts 9/7/10/8) about
// their pivot bones. Finally flags the entity render bit 0x200000 at ent+0x28.
// ---------------------------------------------------------------------------
void rainlvl_limb_animate_tick(int ent)
{
    FLOAT_8061284c = FLOAT_806108ec *
                     (FLOAT_806182a4 /
                      (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                              DOUBLE_806182c0)) +
                     FLOAT_8061284c;
    if (FLOAT_8061829c < FLOAT_8061284c) {
        FLOAT_8061284c = FLOAT_8061284c - FLOAT_806182b0;
    }
    mtx4_table_reset_identity();
    rainlvl_limb_rotate_bone((double)FLOAT_80618294, (double)FLOAT_806182b8, ent, 9, 0x90000,
                             (float *)(DAT_80612848 + 0x28));
    rainlvl_limb_rotate_bone((double)FLOAT_80618294, (double)FLOAT_806182b8, ent, 7, 0x70000,
                             (float *)(DAT_80612848 + 0x20));
    rainlvl_limb_rotate_bone((double)FLOAT_806182ac, (double)FLOAT_806182a4, ent, 10, 0xa0000,
                             (float *)(DAT_80612848 + 0x30));
    rainlvl_limb_rotate_bone((double)FLOAT_806182ac, (double)FLOAT_806182a4, ent, 8, 0x80000,
                             (float *)(DAT_80612848 + 0x30));
    *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x200000;
}

// ---------------------------------------------------------------------------
// 0x8021a258  rainlvl_angle_step_toward
// Steps a single limb angle toward the neutral value FLOAT_80618270, at a
// frame-time scaled rate (param_1 = per-limb rate divisor). Thin wrapper over
// scalar_step_toward_snap; returns whether the angle has snapped to target.
// ---------------------------------------------------------------------------
int rainlvl_angle_step_toward(double rateDiv, float *angle)
{
    return scalar_step_toward_snap(
        (double)FLOAT_80618270,
        (double)(FLOAT_806108ec *
                 (float)((double)(FLOAT_806182a4 /
                                  (float)((double)CONCAT44(0x43300000,
                                                           DAT_806109c0 ^ 0x80000000) -
                                          DOUBLE_806182c0)) /
                         rateDiv)),
        angle);
}

// ---------------------------------------------------------------------------
// 0x8021a2b4  rainlvl_limb_settle_tick  (entity update cb)
// Settles the three aimed limb angles back toward neutral (each at its own rate
// FLOAT_806182c8 / FLOAT_806182cc / FLOAT_806182a4). While any limb has not yet
// snapped, keeps the entity's aim-refresh flag (bit 0x40 at ent+0x18) set.
// ---------------------------------------------------------------------------
void rainlvl_limb_settle_tick(int ent)
{
    unsigned int a;
    unsigned int b;
    unsigned int c;

    a = (unsigned int)rainlvl_angle_step_toward((double)FLOAT_806182c8,
                                                (float *)(DAT_80612848 + 0x20));
    b = (unsigned int)rainlvl_angle_step_toward((double)FLOAT_806182cc,
                                                (float *)(DAT_80612848 + 0x28));
    c = (unsigned int)rainlvl_angle_step_toward((double)FLOAT_806182a4,
                                                (float *)(DAT_80612848 + 0x30));
    if ((a & 1 & b & c) == 0) {
        *(unsigned int *)(ent + 0x18) = *(unsigned int *)(ent + 0x18) | 0x40;
    }
}

// ---------------------------------------------------------------------------
// 0x8021a334  rainlvl_mechanism_object_init
// Object-init dispatcher for the rain-level mechanism entity classes (the class
// id is the 16-bit value at ent+0x94). Wires up the render type and update
// callbacks per class:
//   0xb7  -> render = rainlvl_part_aim_render, register the random-spawn (slot0),
//            limb-settle (slot2) and limb-animate (slot3) ticks; render bits 0x14.
//   0xb1  -> if the passed tag == 'NOTH' ('N''O''T''H'), clear render bits 0x60.
//   0xb0  -> render = rainlvl_debris_expire_tick; render bits 0x10, flags 0x4000;
//            init lifetime timer (ent+0x1cc/0x1d8 = FLOAT_806182ac).
//   0xae  -> set anim clip triple (0x38=2,0x3c=3), render bits 0x40020000.
//   0x390 -> set clip 0xd5, spin angle ent+0xd0 = FLOAT_806182a4, register spin
//            tick (slot3), render bits 0x40020000.
// ---------------------------------------------------------------------------
void rainlvl_mechanism_object_init(int ent, int *tag)
{
    unsigned short cls;
    float f;

    cls = *(unsigned short *)(ent + 0x94);
    if (cls == 0xb1) {
        if (*tag == 0x4e4f4854) {   // 'NOTH'
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xffffff9f;
        }
        return;
    }
    if (cls < 0xb1) {
        if (cls == 0xae) {
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40020000;
            *(int *)(ent + 0x38) = 2;
            *(int *)(ent + 0x3c) = 3;
            *(int *)(ent + 0x34) = *(int *)(ent + 0x38);
            *(unsigned char *)(ent + 0x30) = 2;
            return;
        }
        if (cls < 0xae) {
            return;
        }
        if (cls < 0xb0) {   // 0xaf
            *(int *)(ent + 0x34) = 0xd5;
            *(unsigned char *)(ent + 0x30) = 1;
            *(int *)(ent + 0x38) = *(int *)(ent + 0x34);
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40020000;
            return;
        }
        // 0xb0
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x10;
        *(unsigned int *)(ent + 0x9c) = *(unsigned int *)(ent + 0x9c) | 0x4000;
        entity_set_render_type(ent, (int)0x80219a20);   // rainlvl_debris_expire_tick
        f = FLOAT_806182ac;
        *(float *)(ent + 0x1d8) = FLOAT_806182ac;
        *(float *)(ent + 0x1cc) = f;
        return;
    }
    // cls > 0xb1
    if (cls == 0x390) {
        *(int *)(ent + 0x34) = 0xd5;
        f = FLOAT_806182a4;
        *(unsigned char *)(ent + 0x30) = 1;
        *(int *)(ent + 0x38) = *(int *)(ent + 0x34);
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40020000;
        *(float *)(ent + 0xd0) = f;
        entity_register_for_update(ent, 3, (int)0x802199b0);   // rainlvl_spin_tick
        return;
    }
    if (0x38f < cls) {
        return;
    }
    if (cls != 0xb7) {
        return;
    }
    entity_set_render_type(ent, (int)0x80219d94);              // rainlvl_part_aim_render
    entity_register_for_update(ent, 0, (int)0x80219dd4);       // rainlvl_part_random_spawn_tick
    entity_register_for_update(ent, 2, (int)0x8021a140);       // rainlvl_limb_animate_tick
    entity_register_for_update(ent, 3, (int)0x8021a2b4);       // rainlvl_limb_settle_tick
    *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x14;
}

// ---------------------------------------------------------------------------
// 0x8021a4e8  rainlvl_sfx_table_init
// Clears the mechanism's per-part handle/state table (DAT_80612848): zero the
// first 0x38 bytes, then set the next 0x20 bytes to 0xff (invalid handles).
// ---------------------------------------------------------------------------
void rainlvl_sfx_table_init(void)
{
    memset_wrapper(DAT_80612848, 0, 0x38);
    memset_wrapper(DAT_80612848, 0xff, 0x20);
}

// ---------------------------------------------------------------------------
// 0x8021a524  rainlvl_bot_link_event
// Scripted event handler: on event id 2, finds the AI bot whose linked-entity id
// is 'ZCTP' and caches its metric (bot+0x1cc) into FLOAT_80610e7c for the
// mechanism to read.
// ---------------------------------------------------------------------------
void rainlvl_bot_link_event(int a1, int event)
{
    int bot;

    if ((event == 2) && (bot = (int)aiactor_find_bot_by_linked_entity(0x5054435a), bot != 0)) {
        FLOAT_80610e7c = *(float *)(bot + 0x1cc);
    }
    (void)a1;
}
