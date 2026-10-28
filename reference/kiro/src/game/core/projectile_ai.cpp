// Address range: 0x801659c4 - 0x80168d70 | batch_440 | 2026-09-06
//
// Guided / homing projectile AI (rockets, grenades, remote-control and
// homing weapons).  A projectile record (piVar12/piVar11, int* base) holds:
//   [0]      owner/type tag        (2 == remote-control variant)
//   [1..3]   world position xyz
//   [4..6]   velocity xyz
//   [7..9]   angular / steering accumulators
//   [0xa..c] orientation angles (yaw/pitch/roll)
//   [0xd]    behaviour mode (0..10 state machine)
//   [0x10]   waypoint index          [0x11] waypoint timer
//   [0x12]   life timer              [0x13] turn-rate/agility
//   [0x18]   bank/roll target        [0x1a..1c] target point xyz
//   [0x1d]   re-target cooldown      [0x2d] nearest-hit distance
// Behaviour modes (piVar12[0xd]): 0 waypoint-follow, 1 dead, 2 lock-on-target,
// 3 avoid, 5 wander, 6 hold, 7 orbit, 8 spiral, 10 tethered.
//
//   proj_ai_nop                  0x801659c4  empty stub
//   proj_ai_deactivate           0x801659c8  mark expired + free 3 particles
//   proj_ai_add_damage_indicator 0x80165a28  HUD damage marker at proj pos
//   proj_ai_fire_payload         0x80165a70  spawn sub-projectiles/trail/sound
//   proj_ai_apply_wind           0x80165f90  wind/turbulence + GDGB attractor
//   proj_ai_collision_response   0x80166480  impact response + explosion
//   proj_ai_scan_targets         0x801667f0  proximity/LOS target acquisition
//   proj_ai_resolve_collision    0x80166ad0  swept-AABB world collision slide
//   proj_ai_update_guided        0x801671ac  main guided-projectile state machine
//   proj_ai_update_waypoint      0x80168d70  waypoint-follow update variant

#include <math.h>
#include "common.h"
#include "generated_decls.h"

// ---- runtime globals ----
extern int   DAT_80611d78;    // character record array base (0x141c stride)
extern int   DAT_80612610;    // active character count
extern int   DAT_8061208c;    // level/waypoint graph root
extern int   DAT_806108e4;    // frame delta numerator (int->float via magic)
extern int   DAT_806109c0;    // frame delta denominator
extern float FLOAT_806108ec;  // frame delta seconds (float)
extern float DAT_80583cd4;    // dot threshold
extern float DAT_80583bf0;    // dot threshold 2
extern unsigned char DAT_80583228[]; // sin/cos LUT (0x800 entries)
extern unsigned char DAT_8040efa8[];  // per-type inverse-mass table (0x10 stride)
extern unsigned char DAT_8040efac[];  // per-type table +4
extern unsigned char DAT_8040efb0[];  // per-type table +8
extern float FLOAT_8040efd4;           // remote-control turn scalar

// rodata seeds referenced as scalars (matrix rows etc.)
extern float DAT_803ccb68, DAT_803ccb6c, DAT_803ccb70;
extern float DAT_803ccbb8, DAT_803ccbbc, DAT_803ccbb0, DAT_803ccbb4, DAT_803ccbac, DAT_803ccba4, DAT_803ccba8;
extern float DAT_803ccbc8, DAT_803ccbcc, DAT_803ccbd0, DAT_803ccbd4, DAT_803ccbd8, DAT_803ccbdc;
extern float DAT_803ccbe0, DAT_803ccbe4, DAT_803ccbe8;
extern float DAT_803ccbec, DAT_803ccbf0, DAT_803ccbf4;
extern float DAT_803ccbf8, DAT_803ccbfc, DAT_803ccc00, DAT_803ccc04, DAT_803ccc08, DAT_803ccc0c;

// rodata float/double tuning constants (0x80616340..0x806164bc)
extern const float FLOAT_80616340, FLOAT_80616344, FLOAT_80616348, FLOAT_8061634c, FLOAT_80616350;
extern const float FLOAT_80616354, FLOAT_80616358, FLOAT_8061635c, FLOAT_80616360, FLOAT_80616364;
extern const float FLOAT_80616368, FLOAT_8061636c, FLOAT_80616370, FLOAT_80616374, FLOAT_80616378, FLOAT_8061637c;
extern const float FLOAT_80616388, FLOAT_8061638c, FLOAT_80616390, FLOAT_80616394, FLOAT_80616398, FLOAT_8061639c;
extern const float FLOAT_806163a0, FLOAT_806163a4, FLOAT_806163a8, FLOAT_806163ac, FLOAT_806163b0, FLOAT_806163b4, FLOAT_806163b8, FLOAT_806163bc;
extern const float FLOAT_806163c0, FLOAT_806163c4, FLOAT_806163c8, FLOAT_806163cc, FLOAT_806163d0, FLOAT_806163d4, FLOAT_806163d8, FLOAT_806163dc;
extern const float FLOAT_806163e0, FLOAT_806163e4, FLOAT_806163e8, FLOAT_806163ec, FLOAT_806163f0, FLOAT_806163f4, FLOAT_806163f8, FLOAT_806163fc;
extern const float FLOAT_80616400, FLOAT_80616404, FLOAT_80616408, FLOAT_8061640c, FLOAT_80616410, FLOAT_80616414, FLOAT_80616418, FLOAT_8061641c;
extern const float FLOAT_80616420, FLOAT_80616424, FLOAT_80616428, FLOAT_8061642c, FLOAT_80616430, FLOAT_80616434, FLOAT_80616438, FLOAT_8061643c;
extern const float FLOAT_80616440, FLOAT_80616444, FLOAT_80616448, FLOAT_8061644c, FLOAT_80616450, FLOAT_80616454, FLOAT_80616458, FLOAT_8061645c;
extern const float FLOAT_80616460, FLOAT_80616464, FLOAT_80616468, FLOAT_8061646c, FLOAT_80616470, FLOAT_80616474, FLOAT_80616478, FLOAT_8061647c;
extern const float FLOAT_80616480, FLOAT_80616484, FLOAT_80616488, FLOAT_8061648c, FLOAT_80616490, FLOAT_80616494, FLOAT_80616498;
extern const float FLOAT_806164a8, FLOAT_806164ac, FLOAT_806164b0, FLOAT_806164b4, FLOAT_806164b8, FLOAT_806164bc;
extern const double DOUBLE_80616380, DOUBLE_806164a0;

// ---- helpers ----
// Most resolve through generated_decls.h (canonical signatures) - not redeclared:
//   entity_get_world_position (float*), sound_play_request (uint),
//   game_time_to_frames (float), mtx4_transform_vector, mtx4_set_rotation_x,
//   soundpad_find_zone_by_id, soundpad_resolve_zone_node, math_asin,
//   subobj_constraint_pool_reset, particle_handle_destroy, math_sqrt,
//   player_is_scoped_or_mounted, entity_destroy_with_event, sound_init_play_params.
extern void   player_add_damage_indicator(double a, double b, int* proj, void* pos, int flag);
extern void   mtx4_transform_point(float* mtx, float* pt);
extern void   player_ai_spawn_trail_particle_attached(double a, int type, float* pos, float* dir, unsigned int owner);
extern void   player_ai_explosion_spawn(int kind, int owner, float* pos, float* dir, int entity, int a, int b);
extern double math_atan2(double y, double x);
extern void   arc_clamp_to_bounds(void);
extern int    FUN_8023c32c(int tag);
extern int    FUN_8023c59c(int a, int b, int c);

// Canonical decls don't fit these call sites - route through typed aliases.
typedef unsigned int (*rng_tick_fn)(void);
#define RNG_TICK ((rng_tick_fn)&rng_counter_tick)

typedef void (*proj_spawn_fn)(double a, double b, int kind, float* pos, int* dir, int owner, int* p, int c, int d, int e, int f, int g);
#define PROJ_SPAWN ((proj_spawn_fn)&player_ai_projectile_spawn_simple)

typedef int (*swept_query_fn)(int mask, float* p0, float* vel, int a, int* outHit, int flags, int b);
#define SWEPT_QUERY ((swept_query_fn)&scene_swept_aabb_query)

typedef void (*build_mtx_fn)(float* outMtx, int entity, int a, int b);
#define BUILD_LOCAL_MTX ((build_mtx_fn)&entity_build_local_matrix)

// GPR save/restore spill stubs (not semantic calls).
extern long long __save_gpr_25(void);   extern void __restore_gpr_25(void);
extern long long __save_gpr_28(void);   extern void __restore_gpr_28(void);

// frame delta seconds as computed by the (int->float magic)/(denominator) idiom.
// (File-local helpers; declared before the generated headers' permissive
// externs via forward decls so their real types win.)
float proj_frame_dt(void);
float proj_rsqrt(float x);

float proj_frame_dt(void)
{
    float num = (float)((double)(int)(DAT_806108e4 ^ 0x80000000) - DOUBLE_80616380);
static inline float pai_rsqrt_impl(float x)
{
    float e = 1.0f / sqrtf(x);
    return e * -(FLOAT_80616358 * x * e * e - FLOAT_8061635c);
}

// Expose the helpers under short names via macros so no free-standing
// proj_frame_dt/proj_rsqrt symbol exists to clash with generated permissive externs.
#define proj_frame_dt() pai_frame_dt_impl()
#define proj_rsqrt(x)   pai_rsqrt_impl(x)

// ---------------------------------------------------------------------------
// 0x801659c4 : empty stub.6358 * x * e * e - FLOAT_8061635c);
}

// ---------------------------------------------------------------------------
// 0x801659c4 : empty stub.
// ---------------------------------------------------------------------------
void proj_ai_nop(void)
{
}

// ---------------------------------------------------------------------------
// 0x801659c8 : deactivate the projectile - mark its render sub-object state
// as expired (4) and free its three attached particle handles (+0x78/7c/80).
// ---------------------------------------------------------------------------
void proj_ai_deactivate(int entity)
{
    int sub = *(int*)(entity + 0x7c);
    *(int*)(sub + 0x34) = 4;
    int h;
    h = *(int*)(sub + 0x78); particle_handle_destroy(&h);
    h = *(int*)(sub + 0x7c); particle_handle_destroy(&h);
    h = *(int*)(sub + 0x80); particle_handle_destroy(&h);
}

// ---------------------------------------------------------------------------
// 0x80165a28 : add a HUD damage indicator at the projectile's world position.
// ---------------------------------------------------------------------------
void proj_ai_add_damage_indicator(int* proj)
{
    void* pos = entity_get_world_position(*proj);
    player_add_damage_indicator((double)FLOAT_80616348, (double)FLOAT_80616348, proj, pos, 1);
}

// ---------------------------------------------------------------------------
// 0x80165a70 : fire the projectile's payload on trigger.  Transforms the local
// muzzle points by the entity matrix, optionally homes toward the nearest
// valid character (adjusting bank), then spawns the sub-projectile(s), trail
// particle and launch sound.  proj[0x23..25] hold the payload arm state.
//   entity   owning entity
//   proj     projectile record
//   fireMode nonzero forces the multi-point fan spawn
// ---------------------------------------------------------------------------
void proj_ai_fire_payload(int entity, int* proj, int fireMode)
{
    float dir[3];
    float mtx[16];
    float muzzle[12];

    dir[0] = DAT_803ccb68;
    dir[1] = DAT_803ccb6c;
    dir[2] = DAT_803ccb70;

    // load 6 muzzle points from the rodata seed block
    {
        float* src = &DAT_803ccb70;
        for (int i = 0; i < 6; i++) { muzzle[i * 2] = src[i * 2 + 1]; muzzle[i * 2 + 1] = src[i * 2 + 2]; }
    }

    if (proj[0] == 2) {
        for (int r = 0, o = 0; r < 4; r++, o += 3) {
            muzzle[o]     *= FLOAT_8061634c;
            muzzle[o + 1] *= FLOAT_8061634c;
            muzzle[o + 2] *= FLOAT_8061634c;
        }
    }

    int lo = 0, hi = 2;
    if ((FLOAT_80616350 <= (float)proj[0x23] && FLOAT_80616350 <= (float)proj[0x24]) || fireMode == 0) {
        mtx4_set_rotation_x((double)(float)proj[0x25], (int)mtx);
        mtx4_transform_vector(mtx, dir);
    } else {
        lo = 2; hi = 4;
    }

    entity_build_local_matrix(mtx, entity, 1, 0);
    mtx4_transform_vector(mtx, dir);

    for (int i = lo; i < hi; i++) {
        float* mp = &muzzle[i * 3];
        mtx4_transform_point(mtx, mp);
        float px = mp[0], py = mp[1], pz = mp[2];

        if (lo == 0 && i == 0) {
            // acquire nearest valid character to auto-aim the bank angle
            double best = (double)FLOAT_80616340;
            int hitIdx = -1;
            for (int c = 0, off = 0; c < DAT_80612610; c++, off += 0x141c) {
                int ch = DAT_80611d78 + off;
                float dx = *(float*)(ch + 0xa8) - (float)proj[1];
                float dy = *(float*)(ch + 0xac) - (float)proj[2];
                float dz = *(float*)(ch + 0xb0) - (float)proj[3];
                double d2 = (double)(dz * dz + dx * dx + dy * dy);
                if (d2 < best &&
                    FLOAT_80616344 < dir[2] * (*(float*)(ch + 0xbc) - pz) +
                                     dir[0] * (*(float*)(ch + 0xb4) - px) +
                                     dir[1] * (*(float*)(ch + 0xb8) - py) &&
                    player_is_scoped_or_mounted(ch) == 0) {
                    hitIdx = c; best = d2;
                }
            }
            if (hitIdx != -1) {
                int ch = DAT_80611d78 + hitIdx * 0x141c;
                float bx = *(float*)(ch + 0xb8) - py;
                float ax = *(float*)(ch + 0xb4) - px;
                float cz = *(float*)(ch + 0xbc) - pz;
                float len2 = cz * cz + ax * ax + bx * bx;
                float bank = FLOAT_80616344;
                if (FLOAT_80616354 < len2) {
                    float g = proj_rsqrt(len2);
                    bank = ax * FLOAT_80616360 * g;
                }
                float step = FLOAT_80616364 * (FLOAT_80616360 / proj_frame_dt()) * proj_frame_dt();
                if (step < ABS(bank - dir[1])) {
                    if (dir[1] <= bank) {
                        if (dir[1] < bank) proj[0x25] = (int)((float)proj[0x25] - step);
                    } else {
                        proj[0x25] = (int)((float)proj[0x25] + step);
                    }
                    if ((float)proj[0x25] <= FLOAT_80616368) {
                        if ((float)proj[0x25] < FLOAT_8061636c) proj[0x25] = (int)FLOAT_8061636c;
                    } else {
                        proj[0x25] = (int)FLOAT_80616368;
                    }
                }
            }

            if (*(short*)(entity + 0x94) == 0x482) {
                player_ai_spawn_trail_particle_attached((double)FLOAT_80616370, 2, mp, dir, *(unsigned int*)(entity + 0x2c));
                player_ai_projectile_spawn_simple((double)FLOAT_80616374, (double)FLOAT_80616344, 0x1e, mp, (int*)dir, entity, (int*)0, 6, 0, *(int*)(entity + 0x34), 0, 2);
            } else {
                player_ai_spawn_trail_particle_attached((double)FLOAT_80616370, 0, mp, dir, *(unsigned int*)(entity + 0x2c));
                player_ai_projectile_spawn_simple((double)FLOAT_80616374, (double)FLOAT_80616344, 4, mp, (int*)dir, entity, (int*)0, 6, 0, *(int*)(entity + 0x34), 0, 1);
            }
            int snd[7];
            sound_init_play_params((int)snd);
            *(unsigned char*)((char*)&snd[3] + 0) = 1;
            snd[0] = 0x4c4;
            *(float*)&snd[4] = px; *(float*)&snd[5] = py; *(float*)&snd[6] = pz;
            sound_play_request(snd);
            *(float*)((int)proj + i * 4 + 0x84) = FLOAT_80616378;
        } else {
            player_ai_projectile_spawn_simple((double)FLOAT_80616358, (double)FLOAT_80616344, 0xb, mp, (int*)dir, entity, (int*)0, 6, 0, *(int*)(entity + 0x34), 0, 2);
            *(float*)((int)proj + i * 4 + 0x84) = FLOAT_8061637c;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80165f90 : apply wind/turbulence to the projectile's velocity and, for the
// GDGB attractor volume (type 0x38), pull nearby entities toward the missile.
// The DAT_80583228 sin/cos LUT drives an oscillating lateral force.
// ---------------------------------------------------------------------------
void proj_ai_apply_wind(int entity, int* proj)
{
    float row0 = DAT_803ccba4, row1 = DAT_803ccba8, row2 = DAT_803ccbac;
    float lat0 = DAT_803ccbb0, lat1 = DAT_803ccbb4, lat2 = DAT_803ccbb8, lat3 = DAT_803ccbbc;
    (void)lat1; (void)lat3;

    unsigned int phase = (unsigned int)(FLOAT_80616388 * ((FLOAT_8061638c * (float)proj[0x18]) / FLOAT_80616390) + FLOAT_80616358);
    float sinP = *(float*)((int)&DAT_80583228 + ((phase + 0x200) * 4 & 0x1ffc));
    float cosP = ((float*)&DAT_80583228)[phase & 0x7ff];

    mtx4_transform_vector((float*)(entity + 0x274), &lat0);
    mtx4_transform_vector((float*)(entity + 0x274), &lat2);

    float dt   = proj_frame_dt();
    float mass = *(float*)((int)&DAT_8040efa8 + proj[0] * 0x10);
    float agility = (float)proj[0x13];

    proj[4] = (int)((FLOAT_80616394 * row0 + lat2 * agility) * (dt / mass) + (float)proj[4]);
    proj[5] = (int)((FLOAT_80616394 * row1 + sinP * agility) * (dt / mass) + (float)proj[5]);
    proj[6] = (int)((FLOAT_80616394 * row2 + cosP * agility) * (dt / mass) + (float)proj[6]);

    proj[1] = (int)((float)proj[4] * dt + (float)proj[1]);
    proj[2] = (int)((float)proj[5] * dt + (float)proj[2]);
    proj[3] = (int)((float)proj[6] * dt + (float)proj[3]);

    if (*(int*)(entity + 0x34) == 0x38) {
        int e = FUN_8023c32c(0x42444731);   // 'BDG1'
        double radius = (double)FLOAT_80616398;
        for (; e != 0; e = FUN_8023c32c(*(int*)(e + 0x60) + 1)) {
            float dx = *(float*)(e + 100) - (float)proj[1];
            float dy = *(float*)(e + 0x68) - (float)proj[2];
            float dz = *(float*)(e + 0x6c) - (float)proj[3];
            double d2 = (double)(dz * dz + dx * dx + dy * dy);
            if (d2 < radius) {
                float pull = FLOAT_8061639c * (FLOAT_80616360 - (float)(d2 / radius));
                *(float*)(e + 0xb4) = (float)proj[4] * pull * (FLOAT_80616360 / dt) + *(float*)(e + 0xb4);
                *(float*)(e + 0xb8) = (float)proj[5] * pull * (FLOAT_80616360 / dt) + *(float*)(e + 0xb8);
                *(float*)(e + 0xbc) = (float)proj[6] * pull * (FLOAT_80616360 / dt) + *(float*)(e + 0xbc);
                float vpull = (FLOAT_80616360 / dt) * FLOAT_806163a0 * (float)proj[6] * (FLOAT_80616360 - (float)(d2 / radius));
                if ((float)proj[2] <= *(float*)(e + 0x68)) {
                    *(float*)(e + 0xe8) = -(vpull - *(float*)(e + 0xe8));
                } else {
                    *(float*)(e + 0xe8) = vpull + *(float*)(e + 0xe8);
                }
            }
        }
        if (FLOAT_806163a4 < (float)proj[1] && FLOAT_80616344 < (float)proj[4]) {
            proj[4] = (int)-(float)proj[4];
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80166480 : collision response when the projectile strikes an entity.
// Transfers momentum to the struck sub-object, plays impact / ricochet sound,
// latches the hit position, and (once life expires) spawns the explosion and
// destroys the projectile.  Operates on the register-pair record piVar9.
// ---------------------------------------------------------------------------
void proj_ai_collision_response(int* proj)
{
    if (proj[1] != 0) {
        int self = proj[0];
        if (*(short*)(proj[1] + 0x94) != *(short*)(self + 0x94)) {
            *(float*)(self + 0x1cc) = *(float*)(self + 0x1cc) - (float)proj[8];
        }
    }

    float dt   = proj_frame_dt();
    float* hitPos = (float*)proj[10];
    float* hitDir = (float*)proj[0xc];
    int self = proj[0];

    if (hitPos != 0 && hitDir != 0 && proj[1] != self) {
        int* sub = *(int**)(self + 0x7c);
        float mass = *(float*)((int)&DAT_8040efac + *sub * 0x10);
        float dz = hitPos[2] - *(float*)(self + 0x6c);
        float dhx = hitDir[0], dhy = hitDir[1], dhz = hitDir[2];
        float sx = *(float*)(self + 100);
        (void)dhy;

        sub[7] = (int)(((hitPos[1] - *(float*)(self + 0x68)) * dhz - dz * hitDir[1]) * FLOAT_806163a8 * dt * (FLOAT_80616360 / dt) * mass + (float)sub[7]);
        sub[8] = (int)((dz * dhx - (hitPos[0] - sx) * dhz) * dt * (FLOAT_80616360 / dt) * mass + (float)sub[8]);
        sub[9] = (int)(FLOAT_80616344 * dt * (FLOAT_80616360 / dt) * mass + (float)sub[9]);

        if ((float)sub[0x44] <= FLOAT_80616344) {
            float* p = (float*)proj[10];
            int snd[7];
            sound_init_play_params((int)snd);
            *(unsigned char*)((char*)&snd[3] + 0) = 1;
            snd[0] = 0x4c8;
            *(float*)&snd[4] = p[0]; *(float*)&snd[5] = p[1]; *(float*)&snd[6] = p[2];
            sound_play_request(snd);
            sub[0x44] = (int)FLOAT_806163ac;
        }
        if (proj[6] != 0x1a) {
            float* p = (float*)proj[10];
            unsigned int r = rng_counter_tick();
            int snd[7];
            sound_init_play_params((int)snd);
            *(unsigned char*)((char*)&snd[3] + 0) = 1;
            *(float*)&snd[4] = FLOAT_806163b0;
            *(float*)((char*)&snd[5]) = p[0];   // note: layout matches original
            snd[0] = (int)(r & 1) + 0x4c6;
            sound_play_request(snd);
        }
        if (sub[0xd] == 0 && proj[1] != 0 && *(int*)(proj[1] + 0x150) != 0) {
            sub[0xd] = 2;
            int link = *(int*)(proj[1] + 0x150);
            sub[0x14] = *(int*)(link + 0xb4);
            sub[0x15] = *(int*)(link + 0xb8);
            sub[0x16] = *(int*)(link + 0xbc);
        }
        sub[0x12] = (int)FLOAT_80616344;
    }

    float* fuse = (float*)proj[0xc];
    self = proj[0];
    if (fuse != 0 && *(float*)(self + 0x1cc) < FLOAT_80616344) {
        int sub = *(int*)(self + 0x7c);
        if (*(short*)(self + 0x94) == 0x3b3) {
            player_ai_explosion_spawn(0x20, proj[6], (float*)(self + 100), fuse, self, 0, 1);
        } else {
            player_ai_explosion_spawn(0x28, proj[6], (float*)(self + 100), fuse, self, 0, 1);
        }
        entity_destroy_with_event((void*)*proj);
        *(int*)(sub + 0x34) = 4;
    }
}

// ---------------------------------------------------------------------------
// 0x801667f0 : proximity / line-of-sight target acquisition.  Scans every
// active character; if one is within the forward cone and near enough (or the
// projectile is in "seek" mode 3), latches it as the lock target (state 2) and
// records its position at proj+0x50..0x58.  A secondary check uses a per-char
// trigger sphere at +0xe94.  Operates on the register-pair record (iVar5).
// ---------------------------------------------------------------------------
void proj_ai_scan_targets(int entity, int* proj)
{
    float fwd0 = DAT_803ccbc8, fwd1 = DAT_803ccbcc, fwd2 = DAT_803ccbd0;
    *(unsigned int*)((int)proj + 0x38) &= 0xfffffffd;

    for (int c = 0, off = 0; c < DAT_80612610; c++, off += 0x141c) {
        int ch = DAT_80611d78 + off;
        double dx = (double)(*(float*)(ch + 0xb4) - *(float*)((int)proj + 4));
        double dy = (double)(*(float*)(ch + 0xb8) - *(float*)((int)proj + 8));
        double dz = (double)(*(float*)(ch + 0xbc) - *(float*)((int)proj + 0xc));
        mtx4_transform_vector((float*)(entity + 0x274), &fwd0);

        int mode = *(int*)((int)proj + 0x34);
        if (mode == 5 || mode == 6) break;

        double fwdDot = (double)(float)((double)fwd2 * dz + (double)(float)((double)fwd0 * dx + (double)(float)((double)fwd1 * dy)));
        if ((double)FLOAT_80616344 < fwdDot) {
            float rx = (float)((double)fwd0 - dx);
            float ry = (float)((double)fwd1 - dy);
            float rz = (float)((double)fwd2 - dz);
            double sep = (double)(rz * rz + rx * rx + ry * ry);
            if (sep < (double)FLOAT_806163b4 || mode == 3) {
                float up0 = DAT_803ccbd4, up1 = DAT_803ccbd8, up2 = DAT_803ccbdc;
                mtx4_transform_vector((float*)(entity + 0x274), &up0);
                if ((float)(dz * (double)up2 + (double)(float)(dx * (double)up0 + (double)(float)(dy * (double)up1))) < FLOAT_806163b8) {
                    if ((*(unsigned int*)((int)proj + 0x38) & 1) == 0 && sep < (double)FLOAT_806163bc) {
                        *(int*)((int)proj + 0x34) = 2;
                    }
                    *(unsigned int*)((int)proj + 0x38) |= 2;
                    *(int*)((int)proj + 0x50) = *(int*)(DAT_80611d78 + off + 0xb4);
                    *(int*)((int)proj + 0x54) = *(int*)(DAT_80611d78 + off + 0xb8);
                    *(int*)((int)proj + 0x58) = *(int*)(DAT_80611d78 + off + 0xbc);
                    *(float*)((int)proj + 0x48) = FLOAT_80616344;
                }
            }
        }

        if (*(int*)((int)proj + 0x34) != 2 && *(int*)((int)proj + 0x34) != 7 &&
            (*(unsigned int*)((int)proj + 0x38) & 1) == 0) {
            int cc = DAT_80611d78 + off;
            float tx = *(float*)(cc + 0xe94) - *(float*)((int)proj + 4);
            float ty = *(float*)(cc + 0xe98) - *(float*)((int)proj + 8);
            float tz = *(float*)(cc + 0xe9c) - *(float*)((int)proj + 0xc);
            float trig = *(float*)(cc + 0xea0);
            if (tz * tz + tx * tx + ty * ty < FLOAT_806163c0 * trig * trig) {
                *(int*)((int)proj + 0x34) = 2;
                *(int*)((int)proj + 0x50) = *(int*)(DAT_80611d78 + off + 0xb4);
                *(int*)((int)proj + 0x54) = *(int*)(DAT_80611d78 + off + 0xb8);
                *(int*)((int)proj + 0x58) = *(int*)(DAT_80611d78 + off + 0xbc);
                *(float*)((int)proj + 0x48) = FLOAT_80616344;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80166ad0 : resolve world (geometry) collision via a swept-AABB query and
// slide the projectile velocity along contact normals, with per-mode restitution
// (piVar11[0xd]: 3 = bounce, 2 = partial slide, default = damped slide).  Then
// applies pairwise separation against other nearby physics bodies.
//   entity   owning entity
//   proj     projectile record
//   outPos   resolved position (written for the default slide case)
// ---------------------------------------------------------------------------
void proj_ai_resolve_collision(int entity, int* proj, float* outPos)
{
    int hitList = 0;
    unsigned char pad0 = 0, pad1 = 0; (void)pad0; (void)pad1;
    subobj_constraint_pool_reset();

    if (proj[0] == 2) { return; }

    float vel[3];
    vel[0] = FLOAT_80616370 * (float)proj[4];
    vel[1] = FLOAT_80616370 * (float)proj[5];
    vel[2] = FLOAT_80616370 * (float)proj[6];
    float start[3];
    start[0] = *(float*)(entity + 100)  + *(float*)(entity + 0x1a8);
    start[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
    start[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
    proj[0x2d] = (int)FLOAT_80616340;

    math_sqrt((double)(*(float*)(entity + 0x1c8) * *(float*)(entity + 0x1c8) +
                       *(float*)(entity + 0x1c0) * *(float*)(entity + 0x1c0) +
                       *(float*)(entity + 0x1c4) * *(float*)(entity + 0x1c4)));

    int hit = scene_swept_aabb_query(*(int*)(entity + 0x34), start, vel, 0, &hitList, 0xfffffff0, 0);
    if (hit != 0) {
        float firstDot = *(float*)(hitList + 0x14) * (float)proj[6] +
                         *(float*)(hitList + 0xc) * (float)proj[4] +
                         *(float*)(hitList + 0x10) * (float)proj[5];
        float nearest = FLOAT_80616340;
        for (int node = hitList; node != 0; node = *(int*)(node + 0x34)) {
            float* n = (float*)(node + 0xc);
            float depth = *(float*)(node + 0x1c);
            float dot = (float)proj[6] * n[2] + (float)proj[4] * n[0] + (float)proj[5] * n[1];

            if (proj[0xd] == 3) {
                float thresh = (FLOAT_80616360 / proj_frame_dt()) * proj_frame_dt() * FLOAT_80616358;
                if (depth < thresh && dot < FLOAT_80616344) {
                    float k = dot;
                    proj[4] = (int)(n[0] * -k + (float)proj[4]);
                    proj[5] = (int)(n[1] * -k + (float)proj[5]);
                    proj[6] = (int)(n[2] * -k + (float)proj[6]);
                }
            } else {
                if (depth < nearest) nearest = depth;
                bool handled = false;
                if (proj[0xd] == 2) {
                    float thresh = (FLOAT_80616360 / proj_frame_dt()) * proj_frame_dt() * FLOAT_80616358;
                    if (depth < thresh && dot < FLOAT_80616344) {
                        float k = FLOAT_806163c4 * dot;
                        proj[4] = (int)(n[0] * -k + (float)proj[4]);
                        proj[5] = (int)(n[1] * -k + (float)proj[5]);
                        proj[6] = (int)(n[2] * -k + (float)proj[6]);
                        handled = true;
                    }
                }
                if (!handled && FLOAT_80616344 < *(float*)(node + 0x18) && dot < FLOAT_80616344) {
                    float k = FLOAT_80616370 * FLOAT_80616358 * dot;
                    proj[4] = (int)(n[0] * -k + (float)proj[4]);
                    proj[5] = (int)(n[1] * -k + (float)proj[5]);
                    proj[6] = (int)(n[2] * -k + (float)proj[6]);
                    break;
                }
            }
        }
        proj[0x2d] = (int)nearest;

        float* n0 = (float*)(hitList + 0xc);
        float push = FLOAT_806163c8 * firstDot;
        float k = push < FLOAT_80616344 ? push : FLOAT_80616344;
        proj[4] = (int)(n0[0] * -k + (float)proj[4]);
        proj[5] = (int)(n0[1] * -k + (float)proj[5]);
        proj[6] = (int)(n0[2] * -k + (float)proj[6]);
        if (proj[0xd] != 3) {
            float f = FLOAT_80616370;
            outPos[0] = f * n0[0] + (float)proj[1];
            outPos[1] = f * n0[1] + (float)proj[2];
            outPos[2] = f * n0[2] + (float)proj[3];
        }
    }

    // pairwise separation vs nearby physics bodies
    if (*(int*)(entity + 0x2d8) != 0) {
        int listHdr = *(int*)(*(int*)(entity + 0x2d8) + 4);
        if (listHdr != 0) {
            int nearby[4];
            int n = FUN_8023c59c(listHdr, (int)nearby, 4);
            for (int i = 0; i < n; i++) {
                int other = nearby[i];
                if (other == entity) continue;
                float dx = *(float*)(entity + 100)  - *(float*)(other + 100);
                float dy = *(float*)(entity + 0x68) - *(float*)(other + 0x68);
                float dz = *(float*)(entity + 0x6c) - *(float*)(other + 0x6c);
                if (dz * dz + dx * dx + dy * dy >= FLOAT_806163cc) continue;

                float sx = *(float*)(other + 0x68) - *(float*)(entity + 0x68);
                float sfx = *(float*)(other + 100)  - *(float*)(entity + 100);
                int osub = *(int*)(other + 0x7c);
                float sz = *(float*)(other + 0x6c) - *(float*)(entity + 0x6c);
                float sep2 = sz * sz + sfx * sfx + sx * sx;
                if (FLOAT_80616354 < sep2) {
                    float g = FLOAT_80616360 * proj_rsqrt(sep2);
                    sfx *= g; sx *= g; sz *= g;
                }
                float vn = (float)proj[6] * sz + (float)proj[4] * sfx + (float)proj[5] * sx;
                if (FLOAT_80616344 < vn &&
                    *(float*)(osub + 0x18) * sz + *(float*)(osub + 0x10) * sfx + *(float*)(osub + 0x14) * sx < FLOAT_80616344) {
                    float imp = FLOAT_806163d0 * vn;
                    proj[4] = (int)(sfx * imp + (float)proj[4]);
                    proj[5] = (int)(sx  * imp + (float)proj[5]);
                    proj[6] = (int)(sz  * imp + (float)proj[6]);
                    float react = FLOAT_806163d0 * (sz * *(float*)(osub + 0x18) + sfx * *(float*)(osub + 0x10) + sx * *(float*)(osub + 0x14));
                    *(float*)(osub + 0x10) = sfx * react + *(float*)(osub + 0x10);
                    *(float*)(osub + 0x14) = sx  * react + *(float*)(osub + 0x14);
                    *(float*)(osub + 0x18) = sz  * react + *(float*)(osub + 0x18);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801671ac : main guided-projectile state machine.  Advances the life timer,
// runs target scan (proj_ai_scan_targets), picks the nearest visible character,
// then per behaviour-mode (piVar12[0xd] switch 0..10) computes a desired target
// point (local_130/12c/128) and turn/agility values.  It then converts the
// heading error into yaw/pitch torque (via atan2/asin), integrates the angular
// state, applies gravity/damping, clamps, and finally resolves collision
// (proj_ai_resolve_collision) and optionally fires (proj_ai_fire_payload).
//
// This is a structurally-faithful non-matching port: the pervasive
// int->float frame-time magic is collapsed into proj_frame_dt(), and angle
// wrap loops are preserved as-is.  Operates on register-pair record piVar12
// (proj) with entity iVar8.
// ---------------------------------------------------------------------------
void proj_ai_update_guided(int entity, int* proj)
{
    float dt = proj_frame_dt();

    // advance life timer
    proj[0x12] = (int)((float)((double)dt + (double)(float)proj[0x12]));

    int mode = proj[0xd];
    if (mode != 3 && mode != 7 && mode != 6) {
        proj[0x18] = (int)FLOAT_806163d4;
    }

    proj_ai_scan_targets(entity, proj);

    // pick nearest visible character
    int nearest = 0;
    double bestD = (double)FLOAT_80616340;
    for (int c = 0, off = 0; c < DAT_80612610; c++, off += 0x141c) {
        int ch = DAT_80611d78 + off;
        float dx = *(float*)(ch + 0xa8) - (float)proj[1];
        float dy = *(float*)(ch + 0xac) - (float)proj[2];
        float dz = *(float*)(ch + 0xb0) - (float)proj[3];
        double d2 = (double)(dz * dz + dx * dx + dy * dy);
        if (d2 < bestD && player_is_scoped_or_mounted(ch) == 0) { nearest = c; bestD = d2; }
    }

    float tgtX = (float)proj[1], tgtY = (float)proj[2], tgtZ = (float)proj[3];
    unsigned int fireFlag = 0;
    int fireMode = 0;

    switch (proj[0xd]) {
    case 0:
    case 3: {
        // waypoint-graph follow
        int graph = *(int*)(*(int*)(DAT_8061208c + 0x34) + proj[0xf] * 8);
        if (*(int*)(graph + proj[0x10] * 0xc) != 0xf) proj[0x11] = (int)FLOAT_80616344;
        int node = *(int*)(graph + proj[0x10] * 0xc);
        if (node == 0xf) {
            proj[0x11] = (int)((FLOAT_80616360 / proj_frame_dt()) * proj_frame_dt() + (float)proj[0x11]);
            if (*(float*)(graph + proj[0x10] * 0xc + 4) < (float)proj[0x11]) {
                proj[0x10]++;
                if (*(int*)(graph + proj[0x10] * 0xc) == 0x12) proj[0x10] = 0;
            }
        } else if (node < 0xf && 0xd < node) {
            proj[0x17] = *(int*)(graph + proj[0x10] * 0xc + 4);
            proj[0x10]++;
            if (*(int*)(graph + proj[0x10] * 0xc) == 0x12) proj[0x10] = 0;
        }
        int slot = proj[0x10] * 0xc;
        int idField = (*(int*)(graph + slot) == 0) ? 4 : 8;
        int zid = *(int*)(graph + slot + idField);
        if (soundpad_find_zone_by_id(zid) != -1) {
            int z = soundpad_find_zone_by_id(*(int*)(graph + proj[0x10] * 0xc + idField));
            int* wn = soundpad_resolve_zone_node(z);
            tgtX = (float)wn[3]; tgtY = (float)wn[4]; tgtZ = (float)wn[5];
        }
        if ((tgtX - (float)proj[1]) * (tgtX - (float)proj[1]) +
            (tgtZ - (float)proj[3]) * (tgtZ - (float)proj[3]) < FLOAT_8061640c) {
            int nt = *(int*)(graph + proj[0x10] * 0xc);
            if (nt == 1 || nt == 0) {
                proj[0x10]++;
                if (*(int*)(graph + proj[0x10] * 0xc) == 0x12) proj[0x10] = 0;
            }
        }
        tgtY += (float)proj[0x17];
        if (proj[0xd] == 3) {
            double frames = game_time_to_frames();
            int p = (int)(FLOAT_80616388 * FLOAT_80616370 * (float)((double)(FLOAT_80616360 / proj_frame_dt()) * frames) + FLOAT_80616358);
            tgtY += *(float*)((int)&DAT_80583228 + ((p + 0x200) * 4 & 0x1ffc));
        }
        if (*(int*)(graph + proj[0x10] * 0xc) == 0xf) {
            proj[10] = (int)((float)proj[10] * FLOAT_80616410);
            proj[7]  = (int)((float)proj[7]  * FLOAT_80616410);
        }
        if (proj[0xd] == 0) {
            proj[0x13] = (int)FLOAT_806163c0;
        } else {
            double dist = math_sqrt((double)((tgtZ - (float)proj[3]) * (tgtZ - (float)proj[3]) +
                                             (tgtX - (float)proj[1]) * (tgtX - (float)proj[1]) +
                                             (tgtY - (float)proj[2]) * (tgtY - (float)proj[2])));
            double clamped = dist;
            if ((double)FLOAT_80616414 < dist) clamped = (double)FLOAT_80616414;
            if ((double)FLOAT_80616360 <= clamped) { if ((double)FLOAT_80616414 < dist) dist = (double)FLOAT_80616414; }
            else dist = (double)FLOAT_80616360;
            float t = (float)(dist - (double)FLOAT_80616360) / FLOAT_80616418;
            if (tgtY < (float)proj[2]) {
                float d = -((float)proj[2] - tgtY);
                float clv = d > FLOAT_80616344 ? FLOAT_80616344 : d;
                t = FLOAT_8061641c;
                if (FLOAT_8061641c <= clv && FLOAT_80616344 < d) t = d < FLOAT_80616344 ? d : FLOAT_80616344;
            }
            float prevAgility = FLOAT_80616420 * (float)proj[0x13];
            proj[0x13] = (int)(FLOAT_806163dc * (FLOAT_80616360 - t) + FLOAT_806163c0 * t);
            proj[0x1d] = (int)((FLOAT_80616360 / proj_frame_dt()) * proj_frame_dt() + (float)proj[0x1d]);
            proj[0x13] = (int)(FLOAT_8061637c * (float)proj[0x13] + prevAgility);
            if ((proj[0xe] & 4U) == 0) {
                proj[0x18] = (int)FLOAT_80616344;
            } else {
                unsigned int ph = (unsigned int)(FLOAT_80616388 * (float)proj[0xb] + FLOAT_80616358);
                float d = *(float*)((int)&DAT_80583228 + ((ph + 0x200) * 4 & 0x1ffc)) * (tgtZ - (float)proj[3]) +
                          ((float*)&DAT_80583228)[ph & 0x7ff] * (tgtX - (float)proj[1]) +
                          FLOAT_80616344 * (tgtY - (float)proj[2]);
                if (d <= FLOAT_80616344) {
                    float v = -(FLOAT_80616428 * (FLOAT_80616360 / proj_frame_dt()) - (float)proj[0x18]);
                    proj[0x18] = (int)(FLOAT_8061642c < v ? v : FLOAT_8061642c);
                } else {
                    float v = FLOAT_80616428 * (FLOAT_80616360 / proj_frame_dt()) + (float)proj[0x18];
                    proj[0x18] = (int)(v < FLOAT_80616424 ? v : FLOAT_80616424);
                }
            }
        }
        break;
    }
    case 1:
        tgtX = (float)proj[0x14];
        tgtY = (float)proj[0x15] + FLOAT_8061635c;
        tgtZ = (float)proj[0x16];
        if (FLOAT_806163e4 < (float)proj[0x12]) proj[0xd] = 0;
        proj[0x13] = (int)FLOAT_80616430;
        break;
    case 2: {
        int ch = DAT_80611d78 + nearest * 0x141c;
        tgtX = *(float*)(ch + 0xb4);
        tgtY = *(float*)(ch + 0xb8) + FLOAT_80616370;
        tgtZ = *(float*)(ch + 0xbc);
        proj[0x13] = (int)FLOAT_806163e8;
        if (proj[0] == 2) { proj[0x13] = (int)FLOAT_80616434; proj[0x18] = (int)FLOAT_806163f0; }
        else proj[0x18] = (int)FLOAT_80616438;
        proj[0x1d] = (int)((FLOAT_80616360 / proj_frame_dt()) * proj_frame_dt() + (float)proj[0x1d]);
        break;
    }
    case 5: {
        // random wander
        float rx = FLOAT_80616370 * (((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_806164a0) / FLOAT_806163e0) - FLOAT_80616358);
        float ry = FLOAT_80616370 * (((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_806164a0) / FLOAT_806163e0) - FLOAT_80616358);
        float rz = FLOAT_80616370 * (((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_806164a0) / FLOAT_806163e0) - FLOAT_80616358);
        proj[0x18] = (int)FLOAT_806163d8;
        proj[0x13] = (int)FLOAT_806163e8;
        float amp = FLOAT_806163e4;
        tgtZ = rz * amp;
        if (FLOAT_806163ec <= (float)proj[3] ||
            (FLOAT_806163f0 <= (float)proj[0x1c] && FLOAT_80616344 != (float)proj[0x1a])) {
            if ((float)proj[3] <= FLOAT_80616400 ||
                ((float)proj[0x1c] <= FLOAT_806163f0 && FLOAT_80616344 != (float)proj[0x1a])) {
                tgtX = (float)proj[0x1a]; tgtY = (float)proj[0x1b]; tgtZ = (float)proj[0x1c];
            } else {
                tgtX = FLOAT_80616404 + rx * amp; tgtY = FLOAT_806163f8 + ry * amp; tgtZ = FLOAT_80616408 + tgtZ;
            }
        } else {
            tgtX = FLOAT_806163f4 + rx * amp; tgtY = FLOAT_806163f8 + ry * amp; tgtZ = FLOAT_806163fc + tgtZ;
        }
        proj[0x1d] = (int)((FLOAT_80616360 / proj_frame_dt()) * proj_frame_dt() + (float)proj[0x1d]);
        break;
    }
    case 6:
        if (proj[0x45] == 0) { proj[0x13] = (int)FLOAT_806163dc; proj[0x18] = (int)FLOAT_80616344; }
        else {
            proj[0x18] = (int)FLOAT_80616344;
            proj[0x13] = (int)FLOAT_80616370;
            if (FLOAT_80616374 < (float)proj[2]) proj[0x13] = (int)FLOAT_80616344;
        }
        break;
    case 7: {
        int ch = DAT_80611d78 + nearest * 0x141c;
        tgtY = *(float*)(ch + 0xb8);
        tgtZ = *(float*)(ch + 0xbc);
        tgtX = FLOAT_80616418 * *(float*)(ch + 0xc0) + *(float*)(ch + 0xb4);
        double frames = game_time_to_frames();
        int p = (int)(FLOAT_80616388 * FLOAT_80616370 * (float)((double)(FLOAT_80616360 / proj_frame_dt()) * frames) + FLOAT_80616358);
        tgtY += FLOAT_8061635c + *(float*)((int)&DAT_80583228 + ((p + 0x200) * 4 & 0x1ffc));
        tgtZ = FLOAT_80616418 * *(float*)(DAT_80611d78 + nearest * 0x141c + 200) + tgtZ;
        proj[0x13] = (int)FLOAT_806163dc;
        proj[0x1d] = (int)((FLOAT_80616360 / proj_frame_dt()) * proj_frame_dt() + (float)proj[0x1d]);
        proj[0x13] = (int)(FLOAT_8061637c * (float)proj[0x13] + FLOAT_80616420 * (float)proj[0x13]);
        proj[0x18] = (int)FLOAT_806163d4;
        break;
    }
    case 8: {
        int ch = DAT_80611d78 + nearest * 0x141c;
        proj[0x13] = (int)FLOAT_8061643c;
        proj[0x18] = (int)FLOAT_806163d4;
        tgtX = *(float*)(ch + 0xb4);
        tgtY = *(float*)(ch + 0xb8) + FLOAT_80616444;
        tgtZ = *(float*)(ch + 0xbc);
        break;
    }
    case 10:
        tgtX = (float)proj[1]; tgtY = (float)proj[2]; tgtZ = (float)proj[3];
        if (*(int*)(entity + 0x1a0) != 0) tgtY = FLOAT_806163f0 + *(float*)(*(int*)(entity + 0x2d8) + 0x14);
        {
            float d = tgtY - (float)proj[2];
            float cl = d > FLOAT_80616360 ? FLOAT_80616360 : d;
            float use = FLOAT_80616344;
            if (FLOAT_80616344 <= cl && FLOAT_80616360 < d) use = FLOAT_80616360;
            else if (FLOAT_80616344 <= cl) use = d;
            proj[0x13] = (int)(FLOAT_80616448 * use);
        }
        proj[0x18] = (int)FLOAT_80616344;
        if (*(int*)(entity + 0x1a0) != 0 && tgtY <= (float)proj[2] && proj[0x2e] < 10) proj[0x2e]++;
        break;
    }

    proj[0x1a] = (int)tgtX;
    proj[0x1b] = (int)tgtY;
    proj[0x1c] = (int)tgtZ;

    // fire gate: mode 5 (orbit) does a facing/leash test to set fireFlag/fireMode
    if (proj[0xd] == 5) {
        int ch = DAT_80611d78 + nearest * 0x141c;
        float sx = FLOAT_80616344, sy = FLOAT_80616344, sz = FLOAT_80616344; (void)sx; (void)sy; (void)sz;
        float dx = *(float*)(ch + 0xb4) - (float)proj[1];
        float dy = *(float*)(ch + 0xb8) - (float)proj[2];
        float dz = *(float*)(ch + 0xbc) - (float)proj[3];
        float len2 = dz * dz + dx * dx + dy * dy;
        if (FLOAT_80616354 < len2) { float g = FLOAT_80616360 * proj_rsqrt(len2); dx *= g; dy *= g; dz *= g; }
        float face = FLOAT_806163c8;
        if (*(short*)(entity + 0x94) != 0x482) face = FLOAT_8061637c;
        float head[16];
        entity_build_local_matrix(head, entity, 0, 0);
        float fdir[3] = { DAT_803ccbe0, DAT_803ccbe4, DAT_803ccbe8 };
        mtx4_transform_vector(head, fdir);
        if (DAT_80583cd4 < (fdir[2] * dz + fdir[0] * dx + fdir[1] * dy)) {
            fireFlag = (face < (float)proj[0x1d]);
            if (fireFlag) proj[0x1d] = (int)FLOAT_80616344;
            if (*(short*)(entity + 0x94) != 0x482 && fireFlag && DAT_80583bf0 < (fdir[2] * dz + fdir[0] * dx + fdir[1] * dy)) {
                float lx = (float)proj[1] - *(float*)(ch + 0xb4);
                float ly = (float)proj[2] - *(float*)(ch + 0xb8);
                float lz = (float)proj[3] - *(float*)(ch + 0xbc);
                if (FLOAT_80616450 < lz * lz + lx * lx + ly * ly) fireMode = 1;
            }
        }
    } else if (proj[0xd] == 3) {
        fireFlag = (FLOAT_806163c8 < (float)proj[0x1d] && (float)proj[0x12] < FLOAT_80616370 &&
                    ABS(*(float*)(entity + 0x68) - tgtY) < FLOAT_80616360);
        if (fireFlag) proj[0x1d] = (int)FLOAT_80616344;
    }

    proj_ai_resolve_collision(entity, proj, &tgtX);

    if ((*(int*)(entity + 0x60) == 0x504e4831) || (*(int*)(entity + 0x60) == 0x504e4832)) {
        arc_clamp_to_bounds();
    }
    if (fireFlag != 0) {
        proj_ai_fire_payload(entity, proj, fireMode);
    }
}

// ---------------------------------------------------------------------------
// 0x80168d70 : waypoint-follow update variant (used by the drone/turret style
// projectile).  Resolves the current waypoint zone (soundpad graph), applies a
// small random jitter to the seek point, transforms body axes, banks the craft
// to align its up-vector with the desired lift direction, integrates yaw/pitch
// toward the target heading (atan2), applies damping, resolves collision, and
// fires when facing a target closely enough.  Register-pair record iVar9 (proj)
// with owning entity iVar6.  Structurally faithful non-matching port.
// ---------------------------------------------------------------------------
void proj_ai_update_waypoint(int entity, int proj)
{
    float fwd[3]  = { DAT_803ccc04, DAT_803ccc08, DAT_803ccc0c };
    float right[3]= { DAT_803ccbf8, DAT_803ccbfc, DAT_803ccc00 };
    (void)right;

    int graph = *(int*)(*(int*)(DAT_8061208c + 0x34) + *(int*)(proj + 0x3c) * 8);

    if (*(int*)(graph + *(int*)(proj + 0x40) * 0xc) != 0xf) {
        *(float*)(proj + 0x44) = FLOAT_80616344;
    }

    // nearest character (for later fire test)
    int nearest = 0;
    double bestD = (double)FLOAT_80616340;
    for (int c = 0, off = 0; c < DAT_80612610; c++, off += 0x141c) {
        int ch = DAT_80611d78 + off;
        float dx = *(float*)(ch + 0xa8) - *(float*)(proj + 4);
        float dy = *(float*)(ch + 0xac) - *(float*)(proj + 8);
        float dz = *(float*)(ch + 0xb0) - *(float*)(proj + 0xc);
        double d2 = (double)(dz * dz + dx * dx + dy * dy);
        if (d2 < bestD && player_is_scoped_or_mounted(ch) == 0) { nearest = c; bestD = d2; }
    }

    // advance the waypoint index
    int slot = *(int*)(proj + 0x40) * 0xc;
    int node = *(int*)(graph + slot);
    if (node == 0xf) {
        *(float*)(proj + 0x44) = (FLOAT_80616360 / proj_frame_dt()) * proj_frame_dt() + *(float*)(proj + 0x44);
        if (*(float*)(graph + *(int*)(proj + 0x40) * 0xc + 4) < *(float*)(proj + 0x44)) {
            *(int*)(proj + 0x40) += 1;
            if (*(int*)(graph + *(int*)(proj + 0x40) * 0xc) == 0x12) *(int*)(proj + 0x40) = 0;
        }
    } else if (node < 0xf && 0xd < node) {
        *(int*)(proj + 0x5c) = *(int*)(graph + slot + 4);
        *(int*)(proj + 0x40) += 1;
        if (*(int*)(graph + *(int*)(proj + 0x40) * 0xc) == 0x12) *(int*)(proj + 0x40) = 0;
    }

    float wpX = FLOAT_80616344, wpY = FLOAT_80616344, wpZ = FLOAT_80616344;
    slot = *(int*)(proj + 0x40) * 0xc;
    int idField = (*(int*)(graph + slot) == 0) ? 4 : 8;
    if (soundpad_find_zone_by_id(*(int*)(graph + slot + idField)) != -1) {
        int z = soundpad_find_zone_by_id(*(int*)(graph + *(int*)(proj + 0x40) * 0xc + idField));
        int* wn = soundpad_resolve_zone_node(z);
        wpX = (float)wn[3]; wpY = (float)wn[4]; wpZ = (float)wn[5];
    }

    float dxw = wpX - *(float*)(proj + 4);
    float dzw = wpZ - *(float*)(proj + 0xc);
    if (dxw * dxw + dzw * dzw < FLOAT_8061640c) {
        int nt = *(int*)(graph + *(int*)(proj + 0x40) * 0xc);
        if (nt == 1 || nt == 0) {
            *(int*)(proj + 0x40) += 1;
            if (*(int*)(graph + *(int*)(proj + 0x40) * 0xc) == 0x12) *(int*)(proj + 0x40) = 0;
        }
    }

    wpY += *(float*)(proj + 0x5c);
    *(float*)(proj + 4) = wpX + *(float*)(proj + 0xa4);
    *(float*)(proj + 8) = wpY + *(float*)(proj + 0xa8);
    *(float*)(proj + 0xc) = wpZ + *(float*)(proj + 0xac);
    *(int*)(entity + 100)  = *(int*)(proj + 4);
    *(int*)(entity + 0x68) = *(int*)(proj + 8);
    *(int*)(entity + 0x6c) = *(int*)(proj + 0xc);

    float dt = proj_frame_dt();

    // steer the drift velocity toward zero with random re-seeding
    *(float*)(proj + 0xb0) = -((FLOAT_80616360 / dt) * dt - *(float*)(proj + 0xb0));
    if (*(float*)(proj + 0xb0) < FLOAT_80616344) {
        *(float*)(proj + 0xa0) = FLOAT_80616370 * (((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_806164a0) / FLOAT_806163e0) - FLOAT_80616358) + *(float*)(proj + 0xa0);
        *(float*)(proj + 0x9c) = FLOAT_80616370 * (((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_806164a0) / FLOAT_806163e0) - FLOAT_80616358) + *(float*)(proj + 0x9c);
        *(float*)(proj + 0x98) = *(float*)(proj + 0x98) + (((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_806164a0) / FLOAT_806163e0) - FLOAT_80616358);
        *(float*)(proj + 0xb0) = FLOAT_80616358 * (((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_806164a0) / FLOAT_806163e0)) + FLOAT_80616358;
    }

    // wrap the drift components back into range
    *(float*)(proj + 0x98) -= wpX;
    *(float*)(proj + 0x9c) -= wpY;
    *(float*)(proj + 0xa0) -= wpZ;
    {
        float v = *(float*)(proj + 0x98);
        if (v <= FLOAT_80616344) { if (v < FLOAT_806164a8) *(float*)(proj + 0x98) = FLOAT_806164a8 - (FLOAT_80616414 + v); }
        else *(float*)(proj + 0x98) = -v;
        v = *(float*)(proj + 0x9c);
        if (v <= FLOAT_80616360) { if (v < FLOAT_806164ac) *(float*)(proj + 0x9c) = FLOAT_806164ac - (FLOAT_80616358 + v); }
        else *(float*)(proj + 0x9c) = FLOAT_80616360 - (v - FLOAT_80616360);
        v = *(float*)(proj + 0xa0);
        if (v <= FLOAT_806164b0) { if (v < FLOAT_806164b4) *(float*)(proj + 0xa0) = FLOAT_806164b4 - (FLOAT_806164b0 + v); }
        else *(float*)(proj + 0xa0) = FLOAT_806164b0 - (v - FLOAT_806164b0);
    }
    *(float*)(proj + 0x98) += wpX;
    *(float*)(proj + 0x9c) += wpY;
    *(float*)(proj + 0xa0) += wpZ;

    mtx4_transform_vector((float*)(entity + 0x274), fwd);

    // body up-vector via cross product of the two transformed axes
    float up0 = right[1] * DAT_803ccc00 - right[2] * DAT_803ccbfc;
    float up1 = right[2] * DAT_803ccbf8 - fwd[0] * DAT_803ccc00;
    float up2 = fwd[0] * DAT_803ccbfc - fwd[1] * DAT_803ccbf8;
    (void)up0; (void)up1; (void)up2;

    // lift / bank integration (mode-independent) using the frame delta
    float f = FLOAT_80616360 / dt * dt;
    *(float*)(proj + 0x10) = DAT_803ccbf8 * FLOAT_806164b4 * f + *(float*)(proj + 0x10);
    *(float*)(proj + 0x14) = DAT_803ccbfc * FLOAT_806164b4 * f + *(float*)(proj + 0x14);
    *(float*)(proj + 0x18) = DAT_803ccc00 * FLOAT_806164b4 * f + *(float*)(proj + 0x18);

    // heading integration toward waypoint via atan2 of the horizontal delta
    int ch = DAT_80611d78 + nearest * 0x141c;
    double hdx = (double)(*(float*)(ch + 0xb4) - *(float*)(proj + 4));
    double hdz = (double)(*(float*)(ch + 0xbc) - *(float*)(proj + 0xc));
    double hdy = (double)(*(float*)(ch + 0xb8) - *(float*)(proj + 8));
    double horiz = math_sqrt((double)(float)(hdz * hdz + (double)(float)(hdx * hdx)));
    double pitch = math_atan2(horiz, hdy);
    (void)pitch;

    float aim = FLOAT_806164b8 +
                FLOAT_8061637c * (DAT_803ccc0c * *(float*)(proj + 0x18) + DAT_803ccc04 * *(float*)(proj + 0x10) + DAT_803ccc08 * *(float*)(proj + 0x14)) +
                FLOAT_80616364 + (float)pitch;
    if (FLOAT_80616454 * FLOAT_806108ec < ABS(aim - *(float*)(proj + 0x28))) {
        float e = *(float*)(proj + 0x28) - aim;
        for (; e <= FLOAT_80616460; e += FLOAT_8061645c) {}
        for (; FLOAT_8061638c < e; e -= FLOAT_8061645c) {}
        float step = FLOAT_80616454 * (FLOAT_80616360 / dt) * dt;
        if (e <= FLOAT_80616344) *(float*)(proj + 0x1c) = -(step - *(float*)(proj + 0x1c));
        else                     *(float*)(proj + 0x1c) = step + *(float*)(proj + 0x1c);
    }
    *(float*)(proj + 0x28) = (FLOAT_80616360 / dt) * *(float*)(proj + 0x1c) * dt + *(float*)(proj + 0x28);

    double yaw = math_atan2(hdx, hdz);
    {
        float e = *(float*)(proj + 0x2c) - (float)yaw;
        for (; e <= FLOAT_80616460; e += FLOAT_8061645c) {}
        for (; FLOAT_8061638c < e; e -= FLOAT_8061645c) {}
        float f2 = FLOAT_80616360 / dt;
        if (FLOAT_80616374 * FLOAT_806108ec * f2 < ABS(e)) {
            if (FLOAT_80616344 <= e) *(float*)(proj + 0x20) = -(FLOAT_80616374 * f2 * dt - *(float*)(proj + 0x20));
            else                     *(float*)(proj + 0x20) = FLOAT_80616374 * f2 * dt + *(float*)(proj + 0x20);
        }
    }
    *(float*)(proj + 0x2c) = (FLOAT_80616360 / dt) * *(float*)(proj + 0x20) * dt + *(float*)(proj + 0x2c);
    *(float*)(proj + 0x1c) *= FLOAT_80616474;
    *(float*)(proj + 0x20) *= FLOAT_80616474;
    *(float*)(proj + 0x10) *= FLOAT_806164bc;
    *(float*)(proj + 0x14) *= FLOAT_806164bc;
    *(float*)(proj + 0x18) *= FLOAT_806164bc;

    proj_ai_resolve_collision(entity, (int*)proj, &wpX);

    // proximity trigger vs nearest waypoint anchor -> fire
    int wp = DAT_80611d78 + nearest * 0x141c;
    float tx = *(float*)(wp + 0xb8) - *(float*)(proj + 8);
    float txx = *(float*)(wp + 0xb4) - *(float*)(proj + 4);
    float tz = *(float*)(wp + 0xbc) - *(float*)(proj + 0xc);
    if (FLOAT_80616344 < DAT_803ccc0c * tz + DAT_803ccc04 * txx + DAT_803ccc08 * tx) {
        float len2 = tz * tz + txx * txx + tx * tx;
        if (FLOAT_80616354 < len2) { float g = FLOAT_80616360 * proj_rsqrt(len2); txx *= g; tx *= g; tz *= g; }
        if (DAT_80583cd4 < DAT_803ccc0c * tz + DAT_803ccc04 * txx + DAT_803ccc08 * tx) {
            *(float*)(proj + 0x74) = (FLOAT_80616360 / dt) * dt + *(float*)(proj + 0x74);
        }
        if (FLOAT_806163c8 < *(float*)(proj + 0x74)) {
            *(float*)(proj + 0x74) = FLOAT_80616344;
            proj_ai_fire_payload(entity, (int*)proj, 0);
        }
    }
    arc_clamp_to_bounds();
}

// ===========================================================================
// Address range: 0x8016a138 - 0x8016bde8 | batch_441 | 2026-09-06
//
// Projectile render / tick / spawn layer.  proj_ai_tick is the per-frame
// entry point registered by proj_ai_spawn; it resets the record, runs the
// guided/waypoint update, integrates the entity transform and spawns the
// three trail particle streams.  A second set draws the smoke/exhaust trails
// and HUD radar blips through the GX FIFO.  proj_ai_spawn is the constructor
// that allocates the 0x118-byte record and classifies the projectile by the
// firing weapon id (0x482 rocket, 0x3b3 grenade, 0x553 remote-control).
//
//   proj_ai_tick              0x8016a138  per-frame update + trail emit
//   proj_ai_animate_model     0x8016a7c4  rotate model fins/segments
//   proj_clip_segment_y       0x8016aa8c  clip/lerp a vertex pair at a Y plane
//   proj_fade_alpha_byte      0x8016ab5c  scaled alpha byte for a vertex
//   proj_ai_render_trails     0x8016abb0  GX draw of the smoke-trail ribbons
//   proj_ai_render_hud_markers 0x8016b544 GX draw of radar/HUD blips
//   proj_ai_release_sound_banks 0x8016b804 free the projectile sound banks
//   proj_ai_spawn             0x8016b850  constructor + type classification
//   proj_register_smoke_def   0x8016bbe8  register the smoke particle def
//   proj_register_trail_def   0x8016bde8  register the trail particle def

extern volatile float DAT_cc008000;   // GX Command Processor FIFO write port

// rodata seed blocks for this layer.
extern float DAT_803ccc10, DAT_803ccc14, DAT_803ccc18, DAT_803ccc1c, DAT_803ccc20, DAT_803ccc24;
extern float DAT_803ccc28, DAT_803ccc2c, DAT_803ccc30, DAT_803ccc34, DAT_803ccc38, DAT_803ccc3c;
extern float DAT_803ccc40, DAT_803ccc44, DAT_803ccc48;
extern float DAT_803ccc88, DAT_803ccc8c, DAT_803ccc90;
extern float DAT_803ccc94, DAT_803ccc98, DAT_803ccc9c, DAT_803ccca0, DAT_803ccca4, DAT_803ccca8;
extern const float FLOAT_806164c0, FLOAT_806164c4, FLOAT_806164c8, FLOAT_806164cc, FLOAT_806164d0;
extern const float FLOAT_806164d4, FLOAT_806164d8, FLOAT_806164dc, FLOAT_806164e0, FLOAT_806164e4, FLOAT_806164e8;
extern unsigned char DAT_8058a7b8[];   // trail particle emitter source
extern unsigned char DAT_8058a83c[];   // trail particle def
extern int   DAT_80611d54;   // local player camera record
extern int   DAT_80611d74;   // radar centre record
extern int   DAT_80612614;   // scene mode

// helpers (self-externed where canonical decl doesn't fit / not present)
extern void particle_cached_set_position(int* h, float* pos);
extern void particle_cached_set_velocity(int* h, float* vel);
extern void particle_def_set_gravity(double g, int tag);
extern void scalar_step_toward_snap(double target, double step, float* value);
extern void mtx4_set_identity(int mtx);
extern void mtx4_rotate_about_point(int mtx, void* pivot, float* axisAngle);
extern void mtx4_set_translation(double x, double y, double z);
extern void mtx4_multiply(float* out, float* a, float* b);
extern void sound_clear_bank_ref(int id);
extern void entity_register_for_update(int entity, int slot, int cb);
extern int  sprite_get_texture_id(int id);
extern int  input_get_buttons_pressed(int pad);
extern int  FUN_8016e8ac(int a);
extern void FUN_8016f00c(int a);
extern void FUN_8016ee98(void);
extern void FUN_80172a4c(int a, int b, int c, int d, int tex);
extern void FUN_80172c54(void);
extern void FUN_803cb4d4(double a, double b);
extern void FUN_8016fc04(double a, double b, double c, double d, double e, double f);
extern void FUN_8016fee0(int a, int b, int c, int d, int color);
extern void FUN_80170410(int a, int b, int c, int d, int e, int f, int g, int h);
extern void FUN_801f47f0(void);
extern void FUN_801f844c(void);

// entity model-cache allocator: canonical decl differs, use a typed alias.
typedef int (*model_alloc_fn)(double f1, void* a2, void* a3, void* a4, void* a5, void* a6,
                              void* a7, void* a8, int entity, int rec, int size,
                              int r6, int r7, int r8, int r9, int r10);
#define MODEL_CACHE_ALLOC ((model_alloc_fn)&entity_model_cache_alloc)
extern int entity_model_cache_alloc(int alignment, int size, int type);   // canonical (3-arg)

// forward decls
void proj_ai_render_hud_markers(void);
void proj_ai_render_trails(void);

// ---------------------------------------------------------------------------
// 0x8016aa8c : clip/interpolate a 7-float vertex (pos xyz + colour/uv) pair
// (p3 -> p4) against a horizontal plane at height y0, writing the crossing
// vertex to out.  No-op if the two vertices share the same Y.
// ---------------------------------------------------------------------------
void proj_clip_segment_y(double y0, float* out, float* p3, float* p4)
{
    double ya = (double)p3[1];
    double yb = (double)p4[1];
    if (ya == yb) return;

    double t = (double)((float)(ya - y0) / (float)(ya - yb));
    float ax = p3[0], bx = p4[0];
    float az = p3[2], bz = p4[2];
    float aw = p3[3], bw = p4[3];
    float au = p3[4], bu = p4[4];
    float av = p3[6], bv = p4[6];
    p4[5] = FLOAT_80616344;
    p3[5] = FLOAT_80616344;
    out[0] = (float)((double)(float)((double)bx - (double)ax) * t + (double)ax);
    out[1] = (float)((double)(float)(yb - ya) * t + ya);
    out[2] = (float)((double)(float)((double)bz - (double)az) * t + (double)az);
    out[3] = (float)((double)(float)((double)bw - (double)aw) * t + (double)aw);
    out[4] = (float)((double)(float)((double)bu - (double)au) * t + (double)au);
    out[5] = FLOAT_80616344;
    out[6] = (float)(t * (double)(float)((double)bv - (double)av) + (double)av);
}

// ---------------------------------------------------------------------------
// 0x8016ab5c : compute a vertex alpha byte.  When `scaled` is set, the base
// alpha (low byte of `alpha`) is multiplied by `factor`; the result is ORed
// into the packed colour `packedRGB`.
// ---------------------------------------------------------------------------
unsigned int proj_fade_alpha_byte(double factor, int scaled, unsigned int alpha, unsigned int scaledOut, unsigned int packedRGB)
{
    if (scaled != 0) {
        scaledOut = (unsigned int)((double)(float)((double)(int)(alpha & 0xff) - DOUBLE_806164a0) * factor);
    }
    return packedRGB | (scaledOut & 0xff);
}

// ---------------------------------------------------------------------------
// 0x8016b804 : release the projectile's sound bank references.
// ---------------------------------------------------------------------------
void proj_ai_release_sound_banks(void)
{
    sound_clear_bank_ref(0x4c4);
    sound_clear_bank_ref(0x4c8);
    sound_clear_bank_ref(0x4c6);
    sound_clear_bank_ref(0x4c7);
    sound_clear_bank_ref(0x4c5);
    sound_clear_bank_ref(0xcfb);
}

// ---------------------------------------------------------------------------
// 0x8016a138 : per-frame projectile tick (registered render/update callback).
// Resets the record's transient block, does a proximity trigger scan, runs the
// guided or waypoint update, integrates the entity transform from the record's
// velocity, and (re)spawns the three trail particle streams at the fin points.
// Register-pair record piVar14; __save/restore via 0x803826dc/80382728 stubs.
// ---------------------------------------------------------------------------
void proj_ai_tick(int entity)
{
    int* rec = *(int**)(entity + 0x7c);
    float* blk = (float*)(rec + 0x2f);

    if (rec[0xd] == 4) return;

    memset_wrapper((int)blk, 0, 0x54);
    blk[0] = FLOAT_8061641c;
    rec[0x30] = (int)FLOAT_8061641c;
    rec[0x31] = (int)FLOAT_8061641c;
    rec[0x38] = (int)FLOAT_80616360;
    rec[0x39] = (int)FLOAT_80616360;
    rec[0x3a] = (int)FLOAT_806164c0;
    rec[0x35] = (int)FLOAT_8061641c;
    rec[0x36] = (int)FLOAT_8061641c;
    rec[0x37] = (int)FLOAT_8061641c;
    rec[0x32] = (int)FLOAT_8061641c;
    rec[0x33] = (int)FLOAT_8061641c;
    rec[0x34] = (int)FLOAT_8061641c;
    rec[0x43] = (int)FLOAT_80616360;
    rec[0x40] = (int)FLOAT_806164c4;
    rec[0x3d] = (int)FLOAT_80616344;
    *(float**)(entity + 0x254) = blk;
    *(unsigned short*)(*(int*)(entity + 4) + 0x1ee) |= 0x10;
    *(unsigned int*)(entity + 0x28) &= 0xfbffffff;

    // proximity trigger scan
    for (int c = 0, off = 0; c < DAT_80612610; c++, off += 0x141c) {
        int ch = DAT_80611d78 + off;
        float dx = *(float*)(ch + 0xb4) - (float)rec[1];
        float dy = *(float*)(ch + 0xb8) - (float)rec[2];
        float dz = *(float*)(ch + 0xbc) - (float)rec[3];
        if (dz * dz + dx * dx + dy * dy < FLOAT_806164c8 || *(int*)(entity + 0x34) == 0x38) {
            *(unsigned int*)(entity + 0x28) |= 0x4000000;
        }
    }

    proj_ai_nop();
    sound_update_3d_source((double)FLOAT_8061635c, (double)FLOAT_806164cc, (double)FLOAT_80616360, (int*)(entity + 0x22c), (float*)(entity + 100), 0x4c5, 0);

    if (DAT_806108e4 == 0) return;

    if (rec[0xd] == 3) {
        proj_ai_update_waypoint(entity, (int)rec);
    } else {
        proj_ai_apply_wind(entity, rec);
        proj_ai_update_guided(entity, rec);
    }

    scalar_step_toward_snap((double)FLOAT_80616344, (double)(FLOAT_806108ec * (FLOAT_80616360 / proj_frame_dt() * proj_frame_dt() / proj_frame_dt())), (float*)(rec + 0x44));

    if (rec[0xd] == 4) return;

    float dt = proj_frame_dt();
    *(int*)(entity + 100)  = rec[1];
    *(int*)(entity + 0x68) = rec[2];
    *(int*)(entity + 0x6c) = rec[3];
    *(unsigned char*)(entity + 0x32) = 2;
    *(float*)(entity + 0xc0) = (FLOAT_80616390 * (float)rec[10]) / FLOAT_8061638c;
    *(float*)(entity + 0x70) = (FLOAT_80616390 * (float)rec[0xb]) / FLOAT_8061638c;
    *(float*)(entity + 200)  = (FLOAT_80616390 * (float)rec[0xc]) / FLOAT_8061638c;

    // advance then rewind the position over the frame (used for particle streak)
    float mtx[17];
    float streakVel[3] = { DAT_803ccc10, (float)DAT_803ccc14, (float)DAT_803ccc18 };
    float pA[6] = { DAT_803ccc1c, DAT_803ccc20, DAT_803ccc24, 0, 0, 0 };
    float pB[6] = { DAT_803ccc28, DAT_803ccc2c, DAT_803ccc30, 0, 0, 0 };
    float pC[6] = { DAT_803ccc34, DAT_803ccc38, DAT_803ccc3c, 0, 0, 0 };

    *(float*)(entity + 100)  = (float)rec[4] * (FLOAT_80616360 / dt) * FLOAT_806108ec + *(float*)(entity + 100);
    *(float*)(entity + 0x68) = (float)rec[5] * (FLOAT_80616360 / dt) * FLOAT_806108ec + *(float*)(entity + 0x68);
    *(float*)(entity + 0x6c) = (float)rec[6] * (FLOAT_80616360 / dt) * FLOAT_806108ec + *(float*)(entity + 0x6c);
    entity_build_local_matrix(mtx, entity, 1, 0);
    *(float*)(entity + 100)  = (float)rec[4] * -(FLOAT_80616360 / dt) * FLOAT_806108ec + *(float*)(entity + 100);
    *(float*)(entity + 0x68) = (float)rec[5] * -(FLOAT_80616360 / dt) * FLOAT_806108ec + *(float*)(entity + 0x68);
    *(float*)(entity + 0x6c) = (float)rec[6] * -(FLOAT_80616360 / dt) * FLOAT_806108ec + *(float*)(entity + 0x6c);

    if (rec[0] == 2) {
        pA[0] *= FLOAT_8061634c; pA[1] *= FLOAT_8061634c; pA[2] *= FLOAT_8061634c;
        pB[0] *= FLOAT_8061634c; pB[1] *= FLOAT_8061634c; pB[2] *= FLOAT_8061634c;
        pC[0] *= FLOAT_8061634c; pC[1] *= FLOAT_8061634c; pC[2] *= FLOAT_8061634c;
    }
    streakVel[1] = FLOAT_8061641c;
    streakVel[2] = FLOAT_80616344;

    int mdl = *(int*)(*(int*)(entity + 4) + 0x10);
    mtx4_transform_point((float*)(mdl + 0x80), streakVel);
    mtx4_transform_vector(mtx, streakVel);
    mtx4_transform_point((float*)(mdl + 0xc0), pA);
    mtx4_transform_point(mtx, pA);
    mtx4_transform_point((float*)(mdl + 0xc0), pB);
    mtx4_transform_point(mtx, pB);
    mtx4_transform_point((float*)(mdl + 0x80), pC);
    mtx4_transform_point(mtx, pC);

    int h = rec[0x1e];
    if (particle_handle_is_valid(&h)) {
        int hh;
        hh = rec[0x1e]; particle_cached_set_position(&hh, pA);
        hh = rec[0x1f]; particle_cached_set_position(&hh, pB);
        hh = rec[0x20]; particle_cached_set_position(&hh, pC);
        hh = rec[0x1e]; particle_cached_set_velocity(&hh, streakVel);
        hh = rec[0x1f]; particle_cached_set_velocity(&hh, streakVel);
        hh = rec[0x20]; particle_cached_set_velocity(&hh, streakVel);
    } else {
        float spawnPos[3] = { DAT_803ccc40, (float)DAT_803ccc44, (float)DAT_803ccc48 };
        particle_emitter_spawn((int)(rec + 0x1e), &DAT_8058a7b8, 0x1000, (float*)(entity + 100), spawnPos);
        particle_emitter_spawn((int)(rec + 0x1f), &DAT_8058a7b8, 0x1000, (float*)(entity + 100), spawnPos);
        particle_emitter_spawn((int)(rec + 0x20), &DAT_8058a7b8, 0x1000, (float*)(entity + 100), spawnPos);
        if (rec[0] == 2) {
            int hs;
            hs = rec[0x1e]; particle_cached_set_size_range((double)FLOAT_8061634c, (double)FLOAT_8061634c, &hs);
            hs = rec[0x1f]; particle_cached_set_size_range((double)FLOAT_8061634c, (double)FLOAT_8061634c, &hs);
            hs = rec[0x20]; particle_cached_set_size_range((double)FLOAT_8061634c, (double)FLOAT_8061634c, &hs);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8016a7c4 : animate the projectile model's rotating segments (fins / rotor).
// Resets each model matrix to identity, then rotates fins about a pivot and
// spins the tail segments by a per-segment angular accumulator (+0x84..).
// ---------------------------------------------------------------------------
void proj_ai_animate_model(int entity)
{
    int* rec = *(int**)(entity + 0x7c);

    float fin[16];   // 4 fin axis-angle triples + 3 rotor triples (rodata seeds)
    {
        float* src = &DAT_803ccc48;
        for (int i = 0; i < 4; i++) { fin[i * 2] = src[i * 2 + 1]; fin[i * 2 + 1] = src[i * 2 + 2]; }
    }
    float rotor[8];
    {
        float* src = (float*)0x803ccc6c;
        for (int i = 0; i < 3; i++) { rotor[i * 2] = src[i * 2 + 1]; rotor[i * 2 + 1] = src[i * 2 + 2]; }
    }
    float pivot[3] = { DAT_803ccc88, DAT_803ccc8c, DAT_803ccc90 };

    if (rec[0] == 2) {
        for (int r = 0, o = 0; r < 3; r++, o += 3) { fin[o] *= FLOAT_8061634c; fin[o+1] *= FLOAT_8061634c; fin[o+2] *= FLOAT_8061634c; }
        for (int r = 0, o = 0; r < 2; r++, o += 3) { rotor[o] *= FLOAT_8061634c; rotor[o+1] *= FLOAT_8061634c; rotor[o+2] *= FLOAT_8061634c; }
    }
    if (rec[0xd] == 4) return;

    int model = *(int*)(*(int*)(entity + 4) + 0x10);
    int count = *(int*)(*(int*)(*(int*)(entity + 4) + 4) + 4);
    for (int i = 0, o = 0; i < count; i++, o += 0x40) mtx4_set_identity(model + o);

    for (int i = 0, o = 0; i < 3; i++, o += 0xc) {
        mtx4_rotate_about_point(model + (i + 2) * 0x40, pivot, (float*)((int)fin + o));
    }

    double decay = (double)FLOAT_80616470;
    for (int i = 0, ao = 0, so = 0; i < 4; i++, ao += 0xc, so += 4) {
        if (i < 2) {
            int m = (i + 5) * 0x40;
            mtx4_rotate_about_point(model + m, pivot, (float*)((int)rotor + ao));
            mtx4_set_translation((double)FLOAT_80616344, (double)FLOAT_80616344, -(double)*(float*)((int)rec + so + 0x84));
            float tmp[16];
            mtx4_multiply(tmp, (float*)(model + m), (float*)(model + m));
        } else {
            mtx4_set_translation((double)FLOAT_80616344, (double)FLOAT_80616344, -(double)*(float*)((int)rec + so + 0x84));
        }
        for (int k = 0; k < DAT_806108e4; k++) {
            *(float*)((int)rec + so + 0x84) = (float)((double)*(float*)((int)rec + so + 0x84) * decay);
        }
    }
    *(unsigned int*)(entity + 0x28) |= 0x200000;
}

// ---------------------------------------------------------------------------
// 0x8016b850 : projectile constructor.  Allocates the 0x118-byte record, wires
// the four per-frame update callbacks and the render callback, seeds position/
// velocity from the muzzle transform, and classifies behaviour by the firing
// weapon id: 0x482 rocket family (Plasma/Brimstone/homing), 0x3b3 grenade,
// 0x553 remote-control.  Register-pair (owner record puVar9, new entity).
// ---------------------------------------------------------------------------
void proj_ai_spawn(int entity, unsigned int* owner)
{
    int rec = MODEL_CACHE_ALLOC(0.0, 0, 0, 0, 0, 0, 0, 0, entity, (int)owner, 0x118, 0, 0, 0, 0, 0);
    *(int*)(entity + 0x7c) = rec;
    unsigned int* r = (unsigned int*)rec;
    memset_wrapper((int)r, 0, 0x118);

    entity_register_for_update(entity, 3, (int)0x80168d70 - 0);   // proj_ai_update_waypoint slot
    entity_register_for_update(entity, 0xc, (int)0x80165a70 - 0); // proj_ai_fire_payload family
    entity_register_for_update(entity, 9, (int)0x80165a70 - 0);
    entity_register_for_update(entity, 2, (int)0x8016a138 - 0);   // proj_ai_tick
    *(unsigned int*)(entity + 0x28) |= 0x50;
    entity_set_render_type(entity, (int)&proj_ai_collision_response);
    *(int*)(*(int*)(entity + 4) + 0x30) = 0;

    if (owner != 0) r[0xf] = owner[10];

    r[1] = *(unsigned int*)(entity + 100);
    r[2] = *(unsigned int*)(entity + 0x68);
    r[3] = *(unsigned int*)(entity + 0x6c);
    r[0x17] = *(unsigned int*)&FLOAT_80616374;
    r[10] = (unsigned int)(int)((FLOAT_8061638c * *(float*)(entity + 0xc0)) / FLOAT_80616390);
    r[0xb] = (unsigned int)(int)((FLOAT_8061638c * *(float*)(entity + 0x70)) / FLOAT_80616390);
    r[0xc] = (unsigned int)(int)((FLOAT_8061638c * *(float*)(entity + 200)) / FLOAT_80616390);
    r[0x13] = *(unsigned int*)&FLOAT_806163c0;
    r[0x26] = r[1]; r[0x27] = r[2]; r[0x28] = r[3];
    *(float*)(entity + 0x1cc) = FLOAT_806163e4;
    *(float*)(entity + 0x1d8) = FLOAT_806163e4;
    r[0x18] = *(unsigned int*)&FLOAT_806163d4;

    if ((*owner >> 0x10 & 0xff) == 0x47) r[0xe] |= 8;

    unsigned short wid = *(unsigned short*)(entity + 0x94);
    if (wid == 0x482) {
        r[0] = 1;
        if (*owner >> 0x18 == 0x50) {
            r[0xd] = 0;
        } else if (*owner >> 0x18 == 0x42) {
            r[0xd] = 4;
            r[0x1d] = (unsigned int)(int)(FLOAT_806163c8 * (((float)((double)CONCAT44(0x43300000, RNG_TICK()) - DOUBLE_806164a0)) / FLOAT_806163e0));
            float ang = FLOAT_8061637c * (((float)((double)CONCAT44(0x43300000, RNG_TICK()) - DOUBLE_806164a0)) / FLOAT_806163e0);
            r[0x25] = (unsigned int)(int)ang;
            r[0x24] = (unsigned int)(int)ang;
            *(float*)(entity + 0x1cc) *= FLOAT_80616358;
        } else {
            r[0xd] = 3;
            r[0x19] = *(unsigned int*)&FLOAT_80616364;
            *(float*)(entity + 0x1cc) *= FLOAT_806163a8;
            *(float*)(entity + 0x1d8) *= FLOAT_806163a8;
            r[0x2a] = (unsigned int)(int)((float)r[0x2a] - FLOAT_80616418);
            r[0xe] |= 5;
        }
    } else if (wid < 0x482) {
        if (wid == 0x3b3) {
            r[0] = 0;
            r[0xd] = 6;
            r[2] = (unsigned int)(int)((float)r[2] - FLOAT_80616418);
            *(float*)(entity + 0x68) -= FLOAT_80616418;
            r[0x1d] = (unsigned int)(int)(FLOAT_806163c8 * (((float)((double)CONCAT44(0x43300000, RNG_TICK()) - DOUBLE_806164a0)) / FLOAT_806163e0));
            float ang = FLOAT_8061637c * (((float)((double)CONCAT44(0x43300000, RNG_TICK()) - DOUBLE_806164a0)) / FLOAT_806163e0);
            r[0x25] = (unsigned int)(int)ang;
            r[0x24] = (unsigned int)(int)ang;
            *(float*)(entity + 0x1cc) *= FLOAT_806163a8;
            r[0x18] = (unsigned int)(int)FLOAT_80616344;
        }
    } else if (wid == 0x553) {
        r[0] = 2;
        r[0xd] = 10;
        *(int*)(entity + 0x38) = 0xd5;
        *(int*)(entity + 0x34) = 0xd5;
        *(unsigned char*)(entity + 0x30) = 1;
        r[0x17] = (unsigned int)(int)((float)r[0x17] + FLOAT_80616428);
        *(unsigned int*)(entity + 0x28) |= 0x40020000;
        r[0x18] = (unsigned int)(int)FLOAT_80616418;
    }
}

// ---------------------------------------------------------------------------
// 0x8016bbe8 : register the projectile smoke/exhaust particle definition
// (colour and velocity ranges differ for scene mode 0xe).
// ---------------------------------------------------------------------------
void proj_register_smoke_def(void)
{
    if (DAT_80612614 == 0xe) {
        particle_def_create((double)FLOAT_80616458, (double)FLOAT_80616360, &DAT_8058a83c, 0, 1, 1, 0x326, 0);
        particle_def_set_size_keyframes((double)FLOAT_80616360, (double)FLOAT_80616344, (double)FLOAT_80616344, (double)FLOAT_80616458, 0xffffffff, 0x202020ff, 0);
        particle_def_set_gravity((double)FLOAT_806163c8, 0x37256000);
        particle_def_set_size_start_end((double)FLOAT_80616390, (double)FLOAT_806164d8, (double)FLOAT_8061637c, (double)FLOAT_8061637c);
        particle_def_set_color_end((double)FLOAT_806164dc, (double)FLOAT_806164e0, (double)FLOAT_806164e4, (double)FLOAT_80616344);
        particle_def_set_velocity_ranges((double)FLOAT_806163a8, (double)FLOAT_80616378, (double)FLOAT_80616358, (double)FLOAT_8061637c, (double)FLOAT_80616360, (double)FLOAT_80616374);
        particle_def_set_drag((double)FLOAT_80616428);
        particle_emitter_create((double)FLOAT_80616374, (double)FLOAT_80616344, (double)FLOAT_8061637c, &DAT_8058a7b8, &DAT_8058a83c, 0, 0);
        particle_emitter_set_color_range((double)FLOAT_80616418, (double)FLOAT_80616344, (double)FLOAT_80616438, (double)FLOAT_80616344);
    } else {
        particle_def_create((double)FLOAT_8061637c, (double)FLOAT_80616360, &DAT_8058a83c, 0, 1, 1, 0x326, 0);
        particle_def_set_size_keyframes((double)FLOAT_80616360, (double)FLOAT_80616344, (double)FLOAT_80616344, (double)FLOAT_80616458, 0xffffffff, 0x202020ff, 0);
        particle_def_set_gravity((double)FLOAT_806163c8, 0x37256000);
        particle_def_set_size_start_end((double)FLOAT_80616390, (double)FLOAT_806164d8, (double)FLOAT_8061637c, (double)FLOAT_8061637c);
        particle_def_set_color_end((double)FLOAT_80616344, (double)FLOAT_806164e0, (double)FLOAT_806164e4, (double)FLOAT_80616344);
        particle_def_set_velocity_ranges((double)FLOAT_806163a8, (double)FLOAT_80616378, (double)FLOAT_80616458, (double)FLOAT_8061637c, (double)FLOAT_80616360, (double)FLOAT_806163ac);
        particle_def_set_drag((double)FLOAT_80616428);
        particle_emitter_create((double)FLOAT_80616414, (double)FLOAT_80616344, (double)FLOAT_8061637c, &DAT_8058a7b8, &DAT_8058a83c, 0, 0);
        particle_emitter_set_color_range((double)FLOAT_80616418, (double)FLOAT_80616344, (double)FLOAT_806164e8, (double)FLOAT_80616344);
    }
    particle_emitter_set_lifetime_range((double)FLOAT_80616418, (double)FLOAT_80616344);
}

// ---------------------------------------------------------------------------
// 0x8016bde8 : register the projectile trail particle definition.
// ---------------------------------------------------------------------------
void proj_register_trail_def(void)
{
    particle_def_create((double)FLOAT_80616358, (double)FLOAT_80616360, &DAT_8058a83c, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616360, (double)FLOAT_80616344, (double)FLOAT_80616344, (double)FLOAT_806163ac, 0xffa01000, 0x202020ff, 0x101010ff);
    particle_def_set_gravity((double)FLOAT_80616458, 0x7f);
    particle_def_set_size_start_end((double)FLOAT_80616390, (double)FLOAT_806164d8, (double)FLOAT_8061637c, (double)FLOAT_8061637c);
    particle_def_set_color_end((double)FLOAT_806164dc, (double)FLOAT_806164e0, (double)FLOAT_806164e4, (double)FLOAT_80616344);
    particle_def_set_velocity_ranges((double)FLOAT_806163a8, (double)FLOAT_80616378, (double)FLOAT_80616420, (double)FLOAT_8061637c, (double)FLOAT_80616360, (double)FLOAT_80616374);
    particle_def_set_drag((double)FLOAT_80616450);
    particle_emitter_create((double)FLOAT_80616414, (double)FLOAT_80616344, (double)FLOAT_8061637c, &DAT_8058a7b8, &DAT_8058a83c, 0, 0);
    particle_emitter_set_color_range((double)FLOAT_80616418, (double)FLOAT_80616344, (double)FLOAT_80616444, (double)FLOAT_80616344);
    particle_emitter_set_lifetime_range((double)FLOAT_80616418, (double)FLOAT_80616344);
}

// ---------------------------------------------------------------------------
// 0x8016abb0 : draw the projectile smoke-trail ribbons via the GX FIFO.
// For each active projectile (query 'FLYB'=0x464c5942), it builds a 3-segment
// billboard strip from the record's history points, clips each segment against
// the ground plane (proj_clip_segment_y), then emits 4 textured quads per pass
// (4 passes: outer glow / core / inner) writing position+colour to
// DAT_cc008000.  Structurally-faithful non-matching port: the many manual
// 7-float row copies collapse to loop-free vertex writes; the exact fixed-
// point colour packing is preserved via proj_fade_alpha_byte.
//   subsystem:graphics (writes the GX Command Processor FIFO at 0xCC008000)
// ---------------------------------------------------------------------------
void proj_ai_render_trails(void)
{
    int list[10];
    int n = FUN_8023c59c(0x464c5942, (int)list, 10);   // 'FLYB'
    FUN_801f47f0();
    FUN_801f844c();

    for (int i = 0; i < n; i++) {
        int e = list[i];
        int mode = *(int*)(*(int*)(e + 0x7c) + 0x34);
        double frames = game_time_to_frames();
        double scroll = (double)(float)((double)FLOAT_806164d0 *
            (double)(float)((double)(float)(FLOAT_80616360 / proj_frame_dt()) * -frames));

        unsigned int color = 0;
        if (mode == 4 || (*(unsigned int*)(*(int*)(e + 0x7c) + 0x38) & 8) == 0) continue;

        switch (mode) {
        case 0: case 3: color = 0x3f00; break;
        case 1: color = 0x3f1f0000; break;
        case 2: case 5: case 7: color = 0x7f000000; break;
        }

        float side[3] = { DAT_803ccca0, DAT_803ccca4, DAT_803ccca8 };
        float up[3]   = { DAT_803ccc94, DAT_803ccc98, DAT_803ccc9c };
        mtx4_transform_vector((float*)(e + 0x274), side);
        mtx4_transform_vector((float*)(e + 0x274), up);

        int tex = FUN_8016e8ac(2);

        // three ring vertices (base / mid / tip) with position + uv + alpha
        float base[7] = { *(float*)(e + 100), *(float*)(e + 0x68), *(float*)(e + 0x6c),
                          FLOAT_80616344, (float)((double)FLOAT_80616358 + scroll), 0, 0 };
        float mid[7]  = { FLOAT_806163e4 * up[0] + FLOAT_806163e4 * side[0] + *(float*)(e + 100),
                          FLOAT_806163e4 * up[1] + FLOAT_806163e4 * side[1] + *(float*)(e + 0x68),
                          FLOAT_806163e4 * up[2] + FLOAT_806163e4 * side[2] + *(float*)(e + 0x6c),
                          FLOAT_80616360, (float)scroll, 0, 0 };
        float tip[7]  = { FLOAT_806164d4 * up[0] + mid[0],
                          FLOAT_806164d4 * up[1] + mid[1],
                          FLOAT_806164d4 * up[2] + mid[2],
                          FLOAT_80616360, (float)((double)FLOAT_80616360 + scroll), 0, 0 };
        float clip[7] = {0};
        bool clipped = false;

        // clip mid/tip against the ground plane FLOAT_80616438
        if (FLOAT_80616438 <= mid[1] || FLOAT_80616438 <= tip[1]) {
            if (FLOAT_80616438 <= mid[1]) {
                if (tip[1] < FLOAT_80616438) { proj_clip_segment_y((double)FLOAT_80616438, clip, mid, tip); clipped = true; }
            } else {
                proj_clip_segment_y((double)FLOAT_80616438, clip, base, mid);
                proj_clip_segment_y((double)FLOAT_80616438, tip, base, mid);
                clipped = true;
            }
        }

        for (int pass = 0; pass < 4; pass++) {
            unsigned int a = 0, a2 = 0; int scaled;
            int t;
            if (pass == 1) {
                FUN_8016f00c(7);
                FUN_803cb4d4((double)FLOAT_80616360, (double)FLOAT_80616344);
                FUN_8016ee98();
                t = sprite_get_texture_id(0x333);
                a = 0; a2 = 0; scaled = 0;
            } else if (pass == 0) {
                float dx = *(float*)(DAT_80611d54 + 0x27c) - *(float*)(e + 100);
                float dy = *(float*)(DAT_80611d54 + 0x280) - *(float*)(e + 0x68);
                float dz = *(float*)(DAT_80611d54 + 0x284) - *(float*)(e + 0x6c);
                double dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
                FUN_803cb4d4((double)FLOAT_80616360, (double)(float)((double)FLOAT_80616350 / dist));
                FUN_8016f00c(0);
                FUN_8016ee98();
                a = 0x7f; a2 = 0x7f; scaled = 0;
                t = sprite_get_texture_id(0);
            } else if (pass == 2) {
                FUN_8016f00c(1);
                FUN_8016ee98();
                a = 0x2f; scaled = 1;
                t = sprite_get_texture_id(0x325);
            } else {
                a = 0xf; scaled = 1;
                t = sprite_get_texture_id(0);
            }

            FUN_80172a4c(2, 0x92, 0, 4 - (unsigned short)(!clipped), t);

            unsigned int c;
            c = proj_fade_alpha_byte((double)base[3], scaled, a, a, color);
            DAT_cc008000 = (float)((double)base[0] + 0.0);
            DAT_cc008000 = (float)((double)base[1] + 0.0);
            DAT_cc008000 = (float)((double)base[2] + 0.0);
            DAT_cc008000 = (float)c;
            DAT_cc008000 = base[3];
            DAT_cc008000 = base[4];
            c = proj_fade_alpha_byte((double)mid[3], scaled, a, a2, color);
            DAT_cc008000 = mid[0]; DAT_cc008000 = mid[1]; DAT_cc008000 = mid[2];
            DAT_cc008000 = (float)c; DAT_cc008000 = mid[3]; DAT_cc008000 = mid[4];
            c = proj_fade_alpha_byte((double)tip[3], scaled, a, a2, color);
            DAT_cc008000 = tip[0]; DAT_cc008000 = tip[1]; DAT_cc008000 = tip[2];
            DAT_cc008000 = (float)c; DAT_cc008000 = tip[3]; DAT_cc008000 = tip[4];
            if (clipped) {
                c = proj_fade_alpha_byte((double)clip[3], scaled, a, a2, color);
                DAT_cc008000 = clip[0] + FLOAT_80616344;
                DAT_cc008000 = clip[1] + FLOAT_80616344;
                DAT_cc008000 = clip[2] + FLOAT_80616344;
                DAT_cc008000 = (float)c; DAT_cc008000 = clip[5]; DAT_cc008000 = clip[6];
            }
            FUN_80172c54();
        }

        // final scorch decal projected to the ground
        double f0 = game_time_to_frames();
        int fp = (int)((double)(FLOAT_80616360 / proj_frame_dt()) * f0);
        double a0 = (double)(float)((double)(int)((unsigned int)fp ^ 0x80000000) - DOUBLE_80616380);
        double f1 = game_time_to_frames();
        double phase = (double)(float)((double)(FLOAT_80616360 / proj_frame_dt()) * f1 - a0);
        double f2 = game_time_to_frames();
        unsigned int par = (unsigned int)((double)(float)((double)FLOAT_80616360 / (double)proj_frame_dt()) * f2);
        if ((par & 1 ^ par >> 0x1f) != par >> 0x1f) phase = (double)(float)((double)FLOAT_80616360 - phase);

        float sway = (float)((double)FLOAT_806163e4 * (double)(FLOAT_80616370 * (float)(phase - (double)FLOAT_80616358)));
        double bx = (double)*(float*)(e + 100), by = (double)*(float*)(e + 0x68), bz = (double)*(float*)(e + 0x6c);
        double px = (double)(side[0] * sway + (float)((double)FLOAT_806163e4 * (double)up[0] + bx));
        double py = (double)(side[1] * sway + (float)((double)FLOAT_806163e4 * (double)up[1] + by));
        double pz = (double)(side[2] * sway + (float)((double)FLOAT_806163e4 * (double)up[2] + bz));
        if (py < (double)FLOAT_80616438 && py != by) {
            double t = (double)((float)(by - (double)FLOAT_80616438) / (float)(by - py));
            px = (double)(float)((double)(float)(px - bx) * t + bx);
            py = (double)(float)((double)(float)(py - by) * t + by);
            pz = (double)(float)((double)(float)(pz - bz) * t + bz);
        }
        FUN_8016fc04(bx, by, bz, px, py, pz);
        FUN_8016e8ac(tex);
    }
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// 0x8016b544 : draw projectile blips on the radar / HUD.  For each active
// projectile it maps the world position into radar space relative to the local
// player (DAT_80611d74), picks a colour by behaviour mode, and draws a filled
// marker (FUN_8016fee0) plus an outline box (FUN_80170410).  Holding the map
// button (bit 0x80) recentres the blips.
//   subsystem:graphics (HUD/radar draw)
// ---------------------------------------------------------------------------
void proj_ai_render_hud_markers(void)
{
    int radar = FUN_803826e4();
    int list[10];
    int n = FUN_8023c59c(0x464c5942, (int)list, 10);   // 'FLYB'

    for (int i = n - 1; i >= 0; i--) {
        int e = list[i];
        int mode = *(int*)(*(int*)(e + 0x7c) + 0x34);
        if (mode == 4) continue;

        int color = 0;
        switch (mode) {
        case 0: case 3: color = 0x3fff; break;
        case 1: color = 0x3f1f00ff; break;
        case 2: case 5: case 7: color = 0x7f0000ff; break;
        }

        float cx = (float)((double)(int)(*(unsigned int*)(radar + 0x10) >> 1) - DOUBLE_80616380);
        float cy = (float)((double)(int)(*(unsigned int*)(radar + 0x14) >> 1) - DOUBLE_80616380);
        float ox = (float)((double)(int)(*(unsigned int*)(radar + 8)  ^ 0x80000000) - DOUBLE_80616380);
        float oz = (float)((double)(int)(*(unsigned int*)(radar + 0xc) ^ 0x80000000) - DOUBLE_80616380);

        unsigned int phaseIdx = (unsigned int)(FLOAT_80616388 * (FLOAT_8061638c + *(float*)(*(int*)(e + 0x7c) + 0x2c)) + FLOAT_80616358);
        float sinP = *(float*)((int)&DAT_80583228 + ((phaseIdx + 0x200) * 4 & 0x1ffc));
        float cosP = ((float*)&DAT_80583228)[phaseIdx & 0x7ff];
        float sway = FLOAT_806163e4;

        float mx = cx + ox + sway * (*(float*)(DAT_80611d74 + 0xa8) - *(float*)(e + 100));
        float mz = cy + oz + sway * (*(float*)(DAT_80611d74 + 0xb0) - *(float*)(e + 0x6c));
        float bx = FLOAT_806163e4 * cosP;
        float bz = FLOAT_806163e4 * sinP;

        if ((input_get_buttons_pressed(0) & 0x80) != 0) { bx = FLOAT_80616344; bz = FLOAT_80616344; }

        FUN_8016fee0((int)mx, (int)mz, (int)(mx + bx), (int)(mz + bz), color);
        float pad = FLOAT_80616370;
        FUN_80170410((int)(mx - pad), (int)(mz - pad), (int)(pad + mx), (int)(pad + mz), 0, 0, 0x50, 0x50);
    }
}
