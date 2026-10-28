// worm_column.cpp
// CDH1 "LPTR" rising water/gas column set-piece + its scripted payload-gate
// callbacks. The column is a single shared control block (DAT_805c4ff0 owner
// entity + DAT_805c4ff4.. fill/velocity/position state) driven each frame from
// the parent rig; it spawns particle emitters, pushes the nearest player,
// arcs a jet, deals swept contact damage and fires cluster projectiles, then
// on drain destroys the 'FTWL' floodgate and bursts. The small gate callbacks
// fire a scripted payload (FUN_80221074) when a set of named actors is cleared.
// Address range 0x801e52f0 - 0x801e6878 (batch_617), continuation of the
// worm-effect / CDH1 cluster (worm_debris.cpp / worm_shutter.cpp siblings).
// subsystem:game, worm-effect module.
//
// Functions:
//   column_jitter_position_cb   (0x801e52f0) emitter position callback: copy the
//                               source vec3 and add an rng jitter to Y.
//   column_effect_init          (0x801e536c) build the column effect: seed the
//                               shared control block DAT_805c4ff4.., create the
//                               2 particle emitter defs (FUN_8037dc94/8037daf0),
//                               and alloc the screen-effect distortion slot.
//   column_water_jet_update     (0x801e5510) the per-frame column tick: advance
//                               the fill level, drive the emitter, push the
//                               nearest player up, arc the jet toward the rig,
//                               integrate the column position, spawn a splash
//                               particle + cluster projectile (alternating
//                               anchors) and follow the 3D loop sound.
//   column_impact_tick          (0x801e60a8) column collision/impact tick: apply
//                               a torque to the owner from the contact vector,
//                               decrement the fill timer, fire the scripted
//                               payload at thresholds, and on drain destroy the
//                               'FTWL' floodgate + spawn the burst emitters.
//   column_boss_fill_update     (0x801e63c8) 'BOSS'-actor gated fill ramp: while
//                               the boss is alive ramp the fill + emitter, mark
//                               the boss active, and on full destroy the column
//                               and arm the follow-up state.
//   column_jum_all_cleared_gate (0x801e66f0) payload gate: fire when JUM1/JUM2/
//                               JUM3/BJUM are all absent.
//   column_mac1_absent_gate     (0x801e6784) payload gate: fire when 'MAC1' absent.
//   column_gas0_gate            (0x801e67c0) payload gate: fire when 'GAS0' absent
//                               AND latched-closed, OR when GAS0 is in state 0x50
//                               AND not latched-closed.
//   column_oily_absent_gate     (0x801e683c) payload gate: fire when 'OILY' absent.
//   column_oilx_zone_gate       (0x801e6878) payload gate: fire when the primary
//                               player's current zone == 0x404 AND 'OILX' absent.
//
// Non-matching: FUN_8023c32c is entity-by-4CC lookup; aiactor_find_player_by_team
// resolves an actor by its 4CC team tag. The PPC FP-context spill frames
// (FUN_803826c4/d4/e8/f0 open + FUN_80382720/28/34/3c close) and the
// __save_gpr_28 frame are dead ABI plumbing kept as opaque open/close calls;
// caller-saved FP spill copies (in_fNN / in_psNN_1) are dropped. The int<->double
// magic-bias idiom (CONCAT44(0x43300000,x^0x80000000) - DOUBLE_80617420) is
// reduced to a plain -(double)(int) cast. Several named helpers whose shared
// prototype has a reduced/mis-ordered arity (sound_update_3d_source,
// player_ai_projectile_spawn_simple, FUN_8037dc94 particle-def create,
// FUN_8037daf0 emitter-def create) are routed through file-local typed fn-ptr
// aliases (__wc_*) with the true arity; math_atan2 and the FUN_ helpers are
// declared file-locally (external ROM symbols).
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local, typed to use here) ---
extern int   DAT_80611d78;      // CDH1 level-state block base
extern int   DAT_80611b8c;      // zone-id table base
extern char  DAT_805015bd;      // level id byte
extern int   DAT_80612610;      // active player-slot count
extern int   DAT_806126d4;      // shutter "closed" latch (shared w/ worm_shutter)
extern int   DAT_806126b8;      // column config word
extern int   DAT_80612704;      // column burst emitter A handle
extern unsigned int DAT_806109c0; // frame-count numerator (magic-bias src)
extern int   DAT_806108e4;      // elapsed-count (magic-bias src)
extern float FLOAT_806108ec;    // per-frame delta seconds

// column shared control block
extern int   DAT_805c4ff0;      // owner entity
extern float DAT_805c4ff4;      // position X
extern float DAT_805c4ff8;      // position Y (fill height)
extern float DAT_805c4ffc;      // position Z
extern float DAT_805c5000;      // velocity X
extern float DAT_805c5004;      // velocity Y
extern float DAT_805c5008;      // velocity Z
extern float DAT_805c5030;      // accel X
extern float DAT_805c5034;      // accel Y
extern float DAT_805c5038;      // accel Z
extern int   DAT_805c503c;      // column started flag
extern int   DAT_805c5044;      // column spray emitter cached ref
extern int   DAT_805c5048;      // alternating spawn-anchor toggle
extern float DAT_805c504c;      // spawn cooldown timer
extern int   DAT_805c5060;      // secondary emitter cached ref
extern int   DAT_805c5064;      // secondary emitter cached ref B
extern int   DAT_805c5068;      // secondary emitter cached ref C
extern int   DAT_805c506c;      // burst emitter A def
extern int   DAT_805c50f0;      // burst emitter B def

extern float FLOAT_80610d9c;    // column render field seed
extern float FLOAT_80610da0;    // column init velocity Y
extern float FLOAT_80610da4;    // column trigger height
extern float FLOAT_80610da8;    // spawn cooldown scatter
extern float FLOAT_80610dac;    // spawn cooldown base
extern float FLOAT_80610db0;    // jet arc X offset
extern float FLOAT_80610db4;    // jet arc dot scale
extern float FLOAT_80610db8;    // jet arc Z scale
extern float FLOAT_80610dbc;    // pitch spring gain
extern float FLOAT_80610dc0;    // yaw spring gain
extern float FLOAT_80610dc4;    // roll spring gain
extern float FLOAT_80610dc8;    // velocity damping (per elapsed)
extern float FLOAT_80610dcc;    // angular damping (per elapsed)
extern float FLOAT_80610dd0;    // pitch clamp
extern float FLOAT_80610dd4;    // yaw clamp band
extern float FLOAT_80610dd8;    // roll clamp
extern float FLOAT_80610ddc;    // impact torque gain A
extern float FLOAT_80610de0;    // impact torque gain B
extern float FLOAT_80610de4;    // impact torque gain C

extern float FLOAT_80617428;    // 1.0f numerator
extern float FLOAT_80617430;
extern float FLOAT_80617440;    // 0.0f
extern float FLOAT_80617448;    // rad<->deg factor
extern float FLOAT_8061742c;
extern float FLOAT_8061745c;
extern float FLOAT_8061746c;
extern float FLOAT_80617468;
extern float FLOAT_8061748c;
extern float FLOAT_8061749c;
extern float FLOAT_806174bc;
extern float FLOAT_806174c4;
extern float FLOAT_806174dc;
extern float FLOAT_80617504;
extern float FLOAT_8061750c;
extern float FLOAT_8061754c;
extern float FLOAT_80617554;
extern float FLOAT_80617560;
extern float FLOAT_80617574;
extern float FLOAT_806175e0;
extern float FLOAT_8061760c;    // Y jitter scale
extern float FLOAT_80617610;
extern float FLOAT_80617614;
extern float FLOAT_80617618;    // emitter forward push
extern float FLOAT_8061761c;    // jet base speed
extern float FLOAT_80617620;    // jet speed scatter
extern float FLOAT_80617624;    // size ceiling
extern float FLOAT_80617628;    // low-fill threshold
extern float FLOAT_8061762c;    // aim range scale
extern float FLOAT_80617630;    // ramp start
extern float FLOAT_80617634;    // ramp span
extern float FLOAT_80617638;    // steer dist^2 gate
extern float FLOAT_8061763c;    // impact fill threshold A
extern float FLOAT_80617640;    // boss fill init
extern double DOUBLE_80617420;  // magic-bias subtrahend
extern double DOUBLE_80617438;  // rng bias subtrahend

// rodata pivot/dir vec3 blocks (3 words each)
extern int   DAT_803df7a8;   extern int DAT_803df7ac;  extern int DAT_803df7b0;
extern int   DAT_803df7b4;   extern int DAT_803df7b8;  extern int DAT_803df7bc;
extern int   DAT_803df7c0;   extern int DAT_803df7c4;  extern int DAT_803df7c8;
extern int   DAT_803df7cc;   extern int DAT_803df7d0;  extern int DAT_803df7d4;
extern int   DAT_803df7d8;   extern int DAT_803df7dc;  extern int DAT_803df7e0;
extern int   DAT_803df7e4;   extern int DAT_803df7e8;  extern int DAT_803df7ec;
extern int   DAT_803df7f0;   extern int DAT_803df7f4;  extern int DAT_803df7f8;
extern int   DAT_803df7fc;   extern int DAT_803df800;  extern int DAT_803df804;
extern int   DAT_803df808;   extern int DAT_803df80c;  extern int DAT_803df810;
extern int   DAT_803df814;   extern int DAT_803df818;  extern int DAT_803df81c;
extern int   DAT_803df820;   extern int DAT_803df824;  extern int DAT_803df828;
extern int   DAT_803df82c;   extern int DAT_803df830;  extern int DAT_803df834;

// --- raw code symbols (file-local; not present in the generated headers) ---
extern int  *FUN_8023c32c(int tag);          // entity lookup by 4CC
extern int   FUN_8027ea98(void);             // primary local-player accessor
extern double math_atan2(double y, double x);
extern double FUN_80390324(double);          // periodic-reduce / interp helper
extern int   __save_gpr_28(void);            // gpr spill frame open (returns frame)
extern void  __restore_gpr_28(void);         // gpr spill frame close
extern int   FUN_803826c4(void);             // spill frame open (returns frame)
extern void  FUN_80382710(void);             // spill frame close
extern int   FUN_803826d4(void);             // spill frame open (returns frame)
extern void  FUN_80382720(void);             // spill frame close
extern int   FUN_803826e8(void);             // spill frame open (returns frame)
extern void  FUN_80382734(void);             // spill frame close
extern int   FUN_803826f0(void);             // spill frame open
extern void  FUN_8038273c(void);             // spill frame close
extern void  FUN_80221074(double a, double b, double c, double d,
                          double e, double f, double g, double h); // scripted payload
extern char  FUN_8037dc94(double a, double b, double c, int *pOut, int count,
                          void *pA, void *pB, int *pC, int flags, char chain,
                          int z);            // particle-def create (returns chain byte)
extern short FUN_8037daf0(unsigned long long chain, double b, double c, double d,
                          double e, double f, double g, double h, int def,
                          int one, int *pA, void *pB, int u0, int u1, int u2,
                          int u3);           // emitter-def create

} // extern "C" (raw symbols)

// --- typed fn-ptr aliases for shared-header prototypes with wrong arity ---
// sound_update_3d_source real form is (range,pitch,vol, handle_ptr, position,
// sample_id, flags) — the generated decl mis-orders sample_id/handle/position.
static void (*const __wc_sound3d)(double, double, double, int *, float *, int, int) =
    (void (*)(double, double, double, int *, float *, int, int))&sound_update_3d_source;
// player_ai_projectile_spawn_simple: real 12-arg form (accuracy,pad,def,pos,dir,
// owner,soundDef,mode,pad,damage,flags,ownerId).
static void (*const __wc_proj_spawn)(double, double, int, void *, int *, int, int *,
                                     int, int, int, int, int) =
    (void (*)(double, double, int, void *, int *, int, int *, int, int, int, int,
              int))&player_ai_projectile_spawn_simple;
// player_ai_spawn_trail_particle_attached first arg arrives as a double.
static void (*const __wc_trail_attached)(double, int, float *, float *, unsigned int) =
    (void (*)(double, int, float *, float *, unsigned int))
    &player_ai_spawn_trail_particle_attached;
// rng_counter_tick returns the fresh counter word.
static unsigned int (*const __wc_rng_tick)(void) =
    (unsigned int (*)(void))&rng_counter_tick;

// forward decls (payload cbs referenced from column_effect_init callee tables)
void column_water_jet_update(void);

// ---------------------------------------------------------------------------
// 0x801e52f0  Emitter position callback: copy source vec3 + rng jitter on Y.
void column_jitter_position_cb(int unused, float *pOut, float *pSrc)
{
    unsigned int r;
    (void)unused;
    pOut[0] = pSrc[0];
    pOut[1] = pSrc[1];
    pOut[2] = pSrc[2];
    r = __wc_rng_tick();
    pOut[2] = FLOAT_8061760c *
                  ((float)((double)r - DOUBLE_80617438) / FLOAT_8061742c - FLOAT_80617430) +
              pOut[2];
}

// ---------------------------------------------------------------------------
// 0x801e536c  Build the column effect: control block + emitters + screen slot.
void column_effect_init(void)
{
    int owner;
    char chainA;
    short chainB;
    int slotSpray;
    float defA[3];
    float posA[3];
    float defB[3];

    owner = FUN_803826f0();

    posA[0] = *(float *)&DAT_803df7a8;
    posA[1] = *(float *)&DAT_803df7ac;
    posA[2] = *(float *)&DAT_803df7b0;
    /* second/third rodata vec blocks feed the emitter def below */
    defA[0] = *(float *)&DAT_803df7c0;
    defA[1] = *(float *)&DAT_803df7c4;
    defA[2] = *(float *)&DAT_803df7c8;

    memset_wrapper((int)&DAT_805c4ff4, 0, 0x70);
    DAT_805c5004 = FLOAT_80617440;
    DAT_805c4ff4 = defA[0] - FLOAT_806174dc;
    DAT_805c503c = 0;
    DAT_805c4ff8 = FLOAT_80617610;
    DAT_805c4ffc = FLOAT_80617440;
    DAT_805c4ff0 = owner;
    *(float *)(owner + 0x70) = FLOAT_806175e0;
    *(float *)(owner + 0xc4) = FLOAT_806175e0;
    DAT_805c5000 = FLOAT_80610da0;
    *(float *)(owner + 0xc0) = *(float *)&DAT_806126b8;

    chainA = FUN_8037dc94((double)FLOAT_80617428, (double)FLOAT_80617614,
                          (double)FLOAT_80617614, &DAT_805c5044, 0x20,
                          &DAT_805c4ff4, posA, (int *)0, 0x1000, 0, 0);
    slotSpray = DAT_805c5044;
    particle_cached_set_lifetime(FLOAT_80617560, (ParticleEmitter **)&slotSpray);
    chainB = FUN_8037daf0((unsigned long long)(unsigned int)chainA,
                          (double)FLOAT_80617614, (double)FLOAT_80617614,
                          (double)FLOAT_80610da0, (double)FLOAT_80617440, 0.0, 0.0,
                          0.0, (int)&DAT_805c4ff4 /*def carrier*/, 1,
                          (int *)&DAT_805c4ff4, posA, 0, 0x1000, 0, 0);

    FUN_8037dc94((double)FLOAT_80617468, (double)FLOAT_80617504,
                 (double)FLOAT_80617504, &DAT_805c5060, 7, defA, defB, (int *)0,
                 0x1000, (char)chainB, 0);

    *(float *)(owner + 0x1d8) = FLOAT_80610d9c;
    *(float *)(owner + 0x1cc) = FLOAT_80610d9c;
    if (*(int *)(owner + 0x268) == 0) {
        int es = (int)screen_effect_alloc_slot();
        *(int *)(owner + 0x268) = es;
        screen_effect_init_slot((int *)es, (int *)owner);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801e5510  Per-frame water/gas column update.
void column_water_jet_update(void)
{
    float fv1, fv2, fv3, fv4, fv5;
    int entity;
    int rig;
    int iter;
    int playerSlot;
    unsigned int r;
    double dNorm;
    float srcPos[3];
    float srcVel[3];
    float trailPos[3];
    float dropDir[3];
    float dropPos[3];
    float mtx[16];
    float rotMtx[16];
    int cached;
    int soundParams[7];
    int soundId;
    int soundEnable;
    float soundPx, soundPy, soundPz;

    entity = __save_gpr_28();
    playerSlot = 0;
    dropDir[0] = *(float *)&DAT_803df7cc;
    dropDir[1] = *(float *)&DAT_803df7d0;
    dropDir[2] = *(float *)&DAT_803df7d4;

    /* co-op: pick the higher of the two players' fill columns */
    if (DAT_805015bd == '\n' && 1 < DAT_80612610 &&
        *(float *)(DAT_80611d78 + 0x14c4) < *(float *)(DAT_80611d78 + 0xa8)) {
        playerSlot = 1;
    }

    if (DAT_805c503c == 0) {
        if (FLOAT_80610da4 < *(float *)(DAT_80611d78 + playerSlot * 0x141c + 0xa8)) {
            FUN_80382720();  /* NOTE: ROM restores gpr28 (same close) */
            return;
        }
        DAT_805c503c = 1;
        *(int *)(DAT_80611d78 + 0xe8c) = entity;
        FUN_80221074((double)FLOAT_80617440, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    }

    entity_build_local_matrix(mtx, (void *)(unsigned long long)(unsigned)entity, 0, 0);
    rig = *(int *)(*(int *)(entity + 4) + 0x10);
    srcPos[0] = *(float *)(rig + 0x4f0);
    srcPos[1] = *(float *)(rig + 0x4f4);
    srcPos[2] = *(float *)(rig + 0x4f8);
    mtx4_transform_vector(mtx, srcPos);
    srcPos[0] = srcPos[0] + DAT_805c4ff4;
    srcPos[1] = srcPos[1] + DAT_805c4ff8;
    srcPos[2] = srcPos[2] + DAT_805c4ffc;
    mtx4_transform_vector(mtx, dropDir);
    srcPos[0] = FLOAT_80617618 * dropDir[0] + srcPos[0];
    srcPos[1] = FLOAT_80617618 * dropDir[1] + srcPos[1];
    srcPos[2] = FLOAT_80617618 * dropDir[2] + srcPos[2];

    cached = DAT_805c5044;
    particle_cached_set_position((ParticleEmitter **)&cached, (Vec3 *)srcPos);
    cached = DAT_805c5044;
    particle_cached_set_velocity((ParticleEmitter **)&cached, (Vec3 *)dropDir);

    r = __wc_rng_tick();
    dNorm = (double)(FLOAT_8061761c +
                     (FLOAT_80617620 * ((float)((double)r - DOUBLE_80617438) / FLOAT_8061742c) -
                      FLOAT_80617504));
    if (*(float *)(entity + 0x1cc) < FLOAT_80617440) {
        dNorm = (double)(float)(dNorm * (double)FLOAT_806174bc);
        cached = DAT_805c5044;
        particle_cached_set_size_range(FLOAT_80617624, FLOAT_806174bc,
                                       (ParticleEmitter **)&cached);
    }
    dNorm = -dNorm;
    DAT_805c5030 = (float)((double)dropDir[0] * dNorm);
    DAT_805c5034 = (float)((double)dropDir[1] * dNorm);
    DAT_805c5038 = (float)((double)dropDir[2] * dNorm);

    if (DAT_805c4ff8 <= FLOAT_806174c4 || *(float *)(entity + 0x1cc) <= FLOAT_80617440) {
        if (DAT_805c4ff8 < FLOAT_80617628) {
            DAT_805c5030 = DAT_805c5030 * FLOAT_80617504;
            DAT_805c5034 = DAT_805c5034 * FLOAT_80617504;
            DAT_805c5038 = DAT_805c5038 * FLOAT_80617504;
        }
    } else {
        DAT_805c5034 = DAT_805c5034 * FLOAT_80617430;
    }

    /* steer the jet toward the tracked player's position */
    {
        int pl = DAT_80611d78 + playerSlot * 0x141c;
        float px = *(float *)(pl + 0xb4);
        double dz = (double)(FLOAT_806174c4 - DAT_805c4ff8);
        double dx = (double)((px - FLOAT_80610db0) - DAT_805c4ff4);
        double dc = (double)(FLOAT_80617440 - DAT_805c4ffc);
        /* dot the world delta against rig-local matrix rows (mtx row2 / row0) */
        double pitchTarget = (double)(FLOAT_80610db4 *
            (float)(dc * (double)mtx[10] +
                    (double)(float)(dx * (double)mtx[8] +
                                    (double)(float)(dz * (double)mtx[9]))));
        double a = (double)math_atan2((double)(px - DAT_805c4ff4),
                                      (double)(*(float *)(pl + 0xbc) - DAT_805c4ffc));
        double hi = (double)(FLOAT_806175e0 + FLOAT_80610dd4);
        double aim = (double)((FLOAT_8061762c * (float)a) / FLOAT_80617448);
        double lo = (double)(FLOAT_806175e0 - FLOAT_80610dd4);
        double yawTarget;
        double rollTarget;
        double s;

        double clampedAim = aim;
        if (hi < aim) clampedAim = hi;
        if (lo <= clampedAim) {
            yawTarget = aim;
            if (hi < aim) yawTarget = hi;
        } else {
            yawTarget = lo;
        }
        pitchTarget = (double)(float)((double)*(float *)(entity + 0xc0) - pitchTarget);
        rollTarget = (double)(-FLOAT_80610db8 *
            (float)(dc * (double)mtx[2] +
                    (double)(float)(dx * (double)mtx[0] +
                                    (double)(float)(dz * (double)mtx[1]))));

        s = FUN_80390324((double)((float)(pitchTarget - (double)FLOAT_80617630) / FLOAT_80617634));
        *(float *)(entity + 0xe0) =
            -((FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0)) * FLOAT_806108ec *
              FLOAT_80610dbc * -(float)((double)FLOAT_80617634 * (double)(float)s - pitchTarget) -
              *(float *)(entity + 0xe0));

        {
            double d13 = (double)(float)((double)*(float *)(entity + 0x70) - yawTarget);
            double s2 = FUN_80390324((double)(float)((double)(float)(d13 - (double)FLOAT_80617630) /
                                                     (double)FLOAT_80617634));
            *(float *)(entity + 0xe4) =
                -((FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0)) * FLOAT_806108ec *
                  FLOAT_80610dc0 *
                  -(float)((double)FLOAT_80617634 * (double)(float)s2 - d13) -
                  *(float *)(entity + 0xe4));
        }
        {
            double d15 = (double)(float)((double)*(float *)(entity + 200) - rollTarget);
            double s3 = FUN_80390324((double)(float)((double)(float)(d15 - (double)FLOAT_80617630) /
                                                     (double)FLOAT_80617634));
            *(float *)(entity + 0xe8) =
                -((FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0)) * FLOAT_806108ec *
                  FLOAT_80610dc4 *
                  -(float)((double)FLOAT_80617634 * (double)(float)s3 - d15) -
                  *(float *)(entity + 0xe8));
        }
    }

    /* velocity damping over elapsed frames */
    {
        int n = DAT_806108e4;
        if (0 < n) {
            do {
                DAT_805c5000 = DAT_805c5000 * FLOAT_80610dc8;
                DAT_805c5004 = DAT_805c5004 * FLOAT_80610dc8;
                DAT_805c5008 = DAT_805c5008 * FLOAT_80610dc8;
                n = n + -1;
            } while (n != 0);
        }
    }
    {
        float invFrame = FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0);
        float dtScaled = invFrame * FLOAT_806108ec;
        DAT_805c5000 = DAT_805c5030 * dtScaled + DAT_805c5000;
        DAT_805c5008 = DAT_805c5038 * dtScaled + DAT_805c5008;
        DAT_805c5004 = -(FLOAT_8061761c * FLOAT_806108ec * invFrame -
                         (DAT_805c5034 * dtScaled + DAT_805c5004));
        DAT_805c4ff4 = DAT_805c5000 * dtScaled + DAT_805c4ff4;
        DAT_805c4ffc = DAT_805c5008 * dtScaled + DAT_805c4ffc;
        DAT_805c4ff8 = DAT_805c5004 * dtScaled + DAT_805c4ff8;
    }
    *(float *)(entity + 100) = DAT_805c4ff4;
    *(float *)(entity + 0x68) = DAT_805c4ff8;
    *(float *)(entity + 0x6c) = DAT_805c4ffc;

    /* angular damping */
    for (iter = 0; iter < DAT_806108e4; iter = iter + 1) {
        *(float *)(entity + 0xe0) = *(float *)(entity + 0xe0) * FLOAT_80610dcc;
        *(float *)(entity + 0xe4) = *(float *)(entity + 0xe4) * FLOAT_80610dcc;
        *(float *)(entity + 0xe8) = *(float *)(entity + 0xe8) * FLOAT_80610dcc;
    }

    {
        float invFrame = FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0);
        float f1, f2, f3;
        *(float *)(entity + 0xc0) =
            invFrame * *(float *)(entity + 0xe0) * FLOAT_806108ec + *(float *)(entity + 0xc0);
        f1 = *(float *)(entity + 0xc0);
        f3 = f1;
        if (FLOAT_80610dd0 < f1) f3 = FLOAT_80610dd0;
        f2 = -FLOAT_80610dd0;
        if (f2 <= f3) { f2 = f1; if (FLOAT_80610dd0 < f1) f2 = FLOAT_80610dd0; }
        *(float *)(entity + 0xc0) = f2;

        f1 = invFrame * *(float *)(entity + 0xe4) * FLOAT_806108ec + *(float *)(entity + 0x70);
        *(float *)(entity + 0x70) = f1;
        *(float *)(entity + 0xc4) = f1;
        *(float *)(entity + 200) =
            invFrame * *(float *)(entity + 0xe8) * FLOAT_806108ec + *(float *)(entity + 200);
        f1 = *(float *)(entity + 200);
        f3 = f1;
        if (FLOAT_80610dd8 < f1) f3 = FLOAT_80610dd8;
        f2 = -FLOAT_80610dd8;
        if (f2 <= f3) { f2 = f1; if (FLOAT_80610dd8 < f1) f2 = FLOAT_80610dd8; }
        *(float *)(entity + 200) = f2;

        DAT_805c504c = -(invFrame * FLOAT_806108ec - DAT_805c504c);
    }

    /* spawn a splash particle + cluster projectile (alternating anchor) */
    if (DAT_805c504c < FLOAT_80617440 && FLOAT_80617440 <= *(float *)(entity + 0x1cc)) {
        int pl = DAT_80611d78 + playerSlot * 0x141c;
        dropDir[0] = *(float *)&DAT_803df7d8;
        dropDir[1] = *(float *)&DAT_803df7dc;
        dropDir[2] = *(float *)&DAT_803df7e0;
        rig = *(int *)(*(int *)(entity + 4) + 0x10);
        srcVel[0] = *(float *)(rig + 0x430);
        srcVel[1] = *(float *)(rig + 0x434);
        srcVel[2] = *(float *)(rig + 0x438);
        mtx4_transform_vector(mtx, srcVel);
        srcVel[0] = srcVel[0] + DAT_805c4ff4;
        srcVel[1] = srcVel[1] + DAT_805c4ff8;
        srcVel[2] = srcVel[2] + DAT_805c4ffc;
        rig = *(int *)(*(int *)(entity + 4) + 0x10);
        trailPos[0] = *(float *)(rig + 0x330);
        trailPos[1] = *(float *)(rig + 0x334);
        trailPos[2] = *(float *)(rig + 0x338);
        mtx4_transform_vector(mtx, trailPos);
        trailPos[0] = trailPos[0] + DAT_805c4ff4;
        trailPos[1] = trailPos[1] + DAT_805c4ff8;
        trailPos[2] = trailPos[2] + DAT_805c4ffc;
        mtx4_set_rotation_zyx((double)FLOAT_80617440,
                              (double)((FLOAT_80617448 * *(float *)(entity + 0x70)) / FLOAT_8061762c),
                              (double)((FLOAT_80617448 * *(float *)(entity + 200)) / FLOAT_8061762c),
                              rotMtx);
        mtx4_transform_vector(rotMtx, dropDir);
        {
            float dxg = (*(float *)(pl + 0xb4) - srcVel[0]) - FLOAT_8061754c;
            float dyg = *(float *)(pl + 0xb8) - srcVel[1];
            float dzg = *(float *)(pl + 0xbc) - srcVel[2];
            float mag2 = dzg * dzg + dyg * dyg + dxg * dxg;
            if (FLOAT_80617638 < mag2) {
                float inv = 1.0f / (float)SQRT(mag2);
                inv = inv * -(FLOAT_80617430 * mag2 * inv * inv - FLOAT_8061749c);
                float g = FLOAT_80617428 * inv * -(FLOAT_80617430 * mag2 * inv * inv - FLOAT_8061749c);
                dxg = dxg * g; dyg = dyg * g; dzg = dzg * g;
            }
            if (FLOAT_8061745c < dropDir[2] * dzg + dropDir[0] * dyg + dropDir[1] * dxg) {
                dropDir[0] = dyg; dropDir[1] = dxg; dropDir[2] = dzg;
            }
        }
        if (DAT_805c5048 == 0) {
            __wc_trail_attached((double)FLOAT_80617468, 2, srcVel, dropDir,
                                *(unsigned int *)(entity + 0x2c));
            srcVel[0] = FLOAT_8061748c * dropDir[0] + srcVel[0];
            srcVel[1] = FLOAT_8061748c * dropDir[1] + srcVel[1];
            srcVel[2] = FLOAT_8061748c * dropDir[2] + srcVel[2];
            __wc_proj_spawn((double)FLOAT_80617554, (double)FLOAT_80617440, 0x35,
                            srcVel, (int *)dropDir, entity, (int *)0, 6, 0,
                            *(int *)(entity + 0x34), 0, 2);
        } else {
            __wc_trail_attached((double)FLOAT_80617468, 2, trailPos, dropDir,
                                *(unsigned int *)(entity + 0x2c));
            trailPos[0] = FLOAT_8061748c * dropDir[0] + trailPos[0];
            trailPos[1] = FLOAT_8061748c * dropDir[1] + trailPos[1];
            trailPos[2] = FLOAT_8061748c * dropDir[2] + trailPos[2];
            __wc_proj_spawn((double)FLOAT_80617554, (double)FLOAT_80617440, 0x35,
                            trailPos, (int *)dropDir, entity, (int *)0, 6, 0,
                            *(int *)(entity + 0x34), 0, 2);
        }
        sound_init_play_params((int)soundParams);
        soundEnable = 1;
        soundId = 0x393;
        soundParams[0] = soundId;
        soundParams[3] = *(int *)(entity + 100);
        soundParams[4] = *(int *)(entity + 0x68);
        soundParams[5] = *(int *)(entity + 0x6c);
        (void)soundEnable;
        sound_play_request(soundParams);
        DAT_805c5048 = 1 - DAT_805c5048;
        r = __wc_rng_tick();
        DAT_805c504c = DAT_805c504c +
                       FLOAT_80610da8 * ((float)((double)r - DOUBLE_80617438) / FLOAT_8061742c) +
                       FLOAT_80610dac;
    }

    {
        float *worldPos = entity_get_world_position(entity);
        soundPx = worldPos[0]; soundPy = worldPos[1]; soundPz = worldPos[2];
        (void)soundPx; (void)soundPy; (void)soundPz;
        __wc_sound3d((double)FLOAT_8061746c, (double)FLOAT_8061750c,
                     (double)FLOAT_80617428, (int *)(entity + 0x22c), worldPos, 0x394, 0);
    }

    FUN_80382720();  /* __restore_gpr_28 */
    (void)fv1; (void)fv2; (void)fv3; (void)fv4; (void)fv5; (void)dropPos;
}

// ---------------------------------------------------------------------------
// 0x801e60a8  Column collision/impact tick: torque + fill drain + burst.
void column_impact_tick(void)
{
    int frame;
    int owner;
    float *pContact;
    float *pNormal;
    float local_38[3], local_44[3], local_50[3], local_5c[3], local_68[3], local_74[3];

    frame = FUN_803826d4();
    owner = DAT_805c4ff0;
    if (owner == 0) { FUN_80382720(); return; }

    if ((double)FLOAT_80617440 <= (double)*(float *)(owner + 0x1cc)) {
        pNormal = *(float **)(frame + 0x30);
        pContact = *(float **)(frame + 0x28);
        if (pNormal != 0 && pContact != 0 && DAT_805c503c == 1) {
            double cz = (double)(pContact[2] - *(float *)(owner + 0x6c));
            double cy = (double)(pContact[1] - *(float *)(owner + 0x68));
            double cx = (double)(pContact[0] - *(float *)(owner + 100));
            float nx = pNormal[0], ny = pNormal[1], nz = pNormal[2];
            float appliedForce = *(float *)(frame + 0x20);

            *(float *)(owner + 0xe0) =
                (float)(cy * (double)nz - (double)(float)(cz * (double)ny)) *
                    FLOAT_80610de0 * FLOAT_80610ddc * appliedForce +
                *(float *)(owner + 0xe0);
            *(float *)(owner + 0xe4) =
                (float)((double)(float)(cz * (double)nx - (double)(float)(cx * (double)nz)) *
                        (double)(FLOAT_80610ddc * appliedForce) +
                        (double)*(float *)(owner + 0xe4));
            *(float *)(owner + 0xe8) =
                (float)((double)(float)((double)(float)(cx * (double)ny -
                                                        (double)(float)(cy * (double)nx)) *
                                        (double)FLOAT_80610de4) *
                        (double)(float)((double)FLOAT_80610ddc * (double)appliedForce) +
                        (double)*(float *)(owner + 0xe8));

            *(float *)(owner + 0x1cc) =
                (float)((double)*(float *)(owner + 0x1cc) - (double)appliedForce);

            if ((double)*(float *)(owner + 0x1cc) <= (double)FLOAT_8061763c) {
                FUN_80221074((double)FLOAT_80617440, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
            }
            if (*(float *)(owner + 0x1cc) <= FLOAT_80617574) {
                FUN_80221074((double)FLOAT_80617440, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
            }
            if (*(float *)(owner + 0x1cc) < FLOAT_80617440) {
                int *ftwl;
                int c60, c64, c68, c84, c88;
                local_38[0] = *(float *)&DAT_803df7e4; local_38[1] = *(float *)&DAT_803df7e8; local_38[2] = *(float *)&DAT_803df7ec;
                local_44[0] = *(float *)&DAT_803df7f0; local_44[1] = *(float *)&DAT_803df7f4; local_44[2] = *(float *)&DAT_803df7f8;
                local_50[0] = *(float *)&DAT_803df7fc; local_50[1] = *(float *)&DAT_803df800; local_50[2] = *(float *)&DAT_803df804;
                local_5c[0] = *(float *)&DAT_803df808; local_5c[1] = *(float *)&DAT_803df80c; local_5c[2] = *(float *)&DAT_803df810;
                local_68[0] = *(float *)&DAT_803df814; local_68[1] = *(float *)&DAT_803df818; local_68[2] = *(float *)&DAT_803df81c;
                local_74[0] = *(float *)&DAT_803df820; local_74[1] = *(float *)&DAT_803df824; local_74[2] = *(float *)&DAT_803df828;

                ftwl = FUN_8023c32c(0x4654574c);  /* 'FTWL' floodgate */
                if (ftwl != 0) entity_destroy_with_event(ftwl);

                c60 = DAT_805c5060; particle_cached_set_drag(FLOAT_80617440, (ParticleEmitter **)&c60);
                c64 = DAT_805c5064; particle_cached_set_drag(FLOAT_80617440, (ParticleEmitter **)&c64);
                c68 = DAT_805c5068; particle_cached_set_drag(FLOAT_80617440, (ParticleEmitter **)&c68);
                *(int *)(DAT_80611d78 + 0xe8c) = 0;
                FUN_80221074((double)FLOAT_80617440, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

                particle_emitter_spawn((unsigned int)(unsigned long long)&DAT_80612704,
                                       (unsigned int)(unsigned long long)&DAT_805c506c, 0x1000,
                                       local_38, local_74);
                c84 = DAT_80612704; particle_cached_set_target((ParticleEmitter **)&c84, (Vec3 *)local_68);
                c88 = DAT_80612704; particle_cached_enable((ParticleEmitter **)&c88);
                particle_emitter_spawn(0, (unsigned int)(unsigned long long)&DAT_805c50f0, 0, local_44, local_74);
                particle_emitter_set_target((Vec3 *)local_68);
                particle_emitter_spawn(0, (unsigned int)(unsigned long long)&DAT_805c50f0, 0, local_50, local_74);
                particle_emitter_set_target((Vec3 *)local_68);
                particle_emitter_spawn(0, (unsigned int)(unsigned long long)&DAT_805c50f0, 0, local_5c, local_74);
                particle_emitter_set_target((Vec3 *)local_68);
            }
        }
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// 0x801e63c8  'BOSS'-actor gated fill ramp + destroy-on-full.
void column_boss_fill_update(void)
{
    int *self;
    int boss;
    int rig;
    bool valid;
    unsigned int roomFlags;
    float pos[3];
    float vel[3];
    int cached;

    self = (int *)FUN_803826e8();
    boss = aiactor_find_player_by_team(0x424f5353);  /* 'BOSS' */
    if (boss == 0) {
        self[10] = self[10] & 0xfffffffe;
    } else {
        pos[0] = *(float *)&DAT_803df82c;
        pos[1] = *(float *)&DAT_803df830;
        pos[2] = *(float *)&DAT_803df834;
        self[0x2e] = (int)(float)((float)(-(double)(int)DAT_806108e4) *
                                  (FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0)) +
                                  *(float *)&self[0x2e]);
        DAT_805c4ff8 = (FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0)) *
                           *(float *)&self[0x2e] * (float)(-(double)(int)DAT_806108e4) +
                       DAT_805c4ff8;
        rig = *(int *)(self[1] + 0x10);
        vel[0] = *(float *)(rig + 0x4f0);
        vel[1] = *(float *)(rig + 0x4f4);
        vel[2] = *(float *)(rig + 0x4f8);
        DAT_805c4ff0 = (int)self;
        mtx4_transform_vector((float *)(self + 0x9d), vel);
        vel[0] = vel[0] + DAT_805c4ff4;
        vel[1] = vel[1] + DAT_805c4ff8;
        vel[2] = vel[2] + DAT_805c4ffc;
        mtx4_transform_vector((float *)(*(int *)(self[1] + 0x10) + 0x4c0), pos);
        cached = DAT_805c5044;
        valid = particle_handle_is_valid(&cached);
        if (!valid) {
            FUN_8037dc94((double)FLOAT_80617428, (double)FLOAT_80617614,
                         (double)FLOAT_80617614, &DAT_805c5044, 0x20, vel, pos, (int *)0,
                         0x1000, 0, 0);
            cached = DAT_805c5044;
            particle_cached_set_lifetime(FLOAT_80617560, (ParticleEmitter **)&cached);
        }
        cached = DAT_805c5044; particle_cached_enable((ParticleEmitter **)&cached);
        cached = DAT_805c5044; particle_cached_set_position((ParticleEmitter **)&cached, (Vec3 *)vel);
        cached = DAT_805c5044; particle_cached_set_velocity((ParticleEmitter **)&cached, (Vec3 *)pos);
        self[0x19] = (int)DAT_805c4ff4;
        self[0x1a] = (int)DAT_805c4ff8;
        self[0x1b] = (int)DAT_805c4ffc;
        *(float *)&self[0x30] = FLOAT_80617440;
        *(float *)&self[0x1c] = FLOAT_80617640;
        *(float *)&self[0x31] = FLOAT_80617640;
        *(float *)&self[0x32] = FLOAT_80617440;
        *(unsigned int *)(boss + 0x28) = *(unsigned int *)(boss + 0x28) | 1;
        *(int *)(boss + 0x34) = 0xd5;
        self[10] = self[10] | 1;
        if (FLOAT_80617624 < DAT_805c4ff8) {
            entity_destroy_with_event(self);
            cached = DAT_805c5044; particle_handle_destroy(&cached);
            DAT_805c4ff0 = 0;
            boss = *(int *)(boss + 0x7c);
            if (*(int *)(boss + 0xaac) == 1 && *(int *)(boss + 0xaa8) == 0) {
                if (DAT_805015bd == '\n') roomFlags = *(unsigned int *)(boss + 0x14);
                else roomFlags = *(unsigned int *)(boss + 0x1c);
                if ((roomFlags & 1) == 0) { FUN_80382734(); return; }
            }
            *(int *)(boss + 0xaac) = 0x400000;
            *(int *)(boss + 0xaa8) = 0;
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x801e66f0  Payload gate: fire when JUM1/JUM2/JUM3/BJUM are all absent.
void column_jum_all_cleared_gate(unsigned long long f, double a2, double a3, double a4,
                                 double a5, double a6, double a7, double a8)
{
    (void)f;
    FUN_803826f0();
    if (aiactor_find_player_by_team(0x4a554d31) == 0 &&
        aiactor_find_player_by_team(0x4a554d32) == 0 &&
        aiactor_find_player_by_team(0x4a554d33) == 0 &&
        aiactor_find_player_by_team(0x424a554d) == 0) {
        FUN_80221074((double)FLOAT_806174dc, a2, a3, a4, a5, a6, a7, a8);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801e6784  Payload gate: fire when 'MAC1' is absent.
void column_mac1_absent_gate(unsigned long long f, double a2, double a3, double a4,
                             double a5, double a6, double a7, double a8)
{
    (void)f;
    if (aiactor_find_player_by_team(0x4d414331) == 0) {
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
    }
}

// ---------------------------------------------------------------------------
// 0x801e67c0  Payload gate: 'GAS0' absent+closed, or GAS0 state 0x50 + open.
void column_gas0_gate(unsigned long long f, double a2, double a3, double a4,
                      double a5, double a6, double a7, double a8)
{
    int gas0;
    (void)f;
    gas0 = aiactor_find_player_by_team(0x47415330);  /* 'GAS0' */
    if (gas0 == 0 && DAT_806126d4 != 0) {
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
    }
    if (gas0 != 0 && *(int *)(gas0 + 0x34) == 0x50 && DAT_806126d4 == 0) {
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
    }
}

// ---------------------------------------------------------------------------
// 0x801e683c  Payload gate: fire when 'OILY' is absent.
void column_oily_absent_gate(unsigned long long f, double a2, double a3, double a4,
                             double a5, double a6, double a7, double a8)
{
    (void)f;
    if (aiactor_find_player_by_team(0x4f494c59) == 0) {
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
    }
}

// ---------------------------------------------------------------------------
// 0x801e6878  Payload gate: primary player zone == 0x404 AND 'OILX' absent.
void column_oilx_zone_gate(unsigned long long f, double a2, double a3, double a4,
                           double a5, double a6, double a7, double a8)
{
    int player;
    int oilx;
    int zone;
    (void)f;
    player = FUN_8027ea98();
    oilx = aiactor_find_player_by_team(0x4f494c58);  /* 'OILX' */
    zone = -1;
    if (player != 0) {
        zone = *(int *)(DAT_80611b8c + *(int *)(player + 0x678) * 0x20);
    }
    if (zone == 0x404 && oilx == 0) {
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
    }
}
